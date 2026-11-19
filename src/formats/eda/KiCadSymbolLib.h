#pragma once
// KiCadSymbolLib.h — KiCad v6–v9 Schematic Symbol Library (.kicad_sym) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

namespace DetailKiCadSymSesPos {

inline void TessellateKiCad3PtArc(
    float x1, float y1, float xm, float ym, float x2, float y2,
    std::vector<std::pair<float, float>>& outPts)
{
    outPts.clear();
    double d = 2.0 * (static_cast<double>(x1) * (ym - y2) +
                      static_cast<double>(xm) * (y2 - y1) +
                      static_cast<double>(x2) * (y1 - ym));
    if (std::fabs(d) < 1e-7) {
        outPts.push_back({ x1, y1 });
        outPts.push_back({ x2, y2 });
        return;
    }
    double s1 = static_cast<double>(x1) * x1 + static_cast<double>(y1) * y1;
    double sm = static_cast<double>(xm) * xm + static_cast<double>(ym) * ym;
    double s2 = static_cast<double>(x2) * x2 + static_cast<double>(y2) * y2;
    double cx = (s1 * (ym - y2) + sm * (y2 - y1) + s2 * (y1 - ym)) / d;
    double cy = (s1 * (x2 - xm) + sm * (x1 - x2) + s2 * (xm - x1)) / d;
    double r  = std::hypot(x1 - cx, y1 - cy);
    if (r < 1e-4 || r > 1e5) {
        outPts.push_back({ x1, y1 });
        outPts.push_back({ x2, y2 });
        return;
    }

    const double twoPi = 6.2831853071795864769;
    double a1 = std::atan2(y1 - cy, x1 - cx);
    double am = std::atan2(ym - cy, xm - cx);
    double a2 = std::atan2(y2 - cy, x2 - cx);

    auto normCcw = [&](double a) {
        double da = std::fmod(a - a1, twoPi);
        if (da < 0.0) da += twoPi;
        return da;
    };
    double dm = normCcw(am);
    double d2 = normCcw(a2);
    double sweep = (dm <= d2) ? d2 : (d2 - twoPi);

    int segs = std::clamp(static_cast<int>(std::ceil(std::fabs(sweep) / (twoPi / 24.0))), 4, 32);
    for (int i = 0; i <= segs; ++i) {
        double ang = a1 + sweep * (static_cast<double>(i) / static_cast<double>(segs));
        outPts.push_back({ static_cast<float>(cx + r * std::cos(ang)),
                           static_cast<float>(cy + r * std::sin(ang)) });
    }
}

struct KiSymPin {
    float x = 0.0f, y = 0.0f, angleDeg = 0.0f, length = 2.54f;
    std::string shape; // line, inverted, clock, inverted_clock, input_low, output_low, non_logic
    std::string name;
    std::string number;
    int unit = 0, style = 0;
    bool hidden = false;
};

struct KiSymSeg {
    float x1 = 0, y1 = 0, x2 = 0, y2 = 0, w = 0.25f;
    int unit = 0, style = 0;
};

struct KiSymCirc {
    float cx = 0, cy = 0, r = 1.0f, w = 0.25f;
    int unit = 0, style = 0;
};

struct KiSymTxt {
    float x = 0, y = 0;
    std::string text;
    int unit = 0, style = 0;
};

struct KiSymEntry {
    std::string name;
    std::string extendsName;
    std::string refPrefix = "U";
    std::string valueStr;
    std::string footprintStr;
    bool hidePinNames = false;
    bool hidePinNums  = false;
    std::vector<KiSymSeg> segs;
    std::vector<KiSymCirc> circs;
    std::vector<KiSymPin> pins;
    std::vector<KiSymTxt> texts;
};

inline void ParseKiSymUnitSuffix(std::string_view subName, int& outUnit, int& outStyle) {
    outUnit = 0;
    outStyle = 0;
    size_t u2 = subName.rfind('_');
    if (u2 == std::string_view::npos || u2 == 0 || u2 + 1 >= subName.size()) return;
    size_t u1 = subName.rfind('_', u2 - 1);
    if (u1 == std::string_view::npos || u1 + 1 >= u2) return;
    bool okDigits = true;
    for (size_t i = u1 + 1; i < u2; ++i) if (!std::isdigit(static_cast<unsigned char>(subName[i]))) okDigits = false;
    for (size_t i = u2 + 1; i < subName.size(); ++i) if (!std::isdigit(static_cast<unsigned char>(subName[i]))) okDigits = false;
    if (!okDigits) return;
    outUnit  = std::atoi(std::string(subName.substr(u1 + 1, u2 - u1 - 1)).c_str());
    outStyle = std::atoi(std::string(subName.substr(u2 + 1)).c_str());
}

inline float ExtractStrokeWidthMm(const DetailSpecctra::SNode& primNode, float defW = 0.254f) {
    for (size_t i = 1; i < primNode.list.size(); ++i) {
        const auto& c = primNode.list[i];
        std::string_view k = DetailSpecctra::HeadKey(c);
        if (k == "stroke") {
            for (size_t j = 1; j < c.list.size(); ++j) {
                if (DetailSpecctra::HeadKey(c.list[j]) == "width" && c.list[j].list.size() >= 2) {
                    double v = 0.0;
                    if (DetailSpecctra::AtomFloat(c.list[j].list[1], v) && v > 0.0) {
                        return std::clamp(static_cast<float>(v), 0.14f, 1.5f);
                    }
                }
            }
        } else if (k == "width" && c.list.size() >= 2) {
            double v = 0.0;
            if (DetailSpecctra::AtomFloat(c.list[1], v) && v > 0.0) {
                return std::clamp(static_cast<float>(v), 0.14f, 1.5f);
            }
        }
    }
    return defW;
}

inline bool ExtractPairXY(const DetailSpecctra::SNode& parent, std::string_view key, float& outX, float& outY, float* outThird = nullptr) {
    for (size_t i = 1; i < parent.list.size(); ++i) {
        const auto& c = parent.list[i];
        if (DetailSpecctra::HeadKey(c) == key && c.list.size() >= 3) {
            double vx = 0.0, vy = 0.0;
            if (DetailSpecctra::AtomFloat(c.list[1], vx) && DetailSpecctra::AtomFloat(c.list[2], vy)) {
                outX = static_cast<float>(vx);
                outY = static_cast<float>(vy);
                if (outThird && c.list.size() >= 4) {
                    double vz = 0.0;
                    if (DetailSpecctra::AtomFloat(c.list[3], vz)) *outThird = static_cast<float>(vz);
                }
                return true;
            }
        }
    }
    return false;
}

inline void ParseKiSymPrimitives(const DetailSpecctra::SNode& container, int unit, int style, KiSymEntry& dst) {
    for (size_t i = 1; i < container.list.size(); ++i) {
        const auto& ch = container.list[i];
        std::string_view k = DetailSpecctra::HeadKey(ch);
        if (k == "symbol" && ch.list.size() >= 2 && !ch.list[1].isList) {
            int subU = unit, subS = style;
            ParseKiSymUnitSuffix(ch.list[1].atom, subU, subS);
            ParseKiSymPrimitives(ch, subU, subS, dst);
        } else if (k == "extends" && ch.list.size() >= 2 && !ch.list[1].isList) {
            dst.extendsName = ch.list[1].atom;
        } else if (k == "pin_names") {
            for (size_t j = 1; j < ch.list.size(); ++j) {
                if (!ch.list[j].isList && ch.list[j].atom == "hide") dst.hidePinNames = true;
                else if (DetailSpecctra::HeadKey(ch.list[j]) == "hide" &&
                         (ch.list[j].list.size() < 2 || ch.list[j].list[1].atom != "no")) dst.hidePinNames = true;
            }
        } else if (k == "pin_numbers") {
            for (size_t j = 1; j < ch.list.size(); ++j) {
                if (!ch.list[j].isList && ch.list[j].atom == "hide") dst.hidePinNums = true;
                else if (DetailSpecctra::HeadKey(ch.list[j]) == "hide" &&
                         (ch.list[j].list.size() < 2 || ch.list[j].list[1].atom != "no")) dst.hidePinNums = true;
            }
        } else if (k == "property" && ch.list.size() >= 3 && !ch.list[1].isList && !ch.list[2].isList) {
            const std::string& pk = ch.list[1].atom;
            const std::string& pv = ch.list[2].atom;
            if (pk == "Reference" && !pv.empty()) dst.refPrefix = pv;
            else if (pk == "Value" && !pv.empty()) dst.valueStr = pv;
            else if (pk == "Footprint" && !pv.empty()) dst.footprintStr = pv;
        } else if (k == "rectangle") {
            float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            if (ExtractPairXY(ch, "start", x1, y1) && ExtractPairXY(ch, "end", x2, y2)) {
                float w = ExtractStrokeWidthMm(ch, 0.254f);
                dst.segs.push_back({ x1, y1, x2, y1, w, unit, style });
                dst.segs.push_back({ x2, y1, x2, y2, w, unit, style });
                dst.segs.push_back({ x2, y2, x1, y2, w, unit, style });
                dst.segs.push_back({ x1, y2, x1, y1, w, unit, style });
            }
        } else if (k == "polyline" || k == "bezier") {
            float w = ExtractStrokeWidthMm(ch, 0.254f);
            std::vector<std::pair<float, float>> pts;
            for (size_t j = 1; j < ch.list.size(); ++j) {
                if (DetailSpecctra::HeadKey(ch.list[j]) == "pts") {
                    const auto& ptsNode = ch.list[j];
                    for (size_t m = 1; m < ptsNode.list.size(); ++m) {
                        const auto& xy = ptsNode.list[m];
                        if (DetailSpecctra::HeadKey(xy) == "xy" && xy.list.size() >= 3) {
                            double vx = 0.0, vy = 0.0;
                            if (DetailSpecctra::AtomFloat(xy.list[1], vx) && DetailSpecctra::AtomFloat(xy.list[2], vy)) {
                                pts.push_back({ static_cast<float>(vx), static_cast<float>(vy) });
                            }
                        }
                    }
                }
            }
            if (k == "bezier" && pts.size() == 4) {
                std::pair<float, float> prev = pts[0];
                for (int s = 1; s <= 12; ++s) {
                    float t = static_cast<float>(s) / 12.0f;
                    float u = 1.0f - t;
                    float bx = u*u*u*pts[0].first + 3*u*u*t*pts[1].first + 3*u*t*t*pts[2].first + t*t*t*pts[3].first;
                    float by = u*u*u*pts[0].second + 3*u*u*t*pts[1].second + 3*u*t*t*pts[2].second + t*t*t*pts[3].second;
                    dst.segs.push_back({ prev.first, prev.second, bx, by, w, unit, style });
                    prev = { bx, by };
                }
            } else {
                for (size_t s = 1; s < pts.size(); ++s) {
                    dst.segs.push_back({ pts[s - 1].first, pts[s - 1].second, pts[s].first, pts[s].second, w, unit, style });
                }
            }
        } else if (k == "circle") {
            float cx = 0, cy = 0;
            if (ExtractPairXY(ch, "center", cx, cy)) {
                float r = 1.0f;
                for (size_t j = 1; j < ch.list.size(); ++j) {
                    if (DetailSpecctra::HeadKey(ch.list[j]) == "radius" && ch.list[j].list.size() >= 2) {
                        double rv = 0.0;
                        if (DetailSpecctra::AtomFloat(ch.list[j].list[1], rv) && rv > 0.02) r = static_cast<float>(rv);
                    }
                }
                dst.circs.push_back({ cx, cy, r, ExtractStrokeWidthMm(ch, 0.254f), unit, style });
            }
        } else if (k == "arc") {
            float x1 = 0, y1 = 0, xm = 0, ym = 0, x2 = 0, y2 = 0;
            if (ExtractPairXY(ch, "start", x1, y1) && ExtractPairXY(ch, "mid", xm, ym) && ExtractPairXY(ch, "end", x2, y2)) {
                float w = ExtractStrokeWidthMm(ch, 0.254f);
                std::vector<std::pair<float, float>> arcPts;
                TessellateKiCad3PtArc(x1, y1, xm, ym, x2, y2, arcPts);
                for (size_t s = 1; s < arcPts.size(); ++s) {
                    dst.segs.push_back({ arcPts[s - 1].first, arcPts[s - 1].second, arcPts[s].first, arcPts[s].second, w, unit, style });
                }
            }
        } else if (k == "text" && ch.list.size() >= 2 && !ch.list[1].isList) {
            float tx = 0, ty = 0;
            if (!ch.list[1].atom.empty() && ExtractPairXY(ch, "at", tx, ty)) {
                dst.texts.push_back({ tx, ty, ch.list[1].atom, unit, style });
            }
        } else if (k == "pin" && ch.list.size() >= 4) {
            KiSymPin pin;
            pin.unit = unit;
            pin.style = style;
            if (ch.list.size() >= 3 && !ch.list[2].isList) pin.shape = ch.list[2].atom;
            ExtractPairXY(ch, "at", pin.x, pin.y, &pin.angleDeg);
            for (size_t j = 1; j < ch.list.size(); ++j) {
                const auto& sub = ch.list[j];
                if (!sub.isList && sub.atom == "hide") pin.hidden = true;
                std::string_view sk = DetailSpecctra::HeadKey(sub);
                if (sk == "hide" && (sub.list.size() < 2 || sub.list[1].atom != "no")) pin.hidden = true;
                else if (sk == "length" && sub.list.size() >= 2) {
                    double lv = 0.0;
                    if (DetailSpecctra::AtomFloat(sub.list[1], lv)) pin.length = std::max(0.0f, static_cast<float>(lv));
                } else if (sk == "name" && sub.list.size() >= 2 && !sub.list[1].isList) {
                    pin.name = sub.list[1].atom;
                } else if (sk == "number" && sub.list.size() >= 2 && !sub.list[1].isList) {
                    pin.number = sub.list[1].atom;
                }
            }
            dst.pins.push_back(std::move(pin));
        }
    }
}

} // namespace DetailKiCadSymSesPos

