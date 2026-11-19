#pragma once
// IntelHex.h — Intel HEX Firmware Image (.hex / .ihex) Checksum-Verified 32-Bit Memory Map Parser

namespace Formats {

inline bool ParseIntelHexBoard(std::string_view sv, ParsedEdaBoard& out) {
    if (sv.empty() || sv.find(':') == std::string_view::npos) return false;

    auto hexNib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    auto rdHexByte = [&](std::string_view s, size_t off, uint8_t& outB) -> bool {
        if (off + 2 > s.size()) return false;
        int hi = hexNib(s[off]);
        int lo = hexNib(s[off + 1]);
        if (hi < 0 || lo < 0) return false;
        outB = static_cast<uint8_t>((hi << 4) | lo);
        return true;
    };

    struct DataChunk {
        uint64_t addr = 0;
        uint32_t len = 0;
        uint32_t nonFF = 0;
    };
    std::vector<DataChunk> chunks;
    chunks.reserve(512);

    uint64_t extBase = 0;
    uint32_t entryAddr = 0;
    bool hasEntry = false;
    bool seenEof = false;
    size_t totalBytes = 0;

    size_t pos = 0;
    while (pos < sv.size() && !seenEof) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;

        while (!ln.empty() && static_cast<unsigned char>(ln.front()) <= 0x20u) ln.remove_prefix(1);
        while (!ln.empty() && static_cast<unsigned char>(ln.back()) <= 0x20u) ln.remove_suffix(1);
        if (ln.empty()) continue;

        if (ln[0] != ':' || ln.size() < 11 || ((ln.size() - 1u) & 1u) != 0u) {
            return false;
        }

        uint8_t byteLen = 0, addrHi = 0, addrLo = 0, recType = 0;
        if (!rdHexByte(ln, 1, byteLen) ||
            !rdHexByte(ln, 3, addrHi)  ||
            !rdHexByte(ln, 5, addrLo)  ||
            !rdHexByte(ln, 7, recType)) {
            return false;
        }

        const size_t expectedChars = 11u + static_cast<size_t>(byteLen) * 2u;
        if (ln.size() != expectedChars) return false;

        uint32_t sum = static_cast<uint32_t>(byteLen) + addrHi + addrLo + recType;
        uint8_t payload[256] = {};
        for (uint32_t i = 0; i < byteLen; ++i) {
            if (!rdHexByte(ln, 9u + static_cast<size_t>(i) * 2u, payload[i])) return false;
            sum += payload[i];
        }
        uint8_t cksum = 0;
        if (!rdHexByte(ln, 9u + static_cast<size_t>(byteLen) * 2u, cksum)) return false;
        sum += cksum;

        // Strict Intel HEX two's-complement checksum validation: (sum mod 256) == 0
        if ((sum & 0xFFu) != 0u) {
            return false;
        }

        const uint32_t addr16 = (static_cast<uint32_t>(addrHi) << 8) | addrLo;
        switch (recType) {
        case 0x00: { // Data Record
            if (byteLen > 0) {
                uint32_t nonFF = 0;
                for (uint32_t i = 0; i < byteLen; ++i) {
                    if (payload[i] != 0xFFu) ++nonFF;
                }
                chunks.push_back({ extBase + addr16, byteLen, nonFF });
                totalBytes += byteLen;
            }
            break;
        }
        case 0x01: // End Of File Record
            if (byteLen != 0) return false;
            seenEof = true;
            break;
        case 0x02: // Extended Segment Address (bits 4..19)
            if (byteLen != 2) return false;
            extBase = static_cast<uint64_t>((static_cast<uint32_t>(payload[0]) << 8) | payload[1]) << 4;
            break;
        case 0x03: // Start Segment Address (CS:IP)
            if (byteLen != 4) return false;
            entryAddr = (((static_cast<uint32_t>(payload[0]) << 8) | payload[1]) << 4) +
                        ((static_cast<uint32_t>(payload[2]) << 8) | payload[3]);
            hasEntry = true;
            break;
        case 0x04: // Extended Linear Address (upper 16 bits of 32-bit address)
            if (byteLen != 2) return false;
            extBase = static_cast<uint64_t>((static_cast<uint32_t>(payload[0]) << 8) | payload[1]) << 16;
            break;
        case 0x05: // Start Linear Address (EIP / Reset Vector)
            if (byteLen != 4) return false;
            entryAddr = (static_cast<uint32_t>(payload[0]) << 24) |
                        (static_cast<uint32_t>(payload[1]) << 16) |
                        (static_cast<uint32_t>(payload[2]) << 8)  |
                        static_cast<uint32_t>(payload[3]);
            hasEntry = true;
            break;
        default:
            return false;
        }
    }

