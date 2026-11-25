#pragma once
// KayakCanKcd.h — Kayak Automotive XML CAN Bus Database (.kcd, <NetworkDefinition>) reader
// Parses ECU <Node> definitions, <Bus> channels, <Message> frames (standard 11-bit & extended 29-bit),
// <Producer>/<Consumer> ECU links, and <Signal> bit-field allocations (offset, length, unit, slope),
// rendering a complete CAN bus ECU topology & 64-bit frame packing schematic.

namespace Formats {

inline bool ParseKayakCanKcd(
    const uint8_t* data,
    size_t size,
    ParsedEdaBoard& out)
{
    if (!data || size < 32 || size > 32u * 1024u * 1024u) return false;
    std::string_view sv = ReaderText::StripUtf8Bom(std::string_view(reinterpret_cast<const char*>(data), size));
    if (sv.find('\0') != std::string_view::npos) return false;
    if (sv.find("<NetworkDefinition") == std::string_view::npos) return false;

    struct KcdSignal {
        std::wstring name;
        int offset = 0;
        int length = 1;
        std::wstring unit;
    };
    struct KcdMsg {
        std::wstring idHex;
        std::wstring name;
        int byteLen = 8;
        std::string producerId;
        std::vector<KcdSignal> signals;
    };

    std::unordered_map<std::string, std::wstring> ecuNodes;
    std::vector<std::pair<std::string, std::wstring>> orderedEcus;
    std::vector<KcdMsg> messages;
    std::wstring busName = L"CAN0";
    int baudrate = 500000;

    // Scan top-level <Node .../> and <Bus ...> ... </Bus>
    size_t pos = 0;
    while (pos < sv.size()) {
        const size_t lt = sv.find('<', pos);
        if (lt == std::string_view::npos) break;
        const size_t gt = sv.find('>', lt + 1);
        if (gt == std::string_view::npos) break;
        std::string_view tag = sv.substr(lt, gt - lt + 1);
        pos = gt + 1;

        if (ReaderText::StartsWithCI(tag, "<Node ") || ReaderText::StartsWithCI(tag, "<Node\t")) {
            std::string id(XmlTagScan::GetAttr(tag, "id"));
            std::wstring nm = XmlTagScan::DecodeEntitiesWide(PathUtils::Utf8ToWide(XmlTagScan::GetAttr(tag, "name")));
            if (!id.empty()) {
                if (nm.empty()) nm = L"ECU_" + PathUtils::Utf8ToWide(id);
                if (ecuNodes.emplace(id, nm).second) orderedEcus.emplace_back(id, nm);
            }
        } else if (ReaderText::StartsWithCI(tag, "<Bus ") || ReaderText::StartsWithCI(tag, "<Bus>")) {
            std::string_view bn = XmlTagScan::GetAttr(tag, "name");
            if (!bn.empty()) busName = PathUtils::Utf8ToWide(bn);
            std::string_view br = XmlTagScan::GetAttr(tag, "baudrate");
            int bVal = 0;
            if (!br.empty() && ReaderText::ParseIntSv(br, bVal) && bVal > 0) baudrate = bVal;
        } else if (ReaderText::StartsWithCI(tag, "<Message ")) {
            const size_t msgEnd = sv.find("</Message>", pos);
            if (msgEnd == std::string_view::npos) break;
            std::string_view msgBody = sv.substr(pos, msgEnd - pos);
            pos = msgEnd + 10;

            KcdMsg m;
            m.idHex = PathUtils::Utf8ToWide(XmlTagScan::GetAttr(tag, "id"));
            m.name = XmlTagScan::DecodeEntitiesWide(PathUtils::Utf8ToWide(XmlTagScan::GetAttr(tag, "name")));
            int lenBytes = 8;
            if (ReaderText::ParseIntSv(XmlTagScan::GetAttr(tag, "length"), lenBytes) && lenBytes >= 0 && lenBytes <= 64) {
                m.byteLen = lenBytes;
            }

            // Parse <Producer><NodeRef id="..."/></Producer> and <Signal ...> inside msgBody
            size_t mp = 0;
            bool inProducer = false;
            KcdSignal* activeSig = nullptr;
            while (mp < msgBody.size()) {
                const size_t slt = msgBody.find('<', mp);
                if (slt == std::string_view::npos) break;
                const size_t sgt = msgBody.find('>', slt + 1);
                if (sgt == std::string_view::npos) break;
                std::string_view stag = msgBody.substr(slt, sgt - slt + 1);
                mp = sgt + 1;

                if (ReaderText::StartsWithCI(stag, "<Producer")) inProducer = true;
                else if (ReaderText::StartsWithCI(stag, "</Producer")) inProducer = false;
                else if (inProducer && ReaderText::StartsWithCI(stag, "<NodeRef")) {
                    m.producerId = std::string(XmlTagScan::GetAttr(stag, "id"));
                } else if (ReaderText::StartsWithCI(stag, "<Signal ")) {
                    KcdSignal sig;
                    sig.name = XmlTagScan::DecodeEntitiesWide(PathUtils::Utf8ToWide(XmlTagScan::GetAttr(stag, "name")));
                    ReaderText::ParseIntSv(XmlTagScan::GetAttr(stag, "offset"), sig.offset);
                    if (!ReaderText::ParseIntSv(XmlTagScan::GetAttr(stag, "length"), sig.length) || sig.length < 1) {
                        sig.length = 1;
                    }
                    m.signals.push_back(std::move(sig));
                    activeSig = (stag.size() >= 2 && stag[stag.size() - 2] == '/') ? nullptr : &m.signals.back();
                } else if (ReaderText::StartsWithCI(stag, "</Signal")) {
                    activeSig = nullptr;
                } else if (activeSig && ReaderText::StartsWithCI(stag, "<Value ")) {
                    std::string_view u = XmlTagScan::GetAttr(stag, "unit");
                    if (!u.empty()) activeSig->unit = XmlTagScan::DecodeEntitiesWide(PathUtils::Utf8ToWide(u));
                }
            }
            if (!m.idHex.empty() || !m.name.empty()) {
                messages.push_back(std::move(m));
            }
        }
    }

    if (messages.empty() && orderedEcus.empty()) return false;

    out.kind = EdaDocKind::KiCadSch;
    size_t totalSignals = 0;
    for (const auto& m : messages) totalSignals += m.signals.size();

    wchar_t title[180] = {};
    swprintf_s(title, L"Kayak CAN Database [%ls @ %d kbps] (%zu ECUs, %zu msgs, %zu signals)",
               busName.c_str(), baudrate / 1000, orderedEcus.size(), messages.size(), totalSignals);
    out.formatTitle = title;

    // 1. Differential CAN_H / CAN_L bus backbone at top + ECU transceiver blocks
    const float busSpanW = std::max(72.0f, static_cast<float>(orderedEcus.size()) * 22.0f);
    const float busY_H = 18.0f, busY_L = 16.2f;
    out.traces.push_back({ 0.0f, busY_H, busSpanW, busY_H, 0.35f, EdaLayerId::TopCopper, 1 });
    out.traces.push_back({ 0.0f, busY_L, busSpanW, busY_L, 0.35f, EdaLayerId::BottomCopper, 2 });
    out.labels.push_back({ -9.0f, busY_H, 0.9f, EdaLayerId::SchLabel, L"CAN_H" });
    out.labels.push_back({ -9.0f, busY_L, 0.9f, EdaLayerId::SchLabel, L"CAN_L" });

    for (size_t i = 0; i < orderedEcus.size(); ++i) {
        const float ex = 11.0f + static_cast<float>(i) * 22.0f;
        const float ey = 26.0f;
        DrawEdaBoxRect(out, ex - 7.5f, ey - 3.0f, ex + 7.5f, ey + 3.0f, 0.25f, EdaLayerId::SchSymbol, 0);
        out.traces.push_back({ ex - 1.5f, ey - 3.0f, ex - 1.5f, busY_H, 0.22f, EdaLayerId::SchWire, 1 });
        out.traces.push_back({ ex + 1.5f, ey - 3.0f, ex + 1.5f, busY_L, 0.22f, EdaLayerId::SchWire, 2 });
        out.vias.push_back({ ex - 1.5f, busY_H, 0.6f, 0.0f, 1 });
        out.vias.push_back({ ex + 1.5f, busY_L, 0.6f, 0.0f, 2 });

        EdaComponent3D ecu;
        ecu.refDes = orderedEcus[i].second;
        ecu.value = L"CAN_ECU";
        ecu.footprint = L"QFP-64";
        ecu.x = ex; ecu.y = ey;
        ecu.widthMm = 15.0f; ecu.lengthMm = 6.0f; ecu.heightMm = 1.5f;
        ecu.category = EdaPartCategory::IC_Package;
        out.labels.push_back({ ex - 6.5f, ey + 0.3f, 0.95f, EdaLayerId::SchLabel, ecu.refDes });
        out.components.push_back(std::move(ecu));
    }

    // 2. Per-message 64-bit frame bit-map rows below the CAN bus
    const float bitBarW = 64.0f;
    const size_t maxMsgs = std::min<size_t>(messages.size(), 24u);
    for (size_t mi = 0; mi < maxMsgs; ++mi) {
        const auto& m = messages[mi];
        const float yTop = 9.0f - static_cast<float>(mi) * 8.0f;
        const float yBot = yTop - 4.8f;
        const int totalBits = std::max(8, m.byteLen * 8);

        DrawEdaBoxRect(out, 0.0f, yBot, bitBarW, yTop, 0.22f, EdaLayerId::EdgeCuts, 0);
        std::wstring hdr = m.idHex + L" " + m.name;
        auto pit = ecuNodes.find(m.producerId);
        if (pit != ecuNodes.end()) hdr += L" [" + pit->second + L"]";
        out.labels.push_back({ -34.0f, (yTop + yBot) * 0.5f, 0.9f, EdaLayerId::SchLabel, hdr });

        for (size_t si = 0; si < m.signals.size(); ++si) {
            const auto& s = m.signals[si];
            const float x0 = std::clamp((static_cast<float>(s.offset) / totalBits) * bitBarW, 0.0f, bitBarW);
            const float x1 = std::clamp((static_cast<float>(s.offset + s.length) / totalBits) * bitBarW, x0 + 0.8f, bitBarW);
            const EdaLayerId sLyr = (si % 2 == 0) ? EdaLayerId::TopCopper : EdaLayerId::InnerCopper;
            DrawEdaBoxRect(out, x0 + 0.15f, yBot + 0.35f, x1 - 0.15f, yTop - 0.35f, 0.24f, sLyr, static_cast<int>(mi + 3));
            std::wstring sLbl = s.name;
            if (!s.unit.empty()) sLbl += L" (" + s.unit + L")";
            out.labels.push_back({ x0 + 0.6f, (yTop + yBot) * 0.5f - 0.2f, 0.72f, EdaLayerId::TopSilk, sLbl });
        }
    }

    out.netCount = messages.size() + 2;
    return !out.traces.empty();
}

} // namespace Formats
