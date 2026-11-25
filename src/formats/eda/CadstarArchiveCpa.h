#pragma once
// CadstarArchiveCpa.h — Zuken CADSTAR PCB Design Archive (.cpa) reader
// Reuses DetailSpecctra::ParseSExprTree to parse CADSTAR S-expression PCB archives:
// HEADER/RESOLUTION, LAYERDEFS, BOARD outline, COMP placements, and NET/ROUTE/TRACK/VIA copper.

namespace Formats {

namespace DetailCadstarCpa {

inline bool AtomEqCI(const DetailSpecctra::SNode& n, std::string_view kw) {
    return !n.isList && ReaderText::AsciiEqCI(n.atom, kw);
}

inline const DetailSpecctra::SNode* FindSubList(const DetailSpecctra::SNode& parent, std::string_view headKw) {
    for (const auto& ch : parent.list) {
        if (ch.isList && !ch.list.empty() && AtomEqCI(ch.list[0], headKw)) return &ch;
    }
    return nullptr;
}

inline bool ExtractPtXY(const DetailSpecctra::SNode& ptNode, float scaleMm, float& outX, float& outY) {
    if (!ptNode.isList || ptNode.list.size() < 3 || !AtomEqCI(ptNode.list[0], "PT")) return false;
    if (ptNode.list[1].isList || ptNode.list[2].isList) return false;
    float rx = 0, ry = 0;
    if (!ReaderText::ParseFloatSv(ptNode.list[1].atom, rx) ||
        !ReaderText::ParseFloatSv(ptNode.list[2].atom, ry)) {
        return false;
    }
    outX = rx * scaleMm;
    outY = ry * scaleMm;
    return true;
}

inline void EmitPolylineFromNode(
    const DetailSpecctra::SNode& node,
    float scaleMm,
    float widthMm,
    EdaLayerId layer,
    int netId,
    bool closeLoop,
    ParsedEdaBoard& out)
{
    std::vector<std::pair<float, float>> pts;
    auto collectPts = [&](auto& self, const DetailSpecctra::SNode& cur) -> void {
        float px = 0, py = 0;
        if (ExtractPtXY(cur, scaleMm, px, py)) {
            pts.emplace_back(px, py);
            return;
        }
        for (const auto& ch : cur.list) {
            if (ch.isList) self(self, ch);
        }
    };
    collectPts(collectPts, node);
    if (pts.size() < 2) return;
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        out.traces.push_back({ pts[i].first, pts[i].second, pts[i + 1].first, pts[i + 1].second, widthMm, layer, netId });
    }
    if (closeLoop && pts.size() >= 3 &&
        (std::fabs(pts.front().first - pts.back().first) > 1e-4f || std::fabs(pts.front().second - pts.back().second) > 1e-4f)) {
        out.traces.push_back({ pts.back().first, pts.back().second, pts.front().first, pts.front().second, widthMm, layer, netId });
    }
}

} // namespace DetailCadstarCpa

