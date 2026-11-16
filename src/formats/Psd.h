#pragma once
// ============================================================================
// 2. ADOBE PHOTOSHOP PSD / PSB (.psd, .psb) — #4 Full Upgrade:
//    - Layer-Stack Compositor (Normal, Multiply, Screen, Overlay, Add, Darken, Lighten, Difference + Layer/User Masks)
//      for PSD/PSB files saved without Maximize Compatibility or with blank merged stubs
//    - Compressions: 0 (Raw), 1 (PackBits RLE), 2 (ZIP), 3 (ZIP with Horizontal / Float Byte-Shuffled Prediction)
//    - Bit Depths: 1-bit, 8-bit, 16-bit, and 32-bit IEEE-754 Float HDR
//    - Color Modes: Bitmap(0), Grayscale(1), Indexed(2), RGB(3), CMYK(4), Multichannel(7), Duotone(8), CIE Lab(9)
//    - Unicode 'luni' UTF-16BE layer names + PSB 64-bit ('8B64') tagged blocks
// ============================================================================

namespace PsdDetail {

inline float ReadF32BE(const uint8_t* p) {
    uint32_t u = ReadU32BE(p);
    float f = 0.0f;
    std::memcpy(&f, &u, 4);
    return f;
}

inline void ConvertRawChannelBytesToFloat(
    const uint8_t* rawBytes,
    uint32_t w,
    uint32_t h,
    uint16_t depth,
    bool predictedZip,
    std::vector<float>& outPlane)
{
    size_t nPx = static_cast<size_t>(w) * h;
    outPlane.assign(nPx, 0.0f);
    if (w == 0 || h == 0) return;

    if (depth == 1) {
        size_t rowBytes = (w + 7u) / 8u;
        for (uint32_t y = 0; y < h; ++y) {
            const uint8_t* r = rawBytes + static_cast<size_t>(y) * rowBytes;
            for (uint32_t x = 0; x < w; ++x) {
                bool bit = (r[x >> 3] & (0x80u >> (x & 7u))) != 0;
                outPlane[static_cast<size_t>(y) * w + x] = bit ? 0.0f : 1.0f;
            }
        }
        return;
    }

    if (depth == 8) {
        for (uint32_t y = 0; y < h; ++y) {
            const uint8_t* r = rawBytes + static_cast<size_t>(y) * w;
            uint8_t acc = 0;
            for (uint32_t x = 0; x < w; ++x) {
                acc = predictedZip ? static_cast<uint8_t>(acc + r[x]) : r[x];
                outPlane[static_cast<size_t>(y) * w + x] = static_cast<float>(acc) * (1.0f / 255.0f);
            }
        }
        return;
    }

    if (depth == 16) {
        size_t rowBytes = static_cast<size_t>(w) * 2u;
        for (uint32_t y = 0; y < h; ++y) {
            const uint8_t* r = rawBytes + static_cast<size_t>(y) * rowBytes;
            uint16_t acc = 0;
            for (uint32_t x = 0; x < w; ++x) {
                uint16_t v = ReadU16BE(r + static_cast<size_t>(x) * 2u);
                acc = predictedZip ? static_cast<uint16_t>(acc + v) : v;
                outPlane[static_cast<size_t>(y) * w + x] = static_cast<float>(acc) * (1.0f / 65535.0f);
            }
        }
        return;
    }

    if (depth == 32) {
        size_t rowBytes = static_cast<size_t>(w) * 4u;
        std::vector<uint8_t> rowBuf(rowBytes);
        for (uint32_t y = 0; y < h; ++y) {
            const uint8_t* r = rawBytes + static_cast<size_t>(y) * rowBytes;
            if (predictedZip) {
                // Adobe PSD/TIFF Predictor 3: horizontal byte delta across 4 byte-transposed planes [MSB..LSB]
                rowBuf[0] = r[0];
                for (size_t i = 1; i < rowBytes; ++i) {
                    rowBuf[i] = static_cast<uint8_t>(rowBuf[i - 1] + r[i]);
                }
                for (uint32_t x = 0; x < w; ++x) {
                    uint32_t bits = (static_cast<uint32_t>(rowBuf[x]) << 24) |
                                    (static_cast<uint32_t>(rowBuf[static_cast<size_t>(w) + x]) << 16) |
                                    (static_cast<uint32_t>(rowBuf[static_cast<size_t>(w) * 2u + x]) << 8) |
                                    static_cast<uint32_t>(rowBuf[static_cast<size_t>(w) * 3u + x]);
                    float fv = 0.0f;
                    std::memcpy(&fv, &bits, 4);
                    outPlane[static_cast<size_t>(y) * w + x] = std::isfinite(fv) ? fv : 0.0f;
                }
            } else {
                for (uint32_t x = 0; x < w; ++x) {
                    float fv = ReadF32BE(r + static_cast<size_t>(x) * 4u);
                    outPlane[static_cast<size_t>(y) * w + x] = std::isfinite(fv) ? fv : 0.0f;
                }
            }
        }
    }
}

inline bool DecodePsdPackBitsPlane(
    const uint8_t* data,
    size_t availLen,
    bool isPsb,
    uint32_t w,
    uint32_t h,
    uint16_t depth,
    size_t& bytesConsumed,
    std::vector<float>& outPlane)
{
    size_t cntEntry = isPsb ? 4u : 2u;
    if (static_cast<size_t>(h) * cntEntry > availLen) return false;
    size_t rowBytes = (depth == 1) ? ((w + 7u) / 8u) : (static_cast<size_t>(w) * (depth / 8u));
    std::vector<uint8_t> unpacked(rowBytes * h, 0);

    size_t rlePos = static_cast<size_t>(h) * cntEntry;
    for (uint32_t y = 0; y < h; ++y) {
        uint32_t lineLen = isPsb ? ReadU32BE(data + static_cast<size_t>(y) * 4u)
                                 : ReadU16BE(data + static_cast<size_t>(y) * 2u);
        if (lineLen > availLen - rlePos) return false;
        const uint8_t* sp = data + rlePos;
        const uint8_t* sEnd = sp + lineLen;
        uint8_t* dstRow = unpacked.data() + static_cast<size_t>(y) * rowBytes;
        size_t ox = 0;
        while (sp < sEnd && ox < rowBytes) {
            int8_t hdr = static_cast<int8_t>(*sp++);
            if (hdr >= 0) {
                size_t n = static_cast<size_t>(hdr) + 1u;
                size_t copyN = std::min({ n, static_cast<size_t>(sEnd - sp), rowBytes - ox });
                std::memcpy(dstRow + ox, sp, copyN);
                sp += copyN;
                ox += copyN;
            } else if (hdr != -128) {
                size_t n = static_cast<size_t>(1 - static_cast<int>(hdr));
                if (sp >= sEnd) break;
                uint8_t val = *sp++;
                size_t fillN = std::min(n, rowBytes - ox);
                std::memset(dstRow + ox, val, fillN);
                ox += fillN;
            }
        }
        rlePos += lineLen;
    }
    bytesConsumed = rlePos;
    ConvertRawChannelBytesToFloat(unpacked.data(), w, h, depth, false, outPlane);
    return true;
}

inline bool DecodePsdSingleLayerChannel(
    const uint8_t* chData,
    size_t chLen,
    bool isPsb,
    uint32_t w,
    uint32_t h,
    uint16_t depth,
    std::vector<float>& outPlane)
{
    if (w == 0 || h == 0) {
        outPlane.clear();
        return true;
    }
    if (!chData || chLen < 2) return false;
    uint16_t comp = ReadU16BE(chData);
    const uint8_t* body = chData + 2;
    size_t bodyLen = chLen - 2;
    size_t rowBytes = (depth == 1) ? ((w + 7u) / 8u) : (static_cast<size_t>(w) * (depth / 8u));
    size_t expectedBytes = rowBytes * h;

    if (comp == 0) {
        if (bodyLen < expectedBytes) return false;
        ConvertRawChannelBytesToFloat(body, w, h, depth, false, outPlane);
        return true;
    }
    if (comp == 1) {
        size_t consumed = 0;
        return DecodePsdPackBitsPlane(body, bodyLen, isPsb, w, h, depth, consumed, outPlane);
    }
    if (comp == 2 || comp == 3) {
        std::vector<uint8_t> inf;
        if (!ZipDeflate::InflateZlib(body, bodyLen, inf, expectedBytes + 64)) {
            if (!ZipDeflate::InflateRaw(body, bodyLen, inf, expectedBytes + 64)) return false;
        }
        if (inf.size() < expectedBytes) return false;
        ConvertRawChannelBytesToFloat(inf.data(), w, h, depth, comp == 3, outPlane);
        return true;
    }
    return false;
}

inline void ColorSpaceToRgbaFloat(
    uint16_t colorMode,
    const uint8_t* indexedPalette,
    float c0, float c1, float c2, float c3,
    bool hasC1, bool hasC2, bool hasC3,
    float alpha,
    float& outR, float& outG, float& outB, float& outA)
{
    outA = std::clamp(alpha, 0.0f, 1.0f);
    if (colorMode == 0 || colorMode == 1 || colorMode == 8) { // Bitmap / Grayscale / Duotone
        outR = outG = outB = c0;
    } else if (colorMode == 2 && indexedPalette) { // Indexed
        int idx = std::clamp(static_cast<int>(c0 * 255.0f + 0.5f), 0, 255);
        outR = static_cast<float>(indexedPalette[idx]) * (1.0f / 255.0f);
        outG = static_cast<float>(indexedPalette[256 + idx]) * (1.0f / 255.0f);
        outB = static_cast<float>(indexedPalette[512 + idx]) * (1.0f / 255.0f);
    } else if (colorMode == 4 && hasC1 && hasC2 && hasC3) { // Photoshop CMYK (0 = 100% ink, 1 = 0% ink)
        float k = std::clamp(c3, 0.0f, 1.0f);
        outR = std::clamp(c0, 0.0f, 1.0f) * k;
        outG = std::clamp(c1, 0.0f, 1.0f) * k;
        outB = std::clamp(c2, 0.0f, 1.0f) * k;
    } else if (colorMode == 9 && hasC1 && hasC2) { // CIE Lab (D50 PCS -> sRGB)
        float L = std::clamp(c0, 0.0f, 1.0f) * 100.0f;
        float a = c1 * 255.0f - 128.0f;
        float b = c2 * 255.0f - 128.0f;
        float fy = (L + 16.0f) / 116.0f;
        float fx = fy + (a / 500.0f);
        float fz = fy - (b / 200.0f);
        auto finv = [](float t) -> float {
            return (t > 0.20689655f) ? (t * t * t) : ((t - 16.0f / 116.0f) / 7.787f);
        };
        // D50 reference white -> Bradford chromatic adaptation to D65 sRGB
        float X = 0.96422f * finv(fx);
        float Y = 1.00000f * finv(fy);
        float Z = 0.82521f * finv(fz);
        float rl =  3.1338561f * X - 1.6168667f * Y - 0.4906146f * Z;
        float gl = -0.9787684f * X + 1.9161415f * Y + 0.0334540f * Z;
        float bl =  0.0719453f * X - 0.2289914f * Y + 1.4052427f * Z;
        auto toSrgb = [](float u) -> float {
            u = std::clamp(u, 0.0f, 1.0f);
            return (u <= 0.0031308f) ? (12.92f * u) : (1.055f * std::pow(u, 1.0f / 2.4f) - 0.055f);
        };
        outR = toSrgb(rl); outG = toSrgb(gl); outB = toSrgb(bl);
    } else { // RGB / Multichannel
        outR = c0;
        outG = hasC1 ? c1 : c0;
        outB = hasC2 ? c2 : outG;
    }
}

inline float BlendChannel(uint32_t blendKey, float dst, float src) {
    float d = std::clamp(dst, 0.0f, 1.0f);
    float s = std::clamp(src, 0.0f, 1.0f);
    switch (blendKey) {
        case 0x6D756C20u: // 'mul ' Multiply
            return d * s;
        case 0x7363726Eu: // 'scrn' Screen
            return 1.0f - (1.0f - d) * (1.0f - s);
        case 0x6F766572u: // 'over' Overlay
            return (d < 0.5f) ? (2.0f * d * s) : (1.0f - 2.0f * (1.0f - d) * (1.0f - s));
        case 0x6C646467u: // 'lddg' Linear Dodge (Add)
        case 0x61646420u: // 'add '
            return (dst > 1.0f || src > 1.0f) ? (dst + src) : std::min(1.0f, dst + src);
        case 0x6461726Bu: // 'dark' Darken
            return std::min(dst, src);
        case 0x6C697465u: // 'lite' Lighten
            return std::max(dst, src);
        case 0x64696666u: // 'diff' Difference
            return std::fabs(d - s);
        default:          // 'norm' Normal
            return src;
    }
}

struct PsdLayerRec {
    int32_t top = 0, left = 0, bottom = 0, right = 0;
    int32_t mTop = 0, mLeft = 0, mBottom = 0, mRight = 0;
    uint8_t maskDefault = 255;
    bool hasUserMask = false;
    bool maskDisabled = false;
    uint32_t blendKey = 0x6E6F726Du; // 'norm'
    uint8_t opacity = 255;
    uint8_t flags = 0;
    bool isSectionDivider = false;
    bool hasAdjustmentTag = false;
    std::vector<std::pair<int16_t, uint64_t>> chSpecs;
};

} // namespace PsdDetail

