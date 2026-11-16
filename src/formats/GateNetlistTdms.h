#pragma once
// GateNetlistTdms.h — #30 EDIF (.edif/.edn), BLIF/EBLIF (.blif/.eblif), AIGER (.aig/.aag) Gate Netlists,
//                         Synopsys Liberty (.lib), IEEE 1497 SDF (.sdf) Timing, & NI TDMS (.tdms) Waveforms

namespace Formats {

namespace DetailGateTdms {

struct GatePinConn {
    std::string pinName;
    std::string netName;
    bool isOutput = false;
    bool inverted = false;
};

struct GateNode {
    std::string instName;
    std::string cellType;
    bool isPrimaryIn = false;
    bool isPrimaryOut = false;
    bool isSeq = false;
    std::vector<GatePinConn> pins;
    int level = 0;
    float x = 0.0f, y = 0.0f;
    float w = 8.0f, h = 5.0f;
};

inline bool RenderGateDagToBoard(
    const std::wstring& title,
    std::vector<GateNode>& nodes,
    ParsedEdaBoard& out)
{
    if (nodes.empty()) return false;
    const size_t n = std::min<size_t>(nodes.size(), 512);
    nodes.resize(n);

    // Map netName -> driver node index
    std::unordered_map<std::string, size_t> netDriver;
    std::set<std::string> allNets;
    for (size_t i = 0; i < n; ++i) {
        if (nodes[i].isPrimaryIn) {
            for (const auto& p : nodes[i].pins) {
                if (!p.netName.empty()) { netDriver[p.netName] = i; allNets.insert(p.netName); }
            }
        } else {
            for (const auto& p : nodes[i].pins) {
                if (!p.netName.empty()) allNets.insert(p.netName);
                if (p.isOutput && !p.netName.empty()) netDriver[p.netName] = i;
            }
        }
    }

    // Compute topological rank (cycle-safe memoized DFS; sequential latches/FFs break combinational feedback loops)
    std::vector<int> rankState(n, 0); // 0=unvisited, 1=visiting, 2=done
    auto computeRank = [&](auto& self, size_t u) -> int {
        if (rankState[u] == 2) return nodes[u].level;
        if (rankState[u] == 1) return 0; // Feedback loop broken
        rankState[u] = 1;
        if (nodes[u].isPrimaryIn) {
            nodes[u].level = 0;
        } else {
            int maxPred = 0;
            bool anyPred = false;
            for (const auto& p : nodes[u].pins) {
                if (p.isOutput) continue;
                auto it = netDriver.find(p.netName);
                if (it != netDriver.end() && it->second != u) {
                    anyPred = true;
                    // Sequential elements placed after combinational inputs unless feedback
                    int pr = (nodes[it->second].isSeq && nodes[u].isSeq) ? 0 : self(self, it->second);
                    maxPred = std::max(maxPred, pr);
                }
            }
            nodes[u].level = anyPred ? (maxPred + 1) : (nodes[u].isSeq ? 1 : 1);
        }
        rankState[u] = 2;
        return nodes[u].level;
    };

    int maxCombLevel = 1;
    for (size_t i = 0; i < n; ++i) {
        if (!nodes[i].isPrimaryOut) {
            maxCombLevel = std::max(maxCombLevel, computeRank(computeRank, i));
        }
    }
    for (size_t i = 0; i < n; ++i) {
        if (nodes[i].isPrimaryOut) {
            nodes[i].level = maxCombLevel + 1;
        }
    }

    // Group by level column and position vertically
    std::map<int, std::vector<size_t>> cols;
    for (size_t i = 0; i < n; ++i) cols[nodes[i].level].push_back(i);

    const float kColSpacing = 24.0f;
    const float kRowSpacing = 9.5f;
    for (auto& kv : cols) {
        auto& vec = kv.second;
        // Barycenter sort using fanin Y positions from earlier columns
        if (kv.first > 0) {
            std::stable_sort(vec.begin(), vec.end(), [&](size_t a, size_t b) {
                auto avgInY = [&](size_t idx) -> float {
                    float sum = 0.0f; int cnt = 0;
                    for (const auto& p : nodes[idx].pins) {
                        if (p.isOutput) continue;
                        auto it = netDriver.find(p.netName);
                        if (it != netDriver.end() && nodes[it->second].level < kv.first) {
                            sum += nodes[it->second].y; ++cnt;
                        }
                    }
                    return cnt > 0 ? (sum / static_cast<float>(cnt)) : 0.0f;
                };
                return avgInY(a) > avgInY(b);
            });
        }
        float totalH = static_cast<float>(vec.size() - 1) * kRowSpacing;
        for (size_t ri = 0; ri < vec.size(); ++ri) {
            GateNode& gn = nodes[vec[ri]];
            gn.x = static_cast<float>(kv.first) * kColSpacing;
            gn.y = totalH * 0.5f - static_cast<float>(ri) * kRowSpacing;
            size_t inCnt = 0, outCnt = 0;
            for (const auto& p : gn.pins) { if (p.isOutput) ++outCnt; else ++inCnt; }
            gn.w = (gn.isPrimaryIn || gn.isPrimaryOut) ? 5.0f : 9.0f;
            gn.h = (gn.isPrimaryIn || gn.isPrimaryOut) ? 2.4f : std::max(4.8f, static_cast<float>(std::max(inCnt, outCnt)) * 2.0f + 1.4f);
        }
    }

    out.kind = EdaDocKind::KiCadSch;
    out.formatTitle = title;

    // Record exact pin world positions per (netName) -> driver pt + sink pts
    struct PinAnchor { float x, y; int netId; };
    std::unordered_map<std::string, PinAnchor> driverPt;
    std::unordered_map<std::string, std::vector<PinAnchor>> sinkPts;
    std::unordered_map<std::string, int> netIdLookup;
    int nextNetId = 1;
    for (const auto& nm : allNets) netIdLookup[nm] = nextNetId++;

    for (size_t i = 0; i < n; ++i) {
        const GateNode& gn = nodes[i];
        float hw = gn.w * 0.5f, hh = gn.h * 0.5f;

        if (gn.isPrimaryIn) {
            // Input port chevron
            out.traces.push_back({ gn.x - hw, gn.y - hh, gn.x + hw * 0.5f, gn.y - hh, 0.24f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ gn.x + hw * 0.5f, gn.y - hh, gn.x + hw, gn.y, 0.24f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ gn.x + hw, gn.y, gn.x + hw * 0.5f, gn.y + hh, 0.24f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ gn.x + hw * 0.5f, gn.y + hh, gn.x - hw, gn.y + hh, 0.24f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ gn.x - hw, gn.y + hh, gn.x - hw, gn.y - hh, 0.24f, EdaLayerId::SchSymbol, 0 });
            out.labels.push_back({ gn.x - hw + 0.4f, gn.y - 0.3f, 0.85f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(gn.instName) });
            if (!gn.pins.empty()) {
                int nid = netIdLookup[gn.pins[0].netName];
                driverPt[gn.pins[0].netName] = { gn.x + hw, gn.y, nid };
                out.pads.push_back({ gn.x + hw, gn.y, 0.55f, 0.55f, 0, 0, 0, EdaLayerId::SchSymbol, PathUtils::Utf8ToWide(gn.instName), L"" });
            }
            continue;
        }
        if (gn.isPrimaryOut) {
            out.traces.push_back({ gn.x - hw, gn.y, gn.x - hw * 0.5f, gn.y - hh, 0.24f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ gn.x - hw * 0.5f, gn.y - hh, gn.x + hw, gn.y - hh, 0.24f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ gn.x + hw, gn.y - hh, gn.x + hw, gn.y + hh, 0.24f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ gn.x + hw, gn.y + hh, gn.x - hw * 0.5f, gn.y + hh, 0.24f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ gn.x - hw * 0.5f, gn.y + hh, gn.x - hw, gn.y, 0.24f, EdaLayerId::SchSymbol, 0 });
            out.labels.push_back({ gn.x - hw * 0.3f, gn.y - 0.3f, 0.85f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(gn.instName) });
            if (!gn.pins.empty()) {
                int nid = netIdLookup[gn.pins[0].netName];
                sinkPts[gn.pins[0].netName].push_back({ gn.x - hw, gn.y, nid });
                out.pads.push_back({ gn.x - hw, gn.y, 0.55f, 0.55f, 0, 0, 0, EdaLayerId::SchSymbol, PathUtils::Utf8ToWide(gn.instName), L"" });
            }
            continue;
        }

        // Logic gate / DFF / standard cell block
        out.traces.push_back({ gn.x - hw, gn.y - hh, gn.x + hw, gn.y - hh, 0.28f, EdaLayerId::SchSymbol, 0 });
        out.traces.push_back({ gn.x + hw, gn.y - hh, gn.x + hw, gn.y + hh, 0.28f, EdaLayerId::SchSymbol, 0 });
        out.traces.push_back({ gn.x + hw, gn.y + hh, gn.x - hw, gn.y + hh, 0.28f, EdaLayerId::SchSymbol, 0 });
        out.traces.push_back({ gn.x - hw, gn.y + hh, gn.x - hw, gn.y - hh, 0.28f, EdaLayerId::SchSymbol, 0 });
        if (gn.isSeq) {
            // IEEE clock '>' wedge on bottom-left of sequential DFF/Latch
            out.traces.push_back({ gn.x - hw, gn.y - hh + 0.6f, gn.x - hw + 1.1f, gn.y - hh + 1.2f, 0.22f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ gn.x - hw + 1.1f, gn.y - hh + 1.2f, gn.x - hw, gn.y - hh + 1.8f, 0.22f, EdaLayerId::SchSymbol, 0 });
        }

        std::vector<const GatePinConn*> ins, outs;
        for (const auto& p : gn.pins) {
            if (p.isOutput) outs.push_back(&p);
            else ins.push_back(&p);
        }
        for (size_t pi = 0; pi < ins.size(); ++pi) {
            float py = gn.y + hh - (gn.h / static_cast<float>(ins.size() + 1)) * static_cast<float>(pi + 1);
            float tipX = gn.x - hw - 1.6f;
            if (ins[pi]->inverted) {
                out.circles.push_back({ gn.x - hw - 0.45f, py, 0.42f, 0.20f, false, EdaLayerId::SchSymbol });
                out.traces.push_back({ tipX, py, gn.x - hw - 0.9f, py, 0.22f, EdaLayerId::SchSymbol, 0 });
            } else {
                out.traces.push_back({ tipX, py, gn.x - hw, py, 0.22f, EdaLayerId::SchSymbol, 0 });
            }
            out.pads.push_back({ tipX, py, 0.55f, 0.55f, 0, 0, 0, EdaLayerId::SchSymbol, PathUtils::Utf8ToWide(ins[pi]->pinName), PathUtils::Utf8ToWide(ins[pi]->netName) });
            if (!ins[pi]->netName.empty()) {
                sinkPts[ins[pi]->netName].push_back({ tipX, py, netIdLookup[ins[pi]->netName] });
            }
        }
        for (size_t po = 0; po < outs.size(); ++po) {
            float py = gn.y + hh - (gn.h / static_cast<float>(outs.size() + 1)) * static_cast<float>(po + 1);
            float tipX = gn.x + hw + 1.6f;
            out.traces.push_back({ gn.x + hw, py, tipX, py, 0.22f, EdaLayerId::SchSymbol, 0 });
            out.pads.push_back({ tipX, py, 0.55f, 0.55f, 0, 0, 0, EdaLayerId::SchSymbol, PathUtils::Utf8ToWide(outs[po]->pinName), PathUtils::Utf8ToWide(outs[po]->netName) });
            if (!outs[po]->netName.empty()) {
                driverPt[outs[po]->netName] = { tipX, py, netIdLookup[outs[po]->netName] };
            }
        }

        EdaComponent3D comp;
        comp.refDes = PathUtils::Utf8ToWide(gn.instName);
        comp.value = PathUtils::Utf8ToWide(gn.cellType);
        comp.footprint = PathUtils::Utf8ToWide(gn.cellType);
        comp.x = gn.x; comp.y = gn.y;
        comp.widthMm = gn.w; comp.lengthMm = gn.h; comp.heightMm = 1.2f;
        comp.pinCount = std::max(2, static_cast<int>(gn.pins.size()));
        comp.category = EdaPartCategory::IC_Package;
        out.labels.push_back({ gn.x - hw + 0.5f, gn.y + 0.3f, 0.80f, EdaLayerId::SchLabel, comp.refDes });
        out.labels.push_back({ gn.x - hw + 0.5f, gn.y - 0.9f, 0.72f, EdaLayerId::TopSilk, comp.value });
        out.components.push_back(std::move(comp));
    }

    // Route Manhattan orthogonal net wires from driver to each sink
    size_t wireIdx = 0;
    for (const auto& kv : sinkPts) {
        auto itDrv = driverPt.find(kv.first);
        if (itDrv == driverPt.end()) continue;
        const PinAnchor& src = itDrv->second;
        float channelJog = static_cast<float>((wireIdx % 7) - 3) * 0.65f;
        ++wireIdx;
        for (const auto& dst : kv.second) {
            if (out.traces.size() + 3 > 120000) break;
            float midX = (src.x + dst.x) * 0.5f + channelJog;
            if (dst.x <= src.x) {
                // Feedback wire routed over top/bottom channel
                float loopY = std::max(src.y, dst.y) + 4.2f + std::fabs(channelJog);
                out.traces.push_back({ src.x, src.y, src.x + 1.5f, src.y, 0.20f, EdaLayerId::SchWire, src.netId });
                out.traces.push_back({ src.x + 1.5f, src.y, src.x + 1.5f, loopY, 0.20f, EdaLayerId::SchWire, src.netId });
                out.traces.push_back({ src.x + 1.5f, loopY, dst.x - 1.5f, loopY, 0.20f, EdaLayerId::SchWire, src.netId });
                out.traces.push_back({ dst.x - 1.5f, loopY, dst.x - 1.5f, dst.y, 0.20f, EdaLayerId::SchWire, src.netId });
                out.traces.push_back({ dst.x - 1.5f, dst.y, dst.x, dst.y, 0.20f, EdaLayerId::SchWire, src.netId });
            } else {
                out.traces.push_back({ src.x, src.y, midX, src.y, 0.22f, EdaLayerId::SchWire, src.netId });
                out.traces.push_back({ midX, src.y, midX, dst.y, 0.22f, EdaLayerId::SchWire, src.netId });
                out.traces.push_back({ midX, dst.y, dst.x, dst.y, 0.22f, EdaLayerId::SchWire, src.netId });
            }
        }
        if (kv.second.size() >= 2 && out.vias.size() < 20000) {
            out.vias.push_back({ src.x + (kv.second[0].x - src.x) * 0.5f + channelJog, src.y, 0.55f, 0.0f, src.netId });
        }
    }

    out.netCount = std::max<size_t>(1, allNets.size());
    return !out.traces.empty();
}

inline std::string EdifIdName(const DetailSpecctra::SNode& n) {
    if (!n.isList) return n.atom;
    if (n.list.size() >= 3 && !n.list[0].isList) {
        std::string k = n.list[0].atom;
        for (char& c : k) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (k == "rename") return n.list[2].isList ? n.list[1].atom : n.list[2].atom;
        if (k == "member" && !n.list[1].isList && !n.list[2].isList) return n.list[1].atom + "[" + n.list[2].atom + "]";
    }
    return (!n.list.empty() && !n.list[0].isList) ? n.list[0].atom : "";
}

inline std::string LowerAsciiCopy(std::string_view sv) {
    std::string s(sv);
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

} // namespace DetailGateTdms

// 1. EDIF 2 0 0 Gate-Level Netlist (.edif / .edn)
inline bool ParseEdifNetlistBoard(std::string_view sv, ParsedEdaBoard& out) {
    using namespace DetailGateTdms;
    std::vector<DetailSpecctra::SNode> roots;
    if (!DetailSpecctra::ParseSExprTree(sv, roots) || roots.empty()) return false;

    const DetailSpecctra::SNode* edifRoot = nullptr;
    for (const auto& r : roots) {
        if (LowerAsciiCopy(DetailSpecctra::HeadKey(r)) == "edif") { edifRoot = &r; break; }
    }
    if (!edifRoot || edifRoot->list.size() < 3) return false;
    std::string edifDesignName = EdifIdName(edifRoot->list[1]);

    // Collect cell port directions across all libraries, and locate the richest cell with (contents ...)
    struct EdifCellInfo {
        std::string name;
        std::unordered_map<std::string, std::string> portDirs; // portName -> INPUT/OUTPUT/INOUT
        const DetailSpecctra::SNode* contentsNode = nullptr;
    };
    std::unordered_map<std::string, EdifCellInfo> cells;
    std::string topCellName;

    for (size_t i = 2; i < edifRoot->list.size(); ++i) {
        const auto& topSec = edifRoot->list[i];
        std::string sk = LowerAsciiCopy(DetailSpecctra::HeadKey(topSec));
        if (sk == "library" || sk == "external") {
            for (size_t ci = 2; ci < topSec.list.size(); ++ci) {
                const auto& cellNode = topSec.list[ci];
                if (LowerAsciiCopy(DetailSpecctra::HeadKey(cellNode)) != "cell" || cellNode.list.size() < 3) continue;
                EdifCellInfo ciInfo;
                ciInfo.name = EdifIdName(cellNode.list[1]);
                for (size_t vi = 2; vi < cellNode.list.size(); ++vi) {
                    const auto& viewNode = cellNode.list[vi];
                    if (LowerAsciiCopy(DetailSpecctra::HeadKey(viewNode)) != "view") continue;
                    for (size_t sub = 2; sub < viewNode.list.size(); ++sub) {
                        const auto& sn = viewNode.list[sub];
                        std::string subK = LowerAsciiCopy(DetailSpecctra::HeadKey(sn));
                        if (subK == "interface") {
                            for (size_t pi = 1; pi < sn.list.size(); ++pi) {
                                const auto& pNode = sn.list[pi];
                                if (LowerAsciiCopy(DetailSpecctra::HeadKey(pNode)) == "port" && pNode.list.size() >= 2) {
                                    std::string pName = EdifIdName(pNode.list[1]);
                                    std::string pDir = "INPUT";
                                    for (size_t di = 2; di < pNode.list.size(); ++di) {
                                        if (LowerAsciiCopy(DetailSpecctra::HeadKey(pNode.list[di])) == "direction" &&
                                            pNode.list[di].list.size() >= 2) {
                                            pDir = LowerAsciiCopy(pNode.list[di].list[1].atom);
                                        }
                                    }
                                    ciInfo.portDirs[pName] = pDir;
                                }
                            }
                        } else if (subK == "contents") {
                            ciInfo.contentsNode = &sn;
                            topCellName = ciInfo.name;
                        }
                    }
                }
                cells[ciInfo.name] = std::move(ciInfo);
            }
        } else if (sk == "design" && topSec.list.size() >= 3) {
            for (size_t di = 2; di < topSec.list.size(); ++di) {
                const auto& cr = topSec.list[di];
                if (LowerAsciiCopy(DetailSpecctra::HeadKey(cr)) == "cellref" && cr.list.size() >= 2) {
                    std::string cand = EdifIdName(cr.list[1]);
                    if (cells.count(cand) && cells[cand].contentsNode) topCellName = cand;
                }
            }
        }
    }

    if (topCellName.empty() || !cells.count(topCellName) || !cells[topCellName].contentsNode) {
        return false;
    }

    const EdifCellInfo& topCell = cells[topCellName];
    const auto& contents = *topCell.contentsNode;

    std::vector<GateNode> dagNodes;
    std::unordered_map<std::string, size_t> instMap;
    std::unordered_map<std::string, size_t> topPortNodeMap;

    for (const auto& kv : topCell.portDirs) {
        GateNode pn;
        pn.instName = kv.first;
        pn.cellType = "PORT";
        pn.isPrimaryIn = (kv.second != "output");
        pn.isPrimaryOut = (kv.second == "output");
        pn.pins.push_back({ kv.first, "", pn.isPrimaryIn, false });
        topPortNodeMap[kv.first] = dagNodes.size();
        dagNodes.push_back(std::move(pn));
    }

    // Pass 1: instances
    for (size_t i = 1; i < contents.list.size(); ++i) {
        const auto& item = contents.list[i];
        if (LowerAsciiCopy(DetailSpecctra::HeadKey(item)) == "instance" && item.list.size() >= 3) {
            GateNode gn;
            gn.instName = EdifIdName(item.list[1]);
            for (size_t k = 2; k < item.list.size(); ++k) {
                const auto& vr = item.list[k];
                if (LowerAsciiCopy(DetailSpecctra::HeadKey(vr)) == "viewref") {
                    for (size_t m = 2; m < vr.list.size(); ++m) {
                        if (LowerAsciiCopy(DetailSpecctra::HeadKey(vr.list[m])) == "cellref" && vr.list[m].list.size() >= 2) {
                            gn.cellType = EdifIdName(vr.list[m].list[1]);
                        }
                    }
                }
            }
            std::string cl = LowerAsciiCopy(gn.cellType);
            gn.isSeq = (cl.find("fd") != std::string::npos || cl.find("dff") != std::string::npos || cl.find("reg") != std::string::npos);
            instMap[gn.instName] = dagNodes.size();
            dagNodes.push_back(std::move(gn));
        }
    }

    // Pass 2: nets -> joined portRefs
    for (size_t i = 1; i < contents.list.size(); ++i) {
        const auto& item = contents.list[i];
        if (LowerAsciiCopy(DetailSpecctra::HeadKey(item)) != "net" || item.list.size() < 3) continue;
        std::string netName = EdifIdName(item.list[1]);
        for (size_t k = 2; k < item.list.size(); ++k) {
            const auto& jn = item.list[k];
            if (LowerAsciiCopy(DetailSpecctra::HeadKey(jn)) != "joined") continue;
            for (size_t p = 1; p < jn.list.size(); ++p) {
                const auto& pr = jn.list[p];
                if (LowerAsciiCopy(DetailSpecctra::HeadKey(pr)) != "portref" || pr.list.size() < 2) continue;
                std::string portNm = EdifIdName(pr.list[1]);
                std::string instNm;
                for (size_t m = 2; m < pr.list.size(); ++m) {
                    if (LowerAsciiCopy(DetailSpecctra::HeadKey(pr.list[m])) == "instanceref" && pr.list[m].list.size() >= 2) {
                        instNm = EdifIdName(pr.list[m].list[1]);
                    }
                }
                if (instNm.empty()) {
                    auto itP = topPortNodeMap.find(portNm);
                    if (itP != topPortNodeMap.end() && !dagNodes[itP->second].pins.empty()) {
                        dagNodes[itP->second].pins[0].netName = netName;
                    }
                } else {
                    auto itI = instMap.find(instNm);
                    if (itI != instMap.end()) {
                        GateNode& gn = dagNodes[itI->second];
                        bool isOut = (portNm == "Q" || portNm == "Y" || portNm == "O" || portNm == "Z" || portNm == "ZN" || portNm == "CO");
                        auto itCell = cells.find(gn.cellType);
                        if (itCell != cells.end() && itCell->second.portDirs.count(portNm)) {
                            isOut = (itCell->second.portDirs[portNm] == "output");
                        }
                        gn.pins.push_back({ portNm, netName, isOut, false });
                    }
                }
            }
        }
    }

    std::wstring title = L"EDIF 2 0 0 Gate Netlist (.edif — " + PathUtils::Utf8ToWide(edifDesignName.empty() ? topCellName : edifDesignName) + L")";
    return RenderGateDagToBoard(title, dagNodes, out);
}

// 2. Berkeley BLIF / Yosys EBLIF Gate-Level Netlist (.blif / .eblif)
inline bool ParseBlifNetlistBoard(std::string_view sv, ParsedEdaBoard& out) {
    using namespace DetailGateTdms;
    for (unsigned char c : sv) {
        if (c == 0) return false;
    }
    if (sv.find(".model") == std::string_view::npos || sv.find(".end") == std::string_view::npos) {
        return false;
    }

    // Join '\' continuation lines & strip '#' comments
    std::vector<std::string> lines;
    std::string cur;
    size_t pos = 0;
    while (pos < sv.size() && lines.size() < 50000) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view raw = sv.substr(pos, eol - pos);
        pos = eol + 1;
        size_t hash = raw.find('#');
        if (hash != std::string_view::npos) raw = raw.substr(0, hash);
        while (!raw.empty() && (raw.back() == '\r' || raw.back() == ' ' || raw.back() == '\t')) raw.remove_suffix(1);
        if (!raw.empty() && raw.back() == '\\') {
            raw.remove_suffix(1);
            cur.append(raw.data(), raw.size());
            cur.push_back(' ');
            continue;
        }
        cur.append(raw.data(), raw.size());
        size_t s = cur.find_first_not_of(" \t");
        if (s != std::string::npos) lines.push_back(cur.substr(s));
        cur.clear();
    }

