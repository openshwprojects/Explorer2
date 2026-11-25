#pragma once
// EagleBoard.h — Autodesk / CadSoft EAGLE XML Board (.brd) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

inline bool ParseEagleBoardXml(std::string_view xml, ParsedEdaBoard& out) {
    using namespace DetailEagleXml;
    if (xml.find("<schematic") != std::string_view::npos) {
        return false;
    }
    if (xml.find("<board") == std::string_view::npos &&
        (xml.find("<eagle") == std::string_view::npos || xml.find("<library") != std::string_view::npos)) {
        return false;
    }

    const bool isSch = false;
    out.kind = EdaDocKind::EagleBrd;
    out.formatTitle = L"Eagle XML Board (.brd)";

    std::unordered_map<std::string, EaglePkgDef> pkgMap;
    std::string currentLibName;
    std::string currentPkgName;
    std::string currentNetName;
    bool inPackage = false;
    bool inNet     = false;

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
        if (tag.rfind("/net", 0) == 0 || tag.rfind("/signal", 0) == 0) {
            inNet = false;
            currentNetName.clear();
            continue;
        }

        if (inPackage && !currentPkgName.empty()) {
            RecordEaglePackageTag(tag, currentLibName, currentPkgName, pkgMap);
            continue;
        }

        if (TryParseEagleCommonDrawTag(xml, pos, tag, isSch, inNet, currentNetName, out)) {
            continue;
        }
        if (tag.rfind("via ", 0) == 0 || tag.rfind("hole ", 0) == 0) {
            EdaViaPrim v;
            v.x = GetXmlFloat(tag, "x");
            v.y = GetXmlFloat(tag, "y");
            v.drill = GetXmlFloat(tag, "drill", 0.5f);
            v.diameter = GetXmlFloat(tag, "diameter", v.drill * 1.8f);
            if (v.diameter <= v.drill) v.diameter = v.drill * 1.8f;
            out.vias.push_back(v);
        } else if (tag.rfind("element ", 0) == 0 && !isSch) {
            std::string_view nameSv, pkgSv, libSv, valSv, rotSv;
            GetXmlAttr(tag, "name", nameSv);
            GetXmlAttr(tag, "library", libSv);
            GetXmlAttr(tag, "package", pkgSv);
            GetXmlAttr(tag, "value", valSv);
            GetXmlAttr(tag, "rot", rotSv);

            EdaComponent3D comp;
            comp.refDes = PathUtils::Utf8ToWide(nameSv);
            comp.footprint = PathUtils::Utf8ToWide(pkgSv);
            comp.value = PathUtils::Utf8ToWide(valSv);
            comp.x = GetXmlFloat(tag, "x");
            comp.y = GetXmlFloat(tag, "y");
            parseEagleRot(rotSv, comp.bottomSide, comp.rotDeg);

            float rad = comp.rotDeg * 3.14159265f / 180.0f;
            float cs = std::cos(rad);
            float sn = std::sin(rad);
            float mirX = comp.bottomSide ? -1.0f : 1.0f;

            auto pkgIt = (!libSv.empty() && pkgMap.count(std::string(libSv) + ":" + std::string(pkgSv)))
                ? pkgMap.find(std::string(libSv) + ":" + std::string(pkgSv))
                : pkgMap.find(std::string(pkgSv));
            if (pkgIt != pkgMap.end()) {
                const auto& pkg = pkgIt->second;
                comp.widthMm = std::max(1.2f, pkg.maxX - pkg.minX);
                comp.lengthMm = std::max(1.0f, pkg.maxY - pkg.minY);
                comp.pinCount = static_cast<int>(pkg.localPads.size());

                for (const auto& lp : pkg.localPads) {
                    EdaPadPrim bp = lp;
                    float lx = lp.x * mirX;
                    float ly = lp.y;
                    bp.x = comp.x + (lx * cs - ly * sn);
                    bp.y = comp.y + (lx * sn + ly * cs);
                    bp.rotDeg = comp.rotDeg;
                    if (comp.bottomSide && bp.layer == EdaLayerId::TopCopper) {
                        bp.layer = EdaLayerId::BottomCopper;
                    }
                    out.pads.push_back(bp);
                }
                for (const auto& ls : pkg.localSilk) {
                    EdaTraceSeg bs = ls;
                    float x1 = ls.x1 * mirX, y1 = ls.y1;
                    float x2 = ls.x2 * mirX, y2 = ls.y2;
                    bs.x1 = comp.x + (x1 * cs - y1 * sn);
                    bs.y1 = comp.y + (x1 * sn + y1 * cs);
                    bs.x2 = comp.x + (x2 * cs - y2 * sn);
                    bs.y2 = comp.y + (x2 * sn + y2 * cs);
                    bs.layer = comp.bottomSide ? EdaLayerId::BottomSilk : EdaLayerId::TopSilk;
                    out.traces.push_back(bs);
                }
            }

            comp.category = ClassifyComponent(comp.refDes, comp.footprint, comp.widthMm, comp.lengthMm, comp.heightMm, comp.pinCount);
            out.labels.push_back({ comp.x, comp.y, 1.1f, comp.bottomSide ? EdaLayerId::BottomSilk : EdaLayerId::TopSilk, comp.refDes });
            out.components.push_back(std::move(comp));
        }
    }

    FinalizeBoardBounds(out);
    out.valid = (!out.traces.empty() || !out.pads.empty() || !out.components.empty() || !out.vias.empty());
    return out.valid;
}
