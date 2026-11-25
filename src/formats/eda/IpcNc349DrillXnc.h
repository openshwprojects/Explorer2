#pragma once
// IpcNc349DrillXnc.h — IPC-NC-349 / Sieb & Meyer CNC PCB Drill & Rout Program (.xnc)
// Parses `%` tool table (`T01C<diam>`, `METRIC`/`INCH`), `G00` rapid moves, `G01`/`M15`/`M16` routed
// milling paths (`EdgeCuts`), and plated/non-plated drill hits (`PadsThrough` + `vias`), ending at `M30`/`M00`.

namespace Formats {

namespace DetailIpcXnc {

inline bool ParseXncAxisValue(std::string_view tok, double unitToMm, float& outMm) {
    if (tok.empty()) return false;
    const bool hasDot = (tok.find('.') != std::string_view::npos);
    char buf[48] = {};
    const size_t n = std::min<size_t>(tok.size(), 47u);
    std::memcpy(buf, tok.data(), n);
    char* endp = nullptr;
    double v = std::strtod(buf, &endp);
    if (endp == buf || !std::isfinite(v)) return false;
    if (!hasDot) {
        // Standard IPC-NC-349 2.4 inch or 3.3 metric integer scaling when decimal omitted
        v *= (unitToMm > 10.0) ? 1e-4 : 1e-3;
    }
    outMm = static_cast<float>(v * unitToMm);
    return true;
}

inline bool ExtractCoordPair(std::string_view line, double unitToMm, float& ioX, float& ioY, bool& outMoved) {
    outMoved = false;
    size_t i = 0u;
    while (i < line.size()) {
        const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(line[i])));
        if (c == 'X' || c == 'Y') {
            ++i;
            const size_t s = i;
            if (i < line.size() && (line[i] == '+' || line[i] == '-')) ++i;
            while (i < line.size() && (std::isdigit(static_cast<unsigned char>(line[i])) || line[i] == '.')) ++i;
            if (i == s) return false;
            float val = 0.0f;
            if (!ParseXncAxisValue(line.substr(s, i - s), unitToMm, val)) return false;
            if (c == 'X') ioX = val;
            else          ioY = val;
            outMoved = true;
        } else {
            ++i;
        }
    }
    return true;
}

} // namespace DetailIpcXnc

