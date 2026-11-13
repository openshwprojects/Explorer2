#pragma once

// Fast S-expression helpers for KiCad (.kicad_pcb, .kicad_sch, .kicad_mod)
static size_t FindMatchingParen(std::string_view sv, size_t openPos) {
    int depth = 0;
    bool inQuotes = false;
    for (size_t i = openPos; i < sv.size(); ++i) {
        char c = sv[i];
        if (c == '"' && (i == 0 || sv[i - 1] != '\\')) {
            inQuotes = !inQuotes;
        } else if (!inQuotes) {
            if (c == '(') ++depth;
            else if (c == ')') {
                --depth;
                if (depth == 0) return i;
            }
        }
    }
    return std::string_view::npos;
}

static bool ExtractSExprFloats(std::string_view block, std::string_view key, float& f1, float& f2, float* f3 = nullptr) {
    size_t pos = block.find(key);
    if (pos == std::string_view::npos) return false;
    pos += key.size();
    char buf[96] = {};
    size_t end = block.find(')', pos);
    if (end == std::string_view::npos) return false;
    size_t len = std::min<size_t>(end - pos, 95);
    std::memcpy(buf, block.data() + pos, len);
    char* p = buf;
    char* next = nullptr;
    f1 = std::strtof(p, &next);
    if (next == p) return false;
    p = next;
    f2 = std::strtof(p, &next);
    if (f3 && next != p) {
        p = next;
        *f3 = std::strtof(p, &next);
    }
    return true;
}

static std::string ExtractSExprQuotedOrAtom(std::string_view block, std::string_view prefix) {
    size_t pos = block.find(prefix);
    if (pos == std::string_view::npos) return "";
    pos += prefix.size();
    while (pos < block.size() && (block[pos] == ' ' || block[pos] == '\t')) ++pos;
    if (pos >= block.size()) return "";
    if (block[pos] == '"') {
        ++pos;
        size_t endQ = block.find('"', pos);
        if (endQ == std::string_view::npos) return "";
        return std::string(block.substr(pos, endQ - pos));
    }
    size_t endA = pos;
    while (endA < block.size() && block[endA] != ' ' && block[endA] != ')' && block[endA] != '\r' && block[endA] != '\n') {
        ++endA;
    }
    return std::string(block.substr(pos, endA - pos));
}

static EdaLayerId MapKiCadLayer(std::string_view lyr) {
    if (lyr.find("F.Cu") != std::string_view::npos || lyr.find("*.Cu") != std::string_view::npos) return EdaLayerId::TopCopper;
    if (lyr.find("B.Cu") != std::string_view::npos) return EdaLayerId::BottomCopper;
    if (lyr.find("Edge.Cuts") != std::string_view::npos) return EdaLayerId::EdgeCuts;
    if (lyr.find("B.Silk") != std::string_view::npos || lyr.find("B.Fab") != std::string_view::npos) return EdaLayerId::BottomSilk;
    if (lyr.find(".Cu") != std::string_view::npos) return EdaLayerId::InnerCopper;
    return EdaLayerId::TopSilk;
}

