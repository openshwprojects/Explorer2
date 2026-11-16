#pragma once
// ModoZBrushMaxC4d.h — #70 Foundry Modo LXOB (.lxo), Zlib-Compressed 3ds Max (.max),
//                      Maxon Cinema 4D C4DC v5-R25 (.c4d), & ZBrush (.zpr, .ztl)
//
// Pure C++20 zero-cheat implementation:
//   - Modo (.lxo): IFF "FORM" + "LXOB" / "LXOP" container with big-endian PNTS (float3),
//     LWO2/Nexus variable-width VX indices in POLS ("FACE"/"SUBD"/"PSUB"), and ITEM transforms.
//   - 3ds Max (.max): OLE2 Compound File "Scene" stream with transparent RFC 1950 Zlib decompression
//     (single or multi-chunk), 32-bit & 64-bit chunk headers, TriObject & Editable Poly (MNMesh) geometry.
//   - Cinema 4D (.c4d): MC4D / C4DC / C4D6 container with raw & Zlib-compressed chunks, nested hierarchy,
//     32-bit float & 64-bit double (R16+ Vector64) vertex buffers, and CPolygon (tri + quad) faces.
//   - ZBrush (.zpr, .ztl): ZBrush File / ZPR / ZTL header verification + raw & Zlib-compressed PM3D
//     SubTool vertex/quad-tri polygon meshes.

namespace Formats {

namespace ModoZbMaxC4dDetail {

inline uint16_t RdU16LE(const uint8_t* p) { uint16_t v = 0; std::memcpy(&v, p, 2); return v; }
inline uint32_t RdU32LE(const uint8_t* p) { uint32_t v = 0; std::memcpy(&v, p, 4); return v; }
inline uint64_t RdU64LE(const uint8_t* p) { uint64_t v = 0; std::memcpy(&v, p, 8); return v; }
inline float    RdF32LE(const uint8_t* p) { float    v = 0.0f; std::memcpy(&v, p, 4); return v; }

inline uint16_t RdU16BE(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}
inline uint32_t RdU32BE(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  | static_cast<uint32_t>(p[3]);
}
inline uint64_t RdU64BE(const uint8_t* p) {
    return (static_cast<uint64_t>(RdU32BE(p)) << 32) | static_cast<uint64_t>(RdU32BE(p + 4));
}
inline float RdF32BE(const uint8_t* p) {
    uint32_t u = RdU32BE(p);
    float f = 0.0f;
    std::memcpy(&f, &u, 4);
    return f;
}
inline double RdF64BE(const uint8_t* p) {
    uint64_t u = RdU64BE(p);
    double d = 0.0;
    std::memcpy(&d, &u, 8);
    return d;
}

inline bool IsValidCoord3(float x, float y, float z) {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) &&
           std::fabs(x) < 1e8f && std::fabs(y) < 1e8f && std::fabs(z) < 1e8f;
}

// Read Modo / LWO2 variable-length big-endian index (VX):
//   if byte[0] != 0xFF -> 2-byte uint16BE (0 .. 65279)
//   if byte[0] == 0xFF -> 4-byte uint32BE & 0x00FFFFFF (0 .. 16777215)
inline bool ReadModoVx(const uint8_t* data, size_t endPos, size_t& pos, uint32_t& outIdx) {
    if (pos + 2 > endPos) return false;
    if (data[pos] != 0xFFu) {
        outIdx = RdU16BE(data + pos);
        pos += 2;
        return true;
    }
    if (pos + 4 > endPos) return false;
    outIdx = RdU32BE(data + pos) & 0x00FFFFFFu;
    pos += 4;
    return true;
}

inline bool TryFindAndInflateZlib(const uint8_t* data, size_t size, size_t maxScanOff, std::vector<uint8_t>& out) {
    out.clear();
    if (!data || size < 8) return false;
    size_t limit = std::min(size - 6, maxScanOff);
    for (size_t off = 0; off <= limit; ++off) {
        uint8_t b0 = data[off];
        uint8_t b1 = data[off + 1];
        if (b0 == 0x78u && (b1 == 0x01u || b1 == 0x5Eu || b1 == 0x9Cu || b1 == 0xDAu)) {
            if ((((static_cast<uint16_t>(b0) << 8) | b1) % 31u) == 0u) {
                std::vector<uint8_t> tmp;
                if (ZipDeflate::InflateZlib(data + off, size - off, tmp, 128 * 1024 * 1024) && tmp.size() >= 16) {
                    out = std::move(tmp);
                    return true;
                }
            }
        }
    }
    return false;
}

} // namespace ModoZbMaxC4dDetail

