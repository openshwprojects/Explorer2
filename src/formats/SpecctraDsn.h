#pragma once
// SpecctraDsn.h — #26 Cadence OrCAD Capture OLE2 Schematic (.dsn) & Specctra PCB Autorouter S-Expression (.dsn)

namespace Formats {

namespace DetailSpecctra {

struct SNode {
    std::string atom;
    std::vector<SNode> list;
    bool isList = false;
};

inline bool ParseSExprTree(std::string_view sv, std::vector<SNode>& outRoots) {
    outRoots.clear();
    // Reject binary NULs
    for (unsigned char c : sv) {
        if (c == 0) return false;
    }

    char quoteChar = '"';
    size_t pos = 0;

    auto parseList = [&](auto& self, int depth, SNode& outList) -> bool {
        if (depth > 64) return false;
        outList.isList = true;
        while (pos < sv.size()) {
            while (pos < sv.size()) {
                unsigned char c = static_cast<unsigned char>(sv[pos]);
                if (c <= 0x20u) { ++pos; continue; }
                if (c == '#') {
                    while (pos < sv.size() && sv[pos] != '\n') ++pos;
                    continue;
                }
                break;
            }
            if (pos >= sv.size()) return false; // Unclosed '('!
            char c = sv[pos];
            if (c == ')') {
                ++pos;
                return true;
            }
            // Special Specctra rule: (string_quote ") has a single bare quote character token before ')'!
            if (outList.list.size() == 1 && !outList.list[0].isList && outList.list[0].atom == "string_quote" &&
                c != '(' && c != ')') {
                quoteChar = c;
                ++pos;
                SNode qNode;
                qNode.atom.assign(1, c);
                outList.list.push_back(std::move(qNode));
                continue;
            }
            if (c == '(') {
                ++pos;
                SNode child;
                if (!self(self, depth + 1, child)) return false;
                outList.list.push_back(std::move(child));
                continue;
            }
            if (c == quoteChar || c == '"') {
                char qc = c;
                ++pos;
                SNode atomNode;
                while (pos < sv.size() && sv[pos] != qc) {
                    if (sv[pos] == '\\' && pos + 1 < sv.size()) {
                        atomNode.atom.push_back(sv[++pos]);
                        ++pos;
                    } else {
                        atomNode.atom.push_back(sv[pos++]);
                    }
                }
                if (pos >= sv.size()) return false; // Unterminated string!
                ++pos;
                outList.list.push_back(std::move(atomNode));
                continue;
            }
            size_t s = pos;
            while (pos < sv.size()) {
                unsigned char ch = static_cast<unsigned char>(sv[pos]);
                if (ch <= 0x20u || ch == '(' || ch == ')' || ch == quoteChar || ch == '"') break;
                ++pos;
            }
            SNode atomNode;
            atomNode.atom.assign(sv.data() + s, pos - s);
            outList.list.push_back(std::move(atomNode));
        }
        return false;
    };

    while (pos < sv.size()) {
        while (pos < sv.size()) {
            unsigned char c = static_cast<unsigned char>(sv[pos]);
            if (c <= 0x20u) { ++pos; continue; }
            if (c == '#') { while (pos < sv.size() && sv[pos] != '\n') ++pos; continue; }
            break;
        }
        if (pos >= sv.size()) break;
        if (sv[pos] == ')') return false; // Stray ')'
        if (sv[pos] == '(') {
            ++pos;
            SNode root;
            if (!parseList(parseList, 0, root)) return false;
            outRoots.push_back(std::move(root));
        } else {
            return false;
        }
    }
    return !outRoots.empty();
}

inline std::string_view HeadKey(const SNode& n) {
    if (!n.isList || n.list.empty() || n.list[0].isList) return {};
    return n.list[0].atom;
}

inline bool AtomFloat(const SNode& n, double& outVal) {
    if (n.isList || n.atom.empty()) return false;
    char b[64] = {};
    std::memcpy(b, n.atom.data(), std::min<size_t>(n.atom.size(), 63));
    char* endp = nullptr;
    double v = std::strtod(b, &endp);
    if (endp == b || !std::isfinite(v)) return false;
    outVal = v;
    return true;
}

inline double UnitNameToMm(std::string_view u) {
    std::string s(u);
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (s == "mil" || s == "mils") return 0.0254;
    if (s == "in" || s == "inch" || s == "inches") return 25.4;
    if (s == "um" || s == "micron" || s == "microns") return 0.001;
    if (s == "cm") return 10.0;
    return 1.0; // mm
}

} // namespace DetailSpecctra

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

// ============================================================================
// Cadence OrCAD Capture OLE2 Schematic (.dsn / .olb, magic D0 CF 11 E0 A1 B1 1A E1)
// ============================================================================
inline bool ParseOrCadCaptureOle2Board(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    if (!data || size < 512 || std::memcmp(data, "\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1", 8) != 0) {
        return false;
    }

    Ole2::CompoundFile cf;
    if (!Ole2::ParseCompoundFile(data, size, cf) || !cf.valid) {
        return false;
    }

    // Verify genuine OrCAD Capture storage/stream hierarchy (Views/<Sch>/Pages/<Page>, Packages, Cache, CIS, AdminData)
    bool hasOrCadMarkers = false;
    std::vector<const Ole2::DirEntry*> pageStreams;
    std::unordered_map<std::string, std::vector<std::pair<std::string, std::pair<float, float>>>> pkgSymbolPins;

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
        if (e.objectType == 2 && e.size >= 16) {
            if (lowPath.find(L"/pages/") != std::wstring::npos || lowPath.rfind(L"pages/", 0) == 0) {
                pageStreams.push_back(&e);
            }
        }
    }

