#pragma once
// LtspiceAsc.h — #22 LTspice Schematic (.asc) & Symbol (.asy) Parser with UTF-16LE, Built-in/Companion .asy Geometry & R0..M270 Pin Transforms

namespace Formats {

struct LtspiceSymLine { float x1, y1, x2, y2; };
struct LtspiceSymRect { float x1, y1, x2, y2; };
struct LtspiceSymCirc { float x1, y1, x2, y2; };
struct LtspiceSymArc  { float bx1, by1, bx2, by2, sx, sy, ex, ey; };
struct LtspiceSymPin  { float x, y; std::string name; int order = 0; };

struct LtspiceSymDef {
    std::vector<LtspiceSymLine> lines;
    std::vector<LtspiceSymRect> rects;
    std::vector<LtspiceSymCirc> circles;
    std::vector<LtspiceSymArc>  arcs;
    std::vector<LtspiceSymPin>  pins;
    std::string defaultValue;
    bool valid = false;
};

inline bool DecodeLtspiceTextBuffer(const uint8_t* data, size_t size, std::string& outUtf8) {
    outUtf8.clear();
    if (!data || size < 9 || size > 32 * 1024 * 1024) return false;

    // Check UTF-16LE BOM (FF FE) or BOM-less UTF-16LE ("V\0e\0r\0s\0")
    bool isUtf16LE = false;
    size_t startByte = 0;
    if (size >= 2 && data[0] == 0xFF && data[1] == 0xFE) {
        isUtf16LE = true;
        startByte = 2;
    } else if (size >= 16 && data[1] == 0 && data[3] == 0 && data[5] == 0 &&
               (data[0] == 'V' || data[0] == 'v' || data[0] == ' ' || data[0] == '\r' || data[0] == '\n')) {
        isUtf16LE = true;
        startByte = 0;
    }

    if (isUtf16LE) {
        size_t nChars = (size - startByte) / 2;
        if (nChars < 8) return false;
        std::wstring ws;
        ws.reserve(nChars);
        for (size_t i = 0; i < nChars; ++i) {
            uint16_t ch = static_cast<uint16_t>(data[startByte + i * 2]) |
                          (static_cast<uint16_t>(data[startByte + i * 2 + 1]) << 8);
            if (ch == 0) return false; // Embedded NUL code unit in text
            ws.push_back(static_cast<wchar_t>(ch));
        }
        int u8Len = WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()), nullptr, 0, nullptr, nullptr);
        if (u8Len <= 0) return false;
        outUtf8.resize(static_cast<size_t>(u8Len));
        WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()), outUtf8.data(), u8Len, nullptr, nullptr);
        return true;
    }

    // UTF-8 BOM or plain ASCII / CP1252 (LTspice legacy writes 0xB5 for micro 'u')
    if (size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        startByte = 3;
    }
    for (size_t i = startByte; i < size; ++i) {
        if (data[i] == 0) return false; // Reject binary files
    }
    // Check if valid UTF-8; if legacy CP1252 byte (like 0xB5 micro), transcode CP1252 -> UTF-8
    int wLen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                   reinterpret_cast<const char*>(data + startByte),
                                   static_cast<int>(size - startByte), nullptr, 0);
    if (wLen > 0) {
        outUtf8.assign(reinterpret_cast<const char*>(data + startByte), size - startByte);
        return true;
    }
    wLen = MultiByteToWideChar(1252, 0, reinterpret_cast<const char*>(data + startByte),
                               static_cast<int>(size - startByte), nullptr, 0);
    if (wLen <= 0) return false;
    std::wstring ws(static_cast<size_t>(wLen), L'\0');
    MultiByteToWideChar(1252, 0, reinterpret_cast<const char*>(data + startByte),
                        static_cast<int>(size - startByte), ws.data(), wLen);
    int u8Len = WideCharToMultiByte(CP_UTF8, 0, ws.data(), wLen, nullptr, 0, nullptr, nullptr);
    if (u8Len <= 0) return false;
    outUtf8.resize(static_cast<size_t>(u8Len));
    WideCharToMultiByte(CP_UTF8, 0, ws.data(), wLen, outUtf8.data(), u8Len, nullptr, nullptr);
    return true;
}

