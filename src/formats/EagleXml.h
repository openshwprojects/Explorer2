#pragma once
static bool ParseEagleXml(std::string_view xml, const std::wstring& ext, ParsedEdaBoard& out) {
    bool isSch = (ext == L".sch") || (xml.find("<schematic") != std::string_view::npos);
    out.kind = isSch ? EdaDocKind::EagleSch : EdaDocKind::EagleBrd;
    out.formatTitle = isSch ? L"Eagle XML Schematic (.sch)" : L"Eagle XML Board (.brd)";

    std::unordered_map<std::string, EaglePkgDef> pkgMap;
    std::unordered_map<std::string, EaglePkgDef> symMap;
    std::unordered_map<std::string, std::wstring> schPartValues;
    std::string currentPkgName;
    std::string currentSymName;
    bool inPackage = false;
    bool inSymbol  = false;

    size_t pos = 0;
    while (pos < xml.size()) {
        size_t lt = xml.find('<', pos);
        if (lt == std::string_view::npos) break;
        size_t gt = xml.find('>', lt + 1);
        if (gt == std::string_view::npos) break;
        std::string_view tag = xml.substr(lt + 1, gt - lt - 1);
        pos = gt + 1;

        if (tag.empty() || tag[0] == '?' || tag[0] == '!') continue;

        if (tag.rfind("/package", 0) == 0) {
            inPackage = false;
            currentPkgName.clear();
            continue;
        }
        if (tag.rfind("package ", 0) == 0 && tag.back() != '/') {
            std::string_view nameSv;
            if (GetXmlAttr(tag, "name", nameSv)) {
                currentPkgName = std::string(nameSv);
                inPackage = true;
            }
            continue;
        }
        if (tag.rfind("/symbol", 0) == 0) {
            inSymbol = false;
            currentSymName.clear();
            continue;
        }
        if (tag.rfind("symbol ", 0) == 0 && tag.back() != '/') {
            std::string_view nameSv;
            if (GetXmlAttr(tag, "name", nameSv)) {
                currentSymName = std::string(nameSv);
                inSymbol = true;
            }
            continue;
        }

        if (inSymbol && !currentSymName.empty()) {
            auto& sym = symMap[currentSymName];
            if (tag.rfind("wire ", 0) == 0) {
                EdaTraceSeg seg;
                seg.x1 = GetXmlFloat(tag, "x1");
                seg.y1 = GetXmlFloat(tag, "y1");
                seg.x2 = GetXmlFloat(tag, "x2");
                seg.y2 = GetXmlFloat(tag, "y2");
                seg.width = std::max(0.15f, GetXmlFloat(tag, "width", 0.25f));
                seg.layer = EdaLayerId::SchSymbol;
                sym.localSilk.push_back(seg);
            } else if (tag.rfind("pin ", 0) == 0) {
                EdaPadPrim p;
                p.x = GetXmlFloat(tag, "x");
                p.y = GetXmlFloat(tag, "y");
                p.w = 0.8f; p.h = 0.8f;
                p.layer = EdaLayerId::SchSymbol;
                sym.localPads.push_back(p);
            }
            continue;
        }

        if (inPackage && !currentPkgName.empty()) {
            auto& pkg = pkgMap[currentPkgName];
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
            continue;
        }

        if (tag.rfind("wire ", 0) == 0) {
            int lyr = GetXmlInt(tag, "layer", isSch ? 91 : 1);
            // Skip documentation/unrouted layers unless relevant
            if (!isSch && (lyr == 19 || lyr >= 100)) continue;
            EdaTraceSeg seg;
            seg.x1 = GetXmlFloat(tag, "x1");
            seg.y1 = GetXmlFloat(tag, "y1");
            seg.x2 = GetXmlFloat(tag, "x2");
            seg.y2 = GetXmlFloat(tag, "y2");
            seg.width = std::max(0.15f, GetXmlFloat(tag, "width", 0.25f));
            seg.layer = MapEagleLayer(lyr, isSch);
            out.traces.push_back(seg);
        } else if (tag.rfind("via ", 0) == 0 || tag.rfind("hole ", 0) == 0) {
            EdaViaPrim v;
            v.x = GetXmlFloat(tag, "x");
            v.y = GetXmlFloat(tag, "y");
            v.drill = GetXmlFloat(tag, "drill", 0.5f);
            v.diameter = GetXmlFloat(tag, "diameter", v.drill * 1.8f);
            if (v.diameter <= v.drill) v.diameter = v.drill * 1.8f;
            out.vias.push_back(v);
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
        } else if (tag.rfind("part ", 0) == 0 && isSch) {
            std::string_view nm, val, dev;
            if (GetXmlAttr(tag, "name", nm)) {
                GetXmlAttr(tag, "value", val);
                if (val.empty()) GetXmlAttr(tag, "deviceset", dev);
                schPartValues[std::string(nm)] = PathUtils::Utf8ToWide(val.empty() ? dev : val);
            }
        } else if (tag.rfind("instance ", 0) == 0 && isSch) {
            std::string_view partSv;
            if (GetXmlAttr(tag, "part", partSv)) {
                EdaComponent3D comp;
                comp.refDes = PathUtils::Utf8ToWide(partSv);
                auto it = schPartValues.find(std::string(partSv));
                if (it != schPartValues.end()) comp.value = it->second;
                comp.x = GetXmlFloat(tag, "x");
                comp.y = GetXmlFloat(tag, "y");
                comp.widthMm = 6.0f;
                comp.lengthMm = 4.0f;
                float hw = 3.0f, hh = 2.0f;
                out.traces.push_back({ comp.x - hw, comp.y - hh, comp.x + hw, comp.y - hh, 0.25f, EdaLayerId::SchSymbol, 0 });
                out.traces.push_back({ comp.x + hw, comp.y - hh, comp.x + hw, comp.y + hh, 0.25f, EdaLayerId::SchSymbol, 0 });
                out.traces.push_back({ comp.x + hw, comp.y + hh, comp.x - hw, comp.y + hh, 0.25f, EdaLayerId::SchSymbol, 0 });
                out.traces.push_back({ comp.x - hw, comp.y + hh, comp.x - hw, comp.y - hh, 0.25f, EdaLayerId::SchSymbol, 0 });
                out.components.push_back(comp);
                out.labels.push_back({ comp.x, comp.y, 1.5f, EdaLayerId::SchLabel, comp.refDes + (comp.value.empty() ? L"" : (L":" + comp.value)) });
            }
        } else if (tag.rfind("element ", 0) == 0 && !isSch) {
            std::string_view nameSv, pkgSv, valSv, rotSv;
            GetXmlAttr(tag, "name", nameSv);
            GetXmlAttr(tag, "package", pkgSv);
            GetXmlAttr(tag, "value", valSv);
            GetXmlAttr(tag, "rot", rotSv);

            EdaComponent3D comp;
            comp.refDes = PathUtils::Utf8ToWide(nameSv);
            comp.footprint = PathUtils::Utf8ToWide(pkgSv);
            comp.value = PathUtils::Utf8ToWide(valSv);
            comp.x = GetXmlFloat(tag, "x");
            comp.y = GetXmlFloat(tag, "y");

            if (!rotSv.empty()) {
                if (rotSv.find('M') != std::string_view::npos) comp.bottomSide = true;
                size_t rPos = rotSv.find('R');
                if (rPos != std::string_view::npos && rPos + 1 < rotSv.size()) {
                    char rbuf[16] = {};
                    std::memcpy(rbuf, rotSv.data() + rPos + 1, std::min<size_t>(15, rotSv.size() - rPos - 1));
                    comp.rotDeg = std::strtof(rbuf, nullptr);
                }
            }

            float rad = comp.rotDeg * 3.14159265f / 180.0f;
            float cs = std::cos(rad);
            float sn = std::sin(rad);
            float mirX = comp.bottomSide ? -1.0f : 1.0f;

            auto pkgIt = pkgMap.find(std::string(pkgSv));
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