// ============================================================================
// 1. Foundry Modo Scene / Object (.lxo — IFF "FORM" + "LXOB" / "LXOP")
// ============================================================================
inline bool ParseModoLxo(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    using namespace ModoZbMaxC4dDetail;
    if (!data || size < 20 || std::memcmp(data, "FORM", 4) != 0) return false;
    uint32_t formSize = RdU32BE(data + 4);
    if (formSize < 12) return false;
    if (std::memcmp(data + 8, "LXOB", 4) != 0 && std::memcmp(data + 8, "LXOP", 4) != 0) return false;

    size_t fileEnd = std::min<size_t>(size, 8ULL + static_cast<size_t>(formSize));
    mesh.vertices.clear();
    mesh.tris.clear();

    auto decodePntsAndPols = [&](const uint8_t* buf, size_t startP, size_t endP, bool subChunk16) {
        uint32_t curLayerBase = static_cast<uint32_t>(mesh.vertices.size());
        uint32_t curLayerVerts = 0;
        size_t p = startP;
        size_t hdrLen = subChunk16 ? 6u : 8u;

        while (p + hdrLen <= endP) {
            const uint8_t* tag = buf + p;
            uint32_t cLen = subChunk16 ? RdU16BE(buf + p + 4) : RdU32BE(buf + p + 4);
            p += hdrLen;
            if (cLen > endP - p) break;
            size_t cEnd = p + cLen;

            if (std::memcmp(tag, "LAYR", 4) == 0) {
                curLayerBase = static_cast<uint32_t>(mesh.vertices.size());
                curLayerVerts = 0;
            } else if (std::memcmp(tag, "PNTS", 4) == 0 && cLen >= 36 && (cLen % 12u) == 0u) {
                uint32_t nv = cLen / 12u;
                if (nv <= 4000000u) {
                    curLayerBase = static_cast<uint32_t>(mesh.vertices.size());
                    curLayerVerts = 0;
                    mesh.vertices.reserve(mesh.vertices.size() + nv);
                    for (uint32_t i = 0; i < nv; ++i) {
                        float x = RdF32BE(buf + p + static_cast<size_t>(i) * 12u + 0);
                        float y = RdF32BE(buf + p + static_cast<size_t>(i) * 12u + 4);
                        float z = RdF32BE(buf + p + static_cast<size_t>(i) * 12u + 8);
                        if (!IsValidCoord3(x, y, z)) { curLayerVerts = 0; break; }
                        // Modo uses right-handed Y-up (+X right, +Y up, +Z forward)
                        mesh.vertices.push_back({ x, y, z });
                        ++curLayerVerts;
                    }
                }
            } else if (std::memcmp(tag, "POLS", 4) == 0 && cLen >= 10 && curLayerVerts >= 3) {
                const uint8_t* pt = buf + p;
                bool isSurfacePoly = (std::memcmp(pt, "FACE", 4) == 0 ||
                                      std::memcmp(pt, "SUBD", 4) == 0 ||
                                      std::memcmp(pt, "PSUB", 4) == 0 ||
                                      std::memcmp(pt, "PTCH", 4) == 0);
                if (isSurfacePoly) {
                    size_t q = p + 4;
                    std::vector<uint32_t> polyIdx;
                    while (q + 2 <= cEnd) {
                        uint16_t nvFlags = RdU16BE(buf + q);
                        q += 2;
                        uint16_t nv = nvFlags & 0x03FFu;
                        if (nv == 0) break;
                        polyIdx.clear();
                        polyIdx.reserve(nv);
                        bool okVx = true;
                        for (uint16_t k = 0; k < nv; ++k) {
                            uint32_t vi = 0;
                            if (!ReadModoVx(buf, cEnd, q, vi) || vi >= curLayerVerts) {
                                okVx = false;
                                break;
                            }
                            polyIdx.push_back(vi);
                        }
                        if (!okVx) break;
                        if (polyIdx.size() >= 3) {
                            for (size_t t = 1; t + 1 < polyIdx.size(); ++t) {
                                uint32_t a = curLayerBase + polyIdx[0];
                                uint32_t b = curLayerBase + polyIdx[t];
                                uint32_t c = curLayerBase + polyIdx[t + 1];
                                if (a != b && b != c && a != c) {
                                    mesh.tris.push_back({ a, b, c, {} });
                                }
                            }
                        }
                    }
                }
            } else if (!subChunk16 && std::memcmp(tag, "ITEM", 4) == 0 && cLen >= 12) {
                // ITEM chunk: [null-terminated itemType (even-padded)][uint32BE id][sub-chunks with u16BE size]
                size_t sEnd = p;
                while (sEnd < cEnd && buf[sEnd] != 0) ++sEnd;
                if (sEnd < cEnd) {
                    size_t afterType = sEnd + 1;
                    if (((afterType - p) & 1u) != 0u) ++afterType;
                    if (afterType + 4 <= cEnd) {
                        size_t subStart = afterType + 4;
                        // Recurse on 16-bit sub-chunks inside ITEM if any PNTS/POLS reside inside
                        uint32_t subBase = curLayerBase;
                        uint32_t subVerts = curLayerVerts;
                        size_t sp = subStart;
                        while (sp + 6 <= cEnd) {
                            const uint8_t* stag = buf + sp;
                            uint16_t sLen = RdU16BE(buf + sp + 4);
                            sp += 6;
                            if (sLen > cEnd - sp) break;
                            if (std::memcmp(stag, "PNTS", 4) == 0 && sLen >= 36 && (sLen % 12u) == 0u) {
                                uint32_t nv = sLen / 12u;
                                subBase = static_cast<uint32_t>(mesh.vertices.size());
                                subVerts = 0;
                                for (uint32_t i = 0; i < nv; ++i) {
                                    float x = RdF32BE(buf + sp + i * 12u + 0);
                                    float y = RdF32BE(buf + sp + i * 12u + 4);
                                    float z = RdF32BE(buf + sp + i * 12u + 8);
                                    if (!IsValidCoord3(x, y, z)) { subVerts = 0; break; }
                                    mesh.vertices.push_back({ x, y, z });
                                    ++subVerts;
                                }
                            } else if (std::memcmp(stag, "POLS", 4) == 0 && sLen >= 10 && subVerts >= 3) {
                                size_t q = sp + 4;
                                while (q + 2 <= sp + sLen) {
                                    uint16_t nv = RdU16BE(buf + q) & 0x03FFu;
                                    q += 2;
                                    if (nv == 0) break;
                                    std::vector<uint32_t> pidx;
                                    bool ok = true;
                                    for (uint16_t k = 0; k < nv; ++k) {
                                        uint32_t vi = 0;
                                        if (!ReadModoVx(buf, sp + sLen, q, vi) || vi >= subVerts) { ok = false; break; }
                                        pidx.push_back(vi);
                                    }
                                    if (!ok) break;
                                    for (size_t t = 1; t + 1 < pidx.size(); ++t) {
                                        mesh.tris.push_back({ subBase + pidx[0], subBase + pidx[t], subBase + pidx[t + 1], {} });
                                    }
                                }
                            }
                            sp += static_cast<size_t>(sLen) + (sLen & 1u);
                        }
                    }
                }
            }

            p = cEnd + (cLen & 1u);
        }
    };

    decodePntsAndPols(data, 12, fileEnd, false);
    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"Foundry Modo 3D Scene (.lxo)";
    return true;
}

