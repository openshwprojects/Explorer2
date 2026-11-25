#pragma once
// HyperLynxPcb.h — Zero-dependency C++20 Siemens / Mentor HyperLynx PCB Layout (.hyp) multi-layer board reader.
// Included inside `namespace Explorer2` in src/EdaReaders.cpp.

namespace Formats {

namespace DetailHyperLynx {

using ::Explorer2::DrawEdaBoxRect;
inline void DrawBoxRect(ParsedEdaBoard& out, float x0, float y0, float x1, float y1, float strokeW, EdaLayerId lid, int netId = 0) { DrawEdaBoxRect(out, x0, y0, x1, y1, strokeW, lid, netId); }

inline void HatchCopperPolygon(
    const std::vector<std::pair<float, float>>& pts,
    EdaLayerId layer,
    int netId,
    ParsedEdaBoard& out)
{
    DetailBoardFabEda::HatchPolygonHorizontal(pts, layer, netId, out, 18.0f, 0.30f, 1.40f, 0.55f);
}

struct HypNode {
    char kind = '{'; // '{' or '('
    std::string headKey;
    std::string headVal;
    std::vector<std::string> tokens;
    std::vector<std::pair<std::string, std::string>> kv;
    std::vector<HypNode> children;

    std::string GetStr(std::string_view keyUpper) const {
        for (const auto& p : kv) {
            if (p.first == keyUpper) return p.second;
        }
        return {};
    }

