#pragma once
// OpenStreetMap.h — OpenStreetMap XML (.osm) Nodes, Ways & Tags Table Parser

namespace Formats {

inline bool ParseOpenStreetMapDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    if (sv.find("<osm") == std::string_view::npos) return false;

    out.kind = OfficeFileKind::CsvTable;
    out.isSpreadsheet = true;
    out.formatTitle = L"OpenStreetMap Vector Data (.osm)";

    SpreadsheetSheet sheet;
    sheet.name = L"OSM_Elements";
    sheet.rows.push_back({ L"Element", L"ID", L"Latitude", L"Longitude", L"Tags" });

    auto getAttr = [](std::string_view tag, const char* attr) -> std::wstring {
        size_t p = tag.find(attr);
        if (p == std::string_view::npos) return L"";
        p += std::strlen(attr);
        size_t q = tag.find('"', p);
        if (q == std::string_view::npos) return L"";
        return PathUtils::Utf8ToWide(tag.substr(p, q - p));
    };

    size_t pos = 0;
    while (pos < sv.size() && sheet.rows.size() < 5000) {
        size_t np = sv.find("<node ", pos);
        size_t wp = sv.find("<way ", pos);
        size_t next = std::min(np, wp);
        if (next == std::string_view::npos) break;
        bool isNode = (next == np);
        size_t gt = sv.find('>', next);
        if (gt == std::string_view::npos) break;

        std::string_view openTag = sv.substr(next, gt - next);
        std::wstring id  = getAttr(openTag, "id=\"");
        std::wstring lat = isNode ? getAttr(openTag, "lat=\"") : L"-";
        std::wstring lon = isNode ? getAttr(openTag, "lon=\"") : L"-";
        std::wstring tags;

        if (gt > next && sv[gt - 1] != '/') {
            const char* closeStr = isNode ? "</node>" : "</way>";
            size_t endBlk = sv.find(closeStr, gt);
            if (endBlk != std::string_view::npos) {
                std::string_view inner = sv.substr(gt + 1, endBlk - (gt + 1));
                size_t tp = 0;
                while (tp < inner.size()) {
                    size_t t1 = inner.find("<tag ", tp);
                    if (t1 == std::string_view::npos) break;
                    size_t t2 = inner.find('>', t1);
                    if (t2 == std::string_view::npos) break;
                    std::string_view ttag = inner.substr(t1, t2 - t1);
                    std::wstring k = getAttr(ttag, "k=\"");
                    std::wstring v = getAttr(ttag, "v=\"");
                    if (!k.empty()) {
                        if (!tags.empty()) tags += L"; ";
                        tags += k + L"=" + v;
                    }
                    tp = t2 + 1;
                }
                pos = endBlk + std::strlen(closeStr);
            } else {
                pos = gt + 1;
            }
        } else {
            pos = gt + 1;
        }
        sheet.rows.push_back({ isNode ? L"node" : L"way", id, lat, lon, tags });
    }

    sheet.maxCols = 5;
    out.valid = (sheet.rows.size() >= 2);
    out.sheets.push_back(std::move(sheet));
    return out.valid;
}

} // namespace Formats
