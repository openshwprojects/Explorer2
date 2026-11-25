#pragma once
// SynopsysItfStackup.h — Synopsys StarRC Interconnect Technology Format (.itf)
// Parses `TECHNOLOGY`, `DIELECTRIC`, `CONDUCTOR`, and `VIA` process stackup blocks,
// rendering a Semiconductor BEOL Metal/Dielectric Cross-Section & Via Pillar Stackup diagram.

namespace Formats {

namespace DetailSynopsysItf {

struct ItfLayer {
    bool isConductor = false;
    std::string name;
    double thicknessUm = 0.1;
    double er   = 3.9;
    double wMin = 0.0;
    double rpsq = 0.0;
};

struct ItfVia {
    std::string name;
    std::string fromLayer;
    std::string toLayer;
    double rpv = 0.0;
};

} // namespace DetailSynopsysItf

inline bool ParseSynopsysItfStackup(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    std::string_view sv;
    if (!FormatPrologue::PrepareUtf8TextNoNul(data, size, sv, 24u, (16u << 20))) return false;

    // Strip `$` or `$$` or `//` or `/* ... */` comments and tokenize
    std::vector<std::string> toks;
    int braceDepth = 0;
    for (size_t i = 0u; i < sv.size();) {
        const unsigned char c = static_cast<unsigned char>(sv[i]);
        if (c <= 0x20u) { ++i; continue; }
        if (c == '$' || (c == '/' && i + 1u < sv.size() && sv[i + 1u] == '/')) {
            while (i < sv.size() && sv[i] != '\n') ++i;
            continue;
        }
        if (c == '/' && i + 1u < sv.size() && sv[i + 1u] == '*') {
            i += 2u;
            bool closed = false;
            while (i + 1u < sv.size()) {
                if (sv[i] == '*' && sv[i + 1u] == '/') { i += 2u; closed = true; break; }
                ++i;
            }
            if (!closed) return false;
            continue;
        }
        if (c == '{' || c == '}' || c == '=') {
            if (c == '{') ++braceDepth;
            else if (c == '}') { if (--braceDepth < 0) return false; }
            toks.emplace_back(1u, static_cast<char>(c));
            ++i;
            continue;
        }
        const size_t s = i;
        while (i < sv.size()) {
            const unsigned char ch = static_cast<unsigned char>(sv[i]);
            if (ch <= 0x20u || ch == '{' || ch == '}' || ch == '=' || ch == '$') break;
            ++i;
        }
        toks.emplace_back(sv.substr(s, i - s));
    }
    if (braceDepth != 0 || toks.size() < 6u) return false;

    std::string techName;
    std::vector<DetailSynopsysItf::ItfLayer> layers;
    std::vector<DetailSynopsysItf::ItfVia>   vias;

    for (size_t i = 0u; i < toks.size(); ++i) {
        const std::string up = ReaderText::ToUpperAscii(toks[i]);
        if (up == "TECHNOLOGY") {
            if (i + 2u >= toks.size() || toks[i + 1u] != "=") return false;
            techName = toks[i + 2u];
            i += 2u;
        } else if (up == "DIELECTRIC" || up == "CONDUCTOR") {
            if (i + 2u >= toks.size() || toks[i + 2u] != "{") return false;
            DetailSynopsysItf::ItfLayer lyr{};
            lyr.isConductor = (up == "CONDUCTOR");
            lyr.name = toks[i + 1u];
            size_t p = i + 3u;
            while (p < toks.size() && toks[p] != "}") {
                if (p + 2u < toks.size() && toks[p + 1u] == "=") {
                    const std::string k = ReaderText::ToUpperAscii(toks[p]);
                    double v = 0.0;
                    if (ReaderText::ParseDoubleSv(toks[p + 2u], v) && std::isfinite(v)) {
                        if (k == "THICKNESS") lyr.thicknessUm = std::max(0.001, v);
                        else if (k == "ER")   lyr.er = v;
                        else if (k == "WMIN") lyr.wMin = v;
                        else if (k == "RPSQ") lyr.rpsq = v;
                    }
                    p += 3u;
                } else {
                    ++p;
                }
            }
            if (p >= toks.size()) return false;
            layers.push_back(std::move(lyr));
            i = p;
        } else if (up == "VIA") {
            if (i + 2u >= toks.size() || toks[i + 2u] != "{") return false;
            DetailSynopsysItf::ItfVia via{};
            via.name = toks[i + 1u];
            size_t p = i + 3u;
            while (p < toks.size() && toks[p] != "}") {
                if (p + 2u < toks.size() && toks[p + 1u] == "=") {
                    const std::string k = ReaderText::ToUpperAscii(toks[p]);
                    if (k == "FROM")    via.fromLayer = toks[p + 2u];
                    else if (k == "TO") via.toLayer   = toks[p + 2u];
                    else if (k == "RPV") ReaderText::ParseDoubleSv(toks[p + 2u], via.rpv);
                    p += 3u;
                } else {
                    ++p;
                }
            }
            if (p >= toks.size()) return false;
            vias.push_back(std::move(via));
            i = p;
        }
    }

    if (techName.empty() || layers.empty()) return false;

    out = ParsedEdaBoard{};
    out.kind = EdaDocKind::KiCadPcb;

    // ITF lists layers top-to-bottom or bottom-to-top; render stacked cross-section bands
    std::unordered_map<std::string, float> condCenterY;
    float curY = 0.0f;
    const float crossW = 72.0f;
    size_t condCount = 0u;

    for (size_t li = 0u; li < layers.size(); ++li) {
        const auto& lyr = layers[li];
        const float bandH = std::clamp(static_cast<float>(std::log1p(lyr.thicknessUm * 10.0) * 2.4), 3.2f, 11.0f);
        const float cy    = curY + bandH * 0.5f;
        const EdaLayerId lid = lyr.isConductor
            ? ((condCount++ % 2u == 0u) ? EdaLayerId::TopCopper : EdaLayerId::InnerCopper)
            : EdaLayerId::BottomSilk;

        const float w = lyr.isConductor ? (crossW * 0.78f) : crossW;
        DetailSiliconAndBusEda::AddRectOutline(out, 0.0f, cy, w, bandH - 0.3f, lid, lyr.isConductor ? 0.32f : 0.16f);

        wchar_t lbl[160] = {};
        if (lyr.isConductor) {
            condCenterY[lyr.name] = cy;
            swprintf_s(lbl, L"METAL %ls  t=%.3g um  Rpsq=%.3g",
                       PathUtils::Utf8ToWide(lyr.name).c_str(), lyr.thicknessUm, lyr.rpsq);
            EdaComponent3D c{};
            c.refDes = L"M_" + PathUtils::Utf8ToWide(lyr.name);
            c.value  = lbl;
            c.footprint = L"ITF_CONDUCTOR";
            c.x = 0.0f; c.y = cy;
            c.widthMm = w; c.lengthMm = bandH;
            c.category = EdaPartCategory::GenericSMD;
            out.components.push_back(std::move(c));
        } else {
            swprintf_s(lbl, L"ILD %ls  t=%.3g um  Er=%.3g",
                       PathUtils::Utf8ToWide(lyr.name).c_str(), lyr.thicknessUm, lyr.er);
        }
        out.labels.push_back({ -w * 0.46f, cy - 0.4f, 0.85f, EdaLayerId::TopSilk, lbl });
        curY += bandH;
    }

    // Draw vertical VIA pillars connecting FROM -> TO conductors
    for (size_t vi = 0u; vi < vias.size(); ++vi) {
        auto itA = condCenterY.find(vias[vi].fromLayer);
        auto itB = condCenterY.find(vias[vi].toLayer);
        if (itA == condCenterY.end() || itB == condCenterY.end()) continue;
        const float vx = -18.0f + static_cast<float>(vi % 6u) * 7.2f;
        out.traces.push_back({ vx, itA->second, vx, itB->second, 1.4f, EdaLayerId::PadsThrough, static_cast<int>(vi + 1u) });
        EdaViaPrim vp{};
        vp.x = vx;
        vp.y = (itA->second + itB->second) * 0.5f;
        vp.diameter = 1.8f;
        vp.drill = 0.9f;
        vp.netId = static_cast<int>(vi + 1u);
        out.vias.push_back(vp);
    }

    out.netCount = condCount + vias.size();
    wchar_t title[200] = {};
    swprintf_s(title, L"Synopsys Interconnect Tech Stackup (.itf %ls, %zu metals, %zu vias)",
               PathUtils::Utf8ToWide(techName).c_str(), condCount, vias.size());
    out.formatTitle = title;
    FinalizeBoardBounds(out);
    out.valid = true;
    return true;
}

} // namespace Formats
