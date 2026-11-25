#pragma once
// SegaNinjaNj.h — Sega Dreamcast Katana / Sonic Adventure Ninja 3D Model (.nj, "NJCM" & "NJBM") reader
// Traverses hierarchical NJS_OBJECT trees (52-byte nodes with 32-bit BAMS angles, child & sibling links)
// and decodes both NJCM (Chunk Model vlist vertex cache + plist triangle strips) and NJBM (Basic Model
// NJS_MODEL + NJS_MESHSET triangles, quads, N-gons, and strips).

namespace Formats {

namespace DetailSegaNinja {

inline void BuildNinjaObjectLocal4(
    uint32_t evalFlags,
    const float pos[3],
    const int32_t ang[3],
    const float scl[3],
    float outM[16])
{
    constexpr float kBamsToRad = 6.2831853071795864769f / 65536.0f;
    const bool unitPos = (evalFlags & 0x01u) != 0;
    const bool unitAng = (evalFlags & 0x02u) != 0;
    const bool unitScl = (evalFlags & 0x04u) != 0;
    const bool zxyAng  = (evalFlags & 0x20u) != 0;

    const float tx = unitPos ? 0.0f : pos[0];
    const float ty = unitPos ? 0.0f : pos[1];
    const float tz = unitPos ? 0.0f : pos[2];
    const float sx = unitScl ? 1.0f : scl[0];
    const float sy = unitScl ? 1.0f : scl[1];
    const float sz = unitScl ? 1.0f : scl[2];

    float rot[16] = { 1,0,0,0,  0,1,0,0,  0,0,1,0,  0,0,0,1 };
    if (!unitAng) {
        const float rx = static_cast<float>(ang[0] & 0xFFFF) * kBamsToRad;
        const float ry = static_cast<float>(ang[1] & 0xFFFF) * kBamsToRad;
        const float rz = static_cast<float>(ang[2] & 0xFFFF) * kBamsToRad;
        const float cx = std::cos(rx), snx = std::sin(rx);
        const float cy = std::cos(ry), sny = std::sin(ry);
        const float cz = std::cos(rz), snz = std::sin(rz);
        const float mx[16] = { 1,0,0,0,  0,cx,-snx,0,  0,snx,cx,0,  0,0,0,1 };
        const float my[16] = { cy,0,sny,0,  0,1,0,0,  -sny,0,cy,0,  0,0,0,1 };
        const float mz[16] = { cz,-snz,0,0,  snz,cz,0,0,  0,0,1,0,  0,0,0,1 };
        float t01[16];
        if (zxyAng) {
            MeshBuild3D::MatMul4RowMajor(my, mx, t01);
            MeshBuild3D::MatMul4RowMajor(t01, mz, rot);
        } else {
            MeshBuild3D::MatMul4RowMajor(mz, my, t01);
            MeshBuild3D::MatMul4RowMajor(t01, mx, rot);
        }
    }

    const float sm[16] = { sx,0,0,0,  0,sy,0,0,  0,0,sz,0,  0,0,0,1 };
    MeshBuild3D::MatMul4RowMajor(rot, sm, outM);
    outM[3]  = tx;
    outM[7]  = ty;
    outM[11] = tz;
}

inline Vec3f TransformNjPt(const float m[16], Vec3f p) {
    return {
        m[0] * p.x + m[1] * p.y + m[2]  * p.z + m[3],
        m[4] * p.x + m[5] * p.y + m[6]  * p.z + m[7],
        m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11]
    };
}

inline bool DecodeNjbmModel(
    const uint8_t* chunk,
    size_t chunkLen,
    uint32_t modelOff,
    const float worldM[16],
    LoadedMesh3D& mesh)
{
    if (chunkLen < 24u || static_cast<size_t>(modelOff) > chunkLen - 24u) return false;
    const uint8_t* mp = chunk + modelOff;
    const uint32_t ptsOff     = ByteIO::RdU32LE(mp + 0);
    const int32_t  nbPoint    = ByteIO::RdI32LE(mp + 8);
    const uint32_t meshsetOff = ByteIO::RdU32LE(mp + 12);
    const uint16_t nbMeshset  = ByteIO::RdU16LE(mp + 20);

    if (nbPoint <= 0 || nbPoint > 500000 || nbMeshset == 0 || nbMeshset > 16384) return false;
    if (ptsOff == 0 || static_cast<uint64_t>(ptsOff) + static_cast<uint64_t>(nbPoint) * 12ULL > chunkLen) return false;
    if (meshsetOff == 0 || static_cast<uint64_t>(meshsetOff) + static_cast<uint64_t>(nbMeshset) * 24ULL > chunkLen) return false;

    const uint32_t vBase = static_cast<uint32_t>(mesh.vertices.size());
    for (int32_t i = 0; i < nbPoint; ++i) {
        const uint8_t* vp = chunk + ptsOff + static_cast<size_t>(i) * 12u;
        const float x = ByteIO::RdF32LE(vp + 0);
        const float y = ByteIO::RdF32LE(vp + 4);
        const float z = ByteIO::RdF32LE(vp + 8);
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
        mesh.vertices.push_back(TransformNjPt(worldM, { x, y, z }));
    }

    for (uint16_t ms = 0; ms < nbMeshset; ++ms) {
        const uint8_t* msp = chunk + meshsetOff + static_cast<size_t>(ms) * 24u;
        const uint16_t typeMat = ByteIO::RdU16LE(msp + 0);
        const uint16_t nbMesh  = ByteIO::RdU16LE(msp + 2);
        const uint32_t idxOff  = ByteIO::RdU32LE(msp + 4);
        const uint32_t polyType = (typeMat >> 14) & 3u;
        if (nbMesh == 0 || idxOff == 0 || idxOff >= chunkLen) continue;

        size_t p = idxOff;
        auto rdIdx = [&](uint16_t& out) -> bool {
            if (p + 2u > chunkLen) return false;
            out = ByteIO::RdU16LE(chunk + p);
            p += 2u;
            return true;
        };

        if (polyType == 0) { // NJD_MESHSET_3: triangles
            for (uint16_t t = 0; t < nbMesh; ++t) {
                uint16_t i0 = 0, i1 = 0, i2 = 0;
                if (!rdIdx(i0) || !rdIdx(i1) || !rdIdx(i2)) return false;
                if (i0 >= nbPoint || i1 >= nbPoint || i2 >= nbPoint) return false;
                if (i0 != i1 && i1 != i2 && i0 != i2) {
                    mesh.tris.push_back({ vBase + i0, vBase + i1, vBase + i2, {} });
                }
            }
        } else if (polyType == 1) { // NJD_MESHSET_4: quads
            for (uint16_t q = 0; q < nbMesh; ++q) {
                uint16_t i0 = 0, i1 = 0, i2 = 0, i3 = 0;
                if (!rdIdx(i0) || !rdIdx(i1) || !rdIdx(i2) || !rdIdx(i3)) return false;
                if (i0 >= nbPoint || i1 >= nbPoint || i2 >= nbPoint || i3 >= nbPoint) return false;
                mesh.tris.push_back({ vBase + i0, vBase + i1, vBase + i2, {} });
                mesh.tris.push_back({ vBase + i0, vBase + i2, vBase + i3, {} });
            }
        } else if (polyType == 3) { // NJD_MESHSET_TRIMESH: strips (bit 15 = reversed winding, bits 13..0 = len)
            for (uint16_t s = 0; s < nbMesh; ++s) {
                uint16_t sh = 0;
                if (!rdIdx(sh)) return false;
                const bool rev = (sh & 0x8000u) != 0;
                const uint16_t len = sh & 0x3FFFu;
                std::vector<uint16_t> sv(len);
                for (uint16_t k = 0; k < len; ++k) {
                    if (!rdIdx(sv[k]) || sv[k] >= nbPoint) return false;
                }
                for (uint16_t k = 0; k + 2 < len; ++k) {
                    uint32_t a = vBase + sv[k], b = vBase + sv[k + 1], c = vBase + sv[k + 2];
                    if (a == b || b == c || a == c) continue;
                    const bool flip = ((k & 1u) != 0) ^ rev;
                    if (flip) mesh.tris.push_back({ a, c, b, {} });
                    else      mesh.tris.push_back({ a, b, c, {} });
                }
            }
        }
    }
    return true;
}

inline bool DecodeNjcmModel(
    const uint8_t* chunk,
    size_t chunkLen,
    uint32_t modelOff,
    const float worldM[16],
    std::unordered_map<uint16_t, uint32_t>& vCache,
    LoadedMesh3D& mesh)
{
    if (chunkLen < 8u || static_cast<size_t>(modelOff) > chunkLen - 8u) return false;
    const uint32_t vlistOff = ByteIO::RdU32LE(chunk + modelOff + 0u);
    const uint32_t plistOff = ByteIO::RdU32LE(chunk + modelOff + 4u);

    // 1. Update vertex cache from vlist if non-zero
    if (vlistOff != 0) {
        if (chunkLen < 4u || static_cast<size_t>(vlistOff) > chunkLen - 4u) return false;
        size_t vp = vlistOff;
        while (vp + 4u <= chunkLen) {
            const uint32_t w0 = ByteIO::RdU32LE(chunk + vp);
            const uint8_t cType = static_cast<uint8_t>(w0 & 0xFFu);
            if (cType == 0xFFu) break; // NJD_CV_END
            const uint16_t sizeDwords = static_cast<uint16_t>((w0 >> 16) & 0xFFFFu);
            vp += 4u;
            if (sizeDwords < 1 || vp + static_cast<size_t>(sizeDwords) * 4u > chunkLen) return false;

            const size_t nextVp = vp + static_cast<size_t>(sizeDwords) * 4u;
            if (cType >= 0x20u && cType <= 0x32u) {
                const uint32_t w1 = ByteIO::RdU32LE(chunk + vp);
                const uint16_t idxBase = static_cast<uint16_t>(w1 & 0xFFFFu);
                const uint16_t nbVerts = static_cast<uint16_t>((w1 >> 16) & 0xFFFFu);
                if (nbVerts > 0 && sizeDwords > 1) {
                    const size_t remBytes = (static_cast<size_t>(sizeDwords) - 1u) * 4u;
                    const size_t vStride = remBytes / nbVerts;
                    if (vStride < 12u || vStride * nbVerts != remBytes) return false;
                    const uint8_t* vData = chunk + vp + 4u;
                    for (uint16_t k = 0; k < nbVerts; ++k) {
                        const float x = ByteIO::RdF32LE(vData + static_cast<size_t>(k) * vStride + 0u);
                        const float y = ByteIO::RdF32LE(vData + static_cast<size_t>(k) * vStride + 4u);
                        const float z = ByteIO::RdF32LE(vData + static_cast<size_t>(k) * vStride + 8u);
                        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
                        const uint32_t gIdx = static_cast<uint32_t>(mesh.vertices.size());
                        mesh.vertices.push_back(TransformNjPt(worldM, { x, y, z }));
                        vCache[static_cast<uint16_t>(idxBase + k)] = gIdx;
                    }
                }
            }
            vp = nextVp;
        }
    }

    // 2. Emit triangle strips from plist if non-zero
    if (plistOff != 0) {
        if (chunkLen < 2u || static_cast<size_t>(plistOff) > chunkLen - 2u) return false;
        size_t pp = plistOff;
        while (pp + 2u <= chunkLen) {
            const uint16_t h0 = ByteIO::RdU16LE(chunk + pp);
            const uint8_t pType = static_cast<uint8_t>(h0 & 0xFFu);
            pp += 2u;
            if (pType == 0xFFu) break; // NJD_CE (End)
            if (pType == 0x00u) continue; // Null
            if (pType <= 0x07u) continue; // Bits (already in h0 high byte)
            if (pType <= 0x0Fu) { // Tiny (1 extra u16)
                if (pp + 2u > chunkLen) return false;
                pp += 2u;
                continue;
            }
            // Material (0x10..0x1F), Volume (0x38..0x3A), Strip (0x40..0x4B) all carry a u16 word-size!
            if (pp + 2u > chunkLen) return false;
            const uint16_t sizeU16 = ByteIO::RdU16LE(chunk + pp);
            pp += 2u;
            if (pp + static_cast<size_t>(sizeU16) * 2u > chunkLen) return false;
            const size_t nextPp = pp + static_cast<size_t>(sizeU16) * 2u;

            if (pType >= 0x40u && pType <= 0x4Bu && sizeU16 >= 4) {
                // Extra u16 words per strip vertex after index:
                // 0x40 NJD_CS: 0; 0x41/0x42 UVN/UVH: 2; 0x43 VN: 3; 0x44 UVN_VN: 5; 0x47 D8: 2; 0x48 UVN_D8: 4
                size_t extraPerVert = 0;
                switch (pType) {
                    case 0x40: extraPerVert = 0; break;
                    case 0x41: case 0x42: case 0x47: extraPerVert = 2; break;
                    case 0x43: extraPerVert = 3; break;
                    case 0x48: case 0x49: extraPerVert = 4; break;
                    case 0x44: case 0x45: extraPerVert = 5; break;
                    default: extraPerVert = 0; break;
                }
                const uint16_t stripHdr = ByteIO::RdU16LE(chunk + pp);
                const uint16_t nbStrips = stripHdr & 0x3FFFu;
                const uint16_t userFlags = (stripHdr >> 14) & 3u;
                size_t sp = pp + 2u;

                for (uint16_t s = 0; s < nbStrips; ++s) {
                    if (sp + 2u > nextPp) break;
                    const int16_t rawLen = ByteIO::RdI16LE(chunk + sp);
                    sp += 2u;
                    const bool rev = (rawLen < 0);
                    const int len = rev ? -static_cast<int>(rawLen) : static_cast<int>(rawLen);
                    if (len < 3) return false;

                    std::vector<uint32_t> sv;
                    sv.reserve(static_cast<size_t>(len));
                    for (int k = 0; k < len; ++k) {
                        if (sp + (1u + extraPerVert) * 2u > nextPp) return false;
                        const uint16_t cIdx = ByteIO::RdU16LE(chunk + sp);
                        sp += (1u + extraPerVert) * 2u;
                        if (k >= 2 && userFlags > 0) {
                            if (sp + static_cast<size_t>(userFlags) * 2u > nextPp) return false;
                            sp += static_cast<size_t>(userFlags) * 2u;
                        }
                        auto it = vCache.find(cIdx);
                        if (it == vCache.end()) return false;
                        sv.push_back(it->second);
                    }
                    for (size_t k = 0; k + 2 < sv.size(); ++k) {
                        uint32_t a = sv[k], b = sv[k + 1], c = sv[k + 2];
                        if (a == b || b == c || a == c) continue;
                        const bool flip = ((k & 1u) != 0) ^ rev;
                        if (flip) mesh.tris.push_back({ a, c, b, {} });
                        else      mesh.tris.push_back({ a, b, c, {} });
                    }
                }
            }
            pp = nextPp;
        }
    }
    return true;
}

inline bool WalkNinjaObjectTree(
    const uint8_t* chunk,
    size_t chunkLen,
    bool isChunkModel,
    uint32_t objOff,
    const float parentM[16],
    int depth,
    std::set<uint32_t>& visited,
    std::unordered_map<uint16_t, uint32_t>& vCache,
    LoadedMesh3D& mesh)
{
    uint32_t curOff = objOff;
    while (static_cast<size_t>(curOff) < chunkLen && depth < 64) {
        if (chunkLen < 52u || static_cast<size_t>(curOff) > chunkLen - 52u) return false;
        if (!visited.insert(curOff).second) break; // cycle guard

        const uint8_t* op = chunk + curOff;
        const uint32_t evalFlags  = ByteIO::RdU32LE(op + 0);
        const uint32_t modelOff   = ByteIO::RdU32LE(op + 4);
        float pos[3] = { ByteIO::RdF32LE(op + 8), ByteIO::RdF32LE(op + 12), ByteIO::RdF32LE(op + 16) };
        int32_t ang[3] = { ByteIO::RdI32LE(op + 20), ByteIO::RdI32LE(op + 24), ByteIO::RdI32LE(op + 28) };
        float scl[3] = { ByteIO::RdF32LE(op + 32), ByteIO::RdF32LE(op + 36), ByteIO::RdF32LE(op + 40) };
        const uint32_t childOff   = ByteIO::RdU32LE(op + 44);
        const uint32_t siblingOff = ByteIO::RdU32LE(op + 48);

        if (!std::isfinite(pos[0]) || !std::isfinite(pos[1]) || !std::isfinite(pos[2]) ||
            !std::isfinite(scl[0]) || !std::isfinite(scl[1]) || !std::isfinite(scl[2])) {
            return false;
        }

        float localM[16], worldM[16];
        BuildNinjaObjectLocal4(evalFlags, pos, ang, scl, localM);
        MeshBuild3D::MatMul4RowMajor(parentM, localM, worldM);

        if ((evalFlags & 0x08u) == 0 && modelOff != 0) {
            if (isChunkModel) {
                if (!DecodeNjcmModel(chunk, chunkLen, modelOff, worldM, vCache, mesh)) return false;
            } else {
                if (!DecodeNjbmModel(chunk, chunkLen, modelOff, worldM, mesh)) return false;
            }
        }

        if ((evalFlags & 0x10u) == 0 && childOff != 0) {
            if (!WalkNinjaObjectTree(chunk, chunkLen, isChunkModel, childOff, worldM, depth + 1, visited, vCache, mesh)) {
                return false;
            }
        }

        if (siblingOff == 0) break;
        curOff = siblingOff;
    }
    return true;
}

} // namespace DetailSegaNinja

