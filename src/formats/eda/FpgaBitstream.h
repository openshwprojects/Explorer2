#pragma once
// FpgaBitstream.h — Xilinx / Lattice / Gowin / iCE40 / Altera Raw Programming Data (.bit / .rpd) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

namespace DetailHwBin {

// Xilinx .bit & FPGA .rpd Bitstream Parser
inline bool ParseFpgaBitstream(const uint8_t* data, size_t size, const std::wstring& extLower, ParsedEdaBoard& out) {
    if (!data || size < 32) return false;
    out.kind = EdaDocKind::KiCadPcb;

    if (extLower == L".bit") {
        // Parse Xilinx .bit TLV header: [u16_be syncHdrLen][syncHdr][u16_be 1]['a'][u16 len][design]...['e'][u32_be bitLen]
        size_t pos = 0;
        std::string designName, partName, buildDate, buildTime;
        const uint8_t* cfgBytes = nullptr;
        size_t cfgLen = 0;

        if (size >= 16) {
            uint16_t syncHdrLen = DetailEda100::RdU16BE(data);
            if (syncHdrLen >= 2 && syncHdrLen <= 64 && static_cast<size_t>(2 + syncHdrLen + 2) < size) {
                pos = 2 + syncHdrLen;
                uint16_t firstKeyLen = DetailEda100::RdU16BE(data + pos);
                if (firstKeyLen == 1) {
                    pos += 2;
                    while (pos < size) {
                        uint8_t key = data[pos++];
                        if (key == 'e') {
                            if (pos + 4 > size) return false;
                            uint32_t blen = DetailEda100::RdU32BE(data + pos);
                            pos += 4;
                            if (blen == 0 || pos >= size) return false;
                            cfgBytes = data + pos;
                            cfgLen = std::min<size_t>(blen, size - pos);
                            break;
                        } else if (key >= 'a' && key <= 'd') {
                            if (pos + 2 > size) return false;
                            uint16_t slen = DetailEda100::RdU16BE(data + pos);
                            pos += 2;
                            if (slen > size - pos) return false;
                            std::string s = DetailEda100::TrimAsciiView(
                                std::string_view(reinterpret_cast<const char*>(data + pos), slen));
                            pos += slen;
                            if (key == 'a') designName = s;
                            else if (key == 'b') partName = s;
                            else if (key == 'c') buildDate = s;
                            else if (key == 'd') buildTime = s;
                            // Optional u16_be keyLen==1 before next key in some Xilinx tools, or direct key char
                            if (pos + 3 <= size && data[pos] == 0x00 && data[pos + 1] == 0x01 &&
                                data[pos + 2] >= 'a' && data[pos + 2] <= 'e')
                            {
                                pos += 2;
                            }
                        } else {
                            break;
                        }
                    }
                }
            }
        }

        // Also locate 0xAA995566 sync word in bitstream
        if (!cfgBytes) {
            cfgBytes = data;
            cfgLen = size;
        }
        size_t syncOff = std::string_view::npos;
        for (size_t i = 0; i + 4 <= cfgLen; ++i) {
            if (DetailEda100::RdU32BE(cfgBytes + i) == 0xAA995566u) {
                syncOff = i;
                break;
            }
        }
        if (syncOff == std::string_view::npos && partName.empty()) return false;

        uint32_t idcode = 0;
        size_t fdriWords = 0;
        const uint8_t* frameStart = cfgBytes;
        size_t frameBytes = cfgLen;

        if (syncOff != std::string_view::npos) {
            size_t wp = syncOff + 4;
            while (wp + 4 <= cfgLen) {
                uint32_t w = DetailEda100::RdU32BE(cfgBytes + wp);
                wp += 4;
                uint32_t pktType = (w >> 29) & 0x7u;
                if (pktType == 1) {
                    uint32_t op  = (w >> 27) & 0x3u;
                    uint32_t reg = (w >> 13) & 0x1Fu;
                    uint32_t cnt = w & 0x7FFu;
                    if (op == 2 && reg == 12 && cnt >= 1 && wp + 4 <= cfgLen) { // IDCODE
                        idcode = DetailEda100::RdU32BE(cfgBytes + wp);
                    }
                    if (op == 2 && reg == 2) { // FDRI
                        if (cnt > 0 && wp + cnt * 4u <= cfgLen) {
                            frameStart = cfgBytes + wp;
                            frameBytes = cnt * 4u;
                            fdriWords += cnt;
                        } else if (cnt == 0 && wp + 4 <= cfgLen) {
                            uint32_t w2 = DetailEda100::RdU32BE(cfgBytes + wp);
                            if (((w2 >> 29) & 0x7u) == 2) {
                                wp += 4;
                                uint32_t cnt2 = w2 & 0x07FFFFFFu;
                                size_t avail = std::min<size_t>(static_cast<size_t>(cnt2) * 4u, cfgLen - wp);
                                frameStart = cfgBytes + wp;
                                frameBytes = avail;
                                fdriWords += avail / 4u;
                                wp += avail;
                                continue;
                            }
                        }
                    }
                    size_t skip = std::min<size_t>(static_cast<size_t>(cnt) * 4u, cfgLen - wp);
                    wp += skip;
                }
            }
        }

        // Partition configuration frames across an 8x8 CLB tile floorplan
        constexpr int kGrid = 8;
        DetailEda100::AddRectOutline(out, 0.0f, 0.0f, 52.0f, 52.0f, EdaLayerId::EdgeCuts, 0.45f);
        size_t activeTiles = 0;
        for (int r = 0; r < kGrid; ++r) {
            for (int c = 0; c < kGrid; ++c) {
                size_t tileIdx = static_cast<size_t>(r * kGrid + c);
                size_t b0 = (frameBytes * tileIdx) / (kGrid * kGrid);
                size_t b1 = (frameBytes * (tileIdx + 1)) / (kGrid * kGrid);
                if (b1 <= b0 && frameBytes > 0) { b0 = tileIdx % frameBytes; b1 = b0 + 1; }
                size_t setBits = 0, totalBits = (b1 - b0) * 8;
                for (size_t bi = b0; bi < b1; ++bi) {
                    uint8_t v = frameStart[bi];
                    while (v) { setBits += (v & 1u); v >>= 1; }
                }
                float util = (totalBits > 0) ? (static_cast<float>(setBits) / static_cast<float>(totalBits)) : 0.0f;
                float tx = -19.25f + c * 5.5f;
                float ty = 19.25f - r * 5.5f;
                if (util > 0.02f) {
                    ++activeTiles;
                    float sz = std::clamp(1.2f + util * 3.6f, 1.2f, 4.6f);
                    EdaLayerId tl = (util > 0.35f) ? EdaLayerId::TopCopper : EdaLayerId::InnerCopper;
                    out.pads.push_back({ tx, ty, sz, sz, 0.0f, 0.0f, 1, tl, L"CLB", L"" });
                    if (c > 0 && util > 0.15f) {
                        out.traces.push_back({ tx - 5.5f, ty, tx, ty, 0.18f, EdaLayerId::SchWire, static_cast<int>(tileIdx + 1) });
                    }
                } else {
                    DetailEda100::AddRectOutline(out, tx, ty, 3.8f, 3.8f, EdaLayerId::BottomSilk, 0.08f);
                }
            }
        }

        std::wstring wPart = partName.empty() ? L"Xilinx_FPGA" : PathUtils::Utf8ToWide(partName);
        std::wstring wDes  = designName.empty() ? L"bitstream" : PathUtils::Utf8ToWide(designName);
        wchar_t idHex[32] = {};
        if (idcode != 0) swprintf_s(idHex, L"IDCODE 0x%08X", idcode);

        EdaComponent3D die;
        die.refDes = wPart;
        die.value = wDes;
        die.footprint = (idcode != 0) ? idHex : L"FPGA_CLB_ARRAY";
        die.x = 0.0f; die.y = 0.0f; die.widthMm = 46.0f; die.lengthMm = 46.0f; die.heightMm = 1.8f;
        die.category = EdaPartCategory::IC_Package;
        out.components.push_back(die);

        out.labels.push_back({ 0.0f, 28.5f, 1.3f, EdaLayerId::SchLabel, wPart + L" — " + wDes });
        if (!buildDate.empty() || idcode != 0) {
            std::wstring meta = PathUtils::Utf8ToWide(buildDate + (buildTime.empty() ? "" : (" " + buildTime)));
            if (idcode != 0) meta += (meta.empty() ? L"" : L" | ") + std::wstring(idHex);
            out.labels.push_back({ 0.0f, -28.5f, 1.0f, EdaLayerId::TopSilk, meta });
        }
        out.formatTitle = L"Xilinx FPGA Bitstream (" + wPart + L", " + std::to_wstring(fdriWords) +
                          L" FDRI words, " + std::to_wstring(activeTiles) + L"/64 CLB tiles)";
        out.netCount = std::max<size_t>(1, activeTiles);
        return true;
    }

    // .rpd (FPGA Raw Programming Data — Intel/Altera Active Serial, Xilinx SPI/BPI Flash, Lattice iCE40/ECP5)
    if (extLower == L".rpd" && size >= 64) {
        // Reject known non-FPGA containers & plain ASCII text prose
        if ((size >= 4 && (std::memcmp(data, "PK\x03\x04", 4) == 0 || std::memcmp(data, "\x7F" "ELF", 4) == 0 ||
                           std::memcmp(data, "\x89PNG", 4) == 0 || std::memcmp(data, "%PDF", 4) == 0 ||
                           std::memcmp(data, "RIFF", 4) == 0 || std::memcmp(data, "GIF8", 4) == 0)) ||
            (size >= 2 && std::memcmp(data, "MZ", 2) == 0)) {
            return false;
        }
        size_t nonFF = 0, nonZero = 0, asciiCnt = 0;
        size_t asciiCheckLen = std::min<size_t>(size, 256);
        for (size_t i = 0; i < size; ++i) {
            if (data[i] != 0xFFu) ++nonFF;
            if (data[i] != 0x00u) ++nonZero;
            if (i < asciiCheckLen) {
                uint8_t c = data[i];
                if ((c >= 0x20u && c <= 0x7Eu) || c == '\r' || c == '\n' || c == '\t') ++asciiCnt;
            }
        }
        if (nonFF == 0 || nonZero == 0) return false;
        if (asciiCnt * 100 >= asciiCheckLen * 92) return false; // Plain text renamed to .rpd

        auto bitRev8 = [](uint8_t x) noexcept -> uint8_t {
            x = static_cast<uint8_t>(((x & 0xF0u) >> 4) | ((x & 0x0Fu) << 4));
            x = static_cast<uint8_t>(((x & 0xCCu) >> 2) | ((x & 0x33u) << 2));
            x = static_cast<uint8_t>(((x & 0xAAu) >> 1) | ((x & 0x55u) << 1));
            return x;
        };

        enum class RpdVendor { Unknown, XilinxRaw, AlteraAs, LatticeIce40, LatticeEcp5 };
        RpdVendor vendor = RpdVendor::Unknown;
        bool bitReversed = false;
        size_t syncOffset = 0;
        uint32_t detectedIdcode = 0;
        size_t cfgStartOff = 0;
        const size_t scanLim = std::min<size_t>(size, 262144);

        // 1) Probe Xilinx 32-bit sync word: 0xAA995566 (normal), 0x5599AA66 (SPI bit-reversed), 0x99AA6655 (BPI16 swapped)
        for (size_t i = 0; i + 8 <= scanLim; ++i) {
            uint32_t w = DetailEda100::RdU32BE(data + i);
            if (w == 0xAA995566u || w == 0x5599AA66u || w == 0x99AA6655u) {
                auto rdWord = [&](size_t off) -> uint32_t {
                    if (w == 0x5599AA66u) {
                        uint8_t b0 = bitRev8(data[off + 0]), b1 = bitRev8(data[off + 1]);
                        uint8_t b2 = bitRev8(data[off + 2]), b3 = bitRev8(data[off + 3]);
                        return (static_cast<uint32_t>(b0) << 24) | (static_cast<uint32_t>(b1) << 16) |
                               (static_cast<uint32_t>(b2) << 8)  | static_cast<uint32_t>(b3);
                    }
                    if (w == 0x99AA6655u) {
                        return (static_cast<uint32_t>(data[off + 1]) << 24) | (static_cast<uint32_t>(data[off + 0]) << 16) |
                               (static_cast<uint32_t>(data[off + 3]) << 8)  | static_cast<uint32_t>(data[off + 2]);
                    }
                    return DetailEda100::RdU32BE(data + off);
                };
                // Verify Type-1 packet or NOOP (0x20000000) follows sync word
                uint32_t nextW = rdWord(i + 4);
                uint32_t pktType = (nextW >> 29) & 0x7u;
                if (pktType == 1u || pktType == 2u) {
                    vendor = RpdVendor::XilinxRaw;
                    bitReversed = (w != 0xAA995566u);
                    syncOffset = i;
                    cfgStartOff = i + 4;
                    size_t wp = i + 4;
                    for (int pk = 0; pk < 64 && wp + 4 <= size; ++pk) {
                        uint32_t pw = rdWord(wp);
                        wp += 4;
                        if (((pw >> 29) & 0x7u) == 1u) {
                            uint32_t op = (pw >> 27) & 0x3u;
                            uint32_t reg = (pw >> 13) & 0x1Fu;
                            uint32_t cnt = pw & 0x7FFu;
                            if (op == 2u && reg == 12u && cnt >= 1u && wp + 4 <= size) {
                                detectedIdcode = rdWord(wp);
                            }
                            if (op == 2u && reg == 2u) {
                                cfgStartOff = wp;
                                break;
                            }
                            wp += std::min<size_t>(static_cast<size_t>(cnt) * 4u, size - wp);
                        }
                    }
                    break;
                }
            }
        }

        // 2) Probe Lattice iCE40 (0x7EAA997E) & Lattice ECP5 (0xFFFFBDB3 + 0xE2 LSC_VERIFY_ID)
        if (vendor == RpdVendor::Unknown) {
            for (size_t i = 0; i + 8 <= scanLim; ++i) {
                uint32_t w = DetailEda100::RdU32BE(data + i);
                if (w == 0x7EAA997Eu || w == 0x7E55997Eu) {
                    vendor = RpdVendor::LatticeIce40;
                    bitReversed = (w == 0x7E55997Eu);
                    syncOffset = i;
                    cfgStartOff = i + 4;
                    break;
                }
                if (w == 0xFFFFBDB3u || w == 0xFFFFDBD3u) {
                    vendor = RpdVendor::LatticeEcp5;
                    bitReversed = (w == 0xFFFFDBD3u);
                    syncOffset = i;
                    cfgStartOff = i + 4;
                    for (size_t k = i + 4; k + 8 <= std::min<size_t>(size, i + 128); ++k) {
                        uint8_t op = bitReversed ? bitRev8(data[k]) : data[k];
                        if (op == 0xE2u && k + 8 <= size) { // LSC_VERIFY_ID: E2 00 00 00 <id32>
                            uint8_t z0 = bitReversed ? bitRev8(data[k + 1]) : data[k + 1];
                            uint8_t z1 = bitReversed ? bitRev8(data[k + 2]) : data[k + 2];
                            if (z0 == 0 && z1 == 0) {
                                uint8_t i0 = bitReversed ? bitRev8(data[k + 4]) : data[k + 4];
                                uint8_t i1 = bitReversed ? bitRev8(data[k + 5]) : data[k + 5];
                                uint8_t i2 = bitReversed ? bitRev8(data[k + 6]) : data[k + 6];
                                uint8_t i3 = bitReversed ? bitRev8(data[k + 7]) : data[k + 7];
                                detectedIdcode = (static_cast<uint32_t>(i0) << 24) | (static_cast<uint32_t>(i1) << 16) |
                                                 (static_cast<uint32_t>(i2) << 8)  | static_cast<uint32_t>(i3);
                                break;
                            }
                        }
                    }
                    break;
                }
            }
        }

        // 3) Probe Intel / Altera Active Serial (.rpd):
        //    - Quartus AS preamble: >= 16 bytes of 0xFF followed by 0x6A (MSb) or 0x56 (LSb bit-reversed, bitRev8(0x6A)==0x56)
        //    - Quartus AS Option-Bits block: 0xA5A5 / 0x5A5A marker at offset 0x08..0x88 after 0xFF padding
        //    - Arria 10 / Stratix 10 / Agilex boot header magic 0x62294895 (LE) or bit-reversed 0xA9129446
        if (vendor == RpdVendor::Unknown) {
            for (size_t i = 0; i + 4 <= std::min<size_t>(scanLim, 4096); i += 4) {
                uint32_t le = DetailEda100::RdU32LE(data + i);
                if (le == 0x62294895u || le == 0xA9129446u || le == 0x31575341u) {
                    vendor = RpdVendor::AlteraAs;
                    bitReversed = (le == 0xA9129446u);
                    syncOffset = i;
                    cfgStartOff = i;
                    break;
                }
            }
        }
        if (vendor == RpdVendor::Unknown) {
            // Check Altera Option-Bits table or >= 16-byte 0xFF erased preamble -> 0x6A / 0x56 AS header byte
            size_t ffRun = 0;
            for (size_t i = 0; i < scanLim; ++i) {
                if (data[i] == 0xFFu) {
                    ++ffRun;
                    continue;
                }
                if (ffRun >= 16u) {
                    uint8_t b = data[i];
                    if (b == 0x6Au || b == 0x56u) {
                        vendor = RpdVendor::AlteraAs;
                        bitReversed = (b == 0x56u);
                        syncOffset = i;
                        cfgStartOff = i;
                        break;
                    }
                    // Option-bits signature (0xA5 0xA5 or 0x5A 0x5A) within option sector
                    if (i + 1 < scanLim && ((b == 0xA5u && data[i + 1] == 0xA5u) || (b == 0x5Au && data[i + 1] == 0x5Au))) {
                        vendor = RpdVendor::AlteraAs;
                        bitReversed = (b == 0xA5u);
                        syncOffset = i;
                        cfgStartOff = i;
                        break;
                    }
                }
                ffRun = 0;
            }
        }

        if (vendor == RpdVendor::Unknown) {
            return false; // Reject arbitrary binary files without valid FPGA flash framing!
        }

        // Render SPI NOR Configuration Flash + FPGA CLB/LAB Floorplan Schematic/Layout
        DetailEda100::AddRectOutline(out, 6.0f, 0.0f, 70.0f, 52.0f, EdaLayerId::EdgeCuts, 0.42f);

        // Left IC: 8-pin QSPI Configuration Flash (SOIC-8) wired via 4x SPI traces to FPGA
        const float flashX = -20.0f, flashY = 0.0f;
        DetailEda100::AddRectOutline(out, flashX, flashY, 9.0f, 12.0f, EdaLayerId::TopSilk, 0.22f);
        static const wchar_t* const kQspiPins[8] = { L"CS#", L"DQ1", L"DQ2", L"GND", L"DQ0", L"CLK", L"DQ3", L"VCC" };
        for (int p = 0; p < 8; ++p) {
            float px = (p < 4) ? (flashX - 5.2f) : (flashX + 5.2f);
            float py = (p < 4) ? (3.75f - p * 2.5f) : (-3.75f + (p - 4) * 2.5f);
            out.pads.push_back({ px, py, 1.6f, 0.9f, 0.0f, 0.0f, 1, EdaLayerId::TopCopper, kQspiPins[p], L"" });
            if (p >= 4 && p <= 6) { // DQ0, CLK, DQ3 active bus lines to FPGA die
                out.traces.push_back({ px + 0.8f, py, -8.5f, py, 0.22f, EdaLayerId::TopCopper, p + 1 });
            }
        }
        out.traces.push_back({ flashX - 5.2f, 3.75f, flashX - 5.2f, 8.0f, 0.20f, EdaLayerId::TopCopper, 1 });
        out.traces.push_back({ flashX - 5.2f, 8.0f, -8.5f, 8.0f, 0.20f, EdaLayerId::TopCopper, 1 });

        EdaComponent3D flashIc;
        flashIc.refDes = L"U_QSPI";
        flashIc.value = PathUtils::FormatByteSize(size);
        flashIc.footprint = L"SOIC-8_FLASH";
        flashIc.x = flashX; flashIc.y = flashY;
        flashIc.widthMm = 9.0f; flashIc.lengthMm = 12.0f; flashIc.heightMm = 1.6f;
        flashIc.pinCount = 8;
        flashIc.category = EdaPartCategory::IC_Package;
        out.components.push_back(flashIc);

        // Right IC: FPGA 8x8 LAB/CLB Configuration Tile Array from cfgStartOff..size
        const float fpgaX = 14.5f, fpgaY = 0.0f;
        DetailEda100::AddRectOutline(out, fpgaX, fpgaY, 45.0f, 45.0f, EdaLayerId::TopSilk, 0.28f);
        const size_t payloadLen = (cfgStartOff < size) ? (size - cfgStartOff) : size;
        const uint8_t* payloadPtr = (cfgStartOff < size) ? (data + cfgStartOff) : data;
        size_t activePages = 0;
        for (int i = 0; i < 64; ++i) {
            size_t p0 = (payloadLen * static_cast<size_t>(i)) / 64u;
            size_t p1 = (payloadLen * static_cast<size_t>(i + 1)) / 64u;
            if (p1 <= p0 && payloadLen > 0) { p0 = static_cast<size_t>(i) % payloadLen; p1 = p0 + 1; }
            size_t progBits = 0, totBits = (p1 - p0) * 8u;
            for (size_t k = p0; k < p1; ++k) {
                // In NOR flash .rpd, 0xFF is erased; count programmed ('0') bits or active transitions
                uint8_t inv = static_cast<uint8_t>(~payloadPtr[k]);
                while (inv) { progBits += (inv & 1u); inv >>= 1; }
            }
            float ratio = (totBits > 0) ? (static_cast<float>(progBits) / static_cast<float>(totBits)) : 0.0f;
            float px = fpgaX - 17.5f + (i % 8) * 5.0f;
            float py = fpgaY + 17.5f - (i / 8) * 5.0f;
            if (ratio > 0.02f) {
                ++activePages;
                float sz = std::clamp(1.3f + ratio * 3.1f, 1.3f, 4.4f);
                EdaLayerId tl = (ratio > 0.35f) ? EdaLayerId::TopCopper : EdaLayerId::InnerCopper;
                out.pads.push_back({ px, py, sz, sz, 0.0f, 0.0f, 1, tl, L"LAB", L"" });
            } else {
                DetailEda100::AddRectOutline(out, px, py, 3.4f, 3.4f, EdaLayerId::BottomSilk, 0.08f);
            }
        }

        const wchar_t* vendorName = L"FPGA";
        if (vendor == RpdVendor::AlteraAs) vendorName = bitReversed ? L"Altera AS (LSb)" : L"Altera AS (MSb)";
        else if (vendor == RpdVendor::XilinxRaw) vendorName = bitReversed ? L"Xilinx SPI/BPI" : L"Xilinx Raw";
        else if (vendor == RpdVendor::LatticeIce40) vendorName = L"Lattice iCE40";
        else if (vendor == RpdVendor::LatticeEcp5) vendorName = L"Lattice ECP5";

        wchar_t syncMeta[96] = {};
        if (detectedIdcode != 0) {
            swprintf_s(syncMeta, L"Sync @0x%zX | IDCODE 0x%08X", syncOffset, detectedIdcode);
        } else {
            swprintf_s(syncMeta, L"Sync @0x%zX | %ls", syncOffset, bitReversed ? L"Bit-Reversed SPI" : L"MSb-First");
        }

        EdaComponent3D fpgaIc;
        fpgaIc.refDes = L"U_FPGA";
        fpgaIc.value = vendorName;
        fpgaIc.footprint = syncMeta;
        fpgaIc.x = fpgaX; fpgaIc.y = fpgaY;
        fpgaIc.widthMm = 45.0f; fpgaIc.lengthMm = 45.0f; fpgaIc.heightMm = 1.8f;
        fpgaIc.pinCount = 256;
        fpgaIc.category = EdaPartCategory::IC_Package;
        out.components.push_back(fpgaIc);

        out.labels.push_back({ 6.0f, 28.0f, 1.25f, EdaLayerId::SchLabel, std::wstring(vendorName) + L" Raw Flash Image (.rpd)" });
        out.labels.push_back({ 6.0f, -28.0f, 0.95f, EdaLayerId::TopSilk, syncMeta });
        out.formatTitle = std::wstring(vendorName) + L" Raw Programming Data (.rpd — " +
                          std::to_wstring(activePages) + L"/64 active blocks)";
        out.netCount = std::max<size_t>(4u, activePages);
        return activePages > 0;
    }

    return false;
}

} // namespace DetailHwBin

inline bool ParseFpgaBitstreamBoard(const uint8_t* data, size_t size, const std::wstring& extLower, ParsedEdaBoard& out) {
    if (!data || size < 16) return false;
    return DetailHwBin::ParseFpgaBitstream(data, size, extLower, out);
}

} // namespace Formats