    auto splitWs = [](const std::string& s) -> std::vector<std::string> {
        std::vector<std::string> t;
        size_t p = 0;
        while (p < s.size()) {
            while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) ++p;
            if (p >= s.size()) break;
            size_t e = p;
            while (e < s.size() && s[e] != ' ' && s[e] != '\t') ++e;
            t.push_back(s.substr(p, e - p));
            p = e;
        }
        return t;
    };

    std::string modelName;
    bool sawEnd = false;
    std::vector<GateNode> dagNodes;
    int lutCounter = 0, ffCounter = 0, subCounter = 0;

    for (size_t li = 0; li < lines.size(); ++li) {
        auto toks = splitWs(lines[li]);
        if (toks.empty()) continue;
        const std::string& kw = toks[0];
        if (kw == ".model" && toks.size() >= 2) {
            modelName = toks[1];
        } else if (kw == ".end") {
            sawEnd = true;
            break;
        } else if (kw == ".inputs") {
            for (size_t i = 1; i < toks.size(); ++i) {
                GateNode gn;
                gn.instName = toks[i]; gn.cellType = "INPUT"; gn.isPrimaryIn = true;
                gn.pins.push_back({ "O", toks[i], true, false });
                dagNodes.push_back(std::move(gn));
            }
        } else if (kw == ".outputs") {
            for (size_t i = 1; i < toks.size(); ++i) {
                GateNode gn;
                gn.instName = toks[i]; gn.cellType = "OUTPUT"; gn.isPrimaryOut = true;
                gn.pins.push_back({ "I", toks[i], false, false });
                dagNodes.push_back(std::move(gn));
            }
        } else if (kw == ".latch" && toks.size() >= 3) {
            GateNode gn;
            gn.instName = "DFF_" + std::to_string(++ffCounter);
            gn.cellType = "DFF";
            gn.isSeq = true;
            gn.pins.push_back({ "D", toks[1], false, false });
            if (toks.size() >= 5 && toks[4] != "NIL") gn.pins.push_back({ "CLK", toks[4], false, false });
            gn.pins.push_back({ "Q", toks[2], true, false });
            dagNodes.push_back(std::move(gn));
        } else if (kw == ".names" && toks.size() >= 2) {
            // Collect PLA cover rows following .names
            std::vector<std::string> cover;
            while (li + 1 < lines.size() && !lines[li + 1].empty() && lines[li + 1][0] != '.') {
                cover.push_back(lines[++li]);
            }
            size_t nIn = toks.size() - 2;
            std::string outNet = toks.back();
            GateNode gn;
            gn.instName = "G" + std::to_string(++lutCounter);
            if (nIn == 0) gn.cellType = cover.empty() ? "CONST0" : "CONST1";
            else if (nIn == 1) gn.cellType = (!cover.empty() && cover[0][0] == '0') ? "INV" : "BUF";
            else if (nIn == 2 && cover.size() == 1 && cover[0].rfind("11", 0) == 0) gn.cellType = "AND2";
            else if (nIn == 2 && cover.size() == 2 && ((cover[0].rfind("10", 0) == 0 && cover[1].rfind("01", 0) == 0) ||
                                                       (cover[0].rfind("01", 0) == 0 && cover[1].rfind("10", 0) == 0))) gn.cellType = "XOR2";
            else if (nIn == 2 && cover.size() == 2) gn.cellType = "OR2";
            else gn.cellType = "LUT" + std::to_string(nIn);

            for (size_t k = 0; k < nIn; ++k) {
                gn.pins.push_back({ "I" + std::to_string(k), toks[1 + k], false, gn.cellType == "INV" });
            }
            gn.pins.push_back({ "Y", outNet, true, false });
            dagNodes.push_back(std::move(gn));
        } else if ((kw == ".gate" || kw == ".subckt") && toks.size() >= 3) {
            GateNode gn;
            gn.cellType = toks[1];
            gn.instName = "U" + std::to_string(++subCounter);
            std::string cl = LowerAsciiCopy(gn.cellType);
            gn.isSeq = (cl.find("dff") != std::string::npos || cl.find("fd") != std::string::npos || cl.find("sdff") != std::string::npos);
            for (size_t k = 2; k < toks.size(); ++k) {
                size_t eq = toks[k].find('=');
                if (eq == std::string::npos) continue;
                std::string pn = toks[k].substr(0, eq);
                std::string nn = toks[k].substr(eq + 1);
                bool isOut = (pn == "Y" || pn == "Q" || pn == "O" || pn == "Z" || pn == "ZN" || pn == "CO" || pn == "OUT");
                gn.pins.push_back({ pn, nn, isOut, false });
            }
            // EBLIF optional .cname <instName> right after .subckt
            if (li + 1 < lines.size() && lines[li + 1].rfind(".cname ", 0) == 0) {
                auto ct = splitWs(lines[++li]);
                if (ct.size() >= 2) gn.instName = ct[1];
            }
            dagNodes.push_back(std::move(gn));
        }
    }

