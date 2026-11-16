#pragma once
// KiCadLegacySch.h — KiCad 4/5 Legacy EESchema Schematic (.sch) & Symbol Library (.lib) Full-Fidelity Parser

static EdaLayerId MapDxfLayerToEda(std::string_view layerName) {
    if (layerName.empty() || layerName == "0") return EdaLayerId::TopCopper;
    uint32_t h = 2166136261u;
    for (char c : layerName) {
        h ^= static_cast<uint8_t>(std::toupper(static_cast<unsigned char>(c)));
        h *= 16777619u;
    }
    switch (h % 5) {
    case 0: return EdaLayerId::TopCopper;
    case 1: return EdaLayerId::BottomCopper;
    case 2: return EdaLayerId::TopSilk;
    case 3: return EdaLayerId::EdgeCuts;
    default: return EdaLayerId::InnerCopper;
    }
}

namespace DetailKiCadLegacy {

struct LegSeg  { float x1 = 0, y1 = 0, x2 = 0, y2 = 0, wMm = 0.22f; int unit = 0, conv = 0; };
struct LegCirc { float cx = 0, cy = 0, r = 20.0f, wMm = 0.22f; bool filled = false; int unit = 0, conv = 0; };
struct LegPin  {
    std::string name;
    std::string number;
    float x = 0, y = 0, len = 100.0f; // mils, +Y UP in symbol space
    char dir = 'R';                   // U, D, L, R (direction from tip INTO symbol body)
    std::string shape;                // I, C, CI, N, X, ...
    int unit = 0, conv = 0;
    bool hidden = false;
};

struct LegSymDef {
    std::string name;
    std::string refPrefix = "U";
    bool drawPinNums  = true;
    bool drawPinNames = true;
    bool isPowerSym   = false;
    std::vector<std::string> aliases;
    std::vector<LegSeg> segs;
    std::vector<LegCirc> circs;
    std::vector<LegPin> pins;
};

inline std::string_view StripUtf8BomView(std::string_view sv) {
    if (sv.size() >= 3 &&
        static_cast<uint8_t>(sv[0]) == 0xEFu &&
        static_cast<uint8_t>(sv[1]) == 0xBBu &&
        static_cast<uint8_t>(sv[2]) == 0xBFu)
    {
        sv.remove_prefix(3);
    }
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r' || sv.front() == '\n')) {
        sv.remove_prefix(1);
    }
    return sv;
}

inline void TokenizeLegacyLine(std::string_view ln, std::vector<std::string>& out) {
    out.clear();
    size_t i = 0;
    while (i < ln.size()) {
        while (i < ln.size() && static_cast<unsigned char>(ln[i]) <= 0x20u) ++i;
        if (i >= ln.size()) break;
        std::string tok;
        if (ln[i] == '"') {
            ++i;
            while (i < ln.size() && ln[i] != '"') {
                if (ln[i] == '\\' && i + 1 < ln.size()) ++i;
                tok.push_back(ln[i++]);
            }
            if (i < ln.size()) ++i;
        } else {
            while (i < ln.size() && static_cast<unsigned char>(ln[i]) > 0x20u) {
                tok.push_back(ln[i++]);
            }
        }
        out.push_back(std::move(tok));
    }
}

inline std::string NormalizeSymLookupKey(std::string_view raw) {
    size_t colon = raw.rfind(':');
    if (colon != std::string_view::npos && colon + 1 < raw.size()) {
        raw.remove_prefix(colon + 1);
    }
    std::string s(raw);
    for (char& c : s) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return s;
}

