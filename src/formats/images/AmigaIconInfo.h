#pragma once
// AmigaIconInfo.h — Amiga Workbench Icon (.info — 0xE310 WB_DISKMAGIC)
// Decodes OS3.5+ GlowIcons (IFF FORM..ICON FACE/IMAG), NewIcons (IM1= 7-bit ASCII RLE ToolTypes),
// and Classic Amiga Planar GadgetRender Bitplanes (Workbench 1.x / 2.0+ MagicWB palettes), plus OS4 DualPNG icons.

namespace Formats {

namespace DetailAmigaIcon {

// Unpack AmigaOS 3.5 GlowIcons bit-packed RLE stream (each output element is `bitsPerUnit` bits wide, 1..8)
inline bool UnpackGlowIconsBitRle(
    const uint8_t* src,
    size_t srcLen,
    uint32_t bitsPerUnit,
    size_t expectedUnits,
    std::vector<uint8_t>& outUnits)
{
    outUnits.clear();
    if (!src || bitsPerUnit == 0u || bitsPerUnit > 8u) return false;
    outUnits.reserve(expectedUnits);

    size_t bitPos = 0;
    const size_t totalBits = srcLen * 8u;
    auto readBits = [&](uint32_t n, uint32_t& v) -> bool {
        if (bitPos + n > totalBits) return false;
        uint32_t acc = 0;
        for (uint32_t i = 0; i < n; ++i) {
            const size_t b = bitPos + i;
            acc = (acc << 1) | ((src[b >> 3] >> (7u - (b & 7u))) & 1u);
        }
        bitPos += n;
        v = acc;
        return true;
    };

    while (outUnits.size() < expectedUnits && bitPos + 8u <= totalBits) {
        uint32_t hdr = 0;
        if (!readBits(8u, hdr)) break;
        const int8_t n = static_cast<int8_t>(hdr);
        if (n >= 0) {
            const size_t count = static_cast<size_t>(n) + 1u;
            for (size_t k = 0; k < count && outUnits.size() < expectedUnits; ++k) {
                uint32_t val = 0;
                if (!readBits(bitsPerUnit, val)) return false;
                outUnits.push_back(static_cast<uint8_t>(val));
            }
        } else if (n != -128) {
            const size_t count = static_cast<size_t>(1 - static_cast<int>(n));
            uint32_t val = 0;
            if (!readBits(bitsPerUnit, val)) return false;
            const size_t take = std::min(count, expectedUnits - outUnits.size());
            outUnits.insert(outUnits.end(), take, static_cast<uint8_t>(val));
        }
    }
    return outUnits.size() == expectedUnits;
}

// Decode OS3.5+ GlowIcons IFF "FORM....ICON" appended container
inline bool TryDecodeGlowIconsForm(const uint8_t* formPtr, size_t availLen, ExtendedImageInfo& outInfo) {
    if (!formPtr || availLen < 24u || std::memcmp(formPtr, "FORM", 4) != 0 || std::memcmp(formPtr + 8, "ICON", 4) != 0) {
        return false;
    }
    const uint32_t formLen = ByteIO::RdU32BE(formPtr + 4);
    const size_t endOff = std::min<size_t>(availLen, 8u + static_cast<size_t>(formLen));
    if (endOff < 24u) return false;

    uint32_t faceW = 0, faceH = 0;
    size_t pos = 12u;
    while (pos + 8u <= endOff) {
        const uint8_t* ckId = formPtr + pos;
        const uint32_t ckSz = ByteIO::RdU32BE(formPtr + pos + 4);
        pos += 8u;
        if (ckSz > endOff - pos) break;
        const uint8_t* ck = formPtr + pos;

        if (std::memcmp(ckId, "FACE", 4) == 0 && ckSz >= 6u) {
            faceW = static_cast<uint32_t>(ck[0]) + 1u;
            faceH = static_cast<uint32_t>(ck[1]) + 1u;
        } else if (std::memcmp(ckId, "IMAG", 4) == 0 && ckSz >= 10u && faceW > 0u && faceH > 0u) {
            const uint8_t  transIdx    = ck[0];
            const uint32_t numCols     = static_cast<uint32_t>(ck[1]) + 1u;
            const uint8_t  flags       = ck[2];
            const uint8_t  imgComp     = ck[3];
            const uint8_t  palComp     = ck[4];
            const uint8_t  bitsPerPix  = std::max<uint8_t>(1u, ck[5]);
            const uint16_t imgByteSize = ByteIO::RdU16BE(ck + 6);
            const uint16_t palByteSize = ByteIO::RdU16BE(ck + 8);

            const bool hasTrans   = (flags & 0x01u) != 0u;
            const bool hasPalette = (flags & 0x02u) != 0u;
            if (!hasPalette || 10u + static_cast<size_t>(imgByteSize) + palByteSize > ckSz) {
                pos += (ckSz + 1u) & ~1u;
                continue;
            }

            const uint8_t* imgSrc = ck + 10u;
            const uint8_t* palSrc = imgSrc + imgByteSize;
            const size_t nPx = static_cast<size_t>(faceW) * faceH;
            std::vector<uint8_t> indices;

            if (imgComp == 0u) {
                if (bitsPerPix == 8u && imgByteSize >= nPx) {
                    indices.assign(imgSrc, imgSrc + nPx);
                } else {
                    // Unpack packed bit stream of `bitsPerPix` per pixel
                    indices.reserve(nPx);
                    size_t bitCur = 0;
                    for (size_t i = 0; i < nPx && (bitCur + bitsPerPix) <= static_cast<size_t>(imgByteSize) * 8u; ++i) {
                        uint32_t v = 0;
                        for (uint8_t b = 0; b < bitsPerPix; ++b) {
                            size_t bp = bitCur + b;
                            v = (v << 1) | ((imgSrc[bp >> 3] >> (7u - (bp & 7u))) & 1u);
                        }
                        bitCur += bitsPerPix;
                        indices.push_back(static_cast<uint8_t>(v));
                    }
                }
            } else if (imgComp == 1u) {
                if (!UnpackGlowIconsBitRle(imgSrc, imgByteSize, bitsPerPix, nPx, indices)) {
                    // Fallback: zlib-compressed IMAG extension
                    (void)ZipDeflate::InflateZlib(imgSrc, imgByteSize, indices, nPx + 64u);
                }
            } else {
                (void)ZipDeflate::InflateZlib(imgSrc, imgByteSize, indices, nPx + 64u);
            }

            std::vector<uint8_t> palRgb;
            const size_t needPalBytes = static_cast<size_t>(numCols) * 3u;
            if (palComp == 0u) {
                if (palByteSize >= needPalBytes) palRgb.assign(palSrc, palSrc + needPalBytes);
            } else if (palComp == 1u) {
                if (!UnpackGlowIconsBitRle(palSrc, palByteSize, 8u, needPalBytes, palRgb)) {
                    (void)ZipDeflate::InflateZlib(palSrc, palByteSize, palRgb, needPalBytes + 64u);
                }
            } else {
                (void)ZipDeflate::InflateZlib(palSrc, palByteSize, palRgb, needPalBytes + 64u);
            }

            if (indices.size() >= nPx && palRgb.size() >= needPalBytes) {
                outInfo = {};
                outInfo.width = faceW;
                outInfo.height = faceH;
                outInfo.origWidth = faceW;
                outInfo.origHeight = faceH;
                outInfo.channels = 4u;
                outInfo.bitsPerChannel = 8u;
                outInfo.formatName = L"AmigaOS 3.5+ GlowIcon (.info)";
                outInfo.bgraPixels.resize(nPx * 4u);
                for (size_t i = 0; i < nPx; ++i) {
                    const uint8_t ci = indices[i];
                    if (hasTrans && ci == transIdx) {
                        WritePremulBGRA(&outInfo.bgraPixels[i * 4u], 0, 0, 0, 0);
                    } else if (ci < numCols) {
                        WritePremulBGRA(&outInfo.bgraPixels[i * 4u],
                                        palRgb[ci * 3u + 0u], palRgb[ci * 3u + 1u], palRgb[ci * 3u + 2u], 255u);
                    } else {
                        WritePremulBGRA(&outInfo.bgraPixels[i * 4u], 0, 0, 0, 255u);
                    }
                }
                outInfo.valid = true;
                return true;
            }
        } else if (std::memcmp(ckId, "ARGB", 4) == 0 && ckSz >= 10u && faceW > 0u && faceH > 0u) {
            // AmigaOS 4 truecolor zlib-compressed ARGB chunk inside FORM..ICON
            const size_t nPx = static_cast<size_t>(faceW) * faceH;
            std::vector<uint8_t> argb;
            const uint8_t* compSrc = ck + 10u;
            const size_t compLen = ckSz - 10u;
            if ((ck[3] == 0u && compLen >= nPx * 4u) ||
                ZipDeflate::InflateZlib(compSrc, compLen, argb, nPx * 4u + 64u))
            {
                const uint8_t* sp = (ck[3] == 0u) ? compSrc : argb.data();
                if (ck[3] == 0u || argb.size() >= nPx * 4u) {
                    outInfo = {};
                    outInfo.width = faceW;
                    outInfo.height = faceH;
                    outInfo.origWidth = faceW;
                    outInfo.origHeight = faceH;
                    outInfo.channels = 4u;
                    outInfo.bitsPerChannel = 8u;
                    outInfo.formatName = L"AmigaOS 4 ARGB Icon (.info)";
                    outInfo.bgraPixels.resize(nPx * 4u);
                    for (size_t i = 0; i < nPx; ++i) {
                        WritePremulBGRA(&outInfo.bgraPixels[i * 4u], sp[i * 4u + 1u], sp[i * 4u + 2u], sp[i * 4u + 3u], sp[i * 4u + 0u]);
                    }
                    outInfo.valid = true;
                    return true;
                }
            }
        }
        pos += (ckSz + 1u) & ~1u;
    }
    return false;
}

// Decode NewIcons IM1= 7-bit ASCII RLE ToolTypes stream
inline bool TryDecodeNewIconsToolTypes(const std::vector<std::string>& im1Lines, ExtendedImageInfo& outInfo) {
    if (im1Lines.empty() || im1Lines[0].size() < 5u) return false;
    const std::string& hdr = im1Lines[0];
    const bool transparentZero = (hdr[0] == 'B');
    if (hdr[0] != 'B' && hdr[0] != 'C') return false;
    if (static_cast<uint8_t>(hdr[1]) < 0x21u || static_cast<uint8_t>(hdr[2]) < 0x21u ||
        static_cast<uint8_t>(hdr[3]) < 0x21u || static_cast<uint8_t>(hdr[4]) < 0x21u)
    {
        return false;
    }

    const uint32_t w = static_cast<uint32_t>(static_cast<uint8_t>(hdr[1]) - 0x21u);
    const uint32_t h = static_cast<uint32_t>(static_cast<uint8_t>(hdr[2]) - 0x21u);
    const uint32_t numCols = (static_cast<uint32_t>(static_cast<uint8_t>(hdr[3]) - 0x21u) << 6u) +
                              static_cast<uint32_t>(static_cast<uint8_t>(hdr[4]) - 0x21u);
    if (w == 0 || h == 0 || w > 256u || h > 256u || numCols == 0 || numCols > 256u) return false;

    uint32_t bitsPerIdx = 1u;
    while ((1u << bitsPerIdx) < numCols && bitsPerIdx < 8u) ++bitsPerIdx;

    // Decode NewIcons 7-bit ASCII + RLE zero-group stream into a flat bit buffer:
    //   0x20..0x6F -> v = byte - 0x20 (7 bits)
    //   0x70..0x9F -> v = 0x50 + (byte - 0x70) (7 bits,Wait: in NewIcons encoding:
    //     0x20..0x6F => value 0x00..0x4F (7 bits)
    //     0xA1..0xD0 => value 0x50..0x7F (7 bits)
    //     0xD1..0xFF => RLE repeat of (byte - 0xD0) * 7 zero bits!)
    auto decodeNewIconBitStream = [](std::string_view payload, std::vector<uint8_t>& outBits) {
        for (unsigned char c : payload) {
            uint8_t val7 = 0;
            if (c >= 0x20u && c <= 0x6Fu) {
                val7 = static_cast<uint8_t>(c - 0x20u);
            } else if (c >= 0xA1u && c <= 0xD0u) {
                val7 = static_cast<uint8_t>(0x50u + (c - 0xA1u));
            } else if (c >= 0xD1u) {
                const size_t zeros = static_cast<size_t>(c - 0xD0u) * 7u;
                outBits.insert(outBits.end(), zeros, uint8_t{0});
                continue;
            } else {
                continue;
            }
            for (int b = 6; b >= 0; --b) {
                outBits.push_back(static_cast<uint8_t>((val7 >> b) & 1u));
            }
        }
    };

    // First line after 5-byte header encodes the RGB palette (8 bits * 3 * numCols); subsequent IM1= lines encode pixels
    std::vector<uint8_t> streamBits;
    decodeNewIconBitStream(std::string_view(hdr).substr(5u), streamBits);
    const size_t palBitsNeeded = static_cast<size_t>(numCols) * 24u;
    size_t lineIdx = 1u;
    while (streamBits.size() < palBitsNeeded && lineIdx < im1Lines.size()) {
        decodeNewIconBitStream(im1Lines[lineIdx++], streamBits);
    }
    if (streamBits.size() < palBitsNeeded) return false;

    auto readBitsAt = [&](const std::vector<uint8_t>& bits, size_t& cursor, uint32_t n) -> uint32_t {
        uint32_t v = 0;
        for (uint32_t i = 0; i < n && cursor < bits.size(); ++i, ++cursor) {
            v = (v << 1) | bits[cursor];
        }
        return v;
    };

    std::vector<std::array<uint8_t, 3>> pal(numCols);
    size_t cur = 0;
    for (uint32_t c = 0; c < numCols; ++c) {
        pal[c][0] = static_cast<uint8_t>(readBitsAt(streamBits, cur, 8u));
        pal[c][1] = static_cast<uint8_t>(readBitsAt(streamBits, cur, 8u));
        pal[c][2] = static_cast<uint8_t>(readBitsAt(streamBits, cur, 8u));
    }

    // Remaining lines encode pixel indices (aligning to fresh line boundary if palette finished at end of line 0)
    std::vector<uint8_t> pixBits;
    if (lineIdx == 1u && im1Lines.size() > 1u && streamBits.size() - cur < 7u) {
        for (; lineIdx < im1Lines.size(); ++lineIdx) decodeNewIconBitStream(im1Lines[lineIdx], pixBits);
    } else {
        pixBits.assign(streamBits.begin() + static_cast<ptrdiff_t>(cur), streamBits.end());
        for (; lineIdx < im1Lines.size(); ++lineIdx) decodeNewIconBitStream(im1Lines[lineIdx], pixBits);
    }

    const size_t nPx = static_cast<size_t>(w) * h;
    if (pixBits.size() < nPx * bitsPerIdx) return false;

    outInfo = {};
    outInfo.width = w;
    outInfo.height = h;
    outInfo.origWidth = w;
    outInfo.origHeight = h;
    outInfo.channels = 4u;
    outInfo.bitsPerChannel = 8u;
    outInfo.formatName = L"Amiga NewIcons Workbench Icon (.info)";
    outInfo.bgraPixels.resize(nPx * 4u);

    size_t pCur = 0;
    for (size_t i = 0; i < nPx; ++i) {
        const uint32_t idx = readBitsAt(pixBits, pCur, bitsPerIdx);
        if (transparentZero && idx == 0u) {
            WritePremulBGRA(&outInfo.bgraPixels[i * 4u], 0, 0, 0, 0);
        } else if (idx < numCols) {
            WritePremulBGRA(&outInfo.bgraPixels[i * 4u], pal[idx][0], pal[idx][1], pal[idx][2], 255u);
        } else {
            WritePremulBGRA(&outInfo.bgraPixels[i * 4u], 0, 0, 0, 255u);
        }
    }
    outInfo.valid = true;
    return true;
}

} // namespace DetailAmigaIcon

inline bool DecodeAmigaIconInfo(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 24u) return false;

