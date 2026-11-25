#pragma once
// KiCadPcb.h — KiCad S-Expression PCB Layout (.kicad_pcb) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

inline bool ParseKiCadPcb(std::string_view txt, ParsedEdaBoard& out) {
    using namespace DetailKiCadSExpr;
    if (txt.find("(kicad_pcb") == std::string_view::npos) {
        return false;
    }

    out.kind = EdaDocKind::KiCadPcb;
    out.formatTitle = L"KiCad PCB Layout (.kicad_pcb)";

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

        if (kw == "via") {
            size_t closeP = FindMatchingParen(txt, openP);
            if (closeP == std::string_view::npos) break;
            std::string_view blk = txt.substr(openP, closeP - openP + 1);
            float vx = 0.0f, vy = 0.0f, sz = 0.8f, dr = 0.4f, dummy = 0.0f;
            if (ExtractSExprFloats(blk, "(at ", vx, vy)) {
                ExtractSExprFloats(blk, "(size ", sz, dummy);
                ExtractSExprFloats(blk, "(drill ", dr, dummy);
                out.vias.push_back({ vx, -vy, std::max(0.4f, sz), std::max(0.2f, dr), 0 });
            }
            pos = closeP + 1;
            continue;
        }

        if (kw == "net") {
            size_t closeP = FindMatchingParen(txt, openP);
            if (closeP == std::string_view::npos) break;
            ++out.netCount;
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