    if (!sawEnd || dagNodes.empty()) return false;
    std::wstring title = L"BLIF / EBLIF Gate Netlist (" + PathUtils::Utf8ToWide(modelName.empty() ? "top" : modelName) + L")";
    return RenderGateDagToBoard(title, dagNodes, out);
}

// 3. Biere AIGER And-Inverter Graph (.aig Binary & .aag ASCII)
inline bool ParseAigerNetlistBoard(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    using namespace DetailGateTdms;
    if (!data || size < 10 || size > 32 * 1024 * 1024) return false;
    bool isBin = (std::memcmp(data, "aig ", 4) == 0);
    bool isAsc = (std::memcmp(data, "aag ", 4) == 0);
    if (!isBin && !isAsc) return false;

    size_t eol0 = 4;
    while (eol0 < size && data[eol0] != '\n') ++eol0;
    if (eol0 >= size) return false;
    std::string hdrLine(reinterpret_cast<const char*>(data + 4), eol0 - 4);
    unsigned int M = 0, I = 0, L = 0, O = 0, A = 0;
    if (sscanf_s(hdrLine.c_str(), "%u %u %u %u %u", &M, &I, &L, &O, &A) < 5) return false;
    if (M < I + L + A || (I + L + O + A) == 0 || M > 500000u) return false;

    size_t pos = eol0 + 1;
    auto readLineStr = [&](std::string& sOut) -> bool {
        if (pos >= size) return false;
        size_t e = pos;
        while (e < size && data[e] != '\n') ++e;
        if (e >= size && isBin) return false;
        size_t len = e - pos;
        if (len > 0 && data[pos + len - 1] == '\r') --len;
        sOut.assign(reinterpret_cast<const char*>(data + pos), len);
        pos = (e < size) ? (e + 1) : size;
        return true;
    };

    std::vector<uint32_t> inputLits(I);
    std::vector<std::pair<uint32_t, uint32_t>> latches(L); // (latchLit, nextLit)
    std::vector<uint32_t> outputs(O);
    struct AigAnd { uint32_t lhs, rhs0, rhs1; };
    std::vector<AigAnd> ands;
    ands.reserve(std::min<size_t>(A, 2048));

    if (isBin) {
        for (uint32_t i = 0; i < I; ++i) inputLits[i] = 2u * (i + 1u);
        for (uint32_t i = 0; i < L; ++i) {
            std::string ln;
            if (!readLineStr(ln)) return false;
            unsigned int nxt = 0;
            if (sscanf_s(ln.c_str(), "%u", &nxt) < 1) return false;
            latches[i] = { 2u * (I + i + 1u), nxt };
        }
        for (uint32_t i = 0; i < O; ++i) {
            std::string ln;
            if (!readLineStr(ln)) return false;
            unsigned int ol = 0;
            if (sscanf_s(ln.c_str(), "%u", &ol) < 1) return false;
            outputs[i] = ol;
        }
        // Binary LEB128 delta-encoded AND gates
        auto readVarint = [&](uint32_t& outV) -> bool {
            outV = 0;
            int shift = 0;
            while (pos < size && shift <= 28) {
                uint8_t b = data[pos++];
                outV |= static_cast<uint32_t>(b & 0x7Fu) << shift;
                if ((b & 0x80u) == 0) return true;
                shift += 7;
            }
            return false;
        };
        for (uint32_t i = 0; i < A; ++i) {
            uint32_t d0 = 0, d1 = 0;
            if (!readVarint(d0) || !readVarint(d1)) return false; // Truncated binary AIGER!
            uint32_t lhs = 2u * (I + L + i + 1u);
            if (d0 == 0 || d0 > lhs) return false;
            uint32_t rhs0 = lhs - d0;
            if (d1 > rhs0) return false;
            uint32_t rhs1 = rhs0 - d1;
            if (ands.size() < 1024) ands.push_back({ lhs, rhs0, rhs1 });
        }
    } else {
        for (uint32_t i = 0; i < I; ++i) {
            std::string ln;
            if (!readLineStr(ln)) return false;
            unsigned int il = 0;
            if (sscanf_s(ln.c_str(), "%u", &il) < 1) return false;
            inputLits[i] = il;
        }
        for (uint32_t i = 0; i < L; ++i) {
            std::string ln;
            if (!readLineStr(ln)) return false;
            unsigned int ll = 0, nxt = 0;
            if (sscanf_s(ln.c_str(), "%u %u", &ll, &nxt) < 2) return false;
            latches[i] = { ll, nxt };
        }
        for (uint32_t i = 0; i < O; ++i) {
            std::string ln;
            if (!readLineStr(ln)) return false;
            unsigned int ol = 0;
            if (sscanf_s(ln.c_str(), "%u", &ol) < 1) return false;
            outputs[i] = ol;
        }
        for (uint32_t i = 0; i < A; ++i) {
            std::string ln;
            if (!readLineStr(ln)) return false;
            unsigned int lhs = 0, r0 = 0, r1 = 0;
            if (sscanf_s(ln.c_str(), "%u %u %u", &lhs, &r0, &r1) < 3) return false;
            if (ands.size() < 1024) ands.push_back({ lhs, r0, r1 });
        }
    }

    // Optional AIGER symbol table: i<k> <name>, l<k> <name>, o<k> <name> until 'c'
    std::unordered_map<uint32_t, std::string> inSym, latchSym, outSym;
    while (pos < size) {
        std::string ln;
        if (!readLineStr(ln) || ln.empty()) break;
        if (ln == "c") break;
        if ((ln[0] == 'i' || ln[0] == 'l' || ln[0] == 'o') && ln.size() >= 3) {
            size_t sp = ln.find(' ');
            if (sp != std::string::npos && sp + 1 < ln.size()) {
                uint32_t idx = static_cast<uint32_t>(std::strtoul(ln.c_str() + 1, nullptr, 10));
                std::string nm = ln.substr(sp + 1);
                if (ln[0] == 'i') inSym[idx] = nm;
                else if (ln[0] == 'l') latchSym[idx] = nm;
                else outSym[idx] = nm;
            }
        }
    }

    auto varNet = [](uint32_t lit) -> std::string {
        uint32_t v = lit >> 1;
        return (v == 0) ? "CONST0" : ("v" + std::to_string(v));
    };

    std::vector<GateNode> dagNodes;
    for (uint32_t i = 0; i < I && dagNodes.size() < 512; ++i) {
        GateNode gn;
        gn.instName = inSym.count(i) ? inSym[i] : ("in" + std::to_string(i));
        gn.cellType = "INPUT"; gn.isPrimaryIn = true;
        gn.pins.push_back({ "O", varNet(inputLits[i]), true, false });
        dagNodes.push_back(std::move(gn));
    }
    for (uint32_t i = 0; i < L && dagNodes.size() < 512; ++i) {
        GateNode gn;
        gn.instName = latchSym.count(i) ? latchSym[i] : ("L" + std::to_string(i));
        gn.cellType = "LATCH"; gn.isSeq = true;
        gn.pins.push_back({ "D", varNet(latches[i].second), false, (latches[i].second & 1u) != 0 });
        gn.pins.push_back({ "Q", varNet(latches[i].first), true, false });
        dagNodes.push_back(std::move(gn));
    }
    for (size_t i = 0; i < ands.size() && dagNodes.size() < 512; ++i) {
        GateNode gn;
        gn.instName = "A" + std::to_string(i + 1);
        gn.cellType = "AND2";
        gn.pins.push_back({ "A", varNet(ands[i].rhs0), false, (ands[i].rhs0 & 1u) != 0 });
        gn.pins.push_back({ "B", varNet(ands[i].rhs1), false, (ands[i].rhs1 & 1u) != 0 });
        gn.pins.push_back({ "Y", varNet(ands[i].lhs), true, false });
        dagNodes.push_back(std::move(gn));
    }
    for (uint32_t i = 0; i < O && dagNodes.size() < 512; ++i) {
        GateNode gn;
        gn.instName = outSym.count(i) ? outSym[i] : ("out" + std::to_string(i));
        gn.cellType = "OUTPUT"; gn.isPrimaryOut = true;
        gn.pins.push_back({ "I", varNet(outputs[i]), false, (outputs[i] & 1u) != 0 });
        dagNodes.push_back(std::move(gn));
    }

    wchar_t hdrTitle[160] = {};
    swprintf_s(hdrTitle, L"AIGER %ls Graph (M=%u, I=%u, L=%u, O=%u, A=%u)",
               isBin ? L"Binary (.aig)" : L"ASCII (.aag)", M, I, L, O, A);
    return RenderGateDagToBoard(hdrTitle, dagNodes, out);
}

