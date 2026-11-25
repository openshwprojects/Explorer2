#pragma once
// BerkeleyEspressoPla.h — UC Berkeley Espresso Two-Level PLA (.pla) logic reader
// Parses .i, .o, .ilb, .ob, .p, .type, AND/OR truth-plane cubes ('0','1','-','~'), and .e,
// synthesizing the two-level sum-of-products logic network via DetailGateAndDaqEda::RenderGateDagToBoard.

namespace Formats {

inline bool ParseBerkeleyEspressoPla(
    const uint8_t* data,
    size_t size,
    ParsedEdaBoard& out)
{
    if (!data || size < 10 || size > 32u * 1024u * 1024u) return false;
    std::string_view sv = ReaderText::StripUtf8Bom(std::string_view(reinterpret_cast<const char*>(data), size));
    if (sv.find('\0') != std::string_view::npos) return false;

    int nInputs = -1;
    int nOutputs = -1;
    std::vector<std::string> inLabels;
    std::vector<std::string> outLabels;

    struct PlaCube {
        std::string inMask;
        std::string outMask;
    };
    std::vector<PlaCube> cubes;

    size_t pos = 0;
    std::string_view line;
    while (ReaderText::NextLineSv(sv, pos, line)) {
        const size_t hp = line.find('#');
        if (hp != std::string_view::npos) line = line.substr(0, hp);
        line = ReaderText::TrimSv(line);
        if (line.empty()) continue;

        if (line.front() == '.') {
            std::string_view rem = line, dir;
            ReaderText::NextTokenSv(rem, dir);
            if (ReaderText::AsciiEqCI(dir, ".e") || ReaderText::AsciiEqCI(dir, ".end")) {
                break;
            } else if (ReaderText::AsciiEqCI(dir, ".i")) {
                std::string_view tv;
                if (!ReaderText::NextTokenSv(rem, tv) || !ReaderText::ParseIntSv(tv, nInputs) || nInputs < 1 || nInputs > 256) {
                    return false;
                }
            } else if (ReaderText::AsciiEqCI(dir, ".o")) {
                std::string_view tv;
                if (!ReaderText::NextTokenSv(rem, tv) || !ReaderText::ParseIntSv(tv, nOutputs) || nOutputs < 1 || nOutputs > 256) {
                    return false;
                }
            } else if (ReaderText::AsciiEqCI(dir, ".ilb")) {
                inLabels.clear();
                std::string_view nm;
                while (ReaderText::NextTokenSv(rem, nm)) inLabels.emplace_back(nm);
            } else if (ReaderText::AsciiEqCI(dir, ".ob")) {
                outLabels.clear();
                std::string_view nm;
                while (ReaderText::NextTokenSv(rem, nm)) outLabels.emplace_back(nm);
            }
            continue;
        }

        if (nInputs <= 0 || nOutputs <= 0) return false;

        // Parse cube row: either "<inBits> <outBits>" or "<inBits>|<outBits>" or glued "<inBits><outBits>"
        std::string compact;
        compact.reserve(static_cast<size_t>(nInputs + nOutputs));
        for (char c : line) {
            if (c == ' ' || c == '\t' || c == '|') continue;
            if (c != '0' && c != '1' && c != '-' && c != '~' && c != '2' && c != '4') return false;
            compact.push_back(c);
        }
        if (static_cast<int>(compact.size()) != nInputs + nOutputs) return false;
        if (cubes.size() < 256) {
            cubes.push_back({
                compact.substr(0, static_cast<size_t>(nInputs)),
                compact.substr(static_cast<size_t>(nInputs), static_cast<size_t>(nOutputs))
            });
        }
    }

    if (nInputs <= 0 || nOutputs <= 0 || cubes.empty()) return false;

    while (static_cast<int>(inLabels.size()) < nInputs) {
        inLabels.push_back("in" + std::to_string(inLabels.size()));
    }
    while (static_cast<int>(outLabels.size()) < nOutputs) {
        outLabels.push_back("out" + std::to_string(outLabels.size()));
    }

    std::vector<DetailGateAndDaqEda::GateNode> nodes;
    // 1. Primary Input nodes
    for (int i = 0; i < nInputs; ++i) {
        DetailGateAndDaqEda::GateNode pi;
        pi.instName = inLabels[static_cast<size_t>(i)];
        pi.cellType = "INPUT";
        pi.isPrimaryIn = true;
        pi.pins.push_back({ "Y", "pi_" + std::to_string(i), true, false });
        nodes.push_back(std::move(pi));
    }

    // 2. AND-plane product term gates
    std::vector<std::vector<std::string>> orFanins(static_cast<size_t>(nOutputs));
    for (size_t ci = 0; ci < cubes.size(); ++ci) {
        const auto& cb = cubes[ci];
        bool drivesAnyOut = false;
        for (int o = 0; o < nOutputs; ++o) {
            if (cb.outMask[static_cast<size_t>(o)] == '1') drivesAnyOut = true;
        }
        if (!drivesAnyOut) continue;

        const std::string pNet = "pterm_" + std::to_string(ci);
        DetailGateAndDaqEda::GateNode andGate;
        andGate.instName = "P" + std::to_string(ci);
        andGate.cellType = "AND";
        int litIdx = 0;
        for (int i = 0; i < nInputs; ++i) {
            const char lit = cb.inMask[static_cast<size_t>(i)];
            if (lit == '0' || lit == '1') {
                andGate.pins.push_back({
                    "I" + std::to_string(litIdx++),
                    "pi_" + std::to_string(i),
                    false,
                    (lit == '0')
                });
            }
        }
        andGate.pins.push_back({ "Y", pNet, true, false });
        nodes.push_back(std::move(andGate));

        for (int o = 0; o < nOutputs; ++o) {
            if (cb.outMask[static_cast<size_t>(o)] == '1') {
                orFanins[static_cast<size_t>(o)].push_back(pNet);
            }
        }
    }

    // 3. OR-plane sum gates + Primary Output nodes
    for (int o = 0; o < nOutputs; ++o) {
        const auto& fins = orFanins[static_cast<size_t>(o)];
        const std::string outNet = "po_" + std::to_string(o);
        if (!fins.empty()) {
            DetailGateAndDaqEda::GateNode orGate;
            orGate.instName = "SUM_" + std::to_string(o);
            orGate.cellType = (fins.size() == 1) ? "BUF" : "OR";
            for (size_t k = 0; k < fins.size(); ++k) {
                orGate.pins.push_back({ "I" + std::to_string(k), fins[k], false, false });
            }
            orGate.pins.push_back({ "Y", outNet, true, false });
            nodes.push_back(std::move(orGate));
        }

        DetailGateAndDaqEda::GateNode po;
        po.instName = outLabels[static_cast<size_t>(o)];
        po.cellType = "OUTPUT";
        po.isPrimaryOut = true;
        po.pins.push_back({ "A", outNet, false, false });
        nodes.push_back(std::move(po));
    }

    wchar_t title[160] = {};
    swprintf_s(title, L"Berkeley Espresso PLA (%d in, %d out, %zu product terms)",
               nInputs, nOutputs, cubes.size());
    return DetailGateAndDaqEda::RenderGateDagToBoard(title, nodes, out);
}

} // namespace Formats
