#pragma once
// AsicDef.h — #25 ASIC Design Exchange Format (.def) v5.8 Rectilinear DIEAREA, Orientations, PINS, SPECIALNETS, Routed * Wildcards & VIAS

namespace Formats {

inline bool IsLikelyAsicDefBytes(const uint8_t* data, size_t size) {
    if (!data || size < 16) return false;
    size_t scan = std::min<size_t>(size, 4096);
    for (size_t i = 0; i < scan; ++i) {
        if (data[i] == 0) return false;
    }
    std::string_view sv(reinterpret_cast<const char*>(data), scan);
    if (sv.find("EXPORTS") != std::string_view::npos && sv.find("DIEAREA") == std::string_view::npos) {
        return false; // Windows DLL module-definition .def file
    }
    return sv.find("DIEAREA") != std::string_view::npos ||
           sv.find("COMPONENTS") != std::string_view::npos ||
           (sv.find("DESIGN ") != std::string_view::npos && sv.find("END DESIGN") != std::string_view::npos);
}

inline void TransformDefCellLocal(
    float lx, float ly, float cellW, float cellH,
    std::string_view orient,
    float& outDx, float& outDy)
{
    // Standard LEF/DEF 8 cell orientations placing lower-left bounding box at (0, 0):
    // N: (x, y), S: (W-x, H-y), FN: (W-x, y), FS: (x, H-y)
    // W: (H-y, x), E: (y, W-x), FW: (y, x), FE: (H-y, W-x)
    if (orient == "S")       { outDx = cellW - lx; outDy = cellH - ly; }
    else if (orient == "FN") { outDx = cellW - lx; outDy = ly; }
    else if (orient == "FS") { outDx = lx;         outDy = cellH - ly; }
    else if (orient == "W")  { outDx = cellH - ly; outDy = lx; }
    else if (orient == "E")  { outDx = ly;         outDy = cellW - lx; }
    else if (orient == "FW") { outDx = ly;         outDy = lx; }
    else if (orient == "FE") { outDx = cellH - ly; outDy = cellW - lx; }
    else                     { outDx = lx;         outDy = ly; } // N
}

inline bool ParseAsicDefBoard(std::string_view sv, ParsedEdaBoard& out) {
    if (sv.size() < 16 || sv.size() > 64 * 1024 * 1024) return false;
    for (unsigned char c : sv) {
        if (c == 0) return false;
    }
    if (!IsLikelyAsicDefBytes(reinterpret_cast<const uint8_t*>(sv.data()), sv.size())) {
        return false;
    }

    // Load any companion .lef files from the same directory so standard-cell dimensions & pins match!
    std::unordered_map<std::string, LefMacroSummary> companionMacros;
    if (!out.filePath.empty()) {
        size_t sep = out.filePath.find_last_of(L"\\/");
        if (sep != std::wstring::npos) {
            std::wstring dir = out.filePath.substr(0, sep);
            WIN32_FIND_DATAW fd{};
            HANDLE hFind = FindFirstFileW((dir + L"\\*.lef").c_str(), &fd);
            if (hFind != INVALID_HANDLE_VALUE) {
                int lefLoaded = 0;
                do {
                    if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 && lefLoaded < 8) {
                        std::vector<uint8_t> lefBytes;
                        if (PathUtils::ReadFileBytes(dir + L"\\" + fd.cFileName, 8 * 1024 * 1024, lefBytes) && !lefBytes.empty()) {
                            std::string_view lsv(reinterpret_cast<const char*>(lefBytes.data()), lefBytes.size());
                            ScanLefMacrosForDef(lsv, companionMacros);
                            ++lefLoaded;
                        }
                    }
                } while (FindNextFileW(hFind, &fd));
                FindClose(hFind);
            }
        }
    }