    // AmigaOS 4 / MorphOS DualPNG .info files start directly with PNG magic
    if (size >= 32u && std::memcmp(data, "\x89PNG\r\n\x1A\n", 8) == 0) {
        return GpuTex::DecodePngToImageInfo(data, size, L"AmigaOS PNG Icon (.info)", outInfo);
    }

    // Classic Amiga Workbench DiskObject header: WB_DISKMAGIC (0xE310) + WB_DISKVERSION (0x0001)
    if (size < 78u || ByteIO::RdU16BE(data + 0) != 0xE310u || ByteIO::RdU16BE(data + 2) != 0x0001u) {
        return false;
    }

    const uint16_t gadgetFlags  = ByteIO::RdU16BE(data + 4 + 12);
    const uint32_t gadgetRender = ByteIO::RdU32BE(data + 4 + 18);
    const uint32_t selectRender = ByteIO::RdU32BE(data + 4 + 22);
    const uint32_t userData     = ByteIO::RdU32BE(data + 4 + 40);
    const uint32_t defaultTool  = ByteIO::RdU32BE(data + 50);
    const uint32_t toolTypes    = ByteIO::RdU32BE(data + 54);
    const uint32_t drawerData   = ByteIO::RdU32BE(data + 66);
    const uint32_t toolWindow   = ByteIO::RdU32BE(data + 70);

