#pragma once
// EagleLibrary.h — Autodesk / CadSoft EAGLE XML Library (.lbr) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

inline bool ParseEagleLibraryXml(std::string_view xml, ParsedEdaBoard& out) {
    using namespace DetailEagleXml;
    if (xml.find("<board") != std::string_view::npos ||
        xml.find("<schematic") != std::string_view::npos) {
        return false;
    }
    if (xml.find("<library") == std::string_view::npos &&
        xml.find("<eagle") == std::string_view::npos) {
        return false;
    }

    out.kind = EdaDocKind::EagleBrd;
    out.formatTitle = L"Eagle XML Library (.lbr)";

    std::unordered_map<std::string, EaglePkgDef> pkgMap;
    std::unordered_map<std::string, EagleSymDef> symMap;
    std::unordered_map<std::string, EagleDevSetDef> devSetMap;
    std::string currentLibName;
    std::string currentPkgName;
    std::string currentSymName;
    std::string currentDevSetName;
    bool inPackage = false;
    bool inSymbol  = false;
    bool inDevSet  = false;

    size_t pos = 0;
    while (pos < xml.size()) {
        size_t lt = xml.find('<', pos);
        if (lt == std::string_view::npos) break;
        size_t gt = xml.find('>', lt + 1);
        if (gt == std::string_view::npos) break;
        std::string_view tag = xml.substr(lt + 1, gt - lt - 1);
        pos = gt + 1;

        if (tag.empty() || tag[0] == '?' || tag[0] == '!') continue;

        if (tag.rfind("/library", 0) == 0) {
            currentLibName.clear();
            continue;
        }
        if (tag.rfind("library ", 0) == 0 && tag.back() != '/') {
            std::string_view nm;
            if (GetXmlAttr(tag, "name", nm)) currentLibName.assign(nm.data(), nm.size());
            continue;
        }
        if (tag.rfind("/package", 0) == 0) {
            inPackage = false;
            currentPkgName.clear();
            continue;
        }
        if (tag.rfind("package ", 0) == 0 && tag.back() != '/') {
            std::string_view nameSv;
            if (GetXmlAttr(tag, "name", nameSv)) {
                currentPkgName.assign(nameSv.data(), nameSv.size());
                inPackage = true;
            }
            continue;
        }
        if (tag.rfind("/symbol", 0) == 0) {
            if (inSymbol && !currentSymName.empty() && !currentLibName.empty()) {
                auto itS = symMap.find(currentLibName + ":" + currentSymName);
                if (itS != symMap.end() && !symMap.count(currentSymName)) {
                    symMap[currentSymName] = itS->second;
                }
            }
            inSymbol = false;
            currentSymName.clear();
            continue;
        }
        if (tag.rfind("symbol ", 0) == 0 && tag.back() != '/') {
            std::string_view nameSv;
            if (GetXmlAttr(tag, "name", nameSv)) {
                currentSymName.assign(nameSv.data(), nameSv.size());
                inSymbol = true;
            }
            continue;
        }
        if (tag.rfind("/deviceset", 0) == 0) {
            if (inDevSet && !currentDevSetName.empty() && !currentLibName.empty()) {
                auto itD = devSetMap.find(currentLibName + ":" + currentDevSetName);
                if (itD != devSetMap.end() && !devSetMap.count(currentDevSetName)) {
                    devSetMap[currentDevSetName] = itD->second;
                }
            }
            inDevSet = false;
            currentDevSetName.clear();
            continue;
        }
        if (tag.rfind("deviceset ", 0) == 0 && tag.back() != '/') {
            std::string_view nameSv;
            if (GetXmlAttr(tag, "name", nameSv)) {
                currentDevSetName.assign(nameSv.data(), nameSv.size());
                inDevSet = true;
                std::string dsKey = currentLibName.empty() ? currentDevSetName : (currentLibName + ":" + currentDevSetName);
                devSetMap[dsKey].libName = currentLibName;
            }
            continue;
        }

        if (inDevSet && !currentDevSetName.empty()) {
            ParseEagleDevSetContentTag(tag, currentLibName, currentDevSetName, devSetMap);
            continue;
        }

        if (inSymbol && !currentSymName.empty()) {
            std::string symKey = currentLibName.empty() ? currentSymName : (currentLibName + ":" + currentSymName);
            ParseEagleSymbolContentTag(tag, symMap[symKey]);
            continue;
        }

        if (inPackage && !currentPkgName.empty()) {
            RecordEaglePackageTag(tag, currentLibName, currentPkgName, pkgMap);
            continue;
        }
    }

    // Standalone Eagle library (.lbr) preview when no board/schematic placements exist
    if (out.traces.empty() && out.pads.empty() && out.components.empty()) {
        float offX = 0.0f;
        for (const auto& kv : pkgMap) {
            if (kv.first.find(':') != std::string::npos) continue;
            const auto& pkg = kv.second;
            if (pkg.localPads.empty() && pkg.localSilk.empty()) continue;
            for (auto p : pkg.localPads) { p.x += offX; out.pads.push_back(p); }
            for (auto s : pkg.localSilk) { s.x1 += offX; s.x2 += offX; out.traces.push_back(s); }
            out.labels.push_back({ offX, pkg.maxY + 1.5f, 1.0f, EdaLayerId::TopSilk, PathUtils::Utf8ToWide(kv.first) });
            offX += std::max(10.0f, (pkg.maxX - pkg.minX) + 6.0f);
        }

        // Symbol-only EAGLE `.lbr` library (no `<package>` footprints, only `<symbol>` schematic definitions!)
        if (out.traces.empty() && out.pads.empty() && !symMap.empty()) {
            out.kind = EdaDocKind::EagleSch;
            out.formatTitle = L"Eagle XML Symbol Library (.lbr)";
            float symOffX = 0.0f;
            size_t symCount = 0u;
            for (const auto& kv : symMap) {
                if (kv.first.find(':') != std::string::npos) continue;
                const auto& sym = kv.second;
                if (sym.lines.empty() && sym.circles.empty() && sym.pins.empty()) continue;

                const float minX = (sym.minX <= sym.maxX) ? sym.minX : -5.0f;
                const float maxX = (sym.minX <= sym.maxX) ? sym.maxX :  5.0f;
                const float minY = (sym.minY <= sym.maxY) ? sym.minY : -5.0f;
                const float maxY = (sym.minY <= sym.maxY) ? sym.maxY :  5.0f;
                const float bw = std::max(6.0f, maxX - minX);
                const float bh = std::max(6.0f, maxY - minY);
                const float shiftX = symOffX - minX + 4.0f;

                for (auto ln : sym.lines) {
                    ln.x1 += shiftX; ln.x2 += shiftX;
                    out.traces.push_back(ln);
                }
                for (auto ci : sym.circles) {
                    ci.cx += shiftX;
                    out.circles.push_back(ci);
                }
                for (auto p : sym.pins) {
                    p.x += shiftX;
                    out.pads.push_back(p);
                }

                DrawEdaBoxRect(out, symOffX, minY - 3.0f, symOffX + bw + 8.0f, maxY + 3.0f, 0.18f, EdaLayerId::EdgeCuts, 0);
                out.labels.push_back({ symOffX + 1.2f, maxY + 3.8f, 1.0f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(kv.first) });

                EdaComponent3D sc;
                sc.refDes = PathUtils::Utf8ToWide(kv.first);
                sc.value = std::to_wstring(sym.pins.size()) + L" pins";
                sc.footprint = L"EAGLE_SYM";
                sc.x = symOffX + (bw + 8.0f) * 0.5f;
                sc.y = (minY + maxY) * 0.5f;
                sc.widthMm = bw; sc.lengthMm = bh; sc.heightMm = 1.0f;
                sc.pinCount = std::max<int>(1, static_cast<int>(sym.pins.size()));
                sc.category = EdaPartCategory::IC_Package;
                out.components.push_back(std::move(sc));

                symOffX += bw + 14.0f;
                ++symCount;
            }
            if (symCount > 0u) out.netCount = std::max<size_t>(1u, out.pads.size());
        }
    }

    FinalizeBoardBounds(out);
    out.valid = (!out.traces.empty() || !out.pads.empty() || !out.circles.empty() || !out.components.empty() || !out.vias.empty());
    return out.valid;
}
