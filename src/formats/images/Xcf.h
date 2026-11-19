#pragma once

// ============================================================================
// 3. GIMP XCF (.xcf) — v000..v014+ Canvas, Precision, Zlib/RLE Tiles & Masks
// ============================================================================
struct XcfPrecisionInfo {
    uint32_t bytesPerComp = 1;
    bool isFloat = false;
    bool isLinear = false;
};

inline bool ParseXcfPrecision(int xcfVer, uint32_t code, XcfPrecisionInfo& out) {
    if (xcfVer < 4 || code == 0) {
        out = { 1, false, false };
        return true;
    }
    switch (code) {
        // Legacy GIMP 2.9 development XCF v004..v006 codes
        case 1:   out = { 2, false, true  }; return true;
        case 2:   out = { 4, false, true  }; return true;
        case 3:   out = { 2, true,  true  }; return true;
        case 4:   out = { 4, true,  true  }; return true;
        // Standard GIMP 2.10 / 3.0 precision codes (linear / non-linear sRGB / perceptual)
        case 100: out = { 1, false, true  }; return true;
        case 150:
        case 175: out = { 1, false, false }; return true;
        case 200: out = { 2, false, true  }; return true;
        case 250:
        case 275: out = { 2, false, false }; return true;
        case 300: out = { 4, false, true  }; return true;
        case 350:
        case 375: out = { 4, false, false }; return true;
        case 500: out = { 2, true,  true  }; return true;
        case 550:
        case 575: out = { 2, true,  false }; return true;
        case 600: out = { 4, true,  true  }; return true;
        case 650:
        case 675: out = { 4, true,  false }; return true;
        case 700: out = { 8, true,  true  }; return true;
        case 750:
        case 775: out = { 8, true,  false }; return true;
        default:  return false;
    }
}

inline uint8_t XcfFloatToU8(float v, bool linearToSrgb) {
    if (!std::isfinite(v)) return 0;
    v = std::clamp(v, 0.0f, 1.0f);
    if (linearToSrgb) {
        v = (v <= 0.0031308f) ? (12.92f * v) : (1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f);
        v = std::clamp(v, 0.0f, 1.0f);
    }
    return static_cast<uint8_t>(v * 255.0f + 0.5f);
}

inline uint8_t XcfSampleCompU8(const uint8_t* p, const XcfPrecisionInfo& prec, bool isColorChannel) {
    const bool applySrgb = isColorChannel && prec.isLinear;
    if (prec.bytesPerComp == 1) {
        if (!applySrgb) return p[0];
        return XcfFloatToU8(static_cast<float>(p[0]) * (1.0f / 255.0f), true);
    }
    if (prec.bytesPerComp == 2) {
        uint16_t u16 = ReadU16BE(p);
        if (!prec.isFloat) {
            if (!applySrgb) return static_cast<uint8_t>((static_cast<uint32_t>(u16) * 255u + 32767u) / 65535u);
            return XcfFloatToU8(static_cast<float>(u16) * (1.0f / 65535.0f), true);
        }
        return XcfFloatToU8(GpuTex::HalfToFloat(u16), applySrgb);
    }
    if (prec.bytesPerComp == 4) {
        uint32_t u32 = ReadU32BE(p);
        if (!prec.isFloat) {
            float f = static_cast<float>(static_cast<double>(u32) * (1.0 / 4294967295.0));
            return XcfFloatToU8(f, applySrgb);
        }
        float f = 0.0f;
        std::memcpy(&f, &u32, 4);
        return XcfFloatToU8(f, applySrgb);
    }
    if (prec.bytesPerComp == 8) {
        uint64_t u64 = ReadU64BE(p);
        double d = 0.0;
        std::memcpy(&d, &u64, 8);
        if (!std::isfinite(d)) return 0;
        return XcfFloatToU8(static_cast<float>(std::clamp(d, 0.0, 1.0)), applySrgb);
    }
    return 0;
}

