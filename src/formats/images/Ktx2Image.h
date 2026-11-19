#pragma once
// Ktx2Image.h — Khronos KTX 2.0 GPU Texture Container Decoder (.ktx2)

namespace Formats {

inline bool DecodeKtx2Image(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    static const uint8_t kKtx2Sig[12] = { 0xAB, 'K', 'T', 'X', ' ', '2', '0', 0xBB, 0x0D, 0x0A, 0x1A, 0x0A };
    if (!data || size < 104 || std::memcmp(data, kKtx2Sig, 12) != 0) return false;
        uint32_t vkFormat = DetailImg100::RdU32LE(data + 12);
        uint32_t w = DetailImg100::RdU32LE(data + 20);
        uint32_t h = DetailImg100::RdU32LE(data + 24);
        if (h == 0) h = 1; // 1D KTX2 texture
        uint32_t d = std::max(1u, DetailImg100::RdU32LE(data + 28));
        uint32_t layers = std::max(1u, DetailImg100::RdU32LE(data + 32));
        uint32_t faces = std::max(1u, DetailImg100::RdU32LE(data + 36));
        uint32_t levelCount = std::max(1u, DetailImg100::RdU32LE(data + 40));
        uint32_t supercomp = DetailImg100::RdU32LE(data + 44);
        uint32_t dfdByteOff = DetailImg100::RdU32LE(data + 48);
        uint32_t dfdByteLen = DetailImg100::RdU32LE(data + 52);
        uint64_t sgdByteOff = DetailImg100::RdU64LE(data + 64);
        uint64_t sgdByteLen = DetailImg100::RdU64LE(data + 72);
        if (w == 0 || w > 8192 || h > 8192 || levelCount > 32) return false;

        size_t levelIndexEnd = 80ull + static_cast<size_t>(levelCount) * 24ull;
        if (levelIndexEnd > size) return false;

        // KTX 2.0 Section 3.9.7: levels[0] at byte offset 80 is mip level 0 (largest resolution)
        uint64_t byteOff = DetailImg100::RdU64LE(data + 80);
        uint64_t byteLen = DetailImg100::RdU64LE(data + 88);
        uint64_t uncompLen = DetailImg100::RdU64LE(data + 96);
        if (byteLen == 0 || byteOff < levelIndexEnd || byteOff > size || byteLen > size - byteOff) {
            return false;
        }

        if (supercomp == 1) { // KTX_SS_BASIS_LZ (ETC1S supercompression, KTX 2.0 Section 3.10.2)
            if (vkFormat != 0) return false;
            if (dfdByteLen >= 28 && dfdByteOff <= size && dfdByteLen <= size - dfdByteOff) {
                uint8_t colorModel = data[dfdByteOff + 12];
                if (colorModel == 0u) colorModel = data[dfdByteOff + 16];
                if (colorModel != 163u) return false; // KHR_DF_MODEL_ETC1S == 163
            }
            uint64_t totalImgCount = 0;
            for (uint32_t lv = 0; lv < levelCount; ++lv) {
                uint32_t lvDepth = std::max(1u, d >> lv);
                totalImgCount += static_cast<uint64_t>(layers) * faces * lvDepth;
            }
            if (totalImgCount == 0 || totalImgCount > 65536ull) return false;
            uint64_t descsEnd = 20ull + totalImgCount * 20ull;
            if (sgdByteOff < levelIndexEnd || sgdByteOff > size || sgdByteLen > size - sgdByteOff || sgdByteLen < descsEnd) {
                return false;
            }
            const uint8_t* sgd = data + static_cast<size_t>(sgdByteOff);
            uint16_t endpointCount    = DetailImg100::RdU16LE(sgd + 0);
            uint16_t selectorCount    = DetailImg100::RdU16LE(sgd + 2);
            uint32_t endpointsByteLen = DetailImg100::RdU32LE(sgd + 4);
            uint32_t selectorsByteLen = DetailImg100::RdU32LE(sgd + 8);
            uint32_t tablesByteLen    = DetailImg100::RdU32LE(sgd + 12);
            uint32_t extendedByteLen  = DetailImg100::RdU32LE(sgd + 16);
            if (endpointCount == 0 || selectorCount == 0 ||
                endpointsByteLen == 0 || selectorsByteLen == 0 || tablesByteLen == 0) {
                return false;
            }
            uint64_t sgdRequired = descsEnd + endpointsByteLen + selectorsByteLen + tablesByteLen + extendedByteLen;
            if (sgdRequired > sgdByteLen) return false;

            // Mip 0, layer 0, face 0, slice 0 descriptor at sgd + 20
            uint32_t imageFlags    = DetailImg100::RdU32LE(sgd + 20);
            uint32_t rgbSliceOff   = DetailImg100::RdU32LE(sgd + 24);
            uint32_t rgbSliceLen   = DetailImg100::RdU32LE(sgd + 28);
            uint32_t alphaSliceOff = DetailImg100::RdU32LE(sgd + 32);
            uint32_t alphaSliceLen = DetailImg100::RdU32LE(sgd + 36);
            if (rgbSliceLen == 0 || rgbSliceOff > byteLen || rgbSliceLen > byteLen - rgbSliceOff) {
                return false;
            }

            const uint8_t* epPtr  = sgd + static_cast<size_t>(descsEnd);
            const uint8_t* selPtr = epPtr + endpointsByteLen;
            const uint8_t* tblPtr = selPtr + selectorsByteLen;

            std::vector<DetailBasis::Etc1sEndpoint> endpoints;
            std::vector<DetailBasis::Etc1sSelector> selectors;
            DetailBasis::Etc1sTables tables;
            if (!DetailBasis::DecodeEtc1sEndpoints(epPtr, endpointsByteLen, endpointCount, endpoints) ||
                !DetailBasis::DecodeEtc1sSelectors(selPtr, selectorsByteLen, selectorCount, selectors) ||
                !DetailBasis::DecodeEtc1sTables(tblPtr, tablesByteLen, tables)) {
                return false;
            }

            uint32_t bw4 = (w + 3u) / 4u;
            uint32_t bh4 = (h + 3u) / 4u;
            bool isVideo = (imageFlags & 0x01u) != 0u;
            const uint8_t* mipBase = data + static_cast<size_t>(byteOff);
            std::vector<uint8_t> rgbRgba;
            if (!DetailBasis::DecodeEtc1sSliceToRgba(
                    mipBase + rgbSliceOff, rgbSliceLen, bw4, bh4, w, h,
                    isVideo, endpoints, selectors, tables, rgbRgba)) {
                return false;
            }

            bool hasAlpha = false;
            if (alphaSliceLen > 0) {
                if (alphaSliceOff > byteLen || alphaSliceLen > byteLen - alphaSliceOff) return false;
                std::vector<uint8_t> alphaRgba;
                if (!DetailBasis::DecodeEtc1sSliceToRgba(
                        mipBase + alphaSliceOff, alphaSliceLen, bw4, bh4, w, h,
                        isVideo, endpoints, selectors, tables, alphaRgba)) {
                    return false;
                }
                size_t pxTotal = static_cast<size_t>(w) * h;
                for (size_t i = 0; i < pxTotal; ++i) {
                    rgbRgba[i * 4u + 3] = alphaRgba[i * 4u + 1];
                }
                hasAlpha = true;
            }

            size_t pxTotal = static_cast<size_t>(w) * h;
            outInfo.bgraPixels.resize(pxTotal * 4u);
            for (size_t i = 0; i < pxTotal; ++i) {
                WritePremulBGRA(
                    &outInfo.bgraPixels[i * 4u],
                    rgbRgba[i * 4u + 0],
                    rgbRgba[i * 4u + 1],
                    rgbRgba[i * 4u + 2],
                    rgbRgba[i * 4u + 3]);
            }
            outInfo.width = w;
            outInfo.height = h;
            outInfo.origWidth = w;
            outInfo.origHeight = h;
            outInfo.channels = hasAlpha ? 4u : 3u;
            outInfo.bitsPerChannel = 8;
            outInfo.formatName = L"Khronos KTX 2.0 BasisLZ ETC1S (.ktx2)";
            outInfo.valid = true;
            return true;
        }

        std::vector<uint8_t> inflatedMip;
        const uint8_t* mipPtr = nullptr;
        size_t mipLen = 0;
        if (supercomp == 0) {
            mipPtr = data + static_cast<size_t>(byteOff);
            mipLen = static_cast<size_t>(byteLen);
        } else if (supercomp == 2) { // KTX_SS_ZSTD (RFC 8878 Zstandard supercompression)
            if (uncompLen == 0 || uncompLen > 256ULL * 1024 * 1024) return false;
            if (!StreamCodecs::DecompressZstdFrame(data + static_cast<size_t>(byteOff), static_cast<size_t>(byteLen),
                                                   inflatedMip, static_cast<size_t>(uncompLen) + 64) ||
                inflatedMip.size() < static_cast<size_t>(uncompLen)) {
                return false;
            }
            mipPtr = inflatedMip.data();
            mipLen = static_cast<size_t>(uncompLen);
        } else if (supercomp == 3) { // KTX_SS_ZLIB
            if (uncompLen == 0 || uncompLen > 256ULL * 1024 * 1024) return false;
            if (!ZipDeflate::InflateZlib(data + static_cast<size_t>(byteOff), static_cast<size_t>(byteLen),
                                         inflatedMip, static_cast<size_t>(uncompLen) + 64) ||
                inflatedMip.size() < static_cast<size_t>(uncompLen)) {
                return false;
            }
            mipPtr = inflatedMip.data();
            mipLen = static_cast<size_t>(uncompLen);
        } else {
            return false;
        }

        uint64_t numFacesSlices = static_cast<uint64_t>(layers) * faces * d;
        if (numFacesSlices > 1 && mipLen >= numFacesSlices) {
            // Decode first layer/face/slice of mip0
            mipLen = static_cast<size_t>(mipLen / numFacesSlices);
        }

        size_t pxCount = static_cast<size_t>(w) * h;
        uint32_t bw4 = (w + 3u) / 4u;
        uint32_t bh4 = (h + 3u) / 4u;
        size_t blocks4x4 = static_cast<size_t>(bw4) * bh4;
        std::wstring fmtDesc = L"Khronos KTX 2.0 (.ktx2)";
        uint32_t outCh = 4;
        uint32_t outBits = 8;

        if (vkFormat == 0) { // VK_FORMAT_UNDEFINED -> check DFD colorModel == 166 (KHR_DF_MODEL_UASTC)
            if (dfdByteLen < 28 || dfdByteOff > size || dfdByteLen > size - dfdByteOff) return false;
            uint8_t colorModel = data[dfdByteOff + 12];
            if (colorModel == 0u) colorModel = data[dfdByteOff + 16];
            if (colorModel != 166u || mipLen < blocks4x4 * 16u) return false;
            outInfo.bgraPixels.assign(pxCount * 4u, 0);
            uint8_t blk[64];
            for (uint32_t by = 0; by < bh4; ++by) {
                for (uint32_t bx = 0; bx < bw4; ++bx) {
                    if (!DetailBasis::DecodeUastcBlock4x4(mipPtr + (static_cast<size_t>(by) * bw4 + bx) * 16u, blk)) {
                        return false;
                    }
                    GpuTex::BlitBlock4x4RgbaToPremulBgra(blk, bx, by, w, h, outInfo.bgraPixels);
                }
            }
            fmtDesc = L"Khronos KTX 2.0 UASTC 4x4 (.ktx2)";
        } else if (vkFormat >= 9 && vkFormat <= 15) { // R8
            if (mipLen < pxCount) return false;
            outCh = 1;
            outInfo.bgraPixels.resize(pxCount * 4);
            bool snorm = (vkFormat == 10 || vkFormat == 12 || vkFormat == 14);
            for (size_t i = 0; i < pxCount; ++i) {
                uint8_t v = snorm ? static_cast<uint8_t>(static_cast<int>(static_cast<int8_t>(mipPtr[i])) + 128) : mipPtr[i];
                WritePremulBGRA(&outInfo.bgraPixels[i * 4], v, v, v, 255);
            }
            fmtDesc = L"Khronos KTX 2.0 R8 (.ktx2)";
        } else if (vkFormat >= 16 && vkFormat <= 22) { // RG8
            if (mipLen < pxCount * 2) return false;
            outCh = 2;
            outInfo.bgraPixels.resize(pxCount * 4);
            bool snorm = (vkFormat == 17 || vkFormat == 19 || vkFormat == 21);
            for (size_t i = 0; i < pxCount; ++i) {
                uint8_t r = snorm ? static_cast<uint8_t>(static_cast<int>(static_cast<int8_t>(mipPtr[i * 2 + 0])) + 128) : mipPtr[i * 2 + 0];
                uint8_t g = snorm ? static_cast<uint8_t>(static_cast<int>(static_cast<int8_t>(mipPtr[i * 2 + 1])) + 128) : mipPtr[i * 2 + 1];
                WritePremulBGRA(&outInfo.bgraPixels[i * 4], r, g, 0, 255);
            }
            fmtDesc = L"Khronos KTX 2.0 RG8 (.ktx2)";
        } else if (vkFormat >= 23 && vkFormat <= 29) { // RGB8
            if (mipLen < pxCount * 3) return false;
            outCh = 3;
            outInfo.bgraPixels.resize(pxCount * 4);
            for (size_t i = 0; i < pxCount; ++i) {
                WritePremulBGRA(&outInfo.bgraPixels[i * 4], mipPtr[i * 3 + 0], mipPtr[i * 3 + 1], mipPtr[i * 3 + 2], 255);
            }
            fmtDesc = L"Khronos KTX 2.0 RGB8 (.ktx2)";
        } else if (vkFormat >= 30 && vkFormat <= 36) { // BGR8
            if (mipLen < pxCount * 3) return false;
            outCh = 3;
            outInfo.bgraPixels.resize(pxCount * 4);
            for (size_t i = 0; i < pxCount; ++i) {
                WritePremulBGRA(&outInfo.bgraPixels[i * 4], mipPtr[i * 3 + 2], mipPtr[i * 3 + 1], mipPtr[i * 3 + 0], 255);
            }
            fmtDesc = L"Khronos KTX 2.0 BGR8 (.ktx2)";
        } else if (vkFormat >= 37 && vkFormat <= 43) { // RGBA8
            if (mipLen < pxCount * 4) return false;
            outInfo.bgraPixels.resize(pxCount * 4);
            for (size_t i = 0; i < pxCount; ++i) {
                WritePremulBGRA(&outInfo.bgraPixels[i * 4], mipPtr[i * 4 + 0], mipPtr[i * 4 + 1], mipPtr[i * 4 + 2], mipPtr[i * 4 + 3]);
            }
            fmtDesc = L"Khronos KTX 2.0 RGBA8 (.ktx2)";
        } else if (vkFormat >= 44 && vkFormat <= 50) { // BGRA8
            if (mipLen < pxCount * 4) return false;
            outInfo.bgraPixels.resize(pxCount * 4);
            for (size_t i = 0; i < pxCount; ++i) {
                WritePremulBGRA(&outInfo.bgraPixels[i * 4], mipPtr[i * 4 + 2], mipPtr[i * 4 + 1], mipPtr[i * 4 + 0], mipPtr[i * 4 + 3]);
            }
            fmtDesc = L"Khronos KTX 2.0 BGRA8 (.ktx2)";
        } else if (vkFormat == 76 || vkFormat == 83 || vkFormat == 90 || vkFormat == 97) { // R16F / RG16F / RGB16F / RGBA16F
            uint32_t ch = (vkFormat == 76) ? 1u : (vkFormat == 83) ? 2u : (vkFormat == 90) ? 3u : 4u;
            if (mipLen < pxCount * ch * 2) return false;
            outCh = ch;
            outBits = 16;
            std::vector<float> flt(pxCount * 4, 0.0f);
            for (size_t i = 0; i < pxCount; ++i) {
                const uint8_t* sp = mipPtr + i * ch * 2;
                float r = GpuTex::HalfToFloat(DetailImg100::RdU16LE(sp));
                float g = (ch >= 2) ? GpuTex::HalfToFloat(DetailImg100::RdU16LE(sp + 2)) : r;
                float b = (ch >= 3) ? GpuTex::HalfToFloat(DetailImg100::RdU16LE(sp + 4)) : (ch == 1 ? r : 0.0f);
                float a = (ch >= 4) ? GpuTex::HalfToFloat(DetailImg100::RdU16LE(sp + 6)) : 1.0f;
                flt[i * 4 + 0] = r; flt[i * 4 + 1] = g; flt[i * 4 + 2] = b; flt[i * 4 + 3] = a;
            }
            GpuTex::ConvertFloatRgbaToBgra(flt.data(), w, h, false, outInfo.bgraPixels);
            fmtDesc = L"Khronos KTX 2.0 RGBA16F (.ktx2)";
        } else if (vkFormat == 100 || vkFormat == 103 || vkFormat == 106 || vkFormat == 109) { // R32F / RG32F / RGB32F / RGBA32F
            uint32_t ch = (vkFormat == 100) ? 1u : (vkFormat == 103) ? 2u : (vkFormat == 106) ? 3u : 4u;
            if (mipLen < pxCount * ch * 4) return false;
            outCh = ch;
            outBits = 32;
            std::vector<float> flt(pxCount * 4, 0.0f);
            for (size_t i = 0; i < pxCount; ++i) {
                const uint8_t* sp = mipPtr + i * ch * 4;
                float r = GpuTex::ReadF32LE(sp);
                float g = (ch >= 2) ? GpuTex::ReadF32LE(sp + 4) : r;
                float b = (ch >= 3) ? GpuTex::ReadF32LE(sp + 8) : (ch == 1 ? r : 0.0f);
                float a = (ch >= 4) ? GpuTex::ReadF32LE(sp + 12) : 1.0f;
                flt[i * 4 + 0] = r; flt[i * 4 + 1] = g; flt[i * 4 + 2] = b; flt[i * 4 + 3] = a;
            }
            GpuTex::ConvertFloatRgbaToBgra(flt.data(), w, h, false, outInfo.bgraPixels);
            fmtDesc = L"Khronos KTX 2.0 RGBA32F (.ktx2)";
        } else if (vkFormat >= 131 && vkFormat <= 134) { // BC1
            if (mipLen < blocks4x4 * 8) return false;
            outInfo.bgraPixels.assign(pxCount * 4, 0);
            bool alpha1 = (vkFormat >= 133);
            uint8_t blk[64];
            for (uint32_t by = 0; by < bh4; ++by) {
                for (uint32_t bx = 0; bx < bw4; ++bx) {
                    GpuTex::DecodeBC1Block(mipPtr + (static_cast<size_t>(by) * bw4 + bx) * 8, blk, alpha1);
                    GpuTex::BlitBlock4x4RgbaToPremulBgra(blk, bx, by, w, h, outInfo.bgraPixels);
                }
            }
            fmtDesc = L"Khronos KTX 2.0 BC1 (.ktx2)";
        } else if (vkFormat == 135 || vkFormat == 136) { // BC2
            if (mipLen < blocks4x4 * 16) return false;
            outInfo.bgraPixels.assign(pxCount * 4, 0);
            uint8_t blk[64];
            for (uint32_t by = 0; by < bh4; ++by) {
                for (uint32_t bx = 0; bx < bw4; ++bx) {
                    GpuTex::DecodeBC2Block(mipPtr + (static_cast<size_t>(by) * bw4 + bx) * 16, blk);
                    GpuTex::BlitBlock4x4RgbaToPremulBgra(blk, bx, by, w, h, outInfo.bgraPixels);
                }
            }
            fmtDesc = L"Khronos KTX 2.0 BC2 (.ktx2)";
        } else if (vkFormat == 137 || vkFormat == 138) { // BC3
            if (mipLen < blocks4x4 * 16) return false;
            outInfo.bgraPixels.assign(pxCount * 4, 0);
            uint8_t blk[64];
            for (uint32_t by = 0; by < bh4; ++by) {
                for (uint32_t bx = 0; bx < bw4; ++bx) {
                    GpuTex::DecodeBC3Block(mipPtr + (static_cast<size_t>(by) * bw4 + bx) * 16, blk);
                    GpuTex::BlitBlock4x4RgbaToPremulBgra(blk, bx, by, w, h, outInfo.bgraPixels);
                }
            }
            fmtDesc = L"Khronos KTX 2.0 BC3 (.ktx2)";
        } else if (vkFormat == 139 || vkFormat == 140) { // BC4
            if (mipLen < blocks4x4 * 8) return false;
            outCh = 1;
            outInfo.bgraPixels.assign(pxCount * 4, 0);
            uint8_t blk[64];
            for (uint32_t by = 0; by < bh4; ++by) {
                for (uint32_t bx = 0; bx < bw4; ++bx) {
                    GpuTex::DecodeBC4Block(mipPtr + (static_cast<size_t>(by) * bw4 + bx) * 8, blk, vkFormat == 140);
                    GpuTex::BlitBlock4x4RgbaToPremulBgra(blk, bx, by, w, h, outInfo.bgraPixels);
                }
            }
            fmtDesc = L"Khronos KTX 2.0 BC4 (.ktx2)";
        } else if (vkFormat == 141 || vkFormat == 142) { // BC5
            if (mipLen < blocks4x4 * 16) return false;
            outCh = 2;
            outInfo.bgraPixels.assign(pxCount * 4, 0);
            uint8_t blk[64];
            for (uint32_t by = 0; by < bh4; ++by) {
                for (uint32_t bx = 0; bx < bw4; ++bx) {
                    GpuTex::DecodeBC5Block(mipPtr + (static_cast<size_t>(by) * bw4 + bx) * 16, blk, vkFormat == 142);
                    GpuTex::BlitBlock4x4RgbaToPremulBgra(blk, bx, by, w, h, outInfo.bgraPixels);
                }
            }
            fmtDesc = L"Khronos KTX 2.0 BC5 (.ktx2)";
        } else if (vkFormat == 143 || vkFormat == 144) { // BC6H_UFLOAT / BC6H_SFLOAT
            if (mipLen < blocks4x4 * 16) return false;
            outCh = 3;
            outBits = 16;
            std::vector<float> flt(pxCount * 4, 0.0f);
            float blk[64];
            for (uint32_t by = 0; by < bh4; ++by) {
                for (uint32_t bx = 0; bx < bw4; ++bx) {
                    GpuTex::DecodeBC6HBlock(mipPtr + (static_cast<size_t>(by) * bw4 + bx) * 16, blk, vkFormat == 144);
                    for (uint32_t py = 0; py < 4; ++py) {
                        uint32_t dy = by * 4 + py;
                        if (dy >= h) continue;
                        for (uint32_t px = 0; px < 4; ++px) {
                            uint32_t dx = bx * 4 + px;
                            if (dx >= w) continue;
                            std::memcpy(&flt[(static_cast<size_t>(dy) * w + dx) * 4], &blk[(py * 4 + px) * 4], 16);
                        }
                    }
                }
            }
            GpuTex::ConvertFloatRgbaToBgra(flt.data(), w, h, true, outInfo.bgraPixels);
            fmtDesc = L"Khronos KTX 2.0 BC6H HDR (.ktx2)";
        } else if (vkFormat == 145 || vkFormat == 146) { // BC7
            if (mipLen < blocks4x4 * 16) return false;
            outInfo.bgraPixels.assign(pxCount * 4, 0);
            uint8_t blk[64];
            for (uint32_t by = 0; by < bh4; ++by) {
                for (uint32_t bx = 0; bx < bw4; ++bx) {
                    GpuTex::DecodeBC7Block(mipPtr + (static_cast<size_t>(by) * bw4 + bx) * 16, blk);
                    GpuTex::BlitBlock4x4RgbaToPremulBgra(blk, bx, by, w, h, outInfo.bgraPixels);
                }
            }
            fmtDesc = L"Khronos KTX 2.0 BC7 (.ktx2)";
        } else if (vkFormat == 147 || vkFormat == 148) { // ETC2 RGB8
            if (mipLen < blocks4x4 * 8) return false;
            outCh = 3u;
            outInfo.bgraPixels.assign(pxCount * 4, 0);
            uint8_t blk[64];
            for (uint32_t by = 0; by < bh4; ++by) {
                for (uint32_t bx = 0; bx < bw4; ++bx) {
                    GpuTex::DecodeETC2RgbBlock(mipPtr + (static_cast<size_t>(by) * bw4 + bx) * 8, blk, true);
                    GpuTex::BlitBlock4x4RgbaToPremulBgra(blk, bx, by, w, h, outInfo.bgraPixels);
                }
            }
            fmtDesc = L"Khronos KTX 2.0 ETC2 RGB8 (.ktx2)";
        } else if (vkFormat == 149 || vkFormat == 150) { // ETC2 RGB8A1 punchthrough alpha
            if (mipLen < blocks4x4 * 8) return false;
            outCh = 4u;
            outInfo.bgraPixels.assign(pxCount * 4, 0);
            uint8_t blk[64];
            for (uint32_t by = 0; by < bh4; ++by) {
                for (uint32_t bx = 0; bx < bw4; ++bx) {
                    GpuTex::DecodeETC2RgbA1Block(mipPtr + (static_cast<size_t>(by) * bw4 + bx) * 8, blk);
                    GpuTex::BlitBlock4x4RgbaToPremulBgra(blk, bx, by, w, h, outInfo.bgraPixels);
                }
            }
            fmtDesc = L"Khronos KTX 2.0 ETC2 RGB8A1 (.ktx2)";
        } else if (vkFormat == 151 || vkFormat == 152) { // ETC2 RGBA8
            if (mipLen < blocks4x4 * 16) return false;
            outInfo.bgraPixels.assign(pxCount * 4, 0);
            uint8_t blk[64];
            for (uint32_t by = 0; by < bh4; ++by) {
                for (uint32_t bx = 0; bx < bw4; ++bx) {
                    GpuTex::DecodeETC2Block(mipPtr + (static_cast<size_t>(by) * bw4 + bx) * 16, blk, true);
                    GpuTex::BlitBlock4x4RgbaToPremulBgra(blk, bx, by, w, h, outInfo.bgraPixels);
                }
            }
            fmtDesc = L"Khronos KTX 2.0 ETC2 RGBA8 (.ktx2)";
        } else if (vkFormat == 153 || vkFormat == 154) { // EAC R11 UNORM / SNORM
            if (mipLen < blocks4x4 * 8) return false;
            outCh = 1u;
            outBits = 11u;
            outInfo.bgraPixels.assign(pxCount * 4, 0);
            uint8_t blk[64];
            for (uint32_t by = 0; by < bh4; ++by) {
                for (uint32_t bx = 0; bx < bw4; ++bx) {
                    GpuTex::DecodeEacR11Block(mipPtr + (static_cast<size_t>(by) * bw4 + bx) * 8, vkFormat == 154, blk);
                    GpuTex::BlitBlock4x4RgbaToPremulBgra(blk, bx, by, w, h, outInfo.bgraPixels);
                }
            }
            fmtDesc = L"Khronos KTX 2.0 EAC R11 (.ktx2)";
        } else if (vkFormat == 155 || vkFormat == 156) { // EAC RG11 UNORM / SNORM
            if (mipLen < blocks4x4 * 16) return false;
            outCh = 2u;
            outBits = 11u;
            outInfo.bgraPixels.assign(pxCount * 4, 0);
            uint8_t blk[64];
            for (uint32_t by = 0; by < bh4; ++by) {
                for (uint32_t bx = 0; bx < bw4; ++bx) {
                    GpuTex::DecodeEacRG11Block(mipPtr + (static_cast<size_t>(by) * bw4 + bx) * 16, vkFormat == 156, blk);
                    GpuTex::BlitBlock4x4RgbaToPremulBgra(blk, bx, by, w, h, outInfo.bgraPixels);
                }
            }
            fmtDesc = L"Khronos KTX 2.0 EAC RG11 (.ktx2)";
        } else if (vkFormat >= 157 && vkFormat <= 184) { // ASTC 4x4 .. 12x12
            static const uint8_t kAstcDims[14][2] = {
                {4,4}, {5,4}, {5,5}, {6,5}, {6,6}, {8,5}, {8,6},
                {8,8}, {10,5}, {10,6}, {10,8}, {10,10}, {12,10}, {12,12}
            };
            uint32_t pairIdx = (vkFormat - 157u) / 2u;
            uint32_t bxDim = kAstcDims[pairIdx][0];
            uint32_t byDim = kAstcDims[pairIdx][1];
            if (!DecodeAstcSurface(mipPtr, mipLen, w, h, bxDim, byDim, outInfo.bgraPixels)) {
                return false;
            }
            fmtDesc = L"Khronos KTX 2.0 ASTC " + std::to_wstring(bxDim) + L"x" + std::to_wstring(byDim) + L" (.ktx2)";
        } else {
            return false;
        }

        outInfo.width = w;
        outInfo.height = h;
        outInfo.origWidth = w;
        outInfo.origHeight = h;
        outInfo.channels = outCh;
        outInfo.bitsPerChannel = outBits;
        outInfo.formatName = fmtDesc;
        outInfo.valid = true;
        return true;
}

} // namespace Formats
