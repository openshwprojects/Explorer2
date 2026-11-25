#pragma once
// GedaPcb.h — gEDA / PCB-rnd Layout (.pcb) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

namespace DetailBoardFabEda {

// ============================================================================
// gEDA / PCB-rnd Layout Parser (.pcb)
// ============================================================================
struct GedaTok {
    enum Kind { Word, Quoted, LBrack, RBrack, LParen, RParen } kind = Word;
    std::string text;
};

inline bool TokenizeGedaPcb(std::string_view sv, std::vector<GedaTok>& outToks) {
    for (unsigned char c : sv) {
        if (c == 0) return false; // Reject binary P-CAD / Allegro .pcb files immediately!
    }
    int brackDepth = 0, parenDepth = 0;
    size_t i = 0;
    while (i < sv.size() && outToks.size() < 400000) {
        unsigned char c = static_cast<unsigned char>(sv[i]);
        if (c <= 0x20u) { ++i; continue; }
        if (c == '#') {
            while (i < sv.size() && sv[i] != '\n') ++i;
            continue;
        }
        if (c == '[') { ++brackDepth; outToks.push_back({ GedaTok::LBrack, "[" }); ++i; continue; }
        if (c == ']') { if (--brackDepth < 0) return false; outToks.push_back({ GedaTok::RBrack, "]" }); ++i; continue; }
        if (c == '(') { ++parenDepth; outToks.push_back({ GedaTok::LParen, "(" }); ++i; continue; }
        if (c == ')') { if (--parenDepth < 0) return false; outToks.push_back({ GedaTok::RParen, ")" }); ++i; continue; }
        if (c == '"') {
            ++i;
            std::string s;
            while (i < sv.size() && sv[i] != '"') {
                if (sv[i] == '\\' && i + 1 < sv.size()) ++i;
                s.push_back(sv[i++]);
            }
            if (i >= sv.size()) return false; // Unterminated quote!
            ++i;
            outToks.push_back({ GedaTok::Quoted, std::move(s) });
            continue;
        }
        size_t s = i;
        while (i < sv.size()) {
            unsigned char ch = static_cast<unsigned char>(sv[i]);
            if (ch <= 0x20u || ch == '[' || ch == ']' || ch == '(' || ch == ')' || ch == '"' || ch == '#') break;
            ++i;
        }
        outToks.push_back({ GedaTok::Word, std::string(sv.substr(s, i - s)) });
    }
    return brackDepth == 0 && parenDepth == 0 && !outToks.empty();
}

inline float ParseGedaCoordMm(const std::string& tok, bool bracketHiRes) {
    if (tok.empty()) return 0.0f;
    char* ep = nullptr;
    double val = std::strtod(tok.c_str(), &ep);
    if (!ep || ep == tok.c_str()) return 0.0f;
    std::string unit = DetailSiliconAndBusEda::TrimAsciiView(std::string_view(ep));
    for (char& c : unit) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (unit == "mm")  return static_cast<float>(val);
    if (unit == "cm")  return static_cast<float>(val * 10.0);
    if (unit == "um")  return static_cast<float>(val * 0.001);
    if (unit == "nm")  return static_cast<float>(val * 1e-6);
    if (unit == "in")  return static_cast<float>(val * 25.4);
    if (unit == "mil") return static_cast<float>(val * 0.0254);
    // Unitless gEDA coordinates: [...] = 1/100 mil (0.000254 mm), (...) = 1 mil (0.0254 mm)
    return static_cast<float>(val * (bracketHiRes ? 0.000254 : 0.0254));
}

inline bool ParseGedaPcbBoard(std::string_view sv, ParsedEdaBoard& out) {
    std::vector<GedaTok> toks;
    if (!TokenizeGedaPcb(sv, toks)) return false;

    bool sawPcbHeader = false;
    std::string boardName;
    float boardWMm = 0.0f, boardHMm = 0.0f;
    std::set<std::string> netNames;

    auto collectArgs = [&](size_t& idx, bool& outHiRes, std::vector<GedaTok>& args) -> bool {
        args.clear();
        if (idx >= toks.size()) return false;
        if (toks[idx].kind != GedaTok::LBrack && toks[idx].kind != GedaTok::LParen) return false;
        outHiRes = (toks[idx].kind == GedaTok::LBrack);
        GedaTok::Kind closeK = outHiRes ? GedaTok::RBrack : GedaTok::RParen;
        int depth = 1;
        ++idx;
        while (idx < toks.size() && depth > 0) {
            if (toks[idx].kind == (outHiRes ? GedaTok::LBrack : GedaTok::LParen)) ++depth;
            else if (toks[idx].kind == closeK) {
                if (--depth == 0) { ++idx; return true; }
            }
            if (depth == 1) args.push_back(toks[idx]);
            ++idx;
        }
        return false;
    };

    // Note: gEDA PCB Y axis points DOWNWARD (0 at top-left, +H at bottom-right), so negate Y to match Cartesian EDA view!
    size_t i = 0;
    while (i < toks.size() && out.traces.size() < 60000) {
        if (toks[i].kind != GedaTok::Word) { ++i; continue; }
        const std::string& kw = toks[i].text;
        ++i;

        if (kw == "PCB") {
            bool hi = true;
            std::vector<GedaTok> args;
            if (collectArgs(i, hi, args) && args.size() >= 3) {
                sawPcbHeader = true;
                boardName = args[0].text;
                boardWMm = ParseGedaCoordMm(args[1].text, hi);
                boardHMm = ParseGedaCoordMm(args[2].text, hi);
                if (boardWMm > 0.5f && boardHMm > 0.5f) {
                    out.traces.push_back({ 0.0f,     0.0f,      boardWMm, 0.0f,      0.32f, EdaLayerId::EdgeCuts, 0 });
                    out.traces.push_back({ boardWMm, 0.0f,      boardWMm, -boardHMm, 0.32f, EdaLayerId::EdgeCuts, 0 });
                    out.traces.push_back({ boardWMm, -boardHMm, 0.0f,     -boardHMm, 0.32f, EdaLayerId::EdgeCuts, 0 });
                    out.traces.push_back({ 0.0f,     -boardHMm, 0.0f,     0.0f,      0.32f, EdaLayerId::EdgeCuts, 0 });
                }
            }
        } else if (kw == "Via") {
            bool hi = true;
            std::vector<GedaTok> args;
            if (collectArgs(i, hi, args) && args.size() >= 6) {
                float vx = ParseGedaCoordMm(args[0].text, hi);
                float vy = -ParseGedaCoordMm(args[1].text, hi);
                float thick = std::max(0.4f, ParseGedaCoordMm(args[2].text, hi));
                float drill = std::max(0.2f, ParseGedaCoordMm(args[5].text, hi));
                out.vias.push_back({ vx, vy, thick, drill, 0 });
            }
        } else if (kw == "Element") {
            bool hi = true;
            std::vector<GedaTok> eArgs;
            if (!collectArgs(i, hi, eArgs) || eArgs.size() < 6) continue;
            std::string flags  = eArgs[0].text;
            std::string desc   = eArgs[1].text;
            std::string refdes = eArgs[2].text;
            std::string val    = eArgs[3].text;
            float mx = ParseGedaCoordMm(eArgs[4].text, hi);
            float my = -ParseGedaCoordMm(eArgs[5].text, hi);
            bool onBottom = DetailSiliconAndBusEda::ContainsAsciiI(flags, "onsolder");

            float minPx = mx - 1.2f, maxPx = mx + 1.2f, minPy = my - 0.8f, maxPy = my + 0.8f;
            int pinCount = 0;

            if (i < toks.size() && toks[i].kind == GedaTok::LParen) {
                ++i; // enter Element ( ... )
                int pDepth = 1;
                while (i < toks.size() && pDepth > 0) {
                    if (toks[i].kind == GedaTok::LParen) { ++pDepth; ++i; continue; }
                    if (toks[i].kind == GedaTok::RParen) { --pDepth; ++i; continue; }
                    if (toks[i].kind != GedaTok::Word) { ++i; continue; }
                    std::string subKw = toks[i++].text;
                    bool subHi = true;
                    std::vector<GedaTok> pArgs;
                    if (!collectArgs(i, subHi, pArgs)) continue;

                    if (subKw == "Pin" && pArgs.size() >= 8) {
                        float px = mx + ParseGedaCoordMm(pArgs[0].text, subHi);
                        float py = my - ParseGedaCoordMm(pArgs[1].text, subHi);
                        float thick = std::max(0.5f, ParseGedaCoordMm(pArgs[2].text, subHi));
                        float drill = std::max(0.2f, ParseGedaCoordMm(pArgs[5].text, subHi));
                        std::string pFlags = (pArgs.size() >= 9) ? pArgs[8].text : "";
                        uint8_t sh = DetailSiliconAndBusEda::ContainsAsciiI(pFlags, "square") ? 1 : 0;
                        out.pads.push_back({ px, py, thick, thick, 0.0f, drill, sh, EdaLayerId::PadsThrough,
                                             PathUtils::Utf8ToWide(pArgs[7].text), PathUtils::Utf8ToWide(pArgs[6].text) });
                        minPx = std::min(minPx, px - thick * 0.5f); maxPx = std::max(maxPx, px + thick * 0.5f);
                        minPy = std::min(minPy, py - thick * 0.5f); maxPy = std::max(maxPy, py + thick * 0.5f);
                        ++pinCount;
                    } else if (subKw == "Pad" && pArgs.size() >= 9) {
                        float x1 = mx + ParseGedaCoordMm(pArgs[0].text, subHi);
                        float y1 = my - ParseGedaCoordMm(pArgs[1].text, subHi);
                        float x2 = mx + ParseGedaCoordMm(pArgs[2].text, subHi);
                        float y2 = my - ParseGedaCoordMm(pArgs[3].text, subHi);
                        float thick = std::max(0.25f, ParseGedaCoordMm(pArgs[4].text, subHi));
                        std::string pFlags = (pArgs.size() >= 10) ? pArgs[9].text : "";
                        bool padBot = onBottom ^ DetailSiliconAndBusEda::ContainsAsciiI(pFlags, "onsolder");
                        float cx = (x1 + x2) * 0.5f, cy = (y1 + y2) * 0.5f;
                        float dx = x2 - x1, dy = y2 - y1;
                        float segLen = std::hypot(dx, dy);
                        float pw = segLen + thick, ph = thick;
                        float rot = (segLen > 1e-3f) ? static_cast<float>(std::atan2(dy, dx) * (180.0 / 3.14159265358979323846)) : 0.0f;
                        uint8_t sh = DetailSiliconAndBusEda::ContainsAsciiI(pFlags, "square") ? 1 : 3;
                        EdaLayerId lid = padBot ? EdaLayerId::BottomCopper : EdaLayerId::TopCopper;
                        out.pads.push_back({ cx, cy, pw, ph, rot, 0.0f, sh, lid,
                                             PathUtils::Utf8ToWide(pArgs[8].text), PathUtils::Utf8ToWide(pArgs[7].text) });
                        minPx = std::min({ minPx, x1 - thick * 0.5f, x2 - thick * 0.5f });
                        maxPx = std::max({ maxPx, x1 + thick * 0.5f, x2 + thick * 0.5f });
                        minPy = std::min({ minPy, y1 - thick * 0.5f, y2 - thick * 0.5f });
                        maxPy = std::max({ maxPy, y1 + thick * 0.5f, y2 + thick * 0.5f });
                        ++pinCount;
                    } else if (subKw == "ElementLine" && pArgs.size() >= 5) {
                        float x1 = mx + ParseGedaCoordMm(pArgs[0].text, subHi);
                        float y1 = my - ParseGedaCoordMm(pArgs[1].text, subHi);
                        float x2 = mx + ParseGedaCoordMm(pArgs[2].text, subHi);
                        float y2 = my - ParseGedaCoordMm(pArgs[3].text, subHi);
                        float w = std::max(0.12f, ParseGedaCoordMm(pArgs[4].text, subHi));
                        out.traces.push_back({ x1, y1, x2, y2, w, onBottom ? EdaLayerId::BottomSilk : EdaLayerId::TopSilk, 0 });
                        minPx = std::min({ minPx, x1, x2 }); maxPx = std::max({ maxPx, x1, x2 });
                        minPy = std::min({ minPy, y1, y2 }); maxPy = std::max({ maxPy, y1, y2 });
                    } else if (subKw == "ElementArc" && pArgs.size() >= 7) {
                        float cx = mx + ParseGedaCoordMm(pArgs[0].text, subHi);
                        float cy = my - ParseGedaCoordMm(pArgs[1].text, subHi);
                        float r  = std::max(0.2f, ParseGedaCoordMm(pArgs[2].text, subHi));
                        float w  = std::max(0.12f, ParseGedaCoordMm(pArgs[6].text, subHi));
                        out.circles.push_back({ cx, cy, r, w, false, onBottom ? EdaLayerId::BottomSilk : EdaLayerId::TopSilk });
                    }
                }
            }

            if (!refdes.empty() || pinCount > 0) {
                EdaComponent3D comp;
                comp.refDes = PathUtils::Utf8ToWide(refdes.empty() ? "E1" : refdes);
                comp.value = PathUtils::Utf8ToWide(val);
                comp.footprint = PathUtils::Utf8ToWide(desc);
                comp.x = (minPx + maxPx) * 0.5f;
                comp.y = (minPy + maxPy) * 0.5f;
                comp.widthMm  = std::clamp(maxPx - minPx, 1.0f, 60.0f);
                comp.lengthMm = std::clamp(maxPy - minPy, 1.0f, 60.0f);
                comp.bottomSide = onBottom;
                comp.pinCount = std::max(2, pinCount);
                comp.category = ClassifyComponent(
                    comp.refDes, comp.footprint, comp.widthMm, comp.lengthMm, comp.heightMm, comp.pinCount);
                out.labels.push_back({ comp.x - comp.widthMm * 0.4f, comp.y + comp.lengthMm * 0.5f + 0.5f, 0.85f,
                                       onBottom ? EdaLayerId::BottomSilk : EdaLayerId::TopSilk, comp.refDes });
                out.components.push_back(std::move(comp));
            }
        } else if (kw == "Layer") {
            bool hi = false;
            std::vector<GedaTok> lArgs;
            if (!collectArgs(i, hi, lArgs) || lArgs.size() < 2) continue;
            std::string lName = lArgs[1].text;
            std::string lType = (lArgs.size() >= 3) ? lArgs[2].text : "";
            EdaLayerId lid = DetailSiliconAndBusEda::ClassifyIpcLayerName(lName, lType);
            if (DetailSiliconAndBusEda::ContainsAsciiI(lName, "solder") && !DetailSiliconAndBusEda::ContainsAsciiI(lName, "mask")) {
                lid = EdaLayerId::BottomCopper;
            } else if (DetailSiliconAndBusEda::ContainsAsciiI(lName, "component") || DetailSiliconAndBusEda::ContainsAsciiI(lName, "top")) {
                if (!DetailSiliconAndBusEda::ContainsAsciiI(lName, "silk")) lid = EdaLayerId::TopCopper;
            }

            if (i < toks.size() && toks[i].kind == GedaTok::LParen) {
                ++i;
                int pDepth = 1;
                while (i < toks.size() && pDepth > 0) {
                    if (toks[i].kind == GedaTok::LParen) { ++pDepth; ++i; continue; }
                    if (toks[i].kind == GedaTok::RParen) { --pDepth; ++i; continue; }
                    if (toks[i].kind != GedaTok::Word) { ++i; continue; }
                    std::string subKw = toks[i++].text;
                    bool subHi = true;
                    std::vector<GedaTok> sArgs;
                    if (!collectArgs(i, subHi, sArgs)) continue;

                    if (subKw == "Line" && sArgs.size() >= 5) {
                        float x1 = ParseGedaCoordMm(sArgs[0].text, subHi);
                        float y1 = -ParseGedaCoordMm(sArgs[1].text, subHi);
                        float x2 = ParseGedaCoordMm(sArgs[2].text, subHi);
                        float y2 = -ParseGedaCoordMm(sArgs[3].text, subHi);
                        float w  = std::max(0.10f, ParseGedaCoordMm(sArgs[4].text, subHi));
                        out.traces.push_back({ x1, y1, x2, y2, w, lid, 1 });
                    } else if (subKw == "Arc" && sArgs.size() >= 7) {
                        float cx = ParseGedaCoordMm(sArgs[0].text, subHi);
                        float cy = -ParseGedaCoordMm(sArgs[1].text, subHi);
                        float rx = std::max(0.2f, ParseGedaCoordMm(sArgs[2].text, subHi));
                        float startDeg = std::strtof(sArgs[4].text.c_str(), nullptr);
                        float deltaDeg = std::strtof(sArgs[5].text.c_str(), nullptr);
                        float w = std::max(0.10f, ParseGedaCoordMm(sArgs[6].text, subHi));
                        int segs = std::clamp(static_cast<int>(std::fabs(deltaDeg) / 12.0f), 4, 32);
                        for (int s = 0; s < segs; ++s) {
                            // gEDA arc angles: 0 deg = -X (west), 90 deg = +Y (south = -Y in Cartesian)
                            float a0 = (180.0f + startDeg + deltaDeg * (static_cast<float>(s) / segs)) * (3.14159265358979323846f / 180.0f);
                            float a1 = (180.0f + startDeg + deltaDeg * (static_cast<float>(s + 1) / segs)) * (3.14159265358979323846f / 180.0f);
                            out.traces.push_back({ cx + rx * std::cos(a0), cy + rx * std::sin(a0),
                                                   cx + rx * std::cos(a1), cy + rx * std::sin(a1), w, lid, 1 });
                        }
                    } else if (subKw == "Polygon") {
                        std::vector<std::pair<float, float>> polyPts;
                        if (i < toks.size() && toks[i].kind == GedaTok::LParen) {
                            ++i;
                            int polyDepth = 1;
                            while (i < toks.size() && polyDepth > 0) {
                                if (toks[i].kind == GedaTok::LParen) {
                                    // Hole (...) inside Polygon — skip hole vertices cleanly
                                    int hDepth = 1; ++i;
                                    while (i < toks.size() && hDepth > 0) {
                                        if (toks[i].kind == GedaTok::LParen) ++hDepth;
                                        else if (toks[i].kind == GedaTok::RParen) --hDepth;
                                        ++i;
                                    }
                                    continue;
                                }
                                if (toks[i].kind == GedaTok::RParen) { --polyDepth; ++i; break; }
                                bool ptHi = true;
                                std::vector<GedaTok> ptArgs;
                                if (collectArgs(i, ptHi, ptArgs) && ptArgs.size() >= 2) {
                                    polyPts.push_back({ ParseGedaCoordMm(ptArgs[0].text, ptHi), -ParseGedaCoordMm(ptArgs[1].text, ptHi) });
                                } else {
                                    ++i;
                                }
                            }
                        }
                        if (polyPts.size() >= 3) {
                            for (size_t k = 0; k < polyPts.size(); ++k) {
                                const auto& a = polyPts[k];
                                const auto& b = polyPts[(k + 1) % polyPts.size()];
                                out.traces.push_back({ a.first, a.second, b.first, b.second, 0.20f, lid, 0 });
                            }
                            if (lid == EdaLayerId::TopCopper || lid == EdaLayerId::BottomCopper || lid == EdaLayerId::InnerCopper) {
                                HatchPolygonHorizontal(polyPts, lid, 0, out);
                            }
                        }
                    }
                }
            }
        } else if (kw == "NetList") {
            bool hi = false;
            std::vector<GedaTok> nlArgs;
            collectArgs(i, hi, nlArgs);
            if (i < toks.size() && toks[i].kind == GedaTok::LParen) {
                ++i;
                int nd = 1;
                while (i < toks.size() && nd > 0) {
                    if (toks[i].kind == GedaTok::LParen) { ++nd; ++i; continue; }
                    if (toks[i].kind == GedaTok::RParen) { --nd; ++i; continue; }
                    if (toks[i].kind == GedaTok::Word && toks[i].text == "Net") {
                        ++i;
                        bool nhi = false;
                        std::vector<GedaTok> nArgs;
                        if (collectArgs(i, nhi, nArgs) && !nArgs.empty() && !nArgs[0].text.empty()) {
                            netNames.insert(nArgs[0].text);
                        }
                    } else {
                        ++i;
                    }
                }
            }
        }
    }

    if (!sawPcbHeader || (out.traces.empty() && out.pads.empty() && out.vias.empty() && out.components.empty())) {
        return false;
    }
    out.kind = EdaDocKind::KiCadPcb;
    out.formatTitle = L"gEDA / PCB-rnd Layout (.pcb";
    if (!boardName.empty()) out.formatTitle += L" — " + PathUtils::Utf8ToWide(boardName);
    out.formatTitle += L")";
    out.netCount = std::max<size_t>(1, !netNames.empty() ? netNames.size() : out.components.size());
    return true;
}

} // namespace DetailBoardFabEda

inline bool ParseGedaPcbBoard(std::string_view sv, ParsedEdaBoard& out) {
    if (sv.size() < 12) return false;
    return DetailBoardFabEda::ParseGedaPcbBoard(sv, out);
}

} // namespace Formats