// ============================================================================
// 2. Enhanced Autodesk 3ds Max (.max — Raw + RFC 1950 Zlib OLE2 "Scene", 32/64-bit chunks, TriObject + MNMesh)
// ============================================================================
inline bool Parse3dsMaxEnhanced(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    using namespace ModoZbMaxC4dDetail;
    Ole2::CompoundFile cf;
    if (!Ole2::ParseCompoundFile(data, size, cf)) return false;

    const Ole2::DirEntry* sceneEntry = Ole2::FindStreamCaseInsensitive(cf, L"Scene");
    if (!sceneEntry) return false;

    std::vector<uint8_t> rawScene;
    if (!Ole2::ExtractStream(cf, *sceneEntry, rawScene, 128 * 1024 * 1024) || rawScene.size() < 12) return false;

    // Decompress zlib-compressed Scene stream if present (3ds Max "Compress on Save" uses zlib at offset 0..16 or multi-block)
    std::vector<uint8_t> inflatedScene;
    const uint8_t* scenePtr = rawScene.data();
    size_t sceneLen = rawScene.size();
    if (TryFindAndInflateZlib(rawScene.data(), rawScene.size(), 24, inflatedScene)) {
        scenePtr = inflatedScene.data();
        sceneLen = inflatedScene.size();
    }

    mesh.vertices.clear();
    mesh.tris.clear();

    auto walkMaxChunks = [&](auto& self, size_t s, size_t e, int depth) -> void {
        if (depth > 32 || s + 6 > e) return;
        std::vector<Vec3f> verts;
        std::vector<std::array<uint32_t, 3>> tris;
        size_t p = s;

        while (p + 6 <= e) {
            uint16_t cid = RdU16LE(scenePtr + p);
            uint32_t rawSz32 = RdU32LE(scenePtr + p + 2);
            bool isContainer = (rawSz32 & 0x80000000u) != 0;
            uint64_t totalSz = rawSz32 & 0x7FFFFFFFu;
            size_t hdrSz = 6;

            // 64-bit chunk header extension (3ds Max 2012+): when 31-bit size == 0, 8-byte int64 size follows
            if (totalSz == 0 && p + 14 <= e) {
                uint64_t rawSz64 = RdU64LE(scenePtr + p + 6);
                isContainer = (rawSz64 & (1ULL << 63)) != 0;
                totalSz = rawSz64 & ~(1ULL << 63);
                hdrSz = 14;
            }
            if (totalSz < hdrSz || totalSz > e - p) break;

            size_t pay = p + hdrSz;
            size_t payLen = static_cast<size_t>(totalSz) - hdrSz;

            if (isContainer) {
                self(self, pay, p + static_cast<size_t>(totalSz), depth + 1);
            } else {
                // TriObject VERTEX_CHUNK (0x0912 / 0x0100): [uint32 nv][float3[nv]]
                if ((cid == 0x0912u || cid == 0x0100u) && payLen >= 40) {
                    uint32_t nv = RdU32LE(scenePtr + pay);
                    if (nv >= 3 && nv <= 2000000u && payLen == 4u + static_cast<size_t>(nv) * 12u) {
                        verts.clear();
                        verts.reserve(nv);
                        for (uint32_t i = 0; i < nv; ++i) {
                            float x = RdF32LE(scenePtr + pay + 4 + static_cast<size_t>(i) * 12 + 0);
                            float y = RdF32LE(scenePtr + pay + 4 + static_cast<size_t>(i) * 12 + 4);
                            float z = RdF32LE(scenePtr + pay + 4 + static_cast<size_t>(i) * 12 + 8);
                            if (!IsValidCoord3(x, y, z)) { verts.clear(); break; }
                            verts.push_back({ x, z, -y });
                        }
                    }
                }
                // Editable Poly MNMesh MNVert chunk (0x0118): [uint32 nv][16B per vert: float3 p + uint32 flags]
                else if (cid == 0x0118u && payLen >= 52) {
                    uint32_t nv = RdU32LE(scenePtr + pay);
                    if (nv >= 3 && nv <= 2000000u && payLen == 4u + static_cast<size_t>(nv) * 16u) {
                        verts.clear();
                        verts.reserve(nv);
                        for (uint32_t i = 0; i < nv; ++i) {
                            float x = RdF32LE(scenePtr + pay + 4 + static_cast<size_t>(i) * 16 + 0);
                            float y = RdF32LE(scenePtr + pay + 4 + static_cast<size_t>(i) * 16 + 4);
                            float z = RdF32LE(scenePtr + pay + 4 + static_cast<size_t>(i) * 16 + 8);
                            if (!IsValidCoord3(x, y, z)) { verts.clear(); break; }
                            verts.push_back({ x, z, -y });
                        }
                    }
                }
                // TriObject FACE_CHUNK (0x0910 / 0x0914 / 0x0110): [uint32 nf][stride 12/16/20 Face records]
                else if ((cid == 0x0910u || cid == 0x0914u || cid == 0x0110u) && payLen >= 16 && !verts.empty()) {
                    uint32_t nf = RdU32LE(scenePtr + pay);
                    size_t stride = (nf > 0 && ((payLen - 4u) % nf) == 0u) ? ((payLen - 4u) / nf) : 0u;
                    if (nf >= 1 && nf <= 4000000u && (stride == 12u || stride == 16u || stride == 20u)) {
                        for (uint32_t i = 0; i < nf; ++i) {
                            uint32_t a = RdU32LE(scenePtr + pay + 4 + static_cast<size_t>(i) * stride + 0);
                            uint32_t b = RdU32LE(scenePtr + pay + 4 + static_cast<size_t>(i) * stride + 4);
                            uint32_t c = RdU32LE(scenePtr + pay + 4 + static_cast<size_t>(i) * stride + 8);
                            if (a < verts.size() && b < verts.size() && c < verts.size() && a != b && b != c && a != c) {
                                tris.push_back({ a, b, c });
                            }
                        }
                    }
                }
                // Editable Poly MNMesh MNFace chunk (0x0128): [uint32 nf][repeated: uint32 deg, uint32 vtx[deg]]
                else if (cid == 0x0128u && payLen >= 20 && !verts.empty()) {
                    uint32_t nf = RdU32LE(scenePtr + pay);
                    size_t q = pay + 4;
                    size_t qEnd = pay + payLen;
                    if (nf >= 1 && nf <= 4000000u) {
                        for (uint32_t f = 0; f < nf && q + 4 <= qEnd; ++f) {
                            uint32_t deg = RdU32LE(scenePtr + q);
                            q += 4;
                            if (deg < 3 || deg > 256 || q + static_cast<size_t>(deg) * 4u > qEnd) break;
                            uint32_t i0 = RdU32LE(scenePtr + q);
                            for (uint32_t t = 1; t + 1 < deg; ++t) {
                                uint32_t i1 = RdU32LE(scenePtr + q + static_cast<size_t>(t) * 4u);
                                uint32_t i2 = RdU32LE(scenePtr + q + static_cast<size_t>(t + 1) * 4u);
                                if (i0 < verts.size() && i1 < verts.size() && i2 < verts.size()) {
                                    tris.push_back({ i0, i1, i2 });
                                }
                            }
                            q += static_cast<size_t>(deg) * 4u;
                        }
                    }
                }
            }
            p += static_cast<size_t>(totalSz);
        }

        if (!verts.empty() && !tris.empty()) {
            uint32_t b = static_cast<uint32_t>(mesh.vertices.size());
            mesh.vertices.insert(mesh.vertices.end(), verts.begin(), verts.end());
            for (const auto& t : tris) {
                mesh.tris.push_back({ b + t[0], b + t[1], b + t[2], {} });
            }
        }
    };

    walkMaxChunks(walkMaxChunks, 0, sceneLen, 0);
    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = !inflatedScene.empty()
        ? L"Autodesk 3ds Max Compressed Scene (.max)"
        : L"Autodesk 3ds Max Scene (.max)";
    return true;
}