inline void TransformLtspicePt(float lx, float ly, std::string_view orient, float sx, float sy, float& outBx, float& outBy) {
    // LTspice screen coords (+X right, +Y down):
    // Rotate CW by 0/90/180/270, then if 'M', negate X, then translate by (sx, sy), then convert to board (+Y up, 0.1 mm/grid)
    bool mirror = (!orient.empty() && (orient[0] == 'M' || orient[0] == 'm'));
    int deg = 0;
    if (orient.size() >= 2) {
        deg = std::atoi(std::string(orient.substr(1)).c_str());
    }
    deg = ((deg % 360) + 360) % 360;
    float rx = lx, ry = ly;
    if (deg == 90)       { rx = -ly; ry =  lx; }
    else if (deg == 180) { rx = -lx; ry = -ly; }
    else if (deg == 270) { rx =  ly; ry = -lx; }
    if (mirror) rx = -rx;
    outBx = (sx + rx) * 0.1f;
    outBy = -(sy + ry) * 0.1f;
}

inline void EmitLtspiceEllipseOrArc(
    float bx1, float by1, float bx2, float by2,
    bool isArc, float sxPt, float syPt, float exPt, float eyPt,
    std::string_view orient, float origX, float origY,
    EdaLayerId layer, float strokeW,
    ParsedEdaBoard& out)
{
    float cx = (bx1 + bx2) * 0.5f;
    float cy = (by1 + by2) * 0.5f;
    float rx = std::fabs(bx2 - bx1) * 0.5f;
    float ry = std::fabs(by2 - by1) * 0.5f;
    if (rx < 0.5f || ry < 0.5f) return;

    const float twoPi = 6.283185307f;
    float aStart = 0.0f;
    float sweep = twoPi;
    if (isArc) {
        // In LTspice screen coords (+Y down), ARC sweeps counter-clockwise on screen (= decreasing atan2(y,x)!)
        aStart = std::atan2((syPt - cy) / ry, (sxPt - cx) / rx);
        float aEnd = std::atan2((eyPt - cy) / ry, (exPt - cx) / rx);
        sweep = aEnd - aStart;
        if (sweep >= -1e-4f) sweep -= twoPi;
        if (std::fabs(sweep) < 1e-3f) sweep = -twoPi;
    }

    int segs = isArc ? std::clamp(static_cast<int>(std::ceil(std::fabs(sweep) / (twoPi / 20.0f))), 4, 24) : 20;
    float px = 0.0f, py = 0.0f;
    for (int i = 0; i <= segs; ++i) {
        float ang = aStart + sweep * (static_cast<float>(i) / static_cast<float>(segs));
        float lx = cx + std::cos(ang) * rx;
        float ly = cy + std::sin(ang) * ry;
        float wx = 0.0f, wy = 0.0f;
        TransformLtspicePt(lx, ly, orient, origX, origY, wx, wy);
        if (i > 0 && out.traces.size() < 100000) {
            out.traces.push_back({ px, py, wx, wy, strokeW, layer, 0 });
        }
        px = wx; py = wy;
    }
}

inline bool ParseLtspiceAsyText(std::string_view sv, LtspiceSymDef& outSym) {
    outSym = LtspiceSymDef{};
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r' || sv.front() == '\n')) sv.remove_prefix(1);
    if (sv.rfind("Version 4", 0) != 0 || sv.find("SymbolType ") == std::string_view::npos) {
        return false;
    }

    size_t pos = 0;
    while (pos < sv.size()) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ' || ln.back() == '\t')) ln.remove_suffix(1);
        while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
        if (ln.empty()) continue;

        if (ln.rfind("LINE ", 0) == 0) {
            char style[32] = {};
            float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            if (sscanf_s(std::string(ln.substr(5)).c_str(), "%31s %f %f %f %f", style, (unsigned)_countof(style), &x1, &y1, &x2, &y2) == 5) {
                outSym.lines.push_back({ x1, y1, x2, y2 });
            }
        } else if (ln.rfind("RECTANGLE ", 0) == 0) {
            char style[32] = {};
            float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            if (sscanf_s(std::string(ln.substr(10)).c_str(), "%31s %f %f %f %f", style, (unsigned)_countof(style), &x1, &y1, &x2, &y2) == 5) {
                outSym.rects.push_back({ x1, y1, x2, y2 });
            }
        } else if (ln.rfind("CIRCLE ", 0) == 0) {
            char style[32] = {};
            float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            if (sscanf_s(std::string(ln.substr(7)).c_str(), "%31s %f %f %f %f", style, (unsigned)_countof(style), &x1, &y1, &x2, &y2) == 5) {
                outSym.circles.push_back({ x1, y1, x2, y2 });
            }
        } else if (ln.rfind("ARC ", 0) == 0) {
            char style[32] = {};
            LtspiceSymArc a{};
            if (sscanf_s(std::string(ln.substr(4)).c_str(), "%31s %f %f %f %f %f %f %f %f",
                         style, (unsigned)_countof(style), &a.bx1, &a.by1, &a.bx2, &a.by2, &a.sx, &a.sy, &a.ex, &a.ey) == 9) {
                outSym.arcs.push_back(a);
            }
        } else if (ln.rfind("PIN ", 0) == 0) {
            float px = 0, py = 0;
            if (sscanf_s(std::string(ln.substr(4)).c_str(), "%f %f", &px, &py) == 2) {
                outSym.pins.push_back({ px, py, "", static_cast<int>(outSym.pins.size() + 1) });
            }
        } else if (!outSym.pins.empty() && ln.rfind("PINATTR PinName ", 0) == 0) {
            outSym.pins.back().name = std::string(ln.substr(16));
        } else if (!outSym.pins.empty() && ln.rfind("PINATTR SpiceOrder ", 0) == 0) {
            outSym.pins.back().order = std::atoi(std::string(ln.substr(19)).c_str());
        } else if (ln.rfind("SYMATTR Value ", 0) == 0) {
            outSym.defaultValue = std::string(ln.substr(14));
        }
    }

    outSym.valid = (!outSym.lines.empty() || !outSym.rects.empty() || !outSym.circles.empty() ||
                    !outSym.arcs.empty() || !outSym.pins.empty());
    return outSym.valid;
}