    size_t pos = 78u;
    if (drawerData != 0u) {
        if (pos + 56u > size) return false;
        pos += 56u; // OldDrawerData
    }

    // Parse Classic Planar Image 1 (GadgetRender)
    bool hasClassicImg = false;
    uint16_t imgW = 0, imgH = 0, imgDepth = 0;
    uint8_t planePick = 0xFFu, planeOnOff = 0u;
    const uint8_t* planarPtr = nullptr;
    size_t rowWordBytes = 0, planeBytes = 0;

    auto skipOrCapturePlanarImage = [&](bool capture) -> bool {
        if (pos + 20u > size) return false;
        const uint16_t w     = ByteIO::RdU16BE(data + pos + 4);
        const uint16_t h     = ByteIO::RdU16BE(data + pos + 6);
        const uint16_t depth = ByteIO::RdU16BE(data + pos + 8);
        const uint32_t idPtr = ByteIO::RdU32BE(data + pos + 10);
        const uint8_t  pPick = data[pos + 14];
        const uint8_t  pOn   = data[pos + 15];
        pos += 20u;
        if (w == 0 || h == 0 || w > 1024u || h > 1024u || depth > 8u) return false;
        if (idPtr != 0u && depth > 0u) {
            const size_t rwb = ((static_cast<size_t>(w) + 15u) / 16u) * 2u;
            const size_t pb  = rwb * h;
            const size_t totalPb = pb * depth;
            if (totalPb > size - pos) return false;
            if (capture) {
                hasClassicImg = true;
                imgW = w; imgH = h; imgDepth = depth;
                planePick = pPick; planeOnOff = pOn;
                planarPtr = data + pos;
                rowWordBytes = rwb;
                planeBytes = pb;
            }
            pos += totalPb;
        }
        return true;
    };

