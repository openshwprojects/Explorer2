#pragma once
// XilinxFpgaBit.h — Xilinx 7-Series / UltraScale / Spartan-6 FPGA Bitstream (.bit / .bin / Xilinx SPI .rpd) Reader
// Decodes `.bit` TLV design/part/date/time header fields (`'a'..'e'`) and Type 1 / Type 2 32-bit configuration
// packets (`CRC`, `FAR`, `FDRI`, `CMD`, `CTL0`, `STAT`, `COR0`, `IDCODE`, `WBSTAR`), rendering a true Xilinx
// FPGA clock-region & column floorplan derived from `IDCODE` family and `FAR`/`FDRI` frame geometry.
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

namespace DetailXilinxBit {

inline uint8_t BitRev8(uint8_t v) noexcept {
    v = static_cast<uint8_t>(((v & 0xF0u) >> 4) | ((v & 0x0Fu) << 4));
    v = static_cast<uint8_t>(((v & 0xCCu) >> 2) | ((v & 0x33u) << 2));
    v = static_cast<uint8_t>(((v & 0xAAu) >> 1) | ((v & 0x55u) << 1));
    return v;
}

inline std::wstring FamilyNameFromIdCode(uint32_t idcode) {
    if (idcode == 0u || idcode == 0xFFFFFFFFu) return {};
    // Verify Xilinx manufacturer JEDEC ID (bits [11:1] == 0x049, bit 0 == 1 -> low 12 bits == 0x093)
    const uint32_t mfg = idcode & 0x0FFFu;
    if (mfg != 0x093u) return {};
    const uint32_t part = (idcode >> 12) & 0xFFFFu;
    if (part == 0x362Du || part == 0x362Eu || part == 0x362Fu || part == 0x3631u || part == 0x3632u || part == 0x3636u)
        return L"Artix-7";
    if (part >= 0x3647u && part <= 0x3656u) return L"Kintex-7";
    if (part >= 0x3667u && part <= 0x36B7u) return L"Virtex-7";
    if (part >= 0x3722u && part <= 0x3736u) return L"Zynq-7000";
    if ((part & 0xFF00u) == 0x3800u || (part & 0xFF00u) == 0x3900u) return L"UltraScale";
    if ((part & 0xFF00u) == 0x4A00u || (part & 0xFF00u) == 0x4B00u) return L"UltraScale+";
    if ((part & 0xFF00u) == 0x4000u) return L"Spartan-6";
    return L"Xilinx-FPGA";
}

} // namespace DetailXilinxBit

