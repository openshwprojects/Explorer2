#pragma once
// QuartusMemoryMif.h — Intel / Altera Quartus Memory Initialization File (.mif) reader
// Parses -- and %...% comments, DEPTH, WIDTH, ADDRESS_RADIX, DATA_RADIX (HEX/DEC/UNS/OCT/BIN),
// and CONTENT BEGIN ... END; single-address, multi-word, and [a..b] range initializers.

namespace Formats {

namespace DetailQuartusMif {

inline int RadixFromKeyword(std::string_view kw) {
    if (ReaderText::AsciiEqCI(kw, "HEX")) return 16;
    if (ReaderText::AsciiEqCI(kw, "DEC") || ReaderText::AsciiEqCI(kw, "UNS")) return 10;
    if (ReaderText::AsciiEqCI(kw, "OCT")) return 8;
    if (ReaderText::AsciiEqCI(kw, "BIN")) return 2;
    return -1;
}

inline bool ParseMifUint(std::string_view tok, int radix, uint64_t& outVal) {
    tok = ReaderText::TrimSv(tok);
    if (tok.empty()) return false;
    if (tok.front() == '-') {
        // Signed DEC negative value -> magnitude
        tok.remove_prefix(1);
    } else if (tok.front() == '+') {
        tok.remove_prefix(1);
    }
    if (tok.empty()) return false;

    uint64_t acc = 0;
    for (char c : tok) {
        int d = -1;
        if (radix == 2 && (c == '0' || c == '1')) d = c - '0';
        else if (radix == 8 && c >= '0' && c <= '7') d = c - '0';
        else if (radix == 10 && c >= '0' && c <= '9') d = c - '0';
        else if (radix == 16) d = ReaderText::HexVal(c);
        if (d < 0) return false;
        acc = acc * static_cast<uint64_t>(radix) + static_cast<uint64_t>(d);
    }
    outVal = acc;
    return true;
}

inline std::string StripMifComments(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    size_t i = 0;
    while (i < raw.size()) {
        if (raw[i] == '%') {
            ++i;
            while (i < raw.size() && raw[i] != '%') ++i;
            if (i < raw.size()) ++i;
            out.push_back(' ');
        } else if (i + 1 < raw.size() && raw[i] == '-' && raw[i + 1] == '-') {
            i += 2;
            while (i < raw.size() && raw[i] != '\n') ++i;
        } else {
            out.push_back(raw[i++]);
        }
    }
    return out;
}

} // namespace DetailQuartusMif

inline bool ParseQuartusMemoryMif(
    const uint8_t* data,
    size_t size,
    ParsedEdaBoard& out)
{
    if (!data || size < 24 || size > 64u * 1024u * 1024u) return false;
    std::string_view rawSv = ReaderText::StripUtf8Bom(std::string_view(reinterpret_cast<const char*>(data), size));
    if (rawSv.find('\0') != std::string_view::npos) return false;

    const std::string clean = DetailQuartusMif::StripMifComments(rawSv);
    std::string_view sv(clean);

    // Find CONTENT ... BEGIN ... END
    size_t contentPos = std::string_view::npos;
    for (size_t i = 0; i + 7 <= sv.size(); ++i) {
        if (ReaderText::AsciiEqCI(sv.substr(i, 7), "CONTENT") &&
            (i == 0 || !std::isalnum(static_cast<unsigned char>(sv[i - 1]))) &&
            (i + 7 == sv.size() || !std::isalnum(static_cast<unsigned char>(sv[i + 7])))) {
            contentPos = i;
            break;
        }
    }
    if (contentPos == std::string_view::npos) return false;

    // Parse header key = value; pairs before CONTENT
    uint64_t depth = 0;
    uint32_t width = 0;
    int addrRadix = 16;
    int dataRadix = 16;

    std::string_view hdrPart = sv.substr(0, contentPos);
    size_t hp = 0;
    while (hp < hdrPart.size()) {
        const size_t semi = hdrPart.find(';', hp);
        std::string_view stmt = (semi == std::string_view::npos)
            ? hdrPart.substr(hp)
            : hdrPart.substr(hp, semi - hp);
        hp = (semi == std::string_view::npos) ? hdrPart.size() : (semi + 1u);
        stmt = ReaderText::TrimSv(stmt);
        if (stmt.empty()) continue;

        const size_t eq = stmt.find('=');
        if (eq == std::string_view::npos) return false;
        std::string_view key = ReaderText::TrimSv(stmt.substr(0, eq));
        std::string_view val = ReaderText::TrimSv(stmt.substr(eq + 1));

        if (ReaderText::AsciiEqCI(key, "DEPTH")) {
            if (!ReaderText::ParseUint64Sv(val, depth, 10) || depth == 0 || depth > 16777216ULL) return false;
        } else if (ReaderText::AsciiEqCI(key, "WIDTH")) {
            uint64_t w = 0;
            if (!ReaderText::ParseUint64Sv(val, w, 10) || w == 0 || w > 256ULL) return false;
            width = static_cast<uint32_t>(w);
        } else if (ReaderText::AsciiEqCI(key, "ADDRESS_RADIX")) {
            addrRadix = DetailQuartusMif::RadixFromKeyword(val);
            if (addrRadix < 0) return false;
        } else if (ReaderText::AsciiEqCI(key, "DATA_RADIX")) {
            dataRadix = DetailQuartusMif::RadixFromKeyword(val);
            if (dataRadix < 0) return false;
        }
    }
    if (depth == 0 || width == 0) return false;

    // Locate BEGIN and END inside CONTENT section
    std::string_view bodyPart = sv.substr(contentPos + 7);
    size_t beginOff = std::string_view::npos;
    for (size_t i = 0; i + 5 <= bodyPart.size(); ++i) {
        if (ReaderText::AsciiEqCI(bodyPart.substr(i, 5), "BEGIN") &&
            (i == 0 || !std::isalnum(static_cast<unsigned char>(bodyPart[i - 1]))) &&
            (i + 5 == bodyPart.size() || !std::isalnum(static_cast<unsigned char>(bodyPart[i + 5])))) {
            beginOff = i + 5;
            break;
        }
    }
    if (beginOff == std::string_view::npos) return false;

    std::string_view entriesPart = bodyPart.substr(beginOff);
    const size_t allocDepth = static_cast<size_t>(std::min<uint64_t>(depth, 1048576ULL));
    std::vector<uint64_t> mem(allocDepth, 0);
    std::vector<uint8_t> written(allocDepth, 0);
    bool sawEnd = false;
    size_t writtenCount = 0;

    size_t ep = 0;
    while (ep < entriesPart.size()) {
        const size_t semi = entriesPart.find(';', ep);
        std::string_view stmt = (semi == std::string_view::npos)
            ? entriesPart.substr(ep)
            : entriesPart.substr(ep, semi - ep);
        ep = (semi == std::string_view::npos) ? entriesPart.size() : (semi + 1u);
        stmt = ReaderText::TrimSv(stmt);
        if (stmt.empty()) continue;

        if (ReaderText::AsciiEqCI(stmt, "END")) {
            sawEnd = true;
            break;
        }

        const size_t colon = stmt.find(':');
        if (colon == std::string_view::npos) return false;
        std::string_view lhs = ReaderText::TrimSv(stmt.substr(0, colon));
        std::string_view rhs = ReaderText::TrimSv(stmt.substr(colon + 1));

        uint64_t a0 = 0, a1 = 0;
        bool isRange = false;
        if (lhs.size() >= 6 && lhs.front() == '[' && lhs.back() == ']') {
            std::string_view inner = ReaderText::TrimSv(lhs.substr(1, lhs.size() - 2));
            const size_t dots = inner.find("..");
            if (dots == std::string_view::npos) return false;
            if (!DetailQuartusMif::ParseMifUint(inner.substr(0, dots), addrRadix, a0) ||
                !DetailQuartusMif::ParseMifUint(inner.substr(dots + 2), addrRadix, a1) || a1 < a0) {
                return false;
            }
            isRange = true;
        } else {
            if (!DetailQuartusMif::ParseMifUint(lhs, addrRadix, a0)) return false;
            a1 = a0;
        }
        if (a1 >= depth) return false;

        std::vector<uint64_t> vals;
        std::string_view rem = rhs, vt;
        while (ReaderText::NextTokenSv(rem, vt)) {
            uint64_t dv = 0;
            if (!DetailQuartusMif::ParseMifUint(vt, dataRadix, dv)) return false;
            vals.push_back(dv);
        }
        if (vals.empty()) return false;

        if (isRange) {
            const uint64_t lim = std::min<uint64_t>(a1, allocDepth - 1u);
            for (uint64_t a = a0; a <= lim; ++a) {
                mem[static_cast<size_t>(a)] = vals[(a - a0) % vals.size()];
                if (!written[static_cast<size_t>(a)]) { written[static_cast<size_t>(a)] = 1; ++writtenCount; }
            }
        } else {
            for (size_t k = 0; k < vals.size(); ++k) {
                const uint64_t a = a0 + k;
                if (a >= depth) return false;
                if (a < allocDepth) {
                    mem[static_cast<size_t>(a)] = vals[k];
                    if (!written[static_cast<size_t>(a)]) { written[static_cast<size_t>(a)] = 1; ++writtenCount; }
                }
            }
        }
    }

    if (!sawEnd || writtenCount == 0) return false;

    uint64_t maxVal = 1;
    size_t nonZero = 0;
    for (size_t i = 0; i < allocDepth; ++i) {
        maxVal = std::max(maxVal, mem[i]);
        if (mem[i] != 0) ++nonZero;
    }

    FirmwareMemoryMapEda::MemorySpan sp;
    sp.startAddr = 0;
    sp.wordCount = depth;
    sp.bitsPerWord = width;
    sp.fillFraction = static_cast<double>(nonZero) / static_cast<double>(allocDepth);

    const size_t step = std::max<size_t>(1u, (allocDepth + 19u) / 20u);
    for (size_t off = 0; off < allocDepth && sp.rowLevels.size() < 20; off += step) {
        const size_t cnt = std::min(step, allocDepth - off);
        double sum = 0.0;
        for (size_t k = 0; k < cnt; ++k) sum += static_cast<double>(mem[off + k]) / static_cast<double>(maxVal);
        sp.rowLevels.push_back(static_cast<float>(sum / static_cast<double>(cnt)));
    }

    wchar_t title[160] = {};
    swprintf_s(title, L"Quartus MIF ROM (%llu x %u-bit, %zu initialized)",
        static_cast<unsigned long long>(depth), width, writtenCount);
    return FirmwareMemoryMapEda::EmitMemoryMapBoard(title, L"ALTERA_M9K_ROM", { sp }, false, 0, out);
}

} // namespace Formats
