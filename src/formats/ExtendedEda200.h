#pragma once
// ExtendedEda200.h — 20 Schematics, Silicon OASIS, Signal Integrity, FPGA, Scope/Logic, Automotive & PLC Viewers (#61–#80)
// Covers:
//   PCB/RF/Silicon (#61–#66): .kicad_sym, .ses, .pos/.cpl/.mnt, .ibs, .s4p/.s3p/.s8p, .oas
//   FPGA/PLD/JTAG (#67–#70, #80): .xdc/.sdc, .qsf, .jed, .svf/.xsvf, .sof/.pof
//   Logic/Scope/Automotive/PLC (#71–#79): .sr, .sal, .wfm/.isf, .mf4/.mdf, .ldf, .arxml, .eds/.dcf, .gsdml, .l5x

namespace Formats {

namespace DetailEda200 {

inline uint16_t RdU16LE(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}
inline uint32_t RdU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}
inline uint64_t RdU64LE(const uint8_t* p) {
    return static_cast<uint64_t>(RdU32LE(p)) |
           (static_cast<uint64_t>(RdU32LE(p + 4)) << 32);
}

inline uint64_t ReadOasisVarUInt(const uint8_t* data, size_t size, size_t& pos) {
    uint64_t val = 0;
    int shift = 0;
    while (pos < size && shift < 63) {
        uint8_t b = data[pos++];
        val |= static_cast<uint64_t>(b & 0x7Fu) << shift;
        if ((b & 0x80u) == 0) break;
        shift += 7;
    }
    return val;
}

inline int64_t ReadOasisVarSInt(const uint8_t* data, size_t size, size_t& pos) {
    uint64_t u = ReadOasisVarUInt(data, size, pos);
    bool neg = (u & 1ULL) != 0;
    int64_t mag = static_cast<int64_t>(u >> 1);
    return neg ? -mag : mag;
}

inline void DrawGraticule(ParsedEdaBoard& out, float x0, float y0, float w, float h, int nx = 10, int ny = 8) {
    DetailEda100::AddRectOutline(out, x0 + w * 0.5f, y0 + h * 0.5f, w, h, EdaLayerId::EdgeCuts, 0.35f);
    for (int i = 1; i < nx; ++i) {
        float x = x0 + (w * i) / nx;
        out.traces.push_back({ x, y0, x, y0 + h, 0.08f, EdaLayerId::BottomSilk, 0 });
    }
    for (int j = 1; j < ny; ++j) {
        float y = y0 + (h * j) / ny;
        out.traces.push_back({ x0, y, x0 + w, y, 0.08f, EdaLayerId::BottomSilk, 0 });
    }
}

// Parse BGA pin coordinate like "A1", "B12", "AA14" into (row, col)
inline bool ParseBgaBallCoord(std::string_view pin, int& outRow, int& outCol) {
    while (!pin.empty() && (pin.front() == '"' || pin.front() == ' ' || pin.front() == '\t')) pin.remove_prefix(1);
    while (!pin.empty() && (pin.back() == '"' || pin.back() == ' ' || pin.back() == '\r')) pin.remove_suffix(1);
    if (pin.rfind("PIN_", 0) == 0) pin.remove_prefix(4);
    if (pin.size() < 2 || pin.size() > 5) return false;

    static const char* kJedecRows = "ABCDEFGHJKLMNPRTUVWYZ";
    int r = 0, letters = 0;
    size_t i = 0;
    while (i < pin.size() && ((pin[i] >= 'A' && pin[i] <= 'Z') || (pin[i] >= 'a' && pin[i] <= 'z'))) {
        char up = static_cast<char>(toupper(static_cast<unsigned char>(pin[i++])));
        const char* f = std::strchr(kJedecRows, up);
        int idx = f ? static_cast<int>(f - kJedecRows) : (up - 'A');
        r = r * 20 + idx + 1;
        ++letters;
    }
    if (letters == 0 || i >= pin.size()) return false;
    int c = 0;
    while (i < pin.size() && pin[i] >= '0' && pin[i] <= '9') {
        c = c * 10 + (pin[i++] - '0');
    }
    if (c <= 0 || c > 64 || r <= 0 || r > 64) return false;
    outRow = r - 1;
    outCol = c - 1;
    return true;
}

} // namespace DetailEda200

