#pragma once
// BlizzardBlp.h — Blizzard Entertainment BLP1 / BLP2 Texture Decoder (.blp)

namespace Formats {

inline bool DecodeBlizzardBlp(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 148) return false;
    if (size >= 148 && (std::memcmp(data, "BLP2", 4) == 0 || std::memcmp(data, "BLP1", 4) == 0)) {
        const bool isBlp2 = (data[3] == '2');
        if (!isBlp2 && size < 156) return false; // BLP1 header is 7*4 + 16*4 + 16*4 = 156 bytes

        uint32_t w = DetailImg100::RdU32LE(data + 12);
        uint32_t h = DetailImg100::RdU32LE(data + 16);
        if (w == 0 || h == 0 || w > 4096 || h > 4096) return false;

        // BLP2: [magic, type, enc/alpha/mips(4B), w, h] = 20B -> mipOffsets[0]@20, mipSizes[0]@84
        // BLP1: [magic, compression, flags, w, h, pictureType, pictureSubType] = 28B -> mipOffsets[0]@28, mipSizes[0]@92
        uint32_t mip0Off = DetailImg100::RdU32LE(data + (isBlp2 ? 20 : 28));
        uint32_t mip0Len = DetailImg100::RdU32LE(data + (isBlp2 ? 84 : 92));
        if (mip0Off == 0 || mip0Len == 0 || mip0Off > size || mip0Len > size - mip0Off) return false;

        outInfo.width = w;
        outInfo.height = h;
        outInfo.origWidth = w;
        outInfo.origHeight = h;
        outInfo.channels = 4;
        outInfo.bitsPerChannel = 8;
        outInfo.formatName = isBlp2 ? L"Blizzard BLP2 Texture (.blp)" : L"Blizzard BLP1 Texture (.blp)";

        const size_t pxCount = static_cast<size_t>(w) * h;
        if (isBlp2) {
            uint8_t encoding = data[8];      // 1 = Palette, 2 = DXTC, 3 = Uncompressed BGRA
            uint8_t alphaDepth = data[9];    // 0, 1, 4, 8
            uint8_t alphaEnc = data[10];     // 0 = DXT1, 1 = DXT3, 7 = DXT5
            if (encoding == 1 && size >= 148 + 1024 && mip0Len >= pxCount) {
                const uint8_t* pal = data + 148;
                const uint8_t* idx = data + mip0Off;
                const uint8_t* alphaPlane = idx + pxCount;
                size_t availAlpha = mip0Len - pxCount;
                outInfo.bgraPixels.resize(pxCount * 4);
                for (size_t i = 0; i < pxCount; ++i) {
                    uint8_t c = idx[i];
                    uint8_t a = 255;
                    if (alphaDepth == 8 && i < availAlpha) a = alphaPlane[i];
                    else if (alphaDepth == 4 && (i / 2) < availAlpha) {
                        uint8_t nib = (i & 1u) ? (alphaPlane[i / 2] >> 4) : (alphaPlane[i / 2] & 0x0Fu);
                        a = static_cast<uint8_t>(nib * 17u);
                    } else if (alphaDepth == 1 && (i / 8) < availAlpha) {
                        a = ((alphaPlane[i / 8] >> (i & 7u)) & 1u) ? 255u : 0u;
                    }
                    WritePremulBGRA(&outInfo.bgraPixels[i * 4], pal[c * 4 + 2], pal[c * 4 + 1], pal[c * 4 + 0], a);
                }
                outInfo.valid = true;
                return true;
            }
            if (encoding == 2) {
                uint32_t bw = (w + 3u) / 4u;
                uint32_t bh = (h + 3u) / 4u;
                size_t blkBytes = (alphaEnc == 1 || alphaEnc == 7) ? 16u : 8u;
                if (mip0Len < static_cast<size_t>(bw) * bh * blkBytes) return false;
                outInfo.bgraPixels.assign(pxCount * 4, 0);
                uint8_t blk[64];
                const uint8_t* sp = data + mip0Off;
                for (uint32_t by = 0; by < bh; ++by) {
                    for (uint32_t bx = 0; bx < bw; ++bx) {
                        if (alphaEnc == 7) GpuTex::DecodeBC3Block(sp, blk);
                        else if (alphaEnc == 1) GpuTex::DecodeBC2Block(sp, blk);
                        else GpuTex::DecodeBC1Block(sp, blk, alphaDepth > 0);
                        GpuTex::BlitBlock4x4RgbaToPremulBgra(blk, bx, by, w, h, outInfo.bgraPixels);
                        sp += blkBytes;
                    }
                }
                outInfo.valid = true;
                return true;
            }
            if (encoding == 3 && mip0Len >= pxCount * 4) {
                outInfo.bgraPixels.resize(pxCount * 4);
                const uint8_t* sp = data + mip0Off;
                for (size_t i = 0; i < pxCount; ++i) {
                    WritePremulBGRA(&outInfo.bgraPixels[i * 4], sp[i * 4 + 2], sp[i * 4 + 1], sp[i * 4 + 0], sp[i * 4 + 3]);
                }
                outInfo.valid = true;
                return true;
            }
            if (mip0Len > 16 && DetailImg100::DecodeEmbeddedWicBuffer(data + mip0Off, mip0Len, outInfo.formatName, outInfo)) {
                return true;
            }
            return false;
        }

        // Warcraft III BLP1:
        //   compression (u32@4): 0 = Split JPEG, 1 = Paletted
        //   flags       (u32@8): 0x8 = has alpha
        //   pictureType (u32@20): 3/4 = index + alpha, 5 = index only
        const uint32_t compression = DetailImg100::RdU32LE(data + 4);
        const uint32_t flags       = DetailImg100::RdU32LE(data + 8);
        const uint32_t pictureType = DetailImg100::RdU32LE(data + 20);

        if (compression == 1u) { // Paletted BLP1: 256-entry BGRA palette at offset 156 (1024 bytes)
            if (size < 156u + 1024u || mip0Len < pxCount) return false;
            const uint8_t* pal = data + 156u;
            const uint8_t* idx = data + mip0Off;
            const uint8_t* alphaPlane = idx + pxCount;
            const size_t availAlpha = mip0Len - pxCount;
            const bool wantAlpha = ((flags & 0x8u) != 0u) && (pictureType != 5u);

            int alphaBits = 0;
            if (wantAlpha) {
                if ((pictureType == 3u || pictureType == 4u) && availAlpha >= pxCount) alphaBits = 8;
                else if (availAlpha >= pxCount)                 alphaBits = 8;
                else if (availAlpha >= (pxCount + 1u) / 2u)     alphaBits = 4;
                else if (availAlpha >= (pxCount + 7u) / 8u)     alphaBits = 1;
            }

            outInfo.channels = (alphaBits > 0) ? 4u : 3u;
            outInfo.bgraPixels.resize(pxCount * 4u);
            for (size_t i = 0; i < pxCount; ++i) {
                uint8_t c = idx[i];
                uint8_t a = 255u;
                if (alphaBits == 8) {
                    a = alphaPlane[i];
                } else if (alphaBits == 4) {
                    uint8_t nib = (i & 1u) ? (alphaPlane[i / 2u] >> 4u) : (alphaPlane[i / 2u] & 0x0Fu);
                    a = static_cast<uint8_t>(nib * 17u);
                } else if (alphaBits == 1) {
                    a = ((alphaPlane[i / 8u] >> (i & 7u)) & 1u) ? 255u : 0u;
                }
                WritePremulBGRA(&outInfo.bgraPixels[i * 4u], pal[c * 4u + 2u], pal[c * 4u + 1u], pal[c * 4u + 0u], a);
            }
            outInfo.valid = true;
            return true;
        }

        if (compression == 0u) { // Split-JPEG BLP1: [u32 jpegHeaderSize @ 156] + [shared header @ 160] + [mip0 @ mip0Off]
            if (size < 160u) return false;
            uint32_t jpegHeaderSize = DetailImg100::RdU32LE(data + 156u);
            if (jpegHeaderSize > 65536u || 160u + static_cast<size_t>(jpegHeaderSize) > size) return false;

            const uint8_t* hdrPtr = data + 160u;
            size_t hdrLen = jpegHeaderSize;
            if (hdrLen >= 2u && hdrPtr[hdrLen - 2u] == 0xFFu && hdrPtr[hdrLen - 1u] == 0xD9u) {
                hdrLen -= 2u; // Strip premature EOI from shared quantization/Huffman header
            }
            const uint8_t* bodyPtr = data + mip0Off;
            size_t bodyLen = mip0Len;
            if (hdrLen >= 2u && bodyLen >= 2u && bodyPtr[0] == 0xFFu && bodyPtr[1] == 0xD8u) {
                bodyPtr += 2u; // Strip duplicate SOI if header already starts with SOI
                bodyLen -= 2u;
            }

            std::vector<uint8_t> jpgBuf;
            jpgBuf.reserve(hdrLen + bodyLen);
            jpgBuf.insert(jpgBuf.end(), hdrPtr, hdrPtr + hdrLen);
            jpgBuf.insert(jpgBuf.end(), bodyPtr, bodyPtr + bodyLen);

            DetailBlpJpeg::RawJpegDecoded jd;
            if (DetailBlpJpeg::DecodeBaselineJpegRawChannels(jpgBuf.data(), jpgBuf.size(), jd) &&
                jd.width > 0 && jd.height > 0)
            {
                const uint32_t outW = jd.width;
                const uint32_t outH = jd.height;
                const size_t nPx = static_cast<size_t>(outW) * outH;
                const bool hasAlpha = ((flags & 0x8u) != 0u) && (jd.components == 4u);
                outInfo.width = outW;
                outInfo.height = outH;
                outInfo.origWidth = w;
                outInfo.origHeight = h;
                outInfo.channels = hasAlpha ? 4u : 3u;
                outInfo.bgraPixels.resize(nPx * 4u);

                for (size_t i = 0; i < nPx; ++i) {
                    const uint8_t* ch = &jd.pixels[i * jd.components];
                    uint8_t r = 0, g = 0, b = 0, a = 255u;
                    if (jd.components == 4u) {
                        // Warcraft III BLP1 4-component JPEG stores raw linear [B, G, R, A] directly!
                        b = ch[0];
                        g = ch[1];
                        r = ch[2];
                        a = hasAlpha ? ch[3] : 255u;
                    } else if (jd.components == 3u) {
                        if (jd.hasJfif) {
                            // JFIF YCbCr encoded from BGR input -> convert YCbCr to BGR
                            int yv = ch[0];
                            int cb = static_cast<int>(ch[1]) - 128;
                            int cr = static_cast<int>(ch[2]) - 128;
                            b = static_cast<uint8_t>(std::clamp(yv + ((1436 * cr + 512) >> 10), 0, 255));
                            g = static_cast<uint8_t>(std::clamp(yv - ((352 * cb + 731 * cr + 512) >> 10), 0, 255));
                            r = static_cast<uint8_t>(std::clamp(yv + ((1815 * cb + 512) >> 10), 0, 255));
                        } else {
                            // Raw linear [B, G, R] DCT components
                            b = ch[0];
                            g = ch[1];
                            r = ch[2];
                        }
                    } else {
                        r = g = b = ch[0];
                    }
                    WritePremulBGRA(&outInfo.bgraPixels[i * 4u], r, g, b, a);
                }
                outInfo.valid = true;
                return true;
            }

            // Fallback for progressive SOF2 BLP1 JPEGs via WIC (swapping R<->B for BLP1 BGR channel order)
            if (jpgBuf.size() > 16u && DetailImg100::DecodeEmbeddedWicBuffer(jpgBuf.data(), jpgBuf.size(), outInfo.formatName, outInfo)) {
                for (size_t i = 0; i + 3u < outInfo.bgraPixels.size(); i += 4u) {
                    std::swap(outInfo.bgraPixels[i + 0u], outInfo.bgraPixels[i + 2u]);
                }
                return true;
            }
        }
        return false;
    }
    return false;
}

} // namespace Formats