inline bool ParseLegacySymLibraryMap(std::string_view txt, std::unordered_map<std::string, LegSymDef>& outMap, std::vector<std::string>* outOrder = nullptr) {
    txt = StripUtf8BomView(txt);
    if (txt.rfind("EESchema-LIBRARY", 0) != 0) return false;
    for (unsigned char c : txt) {
        if (c == 0) return false;
    }

    bool inDef = false, inDraw = false;
    LegSymDef cur;
    std::vector<std::string> toks;
    size_t parsedSyms = 0;

    size_t pos = 0;
    while (pos < txt.size() && parsedSyms < 512) {
        size_t eol = txt.find('\n', pos);
        if (eol == std::string_view::npos) eol = txt.size();
        std::string_view ln = txt.substr(pos, eol - pos);
        pos = eol + 1;
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ' || ln.back() == '\t')) ln.remove_suffix(1);
        while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
        if (ln.empty() || ln.front() == '#') continue;

        if (!inDef) {
            if (ln.rfind("DEF ", 0) == 0) {
                TokenizeLegacyLine(ln, toks);
                if (toks.size() >= 3) {
                    cur = LegSymDef{};
                    cur.name = toks[1];
                    if (!cur.name.empty() && cur.name[0] == '~') cur.name.erase(cur.name.begin());
                    cur.refPrefix = toks[2];
                    if (toks.size() >= 6) cur.drawPinNums  = (toks[5] != "N");
                    if (toks.size() >= 7) cur.drawPinNames = (toks[6] != "N");
                    if (toks.size() >= 10 && toks[9] == "P") cur.isPowerSym = true;
                    inDef = true;
                    inDraw = false;
                }
            }
            continue;
        }

        if (ln == "ENDDEF") {
            if (!cur.name.empty()) {
                std::string kMain = NormalizeSymLookupKey(cur.name);
                if (outOrder && !outMap.count(kMain)) outOrder->push_back(kMain);
                std::vector<std::string> aliases = cur.aliases;
                outMap[kMain] = cur;
                for (const auto& al : aliases) {
                    std::string kAl = NormalizeSymLookupKey(al);
                    if (!kAl.empty()) outMap[kAl] = cur;
                }
                ++parsedSyms;
            }
            inDef = false;
            inDraw = false;
            continue;
        }
        if (ln == "DRAW")    { inDraw = true;  continue; }
        if (ln == "ENDDRAW") { inDraw = false; continue; }

        if (!inDraw) {
            if (ln.rfind("ALIAS ", 0) == 0) {
                TokenizeLegacyLine(ln, toks);
                for (size_t i = 1; i < toks.size(); ++i) cur.aliases.push_back(toks[i]);
            }
            continue;
        }

        TokenizeLegacyLine(ln, toks);
        if (toks.empty()) continue;
        const std::string& cmd = toks[0];

        auto thickToMm = [](int thMil) -> float {
            return (thMil > 0) ? std::clamp(static_cast<float>(thMil) * 0.0254f, 0.16f, 1.4f) : 0.24f;
        };

        if (cmd == "S" && toks.size() >= 8) {
            // S startx starty endx endy unit convert thickness [fill]
            float x1 = static_cast<float>(std::atof(toks[1].c_str()));
            float y1 = static_cast<float>(std::atof(toks[2].c_str()));
            float x2 = static_cast<float>(std::atof(toks[3].c_str()));
            float y2 = static_cast<float>(std::atof(toks[4].c_str()));
            int u = std::atoi(toks[5].c_str());
            int c = std::atoi(toks[6].c_str());
            float w = thickToMm(std::atoi(toks[7].c_str()));
            cur.segs.push_back({ x1, y1, x2, y1, w, u, c });
            cur.segs.push_back({ x2, y1, x2, y2, w, u, c });
            cur.segs.push_back({ x2, y2, x1, y2, w, u, c });
            cur.segs.push_back({ x1, y2, x1, y1, w, u, c });
        } else if ((cmd == "P" || cmd == "B") && toks.size() >= 7) {
            // P count unit convert thickness x0 y0 x1 y1 ... [fill]
            int cnt = std::clamp(std::atoi(toks[1].c_str()), 0, 256);
            int u = std::atoi(toks[2].c_str());
            int c = std::atoi(toks[3].c_str());
            float w = thickToMm(std::atoi(toks[4].c_str()));
            std::vector<std::pair<float, float>> pts;
            for (int i = 0; i < cnt && (5 + i * 2 + 1) < static_cast<int>(toks.size()); ++i) {
                pts.push_back({
                    static_cast<float>(std::atof(toks[static_cast<size_t>(5 + i * 2)].c_str())),
                    static_cast<float>(std::atof(toks[static_cast<size_t>(5 + i * 2 + 1)].c_str()))
                });
            }
            for (size_t i = 1; i < pts.size(); ++i) {
                cur.segs.push_back({ pts[i - 1].first, pts[i - 1].second, pts[i].first, pts[i].second, w, u, c });
            }
        } else if (cmd == "C" && toks.size() >= 7) {
            // C posx posy radius unit convert thickness [fill]
            float cx = static_cast<float>(std::atof(toks[1].c_str()));
            float cy = static_cast<float>(std::atof(toks[2].c_str()));
            float r  = std::max(2.0f, static_cast<float>(std::atof(toks[3].c_str())));
            int u = std::atoi(toks[4].c_str());
            int c = std::atoi(toks[5].c_str());
            float w = thickToMm(std::atoi(toks[6].c_str()));
            bool fill = (toks.size() >= 8 && toks[7] == "F");
            cur.circs.push_back({ cx, cy, r, w, fill, u, c });
        } else if (cmd == "A" && toks.size() >= 9) {
            // A posx posy radius start_tenths end_tenths unit convert thickness [fill] [sx sy ex ey]
            float cx = static_cast<float>(std::atof(toks[1].c_str()));
            float cy = static_cast<float>(std::atof(toks[2].c_str()));
            float r  = std::max(2.0f, static_cast<float>(std::atof(toks[3].c_str())));
            double aStart = std::atof(toks[4].c_str()) * 0.1 * (3.14159265358979323846 / 180.0);
            double aEnd   = std::atof(toks[5].c_str()) * 0.1 * (3.14159265358979323846 / 180.0);
            int u = std::atoi(toks[6].c_str());
            int c = std::atoi(toks[7].c_str());
            float w = thickToMm(std::atoi(toks[8].c_str()));
            double sweep = aEnd - aStart;
            while (sweep >  3.14159265358979323846) sweep -= 6.2831853071795864769;
            while (sweep < -3.14159265358979323846) sweep += 6.2831853071795864769;
            if (std::fabs(sweep) < 1e-3) sweep = 3.14159265358979323846;
            int segs = std::clamp(static_cast<int>(std::ceil(std::fabs(sweep) / 0.26)), 4, 24);
            float px = 0, py = 0;
            for (int s = 0; s <= segs; ++s) {
                double ang = aStart + sweep * (static_cast<double>(s) / segs);
                float nx = cx + static_cast<float>(std::cos(ang)) * r;
                float ny = cy + static_cast<float>(std::sin(ang)) * r;
                if (s > 0) cur.segs.push_back({ px, py, nx, ny, w, u, c });
                px = nx; py = ny;
            }
        } else if (cmd == "X" && toks.size() >= 12) {
            // X name num posx posy length dir snum snom unit convert etype [shape]
            LegPin p;
            p.name = (toks[1] == "~") ? "" : toks[1];
            p.number = (toks[2] == "~") ? "" : toks[2];
            p.x = static_cast<float>(std::atof(toks[3].c_str()));
            p.y = static_cast<float>(std::atof(toks[4].c_str()));
            p.len = std::max(0.0f, static_cast<float>(std::atof(toks[5].c_str())));
            p.dir = toks[6].empty() ? 'R' : static_cast<char>(std::toupper(static_cast<unsigned char>(toks[6][0])));
            p.unit = std::atoi(toks[9].c_str());
            p.conv = std::atoi(toks[10].c_str());
            if (toks.size() >= 13) {
                p.shape = toks[12];
                if (p.shape.find('N') != std::string::npos) p.hidden = true;
            }
            cur.pins.push_back(std::move(p));
        }
    }
    return parsedSyms > 0;
}

