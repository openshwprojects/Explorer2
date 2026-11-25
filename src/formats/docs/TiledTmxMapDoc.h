#pragma once
// TiledTmxMapDoc.h — Tiled Map Editor XML Tilemap (.tmx)
// Uses shared `XmlTagScan`, `Base64`, and `ZipDeflate` to inspect `<map>`, `<tileset>`, `<layer>`
// (CSV & base64/zlib/gzip GID tile counts), and `<objectgroup>`/`<object>` entities.

namespace Formats {

namespace DetailTiledTmx {

inline std::string AttrCI(std::string_view attrs, std::string_view key) {
    std::string_view v;
    if (!XmlTagScan::FindAttr(attrs, key, v, true, true)) return {};
    return XmlTagScan::DecodeText(v);
}

inline size_t CountNonZeroGids(
    std::string_view payload,
    const std::string& encoding,
    const std::string& compression)
{
    const std::string encLow  = ReaderText::ToLowerAscii(encoding);
    const std::string compLow = ReaderText::ToLowerAscii(compression);

    if (encLow == "csv") {
        size_t nonZero = 0u;
        size_t p = 0u;
        while (p < payload.size()) {
            while (p < payload.size() && (payload[p] <= ' ' || payload[p] == ',')) ++p;
            const size_t s = p;
            while (p < payload.size() && std::isdigit(static_cast<unsigned char>(payload[p]))) ++p;
            if (p > s) {
                uint64_t gid = 0ULL;
                if (ReaderText::ParseUint64Sv(payload.substr(s, p - s), gid) && (gid & 0x1FFFFFFFULL) != 0ULL) {
                    ++nonZero;
                }
            } else {
                ++p;
            }
        }
        return nonZero;
    }

    if (encLow == "base64") {
        std::vector<uint8_t> rawB64;
        if (!Base64::Decode(payload, rawB64, Base64::Mode::Lenient, 16u << 20)) return 0u;
        std::vector<uint8_t> decoded;
        const uint8_t* gidsPtr = rawB64.data();
        size_t gidsBytes = rawB64.size();

        if (compLow == "zlib") {
            if (!ZipDeflate::InflateZlib(rawB64.data(), rawB64.size(), decoded, 32u << 20)) return 0u;
            gidsPtr = decoded.data();
            gidsBytes = decoded.size();
        } else if (compLow == "gzip") {
            if (!ZipDeflate::InflateGzip(rawB64.data(), rawB64.size(), decoded, 32u << 20)) return 0u;
            gidsPtr = decoded.data();
            gidsBytes = decoded.size();
        }

        size_t nonZero = 0u;
        for (size_t i = 0u; i + 4u <= gidsBytes; i += 4u) {
            if ((ByteIO::RdU32LE(gidsPtr + i) & 0x1FFFFFFFu) != 0u) ++nonZero;
        }
        return nonZero;
    }

    return 0u;
}

} // namespace DetailTiledTmx

inline bool ParseTiledTmxMapDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& outDoc) {
    std::string_view sv;
    if (!FormatPrologue::PrepareUtf8TextNoNul(raw.data(), raw.size(), sv, 28u, (32u << 20))) return false;

    std::vector<std::string> tagStack;
    bool sawMapRoot    = false;
    bool closedMapRoot = false;
    std::string mapOrient, mapW, mapH, tileW, tileH;

    SpreadsheetSheet layerSheet;
    layerSheet.name = L"Layers_Tilesets";
    layerSheet.rows.push_back({ L"Kind", L"Name", L"Dimensions / FirstGID", L"Encoding", L"FilledTiles / Source" });

    SpreadsheetSheet objSheet;
    objSheet.name = L"Objects";
    objSheet.rows.push_back({ L"Group", L"ID", L"Name", L"Class / Type", L"Position_Size" });

    std::string curLayerName, curLayerW, curLayerH, curDataEnc, curDataComp;
    size_t dataBodyStart = 0u;
    bool inLayerData = false;
    std::string curObjGroup;

    size_t pos = 0u;
    XmlTagScan::XmlTag tag{};
    while (XmlTagScan::NextTag(sv, pos, tag)) {
        const std::string lowName = ReaderText::ToLowerAscii(tag.localName);

        if (tag.isClosing) {
            if (tagStack.empty() || tagStack.back() != lowName) return false;
            if (lowName == "data" && inLayerData) {
                std::string_view body = sv.substr(dataBodyStart, tag.tagStart - dataBodyStart);
                const size_t filled = DetailTiledTmx::CountNonZeroGids(body, curDataEnc, curDataComp);
                std::string encDesc = curDataEnc.empty() ? "xml" : curDataEnc;
                if (!curDataComp.empty()) encDesc += "+" + curDataComp;
                if (layerSheet.rows.size() < 4096u) {
                    layerSheet.rows.push_back({
                        L"TileLayer",
                        PathUtils::Utf8ToWide(curLayerName),
                        PathUtils::Utf8ToWide(curLayerW + "x" + curLayerH),
                        PathUtils::Utf8ToWide(encDesc),
                        std::to_wstring(filled)
                    });
                }
                inLayerData = false;
            } else if (lowName == "objectgroup") {
                curObjGroup.clear();
            }

            tagStack.pop_back();
            if (lowName == "map" && tagStack.empty()) closedMapRoot = true;
            continue;
        }

        if (tagStack.empty()) {
            if (lowName != "map") return false;
            sawMapRoot = true;
            mapOrient = DetailTiledTmx::AttrCI(tag.attrs, "orientation");
            mapW      = DetailTiledTmx::AttrCI(tag.attrs, "width");
            mapH      = DetailTiledTmx::AttrCI(tag.attrs, "height");
            tileW     = DetailTiledTmx::AttrCI(tag.attrs, "tilewidth");
            tileH     = DetailTiledTmx::AttrCI(tag.attrs, "tileheight");
            if (mapW.empty() || mapH.empty() || tileW.empty() || tileH.empty()) return false;
        } else if (lowName == "tileset" && layerSheet.rows.size() < 4096u) {
            std::string tsName = DetailTiledTmx::AttrCI(tag.attrs, "name");
            std::string fgid   = DetailTiledTmx::AttrCI(tag.attrs, "firstgid");
            std::string src    = DetailTiledTmx::AttrCI(tag.attrs, "source");
            std::string tcnt   = DetailTiledTmx::AttrCI(tag.attrs, "tilecount");
            layerSheet.rows.push_back({
                L"Tileset",
                PathUtils::Utf8ToWide(tsName.empty() ? src : tsName),
                L"firstgid=" + PathUtils::Utf8ToWide(fgid),
                L"tileset",
                PathUtils::Utf8ToWide(!src.empty() ? src : (tcnt + " tiles"))
            });
        } else if (lowName == "layer") {
            curLayerName = DetailTiledTmx::AttrCI(tag.attrs, "name");
            curLayerW    = DetailTiledTmx::AttrCI(tag.attrs, "width");
            curLayerH    = DetailTiledTmx::AttrCI(tag.attrs, "height");
        } else if (lowName == "data" && !tagStack.empty() && tagStack.back() == "layer" && !tag.isSelfClosing) {
            curDataEnc    = DetailTiledTmx::AttrCI(tag.attrs, "encoding");
            curDataComp   = DetailTiledTmx::AttrCI(tag.attrs, "compression");
            dataBodyStart = tag.afterTag;
            inLayerData   = true;
        } else if (lowName == "objectgroup") {
            curObjGroup = DetailTiledTmx::AttrCI(tag.attrs, "name");
        } else if (lowName == "object" && objSheet.rows.size() < 4096u) {
            std::string oid  = DetailTiledTmx::AttrCI(tag.attrs, "id");
            std::string onm  = DetailTiledTmx::AttrCI(tag.attrs, "name");
            std::string ocls = DetailTiledTmx::AttrCI(tag.attrs, "class");
            if (ocls.empty()) ocls = DetailTiledTmx::AttrCI(tag.attrs, "type");
            std::string ox   = DetailTiledTmx::AttrCI(tag.attrs, "x");
            std::string oy   = DetailTiledTmx::AttrCI(tag.attrs, "y");
            std::string ow   = DetailTiledTmx::AttrCI(tag.attrs, "width");
            std::string oh   = DetailTiledTmx::AttrCI(tag.attrs, "height");
            std::string geom = "(" + ox + "," + oy + ")";
            if (!ow.empty() && !oh.empty()) geom += " " + ow + "x" + oh;
            objSheet.rows.push_back({
                PathUtils::Utf8ToWide(curObjGroup),
                PathUtils::Utf8ToWide(oid),
                PathUtils::Utf8ToWide(onm),
                PathUtils::Utf8ToWide(ocls),
                PathUtils::Utf8ToWide(geom)
            });
        }

        if (!tag.isSelfClosing) {
            if (tagStack.size() >= 64u) return false;
            tagStack.push_back(lowName);
        } else if (lowName == "map" && tagStack.empty()) {
            closedMapRoot = true;
        }
    }

    if (!sawMapRoot || !closedMapRoot || !tagStack.empty()) return false;
    if (layerSheet.rows.size() <= 1u && objSheet.rows.size() <= 1u) return false;

    outDoc.sheets.clear();
    outDoc.paragraphs.clear();
    if (layerSheet.rows.size() > 1u) {
        FinalizeSheetCols(layerSheet);
        outDoc.sheets.push_back(std::move(layerSheet));
    }
    if (objSheet.rows.size() > 1u) {
        FinalizeSheetCols(objSheet);
        outDoc.sheets.push_back(std::move(objSheet));
    }

    std::wstring title = L"Tiled XML Tilemap (.tmx " +
                         PathUtils::Utf8ToWide(mapW + "x" + mapH + " @ " + tileW + "x" + tileH + "px");
    if (!mapOrient.empty()) title += L", " + PathUtils::Utf8ToWide(mapOrient);
    title += L")";
    outDoc.formatTitle = std::move(title);
    outDoc.kind = OfficeFileKind::CsvTable;
    outDoc.isSpreadsheet = true;
    outDoc.valid = true;
    return true;
}

} // namespace Formats
