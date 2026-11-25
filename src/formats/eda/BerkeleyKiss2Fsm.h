#pragma once
// BerkeleyKiss2Fsm.h — UC Berkeley SIS / Espresso / ABC Sequential FSM (.kiss2)
// Parses .i, .o, .p, .s, .r, .e/.end headers and 4-column symbolic transition cubes:
//   <input_cube> <from_state> <to_state> <output_cube>
// Renders a circular State Transition Diagram with double-ring reset state, bowed
// bidirectional transition arcs, radial self-loops, arrowheads, and I/O guard labels.

namespace Formats {

namespace DetailKiss2 {

inline bool IsValidLogicCube(std::string_view cube, int expectedWidth) {
    if (expectedWidth <= 0 || static_cast<int>(cube.size()) != expectedWidth) return false;
    for (char c : cube) {
        if (c != '0' && c != '1' && c != '-' && c != '~') return false;
    }
    return true;
}

struct KissEdge {
    size_t fromIdx = 0;
    size_t toIdx   = 0;
    std::string guardLabel;
    size_t count   = 0;
};

} // namespace DetailKiss2

inline bool ParseBerkeleyKiss2Fsm(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    std::string_view sv;
    if (!FormatPrologue::PrepareUtf8TextNoNul(data, size, sv, 12u, (16u << 20))) return false;

    int numInputs  = -1;
    int numOutputs = -1;
    int declProds  = -1;
    int declStates = -1;
    std::string resetState;

    std::vector<std::string> stateNames;
    std::unordered_map<std::string, size_t> stateMap;
    auto internState = [&](std::string_view nm) -> size_t {
        std::string key(nm);
        auto it = stateMap.find(key);
        if (it != stateMap.end()) return it->second;
        size_t idx = stateNames.size();
        stateNames.push_back(key);
        stateMap[std::move(key)] = idx;
        return idx;
    };

    struct RawTrans {
        std::string inCube;
        std::string fromSt;
        std::string toSt;
        std::string outCube;
    };
    std::vector<RawTrans> rawTransitions;

    size_t linePos = 0;
    std::string_view rawLine;
    while (ReaderText::NextLineSv(sv, linePos, rawLine)) {
        // Strip '#' comments
        size_t hashPos = rawLine.find('#');
        if (hashPos != std::string_view::npos) {
            rawLine = rawLine.substr(0, hashPos);
        }
        std::string_view line = ReaderText::TrimSv(rawLine);
        if (line.empty()) continue;

        if (line.front() == '.') {
            std::string_view dir, arg, extra;
            if (!ReaderText::NextTokenSv(line, dir)) continue;
            if (dir == ".e" || dir == ".end") break;
            if (dir == ".i") {
                if (!ReaderText::NextTokenSv(line, arg) || !ReaderText::ParseIntSv(arg, numInputs) ||
                    numInputs < 1 || numInputs > 256 || ReaderText::NextTokenSv(line, extra)) {
                    return false;
                }
            } else if (dir == ".o") {
                if (!ReaderText::NextTokenSv(line, arg) || !ReaderText::ParseIntSv(arg, numOutputs) ||
                    numOutputs < 1 || numOutputs > 256 || ReaderText::NextTokenSv(line, extra)) {
                    return false;
                }
            } else if (dir == ".p") {
                if (!ReaderText::NextTokenSv(line, arg) || !ReaderText::ParseIntSv(arg, declProds) || declProds < 0) {
                    return false;
                }
            } else if (dir == ".s") {
                if (!ReaderText::NextTokenSv(line, arg) || !ReaderText::ParseIntSv(arg, declStates) || declStates < 1) {
                    return false;
                }
            } else if (dir == ".r") {
                if (!ReaderText::NextTokenSv(line, arg) || arg.empty()) return false;
                resetState.assign(arg.data(), arg.size());
            } else {
                // Unknown dot directive in KISS2
                return false;
            }
            continue;
        }

        // Transition line MUST have 4 tokens: <in_cube> <from_state> <to_state> <out_cube>
        std::string_view tIn, tFrom, tTo, tOut, tExtra;
        if (!ReaderText::NextTokenSv(line, tIn) ||
            !ReaderText::NextTokenSv(line, tFrom) ||
            !ReaderText::NextTokenSv(line, tTo) ||
            !ReaderText::NextTokenSv(line, tOut) ||
            ReaderText::NextTokenSv(line, tExtra)) {
            return false;
        }
        if (numInputs <= 0 || numOutputs <= 0) return false;
        if (!DetailKiss2::IsValidLogicCube(tIn, numInputs) ||
            !DetailKiss2::IsValidLogicCube(tOut, numOutputs)) {
            return false;
        }
        if (tFrom != "*") internState(tFrom);
        if (tTo != "*")   internState(tTo);

        if (rawTransitions.size() >= 65536) return false;
        rawTransitions.push_back({
            std::string(tIn), std::string(tFrom), std::string(tTo), std::string(tOut)
        });
    }

    if (numInputs <= 0 || numOutputs <= 0 || rawTransitions.empty()) return false;
    if (!resetState.empty()) internState(resetState);
    if (stateNames.empty() || stateNames.size() > 1024) return false;

    // Expand wildcard '*' current-state transitions (if any) and aggregate parallel edges (u -> v)
    std::map<std::pair<size_t, size_t>, DetailKiss2::KissEdge> edgeMap;
    auto recordEdge = [&](size_t u, size_t v, const std::string& inC, const std::string& outC) {
        auto& e = edgeMap[{ u, v }];
        e.fromIdx = u;
        e.toIdx   = v;
        if (e.count == 0) {
            e.guardLabel = inC + "/" + outC;
        } else if (e.count == 1) {
            e.guardLabel += ", ...";
        }
        ++e.count;
    };

    for (const auto& rt : rawTransitions) {
        if (rt.toSt == "*") continue;
        size_t v = stateMap[rt.toSt];
        if (rt.fromSt == "*") {
            for (size_t u = 0; u < stateNames.size(); ++u) {
                recordEdge(u, v, rt.inCube, rt.outCube);
            }
        } else {
            size_t u = stateMap[rt.fromSt];
            recordEdge(u, v, rt.inCube, rt.outCube);
        }
    }

    out = ParsedEdaBoard{};
    out.kind = EdaDocKind::KiCadSch;
    out.netCount = edgeMap.size();

    const size_t nStates = stateNames.size();
    const float nodeR = 4.5f;
    const float ringR = (nStates == 1) ? 0.0f : std::max(18.0f, static_cast<float>(nStates) * 5.8f);
    constexpr double kPi = 3.14159265358979323846;

    std::vector<std::pair<float, float>> nodePos(nStates);
    for (size_t i = 0; i < nStates; ++i) {
        // Start at top (90 deg) and step clockwise around FSM circle
        double ang = (kPi * 0.5) - (2.0 * kPi * static_cast<double>(i)) / static_cast<double>(nStates);
        float cx = (nStates == 1) ? 0.0f : static_cast<float>(ringR * std::cos(ang));
        float cy = (nStates == 1) ? 0.0f : static_cast<float>(ringR * std::sin(ang));
        nodePos[i] = { cx, cy };

        const bool isReset = (!resetState.empty() && stateNames[i] == resetState);
        out.circles.push_back({ cx, cy, nodeR, 0.28f, false, EdaLayerId::SchSymbol });
        if (isReset) {
            out.circles.push_back({ cx, cy, nodeR - 0.85f, 0.22f, false, EdaLayerId::TopCopper });
            // Incoming reset arrow along outward radial vector
            float rx = static_cast<float>(std::cos(ang));
            float ry = static_cast<float>(std::sin(ang));
            float tipX = cx + rx * nodeR;
            float tipY = cy + ry * nodeR;
            float srcX = cx + rx * (nodeR + 6.5f);
            float srcY = cy + ry * (nodeR + 6.5f);
            out.traces.push_back({ srcX, srcY, tipX, tipY, 0.28f, EdaLayerId::TopCopper, 0 });
            out.pads.push_back({ srcX, srcY, 0.8f, 0.8f, 0.0f, 0.0f, 0u, EdaLayerId::TopCopper, L"RST",
                                 PathUtils::Utf8ToWide(stateNames[i]) });
            out.labels.push_back({ srcX + rx * 1.2f, srcY + ry * 1.2f, 0.90f, EdaLayerId::TopCopper, L"RESET" });
        }

        std::wstring wState = PathUtils::Utf8ToWide(stateNames[i]);
        out.labels.push_back({ cx - static_cast<float>(wState.size()) * 0.35f, cy - 0.35f, 1.10f,
                               EdaLayerId::SchLabel, wState });

        EdaComponent3D stComp{};
        stComp.refDes = wState;
        stComp.value = isReset ? L"FSM_RESET_STATE" : L"FSM_STATE";
        stComp.footprint = L"KISS2_STATE";
        stComp.x = cx;
        stComp.y = cy;
        stComp.widthMm = nodeR * 2.0f;
        stComp.lengthMm = nodeR * 2.0f;
        stComp.category = EdaPartCategory::IC_Package;
        stComp.pinCount = numInputs + numOutputs;
        out.components.push_back(std::move(stComp));
    }

    // Draw directed transitions (radial self-loops + bowed inter-state arcs + arrowheads)
    int edgeNetId = 1;
    for (const auto& [uv, edge] : edgeMap) {
        const size_t u = uv.first;
        const size_t v = uv.second;
        const auto [ux, uy] = nodePos[u];
        const EdaLayerId lid = (edgeNetId % 2 == 1) ? EdaLayerId::SchWire : EdaLayerId::InnerCopper;

        if (u == v) {
            double ang = (nStates == 1) ? (kPi * 0.25) : std::atan2(uy, ux);
            float rx = static_cast<float>(std::cos(ang));
            float ry = static_cast<float>(std::sin(ang));
            float loopCx = ux + rx * (nodeR + 2.8f);
            float loopCy = uy + ry * (nodeR + 2.8f);
            out.circles.push_back({ loopCx, loopCy, 2.8f, 0.20f, false, lid });
            out.labels.push_back({ loopCx + rx * 3.2f - 1.5f, loopCy + ry * 3.2f, 0.80f, EdaLayerId::TopSilk,
                                   PathUtils::Utf8ToWide(edge.guardLabel) });
            ++edgeNetId;
            continue;
        }

        const auto [vx, vy] = nodePos[v];
        float dx = vx - ux, dy = vy - uy;
        float dist = std::hypot(dx, dy);
        if (dist < 1e-3f) continue;
        float tx = dx / dist, ty = dy / dist;
        // Right-hand normal so u->v and v->u bow cleanly to opposite sides!
        float nx = ty, ny = -tx;
        float bow = std::clamp(dist * 0.14f, 2.2f, 6.5f);

        float sx = ux + tx * nodeR + nx * 0.7f;
        float sy = uy + ty * nodeR + ny * 0.7f;
        float ex = vx - tx * nodeR + nx * 0.7f;
        float ey = vy - ty * nodeR + ny * 0.7f;
        float cx = (sx + ex) * 0.5f + nx * bow;
        float cy = (sy + ey) * 0.5f + ny * bow;

        // 4-segment quadratic Bezier arc
        float px = sx, py = sy;
        for (int s = 1; s <= 4; ++s) {
            float t = static_cast<float>(s) * 0.25f;
            float omt = 1.0f - t;
            float qx = omt * omt * sx + 2.0f * omt * t * cx + t * t * ex;
            float qy = omt * omt * sy + 2.0f * omt * t * cy + t * t * ey;
            out.traces.push_back({ px, py, qx, qy, 0.22f, lid, edgeNetId });
            px = qx; py = qy;
        }

        // Arrowhead barbs at (ex, ey)
        float adx = ex - cx, ady = ey - cy;
        float alen = std::max(1e-3f, std::hypot(adx, ady));
        adx /= alen; ady /= alen;
        float anx = -ady, any = adx;
        out.traces.push_back({ ex, ey, ex - adx * 1.4f + anx * 0.65f, ey - ady * 1.4f + any * 0.65f, 0.24f, lid, edgeNetId });
        out.traces.push_back({ ex, ey, ex - adx * 1.4f - anx * 0.65f, ey - ady * 1.4f - any * 0.65f, 0.24f, lid, edgeNetId });

        // Guard label near Bezier midpoint (t = 0.5 -> 0.25*S + 0.5*C + 0.25*E)
        float mx = 0.25f * sx + 0.5f * cx + 0.25f * ex;
        float my = 0.25f * sy + 0.5f * cy + 0.25f * ey;
        out.labels.push_back({ mx - 1.2f, my + 0.4f, 0.78f, EdaLayerId::TopSilk,
                               PathUtils::Utf8ToWide(edge.guardLabel) });
        ++edgeNetId;
    }

    wchar_t title[200] = {};
    swprintf_s(title, L"Berkeley KISS2 Sequential FSM (.kiss2, %zu states, %zu transitions, %d in / %d out)",
               nStates, rawTransitions.size(), numInputs, numOutputs);
    out.formatTitle = title;
    return true;
}

} // namespace Formats