inline bool GetBuiltinLegacySym(std::string_view rawName, LegSymDef& out) {
    out = LegSymDef{};
    std::string k = NormalizeSymLookupKey(rawName);
    out.name = k;

    if (k == "GND" || k == "GNDA" || k == "GNDD" || k == "GNDREF" || k == "AGND" || k == "DGND" || k == "EARTH") {
        out.isPowerSym = true;
        out.segs = {
            {   0,   0,   0, -50, 0.24f, 0, 0 },
            { -50, -50,  50, -50, 0.26f, 0, 0 },
            {  50, -50,   0,-100, 0.24f, 0, 0 },
            {   0,-100, -50, -50, 0.24f, 0, 0 }
        };
        out.pins = { { "GND", "1", 0, 0, 0, 'D', "N", 0, 0, true } };
        return true;
    }
    if (k == "VCC" || k == "VDD" || k == "+3V3" || k == "+3.3V" || k == "+5V" || k == "+12V" || k == "+24V" || k == "VBAT" || k == "VBUS") {
        out.isPowerSym = true;
        out.segs = {
            {   0,  0,   0,  70, 0.24f, 0, 0 },
            { -45, 70,  45,  70, 0.28f, 0, 0 }
        };
        out.pins = { { k, "1", 0, 0, 0, 'U', "N", 0, 0, true } };
        return true;
    }
    if (k == "R" || k == "R_SMALL" || k == "RES" || k == "R_US") {
        out.refPrefix = "R";
        out.segs = {
            { -40,  100,  40,  100, 0.25f, 0, 0 },
            {  40,  100,  40, -100, 0.25f, 0, 0 },
            {  40, -100, -40, -100, 0.25f, 0, 0 },
            { -40, -100, -40,  100, 0.25f, 0, 0 }
        };
        out.pins = {
            { "", "1", 0,  150, 50, 'D', "", 0, 0, false },
            { "", "2", 0, -150, 50, 'U', "", 0, 0, false }
        };
        return true;
    }
    if (k == "C" || k == "C_SMALL" || k == "CAP" || k == "CP" || k == "CP1") {
        out.refPrefix = "C";
        out.segs = {
            { -80,  30,  80,  30, 0.28f, 0, 0 },
            { -80, -30,  80, -30, 0.28f, 0, 0 }
        };
        if (k == "CP" || k == "CP1") {
            out.segs.push_back({ -55, 55, -25, 55, 0.20f, 0, 0 });
            out.segs.push_back({ -40, 40, -40, 70, 0.20f, 0, 0 });
        }
        out.pins = {
            { "", "1", 0,  150, 120, 'D', "", 0, 0, false },
            { "", "2", 0, -150, 120, 'U', "", 0, 0, false }
        };
        return true;
    }
    if (k == "L" || k == "L_SMALL" || k == "INDUCTOR") {
        out.refPrefix = "L";
        for (int i = 0; i < 4; ++i) {
            float y0 = 100.0f - static_cast<float>(i) * 50.0f;
            float ym = y0 - 25.0f;
            out.segs.push_back({ 0, y0, 35, ym, 0.24f, 0, 0 });
            out.segs.push_back({ 35, ym, 0, y0 - 50.0f, 0.24f, 0, 0 });
        }
        out.pins = {
            { "", "1", 0,  150, 50, 'D', "", 0, 0, false },
            { "", "2", 0, -150, 50, 'U', "", 0, 0, false }
        };
        return true;
    }
    if (k == "D" || k == "LED" || k == "D_SCHOTTKY" || k == "D_ZENER" || k == "DIODE") {
        out.refPrefix = "D";
        out.segs = {
            {  50, -50,  50,  50, 0.26f, 0, 0 },
            {  50,   0, -50,  50, 0.26f, 0, 0 },
            { -50,  50, -50, -50, 0.26f, 0, 0 },
            { -50, -50,  50,   0, 0.26f, 0, 0 }
        };
        if (k == "D_ZENER") {
            out.segs.push_back({ -50,  50, -70,  70, 0.24f, 0, 0 });
            out.segs.push_back({ -50, -50, -30, -70, 0.24f, 0, 0 });
        }
        out.pins = {
            { "K", "1", -150, 0, 100, 'R', "", 0, 0, false },
            { "A", "2",  150, 0, 100, 'L', "", 0, 0, false }
        };
        return true;
    }
    if (k.rfind("Q_NPN", 0) == 0 || k.rfind("Q_PNP", 0) == 0 || k == "NPN" || k == "PNP") {
        out.refPrefix = "Q";
        out.circs = { { 20, 0, 110, 0.22f, false, 0, 0 } };
        out.segs = {
            { -50,  70, -50, -70, 0.30f, 0, 0 },
            { -50,  25,  50, 100, 0.24f, 0, 0 },
            { -50, -25,  50,-100, 0.24f, 0, 0 }
        };
        out.pins = {
            { "B", "1", -200,    0, 150, 'R', "", 0, 0, false },
            { "C", "2",   50,  200, 100, 'D', "", 0, 0, false },
            { "E", "3",   50, -200, 100, 'U', "", 0, 0, false }
        };
        return true;
    }
    if (k.rfind("OPAMP", 0) == 0 || k == "LM358" || k == "TL072" || k == "MCP6002") {
        out.refPrefix = "U";
        out.segs = {
            { -150,  150, -150, -150, 0.28f, 0, 0 },
            { -150, -150,  150,    0, 0.28f, 0, 0 },
            {  150,    0, -150,  150, 0.28f, 0, 0 }
        };
        out.pins = {
            { "+", "3", -250,   75, 100, 'R', "", 1, 0, false },
            { "-", "2", -250,  -75, 100, 'R', "", 1, 0, false },
            { "",  "1",  250,    0, 100, 'L', "", 1, 0, false }
        };
        return true;
    }
    return false;
}