inline bool GetBuiltinLtspiceSym(std::string_view rawSymName, LtspiceSymDef& out) {
    out = LtspiceSymDef{};
    // Strip directory prefix such as "OpAmps\\UniversalOpamp2" or "Misc/signal"
    size_t slash = rawSymName.find_last_of("\\/");
    std::string key(slash == std::string_view::npos ? rawSymName : rawSymName.substr(slash + 1));
    for (char& c : key) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');

    if (key == "res" || key == "res2") {
        // Standard LTspice resistor: pins at (16, 16) and (16, 96), zigzag body from y=28..84
        out.lines = {
            { 16, 16, 16, 28 }, { 16, 28, 26, 33 }, { 26, 33,  6, 43 },
            {  6, 43, 26, 52 }, { 26, 52,  6, 61 }, {  6, 61, 26, 70 },
            { 26, 70,  6, 79 }, {  6, 79, 16, 84 }, { 16, 84, 16, 96 }
        };
        out.pins = { { 16, 16, "1", 1 }, { 16, 96, "2", 2 } };
        out.valid = true;
        return true;
    }
    if (key == "cap" || key == "polcap") {
        // Standard LTspice capacitor: pins at (16, 0) and (16, 64), plates at y=24 and y=40
        out.lines = {
            { 16, 0, 16, 24 }, { 0, 24, 32, 24 },
            { 0, 40, 32, 40 }, { 16, 40, 16, 64 }
        };
        if (key == "polcap") {
            out.lines.push_back({ 4, 12, 10, 12 });
            out.lines.push_back({ 7,  9,  7, 15 });
        }
        out.pins = { { 16, 0, "1", 1 }, { 16, 64, "2", 2 } };
        out.valid = true;
        return true;
    }
    if (key == "ind" || key == "ind2") {
        // Standard LTspice inductor: pins at (16, 16) and (16, 96), 4 semicircular loops
        out.lines = { { 16, 16, 16, 24 }, { 16, 88, 16, 96 } };
        for (int k = 0; k < 4; ++k) {
            float y0 = 24.0f + k * 16.0f;
            out.arcs.push_back({ 6.0f, y0, 26.0f, y0 + 16.0f, 16.0f, y0 + 16.0f, 16.0f, y0 });
        }
        if (key == "ind2") out.circles.push_back({ 4, 20, 8, 24 });
        out.pins = { { 16, 16, "1", 1 }, { 16, 96, "2", 2 } };
        out.valid = true;
        return true;
    }
    if (key == "diode" || key == "zener" || key == "schottky" || key == "led") {
        // Standard LTspice diode: pins at (16, 0) and (16, 64), triangle y=20..44
        out.lines = {
            { 16,  0, 16, 20 },
            {  2, 20, 30, 20 }, {  2, 20, 16, 44 }, { 30, 20, 16, 44 },
            {  2, 44, 30, 44 },
            { 16, 44, 16, 64 }
        };
        if (key == "zener") {
            out.lines.push_back({ 2, 44, -2, 38 });
            out.lines.push_back({ 30, 44, 34, 50 });
        } else if (key == "schottky") {
            out.lines.push_back({ 2, 44, 2, 39 }); out.lines.push_back({ 2, 39, 6, 39 });
            out.lines.push_back({ 30, 44, 30, 49 }); out.lines.push_back({ 30, 49, 26, 49 });
        } else if (key == "led") {
            out.lines.push_back({ 28, 28, 38, 36 });
            out.lines.push_back({ 28, 36, 38, 44 });
        }
        out.pins = { { 16, 0, "A", 1 }, { 16, 64, "K", 2 } };
        out.valid = true;
        return true;
    }
    if (key == "voltage" || key == "current" || key == "bv" || key == "bi") {
        // Standard LTspice independent/behavioral source: pins at (0, 16) and (0, 96), circle center (0, 56) r=24
        out.lines = { { 0, 16, 0, 32 }, { 0, 80, 0, 96 } };
        out.circles = { { -24, 32, 24, 80 } };
        if (key == "current" || key == "bi") {
            out.lines.push_back({ 0, 40, 0, 72 });
            out.lines.push_back({ -6, 62, 0, 72 });
            out.lines.push_back({  6, 62, 0, 72 });
        } else {
            out.lines.push_back({ -5, 44, 5, 44 });
            out.lines.push_back({  0, 39, 0, 49 });
            out.lines.push_back({ -5, 68, 5, 68 });
        }
        out.pins = { { 0, 16, "+", 1 }, { 0, 96, "-", 2 } };
        out.valid = true;
        return true;
    }
    if (key == "npn" || key == "pnp" || key == "npn2" || key == "pnp2") {
        // Standard LTspice BJT: C=(64, 0), B=(0, 48), E=(64, 96)
        out.lines = {
            { 0, 48, 24, 48 }, { 24, 24, 24, 72 },
            { 24, 34, 64, 12 }, { 64, 12, 64, 0 },
            { 24, 62, 64, 84 }, { 64, 84, 64, 96 }
        };
        out.circles = { { 12, 16, 72, 80 } };
        out.pins = { { 64, 0, "C", 1 }, { 0, 48, "B", 2 }, { 64, 96, "E", 3 } };
        out.valid = true;
        return true;
    }
    if (key == "nmos" || key == "pmos" || key == "nmos4" || key == "pmos4") {
        // Standard LTspice MOSFET: D=(48, 0), G=(0, 80), S=(48, 96)
        out.lines = {
            { 0, 80, 16, 80 }, { 16, 24, 16, 80 },
            { 24, 20, 24, 36 }, { 24, 44, 24, 60 }, { 24, 68, 24, 84 },
            { 24, 28, 48, 28 }, { 48, 28, 48, 0 },
            { 24, 76, 48, 76 }, { 48, 76, 48, 96 },
            { 24, 52, 48, 52 }, { 48, 52, 48, 76 }
        };
        out.pins = { { 48, 0, "D", 1 }, { 0, 80, "G", 2 }, { 48, 96, "S", 3 } };
        out.valid = true;
        return true;
    }
    if (key == "opamp" || key == "opamp2" || key == "universalopamp2" || key == "universalopamp") {
        // Standard LTspice 5-pin OpAmp: In-=(-32, 48), In+=(-32, 80), V+=(0, 32), V-=(0, 96), Out=(32, 64)
        out.lines = {
            { -32, 32, -32, 96 }, { -32, 32, 32, 64 }, { -32, 96, 32, 64 },
            { -26, 48, -20, 48 },
            { -26, 80, -20, 80 }, { -23, 77, -23, 83 },
            { 0, 32, 0, 48 }, { 0, 96, 0, 80 }
        };
        out.pins = {
            { -32, 80, "In+", 1 }, { -32, 48, "In-", 2 },
            {   0, 32, "V+",  3 }, {   0, 96, "V-",  4 },
            {  32, 64, "Out", 5 }
        };
        out.valid = true;
        return true;
    }
    if (key == "sw" || key == "csw") {
        out.lines = { { 0, 16, 0, 32 }, { 0, 32, 14, 60 }, { 0, 64, 0, 96 } };
        out.pins = { { 0, 16, "1", 1 }, { 0, 96, "2", 2 }, { -32, 32, "+", 3 }, { -32, 64, "-", 4 } };
        out.valid = true;
        return true;
    }
    return false;
}

