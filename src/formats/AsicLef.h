#pragma once
// AsicLef.h — #25 ASIC Library Exchange Format (.lef) v5.8 Macro, Pin Port (RECT/POLYGON/PATH), OBS & Tech Layer Parser

namespace Formats {

struct LefPinPortRect {
    float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    EdaLayerId layer = EdaLayerId::TopCopper;
    std::string pinName;
};

struct LefMacroSummary {
    float widthUm = 2.4f;
    float heightUm = 2.72f;
    std::vector<LefPinPortRect> pinRects;
};

inline EdaLayerId MapAsicMetalLayer(std::string_view lyrRaw) {
    std::string s;
    s.reserve(lyrRaw.size());
    for (char c : lyrRaw) {
        if (c >= 'A' && c <= 'Z') s.push_back(static_cast<char>(c - 'A' + 'a'));
        else s.push_back(c);
    }
    if (s.find("poly") != std::string::npos || s == "li1" || s == "li") return EdaLayerId::SchWire;
    // Extract trailing or embedded layer index
    int num = 0;
    for (char c : s) {
        if (c >= '0' && c <= '9') { num = num * 10 + (c - '0'); }
    }
    if (s.find("via") != std::string::npos || s.find("cut") != std::string::npos || s.find("mcon") != std::string::npos) {
        return EdaLayerId::Drills;
    }
    if (num == 1) return EdaLayerId::TopCopper;
    if (num == 2) return EdaLayerId::BottomCopper;
    if (num >= 3) return EdaLayerId::InnerCopper;
    return EdaLayerId::TopCopper;
}

inline void ScanLefMacrosForDef(std::string_view sv, std::unordered_map<std::string, LefMacroSummary>& outMacros) {
    // Lightweight fast tokenizer to extract MACRO sizes & PIN RECTs for companion .def placement
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
        if (sv[pos] == ';') { return sv.substr(pos++, 1); }
        size_t s = pos;
        while (pos < sv.size() && static_cast<unsigned char>(sv[pos]) > 0x20u && sv[pos] != ';') ++pos;
        return sv.substr(s, pos - s);
    };

    std::string curMacro;
    std::string curPin;
    bool inObs = false;
    EdaLayerId curLayer = EdaLayerId::TopCopper;

    for (;;) {
        std::string_view t = nextTok();
        if (t.empty()) break;
        if (t == "MACRO") {
            std::string_view nm = nextTok();
            if (!nm.empty() && nm != ";") {
                curMacro = std::string(nm);
                outMacros[curMacro] = LefMacroSummary{};
                curPin.clear();
                inObs = false;
            }
        } else if (!curMacro.empty() && t == "END") {
            std::string_view what = nextTok();
            if (what == curMacro) { curMacro.clear(); curPin.clear(); inObs = false; }
            else if (!curPin.empty() && what == curPin) { curPin.clear(); }
            else if (inObs && what.empty()) { inObs = false; }
            else if (inObs && what == ";") { inObs = false; }
        } else if (!curMacro.empty() && t == "SIZE") {
            std::string_view tw = nextTok();
            std::string_view by = nextTok();
            std::string_view th = nextTok();
            if (!tw.empty() && (_strnicmp(by.data(), "BY", 2) == 0) && !th.empty()) {
                float w = std::strtof(std::string(tw).c_str(), nullptr);
                float h = std::strtof(std::string(th).c_str(), nullptr);
                if (w > 0.01f && h > 0.01f) {
                    outMacros[curMacro].widthUm = w;
                    outMacros[curMacro].heightUm = h;
                }
            }
        } else if (!curMacro.empty() && t == "PIN") {
            std::string_view pn = nextTok();
            if (!pn.empty() && pn != ";") { curPin = std::string(pn); inObs = false; }
        } else if (!curMacro.empty() && t == "OBS") {
            inObs = true; curPin.clear();
        } else if (!curMacro.empty() && t == "LAYER") {
            std::string_view ln = nextTok();
            if (!ln.empty()) curLayer = MapAsicMetalLayer(ln);
        } else if (!curMacro.empty() && !curPin.empty() && !inObs && t == "RECT") {
            std::string_view a = nextTok();
            if (a == "MASK") { nextTok(); a = nextTok(); }
            std::string_view b = nextTok(), c = nextTok(), d = nextTok();
            if (!a.empty() && !b.empty() && !c.empty() && !d.empty()) {
                LefPinPortRect r;
                r.x1 = std::strtof(std::string(a).c_str(), nullptr);
                r.y1 = std::strtof(std::string(b).c_str(), nullptr);
                r.x2 = std::strtof(std::string(c).c_str(), nullptr);
                r.y2 = std::strtof(std::string(d).c_str(), nullptr);
                r.layer = curLayer;
                r.pinName = curPin;
                if (outMacros[curMacro].pinRects.size() < 64) {
                    outMacros[curMacro].pinRects.push_back(std::move(r));
                }
            }
        }
    }
}