static bool ParseKiCadSExpr(std::string_view txt, const std::wstring& ext, ParsedEdaBoard& out) {
    bool isSch = (ext == L".kicad_sch") || (txt.find("(kicad_sch") != std::string_view::npos);
    out.kind = isSch ? EdaDocKind::KiCadSch : EdaDocKind::KiCadPcb;
    out.formatTitle = isSch ? L"KiCad Schematic (.kicad_sch)" : L"KiCad PCB Layout (.kicad_pcb)";

    size_t pos = 0;
    while (pos < txt.size()) {
        size_t openP = txt.find('(', pos);
        if (openP == std::string_view::npos || openP + 2 >= txt.size()) break;

        size_t kwEnd = openP + 1;
        while (kwEnd < txt.size() && txt[kwEnd] > ' ' && txt[kwEnd] != '(' && txt[kwEnd] != ')') ++kwEnd;
        std::string_view kw = txt.substr(openP + 1, kwEnd - (openP + 1));

        if (kw == "lib_symbols") {
            size_t closeP = FindMatchingParen(txt, openP);
            if (closeP == std::string_view::npos) break;
            pos = closeP + 1;
            continue;
        }

        if (kw == "segment" || kw == "gr_line" || kw == "gr_rect") {
            size_t closeP = FindMatchingParen(txt, openP);
            if (closeP == std::string_view::npos) break;
            std::string_view blk = txt.substr(openP, closeP - openP + 1);
            float x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f, w = 0.25f, dummy = 0.0f;
            if (ExtractSExprFloats(blk, "(start ", x1, y1) && ExtractSExprFloats(blk, "(end ", x2, y2)) {
                ExtractSExprFloats(blk, "(width ", w, dummy);
                if (w <= 0.01f) ExtractSExprFloats(blk, "(stroke (width ", w, dummy);
                std::string lyr = ExtractSExprQuotedOrAtom(blk, "(layer ");
                EdaLayerId lid = MapKiCadLayer(lyr);
                if (kw == "gr_rect") {
                    out.traces.push_back({ x1, -y1, x2, -y1, std::max(0.15f, w), lid, 0 });
                    out.traces.push_back({ x2, -y1, x2, -y2, std::max(0.15f, w), lid, 0 });
                    out.traces.push_back({ x2, -y2, x1, -y2, std::max(0.15f, w), lid, 0 });
                    out.traces.push_back({ x1, -y2, x1, -y1, std::max(0.15f, w), lid, 0 });
                } else {
                    out.traces.push_back({ x1, -y1, x2, -y2, std::max(0.15f, w), lid, 0 });
                }
            }
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
            std::string_view blk = txt.substr(openP, closeP - openP + 1);

            EdaComponent3D comp;
            std::string pkgName = ExtractSExprQuotedOrAtom(blk, kw == "footprint" ? "(footprint " : "(module ");
            size_t colon = pkgName.find(':');
            if (colon != std::string::npos) pkgName = pkgName.substr(colon + 1);
            comp.footprint = PathUtils::Utf8ToWide(pkgName);

            float fx = 0.0f, fy = 0.0f, frot = 0.0f;
            ExtractSExprFloats(blk, "(at ", fx, fy, &frot);
            comp.x = fx;
            comp.y = -fy;
            comp.rotDeg = frot;

            std::string lyr = ExtractSExprQuotedOrAtom(blk, "(layer ");
            comp.bottomSide = (lyr.find("B.Cu") != std::string::npos);

            // Reference & Value from either (fp_text reference "U1") or KiCad 8 (property "Reference" "U1")
            std::string refStr = ExtractSExprQuotedOrAtom(blk, "(fp_text reference ");
            if (refStr.empty()) refStr = ExtractSExprQuotedOrAtom(blk, "(property \"Reference\" ");
            std::string valStr = ExtractSExprQuotedOrAtom(blk, "(fp_text value ");
            if (valStr.empty()) valStr = ExtractSExprQuotedOrAtom(blk, "(property \"Value\" ");
            comp.refDes = PathUtils::Utf8ToWide(refStr.empty() ? "U?" : refStr);
            comp.value = PathUtils::Utf8ToWide(valStr);

            float rad = comp.rotDeg * 3.14159265f / 180.0f;
            float cs = std::cos(rad);
            float sn = std::sin(rad);
            float minPx = 1e9f, minPy = 1e9f, maxPx = -1e9f, maxPy = -1e9f;

            // Scan inner pads and silkscreen lines inside the footprint
            size_t subPos = 1 + kw.size();
            while (subPos < blk.size()) {
                size_t subOpen = blk.find('(', subPos);
                if (subOpen == std::string_view::npos || subOpen + 2 >= blk.size()) break;
                size_t subKwEnd = subOpen + 1;
                while (subKwEnd < blk.size() && blk[subKwEnd] > ' ' && blk[subKwEnd] != '(' && blk[subKwEnd] != ')') ++subKwEnd;
                std::string_view subKw = blk.substr(subOpen + 1, subKwEnd - (subOpen + 1));

                if (subKw == "pad") {
                    size_t subClose = FindMatchingParen(blk, subOpen);
                    if (subClose == std::string_view::npos) break;
                    std::string_view padBlk = blk.substr(subOpen, subClose - subOpen + 1);
                    std::string pinNm = ExtractSExprQuotedOrAtom(padBlk, "(pad ");
                    float px = 0.0f, py = 0.0f, prot = 0.0f, pw = 1.0f, ph = 1.0f, dr = 0.0f, dummy = 0.0f;
                    if (ExtractSExprFloats(padBlk, "(at ", px, py, &prot)) {
                        ExtractSExprFloats(padBlk, "(size ", pw, ph);
                        ExtractSExprFloats(padBlk, "(drill ", dr, dummy);
                        float ly = -py;
                        float bx = comp.x + (px * cs - ly * sn);
                        float by = comp.y + (px * sn + ly * cs);
                        EdaPadPrim pad;
                        pad.x = bx;
                        pad.y = by;
                        pad.w = std::max(0.2f, pw);
                        pad.h = std::max(0.2f, ph);
                        pad.drill = dr;
                        pad.rotDeg = prot;
                        pad.pinName = PathUtils::Utf8ToWide(pinNm);
                        pad.shape = (padBlk.find(" circle ") != std::string_view::npos) ? 0 : 1;
                        if (padBlk.find("thru_hole") != std::string_view::npos || dr > 0.05f) {
                            pad.layer = EdaLayerId::PadsThrough;
                        } else {
                            pad.layer = comp.bottomSide ? EdaLayerId::BottomCopper : EdaLayerId::TopCopper;
                        }
                        out.pads.push_back(pad);
                        ++comp.pinCount;
                        minPx = std::min(minPx, px - pw * 0.5f);
                        maxPx = std::max(maxPx, px + pw * 0.5f);
                        minPy = std::min(minPy, ly - ph * 0.5f);
                        maxPy = std::max(maxPy, ly + ph * 0.5f);
                    }
                    subPos = subClose + 1;
                    continue;
                } else if (subKw == "fp_line") {
                    size_t subClose = FindMatchingParen(blk, subOpen);
                    if (subClose == std::string_view::npos) break;
                    std::string_view lnBlk = blk.substr(subOpen, subClose - subOpen + 1);
                    float x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f;
                    if (ExtractSExprFloats(lnBlk, "(start ", x1, y1) && ExtractSExprFloats(lnBlk, "(end ", x2, y2)) {
                        float ly1 = -y1, ly2 = -y2;
                        float bx1 = comp.x + (x1 * cs - ly1 * sn);
                        float by1 = comp.y + (x1 * sn + ly1 * cs);
                        float bx2 = comp.x + (x2 * cs - ly2 * sn);
                        float by2 = comp.y + (x2 * sn + ly2 * cs);
                        out.traces.push_back({ bx1, by1, bx2, by2, 0.15f, comp.bottomSide ? EdaLayerId::BottomSilk : EdaLayerId::TopSilk, 0 });
                        minPx = std::min({ minPx, x1, x2 });
                        maxPx = std::max({ maxPx, x1, x2 });
                        minPy = std::min({ minPy, ly1, ly2 });
                        maxPy = std::max({ maxPy, ly1, ly2 });
                    }
                    subPos = subClose + 1;
                    continue;
                }
                subPos = subKwEnd;
            }

            if (minPx < maxPx && minPy < maxPy) {
                comp.widthMm = std::clamp(maxPx - minPx, 1.0f, 40.0f);
                comp.lengthMm = std::clamp(maxPy - minPy, 0.8f, 40.0f);
            }
            comp.category = ClassifyComponent(comp.refDes, comp.footprint, comp.widthMm, comp.lengthMm, comp.heightMm, comp.pinCount);
            out.labels.push_back({ comp.x, comp.y, 1.0f, comp.bottomSide ? EdaLayerId::BottomSilk : EdaLayerId::TopSilk, comp.refDes });
            out.components.push_back(std::move(comp));

            pos = closeP + 1;
            continue;
        }

        if (isSch && (kw == "wire" || kw == "bus")) {
            size_t closeP = FindMatchingParen(txt, openP);
            if (closeP == std::string_view::npos) break;
            std::string_view blk = txt.substr(openP, closeP - openP + 1);
            size_t xy1Pos = blk.find("(xy ");
            if (xy1Pos != std::string_view::npos) {
                float x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f;
                if (ExtractSExprFloats(blk.substr(xy1Pos), "(xy ", x1, y1)) {
                    size_t xy2Pos = blk.find("(xy ", xy1Pos + 4);
                    if (xy2Pos != std::string_view::npos && ExtractSExprFloats(blk.substr(xy2Pos), "(xy ", x2, y2)) {
                        out.traces.push_back({ x1, -y1, x2, -y2, kw == "bus" ? 0.5f : 0.25f, EdaLayerId::SchWire, 0 });
                    }
                }
            }
            pos = closeP + 1;
            continue;
        }

        if (isSch && kw == "symbol" && openP > 10) {
            size_t closeP = FindMatchingParen(txt, openP);
            if (closeP == std::string_view::npos) break;
            std::string_view blk = txt.substr(openP, closeP - openP + 1);
            if (blk.find("(lib_id ") != std::string_view::npos) {
                float sx = 0.0f, sy = 0.0f, srot = 0.0f;
                if (ExtractSExprFloats(blk, "(at ", sx, sy, &srot)) {
                    std::string refStr = ExtractSExprQuotedOrAtom(blk, "(property \"Reference\" ");
                    std::string valStr = ExtractSExprQuotedOrAtom(blk, "(property \"Value\" ");
                    std::string libStr = ExtractSExprQuotedOrAtom(blk, "(lib_id ");
                    EdaComponent3D comp;
                    comp.refDes = PathUtils::Utf8ToWide(refStr.empty() ? "SYM" : refStr);
                    comp.value = PathUtils::Utf8ToWide(valStr);
                    comp.footprint = PathUtils::Utf8ToWide(libStr);
                    comp.x = sx;
                    comp.y = -sy;
                    comp.rotDeg = srot;
                    comp.widthMm = 7.62f;
                    comp.lengthMm = 5.08f;
                    // Draw symbol box on schematic
                    float hw = 3.81f, hh = 2.54f;
                    out.traces.push_back({ comp.x - hw, comp.y - hh, comp.x + hw, comp.y - hh, 0.3f, EdaLayerId::SchSymbol, 0 });
                    out.traces.push_back({ comp.x + hw, comp.y - hh, comp.x + hw, comp.y + hh, 0.3f, EdaLayerId::SchSymbol, 0 });
                    out.traces.push_back({ comp.x + hw, comp.y + hh, comp.x - hw, comp.y + hh, 0.3f, EdaLayerId::SchSymbol, 0 });
                    out.traces.push_back({ comp.x - hw, comp.y + hh, comp.x - hw, comp.y - hh, 0.3f, EdaLayerId::SchSymbol, 0 });
                    out.labels.push_back({ comp.x, comp.y, 1.27f, EdaLayerId::SchLabel, comp.refDes + (comp.value.empty() ? L"" : (L" (" + comp.value + L")")) });
                    out.components.push_back(std::move(comp));
                }
            }
            pos = closeP + 1;
            continue;
        }

        pos = kwEnd;
    }

    FinalizeBoardBounds(out);
    out.valid = (!out.traces.empty() || !out.pads.empty() || !out.components.empty() || !out.vias.empty());
    return out.valid;
}