    size_t pos = 0;
    auto nextTok = [&]() -> std::string_view {
        while (pos < sv.size()) {
            unsigned char c = static_cast<unsigned char>(sv[pos]);
            if (c <= 0x20u) { ++pos; continue; }
            if (c == '#') {
                while (pos < sv.size() && sv[pos] != '\n') ++pos;
                continue;
            }
            break;
        }
        if (pos >= sv.size()) return {};
        if (sv[pos] == ';' || sv[pos] == '(' || sv[pos] == ')') {
            return sv.substr(pos++, 1);
        }
        size_t s = pos;
        while (pos < sv.size() && static_cast<unsigned char>(sv[pos]) > 0x20u &&
               sv[pos] != ';' && sv[pos] != '(' && sv[pos] != ')') {
            ++pos;
        }
        return sv.substr(s, pos - s);
    };

    auto parseDbu = [](std::string_view t, double& outVal) -> bool {
        if (t.empty() || t == ";" || t == "(" || t == ")") return false;
        char b[64] = {};
        std::memcpy(b, t.data(), std::min<size_t>(t.size(), 63));
        char* endp = nullptr;
        double v = std::strtod(b, &endp);
        if (endp == b || !std::isfinite(v)) return false;
        outVal = v;
        return true;
    };

    std::string designName, defVersion;
    double dbuPerMicron = 100.0;
    std::vector<std::pair<double, double>> diePtsDbu;

    struct DefCompRec {
        std::string inst, macro, orient = "N";
        double xDbu = 0.0, yDbu = 0.0;
        bool placed = false;
    };
    struct DefIoPinRec {
        std::string name, net, layer, orient = "N";
        double xDbu = 0.0, yDbu = 0.0;
        double rx1 = -40.0, ry1 = -40.0, rx2 = 40.0, ry2 = 40.0;
        bool placed = false;
    };
    struct DefWireSeg {
        double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
        double widthDbu = 0;
        EdaLayerId layer = EdaLayerId::TopCopper;
        int netId = 1;
        bool isSpecial = false;
    };
    struct DefViaInst {
        double x = 0, y = 0;
        double sizeDbu = 0;
        int netId = 1;
    };

    std::vector<DefCompRec> comps;
    std::vector<DefIoPinRec> ioPins;
    std::vector<DefWireSeg> wires;
    std::vector<DefViaInst> viaInsts;
    size_t totalNets = 0;
    bool openSection = false;
    bool sawEndDesign = false;