    if (totalBytes == 0 || chunks.empty()) return false;

    std::sort(chunks.begin(), chunks.end(), [](const DataChunk& a, const DataChunk& b) {
        return a.addr < b.addr;
    });

    struct MemoryRegion {
        uint64_t startAddr = 0;
        uint64_t endAddr = 0;
        size_t byteCount = 0;
        size_t nonFFCount = 0;
        std::vector<float> rowRatios;
    };
    std::vector<MemoryRegion> regions;
    for (const auto& ch : chunks) {
        float chRatio = static_cast<float>(ch.nonFF) / static_cast<float>(std::max<uint32_t>(1u, ch.len));
        if (!regions.empty() && ch.addr <= regions.back().endAddr + 64ULL) {
            auto& rg = regions.back();
            rg.endAddr = std::max(rg.endAddr, ch.addr + ch.len);
            rg.byteCount += ch.len;
            rg.nonFFCount += ch.nonFF;
            if (rg.rowRatios.size() < 24) rg.rowRatios.push_back(chRatio);
        } else if (regions.size() < 24) {
            MemoryRegion rg;
            rg.startAddr = ch.addr;
            rg.endAddr = ch.addr + ch.len;
            rg.byteCount = ch.len;
            rg.nonFFCount = ch.nonFF;
            rg.rowRatios.push_back(chRatio);
            regions.push_back(std::move(rg));
        }
    }

    out.kind = EdaDocKind::GerberLayer;
    wchar_t titleBuf[160] = {};
    swprintf_s(titleBuf, L"Intel HEX Firmware Image (%zu bytes, %zu region%ls, 0x%08llX..0x%08llX)",
        totalBytes, regions.size(), regions.size() == 1 ? L"" : L"s",
        static_cast<unsigned long long>(regions.front().startAddr),
        static_cast<unsigned long long>(regions.back().endAddr - 1ULL));
    out.formatTitle = titleBuf;

    const float mapX0 = 0.0f;
    const float mapW  = 64.0f;
    float curY = 0.0f;

    out.labels.push_back({ mapX0, 5.2f, 1.15f, EdaLayerId::TopSilk, out.formatTitle });
    if (hasEntry) {
        wchar_t epBuf[64] = {};
        swprintf_s(epBuf, L"Entry Point (EIP): 0x%08X", entryAddr);
        out.labels.push_back({ mapX0 + mapW + 3.0f, 5.2f, 1.0f, EdaLayerId::InnerCopper, epBuf });
    }

