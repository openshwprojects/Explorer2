#pragma once
// TektronixHexFirmware.h — Tektronix Extended Hexadecimal Firmware Object (.tek) reader
// Parses '%' Extended TekHex records (Type '6' data, Type '3' symbol, Type '8' entry/EOF) with
// strict Tektronix 6-bit character checksum verification across ALL record types (including Type '3'
// symbol records), rendering memory spans & symbols via FirmwareMemoryMapEda.

namespace Formats {

inline bool ParseTektronixHexFirmware(
    const uint8_t* data,
    size_t size,
    ParsedEdaBoard& outBoard)
{
    if (!data || size < 11 || size > 64u * 1024u * 1024u) return false;
    std::string_view sv = ReaderText::StripUtf8Bom(std::string_view(reinterpret_cast<const char*>(data), size));
    if (sv.find('\0') != std::string_view::npos) return false;

    auto hexNib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };

    // Tektronix Extended Hex 6-bit character value table (used for checksum summation over Type '3' symbols & hex records):
    //   '0'..'9' -> 0..9
    //   'A'..'Z' -> 10..35
    //   '$'      -> 36
    //   '%'      -> 37
    //   '.'      -> 38
    //   '_'      -> 39
    //   'a'..'z' -> 40..65
    auto tekCharVal = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'Z') return (c - 'A') + 10;
        if (c == '$') return 36;
        if (c == '%') return 37;
        if (c == '.') return 38;
        if (c == '_') return 39;
        if (c >= 'a' && c <= 'z') return (c - 'a') + 40;
        return -1;
    };

    std::map<uint64_t, uint8_t> memBytes;
    std::vector<FirmwareMemoryMapEda::MemorySymbol> symbols;
    bool sawTermination = false;
    bool hasEntry = false;
    uint64_t entryAddr = 0;
    size_t recordCount = 0;

    size_t pos = 0;
    std::string_view line;
    while (ReaderText::NextLineSv(sv, pos, line)) {
        line = ReaderText::TrimSv(line);
        if (line.empty() || line[0] == ';') continue;
        if (line[0] != '%') return false;
        if (line.size() < 7u) return false;

        const int lHi = hexNib(line[1]), lLo = hexNib(line[2]);
        const char recType = line[3];
        const int cHi = hexNib(line[4]), cLo = hexNib(line[5]);
        if (lHi < 0 || lLo < 0 || cHi < 0 || cLo < 0) return false;
        if (recType != '3' && recType != '6' && recType != '8') return false;

        const size_t recLen = static_cast<size_t>((lHi << 4) | lLo);
        const uint8_t expectedCsum = static_cast<uint8_t>((cHi << 4) | cLo);
        if (recLen < 6u || line.size() != 1u + recLen) return false;

        // Verify Tektronix Extended Hex checksum across all characters after '%', excluding checksum positions [4..5]
        uint32_t charSum = 0;
        for (size_t i = 1u; i < line.size(); ++i) {
            if (i == 4u || i == 5u) continue;
            const int v = (recType == '3') ? tekCharVal(line[i]) : hexNib(line[i]);
            if (v < 0) return false;
            charSum += static_cast<uint32_t>(v);
        }
        if (static_cast<uint8_t>(charSum & 0xFFu) != expectedCsum) {
            return false;
        }

        if (recType == '3') {
            // Type '3' symbol record: %LL3CC<secLenHex><secName><symBlock...>
            // Extract section / symbol definitions `[type:1][len:1][name:len][addrLen:1][addr:addrLen]` when present
            const int secNameLenRaw = hexNib(line[6]);
            if (secNameLenRaw >= 0) {
                const size_t secNameLen = (secNameLenRaw == 0) ? 16u : static_cast<size_t>(secNameLenRaw);
                size_t sp = 7u + secNameLen;
                while (sp + 3u < line.size() && symbols.size() < 128u) {
                    const int symFlag = hexNib(line[sp]);
                    const int symLenRaw = hexNib(line[sp + 1u]);
                    if (symFlag < 0 || symLenRaw < 0) break;
                    const size_t symLen = (symLenRaw == 0) ? 16u : static_cast<size_t>(symLenRaw);
                    sp += 2u;
                    if (sp + symLen + 1u > line.size()) break;
                    std::string symName(line.substr(sp, symLen));
                    sp += symLen;
                    const int valDigitsRaw = hexNib(line[sp++]);
                    if (valDigitsRaw < 0) break;
                    const size_t valDigits = (valDigitsRaw == 0) ? 16u : static_cast<size_t>(valDigitsRaw);
                    if (sp + valDigits > line.size()) break;
                    uint64_t symVal = 0;
                    bool okVal = true;
                    for (size_t k = 0; k < valDigits; ++k) {
                        const int d = hexNib(line[sp + k]);
                        if (d < 0) { okVal = false; break; }
                        symVal = (symVal << 4) | static_cast<uint64_t>(d);
                    }
                    sp += valDigits;
                    if (okVal && !symName.empty()) {
                        symbols.push_back({ symVal, std::move(symName) });
                    }
                }
            }
            ++recordCount;
            continue;
        }

        const int rawAddrDigits = hexNib(line[6]);
        if (rawAddrDigits < 0) return false;
        const size_t addrDigits = (rawAddrDigits == 0) ? 16u : static_cast<size_t>(rawAddrDigits);
        if (6u + addrDigits > recLen) return false;

        uint64_t addr = 0;
        for (size_t k = 0; k < addrDigits; ++k) {
            const int d = hexNib(line[7u + k]);
            if (d < 0) return false;
            addr = (addr << 4) | static_cast<uint64_t>(d);
        }

        const size_t dataStart = 7u + addrDigits;
        const size_t dataHexChars = line.size() - dataStart;

        if (recType == '8') {
            if (dataHexChars != 0u) return false;
            sawTermination = true;
            hasEntry = true;
            entryAddr = addr;
            ++recordCount;
            break;
        }

        // Type '6' data record
        if (dataHexChars < 2u || (dataHexChars & 1u) != 0u) return false;
        const size_t byteCount = dataHexChars / 2u;
        for (size_t b = 0; b < byteCount; ++b) {
            const int hi = hexNib(line[dataStart + b * 2u + 0u]);
            const int lo = hexNib(line[dataStart + b * 2u + 1u]);
            if (hi < 0 || lo < 0) return false;
            memBytes[addr + b] = static_cast<uint8_t>((hi << 4) | lo);
            if (memBytes.size() > 2000000u) return false;
        }
        ++recordCount;
    }

    if (recordCount == 0 || memBytes.empty() || !sawTermination) return false;

    std::vector<FirmwareMemoryMapEda::MemorySpan> spans;
    FirmwareMemoryMapEda::BuildByteSpansFromMap(memBytes, spans);

    return FirmwareMemoryMapEda::EmitMemoryMapBoard(
        L"Tektronix Extended Hex Firmware (.tek)",
        L"TEKHEX_ROM",
        spans,
        hasEntry,
        entryAddr,
        outBoard,
        &symbols);
}

} // namespace Formats
