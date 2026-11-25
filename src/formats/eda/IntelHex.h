#pragma once
// IntelHex.h — Intel HEX 8/16/32-Bit Firmware Image (.hex / .ihex) Reader
// Validates ':' record headers, two's-complement byte checksums, extended segment/linear addressing
// (types 00..05), and entry-point records (03/05), rendering memory regions via FirmwareMemoryMapEda.h.
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

inline bool ParseIntelHexBoard(std::string_view sv, ParsedEdaBoard& outBoard) {
    sv = ReaderText::StripUtf8Bom(sv);
    if (sv.size() < 11 || sv.size() > (64u << 20)) return false;
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
    uint64_t upperBase = 0ULL;
    bool hasEntry = false;
    uint64_t entryAddr = 0ULL;
    bool sawEof = false;
    size_t validRecords = 0;

    size_t pos = 0;
    std::string_view line;
    while (ReaderText::NextLineSv(sv, pos, line)) {
        line = ReaderText::TrimSv(line);
        if (line.empty()) continue;
        if (line[0] != ':') return false;
        if (line.size() < 11u || ((line.size() - 1u) & 1u) != 0u) return false;

        uint8_t byteCount = 0, addrHi = 0, addrLo = 0, recType = 0;
        if (!hexByte(line, 1u, byteCount) ||
            !hexByte(line, 3u, addrHi) ||
            !hexByte(line, 5u, addrLo) ||
            !hexByte(line, 7u, recType)) {
            return false;
        }
        if (line.size() != 11u + static_cast<size_t>(byteCount) * 2u) return false;

        // Verify two's-complement modulo-256 byte checksum across entire record == 0
        uint32_t sum = 0;
        const size_t totalRecBytes = (line.size() - 1u) / 2u;
        for (size_t b = 0; b < totalRecBytes; ++b) {
            uint8_t v = 0;
            if (!hexByte(line, 1u + b * 2u, v)) return false;
            sum += v;
        }
        if ((sum & 0xFFu) != 0u) return false;

        const uint16_t offset16 = static_cast<uint16_t>((static_cast<uint16_t>(addrHi) << 8) | addrLo);

        if (recType == 0x00u) { // Data record
            for (uint8_t i = 0; i < byteCount; ++i) {
                uint8_t db = 0;
                hexByte(line, 9u + static_cast<size_t>(i) * 2u, db);
                memBytes[upperBase + static_cast<uint64_t>(offset16) + i] = db;
                if (memBytes.size() > 4000000u) return false;
            }
        } else if (recType == 0x01u) { // End of File
            if (byteCount != 0u) return false;
            sawEof = true;
            ++validRecords;
            break;
        } else if (recType == 0x02u) { // Extended Segment Address (16-bit << 4)
            if (byteCount != 2u) return false;
            uint8_t s0 = 0, s1 = 0;
            hexByte(line, 9u, s0); hexByte(line, 11u, s1);
            upperBase = (static_cast<uint64_t>((static_cast<uint16_t>(s0) << 8) | s1)) << 4;
        } else if (recType == 0x04u) { // Extended Linear Address (16-bit << 16)
            if (byteCount != 2u) return false;
            uint8_t u0 = 0, u1 = 0;
            hexByte(line, 9u, u0); hexByte(line, 11u, u1);
            upperBase = (static_cast<uint64_t>((static_cast<uint16_t>(u0) << 8) | u1)) << 16;
        } else if (recType == 0x03u) { // Start Segment Address (CS:IP)
            if (byteCount != 4u) return false;
            uint8_t b0 = 0, b1 = 0, b2 = 0, b3 = 0;
            hexByte(line, 9u, b0); hexByte(line, 11u, b1); hexByte(line, 13u, b2); hexByte(line, 15u, b3);
            const uint64_t cs = (static_cast<uint64_t>(b0) << 8) | b1;
            const uint64_t ip = (static_cast<uint64_t>(b2) << 8) | b3;
            entryAddr = (cs << 4) + ip;
            hasEntry = true;
        } else if (recType == 0x05u) { // Start Linear Address (EIP)
            if (byteCount != 4u) return false;
            uint8_t b0 = 0, b1 = 0, b2 = 0, b3 = 0;
            hexByte(line, 9u, b0); hexByte(line, 11u, b1); hexByte(line, 13u, b2); hexByte(line, 15u, b3);
            entryAddr = (static_cast<uint64_t>(b0) << 24) | (static_cast<uint64_t>(b1) << 16) |
                        (static_cast<uint64_t>(b2) << 8)  | static_cast<uint64_t>(b3);
            hasEntry = true;
        } else {
            return false;
        }

        ++validRecords;
    }

    if (validRecords == 0 || memBytes.empty() || !sawEof) return false;

    std::vector<FirmwareMemoryMapEda::MemorySpan> spans;
    FirmwareMemoryMapEda::BuildByteSpansFromMap(memBytes, spans);

    wchar_t titleBuf[180] = {};
    swprintf_s(titleBuf, L"Intel HEX Firmware Image (%zu bytes, %zu region%ls)",
               memBytes.size(), spans.size(), spans.size() == 1u ? L"" : L"s");
    return FirmwareMemoryMapEda::EmitMemoryMapBoard(
        titleBuf, L"INTEL_HEX_ROM", spans, hasEntry, entryAddr, outBoard);
}

} // namespace Formats
