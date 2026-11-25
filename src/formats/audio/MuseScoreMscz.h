#pragma once
// MuseScoreMscz.h — MuseScore Compressed Score (.mscz) Reader

namespace Formats {
namespace DetailMediaAndScoreDoc {

inline bool ParseMuseScoreMscz(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::vector<ZipDeflate::ArchiveItemMeta> entries;
    bool usedFile = false;
    if (!DetailStructuredDataDoc::ListZipPackage(raw, out, entries, usedFile) || entries.empty()) return false;

    std::vector<uint8_t> mscxBytes;
    std::wstring mscxName;
    for (const auto& e : entries) {
        if (!e.isDirectory && PathUtils::ToLower(PathUtils::GetExtension(e.pathInArchive)) == L".mscx") {
            if (DetailStructuredDataDoc::ExtractZipPackageEntry(raw, out, usedFile, e, mscxBytes) && !mscxBytes.empty()) {
                mscxName = e.pathInArchive;
                break;
            }
        }
    }
    if (mscxBytes.empty()) return false;

    namespace X = ::Explorer2::XmlTagScan;
    std::string xml = X::DocumentToUtf8(mscxBytes.data(), mscxBytes.size());
    X::Element msRoot;
    if (!X::FindChild(xml, "museScore", msRoot)) return false;

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"MuseScore Compressed Score (.mscz)";

    SpreadsheetSheet meta;
    meta.name = L"Score_Metadata";
    meta.rows.push_back({ L"Field", L"Value" });
    meta.rows.push_back({ L"Score XML Entry", mscxName });

    std::string ver = X::AttrText(msRoot.attrs, "version");
    if (!ver.empty()) meta.rows.push_back({ L"MuseScore Version", PathUtils::Utf8ToWide(ver) });

    X::Element scoreEl;
    std::string_view scoreBody = msRoot.body;
    if (X::FindChild(msRoot.body, "Score", scoreEl)) {
        scoreBody = scoreEl.body;
    }

    SpreadsheetSheet parts;
    parts.name = L"Parts_&_Staves";
    parts.rows.push_back({ L"Item", L"Name / ID", L"Details" });

    bool gotTimeSig = false, gotTempo = false;
    int partIdx = 0;

    X::ForEachChild(scoreBody, [&](std::string_view tagName, const X::Element& el) {
        if (tagName == "metaTag") {
            std::string name = X::AttrText(el.attrs, "name");
            std::wstring val = PathUtils::Trim(PathUtils::Utf8ToWide(X::DecodeText(el.body)));
            if (!name.empty() && !val.empty()) {
                meta.rows.push_back({ L"Meta: " + PathUtils::Utf8ToWide(name), std::move(val) });
            }
        } else if (tagName == "Part") {
            std::string tName = X::ChildText(el.body, "trackName");
            if (tName.empty()) {
                X::Element inst;
                if (X::FindChild(el.body, "Instrument", inst)) {
                    tName = X::ChildText(inst.body, "longName");
                    if (tName.empty()) tName = X::ChildText(inst.body, "trackName");
                }
            }
            std::string instId;
            X::Element inst;
            if (X::FindChild(el.body, "Instrument", inst)) {
                instId = X::ChildText(inst.body, "instrumentId");
            }
            parts.rows.push_back({
                L"Part " + std::to_wstring(++partIdx),
                tName.empty() ? L"(Part)" : PathUtils::Utf8ToWide(tName),
                PathUtils::Utf8ToWide(instId)
            });
        } else if (tagName == "Staff") {
            std::string sid = X::AttrText(el.attrs, "id");
            size_t mCount = 0;
            X::ForEachChild(el.body, [&](std::string_view sn, const X::Element& mel) {
                if (sn != "Measure") return true;
                ++mCount;
                if (!gotTimeSig || !gotTempo) {
                    size_t vp = 0;
                    X::XmlTag vt;
                    while (X::NextTag(mel.body, vp, vt)) {
                        if (vt.isClosing || vt.isSelfClosing) continue;
                        if (!gotTimeSig && vt.localName == "TimeSig") {
                            std::string_view tbody; size_t after = 0;
                            if (X::FindElementBody(mel.body, vt.afterTag, "TimeSig", tbody, after)) {
                                std::string sigN = X::ChildText(tbody, "sigN");
                                std::string sigD = X::ChildText(tbody, "sigD");
                                if (!sigN.empty() && !sigD.empty()) {
                                    meta.rows.push_back({ L"Time Signature", PathUtils::Utf8ToWide(sigN + "/" + sigD) });
                                    gotTimeSig = true;
                                }
                                vp = after;
                            }
                        } else if (!gotTempo && vt.localName == "Tempo") {
                            std::string_view tbody; size_t after = 0;
                            if (X::FindElementBody(mel.body, vt.afterTag, "Tempo", tbody, after)) {
                                std::string tVal = X::ChildText(tbody, "tempo");
                                if (!tVal.empty()) {
                                    double bps = std::strtod(tVal.c_str(), nullptr);
                                    if (bps > 0.0) {
                                        int bpm = static_cast<int>(bps * 60.0 + 0.5);
                                        meta.rows.push_back({ L"Tempo", std::to_wstring(bpm) + L" BPM" });
                                        gotTempo = true;
                                    }
                                }
                                vp = after;
                            }
                        }
                    }
                }
                return true;
            });
            if (mCount > 0) {
                parts.rows.push_back({ L"Staff", L"ID " + PathUtils::Utf8ToWide(sid), std::to_wstring(mCount) + L" measures" });
            }
        }
        return true;
    });

    FinalizeSheetCols(meta);
    out.sheets.push_back(std::move(meta));
    if (parts.rows.size() > 1) { FinalizeSheetCols(parts); out.sheets.push_back(std::move(parts)); }
    out.valid = true;
    return true;
}

} // namespace DetailMediaAndScoreDoc

inline bool ParseMuseScoreMsczDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) { return DetailMediaAndScoreDoc::ParseMuseScoreMscz(raw, out); }

} // namespace Formats
