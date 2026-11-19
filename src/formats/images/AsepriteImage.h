#pragma once
// AsepriteImage.h — Aseprite Sprite & v1.3 Tilemap Cel Compositor (.ase, .aseprite)

namespace Formats {

namespace DetailArtFont {

// ----------------------------------------------------------------------------
// 5. Aseprite v1.3 (.aseprite) with 0x2023 Tileset & celType == 3 Tilemap Cel
// ----------------------------------------------------------------------------
inline bool DecodeAsepriteUpgraded(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 128 || DetailImg100::RdU16LE(data + 4) != 0xA5E0u) return false;
    uint16_t frames = DetailImg100::RdU16LE(data + 6);
    uint16_t w      = DetailImg100::RdU16LE(data + 8);
    uint16_t h      = DetailImg100::RdU16LE(data + 10);
    uint16_t depth  = DetailImg100::RdU16LE(data + 12);
    uint8_t  transIdx = data[28];
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
    for (int i = 0; i < 256; ++i) {
        uint8_t c = static_cast<uint8_t>(i);
        uint8_t a = (depth == 8 && i == transIdx) ? 0u : 255u;
        palette[i] = (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(c) << 16) | (static_cast<uint32_t>(c) << 8) | c;
    }

    struct AseLayer {
        uint16_t flags = 1;
        uint16_t layerType = 0; // 0 = normal, 1 = group, 2 = tilemap
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
        std::vector<uint8_t> pixels; // tileW * (tileH * numTiles) * (depth / 8)
    };
    std::vector<AseLayer> layers;
    std::map<uint32_t, AseTileset> tilesets;

    auto samplePixelToRgba = [&](const uint8_t* sp, bool isBackgroundLayer, uint8_t& sr, uint8_t& sg, uint8_t& sb, uint8_t& sa) {
        if (depth == 32) {
            sr = sp[0]; sg = sp[1]; sb = sp[2]; sa = sp[3];
        } else if (depth == 16) {
            sr = sg = sb = sp[0]; sa = sp[1];
        } else {
            uint8_t idx = sp[0];
            if (!isBackgroundLayer && idx == transIdx) {
                sr = sg = sb = sa = 0u;
            } else {
                uint32_t c32 = palette[idx];
                sb = static_cast<uint8_t>(c32 & 0xFFu);
                sg = static_cast<uint8_t>((c32 >> 8) & 0xFFu);
                sr = static_cast<uint8_t>((c32 >> 16) & 0xFFu);
                sa = isBackgroundLayer ? 255u : static_cast<uint8_t>((c32 >> 24) & 0xFFu);
            }
        }
    };

    const uint32_t bpp = depth / 8u;
    size_t pos = 128;
    bool decodedAnyCel = false;

