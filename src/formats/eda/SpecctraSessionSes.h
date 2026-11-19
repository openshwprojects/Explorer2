#pragma once
// SpecctraSessionSes.h — Specctra Autorouter Routed Session (.ses) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

inline bool ParseSpecctraSessionSesBoard(std::string_view sv, const std::wstring& ext, ParsedEdaBoard& out) {
    if (sv.size() >= 3 &&
        static_cast<uint8_t>(sv[0]) == 0xEFu &&
        static_cast<uint8_t>(sv[1]) == 0xBBu &&
        static_cast<uint8_t>(sv[2]) == 0xBFu)
    {
        sv.remove_prefix(3);
    }

    // #62. Specctra Autorouter Routed Session (.ses)
    if (ext == L".ses") {
        using namespace DetailKiCadSymSesPos;
        if (sv.find("(session") == std::string_view::npos && sv.find("(routes") == std::string_view::npos) {
            return false;
        }
        std::vector<DetailSpecctra::SNode> roots;
        if (!DetailSpecctra::ParseSExprTree(sv, roots) || roots.empty()) {
            return false;
        }

        const DetailSpecctra::SNode* sesRoot = nullptr;
        for (const auto& r : roots) {
            std::string hk = LowerAsciiSymSes(DetailSpecctra::HeadKey(r));
            if (hk == "session" || hk == "routes") { sesRoot = &r; break; }
        }
        if (!sesRoot) return false;

        out.kind = EdaDocKind::KiCadPcb;
        std::string designName = (sesRoot->list.size() >= 2 && !sesRoot->list[1].isList) ? sesRoot->list[1].atom : "";

        auto parseResNodeScale = [](const DetailSpecctra::SNode& secNode, double fallbackScale) -> double {
            for (size_t i = 1; i < secNode.list.size(); ++i) {
                const auto& ch = secNode.list[i];
                if (LowerAsciiSymSes(DetailSpecctra::HeadKey(ch)) == "resolution" && ch.list.size() >= 3) {
                    double div = 1.0;
                    if (!ch.list[1].isList && DetailSpecctra::AtomFloat(ch.list[2], div) && div > 0.0) {
                        return DetailSpecctra::UnitNameToMm(ch.list[1].atom) / div;
                    }
                }
            }
            return fallbackScale;
        };

        auto mapSesLayer = [](std::string_view lyrRaw) -> EdaLayerId {
            std::string low = LowerAsciiSymSes(lyrRaw);
            if (low.find("b.") != std::string::npos || low.find("bot") != std::string::npos ||
                low.find("back") != std::string::npos || low.find("solder") != std::string::npos) {
                return EdaLayerId::BottomCopper;
            }
            if (low.find("in") == 0 || low.find("inner") != std::string::npos || low.find("mid") != std::string::npos ||
                low.find("gnd") != std::string::npos || low.find("pwr") != std::string::npos) {
                return EdaLayerId::InnerCopper;
            }
            if (low == "2" || low == "16" || low == "32") {
                return EdaLayerId::BottomCopper;
            }
            return EdaLayerId::TopCopper;
        };

        double rootScaleMm = parseResNodeScale(*sesRoot, 0.001);

        struct SesViaPad { float diamMm = 0.8f; float drillMm = 0.4f; };
        std::unordered_map<std::string, SesViaPad> viaPadstacks;
        std::set<std::string> routedNets;

        auto parseRoutesNode = [&](const DetailSpecctra::SNode& routesNode) {
            double rScale = parseResNodeScale(routesNode, rootScaleMm);

            for (size_t i = 1; i < routesNode.list.size(); ++i) {
                const auto& sub = routesNode.list[i];
                if (LowerAsciiSymSes(DetailSpecctra::HeadKey(sub)) != "library_out") continue;
                for (size_t j = 1; j < sub.list.size(); ++j) {
                    const auto& ps = sub.list[j];
                    if (LowerAsciiSymSes(DetailSpecctra::HeadKey(ps)) != "padstack" || ps.list.size() < 2) continue;
                    std::string psName = ps.list[1].atom;
                    float maxD = 0.0f;
                    for (size_t k = 2; k < ps.list.size(); ++k) {
                        const auto& sh = ps.list[k];
                        if (LowerAsciiSymSes(DetailSpecctra::HeadKey(sh)) != "shape" || sh.list.size() < 2) continue;
                        const auto& geom = sh.list[1];
                        std::string gk = LowerAsciiSymSes(DetailSpecctra::HeadKey(geom));
                        if (gk == "circle" && geom.list.size() >= 3) {
                            double d = 0.0;
                            if (DetailSpecctra::AtomFloat(geom.list[2], d)) maxD = std::max(maxD, static_cast<float>(d * rScale));
                        } else if (gk == "rect" && geom.list.size() >= 6) {
                            double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
                            if (DetailSpecctra::AtomFloat(geom.list[2], x1) && DetailSpecctra::AtomFloat(geom.list[3], y1) &&
                                DetailSpecctra::AtomFloat(geom.list[4], x2) && DetailSpecctra::AtomFloat(geom.list[5], y2)) {
                                maxD = std::max(maxD, static_cast<float>(std::max(std::fabs(x2 - x1), std::fabs(y2 - y1)) * rScale));
                            }
                        }
                    }
                    if (!psName.empty() && maxD > 0.1f) {
                        viaPadstacks[psName] = { std::clamp(maxD, 0.25f, 6.0f), std::clamp(maxD * 0.5f, 0.15f, 3.0f) };
                    }
                }
            }

            for (size_t i = 1; i < routesNode.list.size(); ++i) {
                const auto& sub = routesNode.list[i];
                if (LowerAsciiSymSes(DetailSpecctra::HeadKey(sub)) != "network_out") continue;
                for (size_t j = 1; j < sub.list.size(); ++j) {
                    const auto& netN = sub.list[j];
                    if (LowerAsciiSymSes(DetailSpecctra::HeadKey(netN)) != "net" || netN.list.size() < 2) continue;
                    std::string netName = netN.list[1].atom;
                    routedNets.insert(netName);
                    int netId = static_cast<int>(routedNets.size());

                    for (size_t k = 2; k < netN.list.size(); ++k) {
                        const auto& item = netN.list[k];
                        std::string ik = LowerAsciiSymSes(DetailSpecctra::HeadKey(item));
                        if (ik == "via" && item.list.size() >= 4 && out.vias.size() < 25000) {
                            std::string psName = item.list[1].atom;
                            double vx = 0.0, vy = 0.0;
                            if (DetailSpecctra::AtomFloat(item.list[2], vx) && DetailSpecctra::AtomFloat(item.list[3], vy)) {
                                SesViaPad vp{};
                                auto itVp = viaPadstacks.find(psName);
                                if (itVp != viaPadstacks.end()) vp = itVp->second;
                                out.vias.push_back({ static_cast<float>(vx * rScale), static_cast<float>(vy * rScale), vp.diamMm, vp.drillMm, netId });
                            }
                        } else if (ik == "wire") {
                            for (size_t m = 1; m < item.list.size(); ++m) {
                                const auto& wsub = item.list[m];
                                std::string wk = LowerAsciiSymSes(DetailSpecctra::HeadKey(wsub));
                                if ((wk == "path" || wk == "polyline_path" || wk == "polygon") && wsub.list.size() >= 7 && out.traces.size() < 100000) {
                                    EdaLayerId lid = mapSesLayer(wsub.list[1].atom);
                                    double wRaw = 0.0;
                                    DetailSpecctra::AtomFloat(wsub.list[2], wRaw);
                                    float wMm = std::clamp(static_cast<float>(wRaw * rScale), 0.10f, 4.0f);
                                    std::vector<std::pair<float, float>> pts;
                                    for (size_t p = 3; p + 1 < wsub.list.size(); p += 2) {
                                        if (wsub.list[p].isList || wsub.list[p + 1].isList) break;
                                        double px = 0.0, py = 0.0;
                                        if (!DetailSpecctra::AtomFloat(wsub.list[p], px) ||
                                            !DetailSpecctra::AtomFloat(wsub.list[p + 1], py)) break;
                                        pts.push_back({ static_cast<float>(px * rScale), static_cast<float>(py * rScale) });
                                    }
                                    for (size_t s = 1; s < pts.size(); ++s) {
                                        out.traces.push_back({ pts[s - 1].first, pts[s - 1].second, pts[s].first, pts[s].second, wMm, lid, netId });
                                    }
                                    if (wk == "polygon" && pts.size() >= 3) {
                                        out.traces.push_back({ pts.back().first, pts.back().second, pts.front().first, pts.front().second, wMm, lid, netId });
                                    }
                                } else if (wk == "qarc" && wsub.list.size() >= 9 && out.traces.size() < 100000) {
                                    EdaLayerId lid = mapSesLayer(wsub.list[1].atom);
                                    double wRaw = 0, sx = 0, sy = 0, ex = 0, ey = 0, cx = 0, cy = 0;
                                    if (DetailSpecctra::AtomFloat(wsub.list[2], wRaw) &&
                                        DetailSpecctra::AtomFloat(wsub.list[3], sx) && DetailSpecctra::AtomFloat(wsub.list[4], sy) &&
                                        DetailSpecctra::AtomFloat(wsub.list[5], ex) && DetailSpecctra::AtomFloat(wsub.list[6], ey) &&
                                        DetailSpecctra::AtomFloat(wsub.list[7], cx) && DetailSpecctra::AtomFloat(wsub.list[8], cy))
                                    {
                                        float wMm = std::clamp(static_cast<float>(wRaw * rScale), 0.10f, 4.0f);
                                        DetailEda100::AddTessellatedArc(
                                            out, static_cast<float>(sx * rScale), static_cast<float>(sy * rScale),
                                            static_cast<float>(ex * rScale), static_cast<float>(ey * rScale),
                                            static_cast<float>(cx * rScale), static_cast<float>(cy * rScale),
                                            false, wMm, lid, netId);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        };

        if (LowerAsciiSymSes(DetailSpecctra::HeadKey(*sesRoot)) == "routes") {
            parseRoutesNode(*sesRoot);
        } else {
            for (size_t i = 1; i < sesRoot->list.size(); ++i) {
                const auto& sec = sesRoot->list[i];
                std::string sk = LowerAsciiSymSes(DetailSpecctra::HeadKey(sec));
                if (sk == "base_design" && sec.list.size() >= 2 && !sec.list[1].isList) {
                    designName = sec.list[1].atom;
                } else if (sk == "placement") {
                    double pScale = parseResNodeScale(sec, rootScaleMm);
                    for (size_t j = 1; j < sec.list.size(); ++j) {
                        const auto& cmpNode = sec.list[j];
                        if (LowerAsciiSymSes(DetailSpecctra::HeadKey(cmpNode)) != "component" || cmpNode.list.size() < 2) continue;
                        std::string pkgName = cmpNode.list[1].atom;
                        for (size_t k = 2; k < cmpNode.list.size(); ++k) {
                            const auto& plc = cmpNode.list[k];
                            if (LowerAsciiSymSes(DetailSpecctra::HeadKey(plc)) != "place" || plc.list.size() < 6) continue;
                            std::string refDes = plc.list[1].atom;
                            double px = 0.0, py = 0.0, rot = 0.0;
                            if (!DetailSpecctra::AtomFloat(plc.list[2], px) || !DetailSpecctra::AtomFloat(plc.list[3], py)) continue;
                            std::string sideLow = LowerAsciiSymSes(plc.list[4].atom);
                            DetailSpecctra::AtomFloat(plc.list[5], rot);

                            EdaComponent3D comp;
                            comp.refDes = PathUtils::Utf8ToWide(refDes);
                            comp.footprint = PathUtils::Utf8ToWide(pkgName);
                            comp.x = static_cast<float>(px * pScale);
                            comp.y = static_cast<float>(py * pScale);
                            comp.rotDeg = static_cast<float>(rot);
                            comp.bottomSide = (sideLow == "back" || sideLow == "bottom" || sideLow == "b");
                            comp.widthMm = 2.4f; comp.lengthMm = 1.6f; comp.heightMm = 1.0f;
                            comp.category = ClassifyComponent(comp.refDes, comp.footprint, comp.widthMm, comp.lengthMm, comp.heightMm, comp.pinCount);
                            EdaLayerId silkL = comp.bottomSide ? EdaLayerId::BottomSilk : EdaLayerId::TopSilk;
                            DetailEda100::AddRectOutline(out, comp.x, comp.y, comp.widthMm, comp.lengthMm, silkL, 0.16f);
                            out.labels.push_back({ comp.x, comp.y, 0.85f, silkL, comp.refDes });
                            out.components.push_back(std::move(comp));
                        }
                    }
                } else if (sk == "routes") {
                    parseRoutesNode(sec);
                }
            }
        }

        // Zero-Cheat Guarantee: reject empty (session ...) shells with 0 routes, 0 vias, and 0 placed parts
        if (out.traces.empty() && out.vias.empty() && out.components.empty()) {
            out = ParsedEdaBoard{};
            return false;
        }

        out.formatTitle = L"Specctra Autorouter Routed Session (.ses";
        if (!designName.empty()) out.formatTitle += L" — " + PathUtils::Utf8ToWide(designName);
        out.formatTitle += L")";
        out.netCount = std::max<size_t>(1u, routedNets.empty() ? out.components.size() : routedNets.size());
        return true;
    }
    return false;
}

} // namespace Formats
