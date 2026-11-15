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

        struct OasisPt { int64_t x = 0, y = 0; };
        struct OasisShape {
            uint8_t kind = 0; // 1=rect, 2=poly, 3=path, 4=circle, 5=text, 6=place
            uint64_t layer = 0;
            int64_t x = 0, y = 0;
            uint64_t w = 0, h = 0;
            std::vector<OasisPt> pts; // relative to (x, y)
            std::string str;
            double mag = 1.0, deg = 0.0;
            bool flip = false;
            std::vector<OasisPt> repOffsets; // includes (0,0)
        };
        struct OasisCell {
            std::string name;
            std::vector<OasisShape> shapes;
        };
        struct OasisModal {
            bool xyAbs = true;
            uint64_t layer = 0, datatype = 0, textLayer = 0, textType = 0;
            int64_t geomX = 0, geomY = 0, textX = 0, textY = 0, placeX = 0, placeY = 0;
            uint64_t rectW = 0, rectH = 0, circleR = 0, pathHalfW = 0;
            int64_t pathStartExt = 0, pathEndExt = 0;
            uint64_t ctrapType = 0, trapW = 0, trapH = 0;
            std::vector<OasisPt> polyPts, pathPts;
            std::vector<OasisPt> modalRep = { { 0, 0 } };
            std::string textStr, placeCell;
        };

        struct OasisParser {
            bool ok = true;
            bool sawStart = false;
            bool stopStream = false;
            double unit = 1000.0;
            uint64_t nextCellRef = 0, nextTextRef = 0;
            std::vector<std::pair<uint64_t, std::string>> cellNames;
            std::vector<std::pair<uint64_t, std::string>> textStrings;
            std::vector<OasisCell> cells;
            OasisModal mod;

            uint8_t RdByte(const uint8_t* b, size_t n, size_t& p) {
                if (p >= n) { ok = false; return 0; }
                return b[p++];
            }
            uint64_t RdU(const uint8_t* b, size_t n, size_t& p) {
                uint64_t v = 0;
                int sh = 0;
                while (p < n && sh < 64) {
                    uint8_t c = b[p++];
                    v |= static_cast<uint64_t>(c & 0x7Fu) << sh;
                    if ((c & 0x80u) == 0) return v;
                    sh += 7;
                }
                ok = false;
                return 0;
            }
            int64_t RdS(const uint8_t* b, size_t n, size_t& p) {
                uint64_t u = RdU(b, n, p);
                int64_t m = static_cast<int64_t>(u >> 1);
                return (u & 1ULL) ? -m : m;
            }
            std::string RdStr(const uint8_t* b, size_t n, size_t& p) {
                uint64_t len = RdU(b, n, p);
                if (!ok || len > n - p || len > 1048576ULL) { ok = false; return {}; }
                std::string s(reinterpret_cast<const char*>(b + p), static_cast<size_t>(len));
                p += static_cast<size_t>(len);
                return s;
            }
            double RdReal(const uint8_t* b, size_t n, size_t& p) {
                uint64_t tp = RdU(b, n, p);
                if (!ok) return 1.0;
                switch (tp) {
                case 0: return static_cast<double>(RdU(b, n, p));
                case 1: return -static_cast<double>(RdU(b, n, p));
                case 2: { uint64_t d = RdU(b, n, p); return (d != 0) ? (1.0 / static_cast<double>(d)) : 1.0; }
                case 3: { uint64_t d = RdU(b, n, p); return (d != 0) ? (-1.0 / static_cast<double>(d)) : -1.0; }
                case 4: {
                    uint64_t num = RdU(b, n, p), den = RdU(b, n, p);
                    return (den != 0) ? (static_cast<double>(num) / static_cast<double>(den)) : 1.0;
                }
                case 5: {
                    uint64_t num = RdU(b, n, p), den = RdU(b, n, p);
                    return (den != 0) ? (-static_cast<double>(num) / static_cast<double>(den)) : -1.0;
                }
                case 6: {
                    if (p + 4 > n) { ok = false; return 1.0; }
                    uint32_t bits = DetailEda200::RdU32LE(b + p); p += 4;
                    float fv = 1.0f; std::memcpy(&fv, &bits, 4);
                    return static_cast<double>(fv);
                }
                case 7: {
                    if (p + 8 > n) { ok = false; return 1.0; }
                    uint64_t bits = DetailEda200::RdU64LE(b + p); p += 8;
                    double dv = 1.0; std::memcpy(&dv, &bits, 8);
                    return dv;
                }
                default: ok = false; return 1.0;
                }
            }
            void SkipInterval(const uint8_t* b, size_t n, size_t& p) {
                uint64_t it = RdU(b, n, p);
                if (it == 0) return;
                if (it >= 1 && it <= 3) { RdU(b, n, p); return; }
                if (it == 4) { RdU(b, n, p); RdU(b, n, p); return; }
                ok = false;
            }
            OasisPt DirMag(uint64_t dir, int64_t mag) {
                switch (dir & 7ULL) {
                case 0: return {  mag,    0 }; // E
                case 1: return {    0,  mag }; // N
                case 2: return { -mag,    0 }; // W
                case 3: return {    0, -mag }; // S
                case 4: return {  mag,  mag }; // NE
                case 5: return { -mag,  mag }; // NW
                case 6: return { -mag, -mag }; // SW
                default:return {  mag, -mag }; // SE
                }
            }
            OasisPt RdGDelta(const uint8_t* b, size_t n, size_t& p) {
                uint64_t u = RdU(b, n, p);
                if ((u & 1ULL) == 0) {
                    return DirMag((u >> 1) & 7ULL, static_cast<int64_t>(u >> 4));
                }
                int64_t xm = static_cast<int64_t>(u >> 2);
                int64_t dx = (u & 2ULL) ? -xm : xm;
                int64_t dy = RdS(b, n, p);
                return { dx, dy };
            }
            void RdPointList(const uint8_t* b, size_t n, size_t& p, bool isPoly, std::vector<OasisPt>& outPts) {
                uint64_t ptype = RdU(b, n, p);
                uint64_t vcnt = RdU(b, n, p);
                if (!ok || ptype > 5 || vcnt == 0 || vcnt > 65536ULL) { ok = false; return; }
                outPts.clear();
                outPts.reserve(static_cast<size_t>(vcnt) + 2);
                outPts.push_back({ 0, 0 });
                int64_t cx = 0, cy = 0;
                int64_t prevDx = 0, prevDy = 0;
                for (uint64_t i = 0; i < vcnt && ok; ++i) {
                    int64_t dx = 0, dy = 0;
                    if (ptype == 0) {
                        int64_t d = RdS(b, n, p);
                        if ((i & 1ULL) == 0) dx = d; else dy = d;
                    } else if (ptype == 1) {
                        int64_t d = RdS(b, n, p);
                        if ((i & 1ULL) == 0) dy = d; else dx = d;
                    } else if (ptype == 2) {
                        uint64_t u = RdU(b, n, p);
                        OasisPt dm = DirMag(u & 3ULL, static_cast<int64_t>(u >> 2));
                        dx = dm.x; dy = dm.y;
                    } else if (ptype == 3) {
                        uint64_t u = RdU(b, n, p);
                        OasisPt dm = DirMag(u & 7ULL, static_cast<int64_t>(u >> 3));
                        dx = dm.x; dy = dm.y;
                    } else if (ptype == 4) {
                        OasisPt dm = RdGDelta(b, n, p);
                        dx = dm.x; dy = dm.y;
                    } else {
                        OasisPt dm = RdGDelta(b, n, p);
                        prevDx += dm.x; prevDy += dm.y;
                        dx = prevDx; dy = prevDy;
                    }
                    cx += dx; cy += dy;
                    outPts.push_back({ cx, cy });
                }
                if (isPoly && (ptype == 0 || ptype == 1) && cx != 0 && cy != 0) {
                    bool nextIsHoriz = (ptype == 0) ? ((vcnt & 1ULL) == 0) : ((vcnt & 1ULL) != 0);
                    if (nextIsHoriz) outPts.push_back({ 0, cy });
                    else outPts.push_back({ cx, 0 });
                }
            }
            void RdRepetition(const uint8_t* b, size_t n, size_t& p, std::vector<OasisPt>& rep) {
                uint64_t rtype = RdU(b, n, p);
                if (!ok) return;
                if (rtype == 0) return; // reuse modalRep
                rep.clear();
                constexpr size_t kMaxRep = 512;
                if (rtype == 1) {
                    uint64_t xd = RdU(b, n, p) + 2ULL;
                    uint64_t yd = RdU(b, n, p) + 2ULL;
                    int64_t xs = static_cast<int64_t>(RdU(b, n, p));
                    int64_t ys = static_cast<int64_t>(RdU(b, n, p));
                    for (uint64_t iy = 0; iy < yd && rep.size() < kMaxRep; ++iy)
                        for (uint64_t ix = 0; ix < xd && rep.size() < kMaxRep; ++ix)
                            rep.push_back({ static_cast<int64_t>(ix) * xs, static_cast<int64_t>(iy) * ys });
                } else if (rtype == 2) {
                    uint64_t xd = RdU(b, n, p) + 2ULL;
                    int64_t xs = static_cast<int64_t>(RdU(b, n, p));
                    for (uint64_t ix = 0; ix < xd && rep.size() < kMaxRep; ++ix)
                        rep.push_back({ static_cast<int64_t>(ix) * xs, 0 });
                } else if (rtype == 3) {
                    uint64_t yd = RdU(b, n, p) + 2ULL;
                    int64_t ys = static_cast<int64_t>(RdU(b, n, p));
                    for (uint64_t iy = 0; iy < yd && rep.size() < kMaxRep; ++iy)
                        rep.push_back({ 0, static_cast<int64_t>(iy) * ys });
                } else if (rtype >= 4 && rtype <= 7) {
                    bool isX = (rtype == 4 || rtype == 5);
                    bool hasGrid = (rtype == 5 || rtype == 7);
                    uint64_t dim = RdU(b, n, p) + 2ULL;
                    int64_t gf = hasGrid ? static_cast<int64_t>(RdU(b, n, p)) : 1;
                    if (dim > 65536ULL) { ok = false; return; }
                    int64_t acc = 0;
                    rep.push_back({ 0, 0 });
                    for (uint64_t i = 1; i < dim && ok; ++i) {
                        acc += static_cast<int64_t>(RdU(b, n, p)) * gf;
                        if (rep.size() < kMaxRep) rep.push_back(isX ? OasisPt{ acc, 0 } : OasisPt{ 0, acc });
                    }
                } else if (rtype == 8) {
                    uint64_t nd = RdU(b, n, p) + 2ULL;
                    uint64_t md = RdU(b, n, p) + 2ULL;
                    OasisPt nv = RdGDelta(b, n, p);
                    OasisPt mv = RdGDelta(b, n, p);
                    for (uint64_t im = 0; im < md && rep.size() < kMaxRep; ++im)
                        for (uint64_t in = 0; in < nd && rep.size() < kMaxRep; ++in)
                            rep.push_back({ static_cast<int64_t>(in) * nv.x + static_cast<int64_t>(im) * mv.x,
                                            static_cast<int64_t>(in) * nv.y + static_cast<int64_t>(im) * mv.y });
                } else if (rtype == 9) {
                    uint64_t dim = RdU(b, n, p) + 2ULL;
                    OasisPt dv = RdGDelta(b, n, p);
                    for (uint64_t i = 0; i < dim && rep.size() < kMaxRep; ++i)
                        rep.push_back({ static_cast<int64_t>(i) * dv.x, static_cast<int64_t>(i) * dv.y });
                } else if (rtype == 10 || rtype == 11) {
                    uint64_t cnt = RdU(b, n, p) + 2ULL;
                    int64_t gf = (rtype == 11) ? static_cast<int64_t>(RdU(b, n, p)) : 1;
                    if (cnt > 65536ULL) { ok = false; return; }
                    int64_t ax = 0, ay = 0;
                    rep.push_back({ 0, 0 });
                    for (uint64_t i = 1; i < cnt && ok; ++i) {
                        OasisPt dv = RdGDelta(b, n, p);
                        ax += dv.x * gf; ay += dv.y * gf;
                        if (rep.size() < kMaxRep) rep.push_back({ ax, ay });
                    }
                } else {
                    ok = false;
                }
                if (rep.empty()) rep.push_back({ 0, 0 });
            }
            std::string LookupMap(const std::vector<std::pair<uint64_t, std::string>>& tbl, uint64_t ref, const char* pfx) {
                for (const auto& kv : tbl) if (kv.first == ref) return kv.second;
                return std::string(pfx) + std::to_string(ref);
            }
            void SkipPropValue(const uint8_t* b, size_t n, size_t& p) {
                uint64_t vt = RdU(b, n, p);
                if (!ok) return;
                if (vt <= 3 || vt == 8 || vt == 13 || vt == 14 || vt == 15) { RdU(b, n, p); }
                else if (vt == 4 || vt == 5) { RdU(b, n, p); RdU(b, n, p); }
                else if (vt == 6) { if (p + 4 > n) ok = false; else p += 4; }
                else if (vt == 7) { if (p + 8 > n) ok = false; else p += 8; }
                else if (vt == 9) { RdS(b, n, p); }
                else if (vt >= 10 && vt <= 12) { RdStr(b, n, p); }
                else { ok = false; }
            }

            void ParseStream(const uint8_t* b, size_t n, size_t startPos, int depth) {
                size_t p = startPos;
                while (p < n && ok && !stopStream && cells.size() < 4096) {
                    uint8_t recId = b[p++];
                    if (recId == 0 || recId == 29) continue; // PAD / repeat PROPERTY
                    if (recId == 2) { stopStream = true; break; } // END
                    if (recId == 1) { // START
                        std::string ver = RdStr(b, n, p);
                        double u = RdReal(b, n, p);
                        uint64_t offFlag = RdU(b, n, p);
                        if (!ok || ver.empty() || ver[0] != '1' || !(u > 0.0) || !std::isfinite(u)) {
                            ok = false; break;
                        }
                        unit = u;
                        if (offFlag == 0) {
                            for (int k = 0; k < 12 && ok; ++k) RdU(b, n, p);
                        }
                        sawStart = true;
                        continue;
                    }
                    if (recId == 3) { cellNames.push_back({ nextCellRef++, RdStr(b, n, p) }); continue; }
                    if (recId == 4) { std::string s = RdStr(b, n, p); uint64_t r = RdU(b, n, p); cellNames.push_back({ r, s }); continue; }
                    if (recId == 5) { textStrings.push_back({ nextTextRef++, RdStr(b, n, p) }); continue; }
                    if (recId == 6) { std::string s = RdStr(b, n, p); uint64_t r = RdU(b, n, p); textStrings.push_back({ r, s }); continue; }
                    if (recId == 7 || recId == 9) { RdStr(b, n, p); continue; }
                    if (recId == 8 || recId == 10) { RdStr(b, n, p); RdU(b, n, p); continue; }
                    if (recId == 11 || recId == 12) { RdStr(b, n, p); SkipInterval(b, n, p); SkipInterval(b, n, p); continue; }
                    if (recId == 13 || recId == 14) { // CELL
                        std::string cname = (recId == 13) ? LookupMap(cellNames, RdU(b, n, p), "CELL_") : RdStr(b, n, p);
                        cells.push_back({ std::move(cname), {} });
                        mod = OasisModal{};
                        continue;
                    }
                    if (recId == 15) { mod.xyAbs = true; continue; }
                    if (recId == 16) { mod.xyAbs = false; continue; }
                    if (recId == 28) { // PROPERTY
                        uint8_t info = RdByte(b, n, p);
                        if (info & 0x04u) {
                            if (info & 0x02u) RdU(b, n, p);
                            else RdStr(b, n, p);
                        }
                        if ((info & 0x08u) == 0) {
                            uint64_t cnt = (info >> 4) & 0x0Fu;
                            if (cnt == 15ULL) cnt = RdU(b, n, p);
                            if (cnt > 65536ULL) { ok = false; break; }
                            for (uint64_t k = 0; k < cnt && ok; ++k) SkipPropValue(b, n, p);
                        }
                        continue;
                    }
                    if (recId == 30) { RdU(b, n, p); RdStr(b, n, p); continue; }
                    if (recId == 31) { RdU(b, n, p); RdStr(b, n, p); RdU(b, n, p); continue; }
                    if (recId == 32) { RdU(b, n, p); RdStr(b, n, p); continue; }
                    if (recId == 33) {
                        uint8_t info = RdByte(b, n, p);
                        RdU(b, n, p);
                        if (info & 0x01u) mod.layer = RdU(b, n, p);
                        if (info & 0x02u) mod.datatype = RdU(b, n, p);
                        RdStr(b, n, p);
                        if (info & 0x10u) { int64_t d = RdS(b, n, p); mod.geomX = mod.xyAbs ? d : (mod.geomX + d); }
                        if (info & 0x08u) { int64_t d = RdS(b, n, p); mod.geomY = mod.xyAbs ? d : (mod.geomY + d); }
                        if (info & 0x04u) RdRepetition(b, n, p, mod.modalRep);
                        continue;
                    }
                    if (recId == 34) { // CBLOCK (RFC 1951 raw Deflate)
                        uint64_t compType = RdU(b, n, p);
                        uint64_t uncompSz = RdU(b, n, p);
                        uint64_t compSz   = RdU(b, n, p);
                        if (!ok || depth > 3 || compType != 0 || compSz > n - p || uncompSz > 32 * 1024 * 1024ULL) {
                            ok = false; break;
                        }
                        std::vector<uint8_t> cblockBuf;
                        if (!ZipDeflate::InflateRaw(b + p, static_cast<size_t>(compSz), cblockBuf, static_cast<size_t>(uncompSz) + 64)) {
                            ok = false; break;
                        }
                        p += static_cast<size_t>(compSz);
                        ParseStream(cblockBuf.data(), cblockBuf.size(), 0, depth + 1);
                        continue;
                    }

                    if (cells.empty()) cells.push_back({ "TOP", {} });
                    auto& curShapes = cells.back().shapes;

                    if (recId == 17 || recId == 18) { // PLACEMENT
                        uint8_t info = RdByte(b, n, p);
                        if (info & 0x80u) {
                            mod.placeCell = (info & 0x40u) ? LookupMap(cellNames, RdU(b, n, p), "CELL_") : RdStr(b, n, p);
                        }
                        double mag = 1.0, deg = 0.0;
                        bool flip = (info & 0x01u) != 0;
                        bool hasRep = false;
                        if (recId == 17) {
                            deg = static_cast<double>(((info >> 1) & 3u) * 90u);
                            if (info & 0x20u) { int64_t d = RdS(b, n, p); mod.placeX = mod.xyAbs ? d : (mod.placeX + d); }
                            if (info & 0x10u) { int64_t d = RdS(b, n, p); mod.placeY = mod.xyAbs ? d : (mod.placeY + d); }
                            hasRep = (info & 0x08u) != 0;
                        } else {
                            if (info & 0x20u) mag = RdReal(b, n, p);
                            if (info & 0x10u) deg = RdReal(b, n, p);
                            if (info & 0x08u) { int64_t d = RdS(b, n, p); mod.placeX = mod.xyAbs ? d : (mod.placeX + d); }
                            if (info & 0x04u) { int64_t d = RdS(b, n, p); mod.placeY = mod.xyAbs ? d : (mod.placeY + d); }
                            hasRep = (info & 0x02u) != 0;
                        }
                        std::vector<OasisPt> reps = { { 0, 0 } };
                        if (hasRep) { RdRepetition(b, n, p, mod.modalRep); reps = mod.modalRep; }
                        if (ok && curShapes.size() < 16384) {
                            OasisShape sh;
                            sh.kind = 6;
                            sh.x = mod.placeX; sh.y = mod.placeY;
                            sh.str = mod.placeCell;
                            sh.mag = mag; sh.deg = deg; sh.flip = flip;
                            sh.repOffsets = std::move(reps);
                            curShapes.push_back(std::move(sh));
                        }
                    } else if (recId == 19) { // TEXT
                        uint8_t info = RdByte(b, n, p);
                        if (info & 0x40u) {
                            mod.textStr = (info & 0x20u) ? LookupMap(textStrings, RdU(b, n, p), "TXT_") : RdStr(b, n, p);
                        }
                        if (info & 0x01u) mod.textLayer = RdU(b, n, p);
                        if (info & 0x02u) mod.textType  = RdU(b, n, p);
                        if (info & 0x10u) { int64_t d = RdS(b, n, p); mod.textX = mod.xyAbs ? d : (mod.textX + d); }
                        if (info & 0x08u) { int64_t d = RdS(b, n, p); mod.textY = mod.xyAbs ? d : (mod.textY + d); }
                        std::vector<OasisPt> reps = { { 0, 0 } };
                        if (info & 0x04u) { RdRepetition(b, n, p, mod.modalRep); reps = mod.modalRep; }
                        if (ok && !mod.textStr.empty() && curShapes.size() < 16384) {
                            OasisShape sh;
                            sh.kind = 5;
                            sh.layer = mod.textLayer;
                            sh.x = mod.textX; sh.y = mod.textY;
                            sh.str = mod.textStr;
                            sh.repOffsets = std::move(reps);
                            curShapes.push_back(std::move(sh));
                        }
                    } else if (recId == 20) { // RECTANGLE
                        uint8_t info = RdByte(b, n, p);
                        if (info & 0x01u) mod.layer    = RdU(b, n, p);
                        if (info & 0x02u) mod.datatype = RdU(b, n, p);
                        if (info & 0x40u) mod.rectW    = RdU(b, n, p);
                        if (info & 0x80u) mod.rectH    = mod.rectW;
                        else if (info & 0x20u) mod.rectH = RdU(b, n, p);
                        if (info & 0x10u) { int64_t d = RdS(b, n, p); mod.geomX = mod.xyAbs ? d : (mod.geomX + d); }
                        if (info & 0x08u) { int64_t d = RdS(b, n, p); mod.geomY = mod.xyAbs ? d : (mod.geomY + d); }
                        std::vector<OasisPt> reps = { { 0, 0 } };
                        if (info & 0x04u) { RdRepetition(b, n, p, mod.modalRep); reps = mod.modalRep; }
                        if (ok && curShapes.size() < 16384) {
                            OasisShape sh;
                            sh.kind = 1; sh.layer = mod.layer;
                            sh.x = mod.geomX; sh.y = mod.geomY;
                            sh.w = mod.rectW; sh.h = mod.rectH;
                            sh.repOffsets = std::move(reps);
                            curShapes.push_back(std::move(sh));
                        }
                    } else if (recId == 21) { // POLYGON
                        uint8_t info = RdByte(b, n, p);
                        if (info & 0x01u) mod.layer    = RdU(b, n, p);
                        if (info & 0x02u) mod.datatype = RdU(b, n, p);
                        if (info & 0x20u) RdPointList(b, n, p, true, mod.polyPts);
                        if (info & 0x10u) { int64_t d = RdS(b, n, p); mod.geomX = mod.xyAbs ? d : (mod.geomX + d); }
                        if (info & 0x08u) { int64_t d = RdS(b, n, p); mod.geomY = mod.xyAbs ? d : (mod.geomY + d); }
                        std::vector<OasisPt> reps = { { 0, 0 } };
                        if (info & 0x04u) { RdRepetition(b, n, p, mod.modalRep); reps = mod.modalRep; }
                        if (ok && mod.polyPts.size() >= 3 && curShapes.size() < 16384) {
                            OasisShape sh;
                            sh.kind = 2; sh.layer = mod.layer;
                            sh.x = mod.geomX; sh.y = mod.geomY;
                            sh.pts = mod.polyPts;
                            sh.repOffsets = std::move(reps);
                            curShapes.push_back(std::move(sh));
                        }
                    } else if (recId == 22) { // PATH
                        uint8_t info = RdByte(b, n, p);
                        if (info & 0x01u) mod.layer     = RdU(b, n, p);
                        if (info & 0x02u) mod.datatype  = RdU(b, n, p);
                        if (info & 0x40u) mod.pathHalfW = RdU(b, n, p);
                        if (info & 0x80u) {
                            uint8_t es = RdByte(b, n, p);
                            uint8_t sSch = (es >> 2) & 3u, eSch = es & 3u;
                            if (sSch == 3u) mod.pathStartExt = RdS(b, n, p);
                            else if (sSch == 1u) mod.pathStartExt = 0;
                            else if (sSch == 2u) mod.pathStartExt = static_cast<int64_t>(mod.pathHalfW);
                            if (eSch == 3u) mod.pathEndExt = RdS(b, n, p);
                            else if (eSch == 1u) mod.pathEndExt = 0;
                            else if (eSch == 2u) mod.pathEndExt = static_cast<int64_t>(mod.pathHalfW);
                        }
                        if (info & 0x20u) RdPointList(b, n, p, false, mod.pathPts);
                        if (info & 0x10u) { int64_t d = RdS(b, n, p); mod.geomX = mod.xyAbs ? d : (mod.geomX + d); }
                        if (info & 0x08u) { int64_t d = RdS(b, n, p); mod.geomY = mod.xyAbs ? d : (mod.geomY + d); }
                        std::vector<OasisPt> reps = { { 0, 0 } };
                        if (info & 0x04u) { RdRepetition(b, n, p, mod.modalRep); reps = mod.modalRep; }
                        if (ok && mod.pathPts.size() >= 2 && curShapes.size() < 16384) {
                            OasisShape sh;
                            sh.kind = 3; sh.layer = mod.layer;
                            sh.x = mod.geomX; sh.y = mod.geomY;
                            sh.w = mod.pathHalfW * 2ULL;
                            sh.pts = mod.pathPts;
                            sh.repOffsets = std::move(reps);
                            curShapes.push_back(std::move(sh));
                        }
                    } else if (recId == 23 || recId == 24 || recId == 25) { // TRAPEZOID / CTRAPEZOID
                        uint8_t info = RdByte(b, n, p);
                        if (info & 0x01u) mod.layer    = RdU(b, n, p);
                        if (info & 0x02u) mod.datatype = RdU(b, n, p);
                        int64_t da = 0, db = 0;
                        bool vert = false;
                        if (recId == 25) {
                            if (info & 0x80u) mod.ctrapType = RdU(b, n, p);
                            if (info & 0x40u) mod.trapW = RdU(b, n, p);
                            if (info & 0x20u) mod.trapH = RdU(b, n, p);
                            if (mod.ctrapType >= 16 && mod.ctrapType <= 19) mod.trapH = mod.trapW;
                            else if (mod.ctrapType >= 20 && mod.ctrapType <= 23) mod.trapW = mod.trapH;
                            else if (mod.ctrapType == 24) mod.trapH = mod.trapW;
                        } else {
                            vert = (info & 0x80u) != 0;
                            if (info & 0x40u) mod.trapW = RdU(b, n, p);
                            if (info & 0x20u) mod.trapH = RdU(b, n, p);
                            da = RdS(b, n, p);
                            if (recId == 23) db = RdS(b, n, p);
                        }
                        if (info & 0x10u) { int64_t d = RdS(b, n, p); mod.geomX = mod.xyAbs ? d : (mod.geomX + d); }
                        if (info & 0x08u) { int64_t d = RdS(b, n, p); mod.geomY = mod.xyAbs ? d : (mod.geomY + d); }
                        std::vector<OasisPt> reps = { { 0, 0 } };
                        if (info & 0x04u) { RdRepetition(b, n, p, mod.modalRep); reps = mod.modalRep; }
                        if (ok && curShapes.size() < 16384) {
                            int64_t tw = static_cast<int64_t>(mod.trapW), th = static_cast<int64_t>(mod.trapH);
                            OasisShape sh;
                            sh.kind = 2; sh.layer = mod.layer;
                            sh.x = mod.geomX; sh.y = mod.geomY;
                            if (recId == 25) {
                                sh.pts = { { 0, 0 }, { tw, 0 }, { tw, th }, { 0, th } };
                            } else if (!vert) {
                                int64_t x0b = std::max<int64_t>(0, -da), x0t = std::max<int64_t>(0, da);
                                int64_t x1b = tw - std::max<int64_t>(0, db), x1t = tw + std::min<int64_t>(0, db);
                                sh.pts = { { x0b, 0 }, { x1b, 0 }, { x1t, th }, { x0t, th } };
                            } else {
                                int64_t y0l = std::max<int64_t>(0, -da), y0r = std::max<int64_t>(0, da);
                                int64_t y1l = th - std::max<int64_t>(0, db), y1r = th + std::min<int64_t>(0, db);
                                sh.pts = { { 0, y0l }, { tw, y0r }, { tw, y1r }, { 0, y1l } };
                            }
                            sh.repOffsets = std::move(reps);
                            curShapes.push_back(std::move(sh));
                        }
                    } else if (recId == 26) { // CIRCLE
                        uint8_t info = RdByte(b, n, p);
                        if (info & 0x01u) mod.layer    = RdU(b, n, p);
                        if (info & 0x02u) mod.datatype = RdU(b, n, p);
                        if (info & 0x20u) mod.circleR  = RdU(b, n, p);
                        if (info & 0x10u) { int64_t d = RdS(b, n, p); mod.geomX = mod.xyAbs ? d : (mod.geomX + d); }
                        if (info & 0x08u) { int64_t d = RdS(b, n, p); mod.geomY = mod.xyAbs ? d : (mod.geomY + d); }
                        std::vector<OasisPt> reps = { { 0, 0 } };
                        if (info & 0x04u) { RdRepetition(b, n, p, mod.modalRep); reps = mod.modalRep; }
                        if (ok && mod.circleR > 0 && curShapes.size() < 16384) {
                            OasisShape sh;
                            sh.kind = 4; sh.layer = mod.layer;
                            sh.x = mod.geomX; sh.y = mod.geomY;
                            sh.w = mod.circleR;
                            sh.repOffsets = std::move(reps);
                            curShapes.push_back(std::move(sh));
                        }
                    } else {
                        ok = false;
                        break;
                    }
                }
            }
        };

        OasisParser parser;
        parser.ParseStream(data, size, 13, 0);
        if (!parser.ok || !parser.sawStart || parser.cells.empty()) return false;

        double toUm = 1.0 / parser.unit;
        auto mapLayer = [](uint64_t lyr) -> EdaLayerId {
            switch (lyr & 3ULL) {
            case 1: return EdaLayerId::TopCopper;
            case 2: return EdaLayerId::BottomCopper;
            case 3: return EdaLayerId::TopSilk;
            default: return EdaLayerId::InnerCopper;
            }
        };
        auto findCellIdx = [&](const std::string& nm) -> int {
            for (size_t i = 0; i < parser.cells.size(); ++i) {
                if (parser.cells[i].name == nm) return static_cast<int>(i);
            }
            return -1;
        };

        // Determine root cells (cells not instantiated via PLACEMENT by any other cell)
        std::vector<bool> referenced(parser.cells.size(), false);
        for (const auto& c : parser.cells) {
            for (const auto& s : c.shapes) {
                if (s.kind == 6) {
                    int idx = findCellIdx(s.str);
                    if (idx >= 0) referenced[static_cast<size_t>(idx)] = true;
                }
            }
        }

        struct EmitCtx {
            const OasisParser& P;
            ParsedEdaBoard& out;
            double toUm;
            void EmitCell(int cellIdx, double oxUm, double oyUm, double scale, double rotDeg, bool flipY, int depth) {
                if (depth > 8 || out.traces.size() >= 25000) return;
                const auto& cell = P.cells[static_cast<size_t>(cellIdx)];
                double rad = rotDeg * 3.14159265358979323846 / 180.0;
                double cs = std::cos(rad), sn = std::sin(rad);
                auto xform = [&](double lxUm, double lyUm) -> std::pair<float, float> {
                    double sy = flipY ? -lyUm : lyUm;
                    double sx = lxUm * scale, syy = sy * scale;
                    double rx = oxUm + sx * cs - syy * sn;
                    double ry = oyUm + sx * sn + syy * cs;
                    return { static_cast<float>(rx), static_cast<float>(ry) };
                };
                auto mapL = [](uint64_t lyr) -> EdaLayerId {
                    switch (lyr & 3ULL) {
                    case 1: return EdaLayerId::TopCopper;
                    case 2: return EdaLayerId::BottomCopper;
                    case 3: return EdaLayerId::TopSilk;
                    default: return EdaLayerId::InnerCopper;
                    }
                };

                for (const auto& sh : cell.shapes) {
                    if (out.traces.size() >= 25000) break;
                    EdaLayerId lid = mapL(sh.layer);
                    for (const auto& ro : sh.repOffsets) {
                        if (out.traces.size() >= 25000) break;
                        double bx = static_cast<double>(sh.x + ro.x) * toUm;
                        double by = static_cast<double>(sh.y + ro.y) * toUm;
                        if (sh.kind == 1) { // RECTANGLE
                            double rw = static_cast<double>(sh.w) * toUm;
                            double rh = static_cast<double>(sh.h) * toUm;
                            if (rw <= 0.0 && rh <= 0.0) continue;
                            auto p0 = xform(bx, by);
                            auto p1 = xform(bx + rw, by);
                            auto p2 = xform(bx + rw, by + rh);
                            auto p3 = xform(bx, by + rh);
                            out.traces.push_back({ p0.first, p0.second, p1.first, p1.second, 0.18f, lid, static_cast<int>(sh.layer + 1) });
                            out.traces.push_back({ p1.first, p1.second, p2.first, p2.second, 0.18f, lid, static_cast<int>(sh.layer + 1) });
                            out.traces.push_back({ p2.first, p2.second, p3.first, p3.second, 0.18f, lid, static_cast<int>(sh.layer + 1) });
                            out.traces.push_back({ p3.first, p3.second, p0.first, p0.second, 0.18f, lid, static_cast<int>(sh.layer + 1) });
                        } else if (sh.kind == 2 && sh.pts.size() >= 3) { // POLYGON / TRAPEZOID
                            size_t m = sh.pts.size();
                            for (size_t k = 0; k < m; ++k) {
                                size_t kn = (k + 1) % m;
                                auto a = xform(bx + static_cast<double>(sh.pts[k].x) * toUm, by + static_cast<double>(sh.pts[k].y) * toUm);
                                auto b = xform(bx + static_cast<double>(sh.pts[kn].x) * toUm, by + static_cast<double>(sh.pts[kn].y) * toUm);
                                out.traces.push_back({ a.first, a.second, b.first, b.second, 0.18f, lid, static_cast<int>(sh.layer + 1) });
                            }
                        } else if (sh.kind == 3 && sh.pts.size() >= 2) { // PATH
                            float pw = std::clamp(static_cast<float>(static_cast<double>(sh.w) * toUm * scale), 0.12f, 4.0f);
                            for (size_t k = 1; k < sh.pts.size(); ++k) {
                                auto a = xform(bx + static_cast<double>(sh.pts[k - 1].x) * toUm, by + static_cast<double>(sh.pts[k - 1].y) * toUm);
                                auto b = xform(bx + static_cast<double>(sh.pts[k].x) * toUm,     by + static_cast<double>(sh.pts[k].y) * toUm);
                                out.traces.push_back({ a.first, a.second, b.first, b.second, pw, lid, static_cast<int>(sh.layer + 1) });
                            }
                        } else if (sh.kind == 4 && out.circles.size() < 8192) { // CIRCLE
                            auto c = xform(bx, by);
                            float r = std::max(0.05f, static_cast<float>(static_cast<double>(sh.w) * toUm * scale));
                            out.circles.push_back({ c.first, c.second, r, std::min(0.20f, r * 0.35f), false, lid });
                        } else if (sh.kind == 5 && out.labels.size() < 4096) { // TEXT
                            auto c = xform(bx, by);
                            out.labels.push_back({ c.first, c.second, 0.85f, EdaLayerId::TopSilk, PathUtils::Utf8ToWide(sh.str) });
                        } else if (sh.kind == 6) { // PLACEMENT
                            auto c = xform(bx, by);
                            int subIdx = -1;
                            for (size_t ci = 0; ci < P.cells.size(); ++ci) {
                                if (P.cells[ci].name == sh.str) { subIdx = static_cast<int>(ci); break; }
                            }
                            if (subIdx >= 0 && subIdx != cellIdx) {
                                EmitCell(subIdx, c.first, c.second, scale * sh.mag, rotDeg + sh.deg, flipY ^ sh.flip, depth + 1);
                            } else if (out.pads.size() < 4096) {
                                std::wstring wnm = PathUtils::Utf8ToWide(sh.str);
                                float sz = static_cast<float>(2.0 * sh.mag * scale);
                                out.pads.push_back({ c.first, c.second, sz, sz, static_cast<float>(rotDeg + sh.deg), 0.0f, 0, EdaLayerId::TopSilk, wnm, wnm });
                                out.labels.push_back({ c.first, c.second, 0.7f, EdaLayerId::TopSilk, wnm });
                            }
                        }
                    }
                }
            }
        };

        (void)mapLayer;
        EmitCtx emitter{ parser, out, toUm };
        bool anyRoot = false;
        for (size_t i = 0; i < parser.cells.size(); ++i) {
            if (!referenced[i]) {
                anyRoot = true;
                emitter.EmitCell(static_cast<int>(i), 0.0, 0.0, 1.0, 0.0, false, 0);
            }
        }
        if (!anyRoot) {
            emitter.EmitCell(0, 0.0, 0.0, 1.0, 0.0, false, 0);
        }

        if (out.traces.empty() && out.circles.empty() && out.pads.empty() && out.labels.empty()) {
            return false;
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
        if (size < 14) return false;
        bool isSofMagic = (std::memcmp(data, "SOF\0", 4) == 0);
        bool isPofMagic = (std::memcmp(data, "POF\0", 4) == 0);
        if (!isSofMagic && !isPofMagic) return false;

        uint32_t packetCount = DetailEda200::RdU32LE(data + 4);
        if (packetCount == 0 || packetCount > 4096) return false;

        std::string devStr;
        std::string creatorStr;
        std::vector<uint8_t> cfgBytes;
        size_t p = 8;
        for (uint32_t pk = 0; pk < packetCount; ++pk) {
            if (p + 6 > size) return false;
            uint16_t tag  = DetailEda200::RdU16LE(data + p);
            uint32_t plen = DetailEda200::RdU32LE(data + p + 2);
            p += 6;
            if (plen > size - p) return false;

            const uint8_t* pay = data + p;
            auto extractCleanAscii = [](const uint8_t* b, size_t n) -> std::string {
                size_t s = 0;
                while (s < n && (b[s] == 0 || b[s] == ' ' || b[s] == '\t')) ++s;
                size_t e = s;
                while (e < n && b[e] >= 32 && b[e] <= 126) ++e;
                while (e > s && b[e - 1] == ' ') --e;
                return (e > s) ? std::string(reinterpret_cast<const char*>(b + s), e - s) : std::string{};
            };

            if (tag == 1 && creatorStr.empty() && plen > 0 && plen <= 512) {
                creatorStr = extractCleanAscii(pay, plen);
            } else if (tag == 2 && devStr.empty() && plen > 0 && plen <= 256) {
                devStr = extractCleanAscii(pay, plen);
            } else if (plen >= 16) {
                if (cfgBytes.size() < 4 * 1024 * 1024) {
                    size_t take = std::min<size_t>(plen, 4 * 1024 * 1024 - cfgBytes.size());
                    cfgBytes.insert(cfgBytes.end(), pay, pay + take);
                }
            }
            if (devStr.empty() && tag != 1 && plen >= 4 && plen <= 64) {
                std::string cand = extractCleanAscii(pay, plen);
                if (cand.size() >= 4 && isalnum(static_cast<unsigned char>(cand[0]))) devStr = cand;
            }
            p += plen;
        }

        if (cfgBytes.size() < 16) return false;
        if (devStr.empty()) devStr = isPofMagic ? "Altera_POF" : "Altera_SOF";

        out.kind = EdaDocKind::KiCadPcb;
        std::wstring wDev = PathUtils::Utf8ToWide(devStr);
        out.formatTitle = (isPofMagic ? L"Intel/Altera POF Flash Bitstream (" : L"Intel/Altera SOF FPGA Bitstream (") + wDev + L")";

        int side = std::clamp(static_cast<int>(std::sqrt(static_cast<double>(cfgBytes.size()) / 4.0)), 4, 16);
        int cols = side, rows = side;
        int numSec = cols * rows;
        float pitch = 2.8f;
        float pkgW = cols * pitch + 5.0f, pkgH = rows * pitch + 5.0f;
        DetailEda100::AddRectOutline(out, 0.0f, 0.0f, pkgW, pkgH, EdaLayerId::EdgeCuts, 0.42f);

        auto popcnt8 = [](uint8_t v) -> int {
            v = static_cast<uint8_t>((v & 0x55u) + ((v >> 1) & 0x55u));
            v = static_cast<uint8_t>((v & 0x33u) + ((v >> 2) & 0x33u));
            return static_cast<int>((v + (v >> 4)) & 0x0Fu);
        };

        std::vector<float> secDensity(static_cast<size_t>(numSec), 0.0f);
        for (int s = 0; s < numSec; ++s) {
            size_t b0 = (static_cast<size_t>(s) * cfgBytes.size()) / static_cast<size_t>(numSec);
            size_t b1 = (static_cast<size_t>(s + 1) * cfgBytes.size()) / static_cast<size_t>(numSec);
            if (b1 <= b0) continue;
            size_t activeBits = 0;
            for (size_t i = b0; i < b1; ++i) {
                uint8_t bv = isPofMagic ? static_cast<uint8_t>(~cfgBytes[i]) : cfgBytes[i];
                activeBits += static_cast<size_t>(popcnt8(bv));
            }
            secDensity[static_cast<size_t>(s)] = static_cast<float>(activeBits) / static_cast<float>((b1 - b0) * 8u);
        }

        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                float sx = (c - (cols - 1) * 0.5f) * pitch;
                float sy = ((rows - 1) * 0.5f - r) * pitch;
                float dens = secDensity[static_cast<size_t>(r * cols + c)];
                if (dens <= 0.0f) continue;

                float padSz = 0.75f + dens * 1.55f;
                EdaLayerId lid = (dens >= 0.55f) ? EdaLayerId::TopCopper :
                                 (dens >= 0.20f) ? EdaLayerId::InnerCopper : EdaLayerId::BottomCopper;
                std::wstring sName = L"S" + std::to_wstring(r) + L"_" + std::to_wstring(c);
                out.pads.push_back({ sx, sy, padSz, padSz, 0.0f, 0.0f, 1, lid, sName, L"CFG" });

                if (c + 1 < cols && secDensity[static_cast<size_t>(r * cols + c + 1)] > 0.0f) {
                    float nx = ((c + 1) - (cols - 1) * 0.5f) * pitch;
                    out.traces.push_back({ sx, sy, nx, sy, 0.12f + dens * 0.22f, lid, r + 1 });
                }
                if (r + 1 < rows && secDensity[static_cast<size_t>((r + 1) * cols + c)] > 0.0f) {
                    float ny = ((rows - 1) * 0.5f - (r + 1)) * pitch;
                    out.traces.push_back({ sx, sy, sx, ny, 0.12f + dens * 0.22f, lid, r + 1 });
                }
            }
        }

        EdaComponent3D die;
        die.refDes = L"U1_FPGA";
        die.value = wDev;
        die.footprint = isPofMagic ? L"CFM_FLASH" : L"FPGA_DIE";
        die.widthMm = cols * pitch;
        die.lengthMm = rows * pitch;
        die.heightMm = 1.6f;
        die.category = EdaPartCategory::IC_Package;
        out.components.push_back(std::move(die));

        out.labels.push_back({ -pkgW * 0.46f, pkgH * 0.5f + 2.0f, 1.25f, EdaLayerId::TopSilk,
            wDev + L" (" + std::to_wstring(packetCount) + L" pkts, " + std::to_wstring(cfgBytes.size()) + L" B)" });
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

        std::vector<float> samples;
        std::wstring fmtTitle;
        std::wstring metaLabel;

        // 1. Tektronix Binary WFM (0x0F0F LE / 0xF0F0 BE + ":WFM#001" / ":WFM#002" / ":WFM#003")
        bool isTekLe = (size >= 780 && data[0] == 0x0Fu && data[1] == 0x0Fu);
        bool isTekBe = (size >= 780 && data[0] == 0xF0u && data[1] == 0xF0u);
        if ((isTekLe || isTekBe) && std::memcmp(data + 2, ":WFM#00", 7) == 0 &&
            (data[9] == '1' || data[9] == '2' || data[9] == '3'))
        {
            int wfmVer = data[9] - '0';
            size_t minHdr = (wfmVer == 3) ? 838u : 780u;
            if (size < minHdr) return false;
            bool be = isTekBe;

            auto rdU16 = [&](size_t o) -> uint16_t {
                return be ? static_cast<uint16_t>((static_cast<uint16_t>(data[o]) << 8) | data[o + 1])
                          : DetailEda200::RdU16LE(data + o);
            };
            auto rdU32 = [&](size_t o) -> uint32_t {
                return be ? ((static_cast<uint32_t>(data[o]) << 24) |
                             (static_cast<uint32_t>(data[o + 1]) << 16) |
                             (static_cast<uint32_t>(data[o + 2]) << 8) |
                             static_cast<uint32_t>(data[o + 3]))
                          : DetailEda200::RdU32LE(data + o);
            };
            auto rdU64 = [&](size_t o) -> uint64_t {
                return be ? ((static_cast<uint64_t>(rdU32(o)) << 32) | rdU32(o + 4))
                          : DetailEda200::RdU64LE(data + o);
            };
            auto rdF64 = [&](size_t o) -> double {
                uint64_t bits = rdU64(o);
                double dv = 0.0;
                std::memcpy(&dv, &bits, 8);
                return dv;
            };

            uint8_t bpp = data[15];
            uint32_t curveBufOff = rdU32(16);
            if ((bpp != 1 && bpp != 2 && bpp != 4 && bpp != 8) || curveBufOff < minHdr || curveBufOff >= size) {
                return false;
            }

            size_t ed1 = (wfmVer == 1) ? 154u : 168u;
            double dimScale  = rdF64(ed1 + 0);
            double dimOffset = rdF64(ed1 + 8);
            int32_t expFmt   = static_cast<int32_t>(rdU32(ed1 + 72));
            if (!std::isfinite(dimScale) || std::fabs(dimScale) < 1e-18 || std::fabs(dimScale) > 1e12) dimScale = 1.0;
            if (!std::isfinite(dimOffset) || std::fabs(dimOffset) > 1e12) dimOffset = 0.0;

            size_t sStart = curveBufOff;
            size_t sEnd   = size;
            size_t cobj   = curveBufOff - 30u;
            uint32_t dStartOff = rdU32(cobj + 14);
            uint32_t dPostOff  = rdU32(cobj + 18);
            if (dPostOff > dStartOff && curveBufOff + static_cast<size_t>(dPostOff) <= size &&
                (dPostOff - dStartOff) >= static_cast<uint32_t>(bpp) * 2u)
            {
                sStart = curveBufOff + static_cast<size_t>(dStartOff);
                sEnd   = curveBufOff + static_cast<size_t>(dPostOff);
            }

            size_t totalPts = (sEnd - sStart) / bpp;
            if (totalPts < 2) return false;
            size_t stepPts = std::max<size_t>(1, totalPts / 1024);
            samples.reserve(std::min<size_t>(totalPts, 1025));

            for (size_t idx = 0; idx < totalPts && samples.size() < 1024; idx += stepPts) {
                size_t off = sStart + idx * bpp;
                double raw = 0.0;
                if (bpp == 1) {
                    raw = (expFmt == 7) ? static_cast<double>(data[off]) : static_cast<double>(static_cast<int8_t>(data[off]));
                } else if (bpp == 2) {
                    uint16_t u = rdU16(off);
                    raw = static_cast<double>(static_cast<int16_t>(u));
                } else if (bpp == 4) {
                    uint32_t u = rdU32(off);
                    if (expFmt == 4) {
                        float fv = 0.0f; std::memcpy(&fv, &u, 4);
                        raw = std::isfinite(fv) ? static_cast<double>(fv) : 0.0;
                    } else if (expFmt == 2) {
                        raw = static_cast<double>(u);
                    } else {
                        raw = static_cast<double>(static_cast<int32_t>(u));
                    }
                } else {
                    uint64_t u = rdU64(off);
                    if (expFmt == 5 || expFmt == 6) {
                        double dv = 0.0; std::memcpy(&dv, &u, 8);
                        raw = std::isfinite(dv) ? dv : 0.0;
                    } else {
                        raw = static_cast<double>(static_cast<int64_t>(u));
                    }
                }
                samples.push_back(static_cast<float>(raw * dimScale + dimOffset));
            }
            fmtTitle = L"Tektronix WFM#00" + std::to_wstring(wfmVer) + L" Oscilloscope Waveform (.wfm)";
            metaLabel = L"Tek WFM#00" + std::to_wstring(wfmVer) + L" (" + std::to_wstring(totalPts) + L" pts)";
        }
        // 2. Rigol Binary WFM (0xA5 0xA5 header magic)
        else if (ext == L".wfm" && size >= 260 && data[0] == 0xA5u && data[1] == 0xA5u) {
            size_t dataOff = 256u;
            uint32_t candPts = DetailEda200::RdU32LE(data + 28);
            size_t avail = size - dataOff;
            size_t totalPts = (candPts >= 2 && candPts <= avail) ? static_cast<size_t>(candPts) : avail;
            if (totalPts < 2) return false;

            float vScale = 1.0f;
            uint32_t vsRaw = DetailEda200::RdU32LE(data + 36);
            if (vsRaw >= 100u && vsRaw <= 50000000u) {
                vScale = static_cast<float>(vsRaw) * 1e-6f / 25.0f; // Rigol uV/div -> V/LSB
            }
            size_t stepPts = std::max<size_t>(1, totalPts / 1024);
            for (size_t i = 0; i < totalPts && samples.size() < 1024; i += stepPts) {
                int adc = static_cast<int>(data[dataOff + i]) - 128;
                samples.push_back(static_cast<float>(adc) * vScale);
            }
            fmtTitle = L"Rigol Digital Oscilloscope Waveform (.wfm)";
            metaLabel = L"Rigol WFM (" + std::to_wstring(totalPts) + L" pts)";
        }
        // 3. Tektronix ISF (Internal Save Format: WFMPRE / BYT_NR + CURVE #...)
        else {
            size_t hdrScan = std::min<size_t>(size, 4096);
            std::string upHdr(reinterpret_cast<const char*>(data), hdrScan);
            for (char& c : upHdr) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));

            bool hasIsfPre = (upHdr.find("BYT_N") != std::string::npos ||
                              upHdr.find(":WFMP") != std::string::npos ||
                              upHdr.find("WFMPRE") != std::string::npos);
            size_t curvePos = upHdr.find("CURVE");
            size_t kwLen = 5;
            if (curvePos == std::string::npos) { curvePos = upHdr.find("CURV"); kwLen = 4; }
            if (!hasIsfPre || curvePos == std::string::npos) return false;

            std::string pre = upHdr.substr(0, curvePos);
            auto parseTagF = [&](const char* tag, double defVal) -> double {
                size_t tp = pre.find(tag);
                if (tp == std::string::npos) return defVal;
                tp += std::strlen(tag);
                while (tp < pre.size() && (pre[tp] == ' ' || pre[tp] == '\t' || pre[tp] == ':')) ++tp;
                char* ep = nullptr;
                double v = std::strtod(pre.c_str() + tp, &ep);
                return (ep != pre.c_str() + tp && std::isfinite(v)) ? v : defVal;
            };

            int bytNr = static_cast<int>(parseTagF("BYT_NR", parseTagF("BYT_N", 1.0)));
            if (bytNr != 1 && bytNr != 2 && bytNr != 4) bytNr = 1;
            bool msbFirst = (pre.find("LSB") == std::string::npos); // MSB is Tektronix ISF default
            bool isUnsigned = (pre.find("BN_FMT RP") != std::string::npos || pre.find("BN_F RP") != std::string::npos);
            bool isFloat    = (pre.find("BN_FMT FP") != std::string::npos || pre.find("BN_F FP") != std::string::npos);
            double ymult = parseTagF("YMULT", parseTagF("YMU", 1.0));
            double yoff  = parseTagF("YOFF",  parseTagF("YOF", 0.0));
            double yzero = parseTagF("YZERO", parseTagF("YZE", 0.0));
            if (std::fabs(ymult) < 1e-18) ymult = 1.0;

            size_t cp = curvePos + kwLen;
            while (cp < size && (data[cp] == ' ' || data[cp] == '\t')) ++cp;
            if (cp >= size) return false;

            if (data[cp] == '#') {
                if (cp + 2 > size || data[cp + 1] < '0' || data[cp + 1] > '9') return false;
                int ndig = data[cp + 1] - '0';
                size_t sOff = 0, byteCnt = 0;
                if (ndig == 0) {
                    sOff = cp + 2;
                    byteCnt = size - sOff;
                } else {
                    if (cp + 2 + static_cast<size_t>(ndig) > size) return false;
                    for (int d = 0; d < ndig; ++d) {
                        uint8_t ch = data[cp + 2 + d];
                        if (ch < '0' || ch > '9') return false;
                        byteCnt = byteCnt * 10u + static_cast<size_t>(ch - '0');
                    }
                    sOff = cp + 2 + static_cast<size_t>(ndig);
                }
                if (sOff >= size) return false;
                size_t avail = std::min<size_t>(byteCnt, size - sOff);
                size_t totalPts = avail / static_cast<size_t>(bytNr);
                if (totalPts < 2) return false;
                size_t stepPts = std::max<size_t>(1, totalPts / 1024);
                for (size_t idx = 0; idx < totalPts && samples.size() < 1024; idx += stepPts) {
                    size_t o = sOff + idx * static_cast<size_t>(bytNr);
                    double raw = 0.0;
                    if (bytNr == 1) {
                        raw = isUnsigned ? static_cast<double>(data[o]) : static_cast<double>(static_cast<int8_t>(data[o]));
                    } else if (bytNr == 2) {
                        uint16_t u = msbFirst ? static_cast<uint16_t>((static_cast<uint16_t>(data[o]) << 8) | data[o + 1])
                                              : DetailEda200::RdU16LE(data + o);
                        raw = isUnsigned ? static_cast<double>(u) : static_cast<double>(static_cast<int16_t>(u));
                    } else {
                        uint32_t u = msbFirst ? ((static_cast<uint32_t>(data[o]) << 24) |
                                                 (static_cast<uint32_t>(data[o + 1]) << 16) |
                                                 (static_cast<uint32_t>(data[o + 2]) << 8) |
                                                 static_cast<uint32_t>(data[o + 3]))
                                              : DetailEda200::RdU32LE(data + o);
                        if (isFloat) {
                            float fv = 0.0f; std::memcpy(&fv, &u, 4);
                            raw = std::isfinite(fv) ? static_cast<double>(fv) : 0.0;
                        } else {
                            raw = isUnsigned ? static_cast<double>(u) : static_cast<double>(static_cast<int32_t>(u));
                        }
                    }
                    samples.push_back(static_cast<float>((raw - yoff) * ymult + yzero));
                }
            } else {
                // ASCII CURVE values
                std::string asc(reinterpret_cast<const char*>(data + cp), std::min<size_t>(size - cp, 65536));
                const char* ap = asc.c_str();
                while (*ap && samples.size() < 1024) {
                    while (*ap == ' ' || *ap == '\t' || *ap == ',' || *ap == '\r' || *ap == '\n') ++ap;
                    if (!*ap || *ap == ';') break;
                    char* ep = nullptr;
                    double raw = std::strtod(ap, &ep);
                    if (ep == ap) break;
                    if (std::isfinite(raw)) samples.push_back(static_cast<float>((raw - yoff) * ymult + yzero));
                    ap = ep;
                }
            }
            fmtTitle = L"Tektronix ISF Oscilloscope Trace (.isf)";
            metaLabel = L"Tek ISF (" + std::to_wstring(samples.size()) + L" pts)";
        }

        if (samples.size() < 2) return false;

        out.kind = EdaDocKind::KiCadSch;
        out.formatTitle = fmtTitle;
        DetailEda200::DrawGraticule(out, -40.0f, -25.0f, 80.0f, 50.0f, 10, 8);

        float mn = samples[0], mx = samples[0];
        for (float v : samples) { mn = std::min(mn, v); mx = std::max(mx, v); }
        float span = std::max(1e-6f, mx - mn);

        // Zero-volt ground reference indicator on graticule
        float yZeroLine = std::clamp(-20.0f + ((0.0f - mn) / span) * 40.0f, -24.0f, 24.0f);
        out.traces.push_back({ -40.0f, yZeroLine, -38.5f, yZeroLine, 0.28f, EdaLayerId::InnerCopper, 2 });

        for (size_t i = 1; i < samples.size(); ++i) {
            float x0 = -39.0f + (static_cast<float>(i - 1) / static_cast<float>(samples.size() - 1)) * 78.0f;
            float x1 = -39.0f + (static_cast<float>(i)     / static_cast<float>(samples.size() - 1)) * 78.0f;
            float y0 = -20.0f + ((samples[i - 1] - mn) / span) * 40.0f;
            float y1 = -20.0f + ((samples[i]     - mn) / span) * 40.0f;
            out.traces.push_back({ x0, y0, x1, y1, 0.30f, EdaLayerId::TopCopper, 1 });
        }
        out.labels.push_back({ -39.0f, 26.8f, 1.15f, EdaLayerId::TopSilk, metaLabel });
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