    if (gadgetRender != 0u && !skipOrCapturePlanarImage(true)) return false;
    if (selectRender != 0u && (gadgetFlags & 0x0002u) != 0u) {
        (void)skipOrCapturePlanarImage(false);
    }

    auto skipBstr = [&]() -> bool {
        if (pos + 4u > size) return false;
        const uint32_t len = ByteIO::RdU32BE(data + pos);
        pos += 4u;
        if (len > size - pos) return false;
        pos += len;
        return true;
    };

    if (defaultTool != 0u) {
        (void)skipBstr();
    }

    std::vector<std::string> newIconsIm1;
    if (toolTypes != 0u && pos + 4u <= size) {
        const uint32_t rawCnt = ByteIO::RdU32BE(data + pos);
        pos += 4u;
        const uint32_t numStrs = (rawCnt >= 4u) ? ((rawCnt / 4u) - 1u) : 0u;
        for (uint32_t i = 0; i < std::min<uint32_t>(numStrs, 512u) && pos + 4u <= size; ++i) {
            const uint32_t slen = ByteIO::RdU32BE(data + pos);
            pos += 4u;
            if (slen > size - pos) { pos = size; break; }
            std::string_view sv(reinterpret_cast<const char*>(data + pos), slen);
            while (!sv.empty() && sv.back() == '\0') sv.remove_suffix(1);
            if (sv.size() > 4u && sv.substr(0, 4u) == "IM1=") {
                newIconsIm1.emplace_back(sv.substr(4u));
            }
            pos += slen;
        }
    }