inline void StampLtspiceSymInstance(
    const LtspiceSymDef& sym,
    float sx, float sy,
    std::string_view orient,
    const std::wstring& refDes,
    const std::wstring& valStr,
    const std::wstring& footprintName,
    ParsedEdaBoard& out)
{
    float bMinX = 1e9f, bMinY = 1e9f, bMaxX = -1e9f, bMaxY = -1e9f;
    auto expB = [&](float x, float y) {
        bMinX = std::min(bMinX, x); bMaxX = std::max(bMaxX, x);
        bMinY = std::min(bMinY, y); bMaxY = std::max(bMaxY, y);
    };

    for (const auto& l : sym.lines) {
        float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
        TransformLtspicePt(l.x1, l.y1, orient, sx, sy, x1, y1);
        TransformLtspicePt(l.x2, l.y2, orient, sx, sy, x2, y2);
        expB(x1, y1); expB(x2, y2);
        if (out.traces.size() < 100000) {
            out.traces.push_back({ x1, y1, x2, y2, 0.24f, EdaLayerId::SchSymbol, 0 });
        }
    }
    for (const auto& r : sym.rects) {
        float x1 = 0, y1 = 0, x2 = 0, y2 = 0, x3 = 0, y3 = 0, x4 = 0, y4 = 0;
        TransformLtspicePt(r.x1, r.y1, orient, sx, sy, x1, y1);
        TransformLtspicePt(r.x2, r.y1, orient, sx, sy, x2, y2);
        TransformLtspicePt(r.x2, r.y2, orient, sx, sy, x3, y3);
        TransformLtspicePt(r.x1, r.y2, orient, sx, sy, x4, y4);
        expB(x1, y1); expB(x3, y3);
        if (out.traces.size() + 4 <= 100000) {
            out.traces.push_back({ x1, y1, x2, y2, 0.24f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ x2, y2, x3, y3, 0.24f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ x3, y3, x4, y4, 0.24f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ x4, y4, x1, y1, 0.24f, EdaLayerId::SchSymbol, 0 });
        }
    }
    for (const auto& c : sym.circles) {
        EmitLtspiceEllipseOrArc(c.x1, c.y1, c.x2, c.y2, false, 0, 0, 0, 0, orient, sx, sy, EdaLayerId::SchSymbol, 0.22f, out);
        float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
        TransformLtspicePt(c.x1, c.y1, orient, sx, sy, x1, y1);
        TransformLtspicePt(c.x2, c.y2, orient, sx, sy, x2, y2);
        expB(x1, y1); expB(x2, y2);
    }
    for (const auto& a : sym.arcs) {
        EmitLtspiceEllipseOrArc(a.bx1, a.by1, a.bx2, a.by2, true, a.sx, a.sy, a.ex, a.ey, orient, sx, sy, EdaLayerId::SchSymbol, 0.22f, out);
        float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
        TransformLtspicePt(a.bx1, a.by1, orient, sx, sy, x1, y1);
        TransformLtspicePt(a.bx2, a.by2, orient, sx, sy, x2, y2);
        expB(x1, y1); expB(x2, y2);
    }
    for (const auto& p : sym.pins) {
        float px = 0, py = 0;
        TransformLtspicePt(p.x, p.y, orient, sx, sy, px, py);
        expB(px, py);
        if (out.pads.size() < 60000) {
            EdaPadPrim pad{};
            pad.x = px; pad.y = py;
            pad.w = 0.65f; pad.h = 0.65f;
            pad.shape = 0;
            pad.layer = EdaLayerId::SchSymbol;
            pad.pinName = PathUtils::Utf8ToWide(p.name.empty() ? std::to_string(p.order) : p.name);
            out.pads.push_back(std::move(pad));
        }
    }

    if (bMinX > bMaxX) {
        float cx = sx * 0.1f, cy = -sy * 0.1f;
        bMinX = cx - 2.4f; bMaxX = cx + 2.4f;
        bMinY = cy - 1.6f; bMaxY = cy + 1.6f;
        out.traces.push_back({ bMinX, bMinY, bMaxX, bMinY, 0.24f, EdaLayerId::SchSymbol, 0 });
        out.traces.push_back({ bMaxX, bMinY, bMaxX, bMaxY, 0.24f, EdaLayerId::SchSymbol, 0 });
        out.traces.push_back({ bMaxX, bMaxY, bMinX, bMaxY, 0.24f, EdaLayerId::SchSymbol, 0 });
        out.traces.push_back({ bMinX, bMaxY, bMinX, bMinY, 0.24f, EdaLayerId::SchSymbol, 0 });
    }

    EdaComponent3D comp;
    comp.refDes = refDes;
    comp.value = valStr;
    comp.footprint = footprintName;
    comp.x = (bMinX + bMaxX) * 0.5f;
    comp.y = (bMinY + bMaxY) * 0.5f;
    comp.widthMm = std::max(1.2f, bMaxX - bMinX);
    comp.lengthMm = std::max(1.0f, bMaxY - bMinY);
    comp.heightMm = 1.4f;
    comp.pinCount = std::max(2, static_cast<int>(sym.pins.size()));
    if (orient.size() >= 2) comp.rotDeg = static_cast<float>(std::atoi(std::string(orient.substr(1)).c_str()));
    comp.bottomSide = (!orient.empty() && (orient[0] == 'M' || orient[0] == 'm'));

    std::wstring lbl = comp.refDes;
    if (!comp.value.empty()) lbl += L" = " + comp.value;
    if (!lbl.empty() && out.labels.size() < 12000) {
        out.labels.push_back({ bMaxX + 0.4f, comp.y, 1.0f, EdaLayerId::SchLabel, lbl });
    }
    out.components.push_back(std::move(comp));
}

