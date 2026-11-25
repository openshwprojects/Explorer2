#pragma once
// TiTxtFirmware.h — Texas Instruments TI-TXT (.titxt) MSP430 / C2000 Firmware Hex Image reader
// Strictly validates @ADDR hex section headers, 2-digit hex data bytes, and mandatory 'q' EOF
// terminator, extracting MSP430 0xFFFE Reset Vector PC when present.

namespace Formats {

inline bool ParseTiTxtFirmware(
    const uint8_t* data,
    size_t size,
    ParsedEdaBoard& out)
{
    if (!data || size < 6 || size > 64u * 1024u * 1024u) return false;
    std::string_view sv = ReaderText::StripUtf8Bom(std::string_view(reinterpret_cast<const char*>(data), size));
    if (sv.find('\0') != std::string_view::npos) return false;

    struct RawSec {
        uint64_t startAddr = 0;
        std::vector<uint8_t> bytes;
    };
    std::vector<RawSec> secs;
    bool inSection = false;
    bool sawQuit = false;

    size_t pos = 0;
    std::string_view line;
    while (ReaderText::NextLineSv(sv, pos, line)) {
        line = ReaderText::TrimSv(line);
        if (line.empty()) continue;

        if (line == "q" || line == "Q") {
            sawQuit = true;
            break;
        }

        if (line.front() == '@') {
            std::string_view hexAddr = line.substr(1);
            if (hexAddr.empty() || hexAddr.size() > 8) return false;
            for (char c : hexAddr) {
                if (ReaderText::HexVal(c) < 0) return false;
            }
            uint64_t addr = 0;
            if (!ReaderText::ParseUint64Sv(hexAddr, addr, 16)) return false;
            secs.push_back({ addr, {} });
            inSection = true;
            continue;
        }

        if (!inSection) return false;

        std::string_view rem = line, tok;
        size_t lineByteCount = 0;
        while (ReaderText::NextTokenSv(rem, tok)) {
            if (tok.size() != 2) return false;
            const int hi = ReaderText::HexVal(tok[0]);
            const int lo = ReaderText::HexVal(tok[1]);
            if (hi < 0 || lo < 0) return false;
            secs.back().bytes.push_back(static_cast<uint8_t>((hi << 4) | lo));
            ++lineByteCount;
            if (secs.back().bytes.size() > 16u * 1024u * 1024u) return false;
        }
        if (lineByteCount == 0 || lineByteCount > 64) return false;
    }

    if (!sawQuit || secs.empty()) return false;

    std::sort(secs.begin(), secs.end(), [](const RawSec& a, const RawSec& b) {
        return a.startAddr < b.startAddr;
    });

    bool hasResetVec = false;
    uint64_t resetPc = 0;
    uint64_t totalBytes = 0;
    std::vector<FirmwareMemoryMapEda::MemorySpan> spans;

    for (const auto& s : secs) {
        if (s.bytes.empty()) continue;
        totalBytes += s.bytes.size();

        // Check MSP430 Reset Vector at 0xFFFE..0xFFFF
        if (s.startAddr <= 0xFFFEULL && s.startAddr + s.bytes.size() >= 0x10000ULL) {
            const size_t o = static_cast<size_t>(0xFFFEULL - s.startAddr);
            resetPc = static_cast<uint64_t>(s.bytes[o]) | (static_cast<uint64_t>(s.bytes[o + 1]) << 8);
            hasResetVec = true;
        }

        FirmwareMemoryMapEda::MemorySpan sp;
        sp.startAddr = s.startAddr;
        sp.wordCount = s.bytes.size();
        sp.bitsPerWord = 8;

        size_t nonFF = 0;
        for (uint8_t b : s.bytes) if (b != 0xFFu) ++nonFF;
        sp.fillFraction = static_cast<double>(nonFF) / static_cast<double>(s.bytes.size());

        const size_t chunkSz = std::max<size_t>(16u, (s.bytes.size() + 15u) / 16u);
        for (size_t off = 0; off < s.bytes.size() && sp.rowLevels.size() < 20; off += chunkSz) {
            const size_t n = std::min(chunkSz, s.bytes.size() - off);
            size_t active = 0;
            for (size_t k = 0; k < n; ++k) if (s.bytes[off + k] != 0xFFu) ++active;
            sp.rowLevels.push_back(static_cast<float>(active) / static_cast<float>(n));
        }
        spans.push_back(std::move(sp));
    }

    if (spans.empty() || totalBytes == 0) return false;

    wchar_t title[160] = {};
    swprintf_s(title, L"TI-TXT MCU Firmware (%llu bytes, %zu section%ls)",
        static_cast<unsigned long long>(totalBytes),
        spans.size(),
        spans.size() == 1 ? L"" : L"s");
    return FirmwareMemoryMapEda::EmitMemoryMapBoard(title, L"MSP430_FLASH", spans, hasResetVec, resetPc, out);
}

} // namespace Formats
