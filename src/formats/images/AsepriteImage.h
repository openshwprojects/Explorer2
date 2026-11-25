#pragma once
// AsepriteImage.h — Aseprite Sprite, Linked Cel, Legacy Palette & v1.3 Tilemap Compositor (.ase, .aseprite)

namespace Formats {

namespace DetailArtFont {

inline DetailLayeredAndHdrImg::LayerBlendOp MapAsepriteBlendMode(uint16_t mode) {
    using DetailLayeredAndHdrImg::LayerBlendOp;
    switch (mode) {
        case 1:  return LayerBlendOp::Multiply;
        case 2:  return LayerBlendOp::Screen;
        case 3:  return LayerBlendOp::Overlay;
        case 4:  return LayerBlendOp::Darken;
        case 5:  return LayerBlendOp::Lighten;
        case 6:  return LayerBlendOp::ColorDodge;
        case 7:  return LayerBlendOp::ColorBurn;
        case 8:  return LayerBlendOp::HardLight;
        case 9:  return LayerBlendOp::SoftLight;
        case 10: return LayerBlendOp::Difference;
        case 11: return LayerBlendOp::Exclusion;
        case 16: return LayerBlendOp::Add;
        case 17: return LayerBlendOp::Subtract;
        case 18: return LayerBlendOp::Divide;
        default: return LayerBlendOp::SrcOver;
    }
}

inline bool DecodeAsepriteDocument(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 128 || DetailLayeredAndHdrImg::RdU16LE(data + 4) != 0xA5E0u) return false;
    const uint16_t frames      = DetailLayeredAndHdrImg::RdU16LE(data + 6);
    const uint16_t w           = DetailLayeredAndHdrImg::RdU16LE(data + 8);
    const uint16_t h           = DetailLayeredAndHdrImg::RdU16LE(data + 10);
    const uint16_t depth       = DetailLayeredAndHdrImg::RdU16LE(data + 12);
    const uint32_t headerFlags = DetailLayeredAndHdrImg::RdU32LE(data + 14);
    const uint8_t  transIdx    = data[28];
    if (frames == 0 || w == 0 || h == 0 || w > 8192 || h > 8192 || (depth != 32 && depth != 16 && depth != 8)) {
        return false;
    }

    outInfo.width = w;
    outInfo.height = h;
    outInfo.origWidth = w;
    outInfo.origHeight = h;
    outInfo.channels = 4u;
    outInfo.bitsPerChannel = 8u;
    outInfo.formatName = L"Aseprite Sprite (" + std::to_wstring(frames) + L" frames, " + std::to_wstring(depth) + L"-bit)";
    outInfo.bgraPixels.assign(static_cast<size_t>(w) * h * 4u, 0u);

    uint32_t palette[256] = {};
    bool hasNewPalette2019 = false;
    for (int i = 0; i < 256; ++i) {
        const uint8_t c = static_cast<uint8_t>(i);
        const uint8_t a = (depth == 8 && i == transIdx) ? 0u : 255u;
        palette[i] = (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(c) << 16) | (static_cast<uint32_t>(c) << 8) | c;
    }

    struct AseLayer {
        uint16_t flags = 1;
        uint16_t layerType = 0; // 0 = normal, 1 = group, 2 = tilemap
        uint16_t childLevel = 0;
        uint16_t blendMode = 0;
        uint8_t  opacity = 255;
        uint32_t tilesetIndex = 0;
    };
    struct AseTileset {
        uint32_t id = 0;
        uint32_t flags = 0;
        uint32_t numTiles = 0;
        uint16_t tileW = 0;
        uint16_t tileH = 0;
        std::vector<uint8_t> pixels;
    };
    struct AseCelChunk {
        uint16_t frameIdx = 0;
        uint16_t layerIdx = 0;
        int16_t  celX = 0;
        int16_t  celY = 0;
        uint8_t  celOpac = 255;
        uint16_t celType = 0; // 0 = Raw, 1 = Linked, 2 = Compressed Image, 3 = Compressed Tilemap
        uint16_t linkedFrame = 0;
        const uint8_t* cdata = nullptr;
        size_t clen = 0;
    };