inline bool ParseKiCadSymbolLibBoard(std::string_view sv, const std::wstring& ext, ParsedEdaBoard& out) {
    if (sv.size() >= 3 &&
        static_cast<uint8_t>(sv[0]) == 0xEFu &&
        static_cast<uint8_t>(sv[1]) == 0xBBu &&
        static_cast<uint8_t>(sv[2]) == 0xBFu)
    {
        sv.remove_prefix(3);
    }

    // #61. KiCad v6–v9 Schematic Symbol Library (.kicad_sym)
    if (ext == L".kicad_sym" || sv.find("(kicad_symbol_lib") != std::string_view::npos) {
        using namespace DetailKiCadSymSesPos;
        if (sv.find("(kicad_symbol_lib") == std::string_view::npos && sv.find("(symbol ") == std::string_view::npos) {
            return false;
        }

        std::vector<DetailSpecctra::SNode> roots;
        if (!DetailSpecctra::ParseSExprTree(sv, roots) || roots.empty()) return false;

        std::vector<KiSymEntry> syms;
        for (const auto& r : roots) {
            std::string_view hk = DetailSpecctra::HeadKey(r);
            if (hk == "kicad_symbol_lib") {
                for (size_t i = 1; i < r.list.size() && syms.size() < 128; ++i) {
                    const auto& sn = r.list[i];
                    if (DetailSpecctra::HeadKey(sn) == "symbol" && sn.list.size() >= 2 && !sn.list[1].isList) {
                        KiSymEntry ent;
                        ent.name = sn.list[1].atom;
                        ParseKiSymPrimitives(sn, 0, 0, ent);
                        if (!ent.name.empty()) syms.push_back(std::move(ent));
                    }
                }
            } else if (hk == "symbol" && r.list.size() >= 2 && !r.list[1].isList && syms.size() < 128) {
                KiSymEntry ent;
                ent.name = r.list[1].atom;
                ParseKiSymPrimitives(r, 0, 0, ent);
                if (!ent.name.empty()) syms.push_back(std::move(ent));
            }
        }
        if (syms.empty()) return false;

        // Resolve (extends "ParentSymbol") inheritance across the symbol library
        std::unordered_map<std::string, size_t> symIdxMap;
        for (size_t i = 0; i < syms.size(); ++i) {
            symIdxMap[syms[i].name] = i;
            size_t col = syms[i].name.find(':');
            if (col != std::string::npos) symIdxMap[syms[i].name.substr(col + 1)] = i;
        }
        for (int pass = 0; pass < 3; ++pass) {
            for (auto& s : syms) {
                if (s.extendsName.empty()) continue;
                auto it = symIdxMap.find(s.extendsName);
                if (it == symIdxMap.end()) {
                    size_t col = s.extendsName.find(':');
                    if (col != std::string::npos) it = symIdxMap.find(s.extendsName.substr(col + 1));
                }
                if (it == symIdxMap.end() || it->second >= syms.size()) continue;
                const KiSymEntry& parent = syms[it->second];
                if (!parent.extendsName.empty() && pass < 2) continue;

                if (s.segs.empty() && s.circs.empty()) {
                    s.segs = parent.segs;
                    s.circs = parent.circs;
                }
                if (s.pins.empty()) s.pins = parent.pins;
                if (s.texts.empty()) s.texts = parent.texts;
                if (s.footprintStr.empty()) s.footprintStr = parent.footprintStr;
                s.hidePinNames = parent.hidePinNames;
                s.hidePinNums  = parent.hidePinNums;
                s.extendsName.clear();
            }
        }

        out.kind = EdaDocKind::KiCadSch;

        const size_t cols = std::max<size_t>(1u, static_cast<size_t>(std::ceil(std::sqrt(static_cast<double>(syms.size())))));
        float cellX = 0.0f, cellY = 0.0f, rowMaxH = 18.0f;
        size_t colIdx = 0;
        size_t renderedSyms = 0;
        size_t totalPins = 0;

        for (size_t si = 0; si < syms.size(); ++si) {
            const auto& sym = syms[si];
            int targetUnit = 1;
            bool hasU01 = false;
            int minPresentU = 999;
            for (const auto& g : sym.segs)  { if (g.unit <= 1) hasU01 = true; if (g.unit > 0) minPresentU = std::min(minPresentU, g.unit); }
            for (const auto& c : sym.circs) { if (c.unit <= 1) hasU01 = true; if (c.unit > 0) minPresentU = std::min(minPresentU, c.unit); }
            for (const auto& p : sym.pins)  { if (p.unit <= 1) hasU01 = true; if (p.unit > 0) minPresentU = std::min(minPresentU, p.unit); }
            if (!hasU01 && minPresentU < 999) targetUnit = minPresentU;

            auto matchUS = [&](int u, int st) -> bool {
                return (u == 0 || u == targetUnit) && (st == 0 || st == 1);
            };

            float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
            bool hasGeom = false;
            auto expB = [&](float x, float y) {
                minX = std::min(minX, x); maxX = std::max(maxX, x);
                minY = std::min(minY, y); maxY = std::max(maxY, y);
                hasGeom = true;
            };
            for (const auto& g : sym.segs)  if (matchUS(g.unit, g.style)) { expB(g.x1, g.y1); expB(g.x2, g.y2); }
            for (const auto& c : sym.circs) if (matchUS(c.unit, c.style)) { expB(c.cx - c.r, c.cy - c.r); expB(c.cx + c.r, c.cy + c.r); }
            for (const auto& p : sym.pins) {
                if (!matchUS(p.unit, p.style) || p.hidden) continue;
                expB(p.x, p.y);
                float rad = p.angleDeg * 3.14159265f / 180.0f;
                expB(p.x + p.length * std::cos(rad), p.y + p.length * std::sin(rad));
            }
            if (!hasGeom) continue;

            float symW = std::max(4.0f, maxX - minX);
            float symH = std::max(4.0f, maxY - minY);
            if (colIdx >= cols) {
                colIdx = 0;
                cellX = 0.0f;
                cellY -= (rowMaxH + 14.0f);
                rowMaxH = 18.0f;
            }
            float offX = cellX - minX;
            float offY = cellY - (minY + maxY) * 0.5f;

            for (const auto& g : sym.segs) {
                if (!matchUS(g.unit, g.style) || out.traces.size() >= 80000) continue;
                out.traces.push_back({ offX + g.x1, offY + g.y1, offX + g.x2, offY + g.y2, g.w, EdaLayerId::SchSymbol, 0 });
            }
            for (const auto& c : sym.circs) {
                if (!matchUS(c.unit, c.style)) continue;
                out.circles.push_back({ offX + c.cx, offY + c.cy, c.r, c.w, false, EdaLayerId::SchSymbol });
            }
            for (const auto& t : sym.texts) {
                if (!matchUS(t.unit, t.style) || out.labels.size() >= 4096) continue;
                out.labels.push_back({ offX + t.x, offY + t.y, 0.85f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(t.text) });
            }

            int symPins = 0;
            for (const auto& p : sym.pins) {
                if (!matchUS(p.unit, p.style) || p.hidden) continue;
                float tx = offX + p.x;
                float ty = offY + p.y;
                float rad = p.angleDeg * 3.14159265f / 180.0f;
                float dx = std::cos(rad), dy = std::sin(rad);
                float nx = -dy, ny = dx;
                float bx = tx + p.length * dx;
                float by = ty + p.length * dy;

                const bool isInv = (p.shape == "inverted" || p.shape == "inverted_clock");
                const bool isClk = (p.shape == "clock" || p.shape == "inverted_clock" || p.shape == "edge_clock_high");
                float stemEndLen = isInv ? std::max(0.0f, p.length - 1.25f) : p.length;
                if (stemEndLen > 0.02f) {
                    out.traces.push_back({ tx, ty, tx + stemEndLen * dx, ty + stemEndLen * dy, 0.24f, EdaLayerId::SchWire, 1 });
                }
                if (isInv) {
                    out.circles.push_back({ bx - 0.62f * dx, by - 0.62f * dy, 0.60f, 0.22f, false, EdaLayerId::SchSymbol });
                }
                if (isClk) {
                    out.traces.push_back({ bx + 0.75f * nx, by + 0.75f * ny, bx + 1.35f * dx, by + 1.35f * dy, 0.20f, EdaLayerId::SchSymbol, 0 });
                    out.traces.push_back({ bx - 0.75f * nx, by - 0.75f * ny, bx + 1.35f * dx, by + 1.35f * dy, 0.20f, EdaLayerId::SchSymbol, 0 });
                }
                if (p.shape == "non_logic") {
                    out.traces.push_back({ tx - 0.5f, ty - 0.5f, tx + 0.5f, ty + 0.5f, 0.20f, EdaLayerId::SchSymbol, 0 });
                    out.traces.push_back({ tx - 0.5f, ty + 0.5f, tx + 0.5f, ty - 0.5f, 0.20f, EdaLayerId::SchSymbol, 0 });
                }

                EdaPadPrim pad{};
                pad.x = tx; pad.y = ty;
                pad.w = 0.58f; pad.h = 0.58f;
                pad.shape = 0;
                pad.layer = EdaLayerId::SchSymbol;
                pad.pinName = PathUtils::Utf8ToWide(p.number.empty() ? p.name : p.number);
                pad.netName = PathUtils::Utf8ToWide(p.name);
                out.pads.push_back(std::move(pad));

                if (!sym.hidePinNames && !p.name.empty() && p.name != "~" && out.labels.size() < 4096) {
                    out.labels.push_back({ bx + 0.65f * dx - 0.4f, by + 0.65f * dy - 0.25f, 0.80f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(p.name) });
                }
                if (!sym.hidePinNums && !p.number.empty() && p.number != "~" && out.labels.size() < 4096) {
                    float mx = (tx + bx) * 0.5f + 0.55f * nx;
                    float my = (ty + by) * 0.5f + 0.55f * ny;
                    out.labels.push_back({ mx, my, 0.72f, EdaLayerId::TopSilk, PathUtils::Utf8ToWide(p.number) });
                }
                ++symPins;
                ++totalPins;
            }

            std::string dispTitle = sym.name;
            if (!sym.valueStr.empty() && sym.valueStr != sym.name) dispTitle += " (" + sym.valueStr + ")";
            out.labels.push_back({ offX + minX, offY + maxY + 1.8f, 1.05f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(dispTitle) });

            EdaComponent3D comp;
            comp.refDes = PathUtils::Utf8ToWide(sym.name);
            comp.value = PathUtils::Utf8ToWide(sym.valueStr.empty() ? sym.refPrefix : sym.valueStr);
            comp.footprint = PathUtils::Utf8ToWide(sym.footprintStr.empty() ? "KICAD_SYM" : sym.footprintStr);
            comp.x = offX + (minX + maxX) * 0.5f;
            comp.y = offY + (minY + maxY) * 0.5f;
            comp.widthMm = symW;
            comp.lengthMm = symH;
            comp.heightMm = 1.2f;
            comp.pinCount = std::max(1, symPins);
            comp.category = EdaPartCategory::IC_Package;
            out.components.push_back(std::move(comp));

            cellX += symW + 14.0f;
            rowMaxH = std::max(rowMaxH, symH);
            ++colIdx;
            ++renderedSyms;
        }

        if (renderedSyms == 0 || (out.traces.empty() && out.circles.empty() && out.pads.empty())) {
            out = ParsedEdaBoard{};
            return false;
        }
        out.formatTitle = L"KiCad Schematic Symbol Library (.kicad_sym — " +
                          std::to_wstring(renderedSyms) + L" symbol" + (renderedSyms == 1 ? L"" : L"s") +
                          L", " + std::to_wstring(totalPins) + L" pins)";
        out.netCount = std::max<size_t>(1u, totalPins > 0 ? totalPins : renderedSyms);
        return true;
    }
    return false;
}

} // namespace Formats
