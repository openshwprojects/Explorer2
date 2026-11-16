#pragma once
// RenderWare3D.h — RenderWare 3.x Binary Stream 3D Clumps, World Sectors & Texture Dictionaries (.dff, .rws, .txd)

namespace Formats {

namespace RwStreamDetail {

inline uint16_t RwU16(const uint8_t* p) {
    uint16_t v = 0;
    std::memcpy(&v, p, 2);
    return v;
}

inline uint32_t RwU32(const uint8_t* p) {
    uint32_t v = 0;
    std::memcpy(&v, p, 4);
    return v;
}

inline int32_t RwI32(const uint8_t* p) {
    int32_t v = 0;
    std::memcpy(&v, p, 4);
    return v;
}

inline float RwF32(const uint8_t* p) {
    float v = 0.0f;
    std::memcpy(&v, p, 4);
    return v;
}

inline uint32_t UnpackRwLibraryVersion(uint32_t stamp) {
    if ((stamp & 0xFFFF0000u) == 0u) {
        return stamp << 8; // Old unpacked RW 3.0/3.1 format (e.g. 0x0310 -> 0x31000)
    }
    return (((stamp >> 14) & 0x3FF00u) + 0x30000u) | ((stamp >> 16) & 0x3Fu);
}

inline bool IsPlausibleRwVersion(uint32_t stamp) {
    uint32_t ver = UnpackRwLibraryVersion(stamp);
    return ver >= 0x30000u && ver <= 0x39FFF;
}

struct RwChunkHdr {
    uint32_t type = 0;
    uint32_t size = 0;
    uint32_t stamp = 0;
    size_t payloadOffset = 0;
    size_t endOffset = 0;
};

inline bool ReadRwChunk(const uint8_t* data, size_t limit, size_t& pos, RwChunkHdr& out) {
    if (!data || pos + 12 > limit) return false;
    out.type  = RwU32(data + pos + 0);
    out.size  = RwU32(data + pos + 4);
    out.stamp = RwU32(data + pos + 8);
    if (!IsPlausibleRwVersion(out.stamp)) return false;
    if (out.size > limit - (pos + 12)) return false;
    out.payloadOffset = pos + 12;
    out.endOffset = out.payloadOffset + out.size;
    pos = out.endOffset;
    return true;
}

} // namespace RwStreamDetail

#if defined(E2_RW_ENABLE_3D)

namespace Rw3DDetail {

struct RwMat4x3 {
    float rx = 1.0f, ry = 0.0f, rz = 0.0f;
    float ux = 0.0f, uy = 1.0f, uz = 0.0f;
    float ax = 0.0f, ay = 0.0f, az = 1.0f;
    float px = 0.0f, py = 0.0f, pz = 0.0f;

    void TransformPoint(float x, float y, float z, float& ox, float& oy, float& oz) const {
        ox = rx * x + ux * y + ax * z + px;
        oy = ry * x + uy * y + ay * z + py;
        oz = rz * x + uz * y + az * z + pz;
    }

