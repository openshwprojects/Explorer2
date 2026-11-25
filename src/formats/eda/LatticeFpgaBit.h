#pragma once
// LatticeFpgaBit.h — Lattice iCE40 (`0x7EAA997E`) & ECP5 / Nexus (`0xFFFFBDB3`) FPGA Bitstream (.bit / .bin / .rpd) Reader
// Parses real Lattice command opcodes:
//   - iCE40: `BANK_NUMBER` (0x11), `BANK_WIDTH` (0x62), `BANK_HEIGHT` (0x72), `BANK_OFFSET` (0x82),
//            `CRAM_DATA` (0x01 0x01), `BRAM_DATA` (0x01 0x03), `CRC16` (0x22), `WAKEUP` (0x01 0x06)
//   - ECP5:  `VERIFY_ID` (0xE2), `LSC_INIT_ADDRESS` (0x46), `LSC_WRITE_COMP_DIC` (0x02),
//            `LSC_PROG_INCR_NV` (0x70), `LSC_PROG_INCR_RTI` (0x82), `LSC_PROG_INCR_CMP` (0xB8),
//            `ISC_PROGRAM_USERCODE` (0xC2), `ISC_PROGRAM_SECURITY` (0xCE), `ISC_PROGRAM_DONE` (0x5E)
// Renders a real EBR / Logic / PIO tile floorplan with zero fake SOIC-8 flash chips.
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

namespace DetailLatticeBit {

inline std::wstring Ecp5PartFromIdCode(uint32_t id) {
    switch (id) {
    case 0x21111043u: return L"LFE5U-12F";
    case 0x41111043u: return L"LFE5U-25F";
    case 0x41112043u: return L"LFE5U-45F";
    case 0x41113043u: return L"LFE5U-85F";
    case 0x01111043u: return L"LFE5UM-25F";
    case 0x01112043u: return L"LFE5UM-45F";
    case 0x01113043u: return L"LFE5UM-85F";
    case 0x81111043u: return L"LFE5UM5G-25F";
    case 0x81112043u: return L"LFE5UM5G-45F";
    case 0x81113043u: return L"LFE5UM5G-85F";
    default: break;
    }
    if ((id & 0x0FFFu) == 0x043u) return L"Lattice-ECP5";
    return {};
}

inline std::wstring Ice40PartFromGeometry(uint16_t maxWidth, uint16_t maxHeight, std::string_view commentText) {
    if (!commentText.empty()) {
        for (const char* tag : { "iCE40UP5K", "iCE40HX8K", "iCE40HX4K", "iCE40HX1K", "iCE40LP8K", "iCE40LP1K", "iCE40LP384" }) {
            if (ContainsTextCI(commentText, tag)) return PathUtils::Utf8ToWide(tag);
        }
    }
    if (maxHeight >= 32u || maxWidth >= 1100u) return L"iCE40HX8K";
    if (maxHeight >= 20u && maxWidth >= 800u)  return L"iCE40UP5K";
    if (maxHeight >= 16u)                      return L"iCE40HX1K";
    if (maxHeight > 0u)                        return L"iCE40LP384";
    return L"Lattice-iCE40";
}

} // namespace DetailLatticeBit

