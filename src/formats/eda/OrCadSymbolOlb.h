#pragma once
// OrCadSymbolOlb.h — Cadence OrCAD Capture OLE2 Symbol Library (.olb) Reader
// Parses real Cadence OrCAD Capture OLE2 symbol libraries (`Library` string pool + `Packages/*` and
// `Symbols/*` `LibraryPart` / `SthInPages0` streams) using shared `DetailOrCadBin` helpers.
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

inline bool ParseOrCadSymbolOlbBoard(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    if (!data || size < 512 || std::memcmp(data, "\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1", 8) != 0) {
        return false;
    }

    Ole2::CompoundFile cf;
    if (!Ole2::ParseCompoundFile(data, size, cf) || !cf.valid) {
        return false;
    }

    bool hasOrCadMarkers = false;
    const Ole2::DirEntry* libraryEntry = nullptr;
    std::vector<const Ole2::DirEntry*> pkgStreams;
    std::vector<const Ole2::DirEntry*> symStreams;

    for (const auto& e : cf.entries) {
        const std::wstring lowPath = PathUtils::ToLower(e.fullPath);
        const std::wstring lowName = PathUtils::ToLower(e.name);
        if (lowPath.find(L"packages/") != std::wstring::npos ||
            lowPath.find(L"symbols/") != std::wstring::npos ||
            lowName == L"cis" || lowName == L"hsobjects" || lowName == L"admindata" || lowName == L"$packages")
        {
            hasOrCadMarkers = true;
        }
        if (e.objectType == 2 && lowName == L"library" && e.size >= 32) {
            libraryEntry = &e;
        }
        if (e.objectType == 2 && e.size >= 16 &&
            lowPath.find(L"directory") == std::wstring::npos &&
            lowName != L"$types$" && lowName != L"$packages")
        {
            if (lowPath.find(L"packages/") != std::wstring::npos) {
                pkgStreams.push_back(&e);
            } else if (lowPath.find(L"symbols/") != std::wstring::npos) {
                symStreams.push_back(&e);
            }
        }
    }

    std::vector<std::string> libStrings;
    if (libraryEntry) {
        std::vector<uint8_t> libStm;
        if (Ole2::ExtractStream(cf, *libraryEntry, libStm, 16 * 1024 * 1024) &&
            DetailOrCadBin::ParseOrCadLibraryStream(libStm, libStrings))
        {
            hasOrCadMarkers = true;
        }
    }

    const std::vector<const Ole2::DirEntry*>& targetStreams = pkgStreams.empty() ? symStreams : pkgStreams;
    if (!hasOrCadMarkers || targetStreams.empty()) {
        return false;
    }

    out.kind = EdaDocKind::KiCadSch;
    float sheetOffsetX = 0.0f;
    size_t renderedSymbols = 0;

    for (size_t si = 0; si < targetStreams.size() && si < 64; ++si) {
        std::vector<uint8_t> stm;
        if (!Ole2::ExtractStream(cf, *targetStreams[si], stm, 16 * 1024 * 1024) || stm.size() < 16) {
            continue;
        }

        const uint8_t* b = stm.data();
        const size_t n = stm.size();
        const std::string symTitle = PathUtils::WideToUtf8(targetStreams[si]->name);

        std::unordered_map<std::string, DetailOrCadBin::OrCadCachedSym> localParts;
        DetailOrCadBin::CollectCachedLibraryParts(stm, symTitle, localParts);

        const DetailOrCadBin::OrCadCachedSym* bestSym = nullptr;
        const std::string kTarget = DetailOrCadBin::UpperKey(symTitle);
        auto itT = localParts.find(kTarget);
        if (itT != localParts.end()) {
            bestSym = &itT->second;
        } else if (!localParts.empty()) {
            bestSym = &localParts.begin()->second;
        }

        DetailOrCadBin::OrCadCachedSym fallbackSym;
        if (!bestSym) {
            for (size_t off = 0; off + 12 < n; ++off) {
                if (b[off] == 2) {
                    size_t sp = off;
                    if (DetailOrCadBin::ParseOrCadSthInPages0(b, n, sp, n, fallbackSym, nullptr)) {
                        bestSym = &fallbackSym;
                        break;
                    }
                }
            }
        }

        if (!bestSym) continue;

        const float gScale = 0.254f; // 10 OrCAD symbol units = 2.54 mm
        const float bw = std::clamp((bestSym->maxX - bestSym->minX) * gScale, 4.0f, 80.0f);
        const float bh = std::clamp((bestSym->maxY - bestSym->minY) * gScale, 4.0f, 80.0f);
        const float px = sheetOffsetX + 8.0f - bestSym->minX * gScale;
        const float py = -8.0f + bestSym->minY * gScale;
        size_t pinCnt = 0;
        DetailOrCadBin::StampOrCadSymToBoard(bestSym, px, py, 0, false, gScale, EdaLayerId::SchSymbol, out, pinCnt);

        EdaComponent3D comp;
        comp.refDes = PathUtils::Utf8ToWide(symTitle);
        comp.value = PathUtils::Utf8ToWide(bestSym->name);
        comp.footprint = L"ORCAD_OLB_SYM";
        comp.x = sheetOffsetX + 8.0f + bw * 0.5f;
        comp.y = -8.0f - bh * 0.5f;
        comp.widthMm = bw;
        comp.lengthMm = bh;
        comp.heightMm = 1.2f;
        comp.pinCount = std::max<int>(1, static_cast<int>(pinCnt));
        out.components.push_back(std::move(comp));

        const float cardW = bw + 16.0f;
        const float cardH = bh + 16.0f;
        DrawEdaBoxRect(out, sheetOffsetX, -cardH, sheetOffsetX + cardW, 0.0f, 0.20f, EdaLayerId::EdgeCuts, 0);
        out.labels.push_back({
            sheetOffsetX + 2.0f, 1.4f, 1.05f, EdaLayerId::SchLabel,
            L"Symbol: " + PathUtils::Utf8ToWide(symTitle)
        });
        sheetOffsetX += cardW + 14.0f;
        ++renderedSymbols;
    }

    if (renderedSymbols == 0 || (out.traces.empty() && out.components.empty() && out.pads.empty())) {
        out = ParsedEdaBoard{};
        return false;
    }

    out.formatTitle = L"Cadence OrCAD Capture Symbol Library (.olb — " + std::to_wstring(renderedSymbols) + L" symbols)";
    out.netCount = std::max<size_t>(1, out.pads.size());
    return true;
}

} // namespace Formats