    std::vector<AseLayer> layers;
    std::map<uint32_t, AseTileset> tilesets;
    const uint16_t scanFrames = std::min<uint16_t>(frames, 64u);
    std::vector<std::vector<AseCelChunk>> frameCels(scanFrames);
    std::map<uint32_t, size_t> celLookup; // (frame << 16) | layer -> index in frameCels[frame]

    const uint32_t bpp = depth / 8u;
    size_t pos = 128;

    // Pass 1: Parse layers, palettes (0x2019 + legacy 0x0004 / 0x0011), tilesets (0x2023), and index cel chunks across frames
    for (uint16_t f = 0; f < scanFrames && pos + 16u <= size; ++f) {
        const uint32_t frameBytes = DetailLayeredAndHdrImg::RdU32LE(data + pos);
        const uint16_t frameMagic = DetailLayeredAndHdrImg::RdU16LE(data + pos + 4);
        if (frameMagic != 0xF1FAu || frameBytes < 16u || pos + frameBytes > size) break;
        uint32_t numChunks = DetailLayeredAndHdrImg::RdU32LE(data + pos + 12);
        if (numChunks == 0) numChunks = DetailLayeredAndHdrImg::RdU16LE(data + pos + 6);

        size_t cpos = pos + 16u;
        const size_t frameEnd = pos + frameBytes;
        for (uint32_t c = 0; c < numChunks && cpos + 6u <= frameEnd; ++c) {
            const uint32_t chunkSize = DetailLayeredAndHdrImg::RdU32LE(data + cpos);
            const uint16_t chunkType = DetailLayeredAndHdrImg::RdU16LE(data + cpos + 4);
            if (chunkSize < 6u || cpos + chunkSize > frameEnd) break;
            const uint8_t* cdata = data + cpos + 6u;
            const size_t clen = chunkSize - 6u;

            if (chunkType == 0x2004u && clen >= 18u) { // Layer chunk
                AseLayer ly;
                ly.flags      = DetailLayeredAndHdrImg::RdU16LE(cdata + 0);
                ly.layerType  = DetailLayeredAndHdrImg::RdU16LE(cdata + 2);
                ly.childLevel = DetailLayeredAndHdrImg::RdU16LE(cdata + 4);
                ly.blendMode  = DetailLayeredAndHdrImg::RdU16LE(cdata + 10);
                ly.opacity    = cdata[12];
                const uint16_t nameLen = DetailLayeredAndHdrImg::RdU16LE(cdata + 16);
                if (18u + nameLen <= clen) {
                    std::string lname(reinterpret_cast<const char*>(cdata + 18), nameLen);
                    if (outInfo.layerNames.size() < 64u) outInfo.layerNames.push_back(PathUtils::Utf8ToWide(lname));
                    if (ly.layerType == 2u && 18u + nameLen + 4u <= clen) {
                        ly.tilesetIndex = DetailLayeredAndHdrImg::RdU32LE(cdata + 18 + nameLen);
                    }
                }
                layers.push_back(ly);
            } else if (chunkType == 0x2019u && clen >= 20u) { // Modern Palette chunk
                hasNewPalette2019 = true;
                const uint32_t firstIdx = DetailLayeredAndHdrImg::RdU32LE(cdata + 4);
                const uint32_t lastIdx  = DetailLayeredAndHdrImg::RdU32LE(cdata + 8);
                size_t pOff = 20u;
                for (uint32_t pi = firstIdx; pi <= lastIdx && pi < 256u && pOff + 6u <= clen; ++pi) {
                    const uint16_t pFlags = DetailLayeredAndHdrImg::RdU16LE(cdata + pOff);
                    const uint8_t pr = cdata[pOff + 2], pg = cdata[pOff + 3], pb = cdata[pOff + 4], pa = cdata[pOff + 5];
                    palette[pi] = (static_cast<uint32_t>(pa) << 24) | (static_cast<uint32_t>(pr) << 16) |
                                  (static_cast<uint32_t>(pg) << 8) | pb;
                    pOff += 6u;
                    if ((pFlags & 1u) != 0u && pOff + 2u <= clen) {
                        const uint16_t nlen = DetailLayeredAndHdrImg::RdU16LE(cdata + pOff);
                        pOff += 2u + nlen;
                    }
                }
            } else if ((chunkType == 0x0004u || chunkType == 0x0011u) && !hasNewPalette2019 && clen >= 2u) {
                // Legacy Palette chunks: 0x0004 (8-bit 0..255 RGB packets) & 0x0011 (6-bit 0..63 VGA RGB packets)
                const bool scale6Bit = (chunkType == 0x0011u);
                const uint16_t numPackets = DetailLayeredAndHdrImg::RdU16LE(cdata);
                size_t pOff = 2u;
                uint32_t palIdx = 0;
                for (uint16_t pk = 0; pk < numPackets && pOff + 2u <= clen; ++pk) {
                    const uint8_t skip = cdata[pOff++];
                    const uint32_t cnt = (cdata[pOff] == 0u) ? 256u : static_cast<uint32_t>(cdata[pOff]);
                    ++pOff;
                    palIdx += skip;
                    for (uint32_t k = 0; k < cnt && pOff + 3u <= clen; ++k, ++palIdx) {
                        uint8_t pr = cdata[pOff + 0], pg = cdata[pOff + 1], pb = cdata[pOff + 2];
                        pOff += 3u;
                        if (scale6Bit) {
                            pr = static_cast<uint8_t>((std::min<uint8_t>(pr, 63u) * 255u + 31u) / 63u);
                            pg = static_cast<uint8_t>((std::min<uint8_t>(pg, 63u) * 255u + 31u) / 63u);
                            pb = static_cast<uint8_t>((std::min<uint8_t>(pb, 63u) * 255u + 31u) / 63u);
                        }
                        if (palIdx < 256u) {
                            const uint8_t pa = (depth == 8 && palIdx == transIdx) ? 0u : 255u;
                            palette[palIdx] = (static_cast<uint32_t>(pa) << 24) | (static_cast<uint32_t>(pr) << 16) |
                                              (static_cast<uint32_t>(pg) << 8) | pb;
                        }
                    }
                }
            } else if (chunkType == 0x2023u && clen >= 34u) { // Aseprite v1.3 Tileset chunk
                AseTileset ts;
                ts.id       = DetailLayeredAndHdrImg::RdU32LE(cdata + 0);
                ts.flags    = DetailLayeredAndHdrImg::RdU32LE(cdata + 4);
                ts.numTiles = DetailLayeredAndHdrImg::RdU32LE(cdata + 8);
                ts.tileW    = DetailLayeredAndHdrImg::RdU16LE(cdata + 12);
                ts.tileH    = DetailLayeredAndHdrImg::RdU16LE(cdata + 14);
                const uint16_t nameLen = DetailLayeredAndHdrImg::RdU16LE(cdata + 32);
                size_t pOff = 34u + nameLen;
                if ((ts.flags & 0x01u) != 0u) pOff += 8u;
                if ((ts.flags & 0x02u) != 0u && ts.numTiles > 0 && ts.tileW > 0 && ts.tileH > 0 &&
                    ts.tileW <= 1024 && ts.tileH <= 1024 && ts.numTiles <= 65536 && pOff + 4u <= clen)
                {
                    const uint32_t compLen = DetailLayeredAndHdrImg::RdU32LE(cdata + pOff);
                    pOff += 4u;
                    const size_t expectedBytes = static_cast<size_t>(ts.tileW) * ts.tileH * ts.numTiles * bpp;
                    if (compLen <= clen - pOff && expectedBytes <= 64ULL * 1024 * 1024 &&
                        ZipDeflate::InflateZlib(cdata + pOff, compLen, ts.pixels, expectedBytes + 64) &&
                        ts.pixels.size() >= expectedBytes)
                    {
                        tilesets[ts.id] = std::move(ts);
                    }
                }
            } else if (chunkType == 0x2005u && clen >= 16u) { // Cel chunk
                AseCelChunk cc;
                cc.frameIdx = f;
                cc.layerIdx = DetailLayeredAndHdrImg::RdU16LE(cdata + 0);
                cc.celX     = static_cast<int16_t>(DetailLayeredAndHdrImg::RdU16LE(cdata + 2));
                cc.celY     = static_cast<int16_t>(DetailLayeredAndHdrImg::RdU16LE(cdata + 4));
                cc.celOpac  = cdata[6];
                cc.celType  = DetailLayeredAndHdrImg::RdU16LE(cdata + 7);
                if (cc.celType == 1u && clen >= 18u) {
                    cc.linkedFrame = DetailLayeredAndHdrImg::RdU16LE(cdata + 16);
                }
                cc.cdata = cdata;
                cc.clen  = clen;
                const uint32_t key = (static_cast<uint32_t>(f) << 16) | cc.layerIdx;
                celLookup[key] = frameCels[f].size();
                frameCels[f].push_back(cc);
            }
            cpos += chunkSize;
        }
        pos += frameBytes;
    }

