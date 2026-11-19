#pragma once
// EdifNetlist.h — EIA EDIF 2 0 0 Gate-Level Netlist (.edif / .edn) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

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

} // namespace Formats
