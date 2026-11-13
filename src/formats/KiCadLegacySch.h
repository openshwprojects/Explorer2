#pragma once

static EdaLayerId MapDxfLayerToEda(std::string_view layerName) {
    if (layerName.empty() || layerName == "0") return EdaLayerId::TopCopper;
    uint32_t h = 2166136261u;
    for (char c : layerName) {
        h ^= static_cast<uint8_t>(std::toupper(static_cast<unsigned char>(c)));
        h *= 16777619u;
    }
    switch (h % 5) {
    case 0: return EdaLayerId::TopCopper;
    case 1: return EdaLayerId::BottomCopper;
    case 2: return EdaLayerId::TopSilk;
    case 3: return EdaLayerId::EdgeCuts;
    default: return EdaLayerId::InnerCopper;
    }
}

static bool ParseKiCadLegacySch(std::string_view txt, ParsedEdaBoard& out) {
    out.kind = EdaDocKind::KiCadSch;
    out.formatTitle = L"KiCad Legacy Schematic (.sch)";

    size_t pos = 0;
    bool inComp = false;
    EdaComponent3D curComp;
    while (pos < txt.size()) {
        size_t eol = txt.find('\n', pos);
        if (eol == std::string_view::npos) eol = txt.size();
        std::string_view ln = txt.substr(pos, eol - pos);
        if (!ln.empty() && ln.back() == '\r') ln.remove_suffix(1);
        pos = eol + 1;

        if (ln == "Wire Wire Line" || ln == "Wire Bus Line" || ln == "Wire Notes Line") {
            size_t eol2 = txt.find('\n', pos);
            if (eol2 == std::string_view::npos) eol2 = txt.size();
            std::string coords(txt.substr(pos, eol2 - pos));
            pos = eol2 + 1;
            float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            if (sscanf_s(coords.c_str(), "%f %f %f %f", &x1, &y1, &x2, &y2) == 4) {
                // Legacy EESchema uses mils (0.0254 mm)
                const float kMilToMm = 0.0254f;
                out.traces.push_back({ x1 * kMilToMm, -y1 * kMilToMm, x2 * kMilToMm, -y2 * kMilToMm, 0.25f, EdaLayerId::SchWire, 0 });
            }
            continue;
        }
        if (ln == "$Comp") {
            inComp = true;
            curComp = EdaComponent3D{};
            curComp.widthMm = 6.0f;
            curComp.lengthMm = 4.0f;
            continue;
        }
        if (ln == "$EndComp") {
            if (inComp) {
                float hw = 3.0f, hh = 2.0f;
                out.traces.push_back({ curComp.x - hw, curComp.y - hh, curComp.x + hw, curComp.y - hh, 0.28f, EdaLayerId::SchSymbol, 0 });
                out.traces.push_back({ curComp.x + hw, curComp.y - hh, curComp.x + hw, curComp.y + hh, 0.28f, EdaLayerId::SchSymbol, 0 });
                out.traces.push_back({ curComp.x + hw, curComp.y + hh, curComp.x - hw, curComp.y + hh, 0.28f, EdaLayerId::SchSymbol, 0 });
                out.traces.push_back({ curComp.x - hw, curComp.y + hh, curComp.x - hw, curComp.y - hh, 0.28f, EdaLayerId::SchSymbol, 0 });
                out.labels.push_back({ curComp.x, curComp.y, 1.2f, EdaLayerId::SchLabel, curComp.refDes + (curComp.value.empty() ? L"" : (L" (" + curComp.value + L")")) });
                out.components.push_back(std::move(curComp));
            }
            inComp = false;
            continue;
        }
        if (inComp) {
            if (ln.rfind("L ", 0) == 0) {
                std::string s(ln.substr(2));
                char symBuf[64] = {}, refBuf[64] = {};
                if (sscanf_s(s.c_str(), "%63s %63s", symBuf, (unsigned)_countof(symBuf), refBuf, (unsigned)_countof(refBuf)) >= 2) {
                    curComp.footprint = PathUtils::Utf8ToWide(symBuf);
                    curComp.refDes = PathUtils::Utf8ToWide(refBuf);
                }
            } else if (ln.rfind("P ", 0) == 0) {
                float px = 0, py = 0;
                if (sscanf_s(std::string(ln.substr(2)).c_str(), "%f %f", &px, &py) == 2) {
                    curComp.x = px * 0.0254f;
                    curComp.y = -py * 0.0254f;
                }
            } else if (ln.rfind("F 1 \"", 0) == 0) {
                size_t q2 = ln.find('"', 5);
                if (q2 != std::string_view::npos) {
                    curComp.value = PathUtils::Utf8ToWide(ln.substr(5, q2 - 5));
                }
            }
        }
    }
    FinalizeBoardBounds(out);
    out.valid = (!out.traces.empty() || !out.components.empty());
    return out.valid;
}