    for (uint16_t f = 0; f < std::min<uint16_t>(frames, 4) && pos + 16 <= size; ++f) {
        uint32_t frameBytes = DetailImg100::RdU32LE(data + pos);
        uint16_t frameMagic = DetailImg100::RdU16LE(data + pos + 4);
        if (frameMagic != 0xF1FAu || frameBytes < 16 || pos + frameBytes > size) break;
        uint32_t numChunks = DetailImg100::RdU32LE(data + pos + 12);
        if (numChunks == 0) numChunks = DetailImg100::RdU16LE(data + pos + 6);

        size_t cpos = pos + 16;
        size_t frameEnd = pos + frameBytes;
        for (uint32_t c = 0; c < numChunks && cpos + 6 <= frameEnd; ++c) {
            uint32_t chunkSize = DetailImg100::RdU32LE(data + cpos);
            uint16_t chunkType = DetailImg100::RdU16LE(data + cpos + 4);
            if (chunkSize < 6 || cpos + chunkSize > frameEnd) break;
            const uint8_t* cdata = data + cpos + 6;
            size_t clen = chunkSize - 6;

            if (chunkType == 0x2004u && clen >= 18) { // Layer chunk
                AseLayer ly;
                ly.flags     = DetailImg100::RdU16LE(cdata + 0);
                ly.layerType = DetailImg100::RdU16LE(cdata + 2);
                ly.blendMode = DetailImg100::RdU16LE(cdata + 10);
                ly.opacity   = cdata[12];
                uint16_t nameLen = DetailImg100::RdU16LE(cdata + 16);
                if (18u + nameLen <= clen) {
                    std::string lname(reinterpret_cast<const char*>(cdata + 18), nameLen);
                    outInfo.layerNames.push_back(PathUtils::Utf8ToWide(lname));
                    if (ly.layerType == 2u && 18u + nameLen + 4u <= clen) {
                        ly.tilesetIndex = DetailImg100::RdU32LE(cdata + 18 + nameLen);
                    }
                }
                layers.push_back(ly);
            } else if (chunkType == 0x2019u && clen >= 20) { // Palette chunk
                uint32_t firstIdx = DetailImg100::RdU32LE(cdata + 4);
                uint32_t lastIdx  = DetailImg100::RdU32LE(cdata + 8);
                size_t pOff = 20;
                for (uint32_t pi = firstIdx; pi <= lastIdx && pi < 256 && pOff + 6 <= clen; ++pi) {
                    uint16_t flags = DetailImg100::RdU16LE(cdata + pOff);
                    uint8_t pr = cdata[pOff + 2], pg = cdata[pOff + 3], pb = cdata[pOff + 4], pa = cdata[pOff + 5];
                    palette[pi] = (static_cast<uint32_t>(pa) << 24) | (static_cast<uint32_t>(pr) << 16) |
                                  (static_cast<uint32_t>(pg) << 8) | pb;
                    pOff += 6;
                    if ((flags & 1u) != 0 && pOff + 2 <= clen) {
                        uint16_t nlen = DetailImg100::RdU16LE(cdata + pOff);
                        pOff += 2 + nlen;
                    }
                }
            } else if (chunkType == 0x2023u && clen >= 34) { // Aseprite v1.3 Tileset chunk
                AseTileset ts;
                ts.id       = DetailImg100::RdU32LE(cdata + 0);
                ts.flags    = DetailImg100::RdU32LE(cdata + 4);
                ts.numTiles = DetailImg100::RdU32LE(cdata + 8);
                ts.tileW    = DetailImg100::RdU16LE(cdata + 12);
                ts.tileH    = DetailImg100::RdU16LE(cdata + 14);
                uint16_t nameLen = DetailImg100::RdU16LE(cdata + 32);
                size_t pOff = 34u + nameLen;
                if ((ts.flags & 0x01u) != 0u) pOff += 8u; // external file ID + tileset ID
                if ((ts.flags & 0x02u) != 0u && ts.numTiles > 0 && ts.tileW > 0 && ts.tileH > 0 &&
                    ts.tileW <= 1024 && ts.tileH <= 1024 && ts.numTiles <= 65536 && pOff + 4u <= clen)
                {
                    uint32_t compLen = DetailImg100::RdU32LE(cdata + pOff);
                    pOff += 4u;
                    size_t expectedBytes = static_cast<size_t>(ts.tileW) * ts.tileH * ts.numTiles * bpp;
                    if (compLen <= clen - pOff && expectedBytes <= 64ULL * 1024 * 1024 &&
                        ZipDeflate::InflateZlib(cdata + pOff, compLen, ts.pixels, expectedBytes + 64) &&
                        ts.pixels.size() >= expectedBytes)
                    {
                        tilesets[ts.id] = std::move(ts);
                    }
                }
            } else if (chunkType == 0x2005u && f == 0 && clen >= 16) { // Cel chunk (Composite frame 0)
                uint16_t layerIdx = DetailImg100::RdU16LE(cdata + 0);
                int16_t celX      = static_cast<int16_t>(DetailImg100::RdU16LE(cdata + 2));
                int16_t celY      = static_cast<int16_t>(DetailImg100::RdU16LE(cdata + 4));
                uint8_t celOpac   = cdata[6];
                uint16_t celType  = DetailImg100::RdU16LE(cdata + 7);

                bool isVisible = true;
                bool isBgLayer = false;
                uint8_t layerOpac = 255;
                uint32_t tsId = 0;
                if (layerIdx < layers.size()) {
                    isVisible = (layers[layerIdx].flags & 0x01u) != 0u;
                    isBgLayer = (layers[layerIdx].flags & 0x08u) != 0u;
                    if (layers[layerIdx].opacity > 0) layerOpac = layers[layerIdx].opacity;
                    tsId = layers[layerIdx].tilesetIndex;
                }
                if (!isVisible) { cpos += chunkSize; continue; }
                float effOpacity = (static_cast<float>(celOpac) / 255.0f) * (static_cast<float>(layerOpac) / 255.0f);

                if ((celType == 0u || celType == 2u) && clen >= 20) {
                    uint16_t cw = DetailImg100::RdU16LE(cdata + 16);
                    uint16_t ch = DetailImg100::RdU16LE(cdata + 18);
                    std::vector<uint8_t> rawCel;
                    const uint8_t* pixPtr = nullptr;
                    size_t pixLen = 0;
                    if (celType == 0u) {
                        pixPtr = cdata + 20;
                        pixLen = clen - 20;
                    } else if (celType == 2u && clen > 22) {
                        if (ZipDeflate::InflateZlib(cdata + 20, clen - 20, rawCel)) {
                            pixPtr = rawCel.data();
                            pixLen = rawCel.size();
                        }
                    }
                    if (pixPtr && pixLen >= static_cast<size_t>(cw) * ch * bpp) {
                        decodedAnyCel = true;
                        for (uint16_t py = 0; py < ch; ++py) {
                            int dy = celY + py;
                            if (dy < 0 || dy >= h) continue;
                            for (uint16_t px = 0; px < cw; ++px) {
                                int dx = celX + px;
                                if (dx < 0 || dx >= w) continue;
                                const uint8_t* sp = pixPtr + (static_cast<size_t>(py) * cw + px) * bpp;
                                uint8_t sr = 0, sg = 0, sb = 0, sa = 0;
                                samplePixelToRgba(sp, isBgLayer, sr, sg, sb, sa);
                                uint8_t pm[4];
                                WritePremulBGRA(pm, sr, sg, sb, sa);
                                BlendPremulBgraPixel(
                                    &outInfo.bgraPixels[(static_cast<size_t>(dy) * w + dx) * 4u],
                                    pm[0], pm[1], pm[2], pm[3], effOpacity, LayerBlendOp::SrcOver);
                            }
                        }
                    }
                } else if (celType == 3u && clen >= 48) { // Compressed Tilemap Cel (Aseprite v1.3+)
                    auto tsIt = tilesets.find(tsId);
                    if (tsIt == tilesets.end() && !tilesets.empty()) tsIt = tilesets.begin();
                    if (tsIt != tilesets.end()) {
                        const AseTileset& ts = tsIt->second;
                        uint16_t mapW        = DetailImg100::RdU16LE(cdata + 16);
                        uint16_t mapH        = DetailImg100::RdU16LE(cdata + 18);
                        uint16_t bitsPerTile = DetailImg100::RdU16LE(cdata + 20);
                        uint32_t tileIdMask  = DetailImg100::RdU32LE(cdata + 22);
                        uint32_t xFlipMask   = DetailImg100::RdU32LE(cdata + 26);
                        uint32_t yFlipMask   = DetailImg100::RdU32LE(cdata + 30);
                        uint32_t diagMask    = DetailImg100::RdU32LE(cdata + 34);
                        uint32_t bytesPerTile = (bitsPerTile == 8) ? 1u : (bitsPerTile == 16) ? 2u : 4u;
                        size_t needMapBytes = static_cast<size_t>(mapW) * mapH * bytesPerTile;

                        std::vector<uint8_t> mapData;
                        if (mapW > 0 && mapH > 0 && clen > 48 &&
                            ZipDeflate::InflateZlib(cdata + 48, clen - 48, mapData, needMapBytes + 64) &&
                            mapData.size() >= needMapBytes)
                        {
                            decodedAnyCel = true;
                            bool tileZeroIsEmpty = (ts.flags & 0x04u) == 0u;
                            size_t singleTileBytes = static_cast<size_t>(ts.tileW) * ts.tileH * bpp;

                            for (uint16_t my = 0; my < mapH; ++my) {
                                for (uint16_t mx = 0; mx < mapW; ++mx) {
                                    const uint8_t* mp = mapData.data() + (static_cast<size_t>(my) * mapW + mx) * bytesPerTile;
                                    uint32_t rawTile = (bytesPerTile == 1u) ? mp[0] :
                                                       (bytesPerTile == 2u) ? DetailImg100::RdU16LE(mp) :
                                                       DetailImg100::RdU32LE(mp);
                                    uint32_t tileIdx = (tileIdMask != 0u) ? (rawTile & tileIdMask) : rawTile;
                                    if (tileZeroIsEmpty) {
                                        if (tileIdx == 0u) continue;
                                        tileIdx -= 1u;
                                    }
                                    if (tileIdx >= ts.numTiles) continue;

                                    bool flipX = (xFlipMask != 0u) && ((rawTile & xFlipMask) != 0u);
                                    bool flipY = (yFlipMask != 0u) && ((rawTile & yFlipMask) != 0u);
                                    bool flipD = (diagMask  != 0u) && ((rawTile & diagMask)  != 0u);

                                    const uint8_t* tileBase = ts.pixels.data() + static_cast<size_t>(tileIdx) * singleTileBytes;
                                    int baseX = celX + static_cast<int>(mx) * ts.tileW;
                                    int baseY = celY + static_cast<int>(my) * ts.tileH;

                                    for (uint16_t ty = 0; ty < ts.tileH; ++ty) {
                                        int dy = baseY + ty;
                                        if (dy < 0 || dy >= h) continue;
                                        for (uint16_t tx = 0; tx < ts.tileW; ++tx) {
                                            int dx = baseX + tx;
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
                                            BlendPremulBgraPixel(
                                                &outInfo.bgraPixels[(static_cast<size_t>(dy) * w + dx) * 4u],
                                                pm[0], pm[1], pm[2], pm[3], effOpacity, LayerBlendOp::SrcOver);
                                        }
                                    }
                                }
                            }
                            outInfo.formatName = L"Aseprite v1.3 Tilemap Sprite (" + std::to_wstring(ts.numTiles) + L" tiles)";
                        }
                    }
                } else if (celType == 1u && clen >= 18) { // Linked Cel (reuses earlier frame cel; mark valid if non-empty)
                    decodedAnyCel = decodedAnyCel || false;
                }
            }
            cpos += chunkSize;
        }
        pos += frameBytes;
    }
    if (!decodedAnyCel) return false;
    outInfo.valid = true;
    return true;
}


} // namespace DetailArtFont

inline bool DecodeAseprite(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    return DetailArtFont::DecodeAsepriteUpgraded(data, size, outInfo);
}

} // namespace Formats