// 4. Synopsys Liberty Standard-Cell Timing Library (.lib)
inline bool IsLikelyLibertyLibBytes(const uint8_t* data, size_t size) {
    if (!data || size < 24) return false;
    if (size >= 8 && std::memcmp(data, "!<arch>\n", 8) == 0) return false; // COFF archive .lib!
    size_t scan = std::min<size_t>(size, 8192);
    for (size_t i = 0; i < scan; ++i) {
        if (data[i] == 0) return false;
    }
    std::string_view sv(reinterpret_cast<const char*>(data), scan);
    return (sv.find("library (") != std::string_view::npos || sv.find("library(") != std::string_view::npos) &&
           sv.find('{') != std::string_view::npos;
}

inline bool ParseLibertyLibBoard(std::string_view sv, ParsedEdaBoard& out) {
    if (!IsLikelyLibertyLibBytes(reinterpret_cast<const uint8_t*>(sv.data()), sv.size())) return false;
    for (unsigned char c : sv) {
        if (c == 0) return false;
    }

    // Strip /* ... */ comments and validate balanced { ... } braces
    std::string clean;
    clean.reserve(sv.size());
    for (size_t i = 0; i < sv.size(); ++i) {
        if (sv[i] == '/' && i + 1 < sv.size() && sv[i + 1] == '*') {
            i += 2;
            while (i + 1 < sv.size() && !(sv[i] == '*' && sv[i + 1] == '/')) ++i;
            ++i;
            clean.push_back(' ');
        } else if (sv[i] == '/' && i + 1 < sv.size() && sv[i + 1] == '/') {
            while (i < sv.size() && sv[i] != '\n') ++i;
            clean.push_back('\n');
        } else {
            clean.push_back(sv[i]);
        }
    }

    int braceDepth = 0;
    bool inQ = false, sawBrace = false;
    for (size_t i = 0; i < clean.size(); ++i) {
        char c = clean[i];
        if (c == '\\' && i + 1 < clean.size()) { ++i; continue; }
        if (c == '"') { inQ = !inQ; continue; }
        if (!inQ) {
            if (c == '{') { ++braceDepth; sawBrace = true; }
            else if (c == '}') { if (--braceDepth < 0) return false; }
        }
    }
    if (!sawBrace || inQ || braceDepth != 0) return false;

    struct LibPin {
        std::string name;
        std::string dir = "input";
        std::string func;
        std::vector<std::pair<std::string, double>> timingFrom; // (related_pin, midDelayNs)
    };
    struct LibCell {
        std::string name;
        double area = 1.0;
        std::vector<LibPin> pins;
    };

    std::string libName;
    std::vector<LibCell> cells;

    // Recursive group walker: <groupType> ( <arg> ) { ... } and <attr> : <val> ;
    size_t p = 0;
    auto skipWs = [&]() {
        while (p < clean.size() && (static_cast<unsigned char>(clean[p]) <= 0x20u || clean[p] == ';')) ++p;
    };
    auto readIdentOrStr = [&]() -> std::string {
        skipWs();
        if (p >= clean.size()) return {};
        if (clean[p] == '"') {
            ++p;
            std::string s;
            while (p < clean.size() && clean[p] != '"') {
                if (clean[p] == '\\' && p + 1 < clean.size()) ++p;
                s.push_back(clean[p++]);
            }
            if (p < clean.size()) ++p;
            return s;
        }
        size_t s = p;
        while (p < clean.size() && static_cast<unsigned char>(clean[p]) > 0x20u &&
               clean[p] != '(' && clean[p] != ')' && clean[p] != '{' && clean[p] != '}' && clean[p] != ':' && clean[p] != ';') ++p;
        return clean.substr(s, p - s);
    };
    auto readParenArg = [&]() -> std::string {
        skipWs();
        if (p >= clean.size() || clean[p] != '(') return {};
        int d = 1; ++p;
        size_t s = p;
        while (p < clean.size() && d > 0) {
            if (clean[p] == '(') ++d;
            else if (clean[p] == ')') --d;
            ++p;
        }
        std::string raw = clean.substr(s, (p - 1) - s);
        // Strip quotes/spaces
        std::string outS;
        for (char c : raw) if (c != '"' && c != ' ' && c != '\t' && c != '\r' && c != '\n') outS.push_back(c);
        return outS;
    };
    auto skipBraceBlock = [&]() {
        skipWs();
        if (p >= clean.size() || clean[p] != '{') return;
        int d = 1; ++p;
        bool q = false;
        while (p < clean.size() && d > 0) {
            if (clean[p] == '\\' && p + 1 < clean.size()) { p += 2; continue; }
            if (clean[p] == '"') q = !q;
            else if (!q && clean[p] == '{') ++d;
            else if (!q && clean[p] == '}') --d;
            ++p;
        }
    };

    // Find outer library(...) { ... }
    while (p < clean.size()) {
        std::string kw = readIdentOrStr();
        if (kw.empty()) break;
        if (kw == "library") {
            libName = readParenArg();
            skipWs();
            if (p < clean.size() && clean[p] == '{') ++p;
            break;
        }
    }
    if (p >= clean.size()) return false;

    while (p < clean.size() && cells.size() < 128) {
        skipWs();
        if (p >= clean.size() || clean[p] == '}') break;
        std::string kw = readIdentOrStr();
        if (kw.empty()) { ++p; continue; }
        skipWs();
        if (p < clean.size() && clean[p] == ':') {
            ++p;
            readIdentOrStr(); // simple attribute value
            continue;
        }
        if (p < clean.size() && clean[p] == '(') {
            std::string arg = readParenArg();
            skipWs();
            if (p < clean.size() && clean[p] == '{') {
                if (kw == "cell") {
                    ++p; // enter cell {
                    LibCell cell;
                    cell.name = arg;
                    while (p < clean.size()) {
                        skipWs();
                        if (p >= clean.size() || clean[p] == '}') { ++p; break; }
                        std::string ckw = readIdentOrStr();
                        skipWs();
                        if (p < clean.size() && clean[p] == ':') {
                            ++p;
                            std::string cv = readIdentOrStr();
                            if (ckw == "area") cell.area = std::strtod(cv.c_str(), nullptr);
                        } else if (p < clean.size() && clean[p] == '(') {
                            std::string parg = readParenArg();
                            skipWs();
                            if (p < clean.size() && clean[p] == '{') {
                                if (ckw == "pin") {
                                    ++p;
                                    LibPin pin;
                                    pin.name = parg;
                                    while (p < clean.size()) {
                                        skipWs();
                                        if (p >= clean.size() || clean[p] == '}') { ++p; break; }
                                        std::string pkw = readIdentOrStr();
                                        skipWs();
                                        if (p < clean.size() && clean[p] == ':') {
                                            ++p;
                                            std::string pv = readIdentOrStr();
                                            if (pkw == "direction") pin.dir = DetailGateTdms::LowerAsciiCopy(pv);
                                            else if (pkw == "function") pin.func = pv;
                                        } else if (p < clean.size() && clean[p] == '(') {
                                            readParenArg();
                                            skipWs();
                                            if (p < clean.size() && clean[p] == '{') {
                                                if (pkw == "timing") {
                                                    size_t tStart = p;
                                                    skipBraceBlock();
                                                    std::string_view tblk(clean.data() + tStart, p - tStart);
                                                    // Extract related_pin and first numeric value in cell_rise / cell_fall values("...")
                                                    std::string relPin;
                                                    size_t rp = tblk.find("related_pin");
                                                    if (rp != std::string_view::npos) {
                                                        size_t q1 = tblk.find('"', rp);
                                                        size_t q2 = (q1 != std::string_view::npos) ? tblk.find('"', q1 + 1) : std::string_view::npos;
                                                        if (q1 != std::string_view::npos && q2 != std::string_view::npos) {
                                                            relPin = std::string(tblk.substr(q1 + 1, q2 - q1 - 1));
                                                        }
                                                    }
                                                    double dlyVal = 0.05;
                                                    size_t vp = tblk.find("values");
                                                    if (vp != std::string_view::npos) {
                                                        size_t q1 = tblk.find('"', vp);
                                                        if (q1 != std::string_view::npos) {
                                                            dlyVal = std::strtod(std::string(tblk.substr(q1 + 1, 32)).c_str(), nullptr);
                                                        }
                                                    }
                                                    if (!relPin.empty() && pin.timingFrom.size() < 8) {
                                                        pin.timingFrom.push_back({ relPin, dlyVal });
                                                    }
                                                } else {
                                                    skipBraceBlock();
                                                }
                                            }
                                        } else {
                                            ++p;
                                        }
                                    }
                                    cell.pins.push_back(std::move(pin));
                                } else {
                                    skipBraceBlock();
                                }
                            }
                        } else {
                            ++p;
                        }
                    }
                    if (!cell.pins.empty()) cells.push_back(std::move(cell));
                } else {
                    skipBraceBlock();
                }
            }
        }
    }

    if (cells.empty()) return false;

    out.kind = EdaDocKind::KiCadSch;
    out.formatTitle = L"Synopsys Liberty Timing Library (.lib — " + PathUtils::Utf8ToWide(libName.empty() ? "stdcells" : libName) +
                      L", " + std::to_wstring(cells.size()) + L" cells)";

    const size_t cols = std::max<size_t>(1, static_cast<size_t>(std::ceil(std::sqrt(static_cast<double>(cells.size())))));
    float curX = 0.0f, curY = 0.0f;
    size_t totalArcs = 0;

    for (size_t ci = 0; ci < cells.size(); ++ci) {
        const auto& cell = cells[ci];
        if (ci > 0 && (ci % cols) == 0) {
            curX = 0.0f;
            curY -= 22.0f;
        }
        const float boxW = 16.0f, boxH = 12.0f;
        float cx = curX + boxW * 0.5f, cy = curY;

        out.traces.push_back({ curX,        cy - boxH * 0.5f, curX + boxW, cy - boxH * 0.5f, 0.28f, EdaLayerId::SchSymbol, 0 });
        out.traces.push_back({ curX + boxW, cy - boxH * 0.5f, curX + boxW, cy + boxH * 0.5f, 0.28f, EdaLayerId::SchSymbol, 0 });
        out.traces.push_back({ curX + boxW, cy + boxH * 0.5f, curX,        cy + boxH * 0.5f, 0.28f, EdaLayerId::SchSymbol, 0 });
        out.traces.push_back({ curX,        cy + boxH * 0.5f, curX,        cy - boxH * 0.5f, 0.28f, EdaLayerId::SchSymbol, 0 });

        wchar_t hdr[128] = {};
        swprintf_s(hdr, L"%hs (area=%.2f)", cell.name.c_str(), cell.area);
        out.labels.push_back({ curX, cy + boxH * 0.5f + 0.9f, 0.95f, EdaLayerId::SchLabel, hdr });

        std::unordered_map<std::string, std::pair<float, float>> pinCoords;
        std::vector<const LibPin*> ins, outs;
        for (const auto& lp : cell.pins) {
            if (lp.dir == "output") outs.push_back(&lp);
            else ins.push_back(&lp);
        }
        for (size_t i = 0; i < ins.size(); ++i) {
            float py = cy + boxH * 0.35f - static_cast<float>(i) * (boxH * 0.7f / std::max<size_t>(1, ins.size()));
            out.traces.push_back({ curX - 2.4f, py, curX, py, 0.24f, EdaLayerId::SchWire, 0 });
            out.pads.push_back({ curX - 2.4f, py, 0.65f, 0.65f, 0, 0, 0, EdaLayerId::SchSymbol, PathUtils::Utf8ToWide(ins[i]->name), L"" });
            out.labels.push_back({ curX + 0.5f, py - 0.3f, 0.8f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(ins[i]->name) });
            pinCoords[ins[i]->name] = { curX, py };
        }
        for (size_t i = 0; i < outs.size(); ++i) {
            float py = cy + boxH * 0.25f - static_cast<float>(i) * 3.0f;
            out.traces.push_back({ curX + boxW, py, curX + boxW + 2.4f, py, 0.24f, EdaLayerId::SchWire, 0 });
            out.pads.push_back({ curX + boxW + 2.4f, py, 0.65f, 0.65f, 0, 0, 0, EdaLayerId::SchSymbol, PathUtils::Utf8ToWide(outs[i]->name), PathUtils::Utf8ToWide(outs[i]->func) });
            std::string fnLbl = outs[i]->name + (outs[i]->func.empty() ? "" : ("=" + outs[i]->func));
            out.labels.push_back({ curX + 3.5f, py - 0.3f, 0.78f, EdaLayerId::TopSilk, PathUtils::Utf8ToWide(fnLbl) });
            pinCoords[outs[i]->name] = { curX + boxW, py };

            for (const auto& tf : outs[i]->timingFrom) {
                auto itSrc = pinCoords.find(tf.first);
                if (itSrc != pinCoords.end()) {
                    out.traces.push_back({ itSrc->second.first + 0.4f, itSrc->second.second,
                                           curX + boxW - 0.4f, py, 0.16f, EdaLayerId::TopCopper, static_cast<int>(totalArcs + 1) });
                    ++totalArcs;
                }
            }
        }

        EdaComponent3D comp;
        comp.refDes = PathUtils::Utf8ToWide(cell.name);
        comp.footprint = L"LIBERTY_CELL";
        comp.x = cx; comp.y = cy;
        comp.widthMm = boxW; comp.lengthMm = boxH; comp.heightMm = 1.0f;
        comp.pinCount = static_cast<int>(cell.pins.size());
        comp.category = EdaPartCategory::IC_Package;
        out.components.push_back(std::move(comp));

        curX += 26.0f;
    }

    out.netCount = std::max<size_t>(1, totalArcs > 0 ? totalArcs : cells.size());
    return true;
}