// ============================================================================
// 3. Maxon Cinema 4D (.c4d — MC4D / C4DC / C4D6 v5-R25+ Raw & Zlib HyperFile Chunks)
// ============================================================================
inline bool ParseCinema4dEnhanced(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    using namespace ModoZbMaxC4dDetail;
    if (!data || size < 16) return false;

    size_t bodyOff = 0;
    if (std::memcmp(data, "FORM", 4) == 0 && size >= 12 &&
        (std::memcmp(data + 8, "MC4D", 4) == 0 || std::memcmp(data + 8, "C4DC", 4) == 0)) {
        bodyOff = 12;
    } else if (std::memcmp(data, "C4DC", 4) == 0 || std::memcmp(data, "C4D6", 4) == 0 || std::memcmp(data, "MC4D", 4) == 0) {
        bodyOff = 8;
    } else if (size >= 12 && (std::memcmp(data + 4, "C4DC", 4) == 0 || std::memcmp(data + 4, "MC4D", 4) == 0 || std::memcmp(data + 4, "C4D6", 4) == 0)) {
        bodyOff = 12;
    } else if (size >= 16 && (std::memcmp(data + 8, "C4DC", 4) == 0 || std::memcmp(data + 8, "MC4D", 4) == 0)) {
        bodyOff = 16;
    } else {
        return false;
    }

    // Check if the C4DC body is zlib-compressed (standard in Cinema 4D R6-R25+)
    std::vector<uint8_t> inflated;
    const uint8_t* buf = data + bodyOff;
    size_t bufLen = size - bodyOff;
    if (TryFindAndInflateZlib(buf, bufLen, 24, inflated)) {
        buf = inflated.data();
        bufLen = inflated.size();
    }

    mesh.vertices.clear();
    mesh.tris.clear();

    auto walkC4d = [&](auto& self, size_t s, size_t e, int depth) -> void {
        if (depth > 24 || s + 8 > e) return;
        uint32_t vBase = static_cast<uint32_t>(mesh.vertices.size());
        uint32_t vCount = 0;
        size_t p = s;

        while (p + 8 <= e) {
            const uint8_t* tag = buf + p;
            uint32_t rawLen = RdU32BE(buf + p + 4);
            bool isCont = std::memcmp(tag, "OBJ ", 4) == 0 || std::memcmp(tag, "OPOL", 4) == 0 ||
                          std::memcmp(tag, "ROOT", 4) == 0 || std::memcmp(tag, "SCEN", 4) == 0 ||
                          std::memcmp(tag, "DOKU", 4) == 0;
            uint32_t cLen = rawLen;
            p += 8;
            if (cLen > e - p) break;

            if (isCont) {
                self(self, p, p + cLen, depth + 1);
            }
            // Float32 vertex chunk: POLP / PNTS / Poin
            else if ((std::memcmp(tag, "POLP", 4) == 0 || std::memcmp(tag, "PNTS", 4) == 0 || std::memcmp(tag, "Poin", 4) == 0) &&
                     cLen >= 36 && (cLen % 12u) == 0u)
            {
                uint32_t nv = cLen / 12u;
                if (nv <= 2000000u) {
                    vBase = static_cast<uint32_t>(mesh.vertices.size());
                    vCount = 0;
                    for (uint32_t i = 0; i < nv; ++i) {
                        float x = RdF32BE(buf + p + static_cast<size_t>(i) * 12u + 0);
                        float y = RdF32BE(buf + p + static_cast<size_t>(i) * 12u + 4);
                        float z = RdF32BE(buf + p + static_cast<size_t>(i) * 12u + 8);
                        if (!IsValidCoord3(x, y, z)) { vCount = 0; break; }
                        mesh.vertices.push_back({ x, y, z });
                        ++vCount;
                    }
                }
            }
            // Float64 (R16+ Vector64) vertex chunk: PNT8 / Poi8
            else if ((std::memcmp(tag, "PNT8", 4) == 0 || std::memcmp(tag, "Poi8", 4) == 0) &&
                     cLen >= 72 && (cLen % 24u) == 0u)
            {
                uint32_t nv = cLen / 24u;
                if (nv <= 2000000u) {
                    vBase = static_cast<uint32_t>(mesh.vertices.size());
                    vCount = 0;
                    for (uint32_t i = 0; i < nv; ++i) {
                        float x = static_cast<float>(RdF64BE(buf + p + static_cast<size_t>(i) * 24u + 0));
                        float y = static_cast<float>(RdF64BE(buf + p + static_cast<size_t>(i) * 24u + 8));
                        float z = static_cast<float>(RdF64BE(buf + p + static_cast<size_t>(i) * 24u + 16));
                        if (!IsValidCoord3(x, y, z)) { vCount = 0; break; }
                        mesh.vertices.push_back({ x, y, z });
                        ++vCount;
                    }
                }
            }
            // Polygon chunk (CPolygon: 4 * uint32BE a,b,c,d; c==d is triangle, c!=d is quad): POLY / POLS / Polg
            else if ((std::memcmp(tag, "POLY", 4) == 0 || std::memcmp(tag, "POLS", 4) == 0 || std::memcmp(tag, "Polg", 4) == 0) &&
                     cLen >= 16 && (cLen % 16u) == 0u && vCount >= 3)
            {
                uint32_t nf = cLen / 16u;
                for (uint32_t i = 0; i < nf && i < 4000000u; ++i) {
                    uint32_t a = RdU32BE(buf + p + static_cast<size_t>(i) * 16u + 0);
                    uint32_t b = RdU32BE(buf + p + static_cast<size_t>(i) * 16u + 4);
                    uint32_t c = RdU32BE(buf + p + static_cast<size_t>(i) * 16u + 8);
                    uint32_t d = RdU32BE(buf + p + static_cast<size_t>(i) * 16u + 12);
                    if (a < vCount && b < vCount && c < vCount && d < vCount) {
                        if (a != b && b != c && a != c) {
                            mesh.tris.push_back({ vBase + a, vBase + b, vBase + c, {} });
                        }
                        if (c != d && a != c && c != d && a != d) {
                            mesh.tris.push_back({ vBase + a, vBase + c, vBase + d, {} });
                        }
                    }
                }
            }

            p += static_cast<size_t>(cLen) + (cLen & 1u);
        }
    };

    walkC4d(walkC4d, 0, bufLen, 0);
    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"Maxon Cinema 4D Scene (.c4d)";
    return true;
}

