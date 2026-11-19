#pragma once
// ProcreateImage.h — Procreate Document.archive & LZ4/Zlib Tile Compositor (.procreate)

namespace Formats {

namespace DetailArtFont {

// ----------------------------------------------------------------------------
// 3. Procreate (.procreate) Native .chunk LZ4 / Zlib RGBA Tile Compositor
// ----------------------------------------------------------------------------
inline bool DecompressRawLz4BlockSimple(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& out, size_t expectedMax) {
    out.clear();
    out.reserve(expectedMax);
    size_t p = 0;
    while (p < srcLen) {
        uint8_t token = src[p++];
        size_t litLen = token >> 4;
        if (litLen == 15) {
            uint8_t s = 0;
            do {
                if (p >= srcLen) return false;
                s = src[p++];
                litLen += s;
                if (litLen > expectedMax) return false;
            } while (s == 255);
        }
        if (p + litLen > srcLen || out.size() + litLen > expectedMax) return false;
        out.insert(out.end(), src + p, src + p + litLen);
        p += litLen;
        if (p == srcLen) break;
        if (p + 2 > srcLen) return false;
        uint16_t off = DetailImg100::RdU16LE(src + p);
        p += 2;
        if (off == 0 || off > out.size()) return false;
        size_t matchLen = (token & 0x0Fu) + 4u;
        if ((token & 0x0Fu) == 15u) {
            uint8_t s = 0;
            do {
                if (p >= srcLen) return false;
                s = src[p++];
                matchLen += s;
                if (matchLen > expectedMax) return false;
            } while (s == 255);
        }
        if (out.size() + matchLen > expectedMax) return false;
        for (size_t k = 0; k < matchLen; ++k) {
            out.push_back(out[out.size() - off]);
        }
    }
    return !out.empty();
}

inline bool CompositeProcreateChunks(
    const uint8_t* zipData,
    size_t zipSize,
    const std::vector<ZipDeflate::ArchiveItemMeta>& entries,
    ExtendedImageInfo& outInfo)
{
    // Scan Document.archive (ASCII or bplist00 strings) for "{W, H}" canvas size & tileSize if present
    int imgW = 0, imgH = 0, tileSize = 0;
    for (const auto& e : entries) {
        std::wstring nl = PathUtils::ToLower(e.pathInArchive);
        if (nl == L"document.archive") {
            std::vector<uint8_t> docBytes;
            if (ZipDeflate::ExtractZipEntryFromMemory(zipData, zipSize, e, docBytes) && docBytes.size() >= 8) {
                std::string_view sv(reinterpret_cast<const char*>(docBytes.data()), docBytes.size());
                // Find "{W, H}" pattern used by NSStringFromCGSize in Procreate SilicaDocument
                size_t brace = 0;
                while ((brace = sv.find('{', brace)) != std::string_view::npos) {
                    size_t endB = sv.find('}', brace + 1);
                    if (endB != std::string_view::npos && endB - brace < 32) {
                        std::string_view inner = sv.substr(brace + 1, endB - brace - 1);
                        size_t comma = inner.find(',');
                        if (comma != std::string_view::npos) {
                            int wCand = ParseIntOr(std::string(TrimAsciiView(inner.substr(0, comma))), 0);
                            int hCand = ParseIntOr(std::string(TrimAsciiView(inner.substr(comma + 1))), 0);
                            if (wCand > 0 && wCand <= 8192 && hCand > 0 && hCand <= 8192) {
                                imgW = wCand;
                                imgH = hCand;
                                break;
                            }
                        }
                    }
                    ++brace;
                }
            }
        }
    }

    struct ChunkRef {
        std::wstring layerId;
        int col = 0;
        int row = 0;
        const ZipDeflate::ArchiveItemMeta* meta = nullptr;
    };
    std::vector<ChunkRef> chunks;
    std::vector<std::wstring> layerOrder;
    int maxCol = 0, maxRow = 0;

    for (const auto& e : entries) {
        if (e.isDirectory) continue;
        std::wstring p = e.pathInArchive;
        for (wchar_t& ch : p) if (ch == L'\\') ch = L'/';
        std::wstring pl = PathUtils::ToLower(p);
        if (pl.size() <= 6 || pl.substr(pl.size() - 6) != L".chunk") continue;
        size_t slash = p.find(L'/');
        if (slash == std::wstring::npos) continue;
        std::wstring layerId = p.substr(0, slash);
        if (PathUtils::ToLower(layerId).find(L"composite") != std::wstring::npos) {
            // Composite layer can still be used, keep order
        }
        std::wstring leaf = p.substr(slash + 1, p.size() - slash - 1 - 6);
        size_t tilde = leaf.find(L'~');
        if (tilde == std::wstring::npos) continue;
        int col = _wtoi(leaf.substr(0, tilde).c_str());
        int row = _wtoi(leaf.substr(tilde + 1).c_str());
        if (col < 0 || col > 128 || row < 0 || row > 128) continue;
        maxCol = std::max(maxCol, col);
        maxRow = std::max(maxRow, row);
        if (std::find(layerOrder.begin(), layerOrder.end(), layerId) == layerOrder.end()) {
            layerOrder.push_back(layerId);
        }
        chunks.push_back({ layerId, col, row, &e });
    }
    if (chunks.empty()) return false;

    // Decode first chunk to determine tileSize (e.g. 256, 128, 64, 16, 8)
    auto unpackProcreateTile = [&](const ZipDeflate::ArchiveItemMeta& m, std::vector<uint8_t>& outRgba) -> int {
        std::vector<uint8_t> raw;
        if (!ZipDeflate::ExtractZipEntryFromMemory(zipData, zipSize, m, raw) || raw.empty()) return 0;
        if (!StreamCodecs::DecompressLz4Frame(raw.data(), raw.size(), outRgba, 4 * 1024 * 1024)) {
            if (!ZipDeflate::InflateZlib(raw.data(), raw.size(), outRgba, 4 * 1024 * 1024)) {
                if (!DecompressRawLz4BlockSimple(raw.data(), raw.size(), outRgba, 4 * 1024 * 1024)) {
                    outRgba = std::move(raw);
                }
            }
        }
        if ((outRgba.size() & 3u) != 0u || outRgba.size() < 16u) return 0;
        size_t px = outRgba.size() / 4u;
        int side = static_cast<int>(std::llround(std::sqrt(static_cast<double>(px))));
        return (side > 0 && static_cast<size_t>(side) * side == px) ? side : 0;
    };

    std::vector<uint8_t> probeRgba;
    tileSize = unpackProcreateTile(*chunks[0].meta, probeRgba);
    if (tileSize <= 0 || tileSize > 1024) return false;

    if (imgW <= 0 || imgH <= 0) {
        imgW = (maxCol + 1) * tileSize;
        imgH = (maxRow + 1) * tileSize;
    }
    if (imgW <= 0 || imgH <= 0 || imgW > 8192 || imgH > 8192) return false;

    std::vector<uint8_t> canvas(static_cast<size_t>(imgW) * imgH * 4u, 0u);
    size_t blitted = 0;

    for (const auto& lid : layerOrder) {
        if (outInfo.layerNames.size() < 64) outInfo.layerNames.push_back(lid);
        for (const auto& ck : chunks) {
            if (ck.layerId != lid) continue;
            std::vector<uint8_t> tileRgba;
            int ts = unpackProcreateTile(*ck.meta, tileRgba);
            if (ts != tileSize) continue;
            ++blitted;
            int baseX = ck.col * tileSize;
            int baseY = ck.row * tileSize;
            for (int ty = 0; ty < tileSize; ++ty) {
                int dy = baseY + ty;
                if (dy < 0 || dy >= imgH) continue;
                for (int tx = 0; tx < tileSize; ++tx) {
                    int dx = baseX + tx;
                    if (dx < 0 || dx >= imgW) continue;
                    const uint8_t* sp = &tileRgba[(static_cast<size_t>(ty) * tileSize + tx) * 4u];
                    uint8_t pm[4];
                    WritePremulBGRA(pm, sp[0], sp[1], sp[2], sp[3]);
                    BlendPremulBgraPixel(
                        &canvas[(static_cast<size_t>(dy) * imgW + dx) * 4u],
                        pm[0], pm[1], pm[2], pm[3], 1.0f, LayerBlendOp::SrcOver);
                }
            }
        }
    }
    if (blitted == 0) return false;

    outInfo.width = static_cast<uint32_t>(imgW);
    outInfo.height = static_cast<uint32_t>(imgH);
    outInfo.origWidth = outInfo.width;
    outInfo.origHeight = outInfo.height;
    outInfo.channels = 4u;
    outInfo.bitsPerChannel = 8u;
    outInfo.formatName = L"Procreate Silica Tile Artwork (.procreate, " + std::to_wstring(layerOrder.size()) + L" layers)";
    outInfo.bgraPixels = std::move(canvas);
    outInfo.valid = true;
    return true;
}


} // namespace DetailArtFont

inline bool DecodeProcreateImage(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 30 || data[0] != 'P' || data[1] != 'K') return false;
    std::vector<ZipDeflate::ArchiveItemMeta> entries;
    if (!ZipDeflate::ListZipEntriesFromMemory(data, size, entries) || entries.empty()) return false;
    if (DetailArtFont::CompositeProcreateChunks(data, size, entries, outInfo)) {
        return true;
    }
    return DetailArtFont::DecodeZipArtworkFallback(data, size, entries, L"Procreate Artwork (.procreate)", outInfo);
}

} // namespace Formats
