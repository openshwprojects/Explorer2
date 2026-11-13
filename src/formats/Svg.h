#pragma once
// ============================================================================
// 9. Scalable Vector Graphics (.svg) — Native 2D Vector Shape & Path Rasterizer
// ============================================================================
bool DecodeSVG(const uint8_t* data, size_t size, uint32_t maxDimension, ExtendedImageInfo& outInfo) {
    std::string_view sv(reinterpret_cast<const char*>(data), std::min<size_t>(size, 8 * 1024 * 1024));
    size_t svgTag = sv.find("<svg");
    if (svgTag == std::string_view::npos) return false;

    auto extractAttr = [](std::string_view tag, std::string_view attrName) -> std::string_view {
        size_t pos = 0;
        while ((pos = tag.find(attrName, pos)) != std::string_view::npos) {
            if (pos > 0) {
                char prev = tag[pos - 1];
                if ((prev >= 'a' && prev <= 'z') || (prev >= 'A' && prev <= 'Z') || prev == '-') {
                    pos += attrName.size();
                    continue;
                }
            }
            size_t eq = pos + attrName.size();
            while (eq < tag.size() && (tag[eq] == ' ' || tag[eq] == '\t')) ++eq;
            if (eq < tag.size() && tag[eq] == '=') {
                ++eq;
                while (eq < tag.size() && (tag[eq] == ' ' || tag[eq] == '\t')) ++eq;
                if (eq < tag.size() && (tag[eq] == '"' || tag[eq] == '\'')) {
                    char q = tag[eq++];
                    size_t endQ = tag.find(q, eq);
                    if (endQ != std::string_view::npos) return tag.substr(eq, endQ - eq);
                }
            }
            pos += attrName.size();
        }
        return {};
    };

    auto parseSvgColor = [](std::string_view col, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) -> bool {
        while (!col.empty() && (col.front() == ' ' || col.front() == '\t')) col.remove_prefix(1);
        while (!col.empty() && (col.back() == ' ' || col.back() == '\t')) col.remove_suffix(1);
        if (col.empty() || col == "none" || col == "transparent") { a = 0; return false; }
        a = 255;
        if (col[0] == '#') {
            if (col.size() >= 7) {
                unsigned int v = static_cast<unsigned int>(std::strtoul(std::string(col.substr(1, 6)).c_str(), nullptr, 16));
                r = static_cast<uint8_t>((v >> 16) & 0xFF);
                g = static_cast<uint8_t>((v >> 8) & 0xFF);
                b = static_cast<uint8_t>(v & 0xFF);
                return true;
            } else if (col.size() >= 4) {
                auto hex1 = [](char c) -> uint8_t {
                    if (c >= '0' && c <= '9') return static_cast<uint8_t>((c - '0') * 17);
                    if (c >= 'a' && c <= 'f') return static_cast<uint8_t>((c - 'a' + 10) * 17);
                    if (c >= 'A' && c <= 'F') return static_cast<uint8_t>((c - 'A' + 10) * 17);
                    return 0;
                };
                r = hex1(col[1]); g = hex1(col[2]); b = hex1(col[3]);
                return true;
            }
        }
        if (col.substr(0, 4) == "rgb(") {
            int ir = 0, ig = 0, ib = 0;
            if (sscanf_s(std::string(col.substr(4)).c_str(), "%d , %d , %d", &ir, &ig, &ib) == 3) {
                r = static_cast<uint8_t>(std::clamp(ir, 0, 255));
                g = static_cast<uint8_t>(std::clamp(ig, 0, 255));
                b = static_cast<uint8_t>(std::clamp(ib, 0, 255));
                return true;
            }
        }
        if (col == "black" || col == "currentColor") { r = 30; g = 41; b = 59; return true; }
        if (col == "white")  { r = 255; g = 255; b = 255; return true; }
        if (col == "red")    { r = 220; g = 38;  b = 38;  return true; }
        if (col == "green")  { r = 22;  g = 163; b = 74;  return true; }
        if (col == "blue")   { r = 37;  g = 99;  b = 235; return true; }
        if (col == "orange" || col == "gold" || col == "yellow") { r = 234; g = 179; b = 8; return true; }
        if (col == "cyan" || col == "teal") { r = 14; g = 165; b = 233; return true; }
        if (col == "purple" || col == "violet" || col == "magenta") { r = 147; g = 51; b = 234; return true; }
        if (col == "gray" || col == "grey") { r = 100; g = 116; b = 139; return true; }
        r = 37; g = 99; b = 235;
        return true;
    };

    size_t svgClose = sv.find('>', svgTag);
    if (svgClose == std::string_view::npos) return false;
    std::string_view svgHeader = sv.substr(svgTag, svgClose - svgTag + 1);

    float vbX = 0.0f, vbY = 0.0f, vbW = 0.0f, vbH = 0.0f;
    std::string_view vbAttr = extractAttr(svgHeader, "viewBox");
    if (!vbAttr.empty()) {
        std::string vbStr(vbAttr);
        for (char& c : vbStr) if (c == ',') c = ' ';
        sscanf_s(vbStr.c_str(), "%f %f %f %f", &vbX, &vbY, &vbW, &vbH);
    }
    if (vbW <= 0.0f || vbH <= 0.0f) {
        vbW = static_cast<float>(std::atof(std::string(extractAttr(svgHeader, "width")).c_str()));
        vbH = static_cast<float>(std::atof(std::string(extractAttr(svgHeader, "height")).c_str()));
        if (vbW <= 0.0f) vbW = 256.0f;
        if (vbH <= 0.0f) vbH = 256.0f;
    }

    uint32_t targetDim = (maxDimension > 0 && maxDimension <= 1024) ? maxDimension : 512;
    uint32_t outW = targetDim;
    uint32_t outH = targetDim;
    if (vbW >= vbH) {
        outH = std::max(16u, static_cast<uint32_t>(std::round(targetDim * (vbH / vbW))));
    } else {
        outW = std::max(16u, static_cast<uint32_t>(std::round(targetDim * (vbW / vbH))));
    }

    outInfo.width = outW;
    outInfo.height = outH;
    outInfo.origWidth = static_cast<uint32_t>(std::round(vbW));
    outInfo.origHeight = static_cast<uint32_t>(std::round(vbH));
    outInfo.channels = 4;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = L"SVG Vector Graphic (.svg)";
    outInfo.bgraPixels.assign(static_cast<size_t>(outW) * outH * 4, 0);

    float scaleX = static_cast<float>(outW) / std::max(1e-4f, vbW);
    float scaleY = static_cast<float>(outH) / std::max(1e-4f, vbH);
    float scale  = std::min(scaleX, scaleY);

    auto blendPixel = [&](int px, int py, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        if (px < 0 || py < 0 || px >= static_cast<int>(outW) || py >= static_cast<int>(outH) || a == 0) return;
        uint8_t* dst = &outInfo.bgraPixels[(static_cast<size_t>(py) * outW + px) * 4];
        if (a == 255 || dst[3] == 0) {
            WritePremulBGRA(dst, r, g, b, a);
        } else {
            uint32_t invA = 255 - a;
            dst[0] = static_cast<uint8_t>(std::min<uint32_t>(255, (static_cast<uint32_t>(b) * a + dst[0] * invA) / 255));
            dst[1] = static_cast<uint8_t>(std::min<uint32_t>(255, (static_cast<uint32_t>(g) * a + dst[1] * invA) / 255));
            dst[2] = static_cast<uint8_t>(std::min<uint32_t>(255, (static_cast<uint32_t>(r) * a + dst[2] * invA) / 255));
            dst[3] = static_cast<uint8_t>(std::min<uint32_t>(255, a + (dst[3] * invA) / 255));
        }
    };

    auto drawThickSeg = [&](float x0, float y0, float x1, float y1, float thick, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        float sx0 = (x0 - vbX) * scaleX, sy0 = (y0 - vbY) * scaleY;
        float sx1 = (x1 - vbX) * scaleX, sy1 = (y1 - vbY) * scaleY;
        float rad = std::max(0.75f, thick * scale * 0.5f);
        float len = std::hypot(sx1 - sx0, sy1 - sy0);
        int steps = std::max(1, static_cast<int>(std::ceil(len * 2.0f)));
        int ir = static_cast<int>(std::ceil(rad));
        for (int s = 0; s <= steps; ++s) {
            float t = static_cast<float>(s) / static_cast<float>(steps);
            float cx = sx0 + (sx1 - sx0) * t;
            float cy = sy0 + (sy1 - sy0) * t;
            for (int dy = -ir; dy <= ir; ++dy) {
                for (int dx = -ir; dx <= ir; ++dx) {
                    if (dx * dx + dy * dy <= rad * rad + 0.5f) {
                        blendPixel(static_cast<int>(cx) + dx, static_cast<int>(cy) + dy, r, g, b, a);
                    }
                }
            }
        }
    };

    auto fillPolygon = [&](const std::vector<std::pair<float, float>>& pts, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        if (pts.size() < 3 || a == 0) return;
        float minY = 1e9f, maxY = -1e9f;
        std::vector<std::pair<float, float>> spts;
        spts.reserve(pts.size());
        for (const auto& p : pts) {
            float sx = (p.first - vbX) * scaleX;
            float sy = (p.second - vbY) * scaleY;
            minY = std::min(minY, sy);
            maxY = std::max(maxY, sy);
            spts.push_back({ sx, sy });
        }
        int y0 = std::max(0, static_cast<int>(std::floor(minY)));
        int y1 = std::min(static_cast<int>(outH) - 1, static_cast<int>(std::ceil(maxY)));
        std::vector<float> nodes;
        for (int y = y0; y <= y1; ++y) {
            float fy = static_cast<float>(y) + 0.5f;
            nodes.clear();
            size_t j = spts.size() - 1;
            for (size_t i = 0; i < spts.size(); ++i) {
                if ((spts[i].second < fy && spts[j].second >= fy) || (spts[j].second < fy && spts[i].second >= fy)) {
                    nodes.push_back(spts[i].first + (fy - spts[i].second) / (spts[j].second - spts[i].second) * (spts[j].first - spts[i].first));
                }
                j = i;
            }
            std::sort(nodes.begin(), nodes.end());
            for (size_t i = 0; i + 1 < nodes.size(); i += 2) {
                int xStart = std::max(0, static_cast<int>(std::floor(nodes[i])));
                int xEnd   = std::min(static_cast<int>(outW) - 1, static_cast<int>(std::ceil(nodes[i + 1])));
                for (int x = xStart; x <= xEnd; ++x) {
                    blendPixel(x, y, r, g, b, a);
                }
            }
        }
    };

    auto fillCompoundPolygon = [&](const std::vector<std::vector<std::pair<float, float>>>& rings, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        if (rings.empty() || a == 0) return;
        float minY = 1e9f, maxY = -1e9f;
        std::vector<std::vector<std::pair<float, float>>> srings;
        srings.reserve(rings.size());
        for (const auto& pts : rings) {
            if (pts.size() < 3) continue;
            std::vector<std::pair<float, float>> spts;
            spts.reserve(pts.size());
            for (const auto& p : pts) {
                float sx = (p.first - vbX) * scaleX;
                float sy = (p.second - vbY) * scaleY;
                minY = std::min(minY, sy);
                maxY = std::max(maxY, sy);
                spts.push_back({ sx, sy });
            }
            srings.push_back(std::move(spts));
        }
        if (srings.empty()) return;
        int y0 = std::max(0, static_cast<int>(std::floor(minY)));
        int y1 = std::min(static_cast<int>(outH) - 1, static_cast<int>(std::ceil(maxY)));
        std::vector<float> nodes;
        for (int y = y0; y <= y1; ++y) {
            float fy = static_cast<float>(y) + 0.5f;
            nodes.clear();
            for (const auto& spts : srings) {
                size_t j = spts.size() - 1;
                for (size_t i = 0; i < spts.size(); ++i) {
                    if ((spts[i].second < fy && spts[j].second >= fy) || (spts[j].second < fy && spts[i].second >= fy)) {
                        nodes.push_back(spts[i].first + (fy - spts[i].second) / (spts[j].second - spts[i].second) * (spts[j].first - spts[i].first));
                    }
                    j = i;
                }
            }
            std::sort(nodes.begin(), nodes.end());
            for (size_t i = 0; i + 1 < nodes.size(); i += 2) {
                int xStart = std::max(0, static_cast<int>(std::floor(nodes[i])));
                int xEnd   = std::min(static_cast<int>(outW) - 1, static_cast<int>(std::ceil(nodes[i + 1])));
                for (int x = xStart; x <= xEnd; ++x) {
                    blendPixel(x, y, r, g, b, a);
                }
            }
        }
    };

    auto extractStyleOrAttr = [&](std::string_view tag, std::string_view propName) -> std::string_view {
        std::string_view direct = extractAttr(tag, propName);
        if (!direct.empty()) return direct;
        std::string_view styleSv = extractAttr(tag, "style");
        if (styleSv.empty()) return {};
        size_t p = 0;
        while ((p = styleSv.find(propName, p)) != std::string_view::npos) {
            if (p > 0 && styleSv[p - 1] != ';' && styleSv[p - 1] != ' ' && styleSv[p - 1] != '\t') {
                p += propName.size();
                continue;
            }
            size_t colon = p + propName.size();
            while (colon < styleSv.size() && (styleSv[colon] == ' ' || styleSv[colon] == '\t')) ++colon;
            if (colon < styleSv.size() && styleSv[colon] == ':') {
                ++colon;
                while (colon < styleSv.size() && (styleSv[colon] == ' ' || styleSv[colon] == '\t')) ++colon;
                size_t semi = styleSv.find(';', colon);
                if (semi == std::string_view::npos) semi = styleSv.size();
                return styleSv.substr(colon, semi - colon);
            }
            p += propName.size();
        }
        return {};
    };

    uint8_t defFr = 37, defFg = 99, defFb = 235, defFa = 255;
    uint8_t defSr = 15, defSg = 23, defSb = 42,  defSa = 0;
    bool defHasFill = true;
    {
        std::string_view rootFill = extractStyleOrAttr(svgHeader, "fill");
        if (!rootFill.empty()) defHasFill = parseSvgColor(rootFill, defFr, defFg, defFb, defFa);
        std::string_view rootStroke = extractStyleOrAttr(svgHeader, "stroke");
        if (!rootStroke.empty()) parseSvgColor(rootStroke, defSr, defSg, defSb, defSa);
    }

    size_t pos = svgClose + 1;
    while (pos < sv.size()) {
        size_t lt = sv.find('<', pos);
        if (lt == std::string_view::npos) break;
        size_t gt = sv.find('>', lt + 1);
        if (gt == std::string_view::npos) break;
        std::string_view tag = sv.substr(lt + 1, gt - lt - 1);
        pos = gt + 1;
        if (tag.empty() || tag[0] == '!' || tag[0] == '?') continue;
        if (tag[0] == '/') continue;

        size_t nameEnd = 0;
        while (nameEnd < tag.size() && tag[nameEnd] != ' ' && tag[nameEnd] != '\t' && tag[nameEnd] != '/' && tag[nameEnd] != '\r' && tag[nameEnd] != '\n') ++nameEnd;
        std::string_view elem = tag.substr(0, nameEnd);

        if (elem == "g") {
            std::string_view gFill = extractStyleOrAttr(tag, "fill");
            if (!gFill.empty()) defHasFill = parseSvgColor(gFill, defFr, defFg, defFb, defFa);
            std::string_view gStroke = extractStyleOrAttr(tag, "stroke");
            if (!gStroke.empty()) parseSvgColor(gStroke, defSr, defSg, defSb, defSa);
            continue;
        }

        uint8_t fr = defFr, fg = defFg, fb = defFb, fa = defFa;
        uint8_t sr = defSr, sg = defSg, sb = defSb, sa = defSa;
        bool hasFill = defHasFill;
        std::string_view fillAttr = extractStyleOrAttr(tag, "fill");
        if (!fillAttr.empty()) {
            hasFill = parseSvgColor(fillAttr, fr, fg, fb, fa);
        }
        std::string_view strokeAttr = extractStyleOrAttr(tag, "stroke");
        if (!strokeAttr.empty()) {
            parseSvgColor(strokeAttr, sr, sg, sb, sa);
        }
        float strokeW = static_cast<float>(std::atof(std::string(extractStyleOrAttr(tag, "stroke-width")).c_str()));
        if (strokeW <= 0.0f) strokeW = 1.5f;

        if (elem == "rect") {
            float rx = static_cast<float>(std::atof(std::string(extractAttr(tag, "x")).c_str()));
            float ry = static_cast<float>(std::atof(std::string(extractAttr(tag, "y")).c_str()));
            float rw = static_cast<float>(std::atof(std::string(extractAttr(tag, "width")).c_str()));
            float rh = static_cast<float>(std::atof(std::string(extractAttr(tag, "height")).c_str()));
            if (rw > 0.0f && rh > 0.0f) {
                std::vector<std::pair<float, float>> pts = { {rx, ry}, {rx + rw, ry}, {rx + rw, ry + rh}, {rx, ry + rh} };
                if (hasFill) fillPolygon(pts, fr, fg, fb, fa);
                if (sa > 0) {
                    for (size_t i = 0; i < 4; ++i) drawThickSeg(pts[i].first, pts[i].second, pts[(i + 1) % 4].first, pts[(i + 1) % 4].second, strokeW, sr, sg, sb, sa);
                }
            }
        } else if (elem == "circle" || elem == "ellipse") {
            float cx = static_cast<float>(std::atof(std::string(extractAttr(tag, "cx")).c_str()));
            float cy = static_cast<float>(std::atof(std::string(extractAttr(tag, "cy")).c_str()));
            float rx = (elem == "circle")
                ? static_cast<float>(std::atof(std::string(extractAttr(tag, "r")).c_str()))
                : static_cast<float>(std::atof(std::string(extractAttr(tag, "rx")).c_str()));
            float ry = (elem == "circle") ? rx : static_cast<float>(std::atof(std::string(extractAttr(tag, "ry")).c_str()));
            if (rx > 0.0f && ry > 0.0f) {
                std::vector<std::pair<float, float>> pts;
                for (int i = 0; i < 48; ++i) {
                    float a = static_cast<float>(i) * (6.2831853f / 48.0f);
                    pts.push_back({ cx + std::cos(a) * rx, cy + std::sin(a) * ry });
                }
                if (hasFill) fillPolygon(pts, fr, fg, fb, fa);
                if (sa > 0) {
                    for (size_t i = 0; i < pts.size(); ++i) drawThickSeg(pts[i].first, pts[i].second, pts[(i + 1) % pts.size()].first, pts[(i + 1) % pts.size()].second, strokeW, sr, sg, sb, sa);
                }
            }
        } else if (elem == "line") {
            float x1 = static_cast<float>(std::atof(std::string(extractAttr(tag, "x1")).c_str()));
            float y1 = static_cast<float>(std::atof(std::string(extractAttr(tag, "y1")).c_str()));
            float x2 = static_cast<float>(std::atof(std::string(extractAttr(tag, "x2")).c_str()));
            float y2 = static_cast<float>(std::atof(std::string(extractAttr(tag, "y2")).c_str()));
            drawThickSeg(x1, y1, x2, y2, strokeW, (sa > 0) ? sr : fr, (sa > 0) ? sg : fg, (sa > 0) ? sb : fb, 255);
        } else if (elem == "polygon" || elem == "polyline") {
            std::string ptsStr(extractAttr(tag, "points"));
            for (char& c : ptsStr) if (c == ',') c = ' ';
            std::vector<std::pair<float, float>> pts;
            const char* p = ptsStr.c_str();
            while (*p) {
                while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
                if (!*p) break;
                char* n1 = nullptr;
                float x = std::strtof(p, &n1);
                if (n1 == p) break;
                p = n1;
                while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
                char* n2 = nullptr;
                float y = std::strtof(p, &n2);
                if (n2 == p) break;
                p = n2;
                pts.push_back({ x, y });
            }
            if (elem == "polygon" && hasFill) fillPolygon(pts, fr, fg, fb, fa);
            if (sa > 0 || (elem == "polyline" && !hasFill)) {
                uint8_t lr = (sa > 0) ? sr : fr, lg = (sa > 0) ? sg : fg, lb = (sa > 0) ? sb : fb;
                for (size_t i = 0; i + 1 < pts.size(); ++i) {
                    drawThickSeg(pts[i].first, pts[i].second, pts[i + 1].first, pts[i + 1].second, strokeW, lr, lg, lb, 255);
                }
                if (elem == "polygon" && pts.size() >= 3) {
                    drawThickSeg(pts.back().first, pts.back().second, pts.front().first, pts.front().second, strokeW, lr, lg, lb, 255);
                }
            }
        } else if (elem == "path") {
            std::string_view dAttr = extractAttr(tag, "d");
            std::vector<std::vector<std::pair<float, float>>> rings;
            std::vector<std::pair<float, float>> poly;
            float curX = 0.0f, curY = 0.0f, startX = 0.0f, startY = 0.0f;
            float lastCtrlX = 0.0f, lastCtrlY = 0.0f;
            char cmd = 0;
            const char* p = dAttr.data();
            const char* end = dAttr.data() + dAttr.size();
            auto readFloat = [&]() -> float {
                while (p < end && (*p == ' ' || *p == '\t' || *p == ',' || *p == '\r' || *p == '\n')) ++p;
                char* next = nullptr;
                float v = std::strtof(p, &next);
                if (next > p) p = next; else if (p < end) ++p;
                return v;
            };
            while (p < end) {
                while (p < end && (*p == ' ' || *p == '\t' || *p == ',' || *p == '\r' || *p == '\n')) ++p;
                if (p >= end) break;
                if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) {
                    cmd = *p++;
                    if (cmd == 'Z' || cmd == 'z') {
                        if (!poly.empty()) {
                            poly.push_back({ startX, startY });
                            rings.push_back(std::move(poly));
                            poly.clear();
                        }
                        curX = startX; curY = startY;
                        lastCtrlX = curX; lastCtrlY = curY;
                        continue;
                    }
                }
                if (cmd == 'M' || cmd == 'm') {
                    if (!poly.empty()) {
                        rings.push_back(std::move(poly));
                        poly.clear();
                    }
                    float x = readFloat(), y = readFloat();
                    curX = (cmd == 'm') ? (curX + x) : x;
                    curY = (cmd == 'm') ? (curY + y) : y;
                    startX = curX; startY = curY;
                    lastCtrlX = curX; lastCtrlY = curY;
                    poly.push_back({ curX, curY });
                    cmd = (cmd == 'm') ? 'l' : 'L';
                } else if (cmd == 'L' || cmd == 'l') {
                    float x = readFloat(), y = readFloat();
                    curX = (cmd == 'l') ? (curX + x) : x;
                    curY = (cmd == 'l') ? (curY + y) : y;
                    lastCtrlX = curX; lastCtrlY = curY;
                    poly.push_back({ curX, curY });
                } else if (cmd == 'H' || cmd == 'h') {
                    float x = readFloat();
                    curX = (cmd == 'h') ? (curX + x) : x;
                    lastCtrlX = curX; lastCtrlY = curY;
                    poly.push_back({ curX, curY });
                } else if (cmd == 'V' || cmd == 'v') {
                    float y = readFloat();
                    curY = (cmd == 'v') ? (curY + y) : y;
                    lastCtrlX = curX; lastCtrlY = curY;
                    poly.push_back({ curX, curY });
                } else if (cmd == 'C' || cmd == 'c') {
                    float x1 = readFloat(), y1 = readFloat();
                    float x2 = readFloat(), y2 = readFloat();
                    float x = readFloat(),  y = readFloat();
                    if (cmd == 'c') { x1 += curX; y1 += curY; x2 += curX; y2 += curY; x += curX; y += curY; }
                    for (int s = 1; s <= 8; ++s) {
                        float t = s / 8.0f, it = 1.0f - t;
                        float bx = it*it*it*curX + 3*it*it*t*x1 + 3*it*t*t*x2 + t*t*t*x;
                        float by = it*it*it*curY + 3*it*it*t*y1 + 3*it*t*t*y2 + t*t*t*y;
                        poly.push_back({ bx, by });
                    }
                    lastCtrlX = x2; lastCtrlY = y2;
                    curX = x; curY = y;
                } else if (cmd == 'S' || cmd == 's') {
                    float x1 = 2.0f * curX - lastCtrlX, y1 = 2.0f * curY - lastCtrlY;
                    float x2 = readFloat(), y2 = readFloat();
                    float x = readFloat(),  y = readFloat();
                    if (cmd == 's') { x2 += curX; y2 += curY; x += curX; y += curY; }
                    for (int s = 1; s <= 8; ++s) {
                        float t = s / 8.0f, it = 1.0f - t;
                        float bx = it*it*it*curX + 3*it*it*t*x1 + 3*it*t*t*x2 + t*t*t*x;
                        float by = it*it*it*curY + 3*it*it*t*y1 + 3*it*t*t*y2 + t*t*t*y;
                        poly.push_back({ bx, by });
                    }
                    lastCtrlX = x2; lastCtrlY = y2;
                    curX = x; curY = y;
                } else if (cmd == 'Q' || cmd == 'q') {
                    float x1 = readFloat(), y1 = readFloat();
                    float x = readFloat(),  y = readFloat();
                    if (cmd == 'q') { x1 += curX; y1 += curY; x += curX; y += curY; }
                    for (int s = 1; s <= 8; ++s) {
                        float t = s / 8.0f, it = 1.0f - t;
                        float bx = it*it*curX + 2*it*t*x1 + t*t*x;
                        float by = it*it*curY + 2*it*t*y1 + t*t*y;
                        poly.push_back({ bx, by });
                    }
                    lastCtrlX = x1; lastCtrlY = y1;
                    curX = x; curY = y;
                } else if (cmd == 'T' || cmd == 't') {
                    float x1 = 2.0f * curX - lastCtrlX, y1 = 2.0f * curY - lastCtrlY;
                    float x = readFloat(),  y = readFloat();
                    if (cmd == 't') { x += curX; y += curY; }
                    for (int s = 1; s <= 8; ++s) {
                        float t = s / 8.0f, it = 1.0f - t;
                        float bx = it*it*curX + 2*it*t*x1 + t*t*x;
                        float by = it*it*curY + 2*it*t*y1 + t*t*y;
                        poly.push_back({ bx, by });
                    }
                    lastCtrlX = x1; lastCtrlY = y1;
                    curX = x; curY = y;
                } else if (cmd == 'A' || cmd == 'a') {
                    float rx = std::abs(readFloat());
                    float ry = std::abs(readFloat());
                    float rot = readFloat(); (void)rot;
                    float largeArc = readFloat(); (void)largeArc;
                    float sweep = readFloat();
                    float x = readFloat(), y = readFloat();
                    if (cmd == 'a') { x += curX; y += curY; }
                    float mx = (curX + x) * 0.5f, my = (curY + y) * 0.5f;
                    float dx = x - curX, dy = y - curY;
                    float chord = std::hypot(dx, dy);
                    float sag = (chord > 1e-4f) ? ((sweep > 0.5f ? 1.0f : -1.0f) * std::min(rx, ry) * 0.35f) : 0.0f;
                    float nx = (chord > 1e-4f) ? (-dy / chord) : 0.0f;
                    float ny = (chord > 1e-4f) ? ( dx / chord) : 0.0f;
                    float cxCtrl = mx + nx * sag, cyCtrl = my + ny * sag;
                    for (int s = 1; s <= 8; ++s) {
                        float t = s / 8.0f, it = 1.0f - t;
                        float bx = it*it*curX + 2*it*t*cxCtrl + t*t*x;
                        float by = it*it*curY + 2*it*t*cyCtrl + t*t*y;
                        poly.push_back({ bx, by });
                    }
                    curX = x; curY = y;
                    lastCtrlX = curX; lastCtrlY = curY;
                } else {
                    ++p;
                }
            }
            if (!poly.empty()) {
                rings.push_back(std::move(poly));
            }
            if (hasFill && !rings.empty()) fillCompoundPolygon(rings, fr, fg, fb, fa);
            if (sa > 0 || !hasFill) {
                uint8_t lr = (sa > 0) ? sr : fr, lg = (sa > 0) ? sg : fg, lb = (sa > 0) ? sb : fb;
                for (const auto& ring : rings) {
                    for (size_t i = 0; i + 1 < ring.size(); ++i) {
                        drawThickSeg(ring[i].first, ring[i].second, ring[i + 1].first, ring[i + 1].second, strokeW, lr, lg, lb, 255);
                    }
                }
            }
        }
    }

    outInfo.valid = true;
    return true;
}
