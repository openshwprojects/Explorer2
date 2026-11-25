#pragma once
// KritaKra.h — Krita Native VERSION 2 Tile-Store & Document Compositor (.kra)

namespace Formats {

namespace DetailArtFont {

// ----------------------------------------------------------------------------
// 2. Krita (.kra) Native VERSION 2 Tile-Engine Decompressor & Layer Compositor
// ----------------------------------------------------------------------------
inline bool DecodeKritaTileLayer(
    const uint8_t* tileStream,
    size_t streamLen,
    int layerOffX,
    int layerOffY,
    float opacity,
    LayerBlendOp blendOp,
    uint8_t* canvasBgra,
    int imgW,
    int imgH)
{
    if (!tileStream || streamLen < 32) return false;
    size_t pos = 0;
    auto readLine = [&](std::string_view& outLine) -> bool {
        if (pos >= streamLen) return false;
        size_t nl = pos;
        while (nl < streamLen && tileStream[nl] != '\n') ++nl;
        if (nl >= streamLen) return false;
        outLine = TrimAsciiView(std::string_view(reinterpret_cast<const char*>(tileStream + pos), nl - pos));
        pos = nl + 1;
        return true;
    };

    int version = 2, tileW = 64, tileH = 64, pixelSize = 4, numTiles = -1;
    for (int hdrIdx = 0; hdrIdx < 8; ++hdrIdx) {
        std::string_view ln;
        if (!readLine(ln)) return false;
        if (ln.rfind("VERSION ", 0) == 0)         version   = ParseIntOr(std::string(ln.substr(8)), 2);
        else if (ln.rfind("TILEWIDTH ", 0) == 0)  tileW     = ParseIntOr(std::string(ln.substr(10)), 64);
        else if (ln.rfind("TILEHEIGHT ", 0) == 0) tileH     = ParseIntOr(std::string(ln.substr(11)), 64);
        else if (ln.rfind("PIXELSIZE ", 0) == 0)  pixelSize = ParseIntOr(std::string(ln.substr(10)), 4);
        else if (ln.rfind("DATA ", 0) == 0) {
            numTiles = ParseIntOr(std::string(ln.substr(5)), -1);
            break;
        }
    }
    (void)version;
    if (numTiles < 0 || tileW <= 0 || tileW > 512 || tileH <= 0 || tileH > 512 ||
        (pixelSize != 4 && pixelSize != 2 && pixelSize != 1 && pixelSize != 8)) {
        return false;
    }

    const size_t tilePixels = static_cast<size_t>(tileW) * tileH;
    const size_t rawTileBytes = tilePixels * static_cast<size_t>(pixelSize);
    std::vector<uint8_t> decodedTile(rawTileBytes);
    std::vector<uint8_t> linearTile(rawTileBytes);
    size_t tilesBlitted = 0;

    for (int t = 0; t < numTiles && pos < streamLen; ++t) {
        std::string_view tHdr;
        if (!readLine(tHdr)) break;
        // Format: col,row,LZF,compLen  or  col,row,NONE,rawLen
        size_t c1 = tHdr.find(',');
        if (c1 == std::string_view::npos) return false;
        size_t c2 = tHdr.find(',', c1 + 1);
        if (c2 == std::string_view::npos) return false;
        size_t c3 = tHdr.find(',', c2 + 1);
        if (c3 == std::string_view::npos) return false;

        int tileCol = ParseIntOr(std::string(tHdr.substr(0, c1)), 0);
        int tileRow = ParseIntOr(std::string(tHdr.substr(c1 + 1, c2 - c1 - 1)), 0);
        std::string_view codec = tHdr.substr(c2 + 1, c3 - c2 - 1);
        int byteCount = ParseIntOr(std::string(tHdr.substr(c3 + 1)), -1);
        if (byteCount <= 0 || static_cast<size_t>(byteCount) > streamLen - pos) return false;

        const uint8_t* payload = tileStream + pos;
        pos += static_cast<size_t>(byteCount);

        const uint8_t* pxData = nullptr;
        if (codec == "NONE") {
            if (static_cast<size_t>(byteCount) < rawTileBytes) return false;
            pxData = payload;
        } else if (codec == "LZF") {
            uint8_t compFlag = payload[0];
            if (compFlag == 0u) {
                if (static_cast<size_t>(byteCount - 1) < rawTileBytes) return false;
                pxData = payload + 1;
            } else if (compFlag == 1u) {
                if (!::Explorer2::StreamCodecs::DecompressLibLzf(
                        payload + 1, static_cast<size_t>(byteCount - 1), decodedTile, rawTileBytes, rawTileBytes)) {
                    return false;
                }
                // Krita KisTileCompressor2 delinearize: planar [ch0_all, ch1_all, ...] -> interleaved
                for (size_t pi = 0; pi < tilePixels; ++pi) {
                    for (int ch = 0; ch < pixelSize; ++ch) {
                        linearTile[pi * pixelSize + ch] = decodedTile[static_cast<size_t>(ch) * tilePixels + pi];
                    }
                }
                pxData = linearTile.data();
            } else {
                return false;
            }
        } else {
            return false;
        }

        // Krita's default 8-bit "RGBA" color space stores bytes in BGRA order in memory!
        int baseX = tileCol * tileW + layerOffX;
        int baseY = tileRow * tileH + layerOffY;
        for (int ty = 0; ty < tileH; ++ty) {
            int dy = baseY + ty;
            if (dy < 0 || dy >= imgH) continue;
            for (int tx = 0; tx < tileW; ++tx) {
                int dx = baseX + tx;
                if (dx < 0 || dx >= imgW) continue;
                const uint8_t* sp = pxData + (static_cast<size_t>(ty) * tileW + tx) * pixelSize;
                uint8_t b = 0, g = 0, r = 0, a = 255;
                if (pixelSize == 4) {
                    b = sp[0]; g = sp[1]; r = sp[2]; a = sp[3];
                } else if (pixelSize == 8) {
                    b = sp[1]; g = sp[3]; r = sp[5]; a = sp[7];
                } else if (pixelSize == 2) {
                    b = g = r = sp[0]; a = sp[1];
                } else {
                    b = g = r = sp[0]; a = 255;
                }
                uint8_t pm[4];
                WritePremulBGRA(pm, r, g, b, a);
                BlendPremulBgraPixel(
                    &canvasBgra[(static_cast<size_t>(dy) * imgW + dx) * 4u],
                    pm[0], pm[1], pm[2], pm[3], opacity, blendOp);
            }
        }
        ++tilesBlitted;
    }
    return tilesBlitted > 0 || numTiles == 0;
}

inline bool CompositeKritaFromMainDoc(
    const uint8_t* zipData,
    size_t zipSize,
    const std::vector<ZipDeflate::ArchiveItemMeta>& entries,
    std::string_view xml,
    ExtendedImageInfo& outInfo)
{
    int imgW = 0, imgH = 0;
    std::wstring docName;
    struct KraLayer {
        std::wstring name;
        std::wstring filename;
        int x = 0, y = 0;
        float opacity = 1.0f;
        bool visible = true;
        LayerBlendOp op = LayerBlendOp::SrcOver;
    };
    std::vector<KraLayer> layersTopToBottom;

    size_t pos = 0;
    while (pos < xml.size()) {
        size_t lt = xml.find('<', pos);
        if (lt == std::string_view::npos) break;
        size_t gt = xml.find('>', lt + 1);
        if (gt == std::string_view::npos) break;
        std::string_view tag = TrimAsciiView(xml.substr(lt + 1, gt - lt - 1));
        pos = gt + 1;
        if (tag.empty() || tag.front() == '/' || tag.front() == '?' || tag.front() == '!') continue;
        if (!tag.empty() && tag.back() == '/') tag = TrimAsciiView(tag.substr(0, tag.size() - 1));

        size_t sp = 0;
        while (sp < tag.size() && tag[sp] != ' ' && tag[sp] != '\t' && tag[sp] != '\r' && tag[sp] != '\n') ++sp;
        std::string_view elem = tag.substr(0, sp);
        std::string_view attrs = tag.substr(sp);

        if (elem == "IMAGE") {
            imgW = ParseIntOr(GetXmlAttr(attrs, "width"), 0);
            imgH = ParseIntOr(GetXmlAttr(attrs, "height"), 0);
            docName = PathUtils::Utf8ToWide(GetXmlAttr(attrs, "name"));
        } else if (elem == "layer") {
            std::string nodeType = GetXmlAttr(attrs, "nodetype");
            std::string fn = GetXmlAttr(attrs, "filename");
            std::wstring nm = PathUtils::Utf8ToWide(GetXmlAttr(attrs, "name"));
            if (!nm.empty() && outInfo.layerNames.size() < 64) outInfo.layerNames.push_back(nm);
            if (!fn.empty() && (nodeType.empty() || nodeType == "paintlayer")) {
                KraLayer kl;
                kl.name = nm;
                kl.filename = PathUtils::Utf8ToWide(fn);
                kl.x = ParseIntOr(GetXmlAttr(attrs, "x"), 0);
                kl.y = ParseIntOr(GetXmlAttr(attrs, "y"), 0);
                kl.opacity = std::clamp(ParseFloatOr(GetXmlAttr(attrs, "opacity"), 255.0f) / 255.0f, 0.0f, 1.0f);
                kl.visible = (ParseIntOr(GetXmlAttr(attrs, "visible"), 1) != 0);
                kl.op = ParseBlendOp(GetXmlAttr(attrs, "compositeop"));
                layersTopToBottom.push_back(std::move(kl));
            }
        }
    }

    if (imgW <= 0 || imgH <= 0 || imgW > 8192 || imgH > 8192 || layersTopToBottom.empty()) {
        return false;
    }

    std::vector<uint8_t> canvas(static_cast<size_t>(imgW) * imgH * 4u, 0u);
    size_t decodedCount = 0;

    // Krita maindoc.xml lists layers top-to-bottom; composite bottom-to-top
    for (auto it = layersTopToBottom.rbegin(); it != layersTopToBottom.rend(); ++it) {
        if (!it->visible || it->opacity <= 0.0f) continue;
        const ZipDeflate::ArchiveItemMeta* tileEnt = nullptr;
        if (!docName.empty()) {
            tileEnt = FindZipEntryCaseInsensitive(entries, docName + L"/layers/" + it->filename);
        }
        if (!tileEnt) tileEnt = FindZipEntryCaseInsensitive(entries, L"layers/" + it->filename);
        if (!tileEnt) {
            std::wstring suffix = L"/layers/" + PathUtils::ToLower(it->filename);
            for (const auto& e : entries) {
                std::wstring nl = PathUtils::ToLower(e.pathInArchive);
                for (wchar_t& ch : nl) if (ch == L'\\') ch = L'/';
                if (nl.size() >= suffix.size() && nl.substr(nl.size() - suffix.size()) == suffix) {
                    tileEnt = &e;
                    break;
                }
            }
        }
        if (!tileEnt) continue;
        std::vector<uint8_t> tileBytes;
        if (!ZipDeflate::ExtractZipEntryFromMemory(zipData, zipSize, *tileEnt, tileBytes) || tileBytes.empty()) continue;
        if (DecodeKritaTileLayer(tileBytes.data(), tileBytes.size(), it->x, it->y, it->opacity, it->op,
                                 canvas.data(), imgW, imgH)) {
            ++decodedCount;
        }
    }

    if (decodedCount == 0) return false;

    outInfo.width = static_cast<uint32_t>(imgW);
    outInfo.height = static_cast<uint32_t>(imgH);
    outInfo.origWidth = outInfo.width;
    outInfo.origHeight = outInfo.height;
    outInfo.channels = 4u;
    outInfo.bitsPerChannel = 8u;
    outInfo.formatName = L"Krita Tile-Composited Document (.kra, " + std::to_wstring(layersTopToBottom.size()) + L" layers)";
    outInfo.bgraPixels = std::move(canvas);
    outInfo.valid = true;
    return true;
}


} // namespace DetailArtFont

inline bool DecodeKritaKra(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 30 || data[0] != 'P' || data[1] != 'K') return false;
    std::vector<ZipDeflate::ArchiveItemMeta> entries;
    if (!ZipDeflate::ListZipEntriesFromMemory(data, size, entries) || entries.empty()) return false;
    const ZipDeflate::ArchiveItemMeta* mainDocEnt = DetailArtFont::FindZipEntryCaseInsensitive(entries, L"maindoc.xml");
    if (mainDocEnt) {
        std::vector<uint8_t> xmlBytes;
        if (ZipDeflate::ExtractZipEntryFromMemory(data, size, *mainDocEnt, xmlBytes) && !xmlBytes.empty()) {
            std::string_view sv(reinterpret_cast<const char*>(xmlBytes.data()), xmlBytes.size());
            if (DetailArtFont::CompositeKritaFromMainDoc(data, size, entries, sv, outInfo)) {
                return true;
            }
        }
    }
    return DetailArtFont::DecodeZipArtworkFallback(data, size, entries, L"Krita Document (.kra)", outInfo);
}

} // namespace Formats
