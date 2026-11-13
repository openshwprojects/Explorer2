#pragma once
// LtspiceAsc.h — LTspice Schematic (.asc) Parser

namespace Formats {

inline bool ParseLtspiceAsc(std::string_view sv, ParsedEdaBoard& out) {
    if (sv.find("Version 4") == std::string_view::npos && sv.find("WIRE ") == std::string_view::npos) {
        return false;
    }
    out.kind = EdaDocKind::KiCadSch;
    out.formatTitle = L"LTspice Schematic (.asc)";

    EdaComponent3D curComp;
    bool hasComp = false;
    auto flushComp = [&]() {
        if (!hasComp) return;
        float hw = 2.0f, hh = 1.4f;
        out.traces.push_back({ curComp.x - hw, curComp.y - hh, curComp.x + hw, curComp.y - hh, 0.28f, EdaLayerId::SchSymbol, 0 });
        out.traces.push_back({ curComp.x + hw, curComp.y - hh, curComp.x + hw, curComp.y + hh, 0.28f, EdaLayerId::SchSymbol, 0 });
        out.traces.push_back({ curComp.x + hw, curComp.y + hh, curComp.x - hw, curComp.y + hh, 0.28f, EdaLayerId::SchSymbol, 0 });
        out.traces.push_back({ curComp.x - hw, curComp.y + hh, curComp.x - hw, curComp.y - hh, 0.28f, EdaLayerId::SchSymbol, 0 });
        std::wstring lbl = curComp.refDes;
        if (!curComp.value.empty()) lbl += L" (" + curComp.value + L")";
        if (!lbl.empty()) out.labels.push_back({ curComp.x, curComp.y, 1.1f, EdaLayerId::SchLabel, lbl });
        out.components.push_back(std::move(curComp));
        curComp = EdaComponent3D{};
        hasComp = false;
    };

    size_t pos = 0;
    while (pos < sv.size()) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.remove_suffix(1);
        if (ln.empty()) continue;

        if (ln.rfind("WIRE ", 0) == 0) {
            float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            if (sscanf_s(std::string(ln.substr(5)).c_str(), "%f %f %f %f", &x1, &y1, &x2, &y2) == 4) {
                out.traces.push_back({ x1 * 0.1f, -y1 * 0.1f, x2 * 0.1f, -y2 * 0.1f, 0.25f, EdaLayerId::SchWire, 0 });
            }
        } else if (ln.rfind("SYMBOL ", 0) == 0) {
            flushComp();
            char sym[64] = {};
            float sx = 0, sy = 0;
            if (sscanf_s(std::string(ln.substr(7)).c_str(), "%63s %f %f", sym, (unsigned)_countof(sym), &sx, &sy) >= 3) {
                curComp.footprint = PathUtils::Utf8ToWide(sym);
                curComp.refDes = curComp.footprint;
                curComp.x = sx * 0.1f;
                curComp.y = -sy * 0.1f;
                curComp.widthMm = 4.0f;
                curComp.lengthMm = 2.8f;
                curComp.heightMm = 1.5f;
                hasComp = true;
            }
        } else if (hasComp && ln.rfind("SYMATTR InstName ", 0) == 0) {
            curComp.refDes = PathUtils::Utf8ToWide(ln.substr(17));
        } else if (hasComp && ln.rfind("SYMATTR Value ", 0) == 0) {
            curComp.value = PathUtils::Utf8ToWide(ln.substr(14));
        } else if (ln.rfind("FLAG ", 0) == 0) {
            float fx = 0, fy = 0;
            char netName[64] = {};
            if (sscanf_s(std::string(ln.substr(5)).c_str(), "%f %f %63s", &fx, &fy, netName, (unsigned)_countof(netName)) >= 3) {
                out.vias.push_back({ fx * 0.1f, -fy * 0.1f, 0.7f, 0.0f, 0 });
                out.labels.push_back({ fx * 0.1f, -fy * 0.1f + 0.8f, 1.0f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(netName) });
                ++out.netCount;
            }
        }
    }
    flushComp();
    return (!out.traces.empty() || !out.components.empty());
}

} // namespace Formats
