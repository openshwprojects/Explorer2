#pragma once
// SpiceNetlist.h — SPICE Electronic Circuit Netlist (.cir / .spice / .sp / .net) Schematic & Net Topology Parser

namespace Formats {

inline bool ParseSpiceNetlistBoard(std::string_view sv, ParsedEdaBoard& out) {
    if (sv.empty() || sv.find('\0') != std::string_view::npos) return false;

    // Reject XML / JSON / KiCad S-expression files disguised with .net / .sp extensions
    size_t firstNonWs = 0;
    while (firstNonWs < sv.size() && static_cast<unsigned char>(sv[firstNonWs]) <= 0x20u) ++firstNonWs;
    if (firstNonWs >= sv.size()) return false;
    if (sv[firstNonWs] == '<' || sv[firstNonWs] == '{' || sv[firstNonWs] == '(') return false;

    // Split into logical lines, joining '+' continuation lines and stripping inline ';' / '$' comments
    std::vector<std::string> rawLines;
    {
        size_t pos = 0;
        while (pos < sv.size() && rawLines.size() < 8000) {
            size_t eol = sv.find('\n', pos);
            if (eol == std::string_view::npos) eol = sv.size();
            std::string_view ln = sv.substr(pos, eol - pos);
            pos = eol + 1;
            while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
            while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ' || ln.back() == '\t')) ln.remove_suffix(1);
            if (ln.empty()) continue;

            // Strip inline ';' comment (outside quotes)
            bool inQ = false;
            for (size_t i = 0; i < ln.size(); ++i) {
                if (ln[i] == '\'' || ln[i] == '"') inQ = !inQ;
                else if (!inQ && ln[i] == ';') { ln = ln.substr(0, i); break; }
            }
            while (!ln.empty() && (ln.back() == ' ' || ln.back() == '\t')) ln.remove_suffix(1);
            if (ln.empty()) continue;

            if (ln[0] == '+' && !rawLines.empty() && rawLines.back()[0] != '*') {
                std::string_view cont = ln.substr(1);
                while (!cont.empty() && (cont.front() == ' ' || cont.front() == '\t')) cont.remove_prefix(1);
                if (!cont.empty()) {
                    rawLines.back().push_back(' ');
                    rawLines.back().append(cont.data(), cont.size());
                }
            } else {
                rawLines.emplace_back(ln);
            }
        }
    }
    if (rawLines.empty()) return false;

    auto tokenizeSpice = [](std::string_view line) -> std::vector<std::string> {
        std::vector<std::string> toks;
        size_t i = 0;
        while (i < line.size()) {
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == ',')) ++i;
            if (i >= line.size()) break;
            if (line[i] == '(' || line[i] == '{' || line[i] == '\'') {
                char closeCh = (line[i] == '(') ? ')' : ((line[i] == '{') ? '}' : '\'');
                size_t s = i++;
                while (i < line.size() && line[i] != closeCh) ++i;
                if (i < line.size()) ++i;
                toks.emplace_back(line.substr(s, i - s));
            } else {
                size_t s = i;
                while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != ',') {
                    if (line[i] == '(') {
                        while (i < line.size() && line[i] != ')') ++i;
                        if (i < line.size()) ++i;
                        break;
                    }
                    ++i;
                }
                toks.emplace_back(line.substr(s, i - s));
            }
        }
        return toks;
    };

    auto isRefIdentifier = [](const std::string& t, bool& hasDigitOrUnderscore) -> bool {
        hasDigitOrUnderscore = false;
        if (t.size() < 2 || t.size() > 48) return false;
        if (!std::isalpha(static_cast<unsigned char>(t[0]))) return false;
        for (size_t i = 1; i < t.size(); ++i) {
            unsigned char c = static_cast<unsigned char>(t[i]);
            if (std::isdigit(c) || c == '_') hasDigitOrUnderscore = true;
            else if (!std::isalnum(c)) return false;
        }
        return true;
    };

    auto isNodeToken = [](const std::string& t) -> bool {
        if (t.empty() || t.size() > 64) return false;
        for (char ch : t) {
            unsigned char c = static_cast<unsigned char>(ch);
            if (!(std::isalnum(c) || c == '_' || c == '.' || c == '-' || c == '+' || c == '$' || c == '#' || c == ':' || c == '[' || c == ']')) {
                return false;
            }
        }
        // Reject sentence punctuation trailing period
        if (t.back() == '.' || t.back() == ':') return false;
        return true;
    };

    struct SpiceElem {
        char type = 'R';
        std::string ref;
        std::vector<std::string> nodes;
        std::string value;
    };

    std::string titleCard;
    std::vector<std::string> directives;
    std::set<std::string> modelNames;
    bool hasSpiceDirective = false;
    bool hasStarComment = false;
    size_t designatorWithDigitCount = 0;

    // Pre-scan for directives (.end, .tran, .ac, .dc, .subckt, .model, ...)
    for (const auto& ln : rawLines) {
        if (ln[0] == '*') { hasStarComment = true; continue; }
        if (ln[0] == '.') {
            std::string low = PathUtils::ToLowerAscii(ln);
            auto toks = tokenizeSpice(low);
            if (!toks.empty()) {
                const std::string& cmd = toks[0];
                if (cmd == ".end" || cmd == ".ends" || cmd == ".subckt" || cmd == ".model" ||
                    cmd == ".tran" || cmd == ".ac" || cmd == ".dc" || cmd == ".op" ||
                    cmd == ".param" || cmd == ".include" || cmd == ".inc" || cmd == ".lib" ||
                    cmd == ".ic" || cmd == ".nodeset" || cmd == ".options" || cmd == ".temp" ||
                    cmd == ".print" || cmd == ".plot" || cmd == ".probe" || cmd == ".control")
                {
                    hasSpiceDirective = true;
                    if (cmd == ".tran" || cmd == ".ac" || cmd == ".dc" || cmd == ".op") {
                        directives.push_back(ln);
                    } else if (cmd == ".model" && toks.size() >= 2) {
                        modelNames.insert(toks[1]);
                    }
                }
            }
        }
    }

    auto tryParseElem = [&](const std::string& ln, SpiceElem& outEl, bool& outHasDigit) -> bool {
        auto toks = tokenizeSpice(ln);
        if (toks.size() < 3) return false;
        if (!isRefIdentifier(toks[0], outHasDigit)) return false;

        char c0 = static_cast<char>(std::toupper(static_cast<unsigned char>(toks[0][0])));
        outEl = SpiceElem{};
        outEl.type = c0;
        outEl.ref = toks[0];

        if (c0 == 'R' || c0 == 'C' || c0 == 'L' || c0 == 'D' || c0 == 'V' || c0 == 'I') {
            if (toks.size() < 3 || !isNodeToken(toks[1]) || !isNodeToken(toks[2])) return false;
            outEl.nodes = { toks[1], toks[2] };
            for (size_t k = 3; k < toks.size(); ++k) {
                if (!outEl.value.empty()) outEl.value.push_back(' ');
                outEl.value += toks[k];
            }
            // Passive R/C/L/D must specify a value or model
            if ((c0 == 'R' || c0 == 'C' || c0 == 'L' || c0 == 'D') && outEl.value.empty()) return false;
            return true;
        }
        if (c0 == 'F' || c0 == 'H') {
            if (toks.size() < 5 || !isNodeToken(toks[1]) || !isNodeToken(toks[2])) return false;
            outEl.nodes = { toks[1], toks[2] };
            outEl.value = toks[3] + " " + toks[4];
            return true;
        }
        if (c0 == 'Q' || c0 == 'J' || c0 == 'Z') {
            if (toks.size() < 5) return false;
            if (!isNodeToken(toks[1]) || !isNodeToken(toks[2]) || !isNodeToken(toks[3])) return false;
            outEl.nodes = { toks[1], toks[2], toks[3] }; // c b e  or  d g s
            outEl.value = toks[4];
            if (c0 == 'Q' && toks.size() >= 6 && modelNames.count(PathUtils::ToLowerAscii(toks[5]))) {
                outEl.value = toks[5]; // optional 4th substrate node present
            }
            return true;
        }
        if (c0 == 'M') {
            if (toks.size() < 6) return false;
            if (!isNodeToken(toks[1]) || !isNodeToken(toks[2]) || !isNodeToken(toks[3]) || !isNodeToken(toks[4])) return false;
            outEl.nodes = { toks[1], toks[2], toks[3], toks[4] }; // d g s b
            outEl.value = toks[5];
            return true;
        }
        if (c0 == 'E' || c0 == 'G' || c0 == 'S') {
            if (toks.size() < 4 || !isNodeToken(toks[1]) || !isNodeToken(toks[2])) return false;
            std::string up3 = PathUtils::ToLowerAscii(toks[3]);
            if (up3 == "value" || up3 == "vol" || up3 == "cur" || up3.find('=') != std::string::npos) {
                outEl.nodes = { toks[1], toks[2] };
                outEl.value = toks[3];
                return true;
            }
            if (toks.size() < 6 || !isNodeToken(toks[3]) || !isNodeToken(toks[4])) return false;
            outEl.nodes = { toks[1], toks[2], toks[3], toks[4] };
            outEl.value = toks[5];
            return true;
        }
        if (c0 == 'X') {
            // Subcircuit instance: X<name> <n1> ... <nK> <subcktName> [params: ...] [k=v ...]
            size_t endIdx = toks.size();
            for (size_t k = 1; k < toks.size(); ++k) {
                std::string lowK = PathUtils::ToLowerAscii(toks[k]);
                if (lowK == "params:" || toks[k].find('=') != std::string::npos) {
                    endIdx = k;
                    break;
                }
            }
            if (endIdx < 3) return false;
            outEl.value = toks[endIdx - 1]; // subckt name
            for (size_t k = 1; k + 1 < endIdx; ++k) {
                if (!isNodeToken(toks[k])) return false;
                outEl.nodes.push_back(toks[k]);
            }
            return !outEl.nodes.empty();
        }
        return false;
    };

    std::vector<SpiceElem> topElems;
    std::vector<SpiceElem> subcktElems;
    int subcktDepth = 0;

    for (size_t li = 0; li < rawLines.size(); ++li) {
        const std::string& ln = rawLines[li];
        if (li == 0 && ln[0] != '*' && ln[0] != '.') {
            SpiceElem testEl;
            bool hasDig = false;
            if (!tryParseElem(ln, testEl, hasDig) || !hasDig) {
                titleCard = ln;
                continue;
            }
        }
        if (ln[0] == '*') {
            if (titleCard.empty() && ln.size() > 1) {
                std::string_view tsv(ln.data() + 1, ln.size() - 1);
                while (!tsv.empty() && tsv.front() == ' ') tsv.remove_prefix(1);
                titleCard.assign(tsv.data(), tsv.size());
            }
            continue;
        }
        if (ln[0] == '.') {
            std::string low = PathUtils::ToLowerAscii(ln);
            if (low.rfind(".subckt", 0) == 0) ++subcktDepth;
            else if (low.rfind(".ends", 0) == 0 && subcktDepth > 0) --subcktDepth;
            else if (low == ".end" || low.rfind(".end ", 0) == 0) break;
            continue;
        }

        SpiceElem el;
        bool hasDig = false;
        if (tryParseElem(ln, el, hasDig)) {
            if (hasDig) ++designatorWithDigitCount;
            if (subcktDepth > 0) {
                if (subcktElems.size() < 256) subcktElems.push_back(std::move(el));
            } else {
                if (topElems.size() < 256) topElems.push_back(std::move(el));
            }
        }
    }

    std::vector<SpiceElem>& elems = !topElems.empty() ? topElems : subcktElems;
    if (elems.empty()) return false;

    // Guard against random English prose: require either a real SPICE directive or standard numbered designators
    if (!hasSpiceDirective && designatorWithDigitCount == 0) return false;
    if (!hasSpiceDirective && !hasStarComment && elems.size() == 1 && designatorWithDigitCount == 0) return false;

    auto isGndNet = [](const std::string& n) -> bool {
        std::string low = PathUtils::ToLowerAscii(n);
        return low == "0" || low == "gnd" || low == "vss" || low == "agnd" || low == "dgnd" || low == "sgnd";
    };

    // Build true net connectivity table across all element terminals
    std::unordered_map<std::string, int> netToId;
    std::vector<std::string> netNames;
    auto internNet = [&](const std::string& rawNet) -> int {
        std::string key = isGndNet(rawNet) ? "0" : PathUtils::ToLowerAscii(rawNet);
        auto it = netToId.find(key);
        if (it != netToId.end()) return it->second;
        int id = static_cast<int>(netNames.size() + 1);
        netToId[key] = id;
        netNames.push_back(isGndNet(rawNet) ? "0" : rawNet);
        return id;
    };

    for (const auto& el : elems) {
        for (const auto& nd : el.nodes) {
            internNet(nd);
        }
    }

    if (netNames.empty()) return false;

    out.kind = EdaDocKind::KiCadSch;
    out.netCount = netNames.size();
    std::wstring cleanTitle = titleCard.empty() ? L"SPICE Circuit Netlist" : PathUtils::Utf8ToWide(titleCard);
    if (cleanTitle.size() > 56) cleanTitle = cleanTitle.substr(0, 56) + L"...";
    out.formatTitle = cleanTitle + L" (" + std::to_wstring(elems.size()) + L" parts, " +
                      std::to_wstring(netNames.size()) + L" nets)";

    auto drawGndSymbol = [&](float gx, float gy, int gndNetId) {
        out.traces.push_back({ gx, gy, gx, gy - 2.0f, 0.24f, EdaLayerId::SchWire, gndNetId });
        out.traces.push_back({ gx - 1.6f, gy - 2.0f, gx + 1.6f, gy - 2.0f, 0.28f, EdaLayerId::SchSymbol, gndNetId });
        out.traces.push_back({ gx - 1.0f, gy - 2.6f, gx + 1.0f, gy - 2.6f, 0.26f, EdaLayerId::SchSymbol, gndNetId });
        out.traces.push_back({ gx - 0.4f, gy - 3.2f, gx + 0.4f, gy - 3.2f, 0.24f, EdaLayerId::SchSymbol, gndNetId });
    };

    struct PinAnchor {
        float x = 0.0f;
        float y = 0.0f;
        int netId = 0;
        bool isGnd = false;
    };
    std::unordered_map<int, std::vector<PinAnchor>> netPins;

    const size_t cols = std::min<size_t>(4u, std::max<size_t>(2u, static_cast<size_t>(std::ceil(std::sqrt(static_cast<double>(elems.size()))))));
    const float colPitch = 28.0f;
    // Allocate horizontal net routing channel clearance between rows
    const size_t nonGndNets = netNames.size();
    const float chanBandH = std::min(22.0f, std::max(8.0f, static_cast<float>(nonGndNets) * 1.6f + 4.0f));
    const float rowPitch = 18.0f + chanBandH;

    for (size_t idx = 0; idx < elems.size(); ++idx) {
        const auto& el = elems[idx];
        const size_t col = idx % cols;
        const size_t row = idx / cols;
        const float cx = static_cast<float>(col) * colPitch;
        const float cy = -static_cast<float>(row) * rowPitch;

        EdaComponent3D comp;
        comp.refDes = PathUtils::Utf8ToWide(el.ref);
        comp.value = PathUtils::Utf8ToWide(el.value);
        comp.footprint = L"SPICE_" + std::wstring(1, static_cast<wchar_t>(el.type));
        comp.x = cx;
        comp.y = cy;
        comp.widthMm = 8.0f;
        comp.lengthMm = 5.0f;
        comp.pinCount = static_cast<int>(el.nodes.size());

        std::vector<std::pair<float, float>> pinPts;

        if (el.nodes.size() == 2) {
            const float pL = cx - 6.5f;
            const float pR = cx + 6.5f;
            pinPts = { { pL, cy }, { pR, cy } };

            if (el.type == 'R') {
                // Zig-zag resistor symbol
                comp.category = EdaPartCategory::ResistorSMD;
                out.traces.push_back({ pL, cy, cx - 3.6f, cy, 0.24f, EdaLayerId::SchWire, internNet(el.nodes[0]) });
                out.traces.push_back({ cx + 3.6f, cy, pR, cy, 0.24f, EdaLayerId::SchWire, internNet(el.nodes[1]) });
                const float zx[8] = { -3.6f, -3.0f, -1.8f, -0.6f, 0.6f, 1.8f, 3.0f, 3.6f };
                const float zy[8] = {  0.0f,  1.3f, -1.3f,  1.3f, -1.3f, 1.3f, -1.3f, 0.0f };
                for (int k = 0; k < 7; ++k) {
                    out.traces.push_back({ cx + zx[k], cy + zy[k], cx + zx[k + 1], cy + zy[k + 1], 0.28f, EdaLayerId::SchSymbol, 0 });
                }
            } else if (el.type == 'C') {
                // Parallel-plate capacitor symbol
                comp.category = EdaPartCategory::CapacitorSMD;
                out.traces.push_back({ pL, cy, cx - 0.8f, cy, 0.24f, EdaLayerId::SchWire, internNet(el.nodes[0]) });
                out.traces.push_back({ cx + 0.8f, cy, pR, cy, 0.24f, EdaLayerId::SchWire, internNet(el.nodes[1]) });
                out.traces.push_back({ cx - 0.8f, cy - 2.2f, cx - 0.8f, cy + 2.2f, 0.34f, EdaLayerId::SchSymbol, 0 });
                out.traces.push_back({ cx + 0.8f, cy - 2.2f, cx + 0.8f, cy + 2.2f, 0.34f, EdaLayerId::SchSymbol, 0 });
            } else if (el.type == 'L') {
                // 4-hump inductor coil symbol
                comp.category = EdaPartCategory::Inductor;
                out.traces.push_back({ pL, cy, cx - 3.6f, cy, 0.24f, EdaLayerId::SchWire, internNet(el.nodes[0]) });
                out.traces.push_back({ cx + 3.6f, cy, pR, cy, 0.24f, EdaLayerId::SchWire, internNet(el.nodes[1]) });
                for (int h = 0; h < 4; ++h) {
                    float hx0 = cx - 3.6f + h * 1.8f;
                    float hxm = hx0 + 0.9f;
                    float hx1 = hx0 + 1.8f;
                    out.traces.push_back({ hx0, cy, hx0 + 0.35f, cy + 1.3f, 0.26f, EdaLayerId::SchSymbol, 0 });
                    out.traces.push_back({ hx0 + 0.35f, cy + 1.3f, hxm, cy + 1.6f, 0.26f, EdaLayerId::SchSymbol, 0 });
                    out.traces.push_back({ hxm, cy + 1.6f, hx1 - 0.35f, cy + 1.3f, 0.26f, EdaLayerId::SchSymbol, 0 });
                    out.traces.push_back({ hx1 - 0.35f, cy + 1.3f, hx1, cy, 0.26f, EdaLayerId::SchSymbol, 0 });
                }
            } else if (el.type == 'D') {
                // Diode triangle + cathode bar
                comp.category = EdaPartCategory::DiodeLED;
                out.traces.push_back({ pL, cy, cx - 1.6f, cy, 0.24f, EdaLayerId::SchWire, internNet(el.nodes[0]) });
                out.traces.push_back({ cx + 1.6f, cy, pR, cy, 0.24f, EdaLayerId::SchWire, internNet(el.nodes[1]) });
                out.traces.push_back({ cx - 1.6f, cy - 1.8f, cx - 1.6f, cy + 1.8f, 0.28f, EdaLayerId::SchSymbol, 0 });
                out.traces.push_back({ cx - 1.6f, cy + 1.8f, cx + 1.6f, cy, 0.28f, EdaLayerId::SchSymbol, 0 });
                out.traces.push_back({ cx - 1.6f, cy - 1.8f, cx + 1.6f, cy, 0.28f, EdaLayerId::SchSymbol, 0 });
                out.traces.push_back({ cx + 1.6f, cy - 1.8f, cx + 1.6f, cy + 1.8f, 0.32f, EdaLayerId::SchSymbol, 0 });
            } else {
                // Voltage / Current source circle (V, I, behavioral E/G/F/H)
                out.traces.push_back({ pL, cy, cx - 2.6f, cy, 0.24f, EdaLayerId::SchWire, internNet(el.nodes[0]) });
                out.traces.push_back({ cx + 2.6f, cy, pR, cy, 0.24f, EdaLayerId::SchWire, internNet(el.nodes[1]) });
                out.circles.push_back({ cx, cy, 2.6f, 0.28f, false, EdaLayerId::SchSymbol });
                if (el.type == 'I' || el.type == 'F' || el.type == 'G') {
                    out.traces.push_back({ cx - 1.4f, cy, cx + 1.4f, cy, 0.24f, EdaLayerId::SchSymbol, 0 });
                    out.traces.push_back({ cx + 0.5f, cy + 0.7f, cx + 1.4f, cy, 0.24f, EdaLayerId::SchSymbol, 0 });
                    out.traces.push_back({ cx + 0.5f, cy - 0.7f, cx + 1.4f, cy, 0.24f, EdaLayerId::SchSymbol, 0 });
                } else {
                    // '+' on n+ (left) and '-' on n- (right)
                    out.traces.push_back({ cx - 1.7f, cy, cx - 0.7f, cy, 0.22f, EdaLayerId::SchSymbol, 0 });
                    out.traces.push_back({ cx - 1.2f, cy - 0.5f, cx - 1.2f, cy + 0.5f, 0.22f, EdaLayerId::SchSymbol, 0 });
                    out.traces.push_back({ cx + 0.8f, cy, cx + 1.7f, cy, 0.22f, EdaLayerId::SchSymbol, 0 });
                }
            }
        } else if (el.nodes.size() == 3 && (el.type == 'Q' || el.type == 'J' || el.type == 'Z')) {
            // 3-terminal Transistor symbol (Collector/Drain top-right, Base/Gate left, Emitter/Source bottom-right)
            comp.category = EdaPartCategory::TransistorSOT;
            pinPts = { { cx + 4.0f, cy + 4.2f }, { cx - 5.5f, cy }, { cx + 4.0f, cy - 4.2f } };
            out.circles.push_back({ cx, cy, 3.0f, 0.24f, false, EdaLayerId::SchSymbol });
            out.traces.push_back({ cx - 5.5f, cy, cx - 1.2f, cy, 0.24f, EdaLayerId::SchWire, internNet(el.nodes[1]) });
            out.traces.push_back({ cx - 1.2f, cy - 1.9f, cx - 1.2f, cy + 1.9f, 0.34f, EdaLayerId::SchSymbol, 0 });
            // Collector / Drain lead
            out.traces.push_back({ cx - 1.2f, cy + 0.9f, cx + 1.8f, cy + 2.5f, 0.26f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ cx + 1.8f, cy + 2.5f, cx + 4.0f, cy + 4.2f, 0.24f, EdaLayerId::SchWire, internNet(el.nodes[0]) });
            // Emitter / Source lead + arrow
            out.traces.push_back({ cx - 1.2f, cy - 0.9f, cx + 1.8f, cy - 2.5f, 0.26f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ cx + 1.8f, cy - 2.5f, cx + 4.0f, cy - 4.2f, 0.24f, EdaLayerId::SchWire, internNet(el.nodes[2]) });
            out.traces.push_back({ cx + 0.9f, cy - 2.5f, cx + 1.8f, cy - 2.5f, 0.24f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ cx + 1.5f, cy - 1.6f, cx + 1.8f, cy - 2.5f, 0.24f, EdaLayerId::SchSymbol, 0 });
        } else if (el.nodes.size() == 4 && el.type == 'M') {
            // 4-terminal MOSFET symbol (D top-right, G left, S bottom-right, B mid-right)
            comp.category = EdaPartCategory::TransistorSOT;
            pinPts = {
                { cx + 4.5f, cy + 4.2f }, // D
                { cx - 5.5f, cy },        // G
                { cx + 4.5f, cy - 4.2f }, // S
                { cx + 5.5f, cy }         // B
            };
            out.circles.push_back({ cx, cy, 3.2f, 0.22f, false, EdaLayerId::SchSymbol });
            out.traces.push_back({ cx - 5.5f, cy, cx - 1.6f, cy, 0.24f, EdaLayerId::SchWire, internNet(el.nodes[1]) });
            out.traces.push_back({ cx - 1.6f, cy - 2.0f, cx - 1.6f, cy + 2.0f, 0.30f, EdaLayerId::SchSymbol, 0 }); // Gate oxide
            out.traces.push_back({ cx - 0.7f, cy - 2.2f, cx - 0.7f, cy + 2.2f, 0.28f, EdaLayerId::SchSymbol, 0 }); // Channel
            out.traces.push_back({ cx - 0.7f, cy + 1.6f, cx + 4.5f, cy + 1.6f, 0.24f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ cx + 4.5f, cy + 1.6f, cx + 4.5f, cy + 4.2f, 0.24f, EdaLayerId::SchWire, internNet(el.nodes[0]) });
            out.traces.push_back({ cx - 0.7f, cy - 1.6f, cx + 4.5f, cy - 1.6f, 0.24f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ cx + 4.5f, cy - 1.6f, cx + 4.5f, cy - 4.2f, 0.24f, EdaLayerId::SchWire, internNet(el.nodes[2]) });
            out.traces.push_back({ cx - 0.7f, cy, cx + 5.5f, cy, 0.24f, EdaLayerId::SchWire, internNet(el.nodes[3]) });
        } else {
            // N-terminal Subcircuit IC / Controlled-Source Block
            comp.category = EdaPartCategory::IC_Package;
            const size_t nPins = el.nodes.size();
            const size_t nLeft = (nPins + 1u) / 2u;
            const size_t nRight = nPins - nLeft;
            const float boxW = 8.4f;
            const float boxH = std::max(5.0f, static_cast<float>(std::max(nLeft, nRight)) * 2.4f + 1.6f);
            comp.widthMm = boxW;
            comp.lengthMm = boxH;
            const float x0 = cx - boxW * 0.5f, x1 = cx + boxW * 0.5f;
            const float y0 = cy - boxH * 0.5f, y1 = cy + boxH * 0.5f;
            out.traces.push_back({ x0, y0, x1, y0, 0.30f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ x1, y0, x1, y1, 0.30f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ x1, y1, x0, y1, 0.30f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ x0, y1, x0, y0, 0.30f, EdaLayerId::SchSymbol, 0 });

            for (size_t pi = 0; pi < nPins; ++pi) {
                bool onLeft = (pi < nLeft);
                size_t sideIdx = onLeft ? pi : (pi - nLeft);
                size_t sideCnt = onLeft ? nLeft : nRight;
                float py = y1 - ((boxH * static_cast<float>(sideIdx + 1u)) / static_cast<float>(sideCnt + 1u));
                float pxEdge = onLeft ? x0 : x1;
                float pxTip  = onLeft ? (x0 - 2.6f) : (x1 + 2.6f);
                out.traces.push_back({ pxEdge, py, pxTip, py, 0.24f, EdaLayerId::SchWire, internNet(el.nodes[pi]) });
                pinPts.push_back({ pxTip, py });
            }
        }

        // Emit pin pads + ground symbols / net routing anchors
        for (size_t pi = 0; pi < pinPts.size() && pi < el.nodes.size(); ++pi) {
            float px = pinPts[pi].first;
            float py = pinPts[pi].second;
            int nid = internNet(el.nodes[pi]);
            bool gnd = isGndNet(el.nodes[pi]);

            EdaPadPrim pad;
            pad.x = px;
            pad.y = py;
            pad.w = 0.55f;
            pad.h = 0.55f;
            pad.shape = 0;
            pad.layer = EdaLayerId::SchSymbol;
            pad.pinName = std::to_wstring(pi + 1);
            pad.netName = PathUtils::Utf8ToWide(gnd ? "0" : el.nodes[pi]);
            out.pads.push_back(std::move(pad));

            if (gnd) {
                drawGndSymbol(px, py, nid);
            } else {
                netPins[nid].push_back({ px, py, nid, false });
                // Label single-ended or primary node pins directly too
                if (out.labels.size() < 1500) {
                    out.labels.push_back({ px - 1.2f, py + 1.1f, 0.8f, EdaLayerId::SchWire, PathUtils::Utf8ToWide(el.nodes[pi]) });
                }
            }
        }

        std::wstring valDisp = comp.value;
        if (valDisp.size() > 20) valDisp = valDisp.substr(0, 20);
        std::wstring compLbl = comp.refDes + (valDisp.empty() ? L"" : (L"=" + valDisp));
        out.labels.push_back({ cx - 3.2f, cy + comp.lengthMm * 0.5f + 1.8f, 0.95f, EdaLayerId::SchLabel, compLbl });
        out.components.push_back(std::move(comp));
    }

    // Route orthogonal SchWire net connections across all multi-pin non-ground nets
    size_t chanIdx = 0;
    for (size_t ni = 0; ni < netNames.size(); ++ni) {
        int nid = static_cast<int>(ni + 1);
        auto it = netPins.find(nid);
        if (it == netPins.end() || it->second.size() < 2) continue;

        auto& anchors = it->second;
        std::sort(anchors.begin(), anchors.end(), [](const PinAnchor& a, const PinAnchor& b) {
            return (a.x != b.x) ? (a.x < b.x) : (a.y < b.y);
        });

        // Place horizontal net bus channel cleanly above/between component rows
        float avgY = 0.0f;
        for (const auto& a : anchors) avgY += a.y;
        avgY /= static_cast<float>(anchors.size());
        float laneOffset = 6.2f + static_cast<float>(chanIdx % 8u) * 1.45f;
        float busY = avgY + ((chanIdx & 1u) ? -laneOffset : laneOffset);
        ++chanIdx;

        float minBusX = anchors.front().x;
        float maxBusX = anchors.back().x;
        if (maxBusX > minBusX + 0.05f) {
            out.traces.push_back({ minBusX, busY, maxBusX, busY, 0.22f, EdaLayerId::SchWire, nid });
        }
        for (size_t ai = 0; ai < anchors.size(); ++ai) {
            out.traces.push_back({ anchors[ai].x, anchors[ai].y, anchors[ai].x, busY, 0.22f, EdaLayerId::SchWire, nid });
            if (anchors.size() >= 3 && ai > 0 && ai + 1 < anchors.size()) {
                out.circles.push_back({ anchors[ai].x, busY, 0.38f, 0.20f, true, EdaLayerId::SchWire });
            }
        }
        if (out.labels.size() < 1600) {
            out.labels.push_back({ minBusX + 0.8f, busY + 0.7f, 0.85f, EdaLayerId::SchWire, PathUtils::Utf8ToWide(netNames[ni]) });
        }
    }

    if (!directives.empty() && out.labels.size() < 1600) {
        out.labels.push_back({ 0.0f, 14.5f, 1.0f, EdaLayerId::SchLabel, L"Analysis: " + PathUtils::Utf8ToWide(directives.front()) });
    }

    return !out.components.empty();
}

} // namespace Formats
