#pragma once
// PhotoshopBrushAbr.h — Adobe Photoshop Sampled Brush Tip Decoder (.abr v1/v2/v6/v7/v10)

namespace Formats {

inline bool DecodePhotoshopBrushAbr(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data) return false;
    const std::wstring extLower = L".abr";
    if (extLower != L".abr" || size < 10) return false;
    uint16_t ver = DetailImg100::RdU16BE(data + 0);
    if (ver != 1 && ver != 2 && ver != 6 && ver != 7 && ver != 10) return false;
    uint16_t subVer = 0;
    if (ver == 6 || ver == 7 || ver == 10) {
        if (size < 16) return false;
        subVer = DetailImg100::RdU16BE(data + 2);
        if (subVer != 1 && subVer != 2) return false;
    }

    struct BrushTip {
        uint32_t w = 0, h = 0;
        std::wstring name;
        std::vector<uint8_t> alpha;
    };
    std::vector<BrushTip> tips;

    auto decodeAbrSampleBitmap = [](const uint8_t* pix, size_t availBytes,
                                    uint32_t bw, uint32_t bh, uint16_t depth, uint8_t comp,
                                    bool strictTrailingBound, std::vector<uint8_t>& outAlpha) -> bool
    {
        if (bw == 0 || bh == 0 || bw > 8192 || bh > 8192) return false;
        if (depth != 8 && depth != 16) return false;
        if (comp > 1) return false;
        uint64_t pxCount64 = static_cast<uint64_t>(bw) * static_cast<uint64_t>(bh);
        if (pxCount64 == 0 || pxCount64 > 32ull * 1024ull * 1024ull) return false;

        size_t sampleBytes = (depth == 16) ? 2u : 1u;
        size_t rawRowBytes = static_cast<size_t>(bw) * sampleBytes;
        size_t totalRawBytes = static_cast<size_t>(pxCount64) * sampleBytes;
        std::vector<uint8_t> rawPlane;

        if (comp == 0) {
            if (availBytes < totalRawBytes) return false;
            if (strictTrailingBound && availBytes > totalRawBytes + 64u) return false;
            rawPlane.assign(pix, pix + totalRawBytes);
        } else {
            // comp == 1: height * uint16_t BE scanline byte counts followed by PackBits RLE rows
            size_t tblBytes = static_cast<size_t>(bh) * 2u;
            if (availBytes <= tblBytes) return false;
            rawPlane.assign(totalRawBytes, 0);

            size_t rpos = tblBytes;
            bool rowByRowOk = true;
            for (uint32_t ry = 0; ry < bh; ++ry) {
                uint16_t rowCnt = DetailImg100::RdU16BE(pix + static_cast<size_t>(ry) * 2u);
                if (rowCnt == 0 || rpos + rowCnt > availBytes) {
                    rowByRowOk = false;
                    break;
                }
                const uint8_t* rp = pix + rpos;
                const uint8_t* rend = rp + rowCnt;
                uint8_t* dstRow = rawPlane.data() + static_cast<size_t>(ry) * rawRowBytes;
                size_t dIdx = 0;
                while (rp < rend && dIdx < rawRowBytes) {
                    int8_t n = static_cast<int8_t>(*rp++);
                    if (n >= 0) {
                        size_t cnt = static_cast<size_t>(n) + 1u;
                        if (rp + cnt > rend || dIdx + cnt > rawRowBytes) { rowByRowOk = false; break; }
                        std::memcpy(dstRow + dIdx, rp, cnt);
                        rp += cnt;
                        dIdx += cnt;
                    } else if (n != -128) {
                        size_t cnt = static_cast<size_t>(1 - static_cast<int>(n));
                        if (rp >= rend || dIdx + cnt > rawRowBytes) { rowByRowOk = false; break; }
                        uint8_t val = *rp++;
                        std::memset(dstRow + dIdx, val, cnt);
                        dIdx += cnt;
                    }
                }
                if (!rowByRowOk || dIdx != rawRowBytes) {
                    rowByRowOk = false;
                    break;
                }
                rpos += rowCnt;
            }
            if (rowByRowOk && strictTrailingBound && availBytes > rpos + 64u) {
                rowByRowOk = false;
            }

            if (!rowByRowOk) {
                // Fallback for continuous PackBits stream after the bh*2 scanline count table
                const uint8_t* rle = pix + tblBytes;
                const uint8_t* rend = pix + availBytes;
                size_t dstIdx = 0;
                bool contOk = true;
                while (rle < rend && dstIdx < totalRawBytes) {
                    int8_t n = static_cast<int8_t>(*rle++);
                    if (n >= 0) {
                        size_t cnt = static_cast<size_t>(n) + 1u;
                        if (rle + cnt > rend || dstIdx + cnt > totalRawBytes) { contOk = false; break; }
                        std::memcpy(rawPlane.data() + dstIdx, rle, cnt);
                        rle += cnt;
                        dstIdx += cnt;
                    } else if (n != -128) {
                        size_t cnt = static_cast<size_t>(1 - static_cast<int>(n));
                        if (rle >= rend || dstIdx + cnt > totalRawBytes) { contOk = false; break; }
                        uint8_t val = *rle++;
                        std::memset(rawPlane.data() + dstIdx, val, cnt);
                        dstIdx += cnt;
                    }
                }
                size_t consumed = static_cast<size_t>(rle - pix);
                if (!contOk || dstIdx != totalRawBytes) return false;
                if (strictTrailingBound && availBytes > consumed + 64u) return false;
            }
        }

        size_t pxCount = static_cast<size_t>(pxCount64);
        outAlpha.resize(pxCount);
        if (depth == 8) {
            std::memcpy(outAlpha.data(), rawPlane.data(), pxCount);
        } else {
            uint16_t maxV16 = 0;
            for (size_t i = 0; i < pxCount; ++i) {
                uint16_t v16 = DetailImg100::RdU16BE(rawPlane.data() + i * 2u);
                if (v16 > maxV16) maxV16 = v16;
            }
            for (size_t i = 0; i < pxCount; ++i) {
                uint16_t v16 = DetailImg100::RdU16BE(rawPlane.data() + i * 2u);
                if (maxV16 > 32768u) {
                    outAlpha[i] = static_cast<uint8_t>((static_cast<uint32_t>(v16) * 255u + 32767u) / 65535u);
                } else if (maxV16 > 255u) {
                    outAlpha[i] = static_cast<uint8_t>(std::min<uint32_t>(255u, (static_cast<uint32_t>(v16) * 255u + 16384u) / 32768u));
                } else {
                    outAlpha[i] = static_cast<uint8_t>(v16);
                }
            }
        }

        bool hasInk = false;
        for (uint8_t a : outAlpha) {
            if (a > 0) { hasInk = true; break; }
        }
        return hasInk;
    };