// ============================================================================
// #61, #62, #63. KiCad Symbol (.kicad_sym), Specctra Route (.ses), Pick & Place (.pos/.cpl/.mnt)
// ============================================================================
inline bool ParseKiCadSymSesPos(std::string_view sv, const std::wstring& ext, ParsedEdaBoard& out) {
    // #61. KiCad v6–v8 Schematic Symbol Library (.kicad_sym)
    if (ext == L".kicad_sym" || sv.find("(kicad_symbol_lib") != std::string_view::npos) {
        if (sv.find("(kicad_symbol_lib") == std::string_view::npos && sv.find("(symbol ") == std::string_view::npos) return false;
        out.kind = EdaDocKind::KiCadSch;
        out.formatTitle = L"KiCad Schematic Symbol Library (.kicad_sym)";

        size_t pos = 0;
        int symIdx = 0;
        float symOffX = 0.0f;
        while (pos < sv.size() && out.traces.size() < 12000) {
            size_t eol = sv.find('\n', pos);
            if (eol == std::string_view::npos) eol = sv.size();
            std::string_view ln = sv.substr(pos, eol - pos);
            pos = eol + 1;

            if (ln.find("(symbol \"") != std::string_view::npos && ln.find("_0_") == std::string_view::npos &&
                ln.find("_1_") == std::string_view::npos)
            {
                size_t q1 = ln.find('"') + 1;
                size_t q2 = ln.find('"', q1);
                if (q2 != std::string_view::npos) {
                    symOffX = symIdx * 36.0f;
                    out.labels.push_back({ symOffX, 16.0f, 1.5f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(ln.substr(q1, q2 - q1)) });
                    ++symIdx;
                }
            } else if (ln.find("(rectangle") != std::string_view::npos) {
                size_t blkEnd = sv.find("))", pos);
                std::string blk(sv.substr(pos - ln.size() - 1, std::min<size_t>(256, (blkEnd == std::string_view::npos ? sv.size() : blkEnd) - (pos - ln.size() - 1))));
                float x1 = -8, y1 = -10, x2 = 8, y2 = 10;
                size_t st = blk.find("(start "), en = blk.find("(end ");
                if (st != std::string::npos) sscanf_s(blk.c_str() + st + 7, "%f %f", &x1, &y1);
                if (en != std::string::npos) sscanf_s(blk.c_str() + en + 5, "%f %f", &x2, &y2);
                DetailEda100::AddRectOutline(out, symOffX + (x1 + x2) * 0.5f, (y1 + y2) * 0.5f, std::fabs(x2 - x1), std::fabs(y2 - y1), EdaLayerId::SchSymbol, 0.35f);
            } else if (ln.find("(pin ") != std::string_view::npos) {
                size_t pinEnd = sv.find("\n    )", pos);
                std::string blk(sv.substr(pos - ln.size() - 1, std::min<size_t>(384, (pinEnd == std::string_view::npos ? sv.size() : pinEnd) - (pos - ln.size() - 1))));
                float px = -12.7f, py = 0.0f, rot = 0.0f, plen = 2.54f;
                size_t atP = blk.find("(at ");
                if (atP != std::string::npos) sscanf_s(blk.c_str() + atP + 4, "%f %f %f", &px, &py, &rot);
                size_t lnP = blk.find("(length ");
                if (lnP != std::string::npos) sscanf_s(blk.c_str() + lnP + 8, "%f", &plen);

                float rad = rot * 3.14159265f / 180.0f;
                float ex = px + plen * std::cos(rad), ey = py + plen * std::sin(rad);
                out.traces.push_back({ symOffX + px, py, symOffX + ex, ey, 0.25f, EdaLayerId::SchWire, 1 });
                out.circles.push_back({ symOffX + px, py, 0.45f, 0.2f, false, EdaLayerId::SchSymbol });

                size_t nmP = blk.find("(name \"");
                if (nmP != std::string::npos) {
                    size_t q1 = nmP + 7, q2 = blk.find('"', q1);
                    if (q2 != std::string::npos && q2 > q1) {
                        std::string pName = blk.substr(q1, q2 - q1);
                        if (pName != "~") out.labels.push_back({ symOffX + ex, ey, 1.0f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(pName) });
                    }
                }
            }
        }
        if (out.traces.empty()) {
            DetailEda100::AddRectOutline(out, 0.0f, 0.0f, 16.0f, 20.0f, EdaLayerId::SchSymbol, 0.35f);
        }
        return true;
    }

    // #62. Specctra Autorouter Session (.ses)
    if (ext == L".ses") {
        if (sv.find("(session") == std::string_view::npos && sv.find("(routes") == std::string_view::npos) return false;
        out.kind = EdaDocKind::KiCadPcb;
        out.formatTitle = L"Specctra Autorouter Routed Session (.ses)";

        // Resolution default: (resolution mil 10) or (resolution um 1000) -> scale to mm
        float toMm = 0.001f;
        size_t resP = sv.find("(resolution ");
        if (resP != std::string_view::npos) {
            std::string rblk(sv.substr(resP + 12, std::min<size_t>(48, sv.size() - resP - 12)));
            char unit[16] = {}; float div = 1000.0f;
            if (sscanf_s(rblk.c_str(), "%15s %f", unit, static_cast<unsigned>(sizeof(unit)), &div) == 2 && div > 0.0f) {
                if (_stricmp(unit, "mil") == 0) toMm = 0.0254f / div;
                else if (_stricmp(unit, "mm") == 0) toMm = 1.0f / div;
                else if (_stricmp(unit, "um") == 0) toMm = 0.001f / div;
            }
        }

        int netId = 1;
        size_t pos = 0;
        while ((pos = sv.find("(path ", pos)) != std::string_view::npos && out.traces.size() < 30000) {
            size_t closeP = sv.find(')', pos);
            if (closeP == std::string_view::npos) break;
            std::string pstr(sv.substr(pos + 6, closeP - pos - 6));
            pos = closeP + 1;

            char layerName[32] = {}; float widthRaw = 250.0f;
            int consumed = 0;
            if (sscanf_s(pstr.c_str(), "%31s %f%n", layerName, static_cast<unsigned>(sizeof(layerName)), &widthRaw, &consumed) >= 2) {
                EdaLayerId lid = (std::strstr(layerName, "B.") || std::strstr(layerName, "Bot") || std::strcmp(layerName, "2") == 0)
                    ? EdaLayerId::BottomCopper : EdaLayerId::TopCopper;
                float wMm = std::clamp(widthRaw * toMm, 0.12f, 2.5f);
                const char* cp = pstr.c_str() + consumed;
                std::vector<std::pair<float, float>> pts;
                while (*cp) {
                    char* e1 = nullptr; float x = std::strtof(cp, &e1); if (e1 == cp) break; cp = e1;
                    char* e2 = nullptr; float y = std::strtof(cp, &e2); if (e2 == cp) break; cp = e2;
                    pts.push_back({ x * toMm, y * toMm });
                }
                for (size_t k = 1; k < pts.size(); ++k) {
                    out.traces.push_back({ pts[k - 1].first, pts[k - 1].second, pts[k].first, pts[k].second, wMm, lid, netId });
                }
                ++netId;
            }
        }

        size_t vpos = 0;
        while ((vpos = sv.find("(via ", vpos)) != std::string_view::npos && out.vias.size() < 8000) {
            size_t closeV = sv.find(')', vpos);
            if (closeV == std::string_view::npos) break;
            std::string vstr(sv.substr(vpos + 5, closeV - vpos - 5));
            vpos = closeV + 1;
            char padstack[64] = {}; float vx = 0, vy = 0;
            if (sscanf_s(vstr.c_str(), "%63s %f %f", padstack, static_cast<unsigned>(sizeof(padstack)), &vx, &vy) == 3) {
                out.vias.push_back({ vx * toMm, vy * toMm, 0.8f, 0.4f, 1 });
            }
        }
        out.netCount = static_cast<size_t>(std::max(1, netId - 1));
        if (out.traces.empty()) DetailEda100::AddRectOutline(out, 0, 0, 40, 30, EdaLayerId::EdgeCuts, 0.35f);
        return true;
    }

    // #63. SMT Pick-and-Place Centroid File (.pos / .cpl / .mnt)
    if (ext == L".pos" || ext == L".cpl" || ext == L".mnt") {
        out.kind = EdaDocKind::KiCadPcb;
        out.formatTitle = L"SMT Pick-and-Place Assembly Centroid (" + ext + L")";

        size_t pos = 0;
        while (pos < sv.size() && out.components.size() < 4000) {
            size_t eol = sv.find('\n', pos);
            if (eol == std::string_view::npos) eol = sv.size();
            std::string ln(sv.substr(pos, eol - pos));
            pos = eol + 1;

            size_t first = ln.find_first_not_of(" \t\r");
            if (first == std::string::npos || ln[first] == '#') continue;

            // Replace commas & quotes with spaces to parse both KiCad space-aligned .pos and JLCPCB CSV .cpl
            for (char& ch : ln) if (ch == ',' || ch == '"' || ch == ';') ch = ' ';
            std::vector<std::string> cols;
            size_t tp = 0;
            while (tp < ln.size()) {
                while (tp < ln.size() && (ln[tp] == ' ' || ln[tp] == '\t' || ln[tp] == '\r')) ++tp;
                size_t te = ln.find_first_of(" \t\r", tp);
                if (te == std::string::npos) te = ln.size();
                if (te > tp) cols.push_back(ln.substr(tp, te - tp));
                tp = te;
            }
            if (cols.size() < 5) continue;
            if (_stricmp(cols[0].c_str(), "Ref") == 0 || _stricmp(cols[0].c_str(), "Designator") == 0) continue;

            // KiCad .pos: Ref Val Package PosX PosY Rot Side
            // JLCPCB .cpl: Designator MidX MidY Layer Rotation [Comment]
            float px = 0.0f, py = 0.0f, rot = 0.0f;
            bool bottom = false;
            std::string ref = cols[0], valStr, pkgStr = "SMD_0805";

            char* echk = nullptr;
            float testCol1 = std::strtof(cols[1].c_str(), &echk);
            if (echk != cols[1].c_str() && (*echk == '\0' || *echk == 'm')) {
                // JLCPCB order: Ref MidX MidY Layer Rot
                px = testCol1;
                py = std::strtof(cols[2].c_str(), nullptr);
                bottom = (_stricmp(cols[3].c_str(), "Bottom") == 0 || _stricmp(cols[3].c_str(), "B") == 0);
                rot = std::strtof(cols[4].c_str(), nullptr);
            } else if (cols.size() >= 6) {
                valStr = cols[1];
                pkgStr = cols[2];
                px = std::strtof(cols[3].c_str(), nullptr);
                py = std::strtof(cols[4].c_str(), nullptr);
                rot = std::strtof(cols[5].c_str(), nullptr);
                if (cols.size() >= 7) bottom = (_stricmp(cols[6].c_str(), "bottom") == 0 || _stricmp(cols[6].c_str(), "bot") == 0);
            } else {
                continue;
            }

            EdaComponent3D c;
            c.refDes = PathUtils::Utf8ToWide(ref);
            c.value = PathUtils::Utf8ToWide(valStr);
            c.footprint = PathUtils::Utf8ToWide(pkgStr);
            c.x = px; c.y = py; c.rotDeg = rot;
            c.bottomSide = bottom;
            c.widthMm = 2.4f; c.lengthMm = 1.6f; c.heightMm = 1.0f;
            if (!ref.empty() && (ref[0] == 'U' || ref[0] == 'I')) { c.category = EdaPartCategory::IC_Package; c.widthMm = 5.5f; c.lengthMm = 5.5f; c.heightMm = 1.5f; }
            else if (!ref.empty() && ref[0] == 'R') c.category = EdaPartCategory::ResistorSMD;
            else if (!ref.empty() && ref[0] == 'C') c.category = EdaPartCategory::CapacitorSMD;
            else if (!ref.empty() && (ref[0] == 'J' || ref[0] == 'P')) { c.category = EdaPartCategory::Connector; c.widthMm = 6.0f; c.lengthMm = 3.0f; c.heightMm = 4.0f; }
            out.components.push_back(c);

            EdaLayerId padL = bottom ? EdaLayerId::BottomCopper : EdaLayerId::TopCopper;
            out.pads.push_back({ px - c.widthMm * 0.42f, py, 0.7f, 0.9f, rot, 0.0f, 1, padL, L"1", L"" });
            out.pads.push_back({ px + c.widthMm * 0.42f, py, 0.7f, 0.9f, rot, 0.0f, 1, padL, L"2", L"" });
        }

        if (out.components.empty()) return false;
        // Synthesize Edge.Cuts outline around placed parts
        float mnX = out.components[0].x, mxX = mnX, mnY = out.components[0].y, mxY = mnY;
        for (const auto& c : out.components) {
            mnX = std::min(mnX, c.x - 5.0f); mxX = std::max(mxX, c.x + 5.0f);
            mnY = std::min(mnY, c.y - 5.0f); mxY = std::max(mxY, c.y + 5.0f);
        }
        DetailEda100::AddRectOutline(out, (mnX + mxX) * 0.5f, (mnY + mxY) * 0.5f, mxX - mnX, mxY - mnY, EdaLayerId::EdgeCuts, 0.4f);
        return true;
    }
    return false;
}

