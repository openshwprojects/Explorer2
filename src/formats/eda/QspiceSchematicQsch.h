#pragma once
// QspiceSchematicQsch.h — Mike Engelhardt's QSPICE Schematic (.qsch) Reader
// Validates the 4-byte QSPICE header signature (`0xFF 0xD8 0xFF 0xDB`) and parses nested guillemet
// `«...»` (CP1252 `0xAB`..`0xBB` or UTF-8 `0xC2 0xAB`..`0xC2 0xBB`) S-record AST nodes:
//   - `«schematic (x,y) ...»`
//   - `«component (x,y) rot mirror «symbol ... «line» «rect» «ellipse»/«circle» «arc» «triangle» «coil» «pin» «text» «property»»»`
//   - `«wire (x1,y1) (x2,y2) netName»`
//   - `«net (x,y) rot flags "netName"»` / `«junction (x,y)»` / `«text (x,y) ...»`
// NOTE: Included inside `namespace Explorer2`. Do NOT add `#include` directives here.

namespace Formats {

namespace DetailQspiceQsch {

struct QNode {
    bool isBlock = false; // `« ... »`
    std::string atom;     // e.g. `component`, `(1000,2000)`, `"R1"`
    std::vector<QNode> children;
};

// Parse `(x,y)` coordinate tuple in QSPICE schematic units (typically 100 units = 2.54 mm -> scale 0.0254f,
// or auto-normalized if schematic uses fine 1000-unit grid!)
inline bool ParseQschCoordPair(std::string_view tok, double& outX, double& outY) {
    tok = ReaderText::TrimSv(tok);
    if (tok.size() < 5u || tok.front() != '(' || tok.back() != ')') return false;
    tok = tok.substr(1u, tok.size() - 2u);
    const size_t comma = tok.find(',');
    if (comma == std::string_view::npos) return false;
    return ReaderText::ParseDoubleSv(ReaderText::TrimSv(tok.substr(0u, comma)), outX) &&
           ReaderText::ParseDoubleSv(ReaderText::TrimSv(tok.substr(comma + 1u)), outY);
}

inline std::string UnquoteQsch(std::string_view s) {
    s = ReaderText::TrimSv(s);
    if (s.size() >= 2u && s.front() == '"' && s.back() == '"') {
        return std::string(s.substr(1u, s.size() - 2u));
    }
    return std::string(s);
}

// Tokenize & build recursive `« ... »` QSPICE AST (supports both CP1252 `0xAB`/`0xBB` and UTF-8 `C2 AB`/`C2 BB`)
inline bool ParseQschAst(const uint8_t* bytes, size_t len, QNode& outDocument) {
    outDocument = QNode{};
    outDocument.isBlock = true;

    std::vector<QNode*> stack;
    stack.push_back(&outDocument);

    size_t i = 0u;
    size_t nodeCount = 0u;
    constexpr size_t kMaxNodes = 250000u;

    auto isOpenGuill = [&](size_t p, size_t& adv) -> bool {
        if (bytes[p] == 0xABu) { adv = 1u; return true; }
        if (p + 1u < len && bytes[p] == 0xC2u && bytes[p + 1u] == 0xABu) { adv = 2u; return true; }
        return false;
    };
    auto isCloseGuill = [&](size_t p, size_t& adv) -> bool {
        if (bytes[p] == 0xBBu) { adv = 1u; return true; }
        if (p + 1u < len && bytes[p] == 0xC2u && bytes[p + 1u] == 0xBBu) { adv = 2u; return true; }
        return false;
    };

    while (i < len) {
        const uint8_t b = bytes[i];
        if (b == 0u) return false; // Embedded NUL inside text body
        if (b <= 0x20u) { ++i; continue; }

        size_t adv = 0u;
        if (isOpenGuill(i, adv)) {
            if (stack.size() >= 32u || ++nodeCount > kMaxNodes) return false;
            i += adv;
            stack.back()->children.push_back(QNode{ true, {}, {} });
            stack.push_back(&stack.back()->children.back());
            continue;
        }
        if (isCloseGuill(i, adv)) {
            if (stack.size() <= 1u) return false; // Unmatched `»`
            i += adv;
            stack.pop_back();
            continue;
        }

        // Quoted string `"..."`
        if (b == '"') {
            ++i;
            std::string s = "\"";
            while (i < len && bytes[i] != '"') {
                if (bytes[i] == 0u) return false;
                if (bytes[i] == '\\' && i + 1u < len) {
                    ++i;
                    s.push_back(static_cast<char>(bytes[i++]));
                } else {
                    const uint8_t ch = bytes[i++];
                    if (ch == 0xB5u) s += "u"; // CP1252 micro `µ` -> `u`
                    else if (ch >= 0x20u && ch < 0x7Fu) s.push_back(static_cast<char>(ch));
                    else if (ch >= 0x80u) s.push_back(static_cast<char>(ch));
                }
            }
            if (i >= len) return false; // Unclosed quote
            s.push_back('"');
            ++i;
            if (++nodeCount > kMaxNodes) return false;
            stack.back()->children.push_back(QNode{ false, std::move(s), {} });
            continue;
        }

        // Coordinate tuple `(x, y)` (may contain spaces after comma!)
        if (b == '(') {
            std::string tup;
            int pDepth = 0;
            while (i < len) {
                const uint8_t ch = bytes[i++];
                if (ch == 0u) return false;
                if (ch == '(') ++pDepth;
                else if (ch == ')') {
                    --pDepth;
                    tup.push_back(')');
                    if (pDepth == 0) break;
                    continue;
                }
                if (ch > 0x20u) tup.push_back(static_cast<char>(ch));
            }
            if (pDepth != 0) return false;
            if (++nodeCount > kMaxNodes) return false;
            stack.back()->children.push_back(QNode{ false, std::move(tup), {} });
            continue;
        }

        // Plain atom up to whitespace or `«` / `»` / `"` / `(`
        std::string atom;
        while (i < len) {
            if (bytes[i] == 0u) return false;
            if (bytes[i] <= 0x20u || bytes[i] == '"' || bytes[i] == '(') break;
            size_t gAdv = 0u;
            if (isOpenGuill(i, gAdv) || isCloseGuill(i, gAdv)) break;
            const uint8_t ch = bytes[i++];
            if (ch == 0xB5u) atom.push_back('u');
            else atom.push_back(static_cast<char>(ch));
        }
        if (!atom.empty()) {
            if (++nodeCount > kMaxNodes) return false;
            stack.back()->children.push_back(QNode{ false, std::move(atom), {} });
        }
    }

    return stack.size() == 1u && !outDocument.children.empty();
}

struct QschXf {
    double instX = 0.0;
    double instY = 0.0;
    int rot = 0;    // 0..3 (90-degree steps) or degrees (0, 90, 180, 270)
    int mirror = 0; // 0 or 1
    double scale = 0.0254;