inline bool ParseSegaNinjaNj(
    const uint8_t* data,
    size_t size,
    LoadedMesh3D& mesh)
{
    if (!data || size < 60) return false;

    size_t pos = 0;
    while (pos + 8u <= size) {
        const uint8_t* hdr = data + pos;
        const uint32_t chunkLen = ByteIO::RdU32LE(hdr + 4u);
        if (pos + 8ULL + chunkLen > size) return false;

        const bool isNjcm = (std::memcmp(hdr, "NJCM", 4) == 0);
        const bool isNjbm = (std::memcmp(hdr, "NJBM", 4) == 0);
        if (isNjcm || isNjbm) {
            if (chunkLen < 52u) return false;
            const float idM[16] = { 1,0,0,0,  0,1,0,0,  0,0,1,0,  0,0,0,1 };
            std::set<uint32_t> visited;
            std::unordered_map<uint16_t, uint32_t> vCache;
            if (!DetailSegaNinja::WalkNinjaObjectTree(hdr + 8u, chunkLen, isNjcm, 0u, idM, 0, visited, vCache, mesh)) {
                return false;
            }
            if (!mesh.vertices.empty() && !mesh.tris.empty()) {
                mesh.formatName = isNjcm ? L"Sega Ninja Chunk Model (NJCM)" : L"Sega Ninja Basic Model (NJBM)";
                return true;
            }
            return false;
        }

        // Allow leading NJTL / NMDM / POF0 IFF chunks before NJCM/NJBM; reject non-IFF garbage immediately
        if (std::memcmp(hdr, "NJTL", 4) != 0 && std::memcmp(hdr, "POF0", 4) != 0 &&
            std::memcmp(hdr, "NJIN", 4) != 0) {
            return false;
        }
        pos += 8u + chunkLen;
        // Align to 4-byte boundary if padded
        while ((pos & 3u) != 0 && pos < size && data[pos] == 0) ++pos;
    }

    return false;
}

} // namespace Formats