// 5. IEEE 1497 Standard Delay Format (.sdf — (DELAYFILE ...))
inline bool IsLikelyIeeeSdfBytes(const uint8_t* data, size_t size) {
    if (!data || size < 16) return false;
    size_t scan = std::min<size_t>(size, 4096);
    for (size_t i = 0; i < scan; ++i) {
        if (data[i] == 0) return false;
    }
    std::string_view sv(reinterpret_cast<const char*>(data), scan);
    while (!sv.empty() && static_cast<unsigned char>(sv.front()) <= 0x20u) sv.remove_prefix(1);
    return sv.rfind("(DELAYFILE", 0) == 0 || sv.find("(DELAYFILE") != std::string_view::npos;
}

inline bool ParseIeee1497SdfBoard(std::string_view sv, ParsedEdaBoard& out) {
    if (!IsLikelyIeeeSdfBytes(reinterpret_cast<const uint8_t*>(sv.data()), sv.size())) return false;

    std::vector<DetailSpecctra::SNode> roots;
    if (!DetailSpecctra::ParseSExprTree(sv, roots) || roots.empty()) return false;

    const DetailSpecctra::SNode* dfRoot = nullptr;
    for (const auto& r : roots) {
        if (DetailGateTdms::LowerAsciiCopy(DetailSpecctra::HeadKey(r)) == "delayfile") { dfRoot = &r; break; }
    }
    if (!dfRoot) return false;

    std::string designName;
    struct SdfArc { std::string fromPin, toPin; double dly = 0.0; };
    struct SdfCell {
        std::string cellType, instName;
        std::vector<SdfArc> iopaths;
        size_t timingChecks = 0;
    };
    std::vector<SdfCell> cells;
    std::vector<SdfArc> interconnects;

    auto extractTripleTyp = [](const DetailSpecctra::SNode& tripleList) -> double {
        if (!tripleList.isList || tripleList.list.empty()) return 0.0;
        const std::string& raw = tripleList.list[0].atom; // e.g. "0.12:0.15:0.19" or "0.15"
        size_t c1 = raw.find(':');
        if (c1 != std::string::npos) {
            size_t c2 = raw.find(':', c1 + 1);
            std::string mid = (c2 != std::string::npos) ? raw.substr(c1 + 1, c2 - c1 - 1) : raw.substr(c1 + 1);
            if (!mid.empty()) return std::strtod(mid.c_str(), nullptr);
        }
        return std::strtod(raw.c_str(), nullptr);
    };

    for (size_t i = 1; i < dfRoot->list.size(); ++i) {
        const auto& sec = dfRoot->list[i];
        std::string sk = DetailGateTdms::LowerAsciiCopy(DetailSpecctra::HeadKey(sec));
        if (sk == "design" && sec.list.size() >= 2) {
            designName = sec.list[1].atom;
        } else if (sk == "cell") {
            SdfCell cell;
            for (size_t ci = 1; ci < sec.list.size(); ++ci) {
                const auto& csub = sec.list[ci];
                std::string ck = DetailGateTdms::LowerAsciiCopy(DetailSpecctra::HeadKey(csub));
                if (ck == "celltype" && csub.list.size() >= 2) cell.cellType = csub.list[1].atom;
                else if (ck == "instance" && csub.list.size() >= 2) cell.instName = csub.list[1].atom;
                else if (ck == "timingcheck") cell.timingChecks += csub.list.size() - 1;
                else if (ck == "delay") {
                    for (size_t di = 1; di < csub.list.size(); ++di) {
                        const auto& absNode = csub.list[di];
                        std::string ak = DetailGateTdms::LowerAsciiCopy(DetailSpecctra::HeadKey(absNode));
                        if (ak == "absolute" || ak == "increment") {
                            for (size_t pi = 1; pi < absNode.list.size(); ++pi) {
                                const auto& pNode = absNode.list[pi];
                                std::string pk = DetailGateTdms::LowerAsciiCopy(DetailSpecctra::HeadKey(pNode));
                                if (pk == "iopath" && pNode.list.size() >= 4) {
                                    std::string inP = pNode.list[1].isList ? DetailGateTdms::EdifIdName(pNode.list[1]) : pNode.list[1].atom;
                                    std::string outP = pNode.list[2].isList ? DetailGateTdms::EdifIdName(pNode.list[2]) : pNode.list[2].atom;
                                    double d = extractTripleTyp(pNode.list[3]);
                                    cell.iopaths.push_back({ inP, outP, d });
                                } else if (pk == "interconnect" && pNode.list.size() >= 4) {
                                    interconnects.push_back({ pNode.list[1].atom, pNode.list[2].atom, extractTripleTyp(pNode.list[3]) });
                                }
                            }
                        }
                    }
                }
            }
            if (!cell.cellType.empty() || !cell.iopaths.empty()) cells.push_back(std::move(cell));
        }
    }

    if (cells.empty() && interconnects.empty()) return false;

    out.kind = EdaDocKind::KiCadSch;
    out.formatTitle = L"IEEE 1497 Standard Delay Format (.sdf";
    if (!designName.empty()) out.formatTitle += L" — " + PathUtils::Utf8ToWide(designName);
    out.formatTitle += L")";

    float curX = 0.0f;
    for (size_t i = 0; i < cells.size() && i < 128; ++i) {
        const auto& c = cells[i];
        if (c.iopaths.empty() && c.instName.empty() && !interconnects.empty()) continue;
        float boxW = 14.0f, boxH = std::max(7.0f, static_cast<float>(c.iopaths.size()) * 3.0f + 3.0f);
        float cx = curX + boxW * 0.5f, cy = 0.0f;
        out.traces.push_back({ curX,        cy - boxH * 0.5f, curX + boxW, cy - boxH * 0.5f, 0.28f, EdaLayerId::SchSymbol, 0 });
        out.traces.push_back({ curX + boxW, cy - boxH * 0.5f, curX + boxW, cy + boxH * 0.5f, 0.28f, EdaLayerId::SchSymbol, 0 });
        out.traces.push_back({ curX + boxW, cy + boxH * 0.5f, curX,        cy + boxH * 0.5f, 0.28f, EdaLayerId::SchSymbol, 0 });
        out.traces.push_back({ curX,        cy + boxH * 0.5f, curX,        cy - boxH * 0.5f, 0.28f, EdaLayerId::SchSymbol, 0 });

        std::string instTitle = (c.instName.empty() ? ("U" + std::to_string(i + 1)) : c.instName) + " (" + c.cellType + ")";
        out.labels.push_back({ curX, cy + boxH * 0.5f + 0.9f, 0.95f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(instTitle) });

        for (size_t k = 0; k < c.iopaths.size(); ++k) {
            float py = cy + boxH * 0.3f - static_cast<float>(k) * 2.8f;
            out.traces.push_back({ curX - 2.0f, py, curX + boxW + 2.0f, py, 0.24f, EdaLayerId::TopCopper, static_cast<int>(k + 1) });
            out.pads.push_back({ curX - 2.0f, py, 0.6f, 0.6f, 0, 0, 0, EdaLayerId::SchSymbol, PathUtils::Utf8ToWide(c.iopaths[k].fromPin), L"" });
            out.pads.push_back({ curX + boxW + 2.0f, py, 0.6f, 0.6f, 0, 0, 0, EdaLayerId::SchSymbol, PathUtils::Utf8ToWide(c.iopaths[k].toPin), L"" });
            wchar_t arcLbl[96] = {};
            swprintf_s(arcLbl, L"%hs->%hs: %.3f ns", c.iopaths[k].fromPin.c_str(), c.iopaths[k].toPin.c_str(), c.iopaths[k].dly);
            out.labels.push_back({ curX + 0.8f, py + 0.3f, 0.78f, EdaLayerId::TopSilk, arcLbl });
        }

        EdaComponent3D comp;
        comp.refDes = PathUtils::Utf8ToWide(c.instName.empty() ? c.cellType : c.instName);
        comp.value = PathUtils::Utf8ToWide(c.cellType);
        comp.footprint = L"SDF_CELL";
        comp.x = cx; comp.y = cy;
        comp.widthMm = boxW; comp.lengthMm = boxH;
        comp.pinCount = std::max(2, static_cast<int>(c.iopaths.size() * 2));
        out.components.push_back(std::move(comp));
        curX += 22.0f;
    }

    // Also render INTERCONNECT delay bars below cells
    float iy = -10.0f;
    for (size_t i = 0; i < interconnects.size() && i < 32; ++i) {
        float w = std::clamp(static_cast<float>(interconnects[i].dly) * 30.0f, 3.0f, 40.0f);
        out.traces.push_back({ 0.0f, iy, w, iy, 0.32f, EdaLayerId::SchWire, static_cast<int>(i + 1) });
        wchar_t icLbl[160] = {};
        swprintf_s(icLbl, L"NET %hs -> %hs (%.3f ns)", interconnects[i].fromPin.c_str(), interconnects[i].toPin.c_str(), interconnects[i].dly);
        out.labels.push_back({ w + 1.0f, iy - 0.3f, 0.82f, EdaLayerId::SchLabel, icLbl });
        iy -= 2.8f;
    }

    out.netCount = std::max<size_t>(1, interconnects.size() + cells.size());
    return !out.traces.empty();
}

