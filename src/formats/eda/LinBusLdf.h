#pragma once
// LinBusLdf.h — Automotive LIN Bus Network Description (.ldf) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

// Automotive LIN Network Description File (.ldf) — Schematic Bus Topology
inline bool ParseLinBusLdfBoard(std::string_view sv, ParsedEdaBoard& out) {
    auto drawSchRect = [&](float cx, float cy, float w, float h, float strokeW, EdaLayerId lid, int netId = 0) {
        DrawFieldbusSchRect(out, cx, cy, w, h, strokeW, lid, netId);
    };
    {
        if (sv.find("LIN_description_file") == std::string_view::npos) return false;

        // Strip /* ... */ and // ... comments
        std::string clean;
        clean.reserve(sv.size());
        for (size_t i = 0; i < sv.size(); ) {
            if (i + 1 < sv.size() && sv[i] == '/' && sv[i + 1] == '*') {
                size_t endC = sv.find("*/", i + 2);
                i = (endC == std::string_view::npos) ? sv.size() : (endC + 2);
                clean.push_back(' ');
            } else if (i + 1 < sv.size() && sv[i] == '/' && sv[i + 1] == '/') {
                size_t eol = sv.find('\n', i + 2);
                i = (eol == std::string_view::npos) ? sv.size() : (eol + 1);
                clean.push_back('\n');
            } else {
                clean.push_back(sv[i++]);
            }
        }

        auto trimAscii = [](std::string_view s) -> std::string {
            while (!s.empty() && static_cast<unsigned char>(s.front()) <= 0x20u) s.remove_prefix(1);
            while (!s.empty() && static_cast<unsigned char>(s.back()) <= 0x20u) s.remove_suffix(1);
            return std::string(s);
        };
        auto findBlockBody = [&](const char* kw, size_t startFrom = 0) -> std::string_view {
            size_t p = clean.find(kw, startFrom);
            while (p != std::string::npos) {
                bool okBefore = (p == 0 || (!std::isalnum(static_cast<unsigned char>(clean[p - 1])) && clean[p - 1] != '_'));
                size_t after = p + std::strlen(kw);
                bool okAfter = (after >= clean.size() || (!std::isalnum(static_cast<unsigned char>(clean[after])) && clean[after] != '_'));
                if (okBefore && okAfter) {
                    size_t b0 = clean.find('{', after);
                    if (b0 == std::string::npos) return {};
                    int depth = 0;
                    for (size_t k = b0; k < clean.size(); ++k) {
                        if (clean[k] == '{') ++depth;
                        else if (clean[k] == '}') {
                            if (--depth == 0) return std::string_view(clean).substr(b0 + 1, k - b0 - 1);
                        }
                    }
                    return {};
                }
                p = clean.find(kw, after);
            }
            return {};
        };

        double linSpeedKbps = 0.0;
        {
            size_t sp = clean.find("LIN_speed");
            if (sp != std::string::npos) {
                size_t eq = clean.find('=', sp);
                size_t sc = clean.find(';', sp);
                if (eq != std::string::npos && sc != std::string::npos && eq < sc) {
                    linSpeedKbps = std::strtod(clean.c_str() + eq + 1, nullptr);
                }
            }
        }

        std::string masterName;
        std::vector<std::string> slaveNames;
        {
            std::string_view nodesBlk = findBlockBody("Nodes");
            size_t pos = 0;
            while (pos < nodesBlk.size()) {
                size_t sc = nodesBlk.find(';', pos);
                if (sc == std::string_view::npos) break;
                std::string_view stmt = nodesBlk.substr(pos, sc - pos);
                pos = sc + 1;
                size_t col = stmt.find(':');
                if (col == std::string_view::npos) continue;
                std::string role = trimAscii(stmt.substr(0, col));
                std::string_view rhs = stmt.substr(col + 1);
                if (role == "Master") {
                    size_t cm = rhs.find(',');
                    masterName = trimAscii(cm == std::string_view::npos ? rhs : rhs.substr(0, cm));
                } else if (role == "Slaves") {
                    size_t rp = 0;
                    while (rp < rhs.size() && slaveNames.size() < 24u) {
                        size_t cm = rhs.find(',', rp);
                        std::string sn = trimAscii(cm == std::string_view::npos ? rhs.substr(rp) : rhs.substr(rp, cm - rp));
                        if (!sn.empty()) slaveNames.push_back(std::move(sn));
                        if (cm == std::string_view::npos) break;
                        rp = cm + 1;
                    }
                }
            }
        }

        struct LinSig {
            std::string name;
            int bitSize = 0;
            std::string publisher;
            std::vector<std::string> subscribers;
        };
        std::vector<LinSig> signals;
        {
            std::string_view sigBlk = findBlockBody("Signals");
            size_t pos = 0;
            while (pos < sigBlk.size() && signals.size() < 64u) {
                size_t sc = sigBlk.find(';', pos);
                if (sc == std::string_view::npos) break;
                std::string_view stmt = sigBlk.substr(pos, sc - pos);
                pos = sc + 1;
                // Ignore nested braces if any
                size_t col = stmt.find(':');
                if (col == std::string_view::npos) continue;
                LinSig sg;
                sg.name = trimAscii(stmt.substr(0, col));
                if (sg.name.empty()) continue;

                std::vector<std::string> fields;
                std::string_view rhs = stmt.substr(col + 1);
                int bDepth = 0;
                size_t fs = 0;
                for (size_t k = 0; k <= rhs.size(); ++k) {
                    if (k < rhs.size() && rhs[k] == '{') ++bDepth;
                    else if (k < rhs.size() && rhs[k] == '}') --bDepth;
                    else if (bDepth == 0 && (k == rhs.size() || rhs[k] == ',')) {
                        std::string f = trimAscii(rhs.substr(fs, k - fs));
                        if (!f.empty()) fields.push_back(std::move(f));
                        fs = k + 1;
                    }
                }
                if (fields.size() >= 3) {
                    sg.bitSize = std::atoi(fields[0].c_str());
                    sg.publisher = fields[2];
                    for (size_t fi = 3; fi < fields.size(); ++fi) {
                        sg.subscribers.push_back(fields[fi]);
                    }
                    signals.push_back(std::move(sg));
                }
            }
        }

        struct LinFrame {
            std::string name;
            uint32_t id = 0;
            std::string publisher;
            int byteSize = 0;
        };
        std::vector<LinFrame> frames;
        {
            std::string_view frmBlk = findBlockBody("Frames");
            size_t pos = 0;
            while (pos < frmBlk.size() && frames.size() < 48u) {
                size_t b0 = frmBlk.find('{', pos);
                if (b0 == std::string_view::npos) break;
                size_t b1 = frmBlk.find('}', b0 + 1);
                if (b1 == std::string_view::npos) break;
                std::string_view hdr = frmBlk.substr(pos, b0 - pos);
                pos = b1 + 1;

                size_t col = hdr.find(':');
                if (col == std::string_view::npos) continue;
                LinFrame fr;
                fr.name = trimAscii(hdr.substr(0, col));
                std::string_view rhs = hdr.substr(col + 1);
                size_t c1 = rhs.find(',');
                size_t c2 = (c1 != std::string_view::npos) ? rhs.find(',', c1 + 1) : std::string_view::npos;
                if (!fr.name.empty() && c1 != std::string_view::npos && c2 != std::string_view::npos) {
                    fr.id = static_cast<uint32_t>(std::strtoul(trimAscii(rhs.substr(0, c1)).c_str(), nullptr, 0));
                    fr.publisher = trimAscii(rhs.substr(c1 + 1, c2 - c1 - 1));
                    fr.byteSize = std::atoi(trimAscii(rhs.substr(c2 + 1)).c_str());
                    frames.push_back(std::move(fr));
                }
            }
        }

        size_t schedEntries = 0;
        {
            std::string_view schBlk = findBlockBody("Schedule_tables");
            for (char c : schBlk) {
                if (c == ';') ++schedEntries;
            }
        }

        // Zero-Cheat: reject empty LDFs with no ECUs and no frames/signals
        if (masterName.empty() && slaveNames.empty() && frames.empty() && signals.empty()) {
            return false;
        }

        out.kind = EdaDocKind::KiCadSch;
        std::vector<std::pair<std::string, bool>> ecuList;
        if (!masterName.empty()) ecuList.push_back({ masterName, true });
        for (const auto& sn : slaveNames) ecuList.push_back({ sn, false });
        if (ecuList.empty()) {
            std::set<std::string> inferred;
            for (const auto& f : frames) if (!f.publisher.empty()) inferred.insert(f.publisher);
            for (const auto& s : signals) if (!s.publisher.empty()) inferred.insert(s.publisher);
            for (const auto& nm : inferred) ecuList.push_back({ nm, false });
        }
        if (ecuList.empty()) return false;

        wchar_t titleBuf[160] = {};
        swprintf_s(titleBuf, L"LIN Bus Network Schematic (.ldf — %.1f kbps, %zu ECUs, %zu frames, %zu signals)",
            linSpeedKbps > 0.0 ? linSpeedKbps : 19.2, ecuList.size(), frames.size(), signals.size());
        out.formatTitle = titleBuf;

        const float pitchX = 24.0f;
        const float busHalfW = std::max(32.0f, static_cast<float>(ecuList.size()) * pitchX * 0.55f);
        // Horizontal single-wire LIN trunk bus
        out.traces.push_back({ -busHalfW, 0.0f, busHalfW, 0.0f, 0.45f, EdaLayerId::SchWire, 1 });
        wchar_t busLbl[96] = {};
        swprintf_s(busLbl, L"LIN Trunk Bus (%.2f kbps%ls)",
            linSpeedKbps > 0.0 ? linSpeedKbps : 19.2,
            schedEntries > 0 ? (L", " + std::to_wstring(schedEntries) + L" sched slots").c_str() : L"");
        out.labels.push_back({ -busHalfW, 1.4f, 1.05f, EdaLayerId::SchWire, busLbl });

        std::unordered_map<std::string, std::pair<float, float>> nodePos;
        for (size_t i = 0; i < ecuList.size(); ++i) {
            float nx = (ecuList.size() == 1u)
                ? 0.0f
                : (-busHalfW * 0.80f + (busHalfW * 1.60f * static_cast<float>(i)) / static_cast<float>(ecuList.size() - 1u));
            bool above = ecuList[i].second || ((i % 2u) == 1u);
            float ny = above ? 14.0f : -14.0f;
            float bw = 15.0f, bh = 7.0f;

            drawSchRect(nx, ny, bw, bh, ecuList[i].second ? 0.36f : 0.28f, EdaLayerId::SchSymbol, 0);
            if (ecuList[i].second) {
                drawSchRect(nx, ny, bw - 1.2f, bh - 1.2f, 0.18f, EdaLayerId::SchSymbol, 0);
            }

            float pinY = above ? (ny - bh * 0.5f) : (ny + bh * 0.5f);
            out.traces.push_back({ nx, pinY, nx, 0.0f, 0.26f, EdaLayerId::SchWire, 1 });
            out.circles.push_back({ nx, 0.0f, 0.50f, 0.25f, true, EdaLayerId::SchWire });
            out.pads.push_back({ nx, pinY, 0.6f, 0.6f, 0.0f, 0.0f, 0, EdaLayerId::SchSymbol, L"LIN", L"LIN_BUS" });

            std::wstring roleTag = ecuList[i].second ? L" [MASTER]" : L" [SLAVE]";
            out.labels.push_back({ nx - bw * 0.44f, ny + 0.6f, 0.95f, EdaLayerId::SchLabel,
                                   PathUtils::Utf8ToWide(ecuList[i].first) + roleTag });

            EdaComponent3D comp;
            comp.refDes = PathUtils::Utf8ToWide(ecuList[i].first);
            comp.value = ecuList[i].second ? L"LIN Master" : L"LIN Slave";
            comp.footprint = L"LIN_ECU";
            comp.x = nx; comp.y = ny;
            comp.widthMm = bw; comp.lengthMm = bh;
            comp.category = EdaPartCategory::IC_Package;
            out.components.push_back(std::move(comp));
            nodePos[ecuList[i].first] = { nx, ny };
        }

        // Annotate Frames & Publisher->Subscriber Signal flows below topology
        float infoY = -24.5f;
        for (size_t fi = 0; fi < std::min<size_t>(frames.size(), 8u); ++fi) {
            const auto& fr = frames[fi];
            wchar_t fBuf[128] = {};
            swprintf_s(fBuf, L"Frame 0x%02X %hs [%dB] Pub: %hs",
                fr.id, fr.name.c_str(), fr.byteSize, fr.publisher.c_str());
            out.labels.push_back({ -busHalfW, infoY - static_cast<float>(fi) * 2.6f, 0.88f, EdaLayerId::SchLabel, fBuf });
        }
        for (size_t si = 0; si < std::min<size_t>(signals.size(), 8u); ++si) {
            const auto& sg = signals[si];
            std::string subs;
            for (size_t k = 0; k < sg.subscribers.size(); ++k) {
                if (k > 0) subs += ",";
                subs += sg.subscribers[k];
            }
            wchar_t sBuf[160] = {};
            swprintf_s(sBuf, L"Sig %hs (%db): %hs -> %hs",
                sg.name.c_str(), sg.bitSize, sg.publisher.c_str(), subs.c_str());
            out.labels.push_back({ 2.0f, infoY - static_cast<float>(si) * 2.6f, 0.88f, EdaLayerId::SchWire, sBuf });

            auto itP = nodePos.find(sg.publisher);
            if (itP != nodePos.end() && !sg.subscribers.empty()) {
                auto itS = nodePos.find(sg.subscribers[0]);
                if (itS != nodePos.end() && itP->first != itS->first) {
                    float yArc = -4.0f - static_cast<float>(si % 4u) * 1.1f;
                    out.traces.push_back({ itP->second.first + 1.2f, 0.0f, itP->second.first + 1.2f, yArc, 0.16f, EdaLayerId::InnerCopper, static_cast<int>(si + 2) });
                    out.traces.push_back({ itP->second.first + 1.2f, yArc, itS->second.first - 1.2f, yArc, 0.16f, EdaLayerId::InnerCopper, static_cast<int>(si + 2) });
                    out.traces.push_back({ itS->second.first - 1.2f, yArc, itS->second.first - 1.2f, 0.0f, 0.16f, EdaLayerId::InnerCopper, static_cast<int>(si + 2) });
                }
            }
        }

        out.netCount = std::max<size_t>(1u, signals.size() + frames.size());
        return true;
    }
}

} // namespace Formats
