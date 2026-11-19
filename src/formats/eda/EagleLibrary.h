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

    const auto& addCurvedOrStraightWire = DetailEagleXml::AddCurvedOrStraightWire;
    const auto& parseEagleRot = DetailEagleXml::ParseEagleRot;

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
            if (tag.rfind("gate ", 0) == 0) {
                std::string_view gName, sName;
                if (GetXmlAttr(tag, "name", gName) && GetXmlAttr(tag, "symbol", sName)) {
                    std::string dsKey = currentLibName.empty() ? currentDevSetName : (currentLibName + ":" + currentDevSetName);
                    auto& ds = devSetMap[dsKey];
                    EagleGateDef gd;
                    gd.symName.assign(sName.data(), sName.size());
                    gd.gx = GetXmlFloat(tag, "x", 0.0f);
                    gd.gy = GetXmlFloat(tag, "y", 0.0f);
                    std::string gk(gName);
                    if (ds.firstGate.empty()) ds.firstGate = gk;
                    ds.gates[gk] = std::move(gd);
                }
            }
            continue;
        }

        if (inSymbol && !currentSymName.empty()) {
            std::string symKey = currentLibName.empty() ? currentSymName : (currentLibName + ":" + currentSymName);
            auto& sym = symMap[symKey];
            if (tag.rfind("wire ", 0) == 0) {
                float x1 = GetXmlFloat(tag, "x1");
                float y1 = GetXmlFloat(tag, "y1");
                float x2 = GetXmlFloat(tag, "x2");
                float y2 = GetXmlFloat(tag, "y2");
                float curve = GetXmlFloat(tag, "curve", 0.0f);
                float w = std::max(0.16f, GetXmlFloat(tag, "width", 0.25f));
                addCurvedOrStraightWire(sym.lines, x1, y1, x2, y2, curve, w, EdaLayerId::SchSymbol);
                sym.Expand(x1, y1);
                sym.Expand(x2, y2);
            } else if (tag.rfind("rectangle ", 0) == 0) {
                float x1 = GetXmlFloat(tag, "x1");
                float y1 = GetXmlFloat(tag, "y1");
                float x2 = GetXmlFloat(tag, "x2");
                float y2 = GetXmlFloat(tag, "y2");
                sym.lines.push_back({ x1, y1, x2, y1, 0.22f, EdaLayerId::SchSymbol, 0 });
                sym.lines.push_back({ x2, y1, x2, y2, 0.22f, EdaLayerId::SchSymbol, 0 });
                sym.lines.push_back({ x2, y2, x1, y2, 0.22f, EdaLayerId::SchSymbol, 0 });
                sym.lines.push_back({ x1, y2, x1, y1, 0.22f, EdaLayerId::SchSymbol, 0 });
                sym.Expand(x1, y1);
                sym.Expand(x2, y2);
            } else if (tag.rfind("circle ", 0) == 0) {
                EdaCirclePrim c;
                c.cx = GetXmlFloat(tag, "x");
                c.cy = GetXmlFloat(tag, "y");
                c.radius = std::max(0.15f, GetXmlFloat(tag, "radius", 1.0f));
                c.width = std::max(0.16f, GetXmlFloat(tag, "width", 0.22f));
                c.layer = EdaLayerId::SchSymbol;
                sym.circles.push_back(c);
                sym.Expand(c.cx - c.radius, c.cy - c.radius);
                sym.Expand(c.cx + c.radius, c.cy + c.radius);
            } else if (tag.rfind("pin ", 0) == 0) {
                float px = GetXmlFloat(tag, "x");
                float py = GetXmlFloat(tag, "y");
                std::string_view lenSv, rotSv, nmSv;
                float pinLen = 5.08f; // Eagle default "middle" = 5.08 mm (0.2 in)
                if (GetXmlAttr(tag, "length", lenSv)) {
                    if (lenSv == "point") pinLen = 0.0f;
                    else if (lenSv == "short") pinLen = 2.54f;
                    else if (lenSv == "middle") pinLen = 5.08f;
                    else if (lenSv == "long") pinLen = 7.62f;
                    else pinLen = GetXmlFloat(tag, "length", 5.08f);
                }
                bool pMir = false;
                float pDeg = 0.0f;
                if (GetXmlAttr(tag, "rot", rotSv)) parseEagleRot(rotSv, pMir, pDeg);
                float prad = pDeg * 3.14159265f / 180.0f;
                float dirX = (pMir ? -1.0f : 1.0f) * std::cos(prad);
                float dirY = std::sin(prad);
                if (pinLen > 0.05f) {
                    float bx = px + dirX * pinLen;
                    float by = py + dirY * pinLen;
                    sym.lines.push_back({ px, py, bx, by, 0.22f, EdaLayerId::SchSymbol, 0 });
                    sym.Expand(bx, by);
                }
                sym.Expand(px, py);

                EdaPadPrim p;
                p.x = px;
                p.y = py;
                p.w = 0.6f;
                p.h = 0.6f;
                p.shape = 0;
                p.layer = EdaLayerId::SchSymbol;
                if (GetXmlAttr(tag, "name", nmSv)) p.pinName = PathUtils::Utf8ToWide(nmSv);
                sym.pins.push_back(std::move(p));
            }
            continue;
        }

        if (inPackage && !currentPkgName.empty()) {
            std::string pkgKey = currentLibName.empty() ? currentPkgName : (currentLibName + ":" + currentPkgName);
            auto recordPkg = [&](EaglePkgDef& pkg) {
                if (tag.rfind("smd ", 0) == 0) {
                    EdaPadPrim p;
                    p.x = GetXmlFloat(tag, "x");
                    p.y = GetXmlFloat(tag, "y");
                    p.w = GetXmlFloat(tag, "dx", 1.0f);
                    p.h = GetXmlFloat(tag, "dy", 1.0f);
                    int lyr = GetXmlInt(tag, "layer", 1);
                    p.layer = (lyr == 16) ? EdaLayerId::BottomCopper : EdaLayerId::TopCopper;
                    p.shape = 1;
                    std::string_view nm;
                    if (GetXmlAttr(tag, "name", nm)) p.pinName = PathUtils::Utf8ToWide(nm);
                    pkg.localPads.push_back(p);
                    pkg.minX = std::min(pkg.minX, p.x - p.w * 0.5f);
                    pkg.maxX = std::max(pkg.maxX, p.x + p.w * 0.5f);
                    pkg.minY = std::min(pkg.minY, p.y - p.h * 0.5f);
                    pkg.maxY = std::max(pkg.maxY, p.y + p.h * 0.5f);
                } else if (tag.rfind("pad ", 0) == 0) {
                    EdaPadPrim p;
                    p.x = GetXmlFloat(tag, "x");
                    p.y = GetXmlFloat(tag, "y");
                    p.drill = GetXmlFloat(tag, "drill", 0.8f);
                    float diam = GetXmlFloat(tag, "diameter", p.drill * 1.75f);
                    if (diam <= 0.01f) diam = p.drill * 1.75f;
                    p.w = diam;
                    p.h = diam;
                    p.layer = EdaLayerId::PadsThrough;
                    p.shape = 0;
                    std::string_view nm;
                    if (GetXmlAttr(tag, "name", nm)) p.pinName = PathUtils::Utf8ToWide(nm);
                    pkg.localPads.push_back(p);
                    pkg.minX = std::min(pkg.minX, p.x - diam * 0.5f);
                    pkg.maxX = std::max(pkg.maxX, p.x + diam * 0.5f);
                    pkg.minY = std::min(pkg.minY, p.y - diam * 0.5f);
                    pkg.maxY = std::max(pkg.maxY, p.y + diam * 0.5f);
                } else if (tag.rfind("wire ", 0) == 0) {
                    EdaTraceSeg seg;
                    seg.x1 = GetXmlFloat(tag, "x1");
                    seg.y1 = GetXmlFloat(tag, "y1");
                    seg.x2 = GetXmlFloat(tag, "x2");
                    seg.y2 = GetXmlFloat(tag, "y2");
                    seg.width = std::max(0.12f, GetXmlFloat(tag, "width", 0.15f));
                    seg.layer = EdaLayerId::TopSilk;
                    pkg.localSilk.push_back(seg);
                    pkg.minX = std::min({ pkg.minX, seg.x1, seg.x2 });
                    pkg.maxX = std::max({ pkg.maxX, seg.x1, seg.x2 });
                    pkg.minY = std::min({ pkg.minY, seg.y1, seg.y2 });
                    pkg.maxY = std::max({ pkg.maxY, seg.y1, seg.y2 });
                }
            };
            recordPkg(pkgMap[currentPkgName]);
            if (!currentLibName.empty()) recordPkg(pkgMap[pkgKey]);
            continue;
        }
    }

    // Standalone Eagle library (.lbr) preview when no board/schematic placements exist
    if (out.traces.empty() && out.pads.empty() && out.components.empty()) {
        float offX = 0.0f;
        for (const auto& kv : pkgMap) {
            if (kv.first.find(':') != std::string::npos) continue;
            const auto& pkg = kv.second;
            for (auto p : pkg.localPads) { p.x += offX; out.pads.push_back(p); }
            for (auto s : pkg.localSilk) { s.x1 += offX; s.x2 += offX; out.traces.push_back(s); }
            out.labels.push_back({ offX, pkg.maxY + 1.5f, 1.0f, EdaLayerId::TopSilk, PathUtils::Utf8ToWide(kv.first) });
            offX += std::max(10.0f, (pkg.maxX - pkg.minX) + 6.0f);
        }
    }

    FinalizeBoardBounds(out);
    out.valid = (!out.traces.empty() || !out.pads.empty() || !out.components.empty() || !out.vias.empty());
    return out.valid;
}