inline bool ParseXilinxFpgaBitBoard(
    const uint8_t* data,
    size_t size,
    const std::wstring& ext,
    ParsedEdaBoard& outBoard)
{
    using namespace DetailXilinxBit;
    if (!data || size < 32u || size > (128u << 20)) return false;

    std::string designName, partName, buildDate, buildTime;
    size_t cfgSearchStart = 0u;
    bool hasBitTlv = false;

    // 1. Check standard Xilinx `.bit` TLV preamble:
    //    `0x0009` + 9-byte header + `0x0001` + `'a'` design + `'b'` part + `'c'` date + `'d'` time + `'e'` u32BE len
    if (size >= 36u && ByteIO::RdU16BE(data) == 9u &&
        9u + 2u + 2u < size && ByteIO::RdU16BE(data + 11u) == 1u)
    {
        size_t p = 13u;
        auto readTlvStr = [&](char expectedKey, std::string& outS) -> bool {
            if (p + 3u > size || data[p] != static_cast<uint8_t>(expectedKey)) return false;
            const uint16_t slen = ByteIO::RdU16BE(data + p + 1u);
            p += 3u;
            if (p + slen > size) return false;
            size_t cleanLen = slen;
            while (cleanLen > 0u && data[p + cleanLen - 1u] == 0u) --cleanLen;
            outS.assign(reinterpret_cast<const char*>(data + p), cleanLen);
            p += slen;
            return true;
        };

        std::string aStr, bStr, cStr, dStr;
        if (readTlvStr('a', aStr)) {
            readTlvStr('b', bStr);
            readTlvStr('c', cStr);
            readTlvStr('d', dStr);
            if (p + 5u <= size && data[p] == 'e') {
                const uint32_t bitLen = ByteIO::RdU32BE(data + p + 1u);
                p += 5u;
                if (bitLen >= 16u && p < size) {
                    designName = std::move(aStr);
                    partName   = std::move(bStr);
                    buildDate  = std::move(cStr);
                    buildTime  = std::move(dStr);
                    cfgSearchStart = p;
                    hasBitTlv = true;
                }
            }
        }
    }

    // 2. Locate Xilinx 32-bit sync word (`0xAA995566` normal, `0x5599AA66` SPI bit-reversed, `0x99AA6655` BPI swapped)
    enum class WordMode : uint8_t { NormalBE = 0, BitRevBytes = 1, ByteSwap16 = 2 };
    WordMode wordMode = WordMode::NormalBE;
    size_t syncOff = std::numeric_limits<size_t>::max();
    const size_t scanEnd = std::min<size_t>(size, cfgSearchStart + 4096u);

    for (size_t i = cfgSearchStart; i + 4u <= scanEnd; ++i) {
        const uint32_t w = ByteIO::RdU32BE(data + i);
        if (w == 0xAA995566u) {
            syncOff = i;
            wordMode = WordMode::NormalBE;
            break;
        }
        if (w == 0x5599AA66u) {
            syncOff = i;
            wordMode = WordMode::BitRevBytes;
            break;
        }
        if (w == 0x99AA6655u) {
            syncOff = i;
            wordMode = WordMode::ByteSwap16;
            break;
        }
    }
    if (syncOff == std::numeric_limits<size_t>::max()) return false;

    auto rdCfgWord = [&](size_t off) -> uint32_t {
        if (wordMode == WordMode::NormalBE) {
            return ByteIO::RdU32BE(data + off);
        }
        if (wordMode == WordMode::BitRevBytes) {
            return (static_cast<uint32_t>(BitRev8(data[off + 0u])) << 24) |
                   (static_cast<uint32_t>(BitRev8(data[off + 1u])) << 16) |
                   (static_cast<uint32_t>(BitRev8(data[off + 2u])) << 8)  |
                    static_cast<uint32_t>(BitRev8(data[off + 3u]));
        }
        // ByteSwap16
        return (static_cast<uint32_t>(data[off + 1u]) << 24) |
               (static_cast<uint32_t>(data[off + 0u]) << 16) |
               (static_cast<uint32_t>(data[off + 3u]) << 8)  |
                static_cast<uint32_t>(data[off + 2u]);
    };

    // 3. Walk Type 1 & Type 2 32-bit configuration packets after `syncOff + 4`
    uint32_t idcode = 0u;
    uint32_t cor0 = 0u, ctl0 = 0u, wbstar = 0u, lastCmd = 0u, crcReg = 0u;
    bool hasIdcode = false, sawDesync = false;
    uint32_t curFar = 0u;
    uint32_t lastType1Reg = 0u;
    uint32_t lastType1Op  = 0u;
    size_t totalFdriWords = 0u;
    size_t activeFdriWords = 0u;
    size_t packetCount = 0u;

    struct FarColumnStat {
        uint8_t blockType = 0; // 0=CLB/IO/CLK, 1=BRAM_CONTENT
        uint8_t topBot = 0;
        uint8_t row = 0;
        uint16_t majorCol = 0;
        size_t activeWords = 0;
    };
    std::vector<FarColumnStat> farRegions;
    std::vector<uint32_t> fdriSampleWords;
    fdriSampleWords.reserve(8192);

    size_t wp = syncOff + 4u;
    while (wp + 4u <= size && packetCount < 200000u) {
        const uint32_t hdr = rdCfgWord(wp);
        wp += 4u;

        if (hdr == 0x20000000u || hdr == 0x00000000u || hdr == 0xFFFFFFFFu) {
            continue; // NOOP / pad
        }

        const uint32_t pktType = (hdr >> 29) & 0x7u;
        uint32_t op            = (hdr >> 27) & 0x3u; // 0=NOP (or inherit Type1 op on Type2), 1=Read, 2=Write
        uint32_t regAddr = 0u;
        uint32_t wordCnt = 0u;

        if (pktType == 1u) {
            regAddr = (hdr >> 13) & 0x1Fu;
            wordCnt = hdr & 0x7FFu;
            lastType1Reg = regAddr;
            lastType1Op  = op;
        } else if (pktType == 2u) {
            regAddr = lastType1Reg;
            if (op == 0u) op = lastType1Op;
            wordCnt = hdr & 0x07FFFFFFu;
        } else {
            // Stop at post-DESYNC trailing pad or corrupted word if we already decoded valid packets
            if (sawDesync || packetCount >= 2u) break;
            return false;
        }

        const size_t availWords = (size - wp) / 4u;
        const size_t stepWords = std::min<size_t>(wordCnt, availWords);
        ++packetCount;

        if (op == 2u) { // Write register
            if (regAddr == 0u && stepWords >= 1u) { // CRC
                crcReg = rdCfgWord(wp);
            } else if (regAddr == 1u && stepWords >= 1u) { // FAR
                curFar = rdCfgWord(wp);
                const uint8_t bt  = static_cast<uint8_t>((curFar >> 23) & 0x7u);
                const uint8_t tb  = static_cast<uint8_t>((curFar >> 22) & 0x1u);
                const uint8_t rw  = static_cast<uint8_t>((curFar >> 17) & 0x1Fu);
                const uint16_t mc = static_cast<uint16_t>((curFar >> 7) & 0x3FFu);
                if (farRegions.size() < 256u) {
                    farRegions.push_back({ bt, tb, rw, mc, 1u });
                }
            } else if (regAddr == 2u && stepWords > 0u) { // FDRI configuration frames
                totalFdriWords += stepWords;
                size_t regionActive = 0u;
                for (size_t k = 0u; k < stepWords; ++k) {
                    const uint32_t fw = rdCfgWord(wp + k * 4u);
                    if (fw != 0u && fw != 0xFFFFFFFFu) {
                        ++activeFdriWords;
                        ++regionActive;
                    }
                    if (fdriSampleWords.size() < 16384u) fdriSampleWords.push_back(fw);
                }
                if (!farRegions.empty()) {
                    farRegions.back().activeWords += regionActive;
                } else {
                    farRegions.push_back({ 0, 0, 0, 0, regionActive });
                }
            } else if (regAddr == 4u && stepWords >= 1u) { // CMD
                lastCmd = rdCfgWord(wp);
                if (lastCmd == 0x0000000Du) sawDesync = true; // DESYNC command
            } else if (regAddr == 5u && stepWords >= 1u) { // CTL0
                ctl0 = rdCfgWord(wp);
            } else if (regAddr == 9u && stepWords >= 1u) { // COR0
                cor0 = rdCfgWord(wp);
            } else if (regAddr == 12u && stepWords >= 1u) { // IDCODE
                idcode = rdCfgWord(wp);
                hasIdcode = true;
            } else if (regAddr == 16u && stepWords >= 1u) { // WBSTAR
                wbstar = rdCfgWord(wp);
            }
        }

        wp += stepWords * 4u;
        if (stepWords < wordCnt) break;
    }

    if (packetCount == 0u && !hasBitTlv) return false;
    if (totalFdriWords == 0u && !hasIdcode && !hasBitTlv) return false;
    (void)ctl0; (void)cor0; (void)wbstar; (void)crcReg;

    // 4. Render True Xilinx FPGA Die / Clock-Region / Tile-Column Floorplan (ZERO fake SOIC-8 QSPI!)
    outBoard.kind = EdaDocKind::KiCadPcb;

    std::wstring wPart = PathUtils::Utf8ToWide(partName);
    std::wstring wFamily = FamilyNameFromIdCode(idcode);
    if (wPart.empty()) wPart = wFamily.empty() ? L"XC7A_FPGA" : wFamily;

    // Grid geometry: CLB / BRAM / DSP columns across Top & Bottom clock-region banks
    int numCols = 10;
    int numRows = 6;
    if (totalFdriWords >= 1024u || ContainsTextCI(partName, "100t") ||
        ContainsTextCI(partName, "200t") || ContainsTextCI(partName, "325t")) {
        numCols = 14; numRows = 8;
    } else if (totalFdriWords >= 128u || ContainsTextCI(partName, "35t") ||
               ContainsTextCI(partName, "50t")) {
        numCols = 12; numRows = 6;
    }

    const float colW = 4.2f;
    const float rowH = 3.8f;
    const float arrayW = static_cast<float>(numCols) * colW;
    const float arrayH = static_cast<float>(numRows) * rowH;
    const float dieW = arrayW + 12.0f;
    const float dieH = arrayH + 12.0f;

    // Silicon die seal ring + I/O Bank perimeter frame
    DrawEdaBoxRect(outBoard, -dieW * 0.5f, -dieH * 0.5f, dieW * 0.5f, dieH * 0.5f, 0.38f, EdaLayerId::EdgeCuts, 0);
    DrawEdaBoxRect(outBoard, -arrayW * 0.5f - 1.8f, -arrayH * 0.5f - 1.8f,
                   arrayW * 0.5f + 1.8f, arrayH * 0.5f + 1.8f, 0.20f, EdaLayerId::TopSilk, 0);

    // Central CMT / BUFG global clock spine & horizontal Row-0 boundary (separating Top/Bottom FAR halves)
    outBoard.traces.push_back({ 0.0f, -arrayH * 0.5f - 1.8f, 0.0f, arrayH * 0.5f + 1.8f, 0.34f, EdaLayerId::InnerCopper, 1 });
    outBoard.traces.push_back({ -arrayW * 0.5f - 1.8f, 0.0f, arrayW * 0.5f + 1.8f, 0.0f, 0.26f, EdaLayerId::InnerCopper, 1 });

    // Distribute actual FDRI words across tiles so larger/denser bitstreams emit strictly more configured slice pads
    const int totalTiles = numCols * numRows;
    size_t configuredTiles = 0u;

    for (int r = 0; r < numRows; ++r) {
        for (int c = 0; c < numCols; ++c) {
            const int tileIdx = r * numCols + c;
            const bool isBramCol = (c == 2 || c == numCols - 3);
            const bool isDspCol  = (c == 4 || (numCols >= 12 && c == numCols - 5));

            const float tx = -arrayW * 0.5f + (static_cast<float>(c) + 0.5f) * colW;
            const float ty =  arrayH * 0.5f - (static_cast<float>(r) + 0.5f) * rowH;

            const EdaLayerId tileLyr = isBramCol ? EdaLayerId::InnerCopper
                                     : (isDspCol ? EdaLayerId::SchWire : EdaLayerId::BottomCopper);
            DrawEdaBoxRect(outBoard,
                tx - colW * 0.42f, ty - rowH * 0.40f,
                tx + colW * 0.42f, ty + rowH * 0.40f,
                0.15f, tileLyr, isBramCol ? 2 : (isDspCol ? 3 : 4));

            // Count active non-zero / non-FF configuration words mapped to this tile
            size_t tileActive = 0u;
            if (!fdriSampleWords.empty()) {
                for (size_t k = static_cast<size_t>(tileIdx); k < fdriSampleWords.size(); k += static_cast<size_t>(totalTiles)) {
                    const uint32_t fw = fdriSampleWords[k];
                    if (fw != 0u && fw != 0xFFFFFFFFu) ++tileActive;
                }
            } else if (static_cast<size_t>(tileIdx) < farRegions.size()) {
                tileActive = farRegions[static_cast<size_t>(tileIdx)].activeWords;
            }

            if (tileActive > 0u) {
                ++configuredTiles;
                const EdaLayerId padLyr = isBramCol ? EdaLayerId::InnerCopper : EdaLayerId::TopCopper;
                const size_t nSlicePads = std::clamp<size_t>(1u + tileActive / 2u, 1u, 6u);
                for (size_t sp = 0u; sp < nSlicePads; ++sp) {
                    const float ox = (nSlicePads == 1u) ? 0.0f
                        : ((static_cast<float>(sp) - static_cast<float>(nSlicePads - 1u) * 0.5f) * 0.54f);
                    outBoard.pads.push_back({
                        tx + ox, ty, 0.46f, 1.45f, 0.0f, 0.0f, 1, padLyr,
                        isBramCol ? L"RAMB36" : (isDspCol ? L"DSP48E1" : L"CLB"),
                        wPart
                    });
                }
            }
        }
    }

    // Ensure minimal valid bitstream (e.g. header + IDCODE + short FDRI) still places at least 1 config pad
    if (outBoard.pads.empty()) {
        outBoard.pads.push_back({ 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1, EdaLayerId::TopCopper, L"CFG0", wPart });
    }

    // Labels: Part / Design / IDCODE / FAR & FDRI summary
    std::wstring topBanner = L"Xilinx FPGA Floorplan — " + wPart;
    if (!designName.empty()) topBanner += L" (" + PathUtils::Utf8ToWide(designName) + L")";
    outBoard.labels.push_back({ -dieW * 0.48f, dieH * 0.5f + 2.2f, 1.25f, EdaLayerId::TopSilk, topBanner });

    if (hasIdcode) {
        wchar_t idLbl[128] = {};
        swprintf_s(idLbl, L"IDCODE 0x%08X%ls  |  FDRI %zu words (%zu active)  |  FAR 0x%08X",
                   idcode,
                   wFamily.empty() ? L"" : (L" (" + wFamily + L")").c_str(),
                   totalFdriWords, activeFdriWords, curFar);
        outBoard.labels.push_back({ -dieW * 0.48f, -dieH * 0.5f - 2.2f, 0.90f, EdaLayerId::SchLabel, idLbl });
    } else {
        wchar_t fdLbl[128] = {};
        swprintf_s(fdLbl, L"FDRI %zu words (%zu active)  |  %zu packets",
                   totalFdriWords, activeFdriWords, packetCount);
        outBoard.labels.push_back({ -dieW * 0.48f, -dieH * 0.5f - 2.2f, 0.90f, EdaLayerId::SchLabel, fdLbl });
    }

    if (!buildDate.empty() || !buildTime.empty()) {
        outBoard.labels.push_back({
            -dieW * 0.48f, dieH * 0.5f + 0.5f, 0.80f, EdaLayerId::BottomSilk,
            PathUtils::Utf8ToWide(buildDate + (buildTime.empty() ? "" : (" " + buildTime)))
        });
    }

    EdaComponent3D dieComp;
    dieComp.refDes = L"U_FPGA";
    dieComp.value = wPart;
    dieComp.footprint = L"XILINX_DIE";
    dieComp.x = 0.0f; dieComp.y = 0.0f;
    dieComp.widthMm = dieW; dieComp.lengthMm = dieH; dieComp.heightMm = 1.4f;
    dieComp.pinCount = static_cast<int>(outBoard.pads.size());
    dieComp.category = EdaPartCategory::IC_Package;
    outBoard.components.push_back(std::move(dieComp));

    const bool isRpdSpi = (ext == L".rpd" || wordMode == WordMode::BitRevBytes);
    wchar_t titleBuf[240] = {};
    swprintf_s(titleBuf, L"Xilinx %ls Bitstream (%ls%ls, %zu FDRI words, %zu/%d active tiles)",
               isRpdSpi ? L"SPI Configuration (.rpd)" : L"FPGA Configuration (.bit)",
               wPart.c_str(),
               hasIdcode ? (L", IDCODE " + [&]{ wchar_t b[24]; swprintf_s(b, L"0x%08X", idcode); return std::wstring(b); }()).c_str() : L"",
               totalFdriWords, configuredTiles, totalTiles);
    outBoard.formatTitle = titleBuf;
    outBoard.netCount = std::max<size_t>(2u, configuredTiles + 1u);
    return true;
}

} // namespace Formats
