#pragma once
static bool ParseEagleXml(std::string_view xml, const std::wstring& ext, ParsedEdaBoard& out) {
    if (xml.find("<eagle") == std::string_view::npos &&
        xml.find("<board") == std::string_view::npos &&
        xml.find("<schematic") == std::string_view::npos &&
        xml.find("<library") == std::string_view::npos) {
        return false;
    }

    bool isSch = (ext == L".sch") || (xml.find("<schematic") != std::string_view::npos);
    out.kind = isSch ? EdaDocKind::EagleSch : EdaDocKind::EagleBrd;
    out.formatTitle = isSch ? L"Eagle XML Schematic (.sch)" : L"Eagle XML Board (.brd)";

    struct EagleSymDef {
        std::vector<EdaTraceSeg> lines;
        std::vector<EdaCirclePrim> circles;
        std::vector<EdaPadPrim> pins;
        float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;

        void Expand(float x, float y) {
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }
    };
    struct EagleGateDef {
        std::string symName;
        float gx = 0.0f;
        float gy = 0.0f;
    };
    struct EagleDevSetDef {
        std::string libName;
        std::string firstGate;
        std::unordered_map<std::string, EagleGateDef> gates;
    };
    struct EaglePartDef {
        std::string libName;
        std::string devSet;
        std::wstring value;
    };

    std::unordered_map<std::string, EaglePkgDef> pkgMap;
    std::unordered_map<std::string, EagleSymDef> symMap;
    std::unordered_map<std::string, EagleDevSetDef> devSetMap;
    std::unordered_map<std::string, EaglePartDef> schPartMap;

    std::string currentLibName;
    std::string currentPkgName;
    std::string currentSymName;
    std::string currentDevSetName;
    std::string currentNetName;
    bool inPackage = false;
    bool inSymbol  = false;
    bool inDevSet  = false;
    bool inNet     = false;

    auto addCurvedOrStraightWire = [](std::vector<EdaTraceSeg>& dst, float x1, float y1, float x2, float y2,
                                      float curveDeg, float w, EdaLayerId lid) {
        if (std::fabs(curveDeg) < 1.0f || (std::fabs(x2 - x1) < 1e-5f && std::fabs(y2 - y1) < 1e-5f)) {
            dst.push_back({ x1, y1, x2, y2, w, lid, 0 });
            return;
        }
        float rad = curveDeg * 3.14159265f / 180.0f;
        float dx = x2 - x1, dy = y2 - y1;
        float chord = std::hypot(dx, dy);
        float halfSin = std::sin(std::fabs(rad) * 0.5f);
        if (halfSin < 1e-4f) {
            dst.push_back({ x1, y1, x2, y2, w, lid, 0 });
            return;
        }
        float radius = (chord * 0.5f) / halfSin;
        float mx = (x1 + x2) * 0.5f, my = (y1 + y2) * 0.5f;
        float h = std::sqrt(std::max(0.0f, radius * radius - (chord * chord * 0.25f)));
        float sign = (std::fabs(rad) > 3.14159265f) ? -1.0f : 1.0f;
        if (rad < 0.0f) sign = -sign;
        float nx = -dy / chord, ny = dx / chord;
        float cx = mx + nx * h * sign;
        float cy = my + ny * h * sign;
        float a0 = std::atan2(y1 - cy, x1 - cx);
        int steps = std::clamp(static_cast<int>(std::fabs(curveDeg) / 15.0f), 4, 24);
        float px = x1, py = y1;
        for (int s = 1; s <= steps; ++s) {
            float a = a0 + rad * (static_cast<float>(s) / static_cast<float>(steps));
            float qx = cx + radius * std::cos(a);
            float qy = cy + radius * std::sin(a);
            if (s == steps) { qx = x2; qy = y2; }
            dst.push_back({ px, py, qx, qy, w, lid, 0 });
            px = qx; py = qy;
        }
    };

    auto parseEagleRot = [](std::string_view rotSv, bool& outMirror, float& outDeg) {
        outMirror = false;
        outDeg = 0.0f;
        if (rotSv.empty()) return;
        if (rotSv.find('M') != std::string_view::npos) outMirror = true;
        size_t rPos = rotSv.find('R');
        if (rPos != std::string_view::npos && rPos + 1 < rotSv.size()) {
            char rbuf[16] = {};
            std::memcpy(rbuf, rotSv.data() + rPos + 1, std::min<size_t>(15, rotSv.size() - rPos - 1));
            outDeg = std::strtof(rbuf, nullptr);
        }
    };

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
        if (tag.rfind("/net", 0) == 0 || tag.rfind("/signal", 0) == 0) {
            inNet = false;
            currentNetName.clear();
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
        } else if (tag.rfind("junction ", 0) == 0 && isSch) {
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