    if (toolWindow != 0u && pos + 4u <= size) {
        (void)skipBstr();
    }
    if (drawerData != 0u && (userData & 1u) != 0u && pos + 6u <= size) {
        pos += 6u; // DrawerData2 (OS 2.x+)
    }

    // 1. Prefer appended AmigaOS 3.5+ GlowIcons IFF "FORM....ICON" container when present
    for (size_t fp = pos; fp + 16u <= size; ++fp) {
        if (data[fp] == 'F' && std::memcmp(data + fp, "FORM", 4) == 0 && std::memcmp(data + fp + 8, "ICON", 4) == 0) {
            if (DetailAmigaIcon::TryDecodeGlowIconsForm(data + fp, size - fp, outInfo)) {
                return true;
            }
            break;
        }
    }

    // 2. Prefer NewIcons IM1= 7-bit ASCII RLE ToolTypes when present
    if (!newIconsIm1.empty() && DetailAmigaIcon::TryDecodeNewIconsToolTypes(newIconsIm1, outInfo)) {
        return true;
    }

    // 3. Classic Amiga Planar GadgetRender Bitplanes with Workbench 1.x / 2.0+ MagicWB palette
    if (!hasClassicImg || !planarPtr) return false;

    // Workbench 1.3 (UserData == 0) vs Workbench 2.0+ / MagicWB 8-color palette (UserData != 0)
    static constexpr uint8_t kWb13Pal[8][3] = {
        { 0x00, 0x55, 0xAA }, { 0xFF, 0xFF, 0xFF }, { 0x00, 0x00, 0x22 }, { 0xFF, 0x88, 0x00 },
        { 0x7B, 0x7B, 0x7B }, { 0xAF, 0xAF, 0xAF }, { 0xAA, 0x90, 0x7C }, { 0xFF, 0xA9, 0x97 }
    };
    static constexpr uint8_t kWb20Pal[8][3] = {
        { 0x95, 0x95, 0x95 }, { 0x00, 0x00, 0x00 }, { 0xFF, 0xFF, 0xFF }, { 0x3B, 0x67, 0xA2 },
        { 0x7B, 0x7B, 0x7B }, { 0xAF, 0xAF, 0xAF }, { 0xAA, 0x90, 0x7C }, { 0xFF, 0xA9, 0x97 }
    };
    const auto& pal = (userData == 0u) ? kWb13Pal : kWb20Pal;

