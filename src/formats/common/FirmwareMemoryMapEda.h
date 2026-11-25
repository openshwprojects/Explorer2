#pragma once
// FirmwareMemoryMapEda.h — Shared Firmware / Memory Image Span Coalescer & Memory Map Renderer
// Used by IntelHex, MotorolaSrec, TiTxtFirmware, TektronixHexFirmware, XilinxMemoryCoe, and QuartusMemoryMif.
// NOTE: Included inside `namespace Explorer2`. Do NOT add `#include` directives here.

namespace FirmwareMemoryMapEda {

struct MemorySpan {
    uint64_t startAddr = 0;
    size_t wordCount = 0;
    uint32_t bitsPerWord = 8;
    double fillFraction = 0.0;
    std::vector<float> rowLevels;
};

struct MemorySymbol {
    uint64_t addr = 0;
    std::string name;
};

// Build coalesced MemorySpans from a sparse `addr -> byte` map (used by IntelHex, MotorolaSrec, TektronixHexFirmware)
inline void BuildByteSpansFromMap(
    const std::map<uint64_t, uint8_t>& memBytes,
    std::vector<MemorySpan>& outSpans,
    uint64_t mergeGapTolerance = 0ULL)
{
    outSpans.clear();
    auto it = memBytes.begin();
    while (it != memBytes.end()) {
        const uint64_t startA = it->first;
        std::vector<uint8_t> chunk;
        uint64_t curA = startA;
        while (it != memBytes.end() && it->first <= curA + mergeGapTolerance) {
            while (curA < it->first) {
                chunk.push_back(0xFFu);
                ++curA;
            }
            chunk.push_back(it->second);
            ++curA;
            ++it;
        }

        MemorySpan sp;
        sp.startAddr = startA;
        sp.wordCount = chunk.size();
        sp.bitsPerWord = 8u;
        size_t nonErased = 0;
        for (uint8_t b : chunk) {
            if (b != 0xFFu && b != 0x00u) ++nonErased;
        }
        sp.fillFraction = chunk.empty() ? 0.0 : (static_cast<double>(nonErased) / static_cast<double>(chunk.size()));

        const size_t bins = std::clamp<size_t>((chunk.size() + 7u) / 8u, 2u, 24u);
        sp.rowLevels.resize(bins, 0.1f);
        for (size_t bi = 0; bi < bins; ++bi) {
            const size_t i0 = (bi * chunk.size()) / bins;
            const size_t i1 = std::max(i0 + 1u, ((bi + 1u) * chunk.size()) / bins);
            double sum = 0.0;
            for (size_t k = i0; k < i1 && k < chunk.size(); ++k) {
                sum += static_cast<double>(chunk[k]) / 255.0;
            }
            sp.rowLevels[bi] = static_cast<float>(sum / static_cast<double>(i1 - i0));
        }
        outSpans.push_back(std::move(sp));
    }
}

inline bool EmitMemoryMapBoard(
    const std::wstring& formatTitle,
    const std::wstring& footprintTag,
    const std::vector<MemorySpan>& spans,
    bool hasEntry,
    uint64_t entryAddr,
    ParsedEdaBoard& outBoard,
    const std::vector<MemorySymbol>* symbols = nullptr)
{
    if (spans.empty()) return false;

    size_t totalWords = 0;
    uint64_t maxEndAddr = 0;
    for (const auto& sp : spans) {
        if (sp.wordCount == 0) continue;
        totalWords += sp.wordCount;
        const uint64_t endInc = sp.startAddr + sp.wordCount - 1u;
        maxEndAddr = std::max(maxEndAddr, endInc);
    }
    if (totalWords == 0) return false;

    const bool wideAddr = (maxEndAddr > 0xFFFFULL) || (hasEntry && entryAddr > 0xFFFFULL);
    auto fmtAddr = [wideAddr](wchar_t* buf, size_t cap, uint64_t addr) {
        if (wideAddr) {
            swprintf_s(buf, cap, L"0x%08llX", static_cast<unsigned long long>(addr));
        } else {
            swprintf_s(buf, cap, L"0x%04llX", static_cast<unsigned long long>(addr));
        }
    };

    outBoard.kind = EdaDocKind::KiCadSch;
    outBoard.formatTitle = formatTitle;

    const size_t nShow = std::min<size_t>(spans.size(), 32u);
    const float blockW = 72.0f;
    const float spineX = -6.0f;
    float curY = 0.0f;

    // Header summary banner
    {
        wchar_t minBuf[32] = {}, maxBuf[32] = {};
        fmtAddr(minBuf, 32, spans.front().startAddr);
        fmtAddr(maxBuf, 32, maxEndAddr);
        wchar_t hdrBuf[260] = {};
        if (hasEntry) {
            wchar_t entBuf[32] = {};
            fmtAddr(entBuf, 32, entryAddr);
            swprintf_s(hdrBuf, L"%ls  [%ls .. %ls | Entry %ls]",
                       formatTitle.c_str(), minBuf, maxBuf, entBuf);
        } else {
            swprintf_s(hdrBuf, L"%ls  [%ls .. %ls]",
                       formatTitle.c_str(), minBuf, maxBuf);
        }
        outBoard.labels.push_back({ 0.0f, 5.5f, 1.45f, EdaLayerId::TopSilk, hdrBuf });
    }

    for (size_t i = 0; i < nShow; ++i) {
        const auto& sp = spans[i];
        if (sp.wordCount == 0) continue;

        const float blockH = std::clamp(6.5f + std::log2(static_cast<float>(sp.wordCount) + 1.0f) * 1.35f, 7.5f, 22.0f);
        const float yTop = curY;
        const float yBot = curY - blockH;
        const float midY = (yTop + yBot) * 0.5f;

        DrawEdaBoxRect(outBoard, 0.0f, yBot, blockW, yTop, 0.30f, EdaLayerId::EdgeCuts, 0);

        // Bus tap from address spine
        outBoard.traces.push_back({ spineX, midY, 0.0f, midY, 0.28f, EdaLayerId::InnerCopper, static_cast<int>(i + 1) });
        outBoard.pads.push_back({
            0.0f, midY, 1.0f, 1.0f, 0.0f, 0.0f, 1, EdaLayerId::TopCopper,
            L"ADDR", footprintTag
        });

        // Row density / data profile bars inside region block
        const size_t nBars = std::max<size_t>(1u, sp.rowLevels.size());
        const float sliceW = (blockW - 4.0f) / static_cast<float>(nBars);
        for (size_t s = 0; s < nBars; ++s) {
            const float lvl = std::clamp(sp.rowLevels.empty() ? static_cast<float>(sp.fillFraction) : sp.rowLevels[s], 0.05f, 1.0f);
            const float barH = std::max(0.65f, lvl * (blockH - 3.4f));
            const float sx = 2.0f + (static_cast<float>(s) + 0.5f) * sliceW;
            const EdaLayerId lyr = (sp.fillFraction > 0.02) ? EdaLayerId::TopCopper : EdaLayerId::BottomCopper;
            outBoard.traces.push_back({
                sx, yBot + 1.0f, sx, yBot + 1.0f + barH,
                std::max(0.25f, std::min(2.4f, sliceW * 0.65f)), lyr, static_cast<int>(i + 1)
            });
        }

        const uint64_t endInclusive = sp.startAddr + sp.wordCount - 1u;
        wchar_t sAddrBuf[32] = {}, eAddrBuf[32] = {};
        fmtAddr(sAddrBuf, 32, sp.startAddr);
        fmtAddr(eAddrBuf, 32, endInclusive);

        wchar_t addrLbl[180] = {};
        if (sp.bitsPerWord == 8u) {
            swprintf_s(addrLbl, L"%ls .. %ls  (%zu B, %.0f%% used)",
                       sAddrBuf, eAddrBuf, sp.wordCount, sp.fillFraction * 100.0);
        } else {
            swprintf_s(addrLbl, L"%ls .. %ls  (%zu x %u-bit, %.0f%% active)",
                       sAddrBuf, eAddrBuf, sp.wordCount, sp.bitsPerWord, sp.fillFraction * 100.0);
        }
        outBoard.labels.push_back({ 2.0f, yTop - 1.8f, 1.05f, EdaLayerId::SchLabel, addrLbl });

        // Highlight entry-point vector if it falls inside this span
        if (hasEntry && entryAddr >= sp.startAddr && entryAddr <= endInclusive) {
            wchar_t epLbl[64] = {};
            wchar_t epHex[32] = {};
            fmtAddr(epHex, 32, entryAddr);
            swprintf_s(epLbl, L"ENTRY -> %ls", epHex);
            outBoard.labels.push_back({ blockW + 2.0f, midY - 0.35f, 0.95f, EdaLayerId::TopSilk, epLbl });
            outBoard.pads.push_back({ blockW, midY, 1.1f, 1.1f, 0.0f, 0.0f, 0, EdaLayerId::InnerCopper, L"ENTRY", epHex });
        }

        EdaComponent3D comp;
        wchar_t refBuf[32] = {};
        swprintf_s(refBuf, L"MEM%zu", i + 1u);
        comp.refDes = refBuf;
        comp.value = addrLbl;
        comp.footprint = footprintTag.empty() ? L"ROM_REGION" : footprintTag;
        comp.x = blockW * 0.5f;
        comp.y = midY;
        comp.widthMm = blockW;
        comp.lengthMm = blockH;
        comp.heightMm = 1.2f;
        comp.pinCount = 8;
        comp.category = EdaPartCategory::IC_Package;
        outBoard.components.push_back(std::move(comp));

        if (i + 1u < nShow && spans[i + 1u].startAddr > endInclusive + 1ULL) {
            const uint64_t gap = spans[i + 1u].startAddr - (endInclusive + 1ULL);
            wchar_t gapLbl[96] = {};
            if (gap >= 1024ULL) {
                swprintf_s(gapLbl, L"--- gap %llu KB ---", static_cast<unsigned long long>(gap / 1024ULL));
            } else {
                swprintf_s(gapLbl, L"--- gap %llu B ---", static_cast<unsigned long long>(gap));
            }
            outBoard.labels.push_back({ 4.0f, yBot - 2.0f, 0.82f, EdaLayerId::BottomSilk, gapLbl });
        }

        curY = yBot - 3.5f;
    }

    // Address bus vertical spine
    outBoard.traces.push_back({ spineX, 1.5f, spineX, curY + 2.5f, 0.45f, EdaLayerId::InnerCopper, 1 });

    // Optional symbol table callouts (e.g. Tektronix Extended Hex Type '3' symbol records)
    if (symbols && !symbols->empty()) {
        const size_t nSym = std::min<size_t>(symbols->size(), 16u);
        for (size_t si = 0; si < nSym; ++si) {
            wchar_t symAddr[32] = {};
            fmtAddr(symAddr, 32, (*symbols)[si].addr);
            std::wstring symLbl = std::wstring(symAddr) + L" " + PathUtils::Utf8ToWide((*symbols)[si].name);
            outBoard.labels.push_back({ blockW + 22.0f, -static_cast<float>(si) * 2.6f, 0.88f, EdaLayerId::SchLabel, symLbl });
        }
    }

    outBoard.netCount = std::max<size_t>(1u, spans.size() + (hasEntry ? 1u : 0u));
    return !outBoard.components.empty();
}

} // namespace FirmwareMemoryMapEda
