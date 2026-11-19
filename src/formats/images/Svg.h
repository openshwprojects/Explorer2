#pragma once
// ============================================================================
// 9. Scalable Vector Graphics (.svg) — W3C 2D Vector, Transform, Arc & Gradient Rasterizer
// ============================================================================
struct SvgMat2D {
    float a = 1.0f, b = 0.0f;
    float c = 0.0f, d = 1.0f;
    float e = 0.0f, f = 0.0f;

    std::pair<float, float> Apply(float x, float y) const {
        return { a * x + c * y + e, b * x + d * y + f };
    }

    float ScaleFactor() const {
        return std::sqrt(std::max(1e-8f, (a * a + b * b + c * c + d * d) * 0.5f));
    }

    static SvgMat2D Multiply(const SvgMat2D& m1, const SvgMat2D& m2) {
        SvgMat2D r;
        r.a = m1.a * m2.a + m1.c * m2.b;
        r.b = m1.b * m2.a + m1.d * m2.b;
        r.c = m1.a * m2.c + m1.c * m2.d;
        r.d = m1.b * m2.c + m1.d * m2.d;
        r.e = m1.a * m2.e + m1.c * m2.f + m1.e;
        r.f = m1.b * m2.e + m1.d * m2.f + m1.f;
        return r;
    }

    bool Invert(SvgMat2D& out) const {
        double det = static_cast<double>(a) * d - static_cast<double>(b) * c;
        if (std::abs(det) < 1e-12) {
            out = SvgMat2D{};
            return false;
        }
        double invDet = 1.0 / det;
        out.a = static_cast<float>( d * invDet);
        out.b = static_cast<float>(-b * invDet);
        out.c = static_cast<float>(-c * invDet);
        out.d = static_cast<float>( a * invDet);
        out.e = static_cast<float>((static_cast<double>(c) * f - static_cast<double>(d) * e) * invDet);
        out.f = static_cast<float>((static_cast<double>(b) * e - static_cast<double>(a) * f) * invDet);
        return true;
    }
};

struct SvgGradCoord {
    float val = 0.0f;
    bool isPercent = true;
};

struct SvgGradStop {
    float offset = 0.0f;
    uint8_t r = 0, g = 0, b = 0, a = 255;
};

struct SvgGradientDef {
    std::string id;
    std::string hrefId;
    bool isRadial = false;
    bool userSpaceOnUse = false;
    int spreadMethod = 0; // 0 = pad, 1 = reflect, 2 = repeat
    SvgMat2D gradTransform{};
    // Linear defaults: (0%, 0%) -> (100%, 0%)
    SvgGradCoord x1{ 0.0f, true }, y1{ 0.0f, true }, x2{ 1.0f, true }, y2{ 0.0f, true };
    bool hasX1 = false, hasY1 = false, hasX2 = false, hasY2 = false;
    // Radial defaults: cx=50%, cy=50%, r=50%, fx=cx, fy=cy
    SvgGradCoord cx{ 0.5f, true }, cy{ 0.5f, true }, r{ 0.5f, true }, fx{ 0.5f, true }, fy{ 0.5f, true };
    bool hasCx = false, hasCy = false, hasR = false, hasFx = false, hasFy = false;
    bool hasTransform = false;
    std::vector<SvgGradStop> stops;
};

struct SvgPaintSpec {
    enum Kind { None, Solid, Url } kind = None;
    uint8_t r = 0, g = 0, b = 0, a = 255;
    bool hasFallback = false;
    std::string urlId;
};

struct SvgLocalBBox {
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    void Add(float x, float y) {
        minX = std::min(minX, x); minY = std::min(minY, y);
        maxX = std::max(maxX, x); maxY = std::max(maxY, y);
    }
    bool Valid() const { return maxX >= minX && maxY >= minY; }
};