    if (ver == 1 || ver == 2) {
        uint16_t count = DetailImg100::RdU16BE(data + 2);
        if (count == 0) return false;
        size_t pos = 4;
        for (uint16_t b = 0; b < count && pos + 6 <= size && tips.size() < 8; ++b) {
            uint16_t btype = DetailImg100::RdU16BE(data + pos);
            uint32_t bsize = DetailImg100::RdU32BE(data + pos + 2);
            pos += 6;
            if (bsize > size - pos) break;

            if (btype == 2 && bsize >= 34) { // Sampled brush record
                const uint8_t* recStart = data + pos;
                const uint8_t* recEnd   = recStart + bsize;

                auto tryParseV12At = [&](const uint8_t* afterName, const std::wstring& bName) -> bool {
                    if (afterName + 28 > recEnd) return false;
                    // afterName[0] = antialias (1 byte), afterName[1..8] = shortBounds[4]
                    int16_t sTop    = static_cast<int16_t>(DetailImg100::RdU16BE(afterName + 1));
                    int16_t sLeft   = static_cast<int16_t>(DetailImg100::RdU16BE(afterName + 3));
                    int16_t sBottom = static_cast<int16_t>(DetailImg100::RdU16BE(afterName + 5));
                    int16_t sRight  = static_cast<int16_t>(DetailImg100::RdU16BE(afterName + 7));

                    const uint8_t* bp = afterName + 9;
                    int32_t top    = static_cast<int32_t>(DetailImg100::RdU32BE(bp + 0));
                    int32_t left   = static_cast<int32_t>(DetailImg100::RdU32BE(bp + 4));
                    int32_t bottom = static_cast<int32_t>(DetailImg100::RdU32BE(bp + 8));
                    int32_t right  = static_cast<int32_t>(DetailImg100::RdU32BE(bp + 12));
                    uint16_t depth = DetailImg100::RdU16BE(bp + 16);
                    uint8_t comp   = bp[18];
                    const uint8_t* pix = bp + 19;

                    if (bottom <= top || right <= left) {
                        top = sTop; left = sLeft; bottom = sBottom; right = sRight;
                    }
                    int64_t bw64 = static_cast<int64_t>(right) - static_cast<int64_t>(left);
                    int64_t bh64 = static_cast<int64_t>(bottom) - static_cast<int64_t>(top);
                    if (bw64 <= 0 || bh64 <= 0 || bw64 > 8192 || bh64 > 8192) return false;

                    BrushTip tip;
                    tip.w = static_cast<uint32_t>(bw64);
                    tip.h = static_cast<uint32_t>(bh64);
                    tip.name = bName;
                    size_t avail = static_cast<size_t>(recEnd - pix);
                    if (!decodeAbrSampleBitmap(pix, avail, tip.w, tip.h, depth, comp, true, tip.alpha)) {
                        return false;
                    }
                    tips.push_back(std::move(tip));
                    return true;
                };

                // Skip misc (4 bytes) + spacing (2 bytes) = 6 bytes
                const uint8_t* pName = recStart + 6;
                if (ver == 1) {
                    tryParseV12At(pName, L"");
                } else {
                    bool parsed = false;
                    if (pName + 4 <= recEnd) {
                        uint32_t nameLen = DetailImg100::RdU32BE(pName);
                        if (nameLen <= 2048 && pName + 4 + static_cast<size_t>(nameLen) * 2u <= recEnd) {
                            std::wstring bName;
                            for (uint32_t ci = 0; ci < nameLen; ++ci) {
                                wchar_t wc = static_cast<wchar_t>(DetailImg100::RdU16BE(pName + 4 + static_cast<size_t>(ci) * 2u));
                                if (wc != 0) bName.push_back(wc);
                            }
                            parsed = tryParseV12At(pName + 4 + static_cast<size_t>(nameLen) * 2u, bName);
                        }
                    }
                    if (!parsed && pName + 1 <= recEnd) {
                        uint8_t pasLen = pName[0];
                        if (pName + 1 + pasLen <= recEnd) {
                            parsed = tryParseV12At(pName + 1 + pasLen, L"");
                        }
                    }
                }
            }
            pos += bsize;
        }
    } else {
        // ABR v6, v7, v10: walk top-level 8BIM blocks (with fallback scan for "8BIMsamp")
        auto parseSampSection = [&](size_t secStart, size_t secEnd) {
            size_t p = secStart;
            while (p + 4 <= secEnd && tips.size() < 8) {
                uint32_t bLen = DetailImg100::RdU32BE(data + p);
                p += 4;
                if (bLen == 0 || bLen > secEnd - p) break;

                const uint8_t* entryStart = data + p;
                uint8_t idLen = entryStart[0];
                size_t candidates[6] = {};
                size_t numCand = 0;
                auto pushCand = [&](size_t off) {
                    for (size_t i = 0; i < numCand; ++i) {
                        if (candidates[i] == off) return;
                    }
                    if (numCand < 6) candidates[numCand++] = off;
                };

                if (subVer == 1) {
                    pushCand(47u);
                    pushCand(static_cast<size_t>(1u + idLen + 10u));
                    pushCand(37u);
                    pushCand(static_cast<size_t>(1u + idLen));
                    pushCand(301u);
                    pushCand(264u);
                } else {
                    pushCand(301u);
                    pushCand(static_cast<size_t>(1u + idLen + 264u));
                    pushCand(264u);
                    pushCand(47u);
                    pushCand(37u);
                }

                for (size_t ci = 0; ci < numCand; ++ci) {
                    size_t hdrSkip = candidates[ci];
                    if (hdrSkip + 19u >= bLen) continue;
                    const uint8_t* bp = entryStart + hdrSkip;
                    int32_t top    = static_cast<int32_t>(DetailImg100::RdU32BE(bp + 0));
                    int32_t left   = static_cast<int32_t>(DetailImg100::RdU32BE(bp + 4));
                    int32_t bottom = static_cast<int32_t>(DetailImg100::RdU32BE(bp + 8));
                    int32_t right  = static_cast<int32_t>(DetailImg100::RdU32BE(bp + 12));
                    uint16_t depth = DetailImg100::RdU16BE(bp + 16);
                    uint8_t comp   = bp[18];

                    int64_t bw64 = static_cast<int64_t>(right) - static_cast<int64_t>(left);
                    int64_t bh64 = static_cast<int64_t>(bottom) - static_cast<int64_t>(top);
                    if (bw64 <= 0 || bh64 <= 0 || bw64 > 8192 || bh64 > 8192) continue;

                    BrushTip tip;
                    tip.w = static_cast<uint32_t>(bw64);
                    tip.h = static_cast<uint32_t>(bh64);
                    size_t avail = static_cast<size_t>(bLen) - (hdrSkip + 19u);
                    if (decodeAbrSampleBitmap(bp + 19u, avail, tip.w, tip.h, depth, comp, true, tip.alpha)) {
                        tips.push_back(std::move(tip));
                        break;
                    }
                }

                size_t paddedLen = (static_cast<size_t>(bLen) + 3u) & ~3u;
                if (paddedLen <= secEnd - p) {
                    p += paddedLen;
                } else {
                    p = secEnd;
                }
            }
        };

        bool foundSampBlock = false;
        size_t pos = 4;
        while (pos + 12 <= size && std::memcmp(data + pos, "8BIM", 4) == 0) {
            const uint8_t* key = data + pos + 4;
            uint32_t secLen = DetailImg100::RdU32BE(data + pos + 8);
            size_t secStart = pos + 12;
            if (secLen > size - secStart) {
                secLen = static_cast<uint32_t>(size - secStart);
            }
            size_t secEnd = secStart + secLen;
            if (std::memcmp(key, "samp", 4) == 0) {
                foundSampBlock = true;
                parseSampSection(secStart, secEnd);
            }
            pos = secEnd;
            // Align to next 8BIM tag across 2-byte or 4-byte resource padding
            for (size_t pad = 0; pad < 4 && pos < size; ++pad) {
                if (pos + 4 <= size && std::memcmp(data + pos, "8BIM", 4) == 0) break;
                if (data[pos] != 0) break;
                ++pos;
            }
        }

        if (!foundSampBlock) {
            for (size_t i = 4; i + 12 <= size; ++i) {
                if (std::memcmp(data + i, "8BIMsamp", 8) == 0) {
                    uint32_t secLen = DetailImg100::RdU32BE(data + i + 8);
                    size_t secStart = i + 12;
                    size_t secEnd = (secLen <= size - secStart) ? (secStart + secLen) : size;
                    parseSampSection(secStart, secEnd);
                    break;
                }
            }
        }
    }

