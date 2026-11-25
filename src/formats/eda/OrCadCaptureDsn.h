#pragma once
// OrCadCaptureDsn.h — Cadence OrCAD Capture OLE2 Schematic Design (.dsn) Reader
// Parses real Cadence OrCAD Capture v9-v22+ OLE2 schematic designs (`Views/<Sch>/Pages/<Page>`,
// `Library` string pool, `Cache`/`Packages`/`Symbols` `LibraryPart` definitions, and `Structure::Page`
// FutureData prefix-chain records). Symbol library `.olb` files live in `OrCadSymbolOlb.h`.
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

namespace DetailOrCadDsnPage {

// Parse a real Cadence OrCAD Capture Page stream (Structure::Page == 10)
inline bool ParseRealOrCadPageStream(
    const uint8_t* b,
    size_t n,
    float& ioSheetOffsetX,
    const std::string& fallbackTitle,
    const std::vector<std::string>& libStrings,
    const std::unordered_map<std::string, DetailOrCadBin::OrCadCachedSym>& symCache,
    std::set<std::string>& netSet,
    ParsedEdaBoard& out)
{
    using namespace DetailOrCadBin;

    size_t pos = 0;
    OrCadPrefixInfo pagePfx;
    if (!ReadOrCadPrefixChain(b, n, pos, n, pagePfx) || pagePfx.typeId != 10) {
        return false;
    }

    std::string pageName, pageSizeStr;
    if (!ReadStrLenZero(b, n, pos, pageName) || !ReadStrLenZero(b, n, pos, pageSizeStr)) {
        return false;
    }
    if (pageName.empty()) pageName = fallbackTitle;

    // PageSettings (152 bytes): width @+24, height @+28, pinToPin @+32
    if (pos + 152 > pagePfx.outerStop) return false;
    uint32_t rawPageW = RdU32(b + pos + 24);
    uint32_t rawPageH = RdU32(b + pos + 28);
    uint32_t pinToPin = RdU32(b + pos + 32);
    pos += 152;

    const float gridScale = (pinToPin >= 1 && pinToPin <= 1000)
        ? (2.54f / static_cast<float>(pinToPin))
        : 0.254f;
    float pageW = (rawPageW > 1500) ? (static_cast<float>(rawPageW) * 0.0254f) : (static_cast<float>(rawPageW) * gridScale);
    float pageH = (rawPageH > 1500) ? (static_cast<float>(rawPageH) * 0.0254f) : (static_cast<float>(rawPageH) * gridScale);
    pageW = std::clamp(pageW, 60.0f, 600.0f);
    pageH = std::clamp(pageH, 45.0f, 450.0f);

    const float offX = ioSheetOffsetX;
    const size_t primsBefore = out.traces.size() + out.components.size() + out.pads.size();

    auto skipCountedStructs = [&](size_t extraBytesPerItem = 0) -> bool {
        if (pos + 2 > pagePfx.outerStop) return false;
        uint16_t cnt = RdU16(b + pos);
        pos += 2;
        for (uint16_t i = 0; i < cnt; ++i) {
            if (!SkipOrCadStructure(b, n, pos, pagePfx.outerStop)) return false;
            if (pos + extraBytesPerItem > pagePfx.outerStop) return false;
            pos += extraBytesPerItem;
        }
        return true;
    };

    // 1..3: TitleBlocks, T0x34s, T0x35s
    if (!skipCountedStructs() || !skipCountedStructs() || !skipCountedStructs()) {
        return false;
    }

    // 4: Page Net -> ID table
    if (pos + 2 > pagePfx.outerStop) return false;
    uint16_t lenNetMap = RdU16(b + pos);
    pos += 2;
    for (uint16_t i = 0; i < lenNetMap; ++i) {
        std::string netNm;
        if (!ReadStrLenZero(b, n, pos, netNm) || pos + 4 > pagePfx.outerStop) return false;
        pos += 4;
        if (!netNm.empty()) netSet.insert(netNm);
    }

    // 5: Wires (WireScalar == 20, WireBus == 21)
    if (pos + 2 > pagePfx.outerStop) return false;
    uint16_t lenWires = RdU16(b + pos);
    pos += 2;
    std::map<std::pair<int32_t, int32_t>, int> wireNodeDegree;
    for (uint16_t i = 0; i < lenWires; ++i) {
        OrCadPrefixInfo wPfx;
        if (!ReadOrCadPrefixChain(b, n, pos, pagePfx.outerStop, wPfx) ||
            (wPfx.typeId != 20 && wPfx.typeId != 21))
        {
            return false;
        }
        if (pos + 31 <= wPfx.outerStop) {
            int32_t ix1 = RdI32(b + pos + 12);
            int32_t iy1 = RdI32(b + pos + 16);
            int32_t ix2 = RdI32(b + pos + 20);
            int32_t iy2 = RdI32(b + pos + 24);
            wireNodeDegree[{ ix1, iy1 }]++;
            wireNodeDegree[{ ix2, iy2 }]++;
            float x1 = offX + static_cast<float>(ix1) * gridScale;
            float y1 = -static_cast<float>(iy1) * gridScale;
            float x2 = offX + static_cast<float>(ix2) * gridScale;
            float y2 = -static_cast<float>(iy2) * gridScale;
            out.traces.push_back({ x1, y1, x2, y2, (wPfx.typeId == 21) ? 0.48f : 0.24f, EdaLayerId::SchWire, 0 });

            // Inline wire Aliases (Structure::Alias == 49)
            size_t ap = pos + 29;
            uint16_t nAliases = RdU16(b + ap);
            ap += 2;
            for (uint16_t ai = 0; ai < nAliases && ap < wPfx.outerStop; ++ai) {
                OrCadPrefixInfo aPfx;
                if (!ReadOrCadPrefixChain(b, n, ap, wPfx.outerStop, aPfx) || aPfx.typeId != 49) break;
                if (ap + 20 <= aPfx.outerStop) {
                    float ax = offX + static_cast<float>(RdI32(b + ap + 0)) * gridScale;
                    float ay = -static_cast<float>(RdI32(b + ap + 4)) * gridScale;
                    size_t sp = ap + 20;
                    std::string aliasTxt;
                    if (ReadStrLenZero(b, n, sp, aliasTxt) && !aliasTxt.empty()) {
                        netSet.insert(aliasTxt);
                        out.labels.push_back({ ax + 0.2f, ay + 0.35f, 0.90f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(aliasTxt) });
                    }
                }
                ap = aPfx.outerStop;
            }
        }
        pos = wPfx.outerStop;
    }
    for (const auto& [pt, deg] : wireNodeDegree) {
        if (deg >= 3) {
            out.vias.push_back({ offX + static_cast<float>(pt.first) * gridScale,
                                -static_cast<float>(pt.second) * gridScale, 0.68f, 0.0f, 0 });
        }
    }

    // 6: PlacedInstances (Structure::PlacedInstance == 13)
    if (pos + 2 <= pagePfx.outerStop) {
        uint16_t lenInsts = RdU16(b + pos);
        pos += 2;
        for (uint16_t i = 0; i < lenInsts && pos < pagePfx.outerStop; ++i) {
            OrCadPrefixInfo iPfx;
            if (!ReadOrCadPrefixChain(b, n, pos, pagePfx.outerStop, iPfx) || iPfx.typeId != 13) break;

            std::string pkgName, refDes, valStr, srcPkg;
            for (const auto& [nIdx, vIdx] : iPfx.propNameValIdx) {
                if (nIdx < libStrings.size() && vIdx < libStrings.size()) {
                    const std::string& pKey = libStrings[nIdx];
                    if (ReaderText::AsciiEqCI(pKey, "Value") || ReaderText::AsciiEqCI(pKey, "Part Value")) {
                        valStr = libStrings[vIdx];
                    }
                }
            }

            if (pos + 8 <= iPfx.outerStop) {
                size_t ip = pos + 8;
                if (ReadStrLenZero(b, n, ip, pkgName) && ip + 22 <= iPfx.outerStop) {
                    ip += 4 + 8;
                    int16_t lx = RdI16(b + ip + 0);
                    int16_t ly = RdI16(b + ip + 2);
                    uint8_t flags0 = b[ip + 4];
                    uint8_t rotIdx = flags0 & 0x03u;
                    bool mirror = (flags0 & 0x80u) != 0 || (b[ip + 5] & 0x01u) != 0;
                    ip += 8;
                    uint16_t nDisp = RdU16(b + ip);
                    ip += 2;
                    bool dispOk = true;
                    for (uint16_t d = 0; d < nDisp; ++d) {
                        if (!SkipOrCadStructure(b, n, ip, iPfx.outerStop)) { dispOk = false; break; }
                    }
                    if (dispOk && ip + 1 < iPfx.outerStop) {
                        ip += 1;
                        ReadStrLenZero(b, n, ip, refDes);
                        if (ip + 16 <= iPfx.outerStop) {
                            ip += 14;
                            uint16_t nT10 = RdU16(b + ip);
                            ip += 2;
                            bool t10Ok = true;
                            for (uint16_t t = 0; t < nT10; ++t) {
                                if (!SkipOrCadStructure(b, n, ip, iPfx.outerStop)) { t10Ok = false; break; }
                            }
                            if (t10Ok && ip < iPfx.outerStop) {
                                ReadStrLenZero(b, n, ip, srcPkg);
                            }
                        }
                    }

                    const OrCadCachedSym* sym = nullptr;
                    for (const std::string& cand : { UpperKey(pkgName), StripNormalSuffix(UpperKey(pkgName)),
                                                     UpperKey(srcPkg), StripNormalSuffix(UpperKey(srcPkg)) }) {
                        if (cand.empty()) continue;
                        auto it = symCache.find(cand);
                        if (it != symCache.end()) { sym = &it->second; break; }
                    }

                    float wx = offX + static_cast<float>(lx) * gridScale;
                    float wy = -static_cast<float>(ly) * gridScale;
                    size_t pinCnt = 0;
                    StampOrCadSymToBoard(sym, wx, wy, rotIdx, mirror, gridScale, EdaLayerId::SchSymbol, out, pinCnt);

                    float bw = sym ? std::clamp((sym->maxX - sym->minX) * gridScale, 2.5f, 50.0f) : 6.0f;
                    float bh = sym ? std::clamp((sym->maxY - sym->minY) * gridScale, 2.0f, 50.0f) : 4.4f;
                    if (valStr.empty()) valStr = StripNormalSuffix(pkgName);

                    EdaComponent3D comp;
                    comp.refDes = PathUtils::Utf8ToWide(refDes.empty() ? "U?" : refDes);
                    comp.value = PathUtils::Utf8ToWide(valStr);
                    comp.footprint = PathUtils::Utf8ToWide(srcPkg.empty() ? pkgName : srcPkg);
                    comp.x = wx + bw * 0.5f;
                    comp.y = wy - bh * 0.5f;
                    comp.rotDeg = static_cast<float>(rotIdx) * 90.0f;
                    comp.bottomSide = mirror;
                    comp.widthMm = bw; comp.lengthMm = bh; comp.heightMm = 1.4f;
                    comp.pinCount = std::max<int>(2, static_cast<int>(pinCnt));
                    comp.category = ClassifyComponent(comp.refDes, comp.footprint, comp.widthMm, comp.lengthMm, comp.heightMm, comp.pinCount);
                    std::wstring lbl = comp.refDes + (comp.value.empty() ? L"" : (L" (" + comp.value + L")"));
                    out.labels.push_back({ wx, wy + 0.9f, 0.95f, EdaLayerId::SchLabel, lbl });
                    out.components.push_back(std::move(comp));
                }
            }
            pos = iPfx.outerStop;
        }
    }

    auto readGraphicSymSection = [&](size_t trailingBytes, int kindTag) {
        if (pos + 2 > pagePfx.outerStop) return;
        uint16_t cnt = RdU16(b + pos);
        pos += 2;
        for (uint16_t i = 0; i < cnt && pos < pagePfx.outerStop; ++i) {
            OrCadPrefixInfo gPfx;
            if (!ReadOrCadPrefixChain(b, n, pos, pagePfx.outerStop, gPfx)) break;
            if (pos + 8 < gPfx.outerStop) {
                size_t gp = pos + 8;
                std::string symName;
                if (ReadStrLenZero(b, n, gp, symName) && gp + 8 <= gPfx.outerStop) {
                    gp += 4;
                    int16_t ly = RdI16(b + gp + 0);
                    int16_t lx = RdI16(b + gp + 2);
                    float gx = offX + static_cast<float>(lx) * gridScale;
                    float gy = -static_cast<float>(ly) * gridScale;
                    if (!symName.empty()) netSet.insert(symName);
                    if (kindTag == 1 && (ReaderText::AsciiEqCI(symName, "0") || ReaderText::AsciiEqCI(symName, "GND") ||
                                         ReaderText::AsciiEqCI(symName, "GND_SIGNAL") || ReaderText::AsciiEqCI(symName, "AGND"))) {
                        out.traces.push_back({ gx, gy, gx, gy - 1.0f, 0.22f, EdaLayerId::SchSymbol, 0 });
                        out.traces.push_back({ gx - 1.1f, gy - 1.0f, gx + 1.1f, gy - 1.0f, 0.24f, EdaLayerId::SchSymbol, 0 });
                        out.traces.push_back({ gx - 0.7f, gy - 1.5f, gx + 0.7f, gy - 1.5f, 0.22f, EdaLayerId::SchSymbol, 0 });
                        out.traces.push_back({ gx - 0.3f, gy - 2.0f, gx + 0.3f, gy - 2.0f, 0.20f, EdaLayerId::SchSymbol, 0 });
                    } else if (kindTag == 1) {
                        out.traces.push_back({ gx, gy, gx, gy + 1.1f, 0.22f, EdaLayerId::SchSymbol, 0 });
                        out.traces.push_back({ gx - 1.0f, gy + 1.1f, gx + 1.0f, gy + 1.1f, 0.26f, EdaLayerId::SchSymbol, 0 });
                        if (!symName.empty()) out.labels.push_back({ gx - 0.8f, gy + 1.7f, 0.88f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(symName) });
                    } else {
                        out.traces.push_back({ gx - 2.4f, gy - 0.6f, gx, gy, 0.22f, EdaLayerId::SchSymbol, 0 });
                        out.traces.push_back({ gx, gy, gx - 2.4f, gy + 0.6f, 0.22f, EdaLayerId::SchSymbol, 0 });
                        if (!symName.empty()) out.labels.push_back({ gx - 2.3f, gy - 0.2f, 0.85f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(symName) });
                    }
                }
            }
            pos = gPfx.outerStop;
            if (pos + trailingBytes <= pagePfx.outerStop) pos += trailingBytes;
        }
    };

    // 7: Ports, 8: Globals (+5B), 9: OffPageConnectors (+5B), 10: ERCObjects
    readGraphicSymSection(0, 0);
    readGraphicSymSection(5, 1);
    readGraphicSymSection(5, 2);
    skipCountedStructs();

    // 11: BusEntries (Structure::BusEntry == 29)
    if (pos + 2 <= pagePfx.outerStop) {
        uint16_t lenBusEnt = RdU16(b + pos);
        pos += 2;
        for (uint16_t i = 0; i < lenBusEnt && pos < pagePfx.outerStop; ++i) {
            OrCadPrefixInfo bPfx;
            if (!ReadOrCadPrefixChain(b, n, pos, pagePfx.outerStop, bPfx) || bPfx.typeId != 29) break;
            if (pos + 20 <= bPfx.outerStop) {
                float x1 = offX + static_cast<float>(RdI32(b + pos + 4)) * gridScale;
                float y1 = -static_cast<float>(RdI32(b + pos + 8)) * gridScale;
                float x2 = offX + static_cast<float>(RdI32(b + pos + 12)) * gridScale;
                float y2 = -static_cast<float>(RdI32(b + pos + 16)) * gridScale;
                out.traces.push_back({ x1, y1, x2, y2, 0.24f, EdaLayerId::SchWire, 0 });
            }
            pos = bPfx.outerStop;
        }
    }

    // 12: GraphicInsts (Lines, Boxes, Arcs, Ellipses, Polygons, Polylines, CommentTexts on Page)
    if (pos + 2 <= pagePfx.outerStop) {
        uint16_t lenGr = RdU16(b + pos);
        pos += 2;
        for (uint16_t i = 0; i < lenGr && pos < pagePfx.outerStop; ++i) {
            OrCadPrefixInfo grPfx;
            if (!ReadOrCadPrefixChain(b, n, pos, pagePfx.outerStop, grPfx)) break;
            if (pos + 8 < grPfx.outerStop) {
                size_t gp = pos + 8;
                std::string gName;
                if (ReadStrLenZero(b, n, gp, gName) && gp + 22 <= grPfx.outerStop) {
                    gp += 4;
                    int16_t ly = RdI16(b + gp + 0);
                    int16_t lx = RdI16(b + gp + 2);
                    gp += 12 + 4;
                    uint16_t nDisp = RdU16(b + gp);
                    gp += 2;
                    bool okDisp = true;
                    for (uint16_t d = 0; d < nDisp; ++d) {
                        if (!SkipOrCadStructure(b, n, gp, grPfx.outerStop)) { okDisp = false; break; }
                    }
                    if (okDisp && gp < grPfx.outerStop && b[gp] == 0x02u) {
                        ++gp;
                        OrCadCachedSym gSym;
                        std::vector<std::pair<std::pair<float, float>, std::string>> gTexts;
                        if (ParseOrCadSthInPages0(b, n, gp, grPfx.outerStop, gSym, &gTexts)) {
                            float gx = offX + static_cast<float>(lx) * gridScale;
                            float gy = -static_cast<float>(ly) * gridScale;
                            size_t dummyPins = 0;
                            StampOrCadSymToBoard(&gSym, gx, gy, 0, false, gridScale, EdaLayerId::SchLabel, out, dummyPins);
                            for (const auto& [tp, txt] : gTexts) {
                                out.labels.push_back({ gx + tp.first * gridScale, gy - tp.second * gridScale,
                                                       0.95f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(txt) });
                            }
                        }
                    }
                }
            }
            pos = grPfx.outerStop;
        }
    }

    if (out.traces.size() + out.components.size() + out.pads.size() <= primsBefore) {
        return false;
    }

    float sx0 = offX, sy0 = 0.0f, sx1 = offX + pageW, sy1 = -pageH;
    out.traces.push_back({ sx0, sy0, sx1, sy0, 0.22f, EdaLayerId::EdgeCuts, 0 });
    out.traces.push_back({ sx1, sy0, sx1, sy1, 0.22f, EdaLayerId::EdgeCuts, 0 });
    out.traces.push_back({ sx1, sy1, sx0, sy1, 0.22f, EdaLayerId::EdgeCuts, 0 });
    out.traces.push_back({ sx0, sy1, sx0, sy0, 0.22f, EdaLayerId::EdgeCuts, 0 });
    out.labels.push_back({ sx0 + 2.0f, sy0 + 1.5f, 1.15f, EdaLayerId::SchLabel,
                           L"Sheet: " + PathUtils::Utf8ToWide(pageName) });
    ioSheetOffsetX += pageW + 18.0f;
    return true;
}

} // namespace DetailOrCadDsnPage

