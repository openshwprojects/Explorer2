#pragma once
// VerilogVcd.h — IEEE 1364 Verilog Value Change Dump (.vcd) Digital Logic Timing Diagram Parser

namespace Formats {

inline bool ParseVerilogVcdWaveforms(std::string_view sv, ParsedEdaBoard& out) {
    if (sv.find("$var") == std::string_view::npos && sv.find("$dumpvars") == std::string_view::npos) return false;
    out.kind = EdaDocKind::KiCadSch;
    out.formatTitle = L"IEEE 1364 Verilog VCD Logic Waveforms (.vcd)";

    struct VcdSig {
        std::string id;
        std::wstring name;
        int row = 0;
        float lastTime = 0.0f;
        int lastVal = 0;
    };
    std::vector<VcdSig> sigs;

    size_t pos = 0;
    float curTime = 0.0f;
    float maxTime = 10.0f;
    while (pos < sv.size()) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.remove_suffix(1);
        if (ln.empty()) continue;

        if (ln.rfind("$var", 0) == 0 && sigs.size() < 32) {
            char vtype[32] = {}, id[32] = {}, name[64] = {};
            int width = 1;
            if (sscanf_s(std::string(ln).c_str(), "$var %31s %d %31s %63s",
                vtype, (unsigned)_countof(vtype), &width,
                id, (unsigned)_countof(id), name, (unsigned)_countof(name)) >= 4)
            {
                int r = static_cast<int>(sigs.size());
                sigs.push_back({ id, PathUtils::Utf8ToWide(name), r, 0.0f, 0 });
            }
        } else if (ln[0] == '#') {
            float t = std::strtof(std::string(ln.substr(1)).c_str(), nullptr);
            if (std::isfinite(t) && t >= curTime) {
                curTime = t;
                maxTime = std::max(maxTime, curTime);
            }
        } else if ((ln[0] == '0' || ln[0] == '1' || ln[0] == 'x' || ln[0] == 'z') && ln.size() >= 2) {
            int newVal = (ln[0] == '1') ? 1 : 0;
            std::string_view id = ln.substr(1);
            for (auto& s : sigs) {
                if (s.id == id) {
                    float yBase = -s.row * 8.0f;
                    float x0 = s.lastTime * 0.5f;
                    float x1 = curTime * 0.5f;
                    float y0 = yBase + (s.lastVal ? 3.5f : 0.0f);
                    float y1 = yBase + (newVal ? 3.5f : 0.0f);
                    if (x1 > x0) {
                        out.traces.push_back({ x0, y0, x1, y0, 0.30f, EdaLayerId::SchWire, s.row + 1 });
                    }
                    if (s.lastVal != newVal) {
                        out.traces.push_back({ x1, y0, x1, y1, 0.30f, EdaLayerId::SchWire, s.row + 1 });
                    }
                    s.lastTime = curTime;
                    s.lastVal = newVal;
                    break;
                }
            }
        }
    }

    if (sigs.empty()) return false;
    float xEnd = std::max(20.0f, maxTime * 0.5f);
    for (auto& s : sigs) {
        float yBase = -s.row * 8.0f;
        float x0 = s.lastTime * 0.5f;
        float y0 = yBase + (s.lastVal ? 3.5f : 0.0f);
        if (xEnd > x0) {
            out.traces.push_back({ x0, y0, xEnd, y0, 0.30f, EdaLayerId::SchWire, s.row + 1 });
        }
        out.labels.push_back({ -10.0f, yBase + 1.5f, 1.2f, EdaLayerId::SchLabel, s.name });
    }
    out.netCount = static_cast<int>(sigs.size());
    return !out.traces.empty();
}

} // namespace Formats