inline bool LoadCompanionLtspiceAsy(const std::wstring& ascFullPath, std::string_view symName, LtspiceSymDef& outSym) {
    if (ascFullPath.empty() || symName.empty()) return false;
    size_t sep = ascFullPath.find_last_of(L"\\/");
    if (sep == std::wstring::npos) return false;
    std::wstring dir = ascFullPath.substr(0, sep);
    std::wstring rel = PathUtils::Utf8ToWide(symName);
    for (wchar_t& ch : rel) if (ch == L'/') ch = L'\\';
    if (rel.size() < 4 || PathUtils::ToLower(rel.substr(rel.size() - 4)) != L".asy") {
        rel += L".asy";
    }

    const std::wstring candidates[3] = {
        dir + L"\\" + rel,
        dir + L"\\sym\\" + rel,
        (rel.find(L'\\') != std::wstring::npos) ? (dir + L"\\" + rel.substr(rel.find_last_of(L'\\') + 1)) : std::wstring{}
    };
    for (const auto& cand : candidates) {
        if (cand.empty()) continue;
        std::vector<uint8_t> rawAsy;
        if (PathUtils::ReadFileBytes(cand, 2 * 1024 * 1024, rawAsy) && !rawAsy.empty()) {
            std::string asyUtf8;
            if (DecodeLtspiceTextBuffer(rawAsy.data(), rawAsy.size(), asyUtf8) &&
                ParseLtspiceAsyText(asyUtf8, outSym)) {
                return true;
            }
        }
    }
    return false;
}

