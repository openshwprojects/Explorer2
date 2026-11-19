#pragma once
// ReaperRpp.h — REAPER DAW Project (.rpp) Reader

namespace Formats {
namespace DetailMedia100 {

inline bool ParseReaperRpp(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    if (sv.size() >= 3 && static_cast<uint8_t>(sv[0]) == 0xEF && static_cast<uint8_t>(sv[1]) == 0xBB && static_cast<uint8_t>(sv[2]) == 0xBF) {
        sv.remove_prefix(3);
    }
    while (!sv.empty() && static_cast<unsigned char>(sv.front()) <= 32) sv.remove_prefix(1);
    if (sv.size() < 15 || sv.substr(0, 15) != "<REAPER_PROJECT") return false;

    auto tokenizeLine = [](std::string_view ln) -> std::vector<std::string> {
        std::vector<std::string> toks;
        size_t i = 0;
        while (i < ln.size()) {
            while (i < ln.size() && (ln[i] == ' ' || ln[i] == '\t')) ++i;
            if (i >= ln.size()) break;
            if (ln[i] == '"' || ln[i] == '\'' || ln[i] == '`') {
                char q = ln[i++];
                size_t s = i;
                while (i < ln.size() && ln[i] != q) ++i;
                toks.push_back( std::string(ln.substr(s, i - s)) );
                if (i < ln.size()) ++i;
            } else {
                size_t s = i;
                while (i < ln.size() && ln[i] != ' ' && ln[i] != '\t') ++i;
                toks.push_back( std::string(ln.substr(s, i - s)) );
            }
        }
        return toks;
    };

    std::wstring rppVer, tempoStr, srStr;
    struct RppTrack {
        std::wstring name;
        std::wstring volPan;
        size_t itemCount = 0;
        std::vector<std::wstring> fx;
        std::vector<std::wstring> sources;
    };
    std::vector<RppTrack> tracks;
    std::vector<std::string> blockStack;
    int trackDepth = -1;

    size_t ls = 0;
    while (ls < sv.size()) {
        size_t le = sv.find('\n', ls);
        if (le == std::string_view::npos) le = sv.size();
        std::string_view ln = sv.substr(ls, le - ls);
        if (!ln.empty() && ln.back() == '\r') ln.remove_suffix(1);
        ls = (le < sv.size()) ? (le + 1) : sv.size();

        auto toks = tokenizeLine(ln);
        if (toks.empty()) continue;

        const std::string& head = toks[0];
        if (head == ">") {
            if (!blockStack.empty()) {
                if (static_cast<int>(blockStack.size()) - 1 == trackDepth) trackDepth = -1;
                blockStack.pop_back();
            }
            continue;
        }
        if (!head.empty() && head[0] == '<') {
            std::string bName = head.substr(1);
            if (bName == "REAPER_PROJECT" && toks.size() >= 3) {
                rppVer = PathUtils::Utf8ToWide(toks[2]);
            } else if (bName == "TRACK") {
                trackDepth = static_cast<int>(blockStack.size());
                tracks.push_back({});
            } else if (bName == "ITEM" && trackDepth >= 0 && !tracks.empty()) {
                tracks.back().itemCount++;
            } else if ((bName == "VST" || bName == "AU" || bName == "JS" || bName == "CLAP") && trackDepth >= 0 && !tracks.empty() && toks.size() >= 2) {
                tracks.back().fx.push_back(PathUtils::Utf8ToWide(toks[1]));
            }
            blockStack.push_back(std::move(bName));
            continue;
        }

        const std::string curBlk = blockStack.empty() ? "" : blockStack.back();
        if (curBlk == "REAPER_PROJECT") {
            if (head == "TEMPO" && toks.size() >= 4) {
                tempoStr = PathUtils::Utf8ToWide(toks[1] + " BPM (" + toks[2] + "/" + toks[3] + ")");
            } else if (head == "SAMPLERATE" && toks.size() >= 2) {
                srStr = PathUtils::Utf8ToWide(toks[1]) + L" Hz";
            }
        } else if (curBlk == "TRACK" && !tracks.empty()) {
            if (head == "NAME" && toks.size() >= 2) tracks.back().name = PathUtils::Utf8ToWide(toks[1]);
            else if (head == "VOLPAN" && toks.size() >= 3) tracks.back().volPan = L"vol=" + PathUtils::Utf8ToWide(toks[1]) + L", pan=" + PathUtils::Utf8ToWide(toks[2]);
        } else if (curBlk.rfind("SOURCE", 0) == 0 && head == "FILE" && toks.size() >= 2 && trackDepth >= 0 && !tracks.empty()) {
            tracks.back().sources.push_back(PathUtils::Utf8ToWide(toks[1]));
        }
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Cockos REAPER Project (.rpp)";

    SpreadsheetSheet info;
    info.name = L"REAPER_Project";
    info.rows.push_back({ L"Property", L"Value" });
    if (!rppVer.empty()) info.rows.push_back({ L"REAPER Version", rppVer });
    if (!tempoStr.empty()) info.rows.push_back({ L"Tempo", tempoStr });
    if (!srStr.empty()) info.rows.push_back({ L"Sample Rate", srStr });
    info.rows.push_back({ L"Tracks", std::to_wstring(tracks.size()) });
    FinalizeSheetCols(info);
    out.sheets.push_back(std::move(info));

    if (!tracks.empty()) {
        SpreadsheetSheet tsh;
        tsh.name = L"Tracks";
        tsh.rows.push_back({ L"#", L"Track Name", L"Vol/Pan", L"Items", L"FX Plugins", L"Media Files" });
        for (size_t i = 0; i < tracks.size(); ++i) {
            auto joinVec = [](const std::vector<std::wstring>& v) {
                std::wstring s;
                for (size_t k = 0; k < v.size() && k < 6; ++k) {
                    if (k > 0) s += L"; ";
                    s += v[k];
                }
                return s;
            };
            tsh.rows.push_back({
                std::to_wstring(i + 1),
                tracks[i].name.empty() ? L"(Track)" : tracks[i].name,
                tracks[i].volPan,
                std::to_wstring(tracks[i].itemCount),
                joinVec(tracks[i].fx),
                joinVec(tracks[i].sources)
            });
        }
        FinalizeSheetCols(tsh);
        out.sheets.push_back(std::move(tsh));
    }
    out.valid = true;
    return true;
}

} // namespace DetailMedia100

inline bool ParseReaperRppDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) { return DetailMedia100::ParseReaperRpp(raw, out); }

} // namespace Formats
