#pragma once
// KiCadSchematic.h — KiCad S-Expression Schematic (.kicad_sch) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

inline bool ParseKiCadSchematic(std::string_view txt, ParsedEdaBoard& out) {
    using namespace DetailKiCadSExpr;
    if (txt.find("(kicad_sch") == std::string_view::npos) {
        return false;
    }

    const bool isSch = true;
    out.kind = EdaDocKind::KiCadSch;
    out.formatTitle = L"KiCad Schematic (.kicad_sch)";

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
