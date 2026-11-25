#pragma once
// AutosarArxml.h — AUTOSAR System, ECU & SWC Architecture XML (.arxml) Reader
// Uses `XmlTagScan` to parse `<ECU-INSTANCE>`, `<COMMUNICATION-CLUSTER>` / `<CAN-CLUSTER>` / `<LIN-CLUSTER>` /
// `<FLEXRAY-CLUSTER>` / `<ETHERNET-CLUSTER>`, `<COMMUNICATION-CONNECTOR>`, `<I-SIGNAL-I-PDU>`,
// `<P-PORT-PROTOTYPE>`, `<R-PORT-PROTOTYPE>`, and `<ASSEMBLY-SW-CONNECTOR>`, wiring actual parsed
// connectors, bus clusters, and SWC port connections (never modulo `ei % nRails`).
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

namespace DetailAutosarArxml {

struct ArxmlSwcPort {
    std::string name;
    bool isProvide = true; // P-PORT-PROTOTYPE vs R-PORT-PROTOTYPE
};

struct ArxmlSwc {
    std::string name;
    std::vector<ArxmlSwcPort> ports;
};

struct ArxmlEcu {
    std::string name;
    std::vector<std::string> connectors;   // e.g. CAN_Conn, Eth_Conn
    std::vector<std::string> clusterRefs;  // last path segment of referenced clusters
};

struct ArxmlCluster {
    std::string name;
    std::string protocol; // CAN, LIN, FlexRay, Ethernet, Bus
    uint64_t baudrate = 0;
    std::vector<std::string> pdus;
};

struct ArxmlAssemblyConn {
    std::string name;
    std::string provComp;
    std::string provPort;
    std::string reqComp;
    std::string reqPort;
};

inline std::string LastRefSegment(std::string_view refPath) {
    refPath = ReaderText::TrimSv(refPath);
    const size_t sl = refPath.find_last_of('/');
    return std::string((sl == std::string_view::npos) ? refPath : refPath.substr(sl + 1u));
}

inline bool IsClusterTag(std::string_view up) {
    return up == "COMMUNICATION-CLUSTER" || up == "CAN-CLUSTER" || up == "LIN-CLUSTER" ||
           up == "FLEXRAY-CLUSTER" || up == "ETHERNET-CLUSTER" || up == "TTCAN-CLUSTER" ||
           up == "J1939-CLUSTER" || up == "USER-DEFINED-CLUSTER";
}

inline bool IsConnectorTag(std::string_view up) {
    return up == "COMMUNICATION-CONNECTOR" || up == "CAN-COMMUNICATION-CONNECTOR" ||
           up == "LIN-COMMUNICATION-CONNECTOR" || up == "FLEXRAY-COMMUNICATION-CONNECTOR" ||
           up == "ETHERNET-COMMUNICATION-CONNECTOR";
}

inline bool IsSwcTypeTag(std::string_view up) {
    return up == "APPLICATION-SW-COMPONENT-TYPE" || up == "ECU-ABSTRACTION-SW-COMPONENT-TYPE" ||
           up == "SENSOR-ACTUATOR-SW-COMPONENT-TYPE" || up == "COMPLEX-DEVICE-DRIVER-SW-COMPONENT-TYPE" ||
           up == "SERVICE-SW-COMPONENT-TYPE" || up == "COMPOSITION-SW-COMPONENT-TYPE" ||
           up == "SW-COMPONENT-PROTOTYPE";
}

} // namespace DetailAutosarArxml