inline bool ParseOrCadCaptureOle2Board(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    if (!data || size < 512 || std::memcmp(data, "\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1", 8) != 0) {
        return false;
    }

    Ole2::CompoundFile cf;
    if (!Ole2::ParseCompoundFile(data, size, cf) || !cf.valid) {
        return false;
    }

    bool hasOrCadMarkers = false;
    const Ole2::DirEntry* libraryEntry = nullptr;
    const Ole2::DirEntry* cacheEntry = nullptr;
    std::vector<const Ole2::DirEntry*> pageStreams;
    std::vector<const Ole2::DirEntry*> pkgOrSymStreams;

    for (const auto& e : cf.entries) {
        std::wstring lowPath = PathUtils::ToLower(e.fullPath);
        std::wstring lowName = PathUtils::ToLower(e.name);
        if (lowPath.find(L"views/") != std::wstring::npos ||
            lowPath.find(L"/pages/") != std::wstring::npos ||
            lowPath.find(L"packages/") != std::wstring::npos ||
            lowName == L"cis" || lowName == L"hsobjects" || lowName == L"admindata" || lowName == L"$packages")
        {
            hasOrCadMarkers = true;
        }
        if (e.objectType == 2 && lowName == L"library" && e.size >= 32) {
            libraryEntry = &e;
        }
        if (e.objectType == 2 && lowName == L"cache" && e.size >= 16) {
            cacheEntry = &e;
        }
        if (e.objectType == 2 && e.size >= 16) {
            if (lowPath.find(L"/pages/") != std::wstring::npos || lowPath.rfind(L"pages/", 0) == 0) {
                pageStreams.push_back(&e);
            } else if ((lowPath.find(L"packages/") != std::wstring::npos || lowPath.find(L"symbols/") != std::wstring::npos) &&
                       lowPath.find(L"directory") == std::wstring::npos &&
                       lowName != L"$types$" && lowName != L"$packages")
            {
                pkgOrSymStreams.push_back(&e);
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

    if (!hasOrCadMarkers || pageStreams.empty()) {
        return false;
    }

    std::unordered_map<std::string, DetailOrCadBin::OrCadCachedSym> symCache;
    if (cacheEntry) {
        std::vector<uint8_t> cacheStm;
        if (Ole2::ExtractStream(cf, *cacheEntry, cacheStm, 16 * 1024 * 1024)) {
            DetailOrCadBin::CollectCachedLibraryParts(cacheStm, "", symCache);
        }
    }
    for (size_t k = 0; k < pkgOrSymStreams.size() && k < 128; ++k) {
        std::vector<uint8_t> pStm;
        if (Ole2::ExtractStream(cf, *pkgOrSymStreams[k], pStm, 8 * 1024 * 1024)) {
            DetailOrCadBin::CollectCachedLibraryParts(pStm, PathUtils::WideToUtf8(pkgOrSymStreams[k]->name), symCache);
        }
    }

    out.kind = EdaDocKind::KiCadSch;
    float sheetOffsetX = 0.0f;
    size_t renderedPages = 0;
    std::set<std::string> netSet;

    for (size_t pi = 0; pi < pageStreams.size() && pi < 32; ++pi) {
        std::vector<uint8_t> stm;
        if (!Ole2::ExtractStream(cf, *pageStreams[pi], stm, 16 * 1024 * 1024) || stm.size() < 16) {
            continue;
        }

        const uint8_t* b = stm.data();
        const size_t n = stm.size();
        std::string pageTitle = PathUtils::WideToUtf8(pageStreams[pi]->name);

        if (b[0] == 10) {
            if (DetailOrCadDsnPage::ParseRealOrCadPageStream(b, n, sheetOffsetX, pageTitle, libStrings, symCache, netSet, out)) {
                ++renderedPages;
            }
        }
    }

    if (renderedPages == 0 || (out.traces.empty() && out.components.empty() && out.pads.empty())) {
        out = ParsedEdaBoard{};
        return false;
    }

    out.formatTitle = L"Cadence OrCAD Capture Schematic (.dsn — " +
                      std::to_wstring(renderedPages) + L" page" + (renderedPages > 1 ? L"s)" : L")");
    out.netCount = std::max<size_t>(1, netSet.empty() ? out.components.size() : netSet.size());
    return true;
}

} // namespace Formats