    for (size_t ri = 0; ri < regions.size(); ++ri) {
        const auto& rg = regions[ri];
        const size_t nRows = std::clamp<size_t>(rg.rowRatios.size(), 2u, 16u);
        const float regH = std::clamp(static_cast<float>(nRows) * 2.2f + 2.5f, 7.0f, 36.0f);
        const float yTop = curY;
        const float yBot = curY - regH;

        // Region block outline
        out.traces.push_back({ mapX0,        yTop, mapX0 + mapW, yTop, 0.28f, EdaLayerId::TopSilk, static_cast<int>(ri + 1) });
        out.traces.push_back({ mapX0 + mapW, yTop, mapX0 + mapW, yBot, 0.28f, EdaLayerId::TopSilk, static_cast<int>(ri + 1) });
        out.traces.push_back({ mapX0 + mapW, yBot, mapX0,        yBot, 0.28f, EdaLayerId::TopSilk, static_cast<int>(ri + 1) });
        out.traces.push_back({ mapX0,        yBot, mapX0,        yTop, 0.28f, EdaLayerId::TopSilk, static_cast<int>(ri + 1) });

        // 16-byte row density bars inside region
        const float innerH = regH - 2.0f;
        const size_t bars = rg.rowRatios.size();
        for (size_t bi = 0; bi < bars; ++bi) {
            float ry = yTop - 1.2f - (bars > 1 ? (innerH * static_cast<float>(bi)) / static_cast<float>(bars) : innerH * 0.4f);
            float ratio = std::clamp(rg.rowRatios[bi], 0.08f, 1.0f);
            float barW = (mapW - 4.0f) * ratio;
            EdaLayerId barLayer = (ratio > 0.65f) ? EdaLayerId::TopCopper
                                : (ratio > 0.25f) ? EdaLayerId::InnerCopper
                                                  : EdaLayerId::BottomCopper;
            out.traces.push_back({ mapX0 + 2.0f, ry, mapX0 + 2.0f + barW, ry, 0.85f, barLayer, static_cast<int>(ri + 1) });
        }

        double fillPct = 100.0 * static_cast<double>(rg.nonFFCount) / static_cast<double>(std::max<size_t>(1u, rg.byteCount));
        wchar_t rgLbl[128] = {};
        if (rg.byteCount >= 1024u) {
            swprintf_s(rgLbl, L"0x%08llX..0x%08llX (%.2f KB, %.0f%% non-FF)",
                static_cast<unsigned long long>(rg.startAddr),
                static_cast<unsigned long long>(rg.endAddr - 1ULL),
                static_cast<double>(rg.byteCount) / 1024.0, fillPct);
        } else {
            swprintf_s(rgLbl, L"0x%08llX..0x%08llX (%zu B, %.0f%% non-FF)",
                static_cast<unsigned long long>(rg.startAddr),
                static_cast<unsigned long long>(rg.endAddr - 1ULL),
                rg.byteCount, fillPct);
        }
        out.labels.push_back({ mapX0 + mapW + 3.0f, (yTop + yBot) * 0.5f + 0.5f, 0.95f, EdaLayerId::TopSilk, rgLbl });

        if (hasEntry && entryAddr >= rg.startAddr && entryAddr < rg.endAddr) {
            double rel = static_cast<double>(entryAddr - rg.startAddr) /
                         static_cast<double>(std::max<uint64_t>(1ULL, rg.endAddr - rg.startAddr));
            float ey = yTop - static_cast<float>(rel) * regH;
            out.traces.push_back({ mapX0 - 6.0f, ey, mapX0 - 0.6f, ey, 0.40f, EdaLayerId::InnerCopper, 99 });
            out.circles.push_back({ mapX0 - 0.6f, ey, 0.7f, 0.25f, true, EdaLayerId::InnerCopper });
            out.labels.push_back({ mapX0 - 22.0f, ey + 0.5f, 0.9f, EdaLayerId::InnerCopper, L"ENTRY ->" });
        }

        EdaComponent3D segComp;
        wchar_t refBuf[32] = {};
        swprintf_s(refBuf, L"SEG_%zu", ri);
        segComp.refDes = refBuf;
        segComp.value = rgLbl;
        segComp.footprint = L"FLASH_REGION";
        segComp.x = mapX0 + mapW * 0.5f;
        segComp.y = (yTop + yBot) * 0.5f;
        segComp.widthMm = mapW;
        segComp.lengthMm = regH;
        segComp.category = EdaPartCategory::IC_Package;
        out.components.push_back(std::move(segComp));

        // Disjoint address gap indicator between non-contiguous segments
        if (ri + 1 < regions.size()) {
            uint64_t gap = regions[ri + 1].startAddr - rg.endAddr;
            float gapH = (gap >= 4096ULL) ? 6.0f : 3.0f;
            if (gap >= 4096ULL) {
                float gy = yBot - gapH * 0.5f;
                out.traces.push_back({ mapX0 + 8.0f, gy, mapX0 + mapW - 8.0f, gy, 0.14f, EdaLayerId::BottomSilk, 0 });
                wchar_t gapLbl[64] = {};
                swprintf_s(gapLbl, L"... gap +0x%llX ...", static_cast<unsigned long long>(gap));
                out.labels.push_back({ mapX0 + 18.0f, gy + 0.8f, 0.8f, EdaLayerId::BottomSilk, gapLbl });
            }
            curY = yBot - gapH;
        }
    }

    out.netCount = totalBytes;
    return !out.traces.empty();
}

} // namespace Formats
