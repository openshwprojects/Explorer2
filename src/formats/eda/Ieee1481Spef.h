#pragma once
// Ieee1481Spef.h — IEEE Std 1481 Standard Parasitic Exchange Format (.spef) RC Extraction Reader
// Parses *SPEF, *DESIGN, *C_UNIT/*R_UNIT, *NAME_MAP, *PORTS, and *D_NET <net> <totalCap> blocks
// (*CONN driver/load pins & *C physical coordinates, *CAP ground & cross-coupled capacitances,
// *RES distributed wire resistances, *END), rendering an ASIC post-layout RC parasitic network.
// Included inside namespace Explorer2 in EdaReadersVlsiWave.cpp (do NOT put #include <...> here).

namespace Formats {

namespace DetailIeeeSpef {

inline std::string UnquoteSpef(std::string_view s) {
    s = ReaderText::TrimSv(s);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        s = s.substr(1, s.size() - 2);
    }
    // Unescape SPEF escaped delimiters `\[` -> `[`
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            out.push_back(s[++i]);
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

inline std::string ResolveSpefName(
    std::string_view raw,
    const std::unordered_map<std::string, std::string>& nameMap,
    char delim)
{
    raw = ReaderText::TrimSv(raw);
    if (raw.empty() || raw.front() != '*') return UnquoteSpef(raw);
    const size_t sep = raw.find(delim, 1);
    std::string key(raw.substr(0, sep));
    auto it = nameMap.find(key);
    if (it == nameMap.end()) return std::string(raw);
    if (sep == std::string_view::npos) return it->second;
    return it->second + std::string(raw.substr(sep));
}

struct RcNodeInfo {
    std::string name;
    std::string cellType;
    char kind = 'N'; // 'P'=port, 'I'=instPin, 'N'=internal RC tap
    char dir  = 'B'; // 'O'=driver, 'I'=load, 'B'=bidir
    bool hasCoord = false;
    float rawX = 0.0f, rawY = 0.0f;
    float vx = 0.0f, vy = 0.0f;
    double gndCap = 0.0;
};

struct ResSeg {
    std::string n1;
    std::string n2;
    double val = 0.0;
};

struct CoupleCap {
    std::string n1;
    std::string n2;
    double val = 0.0;
};

struct SpefNet {
    std::string name;
    double totalCap = 0.0;
    std::vector<RcNodeInfo> connNodes;
    std::vector<ResSeg> resistors;
    std::vector<CoupleCap> coupleCaps;
    std::unordered_map<std::string, double> gndCaps;
};

} // namespace DetailIeeeSpef

inline bool ParseIeee1481Spef(
    const uint8_t* data,
    size_t size,
    ParsedEdaBoard& out)
{
    if (!data || size < 24 || size > 64u * 1024u * 1024u) return false;
    std::string_view sv = ReaderText::StripUtf8Bom(std::string_view(reinterpret_cast<const char*>(data), size));
    if (sv.find('\0') != std::string_view::npos) return false;

    bool sawSpefHeader = false;
    std::string designName = "asic_top";
    std::string cUnit = "FF";
    std::string rUnit = "OHM";
    char pinDelim = ':';

    std::unordered_map<std::string, std::string> nameMap;
    std::unordered_map<std::string, std::pair<float, float>> portCoords;
    std::vector<DetailIeeeSpef::SpefNet> nets;

    enum class TopSec { Header, NameMap, Ports, DNet };
    enum class NetSub { None, Conn, Cap, Res };
    TopSec topSec = TopSec::Header;
    NetSub netSub = NetSub::None;
    DetailIeeeSpef::SpefNet curNet;
    bool inDNet = false;

    size_t pos = 0;
    std::string_view rawLine;
    while (ReaderText::NextLineSv(sv, pos, rawLine)) {
        const size_t cmt = rawLine.find("//");
        std::string_view line = ReaderText::TrimSv((cmt == std::string_view::npos) ? rawLine : rawLine.substr(0, cmt));
        if (line.empty()) continue;

        std::string_view rem = line, t0;
        if (!ReaderText::NextTokenSv(rem, t0)) continue;

        if (!sawSpefHeader) {
            if (!ReaderText::AsciiEqCI(t0, "*SPEF")) return false;
            sawSpefHeader = true;
            continue;
        }

        if (inDNet) {
            if (ReaderText::AsciiEqCI(t0, "*END")) {
                if (!curNet.connNodes.empty() || !curNet.resistors.empty() || !curNet.gndCaps.empty() || !curNet.coupleCaps.empty()) {
                    if (nets.size() < 256) nets.push_back(std::move(curNet));
                }
                curNet = {};
                inDNet = false;
                netSub = NetSub::None;
                topSec = TopSec::Header;
                continue;
            }
            if (ReaderText::AsciiEqCI(t0, "*CONN")) { netSub = NetSub::Conn; continue; }
            if (ReaderText::AsciiEqCI(t0, "*CAP"))  { netSub = NetSub::Cap;  continue; }
            if (ReaderText::AsciiEqCI(t0, "*RES"))  { netSub = NetSub::Res;  continue; }
            if (ReaderText::AsciiEqCI(t0, "*INDUC")) { netSub = NetSub::None; continue; }

            if (netSub == NetSub::Conn) {
                // *P <port> I|O|B [*C x y] [*D cell]  OR  *I <inst:pin> I|O|B [*C x y] [*D cell]  OR  *N <node> [*C x y]
                if (t0 == "*P" || t0 == "*I" || t0 == "*N") {
                    std::string_view nTok;
                    if (!ReaderText::NextTokenSv(rem, nTok)) return false;
                    DetailIeeeSpef::RcNodeInfo ni;
                    ni.kind = t0[1];
                    ni.name = DetailIeeeSpef::ResolveSpefName(nTok, nameMap, pinDelim);

                    if (ni.kind != 'N') {
                        std::string_view dTok;
                        if (ReaderText::NextTokenSv(rem, dTok) && !dTok.empty()) {
                            ni.dir = static_cast<char>(std::toupper(static_cast<unsigned char>(dTok[0])));
                        }
                    }
                    auto pcIt = portCoords.find(ni.name);
                    if (pcIt != portCoords.end()) {
                        ni.hasCoord = true;
                        ni.rawX = pcIt->second.first;
                        ni.rawY = pcIt->second.second;
                    }

                    std::string_view attr;
                    while (ReaderText::NextTokenSv(rem, attr)) {
                        if (ReaderText::AsciiEqCI(attr, "*C")) {
                            std::string_view sx, sy;
                            float cx = 0, cy = 0;
                            if (ReaderText::NextTokenSv(rem, sx) && ReaderText::ParseFloatSv(sx, cx) &&
                                ReaderText::NextTokenSv(rem, sy) && ReaderText::ParseFloatSv(sy, cy)) {
                                ni.hasCoord = true;
                                ni.rawX = cx;
                                ni.rawY = cy;
                            }
                        } else if (ReaderText::AsciiEqCI(attr, "*D")) {
                            std::string_view dc;
                            if (ReaderText::NextTokenSv(rem, dc)) {
                                ni.cellType = DetailIeeeSpef::ResolveSpefName(dc, nameMap, pinDelim);
                            }
                        }
                    }
                    curNet.connNodes.push_back(std::move(ni));
                }
            } else if (netSub == NetSub::Cap) {
                // Ground cap: <idx> <node1> <val>
                // Coupling cap: <idx> <node1> <node2> <val>
                std::string_view a1, a2, a3;
                uint64_t capIdx = 0;
                if (!ReaderText::ParseUint64Sv(t0, capIdx, 10) ||
                    !ReaderText::NextTokenSv(rem, a1) ||
                    !ReaderText::NextTokenSv(rem, a2)) {
                    return false;
                }
                const std::string n1 = DetailIeeeSpef::ResolveSpefName(a1, nameMap, pinDelim);
                if (ReaderText::NextTokenSv(rem, a3)) {
                    double cv = 0.0;
                    if (!ReaderText::ParseDoubleSv(a3, cv) || !std::isfinite(cv) || cv < 0.0) return false;
                    const std::string n2 = DetailIeeeSpef::ResolveSpefName(a2, nameMap, pinDelim);
                    if (curNet.coupleCaps.size() < 512) curNet.coupleCaps.push_back({ n1, n2, cv });
                } else {
                    double cv = 0.0;
                    if (!ReaderText::ParseDoubleSv(a2, cv) || !std::isfinite(cv) || cv < 0.0) return false;
                    curNet.gndCaps[n1] += cv;
                }
            } else if (netSub == NetSub::Res) {
                // <idx> <node1> <node2> <val>
                std::string_view a1, a2, a3;
                uint64_t resIdx = 0;
                double rv = 0.0;
                if (!ReaderText::ParseUint64Sv(t0, resIdx, 10) ||
                    !ReaderText::NextTokenSv(rem, a1) ||
                    !ReaderText::NextTokenSv(rem, a2) ||
                    !ReaderText::NextTokenSv(rem, a3) ||
                    !ReaderText::ParseDoubleSv(a3, rv) || !std::isfinite(rv) || rv < 0.0) {
                    return false;
                }
                if (curNet.resistors.size() < 1024) {
                    curNet.resistors.push_back({
                        DetailIeeeSpef::ResolveSpefName(a1, nameMap, pinDelim),
                        DetailIeeeSpef::ResolveSpefName(a2, nameMap, pinDelim),
                        rv
                    });
                }
            }
            continue;
        }

        // Top-level SPEF sections
        if (ReaderText::AsciiEqCI(t0, "*DESIGN")) {
            designName = DetailIeeeSpef::UnquoteSpef(rem);
        } else if (ReaderText::AsciiEqCI(t0, "*DELIMITER")) {
            std::string_view dt;
            if (ReaderText::NextTokenSv(rem, dt) && !dt.empty()) pinDelim = dt.front();
        } else if (ReaderText::AsciiEqCI(t0, "*C_UNIT")) {
            std::string_view num, u;
            if (ReaderText::NextTokenSv(rem, num) && ReaderText::NextTokenSv(rem, u)) cUnit = ReaderText::ToUpperAscii(u);
        } else if (ReaderText::AsciiEqCI(t0, "*R_UNIT")) {
            std::string_view num, u;
            if (ReaderText::NextTokenSv(rem, num) && ReaderText::NextTokenSv(rem, u)) rUnit = ReaderText::ToUpperAscii(u);
        } else if (ReaderText::AsciiEqCI(t0, "*NAME_MAP")) {
            topSec = TopSec::NameMap;
        } else if (ReaderText::AsciiEqCI(t0, "*PORTS")) {
            topSec = TopSec::Ports;
        } else if (ReaderText::AsciiEqCI(t0, "*D_NET") || ReaderText::AsciiEqCI(t0, "*R_NET")) {
            std::string_view netTok, capTok;
            double totCap = 0.0;
            if (!ReaderText::NextTokenSv(rem, netTok) ||
                !ReaderText::NextTokenSv(rem, capTok) ||
                !ReaderText::ParseDoubleSv(capTok, totCap) || !std::isfinite(totCap) || totCap < 0.0) {
                return false;
            }
            curNet = {};
            curNet.name = DetailIeeeSpef::ResolveSpefName(netTok, nameMap, pinDelim);
            curNet.totalCap = totCap;
            inDNet = true;
            netSub = NetSub::None;
            topSec = TopSec::DNet;
        } else if (!t0.empty() && t0.front() == '*' && topSec == TopSec::NameMap) {
            std::string mapped = DetailIeeeSpef::UnquoteSpef(rem);
            if (!mapped.empty() && nameMap.size() < 131072) {
                nameMap[std::string(t0)] = std::move(mapped);
            }
        } else if (topSec == TopSec::Ports) {
            std::string pName = DetailIeeeSpef::ResolveSpefName(t0, nameMap, pinDelim);
            std::string_view attr;
            while (ReaderText::NextTokenSv(rem, attr)) {
                if (ReaderText::AsciiEqCI(attr, "*C")) {
                    std::string_view sx, sy;
                    float cx = 0, cy = 0;
                    if (ReaderText::NextTokenSv(rem, sx) && ReaderText::ParseFloatSv(sx, cx) &&
                        ReaderText::NextTokenSv(rem, sy) && ReaderText::ParseFloatSv(sy, cy)) {
                        portCoords[pName] = { cx, cy };
                    }
                }
            }
        }
    }

    if (!sawSpefHeader || inDNet || nets.empty()) return false;

    // Render extracted RC networks cleanly in vertical bands
    out.kind = EdaDocKind::KiCadSch;
    size_t totalRes = 0, totalCap = 0;
    for (const auto& n : nets) {
        totalRes += n.resistors.size();
        totalCap += n.gndCaps.size() + n.coupleCaps.size();
    }

    const size_t showNets = std::min<size_t>(nets.size(), 16u);
    std::unordered_map<std::string, std::pair<float, float>> globalNodePos;

    for (size_t ni = 0; ni < showNets; ++ni) {
        const auto& net = nets[ni];
        const float bandY = 24.0f - static_cast<float>(ni) * 18.0f;
        const int netId = static_cast<int>(ni + 1);

        // Collect all unique RC nodes on this net in deterministic order (drivers -> internal taps -> loads)
        std::unordered_map<std::string, DetailIeeeSpef::RcNodeInfo> connLookup;
        for (const auto& cn : net.connNodes) {
            connLookup[cn.name] = cn;
        }

        std::vector<DetailIeeeSpef::RcNodeInfo> ordered;
        std::unordered_map<std::string, size_t> idxByName;
        auto addNode = [&](const DetailIeeeSpef::RcNodeInfo& info) {
            if (idxByName.find(info.name) == idxByName.end()) {
                idxByName[info.name] = ordered.size();
                ordered.push_back(info);
            }
        };

        for (const auto& cn : net.connNodes) if (cn.dir == 'O') addNode(cn);
        for (const auto& r : net.resistors) {
            if (connLookup.find(r.n1) == connLookup.end()) { DetailIeeeSpef::RcNodeInfo a; a.name = r.n1; addNode(a); }
            if (connLookup.find(r.n2) == connLookup.end()) { DetailIeeeSpef::RcNodeInfo b; b.name = r.n2; addNode(b); }
        }
        for (const auto& kv : net.gndCaps) {
            if (connLookup.find(kv.first) == connLookup.end()) { DetailIeeeSpef::RcNodeInfo c; c.name = kv.first; addNode(c); }
        }
        for (const auto& cn : net.connNodes) if (cn.dir != 'O') addNode(cn);

        // Assign schematic X/Y positions per RC node
        const size_t nNodes = std::max<size_t>(1u, ordered.size());
        size_t loadRow = 0;
        for (size_t k = 0; k < ordered.size(); ++k) {
            auto& nd = ordered[k];
            nd.vx = -26.0f + (76.0f * static_cast<float>(k)) / static_cast<float>(std::max<size_t>(1u, nNodes - 1u));
            if (nd.kind != 'N' && nd.dir == 'I' && ordered.size() > 3) {
                nd.vy = bandY + ((loadRow++ % 2u == 0) ? 2.6f : -2.6f);
            } else {
                nd.vy = bandY;
            }
            globalNodePos[nd.name] = { nd.vx, nd.vy };
        }

        // Net header banner
        wchar_t netHdr[160] = {};
        swprintf_s(netHdr, L"%hs (Ctot=%.2f %hs, %zu R, %zu C)",
            net.name.c_str(), net.totalCap, cUnit.c_str(),
            net.resistors.size(), net.gndCaps.size() + net.coupleCaps.size());
        out.labels.push_back({ -34.0f, bandY + 5.6f, 0.95f, EdaLayerId::TopSilk, netHdr });

        // Emit driver / load cells & pins
        for (const auto& nd : ordered) {
            if (nd.kind == 'P' || nd.kind == 'I') {
                EdaComponent3D cell;
                cell.refDes = PathUtils::Utf8ToWide(nd.name);
                cell.value = PathUtils::Utf8ToWide(nd.cellType.empty() ? (nd.dir == 'O' ? "DRIVER" : "LOAD") : nd.cellType);
                cell.footprint = (nd.kind == 'P') ? L"IO_PAD" : L"STD_CELL";
                cell.x = nd.vx + ((nd.dir == 'O') ? -4.5f : 4.5f);
                cell.y = nd.vy;
                cell.widthMm = 7.2f;
                cell.lengthMm = 3.8f;
                cell.heightMm = 0.9f;
                cell.category = EdaPartCategory::IC_Package;
                DrawEdaBoxRect(out, cell.x - 3.6f, cell.y - 1.9f, cell.x + 3.6f, cell.y + 1.9f, 0.22f, EdaLayerId::SchSymbol, netId);
                out.traces.push_back({ cell.x + ((nd.dir == 'O') ? 3.6f : -3.6f), cell.y, nd.vx, nd.vy, 0.24f, EdaLayerId::SchWire, netId });
                out.labels.push_back({ cell.x - 3.2f, cell.y + 0.2f, 0.72f, EdaLayerId::SchLabel, cell.refDes });
                out.components.push_back(std::move(cell));
            }
            out.vias.push_back({ nd.vx, nd.vy, 0.65f, 0.0f, netId });

            // Ground capacitor symbol hanging below node
            auto cit = net.gndCaps.find(nd.name);
            if (cit != net.gndCaps.end() && cit->second > 0.0) {
                const float x = nd.vx, y = nd.vy;
                out.traces.push_back({ x, y, x, y - 2.0f, 0.18f, EdaLayerId::InnerCopper, netId });
                out.traces.push_back({ x - 1.1f, y - 2.0f, x + 1.1f, y - 2.0f, 0.26f, EdaLayerId::InnerCopper, netId });
                out.traces.push_back({ x - 1.1f, y - 2.6f, x + 1.1f, y - 2.6f, 0.26f, EdaLayerId::InnerCopper, netId });
                out.traces.push_back({ x, y - 2.6f, x, y - 3.6f, 0.18f, EdaLayerId::BottomSilk, 0 });
                out.traces.push_back({ x - 0.8f, y - 3.6f, x + 0.8f, y - 3.6f, 0.20f, EdaLayerId::BottomSilk, 0 });
                wchar_t cTxt[48] = {};
                swprintf_s(cTxt, L"%.1f%hs", cit->second, cUnit.c_str());
                out.labels.push_back({ x + 1.3f, y - 2.5f, 0.65f, EdaLayerId::InnerCopper, cTxt });
            }
        }

        // Emit parasitic resistors along wire segments
        for (const auto& r : net.resistors) {
            auto i1 = globalNodePos.find(r.n1);
            auto i2 = globalNodePos.find(r.n2);
            if (i1 == globalNodePos.end() || i2 == globalNodePos.end()) continue;
            const float x1 = i1->second.first, y1 = i1->second.second;
            const float x2 = i2->second.first, y2 = i2->second.second;
            const float mx = (x1 + x2) * 0.5f, my = (y1 + y2) * 0.5f;
            out.traces.push_back({ x1, y1, x2, y2, 0.28f, EdaLayerId::TopCopper, netId });
            DrawEdaBoxRect(out, mx - 1.6f, my - 0.55f, mx + 1.6f, my + 0.55f, 0.20f, EdaLayerId::TopSilk, netId);
            wchar_t rTxt[48] = {};
            swprintf_s(rTxt, L"%.1f%hs", r.val, rUnit.c_str());
            out.labels.push_back({ mx - 1.5f, my + 1.1f, 0.65f, EdaLayerId::TopSilk, rTxt });
        }
    }

    // Emit cross-coupled capacitors (*CAP idx n1 n2 val) across nets/nodes
    for (size_t ni = 0; ni < showNets; ++ni) {
        for (const auto& cc : nets[ni].coupleCaps) {
            auto i1 = globalNodePos.find(cc.n1);
            auto i2 = globalNodePos.find(cc.n2);
            if (i1 != globalNodePos.end() && i2 != globalNodePos.end()) {
                out.traces.push_back({
                    i1->second.first, i1->second.second,
                    i2->second.first, i2->second.second,
                    0.20f, EdaLayerId::BottomCopper, static_cast<int>(ni + 1)
                });
            }
        }
    }

    if (out.traces.empty() && out.components.empty()) return false;
    out.netCount = nets.size();
    wchar_t title[200] = {};
    swprintf_s(title, L"IEEE 1481 SPEF [%hs] (%zu D_NETs, %zu R, %zu C)",
        designName.c_str(), nets.size(), totalRes, totalCap);
    out.formatTitle = title;
    return true;
}

} // namespace Formats
