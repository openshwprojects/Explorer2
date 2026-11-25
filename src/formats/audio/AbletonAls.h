#pragma once
// AbletonAls.h — Ableton Live Set (.als) Reader

namespace Formats {
namespace DetailMediaAndScoreDoc {

// Ableton Live Set (.als, gzip-compressed XML): <Ableton> <LiveSet> <Tracks> lists the tracks in the order Live
// shows them (audio / MIDI / group / return). Each row: the track's own Name/EffectiveName, the group it sits in
// (TrackGroupId), its session clips (MainSequencer/ClipSlotList) and arrangement clips (ArrangerAutomation) -
// frozen copies in the FreezeSequencer are not counted - and its devices. Tempo from the master track's mixer.
inline bool ParseAbletonAls(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    namespace X = ::Explorer2::XmlTagScan;
    std::vector<uint8_t> uncomp;
    std::string_view xml;
    if (raw.size() >= 2 && raw[0] == 0x1F && raw[1] == 0x8B) {
        if (!ZipDeflate::InflateGzip(raw.data(), raw.size(), uncomp, 256 * 1024 * 1024) || uncomp.empty()) return false;
        xml = std::string_view(reinterpret_cast<const char*>(uncomp.data()), uncomp.size());
    } else {
        xml = std::string_view(reinterpret_cast<const char*>(raw.data()), raw.size());
    }
    X::Element root, liveSet;
    if (!X::FindChild(xml, "Ableton", root) || !X::FindChild(root.body, "LiveSet", liveSet)) return false;

    auto valueOf = [](std::string_view body, std::string_view child) {
        X::Element e;
        return X::FindChild(body, child, e) ? X::AttrText(e.attrs, "Value") : std::string();
    };
    auto countClips = [](std::string_view body) {
        size_t n = 0;
        for (const char* tag : { "AudioClip", "MidiClip" }) {
            X::ForEachElement(body, tag, [&](const X::Element&) { ++n; return true; });
        }
        return n;
    };

    std::string tempo;
    X::Element master, chain, mixer, tempoEl;
    if (X::FindChild(liveSet.body, "MasterTrack", master) && X::FindChild(master.body, "DeviceChain", chain) &&
        X::FindChild(chain.body, "Mixer", mixer) && X::FindChild(mixer.body, "Tempo", tempoEl)) {
        tempo = valueOf(tempoEl.body, "Manual");
    }
    if (tempo.empty()) { // older or reduced sets: the first tempo parameter in the set
        X::ForEachElement(liveSet.body, "Tempo", [&](const X::Element& t) {
            tempo = valueOf(t.body, "Manual");
            return tempo.empty();
        });
    }

    struct TrackRow { std::string type, id, name, groupId; size_t session = 0, arrangement = 0, devices = 0; };
    std::vector<TrackRow> tracks;
    X::Element tracksEl;
    if (X::FindChild(liveSet.body, "Tracks", tracksEl)) {
        X::ForEachChild(tracksEl.body, [&](std::string_view tag, const X::Element& t) {
            if (tracks.size() >= 2000) return false;
            TrackRow r;
            r.type = std::string(tag);
            r.id = X::AttrText(t.attrs, "Id");
            X::Element nameEl;
            if (X::FindChild(t.body, "Name", nameEl)) {
                r.name = valueOf(nameEl.body, "EffectiveName");
                if (r.name.empty()) r.name = valueOf(nameEl.body, "UserName");
            }
            r.groupId = valueOf(t.body, "TrackGroupId");
            X::Element devChain, seq, slots, inner, devices;
            if (X::FindChild(t.body, "DeviceChain", devChain) && X::FindChild(devChain.body, "MainSequencer", seq)) {
                if (X::FindChild(seq.body, "ClipSlotList", slots)) r.session = countClips(slots.body);
                X::ForEachElement(seq.body, "ArrangerAutomation", [&](const X::Element& a) { r.arrangement += countClips(a.body); return true; });
                if (X::FindChild(devChain.body, "DeviceChain", inner) && X::FindChild(inner.body, "Devices", devices)) {
                    X::ForEachChild(devices.body, [&](std::string_view, const X::Element&) { ++r.devices; return true; });
                }
            } else {
                r.arrangement = countClips(t.body); // a reduced set without sequencers: the clips it holds
            }
            tracks.push_back(std::move(r));
            return true;
        });
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Ableton Live Set (.als)";

    std::map<std::string, size_t> byType;
    SpreadsheetSheet trackSheet;
    trackSheet.name = L"Tracks";
    trackSheet.rows.push_back({ L"#", L"Track Type", L"Track Name", L"Group", L"Session Clips", L"Arrangement Clips", L"Devices" });
    for (size_t i = 0; i < tracks.size(); ++i) {
        const TrackRow& r = tracks[i];
        ++byType[r.type];
        std::string group;
        if (!r.groupId.empty() && r.groupId != "-1") {
            for (const auto& g : tracks) {
                if (g.type == "GroupTrack" && g.id == r.groupId) { group = g.name; break; }
            }
        }
        trackSheet.rows.push_back({ std::to_wstring(i + 1), PathUtils::Utf8ToWide(r.type), PathUtils::Utf8ToWide(r.name.empty() ? std::string("(Track)") : r.name),
                                    PathUtils::Utf8ToWide(group), std::to_wstring(r.session), std::to_wstring(r.arrangement), std::to_wstring(r.devices) });
    }

    SpreadsheetSheet info;
    info.name = L"Ableton_Project";
    info.rows.push_back({ L"Property", L"Value" });
    std::string creator = X::AttrText(root.attrs, "Creator");
    info.rows.push_back({ L"Creator", creator.empty() ? L"Ableton Live" : PathUtils::Utf8ToWide(creator) });
    std::string majVer = X::AttrText(root.attrs, "MajorVersion"), minVer = X::AttrText(root.attrs, "MinorVersion");
    if (!majVer.empty()) info.rows.push_back({ L"Schema Version", PathUtils::Utf8ToWide(majVer + "." + minVer) });
    if (!tempo.empty()) info.rows.push_back({ L"Master Tempo", PathUtils::Utf8ToWide(tempo) + L" BPM" });
    info.rows.push_back({ L"Total Tracks", std::to_wstring(tracks.size()) });
    for (const auto& kv : byType) info.rows.push_back({ L"Tracks: " + PathUtils::Utf8ToWide(kv.first), std::to_wstring(kv.second) });
    X::Element scenes;
    if (X::FindChild(liveSet.body, "Scenes", scenes)) {
        size_t n = 0;
        X::ForEachChild(scenes.body, [&](std::string_view, const X::Element&) { ++n; return true; });
        info.rows.push_back({ L"Scenes", std::to_wstring(n) });
    }
    info.rows.push_back({ L"Uncompressed XML Size", PathUtils::FormatByteSize(xml.size()) });
    FinalizeSheetCols(info);
    out.sheets.push_back(std::move(info));
    if (trackSheet.rows.size() > 1) {
        FinalizeSheetCols(trackSheet);
        out.sheets.push_back(std::move(trackSheet));
    }
    out.valid = true;
    return true;
}

} // namespace DetailMediaAndScoreDoc

inline bool ParseAbletonAlsDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) { return DetailMediaAndScoreDoc::ParseAbletonAls(raw, out); }

} // namespace Formats
