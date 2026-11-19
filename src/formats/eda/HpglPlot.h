#pragma once
// HpglPlot.h — Full Hewlett-Packard Graphics Language HP-GL & HP-GL/2 (.hpgl / .plt) Parser

namespace Formats {

inline bool ParseHpglPlotterBoard(std::string_view sv, ParsedEdaBoard& out) {
    if (sv.size() < 4) return false;

    // Strip UTF-8 BOM if present and reject binary NUL bytes
    if (sv.size() >= 3 &&
        static_cast<uint8_t>(sv[0]) == 0xEFu &&
        static_cast<uint8_t>(sv[1]) == 0xBBu &&
        static_cast<uint8_t>(sv[2]) == 0xBFu)
    {
        sv.remove_prefix(3);
    }
    for (unsigned char c : sv) {
        if (c == 0) return false; // Binary files (e.g. CAD/3D binary .plt) must not match text HPGL
    }

    out.kind = EdaDocKind::GerberLayer;

    constexpr float kPlotterUnitMm = 0.025f; // 1 HPGL plotter unit = 0.025 mm (40 units/mm, 1016 units/inch)

    // Plotter state machine
    float curX = 0.0f, curY = 0.0f; // Always stored in mm!
    bool penDown = false;
    bool relMode = false;
    int curPen = 1;
    EdaLayerId curLayer = EdaLayerId::TopCopper;
    float penWidthsMm[9] = { 0.24f, 0.25f, 0.25f, 0.22f, 0.30f, 0.22f, 0.20f, 0.22f, 0.22f };
    bool widthRelativeMode = false; // WU0 = metric mm, WU1 = % of P1-P2 diagonal

    // IP / SC user coordinate scaling state (in plotter units)
    float p1x = 0.0f, p1y = 0.0f, p2x = 10000.0f, p2y = 7000.0f;
    bool scEnabled = false;
    int scType = 0;
    float scXMin = 0.0f, scXMax = 10000.0f, scYMin = 0.0f, scYMax = 7000.0f;

    // Text state
    char labelTerm = '\x03'; // Default ETX (0x03)
    float charHeightMm = 1.8f;

    // Polygon Mode (PM0 / PM1 / PM2, EP, FP)
    bool inPolyMode = false;
    std::vector<std::vector<std::pair<float, float>>> polyContours;
    std::vector<std::pair<float, float>> curContour;

    auto resetDefaults = [&]() {
        relMode = false;
        penDown = false;
        widthRelativeMode = false;
        scEnabled = false;
        labelTerm = '\x03';
        charHeightMm = 1.8f;
        inPolyMode = false;
        polyContours.clear();
        curContour.clear();
    };

    auto penToLayer = [](int pen) noexcept -> EdaLayerId {
        switch (pen) {
        case 1: return EdaLayerId::TopCopper;
        case 2: return EdaLayerId::BottomCopper;
        case 3: return EdaLayerId::TopSilk;
        case 4: return EdaLayerId::EdgeCuts;
        case 5: return EdaLayerId::InnerCopper;
        case 6: return EdaLayerId::BottomSilk;
        case 7: return EdaLayerId::SchWire;
        case 8: return EdaLayerId::SchSymbol;
        default: return EdaLayerId::TopCopper;
        }
    };

    auto curStrokeMm = [&]() noexcept -> float {
        int idx = (curPen >= 0 && curPen <= 8) ? curPen : 1;
        return std::clamp(penWidthsMm[idx], 0.05f, 5.0f);
    };

    auto userScaleX = [&]() noexcept -> float {
        if (!scEnabled) return kPlotterUnitMm;
        if (scType == 2) return scXMax * kPlotterUnitMm; // Point-factor: scXMax holds xFactor
        float spanU = scXMax - scXMin;
        if (std::fabs(spanU) < 1e-9f) return kPlotterUnitMm;
        if (scType == 1) {
            float sx = (p2x - p1x) / spanU;
            float sy = (std::fabs(scYMax - scYMin) > 1e-9f) ? ((p2y - p1y) / (scYMax - scYMin)) : sx;
            float iso = std::min(std::fabs(sx), std::fabs(sy));
            return std::copysign(iso, sx) * kPlotterUnitMm;
        }
        return ((p2x - p1x) / spanU) * kPlotterUnitMm;
    };

    auto userScaleY = [&]() noexcept -> float {
        if (!scEnabled) return kPlotterUnitMm;
        if (scType == 2) return scYMax * kPlotterUnitMm; // Point-factor: scYMax holds yFactor
        float spanU = scYMax - scYMin;
        if (std::fabs(spanU) < 1e-9f) return kPlotterUnitMm;
        if (scType == 1) {
            float sx = (std::fabs(scXMax - scXMin) > 1e-9f) ? ((p2x - p1x) / (scXMax - scXMin)) : 1.0f;
            float sy = (p2y - p1y) / spanU;
            float iso = std::min(std::fabs(sx), std::fabs(sy));
            return std::copysign(iso, sy) * kPlotterUnitMm;
        }
        return ((p2y - p1y) / spanU) * kPlotterUnitMm;
    };

    auto toAbsMmX = [&](float ux) noexcept -> float {
        if (!scEnabled) return ux * kPlotterUnitMm;
        return p1x * kPlotterUnitMm + (ux - scXMin) * userScaleX();
    };
    auto toAbsMmY = [&](float uy) noexcept -> float {
        if (!scEnabled) return uy * kPlotterUnitMm;
        return p1y * kPlotterUnitMm + (uy - scYMin) * userScaleY();
    };
    auto toRelMmX = [&](float dux) noexcept -> float { return dux * userScaleX(); };
    auto toRelMmY = [&](float duy) noexcept -> float { return duy * userScaleY(); };

    auto emitSeg = [&](float x1, float y1, float x2, float y2) {
        if (curPen == 0 || out.traces.size() >= 60000) return;
        out.traces.push_back({ x1, y1, x2, y2, curStrokeMm(), curLayer, curPen });
    };

    auto emitRectMm = [&](float x0, float y0, float x1, float y1, bool filled) {
        emitSeg(x0, y0, x1, y0);
        emitSeg(x1, y0, x1, y1);
        emitSeg(x1, y1, x0, y1);
        emitSeg(x0, y1, x0, y0);
        if (filled) {
            float loX = std::min(x0, x1), hiX = std::max(x0, x1);
            float loY = std::min(y0, y1), hiY = std::max(y0, y1);
            float step = std::max(0.45f, (hiY - loY) / 10.0f);
            for (float y = loY + step; y < hiY - 0.05f && out.traces.size() < 60000; y += step) {
                emitSeg(loX, y, hiX, y);
            }
        }
    };

    auto parseFloats = [](std::string_view argSv, std::vector<float>& vals) {
        vals.clear();
        std::string tmp(argSv);
        for (char& ch : tmp) {
            if (ch == ',') ch = ' ';
        }
        const char* p = tmp.c_str();
        while (*p) {
            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
            if (!*p) break;
            char* endp = nullptr;
            float v = std::strtof(p, &endp);
            if (endp == p) break;
            vals.push_back(v);
            p = endp;
        }
    };

    size_t validCmdCount = 0;
    size_t unknownCmdCount = 0;
    bool hasExplicitMarker = (sv.find(';') != std::string_view::npos);
    std::set<int> usedPens;

    size_t pos = 0;
    std::vector<float> nums;
    nums.reserve(32);

    while (pos < sv.size() && out.traces.size() < 60000) {
        // Skip whitespace & semicolons
        while (pos < sv.size() &&
               (sv[pos] == ' ' || sv[pos] == '\t' || sv[pos] == '\r' || sv[pos] == '\n' || sv[pos] == ';'))
        {
            ++pos;
        }
        if (pos >= sv.size()) break;

        // Handle HP-GL/2 PCL / plotter escape sequences (\x1B%0B, \x1B%1B, \x1B.(, \x1B.Y, \x1B%0A, etc.)
        if (sv[pos] == '\x1B') {
            hasExplicitMarker = true;
            ++pos;
            if (pos < sv.size() && sv[pos] == '%') {
                ++pos;
                while (pos < sv.size() && (std::isdigit(static_cast<unsigned char>(sv[pos])) || sv[pos] == '-')) ++pos;
                if (pos < sv.size() && std::isalpha(static_cast<unsigned char>(sv[pos]))) ++pos;
            } else if (pos < sv.size() && sv[pos] == '.') {
                ++pos;
                if (pos < sv.size()) ++pos;
                while (pos < sv.size() && sv[pos] != ':' && sv[pos] != '\r' && sv[pos] != '\n') ++pos;
                if (pos < sv.size() && sv[pos] == ':') ++pos;
            } else {
                while (pos < sv.size() && !std::isalpha(static_cast<unsigned char>(sv[pos]))) ++pos;
                if (pos < sv.size()) ++pos;
            }
            continue;
        }

        if (pos + 2 > sv.size() ||
            !std::isalpha(static_cast<unsigned char>(sv[pos])) ||
            !std::isalpha(static_cast<unsigned char>(sv[pos + 1])))
        {
            // Non-HPGL token at command boundary -> strong sign of non-HPGL text if repeated
            ++unknownCmdCount;
            if (unknownCmdCount > 12 && validCmdCount < unknownCmdCount) return false;
            while (pos < sv.size() && sv[pos] != ';' && sv[pos] != '\n') ++pos;
            continue;
        }

        const char c0 = static_cast<char>(std::toupper(static_cast<unsigned char>(sv[pos])));
        const char c1 = static_cast<char>(std::toupper(static_cast<unsigned char>(sv[pos + 1])));
        pos += 2;

        // Special handling for DT (Define Label Terminator) & LB (Label) BEFORE generic argument scanning!
        if (c0 == 'D' && c1 == 'T') {
            ++validCmdCount;
            if (pos >= sv.size() || sv[pos] == ';') {
                labelTerm = '\x03';
            } else {
                labelTerm = sv[pos++];
                // Optional mode parameter ",1" before ';'
                while (pos < sv.size() && sv[pos] != ';' && sv[pos] != '\n') ++pos;
            }
            if (pos < sv.size() && sv[pos] == ';') ++pos;
            continue;
        }
        if (c0 == 'L' && c1 == 'B') {
            ++validCmdCount;
            size_t endLbl = sv.find(labelTerm, pos);
            std::string_view rawTxt = (endLbl == std::string_view::npos)
                ? sv.substr(pos)
                : sv.substr(pos, endLbl - pos);
            pos = (endLbl == std::string_view::npos) ? sv.size() : (endLbl + 1);

            std::string cleanTxt;
            cleanTxt.reserve(std::min<size_t>(rawTxt.size(), 256));
            for (unsigned char ch : rawTxt) {
                if (ch == '\r' || ch == '\n') {
                    if (!cleanTxt.empty() && cleanTxt.back() != ' ') cleanTxt.push_back(' ');
                } else if (ch >= 0x20u && cleanTxt.size() < 256) {
                    cleanTxt.push_back(static_cast<char>(ch));
                }
            }
            if (!cleanTxt.empty() && out.labels.size() < 4096) {
                out.labels.push_back({ curX, curY, std::clamp(charHeightMm, 0.6f, 8.0f),
                                       EdaLayerId::SchLabel, PathUtils::Utf8ToWide(cleanTxt) });
                curX += static_cast<float>(cleanTxt.size()) * charHeightMm * 0.65f;
            }
            continue;
        }
        if (c0 == 'C' && c1 == 'O') {
            // HP-GL/2 Comment: CO "text";
            ++validCmdCount;
            size_t semi = sv.find(';', pos);
            pos = (semi == std::string_view::npos) ? sv.size() : (semi + 1);
            continue;
        }

        // Scan numeric/parameter arguments up to ';' or next 2-letter HPGL mnemonic
        // (Taking care NOT to split scientific notation '1.5E-3' on 'E'!)
        size_t nextCmd = pos;
        while (nextCmd < sv.size() && sv[nextCmd] != ';' && sv[nextCmd] != '\x1B') {
            unsigned char ch = static_cast<unsigned char>(sv[nextCmd]);
            if (std::isalpha(ch)) {
                // Allow 'E'/'e' exponent directly after a digit or '.' followed by '+/-'/digit
                if ((ch == 'E' || ch == 'e') && nextCmd > pos &&
                    (std::isdigit(static_cast<unsigned char>(sv[nextCmd - 1])) || sv[nextCmd - 1] == '.') &&
                    nextCmd + 1 < sv.size() &&
                    (std::isdigit(static_cast<unsigned char>(sv[nextCmd + 1])) || sv[nextCmd + 1] == '+' || sv[nextCmd + 1] == '-'))
                {
                    ++nextCmd;
                    continue;
                }
                if (nextCmd + 1 < sv.size() && std::isalpha(static_cast<unsigned char>(sv[nextCmd + 1]))) {
                    break;
                }
                // Prose word with >2 letters -> invalid argument token!
                break;
            }
            ++nextCmd;
        }

        std::string_view argView = sv.substr(pos, nextCmd - pos);
        pos = nextCmd;

        const uint16_t mnem = static_cast<uint16_t>((static_cast<uint16_t>(c0) << 8) | static_cast<uint16_t>(c1));
        auto M = [](char a, char b) constexpr noexcept -> uint16_t {
            return static_cast<uint16_t>((static_cast<uint16_t>(a) << 8) | static_cast<uint16_t>(b));
        };

        switch (mnem) {
        case M('I','N'):
            ++validCmdCount;
            hasExplicitMarker = true;
            resetDefaults();
            curX = 0.0f; curY = 0.0f;
            curPen = 1; curLayer = penToLayer(1);
            break;

        case M('D','F'):
            ++validCmdCount;
            hasExplicitMarker = true;
            resetDefaults();
            break;

        case M('I','P'):
            ++validCmdCount;
            hasExplicitMarker = true;
            parseFloats(argView, nums);
            if (nums.size() >= 4) {
                p1x = nums[0]; p1y = nums[1]; p2x = nums[2]; p2y = nums[3];
            } else if (nums.size() >= 2) {
                float dx = p2x - p1x, dy = p2y - p1y;
                p1x = nums[0]; p1y = nums[1];
                p2x = p1x + dx; p2y = p1y + dy;
            } else {
                p1x = 0.0f; p1y = 0.0f; p2x = 10000.0f; p2y = 7000.0f;
            }
            break;

        case M('S','C'):
            ++validCmdCount;
            hasExplicitMarker = true;
            parseFloats(argView, nums);
            if (nums.size() >= 4) {
                scEnabled = true;
                scXMin = nums[0]; scXMax = nums[1];
                scYMin = nums[2]; scYMax = nums[3];
                scType = (nums.size() >= 5) ? static_cast<int>(nums[4]) : 0;
            } else {
                scEnabled = false;
            }
            break;

        case M('S','P'):
            ++validCmdCount;
            hasExplicitMarker = true;
            parseFloats(argView, nums);
            curPen = nums.empty() ? 1 : std::clamp(static_cast<int>(nums[0]), 0, 8);
            if (curPen > 0) {
                curLayer = penToLayer(curPen);
                usedPens.insert(curPen);
            }
            break;

        case M('W','U'):
            ++validCmdCount;
            parseFloats(argView, nums);
            widthRelativeMode = (!nums.empty() && static_cast<int>(nums[0]) == 1);
            break;

        case M('P','W'):
            ++validCmdCount;
            parseFloats(argView, nums);
            if (!nums.empty()) {
                float wVal = nums[0];
                if (widthRelativeMode) {
                    float diagMm = std::hypot(p2x - p1x, p2y - p1y) * kPlotterUnitMm;
                    wVal = (wVal * 0.001f) * diagMm;
                }
                wVal = std::clamp(wVal, 0.05f, 5.0f);
                if (nums.size() >= 2) {
                    int targetPen = std::clamp(static_cast<int>(nums[1]), 0, 8);
                    penWidthsMm[targetPen] = wVal;
                } else {
                    for (float& pw : penWidthsMm) pw = wVal;
                }
            }
            break;

        case M('S','I'): // Absolute character size (cm width, cm height)
            ++validCmdCount;
            parseFloats(argView, nums);
            if (nums.size() >= 2) {
                charHeightMm = std::clamp(std::fabs(nums[1]) * 10.0f, 0.6f, 12.0f);
            } else {
                charHeightMm = 1.8f;
            }
            break;

        case M('P','U'):
        case M('P','D'):
        case M('P','A'):
        case M('P','R'): {
            ++validCmdCount;
            if (c1 == 'U') penDown = false;
            else if (c1 == 'D') penDown = true;
            else if (c1 == 'A') relMode = false;
            else if (c1 == 'R') relMode = true;

            parseFloats(argView, nums);
            if (c1 == 'U' && inPolyMode && !curContour.empty() && nums.empty()) {
                polyContours.push_back(std::move(curContour));
                curContour.clear();
            }
            for (size_t i = 0; i + 1 < nums.size(); i += 2) {
                float nx = relMode ? (curX + toRelMmX(nums[i]))     : toAbsMmX(nums[i]);
                float ny = relMode ? (curY + toRelMmY(nums[i + 1])) : toAbsMmY(nums[i + 1]);
                if (inPolyMode) {
                    if (!penDown && !curContour.empty()) {
                        polyContours.push_back(std::move(curContour));
                        curContour.clear();
                    }
                    if (curContour.empty()) curContour.emplace_back(curX, curY);
                    curContour.emplace_back(nx, ny);
                } else if (penDown) {
                    emitSeg(curX, curY, nx, ny);
                }
                curX = nx;
                curY = ny;
            }
            break;
        }

        case M('C','I'): { // Circle: CI radius[,chordAngle]
            ++validCmdCount;
            parseFloats(argView, nums);
            if (!nums.empty()) {
                float rx = std::fabs(toRelMmX(nums[0]));
                float ry = std::fabs(toRelMmY(nums[0]));
                float chordDeg = (nums.size() >= 2 && nums[1] >= 1.0f) ? nums[1] : 10.0f;
                int segs = std::clamp(static_cast<int>(std::ceil(360.0f / chordDeg)), 12, 96);
                if (rx > 0.01f && ry > 0.01f) {
                    if (!inPolyMode && std::fabs(rx - ry) < 1e-3f && out.circles.size() < 16384) {
                        out.circles.push_back({ curX, curY, rx, curStrokeMm(), false, curLayer });
                    }
                    for (int s = 0; s < segs; ++s) {
                        float a0 = (static_cast<float>(s) * 6.2831853f) / static_cast<float>(segs);
                        float a1 = (static_cast<float>(s + 1) * 6.2831853f) / static_cast<float>(segs);
                        emitSeg(curX + std::cos(a0) * rx, curY + std::sin(a0) * ry,
                                curX + std::cos(a1) * rx, curY + std::sin(a1) * ry);
                    }
                }
            }
            break;
        }

        case M('A','A'): // Arc Absolute: AA xc,yc,sweepDeg[,chordDeg]
        case M('A','R'): { // Arc Relative: AR dxc,dyc,sweepDeg[,chordDeg]
            ++validCmdCount;
            parseFloats(argView, nums);
            if (nums.size() >= 3) {
                float cx = (c1 == 'R') ? (curX + toRelMmX(nums[0])) : toAbsMmX(nums[0]);
                float cy = (c1 == 'R') ? (curY + toRelMmY(nums[1])) : toAbsMmY(nums[1]);
                float sweepDeg = std::clamp(nums[2], -360.0f, 360.0f);
                float chordDeg = (nums.size() >= 4 && nums[3] >= 1.0f) ? nums[3] : 10.0f;
                float r = std::hypot(curX - cx, curY - cy);
                if (r > 0.01f && std::fabs(sweepDeg) > 0.05f) {
                    float a0 = std::atan2(curY - cy, curX - cx);
                    float sweepRad = sweepDeg * 3.14159265f / 180.0f;
                    int segs = std::clamp(static_cast<int>(std::ceil(std::fabs(sweepDeg) / chordDeg)), 4, 96);
                    float px = curX, py = curY;
                    for (int s = 1; s <= segs; ++s) {
                        float ang = a0 + sweepRad * (static_cast<float>(s) / static_cast<float>(segs));
                        float nx = cx + std::cos(ang) * r;
                        float ny = cy + std::sin(ang) * r;
                        if (inPolyMode) {
                            if (curContour.empty()) curContour.emplace_back(px, py);
                            curContour.emplace_back(nx, ny);
                        } else if (penDown) {
                            emitSeg(px, py, nx, ny);
                        } else {
                            // Note: in HPGL, AA/AR draws with current pen state; if pen is up it moves along arc,
                            // though many PCB CAD exporters emit PU x,y; AA xc,yc,ang expecting visible arc if PD or explicit AA.
                            emitSeg(px, py, nx, ny);
                        }
                        px = nx;
                        py = ny;
                    }
                    curX = px;
                    curY = py;
                }
            }
            break;
        }

        case M('A','T'): // Arc Through 3 points: AT xm,ym,xe,ye[,chordDeg]
        case M('R','T'): {
            ++validCmdCount;
            parseFloats(argView, nums);
            if (nums.size() >= 4) {
                float xm = (c0 == 'R') ? (curX + toRelMmX(nums[0])) : toAbsMmX(nums[0]);
                float ym = (c0 == 'R') ? (curY + toRelMmY(nums[1])) : toAbsMmY(nums[1]);
                float xe = (c0 == 'R') ? (curX + toRelMmX(nums[2])) : toAbsMmX(nums[2]);
                float ye = (c0 == 'R') ? (curY + toRelMmY(nums[3])) : toAbsMmY(nums[3]);
                float x1 = curX, y1 = curY;
                float d = 2.0f * (x1 * (ym - ye) + xm * (ye - y1) + xe * (y1 - ym));
                if (std::fabs(d) < 1e-5f) {
                    emitSeg(x1, y1, xm, ym);
                    emitSeg(xm, ym, xe, ye);
                } else {
                    float s1 = x1 * x1 + y1 * y1, sm = xm * xm + ym * ym, se = xe * xe + ye * ye;
                    float cx = (s1 * (ym - ye) + sm * (ye - y1) + se * (y1 - ym)) / d;
                    float cy = (s1 * (xe - xm) + sm * (x1 - xe) + se * (xm - x1)) / d;
                    float r = std::hypot(x1 - cx, y1 - cy);
                    float a1 = std::atan2(y1 - cy, x1 - cx);
                    float am = std::atan2(ym - cy, xm - cx);
                    float ae = std::atan2(ye - cy, xe - cx);
                    auto normPos = [](float a) {
                        while (a < 0.0f) a += 6.2831853f;
                        while (a >= 6.2831853f) a -= 6.2831853f;
                        return a;
                    };
                    float dm = normPos(am - a1), de = normPos(ae - a1);
                    float sweep = (dm <= de) ? de : (de - 6.2831853f);
                    float px = x1, py = y1;
                    for (int s = 1; s <= 20; ++s) {
                        float ang = a1 + sweep * (static_cast<float>(s) / 20.0f);
                        float nx = cx + std::cos(ang) * r, ny = cy + std::sin(ang) * r;
                        emitSeg(px, py, nx, ny);
                        px = nx; py = ny;
                    }
                }
                curX = xe;
                curY = ye;
            }
            break;
        }

        case M('E','A'): // Edge Rectangle Absolute
        case M('R','A'): // Fill Rectangle Absolute
        case M('E','R'): // Edge Rectangle Relative
        case M('R','R'): { // Fill Rectangle Relative
            ++validCmdCount;
            parseFloats(argView, nums);
            if (nums.size() >= 2) {
                bool isRel = (c1 == 'R');
                bool isFill = (c0 == 'R');
                float x2 = isRel ? (curX + toRelMmX(nums[0])) : toAbsMmX(nums[0]);
                float y2 = isRel ? (curY + toRelMmY(nums[1])) : toAbsMmY(nums[1]);
                emitRectMm(curX, curY, x2, y2, isFill);
            }
            break;
        }

        case M('E','W'): // Edge Wedge: EW r,startDeg,sweepDeg[,chordDeg]
        case M('W','G'): { // Fill Wedge
            ++validCmdCount;
            parseFloats(argView, nums);
            if (nums.size() >= 3) {
                float rx = std::fabs(toRelMmX(nums[0]));
                float ry = std::fabs(toRelMmY(nums[0]));
                float startRad = nums[1] * 3.14159265f / 180.0f;
                float sweepRad = std::clamp(nums[2], -360.0f, 360.0f) * 3.14159265f / 180.0f;
                int segs = 18;
                float sx = curX + std::cos(startRad) * rx, sy = curY + std::sin(startRad) * ry;
                emitSeg(curX, curY, sx, sy);
                float px = sx, py = sy;
                for (int s = 1; s <= segs; ++s) {
                    float ang = startRad + sweepRad * (static_cast<float>(s) / static_cast<float>(segs));
                    float nx = curX + std::cos(ang) * rx, ny = curY + std::sin(ang) * ry;
                    emitSeg(px, py, nx, ny);
                    px = nx; py = ny;
                }
                emitSeg(px, py, curX, curY);
            }
            break;
        }

        case M('P','M'): { // Polygon Mode: PM0 start, PM1 close subpoly, PM2 close & exit
            ++validCmdCount;
            parseFloats(argView, nums);
            int mode = nums.empty() ? 0 : static_cast<int>(nums[0]);
            if (mode == 0) {
                polyContours.clear();
                curContour.clear();
                curContour.emplace_back(curX, curY);
                inPolyMode = true;
            } else if (mode == 1) {
                if (curContour.size() >= 2) {
                    if (curContour.back() != curContour.front()) curContour.push_back(curContour.front());
                    polyContours.push_back(std::move(curContour));
                }
                curContour.clear();
            } else if (mode == 2) {
                if (curContour.size() >= 2) {
                    if (curContour.back() != curContour.front()) curContour.push_back(curContour.front());
                    polyContours.push_back(std::move(curContour));
                }
                curContour.clear();
                inPolyMode = false;
            }
            break;
        }

        case M('E','P'): // Edge Polygon
        case M('F','P'): { // Fill Polygon
            ++validCmdCount;
            for (const auto& cnt : polyContours) {
                for (size_t i = 0; i + 1 < cnt.size(); ++i) {
                    emitSeg(cnt[i].first, cnt[i].second, cnt[i + 1].first, cnt[i + 1].second);
                }
                if (c0 == 'F' && cnt.size() >= 3) {
                    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
                    for (const auto& pt : cnt) {
                        minX = std::min(minX, pt.first); maxX = std::max(maxX, pt.first);
                        minY = std::min(minY, pt.second); maxY = std::max(maxY, pt.second);
                    }
                    float step = std::max(0.5f, (maxY - minY) / 10.0f);
                    for (float y = minY + step; y < maxY - 0.05f; y += step) {
                        std::vector<float> xs;
                        for (size_t i = 0; i + 1 < cnt.size(); ++i) {
                            float y0 = cnt[i].second, y1 = cnt[i + 1].second;
                            if ((y0 <= y && y1 > y) || (y1 <= y && y0 > y)) {
                                float t = (y - y0) / (y1 - y0);
                                xs.push_back(cnt[i].first + t * (cnt[i + 1].first - cnt[i].first));
                            }
                        }
                        std::sort(xs.begin(), xs.end());
                        for (size_t k = 0; k + 1 < xs.size(); k += 2) {
                            emitSeg(xs[k], y, xs[k + 1], y);
                        }
                    }
                }
            }
            break;
        }

        // Standard non-drawing HP-GL / HP-GL/2 setup & control mnemonics (accepted cleanly without error)
        case M('V','S'): case M('A','S'): case M('F','S'): case M('L','T'): case M('L','A'):
        case M('R','O'): case M('I','W'): case M('P','G'): case M('N','R'): case M('A','F'):
        case M('A','H'): case M('E','C'): case M('P','S'): case M('Q','L'): case M('B','P'):
        case M('C','T'): case M('D','I'): case M('D','R'): case M('D','V'): case M('E','S'):
        case M('C','P'): case M('L','O'): case M('S','R'): case M('S','L'): case M('S','D'):
        case M('A','D'): case M('S','S'): case M('S','A'): case M('C','S'): case M('C','A'):
        case M('F','T'): case M('P','T'): case M('S','V'): case M('T','R'): case M('U','L'):
        case M('A','C'): case M('N','P'): case M('P','C'): case M('C','R'): case M('M','C'):
        case M('P','P'): case M('I','R'): case M('S','M'): case M('X','T'): case M('Y','T'):
        case M('T','L'): case M('O','A'): case M('O','C'): case M('O','D'): case M('O','E'):
        case M('O','F'): case M('O','H'): case M('O','I'): case M('O','O'): case M('O','P'):
        case M('O','S'): case M('O','W'):
            ++validCmdCount;
            hasExplicitMarker = true;
            break;

        default:
            ++unknownCmdCount;
            if (unknownCmdCount > 12 && validCmdCount < unknownCmdCount * 2) {
                out = ParsedEdaBoard{};
                return false;
            }
            break;
        }
    }

    // Strict HPGL validation: reject random prose that happened to contain "pa" or "ci" without HPGL framing
    if (!hasExplicitMarker || validCmdCount < 2 || validCmdCount < unknownCmdCount * 2) {
        out = ParsedEdaBoard{};
        return false;
    }
    if (out.traces.empty() && out.circles.empty() && out.labels.empty()) {
        out = ParsedEdaBoard{};
        return false;
    }

    size_t penCount = std::max<size_t>(1u, usedPens.size());
    out.formatTitle = L"HP-GL/2 Vector Plotter (.hpgl/.plt — " + std::to_wstring(penCount) +
                      (penCount == 1 ? L" pen, " : L" pens, ") +
                      std::to_wstring(out.traces.size()) + L" vectors)";
    out.netCount = penCount;
    return true;
}

} // namespace Formats
