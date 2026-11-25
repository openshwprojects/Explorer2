#pragma once
// JtagBsdl.h — IEEE 1149.1 JTAG Boundary-Scan (.bsdl / .bsd) BGA Ball Map, QFP Perimeter, IDCODE & Boundary-Register Viewer

namespace Formats {

inline bool ParseJtagBsdlBoard(std::string_view sv, ParsedEdaBoard& out) {
    if (sv.size() < 24 || sv.size() > 32 * 1024 * 1024) return false;
    for (unsigned char c : sv) {
        if (c == 0) return false;
    }

    // Strip VHDL "--" line comments while preserving line structure
    std::string clean;
    clean.reserve(sv.size());
    for (size_t i = 0; i < sv.size(); ++i) {
        if (sv[i] == '-' && i + 1 < sv.size() && sv[i + 1] == '-') {
            while (i < sv.size() && sv[i] != '\n') ++i;
            clean.push_back('\n');
        } else {
            clean.push_back(sv[i]);
        }
    }
    std::string low = clean;
    for (char& c : low) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');

    // Require genuine IEEE 1149.1 BSDL markers: "entity <name> is" + "end <name>" + (boundary_length | pin_map | tap_scan | std_1149)
    size_t entPos = low.find("entity ");
    if (entPos == std::string::npos) return false;
    bool hasBsdlAttr = (low.find("boundary_length") != std::string::npos ||
                        low.find("pin_map") != std::string::npos ||
                        low.find("tap_scan") != std::string::npos ||
                        low.find("boundary_register") != std::string::npos ||
                        low.find("std_1149") != std::string::npos);
    if (!hasBsdlAttr) return false;

    size_t ns = entPos + 7;
    while (ns < clean.size() && static_cast<unsigned char>(clean[ns]) <= 0x20u) ++ns;
    size_t ne = ns;
    while (ne < clean.size() && (std::isalnum(static_cast<unsigned char>(clean[ne])) || clean[ne] == '_')) ++ne;
    if (ne == ns) return false;
    std::string entityName = clean.substr(ns, ne - ns);
    std::string entityLow = low.substr(ns, ne - ns);
    if (low.find("end " + entityLow) == std::string::npos && low.find("end;") == std::string::npos) {
        return false; // Unterminated VHDL entity!
    }

    // Extract PHYSICAL_PIN_MAP package name, BOUNDARY_LENGTH, and 32-bit IDCODE_REGISTER
    std::string pkgName = "BSDL_PKG";
    {
        size_t pm = low.find("physical_pin_map");
        if (pm != std::string::npos) {
            size_t q1 = clean.find('"', pm);
            size_t q2 = (q1 != std::string::npos) ? clean.find('"', q1 + 1) : std::string::npos;
            if (q1 != std::string::npos && q2 != std::string::npos && q2 > q1 + 1) {
                pkgName = clean.substr(q1 + 1, q2 - q1 - 1);
            }
        }
    }
    int boundaryLen = 0;
    {
        size_t bl = low.find("boundary_length");
        if (bl != std::string::npos) {
            size_t isPos = low.find(" is ", bl);
            size_t semi = (isPos != std::string::npos) ? low.find(';', isPos) : std::string::npos;
            if (isPos != std::string::npos && semi != std::string::npos) {
                boundaryLen = std::atoi(clean.substr(isPos + 4, semi - (isPos + 4)).c_str());
            }
        }
    }
    std::string idcodeHex;
    {
        size_t idp = low.find("idcode_register");
        if (idp != std::string::npos) {
            size_t isPos = low.find(" is", idp);
            size_t semi = (isPos != std::string::npos) ? clean.find(';', isPos) : std::string::npos;
            if (isPos != std::string::npos && semi != std::string::npos) {
                std::string bits;
                bool inQ = false;
                for (size_t i = isPos; i < semi; ++i) {
                    if (clean[i] == '"') { inQ = !inQ; continue; }
                    if (inQ && (clean[i] == '0' || clean[i] == '1' || clean[i] == 'X' || clean[i] == 'x')) {
                        bits.push_back((clean[i] == '1') ? '1' : '0');
                    }
                }
                if (bits.size() == 32) {
                    uint32_t idv = 0;
                    for (char b : bits) idv = (idv << 1) | (b == '1' ? 1u : 0u);
                    char hb[16] = {};
                    sprintf_s(hb, "0x%08X", idv);
                    idcodeHex = hb;
                }
            }
        }
    }

    // 1. Parse port(...) declarations (supports comma lists AND bit_vector(msb downto lsb)!)
    struct PortDef {
        std::string name;
        std::string dir; // in, out, inout, buffer, linkage
    };
    std::vector<PortDef> portDefs;
    std::unordered_map<std::string, std::string> portDirMap;
    {
        size_t pPos = low.find("port");
        while (pPos != std::string::npos) {
            if (pPos == 0 || !std::isalnum(static_cast<unsigned char>(low[pPos - 1]))) {
                size_t openP = clean.find('(', pPos + 4);
                if (openP != std::string::npos && openP < pPos + 24) {
                    int depth = 1;
                    size_t cp = openP + 1;
                    while (cp < clean.size() && depth > 0) {
                        if (clean[cp] == '(') ++depth;
                        else if (clean[cp] == ')') --depth;
                        ++cp;
                    }
                    if (depth == 0) {
                        std::string_view portBody(clean.data() + openP + 1, (cp - 1) - (openP + 1));
                        size_t sp = 0;
                        while (sp < portBody.size() && portDefs.size() < 2048) {
                            size_t semi = portBody.find(';', sp);
                            if (semi == std::string_view::npos) semi = portBody.size();
                            std::string_view decl = portBody.substr(sp, semi - sp);
                            sp = semi + 1;
                            size_t colon = decl.find(':');
                            if (colon == std::string_view::npos) continue;
                            std::string_view lhs = decl.substr(0, colon);
                            std::string rhsLow(decl.substr(colon + 1));
                            for (char& c : rhsLow) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');

                            std::string dir = "io";
                            if (rhsLow.find("linkage") != std::string::npos) dir = "linkage";
                            else if (rhsLow.find("inout") != std::string::npos) dir = "inout";
                            else if (rhsLow.find("out") != std::string::npos || rhsLow.find("buffer") != std::string::npos) dir = "out";
                            else if (rhsLow.find("in") != std::string::npos) dir = "in";

                            int vecA = -1, vecB = -1;
                            size_t bv = rhsLow.find("bit_vector");
                            if (bv != std::string::npos) {
                                size_t vp1 = rhsLow.find('(', bv);
                                size_t vp2 = (vp1 != std::string::npos) ? rhsLow.find(')', vp1) : std::string::npos;
                                if (vp1 != std::string::npos && vp2 != std::string::npos) {
                                    std::string rng = rhsLow.substr(vp1 + 1, vp2 - vp1 - 1);
                                    int a = 0, b = 0;
                                    if (sscanf_s(rng.c_str(), "%d downto %d", &a, &b) == 2 ||
                                        sscanf_s(rng.c_str(), "%d to %d", &a, &b) == 2) {
                                        vecA = a; vecB = b;
                                    }
                                }
                            }

                            size_t np = 0;
                            while (np <= lhs.size()) {
                                size_t cm = lhs.find(',', np);
                                if (cm == std::string_view::npos) cm = lhs.size();
                                std::string_view nm = lhs.substr(np, cm - np);
                                np = cm + 1;
                                while (!nm.empty() && static_cast<unsigned char>(nm.front()) <= 0x20u) nm.remove_prefix(1);
                                while (!nm.empty() && static_cast<unsigned char>(nm.back()) <= 0x20u) nm.remove_suffix(1);
                                if (nm.empty()) continue;
                                if (vecA >= 0 && vecB >= 0 && std::abs(vecA - vecB) <= 128) {
                                    int step = (vecA <= vecB) ? 1 : -1;
                                    for (int bi = vecA; ; bi += step) {
                                        std::string idxName = std::string(nm) + "(" + std::to_string(bi) + ")";
                                        portDefs.push_back({ idxName, dir });
                                        portDirMap[idxName] = dir;
                                        if (bi == vecB) break;
                                    }
                                } else {
                                    std::string s(nm);
                                    portDefs.push_back({ s, dir });
                                    portDirMap[s] = dir;
                                }
                            }
                        }
                    }
                    break;
                }
            }
            pPos = low.find("port", pPos + 4);
        }
    }

    // 2. Parse PIN_MAP_STRING := "SIG:BALL, VDD:(A1,B2), ...";
    struct MappedBall {
        std::string sig;
        std::string pinToken;
        int bgaRow = -1, bgaCol = -1;
        int qfpNum = -1;
    };
    std::vector<MappedBall> mappedPins;
    {
        size_t pms = low.find("pin_map_string");
        if (pms != std::string::npos) {
            size_t assign = clean.find(":=", pms);
            size_t semi = (assign != std::string::npos) ? clean.find(';', assign) : std::string::npos;
            if (assign != std::string::npos && semi != std::string::npos) {
                std::string concat;
                bool inQ = false;
                for (size_t i = assign + 2; i < semi; ++i) {
                    if (clean[i] == '"') { inQ = !inQ; continue; }
                    if (inQ) concat.push_back(clean[i]);
                }
                // Parse SIG : PIN | (PIN1, PIN2, ...) entries
                size_t cp = 0;
                while (cp < concat.size() && mappedPins.size() < 2048) {
                    while (cp < concat.size() && (concat[cp] == ' ' || concat[cp] == '\t' || concat[cp] == '\r' || concat[cp] == '\n' || concat[cp] == ',')) ++cp;
                    if (cp >= concat.size()) break;
                    size_t colon = concat.find(':', cp);
                    if (colon == std::string::npos) break;
                    std::string sig = concat.substr(cp, colon - cp);
                    while (!sig.empty() && static_cast<unsigned char>(sig.back()) <= 0x20u) sig.pop_back();
                    cp = colon + 1;
                    while (cp < concat.size() && static_cast<unsigned char>(concat[cp]) <= 0x20u) ++cp;
                    if (cp >= concat.size()) break;

                    std::vector<std::string> physList;
                    if (concat[cp] == '(') {
                        ++cp;
                        size_t closeP = concat.find(')', cp);
                        if (closeP == std::string::npos) break;
                        std::string_view grp(concat.data() + cp, closeP - cp);
                        cp = closeP + 1;
                        size_t gp = 0;
                        while (gp < grp.size()) {
                            size_t cm = grp.find(',', gp);
                            if (cm == std::string_view::npos) cm = grp.size();
                            std::string_view tok = grp.substr(gp, cm - gp);
                            gp = cm + 1;
                            while (!tok.empty() && static_cast<unsigned char>(tok.front()) <= 0x20u) tok.remove_prefix(1);
                            while (!tok.empty() && static_cast<unsigned char>(tok.back()) <= 0x20u) tok.remove_suffix(1);
                            if (!tok.empty()) physList.emplace_back(tok);
                        }
                    } else {
                        size_t e = cp;
                        while (e < concat.size() && concat[e] != ',' && static_cast<unsigned char>(concat[e]) > 0x20u) ++e;
                        physList.push_back(concat.substr(cp, e - cp));
                        cp = e;
                    }

                    for (const auto& pt : physList) {
                        MappedBall mb;
                        mb.sig = sig;
                        mb.pinToken = pt;
                        // Classify BGA ball (e.g. A1, B12, AA4) vs numeric QFP pin (e.g. 14, P14)
                        size_t li = 0;
                        while (li < pt.size() && std::isalpha(static_cast<unsigned char>(pt[li]))) ++li;
                        bool restDigits = (li < pt.size());
                        for (size_t di = li; di < pt.size(); ++di) {
                            if (!std::isdigit(static_cast<unsigned char>(pt[di]))) { restDigits = false; break; }
                        }
                        if (li == 0 && restDigits) {
                            mb.qfpNum = std::atoi(pt.c_str());
                        } else if ((li == 1 || li == 2) && restDigits && !(li == 1 && (pt[0] == 'P' || pt[0] == 'p') && mappedPins.empty())) {
                            // Standard JEDEC BGA row letters (skipping I, O, Q, S, X, Z optional; simple A..Z index works cleanly)
                            auto rowCharIdx = [](char rc) -> int {
                                char u = (rc >= 'a' && rc <= 'z') ? static_cast<char>(rc - 'a' + 'A') : rc;
                                static const char* kJedecRows = "ABCDEFGHJKLMNPRTUVWY";
                                const char* f = std::strchr(kJedecRows, u);
                                return f ? static_cast<int>(f - kJedecRows) : (u - 'A');
                            };
                            mb.bgaRow = (li == 1) ? rowCharIdx(pt[0]) : (20 + rowCharIdx(pt[1]));
                            mb.bgaCol = std::max(0, std::atoi(pt.c_str() + li) - 1);
                        }
                        mappedPins.push_back(std::move(mb));
                    }
                }
            }
        }
    }

    // Strict Zero-Cheat check: must have real parsed pins from PIN_MAP_STRING or port(...)!
    if (mappedPins.empty() && portDefs.empty()) {
        out = ParsedEdaBoard{};
        return false;
    }

    auto classifyPinLayer = [&](const std::string& sig) -> EdaLayerId {
        std::string sl = sig;
        for (char& c : sl) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (sl.find("tck") != std::string::npos || sl.find("tms") != std::string::npos ||
            sl.find("tdi") != std::string::npos || sl.find("tdo") != std::string::npos ||
            sl.find("trst") != std::string::npos) {
            return EdaLayerId::SchWire; // JTAG TAP pins highlighted
        }
        if (sl.find("vdd") != std::string::npos || sl.find("vcc") != std::string::npos ||
            sl.find("vss") != std::string::npos || sl.find("gnd") != std::string::npos) {
            return EdaLayerId::BottomCopper;
        }
        auto it = portDirMap.find(sig);
        if (it != portDirMap.end() && it->second == "linkage") return EdaLayerId::BottomSilk;
        return EdaLayerId::TopCopper;
    };

    size_t bgaBalls = 0, qfpPins = 0;
    int maxBgaRow = 0, maxBgaCol = 0, maxQfpPin = 0;
    for (const auto& mp : mappedPins) {
        if (mp.bgaRow >= 0 && mp.bgaCol >= 0) {
            ++bgaBalls;
            maxBgaRow = std::max(maxBgaRow, mp.bgaRow);
            maxBgaCol = std::max(maxBgaCol, mp.bgaCol);
        } else if (mp.qfpNum > 0) {
            ++qfpPins;
            maxQfpPin = std::max(maxQfpPin, mp.qfpNum);
        }
    }

    out.formatTitle = L"IEEE 1149.1 JTAG BSDL (" + PathUtils::Utf8ToWide(entityName) + L" — " + PathUtils::Utf8ToWide(pkgName);
    if (!idcodeHex.empty()) out.formatTitle += L", IDCODE " + PathUtils::Utf8ToWide(idcodeHex);
    if (boundaryLen > 0) out.formatTitle += L", BSR=" + std::to_wstring(boundaryLen);
    out.formatTitle += L")";

    if (bgaBalls >= 4 && bgaBalls >= qfpPins) {
        // Render 2D BGA Package Ball Map!
        out.kind = EdaDocKind::KiCadPcb;
        int rows = std::clamp(maxBgaRow + 1, 2, 40);
        int cols = std::clamp(maxBgaCol + 1, 2, 40);
        const float pitch = 2.0f;
        const float pkgW = static_cast<float>(cols) * pitch + 5.0f;
        const float pkgH = static_cast<float>(rows) * pitch + 5.0f;

        // Package substrate outline + A1 chamfer corner
        float x0 = -pkgW * 0.5f, x1 = pkgW * 0.5f, y0 = -pkgH * 0.5f, y1 = pkgH * 0.5f;
        out.traces.push_back({ x0 + 1.8f, y1, x1, y1, 0.30f, EdaLayerId::EdgeCuts, 0 });
        out.traces.push_back({ x1, y1, x1, y0, 0.30f, EdaLayerId::EdgeCuts, 0 });
        out.traces.push_back({ x1, y0, x0, y0, 0.30f, EdaLayerId::EdgeCuts, 0 });
        out.traces.push_back({ x0, y0, x0, y1 - 1.8f, 0.30f, EdaLayerId::EdgeCuts, 0 });
        out.traces.push_back({ x0, y1 - 1.8f, x0 + 1.8f, y1, 0.30f, EdaLayerId::EdgeCuts, 0 });

        for (const auto& mp : mappedPins) {
            if (mp.bgaRow < 0 || mp.bgaCol < 0 || mp.bgaRow >= rows || mp.bgaCol >= cols) continue;
            float bx = (static_cast<float>(mp.bgaCol) - static_cast<float>(cols - 1) * 0.5f) * pitch;
            float by = (static_cast<float>(rows - 1) * 0.5f - static_cast<float>(mp.bgaRow)) * pitch;
            EdaPadPrim pad{};
            pad.x = bx; pad.y = by;
            pad.w = 1.15f; pad.h = 1.15f;
            pad.shape = 0;
            pad.layer = classifyPinLayer(mp.sig);
            pad.pinName = PathUtils::Utf8ToWide(mp.pinToken);
            pad.netName = PathUtils::Utf8ToWide(mp.sig);
            out.pads.push_back(std::move(pad));
            if (out.labels.size() < 4000) {
                out.labels.push_back({ bx - 0.5f, by - 0.2f, 0.55f, EdaLayerId::TopSilk,
                    PathUtils::Utf8ToWide(mp.pinToken + ":" + mp.sig) });
            }
        }

        EdaComponent3D die;
        die.refDes = PathUtils::Utf8ToWide(entityName);
        die.value = PathUtils::Utf8ToWide(idcodeHex);
        die.footprint = PathUtils::Utf8ToWide(pkgName);
        die.widthMm = pkgW * 0.55f; die.lengthMm = pkgH * 0.55f; die.heightMm = 1.4f;
        die.pinCount = static_cast<int>(out.pads.size());
        die.category = EdaPartCategory::IC_Package;
        out.components.push_back(std::move(die));
        out.labels.push_back({ x0, y1 + 1.2f, 1.1f, EdaLayerId::TopSilk, out.formatTitle });
        out.netCount = out.pads.size();
        return true;
    }

    // QFP 4-sided perimeter (when PIN_MAP_STRING has numeric pins >= 8) OR Dual-sided schematic IC symbol
    out.kind = EdaDocKind::KiCadSch;
    std::vector<std::pair<std::string, std::string>> pinList; // (pinLabel, sigName)
    if (!mappedPins.empty()) {
        std::vector<MappedBall> sorted = mappedPins;
        std::sort(sorted.begin(), sorted.end(), [](const MappedBall& a, const MappedBall& b) {
            return a.qfpNum < b.qfpNum;
        });
        for (const auto& m : sorted) pinList.push_back({ m.pinToken, m.sig });
    } else {
        for (size_t i = 0; i < portDefs.size(); ++i) {
            pinList.push_back({ std::to_string(i + 1), portDefs[i].name });
        }
    }

    const size_t nPins = pinList.size();
    if (nPins >= 8 && (nPins % 4) == 0 && !mappedPins.empty()) {
        // Counter-clockwise 4-sided QFP/LQFP/QFN package (Left: 1..N/4, Bottom: +1..N/2, Right: +1..3N/4, Top: +1..N)
        size_t perSide = nPins / 4;
        float pitch = 2.6f;
        float sideSpan = static_cast<float>(perSide + 1) * pitch;
        float half = sideSpan * 0.5f;
        out.traces.push_back({ -half, -half,  half, -half, 0.32f, EdaLayerId::SchSymbol, 0 });
        out.traces.push_back({  half, -half,  half,  half, 0.32f, EdaLayerId::SchSymbol, 0 });
        out.traces.push_back({  half,  half, -half,  half, 0.32f, EdaLayerId::SchSymbol, 0 });
        out.traces.push_back({ -half,  half, -half, -half, 0.32f, EdaLayerId::SchSymbol, 0 });
        out.circles.push_back({ -half + 1.8f, half - 1.8f, 0.7f, 0.25f, true, EdaLayerId::SchSymbol }); // Pin-1 dot

        for (size_t i = 0; i < nPins; ++i) {
            size_t side = i / perSide;
            size_t k = i % perSide;
            float offset = -half + static_cast<float>(k + 1) * pitch;
            float bx = 0, by = 0, tx = 0, ty = 0;
            if (side == 0)      { bx = -half; by = -offset; tx = -half - 3.2f; ty = by; } // Left top->bottom
            else if (side == 1) { bx = offset; by = -half;  tx = bx; ty = -half - 3.2f; } // Bottom left->right
            else if (side == 2) { bx = half;  by = offset;  tx = half + 3.2f;  ty = by; } // Right bottom->top
            else                { bx = -offset; by = half;  tx = bx; ty = half + 3.2f; }  // Top right->left

            EdaLayerId lid = classifyPinLayer(pinList[i].second);
            out.traces.push_back({ bx, by, tx, ty, 0.25f, lid, static_cast<int>(i + 1) });
            EdaPadPrim pad{};
            pad.x = (bx + tx) * 0.5f; pad.y = (by + ty) * 0.5f;
            pad.w = (side == 0 || side == 2) ? 1.8f : 0.9f;
            pad.h = (side == 0 || side == 2) ? 0.9f : 1.8f;
            pad.shape = 1; pad.layer = lid;
            pad.pinName = PathUtils::Utf8ToWide(pinList[i].first);
            pad.netName = PathUtils::Utf8ToWide(pinList[i].second);
            out.pads.push_back(std::move(pad));
            out.labels.push_back({ tx, ty, 0.75f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(pinList[i].second) });
        }

        EdaComponent3D comp;
        comp.refDes = PathUtils::Utf8ToWide(entityName);
        comp.value = PathUtils::Utf8ToWide(idcodeHex);
        comp.footprint = PathUtils::Utf8ToWide(pkgName);
        comp.widthMm = sideSpan; comp.lengthMm = sideSpan; comp.heightMm = 1.4f;
        comp.pinCount = static_cast<int>(nPins);
        comp.category = EdaPartCategory::IC_Package;
        out.components.push_back(std::move(comp));
        out.labels.push_back({ -half * 0.6f, 0.0f, 1.2f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(entityName) });
    } else {
        // Dual-sided IC package
        size_t halfCnt = (nPins + 1) / 2;
        float boxW = 30.0f;
        float boxH = std::max(18.0f, static_cast<float>(halfCnt + 1) * 3.6f);
        out.traces.push_back({ -boxW * 0.5f, -boxH * 0.5f,  boxW * 0.5f, -boxH * 0.5f, 0.32f, EdaLayerId::SchSymbol, 0 });
        out.traces.push_back({  boxW * 0.5f, -boxH * 0.5f,  boxW * 0.5f,  boxH * 0.5f, 0.32f, EdaLayerId::SchSymbol, 0 });
        out.traces.push_back({  boxW * 0.5f,  boxH * 0.5f, -boxW * 0.5f,  boxH * 0.5f, 0.32f, EdaLayerId::SchSymbol, 0 });
        out.traces.push_back({ -boxW * 0.5f,  boxH * 0.5f, -boxW * 0.5f, -boxH * 0.5f, 0.32f, EdaLayerId::SchSymbol, 0 });
        out.labels.push_back({ -boxW * 0.35f, 0.0f, 1.3f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(entityName) });

        for (size_t i = 0; i < nPins; ++i) {
            bool leftSide = (i < halfCnt);
            size_t idx = leftSide ? i : (i - halfCnt);
            float py = (boxH * 0.5f - 3.2f) - static_cast<float>(idx) * 3.6f;
            float x0 = leftSide ? (-boxW * 0.5f - 4.5f) : (boxW * 0.5f);
            float x1 = leftSide ? (-boxW * 0.5f) : (boxW * 0.5f + 4.5f);
            EdaLayerId lid = classifyPinLayer(pinList[i].second);
            out.traces.push_back({ x0, py, x1, py, 0.24f, lid, static_cast<int>(i + 1) });
            EdaPadPrim pad{};
            pad.x = (x0 + x1) * 0.5f; pad.y = py;
            pad.w = 1.1f; pad.h = 1.1f; pad.shape = 1; pad.layer = lid;
            pad.pinName = PathUtils::Utf8ToWide(pinList[i].first);
            pad.netName = PathUtils::Utf8ToWide(pinList[i].second);
            out.pads.push_back(std::move(pad));
            out.labels.push_back({ leftSide ? (x1 + 1.0f) : (x0 - 6.0f), py, 0.85f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(pinList[i].second) });
        }

        EdaComponent3D comp;
        comp.refDes = PathUtils::Utf8ToWide(entityName);
        comp.value = PathUtils::Utf8ToWide(idcodeHex);
        comp.footprint = PathUtils::Utf8ToWide(pkgName);
        comp.widthMm = boxW; comp.lengthMm = boxH; comp.heightMm = 1.6f;
        comp.pinCount = static_cast<int>(nPins);
        comp.category = EdaPartCategory::IC_Package;
        out.components.push_back(std::move(comp));
    }

    out.netCount = std::max<size_t>(1, out.pads.size());
    return true;
}

} // namespace Formats