    static RwMat4x3 Multiply(const RwMat4x3& local, const RwMat4x3& parent) {
        RwMat4x3 r;
        parent.TransformPoint(local.px, local.py, local.pz, r.px, r.py, r.pz);
        r.rx = parent.rx * local.rx + parent.ux * local.ry + parent.ax * local.rz;
        r.ry = parent.ry * local.rx + parent.uy * local.ry + parent.az * local.rz;
        r.rz = parent.rz * local.rx + parent.uz * local.ry + parent.az * local.rz;
        r.ux = parent.rx * local.ux + parent.ux * local.uy + parent.ax * local.uz;
        r.uy = parent.ry * local.ux + parent.uy * local.uy + parent.az * local.uz;
        r.uz = parent.rz * local.ux + parent.uz * local.uy + parent.az * local.uz;
        r.ax = parent.rx * local.ax + parent.ux * local.ay + parent.ax * local.az;
        r.ay = parent.ry * local.ax + parent.uy * local.ay + parent.az * local.az;
        r.az = parent.rz * local.ax + parent.uz * local.ay + parent.az * local.az;
        return r;
    }
};

struct RwParsedGeom {
    std::vector<Vec3f> verts; // Raw RenderWare coordinates before frame transform
    std::vector<std::array<uint32_t, 3>> tris;
};

inline Vec3f RwZUpToViewport(float x, float y, float z) {
    return { x, z, -y };
}

inline bool ParseRwGeometryChunk(const uint8_t* data, size_t start, size_t end, uint32_t chunkStamp, RwParsedGeom& outG) {
    using namespace RwStreamDetail;
    size_t pos = start;
    RwChunkHdr stHdr;
    if (!ReadRwChunk(data, end, pos, stHdr) || stHdr.type != 0x01u || stHdr.size < 16) return false;

    const uint8_t* sp = data + stHdr.payloadOffset;
    const size_t sLen = stHdr.size;
    size_t sOff = 0;

    uint32_t flags = RwU32(sp + 0);
    int32_t numTris = RwI32(sp + 4);
    int32_t numVerts = RwI32(sp + 8);
    int32_t numMorphs = RwI32(sp + 12);
    sOff = 16;

    if (numVerts <= 0 || numVerts > 2000000 || numTris < 0 || numTris > 4000000 || numMorphs <= 0) {
        return false;
    }

    uint32_t rwVer = UnpackRwLibraryVersion(stHdr.stamp ? stHdr.stamp : chunkStamp);
    if (rwVer < 0x34000u) {
        if (sOff + 12 > sLen) return false;
        sOff += 12; // ambient, specular, diffuse in RW < 3.4
    }

    std::vector<std::array<uint32_t, 3>> structTris;
    const bool isNative = (flags & 0x01000000u) != 0;
    if (!isNative) {
        if (flags & 0x08u) { // rpGEOMETRYPRELIT
            uint64_t prelitBytes = static_cast<uint64_t>(numVerts) * 4ULL;
            if (prelitBytes > sLen - sOff) return false;
            sOff += static_cast<size_t>(prelitBytes);
        }
        uint32_t numUvSets = (flags >> 16) & 0xFFu;
        if (numUvSets == 0) {
            if (flags & 0x80u) numUvSets = 2;
            else if (flags & 0x04u) numUvSets = 1;
        }
        uint64_t uvBytes = static_cast<uint64_t>(numUvSets) * static_cast<uint64_t>(numVerts) * 8ULL;
        if (uvBytes > sLen - sOff) return false;
        sOff += static_cast<size_t>(uvBytes);

        uint64_t triBytes = static_cast<uint64_t>(numTris) * 8ULL;
        if (triBytes > sLen - sOff) return false;
        structTris.reserve(static_cast<size_t>(numTris));
        for (int32_t t = 0; t < numTris; ++t) {
            const uint8_t* tp = sp + sOff + static_cast<size_t>(t) * 8;
            // RpTriangle on disk: uint16 v2, uint16 v1, uint16 materialId, uint16 v3
            uint16_t v2 = RwU16(tp + 0);
            uint16_t v1 = RwU16(tp + 2);
            uint16_t v3 = RwU16(tp + 6);
            if (v1 >= static_cast<uint32_t>(numVerts) ||
                v2 >= static_cast<uint32_t>(numVerts) ||
                v3 >= static_cast<uint32_t>(numVerts)) {
                return false;
            }
            if (v1 != v2 && v2 != v3 && v3 != v1) {
                structTris.push_back({ v1, v2, v3 });
            }
        }
        sOff += static_cast<size_t>(triBytes);
    }

    // Morph target 0: boundingSphere (16 B) + hasPositions (4 B) + hasNormals (4 B)
    if (sOff + 24 > sLen) return false;
    int32_t hasPos = RwI32(sp + sOff + 16);
    sOff += 24;
    if (!hasPos) return false;
    uint64_t posBytes = static_cast<uint64_t>(numVerts) * 12ULL;
    if (posBytes > sLen - sOff) return false;

    outG.verts.resize(static_cast<size_t>(numVerts));
    for (int32_t i = 0; i < numVerts; ++i) {
        float x = RwF32(sp + sOff + static_cast<size_t>(i) * 12 + 0);
        float y = RwF32(sp + sOff + static_cast<size_t>(i) * 12 + 4);
        float z = RwF32(sp + sOff + static_cast<size_t>(i) * 12 + 8);
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
        outG.verts[static_cast<size_t>(i)] = { x, y, z };
    }

    // Check child chunks (Material List 0x08, Extension 0x03 -> BinMesh PLG 0x050E)
    std::vector<std::array<uint32_t, 3>> binMeshTris;
    while (pos + 12 <= end) {
        RwChunkHdr sub;
        if (!ReadRwChunk(data, end, pos, sub)) break;
        if (sub.type == 0x03u) { // Extension
            size_t ePos = sub.payloadOffset;
            while (ePos + 12 <= sub.endOffset) {
                RwChunkHdr extCh;
                if (!ReadRwChunk(data, sub.endOffset, ePos, extCh)) break;
                if (extCh.type == 0x050Eu && extCh.size >= 12) { // BinMesh PLG
                    const uint8_t* bp = data + extCh.payloadOffset;
                    uint32_t bmFlags = RwU32(bp + 0);
                    uint32_t numSplits = RwU32(bp + 4);
                    const bool isStrip = (bmFlags & 1u) != 0 || (flags & 1u) != 0;
                    size_t bOff = 12;
                    for (uint32_t s = 0; s < numSplits && bOff + 8 <= extCh.size; ++s) {
                        uint32_t splitIdxCnt = RwU32(bp + bOff + 0);
                        bOff += 8;
                        if (static_cast<uint64_t>(splitIdxCnt) * 4ULL > extCh.size - bOff) break;
                        if (!isStrip) {
                            for (uint32_t k = 0; k + 2 < splitIdxCnt; k += 3) {
                                uint32_t a = RwU32(bp + bOff + static_cast<size_t>(k + 0) * 4);
                                uint32_t b = RwU32(bp + bOff + static_cast<size_t>(k + 1) * 4);
                                uint32_t c = RwU32(bp + bOff + static_cast<size_t>(k + 2) * 4);
                                if (a < outG.verts.size() && b < outG.verts.size() && c < outG.verts.size() &&
                                    a != b && b != c && c != a) {
                                    binMeshTris.push_back({ a, b, c });
                                }
                            }
                        } else {
                            for (uint32_t k = 0; k + 2 < splitIdxCnt; ++k) {
                                uint32_t a = RwU32(bp + bOff + static_cast<size_t>(k + 0) * 4);
                                uint32_t b = RwU32(bp + bOff + static_cast<size_t>(k + 1) * 4);
                                uint32_t c = RwU32(bp + bOff + static_cast<size_t>(k + 2) * 4);
                                if (a < outG.verts.size() && b < outG.verts.size() && c < outG.verts.size() &&
                                    a != b && b != c && c != a) {
                                    if ((k & 1u) == 0) binMeshTris.push_back({ a, b, c });
                                    else               binMeshTris.push_back({ a, c, b });
                                }
                            }
                        }
                        bOff += static_cast<size_t>(splitIdxCnt) * 4;
                    }
                }
            }
        }
    }

    outG.tris = (!structTris.empty() && !(flags & 1u)) ? std::move(structTris) :
                (!binMeshTris.empty() ? std::move(binMeshTris) : std::move(structTris));
    return !outG.verts.empty() && !outG.tris.empty();
}

inline void EmitGeometryInstance(const RwParsedGeom& g, const RwMat4x3& worldXf, LoadedMesh3D& mesh) {
    uint32_t baseV = static_cast<uint32_t>(mesh.vertices.size());
    for (const auto& rv : g.verts) {
        float wx = 0.0f, wy = 0.0f, wz = 0.0f;
        worldXf.TransformPoint(rv.x, rv.y, rv.z, wx, wy, wz);
        mesh.vertices.push_back(RwZUpToViewport(wx, wy, wz));
    }
    for (const auto& t : g.tris) {
        mesh.tris.push_back({ baseV + t[0], baseV + t[2], baseV + t[1], {} });
    }
}

inline bool ParseRwClumpChunk(const uint8_t* data, size_t start, size_t end, LoadedMesh3D& mesh) {
    using namespace RwStreamDetail;
    size_t pos = start;
    std::vector<RwMat4x3> worldFrames;
    std::vector<RwParsedGeom> geoms;
    std::vector<std::pair<int32_t, int32_t>> atomics; // (frameIndex, geomIndex)

    while (pos + 12 <= end) {
        RwChunkHdr ch;
        if (!ReadRwChunk(data, end, pos, ch)) return false;

        if (ch.type == 0x0Eu) { // Frame List
            size_t fPos = ch.payloadOffset;
            RwChunkHdr st;
            if (ReadRwChunk(data, ch.endOffset, fPos, st) && st.type == 0x01u && st.size >= 4) {
                int32_t nFrames = RwI32(data + st.payloadOffset);
                if (nFrames > 0 && nFrames <= 65536 &&
                    static_cast<uint64_t>(nFrames) * 56ULL + 4ULL <= st.size)
                {
                    std::vector<RwMat4x3> local(static_cast<size_t>(nFrames));
                    std::vector<int32_t> parents(static_cast<size_t>(nFrames), -1);
                    for (int32_t i = 0; i < nFrames; ++i) {
                        const uint8_t* fp = data + st.payloadOffset + 4 + static_cast<size_t>(i) * 56;
                        RwMat4x3& m = local[static_cast<size_t>(i)];
                        m.rx = RwF32(fp + 0);  m.ry = RwF32(fp + 4);  m.rz = RwF32(fp + 8);
                        m.ux = RwF32(fp + 12); m.uy = RwF32(fp + 16); m.uz = RwF32(fp + 20);
                        m.ax = RwF32(fp + 24); m.ay = RwF32(fp + 28); m.az = RwF32(fp + 32);
                        m.px = RwF32(fp + 36); m.py = RwF32(fp + 40); m.pz = RwF32(fp + 44);
                        parents[static_cast<size_t>(i)] = RwI32(fp + 48);
                    }
                    worldFrames.resize(static_cast<size_t>(nFrames));
                    for (int32_t i = 0; i < nFrames; ++i) {
                        RwMat4x3 cur = local[static_cast<size_t>(i)];
                        int32_t p = parents[static_cast<size_t>(i)];
                        int guard = 0;
                        while (p >= 0 && p < nFrames && p != i && guard++ < 128) {
                            cur = RwMat4x3::Multiply(cur, local[static_cast<size_t>(p)]);
                            p = parents[static_cast<size_t>(p)];
                        }
                        worldFrames[static_cast<size_t>(i)] = cur;
                    }
                }
            }
        } else if (ch.type == 0x1Au) { // Geometry List
            size_t gPos = ch.payloadOffset;
            RwChunkHdr st;
            if (!ReadRwChunk(data, ch.endOffset, gPos, st) || st.type != 0x01u || st.size < 4) return false;
            int32_t nGeoms = RwI32(data + st.payloadOffset);
            if (nGeoms < 0 || nGeoms > 65536) return false;
            while (gPos + 12 <= ch.endOffset) {
                RwChunkHdr gch;
                if (!ReadRwChunk(data, ch.endOffset, gPos, gch)) return false;
                if (gch.type == 0x0Fu) { // Geometry
                    RwParsedGeom pg;
                    if (ParseRwGeometryChunk(data, gch.payloadOffset, gch.endOffset, gch.stamp, pg)) {
                        geoms.push_back(std::move(pg));
                    }
                }
            }
        } else if (ch.type == 0x14u) { // Atomic
            size_t aPos = ch.payloadOffset;
            RwChunkHdr st;
            if (ReadRwChunk(data, ch.endOffset, aPos, st) && st.type == 0x01u && st.size >= 16) {
                int32_t fIdx = RwI32(data + st.payloadOffset + 0);
                int32_t gIdx = RwI32(data + st.payloadOffset + 4);
                atomics.push_back({ fIdx, gIdx });
            }
        }
    }

    size_t trisBefore = mesh.tris.size();
    if (!atomics.empty() && !geoms.empty()) {
        for (const auto& at : atomics) {
            if (at.second >= 0 && static_cast<size_t>(at.second) < geoms.size()) {
                RwMat4x3 xf;
                if (at.first >= 0 && static_cast<size_t>(at.first) < worldFrames.size()) {
                    xf = worldFrames[static_cast<size_t>(at.first)];
                }
                EmitGeometryInstance(geoms[static_cast<size_t>(at.second)], xf, mesh);
            }
        }
    }
    if (mesh.tris.size() == trisBefore) {
        RwMat4x3 ident;
        for (const auto& g : geoms) {
            EmitGeometryInstance(g, ident, mesh);
        }
    }
    return mesh.tris.size() > trisBefore;
}

inline bool ParseRwWorldSectorRecursive(const uint8_t* data, size_t start, size_t end, uint32_t worldFlags, LoadedMesh3D& mesh, int depth) {
    using namespace RwStreamDetail;
    if (depth > 64) return false;
    size_t pos = start;
    bool any = false;

    while (pos + 12 <= end) {
        RwChunkHdr ch;
        if (!ReadRwChunk(data, end, pos, ch)) break;

        if (ch.type == 0x09u) { // rwID_ATOMICSECT
            size_t sPos = ch.payloadOffset;
            RwChunkHdr st;
            if (!ReadRwChunk(data, ch.endOffset, sPos, st) || st.type != 0x01u || st.size < 44) continue;
            const uint8_t* sp = data + st.payloadOffset;
            int32_t numTris  = RwI32(sp + 4);
            int32_t numVerts = RwI32(sp + 8);
            if (numVerts <= 0 || numVerts > 1000000 || numTris <= 0 || numTris > 2000000) continue;

            size_t off = 44; // matListWindowBase(4) + numTris(4) + numVerts(4) + bbox(24) + collPresent(4) + unused(4)
            uint64_t vBytes = static_cast<uint64_t>(numVerts) * 12ULL;
            if (vBytes > st.size - off) continue;

            uint32_t baseV = static_cast<uint32_t>(mesh.vertices.size());
            bool badVert = false;
            for (int32_t i = 0; i < numVerts; ++i) {
                float x = RwF32(sp + off + static_cast<size_t>(i) * 12 + 0);
                float y = RwF32(sp + off + static_cast<size_t>(i) * 12 + 4);
                float z = RwF32(sp + off + static_cast<size_t>(i) * 12 + 8);
                if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) { badVert = true; break; }
                mesh.vertices.push_back(RwZUpToViewport(x, y, z));
            }
            if (badVert) return false;
            off += static_cast<size_t>(vBytes);

            if (worldFlags & 0x10u) { // rpWORLDNORMALS (4-byte packed normal per vertex)
                uint64_t nBytes = static_cast<uint64_t>(numVerts) * 4ULL;
                if (nBytes <= st.size - off) off += static_cast<size_t>(nBytes);
            }
            if (worldFlags & 0x08u) { // rpWORLDPRELIT
                uint64_t cBytes = static_cast<uint64_t>(numVerts) * 4ULL;
                if (cBytes <= st.size - off) off += static_cast<size_t>(cBytes);
            }
            uint32_t uvSets = (worldFlags >> 16) & 0xFFu;
            if (uvSets == 0) {
                if (worldFlags & 0x80u) uvSets = 2;
                else if (worldFlags & 0x04u) uvSets = 1;
            }
            uint64_t uvBytes = static_cast<uint64_t>(uvSets) * static_cast<uint64_t>(numVerts) * 8ULL;
            if (uvBytes <= st.size - off) off += static_cast<size_t>(uvBytes);

            uint64_t tBytes = static_cast<uint64_t>(numTris) * 8ULL;
            if (tBytes > st.size - off) return false;
            for (int32_t t = 0; t < numTris; ++t) {
                const uint8_t* tp = sp + off + static_cast<size_t>(t) * 8;
                // RpPolygon in World Sector: uint16 matIndex, uint16 vertIndex[3] OR uint16 v1, v2, v3, matIndex
                // Standard RW RpWorldSector stream polygon: uint16 v0, uint16 v1, uint16 v2, uint16 matId (when rwVer >= 3.4: matId or v0..v2)
                uint16_t a = RwU16(tp + 0);
                uint16_t b = RwU16(tp + 2);
                uint16_t c = RwU16(tp + 4);
                uint16_t d = RwU16(tp + 6);
                // Handle both RpVertexIndex[3]+matIndex and matIndex+RpVertexIndex[3]
                if (a >= static_cast<uint32_t>(numVerts) || b >= static_cast<uint32_t>(numVerts) || c >= static_cast<uint32_t>(numVerts)) {
                    a = b; b = c; c = d;
                }
                if (a >= static_cast<uint32_t>(numVerts) || b >= static_cast<uint32_t>(numVerts) || c >= static_cast<uint32_t>(numVerts)) {
                    return false;
                }
                if (a != b && b != c && c != a) {
                    mesh.tris.push_back({ baseV + a, baseV + c, baseV + b, {} });
                    any = true;
                }
            }
        } else if (ch.type == 0x0Au) { // rwID_PLANESECT (first child is Struct 24B, followed by left & right sectors)
            size_t pPos = ch.payloadOffset;
            RwChunkHdr st;
            if (ReadRwChunk(data, ch.endOffset, pPos, st) && st.type == 0x01u) {
                if (ParseRwWorldSectorRecursive(data, pPos, ch.endOffset, worldFlags, mesh, depth + 1)) {
                    any = true;
                }
            }
        }
    }
    return any;
}

inline bool ParseRwWorldChunk(const uint8_t* data, size_t start, size_t end, LoadedMesh3D& mesh) {
    using namespace RwStreamDetail;
    size_t pos = start;
    RwChunkHdr st;
    if (!ReadRwChunk(data, end, pos, st) || st.type != 0x01u || st.size < 52) return false;
    // RpWorld Struct: rootIsWorldSector(4), invOrigin(12), surfaceProps/numTris..., flags at +48 (or +36 in RW 3.4+)
    uint32_t worldFlags = RwU32(data + st.payloadOffset + (st.size >= 56 ? 52 : 48));
    return ParseRwWorldSectorRecursive(data, pos, end, worldFlags, mesh, 0);
}

} // namespace Rw3DDetail

