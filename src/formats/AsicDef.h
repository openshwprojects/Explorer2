#pragma once
// AsicDef.h — ASIC Design Exchange Format (.def) Die Area, Placed Cells & Routed Nets Parser

namespace Formats {

inline bool ParseAsicDefBoard(std::string_view sv, ParsedEdaBoard& out) {
    if (sv.find("DIEAREA") == std::string_view::npos && sv.find("COMPONENTS") == std::string_view::npos) return false;
    out.kind = EdaDocKind::KiCadPcb;
    out.formatTitle = L"ASIC Design Exchange Format (.def)";

    const float scale = 0.01f; // DBU to mm preview scale
    size_t pos = 0;
    while (pos < sv.size()) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.remove_suffix(1);
        if (ln.empty()) continue;

        if (ln.rfind("DIEAREA", 0) == 0) {
            float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            if (sscanf_s(std::string(ln).c_str(), "DIEAREA ( %f %f ) ( %f %f )", &x1, &y1, &x2, &y2) == 4) {
                x1 *= scale; y1 *= scale; x2 *= scale; y2 *= scale;
                out.traces.push_back({ x1, y1, x2, y1, 0.25f, EdaLayerId::EdgeCuts, 0 });
                out.traces.push_back({ x2, y1, x2, y2, 0.25f, EdaLayerId::EdgeCuts, 0 });
                out.traces.push_back({ x2, y2, x1, y2, 0.25f, EdaLayerId::EdgeCuts, 0 });
                out.traces.push_back({ x1, y2, x1, y1, 0.25f, EdaLayerId::EdgeCuts, 0 });
            }
        } else if (ln[0] == '-' && ln.find("PLACED") != std::string_view::npos) {
            char inst[64] = {}, cell[64] = {};
            float cx = 0.0f, cy = 0.0f;
            size_t pl = ln.find("PLACED");
            sscanf_s(std::string(ln).c_str(), "- %63s %63s", inst, (unsigned)_countof(inst), cell, (unsigned)_countof(cell));
            size_t p1 = ln.find('(', pl);
            if (p1 != std::string_view::npos) {
                sscanf_s(std::string(ln.substr(p1)).c_str(), "( %f %f )", &cx, &cy);
            }
            cx *= scale; cy *= scale;
            EdaComponent3D comp;
            comp.refDes = PathUtils::Utf8ToWide(inst);
            comp.value = PathUtils::Utf8ToWide(cell);
            comp.footprint = L"STDCELL";
            comp.x = cx;
            comp.y = cy;
            comp.widthMm = 2.4f;
            comp.lengthMm = 1.6f;
            comp.heightMm = 0.6f;
            comp.category = EdaPartCategory::IC_Package;
            EdaPadPrim pad;
            pad.x = cx;
            pad.y = cy;
            pad.w = 2.0f;
            pad.h = 1.2f;
            pad.shape = 1;
            pad.layer = EdaLayerId::TopCopper;
            out.pads.push_back(pad);
            out.labels.push_back({ cx, cy, 0.8f, EdaLayerId::TopSilk, comp.refDes });
            out.components.push_back(std::move(comp));
        } else if (ln.rfind("ROUTED", 0) == 0 || ln.rfind("+ ROUTED", 0) == 0) {
            size_t p1 = ln.find('(');
            size_t p2 = (p1 != std::string_view::npos) ? ln.find('(', p1 + 1) : std::string_view::npos;
            if (p1 != std::string_view::npos && p2 != std::string_view::npos) {
                float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
                sscanf_s(std::string(ln.substr(p1)).c_str(), "( %f %f )", &x1, &y1);
                sscanf_s(std::string(ln.substr(p2)).c_str(), "( %f %f )", &x2, &y2);
                out.traces.push_back({ x1 * scale, y1 * scale, x2 * scale, y2 * scale, 0.22f, EdaLayerId::TopCopper, 1 });
            }
        }
    }
    out.netCount = std::max(1, static_cast<int>(out.components.size()));
    return !out.components.empty() || !out.traces.empty();
}

} // namespace Formats