    // Strictly reject if zero real sampled brush bitmaps were decoded (no fake circle card!)
    if (tips.empty()) return false;

    for (const auto& t : tips) {
        if (!t.name.empty()) outInfo.layerNames.push_back(t.name);
    }

    if (tips.size() == 1) {
        const auto& tip = tips[0];
        uint32_t dstW = tip.w;
        uint32_t dstH = tip.h;
        if (dstW > 512u || dstH > 512u) {
            double scale = 512.0 / static_cast<double>(std::max(dstW, dstH));
            dstW = std::max(1u, static_cast<uint32_t>(std::round(static_cast<double>(tip.w) * scale)));
            dstH = std::max(1u, static_cast<uint32_t>(std::round(static_cast<double>(tip.h) * scale)));
        }

        outInfo.width = dstW;
        outInfo.height = dstH;
        outInfo.origWidth = tip.w;
        outInfo.origHeight = tip.h;
        outInfo.channels = 4;
        outInfo.bitsPerChannel = 8;
        outInfo.formatName = L"Adobe Photoshop Brush (.abr)";
        outInfo.bgraPixels.resize(static_cast<size_t>(dstW) * dstH * 4u);

        for (uint32_t dy = 0; dy < dstH; ++dy) {
            uint32_t sy = static_cast<uint32_t>((static_cast<uint64_t>(dy) * tip.h) / dstH);
            for (uint32_t dx = 0; dx < dstW; ++dx) {
                uint32_t sx = static_cast<uint32_t>((static_cast<uint64_t>(dx) * tip.w) / dstW);
                uint8_t a = tip.alpha[static_cast<size_t>(sy) * tip.w + sx];
                uint8_t* dst = &outInfo.bgraPixels[(static_cast<size_t>(dy) * dstW + dx) * 4u];
                dst[0] = static_cast<uint8_t>(28u + (224u * a + 127u) / 255u);
                dst[1] = static_cast<uint8_t>(26u + (224u * a + 127u) / 255u);
                dst[2] = static_cast<uint8_t>(24u + (226u * a + 127u) / 255u);
                dst[3] = 255u;
            }
        }
        outInfo.valid = true;
        return true;
    }

