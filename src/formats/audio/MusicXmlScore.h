#pragma once
// MusicXmlScore.h — MusicXML & Compressed MusicXML Score (.musicxml / .mxl)

namespace Formats {

inline bool ParseMusicXmlScoreDoc(const std::vector<uint8_t>& raw, const std::wstring& ext, ParsedOfficeDocument& out) {
    // #96. MusicXML & Compressed MusicXML Score (.musicxml / .mxl)
    if (ext == L".musicxml" || ext == L".mxl") {
        std::vector<uint8_t> xmlBuf;
        std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
        if (raw.size() >= 4 && raw[0] == 'P' && raw[1] == 'K') {
            std::vector<ZipDeflate::ArchiveItemMeta> entries;
            if (ZipDeflate::ListZipEntriesFromMemory(raw.data(), raw.size(), entries)) {
                for (const auto& e : entries) {
                    std::wstring eExt = PathUtils::GetExtension(e.pathInArchive);
                    if ((eExt == L".xml" || eExt == L".musicxml") && e.pathInArchive.find(L"META-INF") == std::wstring::npos) {
                        if (ZipDeflate::ExtractZipEntryFromMemory(raw.data(), raw.size(), e, xmlBuf) && !xmlBuf.empty()) {
                            sv = std::string_view(reinterpret_cast<const char*>(xmlBuf.data()), xmlBuf.size());
                            break;
                        }
                    }
                }
            }
        }
        if (sv.find("<score-partwise") == std::string_view::npos && sv.find("<score-timewise") == std::string_view::npos) return false;

        auto xmlTagVal = [&](std::string_view src, std::string_view tag) -> std::string {
            std::string open = "<" + std::string(tag) + ">";
            std::string close = "</" + std::string(tag) + ">";
            size_t p = src.find(open);
            if (p == std::string_view::npos) return {};
            p += open.size();
            size_t e = src.find(close, p);
            return (e == std::string_view::npos) ? std::string{} : std::string(src.substr(p, e - p));
        };

        std::string title = xmlTagVal(sv, "work-title");
        if (title.empty()) title = xmlTagVal(sv, "movement-title");
        // Notes in document order: part-wise scores nest measures in parts, time-wise ones parts in measures. The time
        // signature in force is tracked; chord notes (sounding with the note before) and grace notes are marked.
        namespace X = ::Explorer2::XmlTagScan;
        SpreadsheetSheet notesSh;
        notesSh.name = L"Score_Notes";
        notesSh.rows.push_back({ L"Part", L"Measure", L"Pitch", L"Duration Type", L"Time Sig", L"Lyric" });
        std::string partId, measureNo, timeSig;
        size_t tagPos = 0;
        X::XmlTag t;
        while (X::NextTag(sv, tagPos, t) && notesSh.rows.size() <= 2000) {
            if (t.isClosing) continue;
            if (t.localName == "part" && !t.isSelfClosing) {
                partId = X::AttrText(t.attrs, "id");
            } else if (t.localName == "measure") {
                measureNo = X::AttrText(t.attrs, "number");
            } else if ((t.localName == "time" || t.localName == "note") && !t.isSelfClosing) {
                std::string_view body;
                size_t after = 0;
                if (!X::FindElementBody(sv, t.afterTag, t.localName, body, after)) break;
                tagPos = after;
                if (t.localName == "time") {
                    std::string beats = X::ChildText(body, "beats");
                    if (!beats.empty()) timeSig = beats + "/" + X::ChildText(body, "beat-type");
                    continue;
                }
                X::Element el;
                std::string pitch;
                if (X::FindChild(body, "pitch", el)) {
                    std::string alter = X::ChildText(el.body, "alter");
                    const char* acc = (alter == "1") ? "#" : (alter == "-1") ? "b" : (alter == "2") ? "##" : (alter == "-2") ? "bb" : "";
                    pitch = X::ChildText(el.body, "step") + acc + X::ChildText(el.body, "octave");
                } else if (X::FindChild(body, "unpitched", el)) {
                    pitch = X::ChildText(el.body, "display-step") + X::ChildText(el.body, "display-octave") + " (unpitched)";
                } else {
                    pitch = "Rest";
                }
                if (X::FindChild(body, "chord", el)) pitch = "+ " + pitch;
                if (X::FindChild(body, "grace", el)) pitch += " (grace)";
                std::string lyric;
                if (X::FindChild(body, "lyric", el)) lyric = X::ChildText(el.body, "text");
                notesSh.rows.push_back({ PathUtils::Utf8ToWide(partId), PathUtils::Utf8ToWide(measureNo), PathUtils::Utf8ToWide(pitch),
                                         PathUtils::Utf8ToWide(X::ChildText(body, "type")), PathUtils::Utf8ToWide(timeSig),
                                         PathUtils::Utf8ToWide(lyric) });
            }
        }
        notesSh.maxCols = 6;

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"MusicXML Musical Score (" + (title.empty() ? ext : PathUtils::Utf8ToWide(title)) + L")";
        out.sheets.push_back(std::move(notesSh));
        out.valid = true;
        return true;
    }

    return false;
}

} // namespace Formats
