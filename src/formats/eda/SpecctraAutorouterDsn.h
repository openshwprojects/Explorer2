#pragma once
// SpecctraAutorouterDsn.h — Specctra PCB Autorouter S-Expression (.dsn) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

inline bool ParseSpecctraSExprBoard(std::string_view sv, ParsedEdaBoard& out) {
    using namespace DetailSpecctra;
    std::vector<SNode> roots;
    if (!ParseSExprTree(sv, roots)) return false;

    const SNode* pcbRoot = nullptr;
    for (const auto& r : roots) {
        std::string_view hk = HeadKey(r);
        if (hk == "pcb" || hk == "PCB") { pcbRoot = &r; break; }
    }
    if (!pcbRoot) return false;

    std::string boardTitle;
    if (pcbRoot->list.size() >= 2 && !pcbRoot->list[1].isList) {
        boardTitle = pcbRoot->list[1].atom;
    }

    // 1. Resolution & Unit: (resolution <unit> <divPerUnit>) and (unit <unit>)
    double coordToMm = 1.0;
    bool hasExplicitRes = false;
    for (size_t i = 1; i < pcbRoot->list.size(); ++i) {
        const SNode& sec = pcbRoot->list[i];
        std::string_view k = HeadKey(sec);
        if (k == "resolution" && sec.list.size() >= 3) {
            double div = 1.0;
            if (!sec.list[1].isList && AtomFloat(sec.list[2], div) && div > 0.0) {
                coordToMm = UnitNameToMm(sec.list[1].atom) / div;
                hasExplicitRes = true;
            }
        } else if (k == "unit" && !hasExplicitRes && sec.list.size() >= 2 && !sec.list[1].isList) {
            coordToMm = UnitNameToMm(sec.list[1].atom);
        }
    }

    // Layer map: top copper = first signal layer or F.Cu/Top, bottom copper = last signal layer or B.Cu/Bottom
    std::unordered_map<std::string, EdaLayerId> layerMap;
    std::vector<std::string> orderedLayers;

    auto classifyLayer = [&](const std::string& lyr) -> EdaLayerId {
        auto it = layerMap.find(lyr);
        if (it != layerMap.end()) return it->second;
        std::string low = lyr;
        for (char& c : low) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (low == "pcb" || low == "outline" || low.find("edge") != std::string::npos) return EdaLayerId::EdgeCuts;
        if (low.find("b.") != std::string::npos || low.find("bot") != std::string::npos || low == "l2" || low == "back") return EdaLayerId::BottomCopper;
        if (low.find("in") != std::string::npos || low.find("gnd") != std::string::npos || low.find("pwr") != std::string::npos) return EdaLayerId::InnerCopper;
        return EdaLayerId::TopCopper;
    };

    struct PadstackDef {
        float wMm = 1.1f;
        float hMm = 1.1f;
        float drillMm = 0.0f;
        uint8_t shape = 0; // 0=circle, 1=rect, 3=oval
        bool throughHole = false;
    };
    struct ImagePinDef {
        std::string padstack;
        std::string pinId;
        float lxMm = 0.0f, lyMm = 0.0f;
        float rotDeg = 0.0f;
    };
    struct ImageDef {
        std::vector<ImagePinDef> pins;
        std::vector<std::pair<std::pair<float, float>, std::pair<float, float>>> silkSegs;
        float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    };

    std::unordered_map<std::string, PadstackDef> padstacks;
    std::unordered_map<std::string, ImageDef> images;
    std::unordered_map<std::string, std::string> pinNetMap; // "U1-1" -> "VCC"
    std::unordered_map<std::string, int> netIds;

    auto toMm = [&](double rawVal) -> float {
        return static_cast<float>(rawVal * coordToMm);
    };

    // Helper to extract path/polygon/rect segments from a shape S-expr
    auto emitShapeEdges = [&](const SNode& sh, EdaLayerId overrideLayer, bool useOverride, int netId,
                              std::vector<EdaTraceSeg>& dstTraces) {
        std::string_view sk = HeadKey(sh);
        if ((sk == "path" || sk == "polyline_path" || sk == "polygon") && sh.list.size() >= 7) {
            EdaLayerId lid = useOverride ? overrideLayer : classifyLayer(sh.list[1].atom);
            double wRaw = 0.0;
            size_t coordStart = 3;
            if (!AtomFloat(sh.list[2], wRaw)) { coordStart = 2; wRaw = 0.0; }
            float wMm = (sk == "polygon") ? 0.16f : std::clamp(toMm(wRaw), 0.10f, 3.0f);
            std::vector<std::pair<float, float>> pts;
            for (size_t i = coordStart; i + 1 < sh.list.size(); i += 2) {
                double rx = 0, ry = 0;
                if (AtomFloat(sh.list[i], rx) && AtomFloat(sh.list[i + 1], ry)) {
                    pts.push_back({ toMm(rx), toMm(ry) });
                }
            }
            if (sk == "polygon" && pts.size() >= 3 && (pts.front() != pts.back())) {
                pts.push_back(pts.front());
            }
            for (size_t i = 0; i + 1 < pts.size() && dstTraces.size() < 120000; ++i) {
                dstTraces.push_back({ pts[i].first, pts[i].second, pts[i + 1].first, pts[i + 1].second, wMm, lid, netId });
            }
        } else if (sk == "rect" && sh.list.size() >= 6) {
            EdaLayerId lid = useOverride ? overrideLayer : classifyLayer(sh.list[1].atom);
            double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            if (AtomFloat(sh.list[2], x1) && AtomFloat(sh.list[3], y1) && AtomFloat(sh.list[4], x2) && AtomFloat(sh.list[5], y2)) {
                float bx1 = toMm(x1), by1 = toMm(y1), bx2 = toMm(x2), by2 = toMm(y2);
                dstTraces.push_back({ bx1, by1, bx2, by1, 0.20f, lid, netId });
                dstTraces.push_back({ bx2, by1, bx2, by2, 0.20f, lid, netId });
                dstTraces.push_back({ bx2, by2, bx1, by2, 0.20f, lid, netId });
                dstTraces.push_back({ bx1, by2, bx1, by1, 0.20f, lid, netId });
            }
        }
    };

    // Pass 1: structure (layers + boundary + planes), library (padstacks + images), network (netlist)
    for (size_t si = 1; si < pcbRoot->list.size(); ++si) {
        const SNode& sec = pcbRoot->list[si];
        std::string_view sk = HeadKey(sec);

        if (sk == "structure") {
            for (size_t ci = 1; ci < sec.list.size(); ++ci) {
                const SNode& sub = sec.list[ci];
                std::string_view subK = HeadKey(sub);
                if (subK == "layer" && sub.list.size() >= 2 && !sub.list[1].isList) {
                    orderedLayers.push_back(sub.list[1].atom);
                }
            }
            if (!orderedLayers.empty()) {
                layerMap[orderedLayers.front()] = EdaLayerId::TopCopper;
                if (orderedLayers.size() >= 2) layerMap[orderedLayers.back()] = EdaLayerId::BottomCopper;
                for (size_t li = 1; li + 1 < orderedLayers.size(); ++li) {
                    layerMap[orderedLayers[li]] = EdaLayerId::InnerCopper;
                }
            }
            for (size_t ci = 1; ci < sec.list.size(); ++ci) {
                const SNode& sub = sec.list[ci];
                std::string_view subK = HeadKey(sub);
                if (subK == "boundary" || subK == "keepout" || subK == "via_keepout" || subK == "place_keepout") {
                    EdaLayerId blid = (subK == "boundary") ? EdaLayerId::EdgeCuts : EdaLayerId::BottomSilk;
                    for (size_t bi = 1; bi < sub.list.size(); ++bi) {
                        emitShapeEdges(sub.list[bi], blid, true, 0, out.traces);
                    }
                } else if (subK == "plane" && sub.list.size() >= 3) {
                    std::string netNm = !sub.list[1].isList ? sub.list[1].atom : "";
                    if (!netNm.empty() && !netIds.count(netNm)) netIds[netNm] = static_cast<int>(netIds.size() + 1);
                    int nid = netNm.empty() ? 0 : netIds[netNm];
                    for (size_t bi = 2; bi < sub.list.size(); ++bi) {
                        emitShapeEdges(sub.list[bi], EdaLayerId::InnerCopper, false, nid, out.traces);
                    }
                }
            }
        } else if (sk == "library") {
            for (size_t li = 1; li < sec.list.size(); ++li) {
                const SNode& item = sec.list[li];
                std::string_view ik = HeadKey(item);
                if (ik == "padstack" && item.list.size() >= 2 && !item.list[1].isList) {
                    PadstackDef ps;
                    int shapeLayers = 0;
                    for (size_t pi = 2; pi < item.list.size(); ++pi) {
                        const SNode& psub = item.list[pi];
                        if (HeadKey(psub) == "shape" && psub.list.size() >= 2) {
                            const SNode& geom = psub.list[1];
                            std::string_view gk = HeadKey(geom);
                            ++shapeLayers;
                            if (gk == "circle" && geom.list.size() >= 3) {
                                double d = 0.0;
                                if (AtomFloat(geom.list[2], d)) {
                                    ps.wMm = ps.hMm = std::max(0.15f, toMm(d));
                                    ps.shape = 0;
                                }
                            } else if (gk == "rect" && geom.list.size() >= 6) {
                                double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
                                if (AtomFloat(geom.list[2], x1) && AtomFloat(geom.list[3], y1) &&
                                    AtomFloat(geom.list[4], x2) && AtomFloat(geom.list[5], y2)) {
                                    ps.wMm = std::max(0.15f, std::fabs(toMm(x2 - x1)));
                                    ps.hMm = std::max(0.15f, std::fabs(toMm(y2 - y1)));
                                    ps.shape = 1;
                                }
                            } else if (gk == "path" && geom.list.size() >= 7) {
                                double w = 0, x1 = 0, y1 = 0, x2 = 0, y2 = 0;
                                if (AtomFloat(geom.list[2], w) && AtomFloat(geom.list[3], x1) && AtomFloat(geom.list[4], y1) &&
                                    AtomFloat(geom.list[5], x2) && AtomFloat(geom.list[6], y2)) {
                                    ps.wMm = std::max(0.15f, std::fabs(toMm(x2 - x1)) + toMm(w));
                                    ps.hMm = std::max(0.15f, std::fabs(toMm(y2 - y1)) + toMm(w));
                                    ps.shape = 3;
                                }
                            }
                        }
                    }
                    if (shapeLayers >= 2) {
                        ps.throughHole = true;
                        ps.drillMm = std::min(ps.wMm, ps.hMm) * 0.55f;
                    }
                    padstacks[item.list[1].atom] = ps;
                }
            }
            for (size_t li = 1; li < sec.list.size(); ++li) {
                const SNode& item = sec.list[li];
                if (HeadKey(item) == "image" && item.list.size() >= 2 && !item.list[1].isList) {
                    ImageDef img;
                    for (size_t ii = 2; ii < item.list.size(); ++ii) {
                        const SNode& isub = item.list[ii];
                        std::string_view isk = HeadKey(isub);
                        if (isk == "outline") {
                            std::vector<EdaTraceSeg> tmpSegs;
                            for (size_t oi = 1; oi < isub.list.size(); ++oi) {
                                emitShapeEdges(isub.list[oi], EdaLayerId::TopSilk, true, 0, tmpSegs);
                            }
                            for (const auto& sg : tmpSegs) {
                                img.silkSegs.push_back({ { sg.x1, sg.y1 }, { sg.x2, sg.y2 } });
                                img.minX = std::min({ img.minX, sg.x1, sg.x2 });
                                img.maxX = std::max({ img.maxX, sg.x1, sg.x2 });
                                img.minY = std::min({ img.minY, sg.y1, sg.y2 });
                                img.maxY = std::max({ img.maxY, sg.y1, sg.y2 });
                            }
                        } else if (isk == "pin" && isub.list.size() >= 5) {
                            // (pin <padstack> [(rotate <deg>)] <pinId> <x> <y>)
                            ImagePinDef pdef;
                            pdef.padstack = isub.list[1].atom;
                            size_t idx = 2;
                            if (idx < isub.list.size() && isub.list[idx].isList && HeadKey(isub.list[idx]) == "rotate" &&
                                isub.list[idx].list.size() >= 2) {
                                double rd = 0.0;
                                if (AtomFloat(isub.list[idx].list[1], rd)) pdef.rotDeg = static_cast<float>(rd);
                                ++idx;
                            }
                            if (idx + 2 < isub.list.size()) {
                                pdef.pinId = isub.list[idx].atom;
                                double px = 0, py = 0;
                                if (AtomFloat(isub.list[idx + 1], px) && AtomFloat(isub.list[idx + 2], py)) {
                                    pdef.lxMm = toMm(px);
                                    pdef.lyMm = toMm(py);
                                    img.minX = std::min(img.minX, pdef.lxMm - 0.6f);
                                    img.maxX = std::max(img.maxX, pdef.lxMm + 0.6f);
                                    img.minY = std::min(img.minY, pdef.lyMm - 0.6f);
                                    img.maxY = std::max(img.maxY, pdef.lyMm + 0.6f);
                                    img.pins.push_back(std::move(pdef));
                                }
                            }
                        }
                    }
                    images[item.list[1].atom] = std::move(img);
                }
            }
        } else if (sk == "network") {
            for (size_t ni = 1; ni < sec.list.size(); ++ni) {
                const SNode& nsub = sec.list[ni];
                if (HeadKey(nsub) == "net" && nsub.list.size() >= 2 && !nsub.list[1].isList) {
                    std::string netNm = nsub.list[1].atom;
                    if (!netIds.count(netNm)) netIds[netNm] = static_cast<int>(netIds.size() + 1);
                    for (size_t pi = 2; pi < nsub.list.size(); ++pi) {
                        const SNode& pnode = nsub.list[pi];
                        if (HeadKey(pnode) == "pins") {
                            for (size_t k = 1; k < pnode.list.size(); ++k) {
                                if (!pnode.list[k].isList) pinNetMap[pnode.list[k].atom] = netNm;
                            }
                        }
                    }
                }
            }
        }
    }

    // Pass 2: placement (component instances) & wiring (routed traces + vias)
    for (size_t si = 1; si < pcbRoot->list.size(); ++si) {
        const SNode& sec = pcbRoot->list[si];
        std::string_view sk = HeadKey(sec);

        if (sk == "placement") {
            for (size_t ci = 1; ci < sec.list.size(); ++ci) {
                const SNode& cnode = sec.list[ci];
                if (HeadKey(cnode) == "component" && cnode.list.size() >= 3 && !cnode.list[1].isList) {
                    std::string imgName = cnode.list[1].atom;
                    auto itImg = images.find(imgName);
                    for (size_t pi = 2; pi < cnode.list.size(); ++pi) {
                        const SNode& pl = cnode.list[pi];
                        if (HeadKey(pl) != "place" || pl.list.size() < 6) continue;
                        std::string refDes = pl.list[1].atom;
                        double rx = 0, ry = 0, rdeg = 0;
                        if (!AtomFloat(pl.list[2], rx) || !AtomFloat(pl.list[3], ry)) continue;
                        std::string sideStr = pl.list[4].atom;
                        AtomFloat(pl.list[5], rdeg);
                        bool backSide = (_stricmp(sideStr.c_str(), "back") == 0 || _stricmp(sideStr.c_str(), "bottom") == 0);

                        std::string partVal;
                        for (size_t ai = 6; ai < pl.list.size(); ++ai) {
                            if (pl.list[ai].isList && (HeadKey(pl.list[ai]) == "PN" || HeadKey(pl.list[ai]) == "value") &&
                                pl.list[ai].list.size() >= 2) {
                                partVal = pl.list[ai].list[1].atom;
                            }
                        }

                        float bx = toMm(rx), by = toMm(ry);
                        float rotRad = static_cast<float>(rdeg) * 3.14159265f / 180.0f;
                        float cs = std::cos(rotRad), sn = std::sin(rotRad);
                        auto xform = [&](float lx, float ly, float& ox, float& oy) {
                            if (backSide) lx = -lx;
                            ox = bx + (lx * cs - ly * sn);
                            oy = by + (lx * sn + ly * cs);
                        };

                        EdaComponent3D comp;
                        comp.refDes = PathUtils::Utf8ToWide(refDes);
                        comp.value = PathUtils::Utf8ToWide(partVal);
                        comp.footprint = PathUtils::Utf8ToWide(imgName);
                        comp.x = bx;
                        comp.y = by;
                        comp.rotDeg = static_cast<float>(rdeg);
                        comp.bottomSide = backSide;

                        if (itImg != images.end()) {
                            const ImageDef& img = itImg->second;
                            if (img.minX <= img.maxX) {
                                comp.widthMm = std::max(1.2f, img.maxX - img.minX);
                                comp.lengthMm = std::max(1.0f, img.maxY - img.minY);
                            }
                            comp.pinCount = std::max(2, static_cast<int>(img.pins.size()));
                            for (const auto& sg : img.silkSegs) {
                                float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
                                xform(sg.first.first, sg.first.second, x1, y1);
                                xform(sg.second.first, sg.second.second, x2, y2);
                                out.traces.push_back({ x1, y1, x2, y2, 0.16f, backSide ? EdaLayerId::BottomSilk : EdaLayerId::TopSilk, 0 });
                            }
                            for (const auto& ip : img.pins) {
                                if (out.pads.size() >= 60000) break;
                                float px = 0, py = 0;
                                xform(ip.lxMm, ip.lyMm, px, py);
                                PadstackDef psd;
                                auto itPs = padstacks.find(ip.padstack);
                                if (itPs != padstacks.end()) psd = itPs->second;

                                EdaPadPrim pad{};
                                pad.x = px; pad.y = py;
                                pad.w = psd.wMm; pad.h = psd.hMm;
                                pad.rotDeg = comp.rotDeg + ip.rotDeg;
                                pad.drill = psd.drillMm;
                                pad.shape = psd.shape;
                                pad.layer = psd.throughHole ? EdaLayerId::PadsThrough
                                          : (backSide ? EdaLayerId::BottomCopper : EdaLayerId::TopCopper);
                                pad.pinName = PathUtils::Utf8ToWide(ip.pinId);
                                auto itNet = pinNetMap.find(refDes + "-" + ip.pinId);
                                if (itNet != pinNetMap.end()) pad.netName = PathUtils::Utf8ToWide(itNet->second);
                                out.pads.push_back(std::move(pad));
                            }
                        } else {
                            // Fallback when (placement ...) omits (library ...) as in minimal Specctra snippets
                            comp.widthMm = 3.0f; comp.lengthMm = 1.8f; comp.heightMm = 1.1f;
                            out.pads.push_back({ bx - 0.9f, by, 0.9f, 0.9f, comp.rotDeg, 0.0f, 1,
                                backSide ? EdaLayerId::BottomCopper : EdaLayerId::TopCopper, L"1", L"" });
                            out.pads.push_back({ bx + 0.9f, by, 0.9f, 0.9f, comp.rotDeg, 0.0f, 1,
                                backSide ? EdaLayerId::BottomCopper : EdaLayerId::TopCopper, L"2", L"" });
                        }
                        comp.category = ClassifyComponent(comp.refDes, comp.footprint, comp.widthMm, comp.lengthMm, comp.heightMm, comp.pinCount);
                        out.labels.push_back({ bx, by, 0.9f, backSide ? EdaLayerId::BottomSilk : EdaLayerId::TopSilk, comp.refDes });
                        out.components.push_back(std::move(comp));
                    }
                }
            }
        } else if (sk == "wiring") {
            for (size_t wi = 1; wi < sec.list.size(); ++wi) {
                const SNode& witem = sec.list[wi];
                std::string_view wk = HeadKey(witem);
                if (wk == "wire") {
                    int nid = 0;
                    for (size_t k = 1; k < witem.list.size(); ++k) {
                        if (HeadKey(witem.list[k]) == "net" && witem.list[k].list.size() >= 2) {
                            std::string nn = witem.list[k].list[1].atom;
                            if (!netIds.count(nn)) netIds[nn] = static_cast<int>(netIds.size() + 1);
                            nid = netIds[nn];
                        }
                    }
                    for (size_t k = 1; k < witem.list.size(); ++k) {
                        emitShapeEdges(witem.list[k], EdaLayerId::TopCopper, false, nid, out.traces);
                    }
                } else if (wk == "via" && witem.list.size() >= 4) {
                    std::string psName = witem.list[1].atom;
                    double vx = 0, vy = 0;
                    if (AtomFloat(witem.list[2], vx) && AtomFloat(witem.list[3], vy)) {
                        float diam = 0.8f, drill = 0.4f;
                        auto itPs = padstacks.find(psName);
                        if (itPs != padstacks.end()) {
                            diam = std::max(0.35f, itPs->second.wMm);
                            drill = itPs->second.drillMm > 0.0f ? itPs->second.drillMm : (diam * 0.5f);
                        }
                        int nid = 0;
                        for (size_t k = 4; k < witem.list.size(); ++k) {
                            if (HeadKey(witem.list[k]) == "net" && witem.list[k].list.size() >= 2) {
                                std::string nn = witem.list[k].list[1].atom;
                                if (!netIds.count(nn)) netIds[nn] = static_cast<int>(netIds.size() + 1);
                                nid = netIds[nn];
                            }
                        }
                        if (out.vias.size() < 60000) {
                            out.vias.push_back({ toMm(vx), toMm(vy), diam, drill, nid });
                        }
                    }
                }
            }
        }
    }

    if (out.traces.empty() && out.pads.empty() && out.components.empty() && out.vias.empty()) {
        out = ParsedEdaBoard{};
        return false;
    }

    out.kind = EdaDocKind::KiCadPcb;
    out.formatTitle = L"Specctra Autorouter PCB (.dsn";
    if (!boardTitle.empty()) out.formatTitle += L" — " + PathUtils::Utf8ToWide(boardTitle);
    out.formatTitle += L")";
    out.netCount = std::max<size_t>(1, netIds.empty() ? out.components.size() : netIds.size());
    return true;
}

} // namespace Formats
