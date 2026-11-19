#pragma once
// VectorCanDbc.h — Vector CAN Database (.dbc) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

inline bool ParseVectorCanDbcBoard(const uint8_t* data, size_t size, const std::wstring& extLower, ParsedEdaBoard& out) {
    if (!data || size < 8) return false;
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    // 2. Vector CAN Database (.dbc)
    if (extLower == L".dbc" || (extLower != L".asc" && sv.find("BO_ ") != std::string_view::npos)) {
        struct DbcSig {
            std::string name;
            int startBit = 0;
            int bitSize = 8;
            bool littleEndian = true;
            double scale = 1.0, offset = 0.0;
            std::string unit;
        };
        struct DbcMsg {
            uint32_t canId = 0;
            bool extId = false;
            int dlc = 8;
            std::string name;
            std::string txNode;
            std::vector<DbcSig> sigs;
        };
        std::vector<DbcMsg> msgs;

        size_t pos = 0;
        while (pos < sv.size() && msgs.size() < 24) {
            size_t eol = sv.find('\n', pos);
            if (eol == std::string_view::npos) eol = sv.size();
            std::string_view ln = sv.substr(pos, eol - pos);
            pos = eol + 1;
            while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
            while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.remove_suffix(1);
            if (ln.empty()) continue;

            if (ln.rfind("BO_ ", 0) == 0) {
                unsigned int rawId = 0;
                char msgName[64] = {}, txNode[64] = {};
                int dlc = 8;
                std::string tmp(ln.substr(4));
                if (sscanf_s(tmp.c_str(), "%u %63[^:]: %d %63s",
                    &rawId, msgName, static_cast<unsigned>(sizeof(msgName)),
                    &dlc, txNode, static_cast<unsigned>(sizeof(txNode))) >= 2)
                {
                    DbcMsg m;
                    m.extId = (rawId & 0x80000000u) != 0;
                    m.canId = rawId & 0x1FFFFFFFu;
                    m.dlc = std::clamp(dlc, 1, 64);
                    m.name = DetailEda100::TrimAsciiView(msgName);
                    m.txNode = txNode;
                    msgs.push_back(std::move(m));
                }
            } else if (ln.rfind("SG_ ", 0) == 0 && !msgs.empty()) {
                size_t colon = ln.find(':');
                if (colon != std::string_view::npos) {
                    std::string_view hdrPart = ln.substr(4, colon - 4);
                    size_t nameEnd = hdrPart.find_first_of(" \t");
                    std::string sigName = DetailEda100::TrimAsciiView(hdrPart.substr(0, nameEnd));
                    std::string spec(ln.substr(colon + 1));

                    int startBit = 0, bitSize = 8;
                    char byteOrd = '1', signCh = '+';
                    double sc = 1.0, off = 0.0;
                    if (sscanf_s(spec.c_str(), " %d|%d@%c%c (%lf,%lf)",
                        &startBit, &bitSize, &byteOrd, 1, &signCh, 1, &sc, &off) >= 4)
                    {
                        DbcSig s;
                        s.name = sigName;
                        s.startBit = std::clamp(startBit, 0, 511);
                        s.bitSize = std::clamp(bitSize, 1, 512);
                        s.littleEndian = (byteOrd == '1');
                        s.scale = sc;
                        s.offset = off;
                        size_t q1 = spec.find('"');
                        if (q1 != std::string::npos) {
                            size_t q2 = spec.find('"', q1 + 1);
                            if (q2 != std::string::npos) s.unit = spec.substr(q1 + 1, q2 - q1 - 1);
                        }
                        msgs.back().sigs.push_back(std::move(s));
                    }
                }
            }
        }

        if (msgs.empty()) return false;

        out.kind = EdaDocKind::KiCadSch;
        size_t totalSigs = 0;
        for (const auto& m : msgs) totalSigs += m.sigs.size();
        out.formatTitle = L"Vector CAN Database (.dbc — " + std::to_wstring(msgs.size()) +
                          L" msgs, " + std::to_wstring(totalSigs) + L" signals)";

        // Differential CAN_H / CAN_L bus rails at top
        out.traces.push_back({ -48.0f, 30.0f, 48.0f, 30.0f, 0.42f, EdaLayerId::TopCopper, 1 });
        out.traces.push_back({ -48.0f, 27.5f, 48.0f, 27.5f, 0.42f, EdaLayerId::BottomCopper, 2 });
        out.labels.push_back({ -44.0f, 31.6f, 1.0f, EdaLayerId::SchLabel, L"CAN_H / CAN_L Differential Bus" });

        for (size_t mi = 0; mi < msgs.size(); ++mi) {
            const auto& m = msgs[mi];
            float yRow = 18.0f - static_cast<float>(mi) * 11.0f;
            int totalBits = std::max(8, m.dlc * 8);

            wchar_t idHex[48] = {};
            swprintf_s(idHex, m.extId ? L"0x%08X [%dB]" : L"0x%03X [%dB]", m.canId, m.dlc);

            EdaComponent3D node;
            node.refDes = PathUtils::Utf8ToWide(m.name);
            node.value = idHex;
            node.footprint = PathUtils::Utf8ToWide(m.txNode.empty() ? "CAN_ECU" : m.txNode);
            node.x = -36.0f;
            node.y = yRow;
            node.widthMm = 18.0f;
            node.lengthMm = 6.5f;
            node.heightMm = 1.5f;
            node.category = EdaPartCategory::IC_Package;
            out.components.push_back(node);
            DetailEda100::AddRectOutline(out, node.x, node.y, node.widthMm, node.lengthMm, EdaLayerId::SchSymbol, 0.28f);
            out.labels.push_back({ node.x, yRow + 1.0f, 1.0f, EdaLayerId::SchLabel, node.refDes });
            out.labels.push_back({ node.x, yRow - 1.5f, 0.85f, EdaLayerId::TopSilk, idHex });

            // Bitfield payload bar (-22.0f .. +46.0f = 68.0 mm for `totalBits`)
            const float barX0 = -22.0f, barW = 68.0f, barH = 5.4f;
            DetailEda100::AddRectOutline(out, barX0 + barW * 0.5f, yRow, barW, barH, EdaLayerId::EdgeCuts, 0.22f);
            for (int b = 1; b < m.dlc && b < 16; ++b) {
                float bx = barX0 + (static_cast<float>(b * 8) / static_cast<float>(totalBits)) * barW;
                out.traces.push_back({ bx, yRow - barH * 0.5f, bx, yRow + barH * 0.5f, 0.10f, EdaLayerId::BottomSilk, 0 });
            }

            for (size_t si = 0; si < m.sigs.size(); ++si) {
                const auto& s = m.sigs[si];
                int effStart = s.littleEndian ? s.startBit : std::max(0, s.startBit - s.bitSize + 1);
                float sx0 = barX0 + (static_cast<float>(std::min(effStart, totalBits - 1)) / static_cast<float>(totalBits)) * barW;
                float sw  = std::max(1.2f, (static_cast<float>(std::min(s.bitSize, totalBits)) / static_cast<float>(totalBits)) * barW - 0.3f);
                sx0 = std::min(sx0, barX0 + barW - sw);
                float scx = sx0 + sw * 0.5f;

                EdaLayerId slid = (si % 2 == 0) ? EdaLayerId::TopCopper : EdaLayerId::SchWire;
                DetailEda100::AddRectOutline(out, scx, yRow, sw, barH - 1.0f, slid, 0.30f);
                out.pads.push_back({ scx, yRow, std::max(0.8f, sw - 0.6f), barH - 1.8f, 0.0f, 0.0f, 1,
                    s.littleEndian ? EdaLayerId::TopCopper : EdaLayerId::BottomCopper,
                    PathUtils::Utf8ToWide(s.name), PathUtils::Utf8ToWide(s.unit) });

                wchar_t sigLbl[96] = {};
                swprintf_s(sigLbl, L"%s [%d:%d%s *%.3g%+g]",
                    PathUtils::Utf8ToWide(s.name).c_str(), s.startBit, s.bitSize,
                    s.littleEndian ? L"LE" : L"BE", s.scale, s.offset);
                out.labels.push_back({ scx, yRow + ((si % 2 == 0) ? 3.5f : -3.6f), 0.8f, EdaLayerId::SchLabel, sigLbl });
            }
        }
        out.netCount = msgs.size() + 2;
        return true;
    }
    return false;
}

} // namespace Formats