inline void LoadCompanionLegacyLibs(const std::wstring& schFullPath, std::unordered_map<std::string, LegSymDef>& symMap) {
    if (schFullPath.empty()) return;
    size_t sep = schFullPath.find_last_of(L"\\/");
    if (sep == std::wstring::npos) return;
    std::wstring dir = schFullPath.substr(0, sep);

    WIN32_FIND_DATAW fd{};
    HANDLE hFind = FindFirstFileW((dir + L"\\*.lib").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    int loadedLibs = 0;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
        std::wstring libPath = dir + L"\\" + fd.cFileName;
        std::vector<uint8_t> raw;
        if (PathUtils::ReadFileBytes(libPath, 8 * 1024 * 1024, raw) && raw.size() >= 24) {
            std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
            if (ParseLegacySymLibraryMap(sv, symMap, nullptr)) {
                if (++loadedLibs >= 12) break;
            }
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
}

inline void StampLegacySymInstance(
    const LegSymDef* sym,
    float posXMil, float posYMil,
    int ma, int mb, int mc, int md,
    int placedUnit, int placedConv,
    const std::wstring& refDes,
    const std::wstring& valStr,
    const std::wstring& fpStr,
    bool emitCompEntry,
    ParsedEdaBoard& out)
{
    constexpr float kMilToMm = 0.0254f;
    // Transform symbol-local (lx, ly) [+Y UP in mils] -> sheet (sx, sy) [+Y DOWN in mils] -> board (bx, by) [+Y UP in mm]:
    //   sheetX = posX + ma * lx + mb * ly
    //   sheetY = posY + mc * lx + md * ly
    //   bx = sheetX * kMilToMm,  by = -sheetY * kMilToMm
    auto xfPt = [&](float lx, float ly, float& outBx, float& outBy) {
        float sx = posXMil + static_cast<float>(ma) * lx + static_cast<float>(mb) * ly;
        float sy = posYMil + static_cast<float>(mc) * lx + static_cast<float>(md) * ly;
        outBx =  sx * kMilToMm;
        outBy = -sy * kMilToMm;
    };

    auto matchUC = [&](int u, int c) -> bool {
        return (u == 0 || u == placedUnit) && (c == 0 || c == placedConv);
    };

    float bMinX = 1e9f, bMinY = 1e9f, bMaxX = -1e9f, bMaxY = -1e9f;
    auto expB = [&](float x, float y) {
        bMinX = std::min(bMinX, x); bMaxX = std::max(bMaxX, x);
        bMinY = std::min(bMinY, y); bMaxY = std::max(bMaxY, y);
    };

    int pinCnt = 0;
    bool drewAny = false;

    if (sym) {
        for (const auto& sg : sym->segs) {
            if (!matchUC(sg.unit, sg.conv) || out.traces.size() >= 100000) continue;
            float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            xfPt(sg.x1, sg.y1, x1, y1);
            xfPt(sg.x2, sg.y2, x2, y2);
            out.traces.push_back({ x1, y1, x2, y2, sg.wMm, EdaLayerId::SchSymbol, 0 });
            expB(x1, y1); expB(x2, y2);
            drewAny = true;
        }
        for (const auto& cr : sym->circs) {
            if (!matchUC(cr.unit, cr.conv)) continue;
            float cx = 0, cy = 0;
            xfPt(cr.cx, cr.cy, cx, cy);
            float rMm = std::max(0.2f, cr.r * kMilToMm);
            out.circles.push_back({ cx, cy, rMm, cr.wMm, cr.filled, EdaLayerId::SchSymbol });
            expB(cx - rMm, cy - rMm); expB(cx + rMm, cy + rMm);
            drewAny = true;
        }
        for (const auto& p : sym->pins) {
            if (!matchUC(p.unit, p.conv)) continue;
            float dx = 0, dy = 0;
            if (p.dir == 'R') dx =  1.0f;
            else if (p.dir == 'L') dx = -1.0f;
            else if (p.dir == 'U') dy =  1.0f;
            else if (p.dir == 'D') dy = -1.0f;

            float tipX = 0, tipY = 0, bodyX = 0, bodyY = 0;
            xfPt(p.x, p.y, tipX, tipY);
            xfPt(p.x + dx * p.len, p.y + dy * p.len, bodyX, bodyY);

            if (!p.hidden) {
                bool isInv = (p.shape.find('I') != std::string::npos);
                if (p.len > 1.0f) {
                    float stemLen = isInv ? std::max(0.0f, p.len - 36.0f) : p.len;
                    float sx2 = 0, sy2 = 0;
                    xfPt(p.x + dx * stemLen, p.y + dy * stemLen, sx2, sy2);
                    out.traces.push_back({ tipX, tipY, sx2, sy2, 0.22f, EdaLayerId::SchSymbol, 0 });
                }
                if (isInv) {
                    float bcx = 0, bcy = 0;
                    xfPt(p.x + dx * std::max(0.0f, p.len - 18.0f), p.y + dy * std::max(0.0f, p.len - 18.0f), bcx, bcy);
                    out.circles.push_back({ bcx, bcy, 0.45f, 0.20f, false, EdaLayerId::SchSymbol });
                }

                EdaPadPrim pad{};
                pad.x = tipX; pad.y = tipY;
                pad.w = 0.58f; pad.h = 0.58f;
                pad.shape = 0;
                pad.layer = EdaLayerId::SchSymbol;
                pad.pinName = PathUtils::Utf8ToWide(p.number.empty() ? p.name : p.number);
                pad.netName = PathUtils::Utf8ToWide(p.name);
                out.pads.push_back(std::move(pad));

                if (sym->drawPinNames && !p.name.empty() && out.labels.size() < 4096) {
                    float lx = 0, ly = 0;
                    xfPt(p.x + dx * (p.len + 25.0f), p.y + dy * (p.len + 25.0f), lx, ly);
                    out.labels.push_back({ lx - 0.3f, ly - 0.25f, 0.78f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(p.name) });
                }
                expB(tipX, tipY);
                expB(bodyX, bodyY);
                drewAny = true;
            }
            ++pinCnt;
        }
    }

    if (!drewAny) {
        float cx = posXMil * kMilToMm;
        float cy = -posYMil * kMilToMm;
        bMinX = cx - 3.0f; bMaxX = cx + 3.0f;
        bMinY = cy - 2.0f; bMaxY = cy + 2.0f;
        out.traces.push_back({ bMinX, bMinY, bMaxX, bMinY, 0.28f, EdaLayerId::SchSymbol, 0 });
        out.traces.push_back({ bMaxX, bMinY, bMaxX, bMaxY, 0.28f, EdaLayerId::SchSymbol, 0 });
        out.traces.push_back({ bMaxX, bMaxY, bMinX, bMaxY, 0.28f, EdaLayerId::SchSymbol, 0 });
        out.traces.push_back({ bMinX, bMaxY, bMinX, bMinY, 0.28f, EdaLayerId::SchSymbol, 0 });
    }

    const bool isHiddenPwrRef = (!refDes.empty() && refDes[0] == L'#');
    if (sym && sym->isPowerSym) {
        if (!valStr.empty() && out.labels.size() < 4096) {
            out.labels.push_back({ (bMinX + bMaxX) * 0.5f - 0.8f, bMaxY + 0.6f, 0.90f, EdaLayerId::SchLabel, valStr });
        }
        if (!emitCompEntry) return;
        return; // Power symbols (#PWR) don't clutter BOM component list
    }

    std::wstring disp = isHiddenPwrRef ? valStr : (refDes + (valStr.empty() ? L"" : (L" (" + valStr + L")")));
    if (!disp.empty() && out.labels.size() < 4096) {
        out.labels.push_back({ bMinX, bMaxY + 0.9f, 0.98f, EdaLayerId::SchLabel, disp });
    }

    if (emitCompEntry && !isHiddenPwrRef) {
        EdaComponent3D comp;
        comp.refDes = refDes.empty() ? L"U?" : refDes;
        comp.value = valStr;
        comp.footprint = fpStr;
        comp.x = (bMinX + bMaxX) * 0.5f;
        comp.y = (bMinY + bMaxY) * 0.5f;
        comp.widthMm  = std::clamp(bMaxX - bMinX, 1.5f, 80.0f);
        comp.lengthMm = std::clamp(bMaxY - bMinY, 1.5f, 80.0f);
        comp.heightMm = 1.2f;
        comp.pinCount = std::max(2, pinCnt);
        out.components.push_back(std::move(comp));
    }
}

} // namespace DetailKiCadLegacy

inline bool IsLikelyKiCadLegacySymLibBytes(const uint8_t* data, size_t size) {
    if (!data || size < 20) return false;
    size_t scan = std::min<size_t>(size, 1024);
    for (size_t i = 0; i < scan; ++i) {
        if (data[i] == 0) return false;
    }
    std::string_view sv(reinterpret_cast<const char*>(data), scan);
    sv = DetailKiCadLegacy::StripUtf8BomView(sv);
    return sv.rfind("EESchema-LIBRARY", 0) == 0;
}

inline bool ParseKiCadLegacySymLibBoard(std::string_view txt, ParsedEdaBoard& out) {
    using namespace DetailKiCadLegacy;
    std::unordered_map<std::string, LegSymDef> symMap;
    std::vector<std::string> order;
    if (!ParseLegacySymLibraryMap(txt, symMap, &order) || order.empty()) {
        return false;
    }

    out.kind = EdaDocKind::KiCadSch;
    const size_t cols = std::max<size_t>(1u, static_cast<size_t>(std::ceil(std::sqrt(static_cast<double>(order.size())))));
    float curXMil = 0.0f, curYMil = 0.0f;
    size_t rendered = 0;

    for (size_t i = 0; i < order.size() && rendered < 128; ++i) {
        const auto& sym = symMap[order[i]];
        if (sym.segs.empty() && sym.circs.empty() && sym.pins.empty()) continue;
        if (rendered > 0 && (rendered % cols) == 0) {
            curXMil = 0.0f;
            curYMil += 800.0f; // +Y down in sheet mils => moves row downward on screen
        }
        StampLegacySymInstance(
            &sym, curXMil, curYMil,
            1, 0, 0, -1,
            1, 1,
            PathUtils::Utf8ToWide(sym.name),
            PathUtils::Utf8ToWide(sym.refPrefix),
            L"EESCHEMA_LIB",
            true, out);
        curXMil += 900.0f;
        ++rendered;
    }

    if (rendered == 0 || (out.traces.empty() && out.pads.empty() && out.circles.empty())) {
        out = ParsedEdaBoard{};
        return false;
    }
    out.formatTitle = L"KiCad Legacy Symbol Library (.lib — " + std::to_wstring(rendered) + L" symbols)";
    out.netCount = std::max<size_t>(1u, out.pads.empty() ? rendered : out.pads.size());
    FinalizeBoardBounds(out);
    out.valid = true;
    return true;
}

static bool ParseKiCadLegacySch(std::string_view txt, const std::wstring& fullPath, ParsedEdaBoard& out) {
    using namespace DetailKiCadLegacy;
    txt = StripUtf8BomView(txt);
    if (txt.rfind("EESchema Schematic", 0) != 0) {
        return false;
    }
    for (unsigned char c : txt) {
        if (c == 0) return false;
    }

    out.kind = EdaDocKind::KiCadSch;
    constexpr float kMilToMm = 0.0254f;

    std::unordered_map<std::string, LegSymDef> companionLibs;
    LoadCompanionLegacyLibs(fullPath, companionLibs);

    auto resolveSym = [&](std::string_view rawSymName, LegSymDef& tmpBuiltin) -> const LegSymDef* {
        std::string key = NormalizeSymLookupKey(rawSymName);
        auto it = companionLibs.find(key);
        if (it != companionLibs.end()) return &it->second;
        if (GetBuiltinLegacySym(rawSymName, tmpBuiltin)) return &tmpBuiltin;
        return nullptr;
    };

    std::string sheetTitle;
    std::set<std::string> netNames;

    bool inDescr = false;
    bool inComp  = false;
    bool inSheet = false;

    struct PendingComp {
        std::string symName;
        std::wstring refDes;
        std::wstring valStr;
        std::wstring fpStr;
        float px = 0, py = 0;
        int unit = 1, conv = 1;
        int ma = 1, mb = 0, mc = 0, md = -1;
    } curComp;

    struct PendingSheet {
        float x = 0, y = 0, w = 400, h = 300;
        std::wstring name, fileName;
        int pins = 0;
    } curSheet;

    std::vector<std::string> toks;
    size_t pos = 0;
    while (pos < txt.size()) {
        size_t eol = txt.find('\n', pos);
        if (eol == std::string_view::npos) eol = txt.size();
        std::string_view rawLn = txt.substr(pos, eol - pos);
        pos = eol + 1;
        while (!rawLn.empty() && rawLn.back() == '\r') rawLn.remove_suffix(1);
        std::string_view ln = rawLn;
        while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
        if (ln.empty()) continue;

        if (ln.rfind("$Descr ", 0) == 0) {
            inDescr = true;
            continue;
        }
        if (ln == "$EndDescr") {
            inDescr = false;
            continue;
        }
        if (inDescr) {
            if (ln.rfind("Title ", 0) == 0) {
                TokenizeLegacyLine(ln, toks);
                if (toks.size() >= 2 && !toks[1].empty()) {
                    sheetTitle = toks[1];
                    out.labels.push_back({ 4.0f, -2.5f, 1.15f, EdaLayerId::SchLabel, L"Title: " + PathUtils::Utf8ToWide(sheetTitle) });
                }
            }
            continue;
        }

        if (ln == "$Comp") {
            inComp = true;
            curComp = PendingComp{};
            continue;
        }
        if (ln == "$EndComp") {
            if (inComp) {
                LegSymDef builtinDef;
                const LegSymDef* sym = resolveSym(curComp.symName, builtinDef);
                StampLegacySymInstance(
                    sym, curComp.px, curComp.py,
                    curComp.ma, curComp.mb, curComp.mc, curComp.md,
                    curComp.unit, curComp.conv,
                    curComp.refDes, curComp.valStr,
                    curComp.fpStr.empty() ? PathUtils::Utf8ToWide(curComp.symName) : curComp.fpStr,
                    true, out);
            }
            inComp = false;
            continue;
        }
        if (inComp) {
            TokenizeLegacyLine(ln, toks);
            if (toks.empty()) continue;
            if (toks[0] == "L" && toks.size() >= 3) {
                curComp.symName = toks[1];
                curComp.refDes = PathUtils::Utf8ToWide(toks[2]);
            } else if (toks[0] == "U" && toks.size() >= 3) {
                curComp.unit = std::max(1, std::atoi(toks[1].c_str()));
                curComp.conv = std::max(1, std::atoi(toks[2].c_str()));
            } else if (toks[0] == "P" && toks.size() >= 3) {
                curComp.px = static_cast<float>(std::atof(toks[1].c_str()));
                curComp.py = static_cast<float>(std::atof(toks[2].c_str()));
            } else if (toks[0] == "F" && toks.size() >= 3) {
                int fIdx = std::atoi(toks[1].c_str());
                if (fIdx == 0 && !toks[2].empty()) curComp.refDes = PathUtils::Utf8ToWide(toks[2]);
                else if (fIdx == 1) curComp.valStr = PathUtils::Utf8ToWide(toks[2]);
                else if (fIdx == 2 && !toks[2].empty()) curComp.fpStr = PathUtils::Utf8ToWide(toks[2]);
            } else if (toks.size() == 4) {
                // 2x2 integer orientation matrix: a b c d (each in {-1, 0, 1})
                int a = std::atoi(toks[0].c_str());
                int b = std::atoi(toks[1].c_str());
                int c = std::atoi(toks[2].c_str());
                int d = std::atoi(toks[3].c_str());
                if (std::abs(a) <= 1 && std::abs(b) <= 1 && std::abs(c) <= 1 && std::abs(d) <= 1 && (a * d - b * c) != 0) {
                    curComp.ma = a; curComp.mb = b; curComp.mc = c; curComp.md = d;
                }
            }
            continue;
        }

        if (ln == "$Sheet") {
            inSheet = true;
            curSheet = PendingSheet{};
            continue;
        }
        if (ln == "$EndSheet") {
            if (inSheet) {
                float x0 = curSheet.x * kMilToMm, y0 = -curSheet.y * kMilToMm;
                float x1 = (curSheet.x + curSheet.w) * kMilToMm, y1 = -(curSheet.y + curSheet.h) * kMilToMm;
                out.traces.push_back({ x0, y0, x1, y0, 0.32f, EdaLayerId::SchSymbol, 0 });
                out.traces.push_back({ x1, y0, x1, y1, 0.32f, EdaLayerId::SchSymbol, 0 });
                out.traces.push_back({ x1, y1, x0, y1, 0.32f, EdaLayerId::SchSymbol, 0 });
                out.traces.push_back({ x0, y1, x0, y0, 0.32f, EdaLayerId::SchSymbol, 0 });
                std::wstring stLbl = curSheet.name.empty() ? L"SubSheet" : curSheet.name;
                if (!curSheet.fileName.empty()) stLbl += L" [" + curSheet.fileName + L"]";
                out.labels.push_back({ x0 + 0.5f, y0 + 1.0f, 1.05f, EdaLayerId::SchLabel, stLbl });

                EdaComponent3D sc;
                sc.refDes = curSheet.name.empty() ? L"SHEET" : curSheet.name;
                sc.value = curSheet.fileName;
                sc.footprint = L"HIER_SHEET";
                sc.x = (x0 + x1) * 0.5f; sc.y = (y0 + y1) * 0.5f;
                sc.widthMm = std::fabs(x1 - x0); sc.lengthMm = std::fabs(y0 - y1);
                sc.pinCount = std::max(1, curSheet.pins);
                out.components.push_back(std::move(sc));
            }
            inSheet = false;
            continue;
        }
        if (inSheet) {
            TokenizeLegacyLine(ln, toks);
            if (toks.size() >= 5 && toks[0] == "S") {
                curSheet.x = static_cast<float>(std::atof(toks[1].c_str()));
                curSheet.y = static_cast<float>(std::atof(toks[2].c_str()));
                curSheet.w = static_cast<float>(std::atof(toks[3].c_str()));
                curSheet.h = static_cast<float>(std::atof(toks[4].c_str()));
            } else if (toks.size() >= 2 && toks[0] == "F0") {
                curSheet.name = PathUtils::Utf8ToWide(toks[1]);
            } else if (toks.size() >= 2 && toks[0] == "F1") {
                curSheet.fileName = PathUtils::Utf8ToWide(toks[1]);
            } else if (toks.size() >= 6 && toks[0].size() >= 2 && toks[0][0] == 'F') {
                float px = static_cast<float>(std::atof(toks[4].c_str())) * kMilToMm;
                float py = -static_cast<float>(std::atof(toks[5].c_str())) * kMilToMm;
                out.pads.push_back({ px, py, 0.65f, 0.65f, 0, 0, 1, EdaLayerId::SchSymbol, PathUtils::Utf8ToWide(toks[1]), L"" });
                out.labels.push_back({ px + 0.6f, py - 0.25f, 0.85f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(toks[1]) });
                ++curSheet.pins;
            }
            continue;
        }

        if (ln == "Wire Wire Line" || ln == "Wire Bus Line" || ln == "Wire Notes Line" ||
            ln == "Entry Wire Line" || ln == "Entry Bus Bus")
        {
            size_t eol2 = txt.find('\n', pos);
            if (eol2 == std::string_view::npos) eol2 = txt.size();
            std::string coords(txt.substr(pos, eol2 - pos));
            pos = eol2 + 1;
            float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            if (sscanf_s(coords.c_str(), "%f %f %f %f", &x1, &y1, &x2, &y2) == 4) {
                bool isBus = (ln.find("Bus") != std::string_view::npos);
                bool isNotes = (ln.find("Notes") != std::string_view::npos);
                float w = isBus ? 0.48f : (isNotes ? 0.16f : 0.25f);
                EdaLayerId lid = isNotes ? EdaLayerId::SchLabel : EdaLayerId::SchWire;
                out.traces.push_back({ x1 * kMilToMm, -y1 * kMilToMm, x2 * kMilToMm, -y2 * kMilToMm, w, lid, 1 });
            }
            continue;
        }

        if (ln.rfind("Connection ~ ", 0) == 0) {
            float jx = 0, jy = 0;
            if (sscanf_s(std::string(ln.substr(13)).c_str(), "%f %f", &jx, &jy) == 2) {
                out.circles.push_back({ jx * kMilToMm, -jy * kMilToMm, 0.45f, 0.24f, true, EdaLayerId::SchWire });
                out.vias.push_back({ jx * kMilToMm, -jy * kMilToMm, 0.65f, 0.0f, 1 });
            }
            continue;
        }

        if (ln.rfind("NoConn ~ ", 0) == 0) {
            float nx = 0, ny = 0;
            if (sscanf_s(std::string(ln.substr(9)).c_str(), "%f %f", &nx, &ny) == 2) {
                float bx = nx * kMilToMm, by = -ny * kMilToMm;
                out.traces.push_back({ bx - 0.6f, by - 0.6f, bx + 0.6f, by + 0.6f, 0.22f, EdaLayerId::SchSymbol, 0 });
                out.traces.push_back({ bx - 0.6f, by + 0.6f, bx + 0.6f, by - 0.6f, 0.22f, EdaLayerId::SchSymbol, 0 });
            }
            continue;
        }

        if (ln.rfind("Text ", 0) == 0) {
            TokenizeLegacyLine(ln, toks);
            size_t eol2 = txt.find('\n', pos);
            if (eol2 == std::string_view::npos) eol2 = txt.size();
            std::string_view payload = txt.substr(pos, eol2 - pos);
            pos = eol2 + 1;
            while (!payload.empty() && (payload.back() == '\r' || payload.back() == ' ')) payload.remove_suffix(1);

            if (toks.size() >= 4 && !payload.empty()) {
                const std::string& kind = toks[1]; // Label, GLabel, HLabel, Notes
                float lx = static_cast<float>(std::atof(toks[2].c_str())) * kMilToMm;
                float ly = -static_cast<float>(std::atof(toks[3].c_str())) * kMilToMm;
                bool isNet = (kind == "Label" || kind == "GLabel" || kind == "HLabel");
                if (isNet) netNames.insert(std::string(payload));

                if (kind == "GLabel" || kind == "HLabel") {
                    out.traces.push_back({ lx, ly, lx + 0.7f, ly + 0.65f, 0.20f, EdaLayerId::SchSymbol, 0 });
                    out.traces.push_back({ lx + 0.7f, ly + 0.65f, lx + 3.8f, ly + 0.65f, 0.20f, EdaLayerId::SchSymbol, 0 });
                    out.traces.push_back({ lx + 3.8f, ly + 0.65f, lx + 3.8f, ly - 0.65f, 0.20f, EdaLayerId::SchSymbol, 0 });
                    out.traces.push_back({ lx + 3.8f, ly - 0.65f, lx + 0.7f, ly - 0.65f, 0.20f, EdaLayerId::SchSymbol, 0 });
                    out.traces.push_back({ lx + 0.7f, ly - 0.65f, lx, ly, 0.20f, EdaLayerId::SchSymbol, 0 });
                }
                if (out.labels.size() < 4096) {
                    out.labels.push_back({ lx + 0.25f, ly + 0.35f, 0.95f, isNet ? EdaLayerId::SchWire : EdaLayerId::SchLabel, PathUtils::Utf8ToWide(payload) });
                }
            }
            continue;
        }
    }

    if (out.traces.empty() && out.components.empty() && out.pads.empty() && out.circles.empty()) {
        out = ParsedEdaBoard{};
        return false;
    }

    out.formatTitle = L"KiCad Legacy Schematic (.sch";
    if (!sheetTitle.empty()) out.formatTitle += L" — " + PathUtils::Utf8ToWide(sheetTitle);
    out.formatTitle += L")";
    out.netCount = std::max<size_t>(1u, netNames.empty() ? out.components.size() : netNames.size());

    FinalizeBoardBounds(out);
    out.valid = true;
    return true;
}

static bool ParseKiCadLegacySch(std::string_view txt, ParsedEdaBoard& out) {
    return ParseKiCadLegacySch(txt, out.filePath, out);
}
