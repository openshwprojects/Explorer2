#pragma once
// VerilogReadmemh.h — IEEE 1364 / 1800 Verilog $readmemh & Xilinx updatemem BRAM Image (.mem) reader
// Parses // and /* */ comments, @<hex_addr> origin directives, and hex data words (with '_' separators),
// rendering contiguous memory spans via FirmwareMemoryMapEda.

namespace Formats {

inline bool ParseVerilogReadmemh(
    const uint8_t* data,
    size_t size,
    ParsedEdaBoard& outBoard)
{
    if (!data || size < 6 || size > 64u * 1024u * 1024u) return false;
    std::string_view sv = ReaderText::StripUtf8Bom(std::string_view(reinterpret_cast<const char*>(data), size));
    if (sv.find('\0') != std::string_view::npos) return false;

    // Strip // and /* ... */ comments into a clean tokenizable buffer
    std::string clean;
    clean.reserve(sv.size());
    for (size_t i = 0; i < sv.size(); ) {
        if (i + 1u < sv.size() && sv[i] == '/' && sv[i + 1u] == '/') {
            i += 2u;
            while (i < sv.size() && sv[i] != '\n') ++i;
            continue;
        }
        if (i + 1u < sv.size() && sv[i] == '/' && sv[i + 1u] == '*') {
            i += 2u;
            bool closed = false;
            while (i + 1u < sv.size()) {
                if (sv[i] == '*' && sv[i + 1u] == '/') {
                    i += 2u;
                    closed = true;
                    break;
                }
                ++i;
            }
            if (!closed) return false;
            clean.push_back(' ');
            continue;
        }
        const char ch = sv[i++];
        clean.push_back((ch == '\r' || ch == '\n') ? ' ' : ch);
    }

    auto parseHex64 = [](std::string_view tok, uint64_t& outVal, size_t& outNibbles) -> bool {
        if (tok.empty() || tok.front() == '_' || tok.back() == '_') return false;
        uint64_t val = 0;
        size_t nibs = 0;
        for (char c : tok) {
            if (c == '_') continue;
            int d = -1;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else return false;
            if (nibs < 16u) {
                val = (val << 4) | static_cast<uint64_t>(d);
            }
            ++nibs;
            if (nibs > 32u) return false;
        }
        if (nibs == 0u) return false;
        outVal = val;
        outNibbles = nibs;
        return true;
    };

    std::map<uint64_t, uint64_t> words;
    uint64_t curAddr = 0;
    bool sawAtAddr = false;
    size_t maxWordNibbles = 2u;

    std::string_view rem(clean);
    std::string_view tok;
    while (ReaderText::NextTokenSv(rem, tok)) {
        if (tok[0] == '@') {
            uint64_t newAddr = 0;
            size_t addrNibs = 0;
            if (!parseHex64(tok.substr(1u), newAddr, addrNibs) || addrNibs > 16u) return false;
            curAddr = newAddr;
            sawAtAddr = true;
        } else {
            uint64_t wVal = 0;
            size_t wNibs = 0;
            if (!parseHex64(tok, wVal, wNibs)) return false;
            maxWordNibbles = std::max(maxWordNibbles, wNibs);
            words[curAddr++] = wVal;
            if (words.size() > 2000000u) return false;
        }
    }

    if (!sawAtAddr || words.empty()) return false;

    uint32_t bitsPerWord = 8u;
    if (maxWordNibbles > 8u)       bitsPerWord = 64u;
    else if (maxWordNibbles > 4u)  bitsPerWord = 32u;
    else if (maxWordNibbles > 2u)  bitsPerWord = 16u;

    const double maxValNorm = (bitsPerWord == 64u) ? static_cast<double>(UINT64_MAX)
                            : static_cast<double>((1ULL << bitsPerWord) - 1ULL);

    std::vector<FirmwareMemoryMapEda::MemorySpan> spans;
    auto it = words.begin();
    while (it != words.end()) {
        const uint64_t startA = it->first;
        std::vector<uint64_t> seq;
        uint64_t a = startA;
        while (it != words.end() && it->first == a) {
            seq.push_back(it->second);
            ++a;
            ++it;
        }

        FirmwareMemoryMapEda::MemorySpan sp;
        sp.startAddr = startA;
        sp.wordCount = seq.size();
        sp.bitsPerWord = bitsPerWord;
        size_t nonZero = 0;
        for (uint64_t w : seq) if (w != 0ULL) ++nonZero;
        sp.fillFraction = seq.empty() ? 0.0 : (static_cast<double>(nonZero) / static_cast<double>(seq.size()));

        const size_t bins = std::clamp<size_t>((seq.size() + 3u) / 4u, 2u, 16u);
        sp.rowLevels.resize(bins, 0.1f);
        for (size_t bi = 0; bi < bins; ++bi) {
            const size_t i0 = (bi * seq.size()) / bins;
            const size_t i1 = std::max(i0 + 1u, ((bi + 1u) * seq.size()) / bins);
            double sum = 0.0;
            for (size_t k = i0; k < i1 && k < seq.size(); ++k) {
                sum += static_cast<double>(seq[k]) / maxValNorm;
            }
            sp.rowLevels[bi] = static_cast<float>(sum / static_cast<double>(i1 - i0));
        }
        spans.push_back(std::move(sp));
    }

    const bool hasBootZero = (words.find(0ULL) != words.end());
    return FirmwareMemoryMapEda::EmitMemoryMapBoard(
        L"Verilog $readmemh / Vivado BRAM (.mem)",
        L"VERILOG_MEM",
        spans,
        hasBootZero,
        0ULL,
        outBoard);
}

} // namespace Formats
