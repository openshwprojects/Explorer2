#pragma once

// ============================================================================
// 3. GIMP XCF (.xcf) — Canvas + Layer Hierarchy + 64x64 RLE Tile Compositor
// ============================================================================
bool DecodeXcfTileRle(
    const uint8_t* tileData,
    size_t tileBytes,
    uint32_t tileW,
    uint32_t tileH,
    uint32_t bpp,
    std::vector<uint8_t>& outInterleaved)
{
    size_t numPixels = static_cast<size_t>(tileW) * tileH;
    outInterleaved.assign(numPixels * bpp, 255);
    size_t pos = 0;

    for (uint32_t ch = 0; ch < bpp; ++ch) {
        size_t px = 0;
        while (px < numPixels && pos < tileBytes) {
            uint8_t n = tileData[pos++];
            if (n <= 126) {
                // Run of (n + 1) identical bytes
                size_t runLen = static_cast<size_t>(n) + 1;
                if (pos >= tileBytes) break;
                uint8_t val = tileData[pos++];
                for (size_t k = 0; k < runLen && px < numPixels; ++k, ++px) {
                    outInterleaved[px * bpp + ch] = val;
                }
            } else if (n == 127) {
                // Long run of identical bytes
                if (pos + 3 > tileBytes) break;
                size_t runLen = (static_cast<size_t>(tileData[pos]) << 8) | tileData[pos + 1];
                uint8_t val = tileData[pos + 2];
                pos += 3;
                for (size_t k = 0; k < runLen && px < numPixels; ++k, ++px) {
                    outInterleaved[px * bpp + ch] = val;
                }
            } else if (n == 128) {
                // Long literal sequence
                if (pos + 2 > tileBytes) break;
                size_t litLen = (static_cast<size_t>(tileData[pos]) << 8) | tileData[pos + 1];
                pos += 2;
                for (size_t k = 0; k < litLen && px < numPixels && pos < tileBytes; ++k, ++px, ++pos) {
                    outInterleaved[px * bpp + ch] = tileData[pos];
                }
            } else {
                // Short literal sequence of (256 - n) bytes
                size_t litLen = 256 - static_cast<size_t>(n);
                for (size_t k = 0; k < litLen && px < numPixels && pos < tileBytes; ++k, ++px, ++pos) {
                    outInterleaved[px * bpp + ch] = tileData[pos];
                }
            }
        }
    }
    return true;
}

