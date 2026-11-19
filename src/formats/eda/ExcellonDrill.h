#pragma once
// ExcellonDrill.h — Excellon NC Drill & Rout (.drl / .xln / .exc) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

static bool ParseExcellonDrillFile(std::string_view txt, ParsedEdaBoard& out) {
    // Reject binary data
    for (unsigned char c : txt) {
        if (c == 0) return false;
    }

    out.kind = EdaDocKind::ExcellonDrill;
    out.formatTitle = L"Excellon NC Drill (.drl)";

    bool metric = false;         // Excellon defaults to INCH (2.4) unless METRIC / M71 specified
    bool explicitUnit = false;
    bool trailingZeroOmit = false; // LZ (omit leading zeros) is most common; TZ pads trailing zeros
    int intDigits = 2;
    int decDigits = 4;
    bool hasFormatHint = false;

    std::unordered_map<int, float> toolDiamMm;
    int curTool = 0;
    float curDiamMm = 0.8f;
    float curX = 0.0f, curY = 0.0f;
    bool routMode = false;
    bool plungeDown = false;
    int interpG = 1; // 0=rapid, 1=linear, 2=CW arc, 3=CCW arc
    bool sawHeaderOrCmd = false;

    size_t pos = 0;
    while (pos < txt.size()) {
        size_t eol = txt.find_first_of("\r\n", pos);
        if (eol == std::string_view::npos) eol = txt.size();
        std::string_view rawLine = txt.substr(pos, eol - pos);
        pos = eol;
        while (pos < txt.size() && (txt[pos] == '\r' || txt[pos] == '\n')) ++pos;

        // Trim whitespace
        while (!rawLine.empty() && (rawLine.front() == ' ' || rawLine.front() == '\t')) rawLine.remove_prefix(1);
        while (!rawLine.empty() && (rawLine.back() == ' ' || rawLine.back() == '\t')) rawLine.remove_suffix(1);
        if (rawLine.empty()) continue;

        // Parse KiCad/Altium format comment hints like ;FILE_FORMAT=3:3 or ;FORMAT={3:3/ absolute / metric / ...}
        if (rawLine[0] == ';') {
            size_t ff = rawLine.find("FILE_FORMAT=");
            if (ff != std::string_view::npos && ff + 15 <= rawLine.size()) {
                int id = rawLine[ff + 12] - '0';
                char sep = rawLine[ff + 13];
                int dd = rawLine[ff + 14] - '0';
                if (sep == ':' && id >= 1 && id <= 6 && dd >= 1 && dd <= 6) {
                    intDigits = id;
                    decDigits = dd;
                    hasFormatHint = true;
                }
            }
            size_t fmtBr = rawLine.find("FORMAT={");
            if (fmtBr != std::string_view::npos && fmtBr + 11 <= rawLine.size()) {
                int id = rawLine[fmtBr + 8] - '0';
                char sep = rawLine[fmtBr + 9];
                int dd = rawLine[fmtBr + 10] - '0';
                if (sep == ':' && id >= 1 && id <= 6 && dd >= 1 && dd <= 6) {
                    intDigits = id;
                    decDigits = dd;
                    hasFormatHint = true;
                }
            }
            continue;
        }

        if (rawLine == "%" || rawLine == "M48" || rawLine == "M95" || rawLine.rfind("FMAT", 0) == 0 || rawLine == "G90") {
            sawHeaderOrCmd = true;
            continue;
        }
        if (rawLine == "M30" || rawLine == "M00") {
            sawHeaderOrCmd = true;
            break;
        }

        // Unit & zero suppression directives: METRIC,LZ,000.000 / INCH,TZ,00.0000 / M71 / M72
        if (rawLine.rfind("METRIC", 0) == 0 || rawLine == "M71") {
            sawHeaderOrCmd = true;
            metric = true;
            explicitUnit = true;
            if (!hasFormatHint) { intDigits = 3; decDigits = 3; }
            if (rawLine.find("TZ") != std::string_view::npos) trailingZeroOmit = true;
            else if (rawLine.find("LZ") != std::string_view::npos) trailingZeroOmit = false;
            size_t zMask = rawLine.find("00");
            if (zMask != std::string_view::npos) {
                size_t dot = rawLine.find('.', zMask);
                if (dot != std::string_view::npos) {
                    int id = static_cast<int>(dot - zMask);
                    size_t ze = dot + 1;
                    while (ze < rawLine.size() && rawLine[ze] == '0') ++ze;
                    int dd = static_cast<int>(ze - (dot + 1));
                    if (id >= 1 && id <= 6 && dd >= 1 && dd <= 6) {
                        intDigits = id; decDigits = dd; hasFormatHint = true;
                    }
                }
            }
            continue;
        }
        if (rawLine.rfind("INCH", 0) == 0 || rawLine == "M72") {
            sawHeaderOrCmd = true;
            metric = false;
            explicitUnit = true;
            if (!hasFormatHint) { intDigits = 2; decDigits = 4; }
            if (rawLine.find("TZ") != std::string_view::npos) trailingZeroOmit = true;
            else if (rawLine.find("LZ") != std::string_view::npos) trailingZeroOmit = false;
            size_t zMask = rawLine.find("00");
            if (zMask != std::string_view::npos) {
                size_t dot = rawLine.find('.', zMask);
                if (dot != std::string_view::npos) {
                    int id = static_cast<int>(dot - zMask);
                    size_t ze = dot + 1;
                    while (ze < rawLine.size() && rawLine[ze] == '0') ++ze;
                    int dd = static_cast<int>(ze - (dot + 1));
                    if (id >= 1 && id <= 6 && dd >= 1 && dd <= 6) {
                        intDigits = id; decDigits = dd; hasFormatHint = true;
                    }
                }
            }
            continue;
        }

        // Rout plunge / retract / drill mode
        if (rawLine == "M15") { plungeDown = true; routMode = true; continue; }
        if (rawLine == "M16" || rawLine == "M17") { plungeDown = false; continue; }
        if (rawLine == "G05") { routMode = false; plungeDown = false; continue; }

        // Tool definition or selection: T01C0.800 or T01
        if (rawLine[0] == 'T' && rawLine.size() >= 2 && rawLine[1] >= '0' && rawLine[1] <= '9') {
            sawHeaderOrCmd = true;
            size_t p = 1;
            int tNum = 0;
            while (p < rawLine.size() && rawLine[p] >= '0' && rawLine[p] <= '9') {
                tNum = tNum * 10 + (rawLine[p] - '0');
                ++p;
            }
            size_t cPos = rawLine.find('C', p);
            if (cPos != std::string_view::npos && cPos + 1 < rawLine.size()) {
                size_t ce = cPos + 1;
                while (ce < rawLine.size() && ((rawLine[ce] >= '0' && rawLine[ce] <= '9') || rawLine[ce] == '.')) ++ce;
                char buf[64] = {};
                std::memcpy(buf, rawLine.data() + cPos + 1, std::min<size_t>(ce - (cPos + 1), 63));
                float d = std::strtof(buf, nullptr);
                if (!explicitUnit && d >= 0.1f) {
                    // Heuristic only if header omitted unit before tool def
                }
                if (!metric) d *= 25.4f;
                if (d > 0.02f && d <= 50.0f) {
                    toolDiamMm[tNum] = d;
                    curTool = tNum;
                    curDiamMm = d;
                }
            } else if (tNum == 0) {
                curTool = 0;
            } else {
                curTool = tNum;
                auto it = toolDiamMm.find(tNum);
                if (it != toolDiamMm.end()) curDiamMm = it->second;
            }
            continue;
        }

        const double unitMm = metric ? 1.0 : 25.4;
        auto readFieldAfter = [&](std::string_view s, char letter, float& outMm) -> bool {
            size_t lp = s.find(letter);
            if (lp == std::string_view::npos) return false;
            size_t vs = lp + 1;
            size_t ve = vs;
            while (ve < s.size() && ((s[ve] >= '0' && s[ve] <= '9') || s[ve] == '-' || s[ve] == '+' || s[ve] == '.')) ++ve;
            if (ve == vs) return false;
            // In Excellon TZ, digits are left-aligned (omit trailing zeros); in LZ (default), digits are right-aligned!
            // Note: Some Excellon writers write "LZ" meaning "Leading Zeros INCLUDED" (= omit trailing zeros) — wait:
            // Standard Excellon:
            //   INCH,TZ = Trailing Zeros INCLUDED (= leading zeros omitted, right-aligned decimal!)
            //   INCH,LZ = Leading Zeros INCLUDED (= trailing zeros omitted, left-aligned!)
            //Wait: KiCad writes `METRIC,TZ` when trailing zeros are KEPT (i.e. right-aligned standard integer*10^-dec), OR decimal points `X12.34Y56.78`!
            // Let's handle explicit decimal points directly, and for integer strings:
            // If `trailingZeroOmit` is true and string length < intDigits+decDigits, pad right; otherwise standard *10^-decDigits!
            return ParseGerberCoordField(s.substr(vs, ve - vs), intDigits, decDigits, trailingZeroOmit, unitMm, outMm);
        };

        // Check for G85 slot: X...Y...G85X...Y...
        size_t g85Pos = rawLine.find("G85");
        if (g85Pos != std::string_view::npos) {
            std::string_view firstHalf = rawLine.substr(0, g85Pos);
            std::string_view secondHalf = rawLine.substr(g85Pos + 3);
            float x1 = curX, y1 = curY, x2 = curX, y2 = curY;
            bool has1 = readFieldAfter(firstHalf, 'X', x1) | readFieldAfter(firstHalf, 'Y', y1);
            bool has2 = readFieldAfter(secondHalf, 'X', x2) | readFieldAfter(secondHalf, 'Y', y2);
            if (has1 && has2) {
                float d = std::max(0.15f, curDiamMm);
                out.vias.push_back({ x1, y1, d * 1.35f, d, curTool });
                out.vias.push_back({ x2, y2, d * 1.35f, d, curTool });
                out.traces.push_back({ x1, y1, x2, y2, d, EdaLayerId::Drills, curTool });
                curX = x2; curY = y2;
            }
            continue;
        }

        // Check for G00 / G01 / G02 / G03 rout prefix
        std::string_view coordPart = rawLine;
        if (coordPart.rfind("G00", 0) == 0) { routMode = true; plungeDown = false; interpG = 0; coordPart.remove_prefix(3); }
        else if (coordPart.rfind("G01", 0) == 0) { routMode = true; interpG = 1; coordPart.remove_prefix(3); }
        else if (coordPart.rfind("G02", 0) == 0) { routMode = true; interpG = 2; coordPart.remove_prefix(3); }
        else if (coordPart.rfind("G03", 0) == 0) { routMode = true; interpG = 3; coordPart.remove_prefix(3); }

        // Repeat drill command: R<count>X<dx>Y<dy>
        if (coordPart.size() >= 2 && coordPart[0] == 'R' && coordPart[1] >= '0' && coordPart[1] <= '9') {
            size_t rp = 1;
            int repCount = 0;
            while (rp < coordPart.size() && coordPart[rp] >= '0' && coordPart[rp] <= '9') {
                repCount = repCount * 10 + (coordPart[rp] - '0');
                ++rp;
            }
            float dx = 0.0f, dy = 0.0f;
            readFieldAfter(coordPart.substr(rp), 'X', dx);
            readFieldAfter(coordPart.substr(rp), 'Y', dy);
            repCount = std::min(repCount, 2000);
            float d = std::max(0.15f, curDiamMm);
            for (int r = 0; r < repCount; ++r) {
                curX += dx;
                curY += dy;
                out.vias.push_back({ curX, curY, d * 1.4f, d, curTool });
            }
            continue;
        }

        float nx = curX, ny = curY;
        bool hasX = readFieldAfter(coordPart, 'X', nx);
        bool hasY = readFieldAfter(coordPart, 'Y', ny);
        if (hasX || hasY) {
            float d = std::max(0.15f, curDiamMm);
            if (routMode) {
                if (plungeDown && interpG >= 1) {
                    out.traces.push_back({ curX, curY, nx, ny, d, EdaLayerId::Drills, curTool });
                }
            } else {
                out.vias.push_back({ nx, ny, d * 1.4f, d, curTool });
            }
            curX = nx;
            curY = ny;
        }
    }

    if (!sawHeaderOrCmd && toolDiamMm.empty()) {
        out = ParsedEdaBoard{};
        return false;
    }
    if (out.vias.empty() && out.traces.empty() && out.pads.empty()) {
        out = ParsedEdaBoard{};
        return false;
    }
    out.netCount = std::max<size_t>(1, toolDiamMm.size());
    FinalizeBoardBounds(out);
    out.valid = true;
    return true;
}
