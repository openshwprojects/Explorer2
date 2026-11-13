#pragma once
// SpiceNetlist.h — SPICE Electronic Circuit Netlist (.cir / .spice / .sp / .net) Topology Parser

namespace Formats {

inline bool ParseSpiceNetlistBoard(std::string_view sv, ParsedEdaBoard& out) {
    out.kind = EdaDocKind::KiCadSch;
    out.formatTitle = L"SPICE Circuit Netlist (.cir)";

    size_t idx = 0;
    size_t pos = 0;
    while (pos < sv.size()) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.remove_suffix(1);
        if (ln.empty() || ln[0] == '*' || ln[0] == '.' || ln[0] == ';') continue;

        char c0 = static_cast<char>(std::toupper(static_cast<unsigned char>(ln[0])));
        if (c0 == 'R' || c0 == 'C' || c0 == 'L' || c0 == 'V' || c0 == 'I' || c0 == 'D' || c0 == 'Q' || c0 == 'M' || c0 == 'X') {
            char ref[64] = {}, n1[64] = {}, n2[64] = {}, val[64] = {};
            if (sscanf_s(std::string(ln).c_str(), "%63s %63s %63s %63s",
                ref, (unsigned)_countof(ref), n1, (unsigned)_countof(n1),
                n2, (unsigned)_countof(n2), val, (unsigned)_countof(val)) >= 3)
            {
                float cx = static_cast<float>((idx % 4) * 18.0f);
                float cy = -static_cast<float>((idx / 4) * 12.0f);
                EdaComponent3D comp;
                comp.refDes = PathUtils::Utf8ToWide(ref);
                comp.value = PathUtils::Utf8ToWide(val);
                comp.footprint = L"SPICE";
                comp.x = cx;
                comp.y = cy;
                comp.widthMm = 5.0f;
                comp.lengthMm = 3.0f;
                comp.heightMm = 1.5f;

                // Component box + pin leads
                out.traces.push_back({ cx - 2.5f, cy - 1.5f, cx + 2.5f, cy - 1.5f, 0.28f, EdaLayerId::SchSymbol, 0 });
                out.traces.push_back({ cx + 2.5f, cy - 1.5f, cx + 2.5f, cy + 1.5f, 0.28f, EdaLayerId::SchSymbol, 0 });
                out.traces.push_back({ cx + 2.5f, cy + 1.5f, cx - 2.5f, cy + 1.5f, 0.28f, EdaLayerId::SchSymbol, 0 });
                out.traces.push_back({ cx - 2.5f, cy + 1.5f, cx - 2.5f, cy - 1.5f, 0.28f, EdaLayerId::SchSymbol, 0 });
                out.traces.push_back({ cx - 5.5f, cy, cx - 2.5f, cy, 0.24f, EdaLayerId::SchWire, 0 });
                out.traces.push_back({ cx + 2.5f, cy, cx + 5.5f, cy, 0.24f, EdaLayerId::SchWire, 0 });

                std::wstring lbl = comp.refDes + (comp.value.empty() ? L"" : (L"=" + comp.value));
                out.labels.push_back({ cx, cy, 1.0f, EdaLayerId::SchLabel, lbl });
                out.components.push_back(std::move(comp));
                ++idx;
            }
        }
    }
    out.netCount = static_cast<int>(idx);
    return !out.components.empty();
}

} // namespace Formats