bool DecodePSD(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 26 || std::memcmp(data, "8BPS", 4) != 0) return false;

    uint16_t version = ReadU16BE(data + 4);
    if (version != 1 && version != 2) return false;
    bool isPsb = (version == 2);

    uint16_t channels  = ReadU16BE(data + 12);
    uint32_t height    = ReadU32BE(data + 14);
    uint32_t width     = ReadU32BE(data + 18);
    uint16_t depth     = ReadU16BE(data + 22);
    uint16_t colorMode = ReadU16BE(data + 24);

    if (width == 0 || height == 0 || width > kMaxRasterDimension || height > kMaxRasterDimension) return false;
    if (channels == 0 || channels > 64) return false;
    if (depth != 1 && depth != 8 && depth != 16 && depth != 32) return false;

    size_t pos = 26;
    if (pos + 4 > size) return false;

    // 1. Color Mode Data Section
    uint32_t colorModeLen = ReadU32BE(data + pos);
    pos += 4;
    if (colorModeLen > size - pos) return false;
    const uint8_t* indexedPalette = (colorMode == 2 && colorModeLen >= 768) ? (data + pos) : nullptr;
    if (colorMode == 2 && !indexedPalette) return false;
    pos += colorModeLen;

    // 2. Image Resources Section
    if (pos + 4 > size) return false;
    uint32_t imgResLen = ReadU32BE(data + pos);
    pos += 4;
    if (imgResLen > size - pos) return false;
    pos += imgResLen;

    // 3. Layer and Mask Information Section
    if (pos + (isPsb ? 8u : 4u) > size) return false;
    uint64_t layerMaskLen = isPsb ? ReadU64BE(data + pos) : ReadU32BE(data + pos);
    pos += (isPsb ? 8u : 4u);
    if (layerMaskLen > size - pos) return false;
    size_t compositePos = pos + static_cast<size_t>(layerMaskLen);

    std::vector<PsdDetail::PsdLayerRec> layers;
    size_t layerChannelDataStart = 0;
    size_t layerInfoEnd = 0;
    bool mergedHasAlpha = false;
    bool anyAdjustmentLayer = false;

    if (layerMaskLen >= (isPsb ? 10u : 6u)) {
        uint64_t layerInfoLen = isPsb ? ReadU64BE(data + pos) : ReadU32BE(data + pos);
        size_t lpos = pos + (isPsb ? 8u : 4u);
        if (layerInfoLen <= compositePos - lpos && layerInfoLen >= 2) {
            layerInfoEnd = lpos + static_cast<size_t>(layerInfoLen);
            int16_t rawLayerCount = ReadI16BE(data + lpos);
            lpos += 2;
            if (rawLayerCount < 0) mergedHasAlpha = true;
            int layerCount = std::abs(static_cast<int>(rawLayerCount));

            bool recsValid = true;
            for (int i = 0; i < layerCount && i < 1024; ++i) {
                if (lpos + 18 > layerInfoEnd) { recsValid = false; break; }
                PsdDetail::PsdLayerRec rec;
                rec.top    = static_cast<int32_t>(ReadU32BE(data + lpos + 0));
                rec.left   = static_cast<int32_t>(ReadU32BE(data + lpos + 4));
                rec.bottom = static_cast<int32_t>(ReadU32BE(data + lpos + 8));
                rec.right  = static_cast<int32_t>(ReadU32BE(data + lpos + 12));
                uint16_t numCh = ReadU16BE(data + lpos + 16);
                lpos += 18;

                size_t chEntryLen = isPsb ? 10u : 6u;
                if (numCh > 64 || static_cast<size_t>(numCh) * chEntryLen + 16 > layerInfoEnd - lpos) {
                    recsValid = false;
                    break;
                }
                for (uint16_t c = 0; c < numCh; ++c) {
                    int16_t cid = ReadI16BE(data + lpos);
                    uint64_t clen = isPsb ? ReadU64BE(data + lpos + 2) : ReadU32BE(data + lpos + 2);
                    rec.chSpecs.push_back({ cid, clen });
                    lpos += chEntryLen;
                }
                if (std::memcmp(data + lpos, "8BIM", 4) != 0) { recsValid = false; break; }
                rec.blendKey = ReadU32BE(data + lpos + 4);
                rec.opacity  = data[lpos + 8];
                rec.flags    = data[lpos + 10];
                uint32_t extraLen = ReadU32BE(data + lpos + 12);
                lpos += 16;
                if (extraLen > layerInfoEnd - lpos) { recsValid = false; break; }
                size_t nextLayerPos = lpos + extraLen;

                // Parse Mask Data + Blending Ranges + Pascal Name + Additional Layer Info ('luni', 'lsct')
                size_t epos = lpos;
                std::wstring layerName;
                if (epos + 4 <= nextLayerPos) {
                    uint32_t maskDataLen = ReadU32BE(data + epos);
                    epos += 4;
                    if (maskDataLen <= nextLayerPos - epos) {
                        if (maskDataLen >= 20) {
                            rec.mTop    = static_cast<int32_t>(ReadU32BE(data + epos + 0));
                            rec.mLeft   = static_cast<int32_t>(ReadU32BE(data + epos + 4));
                            rec.mBottom = static_cast<int32_t>(ReadU32BE(data + epos + 8));
                            rec.mRight  = static_cast<int32_t>(ReadU32BE(data + epos + 12));
                            rec.maskDefault = data[epos + 16];
                            uint8_t mFlags  = data[epos + 17];
                            rec.hasUserMask = (rec.mRight > rec.mLeft && rec.mBottom > rec.mTop);
                            rec.maskDisabled = (mFlags & 0x02u) != 0;
                        }
                        epos += maskDataLen;
                        if (epos + 4 <= nextLayerPos) {
                            uint32_t blendRangesLen = ReadU32BE(data + epos);
                            epos += 4;
                            if (blendRangesLen <= nextLayerPos - epos) {
                                epos += blendRangesLen;
                                if (epos < nextLayerPos) {
                                    uint8_t pLen = data[epos];
                                    if (1u + pLen <= nextLayerPos - epos && pLen > 0) {
                                        std::string pname(reinterpret_cast<const char*>(data + epos + 1), pLen);
                                        layerName = PathUtils::Utf8ToWide(pname);
                                    }
                                    size_t paddedPascal = ((static_cast<size_t>(pLen) + 1u + 3u) & ~size_t(3u));
                                    epos = std::min(nextLayerPos, epos + paddedPascal);

                                    // Walk tagged blocks ('8BIM' / '8B64')
                                    while (epos + 12 <= nextLayerPos) {
                                        bool sig8bim = (std::memcmp(data + epos, "8BIM", 4) == 0);
                                        bool sig8b64 = (std::memcmp(data + epos, "8B64", 4) == 0);
                                        if (!sig8bim && !sig8b64) break;
                                        char tkey[5] = {};
                                        std::memcpy(tkey, data + epos + 4, 4);
                                        bool use64 = isPsb && (sig8b64 ||
                                            std::strcmp(tkey, "LMsk") == 0 || std::strcmp(tkey, "Lr16") == 0 ||
                                            std::strcmp(tkey, "Lr32") == 0 || std::strcmp(tkey, "Layr") == 0 ||
                                            std::strcmp(tkey, "Mt16") == 0 || std::strcmp(tkey, "Mt32") == 0 ||
                                            std::strcmp(tkey, "Mtrn") == 0 || std::strcmp(tkey, "Alph") == 0 ||
                                            std::strcmp(tkey, "FMsk") == 0 || std::strcmp(tkey, "lnk2") == 0 ||
                                            std::strcmp(tkey, "FEid") == 0 || std::strcmp(tkey, "FXid") == 0 ||
                                            std::strcmp(tkey, "PxSD") == 0);
                                        size_t tHdr = use64 ? 16u : 12u;
                                        if (epos + tHdr > nextLayerPos) break;
                                        uint64_t tLen = use64 ? ReadU64BE(data + epos + 8) : ReadU32BE(data + epos + 8);
                                        size_t tBody = epos + tHdr;
                                        if (tLen > nextLayerPos - tBody) break;

                                        if (std::strcmp(tkey, "luni") == 0 && tLen >= 4) {
                                            uint32_t uChars = ReadU32BE(data + tBody);
                                            if (uChars > 0 && uChars <= 512 && 4ULL + static_cast<uint64_t>(uChars) * 2ULL <= tLen) {
                                                std::wstring uName;
                                                uName.reserve(uChars);
                                                for (uint32_t u = 0; u < uChars; ++u) {
                                                    wchar_t wc = static_cast<wchar_t>(ReadU16BE(data + tBody + 4 + u * 2));
                                                    if (wc == 0) break;
                                                    uName.push_back(wc);
                                                }
                                                if (!uName.empty()) layerName = std::move(uName);
                                            }
                                        } else if ((std::strcmp(tkey, "lsct") == 0 || std::strcmp(tkey, "lsdk") == 0) && tLen >= 4) {
                                            uint32_t divType = ReadU32BE(data + tBody);
                                            if (divType >= 1 && divType <= 3) rec.isSectionDivider = true;
                                        } else if (std::strcmp(tkey, "levl") == 0 || std::strcmp(tkey, "curv") == 0 ||
                                                   std::strcmp(tkey, "hue ") == 0 || std::strcmp(tkey, "hue2") == 0 ||
                                                   std::strcmp(tkey, "brit") == 0 || std::strcmp(tkey, "nvrt") == 0) {
                                            rec.hasAdjustmentTag = true;
                                            anyAdjustmentLayer = true;
                                        }

                                        size_t paddedT = static_cast<size_t>((tLen + 1ULL) & ~1ULL);
                                        if (paddedT > nextLayerPos - tBody) break;
                                        epos = tBody + paddedT;
                                    }
                                }
                            }
                        }
                    }
                }
                if (!layerName.empty() && !rec.isSectionDivider) {
                    outInfo.layerNames.push_back(std::move(layerName));
                }
                layers.push_back(std::move(rec));
                lpos = nextLayerPos;
            }
            if (recsValid && static_cast<int>(layers.size()) == layerCount) {
                layerChannelDataStart = lpos;
            } else {
                layers.clear();
            }
        }
    }

    size_t totalPixels = static_cast<size_t>(width) * height;
    outInfo.width = width;
    outInfo.height = height;
    outInfo.origWidth = width;
    outInfo.origHeight = height;
    outInfo.channels = channels;
    outInfo.bitsPerChannel = depth;
    outInfo.formatName = isPsb ? L"Photoshop Big PSB" : L"Photoshop PSD";
    if (depth > 8) outInfo.formatName += L" " + std::to_wstring(depth) + L"-bit";
    if (!outInfo.layerNames.empty()) {
        outInfo.formatName += L" (" + std::to_wstring(outInfo.layerNames.size()) + L" layers)";
    }

    // Helper: composite layer stack into floatRgba [width * height * 4]
    auto tryCompositeLayerStack = [&](std::vector<float>& outCanvas) -> bool {
        if (layers.empty() || layerChannelDataStart == 0 || layerChannelDataStart > layerInfoEnd) return false;
        outCanvas.assign(totalPixels * 4, 0.0f);
        size_t cdPos = layerChannelDataStart;
        bool drewAnyVisiblePixel = false;

        for (const auto& ly : layers) {
            int64_t lw64 = static_cast<int64_t>(ly.right) - static_cast<int64_t>(ly.left);
            int64_t lh64 = static_cast<int64_t>(ly.bottom) - static_cast<int64_t>(ly.top);
            bool visible = ((ly.flags & 0x02u) == 0) && !ly.isSectionDivider && ly.opacity > 0 && lw64 > 0 && lh64 > 0 &&
                           lw64 <= kMaxRasterDimension && lh64 <= kMaxRasterDimension;

            uint32_t lw = (lw64 > 0 && lw64 <= kMaxRasterDimension) ? static_cast<uint32_t>(lw64) : 0u;
            uint32_t lh = (lh64 > 0 && lh64 <= kMaxRasterDimension) ? static_cast<uint32_t>(lh64) : 0u;
            int64_t mw64 = static_cast<int64_t>(ly.mRight) - static_cast<int64_t>(ly.mLeft);
            int64_t mh64 = static_cast<int64_t>(ly.mBottom) - static_cast<int64_t>(ly.mTop);
            uint32_t mw = (ly.hasUserMask && mw64 > 0 && mw64 <= kMaxRasterDimension) ? static_cast<uint32_t>(mw64) : 0u;
            uint32_t mh = (ly.hasUserMask && mh64 > 0 && mh64 <= kMaxRasterDimension) ? static_cast<uint32_t>(mh64) : 0u;

            std::vector<float> chPlane[4];
            bool hasCh[4] = {};
            std::vector<float> alphaPlane, maskPlane;
            bool hasAlpha = false, hasMask = false;

            for (const auto& cs : ly.chSpecs) {
                int16_t cid = cs.first;
                uint64_t clen = cs.second;
                if (clen > layerInfoEnd - cdPos) return false;
                if (visible) {
                    if (cid >= 0 && cid < 4 && lw > 0 && lh > 0) {
                        if (!PsdDetail::DecodePsdSingleLayerChannel(data + cdPos, static_cast<size_t>(clen), isPsb, lw, lh, depth, chPlane[cid])) return false;
                        hasCh[cid] = true;
                    } else if (cid == -1 && lw > 0 && lh > 0) {
                        if (!PsdDetail::DecodePsdSingleLayerChannel(data + cdPos, static_cast<size_t>(clen), isPsb, lw, lh, depth, alphaPlane)) return false;
                        hasAlpha = true;
                    } else if (cid == -2 && mw > 0 && mh > 0 && !ly.maskDisabled) {
                        if (!PsdDetail::DecodePsdSingleLayerChannel(data + cdPos, static_cast<size_t>(clen), isPsb, mw, mh, depth, maskPlane)) return false;
                        hasMask = true;
                    }
                }
                cdPos += static_cast<size_t>(clen);
            }

            if (!visible || !hasCh[0]) continue;
            float opScale = static_cast<float>(ly.opacity) * (1.0f / 255.0f);
            float maskDef = static_cast<float>(ly.maskDefault) * (1.0f / 255.0f);

            int32_t xStart = std::max<int32_t>(0, ly.left);
            int32_t yStart = std::max<int32_t>(0, ly.top);
            int32_t xEnd   = std::min<int32_t>(static_cast<int32_t>(width), ly.right);
            int32_t yEnd   = std::min<int32_t>(static_cast<int32_t>(height), ly.bottom);

            for (int32_t cy = yStart; cy < yEnd; ++cy) {
                uint32_t lyY = static_cast<uint32_t>(cy - ly.top);
                for (int32_t cx = xStart; cx < xEnd; ++cx) {
                    uint32_t lyX = static_cast<uint32_t>(cx - ly.left);
                    size_t si = static_cast<size_t>(lyY) * lw + lyX;

                    float srcA = hasAlpha ? std::clamp(alphaPlane[si], 0.0f, 1.0f) : 1.0f;
                    if (hasMask) {
                        if (cx >= ly.mLeft && cx < ly.mRight && cy >= ly.mTop && cy < ly.mBottom) {
                            size_t mi = static_cast<size_t>(cy - ly.mTop) * mw + static_cast<size_t>(cx - ly.mLeft);
                            srcA *= std::clamp(maskPlane[mi], 0.0f, 1.0f);
                        } else {
                            srcA *= maskDef;
                        }
                    }
                    srcA *= opScale;
                    if (srcA <= 0.0f) continue;

                    float sr = 0.0f, sg = 0.0f, sb = 0.0f, sa = 1.0f;
                    PsdDetail::ColorSpaceToRgbaFloat(
                        colorMode, indexedPalette,
                        chPlane[0][si],
                        hasCh[1] ? chPlane[1][si] : 0.0f,
                        hasCh[2] ? chPlane[2][si] : 0.0f,
                        hasCh[3] ? chPlane[3][si] : 0.0f,
                        hasCh[1], hasCh[2], hasCh[3],
                        srcA, sr, sg, sb, sa);

                    float* dst = &outCanvas[(static_cast<size_t>(cy) * width + static_cast<size_t>(cx)) * 4];
                    float da = dst[3];
                    float outA = sa + da * (1.0f - sa);
                    if (outA > 1e-6f) {
                        float br = (da > 1e-5f) ? PsdDetail::BlendChannel(ly.blendKey, dst[0], sr) : sr;
                        float bg = (da > 1e-5f) ? PsdDetail::BlendChannel(ly.blendKey, dst[1], sg) : sg;
                        float bb = (da > 1e-5f) ? PsdDetail::BlendChannel(ly.blendKey, dst[2], sb) : sb;
                        // Porter-Duff source-over with blend function B(Cb, Cs):
                        // Co = ((1 - da)*sa*Cs + da*sa*B(Cb, Cs) + (1 - sa)*da*Cb) / outA
                        dst[0] = ((1.0f - da) * sa * sr + da * sa * br + (1.0f - sa) * da * dst[0]) / outA;
                        dst[1] = ((1.0f - da) * sa * sg + da * sa * bg + (1.0f - sa) * da * dst[1]) / outA;
                        dst[2] = ((1.0f - da) * sa * sb + da * sa * bb + (1.0f - sa) * da * dst[2]) / outA;
                        dst[3] = outA;
                        drewAnyVisiblePixel = true;
                    }
                }
            }
        }
        return drewAnyVisiblePixel;
    };

    // Helper: decode Merged Composite Image Data Section into floatRgba [width * height * 4]
    auto tryDecodeMergedComposite = [&](std::vector<float>& outCanvas, bool& isAllWhiteStub) -> bool {
        isAllWhiteStub = false;
        if (compositePos + 2 > size) return false;
        uint16_t compression = ReadU16BE(data + compositePos);
        size_t cpos = compositePos + 2;
        size_t avail = size - cpos;

        size_t rowBytes = (depth == 1) ? ((width + 7u) / 8u) : (static_cast<size_t>(width) * (depth / 8u));
        size_t planeBytes = rowBytes * height;
        uint32_t needCh = (colorMode == 4) ? std::min<uint32_t>(channels, 5u) : std::min<uint32_t>(channels, 4u);
        std::vector<std::vector<float>> planes(needCh);

        if (compression == 0) {
            if (avail < planeBytes * needCh) return false;
            for (uint32_t ch = 0; ch < needCh; ++ch) {
                PsdDetail::ConvertRawChannelBytesToFloat(data + cpos + ch * planeBytes, width, height, depth, false, planes[ch]);
            }
        } else if (compression == 1) {
            size_t cntEntry = isPsb ? 4u : 2u;
            size_t totalLines = static_cast<size_t>(channels) * height;
            if (avail < totalLines * cntEntry) return false;
            size_t rleDataPos = cpos + totalLines * cntEntry;
            for (uint32_t ch = 0; ch < channels; ++ch) {
                size_t chRleBytes = 0;
                for (uint32_t y = 0; y < height; ++y) {
                    size_t idx = static_cast<size_t>(ch) * height + y;
                    uint32_t lb = isPsb ? ReadU32BE(data + cpos + idx * 4u)
                                        : ReadU16BE(data + cpos + idx * 2u);
                    chRleBytes += lb;
                }
                if (chRleBytes > size - rleDataPos) return false;
                if (ch < needCh) {
                    std::vector<uint8_t> unpacked(planeBytes, 0);
                    size_t lp = rleDataPos;
                    for (uint32_t y = 0; y < height; ++y) {
                        size_t idx = static_cast<size_t>(ch) * height + y;
                        uint32_t lb = isPsb ? ReadU32BE(data + cpos + idx * 4u)
                                            : ReadU16BE(data + cpos + idx * 2u);
                        const uint8_t* sp = data + lp;
                        const uint8_t* sEnd = sp + lb;
                        uint8_t* dstRow = unpacked.data() + static_cast<size_t>(y) * rowBytes;
                        size_t ox = 0;
                        while (sp < sEnd && ox < rowBytes) {
                            int8_t hdr = static_cast<int8_t>(*sp++);
                            if (hdr >= 0) {
                                size_t n = static_cast<size_t>(hdr) + 1u;
                                size_t cn = std::min({ n, static_cast<size_t>(sEnd - sp), rowBytes - ox });
                                std::memcpy(dstRow + ox, sp, cn);
                                sp += cn; ox += cn;
                            } else if (hdr != -128) {
                                size_t n = static_cast<size_t>(1 - static_cast<int>(hdr));
                                if (sp >= sEnd) break;
                                uint8_t val = *sp++;
                                size_t fn = std::min(n, rowBytes - ox);
                                std::memset(dstRow + ox, val, fn);
                                ox += fn;
                            }
                        }
                        lp += lb;
                    }
                    PsdDetail::ConvertRawChannelBytesToFloat(unpacked.data(), width, height, depth, false, planes[ch]);
                }
                rleDataPos += chRleBytes;
            }
        } else if (compression == 2 || compression == 3) {
            // Merged Composite ZIP / Predicted-ZIP: either one Zlib stream covering all channels, or per-channel Zlib streams
            size_t totalExpected = planeBytes * channels;
            std::vector<uint8_t> infAll;
            if ((ZipDeflate::InflateZlib(data + cpos, avail, infAll, totalExpected + 128) ||
                 ZipDeflate::InflateRaw(data + cpos, avail, infAll, totalExpected + 128)) &&
                infAll.size() >= planeBytes * needCh)
            {
                for (uint32_t ch = 0; ch < needCh; ++ch) {
                    PsdDetail::ConvertRawChannelBytesToFloat(infAll.data() + ch * planeBytes, width, height, depth, compression == 3, planes[ch]);
                }
            } else {
                return false;
            }
        } else {
            return false;
        }

        outCanvas.resize(totalPixels * 4);
        bool allWhite = true;
        for (size_t i = 0; i < totalPixels; ++i) {
            float alpha = 1.0f;
            if ((colorMode == 1 || colorMode == 8) && needCh >= 2) alpha = planes[1][i];
            else if (colorMode == 3 && needCh >= 4 && (mergedHasAlpha || channels >= 4)) alpha = planes[3][i];
            else if (colorMode == 4 && needCh >= 5) alpha = planes[4][i];
            else if (colorMode == 9 && needCh >= 4) alpha = planes[3][i];

            float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
            PsdDetail::ColorSpaceToRgbaFloat(
                colorMode, indexedPalette,
                planes[0][i],
                (needCh >= 2) ? planes[1][i] : 0.0f,
                (needCh >= 3) ? planes[2][i] : 0.0f,
                (needCh >= 4) ? planes[3][i] : 0.0f,
                needCh >= 2, needCh >= 3, needCh >= 4,
                alpha, r, g, b, a);
            outCanvas[i * 4 + 0] = r;
            outCanvas[i * 4 + 1] = g;
            outCanvas[i * 4 + 2] = b;
            outCanvas[i * 4 + 3] = a;
            if (r < 0.995f || g < 0.995f || b < 0.995f || a < 0.995f) allWhite = false;
        }
        isAllWhiteStub = allWhite;
        return true;
    };

    std::vector<float> layerCanvas, mergedCanvas;
    bool okLayers = (!anyAdjustmentLayer) && tryCompositeLayerStack(layerCanvas);
    bool mergedIsWhiteStub = false;
    bool okMerged = tryDecodeMergedComposite(mergedCanvas, mergedIsWhiteStub);

    const std::vector<float>* chosen = nullptr;
    if (okLayers && (!okMerged || mergedIsWhiteStub)) {
        chosen = &layerCanvas;
    } else if (okMerged) {
        chosen = &mergedCanvas;
    } else if (okLayers) {
        chosen = &layerCanvas;
    } else {
        return false;
    }

    GpuTex::ConvertFloatRgbaToBgra(chosen->data(), width, height, false, outInfo.bgraPixels);
    outInfo.valid = true;
    return true;
}
