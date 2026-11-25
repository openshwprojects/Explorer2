#pragma once
// XschemSchematic.h — Xschem Open-Source IC / Analog Schematic & Symbol (.xsch) Reader
// Parses `v {xschem version=...}` header and `N` (wire), `L` (line), `B` (box), `A` (full circle &
// tessellated partial `pa`..`pa+da` arcs), `P` (polygon), `T` (text), and `C` (component instance with
// `rot` 0..3 * 90 deg + `flip` mirror transforms and built-in NMOS/PMOS/R/C/L/D/V/GND/VDD/PIN symbols).
// NOTE: Included inside `namespace Explorer2`. Do NOT add `#include` directives here.

namespace Formats {

namespace DetailXschem {

constexpr float kXschemScale = 0.08f; // 20 Xschem units -> 1.6 mm

inline bool ExtractBraceBlock(std::string_view& rem, std::string_view& outInner) {
    while (!rem.empty() && static_cast<unsigned char>(rem.front()) <= 0x20u) rem.remove_prefix(1);
    if (rem.empty() || rem.front() != '{') return false;
    int depth = 0;
    for (size_t i = 0; i < rem.size(); ++i) {
        if (rem[i] == '\\' && i + 1u < rem.size()) { ++i; continue; }
        if (rem[i] == '{') ++depth;
        else if (rem[i] == '}') {
            --depth;
            if (depth == 0) {
                outInner = rem.substr(1u, i - 1u);
                rem.remove_prefix(i + 1u);
                return true;
            }
        }
    }
    return false;
}

inline std::string ExtractAttrValue(std::string_view props, std::string_view key) {
    size_t pos = 0;
    while (pos < props.size()) {
        while (pos < props.size() && static_cast<unsigned char>(props[pos]) <= 0x20u) ++pos;
        if (pos >= props.size()) break;
        const size_t eq = props.find('=', pos);
        if (eq == std::string_view::npos) break;
        std::string_view k = ReaderText::TrimSv(props.substr(pos, eq - pos));
        pos = eq + 1u;
        std::string val;
        if (pos < props.size() && props[pos] == '"') {
            ++pos;
            while (pos < props.size() && props[pos] != '"') {
                if (props[pos] == '\\' && pos + 1u < props.size()) ++pos;
                val.push_back(props[pos++]);
            }
            if (pos < props.size() && props[pos] == '"') ++pos;
        } else {
            const size_t sp = props.find_first_of(" \t\r\n", pos);
            val.assign(props.substr(pos, sp == std::string_view::npos ? props.size() - pos : sp - pos));
            pos = (sp == std::string_view::npos) ? props.size() : sp;
        }
        if (ReaderText::AsciiEqCI(k, key)) return val;
    }
    return {};
}

// Xschem screen space has +X right, +Y down.
// `flip == 1` mirrors X (`lx = -lx`), then `rot` (`0,1,2,3`) rotates clockwise by `rot * 90` deg in Xschem Y-down space,
// then translates by `(instX, instY)`, then converts to board space `(bx = sx * kScale, by = -sy * kScale)`!
struct XschemXf {
    float instX = 0.0f;
    float instY = 0.0f;
    int rot = 0;
    int flip = 0;

