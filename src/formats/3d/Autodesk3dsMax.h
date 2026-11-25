#pragma once
// Autodesk3dsMax.h — Autodesk 3ds Max (.max — OLE2 Scene TriObject & MNMesh) 3D reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

// ============================================================================
// 2. Enhanced Autodesk 3ds Max (.max — Raw + RFC 1950 Zlib OLE2 "Scene", 32/64-bit chunks, TriObject + MNMesh)
// ============================================================================
inline bool Parse3dsMaxEnhanced(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    using namespace DetailDccSculpt;
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

} // namespace Formats
