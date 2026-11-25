#pragma once
// LatticeDiamondLpf.h — Lattice Diamond / Trellis / ECP5 / MachXO2 Logical Preference File (.lpf) reader
// Parses semicolon-delimited LOCATE COMP "<port>" SITE "<site>", IOBUF PORT, FREQUENCY PORT/NET,
// BLOCK, SYSCONFIG, and BANK statements, rendering via FpgaPinCommon::EmitFpgaPinoutBoard.

namespace Formats {

inline bool ParseLatticeDiamondLpf(
    const uint8_t* data,
    size_t size,
    ParsedEdaBoard& outBoard)
{
    if (!data || size < 16 || size > 16u * 1024u * 1024u) return false;
    std::string_view sv = ReaderText::StripUtf8Bom(std::string_view(reinterpret_cast<const char*>(data), size));
    if (sv.find('\0') != std::string_view::npos) return false;

    // Strip '#' and '//' line comments and '/* ... */' block comments
    std::string clean;
    clean.reserve(sv.size());
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
        clean.push_back((ch == '\r' || ch == '\n' || ch == '\t') ? ' ' : ch);
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
            while (i < stmt.size() && (stmt[i] == ' ' || stmt[i] == '=')) ++i;
            if (i >= stmt.size()) break;
            if (stmt[i] == '"') {
                const size_t st = i++;
                while (i < stmt.size() && stmt[i] != '"') ++i;
                if (i >= stmt.size()) return false;
                ++i;
                outToks.push_back(stmt.substr(st, i - st));
            } else {
                const size_t st = i;
                while (i < stmt.size() && stmt[i] != ' ' && stmt[i] != '=' && stmt[i] != '"') ++i;
                outToks.push_back(stmt.substr(st, i - st));
            }
        }
        return true;
    };

    std::vector<FpgaPinCommon::FpgaPinEntry> pins;
    std::unordered_map<std::string, size_t> pinIndexMap;
    std::vector<FpgaPinCommon::FpgaClockSpec> clocks;

    size_t pos = 0;
    while (pos < clean.size()) {
        while (pos < clean.size() && clean[pos] == ' ') ++pos;
        if (pos >= clean.size()) break;
        const size_t semi = clean.find(';', pos);
        if (semi == std::string::npos) return false; // every LPF statement must end with ';'

        std::string_view stmt = ReaderText::TrimSv(std::string_view(clean).substr(pos, semi - pos));
        pos = semi + 1u;
        if (stmt.empty()) continue;

        std::vector<std::string_view> toks;
        if (!tokenizeStmt(stmt, toks) || toks.empty()) return false;

        const std::string_view kw = toks[0];
        if (ReaderText::AsciiEqCI(kw, "LOCATE")) {
            // LOCATE COMP "<port>" SITE "<site>"
            if (toks.size() < 5u || !ReaderText::AsciiEqCI(toks[1], "COMP")) return false;
            const std::string port(unquote(toks[2]));
            std::string site;
            for (size_t k = 3u; k + 1u < toks.size(); ++k) {
                if (ReaderText::AsciiEqCI(toks[k], "SITE")) {
                    site = std::string(unquote(toks[k + 1u]));
                    break;
                }
            }
            if (port.empty() || site.empty()) return false;
            auto it = pinIndexMap.find(port);
            if (it == pinIndexMap.end()) {
                FpgaPinCommon::FpgaPinEntry pe;
                pe.signal = port;
                pe.site   = site;
                const std::string low = ReaderText::ToLowerAscii(port);
                if (low.find("clk") != std::string::npos || low.find("clock") != std::string::npos) pe.isClock = true;
                pinIndexMap.emplace(port, pins.size());
                pins.push_back(std::move(pe));
            } else {
                pins[it->second].site = site;
            }
        } else if (ReaderText::AsciiEqCI(kw, "IOBUF")) {
            // IOBUF PORT "<port>" IO_TYPE=LVCMOS33 PULLMODE=UP ...
            if (toks.size() < 3u ||
                (!ReaderText::AsciiEqCI(toks[1], "PORT") && !ReaderText::AsciiEqCI(toks[1], "ALLPORTS"))) {
                return false;
            }
            const std::string port(unquote(toks[2]));
            std::string ioType;
            bool pullUp = false;
            for (size_t k = 3u; k + 1u < toks.size(); ++k) {
                if (ReaderText::AsciiEqCI(toks[k], "IO_TYPE")) ioType = std::string(unquote(toks[k + 1u]));
                else if (ReaderText::AsciiEqCI(toks[k], "PULLMODE") && ReaderText::AsciiEqCI(toks[k + 1u], "UP")) pullUp = true;
            }
            auto it = pinIndexMap.find(port);
            if (it != pinIndexMap.end()) {
                if (!ioType.empty()) pins[it->second].ioStd = ioType;
                if (pullUp) pins[it->second].hasPullup = true;
            }
        } else if (ReaderText::AsciiEqCI(kw, "FREQUENCY")) {
            // FREQUENCY PORT|NET "<name>" <mhz> MHz
            if (toks.size() < 5u) return false;
            const std::string clkName(unquote(toks[2]));
            double mhz = 0.0;
            if (!ReaderText::ParseDoubleSv(toks[3], mhz) || mhz <= 0.0 || mhz > 1e5) return false;
            clocks.push_back({ clkName, mhz });
            auto it = pinIndexMap.find(clkName);
            if (it != pinIndexMap.end()) pins[it->second].isClock = true;
        } else if (ReaderText::AsciiEqCI(kw, "BLOCK") ||
                   ReaderText::AsciiEqCI(kw, "SYSCONFIG") ||
                   ReaderText::AsciiEqCI(kw, "BANK") ||
                   ReaderText::AsciiEqCI(kw, "DEFINE") ||
                   ReaderText::AsciiEqCI(kw, "UGROUP") ||
                   ReaderText::AsciiEqCI(kw, "MULTICYCLE") ||
                   ReaderText::AsciiEqCI(kw, "MAXDELAY") ||
                   ReaderText::AsciiEqCI(kw, "INPUT_SETUP") ||
                   ReaderText::AsciiEqCI(kw, "CLOCK_TO_OUT") ||
                   ReaderText::AsciiEqCI(kw, "COMMERCIAL") ||
                   ReaderText::AsciiEqCI(kw, "INDUSTRIAL")) {
            // Valid auxiliary LPF preference statement
        } else {
            return false;
        }
    }

    if (pins.empty()) return false;
    return FpgaPinCommon::EmitFpgaPinoutBoard(
        L"Lattice Diamond / ECP5 Preferences (.lpf)",
        L"ECP5_CABGA",
        pins,
        clocks,
        outBoard);
}

} // namespace Formats
