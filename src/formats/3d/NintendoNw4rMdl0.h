#pragma once
// NintendoNw4rMdl0.h — Nintendo Wii NW4R 3D Model Resource (.mdl0)
// Parses `MDL0` v8/v9/v10/v11 resource groups (`Vertices` section 2 + `Objects` section 8/10),
// decoding hardware CP vertex descriptors (`cpVtxLo`/`cpVtxHi`) & GX display lists via NintendoGxMeshCommon.h.

namespace Formats {

namespace DetailNw4rMdl0 {

inline bool DecodeCpVtxLayout(uint32_t cpLo, uint32_t cpHi, NintendoGxMesh::GxVtxStreamLayout& outLayout) {
    outLayout = {};
    size_t stride = 0u;

    // Bits 0..8 of cpLo: PosMtxIdx + Tex0..7MtxIdx (1 byte each if set)
    for (uint32_t bit = 0u; bit <= 8u; ++bit) {
        if ((cpLo & (1u << bit)) != 0u) stride += 1u;
    }

    auto fieldBytesFor2BitMode = [](uint32_t mode, size_t directBytes, size_t& outBytes) -> bool {
        if (mode == 0u) { outBytes = 0u; return true; } // GX_NONE
        if (mode == 1u) { outBytes = directBytes; return true; } // GX_DIRECT
        if (mode == 2u) { outBytes = 1u; return true; } // GX_INDEX8
        if (mode == 3u) { outBytes = 2u; return true; } // GX_INDEX16
        return false;
    };

    // Bits 9..10: Position (must be Index8 or Index16 in MDL0)
    const uint32_t posMode = (cpLo >> 9u) & 3u;
    if (posMode != 2u && posMode != 3u) return false;
    outLayout.hasPos       = true;
    outLayout.posIdxOffset = stride;
    outLayout.posIdx16Bit  = (posMode == 3u);
    stride += (posMode == 3u) ? 2u : 1u;

    // Bits 11..12: Normal, 13..14: Color0, 15..16: Color1
    for (uint32_t shift : { 11u, 13u, 15u }) {
        size_t fb = 0u;
        if (!fieldBytesFor2BitMode((cpLo >> shift) & 3u, 3u, fb)) return false;
        stride += fb;
    }

    // cpHi: Tex0..Tex7 (2 bits each, bits 0..15)
    for (uint32_t t = 0u; t < 8u; ++t) {
        size_t fb = 0u;
        if (!fieldBytesFor2BitMode((cpHi >> (t * 2u)) & 3u, 4u, fb)) return false;
        stride += fb;
    }

    outLayout.vertexStride = stride;
    return stride > 0u;
}

inline bool EnumResourceGroupOffsets(
    const uint8_t* data,
    size_t size,
    uint32_t groupOff,
    std::vector<size_t>& outEntryAbsOffsets)
{
    outEntryAbsOffsets.clear();
    if (groupOff == 0u || groupOff >= size || size - groupOff < 24u) return false;
    const uint32_t grpBytes   = ByteIO::RdU32BE(data + groupOff + 0u);
    const uint32_t numEntries = ByteIO::RdU32BE(data + groupOff + 4u);
    if (grpBytes < 24u || numEntries == 0u || numEntries > 4096u) return false;
    const uint64_t tableEnd = static_cast<uint64_t>(groupOff) + 8ULL + static_cast<uint64_t>(numEntries + 1u) * 16ULL;
    if (tableEnd > size) return false;

    outEntryAbsOffsets.reserve(numEntries);
    for (uint32_t i = 0u; i < numEntries; ++i) {
        const size_t nodeOff = static_cast<size_t>(groupOff) + 24u + static_cast<size_t>(i) * 16u;
        const int32_t relData = ByteIO::RdI32BE(data + nodeOff + 12u);
        if (relData <= 0) return false;
        const uint64_t absOff = static_cast<uint64_t>(groupOff) + static_cast<uint64_t>(relData);
        if (absOff >= size) return false;
        outEntryAbsOffsets.push_back(static_cast<size_t>(absOff));
    }
    return true;
}

} // namespace DetailNw4rMdl0

inline bool ParseNintendoNw4rMdl0(const uint8_t* data, size_t size, LoadedMesh3D& outMesh) {
    if (!data || size < 0x50u || size > (128u << 20)) return false;
    if (std::memcmp(data, "MDL0", 4) != 0) return false;

    const uint32_t declSize = ByteIO::RdU32BE(data + 4u);
    const uint32_t version  = ByteIO::RdU32BE(data + 8u);
    if (declSize < 0x50u || declSize > size) return false;
    if (version < 8u || version > 11u) return false;

    // Section index 2 is always Vertices; Objects (Polygons) is index 8 in v8/v9, index 10 in v10/v11
    const size_t vtxSecSlot = 2u;
    const size_t objSecSlot = (version >= 10u) ? 10u : 8u;
    const size_t hdrNeed    = 0x10u + (objSecSlot + 1u) * 4u;
    if (declSize < hdrNeed) return false;

    const int32_t vtxGrpRel = ByteIO::RdI32BE(data + 0x10u + vtxSecSlot * 4u);
    const int32_t objGrpRel = ByteIO::RdI32BE(data + 0x10u + objSecSlot * 4u);
    if (vtxGrpRel <= 0 || objGrpRel <= 0) return false;

    std::vector<size_t> vtxEntryOffsets;
    std::vector<size_t> objEntryOffsets;
    if (!DetailNw4rMdl0::EnumResourceGroupOffsets(data, declSize, static_cast<uint32_t>(vtxGrpRel), vtxEntryOffsets) ||
        !DetailNw4rMdl0::EnumResourceGroupOffsets(data, declSize, static_cast<uint32_t>(objGrpRel), objEntryOffsets)) {
        return false;
    }

    // 1. Decode each MDL0VertexData buffer into a contiguous mesh vertex pool
    struct VtxPoolSlice {
        uint32_t meshBase = 0u;
        size_t   count    = 0u;
    };
    std::unordered_map<int32_t, VtxPoolSlice> idToSlice;
    std::vector<VtxPoolSlice> indexToSlice;
    outMesh = LoadedMesh3D{};

    for (size_t vi = 0u; vi < vtxEntryOffsets.size(); ++vi) {
        const size_t vo = vtxEntryOffsets[vi];
        if (declSize - vo < 0x20u) { outMesh = {}; return false; }
        const uint32_t vBlkLen   = ByteIO::RdU32BE(data + vo + 0x00u);
        const int32_t  vDataRel  = ByteIO::RdI32BE(data + vo + 0x08u);
        const int32_t  vId       = ByteIO::RdI32BE(data + vo + 0x10u);
        const uint32_t isXYZ     = ByteIO::RdU32BE(data + vo + 0x14u);
        const uint32_t compType  = ByteIO::RdU32BE(data + vo + 0x18u);
        const uint8_t  fracShift = data[vo + 0x1Cu];
        const uint8_t  vStride   = data[vo + 0x1Du];
        const uint16_t numVerts  = ByteIO::RdU16BE(data + vo + 0x1Eu);

        if (vBlkLen < 0x20u || vBlkLen > declSize - vo || vDataRel < 0x20 || numVerts == 0u) {
            outMesh = {};
            return false;
        }
        const uint64_t rawStart = static_cast<uint64_t>(vo) + static_cast<uint64_t>(vDataRel);
        const uint64_t rawBytes = static_cast<uint64_t>(numVerts) * std::max<uint64_t>(vStride, 2ULL);
        if (rawStart >= declSize || rawBytes > declSize - static_cast<size_t>(rawStart)) {
            outMesh = {};
            return false;
        }

        std::vector<Vec3f> decodedPos;
        if (!NintendoGxMesh::DecodeGxPositionArrayBE(
                data + static_cast<size_t>(rawStart),
                static_cast<size_t>(rawBytes),
                compType,
                fracShift,
                isXYZ != 0u,
                decodedPos,
                vStride)) {
            outMesh = {};
            return false;
        }
        if (decodedPos.size() > numVerts) decodedPos.resize(numVerts);

        VtxPoolSlice sl{};
        sl.meshBase = static_cast<uint32_t>(outMesh.vertices.size());
        sl.count    = decodedPos.size();
        outMesh.vertices.insert(outMesh.vertices.end(), decodedPos.begin(), decodedPos.end());
        idToSlice[vId] = sl;
        indexToSlice.push_back(sl);
    }

    // 2. Decode each MDL0Object (Polygon) hardware CP descriptor + GX primitive display list
    for (size_t oi = 0u; oi < objEntryOffsets.size(); ++oi) {
        const size_t oo = objEntryOffsets[oi];
        if (declSize - oo < 0x44u) { outMesh = {}; return false; }
        const uint32_t objLen   = ByteIO::RdU32BE(data + oo + 0x00u);
        const uint32_t cpLo     = ByteIO::RdU32BE(data + oo + 0x18u);
        const uint32_t cpHi     = ByteIO::RdU32BE(data + oo + 0x1Cu);
        const uint32_t primSize = ByteIO::RdU32BE(data + oo + 0x24u);
        const int32_t  primRel  = ByteIO::RdI32BE(data + oo + 0x2Cu); // relative to +0x24
        const int16_t  vtxId    = ByteIO::RdI16BE(data + oo + 0x40u);

        if (objLen < 0x44u || objLen > declSize - oo || primSize == 0u || primRel <= 0) {
            outMesh = {};
            return false;
        }
        const uint64_t dlAbs = static_cast<uint64_t>(oo) + 0x24ULL + static_cast<uint64_t>(primRel);
        if (dlAbs > declSize || primSize > declSize - static_cast<size_t>(dlAbs)) {
            outMesh = {};
            return false;
        }

        NintendoGxMesh::GxVtxStreamLayout layout{};
        if (!DetailNw4rMdl0::DecodeCpVtxLayout(cpLo, cpHi, layout)) {
            outMesh = {};
            return false;
        }

        VtxPoolSlice slice{};
        auto it = idToSlice.find(static_cast<int32_t>(vtxId));
        if (it != idToSlice.end()) {
            slice = it->second;
        } else if (vtxId >= 0 && static_cast<size_t>(vtxId) < indexToSlice.size()) {
            slice = indexToSlice[static_cast<size_t>(vtxId)];
        } else {
            outMesh = {};
            return false;
        }

        if (!NintendoGxMesh::DecodeGxDisplayList(
                data + static_cast<size_t>(dlAbs),
                primSize,
                layout,
                slice.count,
                slice.meshBase,
                outMesh.tris)) {
            outMesh = {};
            return false;
        }
    }

    if (outMesh.vertices.empty() || outMesh.tris.empty()) {
        outMesh = {};
        return false;
    }

    wchar_t fmtBuf[96] = {};
    swprintf_s(fmtBuf, L"Nintendo Wii NW4R Model (.mdl0 v%u)", version);
    outMesh.formatName = fmtBuf;
    return !outMesh.vertices.empty() && !outMesh.tris.empty();
}

} // namespace Formats
