#pragma once
// MuseScoreMscz.h — MuseScore Compressed Score (.mscz) Reader

namespace Formats {
namespace DetailMedia100 {

// ============================================================================
// 6. Score & DAW Formats (.mscz, .als, .flp, .gp/.gp3/.gp4/.gp5, .rpp)
// ============================================================================
inline bool ParseMuseScoreMscz(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::vector<ZipDeflate::ArchiveItemMeta> entries;
    if (!ZipDeflate::ListZipEntriesFromMemory(raw.data(), raw.size(), entries) || entries.empty()) return false;

    std::vector<uint8_t> mscxBytes;
    std::wstring mscxName;
    for (const auto& e : entries) {
        if (!e.isDirectory && PathUtils::GetExtension(e.pathInArchive) == L".mscx") {
            if (ZipDeflate::ExtractZipEntryFromMemory(raw.data(), raw.size(), e, mscxBytes) && !mscxBytes.empty()) {
                mscxName = e.pathInArchive;
                break;
            }
        }
    }
    if (mscxBytes.empty()) return false;

    std::string_view xml(reinterpret_cast<const char*>(mscxBytes.data()), mscxBytes.size());
    if (xml.find("<museScore") == std::string_view::npos) return false;

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"MuseScore Compressed Score (.mscz)";

    SpreadsheetSheet meta;
    meta.name = L"Score_Metadata";
    meta.rows.push_back({ L"Field", L"Value" });
    meta.rows.push_back({ L"Score XML Entry", mscxName });

    size_t msPos = xml.find("<museScore");
    if (msPos != std::string_view::npos) {
        size_t gt = xml.find('>', msPos);
        if (gt != std::string_view::npos) {
            std::string ver = ExtractXmlAttr(xml.substr(msPos, gt - msPos), "version");
            if (!ver.empty()) meta.rows.push_back({ L"MuseScore Version", PathUtils::Utf8ToWide(ver) });
        }
    }

    // Parse <metaTag name="...">...</metaTag>
    size_t p = 0;
    while ((p = xml.find("<metaTag", p)) != std::string_view::npos) {
        size_t gt = xml.find('>', p);
        size_t cl = (gt != std::string_view::npos) ? xml.find("</metaTag>", gt) : std::string_view::npos;
        if (gt == std::string_view::npos || cl == std::string_view::npos) break;
        std::string name = ExtractXmlAttr(xml.substr(p, gt - p), "name");
        std::wstring val = ::Explorer2::XmlTagScan::DecodeEntitiesWide(PathUtils::Trim(PathUtils::Utf8ToWide(xml.substr(gt + 1, cl - gt - 1))));
        if (!name.empty() && !val.empty()) {
            meta.rows.push_back({ L"Meta: " + PathUtils::Utf8ToWide(name), val });
        }
        p = cl + 10;
    }

    // Parse <TimeSig> and <Tempo>
    size_t tsPos = xml.find("<TimeSig>");
    if (tsPos != std::string_view::npos) {
        size_t tsEnd = xml.find("</TimeSig>", tsPos);
        if (tsEnd != std::string_view::npos) {
            std::string_view tsBlk = xml.substr(tsPos, tsEnd - tsPos);
            std::wstring sigN = ExtractFirstXmlTagText(tsBlk, "sigN");
            std::wstring sigD = ExtractFirstXmlTagText(tsBlk, "sigD");
            if (!sigN.empty() && !sigD.empty()) meta.rows.push_back({ L"Time Signature", sigN + L"/" + sigD });
        }
    }
    size_t tmpPos = xml.find("<Tempo>");
    if (tmpPos != std::string_view::npos) {
        size_t tmpEnd = xml.find("</Tempo>", tmpPos);
        if (tmpEnd != std::string_view::npos) {
            std::wstring tVal = ExtractFirstXmlTagText(xml.substr(tmpPos, tmpEnd - tmpPos), "tempo");
            if (!tVal.empty()) {
                double bps = std::wcstod(tVal.c_str(), nullptr);
                int bpm = static_cast<int>(bps * 60.0 + 0.5);
                meta.rows.push_back({ L"Tempo", std::to_wstring(bpm) + L" BPM" });
            }
        }
    }

    SpreadsheetSheet parts;
    parts.name = L"Parts_&_Staves";
    parts.rows.push_back({ L"Item", L"Name / ID", L"Details" });
    p = 0;
    int partIdx = 0;
    while ((p = xml.find("<Part", p)) != std::string_view::npos) {
        if (p + 5 < xml.size() && xml[p + 5] != '>' && xml[p + 5] != ' ') { p += 5; continue; }
        size_t cl = xml.find("</Part>", p);
        if (cl == std::string_view::npos) break;
        std::string_view blk = xml.substr(p, cl - p);
        std::wstring tName = ExtractFirstXmlTagText(blk, "trackName");
        if (tName.empty()) tName = ExtractFirstXmlTagText(blk, "longName");
        std::wstring instId = ExtractFirstXmlTagText(blk, "instrumentId");
        parts.rows.push_back({ L"Part " + std::to_wstring(++partIdx), tName.empty() ? L"(Part)" : tName, instId });
        p = cl + 7;
    }

    p = 0;
    while ((p = xml.find("<Staff ", p)) != std::string_view::npos) {
        size_t gt = xml.find('>', p);
        size_t cl = (gt != std::string_view::npos) ? xml.find("</Staff>", gt) : std::string_view::npos;
        if (gt == std::string_view::npos || cl == std::string_view::npos) break;
        std::string sid = ExtractXmlAttr(xml.substr(p, gt - p), "id");
        std::string_view blk = xml.substr(gt + 1, cl - gt - 1);
        size_t mCount = 0;
        size_t mp = 0;
        while ((mp = blk.find("<Measure", mp)) != std::string_view::npos) { ++mCount; mp += 8; }
        if (mCount > 0) {
            parts.rows.push_back({ L"Staff", L"ID " + PathUtils::Utf8ToWide(sid), std::to_wstring(mCount) + L" measures" });
        }
        p = cl + 8;
    }

    FinalizeSheetCols(meta);
    out.sheets.push_back(std::move(meta));
    if (parts.rows.size() > 1) { FinalizeSheetCols(parts); out.sheets.push_back(std::move(parts)); }
    out.valid = true;
    return true;
}


} // namespace DetailMedia100

inline bool ParseMuseScoreMsczDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) { return DetailMedia100::ParseMuseScoreMscz(raw, out); }

} // namespace Formats