    void Apply(double lx, double ly, float& outBx, float& outBy) const {
        double x = (mirror != 0) ? -lx : lx;
        double y = ly;
        int rSteps = rot;
        if (std::abs(rSteps) >= 45) rSteps = ((rSteps / 90) % 4 + 4) % 4;
        else                        rSteps = ((rSteps % 4) + 4) % 4;

        double rx = x, ry = y;
        switch (rSteps) {
        case 1: rx = -y; ry =  x; break;
        case 2: rx = -x; ry = -y; break;
        case 3: rx =  y; ry = -x; break;
        default: break;
        }
        outBx = static_cast<float>((instX + rx) * scale);
        // QSPICE uses mathematical +Y-up schematic coordinates!
        outBy = static_cast<float>((instY + ry) * scale);
    }
};

} // namespace DetailQspiceQsch

inline bool ParseQspiceSchematicQsch(
    const uint8_t* data,
    size_t size,
    ParsedEdaBoard& outBoard)
{
    using namespace DetailQspiceQsch;

    // Mandatory 4-byte QSPICE magic signature: `0xFF 0xD8 0xFF 0xDB`
    if (!data || size < 16u || size > (32u << 20)) return false;
    if (data[0] != 0xFFu || data[1] != 0xD8u || data[2] != 0xFFu || data[3] != 0xDBu) {
        return false;
    }

    size_t bodyStart = 4u;
    while (bodyStart < size && data[bodyStart] <= 0x20u) ++bodyStart;
    if (bodyStart >= size) return false;

    QNode doc;
    if (!ParseQschAst(data + bodyStart, size - bodyStart, doc)) return false;

    // Locate `«schematic ...»` root block
    const QNode* schBlock = nullptr;
    for (const auto& ch : doc.children) {
        if (ch.isBlock && !ch.children.empty() && !ch.children[0].isBlock &&
            ReaderText::AsciiEqCI(ch.children[0].atom, "schematic")) {
            schBlock = &ch;
            break;
        }
    }
    if (!schBlock) return false;

    // Inspect coordinate magnitude across schematic to calibrate unit -> mm scale cleanly
    // (QSPICE standard pin grid is 100 units = 2.54 mm -> 0.0254; some dense files use 1000 units -> 0.01)
    double maxAbsCoord = 0.0;
    for (const auto& item : schBlock->children) {
        if (!item.isBlock) continue;
        for (const auto& sub : item.children) {
            double x = 0.0, y = 0.0;
            if (!sub.isBlock && ParseQschCoordPair(sub.atom, x, y)) {
                maxAbsCoord = std::max({ maxAbsCoord, std::fabs(x), std::fabs(y) });
            }
        }
    }
    const double unitScale = (maxAbsCoord > 8000.0) ? 0.01 : 0.0254;

    outBoard = ParsedEdaBoard{};
    outBoard.kind = EdaDocKind::KiCadSch;

    int wireSeq = 1;
    std::set<std::string> netNames;
    std::map<std::pair<int64_t, int64_t>, int> wireEndpointDeg;

    auto collectCoords = [](const QNode& blk, std::vector<std::pair<double, double>>& outPts) {
        outPts.clear();
        for (size_t k = 1u; k < blk.children.size(); ++k) {
            double x = 0.0, y = 0.0;
            if (!blk.children[k].isBlock && ParseQschCoordPair(blk.children[k].atom, x, y)) {
                outPts.emplace_back(x, y);
            }
        }
    };

    for (size_t idx = 1u; idx < schBlock->children.size(); ++idx) {
        const QNode& node = schBlock->children[idx];
        if (!node.isBlock || node.children.empty() || node.children[0].isBlock) continue;
        const std::string kw = ReaderText::ToLowerAscii(node.children[0].atom);

        if (kw == "wire") {
            std::vector<std::pair<double, double>> pts;
            collectCoords(node, pts);
            if (pts.size() >= 2u) {
                const float x1 = static_cast<float>(pts[0].first * unitScale);
                const float y1 = static_cast<float>(pts[0].second * unitScale);
                const float x2 = static_cast<float>(pts[1].first * unitScale);
                const float y2 = static_cast<float>(pts[1].second * unitScale);
                outBoard.traces.push_back({ x1, y1, x2, y2, 0.25f, EdaLayerId::SchWire, wireSeq++ });

                wireEndpointDeg[{ static_cast<int64_t>(std::llround(pts[0].first)), static_cast<int64_t>(std::llround(pts[0].second)) }]++;
                wireEndpointDeg[{ static_cast<int64_t>(std::llround(pts[1].first)), static_cast<int64_t>(std::llround(pts[1].second)) }]++;

                // Optional net name string at end of `«wire (x1,y1) (x2,y2) "VOUT"»`
                for (size_t k = 1u; k < node.children.size(); ++k) {
                    if (!node.children[k].isBlock && !node.children[k].atom.empty() && node.children[k].atom[0] != '(') {
                        std::string nm = UnquoteQsch(node.children[k].atom);
                        if (!nm.empty() && nm != "0" && !(nm[0] >= '0' && nm[0] <= '9')) {
                            netNames.insert(nm);
                        }
                    }
                }
            }
        } else if (kw == "net" || kw == "tap") {
            std::vector<std::pair<double, double>> pts;
            collectCoords(node, pts);
            if (!pts.empty()) {
                const float nx = static_cast<float>(pts[0].first * unitScale);
                const float ny = static_cast<float>(pts[0].second * unitScale);
                std::string netLbl;
                for (size_t k = node.children.size(); k > 1u; --k) {
                    const QNode& c = node.children[k - 1u];
                    if (!c.isBlock && !c.atom.empty() && c.atom[0] != '(') {
                        double dummy = 0.0;
                        if (!ReaderText::ParseDoubleSv(c.atom, dummy) || c.atom.front() == '"') {
                            netLbl = UnquoteQsch(c.atom);
                            break;
                        }
                    }
                }
                if (netLbl == "0" || ReaderText::AsciiEqCI(netLbl, "GND")) {
                    // Ground symbol triangle
                    outBoard.traces.push_back({ nx, ny, nx, ny - 1.2f, 0.24f, EdaLayerId::SchSymbol, 0 });
                    outBoard.traces.push_back({ nx - 1.3f, ny - 1.2f, nx + 1.3f, ny - 1.2f, 0.26f, EdaLayerId::SchSymbol, 0 });
                    outBoard.traces.push_back({ nx - 1.3f, ny - 1.2f, nx, ny - 2.3f, 0.24f, EdaLayerId::SchSymbol, 0 });
                    outBoard.traces.push_back({ nx + 1.3f, ny - 1.2f, nx, ny - 2.3f, 0.24f, EdaLayerId::SchSymbol, 0 });
                } else if (!netLbl.empty()) {
                    netNames.insert(netLbl);
                    outBoard.circles.push_back({ nx, ny, 0.35f, 0.20f, true, EdaLayerId::SchWire });
                    outBoard.labels.push_back({ nx + 0.4f, ny + 0.4f, 0.92f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(netLbl) });
                }
            }
        } else if (kw == "junction") {
            std::vector<std::pair<double, double>> pts;
            collectCoords(node, pts);
            if (!pts.empty()) {
                outBoard.circles.push_back({
                    static_cast<float>(pts[0].first * unitScale),
                    static_cast<float>(pts[0].second * unitScale),
                    0.42f, 0.22f, true, EdaLayerId::SchWire
                });
            }
        } else if (kw == "text" || kw == "comment") {
            std::vector<std::pair<double, double>> pts;
            collectCoords(node, pts);
            if (!pts.empty() && !node.children.back().isBlock) {
                std::string txt = UnquoteQsch(node.children.back().atom);
                if (!txt.empty() && txt[0] != '(') {
                    outBoard.labels.push_back({
                        static_cast<float>(pts[0].first * unitScale),
                        static_cast<float>(pts[0].second * unitScale),
                        0.95f, EdaLayerId::TopSilk, PathUtils::Utf8ToWide(txt)
                    });
                }
            }
        } else if (kw == "line" || kw == "rect" || kw == "ellipse" || kw == "circle") {
            // Sheet-level graphic annotation primitive
            std::vector<std::pair<double, double>> pts;
            collectCoords(node, pts);
            if (pts.size() >= 2u) {
                const float x1 = static_cast<float>(pts[0].first * unitScale);
                const float y1 = static_cast<float>(pts[0].second * unitScale);
                const float x2 = static_cast<float>(pts[1].first * unitScale);
                const float y2 = static_cast<float>(pts[1].second * unitScale);
                if (kw == "line") {
                    outBoard.traces.push_back({ x1, y1, x2, y2, 0.20f, EdaLayerId::BottomSilk, 0 });
                } else if (kw == "rect") {
                    DrawEdaBoxRect(outBoard, std::min(x1, x2), std::min(y1, y2), std::max(x1, x2), std::max(y1, y2), 0.20f, EdaLayerId::BottomSilk, 0);
                } else {
                    outBoard.circles.push_back({ (x1 + x2) * 0.5f, (y1 + y2) * 0.5f, std::max(0.25f, std::fabs(x2 - x1) * 0.5f), 0.20f, false, EdaLayerId::BottomSilk });
                }
            }
        } else if (kw == "component") {
            // `«component (x,y) rot mirror «symbol type ...» «property ...» ...»`
            double cx = 0.0, cy = 0.0;
            int rot = 0, mir = 0;
            size_t numIdx = 0u;
            for (size_t k = 1u; k < node.children.size(); ++k) {
                const QNode& c = node.children[k];
                if (c.isBlock) break;
                if (ParseQschCoordPair(c.atom, cx, cy)) continue;
                int iv = 0;
                if (ReaderText::ParseIntSv(c.atom, iv)) {
                    if (numIdx == 0u) rot = iv;
                    else if (numIdx == 1u) mir = iv;
                    ++numIdx;
                }
            }

            QschXf xf{ cx, cy, rot, mir, unitScale };
            std::string symType, refDes, valStr;
            size_t pinCount = 0u;
            const size_t tracesBefore = outBoard.traces.size() + outBoard.circles.size();

            auto parsePropertyBlock = [&](const QNode& propBlk) {
                // In QSPICE, `«text (x,y) size align flags font "R1"»` or `«property "InstName" "R1" ...»`
                std::vector<std::string> quoted;
                for (size_t k = 1u; k < propBlk.children.size(); ++k) {
                    const QNode& pc = propBlk.children[k];
                    if (!pc.isBlock && !pc.atom.empty() && pc.atom.front() == '"') {
                        quoted.push_back(UnquoteQsch(pc.atom));
                    }
                }
                if (quoted.size() >= 2u) {
                    if (ReaderText::AsciiEqCI(quoted[0], "InstName") || ReaderText::AsciiEqCI(quoted[0], "Ref")) refDes = quoted[1];
                    else if (ReaderText::AsciiEqCI(quoted[0], "Value") || ReaderText::AsciiEqCI(quoted[0], "SpiceModel")) valStr = quoted[1];
                } else if (quoted.size() == 1u && !quoted[0].empty()) {
                    // QSPICE `symbol` embeds two `«text ... "R1"»` and `«text ... "10k"»` children (1st = RefDes, 2nd = Value)!
                    if (refDes.empty()) refDes = quoted[0];
                    else if (valStr.empty()) valStr = quoted[0];
                }
            };

            for (size_t k = 1u; k < node.children.size(); ++k) {
                const QNode& sub = node.children[k];
                if (!sub.isBlock || sub.children.empty() || sub.children[0].isBlock) continue;
                const std::string skw = ReaderText::ToLowerAscii(sub.children[0].atom);

                if (skw == "property" || skw == "text") {
                    parsePropertyBlock(sub);
                } else if (skw == "symbol") {
                    if (sub.children.size() >= 2u && !sub.children[1].isBlock) {
                        symType = UnquoteQsch(sub.children[1].atom);
                    }
                    for (size_t si = 1u; si < sub.children.size(); ++si) {
                        const QNode& prim = sub.children[si];
                        if (!prim.isBlock || prim.children.empty() || prim.children[0].isBlock) continue;
                        const std::string pkw = ReaderText::ToLowerAscii(prim.children[0].atom);

                        std::vector<std::pair<double, double>> pts;
                        collectCoords(prim, pts);

                        if (pkw == "line" && pts.size() >= 2u) {
                            for (size_t p = 0u; p + 1u < pts.size(); ++p) {
                                float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
                                xf.Apply(pts[p].first, pts[p].second, x1, y1);
                                xf.Apply(pts[p + 1u].first, pts[p + 1u].second, x2, y2);
                                outBoard.traces.push_back({ x1, y1, x2, y2, 0.24f, EdaLayerId::SchSymbol, 0 });
                            }
                        } else if (pkw == "rect" && pts.size() >= 2u) {
                            float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
                            xf.Apply(pts[0].first, pts[0].second, x1, y1);
                            xf.Apply(pts[1].first, pts[1].second, x2, y2);
                            DrawEdaBoxRect(outBoard, std::min(x1, x2), std::min(y1, y2), std::max(x1, x2), std::max(y1, y2), 0.24f, EdaLayerId::SchSymbol, 0);
                        } else if ((pkw == "triangle" || pkw == "polygon") && pts.size() >= 3u) {
                            for (size_t p = 0u; p < pts.size(); ++p) {
                                const auto& a = pts[p];
                                const auto& bPt = pts[(p + 1u) % pts.size()];
                                float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
                                xf.Apply(a.first, a.second, x1, y1);
                                xf.Apply(bPt.first, bPt.second, x2, y2);
                                outBoard.traces.push_back({ x1, y1, x2, y2, 0.24f, EdaLayerId::SchSymbol, 0 });
                            }
                        } else if ((pkw == "ellipse" || pkw == "circle") && pts.size() >= 2u) {
                            float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
                            xf.Apply(pts[0].first, pts[0].second, x1, y1);
                            xf.Apply(pts[1].first, pts[1].second, x2, y2);
                            const float rx = std::fabs(x2 - x1) * 0.5f;
                            const float ry = std::fabs(y2 - y1) * 0.5f;
                            outBoard.circles.push_back({ (x1 + x2) * 0.5f, (y1 + y2) * 0.5f, std::max(0.25f, (rx + ry) * 0.5f), 0.24f, false, EdaLayerId::SchSymbol });
                        } else if (pkw == "arc" && pts.size() >= 2u) {
                            // `«arc (bx1,by1) (bx2,by2) (sx,sy) (ex,ey)»` (bounding box + start/end angle vectors, or 3-pt arc)
                            const double mcx = (pts[0].first + pts[1].first) * 0.5;
                            const double mcy = (pts[0].second + pts[1].second) * 0.5;
                            const double rx = std::max(1.0, std::fabs(pts[1].first - pts[0].first) * 0.5);
                            const double ry = std::max(1.0, std::fabs(pts[1].second - pts[0].second) * 0.5);
                            double a0 = 0.0, a1 = 3.14159265358979323846;
                            if (pts.size() >= 4u) {
                                a0 = std::atan2(pts[2].second - mcy, pts[2].first - mcx);
                                a1 = std::atan2(pts[3].second - mcy, pts[3].first - mcx);
                            }
                            double sw = a1 - a0;
                            if (sw <= 0.0) sw += 6.283185307179586;
                            float prevX = 0, prevY = 0;
                            xf.Apply(mcx + rx * std::cos(a0), mcy + ry * std::sin(a0), prevX, prevY);
                            for (int s = 1; s <= 12; ++s) {
                                const double ang = a0 + sw * (static_cast<double>(s) / 12.0);
                                float qx = 0, qy = 0;
                                xf.Apply(mcx + rx * std::cos(ang), mcy + ry * std::sin(ang), qx, qy);
                                outBoard.traces.push_back({ prevX, prevY, qx, qy, 0.24f, EdaLayerId::SchSymbol, 0 });
                                prevX = qx; prevY = qy;
                            }
                        } else if (pkw == "coil" && pts.size() >= 2u) {
                            // QSPICE `«coil (x1,y1) (x2,y2) ...»` inductor primitive: 4 semicircular humps
                            const double dx = (pts[1].first - pts[0].first) / 4.0;
                            const double dy = (pts[1].second - pts[0].second) / 4.0;
                            const double nx = -dy * 0.55, ny = dx * 0.55;
                            for (int h = 0; h < 4; ++h) {
                                const double sx0 = pts[0].first + dx * h, sy0 = pts[0].second + dy * h;
                                const double sx1 = sx0 + dx, sy1 = sy0 + dy;
                                float bx0 = 0, by0 = 0, bxm = 0, bym = 0, bx1 = 0, by1 = 0;
                                xf.Apply(sx0, sy0, bx0, by0);
                                xf.Apply((sx0 + sx1) * 0.5 + nx, (sy0 + sy1) * 0.5 + ny, bxm, bym);
                                xf.Apply(sx1, sy1, bx1, by1);
                                outBoard.traces.push_back({ bx0, by0, bxm, bym, 0.24f, EdaLayerId::SchSymbol, 0 });
                                outBoard.traces.push_back({ bxm, bym, bx1, by1, 0.24f, EdaLayerId::SchSymbol, 0 });
                            }
                        } else if (pkw == "pin" && !pts.empty()) {
                            float px = 0, py = 0;
                            xf.Apply(pts[0].first, pts[0].second, px, py);
                            std::string pinName;
                            for (size_t m = prim.children.size(); m > 1u; --m) {
                                if (!prim.children[m - 1u].isBlock && !prim.children[m - 1u].atom.empty() &&
                                    prim.children[m - 1u].atom.front() == '"') {
                                    pinName = UnquoteQsch(prim.children[m - 1u].atom);
                                    break;
                                }
                            }
                            outBoard.pads.push_back({
                                px, py, 0.58f, 0.58f, 0.0f, 0.0f, 0, EdaLayerId::SchSymbol,
                                PathUtils::Utf8ToWide(pinName), L""
                            });
                            ++pinCount;
                        } else if (pkw == "text" || pkw == "property") {
                            parsePropertyBlock(prim);
                        }
                    }
                }
            }

            float wCx = 0, wCy = 0;
            xf.Apply(0.0, 0.0, wCx, wCy);
            if (outBoard.traces.size() + outBoard.circles.size() == tracesBefore) {
                DrawEdaBoxRect(outBoard, wCx - 2.2f, wCy - 1.6f, wCx + 2.2f, wCy + 1.6f, 0.24f, EdaLayerId::SchSymbol, 0);
            }

            std::wstring compLbl = PathUtils::Utf8ToWide(refDes.empty() ? (symType.empty() ? "U?" : symType) : refDes);
            if (!valStr.empty()) compLbl += L" (" + PathUtils::Utf8ToWide(valStr) + L")";
            outBoard.labels.push_back({ wCx + 1.8f, wCy + 1.2f, 0.85f, EdaLayerId::SchLabel, compLbl });

            EdaComponent3D comp;
            comp.refDes = PathUtils::Utf8ToWide(refDes.empty() ? "U?" : refDes);
            comp.value  = PathUtils::Utf8ToWide(valStr);
            comp.footprint = PathUtils::Utf8ToWide(symType.empty() ? "QSPICE_SYM" : symType);
            comp.x = wCx; comp.y = wCy;
            comp.rotDeg = static_cast<float>(rot * 90);
            comp.bottomSide = (mir != 0);
            comp.widthMm = 4.4f; comp.lengthMm = 3.6f; comp.heightMm = 1.0f;
            comp.pinCount = std::max<int>(1, static_cast<int>(pinCount));
            comp.category = ClassifyComponent(comp.refDes, comp.footprint, comp.widthMm, comp.lengthMm, comp.heightMm, comp.pinCount);
            outBoard.components.push_back(std::move(comp));
        }
    }

    // Add solder junction dots wherever >= 3 wire segments meet
    for (const auto& [pt, deg] : wireEndpointDeg) {
        if (deg >= 3) {
            outBoard.circles.push_back({
                static_cast<float>(static_cast<double>(pt.first) * unitScale),
                static_cast<float>(static_cast<double>(pt.second) * unitScale),
                0.38f, 0.22f, true, EdaLayerId::SchWire
            });
        }
    }

    if (outBoard.traces.empty() && outBoard.components.empty() && outBoard.pads.empty()) return false;

    wchar_t titleBuf[220] = {};
    swprintf_s(titleBuf, L"QSPICE Circuit Schematic (.qsch — %zu components, %d wires)",
               outBoard.components.size(), std::max(0, wireSeq - 1));
    outBoard.formatTitle = titleBuf;
    outBoard.netCount = std::max<size_t>(1u, netNames.empty() ? static_cast<size_t>(std::max(1, wireSeq - 1)) : netNames.size());
    return true;
}

} // namespace Formats