inline bool DecodeXcfTileRle(
    const uint8_t* tileData,
    size_t tileBytes,
    uint32_t tileW,
    uint32_t tileH,
    uint32_t bpp,
    std::vector<uint8_t>& outInterleaved)
{
    size_t numPixels = static_cast<size_t>(tileW) * tileH;
    if (numPixels == 0 || bpp == 0 || bpp > 32) return false;
    outInterleaved.assign(numPixels * bpp, 0);
    size_t pos = 0;

    // GIMP XCF RLE encodes `bpp` byte-planes; plane `ch` writes byte `ch` of every pixel `0..numPixels-1`
    for (uint32_t ch = 0; ch < bpp; ++ch) {
        size_t px = 0;
        while (px < numPixels) {
            if (pos >= tileBytes) return false;
            uint8_t n = tileData[pos++];
            if (n <= 126) {
                size_t runLen = static_cast<size_t>(n) + 1;
                if (pos >= tileBytes || px + runLen > numPixels) return false;
                uint8_t val = tileData[pos++];
                for (size_t k = 0; k < runLen; ++k, ++px) {
                    outInterleaved[px * bpp + ch] = val;
                }
            } else if (n == 127) {
                if (pos + 3 > tileBytes) return false;
                size_t runLen = (static_cast<size_t>(tileData[pos]) << 8) | tileData[pos + 1];
                uint8_t val = tileData[pos + 2];
                pos += 3;
                if (runLen == 0 || px + runLen > numPixels) return false;
                for (size_t k = 0; k < runLen; ++k, ++px) {
                    outInterleaved[px * bpp + ch] = val;
                }
            } else if (n == 128) {
                if (pos + 2 > tileBytes) return false;
                size_t litLen = (static_cast<size_t>(tileData[pos]) << 8) | tileData[pos + 1];
                pos += 2;
                if (litLen == 0 || pos + litLen > tileBytes || px + litLen > numPixels) return false;
                for (size_t k = 0; k < litLen; ++k, ++px, ++pos) {
                    outInterleaved[px * bpp + ch] = tileData[pos];
                }
            } else {
                size_t litLen = 256u - static_cast<size_t>(n);
                if (pos + litLen > tileBytes || px + litLen > numPixels) return false;
                for (size_t k = 0; k < litLen; ++k, ++px, ++pos) {
                    outInterleaved[px * bpp + ch] = tileData[pos];
                }
            }
        }
        if (px != numPixels) return false;
    }
    return true;
}

