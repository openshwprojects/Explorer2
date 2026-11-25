#pragma once
// FidoCadJCircuit.h — FidoCad / FidoCadJ Electronic Schematic & PCB Layout (.fcd) Reader
// Parses `[FIDOCAD]` / `[FIDOLIB]` headers and `LI`, `PL`, `PA`, `SA`, `RV`/`RP`, `EV`/`EP`, `BE`,
// `MC` (with full `rot` 0..3 * 90 deg + `mir` transforms & standard FidoCadJ passive/transistor/opamp/GND/VCC
// library symbol geometry), `TY`, and `PP`/`PV`/`CV`/`CP` primitives (1 FidoCad unit = 5 mil = 0.127 mm).
// NOTE: Included inside `namespace Explorer2`. Do NOT add `#include` directives here.

namespace Formats {

namespace DetailFidoCad {

constexpr float kUnitMm = 0.127f; // 5 mil = 0.127 mm

// FidoCadJ coordinate space: +X right, +Y down.
// Mirror (`mir == 1`) reflects X (`lx = -lx`), then `rot` (`0..3`) rotates in 90-deg steps, then `(instX, instY)`.
struct FcdXf {
    float instX = 0.0f;
    float instY = 0.0f;
    int rot = 0;
    int mir = 0;

    void Apply(float lx, float ly, float& outBx, float& outBy) const {
        float x = (mir != 0) ? -lx : lx;
        float y = ly;
        float rx = x, ry = y;
        switch (rot & 3) {
        case 1: rx = -y; ry =  x; break;
        case 2: rx = -x; ry = -y; break;
        case 3: rx =  y; ry = -x; break;
        default: break;
        }
        outBx = (instX + rx) * kUnitMm;
        outBy = -(instY + ry) * kUnitMm;
    }
};

// Standard FidoCadJ `ihram` / `ey_libraries` / standard Italian & English macro symbol shapes (in FidoCad 5-mil grid units!)
inline void EmitFidoCadMacroSymbol(
    std::string_view macroKey,
    const FcdXf& xf,
    ParsedEdaBoard& outBoard,
    int& outPins)
{
    outPins = 2;
    auto seg = [&](float x1, float y1, float x2, float y2, float w = 0.22f) {
        float bx1 = 0, by1 = 0, bx2 = 0, by2 = 0;
        xf.Apply(x1, y1, bx1, by1);
        xf.Apply(x2, y2, bx2, by2);
        outBoard.traces.push_back({ bx1, by1, bx2, by2, w, EdaLayerId::SchSymbol, 0 });
    };
    auto circ = [&](float cx, float cy, float rUnits, bool filled = false) {
        float bx = 0, by = 0;
        xf.Apply(cx, cy, bx, by);
        outBoard.circles.push_back({ bx, by, rUnits * kUnitMm, 0.20f, filled, EdaLayerId::SchSymbol });
    };
    auto pin = [&](float px, float py, const wchar_t* nm) {
        float bx = 0, by = 0;
        xf.Apply(px, py, bx, by);
        outBoard.pads.push_back({ bx, by, 0.50f, 0.50f, 0.0f, 0.0f, 0, EdaLayerId::SchSymbol, nm, L"" });
    };

    const std::string low = ReaderText::ToLowerAscii(macroKey);

    // Ground (`massa`, `gnd`, `chasis`, `0200` / `0100` standard FidoCad ground)
    if (low.find("gnd") != std::string::npos || low.find("massa") != std::string::npos || low == "0200") {
        seg(0, 0, 0, 5);
        seg(-6, 5, 6, 5, 0.28f);
        seg(-4, 8, 4, 8, 0.24f);
        seg(-2, 11, 2, 11, 0.20f);
        pin(0, 0, L"GND");
        outPins = 1;
        return;
    }

    // Power rail (`vcc`, `vdd`, `alim`, `+v`, `0210`)
    if (low.find("vcc") != std::string::npos || low.find("vdd") != std::string::npos || low.find("alim") != std::string::npos) {
        seg(0, 0, 0, -6);
        seg(-6, -6, 6, -6, 0.30f);
        pin(0, 0, L"VCC");
        outPins = 1;
        return;
    }

    // Resistor (`res`, `resistore`, `r_`, `0300`, `0310`) — standard FidoCad resistor spans `(0,0)` to `(30,0)` or `(-15,0)..(15,0)` -> center-aligned `(-15,0)..(15,0)` or `(0,0)..(20,0)`:
    // FidoCad standard passive origin `(0,0)` has pin 1 at `(0,0)` and pin 2 at `(20,0)`!
    if (low.find("res") != std::string::npos || low == "r" || low.rfind("r_", 0) == 0 || low == "0300" || low == "0310") {
        seg(0, 0, 4, 0);
        const float rx[7] = { 4, 6, 9, 11, 14, 16, 17 };
        const float ry[7] = { 0, -4, 4, -4, 4, -2, 0 };
        for (int k = 0; k < 6; ++k) seg(rx[k], ry[k], rx[k + 1], ry[k + 1]);
        seg(17, 0, 20, 0);
        pin(0, 0, L"1");
        pin(20, 0, L"2");
        return;
    }

    // Capacitor (`cap`, `cond`, `elettrol`, `0400`, `0410`): pin 1 at `(0,0)`, pin 2 at `(20,0)` (or vertical `(0,0)..(0,20)` via `rot`)
    if (low.find("cap") != std::string::npos || low.find("cond") != std::string::npos || low.find("elettr") != std::string::npos ||
        low == "c" || low == "0400" || low == "0410")
    {
        seg(0, 0, 8, 0);
        seg(8, -6, 8, 6, 0.28f);
        seg(12, -6, 12, 6, 0.28f);
        seg(12, 0, 20, 0);
        if (low.find("el") != std::string::npos || low == "0410") {
            seg(4, -5, 6, -5, 0.18f);
            seg(5, -6, 5, -4, 0.18f);
        }
        pin(0, 0, L"1");
        pin(20, 0, L"2");
        return;
    }

    // Inductor (`ind`, `bobina`, `0500`)
    if (low.find("ind") != std::string::npos || low.find("bob") != std::string::npos || low == "l" || low == "0500") {
        seg(0, 0, 4, 0);
        for (int k = 0; k < 3; ++k) {
            const float x0 = 4.0f + static_cast<float>(k) * 4.0f;
            seg(x0, 0, x0 + 2.0f, -4.0f);
            seg(x0 + 2.0f, -4.0f, x0 + 4.0f, 0);
        }
        seg(16, 0, 20, 0);
        pin(0, 0, L"1");
        pin(20, 0, L"2");
        return;
    }

    // Diode / LED / Zener (`diod`, `led`, `zener`, `0600`)
    if (low.find("diod") != std::string::npos || low.find("led") != std::string::npos || low.find("zen") != std::string::npos) {
        seg(0, 0, 6, 0);
        seg(6, -5, 6, 5);
        seg(6, -5, 14, 0);
        seg(6, 5, 14, 0);
        seg(14, -5, 14, 5, 0.28f);
        seg(14, 0, 20, 0);
        pin(0, 0, L"A");
        pin(20, 0, L"K");
        return;
    }

    // BJT / MOSFET (`npn`, `pnp`, `bjt`, `mos`, `fet`, `transistor`)
    if (low.find("npn") != std::string::npos || low.find("pnp") != std::string::npos ||
        low.find("bjt") != std::string::npos || low.find("mos") != std::string::npos || low.find("fet") != std::string::npos)
    {
        circ(10, 0, 9.0f, false);
        seg(0, 0, 6, 0);
        seg(6, -6, 6, 6, 0.28f);
        seg(6, -3, 15, -10);
        seg(6, 3, 15, 10);
        pin(0, 0, L"B");
        pin(15, -10, L"C");
        pin(15, 10, L"E");
        outPins = 3;
        return;
    }

    // Operational Amplifier (`opamp`, `operaz`, `lm358`, `tl08`, `ua741`)
    if (low.find("opamp") != std::string::npos || low.find("operaz") != std::string::npos ||
        low.find("lm3") != std::string::npos || low.find("tl0") != std::string::npos || low.find("741") != std::string::npos)
    {
        seg(0, -5, 5, -5);
        seg(0, 5, 5, 5);
        seg(5, -12, 5, 12, 0.26f);
        seg(5, -12, 25, 0, 0.26f);
        seg(25, 0, 5, 12, 0.26f);
        seg(25, 0, 30, 0);
        // '-' and '+' input signs
        seg(7, -5, 10, -5, 0.18f);
        seg(7, 5, 10, 5, 0.18f);
        seg(8.5f, 3.5f, 8.5f, 6.5f, 0.18f);
        pin(0, -5, L"-");
        pin(0, 5, L"+");
        pin(30, 0, L"OUT");
        outPins = 5;
        return;
    }

    // Default IC / generic macro outline with transformed orientation & pin stubs
    seg(-14, -10, 14, -10);
    seg(14, -10, 14, 10);
    seg(14, 10, -14, 10);
    seg(-14, 10, -14, -10);
    seg(-18, -5, -14, -5);
    seg(-18, 5, -14, 5);
    seg(14, -5, 18, -5);
    seg(14, 5, 18, 5);
    pin(-18, -5, L"1");
    pin(-18, 5, L"2");
    pin(18, 5, L"3");
    pin(18, -5, L"4");
    outPins = 4;
}

} // namespace DetailFidoCad

inline bool ParseFidoCadJCircuit(
    const uint8_t* data,
    size_t size,
    ParsedEdaBoard& outBoard)
{
    using namespace DetailFidoCad;
    if (!data || size < 12 || size > 32u * 1024u * 1024u) return false;
    std::string_view sv = ReaderText::StripUtf8Bom(std::string_view(reinterpret_cast<const char*>(data), size));
    if (sv.find('\0') != std::string_view::npos) return false;

    auto mapLayer = [](int lyr, bool& sawPcbLayer) -> EdaLayerId {
        if (lyr == 1) { sawPcbLayer = true; return EdaLayerId::BottomCopper; }
        if (lyr == 2) { sawPcbLayer = true; return EdaLayerId::TopCopper; }
        if (lyr == 3) return EdaLayerId::TopSilk;
        return EdaLayerId::SchWire;
    };

    bool sawHeader = false;
    bool sawPcb = false;
    int netSeq = 1;
    size_t lastMcCompIdx = std::numeric_limits<size_t>::max();
    int tySinceMc = 0;

    size_t pos = 0;
    std::string_view line;
    while (ReaderText::NextLineSv(sv, pos, line)) {
        line = ReaderText::TrimSv(line);
        if (line.empty()) continue;

        if (!sawHeader) {
            if (ReaderText::StartsWithCI(line, "[FIDOCAD") || ReaderText::StartsWithCI(line, "[FIDOLIB")) {
                sawHeader = true;
                continue;
            }
            return false;
        }

        if (line.front() == '[') continue;

        std::string_view rem = line;
        std::string_view cmd;
        if (!ReaderText::NextTokenSv(rem, cmd)) continue;

        if (cmd == "FJC" || cmd == "FCJ" || cmd == "FJ" || cmd == "TE") {
            continue;
        } else if (cmd == "LI") {
            float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            std::string_view t0, t1, t2, t3, t4;
            if (!ReaderText::NextTokenSv(rem, t0) || !ReaderText::NextTokenSv(rem, t1) ||
                !ReaderText::NextTokenSv(rem, t2) || !ReaderText::NextTokenSv(rem, t3) ||
                !ReaderText::ParseFloatSv(t0, x1) || !ReaderText::ParseFloatSv(t1, y1) ||
                !ReaderText::ParseFloatSv(t2, x2) || !ReaderText::ParseFloatSv(t3, y2)) {
                return false;
            }
            int lyr = 0;
            if (ReaderText::NextTokenSv(rem, t4)) ReaderText::ParseIntSv(t4, lyr);
            outBoard.traces.push_back({
                x1 * kUnitMm, -y1 * kUnitMm, x2 * kUnitMm, -y2 * kUnitMm,
                0.24f, mapLayer(lyr, sawPcb), netSeq++
            });
            lastMcCompIdx = std::numeric_limits<size_t>::max();
        } else if (cmd == "PL") {
            float x1 = 0, y1 = 0, x2 = 0, y2 = 0, w = 2.0f;
            std::string_view t0, t1, t2, t3, t4, t5;
            if (!ReaderText::NextTokenSv(rem, t0) || !ReaderText::NextTokenSv(rem, t1) ||
                !ReaderText::NextTokenSv(rem, t2) || !ReaderText::NextTokenSv(rem, t3) ||
                !ReaderText::NextTokenSv(rem, t4) ||
                !ReaderText::ParseFloatSv(t0, x1) || !ReaderText::ParseFloatSv(t1, y1) ||
                !ReaderText::ParseFloatSv(t2, x2) || !ReaderText::ParseFloatSv(t3, y2) ||
                !ReaderText::ParseFloatSv(t4, w)) {
                return false;
            }
            int lyr = 2;
            if (ReaderText::NextTokenSv(rem, t5)) ReaderText::ParseIntSv(t5, lyr);
            sawPcb = true;
            outBoard.traces.push_back({
                x1 * kUnitMm, -y1 * kUnitMm, x2 * kUnitMm, -y2 * kUnitMm,
                std::clamp(w * kUnitMm, 0.15f, 6.0f), mapLayer(lyr, sawPcb), netSeq++
            });
            lastMcCompIdx = std::numeric_limits<size_t>::max();
        } else if (cmd == "PA") {
            float x = 0, y = 0, rx = 6, ry = 6, dr = 2;
            int shape = 0, lyr = 2;
            std::string_view t0, t1, t2, t3, t4, t5, t6;
            if (!ReaderText::NextTokenSv(rem, t0) || !ReaderText::NextTokenSv(rem, t1) ||
                !ReaderText::NextTokenSv(rem, t2) || !ReaderText::NextTokenSv(rem, t3) ||
                !ReaderText::NextTokenSv(rem, t4) || !ReaderText::NextTokenSv(rem, t5) ||
                !ReaderText::ParseFloatSv(t0, x)  || !ReaderText::ParseFloatSv(t1, y) ||
                !ReaderText::ParseFloatSv(t2, rx) || !ReaderText::ParseFloatSv(t3, ry) ||
                !ReaderText::ParseFloatSv(t4, dr) || !ReaderText::ParseIntSv(t5, shape)) {
                return false;
            }
            if (ReaderText::NextTokenSv(rem, t6)) ReaderText::ParseIntSv(t6, lyr);
            sawPcb = true;
            const uint8_t padShape = (shape == 1) ? 1u : ((shape == 2) ? 2u : 0u);
            outBoard.pads.push_back({
                x * kUnitMm, -y * kUnitMm,
                std::max(0.5f, rx * kUnitMm), std::max(0.5f, ry * kUnitMm),
                0.0f, std::max(0.0f, dr * kUnitMm),
                padShape, mapLayer(lyr, sawPcb), L"", L""
            });
            lastMcCompIdx = std::numeric_limits<size_t>::max();
        } else if (cmd == "SA") {
            float x = 0, y = 0;
            std::string_view t0, t1;
            if (!ReaderText::NextTokenSv(rem, t0) || !ReaderText::NextTokenSv(rem, t1) ||
                !ReaderText::ParseFloatSv(t0, x) || !ReaderText::ParseFloatSv(t1, y)) {
                return false;
            }
            outBoard.circles.push_back({ x * kUnitMm, -y * kUnitMm, 0.35f, 0.2f, true, EdaLayerId::SchWire });
        } else if (cmd == "RV" || cmd == "RP") {
            float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            int lyr = 0;
            std::string_view t0, t1, t2, t3, t4;
            if (!ReaderText::NextTokenSv(rem, t0) || !ReaderText::NextTokenSv(rem, t1) ||
                !ReaderText::NextTokenSv(rem, t2) || !ReaderText::NextTokenSv(rem, t3) ||
                !ReaderText::ParseFloatSv(t0, x1) || !ReaderText::ParseFloatSv(t1, y1) ||
                !ReaderText::ParseFloatSv(t2, x2) || !ReaderText::ParseFloatSv(t3, y2)) {
                return false;
            }
            if (ReaderText::NextTokenSv(rem, t4)) ReaderText::ParseIntSv(t4, lyr);
            DrawEdaBoxRect(outBoard,
                std::min(x1, x2) * kUnitMm, std::min(-y1, -y2) * kUnitMm,
                std::max(x1, x2) * kUnitMm, std::max(-y1, -y2) * kUnitMm,
                0.22f, (lyr == 0) ? EdaLayerId::SchSymbol : mapLayer(lyr, sawPcb), 0);
        } else if (cmd == "EV" || cmd == "EP") {
            float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            std::string_view t0, t1, t2, t3;
            if (!ReaderText::NextTokenSv(rem, t0) || !ReaderText::NextTokenSv(rem, t1) ||
                !ReaderText::NextTokenSv(rem, t2) || !ReaderText::NextTokenSv(rem, t3) ||
                !ReaderText::ParseFloatSv(t0, x1) || !ReaderText::ParseFloatSv(t1, y1) ||
                !ReaderText::ParseFloatSv(t2, x2) || !ReaderText::ParseFloatSv(t3, y2)) {
                return false;
            }
            const float cx = (x1 + x2) * 0.5f * kUnitMm;
            const float cy = -(y1 + y2) * 0.5f * kUnitMm;
            const float rad = std::max(0.25f, (std::fabs(x2 - x1) + std::fabs(y2 - y1)) * 0.25f * kUnitMm);
            outBoard.circles.push_back({ cx, cy, rad, 0.22f, (cmd == "EP"), EdaLayerId::SchSymbol });
        } else if (cmd == "BE") {
            float p[8] = {};
            for (int k = 0; k < 8; ++k) {
                std::string_view tk;
                if (!ReaderText::NextTokenSv(rem, tk) || !ReaderText::ParseFloatSv(tk, p[k])) return false;
            }
            float prevX = p[0] * kUnitMm, prevY = -p[1] * kUnitMm;
            for (int s = 1; s <= 8; ++s) {
                const float t = static_cast<float>(s) / 8.0f;
                const float u = 1.0f - t;
                const float bx = (u*u*u * p[0] + 3*u*u*t * p[2] + 3*u*t*t * p[4] + t*t*t * p[6]) * kUnitMm;
                const float by = -(u*u*u * p[1] + 3*u*u*t * p[3] + 3*u*t*t * p[5] + t*t*t * p[7]) * kUnitMm;
                outBoard.traces.push_back({ prevX, prevY, bx, by, 0.22f, EdaLayerId::SchSymbol, 0 });
                prevX = bx; prevY = by;
            }
        } else if (cmd == "MC") {
            float x = 0, y = 0;
            int rot = 0, mir = 0;
            std::string_view t0, t1, t2, t3, macroName;
            if (!ReaderText::NextTokenSv(rem, t0) || !ReaderText::NextTokenSv(rem, t1) ||
                !ReaderText::NextTokenSv(rem, t2) || !ReaderText::NextTokenSv(rem, t3) ||
                !ReaderText::NextTokenSv(rem, macroName) ||
                !ReaderText::ParseFloatSv(t0, x) || !ReaderText::ParseFloatSv(t1, y) ||
                !ReaderText::ParseIntSv(t2, rot) || !ReaderText::ParseIntSv(t3, mir)) {
                return false;
            }

            int pins = 2;
            FcdXf xf{ x, y, rot, mir };
            EmitFidoCadMacroSymbol(macroName, xf, outBoard, pins);

            const float wx = x * kUnitMm, wy = -y * kUnitMm;
            EdaComponent3D comp;
            wchar_t defRef[32] = {};
            swprintf_s(defRef, L"M%zu", outBoard.components.size() + 1u);
            comp.refDes = defRef;
            comp.value  = PathUtils::Utf8ToWide(macroName);
            comp.footprint = comp.value;
            comp.x = wx;
            comp.y = wy;
            comp.rotDeg = static_cast<float>((rot & 3) * 90);
            comp.bottomSide = (mir != 0);
            comp.category = ClassifyComponent(comp.refDes, comp.footprint, comp.widthMm, comp.lengthMm, comp.heightMm, pins);
            comp.pinCount = pins;
            lastMcCompIdx = outBoard.components.size();
            tySinceMc = 0;
            outBoard.components.push_back(std::move(comp));
        } else if (cmd == "TY") {
            float x = 0, y = 0, h = 4;
            std::string_view t0, t1, t2, t3, t4, t5, t6;
            if (!ReaderText::NextTokenSv(rem, t0) || !ReaderText::NextTokenSv(rem, t1) ||
                !ReaderText::NextTokenSv(rem, t2) || !ReaderText::NextTokenSv(rem, t3) ||
                !ReaderText::NextTokenSv(rem, t4) || !ReaderText::NextTokenSv(rem, t5) ||
                !ReaderText::NextTokenSv(rem, t6) ||
                !ReaderText::ParseFloatSv(t0, x) || !ReaderText::ParseFloatSv(t1, y) ||
                !ReaderText::ParseFloatSv(t2, h)) {
                return false;
            }
            int dummyLyr = 0;
            if (ReaderText::ParseIntSv(t6, dummyLyr)) {
                std::string_view fontTok;
                ReaderText::NextTokenSv(rem, fontTok);
            }
            std::string_view txt = ReaderText::TrimSv(rem);
            if (!txt.empty()) {
                const std::wstring wtxt = PathUtils::Utf8ToWide(txt);
                outBoard.labels.push_back({
                    x * kUnitMm, -y * kUnitMm, std::clamp(h * kUnitMm * 1.4f, 0.7f, 2.8f),
                    EdaLayerId::SchLabel, wtxt
                });
                if (lastMcCompIdx < outBoard.components.size()) {
                    if (tySinceMc == 0) outBoard.components[lastMcCompIdx].refDes = wtxt;
                    else if (tySinceMc == 1) outBoard.components[lastMcCompIdx].value = wtxt;
                    ++tySinceMc;
                }
            }
        } else if (cmd == "PP" || cmd == "PV" || cmd == "CV" || cmd == "CP") {
            std::vector<float> nums;
            std::string_view tk;
            while (ReaderText::NextTokenSv(rem, tk)) {
                float v = 0;
                if (!ReaderText::ParseFloatSv(tk, v)) return false;
                nums.push_back(v);
            }
            const size_t pairCount = nums.size() / 2u;
            for (size_t k = 0; k + 1u < pairCount; ++k) {
                outBoard.traces.push_back({
                    nums[k * 2u] * kUnitMm, -nums[k * 2u + 1u] * kUnitMm,
                    nums[(k + 1u) * 2u] * kUnitMm, -nums[(k + 1u) * 2u + 1u] * kUnitMm,
                    0.22f, EdaLayerId::SchSymbol, 0
                });
            }
        } else {
            return false;
        }
    }

    if (!sawHeader) return false;
    if (outBoard.traces.empty() && outBoard.pads.empty() && outBoard.components.empty() && outBoard.circles.empty()) {
        return false;
    }

    outBoard.kind = sawPcb ? EdaDocKind::KiCadPcb : EdaDocKind::KiCadSch;
    outBoard.formatTitle = sawPcb ? L"FidoCadJ PCB Layout (.fcd)" : L"FidoCadJ Circuit Schematic (.fcd)";
    outBoard.netCount = static_cast<size_t>(std::max(1, netSeq - 1));
    return true;
}

} // namespace Formats
