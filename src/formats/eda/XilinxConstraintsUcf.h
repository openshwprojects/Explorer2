#pragma once
// XilinxConstraintsUcf.h — Xilinx ISE User Constraints File (.ucf — Spartan-3/6, Virtex-5/6, CPLD) reader
// Parses semicolon-delimited NET "<sig>" LOC = "<pin>" | IOSTANDARD = ... | TNM_NET = ...; and
// TIMESPEC TS_... = PERIOD ... ns/MHz; statements, rendering via FpgaPinCommon::EmitFpgaPinoutBoard.

namespace Formats {

inline bool ParseXilinxConstraintsUcf(
    const uint8_t* data,
    size_t size,
    ParsedEdaBoard& outBoard)
{
    if (!data || size < 12 || size > 16u * 1024u * 1024u) return false;
    std::string_view sv = ReaderText::StripUtf8Bom(std::string_view(reinterpret_cast<const char*>(data), size));
    if (sv.find('\0') != std::string_view::npos) return false;

    // Strip '#' and '//' comments and normalize '=', '|', ',' into space-separated tokens
    std::string clean;
    clean.reserve(sv.size() + 64u);
    for (size_t i = 0; i < sv.size(); ) {
        if (sv[i] == '#' || (i + 1u < sv.size() && sv[i] == '/' && sv[i + 1u] == '/')) {
            while (i < sv.size() && sv[i] != '\n') ++i;
            continue;
        }
        if (i + 1u < sv.size() && sv[i] == '/' && sv[i + 1u] == '*') {
            i += 2u;
            bool closed = false;
            while (i + 1u < sv.size()) {
                if (sv[i] == '*' && sv[i + 1u] == '/') { i += 2u; closed = true; break; }
                ++i;
            }
            if (!closed) return false;
            clean.push_back(' ');
            continue;
        }
        const char ch = sv[i++];
        if (ch == '\r' || ch == '\n' || ch == '\t' || ch == '=' || ch == '|' || ch == ',') {
            clean.push_back(' ');
        } else {
            clean.push_back(ch);
        }
    }

    auto unquote = [](std::string_view s) -> std::string_view {
        s = ReaderText::TrimSv(s);
        if (s.size() >= 2u && s.front() == '"' && s.back() == '"') {
            return s.substr(1u, s.size() - 2u);
        }
        return s;
    };

    auto tokenizeStmt = [](std::string_view stmt, std::vector<std::string_view>& outToks) -> bool {
        outToks.clear();
        size_t i = 0;
        while (i < stmt.size()) {
            while (i < stmt.size() && stmt[i] == ' ') ++i;
            if (i >= stmt.size()) break;
            if (stmt[i] == '"') {
                const size_t st = i++;
                while (i < stmt.size() && stmt[i] != '"') ++i;
                if (i >= stmt.size()) return false;
                ++i;
                outToks.push_back(stmt.substr(st, i - st));
            } else {
                const size_t st = i;
                while (i < stmt.size() && stmt[i] != ' ' && stmt[i] != '"') ++i;
                outToks.push_back(stmt.substr(st, i - st));
            }
        }
        return true;
    };

    std::vector<FpgaPinCommon::FpgaPinEntry> pins;
    std::unordered_map<std::string, size_t> pinMap;
    std::unordered_map<std::string, std::string> tnmToSignal;
    std::vector<FpgaPinCommon::FpgaClockSpec> clocks;

    size_t pos = 0;
    while (pos < clean.size()) {
        while (pos < clean.size() && clean[pos] == ' ') ++pos;
        if (pos >= clean.size()) break;
        const size_t semi = clean.find(';', pos);
        if (semi == std::string::npos) return false; // UCF statements must end with ';'

        std::string_view stmt = ReaderText::TrimSv(std::string_view(clean).substr(pos, semi - pos));
        pos = semi + 1u;
        if (stmt.empty()) continue;

        std::vector<std::string_view> toks;
        if (!tokenizeStmt(stmt, toks) || toks.empty()) return false;

        const std::string_view kw = toks[0];
        if (ReaderText::AsciiEqCI(kw, "NET")) {
            if (toks.size() < 2u) return false;
            const std::string sig(unquote(toks[1]));
            if (sig.empty()) return false;

            std::string loc, ioStd, tnm;
            bool pullUp = false;
            bool isClk = false;
            for (size_t k = 2u; k < toks.size(); ++k) {
                const std::string_view ak = toks[k];
                if (ReaderText::AsciiEqCI(ak, "LOC") && k + 1u < toks.size()) {
                    loc = std::string(unquote(toks[++k]));
                } else if (ReaderText::AsciiEqCI(ak, "IOSTANDARD") && k + 1u < toks.size()) {
                    ioStd = std::string(unquote(toks[++k]));
                } else if ((ReaderText::AsciiEqCI(ak, "TNM_NET") || ReaderText::AsciiEqCI(ak, "TNM")) && k + 1u < toks.size()) {
                    tnm = std::string(unquote(toks[++k]));
                    isClk = true;
                } else if (ReaderText::AsciiEqCI(ak, "PERIOD") && k + 1u < toks.size()) {
                    isClk = true;
                } else if (ReaderText::AsciiEqCI(ak, "PULLUP")) {
                    pullUp = true;
                }
            }

            auto it = pinMap.find(sig);
            if (it == pinMap.end()) {
                if (!loc.empty()) {
                    FpgaPinCommon::FpgaPinEntry pe;
                    pe.signal = sig;
                    pe.site   = loc;
                    pe.ioStd  = ioStd;
                    pe.hasPullup = pullUp;
                    const std::string low = ReaderText::ToLowerAscii(sig);
                    pe.isClock = isClk || (low.find("clk") != std::string::npos) || (low.find("clock") != std::string::npos);
                    pinMap.emplace(sig, pins.size());
                    pins.push_back(std::move(pe));
                }
            } else {
                auto& pe = pins[it->second];
                if (!loc.empty()) pe.site = loc;
                if (!ioStd.empty()) pe.ioStd = ioStd;
                if (pullUp) pe.hasPullup = true;
                if (isClk) pe.isClock = true;
            }
            if (!tnm.empty()) tnmToSignal[tnm] = sig;
        } else if (ReaderText::AsciiEqCI(kw, "TIMESPEC")) {
            // TIMESPEC TS_clk = PERIOD "clk_grp" 10.0 ns HIGH 50%
            if (toks.size() < 5u) return false;
            for (size_t k = 2u; k + 2u < toks.size(); ++k) {
                if (ReaderText::AsciiEqCI(toks[k], "PERIOD")) {
                    const std::string grp(unquote(toks[k + 1u]));
                    std::string_view valSv = unquote(toks[k + 2u]);
                    // Handle inline unit like "20ns" vs separate token "20" "ns"
                    size_t numLen = 0;
                    while (numLen < valSv.size() && ((valSv[numLen] >= '0' && valSv[numLen] <= '9') || valSv[numLen] == '.')) {
                        ++numLen;
                    }
                    double val = 0.0;
                    if (numLen == 0u || !ReaderText::ParseDoubleSv(valSv.substr(0u, numLen), val) || val <= 0.0) {
                        return false;
                    }
                    std::string_view unit = (numLen < valSv.size()) ? valSv.substr(numLen)
                                          : ((k + 3u < toks.size()) ? unquote(toks[k + 3u]) : std::string_view("ns"));
                    double mhz = val;
                    if (ReaderText::AsciiEqCI(unit, "ns"))      mhz = 1000.0 / val;
                    else if (ReaderText::AsciiEqCI(unit, "ps")) mhz = 1000000.0 / val;
                    else if (ReaderText::AsciiEqCI(unit, "us")) mhz = 1.0 / val;
                    else if (ReaderText::AsciiEqCI(unit, "mhz")) mhz = val;
                    clocks.push_back({ grp, mhz });

                    auto tit = tnmToSignal.find(grp);
                    const std::string& sigKey = (tit != tnmToSignal.end()) ? tit->second : grp;
                    auto pit = pinMap.find(sigKey);
                    if (pit != pinMap.end()) pins[pit->second].isClock = true;
                    break;
                }
            }
        } else if (ReaderText::AsciiEqCI(kw, "INST") ||
                   ReaderText::AsciiEqCI(kw, "TIMEGRP") ||
                   ReaderText::AsciiEqCI(kw, "CONFIG") ||
                   ReaderText::AsciiEqCI(kw, "PIN") ||
                   ReaderText::AsciiEqCI(kw, "OFFSET") ||
                   ReaderText::AsciiEqCI(kw, "AREA_GROUP")) {
            // Valid auxiliary Xilinx ISE UCF constraint
        } else {
            return false;
        }
    }

    if (pins.empty()) return false;
    return FpgaPinCommon::EmitFpgaPinoutBoard(
        L"Xilinx ISE User Constraints (.ucf)",
        L"XILINX_FPGA",
        pins,
        clocks,
        outBoard);
}

} // namespace Formats
