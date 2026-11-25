#pragma once
// FoundryModoLxo.h — Foundry Modo 3D Scene / Object (.lxo — IFF LXOB / LXOP) reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

// ============================================================================
// 1. Foundry Modo Scene / Object (.lxo — IFF "FORM" + "LXOB" / "LXOP")
// ============================================================================
inline bool ParseModoLxo(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    using namespace DetailDccSculpt;
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

} // namespace Formats