inline bool ParseLtspiceTextInternal(std::string_view sv, const std::wstring& fullPath, const std::wstring& ext, ParsedEdaBoard& out) {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r' || sv.front() == '\n')) {
        sv.remove_prefix(1);
    }
    if (sv.rfind("Version 4", 0) != 0) {
        return false;
    }

    // Standalone .asy symbol preview
    if (ext == L".asy" || (sv.find("SymbolType ") != std::string_view::npos && sv.find("SHEET ") == std::string_view::npos)) {
        LtspiceSymDef sym;
        if (!ParseLtspiceAsyText(sv, sym)) return false;
        out.kind = EdaDocKind::KiCadSch;
        out.formatTitle = L"LTspice Symbol (.asy)";
        std::wstring symName = PathUtils::GetFileName(fullPath);
        StampLtspiceSymInstance(sym, 0.0f, 0.0f, "R0", symName, PathUtils::Utf8ToWide(sym.defaultValue), symName, out);
        out.netCount = std::max<size_t>(1, sym.pins.size());
        return (!out.traces.empty() || !out.pads.empty());
    }

    // Must contain SHEET or WIRE or SYMBOL to be a real LTspice .asc schematic
    if (sv.find("SHEET ") == std::string_view::npos && sv.find("WIRE ") == std::string_view::npos && sv.find("SYMBOL ") == std::string_view::npos) {
        return false;
    }

    out.kind = EdaDocKind::KiCadSch;
    out.formatTitle = L"LTspice Schematic (.asc)";

    std::unordered_map<std::string, LtspiceSymDef> symCache;
    auto resolveSym = [&](const std::string& symName) -> const LtspiceSymDef& {
        auto it = symCache.find(symName);
        if (it != symCache.end()) return it->second;
        LtspiceSymDef def;
        if (!LoadCompanionLtspiceAsy(fullPath, symName, def)) {
            GetBuiltinLtspiceSym(symName, def);
        }
        return symCache.emplace(symName, std::move(def)).first->second;
    };

    struct PendingInst {
        std::string symName;
        std::string orient = "R0";
        float sx = 0.0f, sy = 0.0f;
        std::wstring refDes;
        std::wstring value;
        bool active = false;
    } pend;

    auto flushPending = [&]() {
        if (!pend.active) return;
        const LtspiceSymDef& def = resolveSym(pend.symName);
        std::wstring fpW = PathUtils::Utf8ToWide(pend.symName);
        std::wstring refW = pend.refDes.empty() ? fpW : pend.refDes;
        std::wstring valW = pend.value.empty() ? PathUtils::Utf8ToWide(def.defaultValue) : pend.value;
        StampLtspiceSymInstance(def, pend.sx, pend.sy, pend.orient, refW, valW, fpW, out);
        pend = PendingInst{};
    };

    std::set<std::string> distinctNets;

    size_t pos = 0;
    while (pos < sv.size()) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ' || ln.back() == '\t')) ln.remove_suffix(1);
        while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
        if (ln.empty()) continue;

        if (ln.rfind("WIRE ", 0) == 0) {
            float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            if (sscanf_s(std::string(ln.substr(5)).c_str(), "%f %f %f %f", &x1, &y1, &x2, &y2) == 4) {
                if (out.traces.size() < 100000) {
                    out.traces.push_back({ x1 * 0.1f, -y1 * 0.1f, x2 * 0.1f, -y2 * 0.1f, 0.25f, EdaLayerId::SchWire, 0 });
                }
            }
        } else if (ln.rfind("SYMBOL ", 0) == 0) {
            flushPending();
            char sym[128] = {};
            char ori[16] = "R0";
            float sx = 0, sy = 0;
            int got = sscanf_s(std::string(ln.substr(7)).c_str(), "%127s %f %f %15s",
                               sym, (unsigned)_countof(sym), &sx, &sy, ori, (unsigned)_countof(ori));
            if (got >= 3) {
                pend.symName = sym;
                pend.sx = sx;
                pend.sy = sy;
                pend.orient = (got >= 4) ? ori : "R0";
                pend.active = true;
            }
        } else if (pend.active && ln.rfind("SYMATTR InstName ", 0) == 0) {
            pend.refDes = PathUtils::Utf8ToWide(ln.substr(17));
        } else if (pend.active && ln.rfind("SYMATTR Value ", 0) == 0) {
            pend.value = PathUtils::Utf8ToWide(ln.substr(14));
        } else if (pend.active && ln.rfind("SYMATTR Value2 ", 0) == 0 && pend.value.empty()) {
            pend.value = PathUtils::Utf8ToWide(ln.substr(15));
        } else if (ln.rfind("FLAG ", 0) == 0) {
            float fx = 0, fy = 0;
            char netName[128] = {};
            if (sscanf_s(std::string(ln.substr(5)).c_str(), "%f %f %127s", &fx, &fy, netName, (unsigned)_countof(netName)) == 3) {
                float bx = fx * 0.1f, by = -fy * 0.1f;
                distinctNets.insert(netName);
                if (std::strcmp(netName, "0") == 0 || _stricmp(netName, "GND") == 0) {
                    // Draw standard IEEE Ground triangle/bars at (bx, by)
                    out.traces.push_back({ bx, by, bx, by - 0.8f, 0.22f, EdaLayerId::SchSymbol, 0 });
                    out.traces.push_back({ bx - 1.1f, by - 0.8f, bx + 1.1f, by - 0.8f, 0.24f, EdaLayerId::SchSymbol, 0 });
                    out.traces.push_back({ bx - 0.7f, by - 1.3f, bx + 0.7f, by - 1.3f, 0.22f, EdaLayerId::SchSymbol, 0 });
                    out.traces.push_back({ bx - 0.3f, by - 1.8f, bx + 0.3f, by - 1.8f, 0.20f, EdaLayerId::SchSymbol, 0 });
                } else {
                    out.vias.push_back({ bx, by, 0.65f, 0.0f, 0 });
                    out.labels.push_back({ bx + 0.3f, by + 0.6f, 0.95f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(netName) });
                }
            }
        } else if (ln.rfind("IOPIN ", 0) == 0) {
            float ix = 0, iy = 0;
            char dirStr[32] = {};
            if (sscanf_s(std::string(ln.substr(6)).c_str(), "%f %f %31s", &ix, &iy, dirStr, (unsigned)_countof(dirStr)) >= 2) {
                float bx = ix * 0.1f, by = -iy * 0.1f;
                out.traces.push_back({ bx - 0.9f, by - 0.5f, bx + 0.5f, by - 0.5f, 0.20f, EdaLayerId::SchSymbol, 0 });
                out.traces.push_back({ bx + 0.5f, by - 0.5f, bx + 1.0f, by,        0.20f, EdaLayerId::SchSymbol, 0 });
                out.traces.push_back({ bx + 1.0f, by,        bx + 0.5f, by + 0.5f, 0.20f, EdaLayerId::SchSymbol, 0 });
                out.traces.push_back({ bx + 0.5f, by + 0.5f, bx - 0.9f, by + 0.5f, 0.20f, EdaLayerId::SchSymbol, 0 });
            }
        } else if (ln.rfind("LINE ", 0) == 0) {
            char style[32] = {};
            float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            if (sscanf_s(std::string(ln.substr(5)).c_str(), "%31s %f %f %f %f", style, (unsigned)_countof(style), &x1, &y1, &x2, &y2) == 5) {
                out.traces.push_back({ x1 * 0.1f, -y1 * 0.1f, x2 * 0.1f, -y2 * 0.1f, 0.18f, EdaLayerId::SchLabel, 0 });
            }
        } else if (ln.rfind("RECTANGLE ", 0) == 0) {
            char style[32] = {};
            float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            if (sscanf_s(std::string(ln.substr(10)).c_str(), "%31s %f %f %f %f", style, (unsigned)_countof(style), &x1, &y1, &x2, &y2) == 5) {
                float bx1 = x1 * 0.1f, by1 = -y1 * 0.1f, bx2 = x2 * 0.1f, by2 = -y2 * 0.1f;
                out.traces.push_back({ bx1, by1, bx2, by1, 0.18f, EdaLayerId::SchLabel, 0 });
                out.traces.push_back({ bx2, by1, bx2, by2, 0.18f, EdaLayerId::SchLabel, 0 });
                out.traces.push_back({ bx2, by2, bx1, by2, 0.18f, EdaLayerId::SchLabel, 0 });
                out.traces.push_back({ bx1, by2, bx1, by1, 0.18f, EdaLayerId::SchLabel, 0 });
            }
        } else if (ln.rfind("CIRCLE ", 0) == 0) {
            char style[32] = {};
            float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            if (sscanf_s(std::string(ln.substr(7)).c_str(), "%31s %f %f %f %f", style, (unsigned)_countof(style), &x1, &y1, &x2, &y2) == 5) {
                EmitLtspiceEllipseOrArc(x1, y1, x2, y2, false, 0, 0, 0, 0, "R0", 0, 0, EdaLayerId::SchLabel, 0.18f, out);
            }
        } else if (ln.rfind("ARC ", 0) == 0) {
            char style[32] = {};
            LtspiceSymArc a{};
            if (sscanf_s(std::string(ln.substr(4)).c_str(), "%31s %f %f %f %f %f %f %f %f",
                         style, (unsigned)_countof(style), &a.bx1, &a.by1, &a.bx2, &a.by2, &a.sx, &a.sy, &a.ex, &a.ey) == 9) {
                EmitLtspiceEllipseOrArc(a.bx1, a.by1, a.bx2, a.by2, true, a.sx, a.sy, a.ex, a.ey, "R0", 0, 0, EdaLayerId::SchLabel, 0.18f, out);
            }
        } else if (ln.rfind("TEXT ", 0) == 0) {
            // TEXT <x> <y> <just> <size> [;|!]<content>
            float tx = 0, ty = 0;
            char just[32] = {};
            int tSize = 2;
            int consumed = 0;
            if (sscanf_s(std::string(ln.substr(5)).c_str(), "%f %f %31s %d%n",
                         &tx, &ty, just, (unsigned)_countof(just), &tSize, &consumed) >= 4 && consumed > 0) {
                std::string_view msg = ln.substr(5 + static_cast<size_t>(consumed));
                while (!msg.empty() && msg.front() == ' ') msg.remove_prefix(1);
                if (!msg.empty() && (msg.front() == '!' || msg.front() == ';')) msg.remove_prefix(1);
                if (!msg.empty() && out.labels.size() < 12000) {
                    out.labels.push_back({ tx * 0.1f, -ty * 0.1f, 0.95f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(msg) });
                }
            }
        }
    }

    flushPending();

    if (out.traces.empty() && out.components.empty() && out.pads.empty()) {
        return false;
    }
    out.netCount = std::max<size_t>(1, distinctNets.empty() ? out.components.size() : distinctNets.size());
    return true;
}

inline bool ParseLtspiceBytes(const uint8_t* data, size_t size, const std::wstring& fullPath, const std::wstring& ext, ParsedEdaBoard& out) {
    std::string utf8;
    if (!DecodeLtspiceTextBuffer(data, size, utf8)) {
        return false;
    }
    return ParseLtspiceTextInternal(utf8, fullPath, ext, out);
}

} // namespace Formats