// ============================================================================
// #64, #65, #66. IBIS (.ibs), Multi-Port Touchstone (.s3p/.s4p/.s8p), SEMI OASIS (.oas)
// ============================================================================
inline bool ParseIbisTouchstoneOasis(const uint8_t* data, size_t size, const std::wstring& ext, ParsedEdaBoard& out) {
    std::string_view sv(reinterpret_cast<const char*>(data), size);

    // #64. IBIS Signal Integrity Model (.ibs)
    if (ext == L".ibs") {
        if (sv.find("[IBIS Ver]") == std::string_view::npos && sv.find("[Component]") == std::string_view::npos &&
            sv.find("[Model]") == std::string_view::npos)
        {
            return false;
        }
        out.kind = EdaDocKind::KiCadSch;
        out.formatTitle = L"IBIS High-Speed I/O Buffer Model (.ibs)";
        DetailEda200::DrawGraticule(out, -40.0f, -25.0f, 80.0f, 50.0f, 10, 8);
        out.labels.push_back({ -38.0f, 27.5f, 1.4f, EdaLayerId::TopSilk, L"IBIS V-I / V-T Buffer Curves" });

        auto parseCurveSection = [&](const char* secHdr, EdaLayerId lid) {
            size_t sp = sv.find(secHdr);
            if (sp == std::string_view::npos) return;
            size_t pos = sv.find('\n', sp);
            if (pos == std::string_view::npos) return;
            ++pos;
            std::vector<std::pair<float, float>> pts;
            auto parseEng = [](const char* s) -> float {
                char* ep = nullptr;
                float v = std::strtof(s, &ep);
                if (ep && *ep) {
                    if (*ep == 'm') v *= 1e-3f;
                    else if (*ep == 'u') v *= 1e-6f;
                    else if (*ep == 'n') v *= 1e-9f;
                    else if (*ep == 'p') v *= 1e-12f;
                    else if (*ep == 'k' || *ep == 'K') v *= 1e3f;
                }
                return v;
            };
            while (pos < sv.size() && pts.size() < 512) {
                size_t eol = sv.find('\n', pos);
                if (eol == std::string_view::npos) eol = sv.size();
                std::string ln(sv.substr(pos, eol - pos));
                pos = eol + 1;
                size_t f = ln.find_first_not_of(" \t\r");
                if (f == std::string::npos || ln[f] == '|') continue;
                if (ln[f] == '[') break;
                char c1[32] = {}, c2[32] = {};
                if (sscanf_s(ln.c_str() + f, "%31s %31s", c1, static_cast<unsigned>(sizeof(c1)), c2, static_cast<unsigned>(sizeof(c2))) == 2 &&
                    ((c1[0] >= '0' && c1[0] <= '9') || c1[0] == '-' || c1[0] == '+' || c1[0] == '.'))
                {
                    pts.push_back({ parseEng(c1), parseEng(c2) });
                }
            }
            if (pts.size() >= 2) {
                float minX = pts[0].first, maxX = minX, minY = pts[0].second, maxY = minY;
                for (const auto& p : pts) {
                    minX = std::min(minX, p.first); maxX = std::max(maxX, p.first);
                    minY = std::min(minY, p.second); maxY = std::max(maxY, p.second);
                }
                float dx = std::max(1e-12f, maxX - minX), dy = std::max(1e-12f, maxY - minY);
                for (size_t i = 1; i < pts.size(); ++i) {
                    float x0 = -38.0f + ((pts[i - 1].first - minX) / dx) * 76.0f;
                    float y0 = -23.0f + ((pts[i - 1].second - minY) / dy) * 46.0f;
                    float x1 = -38.0f + ((pts[i].first - minX) / dx) * 76.0f;
                    float y1 = -23.0f + ((pts[i].second - minY) / dy) * 46.0f;
                    out.traces.push_back({ x0, y0, x1, y1, 0.35f, lid, 1 });
                }
            }
        };
        parseCurveSection("[Pulldown]", EdaLayerId::TopCopper);
        parseCurveSection("[Pullup]", EdaLayerId::BottomCopper);
        parseCurveSection("[Rising Waveform]", EdaLayerId::TopSilk);
        parseCurveSection("[Falling Waveform]", EdaLayerId::InnerCopper);
        return true;
    }

    // #65. Multi-Port Differential RF Touchstone (.s3p / .s4p / .s8p)
    if (ext == L".s3p" || ext == L".s4p" || ext == L".s8p") {
        int nPorts = (ext == L".s3p") ? 3 : ((ext == L".s8p") ? 8 : 4);
        out.kind = EdaDocKind::KiCadSch;
        out.formatTitle = L"Touchstone " + std::to_wstring(nPorts) + L"-Port RF Network (" + ext + L")";
        DetailEda200::DrawGraticule(out, -40.0f, -25.0f, 80.0f, 50.0f, 10, 8);

        bool isDb = false, isRi = false;
        std::vector<float> nums;
        size_t pos = 0;
        while (pos < sv.size() && nums.size() < 200000) {
            size_t eol = sv.find('\n', pos);
            if (eol == std::string_view::npos) eol = sv.size();
            std::string_view ln = sv.substr(pos, eol - pos);
            pos = eol + 1;
            size_t excl = ln.find('!');
            if (excl != std::string_view::npos) ln = ln.substr(0, excl);
            size_t f = ln.find_first_not_of(" \t\r");
            if (f == std::string_view::npos) continue;
            if (ln[f] == '#') {
                std::string opt(ln.substr(f));
                for (char& c : opt) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
                isDb = (opt.find(" DB") != std::string::npos);
                isRi = (opt.find(" RI") != std::string::npos);
                continue;
            }
            if (ln[f] == '[') continue; // Touchstone v2 keyword
            const char* p = ln.data() + f;
            const char* end = ln.data() + ln.size();
            std::string tmp(p, end - p);
            const char* cp = tmp.c_str();
            while (*cp) {
                char* ep = nullptr;
                float v = std::strtof(cp, &ep);
                if (ep == cp) { ++cp; continue; }
                nums.push_back(v);
                cp = ep;
            }
        }

        size_t stride = 1 + static_cast<size_t>(nPorts * nPorts * 2);
        if (nums.size() >= stride * 2) {
            auto toDb = [&](float a, float b) -> float {
                if (isDb) return a;
                float mag = isRi ? std::sqrt(a * a + b * b) : std::max(1e-6f, a);
                return 20.0f * std::log10(std::max(1e-6f, mag));
            };
            size_t rows = nums.size() / stride;
            float fMin = nums[0], fMax = nums[(rows - 1) * stride];
            float fSpan = std::max(1e-6f, fMax - fMin);
            for (size_t r = 1; r < rows; ++r) {
                const float* p0 = &nums[(r - 1) * stride];
                const float* p1 = &nums[r * stride];
                float x0 = -38.0f + ((p0[0] - fMin) / fSpan) * 76.0f;
                float x1 = -38.0f + ((p1[0] - fMin) / fSpan) * 76.0f;
                // S11 return loss (TopCopper) & S21 insertion loss (TopSilk)
                float s11_0 = std::clamp(toDb(p0[1], p0[2]), -60.0f, 5.0f);
                float s11_1 = std::clamp(toDb(p1[1], p1[2]), -60.0f, 5.0f);
                float s21_0 = std::clamp(toDb(p0[1 + nPorts * 2], p0[2 + nPorts * 2]), -60.0f, 5.0f);
                float s21_1 = std::clamp(toDb(p1[1 + nPorts * 2], p1[2 + nPorts * 2]), -60.0f, 5.0f);
                out.traces.push_back({ x0, 22.0f + s11_0 * 0.7f, x1, 22.0f + s11_1 * 0.7f, 0.32f, EdaLayerId::TopCopper, 1 });
                out.traces.push_back({ x0, 22.0f + s21_0 * 0.7f, x1, 22.0f + s21_1 * 0.7f, 0.32f, EdaLayerId::TopSilk, 2 });
            }
        } else {
            return false;
        }
        return true;
    }

    // #66. SEMI P39 OASIS Wafer Mask Layout (.oas — "%SEMI-OASIS\r\n")
    if (ext == L".oas" || (size >= 13 && std::memcmp(data, "%SEMI-OASIS\r\n", 13) == 0)) {
        if (size < 13 || std::memcmp(data, "%SEMI-OASIS\r\n", 13) != 0) return false;
        out.kind = EdaDocKind::KiCadPcb;
        out.formatTitle = L"SEMI P39 OASIS Silicon IC Mask (.oas)";

        size_t pos = 13;
        int64_t modX = 0, modY = 0;
        uint64_t modW = 100, modH = 100, modLayer = 1;
        while (pos < size && out.traces.size() < 20000) {
            uint8_t recId = data[pos++];
            if (recId == 0) continue; // PAD
            if (recId == 2) break;    // END record
            if (recId == 1) {         // START: version string + unit real + offset-flag
                uint64_t slen = DetailEda200::ReadOasisVarUInt(data, size, pos);
                pos += static_cast<size_t>(slen);
                continue;
            }
            if (recId == 13 || recId == 14) { // CELL
                if (recId == 13) DetailEda200::ReadOasisVarUInt(data, size, pos);
                else {
                    uint64_t slen = DetailEda200::ReadOasisVarUInt(data, size, pos);
                    pos += static_cast<size_t>(slen);
                }
                continue;
            }
            if (recId == 20 && pos < size) { // RECTANGLE
                uint8_t info = data[pos++];
                if (info & 0x01u) modLayer = DetailEda200::ReadOasisVarUInt(data, size, pos);
                if (info & 0x02u) DetailEda200::ReadOasisVarUInt(data, size, pos); // datatype
                if (info & 0x40u) modW = DetailEda200::ReadOasisVarUInt(data, size, pos);
                if (info & 0x20u) modH = (info & 0x80u) ? modW : DetailEda200::ReadOasisVarUInt(data, size, pos);
                else if (info & 0x80u) modH = modW;
                if (info & 0x10u) modX = DetailEda200::ReadOasisVarSInt(data, size, pos);
                if (info & 0x08u) modY = DetailEda200::ReadOasisVarSInt(data, size, pos);
                if (info & 0x04u) DetailEda200::ReadOasisVarUInt(data, size, pos); // repetition

                float s = 0.05f;
                float fw = std::max(0.5f, modW * s), fh = std::max(0.5f, modH * s);
                float fx = modX * s + fw * 0.5f, fy = modY * s + fh * 0.5f;
                EdaLayerId lid = (modLayer == 1) ? EdaLayerId::TopCopper : ((modLayer == 2) ? EdaLayerId::BottomCopper : EdaLayerId::InnerCopper);
                DetailEda100::AddRectOutline(out, fx, fy, fw, fh, lid, 0.25f);
            } else {
                break; // Stop cleanly if complex CBLOCK encountered
            }
        }
        if (out.traces.empty()) {
            DetailEda100::AddRectOutline(out, 0, 0, 30, 30, EdaLayerId::EdgeCuts, 0.35f);
            for (int r = 0; r < 4; ++r)
                for (int c = 0; c < 4; ++c)
                    DetailEda100::AddRectOutline(out, -9.0f + c * 6.0f, -9.0f + r * 6.0f, 4.2f, 4.2f, (r & 1) ? EdaLayerId::TopCopper : EdaLayerId::BottomCopper, 0.25f);
        }
        return true;
    }
    return false;
}

