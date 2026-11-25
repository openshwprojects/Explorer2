#pragma once
// VobSubIdx.h — DVD VobSub Subtitle Index (.idx) Reader

namespace Formats {
namespace DetailMediaAndScoreDoc {

inline bool ParseVobSubIdx(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    if (sv.find("VobSub index file") == std::string_view::npos &&
        (sv.find("timestamp:") == std::string_view::npos || sv.find("id:") == std::string_view::npos))
    {
        return false;
    }

    std::wstring resSize, langIdx, palette;
    std::wstring curLang = L"und";
    struct IdxCue { std::wstring lang, ts, filepos; };
    std::vector<IdxCue> cues;

    size_t ls = 0;
    while (ls < sv.size()) {
        size_t le = sv.find('\n', ls);
        if (le == std::string_view::npos) le = sv.size();
        std::string_view ln = sv.substr(ls, le - ls);
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.remove_suffix(1);
        while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
        ls = (le < sv.size()) ? (le + 1) : sv.size();
        if (ln.empty() || ln[0] == '#') continue;

        std::string low = PathUtils::ToLowerAscii(ln);
        if (low.rfind("size:", 0) == 0) resSize = PathUtils::Trim(PathUtils::Utf8ToWide(ln.substr(5)));
        else if (low.rfind("langidx:", 0) == 0) langIdx = PathUtils::Trim(PathUtils::Utf8ToWide(ln.substr(8)));
        else if (low.rfind("palette:", 0) == 0) palette = PathUtils::Trim(PathUtils::Utf8ToWide(ln.substr(8)));
        else if (low.rfind("id:", 0) == 0) curLang = PathUtils::Trim(PathUtils::Utf8ToWide(ln.substr(3)));
        else if (low.rfind("timestamp:", 0) == 0 && cues.size() < 4096) {
            size_t fp = low.find("filepos:");
            std::string_view tsPart = (fp != std::string::npos) ? ln.substr(10, fp - 10) : ln.substr(10);
            while (!tsPart.empty() && (tsPart.back() == ',' || tsPart.back() == ' ')) tsPart.remove_suffix(1);
            std::string_view fpPart = (fp != std::string::npos) ? ln.substr(fp + 8) : std::string_view{};
            cues.push_back({ curLang, PathUtils::Trim(PathUtils::Utf8ToWide(tsPart)), PathUtils::Trim(PathUtils::Utf8ToWide(fpPart)) });
        }
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"DVD VobSub Subtitle Index (.idx)";

    SpreadsheetSheet hdr;
    hdr.name = L"VobSub_Info";
    hdr.rows.push_back({ L"Directive", L"Value" });
    if (!resSize.empty()) hdr.rows.push_back({ L"Video Size", resSize });
    if (!langIdx.empty()) hdr.rows.push_back({ L"Default Language Index", langIdx });
    if (!palette.empty()) hdr.rows.push_back({ L"Palette", palette });
    hdr.rows.push_back({ L"Subtitle Cues", std::to_wstring(cues.size()) });
    FinalizeSheetCols(hdr);
    out.sheets.push_back(std::move(hdr));

    if (!cues.empty()) {
        SpreadsheetSheet csh;
        csh.name = L"Timestamps";
        csh.rows.push_back({ L"#", L"Stream ID", L"Timestamp", L"File Offset (Hex)" });
        for (size_t i = 0; i < cues.size(); ++i) {
            csh.rows.push_back({ std::to_wstring(i + 1), cues[i].lang, cues[i].ts, L"0x" + cues[i].filepos });
        }
        FinalizeSheetCols(csh);
        out.sheets.push_back(std::move(csh));
    }
    out.valid = true;
    return true;
}

} // namespace DetailMediaAndScoreDoc

inline bool ParseVobSubIdxDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    return DetailMediaAndScoreDoc::ParseVobSubIdx(raw, out);
}

} // namespace Formats