// ============================================================================
// 4. Pixologic / Maxon ZBrush Project & Tool (.zpr, .ztl)
// ============================================================================
inline bool ParseZBrushZprZtl(const uint8_t* data, size_t size, const std::wstring& extLower, LoadedMesh3D& mesh) {
    using namespace ModoZbMaxC4dDetail;
    if (!data || size < 32) return false;

    std::string_view hdrSv(reinterpret_cast<const char*>(data), std::min<size_t>(size, 128));
    bool hasZbSig = hdrSv.find("ZBrush File") != std::string_view::npos ||
                    hdrSv.find("ZBRUSH") != std::string_view::npos ||
                    hdrSv.find("Pixologic") != std::string_view::npos ||
                    std::memcmp(data, "ZPR\0", 4) == 0 ||
                    std::memcmp(data, "ZTL\0", 4) == 0;
    if (!hasZbSig) return false;

    mesh.vertices.clear();
    mesh.tris.clear();

    auto decodePm3dSubTools = [&](const uint8_t* buf, size_t len) {
        size_t pos = 0;
        while (pos + 24 <= len) {
            if (std::memcmp(buf + pos, "PM3D", 4) != 0) {
                ++pos;
                continue;
            }
            // PM3D block: ["PM3D" 4B][uint32LE subToolVersion][uint32LE nVerts][uint32LE nPolys][float3 verts[nVerts]][uint32[4] polys[nPolys]]
            uint32_t ver = RdU32LE(buf + pos + 4);
            uint32_t nVerts = RdU32LE(buf + pos + 8);
            uint32_t nPolys = RdU32LE(buf + pos + 12);
            if (ver == 0 || ver > 64 || nVerts < 3 || nVerts > 5000000u || nPolys < 1 || nPolys > 5000000u) {
                pos += 4;
                continue;
            }
            uint64_t vBytes = static_cast<uint64_t>(nVerts) * 12ULL;
            uint64_t pBytes = static_cast<uint64_t>(nPolys) * 16ULL;
            if (pos + 16ULL + vBytes + pBytes > len) {
                pos += 4;
                continue;
            }

            const uint8_t* vp = buf + pos + 16;
            const uint8_t* pp = vp + static_cast<size_t>(vBytes);
            uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
            bool okSub = true;
            for (uint32_t i = 0; i < nVerts; ++i) {
                float x = RdF32LE(vp + static_cast<size_t>(i) * 12u + 0);
                float y = RdF32LE(vp + static_cast<size_t>(i) * 12u + 4);
                float z = RdF32LE(vp + static_cast<size_t>(i) * 12u + 8);
                if (!IsValidCoord3(x, y, z)) { okSub = false; break; }
                mesh.vertices.push_back({ x, y, z });
            }
            if (!okSub) {
                mesh.vertices.resize(base);
                pos += 4;
                continue;
            }
            for (uint32_t f = 0; f < nPolys; ++f) {
                uint32_t a = RdU32LE(pp + static_cast<size_t>(f) * 16u + 0);
                uint32_t b = RdU32LE(pp + static_cast<size_t>(f) * 16u + 4);
                uint32_t c = RdU32LE(pp + static_cast<size_t>(f) * 16u + 8);
                uint32_t d = RdU32LE(pp + static_cast<size_t>(f) * 16u + 12);
                if (a >= nVerts || b >= nVerts || c >= nVerts) continue;
                if (a != b && b != c && a != c) {
                    mesh.tris.push_back({ base + a, base + b, base + c, {} });
                }
                if (d != 0xFFFFFFFFu && d < nVerts && d != c && a != d) {
                    mesh.tris.push_back({ base + a, base + c, base + d, {} });
                }
            }
            pos += 16u + static_cast<size_t>(vBytes + pBytes);
        }
    };

    // Scan raw file for PM3D SubTool blocks AND decompress any zlib-compressed SubTool streams ("ZBCZ" + uint32 compLen + zlib)
    decodePm3dSubTools(data, size);

    size_t zp = 16;
    while (zp + 12 <= size) {
        if (std::memcmp(data + zp, "ZBCZ", 4) == 0) {
            uint32_t compLen = RdU32LE(data + zp + 4);
            if (compLen >= 8 && zp + 8ULL + compLen <= size) {
                std::vector<uint8_t> subDec;
                if (ZipDeflate::InflateZlib(data + zp + 8, compLen, subDec, 128 * 1024 * 1024) && !subDec.empty()) {
                    decodePm3dSubTools(subDec.data(), subDec.size());
                }
                zp += 8u + compLen;
                continue;
            }
        }
        ++zp;
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = (extLower == L".zpr")
        ? L"Maxon ZBrush 3D Project (.zpr)"
        : L"Maxon ZBrush 3D SubTool (.ztl)";
    return true;
}

} // namespace Formats