// ============================================================================
// #67, #68, #69, #70, #80. FPGA & PLD: Xilinx/Synopsys (.xdc/.sdc), Quartus (.qsf),
//                          JEDEC Fuse Map (.jed), JTAG (.svf/.xsvf), Altera Bitstream (.sof/.pof)
// ============================================================================
inline bool ParseFpgaPldJtag200(const uint8_t* data, size_t size, const std::wstring& ext, ParsedEdaBoard& out) {
    std::string_view sv(reinterpret_cast<const char*>(data), size);

    // #67 & #68. FPGA Pinout & Timing Constraints (.xdc / .sdc / .qsf)
    if (ext == L".xdc" || ext == L".sdc" || ext == L".qsf") {
        out.kind = EdaDocKind::KiCadPcb;
        out.formatTitle = (ext == L".qsf") ? L"Intel Quartus FPGA Pinout & Settings (.qsf)" :
                          (ext == L".xdc") ? L"Xilinx Vivado FPGA Constraints & Ballout (.xdc)" :
                                             L"Synopsys Design Timing Constraints (.sdc)";

        struct PinAssign { int row = 0, col = 0; std::wstring sig; };
        std::vector<PinAssign> pins;
        int maxRow = 11, maxCol = 11;
        int clockCount = 0;

        size_t pos = 0;
        while (pos < sv.size() && pins.size() < 2048) {
            size_t eol = sv.find('\n', pos);
            if (eol == std::string_view::npos) eol = sv.size();
            std::string ln(sv.substr(pos, eol - pos));
            pos = eol + 1;
            size_t f = ln.find_first_not_of(" \t\r");
            if (f == std::string::npos || ln[f] == '#') continue;

            if (ln.find("create_clock") != std::string::npos) ++clockCount;

            // Xilinx XDC: set_property PACKAGE_PIN <ball> [get_ports <sig>]
            size_t pp = ln.find("PACKAGE_PIN");
            if (pp != std::string::npos) {
                size_t bs = ln.find_first_not_of(" \t", pp + 11);
                size_t be = (bs != std::string::npos) ? ln.find_first_of(" \t]", bs) : std::string::npos;
                int r = 0, c = 0;
                if (bs != std::string::npos && DetailEda200::ParseBgaBallCoord(std::string_view(ln).substr(bs, (be == std::string::npos ? ln.size() : be) - bs), r, c)) {
                    std::string sig = "IO";
                    size_t gp = ln.find("get_ports");
                    if (gp != std::string::npos) {
                        size_t s1 = ln.find_first_not_of(" \t{[\"", gp + 9);
                        size_t s2 = (s1 != std::string::npos) ? ln.find_first_of(" \t}]\"\r", s1) : std::string::npos;
                        if (s1 != std::string::npos) sig = ln.substr(s1, (s2 == std::string::npos ? ln.size() : s2) - s1);
                    }
                    maxRow = std::max(maxRow, r); maxCol = std::max(maxCol, c);
                    pins.push_back({ r, c, PathUtils::Utf8ToWide(sig) });
                }
            }
            // Intel Quartus QSF: set_location_assignment PIN_<ball> -to <sig>
            size_t qp = ln.find("PIN_");
            if (qp != std::string::npos) {
                size_t be = ln.find_first_of(" \t\r", qp);
                int r = 0, c = 0;
                if (DetailEda200::ParseBgaBallCoord(std::string_view(ln).substr(qp, (be == std::string::npos ? ln.size() : be) - qp), r, c)) {
                    std::string sig = "IO";
                    size_t toP = ln.find("-to ");
                    if (toP != std::string::npos) {
                        size_t s1 = ln.find_first_not_of(" \t\"", toP + 4);
                        size_t s2 = (s1 != std::string::npos) ? ln.find_first_of(" \t\"\r", s1) : std::string::npos;
                        if (s1 != std::string::npos) sig = ln.substr(s1, (s2 == std::string::npos ? ln.size() : s2) - s1);
                    }
                    maxRow = std::max(maxRow, r); maxCol = std::max(maxCol, c);
                    pins.push_back({ r, c, PathUtils::Utf8ToWide(sig) });
                }
            }
        }

        if (pins.empty() && clockCount == 0 && sv.find("set_") == std::string_view::npos) return false;

        int gridR = std::clamp(maxRow + 1, 8, 28);
        int gridC = std::clamp(maxCol + 1, 8, 28);
        float pitch = 1.6f;
        float pkgW = gridC * pitch + 4.0f, pkgH = gridR * pitch + 4.0f;
        DetailEda100::AddRectOutline(out, 0.0f, 0.0f, pkgW, pkgH, EdaLayerId::EdgeCuts, 0.4f);

        // Unassigned BGA balls in grey/bottom silk, assigned user signals in gold/top copper!
        for (int r = 0; r < gridR; ++r) {
            for (int c = 0; c < gridC; ++c) {
                float bx = (c - (gridC - 1) * 0.5f) * pitch;
                float by = ((gridR - 1) * 0.5f - r) * pitch;
                out.circles.push_back({ bx, by, 0.42f, 0.12f, false, EdaLayerId::BottomSilk });
            }
        }
        for (const auto& pa : pins) {
            float bx = (pa.col - (gridC - 1) * 0.5f) * pitch;
            float by = ((gridR - 1) * 0.5f - pa.row) * pitch;
            out.pads.push_back({ bx, by, 0.95f, 0.95f, 0.0f, 0.0f, 0, EdaLayerId::PadsThrough, pa.sig, pa.sig });
            out.labels.push_back({ bx, by, 0.65f, EdaLayerId::TopSilk, pa.sig });
        }
        EdaComponent3D fpgaDie;
        fpgaDie.refDes = L"U1_FPGA";
        fpgaDie.footprint = L"FBGA";
        fpgaDie.widthMm = pkgW * 0.45f; fpgaDie.lengthMm = pkgH * 0.45f; fpgaDie.heightMm = 1.8f;
        fpgaDie.category = EdaPartCategory::IC_Package;
        out.components.push_back(fpgaDie);
        return true;
    }

    // #69. JEDEC JESD3-C PLD/CPLD/GAL Fuse Map (.jed)
    if (ext == L".jed") {
        if (sv.find("*L") == std::string_view::npos && sv.find("QF") == std::string_view::npos &&
            sv.find("\nL") == std::string_view::npos) return false;
        out.kind = EdaDocKind::KiCadSch;
        out.formatTitle = L"JEDEC JESD3-C PLD Programmable Fuse Matrix (.jed)";

        std::vector<std::string> fuseRows;
        size_t pos = 0;
        while (pos < sv.size() && fuseRows.size() < 128) {
            // JEDEC commands start after '*' + optional whitespace; 'L' starts a fuse-list record terminated by '*'
            size_t lpos = std::string_view::npos;
            for (size_t i = pos; i + 2 < sv.size(); ++i) {
                if (sv[i] == 'L' && sv[i + 1] >= '0' && sv[i + 1] <= '9' &&
                    (i == 0 || sv[i - 1] == '*' || sv[i - 1] == '\n' || sv[i - 1] == '\r' || sv[i - 1] == ' ' || sv[i - 1] == '\t'))
                {
                    lpos = i;
                    break;
                }
            }
            if (lpos == std::string_view::npos) break;
            size_t star = sv.find('*', lpos + 1);
            if (star == std::string_view::npos) break;
            std::string_view blk = sv.substr(lpos + 1, star - lpos - 1);
            pos = star + 1;
            // Skip address digits, collect '0'/'1' fuse bits
            size_t sp = blk.find_first_of(" \t\r\n");
            std::string bits;
            if (sp != std::string_view::npos) {
                for (size_t k = sp; k < blk.size() && bits.size() < 64; ++k) {
                    if (blk[k] == '0' || blk[k] == '1') bits.push_back(blk[k]);
                }
            }
            if (!bits.empty()) fuseRows.push_back(std::move(bits));
        }
        if (fuseRows.empty()) return false;

        int rows = static_cast<int>(fuseRows.size());
        int cols = static_cast<int>(fuseRows[0].size());
        float pitch = 1.4f;
        float w = cols * pitch, h = rows * pitch;
        DetailEda100::AddRectOutline(out, 0.0f, 0.0f, w + 6.0f, h + 6.0f, EdaLayerId::EdgeCuts, 0.35f);

        for (int r = 0; r < rows; ++r) {
            float y = ((rows - 1) * 0.5f - r) * pitch;
            out.traces.push_back({ -w * 0.5f, y, w * 0.5f, y, 0.10f, EdaLayerId::BottomCopper, 1 });
            for (int c = 0; c < static_cast<int>(fuseRows[r].size()); ++c) {
                float x = (c - (cols - 1) * 0.5f) * pitch;
                if (r == 0) out.traces.push_back({ x, -h * 0.5f, x, h * 0.5f, 0.10f, EdaLayerId::InnerCopper, 1 });
                if (fuseRows[r][c] == '0') {
                    // Intact programmed cross-point fuse!
                    out.circles.push_back({ x, y, 0.36f, 0.18f, true, EdaLayerId::TopCopper });
                }
            }
        }
        return true;
    }

    // #70. JTAG Serial Vector Format (.svf / .xsvf)
    if (ext == L".svf" || ext == L".xsvf") {
        if (ext == L".svf" && sv.find("SDR") == std::string_view::npos && sv.find("SIR") == std::string_view::npos &&
            sv.find("STATE") == std::string_view::npos)
        {
            return false;
        }
        out.kind = EdaDocKind::KiCadSch;
        out.formatTitle = (ext == L".xsvf") ? L"Compact Binary JTAG Scan Vector (.xsvf)" : L"IEEE 1149.1 JTAG Serial Vector (.svf)";
        DetailEda200::DrawGraticule(out, -40.0f, -20.0f, 80.0f, 40.0f, 16, 4);
        out.labels.push_back({ -46.0f, 14.0f, 1.2f, EdaLayerId::TopSilk, L"TCK" });
        out.labels.push_back({ -46.0f,  5.0f, 1.2f, EdaLayerId::TopSilk, L"TMS" });
        out.labels.push_back({ -46.0f, -5.0f, 1.2f, EdaLayerId::TopSilk, L"TDI" });
        out.labels.push_back({ -46.0f,-14.0f, 1.2f, EdaLayerId::TopSilk, L"TDO" });

        // Extract hex digits from first TDI (...) vector to draw real scan-chain bit transitions!
        std::vector<int> tdiBits;
        size_t tdiP = sv.find("TDI");
        if (tdiP != std::string_view::npos) {
            size_t lp = sv.find('(', tdiP), rp = sv.find(')', tdiP);
            if (lp != std::string_view::npos && rp != std::string_view::npos && rp > lp) {
                for (size_t k = lp + 1; k < rp && tdiBits.size() < 64; ++k) {
                    char ch = sv[k];
                    int nib = (ch >= '0' && ch <= '9') ? (ch - '0') :
                              (ch >= 'A' && ch <= 'F') ? (ch - 'A' + 10) :
                              (ch >= 'a' && ch <= 'f') ? (ch - 'a' + 10) : -1;
                    if (nib >= 0) {
                        for (int b = 3; b >= 0; --b) tdiBits.push_back((nib >> b) & 1);
                    }
                }
            }
        }
        if (tdiBits.empty()) {
            for (size_t k = 0; k < std::min<size_t>(size, 8); ++k)
                for (int b = 7; b >= 0; --b) tdiBits.push_back((data[k] >> b) & 1);
        }

        int n = std::min<int>(32, static_cast<int>(tdiBits.size()));
        float step = 76.0f / std::max(1, n);
        for (int i = 0; i < n; ++i) {
            float x0 = -38.0f + i * step, xm = x0 + step * 0.5f, x1 = x0 + step;
            // TCK clock wave
            out.traces.push_back({ x0, 12.0f, x0, 16.0f, 0.25f, EdaLayerId::TopSilk, 1 });
            out.traces.push_back({ x0, 16.0f, xm, 16.0f, 0.25f, EdaLayerId::TopSilk, 1 });
            out.traces.push_back({ xm, 16.0f, xm, 12.0f, 0.25f, EdaLayerId::TopSilk, 1 });
            out.traces.push_back({ xm, 12.0f, x1, 12.0f, 0.25f, EdaLayerId::TopSilk, 1 });
            // TDI data wave
            float yCur = tdiBits[i] ? -3.0f : -7.0f;
            if (i > 0) {
                float yPrev = tdiBits[i - 1] ? -3.0f : -7.0f;
                if (yPrev != yCur) out.traces.push_back({ x0, yPrev, x0, yCur, 0.30f, EdaLayerId::TopCopper, 2 });
            }
            out.traces.push_back({ x0, yCur, x1, yCur, 0.30f, EdaLayerId::TopCopper, 2 });
        }
        return true;
    }

    // #80. Intel/Altera FPGA SRAM/PROM Bitstream (.sof / .pof)
    if (ext == L".sof" || ext == L".pof") {
        if (size < 16) return false;
        out.kind = EdaDocKind::KiCadPcb;
        // Extract printable FPGA device / Quartus string from header
        std::string devStr = "Altera/Intel FPGA";
        for (size_t i = 0; i + 6 < std::min<size_t>(size, 4096); ++i) {
            if ((data[i] == 'E' && data[i + 1] == 'P') ||
                std::memcmp(data + i, "5CE", 3) == 0 ||
                std::memcmp(data + i, "10CL", 4) == 0 ||
                std::memcmp(data + i, "10M", 3) == 0 ||
                std::memcmp(data + i, "AGF", 3) == 0)
            {
                size_t k = i;
                while (k < size && k - i < 24 && data[k] >= 33 && data[k] <= 126) ++k;
                if (k - i >= 5) { devStr.assign(reinterpret_cast<const char*>(data + i), k - i); break; }
            }
        }
        out.formatTitle = L"Intel/Altera FPGA Bitstream (" + PathUtils::Utf8ToWide(devStr) + L")";
        DetailEda100::AddRectOutline(out, 0.0f, 0.0f, 44.0f, 44.0f, EdaLayerId::EdgeCuts, 0.45f);
        for (int r = 0; r < 6; ++r) {
            for (int c = 0; c < 6; ++c) {
                float x = -15.0f + c * 6.0f, y = -15.0f + r * 6.0f;
                EdaLayerId lid = (c == 2 || c == 3) ? EdaLayerId::InnerCopper : EdaLayerId::TopCopper; // DSP/M20K columns vs LABs
                DetailEda100::AddRectOutline(out, x, y, 4.6f, 4.6f, lid, 0.25f);
            }
        }
        out.labels.push_back({ -16.0f, 24.0f, 1.3f, EdaLayerId::TopSilk, PathUtils::Utf8ToWide(devStr) });
        return true;
    }

    return false;
}

