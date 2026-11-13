#pragma once
// AsicLef.h — ASIC Library Exchange Format (.lef) Standard Cell & Pin Physical Layout Parser

namespace Formats {

inline bool ParseAsicLefBoard(std::string_view sv, ParsedEdaBoard& out) {
    if (sv.find("MACRO") == std::string_view::npos && sv.find("RECT") == std::string_view::npos) return false;
    out.kind = EdaDocKind::KiCadPcb;
    out.formatTitle = L"ASIC Library Exchange Format (.lef)";

    float macroX = 0.0f;
    float curW = 10.0f, curH = 10.0f;
    std::wstring curMacro = L"CELL";

    size_t pos = 0;
    while (pos < sv.size()) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.remove_suffix(1);
        if (ln.empty()) continue;

        if (ln.rfind("MACRO ", 0) == 0) {
            char mname[64] = {};
            if (sscanf_s(std::string(ln.substr(6)).c_str(), "%63s", mname, (unsigned)_countof(mname)) == 1) {
                curMacro = PathUtils::Utf8ToWide(mname);
            }
        } else if (ln.rfind("SIZE ", 0) == 0) {
            float w = 0.0f, h = 0.0f;
            if (sscanf_s(std::string(ln.substr(5)).c_str(), "%f BY %f", &w, &h) == 2 ||
                sscanf_s(std::string(ln.substr(5)).c_str(), "%f by %f", &w, &h) == 2)
            {
                curW = std::max(2.0f, w);
                curH = std::max(2.0f, h);
                out.traces.push_back({ macroX, 0, macroX + curW, 0, 0.18f, EdaLayerId::EdgeCuts, 0 });
                out.traces.push_back({ macroX + curW, 0, macroX + curW, curH, 0.18f, EdaLayerId::EdgeCuts, 0 });
                out.traces.push_back({ macroX + curW, curH, macroX, curH, 0.18f, EdaLayerId::EdgeCuts, 0 });
                out.traces.push_back({ macroX, curH, macroX, 0, 0.18f, EdaLayerId::EdgeCuts, 0 });

                EdaComponent3D comp;
                comp.refDes = curMacro;
                comp.footprint = L"LEF_MACRO";
                comp.x = macroX + curW * 0.5f;
                comp.y = curH * 0.5f;
                comp.widthMm = curW;
                comp.lengthMm = curH;
                comp.heightMm = 0.8f;
                comp.category = EdaPartCategory::IC_Package;
                out.components.push_back(std::move(comp));
                out.labels.push_back({ macroX + curW * 0.5f, curH + 1.2f, 1.0f, EdaLayerId::TopSilk, curMacro });
                macroX += curW + 4.0f;
            }
        } else if (ln.rfind("RECT ", 0) == 0) {
            float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            if (sscanf_s(std::string(ln.substr(5)).c_str(), "%f %f %f %f", &x1, &y1, &x2, &y2) == 4) {
                float ox = std::max(0.0f, macroX - curW - 4.0f);
                EdaPadPrim pad;
                pad.x = ox + (x1 + x2) * 0.5f;
                pad.y = (y1 + y2) * 0.5f;
                pad.w = std::max(0.3f, std::fabs(x2 - x1));
                pad.h = std::max(0.3f, std::fabs(y2 - y1));
                pad.shape = 1;
                pad.layer = EdaLayerId::TopCopper;
                out.pads.push_back(pad);
            }
        }
    }
    return !out.components.empty() || !out.pads.empty() || !out.traces.empty();
}

} // namespace Formats
