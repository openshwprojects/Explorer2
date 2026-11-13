#pragma once
// HpglPlot.h — Hewlett-Packard Graphics Language (.hpgl / .plt) Vector Plotter Parser

namespace Formats {

inline bool ParseHpglPlotterBoard(std::string_view sv, ParsedEdaBoard& out) {
    out.kind = EdaDocKind::GerberLayer;
    out.formatTitle = L"HPGL Vector Plotter (.hpgl / .plt)";

    const float scale = 0.025f; // 40 plotter units = 1 mm
    float curX = 0.0f, curY = 0.0f;
    bool penDown = false;
    bool relMode = false;
    EdaLayerId curLayer = EdaLayerId::TopCopper;

    size_t pos = 0;
    while (pos < sv.size() && out.traces.size() < 40000) {
        while (pos < sv.size() && (sv[pos] == ' ' || sv[pos] == '\t' || sv[pos] == '\r' || sv[pos] == '\n' || sv[pos] == ';')) ++pos;
        if (pos + 2 > sv.size()) break;
        char c0 = static_cast<char>(std::toupper(static_cast<unsigned char>(sv[pos])));
        char c1 = static_cast<char>(std::toupper(static_cast<unsigned char>(sv[pos + 1])));
        pos += 2;

        size_t semi = sv.find(';', pos);
        size_t nextCmd = pos;
        while (nextCmd < sv.size() && nextCmd < semi && !(std::isalpha(static_cast<unsigned char>(sv[nextCmd])) &&
               nextCmd + 1 < sv.size() && std::isalpha(static_cast<unsigned char>(sv[nextCmd + 1])))) {
            ++nextCmd;
        }
        std::string args(sv.substr(pos, nextCmd - pos));
        pos = nextCmd;
        for (char& ch : args) if (ch == ',') ch = ' ';

        if (c0 == 'S' && c1 == 'P') {
            int pen = std::atoi(args.c_str());
            if (pen == 2) curLayer = EdaLayerId::BottomCopper;
            else if (pen == 3) curLayer = EdaLayerId::TopSilk;
            else if (pen == 4) curLayer = EdaLayerId::EdgeCuts;
            else curLayer = EdaLayerId::TopCopper;
        } else if (c0 == 'C' && c1 == 'I') {
            float r = std::strtof(args.c_str(), nullptr) * scale;
            if (r > 0.01f) {
                const int segs = 16;
                for (int s = 0; s < segs; ++s) {
                    float a0 = (s * 6.2831853f) / segs;
                    float a1 = ((s + 1) * 6.2831853f) / segs;
                    out.traces.push_back({
                        curX + std::cos(a0) * r, curY + std::sin(a0) * r,
                        curX + std::cos(a1) * r, curY + std::sin(a1) * r,
                        0.22f, curLayer, 0
                    });
                }
            }
        } else if ((c0 == 'P' && (c1 == 'U' || c1 == 'D' || c1 == 'A' || c1 == 'R'))) {
            if (c1 == 'U') penDown = false;
            else if (c1 == 'D') penDown = true;
            else if (c1 == 'A') relMode = false;
            else if (c1 == 'R') relMode = true;

            const char* cur = args.c_str();
            while (*cur) {
                char* next = nullptr;
                float x = std::strtof(cur, &next);
                if (next == cur) break;
                cur = next;
                float y = std::strtof(cur, &next);
                if (next == cur) break;
                cur = next;

                float nx = relMode ? (curX + x * scale) : (x * scale);
                float ny = relMode ? (curY + y * scale) : (y * scale);
                if (penDown) {
                    out.traces.push_back({ curX, curY, nx, ny, 0.24f, curLayer, 0 });
                }
                curX = nx;
                curY = ny;
            }
        }
    }
    return !out.traces.empty();
}

} // namespace Formats
