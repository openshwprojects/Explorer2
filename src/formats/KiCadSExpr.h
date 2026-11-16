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

static bool ExtractSExprSingleFloat(std::string_view block, std::string_view key, float& outVal) {
    size_t pos = block.find(key);
    if (pos == std::string_view::npos) return false;
    pos += key.size();
    size_t end = block.find(')', pos);
    if (end == std::string_view::npos) return false;
    char buf[48] = {};
    std::memcpy(buf, block.data() + pos, std::min<size_t>(end - pos, 47));
    char* ep = nullptr;
    float v = std::strtof(buf, &ep);
    if (ep == buf) return false;
    outVal = v;
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
    if (txt.find("(kicad_pcb") == std::string_view::npos &&
        txt.find("(kicad_sch") == std::string_view::npos &&
        txt.find("(footprint") == std::string_view::npos &&
        txt.find("(module") == std::string_view::npos) {
        return false;
    }

    bool isSch = (ext == L".kicad_sch") || (txt.find("(kicad_sch") != std::string_view::npos);
    out.kind = isSch ? EdaDocKind::KiCadSch : EdaDocKind::KiCadPcb;
    out.formatTitle = isSch ? L"KiCad Schematic (.kicad_sch)" : L"KiCad PCB Layout (.kicad_pcb)";

    struct KiCadSymSeg {
        float x1 = 0, y1 = 0, x2 = 0, y2 = 0, w = 0.25f;
        int unit = 0;
        int convert = 0;
    };
    struct KiCadSymCirc {
        float cx = 0, cy = 0, r = 1.0f, w = 0.25f;
        int unit = 0;
        int convert = 0;
    };
    struct KiCadSymPin {
        float x = 0, y = 0, angleDeg = 0, length = 2.54f;
        std::string name;
        std::string number;
        int unit = 0;
        int convert = 0;
    };
    struct KiCadLibSym {
        std::string extendsId;
        std::vector<KiCadSymSeg> segs;
        std::vector<KiCadSymCirc> circs;
        std::vector<KiCadSymPin> pins;
    };

    std::unordered_map<std::string, KiCadLibSym> libSymMap;
    std::set<std::string> schNetLabels;

    auto tessellate3PtArc = [](std::vector<KiCadSymSeg>& dst, float x1, float y1, float xm, float ym, float x2, float y2,
                               float w, int unit, int convert) {
        double d = 2.0 * (static_cast<double>(x1) * (ym - y2) +
                          static_cast<double>(xm) * (y2 - y1) +
                          static_cast<double>(x2) * (y1 - ym));
        if (std::fabs(d) < 1e-5) {
            dst.push_back({ x1, y1, xm, ym, w, unit, convert });
            dst.push_back({ xm, ym, x2, y2, w, unit, convert });
            return;
        }
        double s1 = static_cast<double>(x1) * x1 + static_cast<double>(y1) * y1;
        double sm = static_cast<double>(xm) * xm + static_cast<double>(ym) * ym;
        double s2 = static_cast<double>(x2) * x2 + static_cast<double>(y2) * y2;
        double ux = (s1 * (ym - y2) + sm * (y2 - y1) + s2 * (y1 - ym)) / d;
        double uy = (s1 * (x2 - xm) + sm * (x1 - x2) + s2 * (xm - x1)) / d;
        double r = std::hypot(x1 - ux, y1 - uy);
        if (!std::isfinite(r) || r <= 1e-4 || r > 5000.0) {
            dst.push_back({ x1, y1, xm, ym, w, unit, convert });
            dst.push_back({ xm, ym, x2, y2, w, unit, convert });
            return;
        }
        constexpr double kTwoPi = 6.2831853071795864769;
        double a1 = std::atan2(y1 - uy, x1 - ux);
        double am = std::atan2(ym - uy, xm - ux);
        double a2 = std::atan2(y2 - uy, x2 - ux);
        auto normCCW = [&](double a) -> double {
            double da = std::fmod(a - a1, kTwoPi);
            if (da < 0.0) da += kTwoPi;
            return da;
        };
        double dm = normCCW(am);
        double d2 = normCCW(a2);
        double sweep = (dm <= d2) ? d2 : -(kTwoPi - d2);

        const int steps = 10;
        float px = x1, py = y1;
        for (int s = 1; s <= steps; ++s) {
            double ang = a1 + sweep * (static_cast<double>(s) / steps);
            float qx = (s == steps) ? x2 : static_cast<float>(ux + r * std::cos(ang));
            float qy = (s == steps) ? y2 : static_cast<float>(uy + r * std::sin(ang));
            dst.push_back({ px, py, qx, qy, w, unit, convert });
            px = qx; py = qy;
        }
    };

    auto parseSymGraphicsBlock = [&](std::string_view gBlk, KiCadLibSym& dstSym, int unitNum, int convertNum) {
        size_t gp = 1;
        // Skip header keyword + quoted symbol name
        while (gp < gBlk.size() && gBlk[gp] != '(') {
            if (gBlk[gp] == '"') {
                ++gp;
                while (gp < gBlk.size() && !(gBlk[gp] == '"' && gBlk[gp - 1] != '\\')) ++gp;
            }
            if (gp < gBlk.size()) ++gp;
        }

        while (gp < gBlk.size()) {
            size_t cOpen = gBlk.find('(', gp);
            if (cOpen == std::string_view::npos || cOpen + 2 >= gBlk.size()) break;
            size_t cClose = FindMatchingParen(gBlk, cOpen);
            if (cClose == std::string_view::npos) break;

            size_t cKwEnd = cOpen + 1;
            while (cKwEnd < cClose && gBlk[cKwEnd] > ' ' && gBlk[cKwEnd] != '(' && gBlk[cKwEnd] != ')') ++cKwEnd;
            std::string_view cKw = gBlk.substr(cOpen + 1, cKwEnd - (cOpen + 1));
            std::string_view item = gBlk.substr(cOpen, cClose - cOpen + 1);

            if (cKw == "polyline") {
                float strokeW = 0.25f;
                ExtractSExprSingleFloat(item, "(width ", strokeW);
                strokeW = std::clamp(strokeW, 0.16f, 2.5f);
                size_t ptsPos = item.find("(pts");
                if (ptsPos != std::string_view::npos) {
                    size_t ptsEnd = FindMatchingParen(item, ptsPos);
                    std::string_view ptsBlk = (ptsEnd != std::string_view::npos) ? item.substr(ptsPos, ptsEnd - ptsPos + 1) : item.substr(ptsPos);
                    std::vector<std::pair<float, float>> pts;
                    size_t xp = 0;
                    while ((xp = ptsBlk.find("(xy ", xp)) != std::string_view::npos) {
                        float vx = 0.0f, vy = 0.0f;
                        if (ExtractSExprFloats(ptsBlk.substr(xp), "(xy ", vx, vy)) {
                            pts.push_back({ vx, vy });
                        }
                        xp += 4;
                    }
                    for (size_t k = 0; k + 1 < pts.size(); ++k) {
                        dstSym.segs.push_back({ pts[k].first, pts[k].second, pts[k + 1].first, pts[k + 1].second, strokeW, unitNum, convertNum });
                    }
                }
            } else if (cKw == "rectangle") {
                float x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f, strokeW = 0.25f;
                if (ExtractSExprFloats(item, "(start ", x1, y1) && ExtractSExprFloats(item, "(end ", x2, y2)) {
                    ExtractSExprSingleFloat(item, "(width ", strokeW);
                    strokeW = std::clamp(strokeW, 0.18f, 2.5f);
                    dstSym.segs.push_back({ x1, y1, x2, y1, strokeW, unitNum, convertNum });
                    dstSym.segs.push_back({ x2, y1, x2, y2, strokeW, unitNum, convertNum });
                    dstSym.segs.push_back({ x2, y2, x1, y2, strokeW, unitNum, convertNum });
                    dstSym.segs.push_back({ x1, y2, x1, y1, strokeW, unitNum, convertNum });
                }
            } else if (cKw == "circle") {
                float cx = 0.0f, cy = 0.0f, r = 1.0f, strokeW = 0.25f;
                if (ExtractSExprFloats(item, "(center ", cx, cy) && ExtractSExprSingleFloat(item, "(radius ", r)) {
                    ExtractSExprSingleFloat(item, "(width ", strokeW);
                    dstSym.circs.push_back({ cx, cy, std::max(0.15f, r), std::clamp(strokeW, 0.16f, 2.5f), unitNum, convertNum });
                }
            } else if (cKw == "arc") {
                float x1 = 0.0f, y1 = 0.0f, xm = 0.0f, ym = 0.0f, x2 = 0.0f, y2 = 0.0f, strokeW = 0.25f;
                if (ExtractSExprFloats(item, "(start ", x1, y1) &&
                    ExtractSExprFloats(item, "(mid ", xm, ym) &&
                    ExtractSExprFloats(item, "(end ", x2, y2))
                {
                    ExtractSExprSingleFloat(item, "(width ", strokeW);
                    tessellate3PtArc(dstSym.segs, x1, y1, xm, ym, x2, y2, std::clamp(strokeW, 0.16f, 2.5f), unitNum, convertNum);
                }
            } else if (cKw == "pin") {
                if (item.find(" hide)") == std::string_view::npos && item.find("(hide yes)") == std::string_view::npos) {
                    float px = 0.0f, py = 0.0f, pang = 0.0f, plen = 2.54f;
                    if (ExtractSExprFloats(item, "(at ", px, py, &pang)) {
                        ExtractSExprSingleFloat(item, "(length ", plen);
                        KiCadSymPin pin;
                        pin.x = px;
                        pin.y = py;
                        pin.angleDeg = pang;
                        pin.length = std::max(0.0f, plen);
                        pin.name = ExtractSExprQuotedOrAtom(item, "(name ");
                        pin.number = ExtractSExprQuotedOrAtom(item, "(number ");
                        pin.unit = unitNum;
                        pin.convert = convertNum;
                        dstSym.pins.push_back(std::move(pin));
                    }
                }
            }

            gp = cClose + 1;
        }
    };

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
            if (isSch) {
                std::string_view libBlk = txt.substr(openP, closeP - openP + 1);
                size_t lp = 1 + kw.size();
                while (lp < libBlk.size()) {
                    size_t sOpen = libBlk.find('(', lp);
                    if (sOpen == std::string_view::npos || sOpen + 2 >= libBlk.size()) break;
                    size_t sClose = FindMatchingParen(libBlk, sOpen);
                    if (sClose == std::string_view::npos) break;
                    std::string_view sBlk = libBlk.substr(sOpen, sClose - sOpen + 1);
                    if (sBlk.rfind("(symbol ", 0) == 0) {
                        std::string libId = ExtractSExprQuotedOrAtom(sBlk, "(symbol ");
                        if (!libId.empty()) {
                            KiCadLibSym sym;
                            sym.extendsId = ExtractSExprQuotedOrAtom(sBlk, "(extends ");
                            parseSymGraphicsBlock(sBlk, sym, 0, 0);

                            // Parse nested unit sub-symbols (symbol "<name>_<unit>_<style>" ...)
                            size_t up = 8;
                            while (up < sBlk.size() && sBlk[up] != '(') {
                                if (sBlk[up] == '"') {
                                    ++up;
                                    while (up < sBlk.size() && !(sBlk[up] == '"' && sBlk[up - 1] != '\\')) ++up;
                                }
                                if (up < sBlk.size()) ++up;
                            }
                            while (up < sBlk.size()) {
                                size_t uOpen = sBlk.find('(', up);
                                if (uOpen == std::string_view::npos || uOpen + 2 >= sBlk.size()) break;
                                size_t uClose = FindMatchingParen(sBlk, uOpen);
                                if (uClose == std::string_view::npos) break;
                                std::string_view uBlk = sBlk.substr(uOpen, uClose - uOpen + 1);
                                if (uBlk.rfind("(symbol ", 0) == 0) {
                                    std::string subName = ExtractSExprQuotedOrAtom(uBlk, "(symbol ");
                                    int unitN = 0, convN = 0;
                                    size_t us2 = subName.rfind('_');
                                    if (us2 != std::string::npos && us2 > 0) {
                                        size_t us1 = subName.rfind('_', us2 - 1);
                                        if (us1 != std::string::npos) {
                                            unitN = std::atoi(subName.c_str() + us1 + 1);
                                            convN = std::atoi(subName.c_str() + us2 + 1);
                                        }
                                    }
                                    parseSymGraphicsBlock(uBlk, sym, unitN, convN);
                                }
                                up = uClose + 1;
                            }

                            libSymMap[libId] = std::move(sym);
                            size_t colon = libId.find(':');
                            if (colon != std::string::npos && !libSymMap.count(libId.substr(colon + 1))) {
                                libSymMap[libId.substr(colon + 1)] = libSymMap[libId];
                            }
                        }
                    }
                    lp = sClose + 1;
                }
            }
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
            comp.bottomSide = (lyr.find("B.Cu") != std::string_view::npos);

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

        if (isSch && (kw == "wire" || kw == "bus" || kw == "polyline")) {
            size_t closeP = FindMatchingParen(txt, openP);
            if (closeP == std::string_view::npos) break;
            std::string_view blk = txt.substr(openP, closeP - openP + 1);
            std::vector<std::pair<float, float>> pts;
            size_t xp = 0;
            while ((xp = blk.find("(xy ", xp)) != std::string_view::npos) {
                float vx = 0.0f, vy = 0.0f;
                if (ExtractSExprFloats(blk.substr(xp), "(xy ", vx, vy)) {
                    pts.push_back({ vx, -vy });
                }
                xp += 4;
            }
            EdaLayerId lid = (kw == "polyline") ? EdaLayerId::SchSymbol : EdaLayerId::SchWire;
            float strokeW = (kw == "bus") ? 0.50f : 0.25f;
            for (size_t k = 0; k + 1 < pts.size(); ++k) {
                out.traces.push_back({ pts[k].first, pts[k].second, pts[k + 1].first, pts[k + 1].second, strokeW, lid, 0 });
            }
            pos = closeP + 1;
            continue;
        }

        if (isSch && kw == "junction") {
            size_t closeP = FindMatchingParen(txt, openP);
            if (closeP == std::string_view::npos) break;
            std::string_view blk = txt.substr(openP, closeP - openP + 1);
            float jx = 0.0f, jy = 0.0f;
            if (ExtractSExprFloats(blk, "(at ", jx, jy)) {
                out.circles.push_back({ jx, -jy, 0.45f, 0.25f, true, EdaLayerId::SchWire });
            }
            pos = closeP + 1;
            continue;
        }

        if (isSch && (kw == "label" || kw == "global_label" || kw == "hierarchical_label" || kw == "text")) {
            size_t closeP = FindMatchingParen(txt, openP);
            if (closeP == std::string_view::npos) break;
            std::string_view blk = txt.substr(openP, closeP - openP + 1);
            std::string prefix = "(" + std::string(kw) + " ";
            std::string lblStr = ExtractSExprQuotedOrAtom(blk, prefix);
            float lx = 0.0f, ly = 0.0f, lrot = 0.0f;
            if (!lblStr.empty() && ExtractSExprFloats(blk, "(at ", lx, ly, &lrot)) {
                EdaLayerId lid = (kw == "text") ? EdaLayerId::SchLabel : EdaLayerId::SchWire;
                if (kw != "text") schNetLabels.insert(lblStr);
                if (out.labels.size() < 2048) {
                    out.labels.push_back({ lx, -ly + 0.35f, 1.15f, lid, PathUtils::Utf8ToWide(lblStr) });
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
                    std::string unitStr = ExtractSExprQuotedOrAtom(blk, "(unit ");
                    std::string convStr = ExtractSExprQuotedOrAtom(blk, "(convert ");
                    int placedUnit = unitStr.empty() ? 1 : std::max(1, std::atoi(unitStr.c_str()));
                    int placedConv = convStr.empty() ? 1 : std::max(1, std::atoi(convStr.c_str()));

                    bool mirrorX = false, mirrorY = false;
                    std::string mirAtom = ExtractSExprQuotedOrAtom(blk, "(mirror ");
                    if (mirAtom == "x") mirrorX = true;
                    else if (mirAtom == "y") mirrorY = true;

                    EdaComponent3D comp;
                    comp.refDes = PathUtils::Utf8ToWide(refStr.empty() ? "SYM" : refStr);
                    comp.value = PathUtils::Utf8ToWide(valStr);
                    comp.footprint = PathUtils::Utf8ToWide(libStr);
                    comp.x = sx;
                    comp.y = -sy;
                    comp.rotDeg = srot;

                    const KiCadLibSym* resolved = nullptr;
                    auto symIt = libSymMap.find(libStr);
                    if (symIt == libSymMap.end()) {
                        size_t colon = libStr.find(':');
                        if (colon != std::string::npos) symIt = libSymMap.find(libStr.substr(colon + 1));
                    }
                    if (symIt != libSymMap.end()) {
                        resolved = &symIt->second;
                        if (resolved->segs.empty() && resolved->pins.empty() && !resolved->extendsId.empty()) {
                            auto extIt = libSymMap.find(resolved->extendsId);
                            if (extIt != libSymMap.end()) resolved = &extIt->second;
                        }
                    }

                    const float rad = srot * 3.14159265f / 180.0f;
                    const float cs = std::cos(rad);
                    const float sn = std::sin(rad);
                    auto xformLocal = [&](float lx, float ly, float& ox, float& oy) {
                        float rx = lx * cs - ly * sn;
                        float ry = lx * sn + ly * cs;
                        if (mirrorY) rx = -rx;
                        if (mirrorX) ry = -ry;
                        ox = comp.x + rx;
                        oy = comp.y + ry;
                    };
                    auto matchUnit = [&](int u, int c) -> bool {
                        return (u == 0 || u == placedUnit) && (c == 0 || c == placedConv);
                    };

                    bool emittedAny = false;
                    float minLx = 1e9f, minLy = 1e9f, maxLx = -1e9f, maxLy = -1e9f;
                    auto expandLocal = [&](float x, float y) {
                        minLx = std::min(minLx, x);
                        minLy = std::min(minLy, y);
                        maxLx = std::max(maxLx, x);
                        maxLy = std::max(maxLy, y);
                    };

                    if (resolved) {
                        for (const auto& sg : resolved->segs) {
                            if (!matchUnit(sg.unit, sg.convert)) continue;
                            float bx1 = 0, by1 = 0, bx2 = 0, by2 = 0;
                            xformLocal(sg.x1, sg.y1, bx1, by1);
                            xformLocal(sg.x2, sg.y2, bx2, by2);
                            out.traces.push_back({ bx1, by1, bx2, by2, sg.w, EdaLayerId::SchSymbol, 0 });
                            expandLocal(sg.x1, sg.y1);
                            expandLocal(sg.x2, sg.y2);
                            emittedAny = true;
                        }
                        for (const auto& cr : resolved->circs) {
                            if (!matchUnit(cr.unit, cr.convert)) continue;
                            float bcx = 0, bcy = 0;
                            xformLocal(cr.cx, cr.cy, bcx, bcy);
                            out.circles.push_back({ bcx, bcy, cr.r, cr.w, false, EdaLayerId::SchSymbol });
                            expandLocal(cr.cx - cr.r, cr.cy - cr.r);
                            expandLocal(cr.cx + cr.r, cr.cy + cr.r);
                            emittedAny = true;
                        }
                        for (const auto& pn : resolved->pins) {
                            if (!matchUnit(pn.unit, pn.convert)) continue;
                            float tipX = 0, tipY = 0;
                            xformLocal(pn.x, pn.y, tipX, tipY);

                            if (pn.length > 0.02f) {
                                float prad = pn.angleDeg * 3.14159265f / 180.0f;
                                float inX = pn.x + pn.length * std::cos(prad);
                                float inY = pn.y + pn.length * std::sin(prad);
                                float bodyX = 0, bodyY = 0;
                                xformLocal(inX, inY, bodyX, bodyY);
                                out.traces.push_back({ tipX, tipY, bodyX, bodyY, 0.22f, EdaLayerId::SchSymbol, 0 });
                                expandLocal(inX, inY);
                            }
                            expandLocal(pn.x, pn.y);

                            EdaPadPrim pad;
                            pad.x = tipX;
                            pad.y = tipY;
                            pad.w = 0.55f;
                            pad.h = 0.55f;
                            pad.shape = 0;
                            pad.layer = EdaLayerId::SchSymbol;
                            pad.pinName = PathUtils::Utf8ToWide(pn.number.empty() ? pn.name : pn.number);
                            out.pads.push_back(std::move(pad));
                            ++comp.pinCount;
                            emittedAny = true;
                        }
                    }

                    if (!emittedAny) {
                        comp.widthMm = 7.62f;
                        comp.lengthMm = 5.08f;
                        float hw = 3.81f, hh = 2.54f;
                        out.traces.push_back({ comp.x - hw, comp.y - hh, comp.x + hw, comp.y - hh, 0.3f, EdaLayerId::SchSymbol, 0 });
                        out.traces.push_back({ comp.x + hw, comp.y - hh, comp.x + hw, comp.y + hh, 0.3f, EdaLayerId::SchSymbol, 0 });
                        out.traces.push_back({ comp.x + hw, comp.y + hh, comp.x - hw, comp.y + hh, 0.3f, EdaLayerId::SchSymbol, 0 });
                        out.traces.push_back({ comp.x - hw, comp.y + hh, comp.x - hw, comp.y - hh, 0.3f, EdaLayerId::SchSymbol, 0 });
                    } else if (minLx < maxLx && minLy < maxLy) {
                        comp.widthMm  = std::clamp(maxLx - minLx, 1.5f, 60.0f);
                        comp.lengthMm = std::clamp(maxLy - minLy, 1.5f, 60.0f);
                    }

                    std::wstring disp = comp.refDes + (comp.value.empty() ? L"" : (L" (" + comp.value + L")"));
                    out.labels.push_back({ comp.x - comp.widthMm * 0.3f, comp.y + comp.lengthMm * 0.5f + 1.2f, 1.15f, EdaLayerId::SchLabel, disp });
                    out.components.push_back(std::move(comp));
                }
            }
            pos = closeP + 1;
            continue;
        }

        pos = kwEnd;
    }

    if (isSch && out.netCount == 0 && !schNetLabels.empty()) {
        out.netCount = schNetLabels.size();
    }

    FinalizeBoardBounds(out);
    out.valid = (!out.traces.empty() || !out.pads.empty() || !out.components.empty() || !out.vias.empty());
    return out.valid;
}