    auto parseRouteSection = [&](bool isSpecial) -> bool {
        openSection = true;
        // Skip optional count + ';'
        while (pos < sv.size()) {
            std::string_view t = nextTok();
            if (t.empty()) return false;
            if (t == ";") break;
            if (t == "END") { nextTok(); openSection = false; return true; }
        }

        int curNetId = static_cast<int>(totalNets);
        while (pos < sv.size()) {
            std::string_view t = nextTok();
            if (t.empty()) return false; // Unclosed NETS/SPECIALNETS!
            if (t == "END") {
                std::string_view secEnd = nextTok();
                (void)secEnd;
                openSection = false;
                return true;
            }
            if (t == "-") {
                std::string_view netNm = nextTok();
                if (!netNm.empty()) {
                    ++totalNets;
                    curNetId = static_cast<int>(totalNets);
                }
                // Parse connections & + ROUTED / NEW clauses up to ';'
                while (pos < sv.size()) {
                    std::string_view nt = nextTok();
                    if (nt.empty()) return false;
                    if (nt == ";") break;
                    if (nt == "(") {
                        // Connection tuple ( comp pin ) or ( PIN ioPin )
                        while (pos < sv.size()) {
                            std::string_view ct = nextTok();
                            if (ct.empty() || ct == ")") break;
                        }
                        continue;
                    }
                    if (nt == "ROUTED" || nt == "FIXED" || nt == "COVER" || nt == "NOSHIELD" || nt == "NEW") {
                        // Layer token next
                        std::string_view lyrTok = nextTok();
                        if (lyrTok.empty() || lyrTok == ";") break;
                        EdaLayerId lid = MapAsicMetalLayer(lyrTok);
                        double wireWidthDbu = 0.0;

                        // In SPECIALNETS, widthDbu immediately follows layerName!
                        // In regular NETS, optional TAPER / TAPERRULE / STYLE / SHAPE may appear before '('
                        for (;;) {
                            size_t saveP = pos;
                            std::string_view peek = nextTok();
                            if (peek.empty()) return false;
                            if (peek == "(" || peek == ";") { pos = saveP; break; }
                            double maybeW = 0.0;
                            if (parseDbu(peek, maybeW) && maybeW > 0.0 && wireWidthDbu == 0.0) {
                                wireWidthDbu = maybeW;
                            } else if (peek == "SHAPE" || peek == "TAPERRULE" || peek == "STYLE" || peek == "MASK") {
                                nextTok(); // argument
                            }
                        }

                        // Parse chain of routing points ( x|* y|* [ext] ) ... [viaName]
                        bool hasPrevPt = false;
                        double prevX = 0.0, prevY = 0.0;
                        for (;;) {
                            size_t saveP = pos;
                            std::string_view ptok = nextTok();
                            if (ptok.empty()) return false;
                            if (ptok == ";") { pos = saveP; break; }
                            if (ptok == "+") break;
                            if (ptok == "NEW") {
                                // Continuation branch on a new layer!
                                lyrTok = nextTok();
                                if (lyrTok.empty() || lyrTok == ";") { pos = saveP; break; }
                                lid = MapAsicMetalLayer(lyrTok);
                                wireWidthDbu = 0.0;
                                hasPrevPt = false;
                                // Optional width in SPECIALNETS NEW <layer> <width>
                                size_t sp2 = pos;
                                std::string_view pk2 = nextTok();
                                double w2 = 0.0;
                                if (pk2 != "(" && pk2 != ";" && parseDbu(pk2, w2) && w2 > 0.0) {
                                    wireWidthDbu = w2;
                                } else {
                                    pos = sp2;
                                }
                                continue;
                            }
                            if (ptok == "(") {
                                std::string_view tx = nextTok();
                                std::string_view ty = nextTok();
                                // Consume optional wire extension up to ')'
                                for (;;) {
                                    std::string_view cpar = nextTok();
                                    if (cpar.empty() || cpar == ")") break;
                                }
                                double curX = prevX, curY = prevY;
                                if (tx != "*") { if (!parseDbu(tx, curX)) continue; }
                                else if (!hasPrevPt) continue;
                                if (ty != "*") { if (!parseDbu(ty, curY)) continue; }
                                else if (!hasPrevPt) continue;

                                if (hasPrevPt && wires.size() < 120000) {
                                    wires.push_back({ prevX, prevY, curX, curY, wireWidthDbu, lid, curNetId, isSpecial });
                                }
                                prevX = curX;
                                prevY = curY;
                                hasPrevPt = true;
                            } else if (ptok == "MASK" || ptok == "RECT" || ptok == "VIRTUAL") {
                                if (ptok == "MASK") nextTok();
                                else if (ptok == "RECT") {
                                    // RECT ( dx1 dy1 dx2 dy2 )
                                    while (pos < sv.size() && nextTok() != ")") {}
                                }
                            } else {
                                // Inline via instance name at (prevX, prevY)! (e.g. M2_M1, via1_2, VIA12)
                                if (hasPrevPt && viaInsts.size() < 60000) {
                                    viaInsts.push_back({ prevX, prevY, wireWidthDbu, curNetId });
                                }
                            }
                        }
                    }
                }
            }
        }
        return false;
    };