    // Multiple brush tips: composite up to 4 (2x2) or 6 (3x2) aspect-preserved cells
    uint32_t gridCols = (tips.size() <= 4) ? 2u : 3u;
    uint32_t gridRows = 2u;
    uint32_t cellSize = 128u;
    uint32_t pad = 12u;
    uint32_t W = gridCols * cellSize + (gridCols + 1u) * pad;
    uint32_t H = gridRows * cellSize + (gridRows + 1u) * pad;

    outInfo.width = W;
    outInfo.height = H;
    outInfo.origWidth = tips[0].w;
    outInfo.origHeight = tips[0].h;
    outInfo.channels = 4;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = L"Adobe Photoshop Brush Set (.abr)";
    outInfo.bgraPixels.resize(static_cast<size_t>(W) * H * 4u);
    DetailImg200::FillRectBGRA(outInfo.bgraPixels, W, H, 0, 0, static_cast<int>(W), static_cast<int>(H), 20, 20, 24);

    size_t maxCells = static_cast<size_t>(gridCols) * gridRows;
    for (size_t t = 0; t < tips.size() && t < maxCells; ++t) {
        uint32_t gx = static_cast<uint32_t>(t) % gridCols;
        uint32_t gy = static_cast<uint32_t>(t) / gridCols;
        uint32_t cellX = pad + gx * (cellSize + pad);
        uint32_t cellY = pad + gy * (cellSize + pad);
        DetailImg200::FillRectBGRA(
            outInfo.bgraPixels, W, H,
            static_cast<int>(cellX), static_cast<int>(cellY),
            static_cast<int>(cellX + cellSize), static_cast<int>(cellY + cellSize),
            34, 34, 39);

        const auto& tip = tips[t];
        uint32_t innerMax = cellSize - 12u;
        double fitScale = std::min(1.0, static_cast<double>(innerMax) / static_cast<double>(std::max(tip.w, tip.h)));
        if (tip.w < innerMax / 2u && tip.h < innerMax / 2u) {
            fitScale = static_cast<double>(innerMax) / static_cast<double>(std::max(tip.w, tip.h));
        }
        uint32_t drawW = std::max(1u, static_cast<uint32_t>(std::round(static_cast<double>(tip.w) * fitScale)));
        uint32_t drawH = std::max(1u, static_cast<uint32_t>(std::round(static_cast<double>(tip.h) * fitScale)));
        uint32_t ox = cellX + (cellSize - drawW) / 2u;
        uint32_t oy = cellY + (cellSize - drawH) / 2u;

        for (uint32_t dy = 0; dy < drawH; ++dy) {
            uint32_t sy = static_cast<uint32_t>((static_cast<uint64_t>(dy) * tip.h) / drawH);
            for (uint32_t dx = 0; dx < drawW; ++dx) {
                uint32_t sx = static_cast<uint32_t>((static_cast<uint64_t>(dx) * tip.w) / drawW);
                uint8_t a = tip.alpha[static_cast<size_t>(sy) * tip.w + sx];
                uint8_t* dst = &outInfo.bgraPixels[(static_cast<size_t>(oy + dy) * W + (ox + dx)) * 4u];
                dst[0] = static_cast<uint8_t>(39u + (214u * a + 127u) / 255u);
                dst[1] = static_cast<uint8_t>(34u + (216u * a + 127u) / 255u);
                dst[2] = static_cast<uint8_t>(34u + (218u * a + 127u) / 255u);
                dst[3] = 255u;
            }
        }
    }
    outInfo.valid = true;
    return true;
}

} // namespace Formats