    // Determine whether layer opacity values are populated (headerFlags bit 0, or any non-zero layer opacity)
    bool anyNonZeroLayerOpacity = false;
    for (const auto& ly : layers) {
        if (ly.opacity > 0u) { anyNonZeroLayerOpacity = true; break; }
    }
    const bool honourLayerOpacity = ((headerFlags & 1u) != 0u) || anyNonZeroLayerOpacity;

    // Precompute group visibility inheritance via childLevel
    std::vector<bool> layerEffectiveVisible(layers.size(), true);
    {
        std::vector<std::pair<uint16_t, bool>> groupStack;
        for (size_t i = 0; i < layers.size(); ++i) {
            while (!groupStack.empty() && groupStack.back().first >= layers[i].childLevel) {
                groupStack.pop_back();
            }
            const bool parentVis = groupStack.empty() ? true : groupStack.back().second;
            const bool selfVis = parentVis && ((layers[i].flags & 0x01u) != 0u);
            layerEffectiveVisible[i] = selfVis;
            if (layers[i].layerType == 1u) {
                groupStack.push_back({ layers[i].childLevel, selfVis });
            }
        }
    }

    auto samplePixelToRgba = [&](const uint8_t* sp, bool isBackgroundLayer, uint8_t& sr, uint8_t& sg, uint8_t& sb, uint8_t& sa) {
        if (depth == 32) {
            sr = sp[0]; sg = sp[1]; sb = sp[2]; sa = sp[3];
        } else if (depth == 16) {
            sr = sg = sb = sp[0]; sa = sp[1];
        } else {
            const uint8_t idx = sp[0];
            if (!isBackgroundLayer && idx == transIdx) {
                sr = sg = sb = sa = 0u;
            } else {
                const uint32_t c32 = palette[idx];
                sb = static_cast<uint8_t>(c32 & 0xFFu);
                sg = static_cast<uint8_t>((c32 >> 8) & 0xFFu);
                sr = static_cast<uint8_t>((c32 >> 16) & 0xFFu);
                sa = isBackgroundLayer ? 255u : static_cast<uint8_t>((c32 >> 24) & 0xFFu);
            }
        }
    };