    // For .olb symbol library files (no Views/Pages, only Packages/<Sym> or Symbols/<Sym>)
    bool isOlbSymbolLib = false;
    if (pageStreams.empty() && hasOrCadMarkers) {
        for (const auto& e : cf.entries) {
            std::wstring lowPath = PathUtils::ToLower(e.fullPath);
            if (e.objectType == 2 && e.size >= 16 &&
                (lowPath.find(L"packages/") != std::wstring::npos || lowPath.find(L"symbols/") != std::wstring::npos) &&
                lowPath.find(L"directory") == std::wstring::npos) {
                pageStreams.push_back(&e);
                isOlbSymbolLib = true;
            }
        }
    }

    if (!hasOrCadMarkers || pageStreams.empty()) {
        return false;
    }

    auto rdU16 = [](const uint8_t* p) -> uint16_t { return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8)); };
    auto rdU32 = [](const uint8_t* p) -> uint32_t {
        return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
               (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
    };
    auto rdI32 = [&](const uint8_t* p) -> int32_t { return static_cast<int32_t>(rdU32(p)); };
    auto rdI16 = [&](const uint8_t* p) -> int16_t { return static_cast<int16_t>(rdU16(p)); };

    auto readLenStr = [&](const uint8_t* buf, size_t len, size_t& p, std::string& sOut) -> bool {
        sOut.clear();
        if (p + 2 > len) return false;
        uint16_t sl = rdU16(buf + p);
        p += 2;
        if (sl > 512 || p + sl > len) return false;
        for (uint16_t i = 0; i < sl; ++i) {
            uint8_t c = buf[p + i];
            if (c == 0) break;
            if (c >= 32 && c < 127) sOut.push_back(static_cast<char>(c));
        }
        p += sl;
        return true;
    };

    out.kind = EdaDocKind::KiCadSch;
    float sheetOffsetX = 0.0f;
    size_t renderedPages = 0;
    std::set<std::string> netSet;

    for (size_t pi = 0; pi < pageStreams.size() && pi < 24; ++pi) {
        std::vector<uint8_t> stm;
        if (!Ole2::ExtractStream(cf, *pageStreams[pi], stm, 16 * 1024 * 1024) || stm.size() < 16) {
            continue;
        }

        const uint8_t* b = stm.data();
        const size_t n = stm.size();
        const float gridScale = 0.1f; // 100 OrCAD grid units -> 10.0 mm

        std::string pageTitle = PathUtils::WideToUtf8(pageStreams[pi]->name);
        float pageW = 110.0f, pageH = 80.0f;
        size_t primsBefore = out.traces.size() + out.components.size() + out.pads.size();

        // OrCAD Capture Page streams begin with an 8-byte page header ("ORCPAGE1" or [ver:2][flags:2][width:2][height:2] / 26-byte struct header)
        size_t pos = 0;
        if (n >= 16 && std::memcmp(b, "ORCPAGE", 7) == 0) {
            pageW = std::clamp(static_cast<float>(rdI32(b + 8)) * gridScale, 40.0f, 400.0f);
            pageH = std::clamp(static_cast<float>(rdI32(b + 12)) * gridScale, 30.0f, 300.0f);
            pos = 16;
        } else if (n >= 32) {
            // Standard OrCAD v16/v17 stream header: skip leading header up to first structured [opCode:1][len:4][00 00 00 00] block
            for (size_t scan = 0; scan + 13 <= std::min<size_t>(n, 256); ++scan) {
                uint8_t op = b[scan];
                uint32_t rlen = rdU32(b + scan + 1);
                uint32_t zpad = rdU32(b + scan + 5);
                if ((op == 0x0D || op == 0x14 || op == 0x15 || op == 0x16 || op == 0x17 || op == 0x21 || op == 0x22 || op == 0x37 || op == 0x38 || op == 0x06) &&
                    zpad == 0 && rlen >= 8 && scan + 9 + rlen <= n) {
                    pos = scan;
                    break;
                }
            }
        }

        // Parse structured OrCAD Capture primitive records: [opCode:1][recLen:4][reserved0:4][payload:recLen]
        while (pos + 9 <= n) {
            uint8_t op = b[pos];
            uint32_t recLen = rdU32(b + pos + 1);
            uint32_t zpad = rdU32(b + pos + 5);
            if (zpad != 0 || pos + 9 + static_cast<uint64_t>(recLen) > n) {
                break;
            }
            const uint8_t* rec = b + pos + 9;
            size_t rp = 0;

            if (op == 0x14 || op == 0x15) {
                // WireScalar (0x14) or WireBus (0x15): int32 x1, y1, x2, y2[, uint16 netNameLen + netName]
                if (recLen >= 16) {
                    float x1 = sheetOffsetX + static_cast<float>(rdI32(rec + 0)) * gridScale;
                    float y1 = -static_cast<float>(rdI32(rec + 4)) * gridScale;
                    float x2 = sheetOffsetX + static_cast<float>(rdI32(rec + 8)) * gridScale;
                    float y2 = -static_cast<float>(rdI32(rec + 12)) * gridScale;
                    rp = 16;
                    std::string netNm;
                    if (rp + 2 <= recLen && readLenStr(rec, recLen, rp, netNm) && !netNm.empty()) {
                        netSet.insert(netNm);
                    }
                    out.traces.push_back({ x1, y1, x2, y2, (op == 0x15) ? 0.48f : 0.25f, EdaLayerId::SchWire, 0 });
                }
            } else if (op == 0x0D) {
                // Placed Part Instance (0x0D):
                // [int32 x, y][int16 w, h][uint8 rot, uint8 mirror][str refDes][str value][str pkgName][uint16 nPins]([int16 dx, dy][str pinName][str pinNum])*
                if (recLen >= 18) {
                    float px = sheetOffsetX + static_cast<float>(rdI32(rec + 0)) * gridScale;
                    float py = -static_cast<float>(rdI32(rec + 4)) * gridScale;
                    float bw = std::clamp(static_cast<float>(std::abs(rdI16(rec + 8))) * gridScale, 2.4f, 40.0f);
                    float bh = std::clamp(static_cast<float>(std::abs(rdI16(rec + 10))) * gridScale, 1.8f, 40.0f);
                    uint8_t rotIdx = rec[12] & 3u;
                    bool mir = (rec[13] != 0);
                    rp = 14;
                    std::string refDes, valStr, pkgStr;
                    readLenStr(rec, recLen, rp, refDes);
                    readLenStr(rec, recLen, rp, valStr);
                    readLenStr(rec, recLen, rp, pkgStr);

                    float rotDeg = static_cast<float>(rotIdx) * 90.0f;
                    float rad = rotDeg * 3.14159265f / 180.0f;
                    float cs = std::cos(rad), sn = std::sin(rad);
                    auto xfPt = [&](float lx, float ly, float& ox, float& oy) {
                        if (mir) lx = -lx;
                        ox = px + (lx * cs - ly * sn);
                        oy = py - (lx * sn + ly * cs);
                    };

                    // Symbol body rectangle
                    float hw = bw * 0.5f, hh = bh * 0.5f;
                    float c0x = 0, c0y = 0, c1x = 0, c1y = 0, c2x = 0, c2y = 0, c3x = 0, c3y = 0;
                    xfPt(-hw, -hh, c0x, c0y); xfPt( hw, -hh, c1x, c1y);
                    xfPt( hw,  hh, c2x, c2y); xfPt(-hw,  hh, c3x, c3y);
                    out.traces.push_back({ c0x, c0y, c1x, c1y, 0.26f, EdaLayerId::SchSymbol, 0 });
                    out.traces.push_back({ c1x, c1y, c2x, c2y, 0.26f, EdaLayerId::SchSymbol, 0 });
                    out.traces.push_back({ c2x, c2y, c3x, c3y, 0.26f, EdaLayerId::SchSymbol, 0 });
                    out.traces.push_back({ c3x, c3y, c0x, c0y, 0.26f, EdaLayerId::SchSymbol, 0 });

                    uint16_t nPins = 0;
                    if (rp + 2 <= recLen) {
                        nPins = std::min<uint16_t>(rdU16(rec + rp), 256);
                        rp += 2;
                        for (uint16_t k = 0; k < nPins && rp + 4 <= recLen; ++k) {
                            float ldx = static_cast<float>(rdI16(rec + rp)) * gridScale;
                            float ldy = static_cast<float>(rdI16(rec + rp + 2)) * gridScale;
                            rp += 4;
                            std::string pName, pNum;
                            readLenStr(rec, recLen, rp, pName);
                            readLenStr(rec, recLen, rp, pNum);
                            float wx = 0, wy = 0;
                            xfPt(ldx, ldy, wx, wy);
                            // Draw short pin lead to body edge
                            float bxClamped = std::clamp(ldx, -hw, hw);
                            float byClamped = std::clamp(ldy, -hh, hh);
                            float ex = 0, ey = 0;
                            xfPt(bxClamped, byClamped, ex, ey);
                            out.traces.push_back({ ex, ey, wx, wy, 0.22f, EdaLayerId::SchSymbol, 0 });
                            EdaPadPrim pad{};
                            pad.x = wx; pad.y = wy; pad.w = 0.65f; pad.h = 0.65f;
                            pad.layer = EdaLayerId::SchSymbol;
                            pad.pinName = PathUtils::Utf8ToWide(pName.empty() ? pNum : pName);
                            out.pads.push_back(std::move(pad));
                        }
                    }

                    EdaComponent3D comp;
                    comp.refDes = PathUtils::Utf8ToWide(refDes.empty() ? "U?" : refDes);
                    comp.value = PathUtils::Utf8ToWide(valStr);
                    comp.footprint = PathUtils::Utf8ToWide(pkgStr.empty() ? "ORCAD_PART" : pkgStr);
                    comp.x = px; comp.y = py;
                    comp.rotDeg = rotDeg;
                    comp.bottomSide = mir;
                    comp.widthMm = bw; comp.lengthMm = bh; comp.heightMm = 1.4f;
                    comp.pinCount = std::max<int>(2, nPins);
                    comp.category = ClassifyComponent(comp.refDes, comp.footprint, comp.widthMm, comp.lengthMm, comp.heightMm, comp.pinCount);
                    std::wstring lbl = comp.refDes + (comp.value.empty() ? L"" : (L" (" + comp.value + L")"));
                    out.labels.push_back({ px - hw, py + hh + 0.8f, 1.0f, EdaLayerId::SchLabel, lbl });
                    out.components.push_back(std::move(comp));
                }
            } else if (op == 0x16 && recLen >= 8) {
                // Junction solder dot (0x16)
                float jx = sheetOffsetX + static_cast<float>(rdI32(rec + 0)) * gridScale;
                float jy = -static_cast<float>(rdI32(rec + 4)) * gridScale;
                out.vias.push_back({ jx, jy, 0.70f, 0.0f, 0 });
            } else if ((op == 0x17 || op == 0x21 || op == 0x22) && recLen >= 10) {
                // NetAlias (0x17), Global/Power/Ground (0x21), Hierarchical Port (0x22)
                float nx = sheetOffsetX + static_cast<float>(rdI32(rec + 0)) * gridScale;
                float ny = -static_cast<float>(rdI32(rec + 4)) * gridScale;
                rp = 8;
                std::string symText;
                readLenStr(rec, recLen, rp, symText);
                if (!symText.empty()) {
                    netSet.insert(symText);
                    if (op == 0x21 && (symText == "0" || _stricmp(symText.c_str(), "GND") == 0 || _stricmp(symText.c_str(), "AGND") == 0)) {
                        out.traces.push_back({ nx, ny, nx, ny - 0.9f, 0.22f, EdaLayerId::SchSymbol, 0 });
                        out.traces.push_back({ nx - 1.1f, ny - 0.9f, nx + 1.1f, ny - 0.9f, 0.24f, EdaLayerId::SchSymbol, 0 });
                        out.traces.push_back({ nx - 0.7f, ny - 1.4f, nx + 0.7f, ny - 1.4f, 0.22f, EdaLayerId::SchSymbol, 0 });
                        out.traces.push_back({ nx - 0.3f, ny - 1.9f, nx + 0.3f, ny - 1.9f, 0.20f, EdaLayerId::SchSymbol, 0 });
                    } else if (op == 0x21) {
                        // Power rail T-bar
                        out.traces.push_back({ nx, ny, nx, ny + 1.0f, 0.22f, EdaLayerId::SchSymbol, 0 });
                        out.traces.push_back({ nx - 1.0f, ny + 1.0f, nx + 1.0f, ny + 1.0f, 0.26f, EdaLayerId::SchSymbol, 0 });
                        out.labels.push_back({ nx - 0.8f, ny + 1.6f, 0.92f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(symText) });
                    } else if (op == 0x22) {
                        // Port chevron box
                        out.traces.push_back({ nx - 2.6f, ny - 0.6f, nx - 0.5f, ny - 0.6f, 0.22f, EdaLayerId::SchSymbol, 0 });
                        out.traces.push_back({ nx - 0.5f, ny - 0.6f, nx,        ny,        0.22f, EdaLayerId::SchSymbol, 0 });
                        out.traces.push_back({ nx,        ny,        nx - 0.5f, ny + 0.6f, 0.22f, EdaLayerId::SchSymbol, 0 });
                        out.traces.push_back({ nx - 0.5f, ny + 0.6f, nx - 2.6f, ny + 0.6f, 0.22f, EdaLayerId::SchSymbol, 0 });
                        out.traces.push_back({ nx - 2.6f, ny + 0.6f, nx - 2.6f, ny - 0.6f, 0.22f, EdaLayerId::SchSymbol, 0 });
                        out.labels.push_back({ nx - 2.4f, ny - 0.2f, 0.85f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(symText) });
                    } else {
                        out.labels.push_back({ nx + 0.2f, ny + 0.4f, 0.92f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(symText) });
                    }
                }
            } else if (op == 0x37 && recLen >= 16) {
                // Graphic Line
                float x1 = sheetOffsetX + static_cast<float>(rdI32(rec + 0)) * gridScale;
                float y1 = -static_cast<float>(rdI32(rec + 4)) * gridScale;
                float x2 = sheetOffsetX + static_cast<float>(rdI32(rec + 8)) * gridScale;
                float y2 = -static_cast<float>(rdI32(rec + 12)) * gridScale;
                out.traces.push_back({ x1, y1, x2, y2, 0.18f, EdaLayerId::SchLabel, 0 });
            } else if (op == 0x38 && recLen >= 16) {
                // Graphic Box
                float x1 = sheetOffsetX + static_cast<float>(rdI32(rec + 0)) * gridScale;
                float y1 = -static_cast<float>(rdI32(rec + 4)) * gridScale;
                float x2 = sheetOffsetX + static_cast<float>(rdI32(rec + 8)) * gridScale;
                float y2 = -static_cast<float>(rdI32(rec + 12)) * gridScale;
                out.traces.push_back({ x1, y1, x2, y1, 0.18f, EdaLayerId::SchLabel, 0 });
                out.traces.push_back({ x2, y1, x2, y2, 0.18f, EdaLayerId::SchLabel, 0 });
                out.traces.push_back({ x2, y2, x1, y2, 0.18f, EdaLayerId::SchLabel, 0 });
                out.traces.push_back({ x1, y2, x1, y1, 0.18f, EdaLayerId::SchLabel, 0 });
            }

            pos += 9 + recLen;
        }

        if (out.traces.size() + out.components.size() + out.pads.size() > primsBefore) {
            // Draw schematic sheet border & title banner for each valid page
            float sx0 = sheetOffsetX, sy0 = 0.0f, sx1 = sheetOffsetX + pageW, sy1 = -pageH;
            out.traces.push_back({ sx0, sy0, sx1, sy0, 0.22f, EdaLayerId::EdgeCuts, 0 });
            out.traces.push_back({ sx1, sy0, sx1, sy1, 0.22f, EdaLayerId::EdgeCuts, 0 });
            out.traces.push_back({ sx1, sy1, sx0, sy1, 0.22f, EdaLayerId::EdgeCuts, 0 });
            out.traces.push_back({ sx0, sy1, sx0, sy0, 0.22f, EdaLayerId::EdgeCuts, 0 });
            out.labels.push_back({ sx0 + 2.0f, sy0 + 1.5f, 1.15f, EdaLayerId::SchLabel,
                L"Sheet: " + PathUtils::Utf8ToWide(pageTitle) });
            sheetOffsetX += pageW + 16.0f;
            ++renderedPages;
        }
    }

    if (renderedPages == 0 || (out.traces.empty() && out.components.empty() && out.pads.empty())) {
        out = ParsedEdaBoard{};
        return false;
    }

    out.formatTitle = isOlbSymbolLib
        ? (L"Cadence OrCAD Capture Symbol Library (.olb — " + std::to_wstring(renderedPages) + L" symbols)")
        : (L"Cadence OrCAD Capture Schematic (.dsn — " + std::to_wstring(renderedPages) + L" page" + (renderedPages > 1 ? L"s)" : L")"));
    out.netCount = std::max<size_t>(1, netSet.empty() ? out.components.size() : netSet.size());
    return true;
}

inline bool ParseOrCadOrSpecctraDsn(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    if (!data || size < 12) return false;
    if (size >= 512 && std::memcmp(data, "\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1", 8) == 0) {
        return ParseOrCadCaptureOle2Board(data, size, out);
    }
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    return ParseSpecctraSExprBoard(sv, out);
}

inline bool ParseSpecctraDsn(std::string_view sv, ParsedEdaBoard& out) {
    return ParseOrCadOrSpecctraDsn(reinterpret_cast<const uint8_t*>(sv.data()), sv.size(), out);
}

} // namespace Formats