inline bool ParseCadstarArchiveCpa(
    const uint8_t* data,
    size_t size,
    ParsedEdaBoard& out)
{
    if (!data || size < 24 || size > 64u * 1024u * 1024u) return false;
    std::string_view sv = ReaderText::StripUtf8Bom(std::string_view(reinterpret_cast<const char*>(data), size));
    if (sv.find('\0') != std::string_view::npos) return false;

    // CADSTAR archives may have leading title lines before the first '(' S-expression block
    const size_t firstParen = sv.find('(');
    if (firstParen == std::string_view::npos) return false;
    std::string_view preamble = sv.substr(0, firstParen);
    const bool hasPreambleMagic = (preamble.find("CADSTAR") != std::string_view::npos ||
                                   preamble.find("CSA_PCB") != std::string_view::npos);

    std::vector<DetailSpecctra::SNode> roots;
    if (!DetailSpecctra::ParseSExprTree(sv.substr(firstParen), roots) || roots.empty()) return false;

    bool sawCadstarRoot = hasPreambleMagic;
    float scaleMm = 0.001f; // Default CADSTAR metric micron/CSU (1/1000 mm)
    std::unordered_map<std::string, EdaLayerId> layerMap;
    std::unordered_map<std::string, float> widthCodeMm;
    size_t netCount = 0;

    auto mapLayerId = [&](const std::string& lid) -> EdaLayerId {
        auto it = layerMap.find(ReaderText::ToUpperAscii(lid));
        return (it != layerMap.end()) ? it->second : EdaLayerId::TopCopper;
    };

    auto walkSection = [&](auto& self, const DetailSpecctra::SNode& n) -> void {
        if (!n.isList || n.list.empty() || n.list[0].isList) return;
        const std::string_view head = n.list[0].atom;

        if (ReaderText::StartsWithCI(head, "CADSTAR") || ReaderText::AsciiEqCI(head, "CSA_PCB")) {
            sawCadstarRoot = true;
            for (const auto& ch : n.list) if (ch.isList) self(self, ch);
            return;
        }
        if (ReaderText::AsciiEqCI(head, "HEADER") || ReaderText::AsciiEqCI(head, "ASSIGNMENTS") ||
            ReaderText::AsciiEqCI(head, "LAYERDEFS") || ReaderText::AsciiEqCI(head, "CODEDEFS") ||
            ReaderText::AsciiEqCI(head, "LAYOUT") || ReaderText::AsciiEqCI(head, "NETS") ||
            ReaderText::AsciiEqCI(head, "COMPONENTS")) {
            for (const auto& ch : n.list) if (ch.isList) self(self, ch);
            return;
        }

        if (ReaderText::AsciiEqCI(head, "RESOLUTION") && n.list.size() >= 3) {
            // (RESOLUTION MM 1000) or (RESOLUTION INCH 10000) or (RESOLUTION MICRON 1)
            const std::string_view unit = n.list[1].atom;
            float div = 1.0f;
            if (ReaderText::ParseFloatSv(n.list[2].atom, div) && div > 0.0f) {
                if (ReaderText::StartsWithCI(unit, "MM") || ReaderText::StartsWithCI(unit, "MILLI")) scaleMm = 1.0f / div;
                else if (ReaderText::StartsWithCI(unit, "MICRON") || ReaderText::AsciiEqCI(unit, "UM")) scaleMm = 0.001f / div;
                else if (ReaderText::StartsWithCI(unit, "IN")) scaleMm = 25.4f / div;
                else if (ReaderText::StartsWithCI(unit, "MIL") || ReaderText::AsciiEqCI(unit, "THOU")) scaleMm = 0.0254f / div;
            }
        } else if (ReaderText::AsciiEqCI(head, "LAYER") && n.list.size() >= 3) {
            const std::string id = ReaderText::ToUpperAscii(n.list[1].atom);
            const std::string nm = ReaderText::ToLowerAscii(n.list[2].atom);
            EdaLayerId mapped = EdaLayerId::InnerCopper;
            if (nm.find("top") != std::string::npos || nm.find("comp") != std::string::npos) {
                mapped = (nm.find("silk") != std::string::npos || nm.find("doc") != std::string::npos)
                    ? EdaLayerId::TopSilk : EdaLayerId::TopCopper;
            } else if (nm.find("bot") != std::string::npos || nm.find("solder") != std::string::npos || nm.find("elec") != std::string::npos) {
                mapped = (nm.find("silk") != std::string::npos) ? EdaLayerId::BottomSilk : EdaLayerId::BottomCopper;
            } else if (nm.find("outline") != std::string::npos || nm.find("board") != std::string::npos) {
                mapped = EdaLayerId::EdgeCuts;
            }
            layerMap[id] = mapped;
        } else if (ReaderText::AsciiEqCI(head, "ROUTECODE") && n.list.size() >= 3) {
            float w = 0.0f;
            if (ReaderText::ParseFloatSv(n.list[2].atom, w) && w > 0.0f) {
                widthCodeMm[ReaderText::ToUpperAscii(n.list[1].atom)] = std::clamp(w * scaleMm, 0.10f, 5.0f);
            }
        } else if (ReaderText::AsciiEqCI(head, "BOARD")) {
            DetailCadstarCpa::EmitPolylineFromNode(n, scaleMm, 0.28f, EdaLayerId::EdgeCuts, 0, true, out);
        } else if ((ReaderText::AsciiEqCI(head, "COMP") || ReaderText::AsciiEqCI(head, "COMPSYM")) && n.list.size() >= 3) {
            // (COMP <id> <refDes> [<partName>] ... (PT x y) [(ORIENT deg)] [(MIRROR)])
            std::string ref = n.list[2].isList ? n.list[1].atom : n.list[2].atom;
            std::string val = (n.list.size() >= 4 && !n.list[3].isList) ? n.list[3].atom : ref;
            float cx = 0.0f, cy = 0.0f, rot = 0.0f;
            bool hasPt = false, mirrored = false;
            std::vector<std::pair<float, float>> relPins;

            for (const auto& ch : n.list) {
                if (!ch.isList || ch.list.empty()) continue;
                if (DetailCadstarCpa::ExtractPtXY(ch, scaleMm, cx, cy)) hasPt = true;
                else if (DetailCadstarCpa::AtomEqCI(ch.list[0], "ORIENT") && ch.list.size() >= 2) {
                    ReaderText::ParseFloatSv(ch.list[1].atom, rot);
                } else if (DetailCadstarCpa::AtomEqCI(ch.list[0], "MIRROR")) {
                    mirrored = true;
                } else if (DetailCadstarCpa::AtomEqCI(ch.list[0], "PAD") || DetailCadstarCpa::AtomEqCI(ch.list[0], "PIN")) {
                    for (const auto& sub : ch.list) {
                        float px = 0, py = 0;
                        if (DetailCadstarCpa::ExtractPtXY(sub, scaleMm, px, py)) relPins.emplace_back(px, py);
                    }
                }
            }
            if (hasPt) {
                EdaComponent3D c;
                c.refDes = PathUtils::Utf8ToWide(ref);
                c.value = PathUtils::Utf8ToWide(val);
                c.footprint = c.value;
                c.x = cx; c.y = cy;
                c.rotDeg = rot;
                c.bottomSide = mirrored;
                c.category = ClassifyComponent(c.refDes, c.footprint, c.widthMm, c.lengthMm, c.heightMm, c.pinCount);
                if (!relPins.empty()) {
                    c.pinCount = static_cast<int>(relPins.size());
                    for (const auto& rp : relPins) {
                        EdaPadPrim p;
                        p.x = rp.first; p.y = rp.second;
                        p.w = 1.1f; p.h = 1.1f; p.shape = 1;
                        p.layer = mirrored ? EdaLayerId::BottomCopper : EdaLayerId::TopCopper;
                        out.pads.push_back(std::move(p));
                    }
                } else {
                    const float dx = c.widthMm * 0.35f;
                    for (int s : { -1, 1 }) {
                        EdaPadPrim p;
                        p.x = cx + s * dx; p.y = cy;
                        p.w = 0.95f; p.h = 1.15f; p.shape = 1;
                        p.layer = mirrored ? EdaLayerId::BottomCopper : EdaLayerId::TopCopper;
                        out.pads.push_back(std::move(p));
                    }
                }
                out.labels.push_back({ cx, cy + c.lengthMm * 0.55f, 0.9f,
                                       mirrored ? EdaLayerId::BottomSilk : EdaLayerId::TopSilk, c.refDes });
                out.components.push_back(std::move(c));
            }
        } else if (ReaderText::AsciiEqCI(head, "NET")) {
            const int nid = static_cast<int>(++netCount);
            for (const auto& ch : n.list) {
                if (!ch.isList || ch.list.empty() || ch.list[0].isList) continue;
                const std::string_view subKw = ch.list[0].atom;
                if (ReaderText::AsciiEqCI(subKw, "TRACK") || ReaderText::AsciiEqCI(subKw, "ROUTE")) {
                    EdaLayerId lyr = EdaLayerId::TopCopper;
                    float wMm = 0.25f;
                    if (ch.list.size() >= 2 && !ch.list[1].isList) lyr = mapLayerId(ch.list[1].atom);
                    if (ch.list.size() >= 3 && !ch.list[2].isList) {
                        auto wit = widthCodeMm.find(ReaderText::ToUpperAscii(ch.list[2].atom));
                        if (wit != widthCodeMm.end()) wMm = wit->second;
                        else {
                            float rawW = 0;
                            if (ReaderText::ParseFloatSv(ch.list[2].atom, rawW) && rawW > 0) {
                                wMm = std::clamp(rawW * scaleMm, 0.10f, 5.0f);
                            }
                        }
                    }
                    DetailCadstarCpa::EmitPolylineFromNode(ch, scaleMm, wMm, lyr, nid, false, out);
                } else if (ReaderText::AsciiEqCI(subKw, "VIA")) {
                    for (const auto& sub : ch.list) {
                        float vx = 0, vy = 0;
                        if (DetailCadstarCpa::ExtractPtXY(sub, scaleMm, vx, vy)) {
                            out.vias.push_back({ vx, vy, 0.75f, 0.35f, nid });
                        }
                    }
                }
            }
        }
    };

    for (const auto& r : roots) walkSection(walkSection, r);

    if (!sawCadstarRoot) return false;
    if (out.traces.empty() && out.pads.empty() && out.components.empty() && out.vias.empty()) return false;

    out.kind = EdaDocKind::KiCadPcb;
    out.netCount = netCount;
    wchar_t title[144] = {};
    swprintf_s(title, L"Zuken CADSTAR PCB Archive (%zu parts, %zu nets, %zu tracks)",
               out.components.size(), out.netCount, out.traces.size());
    out.formatTitle = title;
    return true;
}

} // namespace Formats
