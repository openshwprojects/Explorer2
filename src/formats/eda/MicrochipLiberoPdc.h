#pragma once
// MicrochipLiberoPdc.h — Microchip Libero SoC / PolarFire / SmartFusion2 / IGLOO2 Physical Design Constraints (.pdc)
// Parses Tcl `set_io` physical pin/bank/IOSTD/pull constraints (with `\` line continuations)
// and renders the BGA / QFP pinout via shared FpgaPinConstraintsCommon.h.

namespace Formats {

namespace DetailMicrochipPdc {

inline bool NextTclWord(std::string_view& s, std::string& outWord) {
    outWord.clear();
    while (!s.empty() && static_cast<unsigned char>(s.front()) <= 0x20u) s.remove_prefix(1u);
    if (s.empty() || s.front() == '#') { s = {}; return false; }
    if (s.front() == '"' || s.front() == '{') {
        const char closeCh = (s.front() == '"') ? '"' : '}';
        s.remove_prefix(1u);
        size_t p = 0u;
        while (p < s.size() && s[p] != closeCh) ++p;
        if (p >= s.size()) return false;
        outWord.assign(s.data(), p);
        s.remove_prefix(p + 1u);
        return true;
    }
    size_t e = 0u;
    while (e < s.size() && static_cast<unsigned char>(s[e]) > 0x20u && s[e] != ';') ++e;
    outWord.assign(s.data(), e);
    if (e < s.size() && s[e] == ';') ++e;
    s.remove_prefix(e);
    return true;
}

} // namespace DetailMicrochipPdc

inline bool ParseMicrochipLiberoPdc(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    std::string_view sv;
    if (!FormatPrologue::PrepareUtf8TextNoNul(data, size, sv, 16u, (16u << 20))) return false;

    // Join backslash-continued Tcl lines
    std::vector<std::string> logicalLines;
    {
        size_t pos = 0u;
        std::string_view rawLine;
        std::string accum;
        while (ReaderText::NextLineSv(sv, pos, rawLine)) {
            std::string_view tr = ReaderText::TrimSv(rawLine);
            if (tr.empty()) {
                if (!accum.empty()) { logicalLines.push_back(std::move(accum)); accum.clear(); }
                continue;
            }
            if (tr.front() == '#' && accum.empty()) continue;
            const bool cont = (tr.back() == '\\');
            if (cont) tr = ReaderText::TrimSv(tr.substr(0u, tr.size() - 1u));
            if (!accum.empty()) accum.push_back(' ');
            accum.append(tr.data(), tr.size());
            if (!cont) {
                logicalLines.push_back(std::move(accum));
                accum.clear();
            }
        }
        if (!accum.empty()) return false; // Trailing dangling backslash continuation!
    }

    std::vector<FpgaPinCommon::FpgaPinEntry> pins;
    for (const std::string& fullLine : logicalLines) {
        std::string_view rest = fullLine;
        std::string cmd;
        if (!DetailMicrochipPdc::NextTclWord(rest, cmd)) continue;

        if (cmd == "set_io") {
            FpgaPinCommon::FpgaPinEntry pe{};
            std::vector<std::string> args;
            std::string tok;
            while (DetailMicrochipPdc::NextTclWord(rest, tok)) {
                args.push_back(std::move(tok));
            }
            if (args.size() < 2u) return false;

            size_t ai = 0u;
            // PolarFire style: `set_io -port_name <port> -pin_name <pin> ...` OR SmartFusion2 positional `set_io <port> -pinname <pin> ...`
            if (!args[0].empty() && args[0].front() != '-') {
                pe.signal = args[0];
                ai = 1u;
            }
            for (; ai < args.size(); ++ai) {
                const std::string opt = ReaderText::ToLowerAscii(args[ai]);
                if (opt == "-port_name") {
                    if (ai + 1u >= args.size()) return false;
                    pe.signal = args[++ai];
                } else if (opt == "-pin_name" || opt == "-pinname") {
                    if (ai + 1u >= args.size()) return false;
                    pe.site = args[++ai];
                } else if (opt == "-io_std" || opt == "-iostd") {
                    if (ai + 1u >= args.size()) return false;
                    pe.ioStd = args[++ai];
                } else if (opt == "-res_pull") {
                    if (ai + 1u >= args.size()) return false;
                    std::string pv = ReaderText::ToLowerAscii(args[++ai]);
                    if (pv == "up") pe.hasPullup = true;
                } else if (!opt.empty() && opt.front() == '-') {
                    if (ai + 1u < args.size() && !args[ai + 1u].empty() && args[ai + 1u].front() != '-') {
                        ++ai;
                    }
                }
            }
            if (pe.signal.empty() || pe.site.empty()) return false;
            std::string sigLow = ReaderText::ToLowerAscii(pe.signal);
            pe.isClock = (sigLow.find("clk") != std::string::npos || sigLow.find("clock") != std::string::npos);
            pins.push_back(std::move(pe));
        } else if (cmd == "set_iobank" || cmd == "define_region" || cmd == "assign_net_macros" ||
                   cmd == "assign_local_clock") {
            continue;
        } else {
            return false; // Reject non-Libero Tcl scripts
        }
    }

    if (pins.empty()) return false;

    out = ParsedEdaBoard{};
    wchar_t title[160] = {};
    swprintf_s(title, L"Microchip Libero / PolarFire PDC (.pdc, %zu I/O pins)", pins.size());
    if (!FpgaPinCommon::EmitFpgaPinoutBoard(title, L"POLARFIRE_FBGA", pins, {}, out)) return false;
    FinalizeBoardBounds(out);
    out.valid = true;
    return true;
}

} // namespace Formats
