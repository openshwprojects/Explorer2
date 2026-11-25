#pragma once
// KiCadFootprint.h — KiCad S-Expression Footprint / Module (.kicad_mod) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

inline bool ParseKiCadFootprint(std::string_view txt, ParsedEdaBoard& out) {
    using namespace DetailKiCadSExpr;
    if (txt.find("(kicad_pcb") != std::string_view::npos ||
        txt.find("(kicad_sch") != std::string_view::npos) {
        return false;
    }
    if (txt.find("(footprint") == std::string_view::npos &&
        txt.find("(module") == std::string_view::npos) {
        return false;
    }

    out.kind = EdaDocKind::KiCadPcb;
    out.formatTitle = L"KiCad Footprint (.kicad_mod)";

    size_t pos = 0;
    while (pos < txt.size()) {
        size_t openP = txt.find('(', pos);
        if (openP == std::string_view::npos || openP + 2 >= txt.size()) break;

        size_t kwEnd = openP + 1;
        while (kwEnd < txt.size() && txt[kwEnd] > ' ' && txt[kwEnd] != '(' && txt[kwEnd] != ')') ++kwEnd;
        std::string_view kw = txt.substr(openP + 1, kwEnd - (openP + 1));

        if (kw == "segment" || kw == "gr_line" || kw == "gr_rect") {
            size_t closeP = FindMatchingParen(txt, openP);
            if (closeP == std::string_view::npos) break;
            ParseKiCadGraphicSExprBlock(kw, txt.substr(openP, closeP - openP + 1), out);
            pos = closeP + 1;
            continue;
        }

        if (kw == "footprint" || kw == "module") {
            size_t closeP = FindMatchingParen(txt, openP);
            if (closeP == std::string_view::npos) break;
            ParseKiCadFootprintSExprBlock(kw, txt.substr(openP, closeP - openP + 1), out);
            pos = closeP + 1;
            continue;
        }

        pos = kwEnd;
    }

    FinalizeBoardBounds(out);
    out.valid = (!out.traces.empty() || !out.pads.empty() || !out.components.empty() || !out.vias.empty());
    return out.valid;
}