    // Resolve linked cels (celType == 1) up to 8 hops
    auto resolveCelPayload = [&](const AseCelChunk& startCel) -> const AseCelChunk* {
        const AseCelChunk* cur = &startCel;
        for (int hop = 0; hop < 8 && cur && cur->celType == 1u; ++hop) {
            if (cur->linkedFrame >= frameCels.size()) return nullptr;
            const uint32_t key = (static_cast<uint32_t>(cur->linkedFrame) << 16) | cur->layerIdx;
            auto it = celLookup.find(key);
            if (it == celLookup.end()) return nullptr;
            const AseCelChunk* target = &frameCels[cur->linkedFrame][it->second];
            if (target == cur) return nullptr;
            cur = target;
        }
        return (cur && cur->celType != 1u) ? cur : nullptr;
    };

    // Composite the first frame that contains renderable cels (frame 0 in standard sprites; later frame if frame 0 is empty,
    // or frame 1+ if frame 0 has no cels or linked cel tests live on frame 1 when frame 0 was blank)
    bool decodedAnyCel = false;
    for (uint16_t targetFrame = 0; targetFrame < scanFrames && !decodedAnyCel; ++targetFrame) {
        if (frameCels[targetFrame].empty()) continue;
        std::fill(outInfo.bgraPixels.begin(), outInfo.bgraPixels.end(), uint8_t{0});

        // Sort cels within frame by layerIdx ascending (bottom-to-top)
        std::vector<const AseCelChunk*> ordered;
        ordered.reserve(frameCels[targetFrame].size());
        for (const auto& cc : frameCels[targetFrame]) ordered.push_back(&cc);
        std::stable_sort(ordered.begin(), ordered.end(), [](const AseCelChunk* a, const AseCelChunk* b) {
            return a->layerIdx < b->layerIdx;
        });

        for (const AseCelChunk* instCel : ordered) {
            const uint16_t layerIdx = instCel->layerIdx;
            bool isVisible = true;
            bool isBgLayer = false;
            uint8_t layerOpac = 255u;
            uint32_t tsId = 0;
            DetailLayeredAndHdrImg::LayerBlendOp blendOp = DetailLayeredAndHdrImg::LayerBlendOp::SrcOver;

            if (layerIdx < layers.size()) {
                isVisible = layerEffectiveVisible[layerIdx] && (layers[layerIdx].layerType != 1u);
                isBgLayer = (layers[layerIdx].flags & 0x08u) != 0u;
                layerOpac = honourLayerOpacity ? layers[layerIdx].opacity : 255u;
                tsId = layers[layerIdx].tilesetIndex;
                blendOp = MapAsepriteBlendMode(layers[layerIdx].blendMode);
            }
            if (!isVisible || layerOpac == 0u || instCel->celOpac == 0u) continue;

            const AseCelChunk* srcCel = resolveCelPayload(*instCel);
            if (!srcCel) continue;

            const int16_t celX = instCel->celX;
            const int16_t celY = instCel->celY;
            const float effOpacity = (static_cast<float>(instCel->celOpac) / 255.0f) * (static_cast<float>(layerOpac) / 255.0f);
            const uint8_t* cdata = srcCel->cdata;
            const size_t clen = srcCel->clen;

            if ((srcCel->celType == 0u || srcCel->celType == 2u) && clen >= 20u) {
                const uint16_t cw = DetailLayeredAndHdrImg::RdU16LE(cdata + 16);
                const uint16_t ch = DetailLayeredAndHdrImg::RdU16LE(cdata + 18);
                std::vector<uint8_t> rawCel;
                const uint8_t* pixPtr = nullptr;
                size_t pixLen = 0;
                if (srcCel->celType == 0u) {
                    pixPtr = cdata + 20u;
                    pixLen = clen - 20u;
                } else if (srcCel->celType == 2u && clen > 22u) {
                    if (ZipDeflate::InflateZlib(cdata + 20u, clen - 20u, rawCel)) {
                        pixPtr = rawCel.data();
                        pixLen = rawCel.size();
                    }
                }
                if (pixPtr && cw > 0 && ch > 0 && pixLen >= static_cast<size_t>(cw) * ch * bpp) {
                    decodedAnyCel = true;
                    for (uint16_t py = 0; py < ch; ++py) {
                        const int dy = celY + py;
                        if (dy < 0 || dy >= h) continue;
                        for (uint16_t px = 0; px < cw; ++px) {
                            const int dx = celX + px;
                            if (dx < 0 || dx >= w) continue;
                            const uint8_t* sp = pixPtr + (static_cast<size_t>(py) * cw + px) * bpp;
                            uint8_t sr = 0, sg = 0, sb = 0, sa = 0;
                            samplePixelToRgba(sp, isBgLayer, sr, sg, sb, sa);
                            uint8_t pm[4];
                            WritePremulBGRA(pm, sr, sg, sb, sa);
                            DetailLayeredAndHdrImg::BlendPremulBgraPixel(
                                &outInfo.bgraPixels[(static_cast<size_t>(dy) * w + dx) * 4u],
                                pm[0], pm[1], pm[2], pm[3], effOpacity, blendOp);
                        }
                    }
                }
            } else if (srcCel->celType == 3u && clen >= 48u) { // Compressed Tilemap Cel (Aseprite v1.3+)
                auto tsIt = tilesets.find(tsId);
                if (tsIt == tilesets.end() && !tilesets.empty()) tsIt = tilesets.begin();
                if (tsIt != tilesets.end()) {
                    const AseTileset& ts = tsIt->second;
                    const uint16_t mapW        = DetailLayeredAndHdrImg::RdU16LE(cdata + 16);
                    const uint16_t mapH        = DetailLayeredAndHdrImg::RdU16LE(cdata + 18);
                    const uint16_t bitsPerTile = DetailLayeredAndHdrImg::RdU16LE(cdata + 20);
                    const uint32_t tileIdMask  = DetailLayeredAndHdrImg::RdU32LE(cdata + 22);
                    const uint32_t xFlipMask   = DetailLayeredAndHdrImg::RdU32LE(cdata + 26);
                    const uint32_t yFlipMask   = DetailLayeredAndHdrImg::RdU32LE(cdata + 30);
                    const uint32_t diagMask    = DetailLayeredAndHdrImg::RdU32LE(cdata + 34);
                    const uint32_t bytesPerTile = (bitsPerTile == 8) ? 1u : (bitsPerTile == 16) ? 2u : 4u;
                    const size_t needMapBytes = static_cast<size_t>(mapW) * mapH * bytesPerTile;

                    std::vector<uint8_t> mapData;
                    if (mapW > 0 && mapH > 0 && clen > 48u &&
                        ZipDeflate::InflateZlib(cdata + 48u, clen - 48u, mapData, needMapBytes + 64u) &&
                        mapData.size() >= needMapBytes)
                    {
                        decodedAnyCel = true;
                        const bool tileZeroIsEmpty = (ts.flags & 0x04u) == 0u;
                        const size_t singleTileBytes = static_cast<size_t>(ts.tileW) * ts.tileH * bpp;

                        for (uint16_t my = 0; my < mapH; ++my) {
                            for (uint16_t mx = 0; mx < mapW; ++mx) {
                                const uint8_t* mp = mapData.data() + (static_cast<size_t>(my) * mapW + mx) * bytesPerTile;
                                const uint32_t rawTile = (bytesPerTile == 1u) ? mp[0] :
                                                         (bytesPerTile == 2u) ? DetailLayeredAndHdrImg::RdU16LE(mp) :
                                                         DetailLayeredAndHdrImg::RdU32LE(mp);
                                uint32_t tileIdx = (tileIdMask != 0u) ? (rawTile & tileIdMask) : rawTile;
                                if (tileZeroIsEmpty) {
                                    if (tileIdx == 0u) continue;
                                    tileIdx -= 1u;
                                }
                                if (tileIdx >= ts.numTiles) continue;

                                const bool flipX = (xFlipMask != 0u) && ((rawTile & xFlipMask) != 0u);
                                const bool flipY = (yFlipMask != 0u) && ((rawTile & yFlipMask) != 0u);
                                const bool flipD = (diagMask  != 0u) && ((rawTile & diagMask)  != 0u);

                                const uint8_t* tileBase = ts.pixels.data() + static_cast<size_t>(tileIdx) * singleTileBytes;
                                const int baseX = celX + static_cast<int>(mx) * ts.tileW;
                                const int baseY = celY + static_cast<int>(my) * ts.tileH;

                                for (uint16_t ty = 0; ty < ts.tileH; ++ty) {
                                    const int dy = baseY + ty;
                                    if (dy < 0 || dy >= h) continue;
                                    for (uint16_t tx = 0; tx < ts.tileW; ++tx) {
                                        const int dx = baseX + tx;
                                        if (dx < 0 || dx >= w) continue;
                                        uint16_t sx = tx, sy = ty;
                                        if (flipD && ts.tileW == ts.tileH) std::swap(sx, sy);
                                        if (flipX) sx = static_cast<uint16_t>(ts.tileW - 1u - sx);
                                        if (flipY) sy = static_cast<uint16_t>(ts.tileH - 1u - sy);

                                        const uint8_t* sp = tileBase + (static_cast<size_t>(sy) * ts.tileW + sx) * bpp;
                                        uint8_t sr = 0, sg = 0, sb = 0, sa = 0;
                                        samplePixelToRgba(sp, isBgLayer, sr, sg, sb, sa);
                                        uint8_t pm[4];
                                        WritePremulBGRA(pm, sr, sg, sb, sa);
                                        DetailLayeredAndHdrImg::BlendPremulBgraPixel(
                                            &outInfo.bgraPixels[(static_cast<size_t>(dy) * w + dx) * 4u],
                                            pm[0], pm[1], pm[2], pm[3], effOpacity, blendOp);
                                    }
                                }
                            }
                        }
                        outInfo.formatName = L"Aseprite v1.3 Tilemap Sprite (" + std::to_wstring(ts.numTiles) + L" tiles)";
                    }
                }
            }
        }
    }

    if (!decodedAnyCel) return false;
    outInfo.valid = true;
    return true;
}

} // namespace DetailArtFont

inline bool DecodeAseprite(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    return DetailArtFont::DecodeAsepriteDocument(data, size, outInfo);
}

} // namespace Formats
