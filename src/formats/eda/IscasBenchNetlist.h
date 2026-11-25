#pragma once
// IscasBenchNetlist.h — ISCAS-85 / ISCAS-89 / EPFL Gate-Level Benchmark Netlist (.bench) reader
// Parses INPUT(x), OUTPUT(y), and `z = GATE(a, b, ...)` (AND, NAND, OR, NOR, XOR, XNOR, NOT,
// BUFF/BUF, DFF, MUX), laying out and routing the schematic via DetailGateAndDaqEda::RenderGateDagToBoard.

namespace Formats {

inline bool ParseIscasBenchNetlist(
    const uint8_t* data,
    size_t size,
    ParsedEdaBoard& out)
{
    if (!data || size < 12 || size > 64u * 1024u * 1024u) return false;
    std::string_view sv = ReaderText::StripUtf8Bom(std::string_view(reinterpret_cast<const char*>(data), size));
    if (sv.find('\0') != std::string_view::npos) return false;

    std::vector<DetailGateAndDaqEda::GateNode> nodes;
    size_t nIn = 0, nOut = 0, nGates = 0;

    auto splitArgs = [](std::string_view s, std::vector<std::string>& outArgs) {
        outArgs.clear();
        size_t p = 0;
        while (p < s.size()) {
            const size_t comma = s.find(',', p);
            std::string_view item = (comma == std::string_view::npos) ? s.substr(p) : s.substr(p, comma - p);
            p = (comma == std::string_view::npos) ? s.size() : (comma + 1u);
            item = ReaderText::TrimSv(item);
            if (!item.empty()) outArgs.emplace_back(item);
        }
    };

    size_t pos = 0;
    std::string_view line;
    while (ReaderText::NextLineSv(sv, pos, line)) {
        const size_t hp = line.find('#');
        if (hp != std::string_view::npos) line = line.substr(0, hp);
        line = ReaderText::TrimSv(line);
        if (line.empty()) continue;

        const size_t eq = line.find('=');
        if (eq == std::string_view::npos) {
            // Must be INPUT(net) or OUTPUT(net)
            const size_t lp = line.find('(');
            const size_t rp = line.rfind(')');
            if (lp == std::string_view::npos || rp == std::string_view::npos || rp <= lp + 1) return false;
            std::string_view kw = ReaderText::TrimSv(line.substr(0, lp));
            std::string netName(ReaderText::TrimSv(line.substr(lp + 1, rp - lp - 1)));
            if (netName.empty()) return false;

            if (ReaderText::AsciiEqCI(kw, "INPUT")) {
                DetailGateAndDaqEda::GateNode pi;
                pi.instName = netName;
                pi.cellType = "INPUT";
                pi.isPrimaryIn = true;
                pi.pins.push_back({ "Y", netName, true, false });
                nodes.push_back(std::move(pi));
                ++nIn;
            } else if (ReaderText::AsciiEqCI(kw, "OUTPUT")) {
                DetailGateAndDaqEda::GateNode po;
                po.instName = netName;
                po.cellType = "OUTPUT";
                po.isPrimaryOut = true;
                po.pins.push_back({ "A", netName, false, false });
                nodes.push_back(std::move(po));
                ++nOut;
            } else {
                return false;
            }
        } else {
            // Gate assignment: outNet = GATE(in1, in2, ...)
            std::string lhs(ReaderText::TrimSv(line.substr(0, eq)));
            std::string_view rhs = ReaderText::TrimSv(line.substr(eq + 1));
            const size_t lp = rhs.find('(');
            const size_t rp = rhs.rfind(')');
            if (lhs.empty() || lp == std::string_view::npos || rp == std::string_view::npos || rp < lp + 1) {
                return false;
            }
            std::string gateType = ReaderText::ToUpperAscii(ReaderText::TrimSv(rhs.substr(0, lp)));
            if (gateType != "AND" && gateType != "NAND" && gateType != "OR" && gateType != "NOR" &&
                gateType != "XOR" && gateType != "XNOR" && gateType != "NXOR" &&
                gateType != "NOT" && gateType != "BUFF" && gateType != "BUF" &&
                gateType != "DFF" && gateType != "DFFR" && gateType != "MUX" && gateType != "LUT") {
                return false;
            }
            std::vector<std::string> args;
            splitArgs(rhs.substr(lp + 1, rp - lp - 1), args);
            if (args.empty()) return false;

            DetailGateAndDaqEda::GateNode gn;
            gn.instName = "U_" + lhs;
            gn.cellType = gateType;
            gn.isSeq = (gateType.rfind("DFF", 0) == 0);
            for (size_t k = 0; k < args.size(); ++k) {
                gn.pins.push_back({
                    gn.isSeq ? "D" : ("I" + std::to_string(k)),
                    args[k],
                    false,
                    false
                });
            }
            gn.pins.push_back({ gn.isSeq ? "Q" : "Y", lhs, true, (gateType == "NOT" || gateType[0] == 'N') });
            if (nodes.size() < 2048) nodes.push_back(std::move(gn));
            ++nGates;
        }
    }

    if (nIn == 0 || (nOut == 0 && nGates == 0) || nGates == 0) return false;

    wchar_t title[160] = {};
    swprintf_s(title, L"ISCAS .bench Gate Netlist (%zu inputs, %zu outputs, %zu gates)", nIn, nOut, nGates);
    return DetailGateAndDaqEda::RenderGateDagToBoard(title, nodes, out);
}

} // namespace Formats
