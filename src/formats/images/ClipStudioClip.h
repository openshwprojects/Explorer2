#pragma once
// ClipStudioClip.h — Clip Studio Paint CSFCHUNK Canvas & Layer Decoder (.clip)

namespace Formats {

namespace DetailArtFont {

// ----------------------------------------------------------------------------
// 4. Clip Studio Paint (.clip) CSFCHUNK Chunk Walker + CHNKExta Tile Compositor
// ----------------------------------------------------------------------------
inline bool DecodeClipStudioUpgraded(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 24 || std::memcmp(data, "CSFCHUNK", 8) != 0) return false;

    struct ExtaBlock {
        const uint8_t* payload = nullptr;
        size_t len = 0;
    };
    std::vector<ExtaBlock> extaBlocks;
    uint32_t canvasW = 0, canvasH = 0;

    // Walk CSFCHUNK big-endian chunk chain (header is 24 bytes: "CSFCHUNK" + 8-byte fileSize + 8-byte firstOffset)
    size_t pos = 24;
    while (pos + 16 <= size) {
        if (std::memcmp(data + pos, "CHNK", 4) != 0) break;
        char subType[5] = {
            static_cast<char>(data[pos + 4]), static_cast<char>(data[pos + 5]),
            static_cast<char>(data[pos + 6]), static_cast<char>(data[pos + 7]), 0
        };
        uint64_t ckLen = DetailImg100::RdU64BE(data + pos + 8);
        pos += 16;
        if (ckLen > size - pos) break;
        const uint8_t* ckData = data + pos;
        size_t len = static_cast<size_t>(ckLen);

        if (std::strcmp(subType, "Head") == 0 && len >= 16) {
            uint32_t cw = DetailImg100::RdU32BE(ckData + 4);
            uint32_t ch = DetailImg100::RdU32BE(ckData + 8);
            if (cw > 0 && cw <= 8192 && ch > 0 && ch <= 8192) {
                canvasW = cw;
                canvasH = ch;
            }
        } else if (std::strcmp(subType, "Exta") == 0 && len >= 12) {
            extaBlocks.push_back({ ckData, len });
        } else if (std::strcmp(subType, "Foot") == 0) {
            break;
        }
        pos += len;
    }

    // 1. If CHNKExta external raster blocks exist and encode raw/zlib BGRA/RGBA canvas or layer tiles, composite them!
    if (!extaBlocks.empty()) {
        uint32_t w = (canvasW > 0) ? canvasW : 0;
        uint32_t h = (canvasH > 0) ? canvasH : 0;
        std::vector<uint8_t> canvas;
        size_t appliedLayers = 0;

        for (const auto& eb : extaBlocks) {
            // Support CHNKExta external raster payload:
            // Either [uint32BE width, uint32BE height, zlib/raw RGBA] or [nameLenBE32 + name + uint32BE w, uint32BE h + zlib]
            size_t p = 0;
            if (eb.len >= 16) {
                uint32_t nLen = DetailImg100::RdU32BE(eb.payload);
                if (nLen > 0 && nLen <= 128 && 4u + nLen + 8u <= eb.len) {
                    bool allPrint = true;
                    for (uint32_t k = 0; k < nLen; ++k) {
                        if (eb.payload[4 + k] < 0x20 || eb.payload[4 + k] > 0x7E) { allPrint = false; break; }
                    }
                    if (allPrint) {
                        std::string extId(reinterpret_cast<const char*>(eb.payload + 4), nLen);
                        if (outInfo.layerNames.size() < 64) outInfo.layerNames.push_back(PathUtils::Utf8ToWide(extId));
                        p = 4u + nLen;
                    }
                }
            }
            if (p + 8u <= eb.len) {
                uint32_t bw = DetailImg100::RdU32BE(eb.payload + p);
                uint32_t bh = DetailImg100::RdU32BE(eb.payload + p + 4);
                if (bw > 0 && bw <= 4096 && bh > 0 && bh <= 4096) {
                    std::vector<uint8_t> rawPx;
                    const uint8_t* src = eb.payload + p + 8u;
                    size_t rem = eb.len - (p + 8u);
                    size_t need = static_cast<size_t>(bw) * bh * 4u;
                    if (!ZipDeflate::InflateZlib(src, rem, rawPx, need + 64) && rem >= need) {
                        rawPx.assign(src, src + need);
                    }
                    if (rawPx.size() >= need) {
                        if (w == 0 || h == 0) { w = bw; h = bh; }
                        if (canvas.empty()) canvas.assign(static_cast<size_t>(w) * h * 4u, 0u);
                        uint32_t limY = std::min(h, bh), limX = std::min(w, bw);
                        for (uint32_t y = 0; y < limY; ++y) {
                            for (uint32_t x = 0; x < limX; ++x) {
                                const uint8_t* sp = &rawPx[(static_cast<size_t>(y) * bw + x) * 4u];
                                uint8_t pm[4];
                                WritePremulBGRA(pm, sp[0], sp[1], sp[2], sp[3]);
                                BlendPremulBgraPixel(
                                    &canvas[(static_cast<size_t>(y) * w + x) * 4u],
                                    pm[0], pm[1], pm[2], pm[3], 1.0f, LayerBlendOp::SrcOver);
                            }
                        }
                        ++appliedLayers;
                    }
                }
            }
        }
        if (appliedLayers > 0 && w > 0 && h > 0) {
            outInfo.width = w;
            outInfo.height = h;
            outInfo.origWidth = w;
            outInfo.origHeight = h;
            outInfo.channels = 4u;
            outInfo.bitsPerChannel = 8u;
            outInfo.formatName = L"Clip Studio Paint Layered Canvas (.clip)";
            outInfo.bgraPixels = std::move(canvas);
            outInfo.valid = true;
            return true;
        }
    }

    // 2. Fallback: scan SQLite CHNKSQLi / embedded CanvasPreview PNG
    static const uint8_t kPngSig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    static const uint8_t kIend[8]   = { 'I', 'E', 'N', 'D', 0xAE, 0x42, 0x60, 0x82 };
    for (size_t i = 8; i + 32 < size; ++i) {
        if (std::memcmp(data + i, kPngSig, 8) == 0) {
            size_t pngEnd = size - i;
            for (size_t j = i + 16; j + 8 <= size; ++j) {
                if (std::memcmp(data + j, kIend, 8) == 0) {
                    pngEnd = (j + 8) - i;
                    break;
                }
            }
            if (DetailImg100::DecodeEmbeddedWicBuffer(data + i, pngEnd, L"Clip Studio Paint Canvas (.clip)", outInfo)) {
                return true;
            }
        }
    }
    return false;
}


} // namespace DetailArtFont

inline bool DecodeClipStudio(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    return DetailArtFont::DecodeClipStudioUpgraded(data, size, outInfo);
}

} // namespace Formats
