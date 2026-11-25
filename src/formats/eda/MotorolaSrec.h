#pragma once
// MotorolaSrec.h — Motorola S-Record Firmware Image (.srec / .s19 / .mot) Reader
// Validates S0..S9 records, one's-complement byte checksums, 16/24/32-bit addresses (S1/S2/S3),
// S0 module header ASCII text, and S7/S8/S9 termination entry addresses via FirmwareMemoryMapEda.h.
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

inline bool ParseMotorolaSrecBoard(std::string_view sv, ParsedEdaBoard& outBoard) {
    sv = ReaderText::StripUtf8Bom(sv);
    if (sv.size() < 10 || sv.size() > (64u << 20)) return false;
    if (sv.find('\0') != std::string_view::npos) return false;

    auto hexByte = [](std::string_view s, size_t off, uint8_t& outB) -> bool {
        if (off + 2u > s.size()) return false;
        const int hi = ReaderText::HexVal(s[off]);
        const int lo = ReaderText::HexVal(s[off + 1u]);
        if (hi < 0 || lo < 0) return false;
        outB = static_cast<uint8_t>((hi << 4) | lo);
        return true;
    };

    std::map<uint64_t, uint8_t> memBytes;
    std::string headerText;
    bool hasEntry = false;
    uint64_t entryAddr = 0ULL;
    size_t dataRecords = 0;

    size_t pos = 0;
    std::string_view line;
    while (ReaderText::NextLineSv(sv, pos, line)) {
        line = ReaderText::TrimSv(line);
        if (line.empty()) continue;
        if (line.size() < 10u || (line[0] != 'S' && line[0] != 's')) return false;
        const char typeCh = line[1];
        if (typeCh < '0' || typeCh > '9' || typeCh == '4') return false;
        if ((line.size() & 1u) != 0u) return false;

        uint8_t byteCount = 0;
        if (!hexByte(line, 2u, byteCount) || byteCount < 3u) return false;
        if (line.size() != 4u + static_cast<size_t>(byteCount) * 2u) return false;

        // Verify Motorola S-record one's-complement checksum: `(sum(count + addr + data + csum) & 0xFF) == 0xFF`
        uint32_t sum = byteCount;
        for (size_t b = 0; b < byteCount; ++b) {
            uint8_t v = 0;
            if (!hexByte(line, 4u + b * 2u, v)) return false;
            sum += v;
        }
        if ((sum & 0xFFu) != 0xFFu) return false;

        const size_t addrBytes = (typeCh == '2' || typeCh == '6' || typeCh == '8') ? 3u
                               : (typeCh == '3' || typeCh == '7') ? 4u : 2u;
        if (byteCount < addrBytes + 1u) return false;

        uint64_t addr = 0ULL;
        for (size_t a = 0; a < addrBytes; ++a) {
            uint8_t ab = 0;
            hexByte(line, 4u + a * 2u, ab);
            addr = (addr << 8) | ab;
        }

        const size_t payloadBytes = static_cast<size_t>(byteCount) - addrBytes - 1u;
        const size_t payHexOff = 4u + addrBytes * 2u;

        if (typeCh == '0') {
            for (size_t i = 0; i < payloadBytes; ++i) {
                uint8_t ch = 0;
                hexByte(line, payHexOff + i * 2u, ch);
                if (ch >= 0x20u && ch <= 0x7Eu) headerText.push_back(static_cast<char>(ch));
            }
        } else if (typeCh == '1' || typeCh == '2' || typeCh == '3') {
            for (size_t i = 0; i < payloadBytes; ++i) {
                uint8_t db = 0;
                hexByte(line, payHexOff + i * 2u, db);
                memBytes[addr + i] = db;
                if (memBytes.size() > 4000000u) return false;
            }
            ++dataRecords;
        } else if (typeCh == '7' || typeCh == '8' || typeCh == '9') {
            hasEntry = true;
            entryAddr = addr;
        }
    }

    if (dataRecords == 0 || memBytes.empty()) return false;

    std::vector<FirmwareMemoryMapEda::MemorySpan> spans;
    FirmwareMemoryMapEda::BuildByteSpansFromMap(memBytes, spans);

    std::wstring title = L"Motorola S-Record Firmware";
    std::string_view hdrTrim = ReaderText::TrimSv(headerText);
    if (!hdrTrim.empty()) {
        title += L" (" + PathUtils::Utf8ToWide(hdrTrim) + L" — " + std::to_wstring(memBytes.size()) + L" bytes)";
    } else {
        title += L" (" + std::to_wstring(memBytes.size()) + L" bytes, " + std::to_wstring(spans.size()) + L" regions)";
    }
    return FirmwareMemoryMapEda::EmitMemoryMapBoard(
        title, L"SREC_ROM", spans, hasEntry, entryAddr, outBoard);
}

} // namespace Formats
