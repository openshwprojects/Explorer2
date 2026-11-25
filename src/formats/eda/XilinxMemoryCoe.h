#pragma once
// XilinxMemoryCoe.h — Xilinx Vivado / ISE Coefficient File (.coe) reader
// Supports both Block Memory Generator (`memory_initialization_radix` + `memory_initialization_vector`)
// in radices 2/8/10/16 and FIR Compiler (`radix` + `coefdata` signed/float filter taps).

namespace Formats {

namespace DetailXilinxCoe {

inline bool ParseRadixValue(std::string_view tok, int radix, double& outVal, uint32_t& outBitGuess) {
    tok = ReaderText::TrimSv(tok);
    if (tok.empty()) return false;

    if (radix == 10) {
        double dv = 0.0;
        if (!ReaderText::ParseDoubleSv(tok, dv)) return false;
        outVal = dv;
        const double mag = std::fabs(dv);
        outBitGuess = (mag > 65535.0) ? 32u : ((mag > 255.0) ? 16u : 8u);
        return true;
    }

    bool neg = false;
    if (tok.front() == '-') { neg = true; tok.remove_prefix(1); }
    else if (tok.front() == '+') { tok.remove_prefix(1); }
    if (tok.empty()) return false;

    uint64_t acc = 0;
    for (char c : tok) {
        int d = -1;
        if (radix == 2 && (c == '0' || c == '1')) d = c - '0';
        else if (radix == 8 && c >= '0' && c <= '7') d = c - '0';
        else if (radix == 16) d = ReaderText::HexVal(c);
        if (d < 0) return false;
        acc = (acc * static_cast<uint64_t>(radix)) + static_cast<uint64_t>(d);
    }
    if (radix == 2) outBitGuess = static_cast<uint32_t>(std::clamp<size_t>(tok.size(), 1u, 64u));
    else if (radix == 16) outBitGuess = static_cast<uint32_t>(std::clamp<size_t>(tok.size() * 4u, 8u, 64u));
    else outBitGuess = 16u;

    outVal = neg ? -static_cast<double>(acc) : static_cast<double>(acc);
    return true;
}

} // namespace DetailXilinxCoe

inline bool ParseXilinxMemoryCoe(
    const uint8_t* data,
    size_t size,
    ParsedEdaBoard& out)
{
    if (!data || size < 16 || size > 64u * 1024u * 1024u) return false;
    std::string_view sv = ReaderText::StripUtf8Bom(std::string_view(reinterpret_cast<const char*>(data), size));
    if (sv.find('\0') != std::string_view::npos) return false;

    // Strip ';' comments to end of line
    std::string clean;
    clean.reserve(sv.size());
    size_t pos = 0;
    std::string_view line;
    while (ReaderText::NextLineSv(sv, pos, line)) {
        const size_t sc = line.find(';');
        // Note: in COE, ';' at start or after ';' terminates statement OR starts comment!
        // Keep the FIRST ';' on a statement line if preceded by non-whitespace, strip everything after it!
        if (sc != std::string_view::npos) {
            std::string_view before = ReaderText::TrimSv(line.substr(0, sc));
            if (!before.empty()) {
                clean.append(before);
                clean.push_back(';');
            }
            clean.push_back('\n');
        } else {
            clean.append(line);
            clean.push_back('\n');
        }
    }

    // Parse ';' terminated statements: key = value(s);
    int radix = 10;
    bool sawRadix = false;
    bool isFirCoef = false;
    bool sawVector = false;
    std::vector<double> values;
    uint32_t maxBits = 8;

    size_t sPos = 0;
    while (sPos < clean.size()) {
        const size_t semi = clean.find(';', sPos);
        std::string_view stmt = (semi == std::string::npos)
            ? std::string_view(clean).substr(sPos)
            : std::string_view(clean).substr(sPos, semi - sPos);
        sPos = (semi == std::string::npos) ? clean.size() : (semi + 1u);

        stmt = ReaderText::TrimSv(stmt);
        if (stmt.empty()) continue;

        const size_t eq = stmt.find('=');
        if (eq == std::string_view::npos) return false;
        std::string_view key = ReaderText::TrimSv(stmt.substr(0, eq));
        std::string_view rhs = ReaderText::TrimSv(stmt.substr(eq + 1));

        if (ReaderText::AsciiEqCI(key, "memory_initialization_radix") ||
            ReaderText::AsciiEqCI(key, "radix")) {
            int r = 0;
            if (!ReaderText::ParseIntSv(rhs, r) || (r != 2 && r != 8 && r != 10 && r != 16)) return false;
            radix = r;
            sawRadix = true;
        } else if (ReaderText::AsciiEqCI(key, "memory_initialization_vector") ||
                   ReaderText::AsciiEqCI(key, "coefdata")) {
            isFirCoef = ReaderText::AsciiEqCI(key, "coefdata");
            sawVector = true;

            size_t p = 0;
            while (p < rhs.size()) {
                while (p < rhs.size() && (rhs[p] == ' ' || rhs[p] == '\t' || rhs[p] == '\r' || rhs[p] == '\n' || rhs[p] == ',')) ++p;
                if (p >= rhs.size()) break;
                const size_t st = p;
                while (p < rhs.size() && rhs[p] != ' ' && rhs[p] != '\t' && rhs[p] != '\r' && rhs[p] != '\n' && rhs[p] != ',') ++p;
                double v = 0.0;
                uint32_t bg = 8;
                if (!DetailXilinxCoe::ParseRadixValue(rhs.substr(st, p - st), radix, v, bg)) return false;
                values.push_back(v);
                maxBits = std::max(maxBits, bg);
                if (values.size() > 4000000) break;
            }
        } else if (ReaderText::AsciiEqCI(key, "coefwidth")) {
            int cw = 0;
            if (ReaderText::ParseIntSv(rhs, cw) && cw >= 1 && cw <= 64) maxBits = static_cast<uint32_t>(cw);
        }
    }

    if (!sawVector || values.empty() || (!sawRadix && !isFirCoef)) return false;

    double peakAbs = 1e-9;
    size_t nonZero = 0;
    for (double v : values) {
        peakAbs = std::max(peakAbs, std::fabs(v));
        if (std::fabs(v) > 1e-12) ++nonZero;
    }

    if (isFirCoef) {
        // Plot FIR impulse response stem diagram + BRAM tap summary
        out.kind = EdaDocKind::GerberLayer;
        wchar_t title[160] = {};
        swprintf_s(title, L"Xilinx FIR Filter COE (%zu taps, radix %d)", values.size(), radix);
        out.formatTitle = title;

        const float w = 80.0f, h = 36.0f;
        DrawEdaBoxRect(out, 0.0f, -h * 0.5f, w, h * 0.5f, 0.25f, EdaLayerId::EdgeCuts, 0);
        out.traces.push_back({ 0.0f, 0.0f, w, 0.0f, 0.18f, EdaLayerId::TopSilk, 0 }); // zero axis
        out.labels.push_back({ 0.0f, h * 0.5f + 2.5f, 1.15f, EdaLayerId::TopSilk, title });

        const size_t n = values.size();
        for (size_t i = 0; i < n; ++i) {
            const float x = (n > 1) ? (2.0f + (w - 4.0f) * static_cast<float>(i) / static_cast<float>(n - 1)) : (w * 0.5f);
            const float y = static_cast<float>((values[i] / peakAbs) * (h * 0.44));
            out.traces.push_back({ x, 0.0f, x, y, 0.35f, (y >= 0.0f) ? EdaLayerId::TopCopper : EdaLayerId::BottomCopper, 1 });
            out.circles.push_back({ x, y, 0.45f, 0.20f, true, EdaLayerId::InnerCopper });
        }
        out.netCount = values.size();
        return true;
    }

    FirmwareMemoryMapEda::MemorySpan sp;
    sp.startAddr = 0;
    sp.wordCount = values.size();
    sp.bitsPerWord = maxBits;
    sp.fillFraction = static_cast<double>(nonZero) / static_cast<double>(values.size());

    const size_t step = std::max<size_t>(1u, (values.size() + 19u) / 20u);
    for (size_t off = 0; off < values.size() && sp.rowLevels.size() < 20; off += step) {
        const size_t cnt = std::min(step, values.size() - off);
        double sum = 0.0;
        for (size_t k = 0; k < cnt; ++k) sum += std::fabs(values[off + k]) / peakAbs;
        sp.rowLevels.push_back(static_cast<float>(sum / static_cast<double>(cnt)));
    }

    wchar_t title[160] = {};
    swprintf_s(title, L"Xilinx Block Memory COE (%zu words x %u-bit, radix %d)", values.size(), maxBits, radix);
    return FirmwareMemoryMapEda::EmitMemoryMapBoard(title, L"XILINX_BRAM", { sp }, false, 0, out);
}

} // namespace Formats