inline bool ParseRenderWareDffOrRws(const uint8_t* data, size_t size, const std::wstring& ext, LoadedMesh3D& mesh) {
    using namespace RwStreamDetail;
    if (!data || size < 24) return false;

    size_t pos = 0;
    size_t chunksParsed = 0;
    bool gotWorld = false;

    while (pos + 12 <= size) {
        RwChunkHdr top;
        if (!ReadRwChunk(data, size, pos, top)) {
            if (chunksParsed == 0) return false;
            break;
        }
        ++chunksParsed;

        if (top.type == 0x10u) { // rwID_CLUMP
            Rw3DDetail::ParseRwClumpChunk(data, top.payloadOffset, top.endOffset, mesh);
        } else if (top.type == 0x0Bu) { // rwID_WORLD
            if (Rw3DDetail::ParseRwWorldChunk(data, top.payloadOffset, top.endOffset, mesh)) {
                gotWorld = true;
            }
        } else if (top.type == 0x0Fu) { // Standalone rwID_GEOMETRY
            Rw3DDetail::RwParsedGeom pg;
            if (Rw3DDetail::ParseRwGeometryChunk(data, top.payloadOffset, top.endOffset, top.stamp, pg)) {
                Rw3DDetail::RwMat4x3 ident;
                Rw3DDetail::EmitGeometryInstance(pg, ident, mesh);
            }
        } else if (top.size >= 24) {
            // RWS stream container wrapping nested Clump / World chunks
            size_t inner = top.payloadOffset;
            while (inner + 12 <= top.endOffset) {
                RwChunkHdr sub;
                if (!ReadRwChunk(data, top.endOffset, inner, sub)) break;
                if (sub.type == 0x10u) {
                    Rw3DDetail::ParseRwClumpChunk(data, sub.payloadOffset, sub.endOffset, mesh);
                } else if (sub.type == 0x0Bu) {
                    if (Rw3DDetail::ParseRwWorldChunk(data, sub.payloadOffset, sub.endOffset, mesh)) gotWorld = true;
                }
            }
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    if (ext == L".rws" || gotWorld) {
        mesh.formatName = L"RenderWare World / Stream (.rws)";
    } else {
        mesh.formatName = L"RenderWare 3D Clump (.dff)";
    }
    return true;
}

#endif // E2_RW_ENABLE_3D

#if defined(E2_RW_ENABLE_TXD)

inline bool DecodeRenderWareTxd(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    using namespace RwStreamDetail;
    if (!data || size < 36) return false;

    size_t pos = 0;
    RwChunkHdr dictHdr;
    if (!ReadRwChunk(data, size, pos, dictHdr) || dictHdr.type != 0x16u || dictHdr.size < 24) {
        return false; // Must start with rwID_TEXDICTIONARY (0x16)
    }

    size_t cPos = dictHdr.payloadOffset;
    RwChunkHdr dictSt;
    if (!ReadRwChunk(data, dictHdr.endOffset, cPos, dictSt) || dictSt.type != 0x01u || dictSt.size < 4) {
        return false;
    }
    uint16_t declaredTexCount = RwU16(data + dictSt.payloadOffset);
    if (declaredTexCount == 0) return false;

    bool decodedPrimary = false;
    while (cPos + 12 <= dictHdr.endOffset) {
        RwChunkHdr ch;
        if (!ReadRwChunk(data, dictHdr.endOffset, cPos, ch)) break;
        if (ch.type != 0x15u) continue; // rwID_TEXTURENATIVE

        size_t nPos = ch.payloadOffset;
        RwChunkHdr st;
        if (!ReadRwChunk(data, ch.endOffset, nPos, st) || st.type != 0x01u || st.size < 92) continue;

        const uint8_t* sp = data + st.payloadOffset;
        const size_t sLen = st.size;
        uint32_t platformId = RwU32(sp + 0);
        if (platformId != 8u && platformId != 9u && platformId != 1u && platformId != 2u) {
            continue;
        }

        char texNameBuf[33] = {};
        std::memcpy(texNameBuf, sp + 8, 32);
        texNameBuf[32] = '\0';
        std::string tn(texNameBuf);
        if (!tn.empty()) {
            outInfo.layerNames.push_back(PathUtils::Utf8ToWide(tn));
        }
        if (decodedPrimary) continue;

        uint32_t rasterFmt = RwU32(sp + 72);
        uint32_t d3dOrAlpha = RwU32(sp + 76);
        uint16_t w = RwU16(sp + 80);
        uint16_t h = RwU16(sp + 82);
        uint8_t depth = sp[84];
        uint8_t numLevels = sp[85];
        uint8_t compOrFlags = sp[87];

        if (w == 0 || h == 0 || w > kMaxRasterDimension || h > kMaxRasterDimension || numLevels == 0) {
            return false;
        }

        size_t pOff = 88;
        const uint8_t* palRgba = nullptr;
        size_t palEntries = 0;
        if (rasterFmt & 0x2000u) { // rwRASTERFORMATPAL8
            if (pOff + 1024 > sLen) return false;
            palRgba = sp + pOff;
            palEntries = 256;
            pOff += 1024;
        } else if (rasterFmt & 0x4000u) { // rwRASTERFORMATPAL4
            size_t palBytes = (depth == 4) ? 64u : 128u;
            if (pOff + palBytes > sLen) return false;
            palRgba = sp + pOff;
            palEntries = palBytes / 4;
            pOff += palBytes;
        }

        if (pOff + 4 > sLen) return false;
        uint32_t mip0Size = RwU32(sp + pOff);
        pOff += 4;
        if (mip0Size == 0 || mip0Size > sLen - pOff) return false;
        const uint8_t* mip0 = sp + pOff;

        // Determine compression: DXT1 (1), DXT3 (3), DXT5 (5), or 0 (uncompressed/paletted)
        int dxtMode = 0;
        if (platformId == 9u) {
            if (std::memcmp(&d3dOrAlpha, "DXT1", 4) == 0) dxtMode = 1;
            else if (std::memcmp(&d3dOrAlpha, "DXT2", 4) == 0 || std::memcmp(&d3dOrAlpha, "DXT3", 4) == 0) dxtMode = 3;
            else if (std::memcmp(&d3dOrAlpha, "DXT4", 4) == 0 || std::memcmp(&d3dOrAlpha, "DXT5", 4) == 0) dxtMode = 5;
            else if (compOrFlags & 0x08u) {
                if (d3dOrAlpha == 1u) dxtMode = 1;
                else if (d3dOrAlpha == 3u) dxtMode = 3;
                else if (d3dOrAlpha == 5u) dxtMode = 5;
            }
        } else if (platformId == 8u) {
            if (compOrFlags == 1u) dxtMode = 1;
            else if (compOrFlags == 2u || compOrFlags == 3u) dxtMode = 3;
            else if (compOrFlags == 4u || compOrFlags == 5u) dxtMode = 5;
        }

        outInfo.bgraPixels.assign(static_cast<size_t>(w) * h * 4, 0);
        const uint32_t pixFmt = rasterFmt & 0x0F00u;

        if (dxtMode != 0) {
            uint32_t bw = (w + 3u) / 4u;
            uint32_t bh = (h + 3u) / 4u;
            size_t blkSz = (dxtMode == 1) ? 8u : 16u;
            if (static_cast<uint64_t>(bw) * bh * blkSz > mip0Size) return false;
            bool opaqueDxt1 = (dxtMode == 1) && ((platformId == 8u) ? (d3dOrAlpha == 0) : ((compOrFlags & 0x01u) == 0 && pixFmt == 0x0600u));
            uint8_t blockRgba[64];
            size_t bOff = 0;
            for (uint32_t by = 0; by < bh; ++by) {
                for (uint32_t bx = 0; bx < bw; ++bx) {
                    if (dxtMode == 1) GpuTex::DecodeBC1Block(mip0 + bOff, blockRgba, true, opaqueDxt1);
                    else if (dxtMode == 3) GpuTex::DecodeBC2Block(mip0 + bOff, blockRgba);
                    else GpuTex::DecodeBC3Block(mip0 + bOff, blockRgba);
                    GpuTex::BlitBlock4x4RgbaToPremulBgra(blockRgba, bx, by, w, h, outInfo.bgraPixels);
                    bOff += blkSz;
                }
            }
            outInfo.channels = 4;
        } else if (palRgba != nullptr) {
            size_t count = static_cast<size_t>(w) * h;
            bool hasAlpha = (pixFmt == 0x0500u || pixFmt == 0x0100u || pixFmt == 0x0300u);
            if (depth == 4 && mip0Size >= (count + 1) / 2 && mip0Size < count) {
                for (size_t i = 0; i < count; ++i) {
                    uint8_t byteVal = mip0[i >> 1];
                    uint8_t idx = ((i & 1u) == 0) ? (byteVal & 0x0Fu) : (byteVal >> 4);
                    if (idx >= palEntries) idx = 0;
                    const uint8_t* pe = palRgba + static_cast<size_t>(idx) * 4;
                    WritePremulBGRA(&outInfo.bgraPixels[i * 4], pe[0], pe[1], pe[2], hasAlpha ? pe[3] : 255);
                }
            } else {
                if (mip0Size < count) return false;
                for (size_t i = 0; i < count; ++i) {
                    uint8_t idx = mip0[i];
                    if (idx >= palEntries) idx = 0;
                    const uint8_t* pe = palRgba + static_cast<size_t>(idx) * 4;
                    WritePremulBGRA(&outInfo.bgraPixels[i * 4], pe[0], pe[1], pe[2], hasAlpha ? pe[3] : 255);
                }
            }
            outInfo.channels = hasAlpha ? 4 : 3;
        } else if (pixFmt == 0x0500u || (pixFmt == 0x0600u && depth == 32)) { // 8888 BGRA or 888 32-bit BGRX
            size_t count = static_cast<size_t>(w) * h;
            if (mip0Size < count * 4) return false;
            bool useAlpha = (pixFmt == 0x0500u);
            for (size_t i = 0; i < count; ++i) {
                const uint8_t* spx = mip0 + i * 4;
                WritePremulBGRA(&outInfo.bgraPixels[i * 4], spx[2], spx[1], spx[0], useAlpha ? spx[3] : 255);
            }
            outInfo.channels = useAlpha ? 4 : 3;
        } else if (pixFmt == 0x0600u && depth == 24) { // 24-bit BGR
            size_t count = static_cast<size_t>(w) * h;
            if (mip0Size < count * 3) return false;
            for (size_t i = 0; i < count; ++i) {
                const uint8_t* spx = mip0 + i * 3;
                WritePremulBGRA(&outInfo.bgraPixels[i * 4], spx[2], spx[1], spx[0], 255);
            }
            outInfo.channels = 3;
        } else if (pixFmt == 0x0200u || pixFmt == 0x0100u || pixFmt == 0x0300u) { // 16-bit 565 / 1555 / 4444
            size_t count = static_cast<size_t>(w) * h;
            if (mip0Size < count * 2) return false;
            for (size_t i = 0; i < count; ++i) {
                uint16_t v = RwU16(mip0 + i * 2);
                uint8_t r = 0, g = 0, b = 0, a = 255;
                if (pixFmt == 0x0200u) {
                    GpuTex::UnpackRgb565(v, r, g, b);
                } else if (pixFmt == 0x0100u) {
                    r = static_cast<uint8_t>(((v >> 10) & 31u) * 255u / 31u);
                    g = static_cast<uint8_t>(((v >> 5)  & 31u) * 255u / 31u);
                    b = static_cast<uint8_t>((v         & 31u) * 255u / 31u);
                    a = (v & 0x8000u) ? 255 : 0;
                } else {
                    a = static_cast<uint8_t>(((v >> 12) & 15u) * 17u);
                    r = static_cast<uint8_t>(((v >> 8)  & 15u) * 17u);
                    g = static_cast<uint8_t>(((v >> 4)  & 15u) * 17u);
                    b = static_cast<uint8_t>((v         & 15u) * 17u);
                }
                WritePremulBGRA(&outInfo.bgraPixels[i * 4], r, g, b, a);
            }
            outInfo.channels = (pixFmt == 0x0200u) ? 3 : 4;
        } else if (pixFmt == 0x0400u) { // LUM8
            size_t count = static_cast<size_t>(w) * h;
            if (mip0Size < count) return false;
            for (size_t i = 0; i < count; ++i) {
                uint8_t l = mip0[i];
                WritePremulBGRA(&outInfo.bgraPixels[i * 4], l, l, l, 255);
            }
            outInfo.channels = 1;
        } else {
            return false;
        }

        outInfo.width = w;
        outInfo.height = h;
        outInfo.bitsPerChannel = 8;
        outInfo.formatName = (dxtMode == 1) ? L"RenderWare TXD (DXT1)" :
                             (dxtMode == 3) ? L"RenderWare TXD (DXT3)" :
                             (dxtMode == 5) ? L"RenderWare TXD (DXT5)" :
                             (palRgba != nullptr) ? L"RenderWare TXD (Paletted)" :
                                                    L"RenderWare TXD (.txd)";
        outInfo.valid = true;
        decodedPrimary = true;
    }

    return decodedPrimary && outInfo.valid;
}

#endif // E2_RW_ENABLE_TXD

} // namespace Formats
