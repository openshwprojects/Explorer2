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
        if (tag.rfind("/net", 0) == 0 || tag.rfind("/signal", 0) == 0) {
            inNet = false;
            currentNetName.clear();
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

        if (tag.rfind("wire ", 0) == 0) {
            int lyr = GetXmlInt(tag, "layer", isSch ? 91 : 1);
            if (!isSch && (lyr == 19 || lyr >= 100)) continue;
            float x1 = GetXmlFloat(tag, "x1");
            float y1 = GetXmlFloat(tag, "y1");
            float x2 = GetXmlFloat(tag, "x2");
            float y2 = GetXmlFloat(tag, "y2");
            float curve = GetXmlFloat(tag, "curve", 0.0f);
            float w = std::max(0.15f, GetXmlFloat(tag, "width", 0.25f));
            EdaLayerId lid = (isSch && inNet) ? EdaLayerId::SchWire : MapEagleLayer(lyr, isSch);
            addCurvedOrStraightWire(out.traces, x1, y1, x2, y2, curve, w, lid);
        } else if (tag.rfind("rectangle ", 0) == 0) {
            int lyr = GetXmlInt(tag, "layer", isSch ? 94 : 21);
            float x1 = GetXmlFloat(tag, "x1"), y1 = GetXmlFloat(tag, "y1");
            float x2 = GetXmlFloat(tag, "x2"), y2 = GetXmlFloat(tag, "y2");
            EdaLayerId lid = MapEagleLayer(lyr, isSch);
            out.traces.push_back({ x1, y1, x2, y1, 0.22f, lid, 0 });
            out.traces.push_back({ x2, y1, x2, y2, 0.22f, lid, 0 });
            out.traces.push_back({ x2, y2, x1, y2, 0.22f, lid, 0 });
            out.traces.push_back({ x1, y2, x1, y1, 0.22f, lid, 0 });
        } else if (tag.rfind("via ", 0) == 0 || tag.rfind("hole ", 0) == 0) {
            EdaViaPrim v;
            v.x = GetXmlFloat(tag, "x");
            v.y = GetXmlFloat(tag, "y");
            v.drill = GetXmlFloat(tag, "drill", 0.5f);
            v.diameter = GetXmlFloat(tag, "diameter", v.drill * 1.8f);
            if (v.diameter <= v.drill) v.diameter = v.drill * 1.8f;
            out.vias.push_back(v);
        } else if (tag.rfind("text ", 0) == 0 && tag.back() != '/') {
            size_t endTxt = xml.find("</text>", pos);
            if (endTxt != std::string_view::npos) {
                std::string_view body = xml.substr(pos, endTxt - pos);
                if (!body.empty() && body[0] != '>' && out.labels.size() < 2048) {
                    float tx = GetXmlFloat(tag, "x");
                    float ty = GetXmlFloat(tag, "y");
                    float sz = std::clamp(GetXmlFloat(tag, "size", 1.27f), 0.7f, 4.0f);
                    int lyr = GetXmlInt(tag, "layer", isSch ? 94 : 21);
                    out.labels.push_back({ tx, ty, sz, MapEagleLayer(lyr, isSch), PathUtils::Utf8ToWide(body) });
                }
                pos = endTxt + 7;
            }
        } else if (tag.rfind("circle ", 0) == 0) {
            EdaCirclePrim c;
            c.cx = GetXmlFloat(tag, "x");
            c.cy = GetXmlFloat(tag, "y");
            c.radius = GetXmlFloat(tag, "radius", 1.0f);
            c.width = std::max(0.15f, GetXmlFloat(tag, "width", 0.2f));
            c.layer = MapEagleLayer(GetXmlInt(tag, "layer", 21), isSch);
            out.circles.push_back(c);
        } else if (tag.rfind("signal ", 0) == 0 || tag.rfind("net ", 0) == 0) {
            ++out.netCount;
            std::string_view nn;
            if (GetXmlAttr(tag, "name", nn)) currentNetName.assign(nn.data(), nn.size());
            if (tag.back() != '/') inNet = true;
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