inline bool ParseIpcNc349DrillXnc(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    std::string_view sv;
    if (!FormatPrologue::PrepareUtf8TextNoNul(data, size, sv, 16u, (32u << 20))) return false;

    double unitToMm = 1.0; // IPC-NC-349 defaults to metric unless INCH specified
    std::unordered_map<int, float> toolDiamMm;
    int activeTool = 1;
    bool inHeader = false;
    bool routPlunged = false;
    int gMode = 5; // G05 drill mode default; G00 rapid, G01/G02/G03 rout
    bool sawToolOrHeader = false;
    bool sawEndProgram = false;
    float curX = 0.0f, curY = 0.0f;

    out = ParsedEdaBoard{};
    out.kind = EdaDocKind::KiCadPcb;

    size_t linePos = 0u;
    std::string_view rawLine;
    while (ReaderText::NextLineSv(sv, linePos, rawLine)) {
        // Strip `;` comments
        const size_t sc = rawLine.find(';');
        if (sc != std::string_view::npos) rawLine = rawLine.substr(0u, sc);
        std::string_view line = ReaderText::TrimSv(rawLine);
        if (line.empty()) continue;

        const std::string up = ReaderText::ToUpperAscii(line);
        const std::string_view upSv(up);
        if (upSv == "%") {
            inHeader = !inHeader;
            sawToolOrHeader = true;
            continue;
        }
        if (upSv == "M48") {
            inHeader = true;
            sawToolOrHeader = true;
            continue;
        }
        if (upSv == "M95") {
            inHeader = false;
            continue;
        }
        if (upSv == "M30" || upSv == "M00") {
            sawEndProgram = true;
            break;
        }
        if (upSv.rfind("METRIC", 0) == 0 || upSv == "M71") {
            unitToMm = 1.0;
            sawToolOrHeader = true;
            continue;
        }
        if (upSv.rfind("INCH", 0) == 0 || upSv == "M72") {
            unitToMm = 25.4;
            sawToolOrHeader = true;
            continue;
        }
        if (upSv.rfind("FMAT", 0) == 0 || upSv.rfind("ICI", 0) == 0 || upSv == "G90") {
            sawToolOrHeader = true;
            continue;
        }

        // Tool definition or selection: `T01C0.800` or `T01`
        if (upSv.front() == 'T' && upSv.size() >= 2u && std::isdigit(static_cast<unsigned char>(upSv[1]))) {
            size_t p = 1u;
            while (p < upSv.size() && std::isdigit(static_cast<unsigned char>(upSv[p]))) ++p;
            int tNum = 0;
            if (!ReaderText::ParseIntSv(upSv.substr(1u, p - 1u), tNum) || tNum < 0 || tNum > 999) return false;
            activeTool = tNum;
            sawToolOrHeader = true;
            const size_t cPos = upSv.find('C', p);
            if (cPos != std::string_view::npos) {
                size_t ce = cPos + 1u;
                while (ce < upSv.size() && (std::isdigit(static_cast<unsigned char>(upSv[ce])) || upSv[ce] == '.')) ++ce;
                double dVal = 0.0;
                if (!ReaderText::ParseDoubleSv(upSv.substr(cPos + 1u, ce - cPos - 1u), dVal) || dVal <= 0.0) {
                    return false;
                }
                toolDiamMm[tNum] = std::clamp(static_cast<float>(dVal * unitToMm), 0.1f, 25.0f);
            }
            continue;
        }

        if (upSv == "M15") { routPlunged = true;  continue; }
        if (upSv == "M16" || upSv == "M17") { routPlunged = false; continue; }
        if (upSv == "G05") { gMode = 5; routPlunged = false; continue; }

        std::string_view coordPart = upSv;
        if (upSv.rfind("G00", 0) == 0)      { gMode = 0; coordPart = upSv.substr(3u); }
        else if (upSv.rfind("G01", 0) == 0) { gMode = 1; coordPart = upSv.substr(3u); }
        else if (upSv.rfind("G02", 0) == 0) { gMode = 2; coordPart = upSv.substr(3u); }
        else if (upSv.rfind("G03", 0) == 0) { gMode = 3; coordPart = upSv.substr(3u); }
        else if (upSv.rfind("G85", 0) == 0) { gMode = 85; coordPart = upSv; }

        if (coordPart.empty()) continue;

        // Check G85 slot syntax `X1Y1G85X2Y2`
        const size_t g85Pos = upSv.find("G85");
        const float diam = toolDiamMm.count(activeTool) ? toolDiamMm[activeTool] : 0.8f;
        if (g85Pos != std::string_view::npos) {
            float x1 = curX, y1 = curY, x2 = curX, y2 = curY;
            bool m1 = false, m2 = false;
            if (!DetailIpcXnc::ExtractCoordPair(upSv.substr(0u, g85Pos), unitToMm, x1, y1, m1) ||
                !DetailIpcXnc::ExtractCoordPair(upSv.substr(g85Pos + 3u), unitToMm, x2, y2, m2) || !m1 || !m2) {
                return false;
            }
            out.traces.push_back({ x1, y1, x2, y2, diam, EdaLayerId::EdgeCuts, 0 });
            curX = x2; curY = y2;
            continue;
        }

        const float prevX = curX, prevY = curY;
        bool moved = false;
        if (!DetailIpcXnc::ExtractCoordPair(coordPart, unitToMm, curX, curY, moved)) return false;
        if (!moved) return false; // Unrecognized garbage line!

        if (gMode == 1 || gMode == 2 || gMode == 3 || routPlunged) {
            if (routPlunged || gMode >= 1) {
                out.traces.push_back({ prevX, prevY, curX, curY, diam, EdaLayerId::EdgeCuts, 0 });
            }
        } else if (gMode == 5 && !inHeader) {
            EdaViaPrim v{};
            v.x = curX;
            v.y = curY;
            v.drill = diam;
            v.diameter = diam + 0.4f;
            v.netId = activeTool;
            out.vias.push_back(v);
            out.circles.push_back({ curX, curY, diam * 0.5f, 0.12f, false, EdaLayerId::PadsThrough });
        }
    }

    if (!sawToolOrHeader || !sawEndProgram || (out.vias.empty() && out.traces.empty())) {
        out = {};
        return false;
    }

    out.netCount = toolDiamMm.size();
    wchar_t title[180] = {};
    swprintf_s(title, L"IPC-NC-349 CNC Drill & Rout (.xnc, %zu hits, %zu rout segs, %zu tools)",
               out.vias.size(), out.traces.size(), toolDiamMm.size());
    out.formatTitle = title;
    FinalizeBoardBounds(out);
    out.valid = true;
    return true;
}

} // namespace Formats
