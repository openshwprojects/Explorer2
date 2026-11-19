#pragma once
// AutosarArxml.h — AUTOSAR System & SWC Architecture Description (.arxml) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

// #76. AUTOSAR System / SWC Architecture Description (.arxml)
inline bool ParseAutosarArxml200(std::string_view sv, ParsedEdaBoard& out) {
    auto drawSchRect = [&](float cx, float cy, float w, float h, float strokeW, EdaLayerId lid, int netId = 0) {
        DrawFieldbusSchRect(out, cx, cy, w, h, strokeW, lid, netId);
    };
    {
        if (sv.find("<AUTOSAR") == std::string_view::npos) return false;

        auto extractChildShortName = [&](size_t openTagEnd, size_t blockEnd) -> std::string {
            std::string_view sub = sv.substr(openTagEnd, blockEnd - openTagEnd);
            size_t sp = sub.find("<SHORT-NAME>");
            if (sp == std::string_view::npos) return {};
            sp += 12;
            size_t se = sub.find("</SHORT-NAME>", sp);
            if (se == std::string_view::npos) return {};
            std::string_view nm = sub.substr(sp, se - sp);
            while (!nm.empty() && static_cast<unsigned char>(nm.front()) <= 0x20u) nm.remove_prefix(1);
            while (!nm.empty() && static_cast<unsigned char>(nm.back()) <= 0x20u) nm.remove_suffix(1);
            return std::string(nm);
        };

        auto scanElements = [&](std::string_view tagName, std::vector<std::pair<std::string, std::string_view>>& outBlocks) {
            std::string openPat = "<" + std::string(tagName);
            std::string closePat = "</" + std::string(tagName) + ">";
            size_t p = 0;
            while ((p = sv.find(openPat, p)) != std::string_view::npos && outBlocks.size() < 32u) {
                size_t after = p + openPat.size();
                if (after < sv.size() && sv[after] != '>' && sv[after] != ' ' && sv[after] != '\t' && sv[after] != '\r' && sv[after] != '\n') {
                    p = after;
                    continue;
                }
                size_t gt = sv.find('>', after);
                if (gt == std::string_view::npos) break;
                if (sv[gt - 1] == '/') { p = gt + 1; continue; }
                size_t ce = sv.find(closePat, gt + 1);
                if (ce == std::string_view::npos) break;
                std::string sn = extractChildShortName(gt + 1, ce);
                if (!sn.empty()) {
                    outBlocks.push_back({ std::move(sn), sv.substr(gt + 1, ce - gt - 1) });
                }
                p = ce + closePat.size();
            }
        };

        struct ArCluster { std::string kind; std::string name; };
        struct ArSwc {
            std::string kind;
            std::string name;
            std::vector<std::string> pPorts;
            std::vector<std::string> rPorts;
        };

        std::vector<ArCluster> clusters;
        const std::pair<const char*, const char*> clusterTypes[] = {
            { "CAN-CLUSTER", "CAN" },
            { "LIN-CLUSTER", "LIN" },
            { "FLEXRAY-CLUSTER", "FlexRay" },
            { "ETHERNET-CLUSTER", "Ethernet" }
        };
        for (const auto& ct : clusterTypes) {
            std::vector<std::pair<std::string, std::string_view>> blks;
            scanElements(ct.first, blks);
            for (auto& b : blks) clusters.push_back({ ct.second, std::move(b.first) });
        }

        std::vector<std::pair<std::string, std::string_view>> ecuBlks;
        scanElements("ECU-INSTANCE", ecuBlks);

        std::vector<ArSwc> swcs;
        const std::pair<const char*, const char*> swcTypes[] = {
            { "APPLICATION-SW-COMPONENT-TYPE", "AppSWC" },
            { "SENSOR-ACTUATOR-SW-COMPONENT-TYPE", "SensorSWC" },
            { "COMPLEX-DEVICE-DRIVER-SW-COMPONENT-TYPE", "CDD" },
            { "COMPOSITION-SW-COMPONENT-TYPE", "Composition" },
            { "ECU-ABSTRACTION-SW-COMPONENT-TYPE", "EcuAb" },
            { "SERVICE-SW-COMPONENT-TYPE", "ServiceSWC" },
            { "SW-COMPONENT-TYPE", "SWC" }
        };
        for (const auto& st : swcTypes) {
            std::vector<std::pair<std::string, std::string_view>> blks;
            scanElements(st.first, blks);
            for (auto& b : blks) {
                ArSwc swc;
                swc.kind = st.second;
                swc.name = std::move(b.first);
                auto extractSubNames = [&](const char* subTag, std::vector<std::string>& dst) {
                    std::string op = std::string("<") + subTag;
                    std::string cp = std::string("</") + subTag + ">";
                    size_t sp = 0;
                    while ((sp = b.second.find(op, sp)) != std::string_view::npos && dst.size() < 8u) {
                        size_t sgt = b.second.find('>', sp);
                        size_t sce = (sgt != std::string_view::npos) ? b.second.find(cp, sgt) : std::string_view::npos;
                        if (sce == std::string_view::npos) break;
                        std::string_view inner = b.second.substr(sgt + 1, sce - sgt - 1);
                        size_t n0 = inner.find("<SHORT-NAME>");
                        size_t n1 = (n0 != std::string_view::npos) ? inner.find("</SHORT-NAME>", n0 + 12) : std::string_view::npos;
                        if (n0 != std::string_view::npos && n1 != std::string_view::npos) {
                            dst.emplace_back(inner.substr(n0 + 12, n1 - n0 - 12));
                        }
                        sp = sce + cp.size();
                    }
                };
                extractSubNames("P-PORT-PROTOTYPE", swc.pPorts);
                extractSubNames("R-PORT-PROTOTYPE", swc.rPorts);
                swcs.push_back(std::move(swc));
            }
        }

        std::vector<std::string> sigNames;
        for (const char* sigTag : { "I-SIGNAL", "SYSTEM-SIGNAL", "PDU-TRIGGERING", "FRAME-TRIGGERING" }) {
            std::vector<std::pair<std::string, std::string_view>> blks;
            scanElements(sigTag, blks);
            for (auto& b : blks) {
                if (sigNames.size() < 24u) sigNames.push_back(std::move(b.first));
            }
        }

        // Zero-Cheat: empty <AUTOSAR></AUTOSAR> or empty <AR-PACKAGE> MUST return false
        if (clusters.empty() && ecuBlks.empty() && swcs.empty() && sigNames.empty()) {
            return false;
        }

        out.kind = EdaDocKind::KiCadSch;
        wchar_t titleBuf[160] = {};
        swprintf_s(titleBuf, L"AUTOSAR Architecture Schematic (.arxml — %zu clusters, %zu ECUs, %zu SWCs, %zu signals)",
            clusters.size(), ecuBlks.size(), swcs.size(), sigNames.size());
        out.formatTitle = titleBuf;

        const size_t nRails = std::max<size_t>(1u, clusters.size());
        const size_t nBlocks = ecuBlks.size() + swcs.size();
        const float busHalfW = std::max(36.0f, static_cast<float>(std::max<size_t>(2u, nBlocks)) * 14.0f);

        // Draw Communication Cluster / VFB Rails
        for (size_t ri = 0; ri < nRails; ++ri) {
            float ry = -static_cast<float>(ri) * 3.8f;
            out.traces.push_back({ -busHalfW, ry, busHalfW, ry, 0.42f, EdaLayerId::SchWire, static_cast<int>(ri + 1) });
            std::wstring railName = !clusters.empty()
                ? (PathUtils::Utf8ToWide(clusters[ri].kind) + L": " + PathUtils::Utf8ToWide(clusters[ri].name))
                : L"AUTOSAR Virtual Function Bus (VFB / RTE)";
            out.labels.push_back({ -busHalfW, ry + 0.9f, 0.95f, EdaLayerId::SchWire, railName });
        }

        // Place ECU-INSTANCE blocks above the cluster rails
        for (size_t ei = 0; ei < ecuBlks.size(); ++ei) {
            float ex = (ecuBlks.size() == 1u)
                ? (-busHalfW * 0.4f)
                : (-busHalfW * 0.78f + (busHalfW * 1.56f * static_cast<float>(ei)) / static_cast<float>(ecuBlks.size() - 1u));
            float ey = 14.0f;
            float bw = 16.0f, bh = 7.2f;
            drawSchRect(ex, ey, bw, bh, 0.34f, EdaLayerId::SchSymbol, 0);
            float targetY = -static_cast<float>(ei % nRails) * 3.8f;
            out.traces.push_back({ ex, ey - bh * 0.5f, ex, targetY, 0.26f, EdaLayerId::SchWire, static_cast<int>((ei % nRails) + 1) });
            out.circles.push_back({ ex, targetY, 0.48f, 0.24f, true, EdaLayerId::SchWire });
            out.pads.push_back({ ex, ey - bh * 0.5f, 0.6f, 0.6f, 0.0f, 0.0f, 1, EdaLayerId::SchSymbol, L"COM", L"" });
            out.labels.push_back({ ex - bw * 0.44f, ey + 0.8f, 0.95f, EdaLayerId::SchLabel, L"ECU: " + PathUtils::Utf8ToWide(ecuBlks[ei].first) });

            EdaComponent3D comp;
            comp.refDes = PathUtils::Utf8ToWide(ecuBlks[ei].first);
            comp.value = L"ECU-INSTANCE";
            comp.footprint = L"AUTOSAR_ECU";
            comp.x = ex; comp.y = ey;
            comp.widthMm = bw; comp.lengthMm = bh;
            comp.category = EdaPartCategory::IC_Package;
            out.components.push_back(std::move(comp));
        }

        // Place SW-COMPONENT blocks below the cluster/VFB rails with R-Ports (left) & P-Ports (right)
        const float swcBaseY = -static_cast<float>(nRails - 1u) * 3.8f - 15.0f;
        for (size_t si = 0; si < swcs.size(); ++si) {
            const auto& sw = swcs[si];
            float sx = (swcs.size() == 1u)
                ? (ecuBlks.empty() ? 0.0f : busHalfW * 0.35f)
                : (-busHalfW * 0.78f + (busHalfW * 1.56f * static_cast<float>(si)) / static_cast<float>(swcs.size() - 1u));
            size_t maxPorts = std::max<size_t>({ 1u, sw.pPorts.size(), sw.rPorts.size() });
            float bw = 17.0f;
            float bh = std::max(7.5f, static_cast<float>(maxPorts) * 2.8f + 3.0f);
            float sy = swcBaseY;

            drawSchRect(sx, sy, bw, bh, 0.30f, EdaLayerId::SchSymbol, 0);
            float targetY = -static_cast<float>(si % nRails) * 3.8f;
            out.traces.push_back({ sx, sy + bh * 0.5f, sx, targetY, 0.24f, EdaLayerId::SchWire, static_cast<int>((si % nRails) + 1) });
            out.circles.push_back({ sx, targetY, 0.45f, 0.22f, true, EdaLayerId::SchWire });

            for (size_t rp = 0; rp < sw.rPorts.size(); ++rp) {
                float py = sy + bh * 0.3f - static_cast<float>(rp) * 2.5f;
                out.traces.push_back({ sx - bw * 0.5f - 2.6f, py, sx - bw * 0.5f, py, 0.24f, EdaLayerId::SchWire, 0 });
                out.pads.push_back({ sx - bw * 0.5f - 2.6f, py, 0.55f, 0.55f, 0.0f, 0.0f, 1, EdaLayerId::SchSymbol, L"R", PathUtils::Utf8ToWide(sw.rPorts[rp]) });
                out.labels.push_back({ sx - bw * 0.5f - 9.0f, py + 0.5f, 0.8f, EdaLayerId::SchWire, L"R:" + PathUtils::Utf8ToWide(sw.rPorts[rp]) });
            }
            for (size_t pp = 0; pp < sw.pPorts.size(); ++pp) {
                float py = sy + bh * 0.3f - static_cast<float>(pp) * 2.5f;
                out.traces.push_back({ sx + bw * 0.5f, py, sx + bw * 0.5f + 2.6f, py, 0.24f, EdaLayerId::SchWire, 0 });
                out.pads.push_back({ sx + bw * 0.5f + 2.6f, py, 0.55f, 0.55f, 0.0f, 0.0f, 1, EdaLayerId::SchSymbol, L"P", PathUtils::Utf8ToWide(sw.pPorts[pp]) });
                out.labels.push_back({ sx + bw * 0.5f + 3.0f, py + 0.5f, 0.8f, EdaLayerId::SchWire, L"P:" + PathUtils::Utf8ToWide(sw.pPorts[pp]) });
            }

            out.labels.push_back({ sx - bw * 0.44f, sy + bh * 0.5f - 1.8f, 0.9f, EdaLayerId::SchLabel,
                                   PathUtils::Utf8ToWide(sw.name) + L" [" + PathUtils::Utf8ToWide(sw.kind) + L"]" });

            EdaComponent3D comp;
            comp.refDes = PathUtils::Utf8ToWide(sw.name);
            comp.value = PathUtils::Utf8ToWide(sw.kind);
            comp.footprint = L"AUTOSAR_SWC";
            comp.x = sx; comp.y = sy;
            comp.widthMm = bw; comp.lengthMm = bh;
            comp.pinCount = static_cast<int>(sw.pPorts.size() + sw.rPorts.size() + 1u);
            comp.category = EdaPartCategory::IC_Package;
            out.components.push_back(std::move(comp));
        }

        // Signal / PDU callouts
        float sigListY = swcs.empty() ? -10.0f : (swcBaseY - 12.0f);
        for (size_t k = 0; k < std::min<size_t>(sigNames.size(), 8u); ++k) {
            float lx = -busHalfW + static_cast<float>(k % 2u) * (busHalfW);
            float ly = sigListY - static_cast<float>(k / 2u) * 2.6f;
            out.labels.push_back({ lx, ly, 0.85f, EdaLayerId::SchWire, L"Signal/PDU: " + PathUtils::Utf8ToWide(sigNames[k]) });
        }

        out.netCount = std::max<size_t>(1u, clusters.size() + sigNames.size());
        return true;
    }
}

} // namespace Formats