// ============================================================================
// #71–#79. Logic/Scope (.sr, .sal, .wfm/.isf, .mf4/.mdf), Automotive (.ldf, .arxml),
//          Industrial Fieldbus & PLC (.eds/.dcf, .gsdml, .l5x)
// ============================================================================
inline bool ParseLogicScopeAutoPlc200(const uint8_t* data, size_t size, const std::wstring& ext, ParsedEdaBoard& out) {
    std::string_view sv(reinterpret_cast<const char*>(data), size);

    // #71. Sigrok / PulseView Logic Analyzer Session (.sr — ZIP with metadata + logic-1-1)
    if (ext == L".sr") {
        if (size < 4 || data[0] != 'P' || data[1] != 'K') return false;
        std::vector<uint8_t> metaBytes, logicBytes;
        ZipDeflate::ExtractZipEntryFromMemory(data, size, "metadata", metaBytes);
        if (!ZipDeflate::ExtractZipEntryFromMemory(data, size, "logic-1-1", logicBytes)) {
            ZipDeflate::ExtractZipEntryFromMemory(data, size, "logic-1", logicBytes);
        }
        if (metaBytes.empty() && logicBytes.empty()) return false;

        out.kind = EdaDocKind::KiCadSch;
        out.formatTitle = L"Sigrok / PulseView Logic Capture (.sr)";
        DetailEda200::DrawGraticule(out, -40.0f, -24.0f, 80.0f, 48.0f, 16, 8);

        std::vector<std::wstring> chNames = { L"D0", L"D1", L"D2", L"D3", L"D4", L"D5", L"D6", L"D7" };
        std::string_view msv(reinterpret_cast<const char*>(metaBytes.data()), metaBytes.size());
        for (int ch = 1; ch <= 8; ++ch) {
            std::string key = "probe" + std::to_string(ch) + "=";
            size_t kp = msv.find(key);
            if (kp != std::string_view::npos) {
                kp += key.size();
                size_t eol = msv.find_first_of("\r\n", kp);
                chNames[ch - 1] = PathUtils::Utf8ToWide(msv.substr(kp, (eol == std::string_view::npos ? msv.size() : eol) - kp));
            }
        }

        size_t nSamples = std::min<size_t>(logicBytes.size(), 256);
        if (nSamples >= 2) {
            float step = 76.0f / static_cast<float>(nSamples - 1);
            for (int ch = 0; ch < 8; ++ch) {
                float baseY = 19.0f - ch * 5.6f;
                out.labels.push_back({ -47.0f, baseY, 1.0f, EdaLayerId::TopSilk, chNames[ch] });
                EdaLayerId lid = (ch & 1) ? EdaLayerId::BottomCopper : EdaLayerId::TopCopper;
                for (size_t s = 1; s < nSamples; ++s) {
                    float x0 = -38.0f + (s - 1) * step, x1 = -38.0f + s * step;
                    int b0 = (logicBytes[s - 1] >> ch) & 1;
                    int b1 = (logicBytes[s] >> ch) & 1;
                    float y0 = baseY + (b0 ? 1.8f : -1.8f);
                    float y1 = baseY + (b1 ? 1.8f : -1.8f);
                    if (b0 != b1) out.traces.push_back({ x1, y0, x1, y1, 0.25f, lid, ch + 1 });
                    out.traces.push_back({ x0, y0, x1, y0, 0.25f, lid, ch + 1 });
                }
            }
        }
        return true;
    }

    // #72. Saleae Logic 2 Capture (.sal — ZIP with meta.json + digital-0.bin "<SALEAE>")
    if (ext == L".sal") {
        if (size < 4 || data[0] != 'P' || data[1] != 'K') return false;
        std::vector<uint8_t> metaJson, dig0;
        ZipDeflate::ExtractZipEntryFromMemory(data, size, "meta.json", metaJson);
        ZipDeflate::ExtractZipEntryFromMemory(data, size, "digital-0.bin", dig0);
        if (metaJson.empty() && dig0.empty()) return false;

        out.kind = EdaDocKind::KiCadSch;
        out.formatTitle = L"Saleae Logic 2 Digital Capture (.sal)";
        DetailEda200::DrawGraticule(out, -40.0f, -20.0f, 80.0f, 40.0f, 16, 4);
        out.labels.push_back({ -47.0f, 0.0f, 1.1f, EdaLayerId::TopSilk, L"CH0" });

        // Saleae digital-0.bin starts with "<SALEAE>" (8 bytes) + version(4) + type(4) + initialState(4) + beginTime(8) + endTime(8) + numTransitions(8) + double[]
        std::vector<double> trans;
        uint32_t initState = 0;
        if (dig0.size() >= 44 && std::memcmp(dig0.data(), "<SALEAE>", 8) == 0) {
            initState = DetailEda200::RdU32LE(dig0.data() + 16) & 1u;
            uint64_t nTrans = DetailEda200::RdU64LE(dig0.data() + 36);
            for (uint64_t t = 0; t < nTrans && 44 + (t + 1) * 8 <= dig0.size() && t < 512; ++t) {
                double ts = 0.0;
                std::memcpy(&ts, dig0.data() + 44 + t * 8, 8);
                if (std::isfinite(ts)) trans.push_back(ts);
            }
        }
        if (!trans.empty()) {
            double t0 = trans.front(), tSpan = std::max(1e-9, trans.back() - t0);
            float prevX = -38.0f;
            int state = static_cast<int>(initState);
            for (double ts : trans) {
                float tx = -38.0f + static_cast<float>((ts - t0) / tSpan) * 76.0f;
                float yOld = state ? 8.0f : -8.0f;
                float yNew = state ? -8.0f : 8.0f;
                out.traces.push_back({ prevX, yOld, tx, yOld, 0.32f, EdaLayerId::TopCopper, 1 });
                out.traces.push_back({ tx, yOld, tx, yNew, 0.32f, EdaLayerId::TopCopper, 1 });
                state ^= 1;
                prevX = tx;
            }
            out.traces.push_back({ prevX, state ? 8.0f : -8.0f, 38.0f, state ? 8.0f : -8.0f, 0.32f, EdaLayerId::TopCopper, 1 });
        } else {
            out.traces.push_back({ -38.0f, 0.0f, 38.0f, 0.0f, 0.32f, EdaLayerId::TopCopper, 1 });
        }
        return true;
    }

    // #73. Tektronix / Rigol Digital Oscilloscope Waveform (.wfm / .isf)
    if (ext == L".wfm" || ext == L".isf") {
        if (size < 16) return false;
        out.kind = EdaDocKind::KiCadSch;
        out.formatTitle = (ext == L".isf") ? L"Tektronix ISF Oscilloscope Trace (.isf)" : L"Digital Oscilloscope Waveform (.wfm)";
        DetailEda200::DrawGraticule(out, -40.0f, -25.0f, 80.0f, 50.0f, 10, 8);

        std::vector<float> samples;
        size_t curveP = sv.find("CURVE #");
        if (curveP == std::string_view::npos) curveP = sv.find("CURV #");
        if (curveP != std::string_view::npos) {
            size_t hash = sv.find('#', curveP);
            if (hash + 2 < size && data[hash + 1] >= '1' && data[hash + 1] <= '9') {
                int ndig = data[hash + 1] - '0';
                if (hash + 2 + ndig <= size) {
                    size_t byteCnt = std::strtoul(std::string(sv.substr(hash + 2, ndig)).c_str(), nullptr, 10);
                    size_t sOff = hash + 2 + ndig;
                    size_t avail = std::min(byteCnt, size - sOff);
                    for (size_t i = 0; i < avail && samples.size() < 1024; ++i) {
                        samples.push_back(static_cast<float>(static_cast<int8_t>(data[sOff + i])));
                    }
                }
            }
        }
        if (samples.empty()) {
            // Binary WFM: sample payload after header (skip 838 bytes if Tek WFM#003, else 64 bytes)
            size_t hdrSkip = (size > 900 && sv.find("WFM#") != std::string_view::npos) ? 838 : std::min<size_t>(size / 2, 64);
            size_t step = std::max<size_t>(1, (size - hdrSkip) / 512);
            for (size_t i = hdrSkip; i < size && samples.size() < 512; i += step) {
                samples.push_back(static_cast<float>(static_cast<int8_t>(data[i])));
            }
        }

        if (samples.size() >= 2) {
            float mn = samples[0], mx = samples[0];
            for (float v : samples) { mn = std::min(mn, v); mx = std::max(mx, v); }
            float span = std::max(1.0f, mx - mn);
            for (size_t i = 1; i < samples.size(); ++i) {
                float x0 = -39.0f + (static_cast<float>(i - 1) / (samples.size() - 1)) * 78.0f;
                float x1 = -39.0f + (static_cast<float>(i)     / (samples.size() - 1)) * 78.0f;
                float y0 = -20.0f + ((samples[i - 1] - mn) / span) * 40.0f;
                float y1 = -20.0f + ((samples[i]     - mn) / span) * 40.0f;
                out.traces.push_back({ x0, y0, x1, y1, 0.30f, EdaLayerId::TopSilk, 1 });
            }
        }
        return true;
    }

    // #74. ASAM MDF 4.x Automotive Measurement (.mf4 / .mdf — "MDF     ")
    if (ext == L".mf4" || ext == L".mdf") {
        if (size < 64 || std::memcmp(data, "MDF     ", 8) != 0) return false;
        out.kind = EdaDocKind::KiCadSch;
        std::string verStr(reinterpret_cast<const char*>(data + 8), strnlen(reinterpret_cast<const char*>(data + 8), 8));
        out.formatTitle = L"ASAM MDF " + PathUtils::Utf8ToWide(verStr) + L" ECU Telemetry (" + ext + L")";
        DetailEda200::DrawGraticule(out, -40.0f, -24.0f, 80.0f, 48.0f, 10, 8);

        // Scan MDF4 "##TX" text blocks for channel names & "##DT" data block for samples
        std::vector<std::wstring> chLabels;
        const uint8_t* dtPayload = nullptr;
        size_t dtLen = 0;
        for (size_t p = 64; p + 24 <= size; p += 8) {
            if (data[p] == '#' && data[p + 1] == '#') {
                uint64_t blkLen = DetailEda200::RdU64LE(data + p + 8);
                uint64_t lnkCnt = DetailEda200::RdU64LE(data + p + 16);
                size_t hdrBytes = 24 + static_cast<size_t>(lnkCnt) * 8;
                if (blkLen >= hdrBytes && p + blkLen <= size) {
                    if (std::memcmp(data + p + 2, "TX", 2) == 0 && chLabels.size() < 6) {
                        size_t sLen = strnlen(reinterpret_cast<const char*>(data + p + hdrBytes), static_cast<size_t>(blkLen - hdrBytes));
                        if (sLen > 0 && sLen < 64) {
                            chLabels.push_back(PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(data + p + hdrBytes), sLen)));
                        }
                    } else if (std::memcmp(data + p + 2, "DT", 2) == 0 && !dtPayload) {
                        dtPayload = data + p + hdrBytes;
                        dtLen = static_cast<size_t>(blkLen - hdrBytes);
                    }
                    p += static_cast<size_t>((blkLen + 7ULL) & ~7ULL) - 8;
                }
            }
        }
        for (size_t i = 0; i < chLabels.size(); ++i) {
            out.labels.push_back({ -38.0f + i * 18.0f, 26.0f, 1.1f, EdaLayerId::TopSilk, chLabels[i] });
        }
        if (dtPayload && dtLen >= 8) {
            size_t nF = std::min<size_t>(dtLen / 4, 256);
            std::vector<float> vals;
            for (size_t i = 0; i < nF; ++i) {
                float v = 0.0f;
                std::memcpy(&v, dtPayload + i * 4, 4);
                vals.push_back((std::isfinite(v) && std::fabs(v) < 1e6f) ? v : 0.0f);
            }
            float mn = vals[0], mx = vals[0];
            for (float v : vals) { mn = std::min(mn, v); mx = std::max(mx, v); }
            float sp = std::max(1e-3f, mx - mn);
            for (size_t i = 1; i < vals.size(); ++i) {
                float x0 = -38.0f + (static_cast<float>(i - 1) / (vals.size() - 1)) * 76.0f;
                float x1 = -38.0f + (static_cast<float>(i)     / (vals.size() - 1)) * 76.0f;
                float y0 = -18.0f + ((vals[i - 1] - mn) / sp) * 36.0f;
                float y1 = -18.0f + ((vals[i]     - mn) / sp) * 36.0f;
                out.traces.push_back({ x0, y0, x1, y1, 0.35f, EdaLayerId::TopCopper, 1 });
            }
        }
        return true;
    }

    // #75, #76, #77, #78. Automotive LIN (.ldf), AUTOSAR (.arxml), CANopen (.eds/.dcf), PROFINET (.gsdml)
    if (ext == L".ldf" || ext == L".arxml" || ext == L".eds" || ext == L".dcf" || ext == L".gsdml") {
        std::vector<std::wstring> nodes;
        if (ext == L".ldf") {
            if (sv.find("LIN_description_file") == std::string_view::npos && sv.find("Nodes") == std::string_view::npos) return false;
            out.formatTitle = L"LIN Bus Network Topology (.ldf)";
            size_t np = sv.find("Nodes");
            if (np != std::string_view::npos) {
                size_t b0 = sv.find('{', np), b1 = sv.find('}', np);
                if (b0 != std::string_view::npos && b1 != std::string_view::npos) {
                    std::string blk(sv.substr(b0 + 1, b1 - b0 - 1));
                    for (char& c : blk) if (c == ':' || c == ',' || c == ';') c = ' ';
                    size_t tp = 0;
                    while (tp < blk.size() && nodes.size() < 12) {
                        while (tp < blk.size() && (blk[tp] == ' ' || blk[tp] == '\t' || blk[tp] == '\r' || blk[tp] == '\n')) ++tp;
                        size_t te = blk.find_first_of(" \t\r\n", tp);
                        if (te == std::string::npos) te = blk.size();
                        std::string w = blk.substr(tp, te - tp);
                        if (!w.empty() && w != "Master" && w != "Slaves" && !(w[0] >= '0' && w[0] <= '9')) {
                            nodes.push_back(PathUtils::Utf8ToWide(w));
                        }
                        tp = te;
                    }
                }
            }
        } else if (ext == L".arxml") {
            if (sv.find("AUTOSAR") == std::string_view::npos && sv.find("<SHORT-NAME>") == std::string_view::npos) return false;
            out.formatTitle = L"AUTOSAR ECU Network Architecture (.arxml)";
            size_t p = 0;
            while ((p = sv.find("<SHORT-NAME>", p)) != std::string_view::npos && nodes.size() < 12) {
                p += 12;
                size_t e = sv.find("</SHORT-NAME>", p);
                if (e == std::string_view::npos) break;
                nodes.push_back(PathUtils::Utf8ToWide(sv.substr(p, e - p)));
                p = e + 13;
            }
        } else if (ext == L".eds" || ext == L".dcf") {
            if (sv.find("[DeviceInfo]") == std::string_view::npos && sv.find("ParameterName=") == std::string_view::npos) return false;
            out.formatTitle = L"CiA 306 CANopen Electronic Data Sheet (" + ext + L")";
            size_t p = 0;
            while ((p = sv.find("ParameterName=", p)) != std::string_view::npos && nodes.size() < 12) {
                p += 14;
                size_t e = sv.find_first_of("\r\n", p);
                nodes.push_back(PathUtils::Utf8ToWide(sv.substr(p, (e == std::string_view::npos ? sv.size() : e) - p)));
                p = (e == std::string_view::npos) ? sv.size() : e + 1;
            }
        } else if (ext == L".gsdml") {
            if (sv.find("ISO15745Profile") == std::string_view::npos && sv.find("ModuleItem") == std::string_view::npos &&
                sv.find("GSDML") == std::string_view::npos) return false;
            out.formatTitle = L"PROFINET IO Industrial Device Profile (.gsdml)";
            size_t p = 0;
            while ((p = sv.find("ID=\"", p)) != std::string_view::npos && nodes.size() < 12) {
                p += 4;
                size_t e = sv.find('"', p);
                if (e == std::string_view::npos) break;
                nodes.push_back(PathUtils::Utf8ToWide(sv.substr(p, e - p)));
                p = e + 1;
            }
        }

        out.kind = EdaDocKind::KiCadPcb;
        if (nodes.empty()) nodes = { L"Master_ECU", L"Node_1", L"Node_2" };
        float busW = std::max(44.0f, static_cast<float>(nodes.size()) * 14.0f);
        DetailEda100::AddRectOutline(out, 0.0f, 0.0f, busW + 12.0f, 36.0f, EdaLayerId::EdgeCuts, 0.4f);
        // Differential bus backbone
        out.traces.push_back({ -busW * 0.5f,  1.2f, busW * 0.5f,  1.2f, 0.40f, EdaLayerId::TopCopper, 1 });
        out.traces.push_back({ -busW * 0.5f, -1.2f, busW * 0.5f, -1.2f, 0.40f, EdaLayerId::BottomCopper, 2 });

        for (size_t i = 0; i < nodes.size(); ++i) {
            float nx = -busW * 0.42f + (nodes.size() > 1 ? (busW * 0.84f * i) / (nodes.size() - 1) : 0.0f);
            float ny = (i & 1) ? -10.5f : 10.5f;
            EdaComponent3D ic;
            ic.refDes = nodes[i];
            ic.footprint = L"ECU_NODE";
            ic.x = nx; ic.y = ny;
            ic.widthMm = 7.0f; ic.lengthMm = 5.0f; ic.heightMm = 1.8f;
            ic.category = EdaPartCategory::IC_Package;
            out.components.push_back(ic);
            out.traces.push_back({ nx - 1.0f, ny, nx - 1.0f,  1.2f, 0.25f, EdaLayerId::TopCopper, 1 });
            out.traces.push_back({ nx + 1.0f, ny, nx + 1.0f, -1.2f, 0.25f, EdaLayerId::BottomCopper, 2 });
            out.vias.push_back({ nx - 1.0f,  1.2f, 0.8f, 0.4f, 1 });
            out.vias.push_back({ nx + 1.0f, -1.2f, 0.8f, 0.4f, 2 });
        }
        return true;
    }

    // #79. Rockwell Studio 5000 Logix Designer PLC Ladder Export (.l5x)
    if (ext == L".l5x") {
        if (sv.find("RSLogix5000Content") == std::string_view::npos && sv.find("<Rung") == std::string_view::npos) return false;
        out.kind = EdaDocKind::KiCadSch;
        out.formatTitle = L"Rockwell Studio 5000 PLC Ladder Logic (.l5x)";

        std::vector<std::string> rungs;
        size_t p = 0;
        while ((p = sv.find("<Text>", p)) != std::string_view::npos && rungs.size() < 16) {
            p += 6;
            size_t e = sv.find("</Text>", p);
            if (e == std::string_view::npos) break;
            std::string rtxt(sv.substr(p, e - p));
            // Strip <![CDATA[ ... ]]> if present
            size_t cd = rtxt.find("[CDATA[");
            if (cd != std::string::npos) {
                rtxt.erase(0, cd + 7);
                size_t ce = rtxt.rfind("]]>");
                if (ce != std::string::npos) rtxt.resize(ce);
            }
            if (rtxt.find('(') != std::string::npos) rungs.push_back(std::move(rtxt));
            p = e + 7;
        }
        if (rungs.empty()) rungs.push_back("XIC(Start_PB)XIO(EStop_OK)OTE(Main_Drive);");

        float railH = static_cast<float>(rungs.size()) * 12.0f + 8.0f;
        // Left (+24V) and Right (0V) PLC Ladder Power Rails
        out.traces.push_back({ -40.0f,  10.0f, -40.0f, 10.0f - railH, 0.55f, EdaLayerId::SchWire, 1 });
        out.traces.push_back({  40.0f,  10.0f,  40.0f, 10.0f - railH, 0.55f, EdaLayerId::SchWire, 1 });

        for (size_t r = 0; r < rungs.size(); ++r) {
            float ry = 2.0f - static_cast<float>(r) * 12.0f;
            const std::string& txt = rungs[r];
            struct Inst { std::string op, tag; };
            std::vector<Inst> insts;
            size_t ip = 0;
            while (ip < txt.size() && insts.size() < 6) {
                while (ip < txt.size() && !isalpha(static_cast<unsigned char>(txt[ip]))) ++ip;
                size_t lp = txt.find('(', ip);
                if (lp == std::string::npos) break;
                std::string op = txt.substr(ip, lp - ip);
                size_t rp = txt.find(')', lp + 1);
                if (rp == std::string::npos) break;
                insts.push_back({ op, txt.substr(lp + 1, rp - lp - 1) });
                ip = rp + 1;
            }

            float curX = -40.0f;
            for (size_t k = 0; k < insts.size(); ++k) {
                bool isCoil = (insts[k].op == "OTE" || insts[k].op == "OTL" || insts[k].op == "OTU");
                float symX = isCoil ? 28.0f : (-26.0f + static_cast<float>(k) * 16.0f);
                out.traces.push_back({ curX, ry, symX - 2.2f, ry, 0.25f, EdaLayerId::SchWire, 1 });
                if (isCoil) {
                    out.circles.push_back({ symX, ry, 2.2f, 0.28f, false, EdaLayerId::SchSymbol });
                } else {
                    // Ladder contact | | or normally-closed |/|
                    out.traces.push_back({ symX - 1.5f, ry - 2.4f, symX - 1.5f, ry + 2.4f, 0.32f, EdaLayerId::SchSymbol, 1 });
                    out.traces.push_back({ symX + 1.5f, ry - 2.4f, symX + 1.5f, ry + 2.4f, 0.32f, EdaLayerId::SchSymbol, 1 });
                    if (insts[k].op == "XIO") {
                        out.traces.push_back({ symX - 1.3f, ry - 2.0f, symX + 1.3f, ry + 2.0f, 0.25f, EdaLayerId::SchSymbol, 1 });
                    }
                }
                out.labels.push_back({ symX - 4.0f, ry + 4.2f, 0.95f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(insts[k].tag) });
                curX = symX + 2.2f;
            }
            out.traces.push_back({ curX, ry, 40.0f, ry, 0.25f, EdaLayerId::SchWire, 1 });
        }
        return true;
    }

    return false;
}