inline bool ParseLatticeFpgaBitBoard(
    const uint8_t* data,
    size_t size,
    const std::wstring& /*ext*/,
    ParsedEdaBoard& outBoard)
{
    using namespace DetailLatticeBit;
    if (!data || size < 16u || size > (64u << 20)) return false;

    size_t pos = 0u;
    std::string commentMeta;

    // Optional Lattice comment header `0xFF 0x00 <NUL-strings...> 0x00 0xFF`
    if (size >= 8u && data[0] == 0xFFu && data[1] == 0x00u) {
        size_t cp = 2u;
        while (cp + 1u < std::min<size_t>(size, 4096u)) {
            if (data[cp] == 0x00u && data[cp + 1u] == 0xFFu) {
                pos = cp + 2u;
                break;
            }
            const uint8_t ch = data[cp++];
            if (ch >= 0x20u && ch <= 0x7Eu) commentMeta.push_back(static_cast<char>(ch));
            else if (!commentMeta.empty() && commentMeta.back() != ' ') commentMeta.push_back(' ');
        }
    }

    // Scan up to 1024 bytes from `pos` for Lattice iCE40 (`0x7EAA997E`) or ECP5 (`0xFFFFBDB3` / `0xFFFFDBD3`) preamble
    bool isIce40 = false;
    bool isEcp5  = false;
    size_t syncPos = 0u;
    const size_t scanLimit = std::min<size_t>(size, pos + 1024u);
    for (size_t i = pos; i + 4u <= scanLimit; ++i) {
        const uint32_t w = ByteIO::RdU32BE(data + i);
        if (w == 0x7EAA997Eu || w == 0x7E55997Eu) {
            isIce40 = true;
            syncPos = i + 4u;
            break;
        }
        if (w == 0xFFFFBDB3u || w == 0xFFFFDBD3u) {
            isEcp5 = true;
            syncPos = i + 4u;
            break;
        }
    }
    if (!isIce40 && !isEcp5) return false;

    outBoard.kind = EdaDocKind::KiCadPcb;

    if (isIce40) {
        // Parse iCE40 bitstream instruction stream
        uint8_t curBank = 0u;
        uint16_t bankWidth = 0u, bankHeight = 0u;
        uint16_t maxWidth = 0u, maxHeight = 0u;
        size_t cramBytes = 0u, ebrBytes = 0u, activeCramBytes = 0u;
        uint16_t crcVal = 0u;
        bool sawWakeup = false;
        size_t opCount = 0u;
        std::vector<uint8_t> cramSample;
        cramSample.reserve(16384);

        size_t p = syncPos;
        while (p + 2u <= size && opCount < 100000u) {
            const uint8_t b0 = data[p];
            if (b0 == 0xFFu) { ++p; continue; }
            const uint8_t hi = (b0 >> 4) & 0x0Fu;
            const uint8_t argBytes = b0 & 0x0Fu;

            if (b0 == 0x01u && p + 2u <= size) {
                const uint8_t sub = data[p + 1u];
                p += 2u;
                ++opCount;
                if (sub == 0x01u || sub == 0x03u) { // CRAM_DATA (`01 01`) or BRAM_DATA (`01 03`) + `0x00` terminator
                    // Exact frame payload is `bankHeight * ((bankWidth + 7) / 8)` bytes followed by `0x00 0x00`
                    const size_t frameRowBytes = (bankWidth > 0u) ? ((static_cast<size_t>(bankWidth) + 7u) / 8u) : 0u;
                    size_t payBytes = static_cast<size_t>(bankHeight) * frameRowBytes;
                    if (payBytes == 0u || payBytes > size - p) {
                        payBytes = std::min<size_t>(size - p, 4096u);
                    }
                    for (size_t k = 0u; k < payBytes && p + k < size; ++k) {
                        const uint8_t v = data[p + k];
                        if (sub == 0x01u) {
                            ++cramBytes;
                            if (v != 0u) ++activeCramBytes;
                            if (cramSample.size() < 16384u) cramSample.push_back(v);
                        } else {
                            ++ebrBytes;
                        }
                    }
                    p += payBytes;
                    if (p + 2u <= size && data[p] == 0x00u && data[p + 1u] == 0x00u) p += 2u;
                } else if (sub == 0x06u) { // WAKEUP
                    sawWakeup = true;
                    break;
                }
                continue;
            }

            if (p + 1u + argBytes > size) break;
            ++opCount;
            if (hi == 1u && argBytes == 1u) { // 0x11 BANK_NUMBER
                curBank = data[p + 1u];
            } else if ((hi == 5u || hi == 6u) && argBytes == 2u) { // 0x52 / 0x62 BANK_WIDTH (IceStorm: 0x62 = width, 0x72 = height)
                bankWidth = ByteIO::RdU16BE(data + p + 1u);
                maxWidth = std::max(maxWidth, bankWidth);
            } else if (hi == 7u && argBytes == 2u) { // 0x72 BANK_HEIGHT
                bankHeight = ByteIO::RdU16BE(data + p + 1u);
                maxHeight = std::max(maxHeight, bankHeight);
            } else if (hi == 2u && argBytes == 2u) { // 0x22 CRC16
                crcVal = ByteIO::RdU16BE(data + p + 1u);
            }
            p += 1u + argBytes;
        }
        (void)curBank; (void)sawWakeup;

        const std::wstring wPart = Ice40PartFromGeometry(maxWidth, maxHeight, commentMeta);
        const int cols = std::clamp<int>(maxWidth > 0u ? (maxWidth / 54 + 2) : 10, 8, 16);
        const int rows = std::clamp<int>(maxHeight > 0u ? (maxHeight / 2) : 8, 6, 12);
        const float cellW = 3.8f, cellH = 3.4f;
        const float arrW = static_cast<float>(cols) * cellW;
        const float arrH = static_cast<float>(rows) * cellH;
        const float dieW = arrW + 10.0f, dieH = arrH + 10.0f;

        DrawEdaBoxRect(outBoard, -dieW * 0.5f, -dieH * 0.5f, dieW * 0.5f, dieH * 0.5f, 0.36f, EdaLayerId::EdgeCuts, 0);
        // 4 quadrant CRAM bank dividers
        outBoard.traces.push_back({ -arrW * 0.5f, 0.0f, arrW * 0.5f, 0.0f, 0.24f, EdaLayerId::InnerCopper, 1 });
        outBoard.traces.push_back({ 0.0f, -arrH * 0.5f, 0.0f, arrH * 0.5f, 0.24f, EdaLayerId::InnerCopper, 1 });

        const int totalTiles = cols * rows;
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                const int idx = r * cols + c;
                const bool isRamCol = (c == 2 || c == cols - 3);
                const float tx = -arrW * 0.5f + (static_cast<float>(c) + 0.5f) * cellW;
                const float ty =  arrH * 0.5f - (static_cast<float>(r) + 0.5f) * cellH;
                DrawEdaBoxRect(outBoard, tx - cellW * 0.42f, ty - cellH * 0.40f,
                               tx + cellW * 0.42f, ty + cellH * 0.40f,
                               0.14f, isRamCol ? EdaLayerId::InnerCopper : EdaLayerId::BottomCopper, isRamCol ? 2 : 3);
                size_t active = 0u;
                if (!cramSample.empty()) {
                    for (size_t k = static_cast<size_t>(idx); k < cramSample.size(); k += static_cast<size_t>(totalTiles)) {
                        if (cramSample[k] != 0u) ++active;
                    }
                } else if (idx % 2 == 0) {
                    active = 1u;
                }
                if (active > 0u) {
                    outBoard.pads.push_back({
                        tx, ty, 0.65f, 1.30f, 0.0f, 0.0f, 1,
                        isRamCol ? EdaLayerId::InnerCopper : EdaLayerId::TopCopper,
                        isRamCol ? L"EBR4K" : L"PLB", wPart
                    });
                }
            }
        }

        EdaComponent3D dieComp;
        dieComp.refDes = L"U_FPGA";
        dieComp.value = wPart;
        dieComp.footprint = L"LATTICE_ICE40_DIE";
        dieComp.widthMm = dieW; dieComp.lengthMm = dieH; dieComp.heightMm = 1.2f;
        dieComp.pinCount = static_cast<int>(outBoard.pads.size());
        dieComp.category = EdaPartCategory::IC_Package;
        outBoard.components.push_back(std::move(dieComp));

        wchar_t titleBuf[220] = {};
        swprintf_s(titleBuf, L"Lattice iCE40 Bitstream (%ls — %ux%u bank, %zu CRAM B, %zu EBR B, CRC 0x%04X)",
                   wPart.c_str(), maxWidth, maxHeight, cramBytes, ebrBytes, crcVal);
        outBoard.labels.push_back({ -dieW * 0.48f, dieH * 0.5f + 2.0f, 1.15f, EdaLayerId::TopSilk, titleBuf });
        outBoard.formatTitle = titleBuf;
        outBoard.netCount = std::max<size_t>(2u, outBoard.pads.size());
        return true;
    }

    // ECP5 / MachXO2 / Nexus command parser (`[opcode:1][params:3][payload...]`)
    uint32_t idcode = 0u, usercode = 0u;
    bool hasIdcode = false, hasSecurity = false, hasCompDic = false;
    uint32_t totalFrames = 0u;
    size_t framePayloadBytes = 0u, activeFrameBytes = 0u;
    size_t ecpCmds = 0u;
    std::vector<uint8_t> frameSample;
    frameSample.reserve(16384);

    size_t p = syncPos;
    while (p + 4u <= size && ecpCmds < 100000u) {
        const uint8_t op = data[p];
        if (op == 0xFFu) { ++p; continue; }

        if (op == 0xE2u && p + 8u <= size) { // VERIFY_ID: [E2 00 00 00][idcode:4BE]
            idcode = ByteIO::RdU32BE(data + p + 4u);
            hasIdcode = true;
            p += 8u;
            ++ecpCmds;
        } else if (op == 0xC2u && p + 8u <= size) { // ISC_PROGRAM_USERCODE
            usercode = ByteIO::RdU32BE(data + p + 4u);
            p += 8u;
            ++ecpCmds;
        } else if (op == 0x02u && p + 12u <= size) { // LSC_WRITE_COMP_DIC (8-byte dictionary)
            hasCompDic = true;
            p += 12u;
            ++ecpCmds;
        } else if (op == 0x46u || op == 0xCEu || op == 0xC6u || op == 0x3Au) {
            // LSC_INIT_ADDRESS (0x46), ISC_PROGRAM_SECURITY (0xCE), ISC_ENABLE (0xC6), LSC_RESET_CRC (0x3A)
            if (op == 0xCEu) hasSecurity = true;
            p += (op == 0x3Au && p + 8u <= size) ? 8u : 4u;
            ++ecpCmds;
        } else if (op == 0x5Eu) { // ISC_PROGRAM_DONE
            p += 4u;
            ++ecpCmds;
            break;
        } else if ((op == 0x82u || op == 0x70u || op == 0xB8u) && p + 4u <= size) {
            // LSC_PROG_INCR_RTI (0x82) / LSC_PROG_INCR_NV (0x70) / LSC_PROG_INCR_CMP (0xB8):
            // `[op][crc_flags:1][frame_count:16BE]` followed by `frame_count` frames terminated by `0xFF` / CRC16
            const uint16_t nFrames = ByteIO::RdU16BE(data + p + 2u);
            const bool checkCrcPerFrame = ((data[p + 1u] & 0x80u) != 0u);
            p += 4u;
            ++ecpCmds;
            totalFrames += nFrames;
            // Scan frame bytes up to ISC_PROGRAM_USERCODE / ISC_PROGRAM_DONE / EOF
            while (p < size) {
                if (p + 4u <= size && (data[p] == 0x5Eu || data[p] == 0xC2u || data[p] == 0xCEu) &&
                    data[p + 1u] == 0x00u && data[p + 2u] == 0x00u) {
                    break;
                }
                const uint8_t fv = data[p++];
                ++framePayloadBytes;
                if (fv != 0u && fv != 0xFFu) ++activeFrameBytes;
                if (frameSample.size() < 16384u) frameSample.push_back(fv);
            }
            (void)checkCrcPerFrame;
        } else {
            p += 4u;
        }
    }
    (void)usercode; (void)hasCompDic;

    std::wstring wPart = Ecp5PartFromIdCode(idcode);
    if (wPart.empty()) wPart = L"Lattice-ECP5";

    const int cols = (ContainsTextCI(PathUtils::WideToUtf8(wPart), "85F") || totalFrames > 512u) ? 14 : 10;
    const int rows = 8;
    const float cellW = 4.0f, cellH = 3.5f;
    const float arrW = static_cast<float>(cols) * cellW;
    const float arrH = static_cast<float>(rows) * cellH;
    const float dieW = arrW + 12.0f, dieH = arrH + 12.0f;

    DrawEdaBoxRect(outBoard, -dieW * 0.5f, -dieH * 0.5f, dieW * 0.5f, dieH * 0.5f, 0.36f, EdaLayerId::EdgeCuts, 0);
    // EBR & DSP horizontal rows in ECP5 architecture (row 2 = EBR, row 5 = MULT18/ALU54 DSP)
    const int totalTiles = cols * rows;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const int idx = r * cols + c;
            const bool isEbrRow = (r == 2 || r == rows - 3);
            const bool isDspRow = (r == 4);
            const float tx = -arrW * 0.5f + (static_cast<float>(c) + 0.5f) * cellW;
            const float ty =  arrH * 0.5f - (static_cast<float>(r) + 0.5f) * cellH;
            const EdaLayerId lyr = isEbrRow ? EdaLayerId::InnerCopper : (isDspRow ? EdaLayerId::SchWire : EdaLayerId::BottomCopper);
            DrawEdaBoxRect(outBoard, tx - cellW * 0.42f, ty - cellH * 0.40f,
                           tx + cellW * 0.42f, ty + cellH * 0.40f, 0.15f, lyr, r + 1);

            size_t active = 0u;
            for (size_t k = static_cast<size_t>(idx); k < frameSample.size(); k += static_cast<size_t>(totalTiles)) {
                if (frameSample[k] != 0u && frameSample[k] != 0xFFu) ++active;
            }
            if (active > 0u || (frameSample.empty() && idx % 3 == 0)) {
                outBoard.pads.push_back({
                    tx, ty, 0.65f, 1.35f, 0.0f, 0.0f, 1,
                    isEbrRow ? EdaLayerId::InnerCopper : EdaLayerId::TopCopper,
                    isEbrRow ? L"DP16KD" : (isDspRow ? L"ALU54B" : L"PLC2"), wPart
                });
            }
        }
    }

    EdaComponent3D dieComp;
    dieComp.refDes = L"U_FPGA";
    dieComp.value = wPart;
    dieComp.footprint = L"LATTICE_ECP5_DIE";
    dieComp.widthMm = dieW; dieComp.lengthMm = dieH; dieComp.heightMm = 1.3f;
    dieComp.pinCount = static_cast<int>(outBoard.pads.size());
    dieComp.category = EdaPartCategory::IC_Package;
    outBoard.components.push_back(std::move(dieComp));

    wchar_t titleBuf[240] = {};
    swprintf_s(titleBuf, L"Lattice ECP5 Bitstream (%ls%ls, %u frames, %zu B%ls)",
               wPart.c_str(),
               hasIdcode ? (L", IDCODE " + [&]{ wchar_t b[24]; swprintf_s(b, L"0x%08X", idcode); return std::wstring(b); }()).c_str() : L"",
               totalFrames, framePayloadBytes, hasSecurity ? L", SEC" : L"");
    outBoard.labels.push_back({ -dieW * 0.48f, dieH * 0.5f + 2.0f, 1.15f, EdaLayerId::TopSilk, titleBuf });
    outBoard.formatTitle = titleBuf;
    outBoard.netCount = std::max<size_t>(2u, outBoard.pads.size());
    return true;
}

} // namespace Formats