    const size_t nPx = static_cast<size_t>(imgW) * imgH;
    outInfo = {};
    outInfo.width = imgW;
    outInfo.height = imgH;
    outInfo.origWidth = imgW;
    outInfo.origHeight = imgH;
    outInfo.channels = 4u;
    outInfo.bitsPerChannel = 8u;
    outInfo.formatName = L"Amiga Workbench Planar Icon (.info)";
    outInfo.bgraPixels.resize(nPx * 4u);

    for (uint16_t y = 0; y < imgH; ++y) {
        for (uint16_t x = 0; x < imgW; ++x) {
            uint8_t idx = 0;
            uint16_t srcPlane = 0;
            for (uint8_t bit = 0; bit < 8u; ++bit) {
                if ((planePick & (1u << bit)) != 0u) {
                    if (srcPlane < imgDepth) {
                        const uint8_t byteVal = planarPtr[srcPlane * planeBytes + static_cast<size_t>(y) * rowWordBytes + (x >> 3u)];
                        if ((byteVal & (0x80u >> (x & 7u))) != 0u) {
                            idx |= static_cast<uint8_t>(1u << bit);
                        }
                        ++srcPlane;
                    }
                } else if ((planeOnOff & (1u << bit)) != 0u) {
                    idx |= static_cast<uint8_t>(1u << bit);
                }
            }
            const uint8_t cIdx = idx & 7u;
            const uint8_t a = (idx == 0u) ? 0u : 255u;
            WritePremulBGRA(&outInfo.bgraPixels[(static_cast<size_t>(y) * imgW + x) * 4u],
                            pal[cIdx][0], pal[cIdx][1], pal[cIdx][2], a);
        }
    }
    outInfo.valid = true;
    return true;
}

} // namespace Formats
