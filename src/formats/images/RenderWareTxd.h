#pragma once
// RenderWareTxd.h — RenderWare 3.x Texture Dictionary (.txd).
#include "../common/RenderWareCommon.h"

namespace Formats {
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

} // namespace Formats