    while (pos < sv.size()) {
        std::string_view t = nextTok();
        if (t.empty()) break;

        if (t == "VERSION") {
            std::string_view v = nextTok();
            if (!v.empty() && v != ";") defVersion = std::string(v);
        } else if (t == "DESIGN") {
            std::string_view dn = nextTok();
            if (!dn.empty() && dn != ";") designName = std::string(dn);
        } else if (t == "UNITS") {
            // UNITS DISTANCE MICRONS <dbu> ;
            std::string_view u1 = nextTok();
            std::string_view u2 = nextTok();
            std::string_view u3 = nextTok();
            double dbu = 100.0;
            if (u1 == "DISTANCE" && u2 == "MICRONS" && parseDbu(u3, dbu) && dbu >= 1.0 && dbu <= 100000.0) {
                dbuPerMicron = dbu;
            }
        } else if (t == "DIEAREA") {
            while (pos < sv.size()) {
                std::string_view dt = nextTok();
                if (dt.empty() || dt == ";") break;
                if (dt == "(") {
                    double dx = 0, dy = 0;
                    std::string_view tx = nextTok();
                    std::string_view ty = nextTok();
                    std::string_view cp = nextTok();
                    if (cp == ")" && parseDbu(tx, dx) && parseDbu(ty, dy)) {
                        diePtsDbu.push_back({ dx, dy });
                    }
                }
            }
        } else if (t == "COMPONENTS") {
            openSection = true;
            while (pos < sv.size()) {
                std::string_view ct = nextTok();
                if (ct.empty()) { out = ParsedEdaBoard{}; return false; }
                if (ct == ";") break;
            }
            while (pos < sv.size()) {
                std::string_view ct = nextTok();
                if (ct.empty()) { out = ParsedEdaBoard{}; return false; } // Truncated COMPONENTS!
                if (ct == "END") {
                    nextTok(); // COMPONENTS
                    openSection = false;
                    break;
                }
                if (ct == "-") {
                    DefCompRec rec;
                    rec.inst = std::string(nextTok());
                    rec.macro = std::string(nextTok());
                    while (pos < sv.size()) {
                        std::string_view pt = nextTok();
                        if (pt.empty()) { out = ParsedEdaBoard{}; return false; }
                        if (pt == ";") break;
                        if (pt == "PLACED" || pt == "FIXED" || pt == "COVER") {
                            if (nextTok() == "(") {
                                parseDbu(nextTok(), rec.xDbu);
                                parseDbu(nextTok(), rec.yDbu);
                                nextTok(); // ')'
                                std::string_view ori = nextTok();
                                if (!ori.empty() && ori != ";" && ori != "+") rec.orient = std::string(ori);
                                rec.placed = true;
                                if (ori == ";") break;
                            }
                        }
                    }
                    if (!rec.inst.empty() && comps.size() < 40000) {
                        comps.push_back(std::move(rec));
                    }
                }
            }
        } else if (t == "PINS") {
            openSection = true;
            while (pos < sv.size()) {
                std::string_view pt = nextTok();
                if (pt.empty()) { out = ParsedEdaBoard{}; return false; }
                if (pt == ";") break;
            }
            while (pos < sv.size()) {
                std::string_view pt = nextTok();
                if (pt.empty()) { out = ParsedEdaBoard{}; return false; }
                if (pt == "END") {
                    nextTok(); // PINS
                    openSection = false;
                    break;
                }
                if (pt == "-") {
                    DefIoPinRec pin;
                    pin.name = std::string(nextTok());
                    while (pos < sv.size()) {
                        std::string_view kt = nextTok();
                        if (kt.empty()) { out = ParsedEdaBoard{}; return false; }
                        if (kt == ";") break;
                        if (kt == "NET") {
                            pin.net = std::string(nextTok());
                        } else if (kt == "LAYER") {
                            pin.layer = std::string(nextTok());
                            size_t sp = pos;
                            if (nextTok() == "(") {
                                parseDbu(nextTok(), pin.rx1);
                                parseDbu(nextTok(), pin.ry1);
                                nextTok(); // ')'
                                if (nextTok() == "(") {
                                    parseDbu(nextTok(), pin.rx2);
                                    parseDbu(nextTok(), pin.ry2);
                                    nextTok(); // ')'
                                }
                            } else {
                                pos = sp;
                            }
                        } else if (kt == "PLACED" || kt == "FIXED" || kt == "COVER") {
                            if (nextTok() == "(") {
                                parseDbu(nextTok(), pin.xDbu);
                                parseDbu(nextTok(), pin.yDbu);
                                nextTok(); // ')'
                                std::string_view ori = nextTok();
                                if (!ori.empty() && ori != ";" && ori != "+") pin.orient = std::string(ori);
                                pin.placed = true;
                                if (ori == ";") break;
                            }
                        }
                    }
                    if (!pin.name.empty() && ioPins.size() < 16000) {
                        ioPins.push_back(std::move(pin));
                    }
                }
            }
        } else if (t == "SPECIALNETS") {
            if (!parseRouteSection(true)) { out = ParsedEdaBoard{}; return false; }
        } else if (t == "NETS") {
            if (!parseRouteSection(false)) { out = ParsedEdaBoard{}; return false; }
        } else if (t == "VIAS" || t == "BLOCKAGES" || t == "REGIONS" || t == "GROUPS" || t == "SCANCHAINS") {
            std::string secName(t);
            while (pos < sv.size()) {
                std::string_view st = nextTok();
                if (st.empty()) { out = ParsedEdaBoard{}; return false; }
                if (st == "END") {
                    std::string_view en = nextTok();
                    if (en == secName) break;
                }
            }
        } else if (t == "END") {
            std::string_view what = nextTok();
            if (what == "DESIGN") {
                sawEndDesign = true;
                break;
            }
        }
    }

