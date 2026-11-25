#pragma once
// CryEngineGeometry.h — CryEngine / Lumberyard / O3DE Geometry (.cgf, .chr, .skin, .cga).
#include "../common/MeshFormatCommon.h"

namespace Formats {

// 4. CryEngine / Lumberyard / O3DE Geometry (.cgf, .chr, .skin, .cga — "CryTek" v0x0744/0x0745 & "CrCh" v0x0746)
inline bool IsLikelyCryEngineContainer(const uint8_t* data, size_t size) {
    if (!data || size < 16) return false;
    if (std::memcmp(data, "CryTek", 6) == 0) return true;
    if (std::memcmp(data, "CrCh", 4) == 0) {
        uint32_t ver = CalCryDetail::RdU32(data + 4);
        return (ver == 0x0746u || ver == 0x0747u);
    }
    return false;
}

inline bool ParseCryEngineGeometry(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    LoadedMesh3D& mesh)
{
    using namespace CalCryDetail;
    if (!IsLikelyCryEngineContainer(data, size)) return false;

    struct CryChunk {
        uint32_t type = 0;
        uint32_t version = 0;
        uint32_t id = 0;
        uint32_t offset = 0;
        uint32_t byteSize = 0;
        bool hasInlineHeader = false;
    };

    std::vector<CryChunk> chunks;
    bool isCrCh = (std::memcmp(data, "CrCh", 4) == 0);
    uint32_t fileVer = 0;

    if (isCrCh) {
        fileVer = RdU32(data + 4);
        uint32_t numChunks = RdU32(data + 8);
        uint32_t tableOff  = RdU32(data + 12);
        if (numChunks == 0 || numChunks > 65536 || static_cast<uint64_t>(tableOff) + static_cast<uint64_t>(numChunks) * 16ULL > size) {
            return false;
        }
        chunks.reserve(numChunks);
        for (uint32_t i = 0; i < numChunks; ++i) {
            const uint8_t* cp = data + tableOff + i * 16u;
            CryChunk ch;
            ch.type     = RdU16(cp + 0);
            ch.version  = RdU16(cp + 2);
            ch.id       = RdU32(cp + 4);
            ch.byteSize = RdU32(cp + 8);
            ch.offset   = RdU32(cp + 12);
            ch.hasInlineHeader = false;
            if (ch.offset >= size || static_cast<uint64_t>(ch.offset) + ch.byteSize > size) return false;
            chunks.push_back(ch);
        }
    } else {
        if (size < 20) return false;
        fileVer = RdU32(data + 12);
        uint32_t tableOff = RdU32(data + 16);
        if ((fileVer != 0x0744u && fileVer != 0x0745u) || static_cast<uint64_t>(tableOff) + 4ULL > size) return false;
        uint32_t numChunks = RdU32(data + tableOff);
        size_t entrySz = (fileVer == 0x0745u) ? 20u : 16u;
        if (numChunks == 0 || numChunks > 65536 ||
            static_cast<uint64_t>(tableOff) + 4ULL + static_cast<uint64_t>(numChunks) * entrySz > size) {
            return false;
        }
        chunks.reserve(numChunks);
        for (uint32_t i = 0; i < numChunks; ++i) {
            const uint8_t* cp = data + tableOff + 4u + i * entrySz;
            CryChunk ch;
            ch.type    = RdU32(cp + 0);
            ch.version = RdU32(cp + 4);
            ch.offset  = RdU32(cp + 8);
            ch.id      = RdU32(cp + 12);
            ch.byteSize = (entrySz == 20u) ? RdU32(cp + 16) : 0u;
            ch.hasInlineHeader = true;
            if (ch.offset >= size) return false;
            chunks.push_back(ch);
        }
        // Compute inferred sizes for 0x0744 chunks sorted by offset
        for (size_t i = 0; i < chunks.size(); ++i) {
            if (chunks[i].byteSize == 0) {
                uint32_t nextOff = static_cast<uint32_t>(size);
                if (tableOff > chunks[i].offset && tableOff < nextOff) nextOff = tableOff;
                for (size_t j = 0; j < chunks.size(); ++j) {
                    if (chunks[j].offset > chunks[i].offset && chunks[j].offset < nextOff) {
                        nextOff = chunks[j].offset;
                    }
                }
                chunks[i].byteSize = nextOff - chunks[i].offset;
            }
            if (static_cast<uint64_t>(chunks[i].offset) + chunks[i].byteSize > size) return false;
        }
    }

    auto isMeshChunkType = [](uint32_t t) { return t == 0xCCCC0000u || t == 0x1000u; };
    auto isDataStreamType = [](uint32_t t) { return t == 0xCCCC0016u || t == 0x1016u; };

    // Index all DataStream chunks (0x0800 / 0x0801) by chunk ID
    struct ParsedPosStream { std::vector<Vec3f> verts; };
    struct ParsedIdxStream { std::vector<uint32_t> indices; };
    std::unordered_map<uint32_t, ParsedPosStream> posStreams;
    std::unordered_map<uint32_t, ParsedIdxStream> idxStreams;
    std::vector<uint32_t> posStreamOrder;
    std::vector<uint32_t> idxStreamOrder;

    for (const CryChunk& ch : chunks) {
        if (!isDataStreamType(ch.type)) continue;
        uint64_t chEnd = static_cast<uint64_t>(ch.offset) + ch.byteSize;
        if (chEnd > size) continue;
        uint64_t bodyOff64 = static_cast<uint64_t>(ch.offset) + (ch.hasInlineHeader ? 16ULL : 0ULL);
        if (bodyOff64 + 24ULL > chEnd) continue;
        size_t bodyOff = static_cast<size_t>(bodyOff64);
        const uint8_t* sp = data + bodyOff;
        /*uint32_t flags =*/ RdU32(sp + 0);
        uint32_t streamType = RdU32(sp + 4);
        uint32_t elemCount  = RdU32(sp + 8);
        uint32_t elemSize   = RdU32(sp + 12);
        // reserved[2] at +16..+24
        size_t payloadOff = bodyOff + 24u;
        uint64_t payloadBytes = static_cast<uint64_t>(elemCount) * elemSize;
        if (elemCount == 0 || elemCount > 4000000u || static_cast<uint64_t>(payloadOff) + payloadBytes > chEnd) continue;

        const uint8_t* pay = data + payloadOff;
        // CGF_STREAM_POSITIONS = 0 (float3 = 12B, or half4 = 8B), CGF_STREAM_P3S_C4B_T2S = 15 (half3/half4 + color + uv = 16B)
        if ((streamType == 0u && (elemSize == 12u || elemSize == 8u || elemSize == 16u)) ||
            (streamType == 15u && (elemSize == 16u || elemSize == 8u)))
        {
            ParsedPosStream ps;
            ps.verts.reserve(elemCount);
            bool ok = true;
            for (uint32_t i = 0; i < elemCount; ++i) {
                const uint8_t* ep = pay + static_cast<size_t>(i) * elemSize;
                float vx = 0.0f, vy = 0.0f, vz = 0.0f;
                if (streamType == 0u && elemSize >= 12u) {
                    vx = RdF32(ep + 0); vy = RdF32(ep + 4); vz = RdF32(ep + 8);
                } else {
                    vx = HalfToFloat(RdU16(ep + 0));
                    vy = HalfToFloat(RdU16(ep + 2));
                    vz = HalfToFloat(RdU16(ep + 4));
                }
                if (!std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(vz)) { ok = false; break; }
                // CryEngine Z-up meters -> Y-up viewport
                ps.verts.push_back({ vx, vz, -vy });
            }
            if (ok && !ps.verts.empty()) {
                posStreams[ch.id] = std::move(ps);
                posStreamOrder.push_back(ch.id);
            }
        } else if (streamType == 5u && (elemSize == 2u || elemSize == 4u)) {
            ParsedIdxStream is;
            is.indices.reserve(elemCount);
            for (uint32_t i = 0; i < elemCount; ++i) {
                uint32_t idx = (elemSize == 4u)
                    ? RdU32(pay + static_cast<size_t>(i) * 4u)
                    : static_cast<uint32_t>(RdU16(pay + static_cast<size_t>(i) * 2u));
                is.indices.push_back(idx);
            }
            idxStreams[ch.id] = std::move(is);
            idxStreamOrder.push_back(ch.id);
        }
    }

    mesh.vertices.clear();
    mesh.tris.clear();

    auto appendStreamPair = [&](const ParsedPosStream& ps, const ParsedIdxStream& is) -> bool {
        if (ps.verts.size() < 3 || is.indices.size() < 3) return false;
        uint32_t baseV = static_cast<uint32_t>(mesh.vertices.size());
        size_t addedTris = 0;
        for (size_t t = 0; t + 2 < is.indices.size(); t += 3) {
            uint32_t i0 = is.indices[t + 0];
            uint32_t i1 = is.indices[t + 1];
            uint32_t i2 = is.indices[t + 2];
            if (i0 >= ps.verts.size() || i1 >= ps.verts.size() || i2 >= ps.verts.size()) return false;
            if (i0 != i1 && i1 != i2 && i0 != i2) {
                mesh.tris.push_back({ baseV + i0, baseV + i1, baseV + i2, {} });
                ++addedTris;
            }
        }
        if (addedTris == 0) return false;
        mesh.vertices.insert(mesh.vertices.end(), ps.verts.begin(), ps.verts.end());
        return true;
    };

    // Match Mesh chunks (0x0800 / 0x0801 / 0x0802 stream references, or legacy 0x0744 inline CryVertex/CryFace)
    for (const CryChunk& ch : chunks) {
        if (!isMeshChunkType(ch.type)) continue;
        uint64_t chEnd = static_cast<uint64_t>(ch.offset) + ch.byteSize;
        if (chEnd > size) continue;
        uint64_t bodyOff64 = static_cast<uint64_t>(ch.offset) + (ch.hasInlineHeader ? 16ULL : 0ULL);
        if (bodyOff64 > chEnd) continue;
        size_t bodyOff = static_cast<size_t>(bodyOff64);
        size_t bodyLen = static_cast<size_t>(chEnd - bodyOff64);
        const uint8_t* mp = data + bodyOff;

        if (ch.version == 0x0744u && bodyLen >= 20u) {
            // Legacy Far Cry / CryEngine 1 MESH_CHUNK_DESC_0744:
            // flags(4) + nVerts(4) + nTVerts(4) + nFaces(4) + vertAnimId(4) + nVerts*24 (CryVertex) + nFaces*20 (CryFace)
            int32_t nVerts  = RdI32(mp + 4);
            int32_t nTVerts = RdI32(mp + 8);
            int32_t nFaces  = RdI32(mp + 12);
            (void)nTVerts;
            uint64_t vBytes = static_cast<uint64_t>(nVerts) * 24ULL;
            uint64_t fBytes = static_cast<uint64_t>(nFaces) * 20ULL;
            if (nVerts >= 3 && nVerts <= 2000000 && nFaces >= 1 && nFaces <= 4000000 &&
                20ULL + vBytes + fBytes <= bodyLen)
            {
                uint32_t baseV = static_cast<uint32_t>(mesh.vertices.size());
                const uint8_t* vp = mp + 20;
                for (int32_t v = 0; v < nVerts; ++v) {
                    float vx = RdF32(vp + static_cast<size_t>(v) * 24u + 0);
                    float vy = RdF32(vp + static_cast<size_t>(v) * 24u + 4);
                    float vz = RdF32(vp + static_cast<size_t>(v) * 24u + 8);
                    if (!std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(vz)) return false;
                    mesh.vertices.push_back({ vx, vz, -vy });
                }
                const uint8_t* fp = vp + vBytes;
                for (int32_t f = 0; f < nFaces; ++f) {
                    int32_t i0 = RdI32(fp + static_cast<size_t>(f) * 20u + 0);
                    int32_t i1 = RdI32(fp + static_cast<size_t>(f) * 20u + 4);
                    int32_t i2 = RdI32(fp + static_cast<size_t>(f) * 20u + 8);
                    if (i0 < 0 || i0 >= nVerts || i1 < 0 || i1 >= nVerts || i2 < 0 || i2 >= nVerts) return false;
                    if (i0 != i1 && i1 != i2 && i0 != i2) {
                        mesh.tris.push_back({
                            baseV + static_cast<uint32_t>(i0),
                            baseV + static_cast<uint32_t>(i1),
                            baseV + static_cast<uint32_t>(i2),
                            {}
                        });
                    }
                }
            }
        } else if (bodyLen >= 96u) {
            // MESH_CHUNK_DESC_0800 / 0801: flags1(4), flags2(4), nVerts(4), nIndices(4), nSubsets(4), subsetsChunkId(4),
            // vertAnimId(4), streamChunkID[16] (64B starting at +28)
            uint32_t posCid  = RdU32(mp + 28 + 0 * 4);
            uint32_t p3sCid  = RdU32(mp + 28 + 15 * 4);
            uint32_t idxCid  = RdU32(mp + 28 + 5 * 4);
            uint32_t usePosCid = (posStreams.find(posCid) != posStreams.end()) ? posCid : p3sCid;
            auto itP = posStreams.find(usePosCid);
            auto itI = idxStreams.find(idxCid);
            if (itP != posStreams.end() && itI != idxStreams.end()) {
                appendStreamPair(itP->second, itI->second);
            }
        }
    }

    // Fallback for streamlined 0x0802+ mesh chunks where streamChunkID[16][8] starts at a different offset:
    // pair validated DataStream position & index chunks in chunk order
    if (mesh.tris.empty() && !posStreamOrder.empty() && !idxStreamOrder.empty()) {
        size_t pairs = std::min(posStreamOrder.size(), idxStreamOrder.size());
        for (size_t k = 0; k < pairs; ++k) {
            appendStreamPair(posStreams[posStreamOrder[k]], idxStreams[idxStreamOrder[k]]);
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    if (extLower == L".chr")       mesh.formatName = L"CryEngine Skinned Character (.chr)";
    else if (extLower == L".skin") mesh.formatName = L"CryEngine Skinned Attachment (.skin)";
    else if (extLower == L".cga")  mesh.formatName = L"CryEngine Animated Geometry (.cga)";
    else                           mesh.formatName = isCrCh ? L"CryEngine V Geometry (.cgf CrCh)" : L"CryEngine Geometry (.cgf)";
    return true;
}

} // namespace Formats
