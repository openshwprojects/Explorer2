#pragma once
// IceStormPcf.h — Project IceStorm / Yosys / nextpnr / Lattice iCE40 Physical Constraints (.pcf) reader
// Parses set_io (with --warn-no-port, -pullup, -io_std flags) and set_frequency constraints,
// rendering the FPGA package pinout via FpgaPinCommon::EmitFpgaPinoutBoard.

namespace Formats {

inline bool ParseIceStormPcf(
    const uint8_t* data,
    size_t size,
    ParsedEdaBoard& outBoard)
{
    if (!data || size < 10 || size > 16u * 1024u * 1024u) return false;
    // Reject binary X11 Portable Compiled Font (.pcf starts with "\x01fcp") or any binary payload
    std::string_view sv = ReaderText::StripUtf8Bom(std::string_view(reinterpret_cast<const char*>(data), size));
    if (sv.find('\0') != std::string_view::npos) return false;

    std::vector<FpgaPinCommon::FpgaPinEntry> pins;
    std::vector<FpgaPinCommon::FpgaClockSpec> clocks;
    std::unordered_map<std::string, double> clkMap;

    size_t pos = 0;
    std::string_view line;
    while (ReaderText::NextLineSv(sv, pos, line)) {
        const size_t hp = line.find('#');
        if (hp != std::string_view::npos) line = line.substr(0, hp);
        line = ReaderText::TrimSv(line);
        if (line.empty()) continue;

        std::vector<std::string_view> toks;
        std::string_view rem = line, t;
        while (ReaderText::NextTokenSv(rem, t)) toks.push_back(t);
        if (toks.empty()) continue;

        if (toks[0] == "set_io") {
            FpgaPinCommon::FpgaPinEntry pe;
            std::vector<std::string_view> positional;
            for (size_t i = 1u; i < toks.size(); ++i) {
                const std::string_view a = toks[i];
                if (!a.empty() && a[0] == '-') {
                    if (a == "-pullup" && i + 1u < toks.size()) {
                        pe.hasPullup = ReaderText::AsciiEqCI(toks[++i], "yes") || toks[i] == "1";
                    } else if ((a == "-pullup_resistor" || a == "-io_std" || a == "-drive") && i + 1u < toks.size()) {
                        pe.ioStd = std::string(toks[++i]);
                    }
                    // boolean flags like --warn-no-port or -nowarn take no value
                } else {
                    positional.push_back(a);
                }
            }
            if (positional.size() != 2u) return false;
            pe.signal = std::string(positional[0]);
            pe.site   = std::string(positional[1]);
            if (pe.signal.empty() || pe.site.empty()) return false;
            const std::string lowSig = ReaderText::ToLowerAscii(pe.signal);
            if (lowSig.find("clk") != std::string::npos || lowSig.find("clock") != std::string::npos) {
                pe.isClock = true;
            }
            pins.push_back(std::move(pe));
        } else if (toks[0] == "set_frequency") {
            if (toks.size() != 3u) return false;
            double mhz = 0.0;
            if (!ReaderText::ParseDoubleSv(toks[2], mhz) || mhz <= 0.0 || mhz > 1e5) return false;
            clocks.push_back({ std::string(toks[1]), mhz });
            clkMap[std::string(toks[1])] = mhz;
        } else {
            return false;
        }
    }

    if (pins.empty()) return false;
    for (auto& pe : pins) {
        if (clkMap.find(pe.signal) != clkMap.end()) pe.isClock = true;
    }

    return FpgaPinCommon::EmitFpgaPinoutBoard(
        L"IceStorm / Lattice iCE40 Pinout (.pcf)",
        L"ICE40_PKG",
        pins,
        clocks,
        outBoard);
}

} // namespace Formats