    if (openSection || (!sawEndDesign && !designName.empty())) {
        out = ParsedEdaBoard{};
        return false;
    }

    // Compute board unit scale: convert DBU -> microns, then normalize so layout spans ~40..120 mm comfortably
    double minUmX = 1e30, maxUmX = -1e30, minUmY = 1e30, maxUmY = -1e30;
    auto expUm = [&](double ux, double uy) {
        minUmX = std::min(minUmX, ux); maxUmX = std::max(maxUmX, ux);
        minUmY = std::min(minUmY, uy); maxUmY = std::max(maxUmY, uy);
    };
    for (const auto& dp : diePtsDbu) expUm(dp.first / dbuPerMicron, dp.second / dbuPerMicron);
    for (const auto& c : comps) if (c.placed) expUm(c.xDbu / dbuPerMicron, c.yDbu / dbuPerMicron);
    double spanUm = std::max(maxUmX - minUmX, maxUmY - minUmY);
    double umToBoard = 1.0;
    if (spanUm > 200.0) umToBoard = 80.0 / spanUm;
    else if (spanUm > 0.1 && spanUm < 20.0) umToBoard = 40.0 / spanUm;
    const double dbuToBoard = umToBoard / dbuPerMicron;

    out.kind = EdaDocKind::KiCadPcb;
    out.formatTitle = L"ASIC Design Exchange Format (.def";
    if (!defVersion.empty()) out.formatTitle += L" v" + PathUtils::Utf8ToWide(defVersion);
    if (!designName.empty()) out.formatTitle += L" — " + PathUtils::Utf8ToWide(designName);
    out.formatTitle += L")";

    // 1. Render DIEAREA (2-point bbox OR rectilinear multi-vertex L/T-shaped polygon)
    if (diePtsDbu.size() == 2) {
        float x1 = static_cast<float>(diePtsDbu[0].first * dbuToBoard);
        float y1 = static_cast<float>(diePtsDbu[0].second * dbuToBoard);
        float x2 = static_cast<float>(diePtsDbu[1].first * dbuToBoard);
        float y2 = static_cast<float>(diePtsDbu[1].second * dbuToBoard);
        out.traces.push_back({ x1, y1, x2, y1, 0.28f, EdaLayerId::EdgeCuts, 0 });
        out.traces.push_back({ x2, y1, x2, y2, 0.28f, EdaLayerId::EdgeCuts, 0 });
        out.traces.push_back({ x2, y2, x1, y2, 0.28f, EdaLayerId::EdgeCuts, 0 });
        out.traces.push_back({ x1, y2, x1, y1, 0.28f, EdaLayerId::EdgeCuts, 0 });
    } else if (diePtsDbu.size() >= 3) {
        for (size_t i = 0; i < diePtsDbu.size(); ++i) {
            size_t j = (i + 1) % diePtsDbu.size();
            out.traces.push_back({
                static_cast<float>(diePtsDbu[i].first * dbuToBoard),
                static_cast<float>(diePtsDbu[i].second * dbuToBoard),
                static_cast<float>(diePtsDbu[j].first * dbuToBoard),
                static_cast<float>(diePtsDbu[j].second * dbuToBoard),
                0.28f, EdaLayerId::EdgeCuts, 0
            });
        }
    }

