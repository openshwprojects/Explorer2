#pragma once
// AlteraBitstreamCommon.h — Shared Intel / Altera Quartus .SOF & .POF Packet Directory Parser & FPGA Floorplan Renderer
// Eliminates duplication between IntelFpgaSof.h and IntelFpgaPof.h and renders structured LAB / M9K / CFM
// sectors rather than raw popcount wire meshes.
// NOTE: Included inside `namespace Explorer2`. Do NOT add `#include` directives here.

namespace Formats {
namespace AlteraBitstreamCommon {

struct AlteraPacketEntry {
    uint16_t tag = 0;
    uint32_t length = 0;
    size_t   payloadOff = 0;
};

struct AlteraDecodedContainer {
    bool isPof = false;
    uint32_t headerVer = 0;
    uint32_t declaredPackets = 0;
    std::string creatorTool;
    std::string devicePart;
    uint32_t userCode = 0;
    bool hasUserCode = false;
    uint32_t optionCrc = 0;
    std::vector<AlteraPacketEntry> configSections; // SRAM frames (SOF) or CFM/UFM sectors (POF)
    size_t totalConfigBytes = 0;
};

inline std::string ExtractCleanAsciiField(const uint8_t* p, size_t len) {
    // Scan for longest printable ASCII run (e.g. "EP4CE22F17C6", "10M50DAF484C7G", "Quartus Prime...")
    std::string best;
    std::string cur;
    for (size_t i = 0; i < len; ++i) {
        const uint8_t c = p[i];
        if (c >= 0x20u && c <= 0x7Eu) {
            cur.push_back(static_cast<char>(c));
        } else {
            std::string_view t = ReaderText::TrimSv(cur);
            if (t.size() > best.size()) best.assign(t);
            cur.clear();
        }
    }
    std::string_view t = ReaderText::TrimSv(cur);
    if (t.size() > best.size()) best.assign(t);
    return best;
}

inline bool LooksLikeAlteraPartName(std::string_view s) {
    if (s.size() < 4 || s.size() > 48) return false;
    for (char c : s) {
        if (c == ' ' || c == '\t') return false;
    }
    return ReaderText::StartsWithCI(s, "EP") ||
           ReaderText::StartsWithCI(s, "5C") ||
           ReaderText::StartsWithCI(s, "5S") ||
           ReaderText::StartsWithCI(s, "5A") ||
           ReaderText::StartsWithCI(s, "5M") ||
           ReaderText::StartsWithCI(s, "10M") ||
           ReaderText::StartsWithCI(s, "10C") ||
           ReaderText::StartsWithCI(s, "10A") ||
           ReaderText::StartsWithCI(s, "1SG") ||
           ReaderText::StartsWithCI(s, "1SX") ||
           ReaderText::StartsWithCI(s, "AG") ||
           ReaderText::StartsWithCI(s, "EPM");
}

// Parse Altera "SOF\0" or "POF\0" binary TLV packet container:
// Bytes 0..3: "SOF\0" or "POF\0"
// Bytes 4..7: version / header word (u32 LE)
// Bytes 8..11: packet count (u32 LE)
// Followed by TLV packets: [tag : u16 LE][len : u32 LE][payload : len bytes] cleanly reaching EOF
inline bool ParseAlteraPacketContainer(
    const uint8_t* data,
    size_t size,
    bool expectPof,
    AlteraDecodedContainer& out)
{
    if (!data || size < 18) return false;
    const char* expectedMagic = expectPof ? "POF\0" : "SOF\0";
    if (std::memcmp(data, expectedMagic, 4) != 0) return false;

    out = AlteraDecodedContainer{};
    out.isPof = expectPof;
    out.headerVer = ByteIO::RdU32LE(data + 4);
    out.declaredPackets = ByteIO::RdU32LE(data + 8);
    if (out.declaredPackets == 0 || out.declaredPackets > 65536u) return false;

    size_t pos = 12u;
    uint32_t parsedCount = 0;

    while (pos + 6u <= size && parsedCount < out.declaredPackets) {
        const uint16_t tag = ByteIO::RdU16LE(data + pos);
        const uint32_t plen = ByteIO::RdU32LE(data + pos + 2u);
        pos += 6u;
        if (plen > size - pos) return false;

        const uint8_t* pay = data + pos;

        // Decode standard Quartus SOF/POF packet tags:
        // Tag 1: Creator / Quartus tool string
        // Tag 2: Target FPGA / CPLD device part name
        // Tag 8 / 9 / 13 / 18: Option bits / Usercode / Checksum metadata
        // Tag 6 / 17 / >= 16 (large payload): SRAM bitstream or CFM/UFM flash sectors
        if (tag == 1 && plen > 0 && plen <= 512u) {
            std::string s = ExtractCleanAsciiField(pay, plen);
            if (!s.empty()) out.creatorTool = std::move(s);
        } else if (tag == 2 && plen > 0 && plen <= 256u) {
            std::string s = ExtractCleanAsciiField(pay, plen);
            if (!s.empty()) out.devicePart = std::move(s);
        } else if ((tag == 8 || tag == 18 || tag == 14) && plen >= 4u && plen <= 128u) {
            out.userCode = ByteIO::RdU32LE(pay);
            out.hasUserCode = true;
            if (plen >= 8u) out.optionCrc = ByteIO::RdU32LE(pay + plen - 4u);
        }

        // Fallback if creator/part were stored under alternate tag IDs in older Quartus II
        if (out.devicePart.empty() && plen >= 4u && plen <= 128u) {
            std::string cand = ExtractCleanAsciiField(pay, plen);
            if (LooksLikeAlteraPartName(cand)) out.devicePart = std::move(cand);
        }
        if (out.creatorTool.empty() && plen >= 6u && plen <= 256u) {
            std::string cand = ExtractCleanAsciiField(pay, plen);
            if (ContainsTextCI(cand, "Quartus") || ContainsTextCI(cand, "MAX+PLUS")) {
                out.creatorTool = std::move(cand);
            }
        }

        // Configuration data sections (Tag 6 / 17 / 3 / 5 / >=16 with binary bitstream/CFM data)
        const bool isMetaTag = (tag == 1 || tag == 2);
        if (!isMetaTag && (tag == 6 || tag == 17 || tag == 19 || tag == 21 || plen >= 32u)) {
            out.configSections.push_back({ tag, plen, pos });
            out.totalConfigBytes += plen;
        }

        pos += plen;
        ++parsedCount;
    }

    // Must match declared packet count and reach exact EOF (allowing at most 4 trailing CRC/pad bytes)
    if (parsedCount != out.declaredPackets) return false;
    if (size - pos > 4u) return false;
    if (out.configSections.empty() && out.devicePart.empty() && out.creatorTool.empty()) return false;
    return true;
}

inline bool RenderAlteraFloorplanToBoard(
    const uint8_t* data,
    size_t size,
    const AlteraDecodedContainer& info,
    ParsedEdaBoard& outBoard)
{
    outBoard.kind = EdaDocKind::KiCadPcb;

    const std::wstring wPart = PathUtils::Utf8ToWide(info.devicePart.empty() ? (info.isPof ? "MAX10_CFM" : "CYCLONE_FPGA") : info.devicePart);
    const std::wstring wKind = info.isPof ? L"Intel/Altera Programmer Object File (.pof)" : L"Intel/Altera SRAM Object File (.sof)";

    // Infer LAB array & M9K RAM column geometry from device family & configuration size
    int labCols = 8;
    int labRows = 6;
    if (ContainsTextCI(info.devicePart, "10M50") || ContainsTextCI(info.devicePart, "EP4CE115") ||
        ContainsTextCI(info.devicePart, "5CS") || ContainsTextCI(info.devicePart, "5SG") ||
        info.totalConfigBytes >= 65536u) {
        labCols = 14; labRows = 10;
    } else if (ContainsTextCI(info.devicePart, "EP4CE22") || ContainsTextCI(info.devicePart, "10M08") ||
               info.totalConfigBytes >= 4096u) {
        labCols = 10; labRows = 8;
    }

    const float cellW = 4.2f;
    const float cellH = 3.4f;
    const float arrayW = static_cast<float>(labCols) * cellW;
    const float arrayH = static_cast<float>(labRows) * cellH;
    const float dieW = arrayW + 12.0f;
    const float dieH = arrayH + (info.isPof ? 20.0f : 12.0f);

    // Outer die seal-ring + I/O Bank perimeter
    DrawEdaBoxRect(outBoard, -dieW * 0.5f, -dieH * 0.5f, dieW * 0.5f, dieH * 0.5f, 0.38f, EdaLayerId::EdgeCuts, 0);
    DrawEdaBoxRect(outBoard, -arrayW * 0.5f - 1.8f, -arrayH * 0.5f - 1.8f,
                   arrayW * 0.5f + 1.8f, arrayH * 0.5f + 1.8f, 0.20f, EdaLayerId::TopSilk, 0);

    // Header & metadata banner
    outBoard.labels.push_back({ -dieW * 0.48f, dieH * 0.5f + 2.2f, 1.35f, EdaLayerId::TopSilk,
                                wKind + L" — " + wPart });
    if (!info.creatorTool.empty()) {
        outBoard.labels.push_back({ -dieW * 0.48f, dieH * 0.5f + 0.4f, 0.85f, EdaLayerId::SchLabel,
                                    PathUtils::Utf8ToWide(info.creatorTool) });
    }

    // Collect configuration payload bytes to measure real per-sector utilization (non-0xFF for POF, non-0x00 for SOF)
    std::vector<uint8_t> cfgStream;
    cfgStream.reserve(std::min<size_t>(info.totalConfigBytes, 262144u));
    for (const auto& sec : info.configSections) {
        if (sec.payloadOff + sec.length <= size && cfgStream.size() < 262144u) {
            const size_t take = std::min<size_t>(sec.length, 262144u - cfgStream.size());
            cfgStream.insert(cfgStream.end(), data + sec.payloadOff, data + sec.payloadOff + take);
        }
    }

    const uint8_t erasedByte = info.isPof ? 0xFFu : 0x00u;

    // 4 Corner PLL blocks
    for (int cx = -1; cx <= 1; cx += 2) {
        for (int cy = -1; cy <= 1; cy += 2) {
            const float px = static_cast<float>(cx) * (arrayW * 0.5f + 3.4f);
            const float py = static_cast<float>(cy) * (arrayH * 0.5f + 3.0f);
            DrawEdaBoxRect(outBoard, px - 1.4f, py - 1.1f, px + 1.4f, py + 1.1f, 0.22f, EdaLayerId::InnerCopper, 1);
        }
    }

    // Global clock network H-tree spine
    outBoard.traces.push_back({ -arrayW * 0.5f, 0.0f, arrayW * 0.5f, 0.0f, 0.28f, EdaLayerId::InnerCopper, 1 });
    outBoard.traces.push_back({ 0.0f, -arrayH * 0.5f, 0.0f, arrayH * 0.5f, 0.28f, EdaLayerId::InnerCopper, 1 });

    // Render LAB Logic Array + M9K/M10K Embedded Memory Columns (columns 2 and labCols-3)
    const int totalTiles = labCols * labRows;
    size_t activeTileCount = 0;

    for (int r = 0; r < labRows; ++r) {
        for (int c = 0; c < labCols; ++c) {
            const int tileIdx = r * labCols + c;
            const bool isM9kCol = (c == 2 || (labCols >= 8 && c == labCols - 3));
            const float tx = -arrayW * 0.5f + (static_cast<float>(c) + 0.5f) * cellW;
            const float ty =  arrayH * 0.5f - (static_cast<float>(r) + 0.5f) * cellH;

            // Inspect corresponding slice of configuration stream
            size_t activeBytesInSlice = 0;
            if (!cfgStream.empty()) {
                const size_t s0 = (static_cast<size_t>(tileIdx) * cfgStream.size()) / static_cast<size_t>(totalTiles);
                const size_t s1 = std::max(s0 + 1u, (static_cast<size_t>(tileIdx + 1) * cfgStream.size()) / static_cast<size_t>(totalTiles));
                for (size_t k = s0; k < s1 && k < cfgStream.size(); ++k) {
                    if (cfgStream[k] != erasedByte) ++activeBytesInSlice;
                }
            }

            const EdaLayerId outlineLyr = isM9kCol ? EdaLayerId::InnerCopper : EdaLayerId::BottomCopper;
            DrawEdaBoxRect(outBoard,
                tx - cellW * 0.42f, ty - cellH * 0.40f,
                tx + cellW * 0.42f, ty + cellH * 0.40f,
                0.15f, outlineLyr, isM9kCol ? 2 : 3);

            if (activeBytesInSlice > 0) {
                ++activeTileCount;
                const EdaLayerId padLyr = isM9kCol ? EdaLayerId::InnerCopper : EdaLayerId::TopCopper;
                // Sub-frame pads proportional to configured frame bytes in this LAB/M9K tile (1..4 LE cluster pads)
                const size_t subPads = std::clamp<size_t>(1u + activeBytesInSlice / 16u, 1u, 4u);
                for (size_t sp = 0; sp < subPads; ++sp) {
                    const float ox = (subPads == 1u) ? 0.0f : ((static_cast<float>(sp) - static_cast<float>(subPads - 1u) * 0.5f) * 0.72f);
                    outBoard.pads.push_back({
                        tx + ox, ty, 0.58f, 1.35f, 0.0f, 0.0f, 1, padLyr,
                        isM9kCol ? L"M9K" : L"LAB", wPart
                    });
                }
            }
        }
    }

    // For POF non-volatile images, render explicit CFM0 / CFM1 / UFM Flash Sector blocks along bottom of die
    if (info.isPof && !info.configSections.empty()) {
        const size_t nSec = std::min<size_t>(info.configSections.size(), 6u);
        const float secTotalW = arrayW;
        const float secW = (secTotalW - static_cast<float>(nSec - 1u) * 1.2f) / static_cast<float>(nSec);
        const float secY = -arrayH * 0.5f - 5.4f;

        for (size_t si = 0; si < nSec; ++si) {
            const auto& sec = info.configSections[si];
            const float sx0 = -secTotalW * 0.5f + static_cast<float>(si) * (secW + 1.2f);
            const float sx1 = sx0 + secW;
            DrawEdaBoxRect(outBoard, sx0, secY - 1.8f, sx1, secY + 1.8f, 0.26f, EdaLayerId::TopCopper, static_cast<int>(10 + si));

            wchar_t cfmLbl[96] = {};
            swprintf_s(cfmLbl, L"CFM%zu (%u B)", si, sec.length);
            outBoard.labels.push_back({ sx0 + 0.6f, secY - 0.35f, 0.72f, EdaLayerId::SchLabel, cfmLbl });

            // Sector page pads proportional to programmed non-0xFF pages (1 pad per 64 programmed bytes)
            size_t progBytes = 0;
            if (sec.payloadOff + sec.length <= size) {
                for (uint32_t k = 0; k < sec.length; ++k) {
                    if (data[sec.payloadOff + k] != 0xFFu) ++progBytes;
                }
            }
            const size_t nPagePads = std::clamp<size_t>((progBytes + 31u) / 32u, 1u, 16u);
            for (size_t pi = 0; pi < nPagePads; ++pi) {
                const float px = sx0 + 0.8f + (static_cast<float>(pi) + 0.5f) * ((secW - 1.6f) / static_cast<float>(nPagePads));
                outBoard.pads.push_back({ px, secY + 0.8f, 0.45f, 0.65f, 0.0f, 0.0f, 1, EdaLayerId::TopCopper, L"CFM_PG", cfmLbl });
            }
        }
    }

    // Die component summary
    EdaComponent3D dieComp;
    dieComp.refDes = L"U_FPGA";
    dieComp.value = wPart;
    dieComp.footprint = info.isPof ? L"ALTERA_POF_CFM" : L"ALTERA_SOF_SRAM";
    dieComp.x = 0.0f;
    dieComp.y = 0.0f;
    dieComp.widthMm = dieW;
    dieComp.lengthMm = dieH;
    dieComp.heightMm = 1.4f;
    dieComp.pinCount = static_cast<int>(std::max<size_t>(4u, outBoard.pads.size()));
    dieComp.category = EdaPartCategory::IC_Package;
    outBoard.components.push_back(std::move(dieComp));

    wchar_t statLbl[180] = {};
    if (info.hasUserCode) {
        swprintf_s(statLbl, L"Config: %zu B  |  Active tiles: %zu/%d  |  USERCODE 0x%08X",
                   info.totalConfigBytes, activeTileCount, totalTiles, info.userCode);
    } else {
        swprintf_s(statLbl, L"Config: %zu B  |  Active tiles: %zu/%d  |  Sections: %zu",
                   info.totalConfigBytes, activeTileCount, totalTiles, info.configSections.size());
    }
    outBoard.labels.push_back({ -dieW * 0.48f, -dieH * 0.5f - 2.2f, 0.90f, EdaLayerId::BottomSilk, statLbl });

    wchar_t titleBuf[240] = {};
    swprintf_s(titleBuf, L"%ls — %ls (%zu bytes, %zu active tiles)",
               wKind.c_str(), wPart.c_str(), info.totalConfigBytes, activeTileCount);
    outBoard.formatTitle = titleBuf;
    outBoard.netCount = std::max<size_t>(2u, info.configSections.size() + 2u);
    return true;
}

} // namespace AlteraBitstreamCommon
} // namespace Formats
