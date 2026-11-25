#pragma once
// IcLvsCdlNetlist.h — Cadence / Mentor Calibre IC LVS Circuit Description Language (.cdl) Reader
// Parses `.SUBCKT <name> <pins...> ... .ENDS` hierarchies, `+` continuation lines, and CMOS `M*`
// (PMOS/NMOS), `Q*` (BJT), `D*` (Diode), `R*`, `C*`, and `X*` subcircuit instances, rendering true
// transistor/passive schematic symbols, `.SUBCKT` pin ports, and routed Manhattan net channels!
// NOTE: Included inside `namespace Explorer2`. Do NOT add `#include` directives here.

namespace Formats {

namespace DetailIcCdl {

struct CdlDevice {
    char kind = 'M'; // M, Q, D, R, C, X
    bool isPmos = false;
    std::string instName;
    std::string modelName;
    std::vector<std::string> nets;
    std::string paramSummary;
};

struct CdlSubckt {
    std::string name;
    std::vector<std::string> ports;
    std::vector<CdlDevice> devices;
};

inline bool IsPowerNet(std::string_view net) {
    return ReaderText::AsciiEqCI(net, "VDD") || ReaderText::AsciiEqCI(net, "VCC") ||
           ReaderText::AsciiEqCI(net, "VPWR") || ReaderText::AsciiEqCI(net, "AVDD");
}

inline bool IsGroundNet(std::string_view net) {
    return net == "0" || ReaderText::AsciiEqCI(net, "VSS") || ReaderText::AsciiEqCI(net, "GND") ||
           ReaderText::AsciiEqCI(net, "VGND") || ReaderText::AsciiEqCI(net, "AGND");
}

inline bool IsPmosModel(std::string_view mdl) {
    return ReaderText::StartsWithCI(mdl, "P") || ContainsTextCI(mdl, "pfet") ||
           ContainsTextCI(mdl, "pmos") || ContainsTextCI(mdl, "pch");
}

} // namespace DetailIcCdl

inline bool ParseIcLvsCdlNetlist(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    using namespace DetailIcCdl;

    std::string_view sv;
    if (!FormatPrologue::PrepareUtf8TextNoNul(data, size, sv, 16u, (32u << 20))) return false;

    // Join `+` SPICE/CDL continuation lines and strip `*` / `$` comments
    std::vector<std::string> logicalLines;
    {
        size_t pos = 0u;
        std::string_view rawLine;
        while (ReaderText::NextLineSv(sv, pos, rawLine)) {
            std::string_view ln = ReaderText::TrimSv(rawLine);
            if (ln.empty() || ln.front() == '*') continue;
            // Strip inline `$` CDL comment (e.g. `$SUB=VDD` or `$ comment`, keeping `$` parameter only if needed -> strip after ` $ `)
            const size_t dlr = ln.find(" $");
            if (dlr != std::string_view::npos) ln = ReaderText::TrimSv(ln.substr(0u, dlr));
            if (ln.empty()) continue;

            if (ln.front() == '+' && !logicalLines.empty()) {
                logicalLines.back().push_back(' ');
                logicalLines.back().append(ReaderText::TrimSv(ln.substr(1u)));
            } else {
                logicalLines.emplace_back(ln);
            }
        }
    }
    if (logicalLines.empty()) return false;

    std::vector<CdlSubckt> subckts;
    CdlSubckt topFree;
    topFree.name = "TOP_CDL";
    int openSubcktDepth = 0;

    for (const auto& lineStr : logicalLines) {
        std::vector<std::string_view> toks;
        std::string_view rem(lineStr), tk;
        while (ReaderText::NextTokenSv(rem, tk)) toks.push_back(tk);
        if (toks.empty()) continue;

        const std::string_view first = toks[0];
        if (first.front() == '.') {
            if (ReaderText::AsciiEqCI(first, ".SUBCKT")) {
                if (toks.size() < 2u) return false;
                CdlSubckt sc;
                sc.name = std::string(toks[1]);
                for (size_t i = 2u; i < toks.size(); ++i) {
                    if (toks[i].find('=') != std::string_view::npos || toks[i] == "/") continue;
                    sc.ports.emplace_back(toks[i]);
                }
                subckts.push_back(std::move(sc));
                ++openSubcktDepth;
            } else if (ReaderText::AsciiEqCI(first, ".ENDS")) {
                if (openSubcktDepth <= 0) return false;
                --openSubcktDepth;
            } else if (ReaderText::AsciiEqCI(first, ".PARAM") || ReaderText::AsciiEqCI(first, ".GLOBAL") ||
                       ReaderText::AsciiEqCI(first, ".INCLUDE") || ReaderText::AsciiEqCI(first, ".OPTION") ||
                       ReaderText::AsciiEqCI(first, ".CONNECT") || ReaderText::AsciiEqCI(first, ".MODEL") ||
                       ReaderText::AsciiEqCI(first, ".END")) {
                // Valid CDL directive
            } else {
                return false;
            }
            continue;
        }

        CdlSubckt& dst = (openSubcktDepth > 0 && !subckts.empty()) ? subckts.back() : topFree;
        const char lead = static_cast<char>(std::toupper(static_cast<unsigned char>(first.front())));
        if (lead != 'M' && lead != 'Q' && lead != 'D' && lead != 'R' && lead != 'C' && lead != 'X' && lead != 'L') {
            return false;
        }

        // Separate positional tokens from `key=val` sizing parameters (`W=1u L=0.18u`)
        std::vector<std::string_view> posToks;
        std::string params;
        for (size_t i = 1u; i < toks.size(); ++i) {
            if (toks[i] == "/") continue;
            if (toks[i].find('=') != std::string_view::npos) {
                if (params.size() < 36u) {
                    if (!params.empty()) params.push_back(' ');
                    params.append(toks[i]);
                }
            } else {
                posToks.push_back(toks[i]);
            }
        }

        CdlDevice dev;
        dev.kind = lead;
        dev.instName = std::string(first);
        dev.paramSummary = std::move(params);

        if (lead == 'M') { // Mname D G S B model [W=... L=...]
            if (posToks.size() < 5u) return false;
            for (size_t k = 0; k < 4u; ++k) dev.nets.emplace_back(posToks[k]);
            dev.modelName = std::string(posToks[4]);
            dev.isPmos = IsPmosModel(dev.modelName);
        } else if (lead == 'Q') { // Qname C B E [S] model
            if (posToks.size() < 4u) return false;
            for (size_t k = 0; k < 3u; ++k) dev.nets.emplace_back(posToks[k]);
            dev.modelName = std::string(posToks.back());
        } else if (lead == 'D' || lead == 'R' || lead == 'C' || lead == 'L') {
            if (posToks.size() < 2u) return false;
            dev.nets.emplace_back(posToks[0]);
            dev.nets.emplace_back(posToks[1]);
            if (posToks.size() >= 3u) dev.modelName = std::string(posToks[2]);
        } else if (lead == 'X') { // Xname net1 net2 ... subcktName
            if (posToks.size() < 2u) return false;
            dev.modelName = std::string(posToks.back());
            for (size_t k = 0; k + 1u < posToks.size(); ++k) dev.nets.emplace_back(posToks[k]);
        }

        if (dst.devices.size() < 512u) dst.devices.push_back(std::move(dev));
    }

    if (openSubcktDepth != 0) return false;

    // Pick the richest subcircuit (usually the last/top-level `.SUBCKT` in CDL, or `topFree`)
    const CdlSubckt* active = nullptr;
    if (!subckts.empty()) {
        active = &subckts.back();
        for (const auto& sc : subckts) {
            if (sc.devices.size() >= active->devices.size() && !sc.devices.empty()) active = &sc;
        }
    } else if (!topFree.devices.empty()) {
        active = &topFree;
    }
    if (!active || active->devices.empty()) return false;

    out = ParsedEdaBoard{};
    out.kind = EdaDocKind::KiCadSch;

    struct PinAnchor { float x; float y; };
    std::map<std::string, std::vector<PinAnchor>> netPins;
    std::map<std::string, int> netIds;
    int nextNetId = 1;
    auto regPin = [&](const std::string& net, float px, float py, std::string_view pinLbl) {
        if (net.empty()) return;
        int& nid = netIds[net];
        if (nid == 0) nid = nextNetId++;
        netPins[net].push_back({ px, py });
        out.pads.push_back({
            px, py, 0.60f, 0.60f, 0.0f, 0.0f, 0, EdaLayerId::SchSymbol,
            PathUtils::Utf8ToWide(pinLbl), PathUtils::Utf8ToWide(net)
        });
    };

    const size_t nDev = std::min<size_t>(active->devices.size(), 64u);
    const size_t cols = std::clamp<size_t>(static_cast<size_t>(std::ceil(std::sqrt(static_cast<double>(nDev)))), 2u, 8u);
    const float colPitch = 24.0f;
    const float rowPitch = 22.0f;
    const size_t nRows = (nDev + cols - 1u) / cols;

    // Place `.SUBCKT` boundary I/O ports on left/right margins (excluding VDD/VSS rails which get horizontal power bars)
    size_t ioLeftIdx = 0u, ioRightIdx = 0u;
    for (const auto& port : active->ports) {
        if (IsPowerNet(port) || IsGroundNet(port)) continue;
        const bool isOut = ReaderText::StartsWithCI(port, "Y") || ReaderText::StartsWithCI(port, "Z") ||
                           ReaderText::StartsWithCI(port, "Q") || ReaderText::StartsWithCI(port, "OUT");
        const float px = isOut ? (static_cast<float>(cols) * colPitch + 6.0f) : -14.0f;
        const float py = -static_cast<float>(isOut ? ioRightIdx++ : ioLeftIdx++) * 6.5f;

        // Draw schematic port chevron
        const float dir = isOut ? 1.0f : -1.0f;
        out.traces.push_back({ px, py, px + dir * 2.2f, py + 1.0f, 0.24f, EdaLayerId::SchSymbol, 0 });
        out.traces.push_back({ px + dir * 2.2f, py + 1.0f, px + dir * 4.5f, py + 1.0f, 0.24f, EdaLayerId::SchSymbol, 0 });
        out.traces.push_back({ px + dir * 4.5f, py + 1.0f, px + dir * 4.5f, py - 1.0f, 0.24f, EdaLayerId::SchSymbol, 0 });
        out.traces.push_back({ px + dir * 4.5f, py - 1.0f, px + dir * 2.2f, py - 1.0f, 0.24f, EdaLayerId::SchSymbol, 0 });
        out.traces.push_back({ px + dir * 2.2f, py - 1.0f, px, py, 0.24f, EdaLayerId::SchSymbol, 0 });
        out.labels.push_back({
            isOut ? (px + 1.2f) : (px - 4.2f), py - 0.35f, 0.85f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(port)
        });
        regPin(port, px, py, port);
    }

    // Draw CMOS / BJT / Passive / Subcircuit schematic symbols
    for (size_t di = 0; di < nDev; ++di) {
        const auto& d = active->devices[di];
        const float cx = static_cast<float>(di % cols) * colPitch;
        const float cy = -static_cast<float>(di / cols) * rowPitch;

        if (d.kind == 'M') {
            // 4-terminal MOSFET symbol: Gate on left (`cx-3.8, cy`), Drain/Source vertical (`cx+1.6, cy+-3.4`), Bulk on right (`cx+3.2, cy`)
            // Vertical oxide & channel bars
            out.traces.push_back({ cx - 1.2f, cy - 2.2f, cx - 1.2f, cy + 2.2f, 0.28f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ cx - 0.3f, cy - 2.6f, cx - 0.3f, cy + 2.6f, 0.26f, EdaLayerId::SchSymbol, 0 });
            // Gate lead (+ inversion bubble for PMOS!)
            if (d.isPmos) {
                out.circles.push_back({ cx - 1.75f, cy, 0.50f, 0.22f, false, EdaLayerId::SchSymbol });
                out.traces.push_back({ cx - 3.8f, cy, cx - 2.25f, cy, 0.22f, EdaLayerId::SchSymbol, 0 });
            } else {
                out.traces.push_back({ cx - 3.8f, cy, cx - 1.2f, cy, 0.22f, EdaLayerId::SchSymbol, 0 });
            }
            // Top & bottom channel fingers (`cx-0.3 -> cx+1.6 -> cy+-3.4`)
            out.traces.push_back({ cx - 0.3f, cy + 1.8f, cx + 1.6f, cy + 1.8f, 0.22f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ cx + 1.6f, cy + 1.8f, cx + 1.6f, cy + 3.4f, 0.22f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ cx - 0.3f, cy - 1.8f, cx + 1.6f, cy - 1.8f, 0.22f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ cx + 1.6f, cy - 1.8f, cx + 1.6f, cy - 3.4f, 0.22f, EdaLayerId::SchSymbol, 0 });
            // Bulk connection
            out.traces.push_back({ cx - 0.3f, cy, cx + 3.2f, cy, 0.18f, EdaLayerId::SchSymbol, 0 });

            // Put Source near VDD for PMOS (top) and near VSS for NMOS (bottom)
            const std::string& topNet = d.isPmos ? d.nets[2] : d.nets[0];
            const std::string& botNet = d.isPmos ? d.nets[0] : d.nets[2];
            regPin(topNet,    cx + 1.6f, cy + 3.4f, d.isPmos ? "S" : "D");
            regPin(d.nets[1], cx - 3.8f, cy,        "G");
            regPin(botNet,    cx + 1.6f, cy - 3.4f, d.isPmos ? "D" : "S");
            regPin(d.nets[3], cx + 3.2f, cy,        "B");
        } else if (d.kind == 'Q') {
            out.circles.push_back({ cx + 0.4f, cy, 2.8f, 0.20f, false, EdaLayerId::SchSymbol });
            out.traces.push_back({ cx - 0.8f, cy - 1.8f, cx - 0.8f, cy + 1.8f, 0.30f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ cx - 3.6f, cy, cx - 0.8f, cy, 0.22f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ cx - 0.8f, cy + 0.8f, cx + 1.6f, cy + 3.2f, 0.22f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ cx - 0.8f, cy - 0.8f, cx + 1.6f, cy - 3.2f, 0.22f, EdaLayerId::SchSymbol, 0 });
            regPin(d.nets[0], cx + 1.6f, cy + 3.2f, "C");
            regPin(d.nets[1], cx - 3.6f, cy,        "B");
            regPin(d.nets[2], cx + 1.6f, cy - 3.2f, "E");
        } else if (d.kind == 'R') {
            // Vertical zig-zag resistor
            out.traces.push_back({ cx, cy + 3.4f, cx, cy + 2.1f, 0.22f, EdaLayerId::SchSymbol, 0 });
            const float zy[6] = { 2.1f, 1.3f, 0.4f, -0.4f, -1.3f, -2.1f };
            const float zx[6] = { 0.0f, 1.1f, -1.1f, 1.1f, -1.1f, 0.0f };
            for (int k = 0; k < 5; ++k) {
                out.traces.push_back({ cx + zx[k], cy + zy[k], cx + zx[k + 1], cy + zy[k + 1], 0.24f, EdaLayerId::SchSymbol, 0 });
            }
            out.traces.push_back({ cx, cy - 2.1f, cx, cy - 3.4f, 0.22f, EdaLayerId::SchSymbol, 0 });
            regPin(d.nets[0], cx, cy + 3.4f, "1");
            regPin(d.nets[1], cx, cy - 3.4f, "2");
        } else if (d.kind == 'C') {
            // Parallel-plate capacitor
            out.traces.push_back({ cx, cy + 3.4f, cx, cy + 0.65f, 0.22f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ cx - 1.8f, cy + 0.65f, cx + 1.8f, cy + 0.65f, 0.30f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ cx - 1.8f, cy - 0.65f, cx + 1.8f, cy - 0.65f, 0.30f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ cx, cy - 0.65f, cx, cy - 3.4f, 0.22f, EdaLayerId::SchSymbol, 0 });
            regPin(d.nets[0], cx, cy + 3.4f, "1");
            regPin(d.nets[1], cx, cy - 3.4f, "2");
        } else if (d.kind == 'D') {
            // Diode triangle + cathode bar
            out.traces.push_back({ cx, cy + 3.2f, cx, cy + 1.2f, 0.22f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ cx - 1.4f, cy + 1.2f, cx + 1.4f, cy + 1.2f, 0.24f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ cx - 1.4f, cy + 1.2f, cx, cy - 1.2f, 0.24f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ cx + 1.4f, cy + 1.2f, cx, cy - 1.2f, 0.24f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ cx - 1.4f, cy - 1.2f, cx + 1.4f, cy - 1.2f, 0.28f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ cx, cy - 1.2f, cx, cy - 3.2f, 0.22f, EdaLayerId::SchSymbol, 0 });
            regPin(d.nets[0], cx, cy + 3.2f, "A");
            regPin(d.nets[1], cx, cy - 3.2f, "K");
        } else {
            // Hierarchical subcircuit `X*` block
            const size_t nPins = d.nets.size();
            const size_t half = (nPins + 1u) / 2u;
            const float bh = std::max(5.0f, static_cast<float>(half) * 1.9f + 1.8f);
            const float bw = 9.0f;
            DrawEdaBoxRect(out, cx - bw * 0.5f, cy - bh * 0.5f, cx + bw * 0.5f, cy + bh * 0.5f, 0.26f, EdaLayerId::SchSymbol, 0);
            for (size_t pi = 0; pi < nPins; ++pi) {
                const bool left = (pi < half);
                const size_t slot = left ? pi : (pi - half);
                const float py = cy + bh * 0.5f - 1.5f - static_cast<float>(slot) * 1.9f;
                const float bx = left ? (cx - bw * 0.5f) : (cx + bw * 0.5f);
                const float px = left ? (bx - 1.6f) : (bx + 1.6f);
                out.traces.push_back({ bx, py, px, py, 0.20f, EdaLayerId::SchSymbol, 0 });
                regPin(d.nets[pi], px, py, std::to_string(pi + 1u));
            }
        }

        std::wstring instLbl = PathUtils::Utf8ToWide(d.instName);
        if (!d.modelName.empty()) instLbl += L":" + PathUtils::Utf8ToWide(d.modelName);
        out.labels.push_back({ cx + 2.4f, cy + 2.2f, 0.72f, EdaLayerId::SchLabel, instLbl });

        EdaComponent3D comp;
        comp.refDes = PathUtils::Utf8ToWide(d.instName);
        comp.value = PathUtils::Utf8ToWide(d.modelName + (d.paramSummary.empty() ? "" : (" " + d.paramSummary)));
        comp.footprint = (d.kind == 'M') ? (d.isPmos ? L"CMOS_PMOS" : L"CMOS_NMOS") : L"CDL_CELL";
        comp.x = cx; comp.y = cy;
        comp.widthMm = 7.6f; comp.lengthMm = 6.8f; comp.heightMm = 0.8f;
        comp.pinCount = static_cast<int>(d.nets.size());
        comp.category = ClassifyComponent(comp.refDes, comp.footprint, comp.widthMm, comp.lengthMm, comp.heightMm, comp.pinCount);
        out.components.push_back(std::move(comp));
    }

    // Route Manhattan VDD rail, VSS/GND rail, and inter-row signal routing channels!
    const float vddRailY = 9.5f;
    const float vssRailY = -static_cast<float>(nRows - 1u) * rowPitch - 9.5f;
    const float railLeftX = -16.0f;
    const float railRightX = static_cast<float>(cols) * colPitch + 8.0f;

    int sigChannelIdx = 0;
    for (const auto& [netName, pts] : netPins) {
        const int nid = netIds[netName];
        if (IsPowerNet(netName)) {
            out.traces.push_back({ railLeftX, vddRailY, railRightX, vddRailY, 0.40f, EdaLayerId::TopCopper, nid });
            out.labels.push_back({ railLeftX, vddRailY + 0.8f, 0.90f, EdaLayerId::TopCopper, PathUtils::Utf8ToWide(netName) });
            for (const auto& p : pts) {
                out.traces.push_back({ p.x, p.y, p.x, vddRailY, 0.20f, EdaLayerId::TopCopper, nid });
                out.circles.push_back({ p.x, vddRailY, 0.36f, 0.18f, true, EdaLayerId::TopCopper });
            }
            continue;
        }
        if (IsGroundNet(netName)) {
            out.traces.push_back({ railLeftX, vssRailY, railRightX, vssRailY, 0.40f, EdaLayerId::BottomCopper, nid });
            out.labels.push_back({ railLeftX, vssRailY - 1.8f, 0.90f, EdaLayerId::BottomCopper, PathUtils::Utf8ToWide(netName) });
            for (const auto& p : pts) {
                out.traces.push_back({ p.x, p.y, p.x, vssRailY, 0.20f, EdaLayerId::BottomCopper, nid });
                out.circles.push_back({ p.x, vssRailY, 0.36f, 0.18f, true, EdaLayerId::BottomCopper });
            }
            continue;
        }
        if (pts.size() < 2u) continue;

        // Place horizontal trunk in inter-row channel staggered by `sigChannelIdx`
        float avgY = 0.0f;
        float minX = pts[0].x, maxX = pts[0].x;
        for (const auto& p : pts) {
            avgY += p.y;
            minX = std::min(minX, p.x);
            maxX = std::max(maxX, p.x);
        }
        avgY /= static_cast<float>(pts.size());
        const float chanY = avgY + 5.6f + static_cast<float>((sigChannelIdx % 7) - 3) * 0.72f;
        ++sigChannelIdx;

        out.traces.push_back({ minX, chanY, maxX, chanY, 0.22f, EdaLayerId::SchWire, nid });
        for (const auto& p : pts) {
            out.traces.push_back({ p.x, p.y, p.x, chanY, 0.20f, EdaLayerId::SchWire, nid });
            if (pts.size() >= 3u) {
                out.circles.push_back({ p.x, chanY, 0.34f, 0.18f, true, EdaLayerId::SchWire });
            }
        }
        if (sigChannelIdx <= 24) {
            out.labels.push_back({ (minX + maxX) * 0.5f, chanY + 0.35f, 0.64f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(netName) });
        }
    }

    wchar_t titleBuf[240] = {};
    swprintf_s(titleBuf, L"IC LVS CDL Netlist (.cdl — %hs, %zu devices, %zu nets, %zu subckts)",
               active->name.c_str(), active->devices.size(), netIds.size(), std::max<size_t>(1u, subckts.size()));
    out.labels.push_back({ railLeftX, vddRailY + 3.2f, 1.20f, EdaLayerId::TopSilk, titleBuf });
    out.formatTitle = titleBuf;
    out.netCount = std::max<size_t>(1u, netIds.size());
    FinalizeBoardBounds(out);
    out.valid = true;
    return !out.traces.empty();
}

} // namespace Formats
