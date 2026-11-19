#pragma once
// OgreMesh.h — Ogre3D Binary Mesh (.mesh 0x1000 MeshSerializer) reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {
namespace BspGeoDetail {

inline bool ParseOgreBinaryMesh(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    if (!data || size < 16) return false;
    mesh.vertices.clear();
    mesh.tris.clear();

    // A. Ogre3D Binary Mesh: uint16 0x1000 + "[MeshSerializer_v1.xx]\n"
    if (size >= 24 && RdU16LE(data) == 0x1000u) {
        size_t pos = 2;
        while (pos < size && pos < 80 && data[pos] != '\n' && data[pos] != 0) ++pos;
        std::string_view verSv(reinterpret_cast<const char*>(data + 2), pos - 2);
        if (verSv.find("MeshSerializer") == std::string_view::npos) return false;
        if (pos < size) ++pos;

        auto parseOgreGeometry = [&](size_t gStart, size_t gEnd, std::vector<Vec3f>& outVerts) -> bool {
            if (gStart + 4 > gEnd) return false;
            uint32_t vCount = RdU32LE(data + gStart);
            if (vCount == 0 || vCount > 2000000) return false;
            size_t p = gStart + 4;

            uint16_t posSource = 0, posType = 2, posOffset = 0;
            bool foundPosDecl = false;

            while (p + 6 <= gEnd) {
                uint16_t cid = RdU16LE(data + p);
                uint32_t clen = RdU32LE(data + p + 2);
                if (clen < 6 || p + clen > gEnd) break;
                size_t cBody = p + 6;
                size_t cEnd  = p + clen;

                if (cid == 0x5100u) { // M_GEOMETRY_VERTEX_DECLARATION
                    size_t dp = cBody;
                    while (dp + 16 <= cEnd) {
                        uint16_t did = RdU16LE(data + dp);
                        uint32_t dlen = RdU32LE(data + dp + 2);
                        if (dlen < 16 || dp + dlen > cEnd) break;
                        if (did == 0x5110u) {
                            uint16_t src = RdU16LE(data + dp + 6);
                            uint16_t typ = RdU16LE(data + dp + 8);
                            uint16_t sem = RdU16LE(data + dp + 10); // 1 = VES_POSITION
                            uint16_t off = RdU16LE(data + dp + 12);
                            if (sem == 1) { posSource = src; posType = typ; posOffset = off; foundPosDecl = true; }
                        }
                        dp += dlen;
                    }
                } else if (cid == 0x5200u && cBody + 4 <= cEnd) { // M_GEOMETRY_VERTEX_BUFFER
                    uint16_t bindIdx = RdU16LE(data + cBody);
                    uint16_t vStride = RdU16LE(data + cBody + 2);
                    size_t bp = cBody + 4;
                    if (bp + 6 <= cEnd && RdU16LE(data + bp) == 0x5210u && (!foundPosDecl || bindIdx == posSource)) {
                        size_t rawStart = bp + 6;
                        if (vStride >= 6 && rawStart + static_cast<uint64_t>(vCount) * vStride <= cEnd) {
                            outVerts.resize(vCount);
                            for (uint32_t i = 0; i < vCount; ++i) {
                                const uint8_t* vp = data + rawStart + static_cast<size_t>(i) * vStride + posOffset;
                                if (posType == 2 && posOffset + 12 <= vStride) { // VET_FLOAT3
                                    outVerts[i] = { RdF32LE(vp + 0), RdF32LE(vp + 4), RdF32LE(vp + 8) };
                                } else if (posOffset + 6 <= vStride) { // VET_HALF3 / SHORT3
                                    outVerts[i] = {
                                        SplatVdbDetail::HalfToF32Splat(RdU16LE(vp + 0)),
                                        SplatVdbDetail::HalfToF32Splat(RdU16LE(vp + 2)),
                                        SplatVdbDetail::HalfToF32Splat(RdU16LE(vp + 4))
                                    };
                                }
                            }
                        }
                    }
                }
                p = cEnd;
            }
            return !outVerts.empty();
        };

        while (pos + 6 <= size) {
            uint16_t mId  = RdU16LE(data + pos);
            uint32_t mLen = RdU32LE(data + pos + 2);
            if (mLen < 6 || pos + mLen > size) break;
            if (mId == 0x3000u) { // M_MESH
                size_t mp = pos + 6 + 1; // skip bool skeletallyAnimated
                std::vector<Vec3f> sharedVerts;
                struct DeferredSubMesh {
                    bool useShared = true;
                    uint16_t opType = 4; // OT_TRIANGLE_LIST
                    std::vector<uint32_t> indices;
                    std::vector<Vec3f> ownVerts;
                };
                std::vector<DeferredSubMesh> subs;

                while (mp + 6 <= pos + mLen) {
                    uint16_t sid  = RdU16LE(data + mp);
                    uint32_t slen = RdU32LE(data + mp + 2);
                    if (slen < 6 || mp + slen > pos + mLen) break;
                    size_t sBody = mp + 6;
                    size_t sEnd  = mp + slen;

                    if (sid == 0x5000u) { // Shared M_GEOMETRY
                        parseOgreGeometry(sBody, sEnd, sharedVerts);
                    } else if (sid == 0x4000u) { // M_SUBMESH
                        size_t sp = sBody;
                        while (sp < sEnd && data[sp] != '\n' && data[sp] != 0) ++sp;
                        if (sp < sEnd) ++sp;
                        if (sp + 6 <= sEnd) {
                            DeferredSubMesh sm;
                            sm.useShared = (data[sp++] != 0);
                            uint32_t idxCnt = RdU32LE(data + sp); sp += 4;
                            bool idx32 = (data[sp++] != 0);
                            size_t idxBytes = static_cast<size_t>(idxCnt) * (idx32 ? 4u : 2u);
                            if (idxCnt <= 6000000 && sp + idxBytes <= sEnd) {
                                sm.indices.resize(idxCnt);
                                for (uint32_t i = 0; i < idxCnt; ++i) {
                                    sm.indices[i] = idx32 ? RdU32LE(data + sp + i * 4u) : RdU16LE(data + sp + i * 2u);
                                }
                                sp += idxBytes;
                                while (sp + 6 <= sEnd) {
                                    uint16_t scid  = RdU16LE(data + sp);
                                    uint32_t sclen = RdU32LE(data + sp + 2);
                                    if (sclen < 6 || sp + sclen > sEnd) break;
                                    if (scid == 0x5000u) parseOgreGeometry(sp + 6, sp + sclen, sm.ownVerts);
                                    else if (scid == 0x4010u && sp + 8 <= sEnd) sm.opType = RdU16LE(data + sp + 6);
                                    sp += sclen;
                                }
                                subs.push_back(std::move(sm));
                            }
                        }
                    }
                    mp = sEnd;
                }

                uint32_t sharedBase = 0;
                bool emittedShared = false;
                for (const auto& sm : subs) {
                    const std::vector<Vec3f>& vSrc = sm.useShared ? sharedVerts : sm.ownVerts;
                    if (vSrc.empty() || sm.indices.size() < 3) continue;
                    uint32_t vBase = 0;
                    if (sm.useShared) {
                        if (!emittedShared) {
                            sharedBase = static_cast<uint32_t>(mesh.vertices.size());
                            mesh.vertices.insert(mesh.vertices.end(), sharedVerts.begin(), sharedVerts.end());
                            emittedShared = true;
                        }
                        vBase = sharedBase;
                    } else {
                        vBase = static_cast<uint32_t>(mesh.vertices.size());
                        mesh.vertices.insert(mesh.vertices.end(), sm.ownVerts.begin(), sm.ownVerts.end());
                    }

                    size_t nv = vSrc.size();
                    if (sm.opType == 5) { // OT_TRIANGLE_STRIP
                        for (size_t k = 0; k + 2 < sm.indices.size(); ++k) {
                            uint32_t a = sm.indices[k], b = sm.indices[k + 1], c = sm.indices[k + 2];
                            if (a < nv && b < nv && c < nv && a != b && b != c && a != c) {
                                if ((k & 1u) == 0) mesh.tris.push_back({ vBase + a, vBase + b, vBase + c, {} });
                                else               mesh.tris.push_back({ vBase + b, vBase + a, vBase + c, {} });
                            }
                        }
                    } else if (sm.opType == 6) { // OT_TRIANGLE_FAN
                        for (size_t k = 1; k + 1 < sm.indices.size(); ++k) {
                            uint32_t a = sm.indices[0], b = sm.indices[k], c = sm.indices[k + 1];
                            if (a < nv && b < nv && c < nv) mesh.tris.push_back({ vBase + a, vBase + b, vBase + c, {} });
                        }
                    } else { // OT_TRIANGLE_LIST (4)
                        for (size_t k = 0; k + 2 < sm.indices.size(); k += 3) {
                            uint32_t a = sm.indices[k], b = sm.indices[k + 1], c = sm.indices[k + 2];
                            if (a < nv && b < nv && c < nv) mesh.tris.push_back({ vBase + a, vBase + b, vBase + c, {} });
                        }
                    }
                }
            }
            pos += mLen;
        }
        if (mesh.vertices.empty() || mesh.tris.empty()) return false;
        mesh.formatName = L"Ogre3D Binary Mesh (.mesh)";
        return true;
    }

    return false;
}

} // namespace BspGeoDetail
} // namespace Formats
