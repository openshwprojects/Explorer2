#pragma once
// AlteraRpdBitstream.h — Intel / Altera Raw Programming Data (.rpd) Active Serial Configuration Reader
// Parses Intel/Altera Active Serial configuration headers & section descriptors:
//   1. Arria 10 / Stratix 10 / Agilex Boot Header (`0x62294895` LE or bit-reversed `0xA9129446`) & section pointers
//   2. Cyclone / MAX 10 Active Serial `.rpd` (`0xFF` preamble + `0x6A` / bit-reversed `0x56` AS sync + option bits & SRAM/CFM sectors)
// Renders an Altera FPGA LAB / M9K / Active-Serial Sector Floorplan with ZERO fake SOIC-8 flash chips.
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

inline bool ParseAlteraRpdBitstreamBoard(
    const uint8_t* data,
    size_t size,
    const std::wstring& ext,
    ParsedEdaBoard& outBoard)
{
    if (ext != L".rpd" || !data || size < 64u || size > (128u << 20)) return false;

    auto bitRev8 = [](uint8_t v) noexcept -> uint8_t {
        v = static_cast<uint8_t>(((v & 0xF0u) >> 4) | ((v & 0x0Fu) << 4));
        v = static_cast<uint8_t>(((v & 0xCCu) >> 2) | ((v & 0x33u) << 2));
        v = static_cast<uint8_t>(((v & 0xAAu) >> 1) | ((v & 0x55u) << 1));
        return v;
    };

    struct RpdSection {
        std::wstring name;
        size_t offset = 0;
        size_t length = 0;
        size_t activeBytes = 0;
    };
    std::vector<RpdSection> sections;
    std::wstring familyLabel = L"Cyclone/MAX10";
    bool bitReversedLsbFirst = false;
    bool validHeader = false;

    // Variant A: Stratix 10 / Agilex / Arria 10 Boot Descriptor Magic (`0x62294895` or bit-reversed `0xA9129446`)
    for (size_t off : { size_t(0), size_t(0x10), size_t(0x20), size_t(0x100) }) {
        if (off + 32u > size) break;
        const uint32_t mLE = ByteIO::RdU32LE(data + off);
        uint8_t br[4] = { bitRev8(data[off]), bitRev8(data[off + 1]), bitRev8(data[off + 2]), bitRev8(data[off + 3]) };
        const uint32_t mRev = ByteIO::RdU32LE(br);
        if (mLE == 0x62294895u || mRev == 0x62294895u) {
            validHeader = true;
            bitReversedLsbFirst = (mRev == 0x62294895u && mLE != 0x62294895u);
            familyLabel = L"Stratix10/Agilex";

            auto rdHdr32 = [&](size_t ho) -> uint32_t {
                if (!bitReversedLsbFirst) return ByteIO::RdU32LE(data + ho);
                uint8_t b4[4] = { bitRev8(data[ho]), bitRev8(data[ho + 1]), bitRev8(data[ho + 2]), bitRev8(data[ho + 3]) };
                return ByteIO::RdU32LE(b4);
            };
            // Parse up to 4 section pointer pairs (offset, length) starting after magic + version
            for (size_t s = 0; s < 4u && off + 16u + s * 8u <= size; ++s) {
                const uint32_t sOff = rdHdr32(off + 8u + s * 8u);
                const uint32_t sLen = rdHdr32(off + 12u + s * 8u);
                if (sOff > 0u && sOff < size && sLen > 0u && sOff + sLen <= size) {
                    size_t act = 0u;
                    for (size_t k = sOff; k < sOff + sLen; ++k) if (data[k] != 0xFFu) ++act;
                    sections.push_back({ L"SEC" + std::to_wstring(s), sOff, sLen, act });
                }
            }
            break;
        }
    }

    // Variant B: Cyclone / MAX 10 Active Serial `.rpd` (`>= 16` leading `0xFF` pad bytes followed by `0x6A` or bit-reversed `0x56`)
    size_t syncOff = 0u;
    if (!validHeader) {
        size_t ffRun = 0u;
        while (ffRun < std::min<size_t>(size, 8192u) && data[ffRun] == 0xFFu) ++ffRun;
        if (ffRun >= 16u && ffRun + 16u <= size) {
            const uint8_t syncByte = data[ffRun];
            if (syncByte == 0x6Au || syncByte == 0x56u) { // 0x56 == BitRev8(0x6A)!
                validHeader = true;
                bitReversedLsbFirst = (syncByte == 0x56u);
                syncOff = ffRun;
            }
        }
    }

    if (!validHeader) return false;

    // Partition non-0xFF Active Serial stream into Option-Bits Header + Configuration Frame Sectors
    if (sections.empty()) {
        const size_t optLen = std::min<size_t>(64u, size - syncOff);
        size_t optAct = 0u;
        for (size_t k = syncOff; k < syncOff + optLen; ++k) if (data[k] != 0xFFu) ++optAct;
        sections.push_back({ L"AS_OPT_HDR", syncOff, optLen, optAct });

        size_t cur = syncOff + optLen;
        size_t secIdx = 0u;
        const size_t sectorStep = std::max<size_t>(64u, (size - cur) / 4u);
        while (cur < size && sections.size() < 8u) {
            const size_t slen = std::min<size_t>(sectorStep, size - cur);
            size_t act = 0u;
            for (size_t k = cur; k < cur + slen; ++k) {
                if (data[k] != 0xFFu) ++act;
            }
            if (act > 0u) {
                sections.push_back({ L"CFM_SRAM_" + std::to_wstring(secIdx++), cur, slen, act });
            }
            cur += slen;
        }
    }

    size_t totalActiveBytes = 0u;
    for (const auto& s : sections) totalActiveBytes += s.activeBytes;
    if (totalActiveBytes == 0u) return false;

    outBoard.kind = EdaDocKind::KiCadPcb;

    const int cols = 10, rows = 6;
    const float cellW = 4.2f, cellH = 3.4f;
    const float arrW = static_cast<float>(cols) * cellW;
    const float arrH = static_cast<float>(rows) * cellH;
    const float dieW = arrW + 12.0f;
    const float dieH = arrH + 18.0f;

    DrawEdaBoxRect(outBoard, -dieW * 0.5f, -dieH * 0.5f, dieW * 0.5f, dieH * 0.5f, 0.36f, EdaLayerId::EdgeCuts, 0);
    DrawEdaBoxRect(outBoard, -arrW * 0.5f - 1.5f, -arrH * 0.5f + 1.5f,
                   arrW * 0.5f + 1.5f, arrH * 0.5f + 4.5f, 0.20f, EdaLayerId::TopSilk, 0);

    // Render LAB / M9K array at top of die
    const int totalTiles = cols * rows;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const int idx = r * cols + c;
            const bool isM9k = (c == 2 || c == cols - 3);
            const float tx = -arrW * 0.5f + (static_cast<float>(c) + 0.5f) * cellW;
            const float ty =  arrH * 0.5f + 3.0f - (static_cast<float>(r) + 0.5f) * cellH;
            DrawEdaBoxRect(outBoard, tx - cellW * 0.42f, ty - cellH * 0.40f,
                           tx + cellW * 0.42f, ty + cellH * 0.40f,
                           0.15f, isM9k ? EdaLayerId::InnerCopper : EdaLayerId::BottomCopper, isM9k ? 2 : 3);

            const size_t sampleOff = syncOff + (static_cast<size_t>(idx) * (size - syncOff)) / static_cast<size_t>(totalTiles);
            if (sampleOff < size && data[sampleOff] != 0xFFu) {
                outBoard.pads.push_back({
                    tx, ty, 0.62f, 1.30f, 0.0f, 0.0f, 1,
                    isM9k ? EdaLayerId::InnerCopper : EdaLayerId::TopCopper,
                    isM9k ? L"M9K" : L"LAB", familyLabel
                });
            }
        }
    }

    // Render Active Serial configuration section map along bottom
    const size_t nSec = sections.size();
    const float secW = (arrW - static_cast<float>(nSec - 1u) * 1.2f) / static_cast<float>(std::max<size_t>(1u, nSec));
    const float secY = -arrH * 0.5f - 2.6f;
    for (size_t si = 0; si < nSec; ++si) {
        const auto& sec = sections[si];
        const float sx0 = -arrW * 0.5f + static_cast<float>(si) * (secW + 1.2f);
        const float sx1 = sx0 + secW;
        DrawEdaBoxRect(outBoard, sx0, secY - 1.8f, sx1, secY + 1.8f, 0.24f, EdaLayerId::InnerCopper, static_cast<int>(10 + si));
        wchar_t sLbl[96] = {};
        swprintf_s(sLbl, L"%ls (+0x%zX, %zu B)", sec.name.c_str(), sec.offset, sec.activeBytes);
        outBoard.labels.push_back({ sx0 + 0.5f, secY - 0.35f, 0.68f, EdaLayerId::SchLabel, sLbl });
        outBoard.pads.push_back({
            (sx0 + sx1) * 0.5f, secY + 0.8f, 0.7f, 0.7f, 0.0f, 0.0f, 1,
            EdaLayerId::TopCopper, sec.name, L"AS_SECTION"
        });
    }

    EdaComponent3D dieComp;
    dieComp.refDes = L"U_FPGA";
    dieComp.value = familyLabel;
    dieComp.footprint = L"ALTERA_AS_DIE";
    dieComp.widthMm = dieW; dieComp.lengthMm = dieH; dieComp.heightMm = 1.4f;
    dieComp.pinCount = static_cast<int>(outBoard.pads.size());
    dieComp.category = EdaPartCategory::IC_Package;
    outBoard.components.push_back(std::move(dieComp));

    wchar_t titleBuf[240] = {};
    swprintf_s(titleBuf, L"Intel / Altera AS Raw Programming Data (.rpd — %ls, %ls, %zu active B, %zu sections)",
               familyLabel.c_str(),
               bitReversedLsbFirst ? L"LSB-first AS" : L"MSB-first AS",
               totalActiveBytes, sections.size());
    outBoard.labels.push_back({ -dieW * 0.48f, dieH * 0.5f + 2.0f, 1.15f, EdaLayerId::TopSilk, titleBuf });
    outBoard.formatTitle = titleBuf;
    outBoard.netCount = std::max<size_t>(2u, sections.size() + 1u);
    return true;
}

} // namespace Formats
