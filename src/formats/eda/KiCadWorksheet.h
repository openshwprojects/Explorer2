#pragma once
// KiCadWorksheet.h — KiCad Drawing Sheet / Title Block Layout (.kicad_wks)
// Supports KiCad 5–9 `(kicad_wks ...)` and legacy `(page_layout ...)` S-expressions:
//   - `(setup ...)` margins, default linewidth, default textsize
//   - 4 margin-relative coordinate anchors (`ltcorner`, `lbcorner`, `rtcorner`, `rbcorner`)
//   - `(rect ...)`, `(line ...)`, `(polygon ... (pts (xy ...)))`, `(tbtext ...)`, `(bitmap ...)`
//   - `(repeat N)` with `(incrx dx)` / `(incry dy)` border zone ticks & table rows (bounded)

namespace Formats {

namespace DetailKiCadWks {

enum class CornerAnchor : uint8_t {
    RightBottom = 0, // rbcorner (KiCad default)
    RightTop    = 1, // rtcorner
    LeftBottom  = 2, // lbcorner
    LeftTop     = 3, // ltcorner
};

struct WksSetup {
    double pageW        = 297.0; // ISO A4 landscape reference sheet (mm)
    double pageH        = 210.0;
    double leftMargin   = 10.0;
    double rightMargin  = 10.0;
    double topMargin    = 10.0;
    double bottomMargin = 10.0;
    double lineWidth    = 0.15;
    double textSizeH    = 1.5;
};

inline bool ParseCornerToken(std::string_view tok, CornerAnchor& outCorner) {
    if (tok == "rbcorner" || tok == "rb") { outCorner = CornerAnchor::RightBottom; return true; }
    if (tok == "rtcorner" || tok == "rt") { outCorner = CornerAnchor::RightTop;    return true; }
    if (tok == "lbcorner" || tok == "lb") { outCorner = CornerAnchor::LeftBottom;  return true; }
    if (tok == "ltcorner" || tok == "lt") { outCorner = CornerAnchor::LeftTop;     return true; }
    return false;
}

inline void ResolveWksPoint(
    const WksSetup& s,
    double dx,
    double dy,
    CornerAnchor corner,
    float& outX,
    float& outY)
{
    double x = 0.0;
    double y = 0.0;
    switch (corner) {
    case CornerAnchor::LeftTop:
        x = s.leftMargin + dx;
        y = (s.pageH - s.topMargin) - dy;
        break;
    case CornerAnchor::LeftBottom:
        x = s.leftMargin + dx;
        y = s.bottomMargin + dy;
        break;
    case CornerAnchor::RightTop:
        x = (s.pageW - s.rightMargin) - dx;
        y = (s.pageH - s.topMargin) - dy;
        break;
    case CornerAnchor::RightBottom:
    default:
        x = (s.pageW - s.rightMargin) - dx;
        y = s.bottomMargin + dy;
        break;
    }
    outX = static_cast<float>(x);
    outY = static_cast<float>(y);
}

inline bool ParseAnchoredXY(
    const DetailSpecctra::SNode& n,
    double& outX,
    double& outY,
    CornerAnchor& outCorner)
{
    // Format: (start x y [ltcorner|lbcorner|rtcorner|rbcorner])
    if (!n.isList || n.list.size() < 3) return false;
    if (!DetailSpecctra::AtomFloat(n.list[1], outX)) return false;
    if (!DetailSpecctra::AtomFloat(n.list[2], outY)) return false;
    outCorner = CornerAnchor::RightBottom;
    if (n.list.size() >= 4 && !n.list[3].isList) {
        if (!ParseCornerToken(n.list[3].atom, outCorner)) return false;
    }
    return true;
}

inline std::string StepRepeatLabel(const std::string& base, int step) {
    if (step <= 0 || base.empty()) return base;
    if (base.size() == 1) {
        char c = base[0];
        if (c >= '1' && c <= '9') {
            return std::to_string((c - '0') + step);
        }
        if (c >= 'A' && c <= 'Z') {
            int idx = (c - 'A') + step;
            if (idx >= 0 && idx < 26) return std::string(1, static_cast<char>('A' + idx));
        }
        if (c >= 'a' && c <= 'z') {
            int idx = (c - 'a') + step;
            if (idx >= 0 && idx < 26) return std::string(1, static_cast<char>('a' + idx));
        }
    }
    return base;
}

} // namespace DetailKiCadWks

inline bool ParseKiCadWorksheet(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    std::string_view sv;
    if (!FormatPrologue::PrepareUtf8TextNoNul(data, size, sv, 16u, (16u << 20))) return false;

    std::vector<DetailSpecctra::SNode> roots;
    if (!DetailSpecctra::ParseSExprTree(sv, roots) || roots.size() != 1) return false;

    const auto& root = roots[0];
    std::string_view rootKey = DetailSpecctra::HeadKey(root);
    if (rootKey != "kicad_wks" && rootKey != "page_layout") return false;

    DetailKiCadWks::WksSetup setup;
    std::string versionStr;
    size_t itemCount = 0;

    // Pass 1: parse (setup ...) and (version ...)
    for (size_t i = 1; i < root.list.size(); ++i) {
        const auto& child = root.list[i];
        std::string_view k = DetailSpecctra::HeadKey(child);
        if (k == "version" && child.list.size() >= 2 && !child.list[1].isList) {
            versionStr = child.list[1].atom;
        } else if (k == "setup") {
            for (size_t j = 1; j < child.list.size(); ++j) {
                const auto& sc = child.list[j];
                std::string_view sk = DetailSpecctra::HeadKey(sc);
                double v1 = 0.0, v2 = 0.0;
                if (sk == "linewidth" && sc.list.size() >= 2 && DetailSpecctra::AtomFloat(sc.list[1], v1) && v1 > 0.0) {
                    setup.lineWidth = std::clamp(v1, 0.05, 5.0);
                } else if (sk == "textsize" && sc.list.size() >= 3 &&
                           DetailSpecctra::AtomFloat(sc.list[1], v1) && DetailSpecctra::AtomFloat(sc.list[2], v2)) {
                    setup.textSizeH = std::clamp(std::max(v1, v2), 0.5, 20.0);
                } else if (sk == "left_margin" && sc.list.size() >= 2 && DetailSpecctra::AtomFloat(sc.list[1], v1)) {
                    setup.leftMargin = std::clamp(v1, 0.0, 80.0);
                } else if (sk == "right_margin" && sc.list.size() >= 2 && DetailSpecctra::AtomFloat(sc.list[1], v1)) {
                    setup.rightMargin = std::clamp(v1, 0.0, 80.0);
                } else if (sk == "top_margin" && sc.list.size() >= 2 && DetailSpecctra::AtomFloat(sc.list[1], v1)) {
                    setup.topMargin = std::clamp(v1, 0.0, 80.0);
                } else if (sk == "bottom_margin" && sc.list.size() >= 2 && DetailSpecctra::AtomFloat(sc.list[1], v1)) {
                    setup.bottomMargin = std::clamp(v1, 0.0, 80.0);
                }
            }
        }
    }

    out = ParsedEdaBoard{};
    out.kind = EdaDocKind::KiCadSch;

    // Emit outer A4 reference sheet outline on EdgeCuts
    const float pw = static_cast<float>(setup.pageW);
    const float ph = static_cast<float>(setup.pageH);
    out.traces.push_back({ 0.0f, 0.0f, pw, 0.0f, 0.25f, EdaLayerId::EdgeCuts, 0 });
    out.traces.push_back({ pw, 0.0f, pw, ph,     0.25f, EdaLayerId::EdgeCuts, 0 });
    out.traces.push_back({ pw, ph,   0.0f, ph,   0.25f, EdaLayerId::EdgeCuts, 0 });
    out.traces.push_back({ 0.0f, ph, 0.0f, 0.0f, 0.25f, EdaLayerId::EdgeCuts, 0 });

    // Pass 2: parse drawing elements
    for (size_t i = 1; i < root.list.size(); ++i) {
        const auto& item = root.list[i];
        std::string_view k = DetailSpecctra::HeadKey(item);
        if (k != "rect" && k != "line" && k != "polygon" && k != "tbtext" && k != "bitmap") continue;

        double lw = setup.lineWidth;
        int repeat = 1;
        double incrx = 0.0, incry = 0.0;
        double rotDeg = 0.0;
        double fontSizeH = setup.textSizeH;

        bool hasStart = false, hasEnd = false, hasPos = false;
        double sx = 0.0, sy = 0.0, ex = 0.0, ey = 0.0, px = 0.0, py = 0.0;
        DetailKiCadWks::CornerAnchor sCorner = DetailKiCadWks::CornerAnchor::RightBottom;
        DetailKiCadWks::CornerAnchor eCorner = DetailKiCadWks::CornerAnchor::RightBottom;
        DetailKiCadWks::CornerAnchor pCorner = DetailKiCadWks::CornerAnchor::RightBottom;
        std::vector<std::pair<double, double>> polyPts;

        for (size_t j = 1; j < item.list.size(); ++j) {
            const auto& attr = item.list[j];
            std::string_view ak = DetailSpecctra::HeadKey(attr);
            double v = 0.0;
            if (ak == "start") {
                if (!DetailKiCadWks::ParseAnchoredXY(attr, sx, sy, sCorner)) return false;
                hasStart = true;
            } else if (ak == "end") {
                if (!DetailKiCadWks::ParseAnchoredXY(attr, ex, ey, eCorner)) return false;
                hasEnd = true;
            } else if (ak == "pos") {
                if (!DetailKiCadWks::ParseAnchoredXY(attr, px, py, pCorner)) return false;
                hasPos = true;
            } else if (ak == "linewidth" && attr.list.size() >= 2 && DetailSpecctra::AtomFloat(attr.list[1], v) && v >= 0.0) {
                lw = std::clamp(v > 0.0 ? v : setup.lineWidth, 0.05, 5.0);
            } else if (ak == "repeat" && attr.list.size() >= 2 && DetailSpecctra::AtomFloat(attr.list[1], v)) {
                repeat = std::clamp(static_cast<int>(v), 1, 128);
            } else if (ak == "incrx" && attr.list.size() >= 2 && DetailSpecctra::AtomFloat(attr.list[1], v)) {
                incrx = v;
            } else if (ak == "incry" && attr.list.size() >= 2 && DetailSpecctra::AtomFloat(attr.list[1], v)) {
                incry = v;
            } else if (ak == "rotate" && attr.list.size() >= 2 && DetailSpecctra::AtomFloat(attr.list[1], v)) {
                rotDeg = v;
            } else if (ak == "font") {
                for (size_t fi = 1; fi < attr.list.size(); ++fi) {
                    const auto& fc = attr.list[fi];
                    if (DetailSpecctra::HeadKey(fc) == "size" && fc.list.size() >= 3) {
                        double fh = 0.0, fw = 0.0;
                        if (DetailSpecctra::AtomFloat(fc.list[1], fh) && DetailSpecctra::AtomFloat(fc.list[2], fw)) {
                            fontSizeH = std::clamp(std::max(fh, fw), 0.5, 20.0);
                        }
                    }
                }
            } else if (ak == "pts") {
                for (size_t pi = 1; pi < attr.list.size(); ++pi) {
                    const auto& xy = attr.list[pi];
                    if (DetailSpecctra::HeadKey(xy) == "xy" && xy.list.size() >= 3) {
                        double vx = 0.0, vy = 0.0;
                        if (!DetailSpecctra::AtomFloat(xy.list[1], vx) ||
                            !DetailSpecctra::AtomFloat(xy.list[2], vy)) return false;
                        polyPts.emplace_back(vx, vy);
                    }
                }
            }
        }

        const float stroke = static_cast<float>(lw);

        if (k == "rect") {
            if (!hasStart || !hasEnd) return false;
            for (int r = 0; r < repeat; ++r) {
                float x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f;
                DetailKiCadWks::ResolveWksPoint(setup, sx + r * incrx, sy + r * incry, sCorner, x1, y1);
                DetailKiCadWks::ResolveWksPoint(setup, ex + r * incrx, ey + r * incry, eCorner, x2, y2);
                out.traces.push_back({ x1, y1, x2, y1, stroke, EdaLayerId::SchSymbol, 0 });
                out.traces.push_back({ x2, y1, x2, y2, stroke, EdaLayerId::SchSymbol, 0 });
                out.traces.push_back({ x2, y2, x1, y2, stroke, EdaLayerId::SchSymbol, 0 });
                out.traces.push_back({ x1, y2, x1, y1, stroke, EdaLayerId::SchSymbol, 0 });
            }
            ++itemCount;
        } else if (k == "line") {
            if (!hasStart || !hasEnd) return false;
            for (int r = 0; r < repeat; ++r) {
                float x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f;
                DetailKiCadWks::ResolveWksPoint(setup, sx + r * incrx, sy + r * incry, sCorner, x1, y1);
                DetailKiCadWks::ResolveWksPoint(setup, ex + r * incrx, ey + r * incry, eCorner, x2, y2);
                out.traces.push_back({ x1, y1, x2, y2, stroke, EdaLayerId::SchWire, 0 });
            }
            ++itemCount;
        } else if (k == "polygon") {
            if (!hasPos || polyPts.size() < 3) return false;
            double rad = rotDeg * (3.14159265358979323846 / 180.0);
            double cr = std::cos(rad), sr = std::sin(rad);
            for (int r = 0; r < repeat; ++r) {
                float ox = 0.0f, oy = 0.0f;
                DetailKiCadWks::ResolveWksPoint(setup, px + r * incrx, py + r * incry, pCorner, ox, oy);
                std::vector<std::pair<float, float>> wpts;
                wpts.reserve(polyPts.size());
                for (const auto& [lx, ly] : polyPts) {
                    // Polygon local coords in KiCad wks are mm offsets (Y-down in sheet space -> -Y in Cartesian)
                    double rx = lx * cr - (-ly) * sr;
                    double ry = lx * sr + (-ly) * cr;
                    wpts.emplace_back(ox + static_cast<float>(rx), oy + static_cast<float>(ry));
                }
                for (size_t pi = 0; pi < wpts.size(); ++pi) {
                    const auto& a = wpts[pi];
                    const auto& b = wpts[(pi + 1) % wpts.size()];
                    out.traces.push_back({ a.first, a.second, b.first, b.second, stroke, EdaLayerId::TopSilk, 0 });
                }
            }
            ++itemCount;
        } else if (k == "tbtext") {
            if (item.list.size() < 2 || item.list[1].isList || !hasPos) return false;
            const std::string& rawTxt = item.list[1].atom;
            for (int r = 0; r < repeat; ++r) {
                float tx = 0.0f, ty = 0.0f;
                DetailKiCadWks::ResolveWksPoint(setup, px + r * incrx, py + r * incry, pCorner, tx, ty);
                std::string stepped = DetailKiCadWks::StepRepeatLabel(rawTxt, r);
                if (!stepped.empty()) {
                    EdaTextLabel lbl{};
                    lbl.x = tx;
                    lbl.y = ty;
                    lbl.size = static_cast<float>(fontSizeH);
                    lbl.layer = EdaLayerId::SchLabel;
                    lbl.text = PathUtils::Utf8ToWide(stepped);
                    out.labels.push_back(std::move(lbl));
                }
            }
            ++itemCount;
        } else if (k == "bitmap") {
            if (!hasPos) return false;
            float bx = 0.0f, by = 0.0f;
            DetailKiCadWks::ResolveWksPoint(setup, px, py, pCorner, bx, by);
            DetailSiliconAndBusEda::AddRectOutline(out, bx, by, 14.0f, 8.0f, EdaLayerId::TopSilk, 0.2f);
            out.labels.push_back({ bx - 4.5f, by, 1.4f, EdaLayerId::SchLabel, L"LOGO" });
            ++itemCount;
        }
    }

    if (itemCount == 0) return false;

    out.formatTitle = L"KiCad Drawing Sheet / Title Block (.kicad_wks)";
    if (!versionStr.empty()) {
        out.formatTitle += L" v" + PathUtils::Utf8ToWide(versionStr);
    }
    return true;
}

} // namespace Formats
