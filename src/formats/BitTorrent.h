#pragma once
// BitTorrent.h — BitTorrent Bencoded Metainfo (.torrent) Tracker & File Manifest Parser

namespace Formats {

inline bool ParseBitTorrentDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 11 || raw[0] != 'd') return false;
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"BitTorrent Bencoded Metainfo (.torrent)";

    SpreadsheetSheet sheet;
    sheet.name = L"Torrent_Metainfo";
    sheet.rows.push_back({ L"Key / Field", L"Value" });

    size_t pos = 1;
    std::string lastKey;
    while (pos < sv.size() && sheet.rows.size() < 2000) {
        char c = sv[pos];
        if (c == 'e' || c == 'd' || c == 'l') {
            ++pos;
            continue;
        }
        if (c == 'i') {
            size_t ePos = sv.find('e', pos + 1);
            if (ePos == std::string_view::npos) break;
            std::string_view numSv = sv.substr(pos + 1, ePos - (pos + 1));
            if (!lastKey.empty()) {
                sheet.rows.push_back({ PathUtils::Utf8ToWide(lastKey), PathUtils::Utf8ToWide(numSv) });
                lastKey.clear();
            }
            pos = ePos + 1;
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            size_t colon = sv.find(':', pos);
            if (colon == std::string_view::npos) break;
            size_t len = static_cast<size_t>(std::strtoul(std::string(sv.substr(pos, colon - pos)).c_str(), nullptr, 10));
            pos = colon + 1;
            if (pos + len > sv.size()) break;
            std::string_view strSv = sv.substr(pos, len);
            pos += len;

            if (lastKey == "pieces") {
                sheet.rows.push_back({ L"pieces (SHA-1 hashes)", std::to_wstring(len / 20) + L" pieces (" + std::to_wstring(len) + L" B)" });
                lastKey.clear();
            } else if (lastKey.empty()) {
                lastKey.assign(strSv.data(), strSv.size());
            } else {
                sheet.rows.push_back({ PathUtils::Utf8ToWide(lastKey), PathUtils::Utf8ToWide(strSv) });
                lastKey.clear();
            }
        } else {
            ++pos;
        }
    }

    sheet.maxCols = 2;
    out.valid = (sheet.rows.size() >= 2);
    out.sheets.push_back(std::move(sheet));
    return out.valid;
}

} // namespace Formats
