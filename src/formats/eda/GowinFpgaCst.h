#pragma once
// GowinFpgaCst.h — Gowin EDA FPGA Physical Pin Constraints (.cst — Tang Nano / Arora / LittleBee)
// Parses `IO_LOC "<port>" <pin>[,<diffPin>];` and `IO_PORT "<port>" IO_TYPE=<std> PULL_MODE=...;`
// statements and renders the BGA / QFN pinout via shared FpgaPinConstraintsCommon.h.

namespace Formats {

namespace DetailGowinCst {

inline bool StripQuotes(std::string_view sv, std::string& out) {
    sv = ReaderText::TrimSv(sv);
    if (sv.size() >= 2u && sv.front() == '"' && sv.back() == '"') {
        out.assign(sv.data() + 1u, sv.size() - 2u);
        return !out.empty();
    }
    if (!sv.empty() && sv.find_first_of(" \t;=") == std::string_view::npos) {
        out.assign(sv.data(), sv.size());
        return true;
    }
    return false;
}

} // namespace DetailGowinCst

inline bool ParseGowinFpgaCst(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    std::string_view sv;
    if (!FormatPrologue::PrepareUtf8TextNoNul(data, size, sv, 14u, (16u << 20))) return false;

    std::vector<FpgaPinCommon::FpgaPinEntry> pins;
    std::unordered_map<std::string, size_t> portToIdx;

    size_t linePos = 0u;
    std::string_view rawLine;
    while (ReaderText::NextLineSv(sv, linePos, rawLine)) {
        // Strip '//' comments
        const size_t sl = rawLine.find("//");
        if (sl != std::string_view::npos) rawLine = rawLine.substr(0u, sl);
        std::string_view line = ReaderText::TrimSv(rawLine);
        if (line.empty()) continue;

        // Every Gowin CST statement must terminate with ';'
        if (line.back() != ';') return false;
        line.remove_suffix(1u);
        line = ReaderText::TrimSv(line);
        if (line.empty()) continue;

        std::string_view kw;
        if (!ReaderText::NextTokenSv(line, kw)) continue;

        if (ReaderText::AsciiEqCI(kw, "IO_LOC")) {
            line = ReaderText::TrimSv(line);
            std::string portName;
            if (!line.empty() && line.front() == '"') {
                const size_t q = line.find('"', 1u);
                if (q == std::string_view::npos) return false;
                portName.assign(line.data() + 1u, q - 1u);
                line = ReaderText::TrimSv(line.substr(q + 1u));
            } else {
                std::string_view pTok;
                if (!ReaderText::NextTokenSv(line, pTok)) return false;
                portName.assign(pTok.data(), pTok.size());
                line = ReaderText::TrimSv(line);
            }
            if (portName.empty() || line.empty()) return false;

            // Primary pin site (before optional `,diffNegPin`)
            const size_t comma = line.find(',');
            std::string_view siteSv = ReaderText::TrimSv((comma == std::string_view::npos) ? line : line.substr(0u, comma));
            if (siteSv.empty()) return false;

            auto it = portToIdx.find(portName);
            if (it == portToIdx.end()) {
                FpgaPinCommon::FpgaPinEntry pe{};
                pe.signal = portName;
                pe.site.assign(siteSv.data(), siteSv.size());
                std::string low = ReaderText::ToLowerAscii(portName);
                pe.isClock = (low.find("clk") != std::string::npos || low.find("xtal") != std::string::npos);
                portToIdx[portName] = pins.size();
                pins.push_back(std::move(pe));
            } else {
                pins[it->second].site.assign(siteSv.data(), siteSv.size());
            }
        } else if (ReaderText::AsciiEqCI(kw, "IO_PORT")) {
            line = ReaderText::TrimSv(line);
            std::string portName;
            if (!line.empty() && line.front() == '"') {
                const size_t q = line.find('"', 1u);
                if (q == std::string_view::npos) return false;
                portName.assign(line.data() + 1u, q - 1u);
                line = ReaderText::TrimSv(line.substr(q + 1u));
            } else {
                std::string_view pTok;
                if (!ReaderText::NextTokenSv(line, pTok)) return false;
                portName.assign(pTok.data(), pTok.size());
                line = ReaderText::TrimSv(line);
            }
            if (portName.empty()) return false;

            std::string ioStd;
            bool pullUp = false;
            std::string_view kv;
            while (ReaderText::NextTokenSv(line, kv)) {
                const size_t eq = kv.find('=');
                if (eq == std::string_view::npos) return false;
                std::string_view k = kv.substr(0u, eq);
                std::string_view v = kv.substr(eq + 1u);
                if (ReaderText::AsciiEqCI(k, "IO_TYPE")) {
                    ioStd.assign(v.data(), v.size());
                } else if (ReaderText::AsciiEqCI(k, "PULL_MODE") && ReaderText::AsciiEqCI(v, "UP")) {
                    pullUp = true;
                }
            }
            auto it = portToIdx.find(portName);
            if (it != portToIdx.end()) {
                if (!ioStd.empty()) pins[it->second].ioStd = ioStd;
                if (pullUp) pins[it->second].hasPullup = true;
            }
        } else if (ReaderText::AsciiEqCI(kw, "INS_LOC") || ReaderText::AsciiEqCI(kw, "CLOCK_LOC")) {
            continue;
        } else {
            return false;
        }
    }

    if (pins.empty()) return false;

    out = ParsedEdaBoard{};
    wchar_t title[160] = {};
    swprintf_s(title, L"Gowin EDA FPGA Constraints (.cst, %zu I/O pins)", pins.size());
    if (!FpgaPinCommon::EmitFpgaPinoutBoard(title, L"GOWIN_FPGA_PKG", pins, {}, out)) return false;
    FinalizeBoardBounds(out);
    out.valid = true;
    return true;
}

} // namespace Formats
