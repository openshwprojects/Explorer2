#pragma once
// SoundFontSfz.h — SFZ Instrument Definition (.sfz) Reader

namespace Formats {
namespace DetailMedia100 {

inline bool ParseSoundFontSfz(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 8) return false;
    for (size_t i = 0; i < std::min<size_t>(raw.size(), 2048); ++i) {
        if (raw[i] == 0) return false;
    }

    // Strip // and /* */ comments
    std::string clean;
    clean.reserve(raw.size());
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    for (size_t i = 0; i < sv.size(); ++i) {
        if (i + 1 < sv.size() && sv[i] == '/' && sv[i + 1] == '/') {
            while (i < sv.size() && sv[i] != '\n') ++i;
            clean.push_back('\n');
        } else if (i + 1 < sv.size() && sv[i] == '/' && sv[i + 1] == '*') {
            size_t endC = sv.find("*/", i + 2);
            if (endC == std::string_view::npos) break;
            i = endC + 1;
            clean.push_back(' ');
        } else {
            clean.push_back(sv[i]);
        }
    }

    std::string curSection;
    std::map<std::string, std::string> globalOps;
    std::map<std::string, std::string> groupOps;
    std::map<std::string, std::string> curOps;
    std::vector<std::map<std::string, std::string>> regions;
    int headerCount = 0;

    auto flushRegionIfActive = [&]() {
        if (curSection == "region" && !curOps.empty()) {
            std::map<std::string, std::string> merged = globalOps;
            for (const auto& kv : groupOps) merged[kv.first] = kv.second;
            for (const auto& kv : curOps) merged[kv.first] = kv.second;
            regions.push_back(std::move(merged));
        } else if (curSection == "global") {
            for (const auto& kv : curOps) globalOps[kv.first] = kv.second;
        } else if (curSection == "group") {
            groupOps = curOps;
        }
        curOps.clear();
    };

    size_t i = 0;
    while (i < clean.size()) {
        while (i < clean.size() && static_cast<unsigned char>(clean[i]) <= 32) ++i;
        if (i >= clean.size()) break;

        if (clean[i] == '<') {
            size_t gt = clean.find('>', i + 1);
            if (gt == std::string::npos) break;
            flushRegionIfActive();
            curSection = PathUtils::ToLowerAscii(std::string_view(clean).substr(i + 1, gt - i - 1));
            ++headerCount;
            i = gt + 1;
            continue;
        }

        size_t eq = clean.find('=', i);
        size_t nextLt = clean.find('<', i);
        if (eq == std::string::npos || (nextLt != std::string::npos && nextLt < eq)) {
            i = (nextLt != std::string::npos) ? nextLt : clean.size();
            continue;
        }
        std::string key = PathUtils::ToLowerAscii(std::string_view(clean).substr(i, eq - i));
        // Trim key whitespace (take last word if stray token preceded)
        size_t lastSp = key.find_last_of(" \t\r\n");
        if (lastSp != std::string::npos) key = key.substr(lastSp + 1);
        i = eq + 1;

        std::string val;
        if (key == "sample" || key == "default_path") {
            // Sample paths can contain spaces; read until newline, '<', or next key= token
            size_t lineEnd = clean.find_first_of("\r\n<", i);
            if (lineEnd == std::string::npos) lineEnd = clean.size();
            std::string_view rem(clean.data() + i, lineEnd - i);
            size_t nextEq = rem.find('=');
            if (nextEq != std::string_view::npos) {
                size_t spBefore = rem.rfind(' ', nextEq);
                if (spBefore != std::string_view::npos) rem = rem.substr(0, spBefore);
            }
            val = std::string(rem);
            i += rem.size();
        } else {
            size_t vEnd = i;
            while (vEnd < clean.size() && static_cast<unsigned char>(clean[vEnd]) > 32 && clean[vEnd] != '<') ++vEnd;
            val = clean.substr(i, vEnd - i);
            i = vEnd;
        }
        while (!val.empty() && (val.front() == ' ' || val.front() == '"')) val.erase(val.begin());
        while (!val.empty() && (val.back() == ' ' || val.back() == '"')) val.pop_back();
        if (!key.empty()) curOps[key] = val;
    }
    flushRegionIfActive();

    if (headerCount == 0 || (regions.empty() && globalOps.empty() && groupOps.empty())) return false;

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"SFZ Instrument Definition (.sfz)";

    SpreadsheetSheet regSh;
    regSh.name = L"SFZ_Regions";
    regSh.rows.push_back({ L"#", L"Sample", L"LoKey", L"HiKey", L"PitchCenter", L"LoVel", L"HiVel", L"Volume", L"Pan" });
    auto getOp = [](const std::map<std::string, std::string>& m, const char* k, const char* fallback = "") -> std::wstring {
        auto it = m.find(k);
        if (it != m.end()) return PathUtils::Utf8ToWide(it->second);
        if (fallback[0] != 0) {
            auto it2 = m.find(fallback);
            if (it2 != m.end()) return PathUtils::Utf8ToWide(it2->second);
        }
        return L"-";
    };
    for (size_t r = 0; r < regions.size(); ++r) {
        const auto& m = regions[r];
        regSh.rows.push_back({
            std::to_wstring(r + 1),
            getOp(m, "sample"),
            getOp(m, "lokey", "key"),
            getOp(m, "hikey", "key"),
            getOp(m, "pitch_keycenter", "key"),
            getOp(m, "lovel"),
            getOp(m, "hivel"),
            getOp(m, "volume"),
            getOp(m, "pan")
        });
    }
    if (regions.empty()) {
        for (const auto& kv : globalOps) {
            regSh.rows.push_back({ L"global", PathUtils::Utf8ToWide(kv.first), PathUtils::Utf8ToWide(kv.second) });
        }
    }
    FinalizeSheetCols(regSh);
    out.sheets.push_back(std::move(regSh));
    out.valid = true;
    return true;
}

} // namespace DetailMedia100

inline bool ParseSoundFontSfzDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) { return DetailMedia100::ParseSoundFontSfz(raw, out); }

} // namespace Formats