    bool HasKey(std::string_view keyUpper) const {
        for (const auto& p : kv) {
            if (p.first == keyUpper) return true;
        }
        return false;
    }
};

inline std::string UpperAsciiHyp(std::string_view sv) {
    std::string s(sv);
    for (char& c : s) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return s;
}

inline std::string StripQuotesHyp(std::string_view sv) {
    sv = PathUtils::TrimAscii(sv);
    if (sv.size() >= 2u && ((sv.front() == '"' && sv.back() == '"') || (sv.front() == '\'' && sv.back() == '\''))) {
        sv = sv.substr(1u, sv.size() - 2u);
    }
    return std::string(sv);
}

inline float ParseHypDimMm(const std::string& raw, float unitToMm, float defMm = 0.0f) {
    if (raw.empty()) return defMm;
    char* endp = nullptr;
    double v = std::strtod(raw.c_str(), &endp);
    if (endp == raw.c_str() || !std::isfinite(v)) return defMm;
    std::string sfx = UpperAsciiHyp(PathUtils::TrimAscii(std::string_view(endp)));
    if (sfx == "MM" || sfx == "MILLIMETER") return static_cast<float>(v);
    if (sfx == "CM" || sfx == "CENTIMETER") return static_cast<float>(v * 10.0);
    if (sfx == "MIL" || sfx == "MILS") return static_cast<float>(v * 0.0254);
    if (sfx == "IN" || sfx == "INCH") return static_cast<float>(v * 25.4);
    if (sfx == "UM") return static_cast<float>(v * 1e-3);
    return static_cast<float>(v) * unitToMm;
}

inline bool ParseHypTree(std::string_view src, std::vector<HypNode>& outTop) {
    outTop.clear();
    // Reject binary payloads
    for (size_t i = 0; i < std::min<size_t>(src.size(), 8192u); ++i) {
        if (src[i] == '\0') return false;
    }

    std::vector<HypNode*> stack;
    size_t i = 0;
    const size_t n = src.size();
    bool atLineStart = true;
    size_t nodeCount = 0;

    while (i < n) {
        char c = src[i];
        if (c == '\r' || c == '\n') {
            atLineStart = true;
            ++i;
            continue;
        }
        if (c == ' ' || c == '\t') {
            ++i;
            continue;
        }
        // Line comment starting with '*' at beginning of line
        if (atLineStart && c == '*') {
            while (i < n && src[i] != '\n') ++i;
            continue;
        }
        atLineStart = false;

        // Block comment {* ... }
        if (c == '{' && i + 1u < n && src[i + 1u] == '*') {
            i += 2u;
            int depth = 1;
            while (i < n && depth > 0) {
                if (src[i] == '{') ++depth;
                else if (src[i] == '}') --depth;
                ++i;
            }
            if (depth != 0) return false;
            continue;
        }

        if (c == '{' || c == '(') {
            if (++nodeCount > 250000u || stack.size() >= 24u) return false;
            HypNode nd;
            nd.kind = c;
            ++i;
            if (stack.empty()) {
                outTop.push_back(std::move(nd));
                stack.push_back(&outTop.back());
            } else {
                stack.back()->children.push_back(std::move(nd));
                stack.push_back(&stack.back()->children.back());
            }
            continue;
        }

        if (c == '}' || c == ')') {
            if (stack.empty()) return false;
            char expected = (stack.back()->kind == '{') ? '}' : ')';
            if (c != expected) return false;
            stack.pop_back();
            ++i;
            continue;
        }

        if (c == ',') {
            ++i;
            continue;
        }

        if (stack.empty()) {
            // Skip top-level comments / whitespace-separated header noise if any
            ++i;
            continue;
        }

        // Parse token (possibly KEY=VALUE or quoted string)
        std::string tok;
        while (i < n) {
            char tc = src[i];
            if (tc == ' ' || tc == '\t' || tc == '\r' || tc == '\n' ||
                tc == '{' || tc == '}' || tc == '(' || tc == ')' || tc == ',') {
                break;
            }
            if (tc == '"' || tc == '\'') {
                char q = tc;
                ++i;
                while (i < n && src[i] != q) {
                    tok.push_back(src[i++]);
                }
                if (i < n && src[i] == q) ++i;
            } else {
                tok.push_back(tc);
                ++i;
            }
        }

        if (tok.empty()) continue;
        HypNode& cur = *stack.back();
        size_t eq = tok.find('=');
        if (cur.headKey.empty() && cur.tokens.empty() && cur.kv.empty()) {
            if (eq != std::string::npos) {
                cur.headKey = UpperAsciiHyp(std::string_view(tok).substr(0, eq));
                cur.headVal = StripQuotesHyp(std::string_view(tok).substr(eq + 1u));
                cur.kv.push_back({ cur.headKey, cur.headVal });
            } else {
                cur.headKey = UpperAsciiHyp(tok);
                cur.tokens.push_back(tok);
            }
        } else if (eq != std::string::npos) {
            std::string k = UpperAsciiHyp(std::string_view(tok).substr(0, eq));
            std::string v = StripQuotesHyp(std::string_view(tok).substr(eq + 1u));
            cur.kv.push_back({ std::move(k), std::move(v) });
        } else {
            cur.tokens.push_back(std::move(tok));
        }
    }

    return stack.empty() && !outTop.empty();
}

inline void TessellateHypArc(
    ParsedEdaBoard& out,
    float x1, float y1,
    float x2, float y2,
    float xc, float yc,
    float w,
    EdaLayerId lid,
    int netId)
{
    float r = std::hypot(x1 - xc, y1 - yc);
    if (r < 1e-4f) {
        out.traces.push_back({ x1, y1, x2, y2, w, lid, netId });
        return;
    }
    float a0 = std::atan2(y1 - yc, x1 - xc);
    float a1 = std::atan2(y2 - yc, x2 - xc);
    float da = a1 - a0;
    while (da <= -3.14159265f) da += 6.28318530f;
    while (da >   3.14159265f) da -= 6.28318530f;
    if (std::fabs(da) < 1e-3f && std::hypot(x2 - x1, y2 - y1) < 1e-3f) {
        da = 6.28318530f;
    }
    int steps = std::clamp(static_cast<int>(std::fabs(da) * 6.0f), 6, 24);
    float px = x1, py = y1;
    for (int s = 1; s <= steps; ++s) {
        float a = a0 + da * (static_cast<float>(s) / static_cast<float>(steps));
        float qx = (s == steps) ? x2 : (xc + r * std::cos(a));
        float qy = (s == steps) ? y2 : (yc + r * std::sin(a));
        out.traces.push_back({ px, py, qx, qy, w, lid, netId });
        px = qx;
        py = qy;
    }
}

} // namespace DetailHyperLynx

inline bool ParseHyperLynxPcbBoard(std::string_view text, ParsedEdaBoard& out) {
    if (text.size() < 16u) return false;
    std::vector<DetailHyperLynx::HypNode> topNodes;
    if (!DetailHyperLynx::ParseHypTree(text, topNodes)) return false;

    bool sawBoard = false;
    std::vector<const DetailHyperLynx::HypNode*> records;
    for (const auto& tn : topNodes) {
        if (tn.headKey == "BOARD") {
            sawBoard = true;
            records.push_back(&tn);
            for (const auto& ch : tn.children) records.push_back(&ch);
        } else {
            records.push_back(&tn);
        }
    }
    if (!sawBoard) return false;

    float unitToMm = 25.4f; // HyperLynx default ENGLISH = inches (25.4 mm)
    std::string unitLabel = "ENGLISH (in)";

    // Pass 1: Units & Stackup
    struct CopperLyr { std::string nameUpper; bool isPlane = false; };
    std::vector<CopperLyr> copperStack;

    for (const auto* r : records) {
        if (r->headKey == "UNITS") {
            std::string u = DetailHyperLynx::UpperAsciiHyp(r->headVal);
            bool hasMils = (u == "MILS" || u == "MIL");
            bool hasMm   = (u == "MM" || u == "MILLIMETER" || u == "MILLIMETERS");
            for (const auto& tk : r->tokens) {
                std::string ut = DetailHyperLynx::UpperAsciiHyp(tk);
                if (ut == "MILS" || ut == "MIL") hasMils = true;
                if (ut == "MM" || ut == "MILLIMETER" || ut == "MILLIMETERS") hasMm = true;
                if (u.empty() && (ut == "ENGLISH" || ut == "METRIC")) u = ut;
            }
            if (hasMils) {
                unitToMm = 0.0254f;
                unitLabel = "MILS";
            } else if (u == "METRIC" || hasMm) {
                unitToMm = hasMm ? 1.0f : 10.0f; // HyperLynx METRIC = cm unless MM specified
                unitLabel = hasMm ? "MM" : "METRIC (cm)";
            } else {
                unitToMm = 25.4f;
                unitLabel = "ENGLISH (in)";
            }
        } else if (r->headKey == "STACKUP") {
            for (const auto& sub : r->children) {
                if (sub.headKey == "SIGNAL" || sub.headKey == "PLANE") {
                    std::string ln = sub.GetStr("L");
                    if (ln.empty()) ln = sub.GetStr("NAME");
                    if (!ln.empty()) {
                        copperStack.push_back({ DetailHyperLynx::UpperAsciiHyp(ln), sub.headKey == "PLANE" });
                    }
                }
            }
        }
    }

    std::unordered_map<std::string, EdaLayerId> layerMap;
    for (size_t i = 0; i < copperStack.size(); ++i) {
        const std::string& nm = copperStack[i].nameUpper;
        if (i == 0u || nm == "TOP" || nm == "F.CU" || nm == "F_CU") {
            layerMap[nm] = EdaLayerId::TopCopper;
        } else if (i + 1u == copperStack.size() && copperStack.size() >= 2u) {
            layerMap[nm] = EdaLayerId::BottomCopper;
        } else if (nm.find("BOT") != std::string::npos || nm == "B.CU" || nm == "B_CU") {
            layerMap[nm] = EdaLayerId::BottomCopper;
        } else {
            layerMap[nm] = EdaLayerId::InnerCopper;
        }
    }

    auto mapHypLayer = [&](const std::string& rawName) -> EdaLayerId {
        std::string up = DetailHyperLynx::UpperAsciiHyp(DetailHyperLynx::StripQuotesHyp(rawName));
        auto it = layerMap.find(up);
        if (it != layerMap.end()) return it->second;
        if (up.find("BOT") != std::string::npos || up == "B.CU" || up == "B_CU" || up == "SOLDER") {
            return EdaLayerId::BottomCopper;
        }
        if (up.find("INT") != std::string::npos || up.rfind("IN", 0) == 0 || up.find("MID") != std::string::npos ||
            up.find("GND") != std::string::npos || up.find("VCC") != std::string::npos ||
            up.find("VDD") != std::string::npos || up.find("PWR") != std::string::npos) {
            return EdaLayerId::InnerCopper;
        }
        return EdaLayerId::TopCopper;
    };

    // Pass 2: Padstacks & Devices
    struct HypPadstack {
        float w = 1.2f;
        float h = 1.2f;
        float drill = 0.0f;
        uint8_t shape = 0; // 0=round, 1=rect, 3=oval
        bool multiLayer = false;
    };
    std::unordered_map<std::string, HypPadstack> padstacks;

    struct HypDevBuilder {
        std::string ref;
        std::string val;
        std::string pkg;
        std::string layerName;
        float x = 0.0f, y = 0.0f;
        float rot = 0.0f;
        bool hasXY = false;
        float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
        int pinCount = 0;
    };
    std::vector<HypDevBuilder> devList;
    std::unordered_map<std::string, size_t> devIndexByRef;

    auto addOutlineFromNode = [&](const DetailHyperLynx::HypNode& nd) {
        if (nd.headKey == "PERIMETER_SEGMENT") {
            float x1 = DetailHyperLynx::ParseHypDimMm(nd.GetStr("X1"), unitToMm);
            float y1 = DetailHyperLynx::ParseHypDimMm(nd.GetStr("Y1"), unitToMm);
            float x2 = DetailHyperLynx::ParseHypDimMm(nd.GetStr("X2"), unitToMm);
            float y2 = DetailHyperLynx::ParseHypDimMm(nd.GetStr("Y2"), unitToMm);
            out.traces.push_back({ x1, y1, x2, y2, 0.32f, EdaLayerId::EdgeCuts, 0 });
        } else if (nd.headKey == "PERIMETER_ARC") {
            float x1 = DetailHyperLynx::ParseHypDimMm(nd.GetStr("X1"), unitToMm);
            float y1 = DetailHyperLynx::ParseHypDimMm(nd.GetStr("Y1"), unitToMm);
            float x2 = DetailHyperLynx::ParseHypDimMm(nd.GetStr("X2"), unitToMm);
            float y2 = DetailHyperLynx::ParseHypDimMm(nd.GetStr("Y2"), unitToMm);
            float xc = DetailHyperLynx::ParseHypDimMm(nd.GetStr("XC"), unitToMm);
            float yc = DetailHyperLynx::ParseHypDimMm(nd.GetStr("YC"), unitToMm);
            DetailHyperLynx::TessellateHypArc(out, x1, y1, x2, y2, xc, yc, 0.32f, EdaLayerId::EdgeCuts, 0);
        }
    };

    for (const auto* r : records) {
        addOutlineFromNode(*r);

        if (r->headKey == "BOARD_OUTLINE") {
            std::vector<std::pair<float, float>> pts;
            for (const auto& sub : r->children) {
                addOutlineFromNode(sub);
                if (sub.HasKey("X") && sub.HasKey("Y")) {
                    pts.push_back({
                        DetailHyperLynx::ParseHypDimMm(sub.GetStr("X"), unitToMm),
                        DetailHyperLynx::ParseHypDimMm(sub.GetStr("Y"), unitToMm)
                    });
                }
            }
            if (pts.size() >= 2u) {
                for (size_t i = 0; i + 1u < pts.size(); ++i) {
                    out.traces.push_back({ pts[i].first, pts[i].second, pts[i + 1u].first, pts[i + 1u].second,
                                           0.32f, EdaLayerId::EdgeCuts, 0 });
                }
                if (pts.size() >= 3u &&
                    (std::fabs(pts.front().first - pts.back().first) > 1e-3f ||
                     std::fabs(pts.front().second - pts.back().second) > 1e-3f)) {
                    out.traces.push_back({ pts.back().first, pts.back().second, pts.front().first, pts.front().second,
                                           0.32f, EdaLayerId::EdgeCuts, 0 });
                }
            }
        } else if (r->headKey == "PADSTACK") {
            std::string psName = r->headVal;
            if (psName.empty()) psName = r->GetStr("NAME");
            if (psName.empty() && r->tokens.size() >= 2u) psName = r->tokens[1];
            if (psName.empty()) continue;

            HypPadstack ps;
            if (r->HasKey("DRILL")) {
                ps.drill = DetailHyperLynx::ParseHypDimMm(r->GetStr("DRILL"), unitToMm, 0.0f);
            } else if (!r->headVal.empty() && !r->tokens.empty()) {
                ps.drill = DetailHyperLynx::ParseHypDimMm(r->tokens[0], unitToMm, 0.0f);
            } else if (r->tokens.size() >= 3u) {
                ps.drill = DetailHyperLynx::ParseHypDimMm(r->tokens[2], unitToMm, 0.0f);
            }

            auto applyShapeStr = [&](const std::string& sh) {
                std::string us = DetailHyperLynx::UpperAsciiHyp(sh);
                if (us == "1" || us == "RECT" || us == "RECTANGLE" || us == "SQUARE") ps.shape = 1;
                else if (us == "0" || us == "OVAL" || us == "OBLONG") ps.shape = 3;
                else ps.shape = 0;
            };

            if (r->HasKey("SHAPE")) applyShapeStr(r->GetStr("SHAPE"));
            if (r->HasKey("W") || r->HasKey("SX") || r->HasKey("D") || r->HasKey("DIAM")) {
                std::string sw = r->GetStr("W");
                if (sw.empty()) sw = r->GetStr("SX");
                if (sw.empty()) sw = r->GetStr("D");
                if (sw.empty()) sw = r->GetStr("DIAM");
                ps.w = std::max(0.15f, DetailHyperLynx::ParseHypDimMm(sw, unitToMm, 1.2f));
                ps.h = ps.w;
            }
            if (r->HasKey("H") || r->HasKey("SY")) {
                std::string sh = r->GetStr("H");
                if (sh.empty()) sh = r->GetStr("SY");
                ps.h = std::max(0.15f, DetailHyperLynx::ParseHypDimMm(sh, unitToMm, ps.w));
            }

            size_t layerDefs = 0;
            for (const auto& sub : r->children) {
                ++layerDefs;
                if (sub.HasKey("SHAPE")) applyShapeStr(sub.GetStr("SHAPE"));
                if (sub.HasKey("W") || sub.HasKey("SX")) {
                    std::string sw = sub.HasKey("W") ? sub.GetStr("W") : sub.GetStr("SX");
                    ps.w = std::max(0.15f, DetailHyperLynx::ParseHypDimMm(sw, unitToMm, ps.w));
                }
                if (sub.HasKey("H") || sub.HasKey("SY")) {
                    std::string sh = sub.HasKey("H") ? sub.GetStr("H") : sub.GetStr("SY");
                    ps.h = std::max(0.15f, DetailHyperLynx::ParseHypDimMm(sh, unitToMm, ps.w));
                }
                if (sub.HasKey("DRILL")) {
                    ps.drill = DetailHyperLynx::ParseHypDimMm(sub.GetStr("DRILL"), unitToMm, ps.drill);
                }
                // Positional HyperLynx padstack subrecord: (layer_name, shape_code, sx, sy [, angle])
                if (sub.tokens.size() >= 4u) {
                    applyShapeStr(sub.tokens[1]);
                    ps.w = std::max(0.15f, DetailHyperLynx::ParseHypDimMm(sub.tokens[2], unitToMm, ps.w));
                    ps.h = std::max(0.15f, DetailHyperLynx::ParseHypDimMm(sub.tokens[3], unitToMm, ps.w));
                }
            }
            if (ps.shape == 3u && std::fabs(ps.w - ps.h) < 0.02f) ps.shape = 0u;
            ps.multiLayer = (ps.drill > 0.01f || layerDefs >= 2u);
            padstacks[DetailHyperLynx::UpperAsciiHyp(psName)] = ps;
        } else if (r->headKey == "DEVICES") {
            for (const auto& sub : r->children) {
                std::string ref = sub.GetStr("REF");
                if (ref.empty()) continue;
                HypDevBuilder db;
                db.ref = ref;
                db.val = sub.GetStr("VAL");
                if (db.val.empty()) db.val = sub.GetStr("VALUE");
                if (db.val.empty()) db.val = sub.GetStr("NAME");
                db.pkg = sub.GetStr("PKG");
                if (db.pkg.empty()) db.pkg = sub.GetStr("PACKAGE");
                db.layerName = sub.GetStr("L");
                if (db.layerName.empty()) db.layerName = sub.GetStr("LAYER");
                if (sub.HasKey("X") && sub.HasKey("Y")) {
                    db.x = DetailHyperLynx::ParseHypDimMm(sub.GetStr("X"), unitToMm);
                    db.y = DetailHyperLynx::ParseHypDimMm(sub.GetStr("Y"), unitToMm);
                    db.hasXY = true;
                }
                if (sub.HasKey("R") || sub.HasKey("ROT")) {
                    std::string rs = sub.HasKey("R") ? sub.GetStr("R") : sub.GetStr("ROT");
                    db.rot = static_cast<float>(std::strtod(rs.c_str(), nullptr));
                }
                devIndexByRef[DetailHyperLynx::UpperAsciiHyp(ref)] = devList.size();
                devList.push_back(std::move(db));
            }
        }
    }

    // Pass 3: Nets ({NET=...} -> SEG, ARC, VIA, PIN, POLYGON / POUR)
    size_t netCounter = 0;
    for (const auto* r : records) {
        if (r->headKey != "NET") continue;
        std::string netName = r->headVal;
        if (netName.empty()) netName = r->GetStr("NAME");
        if (netName.empty() && r->tokens.size() >= 2u) netName = r->tokens[1];
        ++netCounter;
        const int netId = static_cast<int>(netCounter);
        const std::wstring netNameW = PathUtils::Utf8ToWide(netName);

        for (const auto& item : r->children) {
            if (item.headKey == "SEG" || item.headKey == "LINE") {
                float x1 = DetailHyperLynx::ParseHypDimMm(item.GetStr("X1"), unitToMm);
                float y1 = DetailHyperLynx::ParseHypDimMm(item.GetStr("Y1"), unitToMm);
                float x2 = DetailHyperLynx::ParseHypDimMm(item.GetStr("X2"), unitToMm);
                float y2 = DetailHyperLynx::ParseHypDimMm(item.GetStr("Y2"), unitToMm);
                float w  = std::clamp(DetailHyperLynx::ParseHypDimMm(item.GetStr("W"), unitToMm, 0.25f), 0.06f, 12.0f);
                EdaLayerId lid = mapHypLayer(item.GetStr("L"));
                if (out.traces.size() < 80000u) {
                    out.traces.push_back({ x1, y1, x2, y2, w, lid, netId });
                }
            } else if (item.headKey == "ARC") {
                float x1 = DetailHyperLynx::ParseHypDimMm(item.GetStr("X1"), unitToMm);
                float y1 = DetailHyperLynx::ParseHypDimMm(item.GetStr("Y1"), unitToMm);
                float x2 = DetailHyperLynx::ParseHypDimMm(item.GetStr("X2"), unitToMm);
                float y2 = DetailHyperLynx::ParseHypDimMm(item.GetStr("Y2"), unitToMm);
                float xc = DetailHyperLynx::ParseHypDimMm(item.GetStr("XC"), unitToMm);
                float yc = DetailHyperLynx::ParseHypDimMm(item.GetStr("YC"), unitToMm);
                float w  = std::clamp(DetailHyperLynx::ParseHypDimMm(item.GetStr("W"), unitToMm, 0.25f), 0.06f, 12.0f);
                EdaLayerId lid = mapHypLayer(item.GetStr("L"));
                DetailHyperLynx::TessellateHypArc(out, x1, y1, x2, y2, xc, yc, w, lid, netId);
            } else if (item.headKey == "VIA") {
                float vx = DetailHyperLynx::ParseHypDimMm(item.GetStr("X"), unitToMm);
                float vy = DetailHyperLynx::ParseHypDimMm(item.GetStr("Y"), unitToMm);
                float diam = 0.7f, drill = 0.35f;
                std::string pRef = DetailHyperLynx::UpperAsciiHyp(item.GetStr("P"));
                if (pRef.empty()) pRef = DetailHyperLynx::UpperAsciiHyp(item.GetStr("PADSTACK"));
                auto itPs = padstacks.find(pRef);
                if (itPs != padstacks.end()) {
                    diam  = std::max(itPs->second.w, itPs->second.h);
                    drill = (itPs->second.drill > 0.02f) ? itPs->second.drill : (diam * 0.5f);
                }
                if (item.HasKey("D")) diam = DetailHyperLynx::ParseHypDimMm(item.GetStr("D"), unitToMm, diam);
                if (item.HasKey("DRILL")) drill = DetailHyperLynx::ParseHypDimMm(item.GetStr("DRILL"), unitToMm, drill);
                if (out.vias.size() < 40000u) {
                    out.vias.push_back({ vx, vy, std::max(diam, drill * 1.3f), drill, netId });
                }
            } else if (item.headKey == "PIN") {
                float px = DetailHyperLynx::ParseHypDimMm(item.GetStr("X"), unitToMm);
                float py = DetailHyperLynx::ParseHypDimMm(item.GetStr("Y"), unitToMm);
                float rot = static_cast<float>(std::strtod(item.GetStr("R").c_str(), nullptr));
                std::string idStr = item.GetStr("ID");
                std::string pinNm = item.GetStr("NAME");
                std::string refPart;
                size_t dot = idStr.find('.');
                if (dot != std::string::npos) {
                    refPart = idStr.substr(0, dot);
                    if (pinNm.empty()) pinNm = idStr.substr(dot + 1u);
                } else if (pinNm.empty()) {
                    pinNm = idStr;
                }

                HypPadstack ps;
                std::string pRef = DetailHyperLynx::UpperAsciiHyp(item.GetStr("P"));
                if (pRef.empty()) pRef = DetailHyperLynx::UpperAsciiHyp(item.GetStr("PADSTACK"));
                auto itPs = padstacks.find(pRef);
                if (itPs != padstacks.end()) ps = itPs->second;

                EdaLayerId padLayer = ps.multiLayer ? EdaLayerId::PadsThrough : EdaLayerId::TopCopper;
                if (item.HasKey("L")) {
                    padLayer = ps.multiLayer ? EdaLayerId::PadsThrough : mapHypLayer(item.GetStr("L"));
                }

                if (!refPart.empty()) {
                    auto itD = devIndexByRef.find(DetailHyperLynx::UpperAsciiHyp(refPart));
                    if (itD != devIndexByRef.end()) {
                        auto& db = devList[itD->second];
                        ++db.pinCount;
                        db.minX = std::min(db.minX, px - ps.w * 0.5f);
                        db.maxX = std::max(db.maxX, px + ps.w * 0.5f);
                        db.minY = std::min(db.minY, py - ps.h * 0.5f);
                        db.maxY = std::max(db.maxY, py + ps.h * 0.5f);
                        if (!ps.multiLayer && !item.HasKey("L") && !db.layerName.empty()) {
                            padLayer = mapHypLayer(db.layerName);
                        }
                    }
                }

                if (out.pads.size() < 60000u) {
                    EdaPadPrim pad;
                    pad.x = px;
                    pad.y = py;
                    pad.w = ps.w;
                    pad.h = ps.h;
                    pad.rotDeg = rot;
                    pad.drill = ps.drill;
                    pad.shape = ps.shape;
                    pad.layer = padLayer;
                    pad.pinName = PathUtils::Utf8ToWide(pinNm);
                    pad.netName = netNameW;
                    out.pads.push_back(std::move(pad));
                }
            } else if (item.headKey == "POLYGON" || item.headKey == "POUR" || item.headKey == "COPPER") {
                std::string tStr = DetailHyperLynx::UpperAsciiHyp(item.GetStr("T"));
                if (tStr == "VOID" || tStr == "CUTOUT") continue;
                EdaLayerId lid = mapHypLayer(item.GetStr("L"));
                float w = std::clamp(DetailHyperLynx::ParseHypDimMm(item.GetStr("W"), unitToMm, 0.20f), 0.08f, 4.0f);
                std::vector<std::pair<float, float>> polyPts;
                for (const auto& v : item.children) {
                    if (v.HasKey("X") && v.HasKey("Y")) {
                        polyPts.push_back({
                            DetailHyperLynx::ParseHypDimMm(v.GetStr("X"), unitToMm),
                            DetailHyperLynx::ParseHypDimMm(v.GetStr("Y"), unitToMm)
                        });
                    } else if (v.HasKey("X1") && v.HasKey("Y1") && v.HasKey("X2") && v.HasKey("Y2")) {
                        float vx1 = DetailHyperLynx::ParseHypDimMm(v.GetStr("X1"), unitToMm);
                        float vy1 = DetailHyperLynx::ParseHypDimMm(v.GetStr("Y1"), unitToMm);
                        float vx2 = DetailHyperLynx::ParseHypDimMm(v.GetStr("X2"), unitToMm);
                        float vy2 = DetailHyperLynx::ParseHypDimMm(v.GetStr("Y2"), unitToMm);
                        if (polyPts.empty()) polyPts.push_back({ vx1, vy1 });
                        polyPts.push_back({ vx2, vy2 });
                    }
                }
                if (polyPts.size() >= 3u) {
                    for (size_t k = 0; k < polyPts.size(); ++k) {
                        const auto& a = polyPts[k];
                        const auto& b = polyPts[(k + 1u) % polyPts.size()];
                        out.traces.push_back({ a.first, a.second, b.first, b.second, w, lid, netId });
                    }
                    DetailHyperLynx::HatchCopperPolygon(polyPts, lid, netId, out);
                }
            }
        }
    }

    // Emit placed PCB components
    for (const auto& db : devList) {
        if (!db.hasXY && db.minX > db.maxX) continue;
        EdaComponent3D comp;
        comp.refDes = PathUtils::Utf8ToWide(db.ref);
        comp.value  = PathUtils::Utf8ToWide(db.val);
        comp.footprint = PathUtils::Utf8ToWide(db.pkg.empty() ? "HYP_PKG" : db.pkg);
        comp.x = db.hasXY ? db.x : ((db.minX + db.maxX) * 0.5f);
        comp.y = db.hasXY ? db.y : ((db.minY + db.maxY) * 0.5f);
        comp.rotDeg = db.rot;
        comp.bottomSide = (mapHypLayer(db.layerName) == EdaLayerId::BottomCopper);
        comp.pinCount = std::max(2, db.pinCount);
        if (db.minX <= db.maxX && db.minY <= db.maxY) {
            comp.widthMm  = std::clamp(db.maxX - db.minX, 1.2f, 60.0f);
            comp.lengthMm = std::clamp(db.maxY - db.minY, 1.0f, 60.0f);
        }
        comp.category = ClassifyComponent(comp.refDes, comp.footprint, comp.widthMm, comp.lengthMm, comp.heightMm, comp.pinCount);

        EdaLayerId silkLayer = comp.bottomSide ? EdaLayerId::BottomSilk : EdaLayerId::TopSilk;
        DetailHyperLynx::DrawBoxRect(out,
            comp.x - comp.widthMm * 0.5f, comp.y - comp.lengthMm * 0.5f,
            comp.x + comp.widthMm * 0.5f, comp.y + comp.lengthMm * 0.5f,
            0.16f, silkLayer, 0);
        out.labels.push_back({ comp.x - comp.widthMm * 0.4f, comp.y + comp.lengthMm * 0.5f + 0.6f,
                               0.95f, silkLayer, comp.refDes });
        out.components.push_back(std::move(comp));
    }

    if (out.traces.empty() && out.pads.empty() && out.vias.empty() && out.components.empty()) {
        return false;
    }

    out.kind = EdaDocKind::KiCadPcb;
    out.netCount = std::max<size_t>(1u, netCounter > 0u ? netCounter : out.components.size());
    out.formatTitle = L"HyperLynx High-Speed PCB (.hyp — " +
                      std::to_wstring(std::max<size_t>(2u, copperStack.size())) + L" layers, " +
                      std::to_wstring(out.components.size()) + L" parts, " +
                      std::to_wstring(netCounter) + L" nets, " +
                      PathUtils::Utf8ToWide(unitLabel) + L")";
    return true;
}

} // namespace Formats
