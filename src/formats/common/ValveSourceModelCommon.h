#pragma once
// ValveSourceModelCommon.h — Shared Valve Source Engine (.mdl v44-49, .vvd v4, .vtx v7)
// LOD0 vertex fixup reconstruction, studiohdr_t mesh vertex-offset extraction, and
// StripGroup / StripHeader_t (STRIP_IS_TRILIST & STRIP_IS_TRISTRIP) topology decoding.
// Included inside namespace Explorer2.

namespace Formats {
namespace ValveSourceCommon {

struct MdlMeshSpan {
    size_t baseVert = 0;
    uint32_t numVerts = 0;
    bool active = false;
};

using MdlBodyModelMeshTable = std::vector<std::vector<std::vector<MdlMeshSpan>>>;

inline bool CheckedRelOffset(size_t base, int32_t rel, size_t limit, size_t& out) {
    int64_t v = static_cast<int64_t>(base) + static_cast<int64_t>(rel);
    if (v < 0 || static_cast<uint64_t>(v) > limit) return false;
    out = static_cast<size_t>(v);
    return true;
}

// Decodes Valve Source "IDSV" v4 .vvd LOD0 vertices (applying fixupTableStart when numFixups > 0)
// and converts right-handed Z-up StudioMDL coordinates (x, y, z) -> Y-up (x, z, -y).
inline bool DecodeValveVvdLod0(
    const uint8_t* data,
    size_t size,
    std::vector<Vec3f>& outVerts,
    int32_t& outChecksum)
{
    outVerts.clear();
    outChecksum = 0;
    if (!data || size < 64u + 48u * 3u) return false;
    if (::Explorer2::ByteIO::RdU32LE(data + 0u) != 0x56534449u) return false; // "IDSV"
    if (::Explorer2::ByteIO::RdI32LE(data + 4u) != 4) return false;

    const int32_t checksum  = ::Explorer2::ByteIO::RdI32LE(data + 8u);
    const int32_t numLODs   = ::Explorer2::ByteIO::RdI32LE(data + 12u);
    if (numLODs < 1 || numLODs > 8) return false;

    int32_t lodVerts[8] = {};
    for (int i = 0; i < 8; ++i) {
        lodVerts[i] = ::Explorer2::ByteIO::RdI32LE(data + 16u + static_cast<size_t>(i) * 4u);
        if (lodVerts[i] < 0 || lodVerts[i] > 2000000) return false;
    }
    if (lodVerts[0] < 3) return false;
    for (int i = 1; i < numLODs; ++i) {
        if (lodVerts[i] > lodVerts[i - 1]) return false;
    }

    const int32_t numFixups        = ::Explorer2::ByteIO::RdI32LE(data + 48u);
    const int32_t fixupTableStart  = ::Explorer2::ByteIO::RdI32LE(data + 52u);
    const int32_t vertexDataStart  = ::Explorer2::ByteIO::RdI32LE(data + 56u);
    const int32_t tangentDataStart = ::Explorer2::ByteIO::RdI32LE(data + 60u);

    if (numFixups < 0 || numFixups > 500000) return false;
    if (vertexDataStart < 64 || static_cast<size_t>(vertexDataStart) >= size) return false;

    size_t vertexEnd = size;
    if (tangentDataStart != 0) {
        if (tangentDataStart <= vertexDataStart || static_cast<size_t>(tangentDataStart) > size) return false;
        vertexEnd = static_cast<size_t>(tangentDataStart);
    }
    const size_t totalStoredVerts = (vertexEnd - static_cast<size_t>(vertexDataStart)) / 48u;
    if (totalStoredVerts < 3u) return false;
    if (tangentDataStart != 0) {
        const size_t tangBytes = size - static_cast<size_t>(tangentDataStart);
        if (tangBytes < totalStoredVerts * 16u) return false;
    }

    auto readPoolVert = [&](size_t idx, Vec3f& outPos) -> bool {
        if (idx >= totalStoredVerts) return false;
        const uint8_t* p = data + static_cast<size_t>(vertexDataStart) + idx * 48u;
        const uint8_t numBones = p[15];
        if (numBones > 3u) return false;
        for (uint8_t b = 0; b < numBones; ++b) {
            const float w = ::Explorer2::ByteIO::RdF32LE(p + static_cast<size_t>(b) * 4u);
            if (!std::isfinite(w) || w < -0.05f || w > 1.05f) return false;
        }
        const float px = ::Explorer2::ByteIO::RdF32LE(p + 16u);
        const float py = ::Explorer2::ByteIO::RdF32LE(p + 20u);
        const float pz = ::Explorer2::ByteIO::RdF32LE(p + 24u);
        const float nx = ::Explorer2::ByteIO::RdF32LE(p + 28u);
        const float ny = ::Explorer2::ByteIO::RdF32LE(p + 32u);
        const float nz = ::Explorer2::ByteIO::RdF32LE(p + 36u);
        const float tu = ::Explorer2::ByteIO::RdF32LE(p + 40u);
        const float tv = ::Explorer2::ByteIO::RdF32LE(p + 44u);
        if (!std::isfinite(px) || !std::isfinite(py) || !std::isfinite(pz) ||
            !std::isfinite(nx) || !std::isfinite(ny) || !std::isfinite(nz) ||
            !std::isfinite(tu) || !std::isfinite(tv)) {
            return false;
        }
        outPos = { px, pz, -py };
        return true;
    };

    outVerts.reserve(static_cast<size_t>(lodVerts[0]));
    if (numFixups > 0) {
        if (fixupTableStart < 64) return false;
        const uint64_t fixEnd = static_cast<uint64_t>(fixupTableStart) + static_cast<uint64_t>(numFixups) * 12ULL;
        if (fixEnd > static_cast<uint64_t>(vertexDataStart) || fixEnd > size) return false;

        for (int32_t f = 0; f < numFixups; ++f) {
            const uint8_t* fp = data + static_cast<size_t>(fixupTableStart) + static_cast<size_t>(f) * 12u;
            const int32_t fLod  = ::Explorer2::ByteIO::RdI32LE(fp + 0u);
            const int32_t srcId = ::Explorer2::ByteIO::RdI32LE(fp + 4u);
            const int32_t count = ::Explorer2::ByteIO::RdI32LE(fp + 8u);
            if (fLod < 0 || fLod >= numLODs || srcId < 0 || count <= 0) return false;
            if (static_cast<uint64_t>(srcId) + static_cast<uint64_t>(count) > totalStoredVerts) return false;
            for (int32_t k = 0; k < count && outVerts.size() < static_cast<size_t>(lodVerts[0]); ++k) {
                Vec3f vp{};
                if (!readPoolVert(static_cast<size_t>(srcId + k), vp)) { outVerts.clear(); return false; }
                outVerts.push_back(vp);
            }
        }
    } else {
        if (static_cast<size_t>(lodVerts[0]) > totalStoredVerts) return false;
        for (int32_t i = 0; i < lodVerts[0]; ++i) {
            Vec3f vp{};
            if (!readPoolVert(static_cast<size_t>(i), vp)) { outVerts.clear(); return false; }
            outVerts.push_back(vp);
        }
    }

    if (outVerts.size() < 3u) {
        outVerts.clear();
        return false;
    }
    outChecksum = checksum;
    return true;
}

// Extracts per-(bodypart, model, mesh) base vertex offsets and vertex counts from a Source "IDST" v44..49 .mdl.
inline bool ExtractValveMdlMeshSpans(
    const uint8_t* mdl,
    size_t mdlSize,
    int32_t& outChecksum,
    uint32_t& outVersion,
    MdlBodyModelMeshTable& outTable)
{
    outTable.clear();
    outChecksum = 0;
    outVersion = 0;
    if (!mdl || mdlSize < 244u || std::memcmp(mdl, "IDST", 4) != 0) return false;
    const uint32_t ver = ::Explorer2::ByteIO::RdU32LE(mdl + 4u);
    if (ver < 44u || ver > 53u) return false;

    const int32_t checksum = ::Explorer2::ByteIO::RdI32LE(mdl + 8u);
    const int32_t mdlNumBp = ::Explorer2::ByteIO::RdI32LE(mdl + 232u);
    const int32_t mdlBpOff = ::Explorer2::ByteIO::RdI32LE(mdl + 236u);
    if (mdlNumBp <= 0 || mdlNumBp > 256 || mdlBpOff < 240 ||
        static_cast<uint64_t>(mdlBpOff) + static_cast<uint64_t>(mdlNumBp) * 16ULL > mdlSize) {
        return false;
    }

    outTable.resize(static_cast<size_t>(mdlNumBp));
    for (int32_t bp = 0; bp < mdlNumBp; ++bp) {
        const size_t bpPos = static_cast<size_t>(mdlBpOff) + static_cast<size_t>(bp) * 16u;
        const int32_t numModels = ::Explorer2::ByteIO::RdI32LE(mdl + bpPos + 4u);
        const int32_t modelRel  = ::Explorer2::ByteIO::RdI32LE(mdl + bpPos + 12u);
        if (numModels < 0 || numModels > 256) return false;
        if (numModels == 0) continue;

        size_t modelsBase = 0;
        if (!CheckedRelOffset(bpPos, modelRel, mdlSize, modelsBase) ||
            modelsBase + static_cast<uint64_t>(numModels) * 148ULL > mdlSize) {
            return false;
        }
        outTable[static_cast<size_t>(bp)].resize(static_cast<size_t>(numModels));

        for (int32_t mi = 0; mi < numModels; ++mi) {
            const size_t mdlPos = modelsBase + static_cast<size_t>(mi) * 148u;
            const int32_t numMeshes   = ::Explorer2::ByteIO::RdI32LE(mdl + mdlPos + 72u);
            const int32_t meshRel     = ::Explorer2::ByteIO::RdI32LE(mdl + mdlPos + 76u);
            const int32_t mdlNumVerts = ::Explorer2::ByteIO::RdI32LE(mdl + mdlPos + 80u);
            const int32_t mdlVertIdx  = ::Explorer2::ByteIO::RdI32LE(mdl + mdlPos + 84u);
            if (numMeshes < 0 || numMeshes > 1024 || mdlVertIdx < 0) return false;
            if (numMeshes == 0 || mdlNumVerts <= 0) continue;

            size_t meshesBase = 0;
            if (!CheckedRelOffset(mdlPos, meshRel, mdlSize, meshesBase) ||
                meshesBase + static_cast<uint64_t>(numMeshes) * 116ULL > mdlSize) {
                return false;
            }
            const size_t modelBaseVert = ((mdlVertIdx % 48) == 0)
                ? static_cast<size_t>(mdlVertIdx / 48)
                : static_cast<size_t>(mdlVertIdx);

            auto& meshVec = outTable[static_cast<size_t>(bp)][static_cast<size_t>(mi)];
            meshVec.resize(static_cast<size_t>(numMeshes));
            for (int32_t m = 0; m < numMeshes; ++m) {
                const size_t meshPos = meshesBase + static_cast<size_t>(m) * 116u;
                const int32_t meshNumVerts   = ::Explorer2::ByteIO::RdI32LE(mdl + meshPos + 8u);
                const int32_t meshVertOffset = ::Explorer2::ByteIO::RdI32LE(mdl + meshPos + 12u);
                if (meshNumVerts <= 0 || meshVertOffset < 0) return false;
                meshVec[static_cast<size_t>(m)] = {
                    modelBaseVert + static_cast<size_t>(meshVertOffset),
                    static_cast<uint32_t>(meshNumVerts),
                    true
                };
            }
        }
    }

    outChecksum = checksum;
    outVersion = ver;
    return true;
}

// Decodes OptimizedModel::FileHeader_t v7 (.vtx / .dx90.vtx / .dx80.vtx / .sw.vtx) LOD0 topology,
// supporting both 25B and 33B StripGroupHeader_t, 27B and 35B StripHeader_t (STRIP_IS_TRILIST &
// STRIP_IS_TRISTRIP), strip-less StripGroup index lists, and optional .mdl mesh vertex-offset tables.
inline bool DecodeValveVtxV7Topology(
    const uint8_t* vtx,
    size_t vtxSize,
    int32_t expectedChecksum,
    size_t lod0VertCount,
    const MdlBodyModelMeshTable* mdlTable,
    uint32_t mdlVerHint,
    std::vector<MeshTriangle>& outTris)
{
    outTris.clear();
    if (!vtx || vtxSize < 36u) return false;
    if (::Explorer2::ByteIO::RdI32LE(vtx + 0u) != 7) return false;

    const int32_t vertCacheSize = ::Explorer2::ByteIO::RdI32LE(vtx + 4u);
    const uint16_t maxBonesStr  = ::Explorer2::ByteIO::RdU16LE(vtx + 8u);
    const int32_t maxBonesVert  = ::Explorer2::ByteIO::RdI32LE(vtx + 12u);
    const int32_t vtxChecksum   = ::Explorer2::ByteIO::RdI32LE(vtx + 16u);
    const int32_t vtxNumLODs    = ::Explorer2::ByteIO::RdI32LE(vtx + 20u);
    const int32_t vtxNumBp      = ::Explorer2::ByteIO::RdI32LE(vtx + 28u);
    const int32_t vtxBpOff      = ::Explorer2::ByteIO::RdI32LE(vtx + 32u);

    if (vtxChecksum != expectedChecksum) return false;
    if (vertCacheSize < 0 || maxBonesStr > 128u || maxBonesVert < 0 || maxBonesVert > 16) return false;
    if (vtxNumLODs < 1 || vtxNumLODs > 8 || vtxNumBp < 1 || vtxNumBp > 256 || vtxBpOff < 36) return false;
    if (static_cast<uint64_t>(vtxBpOff) + static_cast<uint64_t>(vtxNumBp) * 8ULL > vtxSize) return false;
    if (mdlTable && static_cast<size_t>(vtxNumBp) != mdlTable->size()) return false;

    size_t runningMeshVertBase = 0;

    for (int32_t bp = 0; bp < vtxNumBp; ++bp) {
        const size_t bpPos = static_cast<size_t>(vtxBpOff) + static_cast<size_t>(bp) * 8u;
        const int32_t numModels = ::Explorer2::ByteIO::RdI32LE(vtx + bpPos + 0u);
        const int32_t modelRel  = ::Explorer2::ByteIO::RdI32LE(vtx + bpPos + 4u);
        if (numModels < 0 || numModels > 256) { outTris.clear(); return false; }
        if (mdlTable && !(*mdlTable)[static_cast<size_t>(bp)].empty() &&
            static_cast<size_t>(numModels) != (*mdlTable)[static_cast<size_t>(bp)].size()) {
            outTris.clear();
            return false;
        }
        if (numModels == 0) continue;

        size_t modelsBase = 0;
        if (!CheckedRelOffset(bpPos, modelRel, vtxSize, modelsBase) ||
            modelsBase + static_cast<uint64_t>(numModels) * 8ULL > vtxSize) {
            outTris.clear();
            return false;
        }

        for (int32_t mi = 0; mi < numModels; ++mi) {
            const size_t mPos = modelsBase + static_cast<size_t>(mi) * 8u;
            const int32_t mNumLods = ::Explorer2::ByteIO::RdI32LE(vtx + mPos + 0u);
            const int32_t mLodRel  = ::Explorer2::ByteIO::RdI32LE(vtx + mPos + 4u);
            if (mdlTable) {
                const auto& mdlMeshes = (*mdlTable)[static_cast<size_t>(bp)][static_cast<size_t>(mi)];
                if (mdlMeshes.empty()) continue;
            }
            if (mNumLods < 1) {
                if (mdlTable) { outTris.clear(); return false; }
                continue;
            }
            size_t lod0Pos = 0;
            if (!CheckedRelOffset(mPos, mLodRel, vtxSize, lod0Pos) || lod0Pos + 12u > vtxSize) {
                outTris.clear();
                return false;
            }

            const int32_t numMeshes = ::Explorer2::ByteIO::RdI32LE(vtx + lod0Pos + 0u);
            const int32_t meshRel   = ::Explorer2::ByteIO::RdI32LE(vtx + lod0Pos + 4u);
            if (numMeshes < 0 || numMeshes > 1024) { outTris.clear(); return false; }
            if (mdlTable) {
                const auto& mdlMeshes = (*mdlTable)[static_cast<size_t>(bp)][static_cast<size_t>(mi)];
                if (static_cast<size_t>(numMeshes) != mdlMeshes.size()) {
                    outTris.clear();
                    return false;
                }
            }
            if (numMeshes == 0) continue;

            size_t meshesBase = 0;
            if (!CheckedRelOffset(lod0Pos, meshRel, vtxSize, meshesBase) ||
                meshesBase + static_cast<uint64_t>(numMeshes) * 9ULL > vtxSize) {
                outTris.clear();
                return false;
            }

            for (int32_t me = 0; me < numMeshes; ++me) {
                const size_t mePos = meshesBase + static_cast<size_t>(me) * 9u;
                const int32_t numStripGroups = ::Explorer2::ByteIO::RdI32LE(vtx + mePos + 0u);
                const int32_t sgRel          = ::Explorer2::ByteIO::RdI32LE(vtx + mePos + 4u);
                if (numStripGroups < 0 || numStripGroups > 4096) { outTris.clear(); return false; }
                if (numStripGroups == 0) continue;

                size_t sgBase = 0;
                if (!CheckedRelOffset(mePos, sgRel, vtxSize, sgBase) || sgBase + 25u > vtxSize) {
                    outTris.clear();
                    return false;
                }

                auto validateSgStride = [&](size_t stride) -> bool {
                    if (sgBase + static_cast<uint64_t>(numStripGroups) * stride > vtxSize) return false;
                    for (int32_t sg = 0; sg < numStripGroups; ++sg) {
                        const size_t p = sgBase + static_cast<size_t>(sg) * stride;
                        const int32_t nv = ::Explorer2::ByteIO::RdI32LE(vtx + p + 0u);
                        const int32_t vo = ::Explorer2::ByteIO::RdI32LE(vtx + p + 4u);
                        const int32_t ni = ::Explorer2::ByteIO::RdI32LE(vtx + p + 8u);
                        const int32_t io = ::Explorer2::ByteIO::RdI32LE(vtx + p + 12u);
                        const int32_t ns = ::Explorer2::ByteIO::RdI32LE(vtx + p + 16u);
                        const int32_t so = ::Explorer2::ByteIO::RdI32LE(vtx + p + 20u);
                        if (nv <= 0 || ni < 3 || ns < 0 || nv > 500000 || ni > 2000000 || ns > 65536) return false;
                        if (vo < static_cast<int32_t>(stride) || io < static_cast<int32_t>(stride)) return false;
                        size_t va = 0, ia = 0, sa = 0;
                        if (!CheckedRelOffset(p, vo, vtxSize, va) || va + static_cast<uint64_t>(nv) * 9ULL > vtxSize) return false;
                        if (!CheckedRelOffset(p, io, vtxSize, ia) || ia + static_cast<uint64_t>(ni) * 2ULL > vtxSize) return false;
                        if (ns > 0) {
                            if (so < static_cast<int32_t>(stride) || !CheckedRelOffset(p, so, vtxSize, sa) ||
                                sa + static_cast<uint64_t>(ns) * 27ULL > vtxSize) {
                                return false;
                            }
                        }
                    }
                    return true;
                };

                const int32_t sg0Vo = ::Explorer2::ByteIO::RdI32LE(vtx + sgBase + 4u);
                const int32_t sg0Io = ::Explorer2::ByteIO::RdI32LE(vtx + sgBase + 12u);
                const int32_t sg0Ns = ::Explorer2::ByteIO::RdI32LE(vtx + sgBase + 16u);
                const int32_t sg0So = ::Explorer2::ByteIO::RdI32LE(vtx + sgBase + 20u);
                const int32_t minSubOff = (sg0Ns > 0) ? std::min({ sg0Vo, sg0Io, sg0So }) : std::min(sg0Vo, sg0Io);

                size_t sgStride = 25u;
                if ((mdlVerHint >= 49u || sg0Vo == 33) && minSubOff >= numStripGroups * 33 && validateSgStride(33u)) {
                    sgStride = 33u;
                } else if (!validateSgStride(25u)) {
                    if (validateSgStride(33u)) sgStride = 33u;
                    else { outTris.clear(); return false; }
                }

                size_t meshBaseVert = runningMeshVertBase;
                uint32_t meshNumVertsLimit = 0xFFFFFFFFu;
                if (mdlTable) {
                    const MdlMeshSpan& sp = (*mdlTable)[static_cast<size_t>(bp)][static_cast<size_t>(mi)][static_cast<size_t>(me)];
                    if (!sp.active) continue;
                    meshBaseVert = sp.baseVert;
                    meshNumVertsLimit = sp.numVerts;
                }

                size_t meshMaxOrigVert = 0;

                for (int32_t sg = 0; sg < numStripGroups; ++sg) {
                    const size_t sgPos = sgBase + static_cast<size_t>(sg) * sgStride;
                    const int32_t sgNumVerts  = ::Explorer2::ByteIO::RdI32LE(vtx + sgPos + 0u);
                    const int32_t sgVertRel   = ::Explorer2::ByteIO::RdI32LE(vtx + sgPos + 4u);
                    const int32_t sgNumIdx    = ::Explorer2::ByteIO::RdI32LE(vtx + sgPos + 8u);
                    const int32_t sgIdxRel    = ::Explorer2::ByteIO::RdI32LE(vtx + sgPos + 12u);
                    const int32_t sgNumStrips = ::Explorer2::ByteIO::RdI32LE(vtx + sgPos + 16u);
                    const int32_t sgStripRel  = ::Explorer2::ByteIO::RdI32LE(vtx + sgPos + 20u);

                    const size_t sgVertBase = sgPos + static_cast<size_t>(sgVertRel);
                    const size_t sgIdxBase  = sgPos + static_cast<size_t>(sgIdxRel);

                    for (int32_t vi = 0; vi < sgNumVerts; ++vi) {
                        const uint16_t origId = ::Explorer2::ByteIO::RdU16LE(vtx + sgVertBase + static_cast<size_t>(vi) * 9u + 4u);
                        meshMaxOrigVert = std::max<size_t>(meshMaxOrigVert, static_cast<size_t>(origId) + 1u);
                    }

                    auto resolveElemIdx = [&](int32_t elemIdx, uint32_t& outVvdIdx) -> bool {
                        if (elemIdx < 0 || elemIdx >= sgNumIdx) return false;
                        const uint16_t sgVertIdx = ::Explorer2::ByteIO::RdU16LE(vtx + sgIdxBase + static_cast<size_t>(elemIdx) * 2u);
                        if (sgVertIdx >= static_cast<uint32_t>(sgNumVerts)) return false;
                        const uint16_t origVert = ::Explorer2::ByteIO::RdU16LE(vtx + sgVertBase + static_cast<size_t>(sgVertIdx) * 9u + 4u);
                        if (origVert >= meshNumVertsLimit) return false;
                        const size_t finalIdx = meshBaseVert + static_cast<size_t>(origVert);
                        if (finalIdx >= lod0VertCount) return false;
                        outVvdIdx = static_cast<uint32_t>(finalIdx);
                        return true;
                    };

                    if (sgNumStrips > 0) {
                        const size_t sgStrBase = sgPos + static_cast<size_t>(sgStripRel);
                        auto validateStripStride = [&](size_t stStride) -> bool {
                            if (sgStrBase + static_cast<uint64_t>(sgNumStrips) * stStride > vtxSize) return false;
                            for (int32_t st = 0; st < sgNumStrips; ++st) {
                                const size_t sp = sgStrBase + static_cast<size_t>(st) * stStride;
                                const int32_t sni = ::Explorer2::ByteIO::RdI32LE(vtx + sp + 0u);
                                const int32_t sio = ::Explorer2::ByteIO::RdI32LE(vtx + sp + 4u);
                                const uint8_t flg = vtx[sp + 18u];
                                if (sni < 0 || sio < 0 || sio + sni > sgNumIdx) return false;
                                if (sni > 0 && (flg & 0x03u) == 0) return false;
                            }
                            return true;
                        };

                        size_t stripStride = 27u;
                        if (sgStride == 33u && validateStripStride(35u)) {
                            stripStride = 35u;
                        } else if (!validateStripStride(27u)) {
                            if (validateStripStride(35u)) stripStride = 35u;
                            else { outTris.clear(); return false; }
                        }

                        for (int32_t st = 0; st < sgNumStrips; ++st) {
                            const size_t sp = sgStrBase + static_cast<size_t>(st) * stripStride;
                            const int32_t stNumIdx   = ::Explorer2::ByteIO::RdI32LE(vtx + sp + 0u);
                            const int32_t stIdxStart = ::Explorer2::ByteIO::RdI32LE(vtx + sp + 4u);
                            const uint8_t stFlags    = vtx[sp + 18u];

                            if ((stFlags & 0x01u) != 0) { // STRIP_IS_TRILIST
                                for (int32_t k = 0; k + 2 < stNumIdx; k += 3) {
                                    uint32_t i0 = 0, i1 = 0, i2 = 0;
                                    if (!resolveElemIdx(stIdxStart + k + 0, i0) ||
                                        !resolveElemIdx(stIdxStart + k + 1, i1) ||
                                        !resolveElemIdx(stIdxStart + k + 2, i2)) {
                                        outTris.clear();
                                        return false;
                                    }
                                    if (i0 != i1 && i1 != i2 && i0 != i2) {
                                        outTris.push_back({ i0, i2, i1, {} });
                                    }
                                }
                            } else if ((stFlags & 0x02u) != 0) { // STRIP_IS_TRISTRIP
                                for (int32_t k = 0; k + 2 < stNumIdx; ++k) {
                                    uint32_t i0 = 0, i1 = 0, i2 = 0;
                                    if (!resolveElemIdx(stIdxStart + k + 0, i0) ||
                                        !resolveElemIdx(stIdxStart + k + 1, i1) ||
                                        !resolveElemIdx(stIdxStart + k + 2, i2)) {
                                        outTris.clear();
                                        return false;
                                    }
                                    if (i0 != i1 && i1 != i2 && i0 != i2) {
                                        if (k & 1) outTris.push_back({ i0, i1, i2, {} });
                                        else       outTris.push_back({ i0, i2, i1, {} });
                                    }
                                }
                            }
                        }
                    } else {
                        // Strip-less StripGroupHeader_t: direct triangle list in index array
                        for (int32_t k = 0; k + 2 < sgNumIdx; k += 3) {
                            uint32_t i0 = 0, i1 = 0, i2 = 0;
                            if (!resolveElemIdx(k + 0, i0) ||
                                !resolveElemIdx(k + 1, i1) ||
                                !resolveElemIdx(k + 2, i2)) {
                                outTris.clear();
                                return false;
                            }
                            if (i0 != i1 && i1 != i2 && i0 != i2) {
                                outTris.push_back({ i0, i2, i1, {} });
                            }
                        }
                    }
                }

                if (!mdlTable) {
                    runningMeshVertBase += meshMaxOrigVert;
                }
            }
        }
    }

    return !outTris.empty();
}

} // namespace ValveSourceCommon
} // namespace Formats
