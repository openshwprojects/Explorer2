#pragma once
// EagleSchematic.h — Autodesk / CadSoft EAGLE XML Schematic (.sch) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

inline bool ParseEagleSchematicXml(std::string_view xml, ParsedEdaBoard& out) {
    using namespace DetailEagleXml;
    if (xml.find("<board") != std::string_view::npos) {
        return false;
    }
    if (xml.find("<schematic") == std::string_view::npos &&
        (xml.find("<eagle") == std::string_view::npos || xml.find("<library") != std::string_view::npos)) {
        return false;
    }

    const bool isSch = true;
    out.kind = EdaDocKind::EagleSch;
    out.formatTitle = L"Eagle XML Schematic (.sch)";

    std::unordered_map<std::string, EagleSymDef> symMap;
    std::unordered_map<std::string, EagleDevSetDef> devSetMap;
    std::unordered_map<std::string, EaglePartDef> schPartMap;
    std::string currentLibName;
    std::string currentSymName;
    std::string currentDevSetName;
    std::string currentNetName;
    bool inSymbol  = false;
    bool inDevSet  = false;
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
        if (tag.rfind("/net", 0) == 0 || tag.rfind("/signal", 0) == 0) {
            inNet = false;
            currentNetName.clear();
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

        if (TryParseEagleCommonDrawTag(xml, pos, tag, isSch, inNet, currentNetName, out)) {
            continue;
        }
        if (tag.rfind("junction ", 0) == 0 && isSch) {
            EdaCirclePrim jc;
            jc.cx = GetXmlFloat(tag, "x");
            jc.cy = GetXmlFloat(tag, "y");
            jc.radius = 0.45f;
            jc.width = 0.25f;
            jc.filled = true;
            jc.layer = EdaLayerId::SchWire;
            out.circles.push_back(jc);
        } else if (tag.rfind("label ", 0) == 0 && isSch) {
            if (!currentNetName.empty() && out.labels.size() < 2048) {
                float lx = GetXmlFloat(tag, "x");
                float ly = GetXmlFloat(tag, "y");
                float sz = std::clamp(GetXmlFloat(tag, "size", 1.27f), 0.7f, 4.0f);
                out.labels.push_back({ lx, ly + 0.4f, sz, EdaLayerId::SchWire, PathUtils::Utf8ToWide(currentNetName) });
            }
        } else if (tag.rfind("part ", 0) == 0 && isSch) {
            std::string_view nm, val, devSet, libSv;
            if (GetXmlAttr(tag, "name", nm)) {
                GetXmlAttr(tag, "library", libSv);
                GetXmlAttr(tag, "deviceset", devSet);
                GetXmlAttr(tag, "value", val);
                EaglePartDef pd;
                pd.libName.assign(libSv.data(), libSv.size());
                pd.devSet.assign(devSet.data(), devSet.size());
                pd.value = PathUtils::Utf8ToWide(val.empty() ? devSet : val);
                schPartMap[std::string(nm)] = std::move(pd);
            }
        } else if (tag.rfind("instance ", 0) == 0 && isSch) {
            std::string_view partSv, gateSv, rotSv;
            if (GetXmlAttr(tag, "part", partSv)) {
                GetXmlAttr(tag, "gate", gateSv);
                GetXmlAttr(tag, "rot", rotSv);

                bool mirror = false;
                float rotDeg = 0.0f;
                parseEagleRot(rotSv, mirror, rotDeg);
                const float mirX = mirror ? -1.0f : 1.0f;
                const float rad = rotDeg * 3.14159265f / 180.0f;
                const float cs = std::cos(rad);
                const float sn = std::sin(rad);

                EdaComponent3D comp;
                comp.refDes = PathUtils::Utf8ToWide(partSv);
                comp.x = GetXmlFloat(tag, "x");
                comp.y = GetXmlFloat(tag, "y");
                comp.rotDeg = rotDeg;
                comp.bottomSide = mirror;

                const EagleSymDef* resolvedSym = nullptr;
                auto partIt = schPartMap.find(std::string(partSv));
                if (partIt != schPartMap.end()) {
                    const auto& pinfo = partIt->second;
                    comp.value = pinfo.value;
                    comp.footprint = PathUtils::Utf8ToWide(pinfo.devSet);

                    const EagleDevSetDef* dsPtr = nullptr;
                    if (!pinfo.libName.empty()) {
                        auto dsIt = devSetMap.find(pinfo.libName + ":" + pinfo.devSet);
                        if (dsIt != devSetMap.end()) dsPtr = &dsIt->second;
                    }
                    if (!dsPtr) {
                        auto dsIt = devSetMap.find(pinfo.devSet);
                        if (dsIt != devSetMap.end()) dsPtr = &dsIt->second;
                    }

                    std::string symLookup;
                    if (dsPtr) {
                        std::string gKey = gateSv.empty() ? dsPtr->firstGate : std::string(gateSv);
                        auto gIt = dsPtr->gates.find(gKey);
                        if (gIt == dsPtr->gates.end() && !dsPtr->gates.empty()) gIt = dsPtr->gates.begin();
                        if (gIt != dsPtr->gates.end()) {
                            symLookup = gIt->second.symName;
                            if (dsPtr->gates.size() > 1 && !gateSv.empty() && gateSv != "G$1") {
                                comp.refDes += PathUtils::Utf8ToWide(gateSv);
                            }
                        }
                    } else {
                        symLookup = pinfo.devSet;
                    }

                    if (!symLookup.empty()) {
                        if (!pinfo.libName.empty()) {
                            auto sIt = symMap.find(pinfo.libName + ":" + symLookup);
                            if (sIt != symMap.end()) resolvedSym = &sIt->second;
                        }
                        if (!resolvedSym) {
                            auto sIt = symMap.find(symLookup);
                            if (sIt != symMap.end()) resolvedSym = &sIt->second;
                        }
                    }
                }
                if (!resolvedSym && !gateSv.empty()) {
                    auto sIt = symMap.find(std::string(gateSv));
                    if (sIt != symMap.end()) resolvedSym = &sIt->second;
                }

                auto xformPt = [&](float lx, float ly, float& ox, float& oy) {
                    float mx = lx * mirX;
                    ox = comp.x + (mx * cs - ly * sn);
                    oy = comp.y + (mx * sn + ly * cs);
                };

                if (resolvedSym && (!resolvedSym->lines.empty() || !resolvedSym->circles.empty() || !resolvedSym->pins.empty())) {
                    if (resolvedSym->minX <= resolvedSym->maxX && resolvedSym->minY <= resolvedSym->maxY) {
                        comp.widthMm  = std::clamp(resolvedSym->maxX - resolvedSym->minX, 1.5f, 50.0f);
                        comp.lengthMm = std::clamp(resolvedSym->maxY - resolvedSym->minY, 1.5f, 50.0f);
                    }
                    comp.pinCount = std::max(1, static_cast<int>(resolvedSym->pins.size()));

                    for (const auto& ln : resolvedSym->lines) {
                        EdaTraceSeg ws = ln;
                        xformPt(ln.x1, ln.y1, ws.x1, ws.y1);
                        xformPt(ln.x2, ln.y2, ws.x2, ws.y2);
                        ws.layer = EdaLayerId::SchSymbol;
                        out.traces.push_back(ws);
                    }
                    for (const auto& cr : resolvedSym->circles) {
                        EdaCirclePrim wc = cr;
                        xformPt(cr.cx, cr.cy, wc.cx, wc.cy);
                        wc.layer = EdaLayerId::SchSymbol;
                        out.circles.push_back(wc);
                    }
                    for (const auto& lp : resolvedSym->pins) {
                        EdaPadPrim wp = lp;
                        xformPt(lp.x, lp.y, wp.x, wp.y);
                        wp.rotDeg = rotDeg;
                        wp.layer = EdaLayerId::SchSymbol;
                        out.pads.push_back(wp);
                    }
                } else {
                    comp.widthMm = 6.0f;
                    comp.lengthMm = 4.0f;
                    float hw = 3.0f, hh = 2.0f;
                    out.traces.push_back({ comp.x - hw, comp.y - hh, comp.x + hw, comp.y - hh, 0.25f, EdaLayerId::SchSymbol, 0 });
                    out.traces.push_back({ comp.x + hw, comp.y - hh, comp.x + hw, comp.y + hh, 0.25f, EdaLayerId::SchSymbol, 0 });
                    out.traces.push_back({ comp.x + hw, comp.y + hh, comp.x - hw, comp.y + hh, 0.25f, EdaLayerId::SchSymbol, 0 });
                    out.traces.push_back({ comp.x - hw, comp.y + hh, comp.x - hw, comp.y - hh, 0.25f, EdaLayerId::SchSymbol, 0 });
                }

                std::wstring lblTxt = comp.refDes + (comp.value.empty() ? L"" : (L":" + comp.value));
                out.labels.push_back({ comp.x - comp.widthMm * 0.35f, comp.y + comp.lengthMm * 0.5f + 1.4f, 1.15f, EdaLayerId::SchLabel, lblTxt });
                out.components.push_back(std::move(comp));
            }
        }
    }

    FinalizeBoardBounds(out);
    out.valid = (!out.traces.empty() || !out.pads.empty() || !out.components.empty() || !out.vias.empty());
    return out.valid;
}
