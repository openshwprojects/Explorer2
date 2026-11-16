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
// Cadence OrCAD Capture OLE2 Schematic & Library (.dsn / .olb, D0 CF 11 E0 A1 B1 1A E1)
// Supports both real Cadence OrCAD Capture v9-v22+ binary streams (Library string
// pool, Cache/Packages/Symbols PartCell+LibraryPart primitives & SymbolPins, and
// Page FutureData prefix-chain records) AND structured ORCPAGE streams.
// ============================================================================
namespace DetailOrCadBin {

inline uint16_t RdU16(const uint8_t* p) noexcept {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}
inline int16_t RdI16(const uint8_t* p) noexcept {
    return static_cast<int16_t>(RdU16(p));
}
inline uint32_t RdU32(const uint8_t* p) noexcept {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
inline int32_t RdI32(const uint8_t* p) noexcept {
    return static_cast<int32_t>(RdU32(p));
}

inline std::string UpperKey(std::string_view sv) {
    std::string r;
    r.reserve(sv.size());
    for (unsigned char c : sv) {
        if (c == 0) break;
        r.push_back(static_cast<char>(std::toupper(c)));
    }
    while (!r.empty() && (r.back() == ' ' || r.back() == '\t')) r.pop_back();
    return r;
}

inline std::string StripNormalSuffix(const std::string& upperName) {
    size_t dot = upperName.find(".NORMAL");
    if (dot != std::string::npos) return upperName.substr(0, dot);
    dot = upperName.find(".CONVERT");
    if (dot != std::string::npos) return upperName.substr(0, dot);
    return upperName;
}

// OrCAD readStringLenZeroTerm: [len : u16_le][chars : len bytes][0x00 : 1 byte]
inline bool ReadStrLenZero(const uint8_t* buf, size_t n, size_t& pos, std::string& outStr) {
    outStr.clear();
    if (pos + 3 > n) return false;
    uint16_t slen = RdU16(buf + pos);
    if (slen > 2048 || pos + 2 + static_cast<size_t>(slen) + 1 > n) return false;
    if (buf[pos + 2 + slen] != 0) return false;
    for (uint16_t i = 0; i < slen; ++i) {
        uint8_t c = buf[pos + 2 + i];
        if (c == 0) return false;
        if (c >= 32 || c == '\t') outStr.push_back(static_cast<char>(c));
    }
    pos += 2 + static_cast<size_t>(slen) + 1;
    return true;
}

inline void SkipOptionalPreamble(const uint8_t* buf, size_t n, size_t& pos, size_t limitOff) {
    const size_t lim = std::min(n, limitOff);
    if (pos + 8 <= lim &&
        buf[pos + 0] == 0xFFu && buf[pos + 1] == 0xE4u &&
        buf[pos + 2] == 0x5Cu && buf[pos + 3] == 0x39u)
    {
        uint32_t dlen = RdU32(buf + pos + 4);
        if (pos + 8 + static_cast<size_t>(dlen) <= lim) {
            pos += 8 + static_cast<size_t>(dlen);
        }
    }
}

struct OrCadPrefixInfo {
    uint8_t typeId = 0;
    size_t outerStop = 0;
    std::vector<size_t> checkpoints; // sorted in prefix order (checkpoints[0] == outerStop)
    std::vector<std::pair<uint32_t, uint32_t>> propNameValIdx;
};

// Matches OpenOrCadParser GenericParser::auto_read_prefixes (tries k = 10 down to 1)
inline bool ReadOrCadPrefixChain(const uint8_t* buf, size_t n, size_t& pos, size_t parentLimit, OrCadPrefixInfo& outInfo) {
    const size_t lim = std::min(n, parentLimit);
    if (pos + 3 > lim) return false;
    const uint8_t firstType = buf[pos];
    if (firstType == 0 || firstType > 110) return false;

    for (size_t k = 10; k >= 1; --k) {
        size_t p = pos;
        bool ok = true;
        std::vector<size_t> stops;
        stops.reserve(k);

        for (size_t i = 0; i + 1 < k; ++i) {
            if (p + 9 > lim || buf[p] != firstType) { ok = false; break; }
            uint32_t relSz = RdU32(buf + p + 1);
            size_t stopOff = p + 9 + static_cast<size_t>(relSz);
            if (stopOff > lim) { ok = false; break; }
            if (!stops.empty() && stopOff > stops.front()) { ok = false; break; }
            stops.push_back(stopOff);
            p += 9;
        }
        if (!ok) continue;

        // Short prefix at index k - 1: [typeId:u8][propCount:i16] + max(0, propCount)*8B
        if (p + 3 > lim || buf[p] != firstType) continue;
        int16_t propCnt = RdI16(buf + p + 1);
        if (propCnt < -1 || propCnt > 512) continue;
        size_t afterShort = p + 3 + (propCnt > 0 ? static_cast<size_t>(propCnt) * 8u : 0u);
        if (afterShort > lim) continue;
        if (!stops.empty()) {
            bool withinAll = true;
            for (size_t st : stops) {
                if (afterShort > st) { withinAll = false; break; }
            }
            if (!withinAll) continue;
        } else if (k == 1) {
            // Single short-only prefix is rare and ambiguous unless propCnt >= -1; require valid type
        }

        outInfo = OrCadPrefixInfo{};
        outInfo.typeId = firstType;
        outInfo.checkpoints = std::move(stops);
        outInfo.outerStop = outInfo.checkpoints.empty() ? lim : outInfo.checkpoints.front();
        if (propCnt > 0) {
            outInfo.propNameValIdx.reserve(static_cast<size_t>(propCnt));
            for (int16_t pi = 0; pi < propCnt; ++pi) {
                size_t mp = p + 3 + static_cast<size_t>(pi) * 8u;
                outInfo.propNameValIdx.emplace_back(RdU32(buf + mp), RdU32(buf + mp + 4));
            }
        }
        pos = afterShort;
        SkipOptionalPreamble(buf, n, pos, outInfo.outerStop);
        return true;
    }
    return false;
}

inline bool SkipOrCadStructure(const uint8_t* buf, size_t n, size_t& pos, size_t parentLimit) {
    OrCadPrefixInfo pfx;
    if (!ReadOrCadPrefixChain(buf, n, pos, parentLimit, pfx)) return false;
    if (pfx.checkpoints.empty()) return false;
    pos = pfx.outerStop;
    return true;
}

struct SymLineSeg { float x1, y1, x2, y2; };
struct SymCircle  { float cx, cy, r; };
struct SymPinDef  { float startX, startY, hotX, hotY; std::string name; uint16_t shape = 0; };

struct OrCadCachedSym {
    std::string name;
    std::vector<SymLineSeg> segs;
    std::vector<SymCircle> circles;
    std::vector<SymPinDef> pins;
    float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;

    void RecomputeBounds() {
        minX = 1e9f; minY = 1e9f; maxX = -1e9f; maxY = -1e9f;
        auto acc = [&](float x, float y) {
            minX = std::min(minX, x); maxX = std::max(maxX, x);
            minY = std::min(minY, y); maxY = std::max(maxY, y);
        };
        for (const auto& s : segs) { acc(s.x1, s.y1); acc(s.x2, s.y2); }
        for (const auto& c : circles) { acc(c.cx - c.r, c.cy - c.r); acc(c.cx + c.r, c.cy + c.r); }
        for (const auto& p : pins) { acc(p.startX, p.startY); acc(p.hotX, p.hotY); }
        if (minX > maxX) { minX = 0.0f; minY = 0.0f; maxX = 20.0f; maxY = 20.0f; }
    }
};

inline bool IsKnownPrimByte(uint8_t p) noexcept {
    return (p >= 40 && p <= 48) || p == 87;
}

// Parse a single OrCAD Primitive inside LibraryPart or SthInPages0 (coords in OrCAD units)
inline bool ParseOrCadPrimitive(const uint8_t* buf, size_t n, size_t& pos, size_t limitOff,
                                OrCadCachedSym& sym, std::vector<std::pair<std::pair<float, float>, std::string>>* outTexts = nullptr)
{
    const size_t lim = std::min(n, limitOff);
    if (pos + 2 > lim) return false;
    uint8_t p0 = buf[pos];
    uint8_t p1 = buf[pos + 1];
    uint8_t primType = 0;
    if (p0 == 48) {
        primType = 48; // SymbolVector (1-byte prefix in OOCP)
        pos += 1;
    } else if (p0 == p1 && IsKnownPrimByte(p0)) {
        primType = p0;
        pos += 2;
    } else if (IsKnownPrimByte(p0) && pos + 5 <= lim && RdU32(buf + pos + 1) >= 16 && RdU32(buf + pos + 1) <= 4096) {
        primType = p0;
        pos += 1;
    } else {
        return false;
    }

    if (primType == 48) {
        // PrimSymbolVector: [byteLen:u32][0:u32][locX:i16][locY:i16][nPrims:u16]...
        if (pos + 8 > lim) return false;
        uint32_t blen = RdU32(buf + pos);
        if (blen < 8 || pos + blen > lim) return false;
        pos += blen;
        SkipOptionalPreamble(buf, n, pos, lim);
        return true;
    }

    if (pos + 8 > lim) return false;
    const size_t startOff = pos;
    uint32_t byteLen = RdU32(buf + pos);
    if (primType == 46) byteLen += 8u; // PrimCommentText byteLen quirk in OrCAD
    if (byteLen < 12 || startOff + byteLen > lim) return false;

    if (primType == 40 && byteLen >= 24) {
        // Rect: [len:4][0:4][x1:i32][y1:i32][x2:i32][y2:i32]
        float x1 = static_cast<float>(RdI32(buf + startOff + 8));
        float y1 = static_cast<float>(RdI32(buf + startOff + 12));
        float x2 = static_cast<float>(RdI32(buf + startOff + 16));
        float y2 = static_cast<float>(RdI32(buf + startOff + 20));
        sym.segs.push_back({ x1, y1, x2, y1 });
        sym.segs.push_back({ x2, y1, x2, y2 });
        sym.segs.push_back({ x2, y2, x1, y2 });
        sym.segs.push_back({ x1, y2, x1, y1 });
    } else if (primType == 41 && byteLen >= 24) {
        // Line: [len:4][0:4][x1:i32][y1:i32][x2:i32][y2:i32]
        float x1 = static_cast<float>(RdI32(buf + startOff + 8));
        float y1 = static_cast<float>(RdI32(buf + startOff + 12));
        float x2 = static_cast<float>(RdI32(buf + startOff + 16));
        float y2 = static_cast<float>(RdI32(buf + startOff + 20));
        sym.segs.push_back({ x1, y1, x2, y2 });
    } else if (primType == 42 && byteLen >= 40) {
        // Arc: [len:4][0:4][x1,y1,x2,y2:i32][sx,sy,ex,ey:i32]
        float x1 = static_cast<float>(RdI32(buf + startOff + 8));
        float y1 = static_cast<float>(RdI32(buf + startOff + 12));
        float x2 = static_cast<float>(RdI32(buf + startOff + 16));
        float y2 = static_cast<float>(RdI32(buf + startOff + 20));
        float sx = static_cast<float>(RdI32(buf + startOff + 24));
        float sy = static_cast<float>(RdI32(buf + startOff + 28));
        float ex = static_cast<float>(RdI32(buf + startOff + 32));
        float ey = static_cast<float>(RdI32(buf + startOff + 36));
        float cx = (x1 + x2) * 0.5f, cy = (y1 + y2) * 0.5f;
        float rx = std::max(0.5f, std::fabs(x2 - x1) * 0.5f);
        float ry = std::max(0.5f, std::fabs(y2 - y1) * 0.5f);
        float a0 = std::atan2(sy - cy, sx - cx);
        float a1 = std::atan2(ey - cy, ex - cx);
        // OrCAD Y-down screen space: counter-clockwise in math Y-up corresponds to negative sweep in Y-down
        float sw = a1 - a0;
        if (sw <= 0.0f) sw += 6.2831853f;
        if (sw > 6.20f) {
            sym.circles.push_back({ cx, cy, (rx + ry) * 0.5f });
        } else {
            const int steps = 14;
            float px = cx + rx * std::cos(a0), py = cy + ry * std::sin(a0);
            for (int s = 1; s <= steps; ++s) {
                float ang = a0 + sw * (static_cast<float>(s) / static_cast<float>(steps));
                float nx = cx + rx * std::cos(ang), ny = cy + ry * std::sin(ang);
                sym.segs.push_back({ px, py, nx, ny });
                px = nx; py = ny;
            }
        }
    } else if (primType == 43 && byteLen >= 24) {
        // Ellipse: [len:4][0:4][x1,y1,x2,y2:i32]
        float x1 = static_cast<float>(RdI32(buf + startOff + 8));
        float y1 = static_cast<float>(RdI32(buf + startOff + 12));
        float x2 = static_cast<float>(RdI32(buf + startOff + 16));
        float y2 = static_cast<float>(RdI32(buf + startOff + 20));
        float cx = (x1 + x2) * 0.5f, cy = (y1 + y2) * 0.5f;
        float rx = std::fabs(x2 - x1) * 0.5f, ry = std::fabs(y2 - y1) * 0.5f;
        if (std::fabs(rx - ry) < 0.5f && rx > 0.2f) {
            sym.circles.push_back({ cx, cy, rx });
        } else if (rx > 0.2f && ry > 0.2f) {
            float px = cx + rx, py = cy;
            for (int s = 1; s <= 20; ++s) {
                float ang = 6.2831853f * (static_cast<float>(s) / 20.0f);
                float nx = cx + rx * std::cos(ang), ny = cy + ry * std::sin(ang);
                sym.segs.push_back({ px, py, nx, ny });
                px = nx; py = ny;
            }
        }
    } else if ((primType == 44 || primType == 45 || primType == 87) && byteLen >= 14) {
        // Polygon (44), Polyline (45), Bezier (87): Point array [y:i16][x:i16] at end of struct!
        // Locate pointCount (u16) such that hdrSize + 2 + pointCount * 4 == byteLen (or within 4B)
        for (size_t hdrSz : { size_t(8), size_t(16), size_t(24) }) {
            if (hdrSz + 2 <= byteLen) {
                uint16_t nPts = RdU16(buf + startOff + hdrSz);
                size_t ptsEnd = hdrSz + 2 + static_cast<size_t>(nPts) * 4u;
                if (nPts >= 2 && nPts <= 512 && ptsEnd <= byteLen && byteLen - ptsEnd <= 8) {
                    std::vector<std::pair<float, float>> pts;
                    pts.reserve(nPts);
                    for (uint16_t i = 0; i < nPts; ++i) {
                        size_t po = startOff + hdrSz + 2 + static_cast<size_t>(i) * 4u;
                        float py = static_cast<float>(RdI16(buf + po + 0)); // Point::read reads y THEN x!
                        float px = static_cast<float>(RdI16(buf + po + 2));
                        pts.emplace_back(px, py);
                    }
                    for (size_t i = 0; i + 1 < pts.size(); ++i) {
                        sym.segs.push_back({ pts[i].first, pts[i].second, pts[i + 1].first, pts[i + 1].second });
                    }
                    if (primType == 44 && pts.size() >= 3) {
                        sym.segs.push_back({ pts.back().first, pts.back().second, pts.front().first, pts.front().second });
                    }
                    break;
                }
            }
        }
    } else if (primType == 46 && byteLen >= 36) {
        // CommentText: [len:4][0:4][locX:i32][locY:i32][x2,y2,x1,y1:i32][fontIdx:u16][unk:2B][str]
        float tx = static_cast<float>(RdI32(buf + startOff + 8));
        float ty = static_cast<float>(RdI32(buf + startOff + 12));
        size_t sp = startOff + 36;
        std::string txt;
        if (outTexts && ReadStrLenZero(buf, startOff + byteLen, sp, txt) && !txt.empty()) {
            outTexts->push_back({ { tx, ty }, txt });
        }
    }

    pos = startOff + byteLen;
    SkipOptionalPreamble(buf, n, pos, lim);
    return true;
}

// Parse StructLibraryPart (typeId == 24) into OrCadCachedSym
inline bool ParseOrCadLibraryPart(const uint8_t* buf, size_t n, size_t& pos, size_t limitOff, OrCadCachedSym& outSym) {
    OrCadPrefixInfo pfx;
    if (!ReadOrCadPrefixChain(buf, n, pos, limitOff, pfx) || pfx.typeId != 24 || pfx.checkpoints.empty()) {
        return false;
    }

    std::string symName, srcLib;
    if (!ReadStrLenZero(buf, n, pos, symName) || !ReadStrLenZero(buf, n, pos, srcLib)) {
        pos = pfx.outerStop;
        return false;
    }
    outSym = OrCadCachedSym{};
    outSym.name = symName;

    // Find next checkpoint >= pos for end of primitives section
    size_t primStop = pfx.outerStop;
    for (auto it = pfx.checkpoints.rbegin(); it != pfx.checkpoints.rend(); ++it) {
        if (*it > pos + 6 && *it < pfx.outerStop) {
            primStop = *it;
            break;
        }
    }

    if (pos + 6 <= pfx.outerStop) {
        pos += 4; // 4 unknown bytes
        uint16_t nPrims = RdU16(buf + pos);
        pos += 2;
        for (uint16_t i = 0; i < nPrims && pos < primStop; ++i) {
            if (!ParseOrCadPrimitive(buf, n, pos, primStop, outSym, nullptr)) {
                break;
            }
            // Skip mysterious trailing bytes up to next double-byte primitive marker (only between primitives!)
            if (i + 1 < nPrims) {
                for (int d = 0; d < 64 && pos + 2 <= primStop; ++d) {
                    if ((buf[pos] == buf[pos + 1] && IsKnownPrimByte(buf[pos])) || buf[pos] == 48) break;
                    ++pos;
                }
            }
        }
    }

    // Advance to pin section checkpoint if available
    if (primStop < pfx.outerStop && pos <= primStop) {
        pos = primStop;
    }
    if (pos + 2 <= pfx.outerStop) {
        uint16_t nPins = RdU16(buf + pos);
        pos += 2;
        for (uint16_t i = 0; i < nPins && pos < pfx.outerStop && i < 1024; ++i) {
            if (buf[pos] == 0x00u) {
                ++pos; // Skipped pin slot in convert view
                continue;
            }
            OrCadPrefixInfo pinPfx;
            if (!ReadOrCadPrefixChain(buf, n, pos, pfx.outerStop, pinPfx) ||
                (pinPfx.typeId != 26 && pinPfx.typeId != 27))
            {
                break;
            }
            std::string pinNm;
            if (ReadStrLenZero(buf, n, pos, pinNm) && pos + 18 <= pinPfx.outerStop) {
                SymPinDef pd;
                pd.name = pinNm;
                pd.startX = static_cast<float>(RdI32(buf + pos + 0));
                pd.startY = static_cast<float>(RdI32(buf + pos + 4));
                pd.hotX   = static_cast<float>(RdI32(buf + pos + 8));
                pd.hotY   = static_cast<float>(RdI32(buf + pos + 12));
                pd.shape  = RdU16(buf + pos + 16);
                outSym.pins.push_back(std::move(pd));
            }
            pos = pinPfx.outerStop;
        }
    }

    outSym.RecomputeBounds();
    pos = pfx.outerStop;
    return !outSym.segs.empty() || !outSym.circles.empty() || !outSym.pins.empty();
}

// Parse StructSthInPages0 (typeId == 2) graphic wrapper
inline bool ParseOrCadSthInPages0(const uint8_t* buf, size_t n, size_t& pos, size_t limitOff,
                                  OrCadCachedSym& outSym,
                                  std::vector<std::pair<std::pair<float, float>, std::string>>* outTexts = nullptr)
{
    OrCadPrefixInfo pfx;
    if (!ReadOrCadPrefixChain(buf, n, pos, limitOff, pfx) || pfx.typeId != 2 || pfx.checkpoints.empty()) {
        return false;
    }
    std::string nm, srcLib;
    if (ReadStrLenZero(buf, n, pos, nm) && ReadStrLenZero(buf, n, pos, srcLib) && pos + 6 <= pfx.outerStop) {
        outSym.name = nm;
        pos += 4; // color
        uint16_t nPrims = RdU16(buf + pos);
        pos += 2;
        for (uint16_t i = 0; i < nPrims && pos < pfx.outerStop; ++i) {
            if (!ParseOrCadPrimitive(buf, n, pos, pfx.outerStop, outSym, outTexts)) break;
            if (i + 1 < nPrims && pos + 10 <= pfx.outerStop &&
                !((buf[pos] == buf[pos + 1] && IsKnownPrimByte(buf[pos])) || buf[pos] == 48))
            {
                pos += 8; // Optional 8 zero bytes between SthInPages0 primitives
            }
        }
    }
    outSym.RecomputeBounds();
    pos = pfx.outerStop;
    return !outSym.segs.empty() || !outSym.circles.empty() || (outTexts && !outTexts->empty());
}

// Extract string pool from OrCAD "Library" stream
inline bool ParseOrCadLibraryStream(const std::vector<uint8_t>& stm, std::vector<std::string>& outStrings) {
    if (stm.size() < 64) return false;
    std::string_view intro(reinterpret_cast<const char*>(stm.data()), std::min<size_t>(stm.size(), 32));
    if (intro.find("OrCAD Windows Design") == std::string_view::npos &&
        intro.find("OrCAD Windows Library") == std::string_view::npos)
    {
        return false;
    }

    const uint8_t* b = stm.data();
    const size_t n = stm.size();
    size_t pos = 32 + 16; // 32B intro + ver(4) + dates(8) + zero(4)
    if (pos + 2 > n) return true;
    uint16_t fontLen = RdU16(b + pos);
    pos += 2;
    if (fontLen == 0 || fontLen > 256) return true;
    size_t fontBytes = static_cast<size_t>(fontLen - 1) * 60u; // sizeof(LOGFONTA) == 60
    if (pos + fontBytes + 2 > n) return true;
    pos += fontBytes;

    uint16_t someLen = RdU16(b + pos);
    pos += 2;
    if (someLen > 256 || pos + static_cast<size_t>(someLen) * 2u + 8u > n) return true;
    pos += static_cast<size_t>(someLen) * 2u + 8u;

    std::string dummy;
    for (int i = 0; i < 8; ++i) {
        if (!ReadStrLenZero(b, n, pos, dummy)) return true;
    }
    // PageSettings is 152 bytes
    if (pos + 152 > n) return true;
    pos += 152;

    // Try u32 strLstLen first (OrCAD v16/v17+), fallback to u16 strLstLen (older OrCAD)
    for (int mode = 0; mode < 2; ++mode) {
        size_t tp = pos;
        uint32_t cnt = 0;
        if (mode == 0) {
            if (tp + 4 > n) continue;
            cnt = RdU32(b + tp);
            tp += 4;
        } else {
            if (tp + 2 > n) continue;
            cnt = RdU16(b + tp);
            tp += 2;
        }
        if (cnt > 200000) continue;
        std::vector<std::string> candidate;
        candidate.reserve(std::min<size_t>(cnt, 8192));
        bool allOk = true;
        for (uint32_t i = 0; i < cnt; ++i) {
            std::string s;
            if (!ReadStrLenZero(b, n, tp, s)) { allOk = false; break; }
            candidate.push_back(std::move(s));
        }
        if (allOk && tp + 2 <= n) {
            outStrings = std::move(candidate);
            break;
        }
    }
    return true;
}

// Scan Cache or Package stream for all LibraryPart (typeId == 24) symbol definitions
inline void CollectCachedLibraryParts(const std::vector<uint8_t>& stm, const std::string& fallbackKey,
                                      std::unordered_map<std::string, OrCadCachedSym>& symCache)
{
    const uint8_t* b = stm.data();
    const size_t n = stm.size();
    for (size_t i = 0; i + 16 < n; ++i) {
        if (b[i] != 24) continue;
        // Quick filter before full prefix check: byte 0 is 24, next prefix or short prefix also has 24
        uint32_t rel = RdU32(b + i + 1);
        if (rel < 16 || i + 9 + static_cast<size_t>(rel) > n) continue;
        if (b[i + 9] != 24) continue;

        size_t p = i;
        OrCadCachedSym sym;
        if (ParseOrCadLibraryPart(b, n, p, n, sym) && p > i) {
            std::string kFull = UpperKey(sym.name);
            std::string kBase = StripNormalSuffix(kFull);
            if (!kFull.empty()) symCache[kFull] = sym;
            if (!kBase.empty() && symCache.find(kBase) == symCache.end()) symCache[kBase] = sym;
            if (!fallbackKey.empty()) {
                std::string kFb = UpperKey(fallbackKey);
                if (!kFb.empty() && symCache.find(kFb) == symCache.end()) symCache[kFb] = sym;
            }
            i = p - 1;
        }
    }
}

inline void StampOrCadSymToBoard(const OrCadCachedSym* sym, float px, float py, uint8_t rotIdx, bool mirror,
                                 float gridScale, EdaLayerId bodyLayer, ParsedEdaBoard& out, size_t& outPinCount)
{
    const float rotDeg = static_cast<float>(rotIdx & 3u) * 90.0f;
    const float rad = rotDeg * 3.14159265f / 180.0f;
    const float cs = std::cos(rad), sn = std::sin(rad);

    // OrCAD internal symbol coordinates have +X right, +Y down; board schematic space has +X right, +Y up (y -> -y)
    auto xf = [&](float lx, float ly, float& ox, float& oy) {
        float sx = lx * gridScale;
        float sy = ly * gridScale;
        if (mirror) sx = -sx;
        float rx = sx * cs - sy * sn;
        float ry = sx * sn + sy * cs;
        ox = px + rx;
        oy = py - ry;
    };

    if (sym && (!sym->segs.empty() || !sym->circles.empty() || !sym->pins.empty())) {
        for (const auto& sg : sym->segs) {
            float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            xf(sg.x1, sg.y1, x1, y1);
            xf(sg.x2, sg.y2, x2, y2);
            out.traces.push_back({ x1, y1, x2, y2, 0.24f, bodyLayer, 0 });
        }
        for (const auto& ci : sym->circles) {
            float cx = 0, cy = 0;
            xf(ci.cx, ci.cy, cx, cy);
            out.circles.push_back({ cx, cy, std::max(0.25f, ci.r * gridScale), 0.22f, false, bodyLayer });
        }
        for (const auto& pd : sym->pins) {
            float sx = 0, sy = 0, hx = 0, hy = 0;
            xf(pd.startX, pd.startY, sx, sy);
            xf(pd.hotX, pd.hotY, hx, hy);
            out.traces.push_back({ sx, sy, hx, hy, 0.22f, EdaLayerId::SchSymbol, 0 });
            EdaPadPrim pad{};
            pad.x = hx; pad.y = hy; pad.w = 0.60f; pad.h = 0.60f;
            pad.layer = EdaLayerId::SchSymbol;
            pad.pinName = PathUtils::Utf8ToWide(pd.name);
            out.pads.push_back(std::move(pad));
            ++outPinCount;
        }
    } else {
        float hw = 3.0f, hh = 2.2f;
        out.traces.push_back({ px - hw, py - hh, px + hw, py - hh, 0.24f, bodyLayer, 0 });
        out.traces.push_back({ px + hw, py - hh, px + hw, py + hh, 0.24f, bodyLayer, 0 });
        out.traces.push_back({ px + hw, py + hh, px - hw, py + hh, 0.24f, bodyLayer, 0 });
        out.traces.push_back({ px - hw, py + hh, px - hw, py - hh, 0.24f, bodyLayer, 0 });
    }
}

// Parse a real Cadence OrCAD Capture Page stream (Structure::Page == 10)
inline bool ParseRealOrCadPageStream(const uint8_t* b, size_t n, float& ioSheetOffsetX,
                                     const std::string& fallbackTitle,
                                     const std::vector<std::string>& libStrings,
                                     const std::unordered_map<std::string, OrCadCachedSym>& symCache,
                                     std::set<std::string>& netSet,
                                     ParsedEdaBoard& out)
{
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
    // Note: PageSettings width/height in OrCAD are often in mils (e.g. 9700 x 7200 mils) while grid coords use pinToPin (10 units = 100 mils)
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
        pos += 4; // netId
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

            // Parse inline wire Aliases (Structure::Alias == 49)
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
            // Resolve Value from short-prefix property table if Library strLst is available
            for (const auto& [nIdx, vIdx] : iPfx.propNameValIdx) {
                if (nIdx < libStrings.size() && vIdx < libStrings.size()) {
                    const std::string& pKey = libStrings[nIdx];
                    if (_stricmp(pKey.c_str(), "Value") == 0 || _stricmp(pKey.c_str(), "Part Value") == 0) {
                        valStr = libStrings[vIdx];
                    }
                }
            }

            if (pos + 8 <= iPfx.outerStop) {
                size_t ip = pos + 8;
                if (ReadStrLenZero(b, n, ip, pkgName) && ip + 22 <= iPfx.outerStop) {
                    ip += 4 + 8; // dbId(4) + unk(8)
                    int16_t lx = RdI16(b + ip + 0);
                    int16_t ly = RdI16(b + ip + 2);
                    uint8_t flags0 = b[ip + 4];
                    uint8_t rotIdx = flags0 & 0x03u;
                    bool mirror = (flags0 & 0x80u) != 0 || (b[ip + 5] & 0x01u) != 0;
                    ip += 8; // locX(2)+locY(2)+flags(4)
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

    // Helper for GraphicInst-based objects: Port (23), Global (37, +5B), OffPageConnector (38, +5B)
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
                    gp += 4; // dbId
                    int16_t ly = RdI16(b + gp + 0); // StructGraphicInst reads locY THEN locX!
                    int16_t lx = RdI16(b + gp + 2);
                    float gx = offX + static_cast<float>(lx) * gridScale;
                    float gy = -static_cast<float>(ly) * gridScale;
                    if (!symName.empty()) netSet.insert(symName);
                    if (kindTag == 1 && (_stricmp(symName.c_str(), "0") == 0 || _stricmp(symName.c_str(), "GND") == 0 ||
                                         _stricmp(symName.c_str(), "GND_SIGNAL") == 0 || _stricmp(symName.c_str(), "AGND") == 0)) {
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
                    gp += 4; // dbId
                    int16_t ly = RdI16(b + gp + 0);
                    int16_t lx = RdI16(b + gp + 2);
                    gp += 12 + 4; // locY,locX,y2,x2,x1,y1 (12B) + color+3unk (4B)
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

    // Sheet border & title banner
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

} // namespace DetailOrCadBin

inline bool ParseOrCadCaptureOle2Board(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    if (!data || size < 512 || std::memcmp(data, "\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1", 8) != 0) {
        return false;
    }

    Ole2::CompoundFile cf;
    if (!Ole2::ParseCompoundFile(data, size, cf) || !cf.valid) {
        return false;
    }

    // Verify genuine OrCAD Capture storage/stream hierarchy (Library, Views/<Sch>/Pages/<Page>, Packages, Cache, CIS, AdminData)
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

    // For .olb symbol library files (no Views/Pages, only Packages/<Sym> or Symbols/<Sym>)
    bool isOlbSymbolLib = false;
    if (pageStreams.empty() && hasOrCadMarkers && !pkgOrSymStreams.empty()) {
        // Prefer Packages/<Name> over Symbols/$* helper streams when Packages exist
        std::vector<const Ole2::DirEntry*> pkgOnly;
        for (const auto* pe : pkgOrSymStreams) {
            if (PathUtils::ToLower(pe->fullPath).find(L"packages/") != std::wstring::npos) {
                pkgOnly.push_back(pe);
            }
        }
        pageStreams = pkgOnly.empty() ? pkgOrSymStreams : pkgOnly;
        isOlbSymbolLib = true;
    }

    if (!hasOrCadMarkers || pageStreams.empty()) {
        return false;
    }

    // Build cached symbol graphics dictionary from Cache and Packages/* streams
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

    auto rdU16 = DetailOrCadBin::RdU16;
    auto rdU32 = DetailOrCadBin::RdU32;
    auto rdI32 = DetailOrCadBin::RdI32;
    auto rdI16 = DetailOrCadBin::RdI16;

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

    for (size_t pi = 0; pi < pageStreams.size() && pi < 32; ++pi) {
        std::vector<uint8_t> stm;
        if (!Ole2::ExtractStream(cf, *pageStreams[pi], stm, 16 * 1024 * 1024) || stm.size() < 16) {
            continue;
        }

        const uint8_t* b = stm.data();
        const size_t n = stm.size();
        std::string pageTitle = PathUtils::WideToUtf8(pageStreams[pi]->name);

        // Path A: Real Cadence OrCAD Capture Page stream (Structure::Page == 10)
        if (!isOlbSymbolLib && b[0] == 10 && std::memcmp(b, "ORCPAGE", 7) != 0) {
            if (DetailOrCadBin::ParseRealOrCadPageStream(b, n, sheetOffsetX, pageTitle, libStrings, symCache, netSet, out)) {
                ++renderedPages;
                continue;
            }
        }

        // Path B: Real Cadence OrCAD Capture .olb Package / Symbol stream (PartCell==6 / LibraryPart==24 / Symbol SthInPages0==2)
        if (isOlbSymbolLib && std::memcmp(b, "ORCPAGE", 7) != 0) {
            std::unordered_map<std::string, DetailOrCadBin::OrCadCachedSym> localParts;
            DetailOrCadBin::CollectCachedLibraryParts(stm, pageTitle, localParts);
            const DetailOrCadBin::OrCadCachedSym* bestSym = nullptr;
            std::string kTarget = DetailOrCadBin::UpperKey(pageTitle);
            auto itT = localParts.find(kTarget);
            if (itT != localParts.end()) {
                bestSym = &itT->second;
            } else if (!localParts.empty()) {
                bestSym = &localParts.begin()->second;
            }

            DetailOrCadBin::OrCadCachedSym fallbackSym;
            if (!bestSym) {
                // Check standalone Symbol stream (GlobalSymbol 33 / PortSymbol 34 / TitleBlockSymbol 64 wrapping SthInPages0)
                for (size_t si = 0; si + 12 < n; ++si) {
                    if (b[si] == 2) {
                        size_t sp = si;
                        if (DetailOrCadBin::ParseOrCadSthInPages0(b, n, sp, n, fallbackSym, nullptr)) {
                            bestSym = &fallbackSym;
                            break;
                        }
                    }
                }
            }

            if (bestSym) {
                const float gScale = 0.254f; // 10 OrCAD symbol units = 2.54 mm
                float bw = std::clamp((bestSym->maxX - bestSym->minX) * gScale, 4.0f, 80.0f);
                float bh = std::clamp((bestSym->maxY - bestSym->minY) * gScale, 4.0f, 80.0f);
                float px = sheetOffsetX + 8.0f - bestSym->minX * gScale;
                float py = -8.0f + bestSym->minY * gScale;
                size_t pinCnt = 0;
                DetailOrCadBin::StampOrCadSymToBoard(bestSym, px, py, 0, false, gScale, EdaLayerId::SchSymbol, out, pinCnt);

                EdaComponent3D comp;
                comp.refDes = PathUtils::Utf8ToWide(pageTitle);
                comp.value = PathUtils::Utf8ToWide(bestSym->name);
                comp.footprint = L"ORCAD_OLB_SYM";
                comp.x = sheetOffsetX + 8.0f + bw * 0.5f;
                comp.y = -8.0f - bh * 0.5f;
                comp.widthMm = bw; comp.lengthMm = bh; comp.heightMm = 1.2f;
                comp.pinCount = std::max<int>(1, static_cast<int>(pinCnt));
                out.components.push_back(std::move(comp));

                float cardW = bw + 16.0f, cardH = bh + 16.0f;
                out.traces.push_back({ sheetOffsetX, 0.0f, sheetOffsetX + cardW, 0.0f, 0.20f, EdaLayerId::EdgeCuts, 0 });
                out.traces.push_back({ sheetOffsetX + cardW, 0.0f, sheetOffsetX + cardW, -cardH, 0.20f, EdaLayerId::EdgeCuts, 0 });
                out.traces.push_back({ sheetOffsetX + cardW, -cardH, sheetOffsetX, -cardH, 0.20f, EdaLayerId::EdgeCuts, 0 });
                out.traces.push_back({ sheetOffsetX, -cardH, sheetOffsetX, 0.0f, 0.20f, EdaLayerId::EdgeCuts, 0 });
                out.labels.push_back({ sheetOffsetX + 2.0f, 1.4f, 1.05f, EdaLayerId::SchLabel,
                                       L"Symbol: " + PathUtils::Utf8ToWide(pageTitle) });
                sheetOffsetX += cardW + 14.0f;
                ++renderedPages;
                continue;
            }
        }

        // Path C: Structured ORCPAGE / direct [opCode:1][recLen:4][0:4] stream records
        const float gridScale = 0.1f; // 100 OrCAD grid units -> 10.0 mm
        float pageW = 110.0f, pageH = 80.0f;
        size_t primsBefore = out.traces.size() + out.components.size() + out.pads.size();

        size_t pos = 0;
        if (n >= 16 && std::memcmp(b, "ORCPAGE", 7) == 0) {
            pageW = std::clamp(static_cast<float>(rdI32(b + 8)) * gridScale, 40.0f, 400.0f);
            pageH = std::clamp(static_cast<float>(rdI32(b + 12)) * gridScale, 30.0f, 300.0f);
            pos = 16;
        } else if (n >= 32) {
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
                float jx = sheetOffsetX + static_cast<float>(rdI32(rec + 0)) * gridScale;
                float jy = -static_cast<float>(rdI32(rec + 4)) * gridScale;
                out.vias.push_back({ jx, jy, 0.70f, 0.0f, 0 });
            } else if ((op == 0x17 || op == 0x21 || op == 0x22) && recLen >= 10) {
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
                        out.traces.push_back({ nx, ny, nx, ny + 1.0f, 0.22f, EdaLayerId::SchSymbol, 0 });
                        out.traces.push_back({ nx - 1.0f, ny + 1.0f, nx + 1.0f, ny + 1.0f, 0.26f, EdaLayerId::SchSymbol, 0 });
                        out.labels.push_back({ nx - 0.8f, ny + 1.6f, 0.92f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(symText) });
                    } else if (op == 0x22) {
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
                float x1 = sheetOffsetX + static_cast<float>(rdI32(rec + 0)) * gridScale;
                float y1 = -static_cast<float>(rdI32(rec + 4)) * gridScale;
                float x2 = sheetOffsetX + static_cast<float>(rdI32(rec + 8)) * gridScale;
                float y2 = -static_cast<float>(rdI32(rec + 12)) * gridScale;
                out.traces.push_back({ x1, y1, x2, y2, 0.18f, EdaLayerId::SchLabel, 0 });
            } else if (op == 0x38 && recLen >= 16) {
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