inline bool ParseAsicLefBoard(std::string_view sv, ParsedEdaBoard& out) {
    if (sv.size() < 16 || sv.size() > 64 * 1024 * 1024) return false;
    for (unsigned char c : sv) {
        if (c == 0) return false;
    }
    if (sv.find("MACRO") == std::string_view::npos && sv.find("LAYER") == std::string_view::npos) {
        return false;
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
        if (sv[pos] == ';') return sv.substr(pos++, 1);
        if (sv[pos] == '"') {
            ++pos;
            size_t s = pos;
            while (pos < sv.size() && sv[pos] != '"') ++pos;
            std::string_view q = sv.substr(s, pos - s);
            if (pos < sv.size()) ++pos;
            return q;
        }
        size_t s = pos;
        while (pos < sv.size() && static_cast<unsigned char>(sv[pos]) > 0x20u && sv[pos] != ';') ++pos;
        return sv.substr(s, pos - s);
    };

    struct PolyShape {
        std::vector<std::pair<float, float>> pts;
        EdaLayerId layer = EdaLayerId::TopCopper;
        bool isObs = false;
        std::string pinName;
    };
    struct RectShape {
        float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
        EdaLayerId layer = EdaLayerId::TopCopper;
        bool isObs = false;
        std::string pinName;
    };
    struct ParsedMacro {
        std::string name;
        std::string macroClass = "CORE";
        float origX = 0.0f, origY = 0.0f;
        float w = 4.0f, h = 4.0f;
        bool hasSize = false;
        bool closed = false;
        std::set<std::string> pinNames;
        std::vector<RectShape> rects;
        std::vector<PolyShape> polys;
    };
    struct TechLayerCard {
        std::string name;
        std::string type;
        std::string dir;
        float width = 0.14f;
        float pitch = 0.28f;
    };

    std::string lefVer;
    std::vector<ParsedMacro> macros;
    std::vector<TechLayerCard> techLayers;

    ParsedMacro* curMacro = nullptr;
    TechLayerCard* curTechLayer = nullptr;
    std::string curPinName;
    bool inPort = false;
    bool inObs = false;
    EdaLayerId activeMetal = EdaLayerId::TopCopper;
    bool sawEndLibrary = false;

    auto parseFloatSv = [](std::string_view t, float& outF) -> bool {
        if (t.empty() || t == ";") return false;
        char b[64] = {};
        std::memcpy(b, t.data(), std::min<size_t>(t.size(), 63));
        char* endp = nullptr;
        float v = std::strtof(b, &endp);
        if (endp == b || !std::isfinite(v)) return false;
        outF = v;
        return true;
    };

    for (;;) {
        std::string_view t = nextTok();
        if (t.empty()) break;

        if (!curMacro && !curTechLayer && t == "VERSION") {
            std::string_view v = nextTok();
            if (!v.empty() && v != ";") lefVer = std::string(v);
        } else if (!curMacro && !curTechLayer && t == "LAYER") {
            std::string_view ln = nextTok();
            if (!ln.empty() && ln != ";") {
                techLayers.push_back({ std::string(ln), "", "", 0.14f, 0.28f });
                curTechLayer = &techLayers.back();
            }
        } else if (curTechLayer) {
            if (t == "END") {
                nextTok(); // layerName
                curTechLayer = nullptr;
            } else if (t == "TYPE") {
                curTechLayer->type = std::string(nextTok());
            } else if (t == "DIRECTION") {
                curTechLayer->dir = std::string(nextTok());
            } else if (t == "WIDTH") {
                parseFloatSv(nextTok(), curTechLayer->width);
            } else if (t == "PITCH") {
                parseFloatSv(nextTok(), curTechLayer->pitch);
            }
        } else if (!curMacro && t == "MACRO") {
            std::string_view mn = nextTok();
            if (mn.empty() || mn == ";") return false;
            macros.emplace_back();
            curMacro = &macros.back();
            curMacro->name = std::string(mn);
            curPinName.clear();
            inPort = false;
            inObs = false;
        } else if (curMacro) {
            if (t == "END") {
                if (inPort) {
                    inPort = false;
                } else if (inObs) {
                    inObs = false;
                } else {
                    std::string_view endTarget = nextTok();
                    if (!curPinName.empty() && endTarget == curPinName) {
                        curPinName.clear();
                    } else if (endTarget == curMacro->name) {
                        curMacro->closed = true;
                        curMacro = nullptr;
                    }
                }
            } else if (t == "CLASS" && curPinName.empty()) {
                std::string_view cl = nextTok();
                if (!cl.empty() && cl != ";") curMacro->macroClass = std::string(cl);
            } else if (t == "ORIGIN" && curPinName.empty()) {
                parseFloatSv(nextTok(), curMacro->origX);
                parseFloatSv(nextTok(), curMacro->origY);
            } else if (t == "SIZE" && curPinName.empty()) {
                float w = 0.0f, h = 0.0f;
                std::string_view tw = nextTok();
                std::string_view by = nextTok();
                std::string_view th = nextTok();
                if (parseFloatSv(tw, w) && (_strnicmp(by.data(), "BY", 2) == 0) && parseFloatSv(th, h) && w > 0.0f && h > 0.0f) {
                    curMacro->w = w;
                    curMacro->h = h;
                    curMacro->hasSize = true;
                }
            } else if (t == "PIN") {
                std::string_view pn = nextTok();
                if (!pn.empty() && pn != ";") {
                    curPinName = std::string(pn);
                    curMacro->pinNames.insert(curPinName);
                    inObs = false;
                    inPort = false;
                }
            } else if (t == "PORT") {
                inPort = true;
            } else if (t == "OBS") {
                inObs = true;
                inPort = false;
                curPinName.clear();
            } else if ((inPort || inObs || !curPinName.empty()) && t == "LAYER") {
                std::string_view ln = nextTok();
                if (!ln.empty() && ln != ";") {
                    activeMetal = MapAsicMetalLayer(ln);
                }
            } else if (t == "RECT") {
                std::string_view a = nextTok();
                if (a == "MASK") { nextTok(); a = nextTok(); }
                std::string_view b = nextTok(), c = nextTok(), d = nextTok();
                float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
                if (parseFloatSv(a, x1) && parseFloatSv(b, y1) && parseFloatSv(c, x2) && parseFloatSv(d, y2)) {
                    if (curMacro->rects.size() < 8000) {
                        curMacro->rects.push_back({ x1, y1, x2, y2, inObs ? EdaLayerId::BottomSilk : activeMetal, inObs, curPinName });
                    }
                }
            } else if (t == "POLYGON") {
                PolyShape ps;
                ps.layer = inObs ? EdaLayerId::BottomSilk : activeMetal;
                ps.isObs = inObs;
                ps.pinName = curPinName;
                std::string_view ptTok = nextTok();
                if (ptTok == "MASK") { nextTok(); ptTok = nextTok(); }
                while (!ptTok.empty() && ptTok != ";") {
                    std::string_view pyTok = nextTok();
                    float px = 0, py = 0;
                    if (parseFloatSv(ptTok, px) && parseFloatSv(pyTok, py)) {
                        ps.pts.push_back({ px, py });
                    } else {
                        break;
                    }
                    ptTok = nextTok();
                }
                if (ps.pts.size() >= 3 && curMacro->polys.size() < 4000) {
                    curMacro->polys.push_back(std::move(ps));
                }
            } else if (t == "PATH") {
                float pw = 0.14f;
                std::string_view a = nextTok();
                if (a == "MASK") { nextTok(); a = nextTok(); }
                if (a == "WIDTH") { parseFloatSv(nextTok(), pw); a = nextTok(); }
                PolyShape ps;
                ps.layer = inObs ? EdaLayerId::BottomSilk : activeMetal;
                ps.isObs = inObs;
                ps.pinName = curPinName;
                while (!a.empty() && a != ";") {
                    std::string_view b = nextTok();
                    float px = 0, py = 0;
                    if (parseFloatSv(a, px) && parseFloatSv(b, py)) ps.pts.push_back({ px, py });
                    else break;
                    a = nextTok();
                }
                for (size_t k = 0; k + 1 < ps.pts.size() && curMacro->rects.size() < 8000; ++k) {
                    float x1 = std::min(ps.pts[k].first, ps.pts[k + 1].first) - pw * 0.5f;
                    float x2 = std::max(ps.pts[k].first, ps.pts[k + 1].first) + pw * 0.5f;
                    float y1 = std::min(ps.pts[k].second, ps.pts[k + 1].second) - pw * 0.5f;
                    float y2 = std::max(ps.pts[k].second, ps.pts[k + 1].second) + pw * 0.5f;
                    curMacro->rects.push_back({ x1, y1, x2, y2, ps.layer, inObs, curPinName });
                }
            }
        } else if (t == "END") {
            std::string_view what = nextTok();
            if (what == "LIBRARY") {
                sawEndLibrary = true;
                break;
            }
        }
    }

    // Reject unclosed / truncated MACRO definitions
    if (curMacro != nullptr) {
        out = ParsedEdaBoard{};
        return false;
    }
    if (macros.empty() && techLayers.empty()) {
        out = ParsedEdaBoard{};
        return false;
    }
    (void)sawEndLibrary;

    out.kind = EdaDocKind::KiCadPcb;
    out.formatTitle = L"ASIC Library Exchange Format (.lef";
    if (!lefVer.empty()) out.formatTitle += L" v" + PathUtils::Utf8ToWide(lefVer);
    out.formatTitle += L")";

    // Normalize display scale so sub-micron standard cells (e.g. 1.8 um) and large blocks both render crisply
    float maxCellDim = 0.0f;
    for (const auto& m : macros) maxCellDim = std::max({ maxCellDim, m.w, m.h });
    float umScale = 1.0f;
    if (maxCellDim > 0.0f && maxCellDim < 5.0f) umScale = 4.0f;
    else if (maxCellDim > 500.0f) umScale = 100.0f / maxCellDim;

    size_t totalPins = 0;
    const size_t cols = std::max<size_t>(1, static_cast<size_t>(std::ceil(std::sqrt(static_cast<double>(macros.size())))));
    float curX = 0.0f, curY = 0.0f, rowMaxH = 0.0f;

    for (size_t mi = 0; mi < macros.size() && mi < 256; ++mi) {
        const auto& m = macros[mi];
        if (!m.closed) continue;
        float cw = std::max(1.5f, m.w * umScale);
        float ch = std::max(1.5f, m.h * umScale);

        if (mi > 0 && (mi % cols) == 0) {
            curX = 0.0f;
            curY -= (rowMaxH + std::max(3.5f, ch * 0.35f));
            rowMaxH = 0.0f;
        }
        rowMaxH = std::max(rowMaxH, ch);

        // Macro PR boundary on EdgeCuts
        out.traces.push_back({ curX,      curY,      curX + cw, curY,      0.18f, EdaLayerId::EdgeCuts, 0 });
        out.traces.push_back({ curX + cw, curY,      curX + cw, curY + ch, 0.18f, EdaLayerId::EdgeCuts, 0 });
        out.traces.push_back({ curX + cw, curY + ch, curX,      curY + ch, 0.18f, EdaLayerId::EdgeCuts, 0 });
        out.traces.push_back({ curX,      curY + ch, curX,      curY,      0.18f, EdaLayerId::EdgeCuts, 0 });

        EdaComponent3D comp;
        comp.refDes = PathUtils::Utf8ToWide(m.name);
        comp.value = PathUtils::Utf8ToWide(m.macroClass);
        comp.footprint = L"LEF_MACRO";
        comp.x = curX + cw * 0.5f;
        comp.y = curY + ch * 0.5f;
        comp.widthMm = cw;
        comp.lengthMm = ch;
        comp.heightMm = 0.8f;
        comp.pinCount = std::max(2, static_cast<int>(m.pinNames.size()));
        comp.category = EdaPartCategory::IC_Package;
        out.components.push_back(comp);
        out.labels.push_back({ curX, curY + ch + 0.8f, 0.95f, EdaLayerId::TopSilk, comp.refDes });

        totalPins += m.pinNames.size();
        std::set<std::string> labeledPins;

        for (const auto& r : m.rects) {
            float x1 = curX + (r.x1 + m.origX) * umScale;
            float y1 = curY + (r.y1 + m.origY) * umScale;
            float x2 = curX + (r.x2 + m.origX) * umScale;
            float y2 = curY + (r.y2 + m.origY) * umScale;
            if (r.isObs) {
                // Obstruction blockage outline + diagonal hatch
                out.traces.push_back({ x1, y1, x2, y1, 0.10f, EdaLayerId::BottomSilk, 0 });
                out.traces.push_back({ x2, y1, x2, y2, 0.10f, EdaLayerId::BottomSilk, 0 });
                out.traces.push_back({ x2, y2, x1, y2, 0.10f, EdaLayerId::BottomSilk, 0 });
                out.traces.push_back({ x1, y2, x1, y1, 0.10f, EdaLayerId::BottomSilk, 0 });
                out.traces.push_back({ x1, y1, x2, y2, 0.08f, EdaLayerId::BottomSilk, 0 });
            } else {
                EdaPadPrim pad{};
                pad.x = (x1 + x2) * 0.5f;
                pad.y = (y1 + y2) * 0.5f;
                pad.w = std::max(0.18f, std::fabs(x2 - x1));
                pad.h = std::max(0.18f, std::fabs(y2 - y1));
                pad.shape = 1;
                pad.layer = r.layer;
                pad.pinName = PathUtils::Utf8ToWide(r.pinName);
                out.pads.push_back(std::move(pad));
                if (!r.pinName.empty() && !labeledPins.count(r.pinName) && out.labels.size() < 8000) {
                    labeledPins.insert(r.pinName);
                    out.labels.push_back({ (x1 + x2) * 0.5f, (y1 + y2) * 0.5f, 0.75f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(r.pinName) });
                }
            }
        }

        for (const auto& p : m.polys) {
            if (p.pts.size() < 3) continue;
            float cxSum = 0.0f, cySum = 0.0f;
            float pMinX = 1e9f, pMaxX = -1e9f, pMinY = 1e9f, pMaxY = -1e9f;
            for (size_t k = 0; k < p.pts.size(); ++k) {
                size_t kn = (k + 1) % p.pts.size();
                float x1 = curX + (p.pts[k].first + m.origX) * umScale;
                float y1 = curY + (p.pts[k].second + m.origY) * umScale;
                float x2 = curX + (p.pts[kn].first + m.origX) * umScale;
                float y2 = curY + (p.pts[kn].second + m.origY) * umScale;
                cxSum += x1; cySum += y1;
                pMinX = std::min(pMinX, x1); pMaxX = std::max(pMaxX, x1);
                pMinY = std::min(pMinY, y1); pMaxY = std::max(pMaxY, y1);
                out.traces.push_back({ x1, y1, x2, y2, p.isObs ? 0.10f : 0.18f, p.layer, 0 });
            }
            if (!p.isObs) {
                EdaPadPrim pad{};
                pad.x = cxSum / static_cast<float>(p.pts.size());
                pad.y = cySum / static_cast<float>(p.pts.size());
                pad.w = std::max(0.20f, (pMaxX - pMinX) * 0.55f);
                pad.h = std::max(0.20f, (pMaxY - pMinY) * 0.55f);
                pad.shape = 1;
                pad.layer = p.layer;
                pad.pinName = PathUtils::Utf8ToWide(p.pinName);
                out.pads.push_back(std::move(pad));
            }
        }

        curX += cw + std::max(3.0f, cw * 0.35f);
    }

    // Tech LEF fallback (tech-only .lef with LAYER rules and no MACROs)
    if (macros.empty() && !techLayers.empty()) {
        float ly = 0.0f;
        for (size_t i = 0; i < techLayers.size() && i < 32; ++i) {
            const auto& tl = techLayers[i];
            EdaLayerId lid = MapAsicMetalLayer(tl.name);
            out.traces.push_back({ 0.0f, ly, 40.0f, ly, std::clamp(tl.width * 3.0f, 0.2f, 1.5f), lid, static_cast<int>(i + 1) });
            std::string desc = tl.name + " (" + tl.type + (tl.dir.empty() ? "" : (", " + tl.dir)) + ")";
            out.labels.push_back({ 42.0f, ly - 0.3f, 0.95f, EdaLayerId::TopSilk, PathUtils::Utf8ToWide(desc) });
            ly += 3.5f;
        }
    }

    if (out.components.empty() && out.pads.empty() && out.traces.empty()) {
        out = ParsedEdaBoard{};
        return false;
    }
    out.netCount = std::max<size_t>(1, totalPins > 0 ? totalPins : techLayers.size());
    return true;
}

} // namespace Formats