inline bool DecodeXCF(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (size < 26 || std::memcmp(data, "gimp xcf ", 9) != 0) return false;

    int xcfVer = 0;
    if (std::memcmp(data + 9, "file\0", 5) == 0) {
        xcfVer = 0;
    } else if (data[9] == 'v' && data[13] == '\0' &&
               data[10] >= '0' && data[10] <= '9' &&
               data[11] >= '0' && data[11] <= '9' &&
               data[12] >= '0' && data[12] <= '9') {
        xcfVer = (data[10] - '0') * 100 + (data[11] - '0') * 10 + (data[12] - '0');
    } else {
        return false;
    }
    const bool use64BitOffsets = (xcfVer >= 11);

    size_t pos = 14;
    if (pos + 12 > size) return false;
    uint32_t canvasW  = ReadU32BE(data + pos);
    uint32_t canvasH  = ReadU32BE(data + pos + 4);
    uint32_t baseType = ReadU32BE(data + pos + 8);
    pos += 12;

    if (canvasW == 0 || canvasH == 0 || canvasW > kMaxRasterDimension || canvasH > kMaxRasterDimension || baseType > 2) {
        return false;
    }

    uint32_t precisionCode = 150;
    if (xcfVer >= 4) {
        if (pos + 4 > size) return false;
        precisionCode = ReadU32BE(data + pos);
        pos += 4;
    }
    XcfPrecisionInfo prec{};
    if (!ParseXcfPrecision(xcfVer, precisionCode, prec)) {
        return false;
    }

    uint8_t compression = 1; // 0 = None, 1 = RLE, 2 = Zlib
    std::vector<uint8_t> colormap;
    bool sawPropEnd = false;
    while (pos + 8 <= size) {
        uint32_t propType = ReadU32BE(data + pos);
        uint32_t propLen  = ReadU32BE(data + pos + 4);
        pos += 8;
        if (pos + propLen > size) return false;
        if (propType == 1) { // PROP_COLORMAP
            if (propLen < 4) return false;
            uint32_t numColors = ReadU32BE(data + pos);
            if (numColors > 256 || 4u + static_cast<size_t>(numColors) * 3u > propLen) return false;
            colormap.assign(data + pos + 4, data + pos + 4 + static_cast<size_t>(numColors) * 3u);
        } else if (propType == 17 && propLen >= 1) { // PROP_COMPRESSION
            compression = data[pos];
            if (compression > 2) return false;
        }
        pos += propLen;
        if (propType == 0) {
            sawPropEnd = true;
            break;
        }
    }
    if (!sawPropEnd) return false;

    auto readOffset = [&](size_t& p, bool& ok) -> uint64_t {
        if (use64BitOffsets) {
            if (p + 8 > size) { ok = false; return 0; }
            uint64_t v = ReadU64BE(data + p);
            p += 8;
            return v;
        } else {
            if (p + 4 > size) { ok = false; return 0; }
            uint64_t v = ReadU32BE(data + p);
            p += 4;
            return v;
        }
    };

    std::vector<uint64_t> layerOffsets;
    while (pos < size) {
        bool ptrOk = true;
        uint64_t loff = readOffset(pos, ptrOk);
        if (!ptrOk || loff == 0) break;
        if (loff >= size) return false;
        layerOffsets.push_back(loff);
        if (layerOffsets.size() >= 512) break;
    }
    if (layerOffsets.empty()) return false;

    // Detect legacy v000 generator off-by-one header pointer bias (where 13-byte magic length was assumed instead of 14)
    uint64_t ptrBias = 0;
    if (xcfVer == 0 && layerOffsets[0] + 17 <= size && ReadU32BE(data + layerOffsets[0]) == 0) {
        size_t cand = static_cast<size_t>(layerOffsets[0] + 1);
        if (ReadU32BE(data + cand) == canvasW &&
            ReadU32BE(data + cand + 4) == canvasH &&
            ReadU32BE(data + cand + 8) <= 5) {
            ptrBias = 1;
            for (uint64_t& loff : layerOffsets) {
                loff += ptrBias;
                if (loff >= size) return false;
            }
        }
    }

    auto biasOffset = [&](uint64_t v) -> uint64_t {
        return (v != 0) ? (v + ptrBias) : 0;
    };

    struct XcfLayerMeta {
        uint32_t lw = 0;
        uint32_t lh = 0;
        uint32_t ltype = 0;
        std::wstring wname;
        int32_t offX = 0;
        int32_t offY = 0;
        uint32_t opacity = 255;
        bool visible = true;
        bool applyMask = true;
        bool isGroupFolder = false;
        uint64_t hierOff = 0;
        uint64_t maskOff = 0;
    };

    std::vector<XcfLayerMeta> layers;
    layers.reserve(layerOffsets.size());
    bool hasVisibleLeafLayer = false;

    // XCF stores top-most layer first; iterate bottom-to-top
    for (int li = static_cast<int>(layerOffsets.size()) - 1; li >= 0; --li) {
        size_t lp = static_cast<size_t>(layerOffsets[li]);
        if (lp + 16 > size) return false;
        XcfLayerMeta lm{};
        lm.lw    = ReadU32BE(data + lp);
        lm.lh    = ReadU32BE(data + lp + 4);
        lm.ltype = ReadU32BE(data + lp + 8);
        uint32_t nameLen = ReadU32BE(data + lp + 12);
        lp += 16;
        if (lp + nameLen > size || lm.ltype > 5) return false;
        if (nameLen > 1) {
            size_t actualLen = nameLen - 1;
            while (actualLen > 0 && data[lp + actualLen - 1] == '\0') --actualLen;
            std::string lname(reinterpret_cast<const char*>(data + lp), actualLen);
            lm.wname = PathUtils::Utf8ToWide(lname);
        }
        lp += nameLen;

        bool layerPropEnd = false;
        while (lp + 8 <= size) {
            uint32_t pType = ReadU32BE(data + lp);
            uint32_t pLen  = ReadU32BE(data + lp + 4);
            lp += 8;
            if (lp + pLen > size) return false;
            if (pType == 6 && pLen >= 4) { // PROP_OPACITY (0..255)
                lm.opacity = std::min<uint32_t>(255u, ReadU32BE(data + lp));
            } else if (pType == 33 && pLen >= 4) { // PROP_FLOAT_OPACITY (0.0..1.0 float BE)
                uint32_t u = ReadU32BE(data + lp);
                float f = 1.0f;
                std::memcpy(&f, &u, 4);
                if (std::isfinite(f)) {
                    lm.opacity = static_cast<uint32_t>(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f);
                }
            } else if (pType == 7 && pLen >= 4) { // PROP_APPLY_MASK
                lm.applyMask = (ReadU32BE(data + lp) != 0);
            } else if (pType == 8 && pLen >= 4) { // PROP_VISIBLE
                lm.visible = (ReadU32BE(data + lp) != 0);
            } else if (pType == 15 && pLen >= 8) { // PROP_OFFSETS
                lm.offX = ReadI32BE(data + lp);
                lm.offY = ReadI32BE(data + lp + 4);
            } else if (pType == 29) { // PROP_GROUP_ITEM
                lm.isGroupFolder = true;
            }
            lp += pLen;
            if (pType == 0) {
                layerPropEnd = true;
                break;
            }
        }
        if (!layerPropEnd) return false;

        bool ptrOk1 = true, ptrOk2 = true;
        lm.hierOff = biasOffset(readOffset(lp, ptrOk1));
        lm.maskOff = biasOffset(readOffset(lp, ptrOk2));
        if (!ptrOk1 || !ptrOk2) return false;

        if (lm.visible && !lm.isGroupFolder && lm.lw > 0 && lm.lh > 0) {
            hasVisibleLeafLayer = true;
        }
        layers.push_back(std::move(lm));
    }

    auto decodeTileData = [&](size_t tStart, size_t tBytes, uint32_t tw, uint32_t th, uint32_t bpp, std::vector<uint8_t>& tileBuf) -> bool {
        size_t rawLen = static_cast<size_t>(tw) * th * bpp;
        if (rawLen == 0) return false;
        if (compression == 0) {
            if (tStart + rawLen > size) return false;
            tileBuf.assign(data + tStart, data + tStart + rawLen);
            return true;
        }
        if (compression == 1) {
            return DecodeXcfTileRle(data + tStart, tBytes, tw, th, bpp, tileBuf);
        }
        if (compression == 2) {
            tileBuf.clear();
            if (!ZipDeflate::InflateZlib(data + tStart, tBytes, tileBuf, rawLen)) return false;
            return tileBuf.size() == rawLen;
        }
        return false;
    };

    auto decodeMaskAlpha = [&](uint64_t maskPtr, uint32_t mw, uint32_t mh, std::vector<uint8_t>& outMask) -> bool {
        if (maskPtr == 0 || maskPtr + 16 > size) return false;
        size_t mp = static_cast<size_t>(maskPtr);
        uint32_t mWidth   = ReadU32BE(data + mp);
        uint32_t mHeight  = ReadU32BE(data + mp + 4);
        uint32_t mNameLen = ReadU32BE(data + mp + 8);
        mp += 12;
        if (mWidth != mw || mHeight != mh || mp + mNameLen > size) return false;
        mp += mNameLen;
        uint32_t maskOpacity = 255;
        bool sawEnd = false;
        while (mp + 8 <= size) {
            uint32_t pType = ReadU32BE(data + mp);
            uint32_t pLen  = ReadU32BE(data + mp + 4);
            mp += 8;
            if (mp + pLen > size) return false;
            if (pType == 6 && pLen >= 4) {
                maskOpacity = std::min<uint32_t>(255u, ReadU32BE(data + mp));
            } else if (pType == 33 && pLen >= 4) {
                uint32_t u = ReadU32BE(data + mp);
                float f = 1.0f;
                std::memcpy(&f, &u, 4);
                if (std::isfinite(f)) maskOpacity = static_cast<uint32_t>(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f);
            }
            mp += pLen;
            if (pType == 0) { sawEnd = true; break; }
        }
        if (!sawEnd) return false;
        bool ptrOk = true;
        uint64_t mHierOff = biasOffset(readOffset(mp, ptrOk));
        if (!ptrOk || mHierOff == 0 || mHierOff + 12 > size) return false;

        size_t hp = static_cast<size_t>(mHierOff);
        uint32_t hw  = ReadU32BE(data + hp);
        uint32_t hh  = ReadU32BE(data + hp + 4);
        uint32_t bpp = ReadU32BE(data + hp + 8);
        hp += 12;
        if (hw != mw || hh != mh || (bpp != prec.bytesPerComp && bpp != 1)) return false;

        uint64_t levelOff = biasOffset(readOffset(hp, ptrOk));
        if (!ptrOk || levelOff == 0 || levelOff + 8 > size) return false;
        size_t lvp = static_cast<size_t>(levelOff);
        if (ReadU32BE(data + lvp) != mw || ReadU32BE(data + lvp + 4) != mh) return false;
        lvp += 8;

        uint32_t tilesX = (mw + 63) / 64;
        uint32_t tilesY = (mh + 63) / 64;
        uint32_t totalTiles = tilesX * tilesY;
        std::vector<uint64_t> tOffs;
        tOffs.reserve(totalTiles);
        for (uint32_t t = 0; t < totalTiles; ++t) {
            uint64_t toff = biasOffset(readOffset(lvp, ptrOk));
            if (!ptrOk || toff == 0 || toff >= size) return false;
            tOffs.push_back(toff);
        }

        outMask.assign(static_cast<size_t>(mw) * mh, 255);
        XcfPrecisionInfo mPrec = (bpp == prec.bytesPerComp) ? prec : XcfPrecisionInfo{ 1, false, false };
        std::vector<uint8_t> tBuf;
        for (size_t t = 0; t < tOffs.size(); ++t) {
            uint32_t tx = static_cast<uint32_t>(t % tilesX);
            uint32_t ty = static_cast<uint32_t>(t / tilesX);
            uint32_t tw = std::min(64u, mw - tx * 64);
            uint32_t th = std::min(64u, mh - ty * 64);
            size_t tStart = static_cast<size_t>(tOffs[t]);
            size_t tEnd = (t + 1 < tOffs.size() && tOffs[t + 1] > tOffs[t] && tOffs[t + 1] <= size)
                ? static_cast<size_t>(tOffs[t + 1])
                : std::min(size, tStart + static_cast<size_t>(tw) * th * bpp * 2u + 4096u);
            if (!decodeTileData(tStart, tEnd - tStart, tw, th, bpp, tBuf)) return false;
            for (uint32_t py = 0; py < th; ++py) {
                for (uint32_t px = 0; px < tw; ++px) {
                    const uint8_t* sp = tBuf.data() + (static_cast<size_t>(py) * tw + px) * bpp;
                    uint8_t mv = XcfSampleCompU8(sp, mPrec, false);
                    outMask[static_cast<size_t>(ty * 64 + py) * mw + (tx * 64 + px)] =
                        static_cast<uint8_t>((static_cast<uint32_t>(mv) * maskOpacity + 127u) / 255u);
                }
            }
        }
        return true;
    };

    outInfo.width = canvasW;
    outInfo.height = canvasH;
    outInfo.channels = (baseType == 1) ? 2 : 4;
    outInfo.bitsPerChannel = prec.bytesPerComp * 8;
    outInfo.formatName = L"GIMP XCF v" + std::to_wstring(xcfVer);
    outInfo.layerNames.clear();

    size_t totalCanvasPixels = static_cast<size_t>(canvasW) * canvasH;
    outInfo.bgraPixels.assign(totalCanvasPixels * 4, 0);

    size_t totalDecodedTiles = 0;
    for (const auto& lm : layers) {
        if (!lm.wname.empty()) {
            outInfo.layerNames.push_back(lm.wname);
        }
        if (!lm.visible || lm.lw == 0 || lm.lh == 0 || lm.lw > kMaxRasterDimension || lm.lh > kMaxRasterDimension) {
            continue;
        }
        if (lm.isGroupFolder && hasVisibleLeafLayer) {
            continue;
        }

        if (lm.hierOff == 0 || lm.hierOff + 12 > size) return false;
        size_t hp = static_cast<size_t>(lm.hierOff);
        uint32_t hw  = ReadU32BE(data + hp);
        uint32_t hh  = ReadU32BE(data + hp + 4);
        uint32_t bpp = ReadU32BE(data + hp + 8);
        hp += 12;
        if (hw != lm.lw || hh != lm.lh || bpp == 0 || bpp > 32) return false;

        uint32_t expectedComps = 0;
        switch (lm.ltype) {
            case 0: expectedComps = 3; break; // RGB
            case 1: expectedComps = 4; break; // RGBA
            case 2: expectedComps = 1; break; // Grayscale
            case 3: expectedComps = 2; break; // GrayscaleA
            case 4: expectedComps = 1; break; // Indexed
            case 5: expectedComps = 2; break; // IndexedA
            default: return false;
        }

        XcfPrecisionInfo layerPrec = prec;
        if (lm.ltype == 4 || lm.ltype == 5) {
            if (colormap.empty()) return false;
            if (bpp == expectedComps) {
                layerPrec = { 1, false, false };
            } else if (bpp != expectedComps * prec.bytesPerComp) {
                return false;
            }
        } else {
            if (bpp != expectedComps * prec.bytesPerComp) {
                if (bpp == expectedComps) {
                    layerPrec = { 1, false, false };
                } else {
                    return false;
                }
            }
        }

        bool ptrOk = true;
        uint64_t levelOff = biasOffset(readOffset(hp, ptrOk));
        if (!ptrOk || levelOff == 0 || levelOff + 8 > size) return false;
        size_t lvp = static_cast<size_t>(levelOff);
        uint32_t lvw = ReadU32BE(data + lvp);
        uint32_t lvh = ReadU32BE(data + lvp + 4);
        lvp += 8;
        if (lvw != lm.lw || lvh != lm.lh) return false;

        uint32_t tilesX = (lm.lw + 63) / 64;
        uint32_t tilesY = (lm.lh + 63) / 64;
        uint32_t totalTiles = tilesX * tilesY;
        std::vector<uint64_t> tileOffsets;
        tileOffsets.reserve(totalTiles);
        for (uint32_t t = 0; t < totalTiles; ++t) {
            uint64_t toff = biasOffset(readOffset(lvp, ptrOk));
            if (!ptrOk || toff == 0 || toff >= size) return false;
            tileOffsets.push_back(toff);
        }

        std::vector<uint8_t> maskAlpha;
        bool hasMask = false;
        if (lm.maskOff != 0 && lm.applyMask) {
            hasMask = decodeMaskAlpha(lm.maskOff, lm.lw, lm.lh, maskAlpha);
        }

        const uint32_t cb = layerPrec.bytesPerComp;
        const size_t numCols = colormap.size() / 3u;
        std::vector<uint8_t> tileBuf;

        for (size_t t = 0; t < tileOffsets.size(); ++t) {
            uint32_t tx = static_cast<uint32_t>(t % tilesX);
            uint32_t ty = static_cast<uint32_t>(t / tilesX);
            uint32_t tw = std::min(64u, lm.lw - tx * 64);
            uint32_t th = std::min(64u, lm.lh - ty * 64);

            size_t tStart = static_cast<size_t>(tileOffsets[t]);
            size_t tEnd = (t + 1 < tileOffsets.size() && tileOffsets[t + 1] > tileOffsets[t] && tileOffsets[t + 1] <= size)
                ? static_cast<size_t>(tileOffsets[t + 1])
                : std::min(size, tStart + static_cast<size_t>(tw) * th * bpp * 2u + 4096u);
            if (!decodeTileData(tStart, tEnd - tStart, tw, th, bpp, tileBuf)) {
                return false;
            }
            ++totalDecodedTiles;

            for (uint32_t py = 0; py < th; ++py) {
                uint32_t ly = ty * 64 + py;
                int32_t cy = lm.offY + static_cast<int32_t>(ly);
                if (cy < 0 || cy >= static_cast<int32_t>(canvasH)) continue;
                for (uint32_t px = 0; px < tw; ++px) {
                    uint32_t lx = tx * 64 + px;
                    int32_t cx = lm.offX + static_cast<int32_t>(lx);
                    if (cx < 0 || cx >= static_cast<int32_t>(canvasW)) continue;

                    const uint8_t* sp = tileBuf.data() + (static_cast<size_t>(py) * tw + px) * bpp;
                    uint8_t sr = 0, sg = 0, sb = 0, sa = 255;
                    if (lm.ltype == 0) {
                        sr = XcfSampleCompU8(sp,          layerPrec, true);
                        sg = XcfSampleCompU8(sp + cb,     layerPrec, true);
                        sb = XcfSampleCompU8(sp + cb * 2, layerPrec, true);
                    } else if (lm.ltype == 1) {
                        sr = XcfSampleCompU8(sp,          layerPrec, true);
                        sg = XcfSampleCompU8(sp + cb,     layerPrec, true);
                        sb = XcfSampleCompU8(sp + cb * 2, layerPrec, true);
                        sa = XcfSampleCompU8(sp + cb * 3, layerPrec, false);
                    } else if (lm.ltype == 2) {
                        sr = sg = sb = XcfSampleCompU8(sp, layerPrec, true);
                    } else if (lm.ltype == 3) {
                        sr = sg = sb = XcfSampleCompU8(sp, layerPrec, true);
                        sa = XcfSampleCompU8(sp + cb, layerPrec, false);
                    } else if (lm.ltype == 4 || lm.ltype == 5) {
                        uint32_t idx = sp[0];
                        if (idx < numCols) {
                            sr = colormap[idx * 3 + 0];
                            sg = colormap[idx * 3 + 1];
                            sb = colormap[idx * 3 + 2];
                        }
                        if (lm.ltype == 5) {
                            sa = XcfSampleCompU8(sp + cb, layerPrec, false);
                        }
                    }

                    uint32_t effA = (static_cast<uint32_t>(sa) * lm.opacity + 127u) / 255u;
                    if (hasMask) {
                        effA = (effA * maskAlpha[static_cast<size_t>(ly) * lm.lw + lx] + 127u) / 255u;
                    }
                    sa = static_cast<uint8_t>(effA);
                    if (sa == 0) continue;

                    uint8_t* dp = outInfo.bgraPixels.data() + (static_cast<size_t>(cy) * canvasW + cx) * 4;
                    if (sa == 255 || dp[3] == 0) {
                        WritePremulBGRA(dp, sr, sg, sb, sa);
                    } else {
                        uint32_t invA = 255u - sa;
                        dp[0] = static_cast<uint8_t>((static_cast<uint32_t>(sb) * sa + static_cast<uint32_t>(dp[0]) * invA + 127u) / 255u);
                        dp[1] = static_cast<uint8_t>((static_cast<uint32_t>(sg) * sa + static_cast<uint32_t>(dp[1]) * invA + 127u) / 255u);
                        dp[2] = static_cast<uint8_t>((static_cast<uint32_t>(sr) * sa + static_cast<uint32_t>(dp[2]) * invA + 127u) / 255u);
                        dp[3] = static_cast<uint8_t>(sa + (static_cast<uint32_t>(dp[3]) * invA + 127u) / 255u);
                    }
                }
            }
        }
    }

    if (totalDecodedTiles == 0) {
        outInfo.bgraPixels.clear();
        return false;
    }

    if (!outInfo.layerNames.empty()) {
        outInfo.formatName += L" (" + std::to_wstring(outInfo.layerNames.size()) + L" layers)";
    }
    outInfo.valid = true;
    return true;
}