bool DecodeXCF(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (size < 26 || std::memcmp(data, "gimp xcf ", 9) != 0) return false;

    int xcfVer = 0;
    if (std::memcmp(data + 9, "file", 4) == 0) {
        xcfVer = 0;
    } else if (data[9] == 'v') {
        xcfVer = std::atoi(reinterpret_cast<const char*>(data + 10));
    }
    bool use64BitOffsets = (xcfVer >= 11);

    size_t pos = 14;
    if (pos + 12 > size) return false;
    uint32_t canvasW = ReadU32BE(data + pos);
    uint32_t canvasH = ReadU32BE(data + pos + 4);
    uint32_t baseType = ReadU32BE(data + pos + 8);
    pos += 12;

    if (canvasW == 0 || canvasH == 0 || canvasW > kMaxRasterDimension || canvasH > kMaxRasterDimension) return false;
    if (xcfVer >= 4) {
        if (pos + 4 > size) return false;
        pos += 4; // precision field in XCF v004+
    }

    uint8_t compression = 1; // 0 = None, 1 = RLE
    // Read image property list until PROP_END (0)
    while (pos + 8 <= size) {
        uint32_t propType = ReadU32BE(data + pos);
        uint32_t propLen  = ReadU32BE(data + pos + 4);
        pos += 8;
        if (pos + propLen > size) return false;
        if (propType == 17 && propLen >= 1) { // PROP_COMPRESSION
            compression = data[pos];
        }
        pos += propLen;
        if (propType == 0) break;
    }

    auto readOffset = [&](size_t& p) -> uint64_t {
        if (use64BitOffsets) {
            if (p + 8 > size) return 0;
            uint64_t v = ReadU64BE(data + p);
            p += 8;
            return v;
        } else {
            if (p + 4 > size) return 0;
            uint64_t v = ReadU32BE(data + p);
            p += 4;
            return v;
        }
    };

    std::vector<uint64_t> layerOffsets;
    while (pos < size) {
        uint64_t loff = readOffset(pos);
        if (loff == 0) break;
        if (loff < size) layerOffsets.push_back(loff);
        if (layerOffsets.size() >= 256) break;
    }

    outInfo.width = canvasW;
    outInfo.height = canvasH;
    outInfo.channels = (baseType == 1) ? 2 : 4;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = L"GIMP XCF v" + std::to_wstring(xcfVer);

    size_t totalCanvasPixels = static_cast<size_t>(canvasW) * canvasH;
    // Initialize canvas with subtle checkerboard / white base
    outInfo.bgraPixels.assign(totalCanvasPixels * 4, 0);

    // Composite layers from bottom to top (XCF stores top-most layer first in layerOffsets)
    for (int li = static_cast<int>(layerOffsets.size()) - 1; li >= 0; --li) {
        size_t lp = static_cast<size_t>(layerOffsets[li]);
        if (lp + 16 > size) continue;
        uint32_t lw = ReadU32BE(data + lp);
        uint32_t lh = ReadU32BE(data + lp + 4);
        uint32_t ltype = ReadU32BE(data + lp + 8);
        uint32_t nameLen = ReadU32BE(data + lp + 12);
        lp += 16;
        if (lp + nameLen > size) continue;
        if (nameLen > 1) {
            std::string lname(reinterpret_cast<const char*>(data + lp), nameLen - 1);
            outInfo.layerNames.push_back(PathUtils::Utf8ToWide(lname));
        }
        lp += nameLen;

        int32_t offX = 0, offY = 0;
        uint32_t opacity = 255;
        bool visible = true;
        while (lp + 8 <= size) {
            uint32_t pType = ReadU32BE(data + lp);
            uint32_t pLen  = ReadU32BE(data + lp + 4);
            lp += 8;
            if (lp + pLen > size) break;
            if (pType == 6 && pLen >= 4) { // PROP_OPACITY
                opacity = std::min<uint32_t>(255u, ReadU32BE(data + lp));
            } else if (pType == 8 && pLen >= 4) { // PROP_VISIBLE
                visible = (ReadU32BE(data + lp) != 0);
            } else if (pType == 15 && pLen >= 8) { // PROP_OFFSETS
                offX = ReadI32BE(data + lp);
                offY = ReadI32BE(data + lp + 4);
            }
            lp += pLen;
            if (pType == 0) break;
        }
        if (!visible || lw == 0 || lh == 0 || lw > kMaxRasterDimension || lh > kMaxRasterDimension) continue;

        uint64_t hierOff = readOffset(lp);
        if (hierOff == 0 || hierOff + 12 > size) continue;
        size_t hp = static_cast<size_t>(hierOff);
        uint32_t hw = ReadU32BE(data + hp);
        uint32_t hh = ReadU32BE(data + hp + 4);
        uint32_t bpp = ReadU32BE(data + hp + 8);
        hp += 12;
        if (bpp == 0 || bpp > 4 || hw != lw || hh != lh) continue;

        uint64_t levelOff = readOffset(hp);
        if (levelOff == 0 || levelOff + 8 > size) continue;
        size_t lvp = static_cast<size_t>(levelOff);
        uint32_t lvw = ReadU32BE(data + lvp);
        uint32_t lvh = ReadU32BE(data + lvp + 4);
        lvp += 8;
        if (lvw != lw || lvh != lh) continue;

        uint32_t tilesX = (lw + 63) / 64;
        uint32_t tilesY = (lh + 63) / 64;
        uint32_t totalTiles = tilesX * tilesY;
        std::vector<uint64_t> tileOffsets;
        for (uint32_t t = 0; t < totalTiles; ++t) {
            uint64_t toff = readOffset(lvp);
            if (toff == 0) break;
            tileOffsets.push_back(toff);
        }

        std::vector<uint8_t> tileBuf;
        for (size_t t = 0; t < tileOffsets.size(); ++t) {
            uint32_t tx = static_cast<uint32_t>(t % tilesX);
            uint32_t ty = static_cast<uint32_t>(t / tilesX);
            uint32_t tw = std::min(64u, lw - tx * 64);
            uint32_t th = std::min(64u, lh - ty * 64);

            size_t tStart = static_cast<size_t>(tileOffsets[t]);
            if (tStart >= size) continue;
            size_t tEnd = (t + 1 < tileOffsets.size() && tileOffsets[t + 1] > tileOffsets[t] && tileOffsets[t + 1] <= size)
                ? static_cast<size_t>(tileOffsets[t + 1])
                : std::min(size, tStart + static_cast<size_t>(tw) * th * bpp * 2);
            size_t tBytes = tEnd - tStart;

            if (compression == 0) {
                size_t rawLen = static_cast<size_t>(tw) * th * bpp;
                if (tStart + rawLen > size) continue;
                tileBuf.assign(data + tStart, data + tStart + rawLen);
            } else {
                DecodeXcfTileRle(data + tStart, tBytes, tw, th, bpp, tileBuf);
            }

            // Alpha-blend tile pixels onto canvas
            for (uint32_t py = 0; py < th; ++py) {
                int32_t cy = offY + static_cast<int32_t>(ty * 64 + py);
                if (cy < 0 || cy >= static_cast<int32_t>(canvasH)) continue;
                for (uint32_t px = 0; px < tw; ++px) {
                    int32_t cx = offX + static_cast<int32_t>(tx * 64 + px);
                    if (cx < 0 || cx >= static_cast<int32_t>(canvasW)) continue;

                    const uint8_t* sp = tileBuf.data() + (static_cast<size_t>(py) * tw + px) * bpp;
                    uint8_t sr = 0, sg = 0, sb = 0, sa = 255;
                    if (ltype == 0 || bpp == 3) {
                        sr = sp[0]; sg = sp[1]; sb = sp[2]; sa = 255;
                    } else if (ltype == 1 || bpp == 4) {
                        sr = sp[0]; sg = sp[1]; sb = sp[2]; sa = sp[3];
                    } else if (bpp == 1) {
                        sr = sg = sb = sp[0]; sa = 255;
                    } else if (bpp == 2) {
                        sr = sg = sb = sp[0]; sa = sp[1];
                    }
                    sa = static_cast<uint8_t>((sa * opacity) / 255);
                    if (sa == 0) continue;

                    uint8_t* dp = outInfo.bgraPixels.data() + (static_cast<size_t>(cy) * canvasW + cx) * 4;
                    if (sa == 255 || dp[3] == 0) {
                        WritePremulBGRA(dp, sr, sg, sb, sa);
                    } else {
                        uint32_t invA = 255 - sa;
                        uint8_t outB = static_cast<uint8_t>((static_cast<uint32_t>(sb) * sa + static_cast<uint32_t>(dp[0]) * invA) / 255);
                        uint8_t outG = static_cast<uint8_t>((static_cast<uint32_t>(sg) * sa + static_cast<uint32_t>(dp[1]) * invA) / 255);
                        uint8_t outR = static_cast<uint8_t>((static_cast<uint32_t>(sr) * sa + static_cast<uint32_t>(dp[2]) * invA) / 255);
                        uint8_t outA = static_cast<uint8_t>(sa + (dp[3] * invA) / 255);
                        dp[0] = outB; dp[1] = outG; dp[2] = outR; dp[3] = outA;
                    }
                }
            }
        }
    }

    if (!outInfo.layerNames.empty()) {
        outInfo.formatName += L" (" + std::to_wstring(outInfo.layerNames.size()) + L" layers)";
    }
    outInfo.valid = true;
    return true;
}

