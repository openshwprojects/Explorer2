#pragma once
// NintendoJ3dModel.h — Nintendo GameCube / Wii JSystem J3D 3D Model (.bmd / .bdl)
// Parses `J3D2bmd3` / `J3D2bdl4` / `J3D1bmd2` chunked containers (`VTX1` vertex arrays + `SHP1`
// shape descriptors & hardware GX display lists) via shared NintendoGxMeshCommon.h.

namespace Formats {

inline bool ParseNintendoJ3dModel(const uint8_t* data, size_t size, LoadedMesh3D& outMesh) {
    if (!data || size < 64u || size > (128u << 20)) return false;
    const bool isBmd = (std::memcmp(data, "J3D2bmd3", 8) == 0 ||
                        std::memcmp(data, "J3D2bmd2", 8) == 0 ||
                        std::memcmp(data, "J3D1bmd2", 8) == 0);
    const bool isBdl = (std::memcmp(data, "J3D2bdl4", 8) == 0 ||
                        std::memcmp(data, "J3D2bdl3", 8) == 0);
    if (!isBmd && !isBdl) return false;

    const uint32_t declSize   = ByteIO::RdU32BE(data + 8u);
    const uint32_t blockCount = ByteIO::RdU32BE(data + 12u);
    if (declSize < 64u || declSize > size || blockCount == 0u || blockCount > 32u) return false;

    const uint8_t* vtx1Ptr = nullptr;
    size_t vtx1Len = 0u;
    const uint8_t* shp1Ptr = nullptr;
    size_t shp1Len = 0u;

    size_t pos = 0x20u;
    for (uint32_t bi = 0u; bi < blockCount; ++bi) {
        if (declSize - pos < 8u) return false;
        const uint8_t* blk = data + pos;
        const uint32_t blkLen = ByteIO::RdU32BE(blk + 4u);
        if (blkLen < 8u || blkLen > declSize - pos) return false;

        if (std::memcmp(blk, "VTX1", 4) == 0) {
            vtx1Ptr = blk;
            vtx1Len = blkLen;
        } else if (std::memcmp(blk, "SHP1", 4) == 0) {
            shp1Ptr = blk;
            shp1Len = blkLen;
        }
        pos += blkLen;
    }

    if (!vtx1Ptr || vtx1Len < 0x40u || !shp1Ptr || shp1Len < 0x2Cu) return false;

    // 1. Decode VTX1 Position array
    const uint32_t fmtOff    = ByteIO::RdU32BE(vtx1Ptr + 0x08u);
    const uint32_t posArrOff = ByteIO::RdU32BE(vtx1Ptr + 0x0Cu);
    if (fmtOff < 0x40u || fmtOff >= vtx1Len || posArrOff < 0x40u || posArrOff >= vtx1Len) return false;

    uint32_t posCompCount = 1u; // GX_POS_XYZ
    uint32_t posCompType  = 4u; // GX_F32
    uint8_t  posFracShift = 0u;
    bool     foundPosFmt  = false;

    for (size_t fp = fmtOff; fp + 16u <= vtx1Len; fp += 16u) {
        const uint32_t attr = ByteIO::RdU32BE(vtx1Ptr + fp + 0u);
        if (attr == 0xFFu || attr == 0xFFFFFFFFu) break; // GX_VA_NULL
        if (attr == 9u) { // GX_VA_POS
            posCompCount = ByteIO::RdU32BE(vtx1Ptr + fp + 4u);
            posCompType  = ByteIO::RdU32BE(vtx1Ptr + fp + 8u);
            posFracShift = vtx1Ptr[fp + 12u];
            foundPosFmt  = true;
            break;
        }
    }
    if (!foundPosFmt) return false;

    // Determine end of Position array as min non-zero subsequent array offset in VTX1[0x10..0x3C], else vtx1Len
    size_t posArrEnd = vtx1Len;
    for (size_t ao = 0x10u; ao <= 0x3Cu; ao += 4u) {
        const uint32_t nextOff = ByteIO::RdU32BE(vtx1Ptr + ao);
        if (nextOff > posArrOff && nextOff < posArrEnd) {
            posArrEnd = nextOff;
        }
    }

    std::vector<Vec3f> positions;
    if (!NintendoGxMesh::DecodeGxPositionArrayBE(
            vtx1Ptr + posArrOff,
            posArrEnd - posArrOff,
            posCompType,
            posFracShift,
            posCompCount != 0u,
            positions)) {
        return false;
    }

    // 2. Decode SHP1 shapes and GX display lists
    const uint16_t shapeCount    = ByteIO::RdU16BE(shp1Ptr + 0x08u);
    const uint32_t shapeInitOff  = ByteIO::RdU32BE(shp1Ptr + 0x0Cu);
    const uint32_t vtxDescOff    = ByteIO::RdU32BE(shp1Ptr + 0x14u);
    const uint32_t dlDataBaseOff = ByteIO::RdU32BE(shp1Ptr + 0x20u);
    const uint32_t pktLocOff     = ByteIO::RdU32BE(shp1Ptr + 0x28u);

    if (shapeCount == 0u || shapeCount > 8192u) return false;
    if (shapeInitOff >= shp1Len || static_cast<uint64_t>(shapeCount) * 0x28ULL > shp1Len - shapeInitOff) return false;
    if (vtxDescOff >= shp1Len || dlDataBaseOff >= shp1Len || pktLocOff >= shp1Len) return false;

    outMesh = LoadedMesh3D{};
    outMesh.vertices = positions;

    for (uint16_t si = 0u; si < shapeCount; ++si) {
        const uint8_t* sh = shp1Ptr + shapeInitOff + static_cast<size_t>(si) * 0x28u;
        const uint16_t mtxGroupCount = ByteIO::RdU16BE(sh + 0x02u);
        const uint32_t vtxDescByteOff = ByteIO::RdU16BE(sh + 0x04u);
        const uint16_t firstPktIdx   = ByteIO::RdU16BE(sh + 0x0Au);

        if (vtxDescByteOff >= shp1Len - vtxDescOff) { outMesh = {}; return false; }

        NintendoGxMesh::GxVtxStreamLayout layout{};
        size_t dp = vtxDescOff + vtxDescByteOff;
        bool sawNullTerm = false;
        while (dp + 8u <= shp1Len) {
            const uint32_t attr    = ByteIO::RdU32BE(shp1Ptr + dp + 0u);
            const uint32_t idxType = ByteIO::RdU32BE(shp1Ptr + dp + 4u);
            dp += 8u;
            if (attr == 0xFFu || attr == 0xFFFFFFFFu) {
                sawNullTerm = true;
                break;
            }
            if (idxType == 0u) continue; // GX_NONE
            size_t fieldBytes = 0u;
            if (idxType == 1u) {
                // GX_DIRECT (used for PNMTXIDX / TEXMTXIDX = 1 byte)
                fieldBytes = 1u;
            } else if (idxType == 2u) {
                fieldBytes = 1u; // GX_INDEX8
            } else if (idxType == 3u) {
                fieldBytes = 2u; // GX_INDEX16
            } else {
                outMesh = {};
                return false;
            }
            if (attr == 9u) { // GX_VA_POS
                if (idxType != 2u && idxType != 3u) { outMesh = {}; return false; }
                layout.hasPos       = true;
                layout.posIdxOffset = layout.vertexStride;
                layout.posIdx16Bit  = (idxType == 3u);
            }
            layout.vertexStride += fieldBytes;
        }
        if (!sawNullTerm || !layout.hasPos) { outMesh = {}; return false; }

        for (uint16_t pi = 0u; pi < mtxGroupCount; ++pi) {
            const uint64_t locEntryOff = static_cast<uint64_t>(pktLocOff) +
                                         static_cast<uint64_t>(firstPktIdx + pi) * 8ULL;
            if (locEntryOff + 8ULL > shp1Len) { outMesh = {}; return false; }
            const uint32_t pktDlSize = ByteIO::RdU32BE(shp1Ptr + static_cast<size_t>(locEntryOff) + 0u);
            const uint32_t pktDlRel  = ByteIO::RdU32BE(shp1Ptr + static_cast<size_t>(locEntryOff) + 4u);
            const uint64_t absDlOff  = static_cast<uint64_t>(dlDataBaseOff) + pktDlRel;
            if (absDlOff > shp1Len || pktDlSize > shp1Len - static_cast<size_t>(absDlOff)) {
                outMesh = {};
                return false;
            }
            if (!NintendoGxMesh::DecodeGxDisplayList(
                    shp1Ptr + static_cast<size_t>(absDlOff),
                    pktDlSize,
                    layout,
                    outMesh.vertices.size(),
                    0u,
                    outMesh.tris)) {
                outMesh = {};
                return false;
            }
        }
    }

    if (outMesh.tris.empty()) {
        outMesh = {};
        return false;
    }

    // Trim trailing 32-byte chunk alignment padding slots beyond the highest referenced vertex index
    uint32_t maxUsedIdx = 0u;
    for (const auto& t : outMesh.tris) {
        maxUsedIdx = std::max({ maxUsedIdx, t.i0, t.i1, t.i2 });
    }
    if (static_cast<size_t>(maxUsedIdx) + 1u < outMesh.vertices.size()) {
        outMesh.vertices.resize(static_cast<size_t>(maxUsedIdx) + 1u);
    }

    outMesh.formatName = isBdl
        ? L"Nintendo JSystem J3D Display-List Model (.bdl)"
        : L"Nintendo JSystem J3D Model (.bmd)";
    return !outMesh.vertices.empty() && !outMesh.tris.empty();
}

} // namespace Formats