    // 2. Render placed standard cells & macros (with companion LEF geometry + orientation when available!)
    for (const auto& c : comps) {
        float bx0 = static_cast<float>(c.xDbu * dbuToBoard);
        float by0 = static_cast<float>(c.yDbu * dbuToBoard);

        float cellWUm = 2.4f, cellHUm = 1.6f;
        const LefMacroSummary* lefM = nullptr;
        auto itM = companionMacros.find(c.macro);
        if (itM != companionMacros.end()) {
            lefM = &itM->second;
            cellWUm = lefM->widthUm;
            cellHUm = lefM->heightUm;
        }
        const bool rot90 = (c.orient == "E" || c.orient == "W" || c.orient == "FE" || c.orient == "FW");
        float boxW = std::max(0.45f, static_cast<float>((rot90 ? cellHUm : cellWUm) * umToBoard));
        float boxH = std::max(0.45f, static_cast<float>((rot90 ? cellWUm : cellHUm) * umToBoard));
        // If no companion LEF and default cell is tiny/huge relative to die, clamp nicely
        if (!lefM && diePtsDbu.size() >= 2) {
            boxW = std::clamp(boxW, 1.2f, 4.0f);
            boxH = std::clamp(boxH, 0.9f, 3.0f);
        }

        float cx = lefM ? (bx0 + boxW * 0.5f) : bx0;
        float cy = lefM ? (by0 + boxH * 0.5f) : by0;

        EdaComponent3D comp;
        comp.refDes = PathUtils::Utf8ToWide(c.inst);
        comp.value = PathUtils::Utf8ToWide(c.macro);
        comp.footprint = PathUtils::Utf8ToWide("STDCELL_" + c.orient);
        comp.x = cx;
        comp.y = cy;
        comp.widthMm = boxW;
        comp.lengthMm = boxH;
        comp.heightMm = 0.6f;
        if (c.orient == "W" || c.orient == "FW") comp.rotDeg = 90.0f;
        else if (c.orient == "S" || c.orient == "FS") comp.rotDeg = 180.0f;
        else if (c.orient == "E" || c.orient == "FE") comp.rotDeg = 270.0f;
        comp.bottomSide = (c.orient.size() == 2 && c.orient[0] == 'F');
        comp.pinCount = lefM ? std::max(2, static_cast<int>(lefM->pinRects.size())) : 3;
        comp.category = EdaPartCategory::IC_Package;
        out.components.push_back(comp);

        if (lefM && !lefM->pinRects.empty()) {
            // Cell outline + transformed LEF pin pads!
            out.traces.push_back({ bx0,        by0,        bx0 + boxW, by0,        0.10f, EdaLayerId::TopSilk, 0 });
            out.traces.push_back({ bx0 + boxW, by0,        bx0 + boxW, by0 + boxH, 0.10f, EdaLayerId::TopSilk, 0 });
            out.traces.push_back({ bx0 + boxW, by0 + boxH, bx0,        by0 + boxH, 0.10f, EdaLayerId::TopSilk, 0 });
            out.traces.push_back({ bx0,        by0 + boxH, bx0,        by0,        0.10f, EdaLayerId::TopSilk, 0 });
            for (const auto& pr : lefM->pinRects) {
                if (out.pads.size() >= 60000) break;
                float dx1 = 0, dy1 = 0, dx2 = 0, dy2 = 0;
                TransformDefCellLocal(pr.x1, pr.y1, cellWUm, cellHUm, c.orient, dx1, dy1);
                TransformDefCellLocal(pr.x2, pr.y2, cellWUm, cellHUm, c.orient, dx2, dy2);
                EdaPadPrim pad{};
                pad.x = bx0 + (dx1 + dx2) * 0.5f * static_cast<float>(umToBoard);
                pad.y = by0 + (dy1 + dy2) * 0.5f * static_cast<float>(umToBoard);
                pad.w = std::max(0.14f, std::fabs(dx2 - dx1) * static_cast<float>(umToBoard));
                pad.h = std::max(0.14f, std::fabs(dy2 - dy1) * static_cast<float>(umToBoard));
                pad.shape = 1;
                pad.layer = pr.layer;
                pad.pinName = PathUtils::Utf8ToWide(pr.pinName);
                out.pads.push_back(std::move(pad));
            }
        } else if (out.pads.size() < 60000) {
            EdaPadPrim pad{};
            pad.x = cx; pad.y = cy;
            pad.w = boxW * 0.85f; pad.h = boxH * 0.80f;
            pad.shape = 1;
            pad.layer = EdaLayerId::TopCopper;
            out.pads.push_back(pad);
        }
        if (out.labels.size() < 8000) {
            out.labels.push_back({ cx, cy, 0.72f, EdaLayerId::TopSilk, comp.refDes });
        }
    }

