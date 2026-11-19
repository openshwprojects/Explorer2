#pragma once
// VisioVsdx.h — Microsoft Visio Drawing Package (.vsdx)

namespace Formats {

inline bool ParseVisioVsdxDoc(const std::vector<uint8_t>& raw, const std::wstring& ext, ParsedOfficeDocument& out) {
    // #100a. Microsoft Visio Drawing Package (.vsdx — OPC ZIP + Visio Page XML)
    if (ext == L".vsdx" && raw.size() >= 4 && raw[0] == 'P' && raw[1] == 'K') {
        std::vector<ZipDeflate::ArchiveItemMeta> entries;
        if (!ZipDeflate::ListZipEntriesFromMemory(raw.data(), raw.size(), entries)) return false;

        bool hasVisioPart = false;
        SpreadsheetSheet sh;
        sh.name = L"Visio_Shapes";
        sh.maxCols = 3;
        sh.rows.push_back({ L"Part / Page", L"Shape / Component", L"Text / Size" });

        for (const auto& e : entries) {
            std::wstring lp = PathUtils::ToLower(e.pathInArchive);
            std::replace(lp.begin(), lp.end(), L'\\', L'/');
            if (lp.find(L"visio/") != std::wstring::npos) hasVisioPart = true;
            if (lp.find(L"visio/pages/page") != std::wstring::npos && PathUtils::GetExtension(lp) == L".xml") {
                std::vector<uint8_t> pgBytes;
                if (ZipDeflate::ExtractZipEntryFromMemory(raw.data(), raw.size(), e, pgBytes) && !pgBytes.empty()) {
                    std::string_view psv(reinterpret_cast<const char*>(pgBytes.data()), pgBytes.size());
                    size_t tp = 0;
                    int sIdx = 0;
                    while ((tp = psv.find("<Text", tp)) != std::string_view::npos && sh.rows.size() < 300) {
                        size_t gt = psv.find('>', tp);
                        size_t te = (gt != std::string_view::npos) ? psv.find("</Text>", gt + 1) : std::string_view::npos;
                        if (te == std::string_view::npos) break;
                        std::string rawTxt(psv.substr(gt + 1, te - gt - 1));
                        std::string clean;
                        bool inTag = false;
                        for (char c : rawTxt) {
                            if (c == '<') inTag = true;
                            else if (c == '>') inTag = false;
                            else if (!inTag) clean.push_back(c);
                        }
                        std::wstring wtxt = PathUtils::Trim(PathUtils::Utf8ToWide(clean));
                        if (!wtxt.empty()) {
                            sh.rows.push_back({ e.name, L"Shape #" + std::to_wstring(++sIdx), wtxt });
                        }
                        tp = te + 7;
                    }
                }
            }
        }
        if (!hasVisioPart) return false;
        if (sh.rows.size() == 1) {
            for (const auto& e : entries) {
                if (!e.isDirectory && sh.rows.size() < 200) {
                    sh.rows.push_back({ e.pathInArchive, L"Visio Part", PathUtils::FormatByteSize(e.uncompressedSize) });
                }
            }
        }
        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"Microsoft Visio Drawing Package (.vsdx)";
        out.sheets.push_back(std::move(sh));
        out.valid = true;
        return true;
    }

    return false;
}

} // namespace Formats