// 6. National Instruments TDMS Binary Waveform (.tdms — "TDSm" v1.0/v2.0 Lead-In + Meta + Raw Channels)
inline bool ParseNiTdmsBoard(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    if (!data || size < 28 || size > 128 * 1024 * 1024) return false;
    if (std::memcmp(data, "TDSm", 4) != 0) return false;

    struct TdmsChannel {
        std::string path;
        std::string displayName;
        std::string unit;
        uint32_t dataType = 0;
        uint64_t numValues = 0;
        double wfIncrement = 1e-4;
        double wfStartOffset = 0.0;
        bool hasRawIndex = false;
        std::vector<double> samples;
    };
    std::map<std::string, TdmsChannel> chanMap;
    std::vector<std::string> activeOrder;

    size_t segPos = 0;
    size_t validSegs = 0;

    while (segPos + 28 <= size && validSegs < 4096) {
        if (std::memcmp(data + segPos, "TDSm", 4) != 0) {
            if (validSegs == 0) return false;
            break;
        }
        uint32_t toc = static_cast<uint32_t>(data[segPos + 4]) |
                       (static_cast<uint32_t>(data[segPos + 5]) << 8) |
                       (static_cast<uint32_t>(data[segPos + 6]) << 16) |
                       (static_cast<uint32_t>(data[segPos + 7]) << 24);
        const bool be = (toc & (1u << 6)) != 0;
        auto rdU32 = [&](const uint8_t* p) -> uint32_t {
            return be ? ((static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) | p[3])
                      : (static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24));
        };
        auto rdU64 = [&](const uint8_t* p) -> uint64_t {
            return be ? ((static_cast<uint64_t>(rdU32(p)) << 32) | rdU32(p + 4))
                      : (static_cast<uint64_t>(rdU32(p)) | (static_cast<uint64_t>(rdU32(p + 4)) << 32));
        };
        auto rdF64 = [&](const uint8_t* p) -> double {
            double d = 0.0;
            if (be) {
                uint8_t r[8] = { p[7], p[6], p[5], p[4], p[3], p[2], p[1], p[0] };
                std::memcpy(&d, r, 8);
            } else {
                std::memcpy(&d, p, 8);
            }
            return d;
        };
        auto rdF32 = [&](const uint8_t* p) -> double {
            float f = 0.0f;
            if (be) {
                uint8_t r[4] = { p[3], p[2], p[1], p[0] };
                std::memcpy(&f, r, 4);
            } else {
                std::memcpy(&f, p, 4);
            }
            return static_cast<double>(f);
        };

        uint32_t ver = rdU32(data + segPos + 8);
        if (ver != 4712u && ver != 4713u) return false;
        uint64_t nextSegOff = rdU64(data + segPos + 12);
        uint64_t rawDataOff = rdU64(data + segPos + 20);
        if (nextSegOff == 0xFFFFFFFFFFFFFFFFULL) return false; // Incomplete crash segment
        if (rawDataOff > nextSegOff || segPos + 28ULL + nextSegOff > size) return false;

        const size_t metaStart = segPos + 28;
        const size_t rawStart  = metaStart + static_cast<size_t>(rawDataOff);
        const size_t segEnd    = metaStart + static_cast<size_t>(nextSegOff);

        const bool hasMeta    = (toc & (1u << 1)) != 0;
        const bool newObjList = (toc & (1u << 2)) != 0;
        const bool hasRaw     = (toc & (1u << 3)) != 0;
        const bool interleave = (toc & (1u << 5)) != 0;

        if (newObjList) activeOrder.clear();

        if (hasMeta) {
            size_t mp = metaStart;
            if (mp + 4 > rawStart) return false;
            uint32_t numObjs = rdU32(data + mp);
            mp += 4;
            if (numObjs > 4096) return false;

            for (uint32_t oi = 0; oi < numObjs; ++oi) {
                if (mp + 8 > rawStart) return false;
                uint32_t pathLen = rdU32(data + mp);
                mp += 4;
                if (pathLen > rawStart - mp) return false;
                std::string objPath(reinterpret_cast<const char*>(data + mp), pathLen);
                mp += pathLen;

                TdmsChannel& ch = chanMap[objPath];
                if (ch.path.empty()) {
                    ch.path = objPath;
                    // Clean "/'Group'/'Chan'" -> "Group.Chan"
                    std::string dn;
                    for (char c : objPath) {
                        if (c == '\'') continue;
                        if (c == '/' && !dn.empty()) dn.push_back('.');
                        else if (c != '/') dn.push_back(c);
                    }
                    ch.displayName = dn.empty() ? objPath : dn;
                }

                if (mp + 4 > rawStart) return false;
                uint32_t rawIdx = rdU32(data + mp);
                mp += 4;
                if (rawIdx == 0xFFFFFFFFu) {
                    // No raw data in this segment for this object
                } else if (rawIdx == 0x00000000u) {
                    // Reuses previous raw data index
                    if (ch.hasRawIndex && std::find(activeOrder.begin(), activeOrder.end(), objPath) == activeOrder.end()) {
                        activeOrder.push_back(objPath);
                    }
                } else {
                    if (rawIdx < 20u || mp + 16 > rawStart) return false;
                    ch.dataType = rdU32(data + mp);
                    uint32_t arrDim = rdU32(data + mp + 4);
                    ch.numValues = rdU64(data + mp + 8);
                    mp += 16;
                    if (arrDim != 1u) return false;
                    if (ch.dataType == 0x20u) { // tdsTypeString has extra uint64 totalSizeBytes
                        if (mp + 8 > rawStart) return false;
                        mp += 8;
                    }
                    ch.hasRawIndex = true;
                    if (std::find(activeOrder.begin(), activeOrder.end(), objPath) == activeOrder.end()) {
                        activeOrder.push_back(objPath);
                    }
                }

                // Properties ([numProps:u32])
                if (mp + 4 > rawStart) return false;
                uint32_t numProps = rdU32(data + mp);
                mp += 4;
                if (numProps > 4096) return false;
                for (uint32_t pi = 0; pi < numProps; ++pi) {
                    if (mp + 8 > rawStart) return false;
                    uint32_t pNameLen = rdU32(data + mp);
                    mp += 4;
                    if (pNameLen > rawStart - mp) return false;
                    std::string pName(reinterpret_cast<const char*>(data + mp), pNameLen);
                    mp += pNameLen;
                    if (mp + 4 > rawStart) return false;
                    uint32_t pType = rdU32(data + mp);
                    mp += 4;

                    if (pType == 0x20u) { // String
                        if (mp + 4 > rawStart) return false;
                        uint32_t sl = rdU32(data + mp);
                        mp += 4;
                        if (sl > rawStart - mp) return false;
                        std::string svProp(reinterpret_cast<const char*>(data + mp), sl);
                        mp += sl;
                        if (pName == "unit_string") ch.unit = svProp;
                    } else if (pType == 10u || pType == 0x1Au) { // Double
                        if (mp + 8 > rawStart) return false;
                        double dv = rdF64(data + mp);
                        mp += 8;
                        if (pName == "wf_increment" && dv > 0.0) ch.wfIncrement = dv;
                        else if (pName == "wf_start_offset") ch.wfStartOffset = dv;
                    } else if (pType == 9u || pType == 0x19u) { // Float
                        if (mp + 4 > rawStart) return false;
                        double fv = rdF32(data + mp);
                        mp += 4;
                        if (pName == "wf_increment" && fv > 0.0) ch.wfIncrement = fv;
                    } else if (pType == 0x44u) { // Timestamp (16 bytes: u64 fractions + i64 seconds)
                        if (mp + 16 > rawStart) return false;
                        mp += 16;
                    } else if (pType == 1u || pType == 5u || pType == 0x21u) {
                        if (mp + 1 > rawStart) return false;
                        mp += 1;
                    } else if (pType == 2u || pType == 6u) {
                        if (mp + 2 > rawStart) return false;
                        mp += 2;
                    } else if (pType == 3u || pType == 7u) {
                        if (mp + 4 > rawStart) return false;
                        mp += 4;
                    } else if (pType == 4u || pType == 8u) {
                        if (mp + 8 > rawStart) return false;
                        mp += 8;
                    } else {
                        break;
                    }
                }
            }
        }

        auto elemBytesOf = [](uint32_t dt) -> size_t {
            switch (dt) {
            case 1: case 5: case 0x21: return 1;
            case 2: case 6: return 2;
            case 3: case 7: case 9: case 0x19: return 4;
            case 4: case 8: case 10: case 0x1A: return 8;
            default: return 0;
            }
        };
        auto decodeSample = [&](uint32_t dt, const uint8_t* p) -> double {
            switch (dt) {
            case 1: return static_cast<double>(static_cast<int8_t>(p[0]));
            case 5: case 0x21: return static_cast<double>(p[0]);
            case 2: return static_cast<double>(static_cast<int16_t>(be ? ((p[0] << 8) | p[1]) : (p[0] | (p[1] << 8))));
            case 6: return static_cast<double>(static_cast<uint16_t>(be ? ((p[0] << 8) | p[1]) : (p[0] | (p[1] << 8))));
            case 3: return static_cast<double>(static_cast<int32_t>(rdU32(p)));
            case 7: return static_cast<double>(rdU32(p));
            case 4: return static_cast<double>(static_cast<int64_t>(rdU64(p)));
            case 8: return static_cast<double>(rdU64(p));
            case 9: case 0x19: return rdF32(p);
            case 10: case 0x1A: return rdF64(p);
            default: return 0.0;
            }
        };

        if (hasRaw && rawStart < segEnd && !activeOrder.empty()) {
            uint64_t chunkBytes = 0;
            for (const auto& op : activeOrder) {
                const auto& ch = chanMap[op];
                size_t eb = elemBytesOf(ch.dataType);
                if (eb == 0) return false;
                chunkBytes += ch.numValues * eb;
            }
            if (chunkBytes > 0) {
                size_t rp = rawStart;
                while (rp + chunkBytes <= segEnd) {
                    if (!interleave) {
                        for (const auto& op : activeOrder) {
                            auto& ch = chanMap[op];
                            size_t eb = elemBytesOf(ch.dataType);
                            for (uint64_t i = 0; i < ch.numValues; ++i) {
                                if (ch.samples.size() < 65536) {
                                    ch.samples.push_back(decodeSample(ch.dataType, data + rp));
                                }
                                rp += eb;
                            }
                        }
                    } else {
                        uint64_t nRows = chanMap[activeOrder[0]].numValues;
                        size_t rowStride = 0;
                        for (const auto& op : activeOrder) rowStride += elemBytesOf(chanMap[op].dataType);
                        if (rowStride * nRows != chunkBytes) break;
                        for (uint64_t r = 0; r < nRows; ++r) {
                            for (const auto& op : activeOrder) {
                                auto& ch = chanMap[op];
                                size_t eb = elemBytesOf(ch.dataType);
                                if (ch.samples.size() < 65536) {
                                    ch.samples.push_back(decodeSample(ch.dataType, data + rp));
                                }
                                rp += eb;
                            }
                        }
                    }
                }
            }
        }

        segPos = segEnd;
        ++validSegs;
    }

    std::vector<SpiceWaveVar> yVars;
    size_t maxPts = 0;
    double dt = 1e-4, t0 = 0.0;
    for (auto& kv : chanMap) {
        if (kv.second.samples.size() >= 2 && yVars.size() < 16) {
            if (yVars.empty()) { dt = kv.second.wfIncrement; t0 = kv.second.wfStartOffset; }
            maxPts = std::max(maxPts, kv.second.samples.size());
            SpiceWaveVar sv;
            sv.name = kv.second.displayName;
            sv.unit = kv.second.unit;
            sv.values = std::move(kv.second.samples);
            yVars.push_back(std::move(sv));
        }
    }
    if (yVars.empty() || maxPts < 2) return false;

    SpiceWaveVar xTime;
    xTime.name = "time (s)";
    xTime.values.resize(maxPts);
    for (size_t i = 0; i < maxPts; ++i) {
        xTime.values[i] = t0 + static_cast<double>(i) * dt;
    }

    std::wstring title = L"NI TDMS High-Speed Telemetry (.tdms — " + std::to_wstring(yVars.size()) +
                         L" channels, " + std::to_wstring(validSegs) + L" segments)";
    RenderSpiceWaveformsToBoard(title, false, xTime, yVars, out);
    return !out.traces.empty();
}

inline bool ParseGateNetlistOrTdmsFile(const uint8_t* data, size_t size, const std::wstring& ext, ParsedEdaBoard& out) {
    if (!data || size < 10) return false;
    if (ext == L".tdms" || (size >= 28 && std::memcmp(data, "TDSm", 4) == 0)) {
        return ParseNiTdmsBoard(data, size, out);
    }
    if (ext == L".aig" || ext == L".aag") {
        return ParseAigerNetlistBoard(data, size, out);
    }
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    if (ext == L".edif" || ext == L".edn") {
        return ParseEdifNetlistBoard(sv, out);
    }
    if (ext == L".blif" || ext == L".eblif") {
        return ParseBlifNetlistBoard(sv, out);
    }
    if (ext == L".lib") {
        if (ParseLibertyLibBoard(sv, out)) return true;
        return ParseKiCadLegacySymLibBoard(sv, out);
    }
    if (ext == L".sdf") {
        return ParseIeee1497SdfBoard(sv, out);
    }
    return false;
}

} // namespace Formats
