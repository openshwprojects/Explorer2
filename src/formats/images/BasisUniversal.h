#pragma once
// BasisUniversal.h — Basis Universal UASTC 4x4 & ETC1S Supercompressed Texture Decoder (.basis)

namespace Formats {

inline bool DecodeBasisUniversal(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data) return false;
    const std::wstring extLower = L".basis";
    // Basis Universal (.basis): magic 's','B' (0x4273 LE), 77-byte basis_file_header + 23-byte basis_slice_desc
    if (size < 100 || data[0] != 's' || data[1] != 'B') return false;
    uint16_t hdrVer        = DetailImg100::RdU16LE(data + 2);
    uint16_t hdrSize       = DetailImg100::RdU16LE(data + 4);
    uint16_t hdrCrc16      = DetailImg100::RdU16LE(data + 6);
    uint32_t dataSize      = DetailImg100::RdU32LE(data + 8);
    uint16_t dataCrc16     = DetailImg100::RdU16LE(data + 12);
    uint32_t totalSlices   = DetailImg100::RdU24LE(data + 14);
    uint32_t totalImages   = DetailImg100::RdU24LE(data + 17);
    uint8_t  texFormat     = data[20]; // 0 = cETC1S, 1 = cUASTC4x4
    uint16_t flags         = DetailImg100::RdU16LE(data + 21);
    uint8_t  texType       = data[23]; // 3 = cBASISTexTypeVideoFrames
    uint16_t numEndpoints  = DetailImg100::RdU16LE(data + 39);
    uint32_t epCbOfs       = DetailImg100::RdU32LE(data + 41);
    uint32_t epCbSize      = DetailImg100::RdU24LE(data + 45);
    uint16_t numSelectors  = DetailImg100::RdU16LE(data + 48);
    uint32_t selCbOfs      = DetailImg100::RdU32LE(data + 50);
    uint32_t selCbSize     = DetailImg100::RdU24LE(data + 54);
    uint32_t tablesOfs     = DetailImg100::RdU32LE(data + 57);
    uint32_t tablesSize    = DetailImg100::RdU32LE(data + 61);
    uint32_t sliceDescOfs  = DetailImg100::RdU32LE(data + 65);

    if (hdrVer == 0 || hdrSize < 77 || hdrSize > size || totalSlices == 0 || totalImages == 0 || dataSize == 0) {
        return false;
    }
    if (static_cast<uint64_t>(hdrSize) + dataSize > size || texFormat > 1u) {
        return false;
    }
    if (hdrCrc16 != 0 && DetailBasis::BasisCrc16(data + 8, hdrSize - 8u) != hdrCrc16) {
        return false;
    }
    if (dataCrc16 != 0 && DetailBasis::BasisCrc16(data + hdrSize, dataSize) != dataCrc16) {
        return false;
    }

    uint64_t sliceDescBytes = static_cast<uint64_t>(totalSlices) * 23ull;
    if (sliceDescOfs < hdrSize || sliceDescOfs > size || sliceDescBytes > size - sliceDescOfs) {
        return false;
    }

    // Pick first mip-0 RGB slice descriptor (23 bytes per basis_slice_desc)
    size_t chosenIdx = 0;
    for (size_t si = 0; si < totalSlices; ++si) {
        const uint8_t* sd = data + sliceDescOfs + si * 23u;
        if (sd[3] == 0u && (sd[4] & 0x01u) == 0u) { // m_level_index == 0 and not alpha slice
            chosenIdx = si;
            break;
        }
    }
    const uint8_t* chosenSlice = data + sliceDescOfs + chosenIdx * 23u;
    uint32_t chosenImgIdx = DetailImg100::RdU24LE(chosenSlice + 0);
    uint8_t  chosenLvlIdx = chosenSlice[3];
    uint32_t origW        = DetailImg100::RdU16LE(chosenSlice + 5);
    uint32_t origH        = DetailImg100::RdU16LE(chosenSlice + 7);
    uint32_t blocksX      = DetailImg100::RdU16LE(chosenSlice + 9);
    uint32_t blocksY      = DetailImg100::RdU16LE(chosenSlice + 11);
    uint32_t sliceOfs     = DetailImg100::RdU32LE(chosenSlice + 13);
    uint32_t sliceSize    = DetailImg100::RdU32LE(chosenSlice + 17);
    uint16_t sliceCrc16   = DetailImg100::RdU16LE(chosenSlice + 21);

    if (origW == 0 || origH == 0 || origW > 8192 || origH > 8192 || blocksX == 0 || blocksY == 0) {
        return false;
    }
    if (blocksX < (origW + 3u) / 4u || blocksY < (origH + 3u) / 4u) {
        return false;
    }
    if (sliceSize == 0 || sliceOfs < hdrSize || sliceOfs > size || sliceSize > size - sliceOfs) {
        return false;
    }
    if (sliceCrc16 != 0 && DetailBasis::BasisCrc16(data + sliceOfs, sliceSize) != sliceCrc16) {
        return false;
    }

    const uint8_t* sliceBytes = data + sliceOfs;
    size_t totalBlocks = static_cast<size_t>(blocksX) * blocksY;