    void Apply(float lx, float ly, float& outBx, float& outBy) const {
        float x = (flip != 0) ? -lx : lx;
        float y = ly;
        float rx = x, ry = y;
        switch (rot & 3) {
        case 1: rx = -y; ry =  x; break; // 90 deg CW in Y-down
        case 2: rx = -x; ry = -y; break; // 180 deg
        case 3: rx =  y; ry = -x; break; // 270 deg CW
        default: break;
        }
        outBx = (instX + rx) * kXschemScale;
        outBy = -(instY + ry) * kXschemScale;
    }
};

inline void EmitBuiltinXschemSymbol(
    std::string_view symPath,
    const XschemXf& xf,
    ParsedEdaBoard& outBoard,
    int& outPinCount)
{
    outPinCount = 0;
    auto seg = [&](float x1, float y1, float x2, float y2, float w = 0.24f) {
        float bx1 = 0, by1 = 0, bx2 = 0, by2 = 0;
        xf.Apply(x1, y1, bx1, by1);
        xf.Apply(x2, y2, bx2, by2);
        outBoard.traces.push_back({ bx1, by1, bx2, by2, w, EdaLayerId::SchSymbol, 0 });
    };
    auto circ = [&](float cx, float cy, float r, bool filled = false) {
        float bx = 0, by = 0;
        xf.Apply(cx, cy, bx, by);
        outBoard.circles.push_back({ bx, by, r * kXschemScale, 0.20f, filled, EdaLayerId::SchSymbol });
    };
    auto pin = [&](float px, float py, const wchar_t* pname) {
        float bx = 0, by = 0;
        xf.Apply(px, py, bx, by);
        outBoard.pads.push_back({ bx, by, 0.55f, 0.55f, 0.0f, 0.0f, 0, EdaLayerId::SchSymbol, pname, L"" });
        ++outPinCount;
    };

    const std::string low = ReaderText::ToLowerAscii(symPath);

    // Pin / Power / Ground net symbols (`ipin`, `opin`, `iopin`, `lab_pin`, `vdd`, `gnd`, `vss`)
    if (low.find("gnd") != std::string::npos || low.find("vss") != std::string::npos) {
        seg(0, 0, 0, 10);
        seg(-12, 10, 12, 10, 0.28f);
        seg(-8, 15, 8, 15, 0.24f);
        seg(-4, 20, 4, 20, 0.20f);
        circ(0, 0, 4.0f, true);
        pin(0, 0, L"GND");
        return;
    }
    if (low.find("vdd") != std::string::npos || low.find("vcc") != std::string::npos) {
        seg(0, 0, 0, -12);
        seg(-12, -12, 12, -12, 0.30f);
        circ(0, 0, 4.0f, true);
        pin(0, 0, L"VDD");
        return;
    }
    if (low.find("ipin") != std::string::npos || low.find("opin") != std::string::npos ||
        low.find("iopin") != std::string::npos || low.find("lab_pin") != std::string::npos)
    {
        seg(-20, -6, -6, -6);
        seg(-6, -6, 0, 0);
        seg(0, 0, -6, 6);
        seg(-6, 6, -20, 6);
        seg(-20, 6, -20, -6);
        circ(0, 0, 4.5f, true);
        pin(0, 0, L"PIN");
        return;
    }

    // NMOS / NFET / PMOS / PFET (Xschem standard MOS: Gate at `(-20, 0)`, Drain at `(20, -30)`, Source at `(20, 30)`)
    const bool isPmos = (low.find("pmos") != std::string::npos || low.find("pfet") != std::string::npos);
    const bool isNmos = !isPmos && (low.find("nmos") != std::string::npos || low.find("nfet") != std::string::npos || low.find("mos") != std::string::npos);
    if (isPmos || isNmos) {
        seg(-4, -18, -4, 18, 0.28f); // gate oxide
        seg(4, -20, 4, 20, 0.26f);   // channel
        if (isPmos) {
            circ(-9.0f, 0.0f, 4.5f, false);
            seg(-20, 0, -13.5f, 0);
        } else {
            seg(-20, 0, -4, 0);
        }
        seg(4, -16, 20, -16);
        seg(20, -16, 20, -30);
        seg(4, 16, 20, 16);
        seg(20, 16, 20, 30);
        pin(-20, 0, L"G");
        pin(20, -30, L"D");
        pin(20, 30, L"S");
        if (low.find("4") != std::string::npos) {
            seg(4, 0, 20, 0);
            pin(20, 0, L"B");
        }
        return;
    }

    // Resistor (`res.sym`): pins at `(0, -30)` and `(0, 30)`
    if (low.find("res") != std::string::npos) {
        seg(0, -30, 0, -18);
        const float ry[7] = { -18, -12, -6, 0, 6, 12, 18 };
        const float rx[7] = { 0, 8, -8, 8, -8, 8, 0 };
        for (int k = 0; k < 6; ++k) seg(rx[k], ry[k], rx[k + 1], ry[k + 1]);
        seg(0, 18, 0, 30);
        pin(0, -30, L"P");
        pin(0, 30, L"M");
        return;
    }

    // Capacitor (`cap.sym` / `capa.sym`): pins at `(0, -30)` and `(0, 30)`
    if (low.find("cap") != std::string::npos) {
        seg(0, -30, 0, -5);
        seg(-14, -5, 14, -5, 0.30f);
        seg(-14, 5, 14, 5, 0.30f);
        seg(0, 5, 0, 30);
        pin(0, -30, L"1");
        pin(0, 30, L"2");
        return;
    }

    // Inductor (`ind.sym`): pins at `(0, -30)` and `(0, 30)`
    if (low.find("ind") != std::string::npos) {
        seg(0, -30, 0, -18);
        for (int k = 0; k < 3; ++k) {
            const float y0 = -18.0f + static_cast<float>(k) * 12.0f;
            seg(0, y0, 9, y0 + 6);
            seg(9, y0 + 6, 0, y0 + 12);
        }
        seg(0, 18, 0, 30);
        pin(0, -30, L"1");
        pin(0, 30, L"2");
        return;
    }

    // Voltage / Current Source (`vsource.sym`, `isource.sym`): circle + `(0, -30)` & `(0, 30)` pins
    if (low.find("vsource") != std::string::npos || low.find("isource") != std::string::npos || low.find("vdc") != std::string::npos) {
        seg(0, -30, 0, -16);
        circ(0, 0, 16.0f, false);
        seg(0, 16, 0, 30);
        // '+' marker at top inside circle
        seg(-4, -8, 4, -8, 0.20f);
        seg(0, -12, 0, -4, 0.20f);
        seg(-4, 8, 4, 8, 0.20f);
        pin(0, -30, L"+");
        pin(0, 30, L"-");
        return;
    }

    // Generic IC / Opamp / Subcircuit symbol box with transformed leads
    seg(-30, -36, 30, -36);
    seg(30, -36, 30, 36);
    seg(30, 36, -30, 36);
    seg(-30, 36, -30, -36);
    seg(-40, -18, -30, -18);
    seg(-40, 18, -30, 18);
    seg(30, 0, 40, 0);
    pin(-40, -18, L"IN1");
    pin(-40, 18, L"IN2");
    pin(40, 0, L"OUT");
}

} // namespace DetailXschem

inline bool ParseXschemSchematic(
    const uint8_t* data,
    size_t size,
    ParsedEdaBoard& outBoard)
{
    using namespace DetailXschem;
    if (!data || size < 14u || size > (32u << 20)) return false;

    std::string_view sv = ReaderText::StripUtf8Bom(std::string_view(reinterpret_cast<const char*>(data), size));
    if (sv.find('\0') != std::string_view::npos) return false;

    bool sawVersionHeader = false;
    int wireNetSeq = 1;
    std::set<std::string> namedNets;

    size_t pos = 0u;
    std::string logicalLine;

    while (pos < sv.size()) {
        std::string_view rawLine;
        if (!ReaderText::NextLineSv(sv, pos, rawLine)) break;
        logicalLine.assign(rawLine);

        // Join multi-line `{...}` attribute blocks (common in Xschem `v {...}`, `G {...}`, `S {...}`, `C {...}`)
        int braceBalance = 0;
        auto countBraces = [](std::string_view s) -> int {
            int b = 0;
            for (size_t i = 0; i < s.size(); ++i) {
                if (s[i] == '\\' && i + 1u < s.size()) { ++i; continue; }
                if (s[i] == '{') ++b;
                else if (s[i] == '}') --b;
            }
            return b;
        };
        braceBalance = countBraces(logicalLine);
        while (braceBalance > 0 && pos < sv.size()) {
            std::string_view cont;
            if (!ReaderText::NextLineSv(sv, pos, cont)) break;
            logicalLine.push_back(' ');
            logicalLine.append(cont);
            braceBalance += countBraces(cont);
        }
        if (braceBalance != 0) return false;

        std::string_view rem = ReaderText::TrimSv(logicalLine);
        if (rem.empty() || rem.front() == '#') continue;

        const char cmd = rem.front();
        if (rem.size() > 1u && static_cast<unsigned char>(rem[1]) > 0x20u && rem[1] != '{') {
            return false;
        }
        rem.remove_prefix(1u);

        if (cmd == 'v') {
            std::string_view vProps;
            if (!ExtractBraceBlock(rem, vProps)) return false;
            if (!ContainsTextCI(vProps, "xschem")) return false;
            sawVersionHeader = true;
            continue;
        }

        if (!sawVersionHeader) return false;

        if (cmd == 'G' || cmd == 'K' || cmd == 'V' || cmd == 'S' || cmd == 'E' || cmd == 'F') {
            // Schematic/VHDL/Verilog/SPICE header block
            continue;
        } else if (cmd == 'N') {
            // `N x1 y1 x2 y2 {lab=...}`
            float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            std::string_view t0, t1, t2, t3, props;
            if (!ReaderText::NextTokenSv(rem, t0) || !ReaderText::NextTokenSv(rem, t1) ||
                !ReaderText::NextTokenSv(rem, t2) || !ReaderText::NextTokenSv(rem, t3) ||
                !ReaderText::ParseFloatSv(t0, x1) || !ReaderText::ParseFloatSv(t1, y1) ||
                !ReaderText::ParseFloatSv(t2, x2) || !ReaderText::ParseFloatSv(t3, y2)) {
                return false;
            }
            outBoard.traces.push_back({
                x1 * kXschemScale, -y1 * kXschemScale,
                x2 * kXschemScale, -y2 * kXschemScale,
                0.24f, EdaLayerId::SchWire, wireNetSeq++
            });
            if (ExtractBraceBlock(rem, props)) {
                std::string lab = ExtractAttrValue(props, "lab");
                if (!lab.empty()) namedNets.insert(std::move(lab));
            }
        } else if (cmd == 'L') {
            // `L layer x1 y1 x2 y2 {...}`
            int lyr = 4;
            float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            std::string_view tl, t0, t1, t2, t3;
            if (!ReaderText::NextTokenSv(rem, tl) || !ReaderText::NextTokenSv(rem, t0) ||
                !ReaderText::NextTokenSv(rem, t1) || !ReaderText::NextTokenSv(rem, t2) ||
                !ReaderText::NextTokenSv(rem, t3) || !ReaderText::ParseIntSv(tl, lyr) ||
                !ReaderText::ParseFloatSv(t0, x1) || !ReaderText::ParseFloatSv(t1, y1) ||
                !ReaderText::ParseFloatSv(t2, x2) || !ReaderText::ParseFloatSv(t3, y2)) {
                return false;
            }
            (void)lyr;
            outBoard.traces.push_back({
                x1 * kXschemScale, -y1 * kXschemScale,
                x2 * kXschemScale, -y2 * kXschemScale,
                0.22f, EdaLayerId::SchSymbol, 0
            });
        } else if (cmd == 'B') {
            // `B layer x1 y1 x2 y2 {...}` (layer == 5 is Xschem symbol pin square!)
            int lyr = 4;
            float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            std::string_view tl, t0, t1, t2, t3, props;
            if (!ReaderText::NextTokenSv(rem, tl) || !ReaderText::NextTokenSv(rem, t0) ||
                !ReaderText::NextTokenSv(rem, t1) || !ReaderText::NextTokenSv(rem, t2) ||
                !ReaderText::NextTokenSv(rem, t3) || !ReaderText::ParseIntSv(tl, lyr) ||
                !ReaderText::ParseFloatSv(t0, x1) || !ReaderText::ParseFloatSv(t1, y1) ||
                !ReaderText::ParseFloatSv(t2, x2) || !ReaderText::ParseFloatSv(t3, y2)) {
                return false;
            }
            const float bx0 = std::min(x1, x2) * kXschemScale;
            const float bx1 = std::max(x1, x2) * kXschemScale;
            const float by0 = std::min(-y1, -y2) * kXschemScale;
            const float by1 = std::max(-y1, -y2) * kXschemScale;
            if (lyr == 5) {
                std::string pinNm = "PIN";
                if (ExtractBraceBlock(rem, props)) {
                    std::string n = ExtractAttrValue(props, "name");
                    if (!n.empty()) pinNm = std::move(n);
                }
                outBoard.pads.push_back({
                    (bx0 + bx1) * 0.5f, (by0 + by1) * 0.5f,
                    std::max(0.55f, bx1 - bx0), std::max(0.55f, by1 - by0),
                    0.0f, 0.0f, 1, EdaLayerId::SchSymbol, PathUtils::Utf8ToWide(pinNm), L""
                });
            } else {
                DrawEdaBoxRect(outBoard, bx0, by0, bx1, by1, 0.22f, EdaLayerId::SchSymbol, 0);
            }
        } else if (cmd == 'A') {
            // `A layer x y rad pa da {...}` (`pa` = start angle deg CCW, `da` = sweep angle deg)
            int lyr = 4;
            float cx = 0, cy = 0, rad = 0, paDeg = 0, daDeg = 360.0f;
            std::string_view tl, t0, t1, t2, t3, t4;
            if (!ReaderText::NextTokenSv(rem, tl) || !ReaderText::NextTokenSv(rem, t0) ||
                !ReaderText::NextTokenSv(rem, t1) || !ReaderText::NextTokenSv(rem, t2) ||
                !ReaderText::NextTokenSv(rem, t3) || !ReaderText::NextTokenSv(rem, t4) ||
                !ReaderText::ParseIntSv(tl, lyr)  || !ReaderText::ParseFloatSv(t0, cx) ||
                !ReaderText::ParseFloatSv(t1, cy) || !ReaderText::ParseFloatSv(t2, rad) ||
                !ReaderText::ParseFloatSv(t3, paDeg) || !ReaderText::ParseFloatSv(t4, daDeg)) {
                return false;
            }
            (void)lyr;
            const float wCx = cx * kXschemScale;
            const float wCy = -cy * kXschemScale;
            const float wR  = std::max(0.15f, std::fabs(rad) * kXschemScale);

            if (std::fabs(daDeg) >= 359.5f) {
                outBoard.circles.push_back({ wCx, wCy, wR, 0.22f, false, EdaLayerId::SchSymbol });
            } else if (std::fabs(daDeg) > 0.5f) {
                // Tessellate partial arc along `[paDeg .. paDeg + daDeg]` in math +Y-up angles!
                constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
                const int steps = std::clamp(static_cast<int>(std::ceil(std::fabs(daDeg) / 12.0f)), 4, 32);
                const float a0 = paDeg * kDegToRad;
                const float dA = daDeg * kDegToRad;
                float prevX = wCx + wR * std::cos(a0);
                float prevY = wCy + wR * std::sin(a0);
                for (int s = 1; s <= steps; ++s) {
                    const float ang = a0 + dA * (static_cast<float>(s) / static_cast<float>(steps));
                    const float nx = wCx + wR * std::cos(ang);
                    const float ny = wCy + wR * std::sin(ang);
                    outBoard.traces.push_back({ prevX, prevY, nx, ny, 0.22f, EdaLayerId::SchSymbol, 0 });
                    prevX = nx; prevY = ny;
                }
            }
        } else if (cmd == 'P') {
            // `P layer npts x0 y0 x1 y1 ... {...}`
            int lyr = 4, nPts = 0;
            std::string_view tl, tn;
            if (!ReaderText::NextTokenSv(rem, tl) || !ReaderText::NextTokenSv(rem, tn) ||
                !ReaderText::ParseIntSv(tl, lyr)  || !ReaderText::ParseIntSv(tn, nPts) || nPts < 2 || nPts > 1024) {
                return false;
            }
            (void)lyr;
            std::vector<std::pair<float, float>> pts;
            pts.reserve(static_cast<size_t>(nPts));
            for (int k = 0; k < nPts; ++k) {
                std::string_view tx, ty;
                float px = 0, py = 0;
                if (!ReaderText::NextTokenSv(rem, tx) || !ReaderText::NextTokenSv(rem, ty) ||
                    !ReaderText::ParseFloatSv(tx, px) || !ReaderText::ParseFloatSv(ty, py)) {
                    return false;
                }
                pts.push_back({ px * kXschemScale, -py * kXschemScale });
            }
            for (size_t k = 0; k + 1u < pts.size(); ++k) {
                outBoard.traces.push_back({ pts[k].first, pts[k].second, pts[k + 1u].first, pts[k + 1u].second, 0.22f, EdaLayerId::SchSymbol, 0 });
            }
        } else if (cmd == 'T') {
            // `T {text} x y rot flip xscale yscale {...}`
            std::string_view txtSv, tx, ty;
            float x = 0, y = 0;
            if (!ExtractBraceBlock(rem, txtSv) || !ReaderText::NextTokenSv(rem, tx) ||
                !ReaderText::NextTokenSv(rem, ty) || !ReaderText::ParseFloatSv(tx, x) ||
                !ReaderText::ParseFloatSv(ty, y)) {
                return false;
            }
            txtSv = ReaderText::TrimSv(txtSv);
            if (!txtSv.empty()) {
                outBoard.labels.push_back({
                    x * kXschemScale, -y * kXschemScale, 0.95f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(txtSv)
                });
            }
        } else if (cmd == 'C') {
            // `C {symPath} x y rot flip {name=M1 value=...}`
            std::string_view symSv, tx, ty, trot, tflip, props;
            float x = 0, y = 0;
            int rot = 0, flip = 0;
            if (!ExtractBraceBlock(rem, symSv) ||
                !ReaderText::NextTokenSv(rem, tx) || !ReaderText::NextTokenSv(rem, ty) ||
                !ReaderText::NextTokenSv(rem, trot) || !ReaderText::NextTokenSv(rem, tflip) ||
                !ReaderText::ParseFloatSv(tx, x) || !ReaderText::ParseFloatSv(ty, y) ||
                !ReaderText::ParseIntSv(trot, rot) || !ReaderText::ParseIntSv(tflip, flip)) {
                return false;
            }

            std::string instName, valStr, labStr;
            if (ExtractBraceBlock(rem, props)) {
                instName = ExtractAttrValue(props, "name");
                valStr   = ExtractAttrValue(props, "value");
                if (valStr.empty()) valStr = ExtractAttrValue(props, "model");
                labStr   = ExtractAttrValue(props, "lab");
            }
            if (!labStr.empty()) namedNets.insert(labStr);

            XschemXf xf{ x, y, rot, flip };
            int pinCnt = 0;
            EmitBuiltinXschemSymbol(symSv, xf, outBoard, pinCnt);

            const float wx = x * kXschemScale;
            const float wy = -y * kXschemScale;
            const std::string dispLbl = !labStr.empty() ? labStr : (!instName.empty() ? instName : std::string(symSv));
            if (!dispLbl.empty()) {
                std::wstring wLbl = PathUtils::Utf8ToWide(dispLbl);
                if (!valStr.empty() && labStr.empty()) wLbl += L"=" + PathUtils::Utf8ToWide(valStr);
                outBoard.labels.push_back({ wx + 1.4f, wy + 1.0f, 0.80f, EdaLayerId::SchLabel, wLbl });
            }

            // Emit component entry for active/passive instances (skip pure net labels if desired, or keep compact)
            const std::string lowSym = ReaderText::ToLowerAscii(symSv);
            const bool isNetPinOnly = (lowSym.find("lab_pin") != std::string::npos ||
                                       lowSym.find("gnd") != std::string::npos ||
                                       lowSym.find("vdd") != std::string::npos);
            if (!isNetPinOnly) {
                EdaComponent3D comp;
                comp.refDes = PathUtils::Utf8ToWide(instName.empty() ? "X?" : instName);
                comp.value = PathUtils::Utf8ToWide(valStr.empty() ? std::string(symSv) : valStr);
                comp.footprint = PathUtils::Utf8ToWide(symSv);
                comp.x = wx; comp.y = wy;
                comp.rotDeg = static_cast<float>((rot & 3) * 90);
                comp.bottomSide = (flip != 0);
                comp.widthMm = 3.8f; comp.lengthMm = 4.6f; comp.heightMm = 0.9f;
                comp.pinCount = std::max(1, pinCnt);
                comp.category = ClassifyComponent(comp.refDes, comp.footprint, comp.widthMm, comp.lengthMm, comp.heightMm, comp.pinCount);
                outBoard.components.push_back(std::move(comp));
            }
        } else {
            return false;
        }
    }

    if (!sawVersionHeader) return false;
    if (outBoard.traces.empty() && outBoard.circles.empty() && outBoard.pads.empty() && outBoard.components.empty()) {
        return false;
    }

    outBoard.kind = EdaDocKind::KiCadSch;
    outBoard.formatTitle = L"Xschem Circuit Schematic (.xsch — " +
                           std::to_wstring(outBoard.components.size()) + L" components)";
    outBoard.netCount = std::max<size_t>(1u, namedNets.empty() ? static_cast<size_t>(std::max(1, wireNetSeq - 1)) : namedNets.size());
    return true;
}

} // namespace Formats
