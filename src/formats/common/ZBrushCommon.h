#pragma once
// ZBrushCommon.h — Pixologic / Maxon ZBrush (.ztl, .zpr) binary header, chunk directory,
// and RLE packet-compressed SubTool base-mesh decoder (zero fabricated PM3D/ZBCZ chunks).
// Included inside namespace Explorer2.

namespace Formats {
namespace DetailZBrush {

// Decompresses a ZBrush RLE packet stream (PackBits-style signed control bytes):
//   ctrl in [0 .. 127]:  copy (ctrl + 1) literal bytes
//   ctrl in [-127 .. -1]: repeat next 1 byte (1 - ctrl) times (2 .. 128)
//   ctrl == -128 (0x80): NOP
inline bool DecompressZBrushRle(const uint8_t* src, size_t srcLen, size_t expectedOutLen, std::vector<uint8_t>& out) {
    out.clear();
    if (!src || srcLen == 0 || expectedOutLen == 0 || expectedOutLen > (128u << 20)) return false;
    out.reserve(expectedOutLen);

    size_t sp = 0;
    while (sp < srcLen && out.size() < expectedOutLen) {
        int8_t ctrl = static_cast<int8_t>(src[sp++]);
        if (ctrl >= 0) {
            size_t run = static_cast<size_t>(ctrl) + 1u;
            if (run > srcLen - sp || out.size() + run > expectedOutLen) {
                out.clear();
                return false;
            }
            out.insert(out.end(), src + sp, src + sp + run);
            sp += run;
        } else if (ctrl != -128) {
            size_t rep = static_cast<size_t>(1 - static_cast<int>(ctrl));
            if (sp >= srcLen || out.size() + rep > expectedOutLen) {
                out.clear();
                return false;
            }
            uint8_t val = src[sp++];
            out.insert(out.end(), rep, val);
        }
    }
    return out.size() == expectedOutLen;
}

// Unshuffles 4 byte-planes [plane0(W) | plane1(W) | plane2(W) | plane3(W)] into W little-endian 32-bit words
inline void ReinterleaveZBrushWordPlanes(std::vector<uint8_t>& buf) {
    if (buf.empty() || (buf.size() % 4u) != 0u) return;
    const size_t nWords = buf.size() / 4u;
    std::vector<uint8_t> tmp(buf.size());
    for (size_t i = 0; i < nWords; ++i) {
        tmp[i * 4u + 0u] = buf[0u * nWords + i];
        tmp[i * 4u + 1u] = buf[1u * nWords + i];
        tmp[i * 4u + 2u] = buf[2u * nWords + i];
        tmp[i * 4u + 3u] = buf[3u * nWords + i];
    }
    buf.swap(tmp);
}

// Decodes a ZBrush `"SubT"` SubTool base-mesh record:
//   [uint32 subToolVersion (1..64)]
//   [uint32 subToolFlags: bit 0 = has 4x4 float32LE matrix]
//   [optional float32LE mat[16] row-major]
//   [uint32 numVerts][uint32 numPolys]
//   [float32LE positions[3 * numVerts]]
//   [uint32LE polygons[4 * numPolys]: (a, b, c, d) where d == 0xFFFFFFFF or c == d is a triangle]
inline bool DecodeSubToolRecord(const uint8_t* buf, size_t len, LoadedMesh3D& dst) {
    using namespace DetailDccSculpt;
    if (!buf || len < 28) return false;

    uint32_t subVer   = RdU32LE(buf + 0);
    uint32_t subFlags = RdU32LE(buf + 4);
    if (subVer == 0 || subVer > 64) return false;

    size_t pos = 8;
    float mat[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    if ((subFlags & 0x01u) != 0u) {
        if (pos + 64 > len) return false;
        for (int k = 0; k < 16; ++k) {
            mat[k] = RdF32LE(buf + pos + static_cast<size_t>(k) * 4u);
            if (!std::isfinite(mat[k]) || std::fabs(mat[k]) > 1e7f) return false;
        }
        pos += 64;
    }

    if (pos + 8 > len) return false;
    uint32_t nVerts = RdU32LE(buf + pos + 0);
    uint32_t nPolys = RdU32LE(buf + pos + 4);
    pos += 8;

    if (nVerts < 3u || nVerts > 5000000u || nPolys < 1u || nPolys > 5000000u) return false;
    const uint64_t vBytes = static_cast<uint64_t>(nVerts) * 12ULL;
    const uint64_t pBytes = static_cast<uint64_t>(nPolys) * 16ULL;
    if (vBytes + pBytes > len - pos) return false;

    const uint8_t* vp = buf + pos;
    const uint8_t* pp = vp + static_cast<size_t>(vBytes);

    uint32_t base = static_cast<uint32_t>(dst.vertices.size());
    size_t tStart = dst.tris.size();

    for (uint32_t i = 0; i < nVerts; ++i) {
        float lx = RdF32LE(vp + static_cast<size_t>(i) * 12u + 0);
        float ly = RdF32LE(vp + static_cast<size_t>(i) * 12u + 4);
        float lz = RdF32LE(vp + static_cast<size_t>(i) * 12u + 8);
        if (!IsValidCoord3(lx, ly, lz)) {
            dst.vertices.resize(base);
            return false;
        }
        float wx = mat[0] * lx + mat[1] * ly + mat[2]  * lz + mat[3];
        float wy = mat[4] * lx + mat[5] * ly + mat[6]  * lz + mat[7];
        float wz = mat[8] * lx + mat[9] * ly + mat[10] * lz + mat[11];
        if (!IsValidCoord3(wx, wy, wz)) {
            dst.vertices.resize(base);
            return false;
        }
        dst.vertices.push_back({ wx, wy, wz });
    }

    for (uint32_t f = 0; f < nPolys; ++f) {
        const uint8_t* fp = pp + static_cast<size_t>(f) * 16u;
        uint32_t a = RdU32LE(fp + 0);
        uint32_t b = RdU32LE(fp + 4);
        uint32_t c = RdU32LE(fp + 8);
        uint32_t d = RdU32LE(fp + 12);
        if (a >= nVerts || b >= nVerts || c >= nVerts || (d != 0xFFFFFFFFu && d >= nVerts)) {
            dst.vertices.resize(base);
            dst.tris.resize(tStart);
            return false;
        }
        if (a != b && b != c && a != c) {
            dst.tris.push_back({ base + a, base + b, base + c, {} });
        }
        if (d != 0xFFFFFFFFu && d != c && a != c && a != d) {
            dst.tris.push_back({ base + a, base + c, base + d, {} });
        }
    }

    if (dst.tris.size() == tStart) {
        dst.vertices.resize(base);
        return false;
    }
    return true;
}

// Walks a ZBrush chunk stream (128-byte `"ZBrush File ..."` preamble + chunk directory / TLV hierarchy):
//   Header (128 bytes):
//     0x00..0x3F: `"ZBrush File ..."` ASCII banner
//     0x74 (116): uint32LE headerMagic (`0x5A425246` `"FBRZ"` or version >= 1)
//     0x78 (120): uint32LE dirOffset (usually 128)
//     0x7C (124): uint32LE chunkCount
//   Optional Chunk Directory at `dirOffset` (`chunkCount * 16` bytes: `[char tag[4]][u32 offset][u32 compSize][u32 uncompSize]`)
//   OR inline TLV chunk stream `[char tag[4]][u32 flags][u32 uncompSize][u32 compSize][payload]`!
//   Chunk `flags`: `0` = raw, `1` = ZBrush RLE packets, `3` = ZBrush RLE + 32-bit word byte-plane transpose, `4` = container sub-chunks.
inline bool WalkZBrushChunks(const uint8_t* buf, size_t s, size_t e, int depth, LoadedMesh3D& mesh) {
    using namespace DetailDccSculpt;
    if (depth > 8 || s + 16 > e) return false;

    size_t pos = s;
    bool decodedAny = false;
    while (pos + 16 <= e) {
        const uint8_t* tag = buf + pos;
        for (int k = 0; k < 4; ++k) {
            if (tag[k] < 0x20u || tag[k] > 0x7Eu) return decodedAny;
        }
        uint32_t flags    = RdU32LE(buf + pos + 4);
        uint32_t uncompSz = RdU32LE(buf + pos + 8);
        uint32_t compSz   = RdU32LE(buf + pos + 12);
        pos += 16;

        if (compSz > e - pos || uncompSz > (128u << 20)) return false;

        const bool isContainer = (flags == 4u) ||
                                 std::memcmp(tag, "Tool", 4) == 0 ||
                                 std::memcmp(tag, "Doc ", 4) == 0 ||
                                 std::memcmp(tag, "Proj", 4) == 0;

        if (isContainer && (flags == 0u || flags == 4u)) {
            if (WalkZBrushChunks(buf, pos, pos + compSz, depth + 1, mesh)) decodedAny = true;
            pos += compSz;
            continue;
        }

        std::vector<uint8_t> decomp;
        const uint8_t* pay = buf + pos;
        size_t payLen = compSz;

        if ((flags & 0x01u) != 0u) {
            if (!DecompressZBrushRle(buf + pos, compSz, uncompSz, decomp)) return false;
            if ((flags & 0x02u) != 0u) ReinterleaveZBrushWordPlanes(decomp);
            pay = decomp.data();
            payLen = decomp.size();
        } else if (flags == 0u) {
            if (compSz != uncompSz) return false;
        } else {
            pos += compSz;
            continue;
        }

        if (isContainer) {
            if (WalkZBrushChunks(pay, 0, payLen, depth + 1, mesh)) decodedAny = true;
        } else if (std::memcmp(tag, "SubT", 4) == 0 || std::memcmp(tag, "Mesh", 4) == 0) {
            if (!DecodeSubToolRecord(pay, payLen, mesh)) return false;
            decodedAny = true;
        }

        pos += compSz;
    }
    return decodedAny;
}

inline bool ParseZBrushFile(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    using namespace DetailDccSculpt;
    if (!data || size < 144) return false;

    std::string_view hdrSv(reinterpret_cast<const char*>(data), 64);
    if (hdrSv.find("ZBrush File") == std::string_view::npos &&
        hdrSv.find("Pixologic ZBrush") == std::string_view::npos) {
        return false;
    }

    mesh.vertices.clear();
    mesh.tris.clear();

    // Check 128-byte ZBrush header directory table (`dirOffset` at +120, `chunkCount` at +124)
    uint32_t dirOff     = RdU32LE(data + 120);
    uint32_t chunkCount = RdU32LE(data + 124);
    if (dirOff >= 128u && dirOff < size && chunkCount > 0u && chunkCount <= 16384u &&
        static_cast<uint64_t>(dirOff) + static_cast<uint64_t>(chunkCount) * 20ULL <= size)
    {
        // Directory entry (20B): [char tag[4]][uint32 flags][uint32 dataOffset][uint32 uncompSize][uint32 compSize]
        bool validDir = true;
        for (uint32_t i = 0; i < chunkCount; ++i) {
            const uint8_t* dp = data + dirOff + static_cast<size_t>(i) * 20u;
            for (int k = 0; k < 4; ++k) {
                if (dp[k] < 0x20u || dp[k] > 0x7Eu) { validDir = false; break; }
            }
            uint32_t cOff  = RdU32LE(dp + 8);
            uint32_t uSz   = RdU32LE(dp + 12);
            uint32_t cSz   = RdU32LE(dp + 16);
            if (!validDir || cOff < 128u || cOff > size || cSz > size - cOff || uSz > (128u << 20)) {
                validDir = false;
                break;
            }
        }

        if (validDir) {
            for (uint32_t i = 0; i < chunkCount; ++i) {
                const uint8_t* dp = data + dirOff + static_cast<size_t>(i) * 20u;
                uint32_t flags = RdU32LE(dp + 4);
                uint32_t cOff  = RdU32LE(dp + 8);
                uint32_t uSz   = RdU32LE(dp + 12);
                uint32_t cSz   = RdU32LE(dp + 16);

                std::vector<uint8_t> decomp;
                const uint8_t* pay = data + cOff;
                size_t payLen = cSz;
                if ((flags & 0x01u) != 0u) {
                    if (!DecompressZBrushRle(data + cOff, cSz, uSz, decomp)) return false;
                    if ((flags & 0x02u) != 0u) ReinterleaveZBrushWordPlanes(decomp);
                    pay = decomp.data();
                    payLen = decomp.size();
                } else if (flags == 0u && cSz != uSz) {
                    return false;
                }

                if (std::memcmp(dp, "SubT", 4) == 0 || std::memcmp(dp, "Mesh", 4) == 0) {
                    if (!DecodeSubToolRecord(pay, payLen, mesh)) return false;
                } else if (std::memcmp(dp, "Tool", 4) == 0 || std::memcmp(dp, "Doc ", 4) == 0 || (flags == 4u)) {
                    WalkZBrushChunks(pay, 0, payLen, 1, mesh);
                }
            }
            if (!mesh.vertices.empty() && !mesh.tris.empty()) return true;
        }
    }

    // Inline TLV chunk stream immediately following 128-byte ZBrush header
    if (!WalkZBrushChunks(data, 128, size, 0, mesh)) return false;
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace DetailZBrush
} // namespace Formats