    // 3. Render edge I/O PINS
    for (const auto& p : ioPins) {
        if (!p.placed || out.pads.size() >= 60000) continue;
        float px = static_cast<float>(p.xDbu * dbuToBoard);
        float py = static_cast<float>(p.yDbu * dbuToBoard);
        float pw = std::max(0.35f, static_cast<float>(std::fabs(p.rx2 - p.rx1) * dbuToBoard));
        float ph = std::max(0.35f, static_cast<float>(std::fabs(p.ry2 - p.ry1) * dbuToBoard));
        EdaPadPrim pad{};
        pad.x = px; pad.y = py; pad.w = pw; pad.h = ph;
        pad.shape = 1;
        pad.layer = p.layer.empty() ? EdaLayerId::InnerCopper : MapAsicMetalLayer(p.layer);
        pad.pinName = PathUtils::Utf8ToWide(p.name);
        pad.netName = PathUtils::Utf8ToWide(p.net);
        out.pads.push_back(std::move(pad));
        if (out.labels.size() < 8000) {
            out.labels.push_back({ px + pw * 0.6f, py, 0.75f, EdaLayerId::SchLabel, pad.pinName });
        }
    }

    // 4. Render routed SPECIALNETS & NETS wires + VIAS
    for (const auto& w : wires) {
        if (out.traces.size() >= 120000) break;
        float strokeW = (w.widthDbu > 0.0)
            ? std::clamp(static_cast<float>(w.widthDbu * dbuToBoard), 0.14f, 2.4f)
            : (w.isSpecial ? 0.45f : 0.20f);
        out.traces.push_back({
            static_cast<float>(w.x1 * dbuToBoard),
            static_cast<float>(w.y1 * dbuToBoard),
            static_cast<float>(w.x2 * dbuToBoard),
            static_cast<float>(w.y2 * dbuToBoard),
            strokeW, w.layer, w.netId
        });
    }
    for (const auto& v : viaInsts) {
        if (out.vias.size() >= 60000) break;
        float d = (v.sizeDbu > 0.0) ? std::clamp(static_cast<float>(v.sizeDbu * dbuToBoard), 0.25f, 1.4f) : 0.36f;
        out.vias.push_back({
            static_cast<float>(v.x * dbuToBoard),
            static_cast<float>(v.y * dbuToBoard),
            d * 1.5f, d, v.netId
        });
    }

    if (out.components.empty() && out.traces.empty() && out.pads.empty()) {
        out = ParsedEdaBoard{};
        return false;
    }
    out.netCount = std::max<size_t>(1, totalNets > 0 ? totalNets : out.components.size());
    return true;
}

} // namespace Formats