inline bool DecodeSVG(const uint8_t* data, size_t size, uint32_t maxDimension, ExtendedImageInfo& outInfo) {
    if (!data || size < 12) return false;
    std::string_view sv(reinterpret_cast<const char*>(data), std::min<size_t>(size, 8 * 1024 * 1024));

    size_t svgTag = std::string_view::npos;
    {
        size_t searchPos = 0;
        while ((searchPos = sv.find("<svg", searchPos)) != std::string_view::npos) {
            if (searchPos + 4 < sv.size()) {
                char nxt = sv[searchPos + 4];
                if (nxt == ' ' || nxt == '\t' || nxt == '\r' || nxt == '\n' || nxt == '>' || nxt == '/' || nxt == ':') {
                    svgTag = searchPos;
                    break;
                }
            }
            searchPos += 4;
        }
    }
    if (svgTag == std::string_view::npos) return false;

    auto trimSv = [](std::string_view s) -> std::string_view {
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) s.remove_prefix(1);
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.remove_suffix(1);
        return s;
    };

    auto eqIgnoreCase = [](std::string_view a, std::string_view b) -> bool {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            char ca = (a[i] >= 'A' && a[i] <= 'Z') ? static_cast<char>(a[i] + 32) : a[i];
            char cb = (b[i] >= 'A' && b[i] <= 'Z') ? static_cast<char>(b[i] + 32) : b[i];
            if (ca != cb) return false;
        }
        return true;
    };

    auto findTagClose = [&](size_t startLt) -> size_t {
        char quote = 0;
        for (size_t i = startLt + 1; i < sv.size(); ++i) {
            char c = sv[i];
            if (quote != 0) {
                if (c == quote) quote = 0;
            } else {
                if (c == '"' || c == '\'') quote = c;
                else if (c == '>') return i;
            }
        }
        return std::string_view::npos;
    };

    auto extractAttr = [](std::string_view tag, std::string_view attrName) -> std::string_view {
        size_t pos = 0;
        while ((pos = tag.find(attrName, pos)) != std::string_view::npos) {
            if (pos > 0) {
                char prev = tag[pos - 1];
                if ((prev >= 'a' && prev <= 'z') || (prev >= 'A' && prev <= 'Z') ||
                    (prev >= '0' && prev <= '9') || prev == '-' || prev == '_') {
                    pos += attrName.size();
                    continue;
                }
            }
            size_t eq = pos + attrName.size();
            while (eq < tag.size() && (tag[eq] == ' ' || tag[eq] == '\t' || tag[eq] == '\r' || tag[eq] == '\n')) ++eq;
            if (eq < tag.size() && tag[eq] == '=') {
                ++eq;
                while (eq < tag.size() && (tag[eq] == ' ' || tag[eq] == '\t' || tag[eq] == '\r' || tag[eq] == '\n')) ++eq;
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

    auto extractStyleOrAttr = [&](std::string_view tag, std::string_view propName) -> std::string_view {
        std::string_view styleSv = extractAttr(tag, "style");
        if (!styleSv.empty()) {
            size_t p = 0;
            while ((p = styleSv.find(propName, p)) != std::string_view::npos) {
                if (p > 0) {
                    char prev = styleSv[p - 1];
                    if (prev != ';' && prev != ' ' && prev != '\t' && prev != '\r' && prev != '\n') {
                        p += propName.size();
                        continue;
                    }
                }
                size_t colon = p + propName.size();
                while (colon < styleSv.size() && (styleSv[colon] == ' ' || styleSv[colon] == '\t')) ++colon;
                if (colon < styleSv.size() && styleSv[colon] == ':') {
                    ++colon;
                    while (colon < styleSv.size() && (styleSv[colon] == ' ' || styleSv[colon] == '\t')) ++colon;
                    size_t semi = styleSv.find(';', colon);
                    if (semi == std::string_view::npos) semi = styleSv.size();
                    return trimSv(styleSv.substr(colon, semi - colon));
                }
                p += propName.size();
            }
        }
        return trimSv(extractAttr(tag, propName));
    };

    auto parseSvgColor = [&](std::string_view col, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) -> bool {
        col = trimSv(col);
        if (col.empty() || eqIgnoreCase(col, "none") || eqIgnoreCase(col, "transparent")) {
            a = 0;
            return false;
        }
        a = 255;
        auto hex1 = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        if (col[0] == '#') {
            if (col.size() >= 7) {
                int r1 = hex1(col[1]), r0 = hex1(col[2]);
                int g1 = hex1(col[3]), g0 = hex1(col[4]);
                int b1 = hex1(col[5]), b0 = hex1(col[6]);
                if (r1 >= 0 && r0 >= 0 && g1 >= 0 && g0 >= 0 && b1 >= 0 && b0 >= 0) {
                    r = static_cast<uint8_t>((r1 << 4) | r0);
                    g = static_cast<uint8_t>((g1 << 4) | g0);
                    b = static_cast<uint8_t>((b1 << 4) | b0);
                    if (col.size() >= 9) {
                        int a1 = hex1(col[7]), a0 = hex1(col[8]);
                        if (a1 >= 0 && a0 >= 0) a = static_cast<uint8_t>((a1 << 4) | a0);
                    }
                    return a > 0;
                }
            } else if (col.size() >= 4) {
                int r1 = hex1(col[1]), g1 = hex1(col[2]), b1 = hex1(col[3]);
                if (r1 >= 0 && g1 >= 0 && b1 >= 0) {
                    r = static_cast<uint8_t>(r1 * 17);
                    g = static_cast<uint8_t>(g1 * 17);
                    b = static_cast<uint8_t>(b1 * 17);
                    if (col.size() >= 5 && hex1(col[4]) >= 0) a = static_cast<uint8_t>(hex1(col[4]) * 17);
                    return a > 0;
                }
            }
            return false;
        }
        if (col.size() > 5 && (eqIgnoreCase(col.substr(0, 4), "rgb(") || eqIgnoreCase(col.substr(0, 5), "rgba("))) {
            size_t lp = col.find('(');
            size_t rp = col.find(')', lp);
            if (lp != std::string_view::npos && rp != std::string_view::npos) {
                std::string inner(col.substr(lp + 1, rp - lp - 1));
                for (char& c : inner) if (c == ',') c = ' ';
                const char* p = inner.c_str();
                float vals[4] = { 0, 0, 0, 1.0f };
                bool pct[4] = { false, false, false, false };
                int cnt = 0;
                while (*p && cnt < 4) {
                    while (*p == ' ' || *p == '\t') ++p;
                    if (!*p) break;
                    char* nxt = nullptr;
                    vals[cnt] = std::strtof(p, &nxt);
                    if (nxt == p) break;
                    p = nxt;
                    if (*p == '%') { pct[cnt] = true; ++p; }
                    ++cnt;
                }
                if (cnt >= 3) {
                    r = static_cast<uint8_t>(std::clamp(pct[0] ? (vals[0] * 2.55f + 0.5f) : vals[0], 0.0f, 255.0f));
                    g = static_cast<uint8_t>(std::clamp(pct[1] ? (vals[1] * 2.55f + 0.5f) : vals[1], 0.0f, 255.0f));
                    b = static_cast<uint8_t>(std::clamp(pct[2] ? (vals[2] * 2.55f + 0.5f) : vals[2], 0.0f, 255.0f));
                    if (cnt == 4) {
                        float af = pct[3] ? (vals[3] * 0.01f) : vals[3];
                        a = static_cast<uint8_t>(std::clamp(af * 255.0f + 0.5f, 0.0f, 255.0f));
                    }
                    return a > 0;
                }
            }
        }

        struct NamedCol { const char* name; uint8_t r, g, b; };
        static constexpr NamedCol kNamed[] = {
            { "black", 0, 0, 0 },       { "currentColor", 0, 0, 0 },
            { "white", 255, 255, 255 }, { "red", 255, 0, 0 },
            { "lime", 0, 255, 0 },      { "green", 0, 128, 0 },
            { "blue", 0, 0, 255 },      { "yellow", 255, 255, 0 },
            { "cyan", 0, 255, 255 },    { "aqua", 0, 255, 255 },
            { "magenta", 255, 0, 255 }, { "fuchsia", 255, 0, 255 },
            { "orange", 255, 165, 0 },  { "gold", 255, 215, 0 },
            { "purple", 128, 0, 128 },  { "violet", 238, 130, 238 },
            { "indigo", 75, 0, 130 },   { "pink", 255, 192, 203 },
            { "teal", 0, 128, 128 },    { "navy", 0, 0, 128 },
            { "maroon", 128, 0, 0 },    { "olive", 128, 128, 0 },
            { "brown", 165, 42, 42 },   { "coral", 255, 127, 80 },
            { "crimson", 220, 20, 60 }, { "salmon", 250, 128, 114 },
            { "skyblue", 135, 206, 235},{ "steelblue", 70, 130, 180 },
            { "gray", 128, 128, 128 },  { "grey", 128, 128, 128 },
            { "silver", 192, 192, 192 },{ "darkgray", 169, 169, 169 },
            { "darkgrey", 169, 169, 169},{ "lightgray", 211, 211, 211 },
            { "lightgrey", 211, 211, 211}
        };
        for (const auto& nc : kNamed) {
            if (eqIgnoreCase(col, nc.name)) {
                r = nc.r; g = nc.g; b = nc.b;
                return true;
            }
        }
        return false;
    };

    auto parsePaintSpec = [&](std::string_view val, SvgPaintSpec& out) {
        val = trimSv(val);
        if (val.empty()) return;
        if (eqIgnoreCase(val, "none") || eqIgnoreCase(val, "transparent")) {
            out.kind = SvgPaintSpec::None;
            out.a = 0;
            return;
        }
        if (val.size() > 5 && eqIgnoreCase(val.substr(0, 4), "url(")) {
            size_t closeP = val.find(')', 4);
            if (closeP != std::string_view::npos) {
                std::string_view inside = trimSv(val.substr(4, closeP - 4));
                if (!inside.empty() && (inside.front() == '"' || inside.front() == '\'')) inside.remove_prefix(1);
                if (!inside.empty() && (inside.back() == '"' || inside.back() == '\'')) inside.remove_suffix(1);
                if (!inside.empty() && inside.front() == '#') inside.remove_prefix(1);
                out.kind = SvgPaintSpec::Url;
                out.urlId.assign(inside.data(), inside.size());
                out.hasFallback = false;
                std::string_view rest = trimSv(val.substr(closeP + 1));
                if (!rest.empty()) {
                    uint8_t fr = 0, fg = 0, fb = 0, fa = 255;
                    if (parseSvgColor(rest, fr, fg, fb, fa)) {
                        out.r = fr; out.g = fg; out.b = fb; out.a = fa;
                        out.hasFallback = true;
                    }
                }
                return;
            }
        }
        uint8_t cr = 0, cg = 0, cb = 0, ca = 255;
        if (parseSvgColor(val, cr, cg, cb, ca)) {
            out.kind = SvgPaintSpec::Solid;
            out.r = cr; out.g = cg; out.b = cb; out.a = ca;
        } else {
            out.kind = SvgPaintSpec::None;
            out.a = 0;
        }
    };

    auto parseTransform = [](std::string_view attr) -> SvgMat2D {
        SvgMat2D total{};
        constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
        size_t p = 0;
        while (p < attr.size()) {
            while (p < attr.size() && (attr[p] == ' ' || attr[p] == '\t' || attr[p] == ',' || attr[p] == '\r' || attr[p] == '\n')) ++p;
            if (p >= attr.size()) break;
            size_t lp = attr.find('(', p);
            if (lp == std::string_view::npos) break;
            std::string_view fn = attr.substr(p, lp - p);
            while (!fn.empty() && (fn.back() == ' ' || fn.back() == '\t')) fn.remove_suffix(1);
            size_t rp = attr.find(')', lp + 1);
            if (rp == std::string_view::npos) break;

            std::string argsStr(attr.substr(lp + 1, rp - lp - 1));
            for (char& c : argsStr) if (c == ',') c = ' ';
            float v[6] = { 0, 0, 0, 0, 0, 0 };
            int n = 0;
            const char* ap = argsStr.c_str();
            while (*ap && n < 6) {
                while (*ap == ' ' || *ap == '\t' || *ap == '\r' || *ap == '\n') ++ap;
                if (!*ap) break;
                char* nxt = nullptr;
                v[n] = std::strtof(ap, &nxt);
                if (nxt == ap) break;
                ap = nxt;
                ++n;
            }

            SvgMat2D m{};
            if (fn == "matrix" && n >= 6) {
                m = { v[0], v[1], v[2], v[3], v[4], v[5] };
            } else if (fn == "translate" && n >= 1) {
                m = { 1.0f, 0.0f, 0.0f, 1.0f, v[0], (n >= 2 ? v[1] : 0.0f) };
            } else if (fn == "scale" && n >= 1) {
                float sy = (n >= 2) ? v[1] : v[0];
                m = { v[0], 0.0f, 0.0f, sy, 0.0f, 0.0f };
            } else if (fn == "rotate" && n >= 1) {
                float rad = v[0] * kDegToRad;
                float cs = std::cos(rad), sn = std::sin(rad);
                m = { cs, sn, -sn, cs, 0.0f, 0.0f };
                if (n >= 3) {
                    SvgMat2D t1{ 1.0f, 0.0f, 0.0f, 1.0f,  v[1],  v[2] };
                    SvgMat2D t2{ 1.0f, 0.0f, 0.0f, 1.0f, -v[1], -v[2] };
                    m = SvgMat2D::Multiply(SvgMat2D::Multiply(t1, m), t2);
                }
            } else if (fn == "skewX" && n >= 1) {
                m = { 1.0f, 0.0f, std::tan(v[0] * kDegToRad), 1.0f, 0.0f, 0.0f };
            } else if (fn == "skewY" && n >= 1) {
                m = { 1.0f, std::tan(v[0] * kDegToRad), 0.0f, 1.0f, 0.0f, 0.0f };
            }
            total = SvgMat2D::Multiply(total, m);
            p = rp + 1;
        }
        return total;
    };

    auto parseGradCoord = [&](std::string_view s, SvgGradCoord& out) -> bool {
        s = trimSv(s);
        if (s.empty()) return false;
        std::string tmp(s);
        char* endPtr = nullptr;
        float v = std::strtof(tmp.c_str(), &endPtr);
        if (endPtr == tmp.c_str()) return false;
        if (*endPtr == '%') {
            out.val = v * 0.01f;
            out.isPercent = true;
        } else {
            out.val = v;
            out.isPercent = false;
        }
        return true;
    };

    auto parseOpacityAttr = [](std::string_view s, float defVal) -> float {
        if (s.empty()) return defVal;
        std::string tmp(s);
        char* endPtr = nullptr;
        float v = std::strtof(tmp.c_str(), &endPtr);
        if (endPtr == tmp.c_str()) return defVal;
        if (*endPtr == '%') v *= 0.01f;
        return std::clamp(v, 0.0f, 1.0f);
    };

    size_t svgClose = findTagClose(svgTag);
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
    outInfo.origWidth = static_cast<uint32_t>(std::max(1.0f, std::round(vbW)));
    outInfo.origHeight = static_cast<uint32_t>(std::max(1.0f, std::round(vbH)));
    outInfo.channels = 4;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = L"SVG Vector Graphic (.svg)";
    outInfo.bgraPixels.assign(static_cast<size_t>(outW) * outH * 4, 0);

    const float scaleX = static_cast<float>(outW) / std::max(1e-4f, vbW);
    const float scaleY = static_cast<float>(outH) / std::max(1e-4f, vbH);
    const float scale  = std::min(scaleX, scaleY);

    std::vector<SvgGradientDef> gradients;
    auto findGradient = [&](std::string_view id) -> const SvgGradientDef* {
        for (const auto& g : gradients) {
            if (g.id == id) return &g;
        }
        return nullptr;
    };

    auto evalPaintAtScreen = [&](
        const SvgPaintSpec& paint,
        float sx,
        float sy,
        const SvgMat2D& invElemCtm,
        const SvgLocalBBox& bbox,
        float opacityMul,
        uint8_t& outR,
        uint8_t& outG,
        uint8_t& outB,
        uint8_t& outA) -> bool
    {
        if (paint.kind == SvgPaintSpec::None || opacityMul <= 0.0f) return false;
        if (paint.kind == SvgPaintSpec::Solid) {
            outR = paint.r; outG = paint.g; outB = paint.b;
            outA = static_cast<uint8_t>(std::clamp(paint.a * opacityMul + 0.5f, 0.0f, 255.0f));
            return outA > 0;
        }

        const SvgGradientDef* baseG = findGradient(paint.urlId);
        if (!baseG) {
            if (paint.hasFallback) {
                outR = paint.r; outG = paint.g; outB = paint.b;
                outA = static_cast<uint8_t>(std::clamp(paint.a * opacityMul + 0.5f, 0.0f, 255.0f));
                return outA > 0;
            }
            return false;
        }

        // Resolve optional href / xlink:href stop inheritance (up to 4 hops)
        const std::vector<SvgGradStop>* stopsPtr = &baseG->stops;
        const SvgGradientDef* cur = baseG;
        for (int hop = 0; hop < 4 && stopsPtr->empty() && !cur->hrefId.empty(); ++hop) {
            const SvgGradientDef* ref = findGradient(cur->hrefId);
            if (!ref) break;
            if (!ref->stops.empty()) stopsPtr = &ref->stops;
            cur = ref;
        }
        if (stopsPtr->empty()) {
            if (paint.hasFallback) {
                outR = paint.r; outG = paint.g; outB = paint.b;
                outA = static_cast<uint8_t>(std::clamp(paint.a * opacityMul + 0.5f, 0.0f, 255.0f));
                return outA > 0;
            }
            return false;
        }
        if (stopsPtr->size() == 1) {
            const auto& s0 = (*stopsPtr)[0];
            outR = s0.r; outG = s0.g; outB = s0.b;
            outA = static_cast<uint8_t>(std::clamp(s0.a * opacityMul + 0.5f, 0.0f, 255.0f));
            return outA > 0;
        }

        // Screen pixel -> root SVG world space -> element local user space
        float worldX = sx / scaleX + vbX;
        float worldY = sy / scaleY + vbY;
        auto [localX, localY] = invElemCtm.Apply(worldX, worldY);

        float gx = localX, gy = localY;
        if (!baseG->userSpaceOnUse) {
            float bw = std::max(1e-5f, bbox.maxX - bbox.minX);
            float bh = std::max(1e-5f, bbox.maxY - bbox.minY);
            gx = (localX - bbox.minX) / bw;
            gy = (localY - bbox.minY) / bh;
        }
        if (baseG->hasTransform) {
            SvgMat2D invGrad{};
            if (baseG->gradTransform.Invert(invGrad)) {
                auto [tx, ty] = invGrad.Apply(gx, gy);
                gx = tx; gy = ty;
            }
        }

        auto resolveCoord = [&](const SvgGradCoord& gc, float spanOrigin, float spanLen) -> float {
            if (baseG->userSpaceOnUse) {
                return gc.isPercent ? (spanOrigin + gc.val * spanLen) : gc.val;
            }
            return gc.val; // In objectBoundingBox mode, both 50% (0.5) and 0.5 are [0..1] bbox units
        };

        float t = 0.0f;
        if (!baseG->isRadial) {
            float x1 = resolveCoord(baseG->x1, vbX, vbW);
            float y1 = resolveCoord(baseG->y1, vbY, vbH);
            float x2 = resolveCoord(baseG->x2, vbX, vbW);
            float y2 = resolveCoord(baseG->y2, vbY, vbH);
            float dx = x2 - x1, dy = y2 - y1;
            float len2 = dx * dx + dy * dy;
            t = (len2 > 1e-10f) ? (((gx - x1) * dx + (gy - y1) * dy) / len2) : 0.0f;
        } else {
            float diagSpan = std::hypot(vbW, vbH) / 1.41421356f;
            float cx = resolveCoord(baseG->cx, vbX, vbW);
            float cy = resolveCoord(baseG->cy, vbY, vbH);
            float rad = std::max(1e-5f, resolveCoord(baseG->r, 0.0f, diagSpan));
            float fx = baseG->hasFx ? resolveCoord(baseG->fx, vbX, vbW) : cx;
            float fy = baseG->hasFy ? resolveCoord(baseG->fy, vbY, vbH) : cy;
            if (std::hypot(fx - cx, fy - cy) < 1e-5f) {
                t = std::hypot(gx - cx, gy - cy) / rad;
            } else {
                // Clamp focal point inside circle of radius 0.999*rad per SVG 1.1 §13.2.3
                float fdx = fx - cx, fdy = fy - cy;
                float fd = std::hypot(fdx, fdy);
                if (fd >= rad * 0.999f) {
                    fx = cx + fdx * (rad * 0.999f / fd);
                    fy = cy + fdy * (rad * 0.999f / fd);
                    fdx = fx - cx; fdy = fy - cy;
                }
                float pdx = gx - fx, pdy = gy - fy;
                float qa = fdx * fdx + fdy * fdy - rad * rad; // < 0
                float qb = 2.0f * (pdx * fdx + pdy * fdy);
                float qc = pdx * pdx + pdy * pdy;
                float disc = std::max(0.0f, qb * qb - 4.0f * qa * qc);
                float denom = 2.0f * qa;
                t = (std::abs(denom) > 1e-10f) ? std::max(0.0f, (-qb - std::sqrt(disc)) / denom) : 0.0f;
            }
        }

        if (!std::isfinite(t)) t = 0.0f;
        if (baseG->spreadMethod == 2) { // repeat
            t = t - std::floor(t);
        } else if (baseG->spreadMethod == 1) { // reflect
            t = std::fmod(std::abs(t), 2.0f);
            if (t > 1.0f) t = 2.0f - t;
        } else { // pad
            t = std::clamp(t, 0.0f, 1.0f);
        }

        const auto& stops = *stopsPtr;
        if (t <= stops.front().offset) {
            const auto& s = stops.front();
            outR = s.r; outG = s.g; outB = s.b;
            outA = static_cast<uint8_t>(std::clamp(s.a * opacityMul + 0.5f, 0.0f, 255.0f));
            return outA > 0;
        }
        if (t >= stops.back().offset) {
            const auto& s = stops.back();
            outR = s.r; outG = s.g; outB = s.b;
            outA = static_cast<uint8_t>(std::clamp(s.a * opacityMul + 0.5f, 0.0f, 255.0f));
            return outA > 0;
        }
        for (size_t i = 0; i + 1 < stops.size(); ++i) {
            if (t >= stops[i].offset && t <= stops[i + 1].offset) {
                float span = stops[i + 1].offset - stops[i].offset;
                float u = (span > 1e-6f) ? ((t - stops[i].offset) / span) : 0.0f;
                outR = static_cast<uint8_t>(std::clamp(stops[i].r + (stops[i + 1].r - stops[i].r) * u + 0.5f, 0.0f, 255.0f));
                outG = static_cast<uint8_t>(std::clamp(stops[i].g + (stops[i + 1].g - stops[i].g) * u + 0.5f, 0.0f, 255.0f));
                outB = static_cast<uint8_t>(std::clamp(stops[i].b + (stops[i + 1].b - stops[i].b) * u + 0.5f, 0.0f, 255.0f));
                float aInterp = stops[i].a + (stops[i + 1].a - stops[i].a) * u;
                outA = static_cast<uint8_t>(std::clamp(aInterp * opacityMul + 0.5f, 0.0f, 255.0f));
                return outA > 0;
            }
        }
        return false;
    };

    auto blendPixel = [&](int px, int py, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        if (px < 0 || py < 0 || px >= static_cast<int>(outW) || py >= static_cast<int>(outH) || a == 0) return;
        uint8_t* dst = &outInfo.bgraPixels[(static_cast<size_t>(py) * outW + px) * 4];
        if (a == 255 || dst[3] == 0) {
            WritePremulBGRA(dst, r, g, b, a);
        } else {
            uint32_t invA = 255u - a;
            dst[0] = static_cast<uint8_t>(std::min<uint32_t>(255u, (static_cast<uint32_t>(b) * a + dst[0] * invA + 127u) / 255u));
            dst[1] = static_cast<uint8_t>(std::min<uint32_t>(255u, (static_cast<uint32_t>(g) * a + dst[1] * invA + 127u) / 255u));
            dst[2] = static_cast<uint8_t>(std::min<uint32_t>(255u, (static_cast<uint32_t>(r) * a + dst[2] * invA + 127u) / 255u));
            dst[3] = static_cast<uint8_t>(std::min<uint32_t>(255u, a + (dst[3] * invA + 127u) / 255u));
        }
    };

    auto drawThickSeg = [&](
        float x0, float y0, float x1, float y1, float thickWorld,
        const SvgPaintSpec& paint, const SvgMat2D& invElemCtm, const SvgLocalBBox& bbox, float opacityMul)
    {
        if (paint.kind == SvgPaintSpec::None || opacityMul <= 0.0f) return;
        float sx0 = (x0 - vbX) * scaleX, sy0 = (y0 - vbY) * scaleY;
        float sx1 = (x1 - vbX) * scaleX, sy1 = (y1 - vbY) * scaleY;
        float rad = std::max(0.75f, thickWorld * scale * 0.5f);
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
                        int px = static_cast<int>(cx) + dx;
                        int py = static_cast<int>(cy) + dy;
                        if (px < 0 || py < 0 || px >= static_cast<int>(outW) || py >= static_cast<int>(outH)) continue;
                        uint8_t r = 0, g = 0, b = 0, a = 0;
                        if (evalPaintAtScreen(paint, px + 0.5f, py + 0.5f, invElemCtm, bbox, opacityMul, r, g, b, a)) {
                            blendPixel(px, py, r, g, b, a);
                        }
                    }
                }
            }
        }
    };

    auto fillCompoundWorldRings = [&](
        const std::vector<std::vector<std::pair<float, float>>>& worldRings,
        bool evenOdd,
        const SvgPaintSpec& paint,
        const SvgMat2D& invElemCtm,
        const SvgLocalBBox& bbox,
        float opacityMul)
    {
        if (worldRings.empty() || paint.kind == SvgPaintSpec::None || opacityMul <= 0.0f) return;
        float minY = 1e9f, maxY = -1e9f;
        std::vector<std::vector<std::pair<float, float>>> srings;
        srings.reserve(worldRings.size());
        for (const auto& pts : worldRings) {
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
        struct EdgeHit { float x; int dir; };
        std::vector<EdgeHit> hits;

        for (int y = y0; y <= y1; ++y) {
            float fy = static_cast<float>(y) + 0.5f;
            hits.clear();
            for (const auto& spts : srings) {
                size_t j = spts.size() - 1;
                for (size_t i = 0; i < spts.size(); ++i) {
                    float yi = spts[i].second, yj = spts[j].second;
                    if ((yi <= fy && yj > fy) || (yj <= fy && yi > fy)) {
                        float xi = spts[i].first, xj = spts[j].first;
                        float xCross = xi + (fy - yi) / (yj - yi) * (xj - xi);
                        int dir = (yj > yi) ? 1 : -1;
                        hits.push_back({ xCross, dir });
                    }
                    j = i;
                }
            }
            if (hits.size() < 2) continue;
            std::sort(hits.begin(), hits.end(), [](const EdgeHit& a, const EdgeHit& b) { return a.x < b.x; });

            auto fillSpan = [&](float xLeft, float xRight) {
                int xStart = std::max(0, static_cast<int>(std::floor(xLeft + 0.5f)));
                int xEnd   = std::min(static_cast<int>(outW) - 1, static_cast<int>(std::floor(xRight - 0.5f)));
                if (xEnd < xStart && xRight - xLeft >= 0.5f) {
                    xStart = std::clamp(static_cast<int>(std::round((xLeft + xRight) * 0.5f)), 0, static_cast<int>(outW) - 1);
                    xEnd = xStart;
                }
                for (int x = xStart; x <= xEnd; ++x) {
                    uint8_t r = 0, g = 0, b = 0, a = 0;
                    if (evalPaintAtScreen(paint, x + 0.5f, fy, invElemCtm, bbox, opacityMul, r, g, b, a)) {
                        blendPixel(x, y, r, g, b, a);
                    }
                }
            };

            if (evenOdd) {
                for (size_t i = 0; i + 1 < hits.size(); i += 2) {
                    fillSpan(hits[i].x, hits[i + 1].x);
                }
            } else {
                int winding = 0;
                float spanStart = 0.0f;
                for (const auto& h : hits) {
                    if (winding == 0) spanStart = h.x;
                    winding += h.dir;
                    if (winding == 0) fillSpan(spanStart, h.x);
                }
            }
        }
    };

    // W3C SVG 1.1 Appendix F.6.5 / F.6.6 Exact Endpoint-to-Center Elliptical Arc Sampler
    auto appendSvgEllipticalArc = [](
        std::vector<std::pair<float, float>>& poly,
        float x1, float y1,
        float rxIn, float ryIn,
        float xAxisRotationDeg,
        bool largeArcFlag,
        bool sweepFlag,
        float x2, float y2)
    {
        if (std::abs(x1 - x2) < 1e-6f && std::abs(y1 - y2) < 1e-6f) return;
        double rx = std::abs(static_cast<double>(rxIn));
        double ry = std::abs(static_cast<double>(ryIn));
        if (rx < 1e-6 || ry < 1e-6) {
            poly.push_back({ x2, y2 });
            return;
        }

        constexpr double kPi = 3.14159265358979323846;
        double phi = std::fmod(static_cast<double>(xAxisRotationDeg), 360.0) * (kPi / 180.0);
        double cosPhi = std::cos(phi);
        double sinPhi = std::sin(phi);

        // F.6.5.1: Compute (x1', y1')
        double dx2 = (static_cast<double>(x1) - static_cast<double>(x2)) * 0.5;
        double dy2 = (static_cast<double>(y1) - static_cast<double>(y2)) * 0.5;
        double x1p =  cosPhi * dx2 + sinPhi * dy2;
        double y1p = -sinPhi * dx2 + cosPhi * dy2;

        // F.6.6.2 & F.6.6.3: Ensure radii are large enough
        double rx2 = rx * rx;
        double ry2 = ry * ry;
        double x1p2 = x1p * x1p;
        double y1p2 = y1p * y1p;
        double lambda = x1p2 / rx2 + y1p2 / ry2;
        if (lambda > 1.0) {
            double scaleUp = std::sqrt(lambda);
            rx *= scaleUp;
            ry *= scaleUp;
            rx2 = rx * rx;
            ry2 = ry * ry;
        }

        // F.6.5.2: Compute (cx', cy') with sign (+ if fA != fS, - if fA == fS)
        double num = std::max(0.0, rx2 * ry2 - rx2 * y1p2 - ry2 * x1p2);
        double den = rx2 * y1p2 + ry2 * x1p2;
        double sq = (den > 1e-18) ? std::sqrt(num / den) : 0.0;
        if (largeArcFlag == sweepFlag) sq = -sq;
        double cxp =  sq * (rx * y1p / ry);
        double cyp = -sq * (ry * x1p / rx);

        // F.6.5.3: Compute (cx, cy) from (cx', cy')
        double cx = cosPhi * cxp - sinPhi * cyp + (static_cast<double>(x1) + static_cast<double>(x2)) * 0.5;
        double cy = sinPhi * cxp + cosPhi * cyp + (static_cast<double>(y1) + static_cast<double>(y2)) * 0.5;

        // F.6.5.5 & F.6.5.6: Compute theta1 and dTheta
        auto vecAngle = [](double ux, double uy, double vx, double vy) -> double {
            double dot = ux * vx + uy * vy;
            double len = std::sqrt((ux * ux + uy * uy) * (vx * vx + vy * vy));
            double ang = std::acos(std::clamp(dot / std::max(1e-18, len), -1.0, 1.0));
            return (ux * vy - uy * vx < 0.0) ? -ang : ang;
        };

        double ux = ( x1p - cxp) / rx;
        double uy = ( y1p - cyp) / ry;
        double vx = (-x1p - cxp) / rx;
        double vy = (-y1p - cyp) / ry;

        double theta1 = vecAngle(1.0, 0.0, ux, uy);
        double dTheta = std::fmod(vecAngle(ux, uy, vx, vy), 2.0 * kPi);
        if (!sweepFlag && dTheta > 0.0) dTheta -= 2.0 * kPi;
        if ( sweepFlag && dTheta < 0.0) dTheta += 2.0 * kPi;

        int segs = std::clamp(static_cast<int>(std::ceil(std::abs(dTheta) / (kPi / 18.0))), 8, 96);
        for (int s = 1; s <= segs; ++s) {
            if (s == segs) {
                poly.push_back({ x2, y2 });
            } else {
                double ang = theta1 + dTheta * (static_cast<double>(s) / static_cast<double>(segs));
                double ex = rx * std::cos(ang);
                double ey = ry * std::sin(ang);
                float px = static_cast<float>(cosPhi * ex - sinPhi * ey + cx);
                float py = static_cast<float>(sinPhi * ex + cosPhi * ey + cy);
                poly.push_back({ px, py });
            }
        }
    };

    struct SvgState {
        std::string elemName;
        SvgMat2D ctm{};
        SvgPaintSpec fill{ SvgPaintSpec::Solid, 0, 0, 0, 255, false, {} }; // W3C default fill: black
        SvgPaintSpec stroke{ SvgPaintSpec::None, 0, 0, 0, 0, false, {} };  // W3C default stroke: none
        float fillOpacity = 1.0f;
        float strokeOpacity = 1.0f;
        float opacity = 1.0f;
        float strokeWidth = 1.0f;
        bool evenOdd = false;
        bool hidden = false;
    };

    auto applyAttrsToState = [&](std::string_view tag, SvgState& st) {
        std::string_view tf = extractAttr(tag, "transform");
        if (!tf.empty()) {
            st.ctm = SvgMat2D::Multiply(st.ctm, parseTransform(tf));
        }
        std::string_view fillAttr = extractStyleOrAttr(tag, "fill");
        if (!fillAttr.empty()) parsePaintSpec(fillAttr, st.fill);

        std::string_view strokeAttr = extractStyleOrAttr(tag, "stroke");
        if (!strokeAttr.empty()) parsePaintSpec(strokeAttr, st.stroke);

        std::string_view swAttr = extractStyleOrAttr(tag, "stroke-width");
        if (!swAttr.empty()) {
            float sw = static_cast<float>(std::atof(std::string(swAttr).c_str()));
            if (sw >= 0.0f) st.strokeWidth = sw;
        }
        st.fillOpacity   = parseOpacityAttr(extractStyleOrAttr(tag, "fill-opacity"), st.fillOpacity);
        st.strokeOpacity = parseOpacityAttr(extractStyleOrAttr(tag, "stroke-opacity"), st.strokeOpacity);
        // Element/group `opacity` multiplies cumulative opacity
        float elemOp = parseOpacityAttr(extractStyleOrAttr(tag, "opacity"), 1.0f);
        st.opacity *= elemOp;

        std::string_view frAttr = extractStyleOrAttr(tag, "fill-rule");
        if (!frAttr.empty()) {
            st.evenOdd = eqIgnoreCase(frAttr, "evenodd");
        }
        std::string_view dispAttr = extractStyleOrAttr(tag, "display");
        if (eqIgnoreCase(dispAttr, "none")) {
            st.hidden = true;
        }
        std::string_view visAttr = extractStyleOrAttr(tag, "visibility");
        if (eqIgnoreCase(visAttr, "hidden") || eqIgnoreCase(visAttr, "collapse")) {
            st.hidden = true;
        }
    };

    auto isNonRenderingContainer = [&](std::string_view elem) -> bool {
        return elem == "defs" || elem == "clipPath" || elem == "mask" ||
               elem == "pattern" || elem == "symbol" || elem == "marker" ||
               elem == "style" || elem == "script" || elem == "metadata" ||
               elem == "title" || elem == "desc" || elem == "filter" ||
               elem == "linearGradient" || elem == "radialGradient";
    };

    SvgState rootState{};
    rootState.elemName = "svg";
    applyAttrsToState(svgHeader, rootState);
    std::vector<SvgState> stateStack;
    stateStack.push_back(rootState);

    int activeGradientIdx = -1;

    size_t pos = svgClose + 1;
    while (pos < sv.size()) {
        size_t lt = sv.find('<', pos);
        if (lt == std::string_view::npos) break;

        // Skip XML comments <!-- ... -->
        if (lt + 4 <= sv.size() && sv.substr(lt, 4) == "<!--") {
            size_t endComment = sv.find("-->", lt + 4);
            if (endComment == std::string_view::npos) break;
            pos = endComment + 3;
            continue;
        }
        // Skip CDATA <![CDATA[ ... ]]>
        if (lt + 9 <= sv.size() && sv.substr(lt, 9) == "<![CDATA[") {
            size_t endCdata = sv.find("]]>", lt + 9);
            if (endCdata == std::string_view::npos) break;
            pos = endCdata + 3;
            continue;
        }

        size_t gt = findTagClose(lt);
        if (gt == std::string_view::npos) break;
        std::string_view rawTag = trimSv(sv.substr(lt + 1, gt - lt - 1));
        pos = gt + 1;
        if (rawTag.empty() || rawTag[0] == '!' || rawTag[0] == '?') continue;

        // Handle closing tag </elem>
        if (rawTag[0] == '/') {
            std::string_view closeName = trimSv(rawTag.substr(1));
            size_t colonPos = closeName.find(':');
            if (colonPos != std::string_view::npos) closeName.remove_prefix(colonPos + 1);
            if (closeName == "linearGradient" || closeName == "radialGradient") {
                activeGradientIdx = -1;
            }
            if (closeName == "svg" && stateStack.size() <= 1) {
                break;
            }
            for (size_t si = stateStack.size(); si > 1; --si) {
                if (stateStack[si - 1].elemName == closeName) {
                    stateStack.resize(si - 1);
                    break;
                }
            }
            continue;
        }

        bool selfClosing = false;
        if (!rawTag.empty() && rawTag.back() == '/') {
            selfClosing = true;
            rawTag.remove_suffix(1);
            rawTag = trimSv(rawTag);
        }

        size_t nameEnd = 0;
        while (nameEnd < rawTag.size() && rawTag[nameEnd] != ' ' && rawTag[nameEnd] != '\t' &&
               rawTag[nameEnd] != '\r' && rawTag[nameEnd] != '\n') {
            ++nameEnd;
        }
        std::string_view elem = rawTag.substr(0, nameEnd);
        size_t nsColon = elem.find(':');
        if (nsColon != std::string_view::npos) elem.remove_prefix(nsColon + 1);

        // Parse <linearGradient>, <radialGradient>, and <stop> even inside <defs>
        if (elem == "linearGradient" || elem == "radialGradient") {
            SvgGradientDef gd{};
            gd.id = std::string(trimSv(extractAttr(rawTag, "id")));
            gd.isRadial = (elem == "radialGradient");
            std::string_view href = extractAttr(rawTag, "href");
            if (href.empty()) href = extractAttr(rawTag, "xlink:href");
            href = trimSv(href);
            if (!href.empty() && href.front() == '#') href.remove_prefix(1);
            gd.hrefId.assign(href.data(), href.size());

            // Copy defaults from referenced gradient if already defined
            if (!gd.hrefId.empty()) {
                if (const SvgGradientDef* parentG = findGradient(gd.hrefId)) {
                    bool keepRadial = gd.isRadial;
                    std::string keepId = gd.id, keepHref = gd.hrefId;
                    gd = *parentG;
                    gd.id = std::move(keepId);
                    gd.hrefId = std::move(keepHref);
                    gd.isRadial = keepRadial;
                    gd.stops.clear();
                }
            }

            std::string_view gu = extractAttr(rawTag, "gradientUnits");
            if (gu == "userSpaceOnUse") gd.userSpaceOnUse = true;
            else if (gu == "objectBoundingBox") gd.userSpaceOnUse = false;

            std::string_view sm = extractAttr(rawTag, "spreadMethod");
            if (sm == "reflect") gd.spreadMethod = 1;
            else if (sm == "repeat") gd.spreadMethod = 2;
            else if (sm == "pad") gd.spreadMethod = 0;

            std::string_view gtAttr = extractAttr(rawTag, "gradientTransform");
            if (!gtAttr.empty()) {
                gd.gradTransform = parseTransform(gtAttr);
                gd.hasTransform = true;
            }
            if (!gd.isRadial) {
                gd.hasX1 = parseGradCoord(extractAttr(rawTag, "x1"), gd.x1) || gd.hasX1;
                gd.hasY1 = parseGradCoord(extractAttr(rawTag, "y1"), gd.y1) || gd.hasY1;
                gd.hasX2 = parseGradCoord(extractAttr(rawTag, "x2"), gd.x2) || gd.hasX2;
                gd.hasY2 = parseGradCoord(extractAttr(rawTag, "y2"), gd.y2) || gd.hasY2;
            } else {
                gd.hasCx = parseGradCoord(extractAttr(rawTag, "cx"), gd.cx) || gd.hasCx;
                gd.hasCy = parseGradCoord(extractAttr(rawTag, "cy"), gd.cy) || gd.hasCy;
                gd.hasR  = parseGradCoord(extractAttr(rawTag, "r"),  gd.r)  || gd.hasR;
                gd.hasFx = parseGradCoord(extractAttr(rawTag, "fx"), gd.fx) || gd.hasFx;
                gd.hasFy = parseGradCoord(extractAttr(rawTag, "fy"), gd.fy) || gd.hasFy;
            }

            gradients.push_back(std::move(gd));
            activeGradientIdx = selfClosing ? -1 : static_cast<int>(gradients.size()) - 1;
            if (!selfClosing) {
                SvgState child = stateStack.back();
                child.elemName.assign(elem.data(), elem.size());
                child.hidden = true;
                stateStack.push_back(std::move(child));
            }
            continue;
        }

        if (elem == "stop") {
            if (activeGradientIdx >= 0 && activeGradientIdx < static_cast<int>(gradients.size())) {
                SvgGradStop st{};
                SvgGradCoord offC{ 0.0f, false };
                if (parseGradCoord(extractAttr(rawTag, "offset"), offC)) {
                    st.offset = std::clamp(offC.val, 0.0f, 1.0f);
                }
                if (!gradients[activeGradientIdx].stops.empty()) {
                    st.offset = std::max(st.offset, gradients[activeGradientIdx].stops.back().offset);
                }
                uint8_t cr = 0, cg = 0, cb = 0, ca = 255;
                std::string_view sc = extractStyleOrAttr(rawTag, "stop-color");
                if (!sc.empty()) {
                    parseSvgColor(sc, cr, cg, cb, ca);
                }
                float so = parseOpacityAttr(extractStyleOrAttr(rawTag, "stop-opacity"), 1.0f);
                st.r = cr; st.g = cg; st.b = cb;
                st.a = static_cast<uint8_t>(std::clamp(ca * so + 0.5f, 0.0f, 255.0f));
                gradients[activeGradientIdx].stops.push_back(st);
            }
            if (!selfClosing) {
                SvgState child = stateStack.back();
                child.elemName = "stop";
                child.hidden = true;
                stateStack.push_back(std::move(child));
            }
            continue;
        }

        SvgState cur = stateStack.back();
        cur.elemName.assign(elem.data(), elem.size());
        if (isNonRenderingContainer(elem)) {
            cur.hidden = true;
        }
        applyAttrsToState(rawTag, cur);

        if (!selfClosing && (elem == "g" || elem == "svg" || elem == "a" || isNonRenderingContainer(elem))) {
            stateStack.push_back(cur);
            continue;
        }
        if (!selfClosing) {
            stateStack.push_back(cur);
        }
        if (cur.hidden) {
            continue;
        }

        SvgMat2D invElemCtm{};
        cur.ctm.Invert(invElemCtm);
        const float fillAlphaMul   = std::clamp(cur.opacity * cur.fillOpacity, 0.0f, 1.0f);
        const float strokeAlphaMul = std::clamp(cur.opacity * cur.strokeOpacity, 0.0f, 1.0f);
        const float strokeWorldW   = cur.strokeWidth * cur.ctm.ScaleFactor();

        auto renderShapeRings = [&](const std::vector<std::vector<std::pair<float, float>>>& localRings,
                                    const std::vector<bool>& closedFlags,
                                    bool allowFill)
        {
            if (localRings.empty()) return;
            SvgLocalBBox bbox{};
            std::vector<std::vector<std::pair<float, float>>> worldRings;
            worldRings.reserve(localRings.size());
            for (const auto& lr : localRings) {
                std::vector<std::pair<float, float>> wr;
                wr.reserve(lr.size());
                for (const auto& p : lr) {
                    bbox.Add(p.first, p.second);
                    wr.push_back(cur.ctm.Apply(p.first, p.second));
                }
                worldRings.push_back(std::move(wr));
            }
            if (!bbox.Valid()) { bbox.minX = bbox.minY = 0.0f; bbox.maxX = bbox.maxY = 1.0f; }

            if (allowFill && cur.fill.kind != SvgPaintSpec::None && fillAlphaMul > 0.0f) {
                fillCompoundWorldRings(worldRings, cur.evenOdd, cur.fill, invElemCtm, bbox, fillAlphaMul);
            }
            if (cur.stroke.kind != SvgPaintSpec::None && strokeAlphaMul > 0.0f && cur.strokeWidth > 0.0f) {
                for (size_t ri = 0; ri < worldRings.size(); ++ri) {
                    const auto& wr = worldRings[ri];
                    for (size_t i = 0; i + 1 < wr.size(); ++i) {
                        drawThickSeg(wr[i].first, wr[i].second, wr[i + 1].first, wr[i + 1].second,
                                     strokeWorldW, cur.stroke, invElemCtm, bbox, strokeAlphaMul);
                    }
                    if (ri < closedFlags.size() && closedFlags[ri] && wr.size() >= 3) {
                        drawThickSeg(wr.back().first, wr.back().second, wr.front().first, wr.front().second,
                                     strokeWorldW, cur.stroke, invElemCtm, bbox, strokeAlphaMul);
                    }
                }
            }
        };

        if (elem == "rect") {
            float x  = static_cast<float>(std::atof(std::string(extractAttr(rawTag, "x")).c_str()));
            float y  = static_cast<float>(std::atof(std::string(extractAttr(rawTag, "y")).c_str()));
            float rw = static_cast<float>(std::atof(std::string(extractAttr(rawTag, "width")).c_str()));
            float rh = static_cast<float>(std::atof(std::string(extractAttr(rawTag, "height")).c_str()));
            if (rw > 0.0f && rh > 0.0f) {
                std::string_view rxSv = extractAttr(rawTag, "rx");
                std::string_view rySv = extractAttr(rawTag, "ry");
                float crx = rxSv.empty() ? -1.0f : static_cast<float>(std::atof(std::string(rxSv).c_str()));
                float cry = rySv.empty() ? -1.0f : static_cast<float>(std::atof(std::string(rySv).c_str()));
                if (crx < 0.0f && cry >= 0.0f) crx = cry;
                if (cry < 0.0f && crx >= 0.0f) cry = crx;
                crx = std::clamp(crx, 0.0f, rw * 0.5f);
                cry = std::clamp(cry, 0.0f, rh * 0.5f);

                std::vector<std::pair<float, float>> pts;
                if (crx > 1e-4f && cry > 1e-4f) {
                    constexpr float kHalfPi = 1.5707963268f;
                    auto addCorner = [&](float ccx, float ccy, float a0) {
                        for (int s = 0; s <= 8; ++s) {
                            float ang = a0 + kHalfPi * (static_cast<float>(s) / 8.0f);
                            pts.push_back({ ccx + std::cos(ang) * crx, ccy + std::sin(ang) * cry });
                        }
                    };
                    addCorner(x + rw - crx, y + cry,      -kHalfPi);
                    addCorner(x + rw - crx, y + rh - cry,  0.0f);
                    addCorner(x + crx,      y + rh - cry,  kHalfPi);
                    addCorner(x + crx,      y + cry,       kHalfPi * 2.0f);
                } else {
                    pts = { { x, y }, { x + rw, y }, { x + rw, y + rh }, { x, y + rh } };
                }
                renderShapeRings({ std::move(pts) }, { true }, true);
            }
        } else if (elem == "circle" || elem == "ellipse") {
            float cx = static_cast<float>(std::atof(std::string(extractAttr(rawTag, "cx")).c_str()));
            float cy = static_cast<float>(std::atof(std::string(extractAttr(rawTag, "cy")).c_str()));
            float rx = (elem == "circle")
                ? static_cast<float>(std::atof(std::string(extractAttr(rawTag, "r")).c_str()))
                : static_cast<float>(std::atof(std::string(extractAttr(rawTag, "rx")).c_str()));
            float ry = (elem == "circle") ? rx : static_cast<float>(std::atof(std::string(extractAttr(rawTag, "ry")).c_str()));
            if (rx > 0.0f && ry > 0.0f) {
                std::vector<std::pair<float, float>> pts;
                pts.reserve(64);
                for (int i = 0; i < 64; ++i) {
                    float a = static_cast<float>(i) * (6.283185307f / 64.0f);
                    pts.push_back({ cx + std::cos(a) * rx, cy + std::sin(a) * ry });
                }
                renderShapeRings({ std::move(pts) }, { true }, true);
            }
        } else if (elem == "line") {
            float x1 = static_cast<float>(std::atof(std::string(extractAttr(rawTag, "x1")).c_str()));
            float y1 = static_cast<float>(std::atof(std::string(extractAttr(rawTag, "y1")).c_str()));
            float x2 = static_cast<float>(std::atof(std::string(extractAttr(rawTag, "x2")).c_str()));
            float y2 = static_cast<float>(std::atof(std::string(extractAttr(rawTag, "y2")).c_str()));
            renderShapeRings({ { { x1, y1 }, { x2, y2 } } }, { false }, false);
        } else if (elem == "polygon" || elem == "polyline") {
            std::string ptsStr(extractAttr(rawTag, "points"));
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
            bool isPoly = (elem == "polygon");
            renderShapeRings({ std::move(pts) }, { isPoly }, true);
        } else if (elem == "path") {
            std::string_view dAttr = extractAttr(rawTag, "d");
            std::vector<std::vector<std::pair<float, float>>> rings;
            std::vector<bool> closedFlags;
            std::vector<std::pair<float, float>> poly;
            float curX = 0.0f, curY = 0.0f, startX = 0.0f, startY = 0.0f;
            float lastCtrlX = 0.0f, lastCtrlY = 0.0f;
            char cmd = 0, prevCmd = 0;

            const char* p = dAttr.data();
            const char* end = dAttr.data() + dAttr.size();
            auto skipSep = [&]() {
                while (p < end && (*p == ' ' || *p == '\t' || *p == ',' || *p == '\r' || *p == '\n')) ++p;
            };
            auto isCmdChar = [](char c) -> bool {
                switch (c) {
                    case 'M': case 'm': case 'Z': case 'z': case 'L': case 'l':
                    case 'H': case 'h': case 'V': case 'v': case 'C': case 'c':
                    case 'S': case 's': case 'Q': case 'q': case 'T': case 't':
                    case 'A': case 'a': return true;
                    default: return false;
                }
            };
            auto readFloat = [&]() -> float {
                skipSep();
                if (p >= end) return 0.0f;
                char* next = nullptr;
                float v = std::strtof(p, &next);
                if (next > p) p = next;
                else ++p;
                return v;
            };
            // Dedicated single-character '0'/'1' flag lexer for minified SVG elliptical arcs (e.g., "a10,10 0 0110,0")
            auto readArcFlag = [&]() -> bool {
                skipSep();
                if (p < end && (*p == '0' || *p == '1')) {
                    return (*p++ == '1');
                }
                return readFloat() != 0.0f;
            };

            while (p < end) {
                skipSep();
                if (p >= end) break;
                if (isCmdChar(*p)) {
                    cmd = *p++;
                    if (cmd == 'Z' || cmd == 'z') {
                        if (!poly.empty()) {
                            poly.push_back({ startX, startY });
                            rings.push_back(std::move(poly));
                            closedFlags.push_back(true);
                            poly.clear();
                        }
                        curX = startX; curY = startY;
                        lastCtrlX = curX; lastCtrlY = curY;
                        prevCmd = cmd;
                        continue;
                    }
                } else if (cmd == 0) {
                    ++p;
                    continue;
                }

                if (cmd == 'M' || cmd == 'm') {
                    if (!poly.empty()) {
                        rings.push_back(std::move(poly));
                        closedFlags.push_back(false);
                        poly.clear();
                    }
                    float x = readFloat(), y = readFloat();
                    curX = (cmd == 'm') ? (curX + x) : x;
                    curY = (cmd == 'm') ? (curY + y) : y;
                    startX = curX; startY = curY;
                    lastCtrlX = curX; lastCtrlY = curY;
                    poly.push_back({ curX, curY });
                    prevCmd = cmd;
                    cmd = (cmd == 'm') ? 'l' : 'L';
                } else if (cmd == 'L' || cmd == 'l') {
                    float x = readFloat(), y = readFloat();
                    curX = (cmd == 'l') ? (curX + x) : x;
                    curY = (cmd == 'l') ? (curY + y) : y;
                    lastCtrlX = curX; lastCtrlY = curY;
                    poly.push_back({ curX, curY });
                    prevCmd = cmd;
                } else if (cmd == 'H' || cmd == 'h') {
                    float x = readFloat();
                    curX = (cmd == 'h') ? (curX + x) : x;
                    lastCtrlX = curX; lastCtrlY = curY;
                    poly.push_back({ curX, curY });
                    prevCmd = cmd;
                } else if (cmd == 'V' || cmd == 'v') {
                    float y = readFloat();
                    curY = (cmd == 'v') ? (curY + y) : y;
                    lastCtrlX = curX; lastCtrlY = curY;
                    poly.push_back({ curX, curY });
                    prevCmd = cmd;
                } else if (cmd == 'C' || cmd == 'c') {
                    float x1 = readFloat(), y1 = readFloat();
                    float x2 = readFloat(), y2 = readFloat();
                    float x  = readFloat(), y  = readFloat();
                    if (cmd == 'c') { x1 += curX; y1 += curY; x2 += curX; y2 += curY; x += curX; y += curY; }
                    for (int s = 1; s <= 12; ++s) {
                        float t = s / 12.0f, it = 1.0f - t;
                        float bx = it*it*it*curX + 3.0f*it*it*t*x1 + 3.0f*it*t*t*x2 + t*t*t*x;
                        float by = it*it*it*curY + 3.0f*it*it*t*y1 + 3.0f*it*t*t*y2 + t*t*t*y;
                        poly.push_back({ bx, by });
                    }
                    lastCtrlX = x2; lastCtrlY = y2;
                    curX = x; curY = y;
                    prevCmd = cmd;
                } else if (cmd == 'S' || cmd == 's') {
                    bool reflect = (prevCmd == 'C' || prevCmd == 'c' || prevCmd == 'S' || prevCmd == 's');
                    float x1 = reflect ? (2.0f * curX - lastCtrlX) : curX;
                    float y1 = reflect ? (2.0f * curY - lastCtrlY) : curY;
                    float x2 = readFloat(), y2 = readFloat();
                    float x  = readFloat(), y  = readFloat();
                    if (cmd == 's') { x2 += curX; y2 += curY; x += curX; y += curY; }
                    for (int s = 1; s <= 12; ++s) {
                        float t = s / 12.0f, it = 1.0f - t;
                        float bx = it*it*it*curX + 3.0f*it*it*t*x1 + 3.0f*it*t*t*x2 + t*t*t*x;
                        float by = it*it*it*curY + 3.0f*it*it*t*y1 + 3.0f*it*t*t*y2 + t*t*t*y;
                        poly.push_back({ bx, by });
                    }
                    lastCtrlX = x2; lastCtrlY = y2;
                    curX = x; curY = y;
                    prevCmd = cmd;
                } else if (cmd == 'Q' || cmd == 'q') {
                    float x1 = readFloat(), y1 = readFloat();
                    float x  = readFloat(), y  = readFloat();
                    if (cmd == 'q') { x1 += curX; y1 += curY; x += curX; y += curY; }
                    for (int s = 1; s <= 12; ++s) {
                        float t = s / 12.0f, it = 1.0f - t;
                        float bx = it*it*curX + 2.0f*it*t*x1 + t*t*x;
                        float by = it*it*curY + 2.0f*it*t*y1 + t*t*y;
                        poly.push_back({ bx, by });
                    }
                    lastCtrlX = x1; lastCtrlY = y1;
                    curX = x; curY = y;
                    prevCmd = cmd;
                } else if (cmd == 'T' || cmd == 't') {
                    bool reflect = (prevCmd == 'Q' || prevCmd == 'q' || prevCmd == 'T' || prevCmd == 't');
                    float x1 = reflect ? (2.0f * curX - lastCtrlX) : curX;
                    float y1 = reflect ? (2.0f * curY - lastCtrlY) : curY;
                    float x  = readFloat(), y  = readFloat();
                    if (cmd == 't') { x += curX; y += curY; }
                    for (int s = 1; s <= 12; ++s) {
                        float t = s / 12.0f, it = 1.0f - t;
                        float bx = it*it*curX + 2.0f*it*t*x1 + t*t*x;
                        float by = it*it*curY + 2.0f*it*t*y1 + t*t*y;
                        poly.push_back({ bx, by });
                    }
                    lastCtrlX = x1; lastCtrlY = y1;
                    curX = x; curY = y;
                    prevCmd = cmd;
                } else if (cmd == 'A' || cmd == 'a') {
                    float rx = readFloat();
                    float ry = readFloat();
                    float rot = readFloat();
                    bool largeArc = readArcFlag();
                    bool sweep = readArcFlag();
                    float x = readFloat(), y = readFloat();
                    if (cmd == 'a') { x += curX; y += curY; }
                    appendSvgEllipticalArc(poly, curX, curY, rx, ry, rot, largeArc, sweep, x, y);
                    curX = x; curY = y;
                    lastCtrlX = curX; lastCtrlY = curY;
                    prevCmd = cmd;
                } else {
                    ++p;
                }
            }
            if (!poly.empty()) {
                rings.push_back(std::move(poly));
                closedFlags.push_back(false);
            }
            renderShapeRings(rings, closedFlags, true);
        }
    }

    outInfo.valid = true;
    return true;
}