inline bool ParseAutosarArxmlBoard(std::string_view sv, ParsedEdaBoard& out) {
    using namespace DetailAutosarArxml;

    sv = ReaderText::StripUtf8Bom(sv);
    if (sv.size() < 28u || sv.size() > (64u << 20)) return false;
    if (sv.find('\0') != std::string_view::npos) return false;

    bool sawAutosarRoot = false;
    bool closedAutosarRoot = false;
    std::vector<std::string> tagStack;

    std::vector<ArxmlEcu> ecus;
    std::vector<ArxmlCluster> clusters;
    std::vector<std::string> globalPdus;
    std::vector<ArxmlSwc> swcs;
    std::vector<ArxmlAssemblyConn> asmConns;
    std::unordered_map<std::string, std::string> prototypeToType;

    int inEcuDepth = -1;
    int inClusterDepth = -1;
    int inSwcDepth = -1;
    int inAsmConnDepth = -1;
    int inProvIrefDepth = -1;
    int inReqIrefDepth = -1;
    std::string pendingProto;

    size_t prevAfterTag = 0u;
    size_t pos = 0u;
    XmlTagScan::XmlTag tag{};

    while (XmlTagScan::NextTag(sv, pos, tag)) {
        const std::string upName = ReaderText::ToUpperAscii(tag.localName);

        if (tag.isClosing) {
            if (tagStack.empty() || tagStack.back() != upName) return false;
            const std::string textVal = XmlTagScan::DecodeText(
                ReaderText::TrimSv(sv.substr(prevAfterTag, tag.tagStart - prevAfterTag)));

            const int curDepth = static_cast<int>(tagStack.size()) - 1;
            const std::string parent = (tagStack.size() >= 2u) ? tagStack[tagStack.size() - 2u] : std::string{};

            if (!textVal.empty()) {
                if (upName == "SHORT-NAME") {
                    if (inEcuDepth >= 0 && curDepth == inEcuDepth + 1 && !ecus.empty() && ecus.back().name.empty()) {
                        ecus.back().name = textVal;
                    } else if (inEcuDepth >= 0 && IsConnectorTag(parent) && !ecus.empty()) {
                        ecus.back().connectors.push_back(textVal);
                    } else if (inClusterDepth >= 0 && curDepth == inClusterDepth + 1 && !clusters.empty() && clusters.back().name.empty()) {
                        clusters.back().name = textVal;
                    } else if (parent == "I-SIGNAL-I-PDU" || parent == "N-PDU" || parent == "DCM-I-PDU" || parent == "PDU-TRIGGERING") {
                        if (inClusterDepth >= 0 && !clusters.empty()) clusters.back().pdus.push_back(textVal);
                        else if (globalPdus.size() < 256u) globalPdus.push_back(textVal);
                    } else if (inSwcDepth >= 0 && curDepth == inSwcDepth + 1 && !swcs.empty() && swcs.back().name.empty()) {
                        swcs.back().name = textVal;
                        pendingProto = textVal;
                    } else if (inSwcDepth >= 0 && (parent == "P-PORT-PROTOTYPE" || parent == "PR-PORT-PROTOTYPE") && !swcs.empty()) {
                        swcs.back().ports.push_back({ textVal, true });
                    } else if (inSwcDepth >= 0 && parent == "R-PORT-PROTOTYPE" && !swcs.empty()) {
                        swcs.back().ports.push_back({ textVal, false });
                    } else if (inAsmConnDepth >= 0 && curDepth == inAsmConnDepth + 1 && !asmConns.empty() && asmConns.back().name.empty()) {
                        asmConns.back().name = textVal;
                    }
                } else if (upName == "BAUDRATE" && inClusterDepth >= 0 && !clusters.empty()) {
                    uint64_t br = 0;
                    if (ReaderText::ParseUint64Sv(textVal, br, 10)) clusters.back().baudrate = br;
                } else if ((upName == "CLUSTER-REF" || upName == "COMMUNICATION-CLUSTER-REF" || upName == "COMM-CONTROLLER-REF") &&
                           inEcuDepth >= 0 && !ecus.empty()) {
                    ecus.back().clusterRefs.push_back(LastRefSegment(textVal));
                } else if (upName == "COMMUNICATION-CONNECTOR-REF") {
                    // `<COMMUNICATION-CONNECTOR-REF>/.../ECU_Name/Conn_Name</COMMUNICATION-CONNECTOR-REF>` inside a cluster channel!
                    if (inClusterDepth >= 0 && !clusters.empty()) {
                        std::string_view rp = ReaderText::TrimSv(textVal);
                        const size_t sl2 = rp.find_last_of('/');
                        if (sl2 != std::string_view::npos && sl2 > 0) {
                            const size_t sl1 = rp.find_last_of('/', sl2 - 1u);
                            std::string ecuRef(sl1 == std::string_view::npos ? rp.substr(0, sl2) : rp.substr(sl1 + 1u, sl2 - sl1 - 1u));
                            std::string connNm(rp.substr(sl2 + 1u));
                            for (auto& e : ecus) {
                                if (ReaderText::AsciiEqCI(e.name, ecuRef)) {
                                    e.clusterRefs.push_back(clusters.back().name);
                                    if (!connNm.empty()) e.connectors.push_back(connNm);
                                }
                            }
                        }
                    }
                } else if (upName == "TYPE-TREF" && inSwcDepth >= 0 && !pendingProto.empty()) {
                    prototypeToType[pendingProto] = LastRefSegment(textVal);
                } else if (inAsmConnDepth >= 0 && !asmConns.empty()) {
                    if (inProvIrefDepth >= 0) {
                        if (upName == "CONTEXT-COMPONENT-REF") asmConns.back().provComp = LastRefSegment(textVal);
                        else if (upName == "TARGET-P-PORT-REF") asmConns.back().provPort = LastRefSegment(textVal);
                    } else if (inReqIrefDepth >= 0) {
                        if (upName == "CONTEXT-COMPONENT-REF") asmConns.back().reqComp = LastRefSegment(textVal);
                        else if (upName == "TARGET-R-PORT-REF") asmConns.back().reqPort = LastRefSegment(textVal);
                    }
                }
            }

            if (curDepth == inEcuDepth) inEcuDepth = -1;
            if (curDepth == inClusterDepth) inClusterDepth = -1;
            if (curDepth == inSwcDepth) { inSwcDepth = -1; pendingProto.clear(); }
            if (curDepth == inAsmConnDepth) inAsmConnDepth = -1;
            if (curDepth == inProvIrefDepth) inProvIrefDepth = -1;
            if (curDepth == inReqIrefDepth) inReqIrefDepth = -1;

            tagStack.pop_back();
            if (upName == "AUTOSAR" && tagStack.empty()) closedAutosarRoot = true;
            prevAfterTag = tag.afterTag;
            continue;
        }

        if (tagStack.empty()) {
            if (upName != "AUTOSAR") return false;
            sawAutosarRoot = true;
        }

        if (!tag.isSelfClosing) {
            if (tagStack.size() >= 96u) return false;
            const int newDepth = static_cast<int>(tagStack.size());
            tagStack.push_back(upName);

            if (upName == "ECU-INSTANCE" && inEcuDepth < 0 && ecus.size() < 64u) {
                inEcuDepth = newDepth;
                ecus.emplace_back();
            } else if (IsClusterTag(upName) && inClusterDepth < 0 && clusters.size() < 32u) {
                inClusterDepth = newDepth;
                ArxmlCluster cl;
                if (upName.rfind("CAN-", 0) == 0 || upName.rfind("TTCAN-", 0) == 0 || upName.rfind("J1939-", 0) == 0) cl.protocol = "CAN";
                else if (upName.rfind("LIN-", 0) == 0) cl.protocol = "LIN";
                else if (upName.rfind("FLEXRAY-", 0) == 0) cl.protocol = "FlexRay";
                else if (upName.rfind("ETHERNET-", 0) == 0) cl.protocol = "Ethernet";
                else cl.protocol = "Bus";
                clusters.push_back(std::move(cl));
            } else if (IsSwcTypeTag(upName) && inSwcDepth < 0 && swcs.size() < 64u) {
                inSwcDepth = newDepth;
                swcs.emplace_back();
            } else if (upName == "ASSEMBLY-SW-CONNECTOR" && inAsmConnDepth < 0 && asmConns.size() < 256u) {
                inAsmConnDepth = newDepth;
                asmConns.emplace_back();
            } else if (upName == "PROVIDER-IREF" && inAsmConnDepth >= 0) {
                inProvIrefDepth = newDepth;
            } else if (upName == "REQUESTER-IREF" && inAsmConnDepth >= 0) {
                inReqIrefDepth = newDepth;
            }
        } else if (upName == "AUTOSAR" && tagStack.empty()) {
            closedAutosarRoot = true;
        }
        prevAfterTag = tag.afterTag;
    }

    if (!sawAutosarRoot || !closedAutosarRoot || !tagStack.empty()) return false;

    // Remove unnamed entries
    ecus.erase(std::remove_if(ecus.begin(), ecus.end(), [](const ArxmlEcu& e) { return e.name.empty(); }), ecus.end());
    clusters.erase(std::remove_if(clusters.begin(), clusters.end(), [](const ArxmlCluster& c) { return c.name.empty(); }), clusters.end());
    swcs.erase(std::remove_if(swcs.begin(), swcs.end(), [](const ArxmlSwc& s) { return s.name.empty(); }), swcs.end());

    if (ecus.empty() && clusters.empty() && swcs.empty() && globalPdus.empty()) return false;

    out = ParsedEdaBoard{};
    out.kind = EdaDocKind::KiCadSch;

    // 1. Render Communication Cluster Bus Rails (horizontal multi-drop backbones)
    const size_t nClusters = std::min<size_t>(clusters.size(), 8u);
    const size_t nEcus = std::min<size_t>(ecus.size(), 16u);
    const float busX0 = -42.0f;
    const float busX1 = std::max(42.0f, busX0 + static_cast<float>(std::max<size_t>(3u, nEcus)) * 22.0f);

    std::unordered_map<std::string, size_t> clusterIndexByName;
    for (size_t ci = 0; ci < nClusters; ++ci) {
        clusterIndexByName[ReaderText::ToLowerAscii(clusters[ci].name)] = ci;
        const float railY = 8.0f - static_cast<float>(ci) * 6.5f;
        const EdaLayerId lid = (clusters[ci].protocol == "CAN")      ? EdaLayerId::TopCopper
                             : (clusters[ci].protocol == "Ethernet") ? EdaLayerId::InnerCopper
                             : (clusters[ci].protocol == "FlexRay")  ? EdaLayerId::BottomCopper
                                                                     : EdaLayerId::InnerCopper;
        out.traces.push_back({ busX0, railY, busX1, railY, 0.50f, lid, static_cast<int>(ci + 1) });

        std::wstring clLbl = PathUtils::Utf8ToWide(clusters[ci].name + " [" + clusters[ci].protocol + "]");
        if (clusters[ci].baudrate > 0) {
            clLbl += L" " + std::to_wstring(clusters[ci].baudrate / 1000ULL) + L" kbps";
        }
        if (!clusters[ci].pdus.empty()) {
            clLbl += L" (" + std::to_wstring(clusters[ci].pdus.size()) + L" PDUs)";
        }
        out.labels.push_back({ busX0 - 1.0f, railY + 1.0f, 0.95f, lid, clLbl });
    }

    // 2. Render ECU Instances above the cluster bus & wire ONLY actual parsed connectors / cluster refs!
    for (size_t ei = 0; ei < nEcus; ++ei) {
        const auto& ecu = ecus[ei];
        const float cx = busX0 + 12.0f + static_cast<float>(ei) * 22.0f;
        const float cy = 22.0f;
        const float cw = 18.0f, ch = 9.0f;

        DrawEdaBoxRect(out, cx - cw * 0.5f, cy - ch * 0.5f, cx + cw * 0.5f, cy + ch * 0.5f, 0.32f, EdaLayerId::SchSymbol, 0);
        out.labels.push_back({ cx - cw * 0.44f, cy + 1.2f, 0.98f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(ecu.name) });

        EdaComponent3D comp;
        comp.refDes = PathUtils::Utf8ToWide(ecu.name);
        comp.value = std::to_wstring(ecu.connectors.size()) + L" conn";
        comp.footprint = L"AUTOSAR_ECU";
        comp.x = cx; comp.y = cy;
        comp.widthMm = cw; comp.lengthMm = ch; comp.heightMm = 1.6f;
        comp.pinCount = std::max<int>(1, static_cast<int>(ecu.connectors.size() + ecu.clusterRefs.size()));
        comp.category = EdaPartCategory::IC_Package;
        out.components.push_back(std::move(comp));

        // Resolve which cluster(s) this ECU actually connects to via `<CLUSTER-REF>` or connector name prefix
        std::vector<size_t> matchedClusters;
        for (const auto& cr : ecu.clusterRefs) {
            auto it = clusterIndexByName.find(ReaderText::ToLowerAscii(cr));
            if (it != clusterIndexByName.end()) matchedClusters.push_back(it->second);
        }
        if (matchedClusters.empty() && !ecu.connectors.empty()) {
            for (const auto& cn : ecu.connectors) {
                for (size_t ci = 0; ci < nClusters; ++ci) {
                    if (ContainsTextCI(cn, clusters[ci].name) ||
                        ContainsTextCI(cn, clusters[ci].protocol)) {
                        matchedClusters.push_back(ci);
                    }
                }
                // Single-cluster topology: an explicit `<COMMUNICATION-CONNECTOR>` on the ECU connects to that single cluster
                if (matchedClusters.empty() && nClusters == 1u) {
                    matchedClusters.push_back(0u);
                }
            }
        }
        std::sort(matchedClusters.begin(), matchedClusters.end());
        matchedClusters.erase(std::unique(matchedClusters.begin(), matchedClusters.end()), matchedClusters.end());

        for (size_t k = 0; k < matchedClusters.size(); ++k) {
            const size_t ci = matchedClusters[k];
            const float railY = 8.0f - static_cast<float>(ci) * 6.5f;
            const float pinX = cx + (static_cast<float>(k) - static_cast<float>(matchedClusters.size() - 1u) * 0.5f) * 2.8f;
            const float pinY = cy - ch * 0.5f;
            const std::wstring connName = (k < ecu.connectors.size())
                ? PathUtils::Utf8ToWide(ecu.connectors[k])
                : PathUtils::Utf8ToWide(clusters[ci].name);

            out.pads.push_back({ pinX, pinY, 0.85f, 0.85f, 0.0f, 0.0f, 1, EdaLayerId::SchSymbol, connName, PathUtils::Utf8ToWide(clusters[ci].name) });
            out.traces.push_back({ pinX, pinY, pinX, railY, 0.26f, EdaLayerId::SchWire, static_cast<int>(ci + 1) });
            out.circles.push_back({ pinX, railY, 0.48f, 0.22f, true, EdaLayerId::SchWire });
        }
    }

    // 3. Render SWC Components & Routed `<ASSEMBLY-SW-CONNECTOR>` P-Port -> R-Port Wires below the bus rails!
    const float swcBaseY = (nClusters > 0u) ? (8.0f - static_cast<float>(nClusters) * 6.5f - 14.0f) : 0.0f;
    struct PortCoord { float x; float y; };
    std::unordered_map<std::string, PortCoord> pPortCoords;
    std::unordered_map<std::string, PortCoord> rPortCoords;

    const size_t nSwcs = std::min<size_t>(swcs.size(), 16u);
    for (size_t si = 0; si < nSwcs; ++si) {
        const auto& swc = swcs[si];
        const float sx = busX0 + 14.0f + static_cast<float>(si % 4u) * 32.0f;
        const float sy = swcBaseY - static_cast<float>(si / 4u) * 20.0f;
        const float sw = 22.0f;
        const float sh = std::max(8.0f, static_cast<float>(std::max<size_t>(2u, swc.ports.size())) * 2.2f + 3.0f);

        DrawEdaBoxRect(out, sx - sw * 0.5f, sy - sh * 0.5f, sx + sw * 0.5f, sy + sh * 0.5f, 0.28f, EdaLayerId::SchSymbol, 0);
        out.labels.push_back({ sx - sw * 0.45f, sy + sh * 0.5f - 1.8f, 0.88f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(swc.name) });

        size_t pIdx = 0, rIdx = 0;
        for (const auto& pt : swc.ports) {
            const float px = pt.isProvide ? (sx + sw * 0.5f) : (sx - sw * 0.5f);
            const float py = sy + sh * 0.5f - 3.6f - static_cast<float>(pt.isProvide ? pIdx++ : rIdx++) * 2.2f;
            out.pads.push_back({
                px, py, 0.70f, 0.70f, 0.0f, 0.0f, 1,
                pt.isProvide ? EdaLayerId::TopCopper : EdaLayerId::InnerCopper,
                PathUtils::Utf8ToWide(pt.name), PathUtils::Utf8ToWide(swc.name)
            });
            const std::string key = ReaderText::ToLowerAscii(swc.name + "/" + pt.name);
            if (pt.isProvide) pPortCoords[key] = { px, py };
            else              rPortCoords[key] = { px, py };
        }

        EdaComponent3D comp;
        comp.refDes = PathUtils::Utf8ToWide(swc.name);
        comp.value = std::to_wstring(swc.ports.size()) + L" ports";
        comp.footprint = L"AUTOSAR_SWC";
        comp.x = sx; comp.y = sy;
        comp.widthMm = sw; comp.lengthMm = sh; comp.heightMm = 1.2f;
        comp.pinCount = std::max<int>(1, static_cast<int>(swc.ports.size()));
        comp.category = EdaPartCategory::IC_Package;
        out.components.push_back(std::move(comp));
    }

    // Wire `<ASSEMBLY-SW-CONNECTOR>` P-Port -> R-Port connections orthogonally
    int asmNetId = static_cast<int>(nClusters + 1u);
    for (const auto& ac : asmConns) {
        auto resolvePort = [&](const std::string& compNm, const std::string& portNm,
                               const std::unordered_map<std::string, PortCoord>& map, PortCoord& outPt) -> bool {
            auto it = map.find(ReaderText::ToLowerAscii(compNm + "/" + portNm));
            if (it != map.end()) { outPt = it->second; return true; }
            auto pit = prototypeToType.find(compNm);
            if (pit != prototypeToType.end()) {
                auto it2 = map.find(ReaderText::ToLowerAscii(pit->second + "/" + portNm));
                if (it2 != map.end()) { outPt = it2->second; return true; }
            }
            return false;
        };

        PortCoord src{}, dst{};
        if (resolvePort(ac.provComp, ac.provPort, pPortCoords, src) &&
            resolvePort(ac.reqComp, ac.reqPort, rPortCoords, dst))
        {
            const float midX = (src.x + dst.x) * 0.5f + static_cast<float>((asmNetId % 5) - 2) * 0.7f;
            out.traces.push_back({ src.x, src.y, midX, src.y, 0.22f, EdaLayerId::SchWire, asmNetId });
            out.traces.push_back({ midX, src.y, midX, dst.y, 0.22f, EdaLayerId::SchWire, asmNetId });
            out.traces.push_back({ midX, dst.y, dst.x, dst.y, 0.22f, EdaLayerId::SchWire, asmNetId });
            ++asmNetId;
        }
    }

    // Standalone I-SIGNAL-I-PDU frames if file only defined PDUs without ECUs/SWCs
    if (out.traces.empty() && !globalPdus.empty()) {
        for (size_t pi = 0; pi < std::min<size_t>(globalPdus.size(), 16u); ++pi) {
            const float py = -static_cast<float>(pi) * 5.0f;
            DrawEdaBoxRect(out, -20.0f, py - 1.8f, 20.0f, py + 1.8f, 0.25f, EdaLayerId::TopCopper, static_cast<int>(pi + 1));
            out.labels.push_back({ -18.5f, py - 0.35f, 0.88f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(globalPdus[pi]) });
        }
    }

    wchar_t titleBuf[240] = {};
    swprintf_s(titleBuf, L"AUTOSAR Architecture (.arxml — %zu ECUs, %zu clusters, %zu SWCs, %zu connectors)",
               ecus.size(), clusters.size(), swcs.size(), asmConns.size());
    out.formatTitle = titleBuf;
    out.netCount = std::max<size_t>(1u, clusters.size() + asmConns.size() + globalPdus.size());
    return !out.traces.empty() || !out.components.empty();
}

} // namespace Formats