    if (texFormat == 1u) { // cUASTC4x4 (16 bytes per 4x4 block)
        if (sliceSize < totalBlocks * 16u) return false;
        outInfo.bgraPixels.assign(static_cast<size_t>(origW) * origH * 4u, 0);
        uint8_t blkRgba[64];
        bool anyNonOpaque = false;
        for (uint32_t by = 0; by < blocksY; ++by) {
            for (uint32_t bx = 0; bx < blocksX; ++bx) {
                const uint8_t* blkPtr = sliceBytes + (static_cast<size_t>(by) * blocksX + bx) * 16u;
                if (!DetailBasis::DecodeUastcBlock4x4(blkPtr, blkRgba)) {
                    outInfo.bgraPixels.clear();
                    return false;
                }
                for (int t = 0; t < 16; ++t) {
                    if (blkRgba[t * 4 + 3] < 255u) anyNonOpaque = true;
                }
                GpuTex::BlitBlock4x4RgbaToPremulBgra(blkRgba, bx, by, origW, origH, outInfo.bgraPixels);
            }
        }
        outInfo.width = origW;
        outInfo.height = origH;
        outInfo.origWidth = origW;
        outInfo.origHeight = origH;
        outInfo.channels = (anyNonOpaque || (flags & 0x04u) != 0u) ? 4u : 3u;
        outInfo.bitsPerChannel = 8;
        outInfo.formatName = L"Basis Universal UASTC 4x4 (.basis)";
        outInfo.valid = true;
        return true;
    }

    // Mode 0: cETC1S — require valid endpoint codebook, selector codebook, and Huffman tables
    if (numEndpoints == 0 || epCbSize == 0 || epCbOfs < hdrSize || epCbOfs > size || epCbSize > size - epCbOfs) {
        return false;
    }
    if (numSelectors == 0 || selCbSize == 0 || selCbOfs < hdrSize || selCbOfs > size || selCbSize > size - selCbOfs) {
        return false;
    }
    if (tablesSize == 0 || tablesOfs < hdrSize || tablesOfs > size || tablesSize > size - tablesOfs) {
        return false;
    }

    std::vector<DetailBasis::Etc1sEndpoint> endpoints;
    std::vector<DetailBasis::Etc1sSelector> selectors;
    DetailBasis::Etc1sTables tables;
    if (!DetailBasis::DecodeEtc1sEndpoints(data + epCbOfs, epCbSize, numEndpoints, endpoints) ||
        !DetailBasis::DecodeEtc1sSelectors(data + selCbOfs, selCbSize, numSelectors, selectors) ||
        !DetailBasis::DecodeEtc1sTables(data + tablesOfs, tablesSize, tables)) {
        return false;
    }

    bool isVideo = (texType == 3u);
    std::vector<uint8_t> rgbRgba;
    if (!DetailBasis::DecodeEtc1sSliceToRgba(
            sliceBytes, sliceSize, blocksX, blocksY, origW, origH,
            isVideo, endpoints, selectors, tables, rgbRgba)) {
        return false;
    }

    // Optional paired alpha slice when cBASISHeaderFlagHasAlphaSlices (0x04) or next slice has cSliceDescFlagsHasAlpha (0x01)
    bool hasAlpha = false;
    if (chosenIdx + 1u < totalSlices) {
        const uint8_t* alphaDesc = data + sliceDescOfs + (chosenIdx + 1u) * 23u;
        uint32_t aImgIdx = DetailImg100::RdU24LE(alphaDesc + 0);
        uint8_t  aLvlIdx = alphaDesc[3];
        uint8_t  aFlags  = alphaDesc[4];
        if (aImgIdx == chosenImgIdx && aLvlIdx == chosenLvlIdx &&
            ((flags & 0x04u) != 0u || (flags & 0x02u) != 0u || (aFlags & 0x01u) != 0u)) {
            uint32_t aBlocksX = DetailImg100::RdU16LE(alphaDesc + 9);
            uint32_t aBlocksY = DetailImg100::RdU16LE(alphaDesc + 11);
            uint32_t aOfs     = DetailImg100::RdU32LE(alphaDesc + 13);
            uint32_t aSize    = DetailImg100::RdU32LE(alphaDesc + 17);
            uint16_t aCrc16   = DetailImg100::RdU16LE(alphaDesc + 21);
            if (aBlocksX != blocksX || aBlocksY != blocksY ||
                aSize == 0 || aOfs < hdrSize || aOfs > size || aSize > size - aOfs) {
                return false;
            }
            if (aCrc16 != 0 && DetailBasis::BasisCrc16(data + aOfs, aSize) != aCrc16) {
                return false;
            }
            std::vector<uint8_t> alphaRgba;
            if (!DetailBasis::DecodeEtc1sSliceToRgba(
                    data + aOfs, aSize, blocksX, blocksY, origW, origH,
                    isVideo, endpoints, selectors, tables, alphaRgba)) {
                return false;
            }
            size_t pxTotal = static_cast<size_t>(origW) * origH;
            for (size_t i = 0; i < pxTotal; ++i) {
                rgbRgba[i * 4u + 3] = alphaRgba[i * 4u + 1]; // ETC1S alpha slice stores alpha in Green
            }
            hasAlpha = true;
        }
    }

    size_t pxTotal = static_cast<size_t>(origW) * origH;
    outInfo.bgraPixels.resize(pxTotal * 4u);
    for (size_t i = 0; i < pxTotal; ++i) {
        WritePremulBGRA(
            &outInfo.bgraPixels[i * 4u],
            rgbRgba[i * 4u + 0],
            rgbRgba[i * 4u + 1],
            rgbRgba[i * 4u + 2],
            rgbRgba[i * 4u + 3]);
    }

    (void)extLower;
    outInfo.width = origW;
    outInfo.height = origH;
    outInfo.origWidth = origW;
    outInfo.origHeight = origH;
    outInfo.channels = hasAlpha ? 4u : 3u;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = L"Basis Universal ETC1S (.basis)";
    outInfo.valid = true;
    return true;
}

} // namespace Formats