inline bool ParseExtendedEda200(const uint8_t* data, size_t size, const std::wstring& ext, ParsedEdaBoard& out) {
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    if (ext == L".kicad_sym" || ext == L".ses" || ext == L".pos" || ext == L".cpl" || ext == L".mnt") {
        return ParseKiCadSymSesPos(sv, ext, out);
    }
    if (ext == L".ibs" || ext == L".s3p" || ext == L".s4p" || ext == L".s8p" || ext == L".oas") {
        return ParseIbisTouchstoneOasis(data, size, ext, out);
    }
    if (ext == L".xdc" || ext == L".sdc" || ext == L".qsf" || ext == L".jed" ||
        ext == L".svf" || ext == L".xsvf"|| ext == L".sof" || ext == L".pof")
    {
        return ParseFpgaPldJtag200(data, size, ext, out);
    }
    if (ext == L".sr"    || ext == L".sal" || ext == L".wfm" || ext == L".isf" ||
        ext == L".mf4"   || ext == L".mdf" || ext == L".ldf" || ext == L".arxml" ||
        ext == L".eds"   || ext == L".dcf" || ext == L".gsdml" || ext == L".l5x")
    {
        return ParseLogicScopeAutoPlc200(data, size, ext, out);
    }
    return false;
}

} // namespace Formats
