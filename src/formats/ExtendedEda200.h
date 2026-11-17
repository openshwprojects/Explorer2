#pragma once
#include "../ByteIO.h"
// ExtendedEda200.h — 20 Schematics, Silicon OASIS, Signal Integrity, FPGA, Scope/Logic, Automotive & PLC Viewers (#61–#80)
// Covers:
//   PCB/RF/Silicon (#61–#66): .kicad_sym, .ses, .pos/.cpl/.mnt, .ibs, .s4p/.s3p/.s8p, .oas
//   FPGA/PLD/JTAG (#67–#70, #80): .xdc/.sdc, .qsf, .jed, .svf/.xsvf, .sof/.pof
//   Logic/Scope/Automotive/PLC (#71–#79): .sr, .sal, .wfm/.isf, .mf4/.mdf, .ldf, .arxml, .eds/.dcf, .gsdml, .l5x

namespace Formats {

namespace DetailEda200 {

using ::Explorer2::ByteIO::RdU16LE;
using ::Explorer2::ByteIO::RdU32LE;
using ::Explorer2::ByteIO::RdU64LE;
using ::Explorer2::ByteIO::RdU16BE;
using ::Explorer2::ByteIO::RdU32BE;

struct JtagBitCycle {
    uint8_t tms = 0;
    uint8_t tdi = 0;
    uint8_t tdo = 0;
    bool hasTdo = false;
};

struct JtagTapTracker {
    static constexpr size_t kMaxVisualCycles = 96;
    // IEEE 1149.1 TAP next-state table indexed by XAPP503 state encoding (0x00..0x0F) and TMS (0 or 1)
    static constexpr uint8_t kNextTap[16][2] = {
        { 0x01, 0x00 }, // 0x00 Test-Logic-Reset
        { 0x01, 0x02 }, // 0x01 Run-Test/Idle
        { 0x03, 0x09 }, // 0x02 Select-DR-Scan
        { 0x04, 0x05 }, // 0x03 Capture-DR
        { 0x04, 0x05 }, // 0x04 Shift-DR
        { 0x06, 0x08 }, // 0x05 Exit1-DR
        { 0x06, 0x07 }, // 0x06 Pause-DR
        { 0x04, 0x08 }, // 0x07 Exit2-DR
        { 0x01, 0x02 }, // 0x08 Update-DR
        { 0x0A, 0x00 }, // 0x09 Select-IR-Scan
        { 0x0B, 0x0C }, // 0x0A Capture-IR
        { 0x0B, 0x0C }, // 0x0B Shift-IR
        { 0x0D, 0x0F }, // 0x0C Exit1-IR
        { 0x0D, 0x0E }, // 0x0D Pause-IR
        { 0x0B, 0x0F }, // 0x0E Exit2-IR
        { 0x01, 0x02 }, // 0x0F Update-IR
    };

    uint8_t curState = 0x00;
    std::vector<JtagBitCycle> cycles;

    void PushClock(uint8_t tms, uint8_t tdi = 0, uint8_t tdo = 0, bool hasTdo = false) {
        if (cycles.size() < kMaxVisualCycles) {
            cycles.push_back({ static_cast<uint8_t>(tms & 1u), static_cast<uint8_t>(tdi & 1u), static_cast<uint8_t>(tdo & 1u), hasTdo });
        }
        if (curState < 16u) {
            curState = kNextTap[curState][tms & 1u];
        }
    }

    void StepTo(uint8_t target, bool forceClockIfSame) {
        if (target > 0x0Fu) return;
        if (curState == target) {
            if (forceClockIfSame) {
                if (target == 0x00u) PushClock(1);
                else if (target == 0x01u || target == 0x04u || target == 0x06u || target == 0x0Bu || target == 0x0Du) PushClock(0);
            }
            return;
        }
        int prevSt[16];
        uint8_t prevTms[16] = {};
        for (int i = 0; i < 16; ++i) prevSt[i] = -1;
        uint8_t q[16] = {};
        int qh = 0, qt = 0;
        q[qt++] = curState;
        prevSt[curState] = static_cast<int>(curState);
        while (qh < qt) {
            uint8_t u = q[qh++];
            if (u == target) break;
            for (uint8_t tms = 0; tms <= 1u; ++tms) {
                uint8_t v = kNextTap[u][tms];
                if (prevSt[v] < 0) {
                    prevSt[v] = static_cast<int>(u);
                    prevTms[v] = tms;
                    q[qt++] = v;
                }
            }
        }
        if (prevSt[target] < 0) return;
        uint8_t path[16] = {};
        int plen = 0;
        uint8_t cur = target;
        while (cur != curState && plen < 16) {
            path[plen++] = prevTms[cur];
            cur = static_cast<uint8_t>(prevSt[cur]);
        }
        for (int i = plen - 1; i >= 0; --i) {
            PushClock(path[i]);
        }
    }

    void EmitIdleClocks(uint32_t count, uint8_t waitState = 0x01u) {
        StepTo(waitState, false);
        uint32_t vis = std::min<uint32_t>(count, 8u);
        uint8_t holdTms = (waitState == 0x00u) ? 1u : 0u;
        for (uint32_t i = 0; i < vis; ++i) {
            PushClock(holdTms);
        }
    }

    void ShiftBytesBE(
        uint8_t shiftState,
        uint8_t endState,
        uint32_t numBits,
        const uint8_t* tdiBytes,
        const uint8_t* tdoBytes,
        const std::vector<uint8_t>* tdoMask,
        bool exitOnLast)
    {
        if (numBits == 0u) return;
        StepTo(shiftState, false);
        const size_t nb = (static_cast<size_t>(numBits) + 7u) / 8u;
        const uint32_t emitLimit = (cycles.size() < kMaxVisualCycles)
            ? static_cast<uint32_t>(std::min<size_t>(static_cast<size_t>(numBits), kMaxVisualCycles - cycles.size()))
            : 0u;

        for (uint32_t b = 0; b < emitLimit; ++b) {
            bool isLast = exitOnLast && (b + 1u == numBits);
            size_t byteIdx = nb - 1u - static_cast<size_t>(b / 8u);
            int bitShift = static_cast<int>(b & 7u);
            uint8_t tdiBit = (tdiBytes != nullptr) ? static_cast<uint8_t>((tdiBytes[byteIdx] >> bitShift) & 1u) : 0u;
            bool careTdo = (tdoBytes != nullptr);
            if (careTdo && tdoMask != nullptr && tdoMask->size() == nb) {
                careTdo = ((((*tdoMask)[byteIdx] >> bitShift) & 1u) != 0u);
            }
            uint8_t tdoBit = (tdoBytes != nullptr) ? static_cast<uint8_t>((tdoBytes[byteIdx] >> bitShift) & 1u) : 0u;
            cycles.push_back({ static_cast<uint8_t>(isLast ? 1u : 0u), tdiBit, tdoBit, careTdo });
        }

        if (exitOnLast) {
            curState = kNextTap[shiftState][1];
            StepTo(endState, false);
        } else {
            curState = shiftState;
        }
    }
};

inline void DrawGraticule(ParsedEdaBoard& out, float x0, float y0, float w, float h, int nx = 10, int ny = 8) {
    DetailEda100::AddRectOutline(out, x0 + w * 0.5f, y0 + h * 0.5f, w, h, EdaLayerId::EdgeCuts, 0.35f);
    for (int i = 1; i < nx; ++i) {
        float x = x0 + (w * i) / nx;
        out.traces.push_back({ x, y0, x, y0 + h, 0.08f, EdaLayerId::BottomSilk, 0 });
    }
    for (int j = 1; j < ny; ++j) {
        float y = y0 + (h * j) / ny;
        out.traces.push_back({ x0, y, x0 + w, y, 0.08f, EdaLayerId::BottomSilk, 0 });
    }
}

inline void RenderJtag4LaneWaveforms(
    ParsedEdaBoard& out,
    const std::vector<JtagBitCycle>& cycles,
    const std::wstring& summaryLabel)
{
    DrawGraticule(out, -40.0f, -20.0f, 80.0f, 40.0f, 16, 4);
    out.labels.push_back({ -46.5f,  14.0f, 1.15f, EdaLayerId::TopSilk, L"TCK" });
    out.labels.push_back({ -46.5f,   5.0f, 1.15f, EdaLayerId::TopSilk, L"TMS" });
    out.labels.push_back({ -46.5f,  -5.0f, 1.15f, EdaLayerId::TopSilk, L"TDI" });
    out.labels.push_back({ -46.5f, -14.0f, 1.15f, EdaLayerId::TopSilk, L"TDO" });
    if (!summaryLabel.empty()) {
        out.labels.push_back({ -39.0f, 21.6f, 1.05f, EdaLayerId::TopSilk, summaryLabel });
    }

    const int n = static_cast<int>(std::min<size_t>(cycles.size(), JtagTapTracker::kMaxVisualCycles));
    if (n <= 0) return;
    const float step = 76.0f / static_cast<float>(n);

    for (int i = 0; i < n; ++i) {
        const float x0 = -38.0f + static_cast<float>(i) * step;
        const float xm = x0 + step * 0.5f;
        const float x1 = x0 + step;

        // Lane 1: TCK clock pulse
        out.traces.push_back({ x0, 12.2f, x0, 15.8f, 0.22f, EdaLayerId::TopSilk, 1 });
        out.traces.push_back({ x0, 15.8f, xm, 15.8f, 0.22f, EdaLayerId::TopSilk, 1 });
        out.traces.push_back({ xm, 15.8f, xm, 12.2f, 0.22f, EdaLayerId::TopSilk, 1 });
        out.traces.push_back({ xm, 12.2f, x1, 12.2f, 0.22f, EdaLayerId::TopSilk, 1 });

        // Lane 2: TMS state control
        const float yTms = cycles[static_cast<size_t>(i)].tms ? 6.8f : 3.2f;
        if (i > 0) {
            const float yPrev = cycles[static_cast<size_t>(i - 1)].tms ? 6.8f : 3.2f;
            if (yPrev != yTms) out.traces.push_back({ x0, yPrev, x0, yTms, 0.28f, EdaLayerId::InnerCopper, 2 });
        }
        out.traces.push_back({ x0, yTms, x1, yTms, 0.28f, EdaLayerId::InnerCopper, 2 });

        // Lane 3: TDI scan-in data
        const float yTdi = cycles[static_cast<size_t>(i)].tdi ? -3.2f : -6.8f;
        if (i > 0) {
            const float yPrev = cycles[static_cast<size_t>(i - 1)].tdi ? -3.2f : -6.8f;
            if (yPrev != yTdi) out.traces.push_back({ x0, yPrev, x0, yTdi, 0.28f, EdaLayerId::TopCopper, 3 });
        }
        out.traces.push_back({ x0, yTdi, x1, yTdi, 0.28f, EdaLayerId::TopCopper, 3 });

        // Lane 4: TDO expected scan-out (high/low when care bit active, mid-rail high-Z when masked/unused)
        const auto& cyc = cycles[static_cast<size_t>(i)];
        const float yTdo = cyc.hasTdo ? (cyc.tdo ? -12.2f : -15.8f) : -14.0f;
        const float wTdo = cyc.hasTdo ? 0.28f : 0.14f;
        if (i > 0) {
            const auto& prevCyc = cycles[static_cast<size_t>(i - 1)];
            const float yPrev = prevCyc.hasTdo ? (prevCyc.tdo ? -12.2f : -15.8f) : -14.0f;
            if (yPrev != yTdo) out.traces.push_back({ x0, yPrev, x0, yTdo, 0.24f, EdaLayerId::BottomCopper, 4 });
        }
        out.traces.push_back({ x0, yTdo, x1, yTdo, wTdo, EdaLayerId::BottomCopper, 4 });
    }
    out.netCount = 4;
}

// Parse BGA pin coordinate like "A1", "B12", "AA14" into (row, col)
inline bool ParseBgaBallCoord(std::string_view pin, int& outRow, int& outCol) {
    while (!pin.empty() && (pin.front() == '"' || pin.front() == ' ' || pin.front() == '\t')) pin.remove_prefix(1);
    while (!pin.empty() && (pin.back() == '"' || pin.back() == ' ' || pin.back() == '\r')) pin.remove_suffix(1);
    if (pin.rfind("PIN_", 0) == 0) pin.remove_prefix(4);
    if (pin.size() < 2 || pin.size() > 5) return false;

    static const char* kJedecRows = "ABCDEFGHJKLMNPRTUVWYZ";
    int r = 0, letters = 0;
    size_t i = 0;
    while (i < pin.size() && ((pin[i] >= 'A' && pin[i] <= 'Z') || (pin[i] >= 'a' && pin[i] <= 'z'))) {
        char up = static_cast<char>(toupper(static_cast<unsigned char>(pin[i++])));
        const char* f = std::strchr(kJedecRows, up);
        int idx = f ? static_cast<int>(f - kJedecRows) : (up - 'A');
        r = r * 20 + idx + 1;
        ++letters;
    }
    if (letters == 0 || i >= pin.size()) return false;
    int c = 0;
    while (i < pin.size() && pin[i] >= '0' && pin[i] <= '9') {
        c = c * 10 + (pin[i++] - '0');
    }
    if (c <= 0 || c > 64 || r <= 0 || r > 64) return false;
    outRow = r - 1;
    outCol = c - 1;
    return true;
}

} // namespace DetailEda200

// ============================================================================
// #61, #62, #63. KiCad Symbol (.kicad_sym), Specctra Route (.ses), Pick & Place (.pos/.cpl/.mnt)
// ============================================================================
namespace DetailKiCadSymSesPos {

inline std::string LowerAsciiSymSes(std::string_view sv) { return ::Explorer2::PathUtils::ToLowerAscii(sv); }

inline void TessellateKiCad3PtArc(
    float x1, float y1, float xm, float ym, float x2, float y2,
    std::vector<std::pair<float, float>>& outPts)
{
    outPts.clear();
    double d = 2.0 * (static_cast<double>(x1) * (ym - y2) +
                      static_cast<double>(xm) * (y2 - y1) +
                      static_cast<double>(x2) * (y1 - ym));
    if (std::fabs(d) < 1e-7) {
        outPts.push_back({ x1, y1 });
        outPts.push_back({ x2, y2 });
        return;
    }
    double s1 = static_cast<double>(x1) * x1 + static_cast<double>(y1) * y1;
    double sm = static_cast<double>(xm) * xm + static_cast<double>(ym) * ym;
    double s2 = static_cast<double>(x2) * x2 + static_cast<double>(y2) * y2;
    double cx = (s1 * (ym - y2) + sm * (y2 - y1) + s2 * (y1 - ym)) / d;
    double cy = (s1 * (x2 - xm) + sm * (x1 - x2) + s2 * (xm - x1)) / d;
    double r  = std::hypot(x1 - cx, y1 - cy);
    if (r < 1e-4 || r > 1e5) {
        outPts.push_back({ x1, y1 });
        outPts.push_back({ x2, y2 });
        return;
    }

    const double twoPi = 6.2831853071795864769;
    double a1 = std::atan2(y1 - cy, x1 - cx);
    double am = std::atan2(ym - cy, xm - cx);
    double a2 = std::atan2(y2 - cy, x2 - cx);

    auto normCcw = [&](double a) {
        double da = std::fmod(a - a1, twoPi);
        if (da < 0.0) da += twoPi;
        return da;
    };
    double dm = normCcw(am);
    double d2 = normCcw(a2);
    double sweep = (dm <= d2) ? d2 : (d2 - twoPi);

    int segs = std::clamp(static_cast<int>(std::ceil(std::fabs(sweep) / (twoPi / 24.0))), 4, 32);
    for (int i = 0; i <= segs; ++i) {
        double ang = a1 + sweep * (static_cast<double>(i) / static_cast<double>(segs));
        outPts.push_back({ static_cast<float>(cx + r * std::cos(ang)),
                           static_cast<float>(cy + r * std::sin(ang)) });
    }
}

struct KiSymPin {
    float x = 0.0f, y = 0.0f, angleDeg = 0.0f, length = 2.54f;
    std::string shape; // line, inverted, clock, inverted_clock, input_low, output_low, non_logic
    std::string name;
    std::string number;
    int unit = 0, style = 0;
    bool hidden = false;
};

struct KiSymSeg {
    float x1 = 0, y1 = 0, x2 = 0, y2 = 0, w = 0.25f;
    int unit = 0, style = 0;
};

struct KiSymCirc {
    float cx = 0, cy = 0, r = 1.0f, w = 0.25f;
    int unit = 0, style = 0;
};

struct KiSymTxt {
    float x = 0, y = 0;
    std::string text;
    int unit = 0, style = 0;
};

struct KiSymEntry {
    std::string name;
    std::string extendsName;
    std::string refPrefix = "U";
    std::string valueStr;
    std::string footprintStr;
    bool hidePinNames = false;
    bool hidePinNums  = false;
    std::vector<KiSymSeg> segs;
    std::vector<KiSymCirc> circs;
    std::vector<KiSymPin> pins;
    std::vector<KiSymTxt> texts;
};

inline void ParseKiSymUnitSuffix(std::string_view subName, int& outUnit, int& outStyle) {
    outUnit = 0;
    outStyle = 0;
    size_t u2 = subName.rfind('_');
    if (u2 == std::string_view::npos || u2 == 0 || u2 + 1 >= subName.size()) return;
    size_t u1 = subName.rfind('_', u2 - 1);
    if (u1 == std::string_view::npos || u1 + 1 >= u2) return;
    bool okDigits = true;
    for (size_t i = u1 + 1; i < u2; ++i) if (!std::isdigit(static_cast<unsigned char>(subName[i]))) okDigits = false;
    for (size_t i = u2 + 1; i < subName.size(); ++i) if (!std::isdigit(static_cast<unsigned char>(subName[i]))) okDigits = false;
    if (!okDigits) return;
    outUnit  = std::atoi(std::string(subName.substr(u1 + 1, u2 - u1 - 1)).c_str());
    outStyle = std::atoi(std::string(subName.substr(u2 + 1)).c_str());
}

inline float ExtractStrokeWidthMm(const DetailSpecctra::SNode& primNode, float defW = 0.254f) {
    for (size_t i = 1; i < primNode.list.size(); ++i) {
        const auto& c = primNode.list[i];
        std::string_view k = DetailSpecctra::HeadKey(c);
        if (k == "stroke") {
            for (size_t j = 1; j < c.list.size(); ++j) {
                if (DetailSpecctra::HeadKey(c.list[j]) == "width" && c.list[j].list.size() >= 2) {
                    double v = 0.0;
                    if (DetailSpecctra::AtomFloat(c.list[j].list[1], v) && v > 0.0) {
                        return std::clamp(static_cast<float>(v), 0.14f, 1.5f);
                    }
                }
            }
        } else if (k == "width" && c.list.size() >= 2) {
            double v = 0.0;
            if (DetailSpecctra::AtomFloat(c.list[1], v) && v > 0.0) {
                return std::clamp(static_cast<float>(v), 0.14f, 1.5f);
            }
        }
    }
    return defW;
}

inline bool ExtractPairXY(const DetailSpecctra::SNode& parent, std::string_view key, float& outX, float& outY, float* outThird = nullptr) {
    for (size_t i = 1; i < parent.list.size(); ++i) {
        const auto& c = parent.list[i];
        if (DetailSpecctra::HeadKey(c) == key && c.list.size() >= 3) {
            double vx = 0.0, vy = 0.0;
            if (DetailSpecctra::AtomFloat(c.list[1], vx) && DetailSpecctra::AtomFloat(c.list[2], vy)) {
                outX = static_cast<float>(vx);
                outY = static_cast<float>(vy);
                if (outThird && c.list.size() >= 4) {
                    double vz = 0.0;
                    if (DetailSpecctra::AtomFloat(c.list[3], vz)) *outThird = static_cast<float>(vz);
                }
                return true;
            }
        }
    }
    return false;
}

inline void ParseKiSymPrimitives(const DetailSpecctra::SNode& container, int unit, int style, KiSymEntry& dst) {
    for (size_t i = 1; i < container.list.size(); ++i) {
        const auto& ch = container.list[i];
        std::string_view k = DetailSpecctra::HeadKey(ch);
        if (k == "symbol" && ch.list.size() >= 2 && !ch.list[1].isList) {
            int subU = unit, subS = style;
            ParseKiSymUnitSuffix(ch.list[1].atom, subU, subS);
            ParseKiSymPrimitives(ch, subU, subS, dst);
        } else if (k == "extends" && ch.list.size() >= 2 && !ch.list[1].isList) {
            dst.extendsName = ch.list[1].atom;
        } else if (k == "pin_names") {
            for (size_t j = 1; j < ch.list.size(); ++j) {
                if (!ch.list[j].isList && ch.list[j].atom == "hide") dst.hidePinNames = true;
                else if (DetailSpecctra::HeadKey(ch.list[j]) == "hide" &&
                         (ch.list[j].list.size() < 2 || ch.list[j].list[1].atom != "no")) dst.hidePinNames = true;
            }
        } else if (k == "pin_numbers") {
            for (size_t j = 1; j < ch.list.size(); ++j) {
                if (!ch.list[j].isList && ch.list[j].atom == "hide") dst.hidePinNums = true;
                else if (DetailSpecctra::HeadKey(ch.list[j]) == "hide" &&
                         (ch.list[j].list.size() < 2 || ch.list[j].list[1].atom != "no")) dst.hidePinNums = true;
            }
        } else if (k == "property" && ch.list.size() >= 3 && !ch.list[1].isList && !ch.list[2].isList) {
            const std::string& pk = ch.list[1].atom;
            const std::string& pv = ch.list[2].atom;
            if (pk == "Reference" && !pv.empty()) dst.refPrefix = pv;
            else if (pk == "Value" && !pv.empty()) dst.valueStr = pv;
            else if (pk == "Footprint" && !pv.empty()) dst.footprintStr = pv;
        } else if (k == "rectangle") {
            float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            if (ExtractPairXY(ch, "start", x1, y1) && ExtractPairXY(ch, "end", x2, y2)) {
                float w = ExtractStrokeWidthMm(ch, 0.254f);
                dst.segs.push_back({ x1, y1, x2, y1, w, unit, style });
                dst.segs.push_back({ x2, y1, x2, y2, w, unit, style });
                dst.segs.push_back({ x2, y2, x1, y2, w, unit, style });
                dst.segs.push_back({ x1, y2, x1, y1, w, unit, style });
            }
        } else if (k == "polyline" || k == "bezier") {
            float w = ExtractStrokeWidthMm(ch, 0.254f);
            std::vector<std::pair<float, float>> pts;
            for (size_t j = 1; j < ch.list.size(); ++j) {
                if (DetailSpecctra::HeadKey(ch.list[j]) == "pts") {
                    const auto& ptsNode = ch.list[j];
                    for (size_t m = 1; m < ptsNode.list.size(); ++m) {
                        const auto& xy = ptsNode.list[m];
                        if (DetailSpecctra::HeadKey(xy) == "xy" && xy.list.size() >= 3) {
                            double vx = 0.0, vy = 0.0;
                            if (DetailSpecctra::AtomFloat(xy.list[1], vx) && DetailSpecctra::AtomFloat(xy.list[2], vy)) {
                                pts.push_back({ static_cast<float>(vx), static_cast<float>(vy) });
                            }
                        }
                    }
                }
            }
            if (k == "bezier" && pts.size() == 4) {
                std::pair<float, float> prev = pts[0];
                for (int s = 1; s <= 12; ++s) {
                    float t = static_cast<float>(s) / 12.0f;
                    float u = 1.0f - t;
                    float bx = u*u*u*pts[0].first + 3*u*u*t*pts[1].first + 3*u*t*t*pts[2].first + t*t*t*pts[3].first;
                    float by = u*u*u*pts[0].second + 3*u*u*t*pts[1].second + 3*u*t*t*pts[2].second + t*t*t*pts[3].second;
                    dst.segs.push_back({ prev.first, prev.second, bx, by, w, unit, style });
                    prev = { bx, by };
                }
            } else {
                for (size_t s = 1; s < pts.size(); ++s) {
                    dst.segs.push_back({ pts[s - 1].first, pts[s - 1].second, pts[s].first, pts[s].second, w, unit, style });
                }
            }
        } else if (k == "circle") {
            float cx = 0, cy = 0;
            if (ExtractPairXY(ch, "center", cx, cy)) {
                float r = 1.0f;
                for (size_t j = 1; j < ch.list.size(); ++j) {
                    if (DetailSpecctra::HeadKey(ch.list[j]) == "radius" && ch.list[j].list.size() >= 2) {
                        double rv = 0.0;
                        if (DetailSpecctra::AtomFloat(ch.list[j].list[1], rv) && rv > 0.02) r = static_cast<float>(rv);
                    }
                }
                dst.circs.push_back({ cx, cy, r, ExtractStrokeWidthMm(ch, 0.254f), unit, style });
            }
        } else if (k == "arc") {
            float x1 = 0, y1 = 0, xm = 0, ym = 0, x2 = 0, y2 = 0;
            if (ExtractPairXY(ch, "start", x1, y1) && ExtractPairXY(ch, "mid", xm, ym) && ExtractPairXY(ch, "end", x2, y2)) {
                float w = ExtractStrokeWidthMm(ch, 0.254f);
                std::vector<std::pair<float, float>> arcPts;
                TessellateKiCad3PtArc(x1, y1, xm, ym, x2, y2, arcPts);
                for (size_t s = 1; s < arcPts.size(); ++s) {
                    dst.segs.push_back({ arcPts[s - 1].first, arcPts[s - 1].second, arcPts[s].first, arcPts[s].second, w, unit, style });
                }
            }
        } else if (k == "text" && ch.list.size() >= 2 && !ch.list[1].isList) {
            float tx = 0, ty = 0;
            if (!ch.list[1].atom.empty() && ExtractPairXY(ch, "at", tx, ty)) {
                dst.texts.push_back({ tx, ty, ch.list[1].atom, unit, style });
            }
        } else if (k == "pin" && ch.list.size() >= 4) {
            KiSymPin pin;
            pin.unit = unit;
            pin.style = style;
            if (ch.list.size() >= 3 && !ch.list[2].isList) pin.shape = ch.list[2].atom;
            ExtractPairXY(ch, "at", pin.x, pin.y, &pin.angleDeg);
            for (size_t j = 1; j < ch.list.size(); ++j) {
                const auto& sub = ch.list[j];
                if (!sub.isList && sub.atom == "hide") pin.hidden = true;
                std::string_view sk = DetailSpecctra::HeadKey(sub);
                if (sk == "hide" && (sub.list.size() < 2 || sub.list[1].atom != "no")) pin.hidden = true;
                else if (sk == "length" && sub.list.size() >= 2) {
                    double lv = 0.0;
                    if (DetailSpecctra::AtomFloat(sub.list[1], lv)) pin.length = std::max(0.0f, static_cast<float>(lv));
                } else if (sk == "name" && sub.list.size() >= 2 && !sub.list[1].isList) {
                    pin.name = sub.list[1].atom;
                } else if (sk == "number" && sub.list.size() >= 2 && !sub.list[1].isList) {
                    pin.number = sub.list[1].atom;
                }
            }
            dst.pins.push_back(std::move(pin));
        }
    }
}

// Split a CSV or whitespace Pick-and-Place line while honoring double-quoted strings containing commas/spaces
inline std::vector<std::string> SplitPickAndPlaceRow(std::string_view ln, bool csvDelimited) {
    std::vector<std::string> out;
    if (csvDelimited) {
        char sep = (ln.find(',') != std::string_view::npos) ? ',' : ((ln.find(';') != std::string_view::npos) ? ';' : '\t');
        size_t i = 0;
        while (i <= ln.size()) {
            while (i < ln.size() && (ln[i] == ' ' || (sep != '\t' && ln[i] == '\t'))) ++i;
            std::string cell;
            if (i < ln.size() && ln[i] == '"') {
                ++i;
                while (i < ln.size()) {
                    if (ln[i] == '"') {
                        if (i + 1 < ln.size() && ln[i + 1] == '"') { cell.push_back('"'); i += 2; }
                        else { ++i; break; }
                    } else {
                        cell.push_back(ln[i++]);
                    }
                }
                while (i < ln.size() && ln[i] != sep) ++i;
            } else {
                size_t e = ln.find(sep, i);
                if (e == std::string_view::npos) e = ln.size();
                cell.assign(ln.data() + i, e - i);
                i = e;
            }
            while (!cell.empty() && static_cast<unsigned char>(cell.back()) <= 0x20u) cell.pop_back();
            while (!cell.empty() && static_cast<unsigned char>(cell.front()) <= 0x20u) cell.erase(cell.begin());
            out.push_back(std::move(cell));
            if (i >= ln.size()) break;
            ++i; // consume separator
        }
        return out;
    }

    size_t i = 0;
    while (i < ln.size()) {
        while (i < ln.size() && static_cast<unsigned char>(ln[i]) <= 0x20u) ++i;
        if (i >= ln.size()) break;
        std::string tok;
        if (ln[i] == '"') {
            ++i;
            while (i < ln.size() && ln[i] != '"') tok.push_back(ln[i++]);
            if (i < ln.size()) ++i;
        } else {
            while (i < ln.size() && static_cast<unsigned char>(ln[i]) > 0x20u) tok.push_back(ln[i++]);
        }
        out.push_back(std::move(tok));
    }
    return out;
}

inline bool ParsePnpCoordTokenMm(std::string_view raw, float defaultUnitToMm, float& outMm) {
    std::string s(raw);
    while (!s.empty() && (static_cast<unsigned char>(s.front()) <= 0x20u || s.front() == '"')) s.erase(s.begin());
    while (!s.empty() && (static_cast<unsigned char>(s.back()) <= 0x20u || s.back() == '"')) s.pop_back();
    if (s.empty()) return false;

    char* endp = nullptr;
    double val = std::strtod(s.c_str(), &endp);
    if (!endp || endp == s.c_str() || !std::isfinite(val)) return false;

    std::string unit = LowerAsciiSymSes(endp);
    while (!unit.empty() && static_cast<unsigned char>(unit.front()) <= 0x20u) unit.erase(unit.begin());

    double scale = defaultUnitToMm;
    if (unit.empty()) {
        scale = defaultUnitToMm;
    } else if (unit == "mm") {
        scale = 1.0;
    } else if (unit == "mil" || unit == "mils" || unit == "th") {
        scale = 0.0254;
    } else if (unit == "in" || unit == "inch" || unit == "inches") {
        scale = 25.4;
    } else if (unit == "um") {
        scale = 0.001;
    } else {
        return false; // Reject non-coordinate tokens like "100nF" or "10k"!
    }

    outMm = static_cast<float>(val * scale);
    return std::fabs(outMm) < 50000.0f;
}

inline bool LooksLikeValidRefDes(const std::string& ref) {
    if (ref.empty() || ref.size() > 24) return false;
    if (!std::isalpha(static_cast<unsigned char>(ref[0])) && ref[0] != '#') return false;
    bool hasDigitOrShort = false;
    for (size_t i = 0; i < ref.size(); ++i) {
        unsigned char uc = static_cast<unsigned char>(ref[i]);
        if (std::isdigit(uc) || ref[i] == '?') hasDigitOrShort = true;
        else if (!std::isalnum(uc) && ref[i] != '_' && ref[i] != '-' && ref[i] != '#') return false;
    }
    return hasDigitOrShort || ref.size() <= 4;
}

inline int ExtractTrailingPinCount(const std::string& pkgUpper, int defPins) {
    size_t xPos = pkgUpper.find('X');
    if (xPos != std::string::npos && xPos > 0 && xPos + 1 < pkgUpper.size() &&
        std::isdigit(static_cast<unsigned char>(pkgUpper[xPos - 1])) &&
        std::isdigit(static_cast<unsigned char>(pkgUpper[xPos + 1])))
    {
        size_t s = xPos - 1;
        while (s > 0 && std::isdigit(static_cast<unsigned char>(pkgUpper[s - 1]))) --s;
        int r = std::atoi(pkgUpper.c_str() + s);
        int c = std::atoi(pkgUpper.c_str() + xPos + 1);
        if (r >= 1 && r <= 8 && c >= 1 && c <= 64) return r * c;
    }
    for (size_t i = 0; i < pkgUpper.size(); ++i) {
        if (std::isdigit(static_cast<unsigned char>(pkgUpper[i]))) {
            size_t j = i;
            while (j < pkgUpper.size() && std::isdigit(static_cast<unsigned char>(pkgUpper[j]))) ++j;
            int n = std::atoi(pkgUpper.substr(i, j - i).c_str());
            if (n >= 3 && n <= 256 && n != 402 && n != 603 && n != 805 && n != 1206 && n != 2512 && n != 23 && n != 223) {
                return n;
            }
            i = j;
        }
    }
    return defPins;
}

} // namespace DetailKiCadSymSesPos

inline bool ParseKiCadSymSesPos(std::string_view sv, const std::wstring& ext, ParsedEdaBoard& out) {
    if (sv.size() >= 3 &&
        static_cast<uint8_t>(sv[0]) == 0xEFu &&
        static_cast<uint8_t>(sv[1]) == 0xBBu &&
        static_cast<uint8_t>(sv[2]) == 0xBFu)
    {
        sv.remove_prefix(3);
    }

    // #61. KiCad v6–v9 Schematic Symbol Library (.kicad_sym)
    if (ext == L".kicad_sym" || sv.find("(kicad_symbol_lib") != std::string_view::npos) {
        using namespace DetailKiCadSymSesPos;
        if (sv.find("(kicad_symbol_lib") == std::string_view::npos && sv.find("(symbol ") == std::string_view::npos) {
            return false;
        }

        std::vector<DetailSpecctra::SNode> roots;
        if (!DetailSpecctra::ParseSExprTree(sv, roots) || roots.empty()) return false;

        std::vector<KiSymEntry> syms;
        for (const auto& r : roots) {
            std::string_view hk = DetailSpecctra::HeadKey(r);
            if (hk == "kicad_symbol_lib") {
                for (size_t i = 1; i < r.list.size() && syms.size() < 128; ++i) {
                    const auto& sn = r.list[i];
                    if (DetailSpecctra::HeadKey(sn) == "symbol" && sn.list.size() >= 2 && !sn.list[1].isList) {
                        KiSymEntry ent;
                        ent.name = sn.list[1].atom;
                        ParseKiSymPrimitives(sn, 0, 0, ent);
                        if (!ent.name.empty()) syms.push_back(std::move(ent));
                    }
                }
            } else if (hk == "symbol" && r.list.size() >= 2 && !r.list[1].isList && syms.size() < 128) {
                KiSymEntry ent;
                ent.name = r.list[1].atom;
                ParseKiSymPrimitives(r, 0, 0, ent);
                if (!ent.name.empty()) syms.push_back(std::move(ent));
            }
        }
        if (syms.empty()) return false;

        // Resolve (extends "ParentSymbol") inheritance across the symbol library
        std::unordered_map<std::string, size_t> symIdxMap;
        for (size_t i = 0; i < syms.size(); ++i) {
            symIdxMap[syms[i].name] = i;
            size_t col = syms[i].name.find(':');
            if (col != std::string::npos) symIdxMap[syms[i].name.substr(col + 1)] = i;
        }
        for (int pass = 0; pass < 3; ++pass) {
            for (auto& s : syms) {
                if (s.extendsName.empty()) continue;
                auto it = symIdxMap.find(s.extendsName);
                if (it == symIdxMap.end()) {
                    size_t col = s.extendsName.find(':');
                    if (col != std::string::npos) it = symIdxMap.find(s.extendsName.substr(col + 1));
                }
                if (it == symIdxMap.end() || it->second >= syms.size()) continue;
                const KiSymEntry& parent = syms[it->second];
                if (!parent.extendsName.empty() && pass < 2) continue;

                if (s.segs.empty() && s.circs.empty()) {
                    s.segs = parent.segs;
                    s.circs = parent.circs;
                }
                if (s.pins.empty()) s.pins = parent.pins;
                if (s.texts.empty()) s.texts = parent.texts;
                if (s.footprintStr.empty()) s.footprintStr = parent.footprintStr;
                s.hidePinNames = parent.hidePinNames;
                s.hidePinNums  = parent.hidePinNums;
                s.extendsName.clear();
            }
        }

        out.kind = EdaDocKind::KiCadSch;

        const size_t cols = std::max<size_t>(1u, static_cast<size_t>(std::ceil(std::sqrt(static_cast<double>(syms.size())))));
        float cellX = 0.0f, cellY = 0.0f, rowMaxH = 18.0f;
        size_t colIdx = 0;
        size_t renderedSyms = 0;
        size_t totalPins = 0;

        for (size_t si = 0; si < syms.size(); ++si) {
            const auto& sym = syms[si];
            int targetUnit = 1;
            bool hasU01 = false;
            int minPresentU = 999;
            for (const auto& g : sym.segs)  { if (g.unit <= 1) hasU01 = true; if (g.unit > 0) minPresentU = std::min(minPresentU, g.unit); }
            for (const auto& c : sym.circs) { if (c.unit <= 1) hasU01 = true; if (c.unit > 0) minPresentU = std::min(minPresentU, c.unit); }
            for (const auto& p : sym.pins)  { if (p.unit <= 1) hasU01 = true; if (p.unit > 0) minPresentU = std::min(minPresentU, p.unit); }
            if (!hasU01 && minPresentU < 999) targetUnit = minPresentU;

            auto matchUS = [&](int u, int st) -> bool {
                return (u == 0 || u == targetUnit) && (st == 0 || st == 1);
            };

            float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
            bool hasGeom = false;
            auto expB = [&](float x, float y) {
                minX = std::min(minX, x); maxX = std::max(maxX, x);
                minY = std::min(minY, y); maxY = std::max(maxY, y);
                hasGeom = true;
            };
            for (const auto& g : sym.segs)  if (matchUS(g.unit, g.style)) { expB(g.x1, g.y1); expB(g.x2, g.y2); }
            for (const auto& c : sym.circs) if (matchUS(c.unit, c.style)) { expB(c.cx - c.r, c.cy - c.r); expB(c.cx + c.r, c.cy + c.r); }
            for (const auto& p : sym.pins) {
                if (!matchUS(p.unit, p.style) || p.hidden) continue;
                expB(p.x, p.y);
                float rad = p.angleDeg * 3.14159265f / 180.0f;
                expB(p.x + p.length * std::cos(rad), p.y + p.length * std::sin(rad));
            }
            if (!hasGeom) continue;

            float symW = std::max(4.0f, maxX - minX);
            float symH = std::max(4.0f, maxY - minY);
            if (colIdx >= cols) {
                colIdx = 0;
                cellX = 0.0f;
                cellY -= (rowMaxH + 14.0f);
                rowMaxH = 18.0f;
            }
            float offX = cellX - minX;
            float offY = cellY - (minY + maxY) * 0.5f;

            for (const auto& g : sym.segs) {
                if (!matchUS(g.unit, g.style) || out.traces.size() >= 80000) continue;
                out.traces.push_back({ offX + g.x1, offY + g.y1, offX + g.x2, offY + g.y2, g.w, EdaLayerId::SchSymbol, 0 });
            }
            for (const auto& c : sym.circs) {
                if (!matchUS(c.unit, c.style)) continue;
                out.circles.push_back({ offX + c.cx, offY + c.cy, c.r, c.w, false, EdaLayerId::SchSymbol });
            }
            for (const auto& t : sym.texts) {
                if (!matchUS(t.unit, t.style) || out.labels.size() >= 4096) continue;
                out.labels.push_back({ offX + t.x, offY + t.y, 0.85f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(t.text) });
            }

            int symPins = 0;
            for (const auto& p : sym.pins) {
                if (!matchUS(p.unit, p.style) || p.hidden) continue;
                float tx = offX + p.x;
                float ty = offY + p.y;
                float rad = p.angleDeg * 3.14159265f / 180.0f;
                float dx = std::cos(rad), dy = std::sin(rad);
                float nx = -dy, ny = dx;
                float bx = tx + p.length * dx;
                float by = ty + p.length * dy;

                const bool isInv = (p.shape == "inverted" || p.shape == "inverted_clock");
                const bool isClk = (p.shape == "clock" || p.shape == "inverted_clock" || p.shape == "edge_clock_high");
                float stemEndLen = isInv ? std::max(0.0f, p.length - 1.25f) : p.length;
                if (stemEndLen > 0.02f) {
                    out.traces.push_back({ tx, ty, tx + stemEndLen * dx, ty + stemEndLen * dy, 0.24f, EdaLayerId::SchWire, 1 });
                }
                if (isInv) {
                    out.circles.push_back({ bx - 0.62f * dx, by - 0.62f * dy, 0.60f, 0.22f, false, EdaLayerId::SchSymbol });
                }
                if (isClk) {
                    out.traces.push_back({ bx + 0.75f * nx, by + 0.75f * ny, bx + 1.35f * dx, by + 1.35f * dy, 0.20f, EdaLayerId::SchSymbol, 0 });
                    out.traces.push_back({ bx - 0.75f * nx, by - 0.75f * ny, bx + 1.35f * dx, by + 1.35f * dy, 0.20f, EdaLayerId::SchSymbol, 0 });
                }
                if (p.shape == "non_logic") {
                    out.traces.push_back({ tx - 0.5f, ty - 0.5f, tx + 0.5f, ty + 0.5f, 0.20f, EdaLayerId::SchSymbol, 0 });
                    out.traces.push_back({ tx - 0.5f, ty + 0.5f, tx + 0.5f, ty - 0.5f, 0.20f, EdaLayerId::SchSymbol, 0 });
                }

                EdaPadPrim pad{};
                pad.x = tx; pad.y = ty;
                pad.w = 0.58f; pad.h = 0.58f;
                pad.shape = 0;
                pad.layer = EdaLayerId::SchSymbol;
                pad.pinName = PathUtils::Utf8ToWide(p.number.empty() ? p.name : p.number);
                pad.netName = PathUtils::Utf8ToWide(p.name);
                out.pads.push_back(std::move(pad));

                if (!sym.hidePinNames && !p.name.empty() && p.name != "~" && out.labels.size() < 4096) {
                    out.labels.push_back({ bx + 0.65f * dx - 0.4f, by + 0.65f * dy - 0.25f, 0.80f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(p.name) });
                }
                if (!sym.hidePinNums && !p.number.empty() && p.number != "~" && out.labels.size() < 4096) {
                    float mx = (tx + bx) * 0.5f + 0.55f * nx;
                    float my = (ty + by) * 0.5f + 0.55f * ny;
                    out.labels.push_back({ mx, my, 0.72f, EdaLayerId::TopSilk, PathUtils::Utf8ToWide(p.number) });
                }
                ++symPins;
                ++totalPins;
            }

            std::string dispTitle = sym.name;
            if (!sym.valueStr.empty() && sym.valueStr != sym.name) dispTitle += " (" + sym.valueStr + ")";
            out.labels.push_back({ offX + minX, offY + maxY + 1.8f, 1.05f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(dispTitle) });

            EdaComponent3D comp;
            comp.refDes = PathUtils::Utf8ToWide(sym.name);
            comp.value = PathUtils::Utf8ToWide(sym.valueStr.empty() ? sym.refPrefix : sym.valueStr);
            comp.footprint = PathUtils::Utf8ToWide(sym.footprintStr.empty() ? "KICAD_SYM" : sym.footprintStr);
            comp.x = offX + (minX + maxX) * 0.5f;
            comp.y = offY + (minY + maxY) * 0.5f;
            comp.widthMm = symW;
            comp.lengthMm = symH;
            comp.heightMm = 1.2f;
            comp.pinCount = std::max(1, symPins);
            comp.category = EdaPartCategory::IC_Package;
            out.components.push_back(std::move(comp));

            cellX += symW + 14.0f;
            rowMaxH = std::max(rowMaxH, symH);
            ++colIdx;
            ++renderedSyms;
        }

        if (renderedSyms == 0 || (out.traces.empty() && out.circles.empty() && out.pads.empty())) {
            out = ParsedEdaBoard{};
            return false;
        }
        out.formatTitle = L"KiCad Schematic Symbol Library (.kicad_sym — " +
                          std::to_wstring(renderedSyms) + L" symbol" + (renderedSyms == 1 ? L"" : L"s") +
                          L", " + std::to_wstring(totalPins) + L" pins)";
        out.netCount = std::max<size_t>(1u, totalPins > 0 ? totalPins : renderedSyms);
        return true;
    }

    // #62. Specctra Autorouter Routed Session (.ses)
    if (ext == L".ses") {
        using namespace DetailKiCadSymSesPos;
        if (sv.find("(session") == std::string_view::npos && sv.find("(routes") == std::string_view::npos) {
            return false;
        }
        std::vector<DetailSpecctra::SNode> roots;
        if (!DetailSpecctra::ParseSExprTree(sv, roots) || roots.empty()) {
            return false;
        }

        const DetailSpecctra::SNode* sesRoot = nullptr;
        for (const auto& r : roots) {
            std::string hk = LowerAsciiSymSes(DetailSpecctra::HeadKey(r));
            if (hk == "session" || hk == "routes") { sesRoot = &r; break; }
        }
        if (!sesRoot) return false;

        out.kind = EdaDocKind::KiCadPcb;
        std::string designName = (sesRoot->list.size() >= 2 && !sesRoot->list[1].isList) ? sesRoot->list[1].atom : "";

        auto parseResNodeScale = [](const DetailSpecctra::SNode& secNode, double fallbackScale) -> double {
            for (size_t i = 1; i < secNode.list.size(); ++i) {
                const auto& ch = secNode.list[i];
                if (LowerAsciiSymSes(DetailSpecctra::HeadKey(ch)) == "resolution" && ch.list.size() >= 3) {
                    double div = 1.0;
                    if (!ch.list[1].isList && DetailSpecctra::AtomFloat(ch.list[2], div) && div > 0.0) {
                        return DetailSpecctra::UnitNameToMm(ch.list[1].atom) / div;
                    }
                }
            }
            return fallbackScale;
        };

        auto mapSesLayer = [](std::string_view lyrRaw) -> EdaLayerId {
            std::string low = LowerAsciiSymSes(lyrRaw);
            if (low.find("b.") != std::string::npos || low.find("bot") != std::string::npos ||
                low.find("back") != std::string::npos || low.find("solder") != std::string::npos) {
                return EdaLayerId::BottomCopper;
            }
            if (low.find("in") == 0 || low.find("inner") != std::string::npos || low.find("mid") != std::string::npos ||
                low.find("gnd") != std::string::npos || low.find("pwr") != std::string::npos) {
                return EdaLayerId::InnerCopper;
            }
            if (low == "2" || low == "16" || low == "32") {
                return EdaLayerId::BottomCopper;
            }
            return EdaLayerId::TopCopper;
        };

        double rootScaleMm = parseResNodeScale(*sesRoot, 0.001);

        struct SesViaPad { float diamMm = 0.8f; float drillMm = 0.4f; };
        std::unordered_map<std::string, SesViaPad> viaPadstacks;
        std::set<std::string> routedNets;

        auto parseRoutesNode = [&](const DetailSpecctra::SNode& routesNode) {
            double rScale = parseResNodeScale(routesNode, rootScaleMm);

            for (size_t i = 1; i < routesNode.list.size(); ++i) {
                const auto& sub = routesNode.list[i];
                if (LowerAsciiSymSes(DetailSpecctra::HeadKey(sub)) != "library_out") continue;
                for (size_t j = 1; j < sub.list.size(); ++j) {
                    const auto& ps = sub.list[j];
                    if (LowerAsciiSymSes(DetailSpecctra::HeadKey(ps)) != "padstack" || ps.list.size() < 2) continue;
                    std::string psName = ps.list[1].atom;
                    float maxD = 0.0f;
                    for (size_t k = 2; k < ps.list.size(); ++k) {
                        const auto& sh = ps.list[k];
                        if (LowerAsciiSymSes(DetailSpecctra::HeadKey(sh)) != "shape" || sh.list.size() < 2) continue;
                        const auto& geom = sh.list[1];
                        std::string gk = LowerAsciiSymSes(DetailSpecctra::HeadKey(geom));
                        if (gk == "circle" && geom.list.size() >= 3) {
                            double d = 0.0;
                            if (DetailSpecctra::AtomFloat(geom.list[2], d)) maxD = std::max(maxD, static_cast<float>(d * rScale));
                        } else if (gk == "rect" && geom.list.size() >= 6) {
                            double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
                            if (DetailSpecctra::AtomFloat(geom.list[2], x1) && DetailSpecctra::AtomFloat(geom.list[3], y1) &&
                                DetailSpecctra::AtomFloat(geom.list[4], x2) && DetailSpecctra::AtomFloat(geom.list[5], y2)) {
                                maxD = std::max(maxD, static_cast<float>(std::max(std::fabs(x2 - x1), std::fabs(y2 - y1)) * rScale));
                            }
                        }
                    }
                    if (!psName.empty() && maxD > 0.1f) {
                        viaPadstacks[psName] = { std::clamp(maxD, 0.25f, 6.0f), std::clamp(maxD * 0.5f, 0.15f, 3.0f) };
                    }
                }
            }

            for (size_t i = 1; i < routesNode.list.size(); ++i) {
                const auto& sub = routesNode.list[i];
                if (LowerAsciiSymSes(DetailSpecctra::HeadKey(sub)) != "network_out") continue;
                for (size_t j = 1; j < sub.list.size(); ++j) {
                    const auto& netN = sub.list[j];
                    if (LowerAsciiSymSes(DetailSpecctra::HeadKey(netN)) != "net" || netN.list.size() < 2) continue;
                    std::string netName = netN.list[1].atom;
                    routedNets.insert(netName);
                    int netId = static_cast<int>(routedNets.size());

                    for (size_t k = 2; k < netN.list.size(); ++k) {
                        const auto& item = netN.list[k];
                        std::string ik = LowerAsciiSymSes(DetailSpecctra::HeadKey(item));
                        if (ik == "via" && item.list.size() >= 4 && out.vias.size() < 25000) {
                            std::string psName = item.list[1].atom;
                            double vx = 0.0, vy = 0.0;
                            if (DetailSpecctra::AtomFloat(item.list[2], vx) && DetailSpecctra::AtomFloat(item.list[3], vy)) {
                                SesViaPad vp{};
                                auto itVp = viaPadstacks.find(psName);
                                if (itVp != viaPadstacks.end()) vp = itVp->second;
                                out.vias.push_back({ static_cast<float>(vx * rScale), static_cast<float>(vy * rScale), vp.diamMm, vp.drillMm, netId });
                            }
                        } else if (ik == "wire") {
                            for (size_t m = 1; m < item.list.size(); ++m) {
                                const auto& wsub = item.list[m];
                                std::string wk = LowerAsciiSymSes(DetailSpecctra::HeadKey(wsub));
                                if ((wk == "path" || wk == "polyline_path" || wk == "polygon") && wsub.list.size() >= 7 && out.traces.size() < 100000) {
                                    EdaLayerId lid = mapSesLayer(wsub.list[1].atom);
                                    double wRaw = 0.0;
                                    DetailSpecctra::AtomFloat(wsub.list[2], wRaw);
                                    float wMm = std::clamp(static_cast<float>(wRaw * rScale), 0.10f, 4.0f);
                                    std::vector<std::pair<float, float>> pts;
                                    for (size_t p = 3; p + 1 < wsub.list.size(); p += 2) {
                                        if (wsub.list[p].isList || wsub.list[p + 1].isList) break;
                                        double px = 0.0, py = 0.0;
                                        if (!DetailSpecctra::AtomFloat(wsub.list[p], px) ||
                                            !DetailSpecctra::AtomFloat(wsub.list[p + 1], py)) break;
                                        pts.push_back({ static_cast<float>(px * rScale), static_cast<float>(py * rScale) });
                                    }
                                    for (size_t s = 1; s < pts.size(); ++s) {
                                        out.traces.push_back({ pts[s - 1].first, pts[s - 1].second, pts[s].first, pts[s].second, wMm, lid, netId });
                                    }
                                    if (wk == "polygon" && pts.size() >= 3) {
                                        out.traces.push_back({ pts.back().first, pts.back().second, pts.front().first, pts.front().second, wMm, lid, netId });
                                    }
                                } else if (wk == "qarc" && wsub.list.size() >= 9 && out.traces.size() < 100000) {
                                    EdaLayerId lid = mapSesLayer(wsub.list[1].atom);
                                    double wRaw = 0, sx = 0, sy = 0, ex = 0, ey = 0, cx = 0, cy = 0;
                                    if (DetailSpecctra::AtomFloat(wsub.list[2], wRaw) &&
                                        DetailSpecctra::AtomFloat(wsub.list[3], sx) && DetailSpecctra::AtomFloat(wsub.list[4], sy) &&
                                        DetailSpecctra::AtomFloat(wsub.list[5], ex) && DetailSpecctra::AtomFloat(wsub.list[6], ey) &&
                                        DetailSpecctra::AtomFloat(wsub.list[7], cx) && DetailSpecctra::AtomFloat(wsub.list[8], cy))
                                    {
                                        float wMm = std::clamp(static_cast<float>(wRaw * rScale), 0.10f, 4.0f);
                                        DetailEda100::AddTessellatedArc(
                                            out, static_cast<float>(sx * rScale), static_cast<float>(sy * rScale),
                                            static_cast<float>(ex * rScale), static_cast<float>(ey * rScale),
                                            static_cast<float>(cx * rScale), static_cast<float>(cy * rScale),
                                            false, wMm, lid, netId);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        };

        if (LowerAsciiSymSes(DetailSpecctra::HeadKey(*sesRoot)) == "routes") {
            parseRoutesNode(*sesRoot);
        } else {
            for (size_t i = 1; i < sesRoot->list.size(); ++i) {
                const auto& sec = sesRoot->list[i];
                std::string sk = LowerAsciiSymSes(DetailSpecctra::HeadKey(sec));
                if (sk == "base_design" && sec.list.size() >= 2 && !sec.list[1].isList) {
                    designName = sec.list[1].atom;
                } else if (sk == "placement") {
                    double pScale = parseResNodeScale(sec, rootScaleMm);
                    for (size_t j = 1; j < sec.list.size(); ++j) {
                        const auto& cmpNode = sec.list[j];
                        if (LowerAsciiSymSes(DetailSpecctra::HeadKey(cmpNode)) != "component" || cmpNode.list.size() < 2) continue;
                        std::string pkgName = cmpNode.list[1].atom;
                        for (size_t k = 2; k < cmpNode.list.size(); ++k) {
                            const auto& plc = cmpNode.list[k];
                            if (LowerAsciiSymSes(DetailSpecctra::HeadKey(plc)) != "place" || plc.list.size() < 6) continue;
                            std::string refDes = plc.list[1].atom;
                            double px = 0.0, py = 0.0, rot = 0.0;
                            if (!DetailSpecctra::AtomFloat(plc.list[2], px) || !DetailSpecctra::AtomFloat(plc.list[3], py)) continue;
                            std::string sideLow = LowerAsciiSymSes(plc.list[4].atom);
                            DetailSpecctra::AtomFloat(plc.list[5], rot);

                            EdaComponent3D comp;
                            comp.refDes = PathUtils::Utf8ToWide(refDes);
                            comp.footprint = PathUtils::Utf8ToWide(pkgName);
                            comp.x = static_cast<float>(px * pScale);
                            comp.y = static_cast<float>(py * pScale);
                            comp.rotDeg = static_cast<float>(rot);
                            comp.bottomSide = (sideLow == "back" || sideLow == "bottom" || sideLow == "b");
                            comp.widthMm = 2.4f; comp.lengthMm = 1.6f; comp.heightMm = 1.0f;
                            comp.category = ClassifyComponent(comp.refDes, comp.footprint, comp.widthMm, comp.lengthMm, comp.heightMm, comp.pinCount);
                            EdaLayerId silkL = comp.bottomSide ? EdaLayerId::BottomSilk : EdaLayerId::TopSilk;
                            DetailEda100::AddRectOutline(out, comp.x, comp.y, comp.widthMm, comp.lengthMm, silkL, 0.16f);
                            out.labels.push_back({ comp.x, comp.y, 0.85f, silkL, comp.refDes });
                            out.components.push_back(std::move(comp));
                        }
                    }
                } else if (sk == "routes") {
                    parseRoutesNode(sec);
                }
            }
        }

        // Zero-Cheat Guarantee: reject empty (session ...) shells with 0 routes, 0 vias, and 0 placed parts
        if (out.traces.empty() && out.vias.empty() && out.components.empty()) {
            out = ParsedEdaBoard{};
            return false;
        }

        out.formatTitle = L"Specctra Autorouter Routed Session (.ses";
        if (!designName.empty()) out.formatTitle += L" — " + PathUtils::Utf8ToWide(designName);
        out.formatTitle += L")";
        out.netCount = std::max<size_t>(1u, routedNets.empty() ? out.components.size() : routedNets.size());
        return true;
    }

    // #63. SMT Pick-and-Place Centroid File (.pos / .cpl / .mnt)
    if (ext == L".pos" || ext == L".cpl" || ext == L".mnt") {
        using namespace DetailKiCadSymSesPos;
        for (unsigned char c : sv) {
            if (c == 0) return false;
        }

        float defaultUnitToMm = 1.0f;
        int colRef = -1, colVal = -1, colPkg = -1, colX = -1, colY = -1, colRot = -1, colSide = -1;
        bool hasExplicitHeader = false;

        out.kind = EdaDocKind::KiCadPcb;

        size_t pos = 0;
        while (pos < sv.size() && out.components.size() < 8000) {
            size_t eol = sv.find('\n', pos);
            if (eol == std::string_view::npos) eol = sv.size();
            std::string_view ln = sv.substr(pos, eol - pos);
            pos = eol + 1;
            while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ' || ln.back() == '\t')) ln.remove_suffix(1);
            while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
            if (ln.empty()) continue;

            if (ln.front() == '#' || ln.rfind("//", 0) == 0) {
                std::string lowHdr = LowerAsciiSymSes(ln);
                if (lowHdr.find("unit") != std::string::npos) {
                    if (lowHdr.find("inch") != std::string::npos || lowHdr.find(" = in") != std::string::npos) defaultUnitToMm = 25.4f;
                    else if (lowHdr.find("mil") != std::string::npos) defaultUnitToMm = 0.0254f;
                    else if (lowHdr.find("mm") != std::string::npos) defaultUnitToMm = 1.0f;
                }
                continue;
            }

            const bool isCsvLine = (ln.find(',') != std::string_view::npos || ln.find(';') != std::string_view::npos);
            std::vector<std::string> cols = SplitPickAndPlaceRow(ln, isCsvLine);
            if (cols.size() < 4) continue;

            std::string c0Low = LowerAsciiSymSes(cols[0]);
            if (c0Low == "ref" || c0Low == "refdes" || c0Low == "designator" || c0Low == "part" || c0Low == "component") {
                colRef = 0;
                colVal = colPkg = colX = colY = colRot = colSide = -1;
                for (size_t ci = 1; ci < cols.size(); ++ci) {
                    std::string h = LowerAsciiSymSes(cols[ci]);
                    if (h.find("(mil)") != std::string::npos) defaultUnitToMm = 0.0254f;
                    else if (h.find("(in)") != std::string::npos || h.find("(inch)") != std::string::npos) defaultUnitToMm = 25.4f;
                    else if (h.find("(mm)") != std::string::npos) defaultUnitToMm = 1.0f;

                    if (colX < 0 && (h == "posx" || h == "mid x" || h == "midx" || h == "x" || h.find("center-x") != std::string::npos || h.find("ref x") != std::string::npos || h.find("pad x") != std::string::npos)) {
                        colX = static_cast<int>(ci);
                    } else if (colY < 0 && (h == "posy" || h == "mid y" || h == "midy" || h == "y" || h.find("center-y") != std::string::npos || h.find("ref y") != std::string::npos || h.find("pad y") != std::string::npos)) {
                        colY = static_cast<int>(ci);
                    } else if (colRot < 0 && (h.find("rot") != std::string::npos || h.find("angle") != std::string::npos || h == "orient")) {
                        colRot = static_cast<int>(ci);
                    } else if (colSide < 0 && (h == "side" || h == "layer" || h == "tb")) {
                        colSide = static_cast<int>(ci);
                    } else if (colPkg < 0 && (h.find("pack") != std::string::npos || h.find("foot") != std::string::npos || h == "pattern")) {
                        colPkg = static_cast<int>(ci);
                    } else if (colVal < 0 && (h == "val" || h == "value" || h == "comment" || h == "device")) {
                        colVal = static_cast<int>(ci);
                    }
                }
                if (colX >= 0 && colY >= 0) hasExplicitHeader = true;
                continue;
            }

            std::string ref, valStr, pkgStr;
            float px = 0.0f, py = 0.0f, rot = 0.0f;
            bool bottom = false;

            if (hasExplicitHeader) {
                int maxReq = std::max(colRef, std::max(colX, colY));
                if (static_cast<int>(cols.size()) <= maxReq) continue;
                ref = cols[static_cast<size_t>(colRef)];
                if (!LooksLikeValidRefDes(ref)) continue;
                if (!ParsePnpCoordTokenMm(cols[static_cast<size_t>(colX)], defaultUnitToMm, px) ||
                    !ParsePnpCoordTokenMm(cols[static_cast<size_t>(colY)], defaultUnitToMm, py)) {
                    continue;
                }
                if (colVal >= 0 && colVal < static_cast<int>(cols.size())) valStr = cols[static_cast<size_t>(colVal)];
                if (colPkg >= 0 && colPkg < static_cast<int>(cols.size())) pkgStr = cols[static_cast<size_t>(colPkg)];
                if (colRot >= 0 && colRot < static_cast<int>(cols.size())) {
                    rot = std::strtof(cols[static_cast<size_t>(colRot)].c_str(), nullptr);
                }
                if (colSide >= 0 && colSide < static_cast<int>(cols.size())) {
                    std::string sl = LowerAsciiSymSes(cols[static_cast<size_t>(colSide)]);
                    bottom = (sl == "bottom" || sl == "bot" || sl == "b" || sl == "back" || sl == "bottomlayer");
                }
            } else {
                ref = cols[0];
                if (!LooksLikeValidRefDes(ref)) continue;

                float tx = 0.0f, ty = 0.0f;
                if (cols.size() >= 5 &&
                    ParsePnpCoordTokenMm(cols[1], defaultUnitToMm, tx) &&
                    ParsePnpCoordTokenMm(cols[2], defaultUnitToMm, ty))
                {
                    px = tx; py = ty;
                    std::string c3Low = LowerAsciiSymSes(cols[3]);
                    if (c3Low == "top" || c3Low == "bottom" || c3Low == "bot" || c3Low == "t" || c3Low == "b") {
                        bottom = (c3Low == "bottom" || c3Low == "bot" || c3Low == "b");
                        rot = std::strtof(cols[4].c_str(), nullptr);
                        if (cols.size() >= 6) valStr = cols[5];
                    } else {
                        rot = std::strtof(cols[3].c_str(), nullptr);
                        valStr = cols[4];
                        if (cols.size() >= 6) pkgStr = cols[5];
                    }
                } else if (cols.size() >= 6 &&
                           ParsePnpCoordTokenMm(cols[3], defaultUnitToMm, tx) &&
                           ParsePnpCoordTokenMm(cols[4], defaultUnitToMm, ty))
                {
                    valStr = cols[1];
                    pkgStr = cols[2];
                    px = tx; py = ty;
                    rot = std::strtof(cols[5].c_str(), nullptr);
                    if (cols.size() >= 7) {
                        std::string c6Low = LowerAsciiSymSes(cols[6]);
                        bottom = (c6Low == "bottom" || c6Low == "bot" || c6Low == "b" || c6Low == "back");
                    }
                } else {
                    continue;
                }
            }

            if (pkgStr.empty()) pkgStr = "SMD_0805";

            EdaComponent3D c;
            c.refDes = PathUtils::Utf8ToWide(ref);
            c.value = PathUtils::Utf8ToWide(valStr);
            c.footprint = PathUtils::Utf8ToWide(pkgStr);
            c.x = px;
            c.y = py;
            c.rotDeg = rot;
            c.bottomSide = bottom;
            c.widthMm = 2.0f;
            c.lengthMm = 1.25f;
            c.heightMm = 0.8f;
            c.pinCount = 2;
            c.category = ClassifyComponent(c.refDes, c.footprint, c.widthMm, c.lengthMm, c.heightMm, c.pinCount);

            std::string pkgUp = pkgStr;
            for (char& ch : pkgUp) if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
            c.pinCount = ExtractTrailingPinCount(pkgUp, c.pinCount);

            const float rad = rot * 3.14159265f / 180.0f;
            const float cs = std::cos(rad), sn = std::sin(rad);
            auto emitRotPad = [&](float lx, float ly, float pw, float ph, float padLocalRot, uint8_t shape, int pinNo) {
                float wx = px + (lx * cs - ly * sn);
                float wy = py + (lx * sn + ly * cs);
                EdaLayerId padL = bottom ? EdaLayerId::BottomCopper : EdaLayerId::TopCopper;
                out.pads.push_back({ wx, wy, pw, ph, rot + padLocalRot, 0.0f, shape, padL, std::to_wstring(pinNo), L"" });
            };

            if (pkgUp.find("BGA") != std::string::npos) {
                int gridN = std::clamp(static_cast<int>(std::round(std::sqrt(static_cast<double>(std::max(4, c.pinCount))))), 2, 8);
                float pitch = 0.8f;
                c.widthMm = std::max(c.widthMm, static_cast<float>(gridN) * pitch + 1.0f);
                c.lengthMm = c.widthMm;
                int pno = 1;
                for (int r = 0; r < gridN; ++r) {
                    for (int cl = 0; cl < gridN; ++cl) {
                        float lx = (static_cast<float>(cl) - (gridN - 1) * 0.5f) * pitch;
                        float ly = (static_cast<float>(r)  - (gridN - 1) * 0.5f) * pitch;
                        emitRotPad(lx, ly, 0.42f, 0.42f, 0.0f, 0, pno++);
                    }
                }
                c.pinCount = gridN * gridN;
            } else if (pkgUp.find("QFP") != std::string::npos || pkgUp.find("QFN") != std::string::npos) {
                int perSide = std::clamp(std::max(16, c.pinCount) / 4, 3, 20);
                float pitch = (perSide >= 12) ? 0.5f : 0.65f;
                float bodySpan = static_cast<float>(perSide) * pitch + 1.2f;
                c.widthMm = bodySpan;
                c.lengthMm = bodySpan;
                float halfSpan = bodySpan * 0.5f;
                int pno = 1;
                for (int i = 0; i < perSide; ++i) {
                    float ofs = (static_cast<float>(i) - (perSide - 1) * 0.5f) * pitch;
                    emitRotPad(-halfSpan, -ofs, 0.70f, pitch * 0.55f, 0.0f, 1, pno++);
                }
                for (int i = 0; i < perSide; ++i) {
                    float ofs = (static_cast<float>(i) - (perSide - 1) * 0.5f) * pitch;
                    emitRotPad(ofs, -halfSpan, pitch * 0.55f, 0.70f, 0.0f, 1, pno++);
                }
                for (int i = 0; i < perSide; ++i) {
                    float ofs = (static_cast<float>(i) - (perSide - 1) * 0.5f) * pitch;
                    emitRotPad(halfSpan, ofs, 0.70f, pitch * 0.55f, 0.0f, 1, pno++);
                }
                for (int i = 0; i < perSide; ++i) {
                    float ofs = (static_cast<float>(i) - (perSide - 1) * 0.5f) * pitch;
                    emitRotPad(-ofs, halfSpan, pitch * 0.55f, 0.70f, 0.0f, 1, pno++);
                }
                c.pinCount = perSide * 4;
            } else if (c.category == EdaPartCategory::TransistorSOT && c.pinCount <= 3) {
                emitRotPad(-0.95f, -1.0f, 0.60f, 0.70f, 0.0f, 1, 1);
                emitRotPad( 0.95f, -1.0f, 0.60f, 0.70f, 0.0f, 1, 2);
                emitRotPad( 0.0f,   1.0f, 0.60f, 0.70f, 0.0f, 1, 3);
                c.pinCount = 3;
            } else if (c.pinCount >= 4 && (c.category == EdaPartCategory::IC_Package || c.category == EdaPartCategory::TransistorSOT || c.category == EdaPartCategory::Connector)) {
                int half = std::clamp((c.pinCount + 1) / 2, 2, 24);
                float pitch = (pkgUp.find("TSSOP") != std::string::npos || pkgUp.find("MSOP") != std::string::npos) ? 0.65f
                            : (c.category == EdaPartCategory::Connector ? 2.54f : 1.27f);
                c.lengthMm = std::max(c.lengthMm, static_cast<float>(half) * pitch + 0.6f);
                float rowX = c.widthMm * 0.5f;
                int pno = 1;
                for (int i = 0; i < half; ++i) {
                    float ly = ((half - 1) * 0.5f - static_cast<float>(i)) * pitch;
                    emitRotPad(-rowX, ly, 0.85f, pitch * 0.52f, 0.0f, 1, pno++);
                }
                for (int i = 0; i < half; ++i) {
                    float ly = (static_cast<float>(i) - (half - 1) * 0.5f) * pitch;
                    emitRotPad(rowX, ly, 0.85f, pitch * 0.52f, 0.0f, 1, pno++);
                }
            } else {
                float dx = c.widthMm * 0.44f;
                float pw = std::clamp(c.widthMm * 0.34f, 0.30f, 1.4f);
                float ph = std::clamp(c.lengthMm * 0.86f, 0.35f, 2.2f);
                emitRotPad(-dx, 0.0f, pw, ph, 0.0f, 1, 1);
                emitRotPad( dx, 0.0f, pw, ph, 0.0f, 1, 2);
            }

            float hw = c.widthMm * 0.5f, hl = c.lengthMm * 0.5f;
            auto rotCorner = [&](float lx, float ly) -> std::pair<float, float> {
                return { px + (lx * cs - ly * sn), py + (lx * sn + ly * cs) };
            };
            auto k0 = rotCorner(-hw, -hl), k1 = rotCorner(hw, -hl), k2 = rotCorner(hw, hl), k3 = rotCorner(-hw, hl);
            EdaLayerId silkL = bottom ? EdaLayerId::BottomSilk : EdaLayerId::TopSilk;
            out.traces.push_back({ k0.first, k0.second, k1.first, k1.second, 0.14f, silkL, 0 });
            out.traces.push_back({ k1.first, k1.second, k2.first, k2.second, 0.14f, silkL, 0 });
            out.traces.push_back({ k2.first, k2.second, k3.first, k3.second, 0.14f, silkL, 0 });
            out.traces.push_back({ k3.first, k3.second, k0.first, k0.second, 0.14f, silkL, 0 });
            out.labels.push_back({ px, py, 0.85f, silkL, c.refDes });

            out.components.push_back(std::move(c));
        }

        if (out.components.empty()) {
            out = ParsedEdaBoard{};
            return false;
        }

        float mnX = out.components[0].x, mxX = mnX, mnY = out.components[0].y, mxY = mnY;
        for (const auto& c : out.components) {
            float span = std::max(c.widthMm, c.lengthMm) * 0.5f + 3.0f;
            mnX = std::min(mnX, c.x - span); mxX = std::max(mxX, c.x + span);
            mnY = std::min(mnY, c.y - span); mxY = std::max(mxY, c.y + span);
        }
        DetailEda100::AddRectOutline(out, (mnX + mxX) * 0.5f, (mnY + mxY) * 0.5f, mxX - mnX, mxY - mnY, EdaLayerId::EdgeCuts, 0.36f);
        out.formatTitle = L"SMT Pick-and-Place Assembly (" + ext + L" — " + std::to_wstring(out.components.size()) + L" parts)";
        out.netCount = out.components.size();
        return true;
    }
    return false;
}

// ============================================================================
// #64, #65, #66. IBIS (.ibs), Multi-Port Touchstone (.s3p/.s4p/.s8p), SEMI OASIS (.oas)
// ============================================================================
inline bool ParseIbisTouchstoneOasis(const uint8_t* data, size_t size, const std::wstring& ext, ParsedEdaBoard& out) {
    std::string_view sv(reinterpret_cast<const char*>(data), size);

    // #64. IBIS Signal Integrity Model (.ibs — ANSI/EIA-656 v3.2–v7.2)
    if (ext == L".ibs") {
        for (size_t i = 0; i < size; ++i) {
            if (data[i] == 0) return false;
        }
        if (sv.find("[IBIS Ver]") == std::string_view::npos &&
            sv.find("[ibis ver]") == std::string_view::npos &&
            sv.find("[Component]") == std::string_view::npos &&
            sv.find("[Model]") == std::string_view::npos)
        {
            return false;
        }

        auto parseIbisVal = [](std::string_view tok, double& outV) -> bool {
            while (!tok.empty() && static_cast<unsigned char>(tok.front()) <= 0x20u) tok.remove_prefix(1);
            while (!tok.empty() && static_cast<unsigned char>(tok.back()) <= 0x20u) tok.remove_suffix(1);
            if (tok.empty() || _strnicmp(tok.data(), "NA", 2) == 0) return false;
            char buf[64] = {};
            std::memcpy(buf, tok.data(), std::min<size_t>(tok.size(), 63));
            char* ep = nullptr;
            double v = std::strtod(buf, &ep);
            if (!ep || ep == buf || !std::isfinite(v)) return false;
            switch (*ep) {
            case 'f': case 'F': if (ep[1] != 'a' && ep[1] != 'A') v *= 1e-15; break;
            case 'p': v *= 1e-12; break;
            case 'n': v *= 1e-9;  break;
            case 'u': v *= 1e-6;  break;
            case 'm': v *= 1e-3;  break;
            case 'k': case 'K': v *= 1e3; break;
            case 'M': v *= 1e6;   break;
            case 'G': case 'g': v *= 1e9; break;
            case 'T': case 't': v *= 1e12; break;
            default: break;
            }
            outV = v;
            return true;
        };

        auto splitWs = [](std::string_view ln, std::vector<std::string_view>& toks) {
            toks.clear();
            size_t p = 0;
            while (p < ln.size()) {
                while (p < ln.size() && static_cast<unsigned char>(ln[p]) <= 0x20u) ++p;
                if (p >= ln.size()) break;
                size_t s = p;
                while (p < ln.size() && static_cast<unsigned char>(ln[p]) > 0x20u) ++p;
                toks.push_back(ln.substr(s, p - s));
            }
        };

        struct IbisPinEntry {
            std::string pinName;
            std::string sigName;
            std::string modelName;
            double rPin = 0.0, lPin = 0.0, cPin = 0.0;
            bool hasRlc = false;
        };
        struct IbisCurvePt {
            double x = 0.0;
            double yTyp = 0.0, yMin = 0.0, yMax = 0.0;
            bool hasMin = false, hasMax = false;
        };
        struct IbisCurveTable {
            std::string kind;
            bool isVt = false;
            std::vector<IbisCurvePt> pts;
        };

        std::string ibisVer, compName, mfgName, activeModel, modelType;
        char commentChar = '|';
        double rPkg = 0.0, lPkg = 0.0, cPkg = 0.0;
        bool hasPkgRlc = false;
        double vinl = 0.8, vinh = 2.0, cComp = 0.0;
        bool hasThresh = false, hasRamp = false;
        double dvDtRise = 0.0, dvDtFall = 0.0;

        std::vector<IbisPinEntry> pins;
        std::vector<IbisCurveTable> curves;
        int curSec = 0;

        std::vector<std::string_view> toks;
        size_t pos = 0;
        while (pos < sv.size()) {
            size_t eol = sv.find('\n', pos);
            if (eol == std::string_view::npos) eol = sv.size();
            std::string_view ln = sv.substr(pos, eol - pos);
            pos = eol + 1;

            while (!ln.empty() && static_cast<unsigned char>(ln.front()) <= 0x20u) ln.remove_prefix(1);
            while (!ln.empty() && static_cast<unsigned char>(ln.back()) <= 0x20u) ln.remove_suffix(1);
            if (ln.empty() || ln.front() == commentChar) continue;

            size_t cmtP = ln.find(commentChar);
            if (cmtP != std::string_view::npos) {
                ln = ln.substr(0, cmtP);
                while (!ln.empty() && static_cast<unsigned char>(ln.back()) <= 0x20u) ln.remove_suffix(1);
                if (ln.empty()) continue;
            }

            if (ln.front() == '[') {
                size_t rb = ln.find(']');
                if (rb == std::string_view::npos) continue;
                std::string kw = DetailKiCadSymSesPos::LowerAsciiSymSes(ln.substr(1, rb - 1));
                std::string_view arg = ln.substr(rb + 1);
                while (!arg.empty() && static_cast<unsigned char>(arg.front()) <= 0x20u) arg.remove_prefix(1);

                curSec = 0;
                if (kw == "end") break;
                else if (kw == "ibis ver") ibisVer = std::string(arg);
                else if (kw == "comment char") {
                    if (!arg.empty()) commentChar = arg[0];
                } else if (kw == "component") compName = std::string(arg);
                else if (kw == "manufacturer") mfgName = std::string(arg);
                else if (kw == "package") curSec = 1;
                else if (kw == "pin") curSec = 2;
                else if (kw == "model") {
                    if (activeModel.empty() && !arg.empty()) activeModel = std::string(arg);
                    curSec = 3;
                } else if (kw == "ramp") curSec = 4;
                else if (kw == "pulldown" || kw == "pullup" || kw == "gnd clamp" || kw == "power clamp" ||
                         kw == "rising waveform" || kw == "falling waveform")
                {
                    if (curves.size() < 24) {
                        IbisCurveTable ct;
                        ct.kind = std::string(ln.substr(1, rb - 1));
                        ct.isVt = (kw.find("waveform") != std::string::npos);
                        curves.push_back(std::move(ct));
                        curSec = 5;
                    }
                }
                continue;
            }

            if (curSec == 1) {
                splitWs(ln, toks);
                if (toks.size() >= 2) {
                    std::string k = DetailKiCadSymSesPos::LowerAsciiSymSes(toks[0]);
                    double v = 0.0;
                    if (parseIbisVal(toks[1], v)) {
                        if (k == "r_pkg") { rPkg = v; hasPkgRlc = true; }
                        else if (k == "l_pkg") { lPkg = v; hasPkgRlc = true; }
                        else if (k == "c_pkg") { cPkg = v; hasPkgRlc = true; }
                    }
                }
            } else if (curSec == 2 && pins.size() < 256) {
                splitWs(ln, toks);
                if (toks.size() >= 3) {
                    IbisPinEntry pe;
                    pe.pinName   = std::string(toks[0]);
                    pe.sigName   = std::string(toks[1]);
                    pe.modelName = std::string(toks[2]);
                    if (toks.size() >= 6) {
                        bool okR = parseIbisVal(toks[3], pe.rPin);
                        bool okL = parseIbisVal(toks[4], pe.lPin);
                        bool okC = parseIbisVal(toks[5], pe.cPin);
                        pe.hasRlc = (okR || okL || okC);
                    }
                    pins.push_back(std::move(pe));
                }
            } else if (curSec == 3) {
                size_t eq = ln.find('=');
                std::string normLn(ln);
                if (eq != std::string_view::npos) normLn[eq] = ' ';
                splitWs(normLn, toks);
                if (toks.size() >= 2) {
                    std::string k = DetailKiCadSymSesPos::LowerAsciiSymSes(toks[0]);
                    double v = 0.0;
                    if (k == "model_type" && modelType.empty()) modelType = std::string(toks[1]);
                    else if (k == "vinl" && parseIbisVal(toks[1], v)) { vinl = v; hasThresh = true; }
                    else if (k == "vinh" && parseIbisVal(toks[1], v)) { vinh = v; hasThresh = true; }
                    else if (k == "c_comp" && parseIbisVal(toks[1], v)) { cComp = v; }
                }
            } else if (curSec == 4) {
                std::string normLn(ln);
                for (char& ch : normLn) if (ch == '=' || ch == '/') ch = ' ';
                splitWs(normLn, toks);
                if (toks.size() >= 3) {
                    std::string k0 = DetailKiCadSymSesPos::LowerAsciiSymSes(toks[0]);
                    std::string k1 = DetailKiCadSymSesPos::LowerAsciiSymSes(toks[1]);
                    double dv = 0.0, dt = 0.0;
                    if (k0 == "dv" && parseIbisVal(toks[2], dv) && toks.size() >= 4 && parseIbisVal(toks[3], dt) && dt > 1e-15) {
                        if (k1 == "dt_r") { dvDtRise = (dv / dt) * 1e-9; hasRamp = true; }
                        else if (k1 == "dt_f") { dvDtFall = (dv / dt) * 1e-9; hasRamp = true; }
                    }
                }
            } else if (curSec == 5 && !curves.empty()) {
                if (ln.find('=') != std::string_view::npos) continue;
                splitWs(ln, toks);
                if (toks.size() >= 2 && curves.back().pts.size() < 1024) {
                    IbisCurvePt pt;
                    if (parseIbisVal(toks[0], pt.x) && parseIbisVal(toks[1], pt.yTyp)) {
                        if (toks.size() >= 3) pt.hasMin = parseIbisVal(toks[2], pt.yMin);
                        if (toks.size() >= 4) pt.hasMax = parseIbisVal(toks[3], pt.yMax);
                        curves.back().pts.push_back(pt);
                    }
                }
            }
        }

        size_t validViCurves = 0, validVtCurves = 0;
        for (auto& ct : curves) {
            if (ct.pts.size() >= 2) {
                std::sort(ct.pts.begin(), ct.pts.end(), [](const IbisCurvePt& a, const IbisCurvePt& b) { return a.x < b.x; });
                if (ct.isVt) ++validVtCurves;
                else         ++validViCurves;
            }
        }

        if (validViCurves == 0 && validVtCurves == 0 && pins.empty() && !hasRamp) {
            return false;
        }

        out.kind = EdaDocKind::KiCadSch;
        std::wstring title = L"IBIS High-Speed I/O Buffer Model (";
        if (!compName.empty()) title += PathUtils::Utf8ToWide(compName);
        else if (!activeModel.empty()) title += PathUtils::Utf8ToWide(activeModel);
        else title += L".ibs";
        if (!ibisVer.empty()) title += L", v" + PathUtils::Utf8ToWide(ibisVer);
        title += L")";
        out.formatTitle = title;

        const bool showVi = (validViCurves > 0);
        const bool showVt = (validVtCurves > 0 || (validViCurves == 0 && hasRamp));
        const float viX0 = -44.0f, viW = showVt ? 40.0f : 84.0f;
        const float vtX0 = showVi ? 4.0f : -44.0f, vtW = showVi ? 40.0f : 84.0f;
        const float plotY0 = -16.0f, plotH = 38.0f;
        out.labels.push_back({ -44.0f, plotY0 + plotH + 7.0f, 1.12f, EdaLayerId::SchLabel, title });

        if (showVi) {
            DetailEda200::DrawGraticule(out, viX0, plotY0, viW, plotH, 8, 8);
            double vMin = 1e300, vMax = -1e300, iMin = 1e300, iMax = -1e300;
            for (const auto& ct : curves) {
                if (ct.isVt || ct.pts.size() < 2) continue;
                for (const auto& p : ct.pts) {
                    vMin = std::min(vMin, p.x); vMax = std::max(vMax, p.x);
                    double yc = std::clamp(p.yTyp, -0.5, 0.5);
                    iMin = std::min(iMin, yc); iMax = std::max(iMax, yc);
                }
            }
            if (!(vMax > vMin + 1e-9)) { vMin -= 1.0; vMax += 1.0; }
            if (!(iMax > iMin + 1e-9)) { iMin -= 0.01; iMax += 0.01; }

            wchar_t viHdr[128] = {};
            swprintf_s(viHdr, L"V-I Characteristics [%.2g..%.2g V, %.1f..%.1f mA]", vMin, vMax, iMin * 1e3, iMax * 1e3);
            out.labels.push_back({ viX0, plotY0 + plotH + 1.5f, 0.95f, EdaLayerId::SchLabel, viHdr });

            auto mapViX = [&](double v) -> float {
                return viX0 + 1.0f + static_cast<float>(std::clamp((v - vMin) / (vMax - vMin), 0.0, 1.0)) * (viW - 2.0f);
            };
            auto mapViY = [&](double iAmp) -> float {
                double ic = std::clamp(iAmp, iMin, iMax);
                return plotY0 + 1.0f + static_cast<float>(std::clamp((ic - iMin) / (iMax - iMin), 0.0, 1.0)) * (plotH - 2.0f);
            };

            if (iMin < 0.0 && iMax > 0.0) {
                float yZero = mapViY(0.0);
                out.traces.push_back({ viX0, yZero, viX0 + viW, yZero, 0.14f, EdaLayerId::SchSymbol, 0 });
            }

            int cIdx = 0;
            for (const auto& ct : curves) {
                if (ct.isVt || ct.pts.size() < 2) continue;
                std::string kl = DetailKiCadSymSesPos::LowerAsciiSymSes(ct.kind);
                EdaLayerId lid = (kl == "pulldown") ? EdaLayerId::TopCopper
                               : (kl == "pullup")   ? EdaLayerId::BottomCopper
                               : (kl == "gnd clamp")? EdaLayerId::SchWire
                                                    : EdaLayerId::InnerCopper;
                for (size_t i = 1; i < ct.pts.size(); ++i) {
                    const auto& p0 = ct.pts[i - 1];
                    const auto& p1 = ct.pts[i];
                    out.traces.push_back({ mapViX(p0.x), mapViY(p0.yTyp), mapViX(p1.x), mapViY(p1.yTyp), 0.34f, lid, cIdx + 1 });
                    if (p0.hasMin && p1.hasMin) {
                        out.traces.push_back({ mapViX(p0.x), mapViY(p0.yMin), mapViX(p1.x), mapViY(p1.yMin), 0.14f, lid, cIdx + 1 });
                    }
                    if (p0.hasMax && p1.hasMax) {
                        out.traces.push_back({ mapViX(p0.x), mapViY(p0.yMax), mapViX(p1.x), mapViY(p1.yMax), 0.14f, lid, cIdx + 1 });
                    }
                }
                EdaComponent3D mc;
                mc.refDes = PathUtils::Utf8ToWide(ct.kind);
                mc.value = std::to_wstring(ct.pts.size()) + L" V-I pts";
                mc.footprint = L"IBIS_VI";
                mc.x = mapViX(ct.pts[ct.pts.size() / 2].x);
                mc.y = mapViY(ct.pts[ct.pts.size() / 2].yTyp);
                mc.widthMm = 3.0f; mc.lengthMm = 2.0f;
                out.components.push_back(std::move(mc));
                ++cIdx;
            }
        }

        if (showVt) {
            DetailEda200::DrawGraticule(out, vtX0, plotY0, vtW, plotH, 8, 8);
            if (validVtCurves > 0) {
                double tMin = 1e300, tMax = -1e300, vMin = 1e300, vMax = -1e300;
                for (const auto& ct : curves) {
                    if (!ct.isVt || ct.pts.size() < 2) continue;
                    for (const auto& p : ct.pts) {
                        tMin = std::min(tMin, p.x); tMax = std::max(tMax, p.x);
                        vMin = std::min(vMin, p.yTyp); vMax = std::max(vMax, p.yTyp);
                    }
                }
                if (!(tMax > tMin + 1e-15)) { tMax = tMin + 1e-9; }
                if (!(vMax > vMin + 1e-9))  { vMin -= 0.5; vMax += 0.5; }

                wchar_t vtHdr[128] = {};
                swprintf_s(vtHdr, L"V-T Switching Waveforms [0..%.2f ns, %.2f..%.2f V]", (tMax - tMin) * 1e9, vMin, vMax);
                out.labels.push_back({ vtX0, plotY0 + plotH + 1.5f, 0.95f, EdaLayerId::SchLabel, vtHdr });

                auto mapVtX = [&](double t) -> float {
                    return vtX0 + 1.0f + static_cast<float>(std::clamp((t - tMin) / (tMax - tMin), 0.0, 1.0)) * (vtW - 2.0f);
                };
                auto mapVtY = [&](double v) -> float {
                    return plotY0 + 1.0f + static_cast<float>(std::clamp((v - vMin) / (vMax - vMin), 0.0, 1.0)) * (plotH - 2.0f);
                };

                if (hasThresh) {
                    if (vinl >= vMin && vinl <= vMax) {
                        float yl = mapVtY(vinl);
                        out.traces.push_back({ vtX0, yl, vtX0 + vtW, yl, 0.14f, EdaLayerId::SchSymbol, 0 });
                        out.labels.push_back({ vtX0 + 0.8f, yl + 0.4f, 0.72f, EdaLayerId::SchLabel, L"Vinl" });
                    }
                    if (vinh >= vMin && vinh <= vMax) {
                        float yh = mapVtY(vinh);
                        out.traces.push_back({ vtX0, yh, vtX0 + vtW, yh, 0.14f, EdaLayerId::SchSymbol, 0 });
                        out.labels.push_back({ vtX0 + 0.8f, yh + 0.4f, 0.72f, EdaLayerId::SchLabel, L"Vinh" });
                    }
                }

                int wIdx = 10;
                for (const auto& ct : curves) {
                    if (!ct.isVt || ct.pts.size() < 2) continue;
                    bool isRise = (DetailKiCadSymSesPos::LowerAsciiSymSes(ct.kind).find("rising") != std::string::npos);
                    EdaLayerId lid = isRise ? EdaLayerId::SchWire : EdaLayerId::InnerCopper;
                    for (size_t i = 1; i < ct.pts.size(); ++i) {
                        const auto& p0 = ct.pts[i - 1];
                        const auto& p1 = ct.pts[i];
                        out.traces.push_back({ mapVtX(p0.x), mapVtY(p0.yTyp), mapVtX(p1.x), mapVtY(p1.yTyp), 0.36f, lid, wIdx });
                        if (p0.hasMin && p1.hasMin) {
                            out.traces.push_back({ mapVtX(p0.x), mapVtY(p0.yMin), mapVtX(p1.x), mapVtY(p1.yMin), 0.14f, lid, wIdx });
                        }
                        if (p0.hasMax && p1.hasMax) {
                            out.traces.push_back({ mapVtX(p0.x), mapVtY(p0.yMax), mapVtX(p1.x), mapVtY(p1.yMax), 0.14f, lid, wIdx });
                        }
                    }
                    EdaComponent3D wc;
                    wc.refDes = PathUtils::Utf8ToWide(ct.kind);
                    wc.value = std::to_wstring(ct.pts.size()) + L" V-T pts";
                    wc.footprint = L"IBIS_VT";
                    wc.x = mapVtX(ct.pts[ct.pts.size() / 2].x);
                    wc.y = mapVtY(ct.pts[ct.pts.size() / 2].yTyp);
                    wc.widthMm = 3.0f; wc.lengthMm = 2.0f;
                    out.components.push_back(std::move(wc));
                    ++wIdx;
                }
            } else if (hasRamp) {
                out.labels.push_back({ vtX0, plotY0 + plotH + 1.5f, 0.95f, EdaLayerId::SchLabel, L"[Ramp] Slew-Rate Edges" });
                float rFrac = std::clamp(static_cast<float>(3.3 / std::max(0.2, dvDtRise)) / 5.0f, 0.08f, 0.85f);
                float fFrac = std::clamp(static_cast<float>(3.3 / std::max(0.2, dvDtFall)) / 5.0f, 0.08f, 0.85f);
                float xR0 = vtX0 + 4.0f, xR1 = xR0 + (vtW - 8.0f) * rFrac, xEnd = vtX0 + vtW - 4.0f;
                float yLo = plotY0 + 4.0f, yHi = plotY0 + plotH - 4.0f;
                out.traces.push_back({ xR0, yLo, xR1, yHi, 0.34f, EdaLayerId::SchWire, 10 });
                out.traces.push_back({ xR1, yHi, xEnd, yHi, 0.34f, EdaLayerId::SchWire, 10 });
                float xF1 = xR0 + (vtW - 8.0f) * fFrac;
                out.traces.push_back({ xR0, yHi, xF1, yLo, 0.34f, EdaLayerId::InnerCopper, 11 });
                out.traces.push_back({ xF1, yLo, xEnd, yLo, 0.34f, EdaLayerId::InnerCopper, 11 });
            }
        }

        {
            std::wstring meta = L"Model: " + PathUtils::Utf8ToWide(activeModel.empty() ? "IO_BUF" : activeModel);
            if (!modelType.empty()) meta += L" [" + PathUtils::Utf8ToWide(modelType) + L"]";
            if (cComp > 0.0) {
                wchar_t cb[48] = {};
                swprintf_s(cb, L" | C_comp=%.2f pF", cComp * 1e12);
                meta += cb;
            }
            if (hasPkgRlc) {
                wchar_t pb[80] = {};
                swprintf_s(pb, L" | Pkg RLC=(%.0f m\x03A9, %.2f nH, %.2f pF)", rPkg * 1e3, lPkg * 1e9, cPkg * 1e12);
                meta += pb;
            }
            out.labels.push_back({ -44.0f, plotY0 + plotH + 4.2f, 1.02f, EdaLayerId::SchLabel, meta });
        }

        if (!pins.empty()) {
            const size_t nShowPins = std::min<size_t>(pins.size(), 24u);
            const float stripW = std::max(28.0f, static_cast<float>(nShowPins) * 3.4f + 4.0f);
            const float stripCx = 0.0f, stripCy = plotY0 - 8.5f, stripH = 5.5f;
            DetailEda100::AddRectOutline(out, stripCx, stripCy, stripW, stripH, EdaLayerId::SchSymbol, 0.28f);
            float startX = stripCx - stripW * 0.5f + 2.8f;
            float pitch = (nShowPins > 1) ? ((stripW - 5.6f) / static_cast<float>(nShowPins - 1)) : 0.0f;
            for (size_t pi = 0; pi < nShowPins; ++pi) {
                float px = startX + static_cast<float>(pi) * pitch;
                float py = stripCy - stripH * 0.5f - 1.5f;
                out.traces.push_back({ px, stripCy - stripH * 0.5f, px, py, 0.20f, EdaLayerId::SchWire, 0 });
                out.pads.push_back({ px, py, 0.55f, 0.55f, 0.0f, 0.0f, 0, EdaLayerId::SchSymbol,
                                     PathUtils::Utf8ToWide(pins[pi].pinName), PathUtils::Utf8ToWide(pins[pi].sigName) });
                if (nShowPins <= 16) {
                    out.labels.push_back({ px - 0.9f, stripCy - 0.3f, 0.68f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(pins[pi].sigName) });
                }
            }
            EdaComponent3D pkgComp;
            pkgComp.refDes = PathUtils::Utf8ToWide(compName.empty() ? "IBIS_IC" : compName);
            pkgComp.value = std::to_wstring(pins.size()) + L" pins";
            pkgComp.footprint = PathUtils::Utf8ToWide(mfgName.empty() ? "IBIS_PKG" : mfgName);
            pkgComp.x = stripCx; pkgComp.y = stripCy;
            pkgComp.widthMm = stripW; pkgComp.lengthMm = stripH;
            pkgComp.pinCount = static_cast<int>(pins.size());
            out.components.push_back(std::move(pkgComp));
        }

        out.netCount = std::max<size_t>(1u, validViCurves + validVtCurves + (pins.empty() ? 0u : 1u));
        return !out.traces.empty();
    }

    // #65. Multi-Port Single-Ended & Mixed-Mode Differential RF Touchstone (.s3p / .s4p / .s8p — v1.0 & v2.0)
    if (ext == L".s3p" || ext == L".s4p" || ext == L".s8p") {
        for (size_t i = 0; i < size; ++i) {
            if (data[i] == 0) return false;
        }

        int nPorts = (ext == L".s3p") ? 3 : ((ext == L".s8p") ? 8 : 4);
        enum class TsFmt : uint8_t { MA = 0, DB = 1, RI = 2 };
        TsFmt dataFmt = TsFmt::MA;
        std::wstring freqUnit = L"GHz";
        std::string paramKind = "S";
        double z0Ref = 50.0;
        bool hasOptionLine = false;
        bool isV2 = false;
        bool inNetData = false;
        int matrixFmt = 0;

        std::vector<double> rawNums;
        rawNums.reserve(4096);
        bool stopScan = false;

        size_t pos = 0;
        while (pos < sv.size() && !stopScan && rawNums.size() < 400000) {
            size_t eol = sv.find('\n', pos);
            if (eol == std::string_view::npos) eol = sv.size();
            std::string_view ln = sv.substr(pos, eol - pos);
            pos = eol + 1;

            size_t excl = ln.find('!');
            if (excl != std::string_view::npos) ln = ln.substr(0, excl);
            while (!ln.empty() && static_cast<unsigned char>(ln.front()) <= 0x20u) ln.remove_prefix(1);
            while (!ln.empty() && static_cast<unsigned char>(ln.back()) <= 0x20u) ln.remove_suffix(1);
            if (ln.empty()) continue;

            if (ln.front() == '[') {
                std::string low = DetailKiCadSymSesPos::LowerAsciiSymSes(ln);
                if (low.rfind("[version]", 0) == 0) {
                    isV2 = true;
                } else if (low.rfind("[number of ports]", 0) == 0) {
                    isV2 = true;
                    size_t rb = low.find(']');
                    if (rb != std::string::npos) {
                        int np = std::atoi(low.c_str() + rb + 1);
                        if (np >= 1 && np <= 32) nPorts = np;
                    }
                } else if (low.rfind("[matrix format]", 0) == 0) {
                    isV2 = true;
                    if (low.find("lower") != std::string::npos) matrixFmt = 1;
                    else if (low.find("upper") != std::string::npos) matrixFmt = 2;
                    else matrixFmt = 0;
                } else if (low.rfind("[network data]", 0) == 0) {
                    isV2 = true;
                    inNetData = true;
                } else if (low.rfind("[noise data]", 0) == 0 || low.rfind("[end]", 0) == 0) {
                    stopScan = true;
                }
                continue;
            }

            if (ln.front() == '#') {
                hasOptionLine = true;
                std::string opt = DetailKiCadSymSesPos::LowerAsciiSymSes(ln.substr(1));
                size_t p = 0;
                while (p < opt.size()) {
                    while (p < opt.size() && static_cast<unsigned char>(opt[p]) <= 0x20u) ++p;
                    if (p >= opt.size()) break;
                    size_t s = p;
                    while (p < opt.size() && static_cast<unsigned char>(opt[p]) > 0x20u) ++p;
                    std::string tok = opt.substr(s, p - s);
                    if (tok == "hz")       freqUnit = L"Hz";
                    else if (tok == "khz") freqUnit = L"kHz";
                    else if (tok == "mhz") freqUnit = L"MHz";
                    else if (tok == "ghz") freqUnit = L"GHz";
                    else if (tok == "db")  dataFmt = TsFmt::DB;
                    else if (tok == "ma")  dataFmt = TsFmt::MA;
                    else if (tok == "ri")  dataFmt = TsFmt::RI;
                    else if (tok == "s" || tok == "y" || tok == "z" || tok == "g" || tok == "h") {
                        paramKind.assign(1, static_cast<char>(std::toupper(static_cast<unsigned char>(tok[0]))));
                    } else if (tok == "r") {
                        while (p < opt.size() && static_cast<unsigned char>(opt[p]) <= 0x20u) ++p;
                        size_t rs = p;
                        while (p < opt.size() && static_cast<unsigned char>(opt[p]) > 0x20u) ++p;
                        if (p > rs) {
                            double rz = std::strtod(opt.substr(rs, p - rs).c_str(), nullptr);
                            if (std::isfinite(rz) && rz > 0.0) z0Ref = rz;
                        }
                    }
                }
                continue;
            }

            if (isV2 && !inNetData) continue;

            std::string tmp(ln);
            const char* cp = tmp.c_str();
            while (*cp) {
                while (*cp && static_cast<unsigned char>(*cp) <= 0x20u) ++cp;
                if (!*cp) break;
                char* ep = nullptr;
                double v = std::strtod(cp, &ep);
                if (ep == cp || !std::isfinite(v)) return false;
                rawNums.push_back(v);
                cp = ep;
            }
        }

        if (!hasOptionLine) return false;

        const size_t complexEntries = (matrixFmt == 0)
            ? static_cast<size_t>(nPorts * nPorts)
            : static_cast<size_t>((nPorts * (nPorts + 1)) / 2);
        const size_t stride = 1u + complexEntries * 2u;
        if (rawNums.size() < stride * 2u) return false;

        struct Cplx { double re = 0.0, im = 0.0; };
        auto pairToCplx = [&](double a, double b) -> Cplx {
            if (dataFmt == TsFmt::RI) return { a, b };
            double mag = (dataFmt == TsFmt::DB) ? std::pow(10.0, a / 20.0) : a;
            double rad = b * (3.14159265358979323846 / 180.0);
            return { mag * std::cos(rad), mag * std::sin(rad) };
        };
        auto cplxDb = [](Cplx c) -> double {
            return 20.0 * std::log10(std::max(1e-6, std::hypot(c.re, c.im)));
        };

        struct MultiPortSample {
            double freq = 0.0;
            double c0Db = 0.0;
            double c1Db = 0.0;
            double c2Db = 0.0;
            double c3Db = 0.0;
        };
        std::vector<MultiPortSample> samples;
        const size_t nRows = std::min<size_t>(rawNums.size() / stride, 4096u);
        samples.reserve(nRows);

        std::vector<Cplx> mat(static_cast<size_t>(nPorts * nPorts));
        auto S = [&](int r1Based, int c1Based) -> Cplx {
            return mat[static_cast<size_t>((r1Based - 1) * nPorts + (c1Based - 1))];
        };

        for (size_t ri = 0; ri < nRows; ++ri) {
            const double* row = &rawNums[ri * stride];
            double f = row[0];
            if (!samples.empty() && f <= samples.back().freq) break;

            if (matrixFmt == 0) {
                for (int r = 0; r < nPorts; ++r) {
                    for (int c = 0; c < nPorts; ++c) {
                        size_t idx = static_cast<size_t>(r * nPorts + c);
                        mat[idx] = pairToCplx(row[1u + idx * 2u], row[2u + idx * 2u]);
                    }
                }
            } else if (matrixFmt == 1) {
                size_t k = 0;
                for (int r = 0; r < nPorts; ++r) {
                    for (int c = 0; c <= r; ++c, ++k) {
                        Cplx v = pairToCplx(row[1u + k * 2u], row[2u + k * 2u]);
                        mat[static_cast<size_t>(r * nPorts + c)] = v;
                        mat[static_cast<size_t>(c * nPorts + r)] = v;
                    }
                }
            } else {
                size_t k = 0;
                for (int r = 0; r < nPorts; ++r) {
                    for (int c = r; c < nPorts; ++c, ++k) {
                        Cplx v = pairToCplx(row[1u + k * 2u], row[2u + k * 2u]);
                        mat[static_cast<size_t>(r * nPorts + c)] = v;
                        mat[static_cast<size_t>(c * nPorts + r)] = v;
                    }
                }
            }

            MultiPortSample smp;
            smp.freq = f;
            if (nPorts >= 4) {
                Cplx s11 = S(1,1), s13 = S(1,3), s31 = S(3,1), s33 = S(3,3);
                Cplx s21 = S(2,1), s23 = S(2,3), s41 = S(4,1), s43 = S(4,3);
                Cplx sdd11{ 0.5 * (s11.re - s13.re - s31.re + s33.re), 0.5 * (s11.im - s13.im - s31.im + s33.im) };
                Cplx sdd21{ 0.5 * (s21.re - s23.re - s41.re + s43.re), 0.5 * (s21.im - s23.im - s41.im + s43.im) };
                smp.c0Db = cplxDb(sdd11);
                smp.c1Db = cplxDb(sdd21);
                smp.c2Db = cplxDb(s21);
                smp.c3Db = cplxDb(S(3, 1));
            } else {
                smp.c0Db = cplxDb(S(1, 1));
                smp.c1Db = (nPorts >= 2) ? cplxDb(S(2, 1)) : smp.c0Db;
                smp.c2Db = (nPorts >= 3) ? cplxDb(S(3, 1)) : smp.c1Db;
                smp.c3Db = smp.c2Db;
            }
            samples.push_back(smp);
        }

        if (samples.size() < 2) return false;

        double minF = samples.front().freq;
        double maxF = samples.back().freq;
        if (maxF <= minF) maxF = minF + 1.0;

        double dbMin = 1e300, dbMax = -1e300;
        const int nTraces = (nPorts >= 4) ? 4 : std::min(nPorts, 3);
        for (const auto& s : samples) {
            dbMin = std::min({ dbMin, s.c0Db, s.c1Db, s.c2Db });
            dbMax = std::max({ dbMax, s.c0Db, s.c1Db, s.c2Db });
            if (nTraces >= 4) { dbMin = std::min(dbMin, s.c3Db); dbMax = std::max(dbMax, s.c3Db); }
        }
        dbMin = std::max(-90.0, std::floor((dbMin - 2.0) / 5.0) * 5.0);
        dbMax = std::min(25.0,  std::ceil((dbMax + 2.0) / 5.0) * 5.0);
        if (dbMax - dbMin < 10.0) { dbMin -= 5.0; dbMax += 5.0; }
        const double dbSpan = dbMax - dbMin;

        out.kind = EdaDocKind::KiCadSch;
        const wchar_t* fmtName = (dataFmt == TsFmt::DB) ? L"DB" : ((dataFmt == TsFmt::RI) ? L"RI" : L"MA");
        wchar_t titleBuf[180] = {};
        swprintf_s(titleBuf, L"Touchstone %d-Port RF %hs-Matrix (%ls, R=%.0f \x03A9, %zu pts%ls)",
            nPorts, paramKind.c_str(), fmtName, z0Ref, samples.size(),
            (nPorts >= 4) ? L", Mixed-Mode SDD" : L"");
        out.formatTitle = titleBuf;

        const float x0 = -40.0f, y0 = -24.0f, w = 84.0f, h = 48.0f;
        DetailEda200::DrawGraticule(out, x0, y0, w, h, 8, 6);

        for (int gy = 0; gy <= 6; ++gy) {
            float yy = y0 + (h * static_cast<float>(gy)) / 6.0f;
            double dv = dbMin + (dbSpan * static_cast<double>(gy)) / 6.0;
            wchar_t dl[32] = {};
            swprintf_s(dl, L"%+.0f dB", dv);
            out.labels.push_back({ x0 - 11.5f, yy - 0.3f, 0.82f, EdaLayerId::SchLabel, dl });
        }
        for (int gx = 0; gx <= 4; ++gx) {
            float xx = x0 + (w * static_cast<float>(gx)) / 4.0f;
            double fv = minF + ((maxF - minF) * static_cast<double>(gx)) / 4.0;
            wchar_t fl[48] = {};
            swprintf_s(fl, L"%.3g %ls", fv, freqUnit.c_str());
            out.labels.push_back({ xx - 3.2f, y0 - 2.8f, 0.82f, EdaLayerId::SchLabel, fl });
        }

        auto mapX = [&](double f) -> float {
            return x0 + static_cast<float>(std::clamp((f - minF) / (maxF - minF), 0.0, 1.0)) * w;
        };
        auto mapY = [&](double db) -> float {
            return y0 + static_cast<float>(std::clamp((db - dbMin) / dbSpan, 0.0, 1.0)) * h;
        };

        for (size_t i = 1; i < samples.size(); ++i) {
            const auto& p0 = samples[i - 1];
            const auto& p1 = samples[i];
            float sx0 = mapX(p0.freq), sx1 = mapX(p1.freq);
            out.traces.push_back({ sx0, mapY(p0.c0Db), sx1, mapY(p1.c0Db), 0.34f, EdaLayerId::TopCopper, 1 });
            out.traces.push_back({ sx0, mapY(p0.c1Db), sx1, mapY(p1.c1Db), 0.36f, EdaLayerId::SchWire, 2 });
            if (nTraces >= 3) {
                out.traces.push_back({ sx0, mapY(p0.c2Db), sx1, mapY(p1.c2Db), 0.24f, EdaLayerId::BottomCopper, 3 });
            }
            if (nTraces >= 4) {
                out.traces.push_back({ sx0, mapY(p0.c3Db), sx1, mapY(p1.c3Db), 0.22f, EdaLayerId::InnerCopper, 4 });
            }
        }

        const wchar_t* cNamesDiff[4] = { L"SDD11 (Diff Return Loss)", L"SDD21 (Diff Insertion)", L"S21 (Single-Ended)", L"S31 (NEXT Crosstalk)" };
        const wchar_t* cNames3P[3]   = { L"S11 (Return Loss)", L"S21 (Port 1->2)", L"S31 (Port 1->3)" };
        out.labels.push_back({ x0, y0 + h + 1.8f, 0.96f, EdaLayerId::SchLabel,
            (nPorts >= 4) ? L"Traces: SDD21 (Green), SDD11 (Red), S21 (Blue), S31 (Purple)"
                          : L"Traces: S21 (Green), S11 (Red), S31 (Blue)" });

        for (int ci = 0; ci < nTraces; ++ci) {
            EdaComponent3D cc;
            cc.refDes = (nPorts >= 4) ? cNamesDiff[ci] : cNames3P[ci];
            cc.value = fmtName;
            cc.footprint = L"TOUCHSTONE_PORT";
            size_t mid = samples.size() / 2;
            double mv = (ci == 0) ? samples[mid].c0Db : (ci == 1 ? samples[mid].c1Db : (ci == 2 ? samples[mid].c2Db : samples[mid].c3Db));
            cc.x = mapX(samples[mid].freq);
            cc.y = mapY(mv);
            cc.widthMm = 3.0f; cc.lengthMm = 2.0f;
            out.components.push_back(std::move(cc));
        }

        out.netCount = static_cast<size_t>(nPorts);
        return !out.traces.empty();
    }

    // #66. SEMI P39 OASIS Wafer Mask Layout (.oas — "%SEMI-OASIS\r\n")
    if (ext == L".oas" || (size >= 13 && std::memcmp(data, "%SEMI-OASIS\r\n", 13) == 0)) {
        if (size < 13 || std::memcmp(data, "%SEMI-OASIS\r\n", 13) != 0) return false;
        out.kind = EdaDocKind::KiCadPcb;
        out.formatTitle = L"SEMI P39 OASIS Silicon IC Mask (.oas)";

        struct OasisPt { int64_t x = 0, y = 0; };
        struct OasisShape {
            uint8_t kind = 0; // 1=rect, 2=poly, 3=path, 4=circle, 5=text, 6=place
            uint64_t layer = 0;
            int64_t x = 0, y = 0;
            uint64_t w = 0, h = 0;
            std::vector<OasisPt> pts; // relative to (x, y)
            std::string str;
            double mag = 1.0, deg = 0.0;
            bool flip = false;
            std::vector<OasisPt> repOffsets; // includes (0,0)
        };
        struct OasisCell {
            std::string name;
            std::vector<OasisShape> shapes;
        };
        struct OasisModal {
            bool xyAbs = true;
            uint64_t layer = 0, datatype = 0, textLayer = 0, textType = 0;
            int64_t geomX = 0, geomY = 0, textX = 0, textY = 0, placeX = 0, placeY = 0;
            uint64_t rectW = 0, rectH = 0, circleR = 0, pathHalfW = 0;
            int64_t pathStartExt = 0, pathEndExt = 0;
            uint64_t ctrapType = 0, trapW = 0, trapH = 0;
            std::vector<OasisPt> polyPts, pathPts;
            std::vector<OasisPt> modalRep = { { 0, 0 } };
            std::string textStr, placeCell;
        };

        struct OasisParser {
            bool ok = true;
            bool sawStart = false;
            bool stopStream = false;
            double unit = 1000.0;
            uint64_t nextCellRef = 0, nextTextRef = 0;
            std::vector<std::pair<uint64_t, std::string>> cellNames;
            std::vector<std::pair<uint64_t, std::string>> textStrings;
            std::vector<OasisCell> cells;
            OasisModal mod;

            uint8_t RdByte(const uint8_t* b, size_t n, size_t& p) {
                if (p >= n) { ok = false; return 0; }
                return b[p++];
            }
            uint64_t RdU(const uint8_t* b, size_t n, size_t& p) {
                uint64_t v = 0;
                int sh = 0;
                while (p < n && sh < 64) {
                    uint8_t c = b[p++];
                    v |= static_cast<uint64_t>(c & 0x7Fu) << sh;
                    if ((c & 0x80u) == 0) return v;
                    sh += 7;
                }
                ok = false;
                return 0;
            }
            int64_t RdS(const uint8_t* b, size_t n, size_t& p) {
                uint64_t u = RdU(b, n, p);
                int64_t m = static_cast<int64_t>(u >> 1);
                return (u & 1ULL) ? -m : m;
            }
            std::string RdStr(const uint8_t* b, size_t n, size_t& p) {
                uint64_t len = RdU(b, n, p);
                if (!ok || len > n - p || len > 1048576ULL) { ok = false; return {}; }
                std::string s(reinterpret_cast<const char*>(b + p), static_cast<size_t>(len));
                p += static_cast<size_t>(len);
                return s;
            }
            double RdReal(const uint8_t* b, size_t n, size_t& p) {
                uint64_t tp = RdU(b, n, p);
                if (!ok) return 1.0;
                switch (tp) {
                case 0: return static_cast<double>(RdU(b, n, p));
                case 1: return -static_cast<double>(RdU(b, n, p));
                case 2: { uint64_t d = RdU(b, n, p); return (d != 0) ? (1.0 / static_cast<double>(d)) : 1.0; }
                case 3: { uint64_t d = RdU(b, n, p); return (d != 0) ? (-1.0 / static_cast<double>(d)) : -1.0; }
                case 4: {
                    uint64_t num = RdU(b, n, p), den = RdU(b, n, p);
                    return (den != 0) ? (static_cast<double>(num) / static_cast<double>(den)) : 1.0;
                }
                case 5: {
                    uint64_t num = RdU(b, n, p), den = RdU(b, n, p);
                    return (den != 0) ? (-static_cast<double>(num) / static_cast<double>(den)) : -1.0;
                }
                case 6: {
                    if (p + 4 > n) { ok = false; return 1.0; }
                    uint32_t bits = DetailEda200::RdU32LE(b + p); p += 4;
                    float fv = 1.0f; std::memcpy(&fv, &bits, 4);
                    return static_cast<double>(fv);
                }
                case 7: {
                    if (p + 8 > n) { ok = false; return 1.0; }
                    uint64_t bits = DetailEda200::RdU64LE(b + p); p += 8;
                    double dv = 1.0; std::memcpy(&dv, &bits, 8);
                    return dv;
                }
                default: ok = false; return 1.0;
                }
            }
            void SkipInterval(const uint8_t* b, size_t n, size_t& p) {
                uint64_t it = RdU(b, n, p);
                if (it == 0) return;
                if (it >= 1 && it <= 3) { RdU(b, n, p); return; }
                if (it == 4) { RdU(b, n, p); RdU(b, n, p); return; }
                ok = false;
            }
            OasisPt DirMag(uint64_t dir, int64_t mag) {
                switch (dir & 7ULL) {
                case 0: return {  mag,    0 }; // E
                case 1: return {    0,  mag }; // N
                case 2: return { -mag,    0 }; // W
                case 3: return {    0, -mag }; // S
                case 4: return {  mag,  mag }; // NE
                case 5: return { -mag,  mag }; // NW
                case 6: return { -mag, -mag }; // SW
                default:return {  mag, -mag }; // SE
                }
            }
            OasisPt RdGDelta(const uint8_t* b, size_t n, size_t& p) {
                uint64_t u = RdU(b, n, p);
                if ((u & 1ULL) == 0) {
                    return DirMag((u >> 1) & 7ULL, static_cast<int64_t>(u >> 4));
                }
                int64_t xm = static_cast<int64_t>(u >> 2);
                int64_t dx = (u & 2ULL) ? -xm : xm;
                int64_t dy = RdS(b, n, p);
                return { dx, dy };
            }
            void RdPointList(const uint8_t* b, size_t n, size_t& p, bool isPoly, std::vector<OasisPt>& outPts) {
                uint64_t ptype = RdU(b, n, p);
                uint64_t vcnt = RdU(b, n, p);
                if (!ok || ptype > 5 || vcnt == 0 || vcnt > 65536ULL) { ok = false; return; }
                outPts.clear();
                outPts.reserve(static_cast<size_t>(vcnt) + 2);
                outPts.push_back({ 0, 0 });
                int64_t cx = 0, cy = 0;
                int64_t prevDx = 0, prevDy = 0;
                for (uint64_t i = 0; i < vcnt && ok; ++i) {
                    int64_t dx = 0, dy = 0;
                    if (ptype == 0) {
                        int64_t d = RdS(b, n, p);
                        if ((i & 1ULL) == 0) dx = d; else dy = d;
                    } else if (ptype == 1) {
                        int64_t d = RdS(b, n, p);
                        if ((i & 1ULL) == 0) dy = d; else dx = d;
                    } else if (ptype == 2) {
                        uint64_t u = RdU(b, n, p);
                        OasisPt dm = DirMag(u & 3ULL, static_cast<int64_t>(u >> 2));
                        dx = dm.x; dy = dm.y;
                    } else if (ptype == 3) {
                        uint64_t u = RdU(b, n, p);
                        OasisPt dm = DirMag(u & 7ULL, static_cast<int64_t>(u >> 3));
                        dx = dm.x; dy = dm.y;
                    } else if (ptype == 4) {
                        OasisPt dm = RdGDelta(b, n, p);
                        dx = dm.x; dy = dm.y;
                    } else {
                        OasisPt dm = RdGDelta(b, n, p);
                        prevDx += dm.x; prevDy += dm.y;
                        dx = prevDx; dy = prevDy;
                    }
                    cx += dx; cy += dy;
                    outPts.push_back({ cx, cy });
                }
                if (isPoly && (ptype == 0 || ptype == 1) && cx != 0 && cy != 0) {
                    bool nextIsHoriz = (ptype == 0) ? ((vcnt & 1ULL) == 0) : ((vcnt & 1ULL) != 0);
                    if (nextIsHoriz) outPts.push_back({ 0, cy });
                    else outPts.push_back({ cx, 0 });
                }
            }
            void RdRepetition(const uint8_t* b, size_t n, size_t& p, std::vector<OasisPt>& rep) {
                uint64_t rtype = RdU(b, n, p);
                if (!ok) return;
                if (rtype == 0) return; // reuse modalRep
                rep.clear();
                constexpr size_t kMaxRep = 512;
                if (rtype == 1) {
                    uint64_t xd = RdU(b, n, p) + 2ULL;
                    uint64_t yd = RdU(b, n, p) + 2ULL;
                    int64_t xs = static_cast<int64_t>(RdU(b, n, p));
                    int64_t ys = static_cast<int64_t>(RdU(b, n, p));
                    for (uint64_t iy = 0; iy < yd && rep.size() < kMaxRep; ++iy)
                        for (uint64_t ix = 0; ix < xd && rep.size() < kMaxRep; ++ix)
                            rep.push_back({ static_cast<int64_t>(ix) * xs, static_cast<int64_t>(iy) * ys });
                } else if (rtype == 2) {
                    uint64_t xd = RdU(b, n, p) + 2ULL;
                    int64_t xs = static_cast<int64_t>(RdU(b, n, p));
                    for (uint64_t ix = 0; ix < xd && rep.size() < kMaxRep; ++ix)
                        rep.push_back({ static_cast<int64_t>(ix) * xs, 0 });
                } else if (rtype == 3) {
                    uint64_t yd = RdU(b, n, p) + 2ULL;
                    int64_t ys = static_cast<int64_t>(RdU(b, n, p));
                    for (uint64_t iy = 0; iy < yd && rep.size() < kMaxRep; ++iy)
                        rep.push_back({ 0, static_cast<int64_t>(iy) * ys });
                } else if (rtype >= 4 && rtype <= 7) {
                    bool isX = (rtype == 4 || rtype == 5);
                    bool hasGrid = (rtype == 5 || rtype == 7);
                    uint64_t dim = RdU(b, n, p) + 2ULL;
                    int64_t gf = hasGrid ? static_cast<int64_t>(RdU(b, n, p)) : 1;
                    if (dim > 65536ULL) { ok = false; return; }
                    int64_t acc = 0;
                    rep.push_back({ 0, 0 });
                    for (uint64_t i = 1; i < dim && ok; ++i) {
                        acc += static_cast<int64_t>(RdU(b, n, p)) * gf;
                        if (rep.size() < kMaxRep) rep.push_back(isX ? OasisPt{ acc, 0 } : OasisPt{ 0, acc });
                    }
                } else if (rtype == 8) {
                    uint64_t nd = RdU(b, n, p) + 2ULL;
                    uint64_t md = RdU(b, n, p) + 2ULL;
                    OasisPt nv = RdGDelta(b, n, p);
                    OasisPt mv = RdGDelta(b, n, p);
                    for (uint64_t im = 0; im < md && rep.size() < kMaxRep; ++im)
                        for (uint64_t in = 0; in < nd && rep.size() < kMaxRep; ++in)
                            rep.push_back({ static_cast<int64_t>(in) * nv.x + static_cast<int64_t>(im) * mv.x,
                                            static_cast<int64_t>(in) * nv.y + static_cast<int64_t>(im) * mv.y });
                } else if (rtype == 9) {
                    uint64_t dim = RdU(b, n, p) + 2ULL;
                    OasisPt dv = RdGDelta(b, n, p);
                    for (uint64_t i = 0; i < dim && rep.size() < kMaxRep; ++i)
                        rep.push_back({ static_cast<int64_t>(i) * dv.x, static_cast<int64_t>(i) * dv.y });
                } else if (rtype == 10 || rtype == 11) {
                    uint64_t cnt = RdU(b, n, p) + 2ULL;
                    int64_t gf = (rtype == 11) ? static_cast<int64_t>(RdU(b, n, p)) : 1;
                    if (cnt > 65536ULL) { ok = false; return; }
                    int64_t ax = 0, ay = 0;
                    rep.push_back({ 0, 0 });
                    for (uint64_t i = 1; i < cnt && ok; ++i) {
                        OasisPt dv = RdGDelta(b, n, p);
                        ax += dv.x * gf; ay += dv.y * gf;
                        if (rep.size() < kMaxRep) rep.push_back({ ax, ay });
                    }
                } else {
                    ok = false;
                }
                if (rep.empty()) rep.push_back({ 0, 0 });
            }
            std::string LookupMap(const std::vector<std::pair<uint64_t, std::string>>& tbl, uint64_t ref, const char* pfx) {
                for (const auto& kv : tbl) if (kv.first == ref) return kv.second;
                return std::string(pfx) + std::to_string(ref);
            }
            void SkipPropValue(const uint8_t* b, size_t n, size_t& p) {
                uint64_t vt = RdU(b, n, p);
                if (!ok) return;
                if (vt <= 3 || vt == 8 || vt == 13 || vt == 14 || vt == 15) { RdU(b, n, p); }
                else if (vt == 4 || vt == 5) { RdU(b, n, p); RdU(b, n, p); }
                else if (vt == 6) { if (p + 4 > n) ok = false; else p += 4; }
                else if (vt == 7) { if (p + 8 > n) ok = false; else p += 8; }
                else if (vt == 9) { RdS(b, n, p); }
                else if (vt >= 10 && vt <= 12) { RdStr(b, n, p); }
                else { ok = false; }
            }

            void ParseStream(const uint8_t* b, size_t n, size_t startPos, int depth) {
                size_t p = startPos;
                while (p < n && ok && !stopStream && cells.size() < 4096) {
                    uint8_t recId = b[p++];
                    if (recId == 0 || recId == 29) continue; // PAD / repeat PROPERTY
                    if (recId == 2) { stopStream = true; break; } // END
                    if (recId == 1) { // START
                        std::string ver = RdStr(b, n, p);
                        double u = RdReal(b, n, p);
                        uint64_t offFlag = RdU(b, n, p);
                        if (!ok || ver.empty() || ver[0] != '1' || !(u > 0.0) || !std::isfinite(u)) {
                            ok = false; break;
                        }
                        unit = u;
                        if (offFlag == 0) {
                            for (int k = 0; k < 12 && ok; ++k) RdU(b, n, p);
                        }
                        sawStart = true;
                        continue;
                    }
                    if (recId == 3) { cellNames.push_back({ nextCellRef++, RdStr(b, n, p) }); continue; }
                    if (recId == 4) { std::string s = RdStr(b, n, p); uint64_t r = RdU(b, n, p); cellNames.push_back({ r, s }); continue; }
                    if (recId == 5) { textStrings.push_back({ nextTextRef++, RdStr(b, n, p) }); continue; }
                    if (recId == 6) { std::string s = RdStr(b, n, p); uint64_t r = RdU(b, n, p); textStrings.push_back({ r, s }); continue; }
                    if (recId == 7 || recId == 9) { RdStr(b, n, p); continue; }
                    if (recId == 8 || recId == 10) { RdStr(b, n, p); RdU(b, n, p); continue; }
                    if (recId == 11 || recId == 12) { RdStr(b, n, p); SkipInterval(b, n, p); SkipInterval(b, n, p); continue; }
                    if (recId == 13 || recId == 14) { // CELL
                        std::string cname = (recId == 13) ? LookupMap(cellNames, RdU(b, n, p), "CELL_") : RdStr(b, n, p);
                        cells.push_back({ std::move(cname), {} });
                        mod = OasisModal{};
                        continue;
                    }
                    if (recId == 15) { mod.xyAbs = true; continue; }
                    if (recId == 16) { mod.xyAbs = false; continue; }
                    if (recId == 28) { // PROPERTY
                        uint8_t info = RdByte(b, n, p);
                        if (info & 0x04u) {
                            if (info & 0x02u) RdU(b, n, p);
                            else RdStr(b, n, p);
                        }
                        if ((info & 0x08u) == 0) {
                            uint64_t cnt = (info >> 4) & 0x0Fu;
                            if (cnt == 15ULL) cnt = RdU(b, n, p);
                            if (cnt > 65536ULL) { ok = false; break; }
                            for (uint64_t k = 0; k < cnt && ok; ++k) SkipPropValue(b, n, p);
                        }
                        continue;
                    }
                    if (recId == 30) { RdU(b, n, p); RdStr(b, n, p); continue; }
                    if (recId == 31) { RdU(b, n, p); RdStr(b, n, p); RdU(b, n, p); continue; }
                    if (recId == 32) { RdU(b, n, p); RdStr(b, n, p); continue; }
                    if (recId == 33) {
                        uint8_t info = RdByte(b, n, p);
                        RdU(b, n, p);
                        if (info & 0x01u) mod.layer = RdU(b, n, p);
                        if (info & 0x02u) mod.datatype = RdU(b, n, p);
                        RdStr(b, n, p);
                        if (info & 0x10u) { int64_t d = RdS(b, n, p); mod.geomX = mod.xyAbs ? d : (mod.geomX + d); }
                        if (info & 0x08u) { int64_t d = RdS(b, n, p); mod.geomY = mod.xyAbs ? d : (mod.geomY + d); }
                        if (info & 0x04u) RdRepetition(b, n, p, mod.modalRep);
                        continue;
                    }
                    if (recId == 34) { // CBLOCK (RFC 1951 raw Deflate)
                        uint64_t compType = RdU(b, n, p);
                        uint64_t uncompSz = RdU(b, n, p);
                        uint64_t compSz   = RdU(b, n, p);
                        if (!ok || depth > 3 || compType != 0 || compSz > n - p || uncompSz > 32 * 1024 * 1024ULL) {
                            ok = false; break;
                        }
                        std::vector<uint8_t> cblockBuf;
                        if (!ZipDeflate::InflateRaw(b + p, static_cast<size_t>(compSz), cblockBuf, static_cast<size_t>(uncompSz) + 64)) {
                            ok = false; break;
                        }
                        p += static_cast<size_t>(compSz);
                        ParseStream(cblockBuf.data(), cblockBuf.size(), 0, depth + 1);
                        continue;
                    }

                    if (cells.empty()) cells.push_back({ "TOP", {} });
                    auto& curShapes = cells.back().shapes;

                    if (recId == 17 || recId == 18) { // PLACEMENT
                        uint8_t info = RdByte(b, n, p);
                        if (info & 0x80u) {
                            mod.placeCell = (info & 0x40u) ? LookupMap(cellNames, RdU(b, n, p), "CELL_") : RdStr(b, n, p);
                        }
                        double mag = 1.0, deg = 0.0;
                        bool flip = (info & 0x01u) != 0;
                        bool hasRep = false;
                        if (recId == 17) {
                            deg = static_cast<double>(((info >> 1) & 3u) * 90u);
                            if (info & 0x20u) { int64_t d = RdS(b, n, p); mod.placeX = mod.xyAbs ? d : (mod.placeX + d); }
                            if (info & 0x10u) { int64_t d = RdS(b, n, p); mod.placeY = mod.xyAbs ? d : (mod.placeY + d); }
                            hasRep = (info & 0x08u) != 0;
                        } else {
                            if (info & 0x20u) mag = RdReal(b, n, p);
                            if (info & 0x10u) deg = RdReal(b, n, p);
                            if (info & 0x08u) { int64_t d = RdS(b, n, p); mod.placeX = mod.xyAbs ? d : (mod.placeX + d); }
                            if (info & 0x04u) { int64_t d = RdS(b, n, p); mod.placeY = mod.xyAbs ? d : (mod.placeY + d); }
                            hasRep = (info & 0x02u) != 0;
                        }
                        std::vector<OasisPt> reps = { { 0, 0 } };
                        if (hasRep) { RdRepetition(b, n, p, mod.modalRep); reps = mod.modalRep; }
                        if (ok && curShapes.size() < 16384) {
                            OasisShape sh;
                            sh.kind = 6;
                            sh.x = mod.placeX; sh.y = mod.placeY;
                            sh.str = mod.placeCell;
                            sh.mag = mag; sh.deg = deg; sh.flip = flip;
                            sh.repOffsets = std::move(reps);
                            curShapes.push_back(std::move(sh));
                        }
                    } else if (recId == 19) { // TEXT
                        uint8_t info = RdByte(b, n, p);
                        if (info & 0x40u) {
                            mod.textStr = (info & 0x20u) ? LookupMap(textStrings, RdU(b, n, p), "TXT_") : RdStr(b, n, p);
                        }
                        if (info & 0x01u) mod.textLayer = RdU(b, n, p);
                        if (info & 0x02u) mod.textType  = RdU(b, n, p);
                        if (info & 0x10u) { int64_t d = RdS(b, n, p); mod.textX = mod.xyAbs ? d : (mod.textX + d); }
                        if (info & 0x08u) { int64_t d = RdS(b, n, p); mod.textY = mod.xyAbs ? d : (mod.textY + d); }
                        std::vector<OasisPt> reps = { { 0, 0 } };
                        if (info & 0x04u) { RdRepetition(b, n, p, mod.modalRep); reps = mod.modalRep; }
                        if (ok && !mod.textStr.empty() && curShapes.size() < 16384) {
                            OasisShape sh;
                            sh.kind = 5;
                            sh.layer = mod.textLayer;
                            sh.x = mod.textX; sh.y = mod.textY;
                            sh.str = mod.textStr;
                            sh.repOffsets = std::move(reps);
                            curShapes.push_back(std::move(sh));
                        }
                    } else if (recId == 20) { // RECTANGLE
                        uint8_t info = RdByte(b, n, p);
                        if (info & 0x01u) mod.layer    = RdU(b, n, p);
                        if (info & 0x02u) mod.datatype = RdU(b, n, p);
                        if (info & 0x40u) mod.rectW    = RdU(b, n, p);
                        if (info & 0x80u) mod.rectH    = mod.rectW;
                        else if (info & 0x20u) mod.rectH = RdU(b, n, p);
                        if (info & 0x10u) { int64_t d = RdS(b, n, p); mod.geomX = mod.xyAbs ? d : (mod.geomX + d); }
                        if (info & 0x08u) { int64_t d = RdS(b, n, p); mod.geomY = mod.xyAbs ? d : (mod.geomY + d); }
                        std::vector<OasisPt> reps = { { 0, 0 } };
                        if (info & 0x04u) { RdRepetition(b, n, p, mod.modalRep); reps = mod.modalRep; }
                        if (ok && curShapes.size() < 16384) {
                            OasisShape sh;
                            sh.kind = 1; sh.layer = mod.layer;
                            sh.x = mod.geomX; sh.y = mod.geomY;
                            sh.w = mod.rectW; sh.h = mod.rectH;
                            sh.repOffsets = std::move(reps);
                            curShapes.push_back(std::move(sh));
                        }
                    } else if (recId == 21) { // POLYGON
                        uint8_t info = RdByte(b, n, p);
                        if (info & 0x01u) mod.layer    = RdU(b, n, p);
                        if (info & 0x02u) mod.datatype = RdU(b, n, p);
                        if (info & 0x20u) RdPointList(b, n, p, true, mod.polyPts);
                        if (info & 0x10u) { int64_t d = RdS(b, n, p); mod.geomX = mod.xyAbs ? d : (mod.geomX + d); }
                        if (info & 0x08u) { int64_t d = RdS(b, n, p); mod.geomY = mod.xyAbs ? d : (mod.geomY + d); }
                        std::vector<OasisPt> reps = { { 0, 0 } };
                        if (info & 0x04u) { RdRepetition(b, n, p, mod.modalRep); reps = mod.modalRep; }
                        if (ok && mod.polyPts.size() >= 3 && curShapes.size() < 16384) {
                            OasisShape sh;
                            sh.kind = 2; sh.layer = mod.layer;
                            sh.x = mod.geomX; sh.y = mod.geomY;
                            sh.pts = mod.polyPts;
                            sh.repOffsets = std::move(reps);
                            curShapes.push_back(std::move(sh));
                        }
                    } else if (recId == 22) { // PATH
                        uint8_t info = RdByte(b, n, p);
                        if (info & 0x01u) mod.layer     = RdU(b, n, p);
                        if (info & 0x02u) mod.datatype  = RdU(b, n, p);
                        if (info & 0x40u) mod.pathHalfW = RdU(b, n, p);
                        if (info & 0x80u) {
                            uint8_t es = RdByte(b, n, p);
                            uint8_t sSch = (es >> 2) & 3u, eSch = es & 3u;
                            if (sSch == 3u) mod.pathStartExt = RdS(b, n, p);
                            else if (sSch == 1u) mod.pathStartExt = 0;
                            else if (sSch == 2u) mod.pathStartExt = static_cast<int64_t>(mod.pathHalfW);
                            if (eSch == 3u) mod.pathEndExt = RdS(b, n, p);
                            else if (eSch == 1u) mod.pathEndExt = 0;
                            else if (eSch == 2u) mod.pathEndExt = static_cast<int64_t>(mod.pathHalfW);
                        }
                        if (info & 0x20u) RdPointList(b, n, p, false, mod.pathPts);
                        if (info & 0x10u) { int64_t d = RdS(b, n, p); mod.geomX = mod.xyAbs ? d : (mod.geomX + d); }
                        if (info & 0x08u) { int64_t d = RdS(b, n, p); mod.geomY = mod.xyAbs ? d : (mod.geomY + d); }
                        std::vector<OasisPt> reps = { { 0, 0 } };
                        if (info & 0x04u) { RdRepetition(b, n, p, mod.modalRep); reps = mod.modalRep; }
                        if (ok && mod.pathPts.size() >= 2 && curShapes.size() < 16384) {
                            OasisShape sh;
                            sh.kind = 3; sh.layer = mod.layer;
                            sh.x = mod.geomX; sh.y = mod.geomY;
                            sh.w = mod.pathHalfW * 2ULL;
                            sh.pts = mod.pathPts;
                            sh.repOffsets = std::move(reps);
                            curShapes.push_back(std::move(sh));
                        }
                    } else if (recId == 23 || recId == 24 || recId == 25) { // TRAPEZOID / CTRAPEZOID
                        uint8_t info = RdByte(b, n, p);
                        if (info & 0x01u) mod.layer    = RdU(b, n, p);
                        if (info & 0x02u) mod.datatype = RdU(b, n, p);
                        int64_t da = 0, db = 0;
                        bool vert = false;
                        if (recId == 25) {
                            if (info & 0x80u) mod.ctrapType = RdU(b, n, p);
                            if (info & 0x40u) mod.trapW = RdU(b, n, p);
                            if (info & 0x20u) mod.trapH = RdU(b, n, p);
                            if (mod.ctrapType >= 16 && mod.ctrapType <= 19) mod.trapH = mod.trapW;
                            else if (mod.ctrapType >= 20 && mod.ctrapType <= 23) mod.trapW = mod.trapH;
                            else if (mod.ctrapType == 24) mod.trapH = mod.trapW;
                        } else {
                            vert = (info & 0x80u) != 0;
                            if (info & 0x40u) mod.trapW = RdU(b, n, p);
                            if (info & 0x20u) mod.trapH = RdU(b, n, p);
                            da = RdS(b, n, p);
                            if (recId == 23) db = RdS(b, n, p);
                        }
                        if (info & 0x10u) { int64_t d = RdS(b, n, p); mod.geomX = mod.xyAbs ? d : (mod.geomX + d); }
                        if (info & 0x08u) { int64_t d = RdS(b, n, p); mod.geomY = mod.xyAbs ? d : (mod.geomY + d); }
                        std::vector<OasisPt> reps = { { 0, 0 } };
                        if (info & 0x04u) { RdRepetition(b, n, p, mod.modalRep); reps = mod.modalRep; }
                        if (ok && curShapes.size() < 16384) {
                            int64_t tw = static_cast<int64_t>(mod.trapW), th = static_cast<int64_t>(mod.trapH);
                            OasisShape sh;
                            sh.kind = 2; sh.layer = mod.layer;
                            sh.x = mod.geomX; sh.y = mod.geomY;
                            if (recId == 25) {
                                sh.pts = { { 0, 0 }, { tw, 0 }, { tw, th }, { 0, th } };
                            } else if (!vert) {
                                int64_t x0b = std::max<int64_t>(0, -da), x0t = std::max<int64_t>(0, da);
                                int64_t x1b = tw - std::max<int64_t>(0, db), x1t = tw + std::min<int64_t>(0, db);
                                sh.pts = { { x0b, 0 }, { x1b, 0 }, { x1t, th }, { x0t, th } };
                            } else {
                                int64_t y0l = std::max<int64_t>(0, -da), y0r = std::max<int64_t>(0, da);
                                int64_t y1l = th - std::max<int64_t>(0, db), y1r = th + std::min<int64_t>(0, db);
                                sh.pts = { { 0, y0l }, { tw, y0r }, { tw, y1r }, { 0, y1l } };
                            }
                            sh.repOffsets = std::move(reps);
                            curShapes.push_back(std::move(sh));
                        }
                    } else if (recId == 26) { // CIRCLE
                        uint8_t info = RdByte(b, n, p);
                        if (info & 0x01u) mod.layer    = RdU(b, n, p);
                        if (info & 0x02u) mod.datatype = RdU(b, n, p);
                        if (info & 0x20u) mod.circleR  = RdU(b, n, p);
                        if (info & 0x10u) { int64_t d = RdS(b, n, p); mod.geomX = mod.xyAbs ? d : (mod.geomX + d); }
                        if (info & 0x08u) { int64_t d = RdS(b, n, p); mod.geomY = mod.xyAbs ? d : (mod.geomY + d); }
                        std::vector<OasisPt> reps = { { 0, 0 } };
                        if (info & 0x04u) { RdRepetition(b, n, p, mod.modalRep); reps = mod.modalRep; }
                        if (ok && mod.circleR > 0 && curShapes.size() < 16384) {
                            OasisShape sh;
                            sh.kind = 4; sh.layer = mod.layer;
                            sh.x = mod.geomX; sh.y = mod.geomY;
                            sh.w = mod.circleR;
                            sh.repOffsets = std::move(reps);
                            curShapes.push_back(std::move(sh));
                        }
                    } else {
                        ok = false;
                        break;
                    }
                }
            }
        };

        OasisParser parser;
        parser.ParseStream(data, size, 13, 0);
        if (!parser.ok || !parser.sawStart || parser.cells.empty()) return false;

        double toUm = 1.0 / parser.unit;
        auto mapLayer = [](uint64_t lyr) -> EdaLayerId {
            switch (lyr & 3ULL) {
            case 1: return EdaLayerId::TopCopper;
            case 2: return EdaLayerId::BottomCopper;
            case 3: return EdaLayerId::TopSilk;
            default: return EdaLayerId::InnerCopper;
            }
        };
        auto findCellIdx = [&](const std::string& nm) -> int {
            for (size_t i = 0; i < parser.cells.size(); ++i) {
                if (parser.cells[i].name == nm) return static_cast<int>(i);
            }
            return -1;
        };

        // Determine root cells (cells not instantiated via PLACEMENT by any other cell)
        std::vector<bool> referenced(parser.cells.size(), false);
        for (const auto& c : parser.cells) {
            for (const auto& s : c.shapes) {
                if (s.kind == 6) {
                    int idx = findCellIdx(s.str);
                    if (idx >= 0) referenced[static_cast<size_t>(idx)] = true;
                }
            }
        }

        struct EmitCtx {
            const OasisParser& P;
            ParsedEdaBoard& out;
            double toUm;
            void EmitCell(int cellIdx, double oxUm, double oyUm, double scale, double rotDeg, bool flipY, int depth) {
                if (depth > 8 || out.traces.size() >= 25000) return;
                const auto& cell = P.cells[static_cast<size_t>(cellIdx)];
                double rad = rotDeg * 3.14159265358979323846 / 180.0;
                double cs = std::cos(rad), sn = std::sin(rad);
                auto xform = [&](double lxUm, double lyUm) -> std::pair<float, float> {
                    double sy = flipY ? -lyUm : lyUm;
                    double sx = lxUm * scale, syy = sy * scale;
                    double rx = oxUm + sx * cs - syy * sn;
                    double ry = oyUm + sx * sn + syy * cs;
                    return { static_cast<float>(rx), static_cast<float>(ry) };
                };
                auto mapL = [](uint64_t lyr) -> EdaLayerId {
                    switch (lyr & 3ULL) {
                    case 1: return EdaLayerId::TopCopper;
                    case 2: return EdaLayerId::BottomCopper;
                    case 3: return EdaLayerId::TopSilk;
                    default: return EdaLayerId::InnerCopper;
                    }
                };

                for (const auto& sh : cell.shapes) {
                    if (out.traces.size() >= 25000) break;
                    EdaLayerId lid = mapL(sh.layer);
                    for (const auto& ro : sh.repOffsets) {
                        if (out.traces.size() >= 25000) break;
                        double bx = static_cast<double>(sh.x + ro.x) * toUm;
                        double by = static_cast<double>(sh.y + ro.y) * toUm;
                        if (sh.kind == 1) { // RECTANGLE
                            double rw = static_cast<double>(sh.w) * toUm;
                            double rh = static_cast<double>(sh.h) * toUm;
                            if (rw <= 0.0 && rh <= 0.0) continue;
                            auto p0 = xform(bx, by);
                            auto p1 = xform(bx + rw, by);
                            auto p2 = xform(bx + rw, by + rh);
                            auto p3 = xform(bx, by + rh);
                            out.traces.push_back({ p0.first, p0.second, p1.first, p1.second, 0.18f, lid, static_cast<int>(sh.layer + 1) });
                            out.traces.push_back({ p1.first, p1.second, p2.first, p2.second, 0.18f, lid, static_cast<int>(sh.layer + 1) });
                            out.traces.push_back({ p2.first, p2.second, p3.first, p3.second, 0.18f, lid, static_cast<int>(sh.layer + 1) });
                            out.traces.push_back({ p3.first, p3.second, p0.first, p0.second, 0.18f, lid, static_cast<int>(sh.layer + 1) });
                        } else if (sh.kind == 2 && sh.pts.size() >= 3) { // POLYGON / TRAPEZOID
                            size_t m = sh.pts.size();
                            for (size_t k = 0; k < m; ++k) {
                                size_t kn = (k + 1) % m;
                                auto a = xform(bx + static_cast<double>(sh.pts[k].x) * toUm, by + static_cast<double>(sh.pts[k].y) * toUm);
                                auto b = xform(bx + static_cast<double>(sh.pts[kn].x) * toUm, by + static_cast<double>(sh.pts[kn].y) * toUm);
                                out.traces.push_back({ a.first, a.second, b.first, b.second, 0.18f, lid, static_cast<int>(sh.layer + 1) });
                            }
                        } else if (sh.kind == 3 && sh.pts.size() >= 2) { // PATH
                            float pw = std::clamp(static_cast<float>(static_cast<double>(sh.w) * toUm * scale), 0.12f, 4.0f);
                            for (size_t k = 1; k < sh.pts.size(); ++k) {
                                auto a = xform(bx + static_cast<double>(sh.pts[k - 1].x) * toUm, by + static_cast<double>(sh.pts[k - 1].y) * toUm);
                                auto b = xform(bx + static_cast<double>(sh.pts[k].x) * toUm,     by + static_cast<double>(sh.pts[k].y) * toUm);
                                out.traces.push_back({ a.first, a.second, b.first, b.second, pw, lid, static_cast<int>(sh.layer + 1) });
                            }
                        } else if (sh.kind == 4 && out.circles.size() < 8192) { // CIRCLE
                            auto c = xform(bx, by);
                            float r = std::max(0.05f, static_cast<float>(static_cast<double>(sh.w) * toUm * scale));
                            out.circles.push_back({ c.first, c.second, r, std::min(0.20f, r * 0.35f), false, lid });
                        } else if (sh.kind == 5 && out.labels.size() < 4096) { // TEXT
                            auto c = xform(bx, by);
                            out.labels.push_back({ c.first, c.second, 0.85f, EdaLayerId::TopSilk, PathUtils::Utf8ToWide(sh.str) });
                        } else if (sh.kind == 6) { // PLACEMENT
                            auto c = xform(bx, by);
                            int subIdx = -1;
                            for (size_t ci = 0; ci < P.cells.size(); ++ci) {
                                if (P.cells[ci].name == sh.str) { subIdx = static_cast<int>(ci); break; }
                            }
                            if (subIdx >= 0 && subIdx != cellIdx) {
                                EmitCell(subIdx, c.first, c.second, scale * sh.mag, rotDeg + sh.deg, flipY ^ sh.flip, depth + 1);
                            } else if (out.pads.size() < 4096) {
                                std::wstring wnm = PathUtils::Utf8ToWide(sh.str);
                                float sz = static_cast<float>(2.0 * sh.mag * scale);
                                out.pads.push_back({ c.first, c.second, sz, sz, static_cast<float>(rotDeg + sh.deg), 0.0f, 0, EdaLayerId::TopSilk, wnm, wnm });
                                out.labels.push_back({ c.first, c.second, 0.7f, EdaLayerId::TopSilk, wnm });
                            }
                        }
                    }
                }
            }
        };

        (void)mapLayer;
        EmitCtx emitter{ parser, out, toUm };
        bool anyRoot = false;
        for (size_t i = 0; i < parser.cells.size(); ++i) {
            if (!referenced[i]) {
                anyRoot = true;
                emitter.EmitCell(static_cast<int>(i), 0.0, 0.0, 1.0, 0.0, false, 0);
            }
        }
        if (!anyRoot) {
            emitter.EmitCell(0, 0.0, 0.0, 1.0, 0.0, false, 0);
        }

        if (out.traces.empty() && out.circles.empty() && out.pads.empty() && out.labels.empty()) {
            return false;
        }
        return true;
    }
    return false;
}

// ============================================================================
// #67, #68, #69, #70, #80. FPGA & PLD: Xilinx/Synopsys (.xdc/.sdc), Quartus (.qsf),
//                          JEDEC Fuse Map (.jed), JTAG (.svf/.xsvf), Altera Bitstream (.sof/.pof)
// ============================================================================
inline bool ParseFpgaPldJtag200(const uint8_t* data, size_t size, const std::wstring& ext, ParsedEdaBoard& out) {
    std::string_view sv(reinterpret_cast<const char*>(data), size);

    // #67 & #68. FPGA Pinout & Timing Constraints (.xdc / .sdc / .qsf)
    if (ext == L".xdc" || ext == L".sdc" || ext == L".qsf") {
        for (unsigned char c : sv) {
            if (c == 0) return false;
        }
        // Validate balanced Tcl square/curly brackets so corrupt/truncated Tcl scripts fail cleanly
        {
            int sq = 0, cb = 0;
            bool inQ = false;
            for (size_t i = 0; i < sv.size(); ++i) {
                char c = sv[i];
                if (!inQ && c == '#') {
                    while (i < sv.size() && sv[i] != '\n') ++i;
                    continue;
                }
                if (c == '\\' && i + 1 < sv.size()) { ++i; continue; }
                if (c == '"') { inQ = !inQ; continue; }
                if (!inQ) {
                    if (c == '[') ++sq;
                    else if (c == ']') { if (--sq < 0) return false; }
                    else if (c == '{') ++cb;
                    else if (c == '}') { if (--cb < 0) return false; }
                }
            }
            if (inQ || sq != 0 || cb != 0) return false;
        }

        struct PinAssign { int row = 0, col = 0; std::wstring sig; std::wstring iostd; };
        struct ClockSpec { std::wstring name; double periodNs = 10.0; };
        struct IoDelaySpec { std::wstring port; double delayNs = 0.0; bool isOutput = false; };

        std::vector<PinAssign> pins;
        std::map<std::wstring, std::wstring> sigIoStd;
        std::vector<ClockSpec> clocks;
        std::vector<IoDelaySpec> ioDelays;
        size_t exceptionCount = 0;
        std::string partDevice;
        int maxRow = 7, maxCol = 7;

        size_t pos = 0;
        while (pos < sv.size() && pins.size() < 2048) {
            size_t eol = sv.find('\n', pos);
            if (eol == std::string_view::npos) eol = sv.size();
            std::string ln(sv.substr(pos, eol - pos));
            pos = eol + 1;
            size_t f = ln.find_first_not_of(" \t\r");
            if (f == std::string::npos || ln[f] == '#') continue;

            auto extractFlagArg = [&](const char* flag) -> std::string {
                size_t fp = ln.find(flag);
                if (fp == std::string::npos) return {};
                size_t s1 = ln.find_first_not_of(" \t\"", fp + std::strlen(flag));
                if (s1 == std::string::npos) return {};
                size_t s2 = ln.find_first_of(" \t\"\r]", s1);
                return ln.substr(s1, (s2 == std::string::npos ? ln.size() : s2) - s1);
            };
            auto extractGetPorts = [&]() -> std::string {
                size_t gp = ln.find("get_ports");
                if (gp == std::string::npos) gp = ln.find("get_pins");
                if (gp == std::string::npos) return {};
                size_t s1 = ln.find_first_not_of(" \t{[\"", gp + 9);
                size_t s2 = (s1 != std::string::npos) ? ln.find_first_of(" \t}]\"\r", s1) : std::string::npos;
                return (s1 != std::string::npos) ? ln.substr(s1, (s2 == std::string::npos ? ln.size() : s2) - s1) : std::string{};
            };

            if (ln.find("create_clock") != std::string::npos || ln.find("create_generated_clock") != std::string::npos) {
                std::string clkNm = extractFlagArg("-name");
                if (clkNm.empty()) clkNm = extractGetPorts();
                if (clkNm.empty()) clkNm = "clk" + std::to_string(clocks.size() + 1);
                double per = 10.0;
                std::string perS = extractFlagArg("-period");
                if (!perS.empty()) per = std::max(0.05, std::strtod(perS.c_str(), nullptr));
                else {
                    std::string divS = extractFlagArg("-divide_by");
                    if (!divS.empty() && !clocks.empty()) per = clocks.front().periodNs * std::max(1, std::atoi(divS.c_str()));
                }
                clocks.push_back({ PathUtils::Utf8ToWide(clkNm), per });
            } else if (ln.find("set_input_delay") != std::string::npos || ln.find("set_output_delay") != std::string::npos) {
                bool isOut = (ln.find("set_output_delay") != std::string::npos);
                std::string prt = extractGetPorts();
                // Find numeric delay token (first standalone float/number not preceded by -clock)
                double dly = 2.0;
                for (size_t ti = 0; ti < ln.size(); ++ti) {
                    if ((ln[ti] == '-' && ti + 1 < ln.size() && std::isdigit(static_cast<unsigned char>(ln[ti + 1]))) ||
                        (std::isdigit(static_cast<unsigned char>(ln[ti])) && (ti == 0 || ln[ti - 1] == ' ' || ln[ti - 1] == '\t'))) {
                        dly = std::strtod(ln.c_str() + ti, nullptr);
                        break;
                    }
                }
                if (ioDelays.size() < 64) {
                    ioDelays.push_back({ PathUtils::Utf8ToWide(prt.empty() ? "io" : prt), dly, isOut });
                }
            } else if (ln.find("set_false_path") != std::string::npos ||
                       ln.find("set_multicycle_path") != std::string::npos ||
                       ln.find("set_clock_groups") != std::string::npos ||
                       ln.find("set_max_delay") != std::string::npos) {
                ++exceptionCount;
            }

            // Xilinx XDC IOSTANDARD
            size_t ios = ln.find("IOSTANDARD");
            if (ios != std::string::npos) {
                size_t bs = ln.find_first_not_of(" \t", ios + 10);
                size_t be = (bs != std::string::npos) ? ln.find_first_of(" \t]", bs) : std::string::npos;
                std::string stdName = (bs != std::string::npos) ? ln.substr(bs, (be == std::string::npos ? ln.size() : be) - bs) : "";
                std::string sig = extractGetPorts();
                if (!sig.empty() && !stdName.empty()) sigIoStd[PathUtils::Utf8ToWide(sig)] = PathUtils::Utf8ToWide(stdName);
            }

            // Quartus DEVICE
            if (ln.find("set_global_assignment") != std::string::npos && ln.find("DEVICE ") != std::string::npos) {
                size_t dp = ln.find("DEVICE ") + 7;
                size_t s1 = ln.find_first_not_of(" \t\"", dp);
                size_t s2 = (s1 != std::string::npos) ? ln.find_first_of(" \t\"\r", s1) : std::string::npos;
                if (s1 != std::string::npos) partDevice = ln.substr(s1, (s2 == std::string::npos ? ln.size() : s2) - s1);
            }

            // Xilinx XDC: set_property PACKAGE_PIN <ball> [get_ports <sig>]
            size_t pp = ln.find("PACKAGE_PIN");
            if (pp != std::string::npos) {
                size_t bs = ln.find_first_not_of(" \t", pp + 11);
                size_t be = (bs != std::string::npos) ? ln.find_first_of(" \t]", bs) : std::string::npos;
                int r = 0, c = 0;
                if (bs != std::string::npos && DetailEda200::ParseBgaBallCoord(std::string_view(ln).substr(bs, (be == std::string::npos ? ln.size() : be) - bs), r, c)) {
                    std::string sig = extractGetPorts();
                    if (sig.empty()) sig = "IO";
                    maxRow = std::max(maxRow, r); maxCol = std::max(maxCol, c);
                    pins.push_back({ r, c, PathUtils::Utf8ToWide(sig), L"" });
                }
            }
            // Intel Quartus QSF: set_location_assignment PIN_<ball> -to <sig>
            size_t qp = ln.find("PIN_");
            if (qp != std::string::npos) {
                size_t be = ln.find_first_of(" \t\r", qp);
                int r = 0, c = 0;
                if (DetailEda200::ParseBgaBallCoord(std::string_view(ln).substr(qp, (be == std::string::npos ? ln.size() : be) - qp), r, c)) {
                    std::string sig = extractFlagArg("-to");
                    if (sig.empty()) sig = "IO";
                    maxRow = std::max(maxRow, r); maxCol = std::max(maxCol, c);
                    pins.push_back({ r, c, PathUtils::Utf8ToWide(sig), L"" });
                }
            }
        }

        if (pins.empty() && clocks.empty() && ioDelays.empty() && exceptionCount == 0) {
            return false;
        }

        // Timing-only SDC/XDC diagram: render true period-scaled clock waveforms + I/O setup/hold budget bars!
        if (pins.empty()) {
            out.kind = EdaDocKind::KiCadSch;
            out.formatTitle = (ext == L".xdc")
                ? L"Xilinx Timing Constraints (.xdc — "
                : L"Synopsys Design Timing Constraints (.sdc — ";
            out.formatTitle += std::to_wstring(clocks.size()) + L" clocks, " + std::to_wstring(ioDelays.size()) + L" I/O delays)";

            double maxPer = 1.0;
            for (const auto& ck : clocks) maxPer = std::max(maxPer, ck.periodNs);
            const float x0 = -30.0f, plotW = 80.0f;
            float y = 24.0f;

            for (size_t ci = 0; ci < clocks.size() && ci < 16; ++ci) {
                const auto& ck = clocks[ci];
                double freqMhz = 1000.0 / std::max(0.01, ck.periodNs);
                wchar_t clkLbl[160] = {};
                swprintf_s(clkLbl, L"%s (T=%.3g ns, %.1f MHz)", ck.name.c_str(), ck.periodNs, freqMhz);
                out.labels.push_back({ -54.0f, y, 0.95f, EdaLayerId::SchLabel, clkLbl });

                // Number of cycles across window proportional to maxPer / ck.periodNs
                double cycles = std::clamp((maxPer * 3.0) / std::max(0.05, ck.periodNs), 1.5, 24.0);
                float halfPeriodW = static_cast<float>(plotW / (cycles * 2.0));
                float cx = x0;
                bool high = true;
                EdaLayerId lid = (ci & 1) ? EdaLayerId::TopCopper : EdaLayerId::SchWire;
                while (cx < x0 + plotW - 0.05f) {
                    float nx = std::min(x0 + plotW, cx + halfPeriodW);
                    float yLvl = y + (high ? 2.0f : -2.0f);
                    float yOpp = y + (high ? -2.0f : 2.0f);
                    out.traces.push_back({ cx, yLvl, nx, yLvl, 0.28f, lid, static_cast<int>(ci + 1) });
                    if (nx < x0 + plotW - 0.02f) {
                        out.traces.push_back({ nx, yLvl, nx, yOpp, 0.26f, lid, static_cast<int>(ci + 1) });
                    }
                    cx = nx;
                    high = !high;
                }
                y -= 6.5f;
            }

            for (size_t di = 0; di < ioDelays.size() && di < 16; ++di) {
                const auto& io = ioDelays[di];
                float barFrac = std::clamp(static_cast<float>(std::fabs(io.delayNs) / std::max(1.0, maxPer)), 0.08f, 0.95f);
                float bw = plotW * barFrac;
                EdaLayerId lid = io.isOutput ? EdaLayerId::BottomCopper : EdaLayerId::InnerCopper;
                DetailEda100::AddRectOutline(out, x0 + bw * 0.5f, y, bw, 3.0f, lid, 0.26f);
                wchar_t dLbl[128] = {};
                swprintf_s(dLbl, L"%s %s: %.2f ns", io.isOutput ? L"OUT" : L"IN", io.port.c_str(), io.delayNs);
                out.labels.push_back({ -54.0f, y, 0.9f, EdaLayerId::SchLabel, dLbl });
                y -= 5.2f;
            }
            if (exceptionCount > 0) {
                out.labels.push_back({ x0, y, 0.95f, EdaLayerId::TopSilk,
                    L"Timing Exceptions (false/multicycle/clock_groups): " + std::to_wstring(exceptionCount) });
            }
            out.netCount = std::max<size_t>(1, clocks.size() + ioDelays.size());
            return !out.traces.empty() || !out.labels.empty();
        }

        out.kind = EdaDocKind::KiCadPcb;
        out.formatTitle = (ext == L".qsf") ? L"Intel Quartus FPGA Pinout & Settings (.qsf" :
                          (ext == L".xdc") ? L"Xilinx Vivado FPGA Constraints & Ballout (.xdc" :
                                             L"Synopsys Design Constraints (.sdc";
        if (!partDevice.empty()) out.formatTitle += L" — " + PathUtils::Utf8ToWide(partDevice);
        out.formatTitle += L")";

        int gridR = std::clamp(maxRow + 1, 6, 32);
        int gridC = std::clamp(maxCol + 1, 6, 32);
        float pitch = 1.6f;
        float pkgW = gridC * pitch + 4.0f, pkgH = gridR * pitch + 4.0f;
        DetailEda100::AddRectOutline(out, 0.0f, 0.0f, pkgW, pkgH, EdaLayerId::EdgeCuts, 0.4f);

        for (int r = 0; r < gridR; ++r) {
            for (int c = 0; c < gridC; ++c) {
                float bx = (c - (gridC - 1) * 0.5f) * pitch;
                float by = ((gridR - 1) * 0.5f - r) * pitch;
                out.circles.push_back({ bx, by, 0.42f, 0.12f, false, EdaLayerId::BottomSilk });
            }
        }
        for (const auto& pa : pins) {
            float bx = (pa.col - (gridC - 1) * 0.5f) * pitch;
            float by = ((gridR - 1) * 0.5f - pa.row) * pitch;
            auto itStd = sigIoStd.find(pa.sig);
            std::wstring stdStr = (itStd != sigIoStd.end()) ? itStd->second : L"";
            EdaLayerId lid = (stdStr.find(L"LVDS") != std::wstring::npos || stdStr.find(L"DIFF") != std::wstring::npos)
                ? EdaLayerId::TopCopper : EdaLayerId::PadsThrough;
            out.pads.push_back({ bx, by, 0.95f, 0.95f, 0.0f, 0.0f, 0, lid, pa.sig, stdStr });
            out.labels.push_back({ bx, by, 0.65f, EdaLayerId::TopSilk, pa.sig });
        }
        EdaComponent3D fpgaDie;
        fpgaDie.refDes = partDevice.empty() ? L"U1_FPGA" : PathUtils::Utf8ToWide(partDevice);
        fpgaDie.footprint = L"FBGA";
        fpgaDie.widthMm = pkgW * 0.45f; fpgaDie.lengthMm = pkgH * 0.45f; fpgaDie.heightMm = 1.8f;
        fpgaDie.pinCount = static_cast<int>(pins.size());
        fpgaDie.category = EdaPartCategory::IC_Package;
        out.components.push_back(fpgaDie);
        out.netCount = std::max<size_t>(1, pins.size() + clocks.size());
        return true;
    }

    // #69. JEDEC JESD3-C PLD/CPLD/GAL Fuse Map (.jed)
    if (ext == L".jed") {
        if (sv.find("*L") == std::string_view::npos && sv.find("QF") == std::string_view::npos &&
            sv.find("\nL") == std::string_view::npos) return false;
        out.kind = EdaDocKind::KiCadSch;
        out.formatTitle = L"JEDEC JESD3-C PLD Programmable Fuse Matrix (.jed)";

        std::vector<std::string> fuseRows;
        size_t pos = 0;
        while (pos < sv.size() && fuseRows.size() < 128) {
            // JEDEC commands start after '*' + optional whitespace; 'L' starts a fuse-list record terminated by '*'
            size_t lpos = std::string_view::npos;
            for (size_t i = pos; i + 2 < sv.size(); ++i) {
                if (sv[i] == 'L' && sv[i + 1] >= '0' && sv[i + 1] <= '9' &&
                    (i == 0 || sv[i - 1] == '*' || sv[i - 1] == '\n' || sv[i - 1] == '\r' || sv[i - 1] == ' ' || sv[i - 1] == '\t'))
                {
                    lpos = i;
                    break;
                }
            }
            if (lpos == std::string_view::npos) break;
            size_t star = sv.find('*', lpos + 1);
            if (star == std::string_view::npos) break;
            std::string_view blk = sv.substr(lpos + 1, star - lpos - 1);
            pos = star + 1;
            // Skip address digits, collect '0'/'1' fuse bits
            size_t sp = blk.find_first_of(" \t\r\n");
            std::string bits;
            if (sp != std::string_view::npos) {
                for (size_t k = sp; k < blk.size() && bits.size() < 64; ++k) {
                    if (blk[k] == '0' || blk[k] == '1') bits.push_back(blk[k]);
                }
            }
            if (!bits.empty()) fuseRows.push_back(std::move(bits));
        }
        if (fuseRows.empty()) return false;

        int rows = static_cast<int>(fuseRows.size());
        int cols = static_cast<int>(fuseRows[0].size());
        float pitch = 1.4f;
        float w = cols * pitch, h = rows * pitch;
        DetailEda100::AddRectOutline(out, 0.0f, 0.0f, w + 6.0f, h + 6.0f, EdaLayerId::EdgeCuts, 0.35f);

        for (int r = 0; r < rows; ++r) {
            float y = ((rows - 1) * 0.5f - r) * pitch;
            out.traces.push_back({ -w * 0.5f, y, w * 0.5f, y, 0.10f, EdaLayerId::BottomCopper, 1 });
            for (int c = 0; c < static_cast<int>(fuseRows[r].size()); ++c) {
                float x = (c - (cols - 1) * 0.5f) * pitch;
                if (r == 0) out.traces.push_back({ x, -h * 0.5f, x, h * 0.5f, 0.10f, EdaLayerId::InnerCopper, 1 });
                if (fuseRows[r][c] == '0') {
                    // Intact programmed cross-point fuse!
                    out.circles.push_back({ x, y, 0.36f, 0.18f, true, EdaLayerId::TopCopper });
                }
            }
        }
        return true;
    }

    // #70a. IEEE 1149.1 ASCII JTAG Serial Vector Format (.svf)
    if (ext == L".svf") {
        if (size == 0) return false;
        size_t binProbe = std::min<size_t>(size, 4096);
        for (size_t i = 0; i < binProbe; ++i) {
            if (data[i] == 0u) return false;
        }

        // Strip '!' and '//' comments while preserving statement boundaries (';')
        std::string cleanSvf;
        cleanSvf.reserve(std::min<size_t>(size, 524288));
        for (size_t i = 0; i < size && cleanSvf.size() < 524288; ++i) {
            char c = sv[i];
            if (c == '!' || (c == '/' && i + 1 < size && sv[i + 1] == '/')) {
                while (i < size && sv[i] != '\n') ++i;
                cleanSvf.push_back(' ');
            } else {
                cleanSvf.push_back((c >= 'a' && c <= 'z') ? static_cast<char>(c - ('a' - 'A')) : c);
            }
        }

        auto mapSvfState = [](std::string_view tok, uint8_t& outSt) -> bool {
            if (tok == "RESET")     { outSt = 0x00u; return true; }
            if (tok == "IDLE")      { outSt = 0x01u; return true; }
            if (tok == "DRSELECT")  { outSt = 0x02u; return true; }
            if (tok == "DRCAPTURE") { outSt = 0x03u; return true; }
            if (tok == "DRSHIFT")   { outSt = 0x04u; return true; }
            if (tok == "DREXIT1")   { outSt = 0x05u; return true; }
            if (tok == "DRPAUSE")   { outSt = 0x06u; return true; }
            if (tok == "DREXIT2")   { outSt = 0x07u; return true; }
            if (tok == "DRUPDATE")  { outSt = 0x08u; return true; }
            if (tok == "IRSELECT")  { outSt = 0x09u; return true; }
            if (tok == "IRCAPTURE") { outSt = 0x0Au; return true; }
            if (tok == "IRSHIFT")   { outSt = 0x0Bu; return true; }
            if (tok == "IREXIT1")   { outSt = 0x0Cu; return true; }
            if (tok == "IRPAUSE")   { outSt = 0x0Du; return true; }
            if (tok == "IREXIT2")   { outSt = 0x0Eu; return true; }
            if (tok == "IRUPDATE")  { outSt = 0x0Fu; return true; }
            return false;
        };

        auto parseParenHexBE = [](std::string_view stmt, std::string_view kw, uint32_t numBits, std::vector<uint8_t>& outBe) -> bool {
            size_t kp = 0;
            while ((kp = stmt.find(kw, kp)) != std::string_view::npos) {
                bool leftOk = (kp == 0) || !isalnum(static_cast<unsigned char>(stmt[kp - 1]));
                size_t afterKw = kp + kw.size();
                bool rightOk = (afterKw >= stmt.size()) || !isalnum(static_cast<unsigned char>(stmt[afterKw]));
                if (leftOk && rightOk) {
                    size_t lp = stmt.find('(', afterKw);
                    size_t rp = (lp != std::string_view::npos) ? stmt.find(')', lp + 1) : std::string_view::npos;
                    if (lp != std::string_view::npos && rp != std::string_view::npos && rp > lp) {
                        std::vector<uint8_t> nibs;
                        for (size_t k = lp + 1; k < rp; ++k) {
                            char ch = stmt[k];
                            int nib = (ch >= '0' && ch <= '9') ? (ch - '0') :
                                      (ch >= 'A' && ch <= 'F') ? (ch - 'A' + 10) : -1;
                            if (nib >= 0) nibs.push_back(static_cast<uint8_t>(nib));
                        }
                        const size_t nb = (static_cast<size_t>(numBits) + 7u) / 8u;
                        outBe.assign(nb, 0u);
                        for (size_t g = 0; g < nibs.size() && (g / 2u) < nb; ++g) {
                            uint8_t nib = nibs[nibs.size() - 1u - g];
                            size_t byteIdx = nb - 1u - (g / 2u);
                            if ((g & 1u) == 0u) outBe[byteIdx] |= nib;
                            else outBe[byteIdx] |= static_cast<uint8_t>(nib << 4);
                        }
                        return true;
                    }
                }
                kp = afterKw;
            }
            return false;
        };

        DetailEda200::JtagTapTracker tap;
        uint8_t svfEndIr = 0x01u; // IDLE
        uint8_t svfEndDr = 0x01u; // IDLE
        size_t validSvfCmds = 0;
        uint64_t totalShiftedBits = 0;
        uint32_t lastSirBits = 0, lastSdrBits = 0;
        std::vector<uint8_t> stickySirTdi, stickySdrTdi;

        size_t sPos = 0;
        while (sPos < cleanSvf.size()) {
            size_t semi = cleanSvf.find(';', sPos);
            if (semi == std::string::npos) break;
            std::string_view stmt(cleanSvf.data() + sPos, semi - sPos);
            sPos = semi + 1;

            // Tokenize leading keyword and whitespace-separated words
            std::vector<std::string_view> toks;
            size_t tp = 0;
            while (tp < stmt.size() && toks.size() < 32) {
                while (tp < stmt.size() && (stmt[tp] == ' ' || stmt[tp] == '\t' || stmt[tp] == '\r' || stmt[tp] == '\n')) ++tp;
                if (tp >= stmt.size()) break;
                if (stmt[tp] == '(') {
                    size_t rp = stmt.find(')', tp + 1);
                    if (rp == std::string_view::npos) break;
                    tp = rp + 1;
                    continue;
                }
                size_t te = tp;
                while (te < stmt.size() && stmt[te] != ' ' && stmt[te] != '\t' && stmt[te] != '\r' && stmt[te] != '\n' && stmt[te] != '(') ++te;
                toks.push_back(stmt.substr(tp, te - tp));
                tp = te;
            }
            if (toks.empty()) continue;

            std::string_view cmd = toks[0];
            if (cmd == "SIR" || cmd == "SDR") {
                if (toks.size() < 2) continue;
                uint64_t bits = 0;
                bool numOk = !toks[1].empty();
                for (char c : toks[1]) {
                    if (c < '0' || c > '9') { numOk = false; break; }
                    bits = bits * 10ULL + static_cast<uint64_t>(c - '0');
                    if (bits > 16777216ULL) { numOk = false; break; }
                }
                if (!numOk || bits == 0ULL) continue;
                uint32_t nb32 = static_cast<uint32_t>(bits);
                const size_t nBytes = (static_cast<size_t>(nb32) + 7u) / 8u;

                std::vector<uint8_t> tdiBe, tdoBe, maskBe;
                bool hasTdi = parseParenHexBE(stmt, "TDI", nb32, tdiBe);
                bool hasTdo = parseParenHexBE(stmt, "TDO", nb32, tdoBe);
                bool hasMask = parseParenHexBE(stmt, "MASK", nb32, maskBe);

                if (cmd == "SIR") {
                    if (hasTdi) { stickySirTdi = tdiBe; lastSirBits = nb32; }
                    else if (lastSirBits == nb32 && stickySirTdi.size() == nBytes) tdiBe = stickySirTdi;
                    else tdiBe.assign(nBytes, 0u);
                    tap.ShiftBytesBE(0x0Bu, svfEndIr, nb32, tdiBe.data(),
                        hasTdo ? tdoBe.data() : nullptr, hasMask ? &maskBe : nullptr, true);
                } else {
                    if (hasTdi) { stickySdrTdi = tdiBe; lastSdrBits = nb32; }
                    else if (lastSdrBits == nb32 && stickySdrTdi.size() == nBytes) tdiBe = stickySdrTdi;
                    else tdiBe.assign(nBytes, 0u);
                    tap.ShiftBytesBE(0x04u, svfEndDr, nb32, tdiBe.data(),
                        hasTdo ? tdoBe.data() : nullptr, hasMask ? &maskBe : nullptr, true);
                }
                totalShiftedBits += nb32;
                ++validSvfCmds;
            } else if (cmd == "STATE") {
                bool anySt = false;
                for (size_t i = 1; i < toks.size(); ++i) {
                    uint8_t st = 0;
                    if (mapSvfState(toks[i], st)) {
                        tap.StepTo(st, true);
                        anySt = true;
                    }
                }
                if (anySt) ++validSvfCmds;
            } else if (cmd == "ENDIR" && toks.size() >= 2) {
                uint8_t st = 0;
                if (mapSvfState(toks[1], st)) { svfEndIr = st; ++validSvfCmds; }
            } else if (cmd == "ENDDR" && toks.size() >= 2) {
                uint8_t st = 0;
                if (mapSvfState(toks[1], st)) { svfEndDr = st; ++validSvfCmds; }
            } else if (cmd == "RUNTEST" && toks.size() >= 2) {
                uint8_t runSt = 0x01u;
                size_t idx = 1;
                if (mapSvfState(toks[idx], runSt)) ++idx;
                uint32_t clks = 4u;
                if (idx < toks.size()) {
                    char* ep = nullptr;
                    std::string ns(toks[idx]);
                    unsigned long v = std::strtoul(ns.c_str(), &ep, 10);
                    if (ep != ns.c_str() && v > 0UL) clks = static_cast<uint32_t>(std::min<unsigned long>(v, 8UL));
                }
                tap.EmitIdleClocks(clks, runSt);
                ++validSvfCmds;
            } else if (cmd == "TRST" || cmd == "FREQUENCY" || cmd == "HIR" || cmd == "HDR" ||
                       cmd == "TIR" || cmd == "TDR" || cmd == "PIO" || cmd == "PIOMAP")
            {
                if (cmd == "TRST") tap.StepTo(0x00u, true);
                ++validSvfCmds;
            }
        }

        if (validSvfCmds == 0 || tap.cycles.empty()) return false;

        out.kind = EdaDocKind::KiCadSch;
        out.formatTitle = L"IEEE 1149.1 JTAG Serial Vector (.svf)";
        DetailEda200::RenderJtag4LaneWaveforms(out, tap.cycles,
            L"IEEE 1149.1 SVF (" + std::to_wstring(validSvfCmds) + L" cmds, " + std::to_wstring(totalShiftedBits) + L" bits)");
        return true;
    }

    // #70b. Xilinx XAPP503 Binary JTAG Scan Vector (.xsvf)
    if (ext == L".xsvf") {
        if (size == 0) return false;

        auto bytesForBits = [](uint32_t bits) -> size_t {
            return (static_cast<size_t>(bits) + 7u) / 8u;
        };
        auto popcntByte = [](uint8_t v) -> uint32_t {
            v = static_cast<uint8_t>((v & 0x55u) + ((v >> 1) & 0x55u));
            v = static_cast<uint8_t>((v & 0x33u) + ((v >> 2) & 0x33u));
            return static_cast<uint32_t>((v + (v >> 4)) & 0x0Fu);
        };

        DetailEda200::JtagTapTracker tap;
        uint32_t xsdrSizeBits = 0;
        uint8_t xrepeat = 32;
        uint8_t xendir = 0; // 0 = Run-Test/Idle (0x01), 1 = Pause-IR (0x0D)
        uint8_t xenddr = 0; // 0 = Run-Test/Idle (0x01), 1 = Pause-DR (0x06)
        uint32_t dataMaskOnes = 0;
        std::vector<uint8_t> tdoMask;
        size_t shiftOrStateCmds = 0;
        size_t totalOpcodes = 0;
        uint64_t totalShiftedBits = 0;
        std::string firstComment;

        size_t pos = 0;
        while (pos < size) {
            uint8_t op = data[pos++];
            ++totalOpcodes;

            if (op == 0x00u) { // XCOMPLETE — clean end of XAPP503 stream
                break;
            } else if (op == 0x01u) { // XTDOMASK
                size_t nb = bytesForBits(xsdrSizeBits);
                if (nb > size - pos) return false;
                tdoMask.assign(data + pos, data + pos + nb);
                pos += nb;
            } else if (op == 0x02u) { // XSIR (1B lengthBits + TDI)
                if (1u > size - pos) return false;
                uint8_t lenBits = data[pos++];
                if (lenBits == 0u) return false;
                size_t nb = bytesForBits(lenBits);
                if (nb > size - pos) return false;
                tap.ShiftBytesBE(0x0Bu, xendir ? 0x0Du : 0x01u, lenBits, data + pos, nullptr, nullptr, true);
                pos += nb;
                totalShiftedBits += lenBits;
                ++shiftOrStateCmds;
            } else if (op == 0x03u) { // XSDR
                if (xsdrSizeBits == 0u) return false;
                size_t nb = bytesForBits(xsdrSizeBits);
                if (nb > size - pos) return false;
                tap.ShiftBytesBE(0x04u, xenddr ? 0x06u : 0x01u, xsdrSizeBits, data + pos, nullptr, nullptr, true);
                pos += nb;
                totalShiftedBits += xsdrSizeBits;
                ++shiftOrStateCmds;
            } else if (op == 0x04u) { // XRUNTEST (4B uint32 BE)
                if (4u > size - pos) return false;
                uint32_t runClks = DetailEda200::RdU32BE(data + pos);
                pos += 4u;
                if (runClks > 0u) {
                    tap.EmitIdleClocks(runClks, 0x01u);
                }
            } else if (op == 0x07u) { // XREPEAT (1B)
                if (1u > size - pos) return false;
                xrepeat = data[pos++];
                (void)xrepeat;
            } else if (op == 0x08u) { // XSDRSIZE (4B uint32 BE)
                if (4u > size - pos) return false;
                xsdrSizeBits = DetailEda200::RdU32BE(data + pos);
                pos += 4u;
                if (xsdrSizeBits > 16777216u) return false;
            } else if (op == 0x09u) { // XSDRTDO (TDI + TDO expected)
                if (xsdrSizeBits == 0u) return false;
                size_t nb = bytesForBits(xsdrSizeBits);
                if (nb > (size - pos) / 2u) return false;
                tap.ShiftBytesBE(0x04u, xenddr ? 0x06u : 0x01u, xsdrSizeBits,
                    data + pos, data + pos + nb, &tdoMask, true);
                pos += nb * 2u;
                totalShiftedBits += xsdrSizeBits;
                ++shiftOrStateCmds;
            } else if (op == 0x0Au) { // XSETSDRMASKS (addrMask + dataMask)
                size_t nb = bytesForBits(xsdrSizeBits);
                if (nb > (size - pos) / 2u) return false;
                const uint8_t* dMask = data + pos + nb;
                dataMaskOnes = 0;
                for (size_t i = 0; i < nb; ++i) {
                    uint8_t mb = dMask[i];
                    if (i == 0 && (xsdrSizeBits & 7u) != 0u) {
                        mb &= static_cast<uint8_t>((1u << (xsdrSizeBits & 7u)) - 1u);
                    }
                    dataMaskOnes += popcntByte(mb);
                }
                pos += nb * 2u;
            } else if (op == 0x0Bu) { // XSDRINC (startVal + 1B numTimes + numTimes * bytes(dataMaskOnes))
                if (xsdrSizeBits == 0u) return false;
                size_t nb = bytesForBits(xsdrSizeBits);
                if (nb + 1u > size - pos) return false;
                const uint8_t* startVal = data + pos;
                pos += nb;
                uint8_t numTimes = data[pos++];
                size_t incBytes = bytesForBits(dataMaskOnes);
                if (incBytes > 0u && static_cast<size_t>(numTimes) > (size - pos) / incBytes) return false;
                tap.ShiftBytesBE(0x04u, xenddr ? 0x06u : 0x01u, xsdrSizeBits, startVal, nullptr, nullptr, true);
                pos += static_cast<size_t>(numTimes) * incBytes;
                totalShiftedBits += static_cast<uint64_t>(xsdrSizeBits) * (1ULL + static_cast<uint64_t>(numTimes));
                ++shiftOrStateCmds;
            } else if (op == 0x0Cu || op == 0x0Du || op == 0x0Eu) { // XSDRB, XSDRC, XSDRE
                if (xsdrSizeBits == 0u) return false;
                size_t nb = bytesForBits(xsdrSizeBits);
                if (nb > size - pos) return false;
                bool exitOnLast = (op == 0x0Eu);
                tap.ShiftBytesBE(0x04u, xenddr ? 0x06u : 0x01u, xsdrSizeBits, data + pos, nullptr, nullptr, exitOnLast);
                pos += nb;
                totalShiftedBits += xsdrSizeBits;
                ++shiftOrStateCmds;
            } else if (op == 0x0Fu || op == 0x10u || op == 0x11u) { // XSDRTDOB, XSDRTDOC, XSDRTDOE
                if (xsdrSizeBits == 0u) return false;
                size_t nb = bytesForBits(xsdrSizeBits);
                if (nb > (size - pos) / 2u) return false;
                bool exitOnLast = (op == 0x11u);
                tap.ShiftBytesBE(0x04u, xenddr ? 0x06u : 0x01u, xsdrSizeBits,
                    data + pos, data + pos + nb, &tdoMask, exitOnLast);
                pos += nb * 2u;
                totalShiftedBits += xsdrSizeBits;
                ++shiftOrStateCmds;
            } else if (op == 0x12u) { // XSTATE (1B TAP state 0x00..0x0F)
                if (1u > size - pos) return false;
                uint8_t st = data[pos++];
                if (st > 0x0Fu) return false;
                tap.StepTo(st, true);
                ++shiftOrStateCmds;
            } else if (op == 0x13u) { // XENDIR (1B: 0 or 1)
                if (1u > size - pos) return false;
                uint8_t v = data[pos++];
                if (v > 1u) return false;
                xendir = v;
            } else if (op == 0x14u) { // XENDDR (1B: 0 or 1)
                if (1u > size - pos) return false;
                uint8_t v = data[pos++];
                if (v > 1u) return false;
                xenddr = v;
            } else if (op == 0x15u) { // XSIR2 (2B uint16 BE lengthBits + TDI)
                if (2u > size - pos) return false;
                uint16_t lenBits = DetailEda200::RdU16BE(data + pos);
                pos += 2u;
                if (lenBits == 0u) return false;
                size_t nb = bytesForBits(lenBits);
                if (nb > size - pos) return false;
                tap.ShiftBytesBE(0x0Bu, xendir ? 0x0Du : 0x01u, lenBits, data + pos, nullptr, nullptr, true);
                pos += nb;
                totalShiftedBits += lenBits;
                ++shiftOrStateCmds;
            } else if (op == 0x16u) { // XCOMMENT (NUL-terminated ASCII string)
                size_t nulPos = pos;
                while (nulPos < size && data[nulPos] != 0u) ++nulPos;
                if (nulPos >= size) return false;
                if (firstComment.empty() && nulPos > pos) {
                    size_t take = std::min<size_t>(nulPos - pos, 48u);
                    firstComment.assign(reinterpret_cast<const char*>(data + pos), take);
                }
                pos = nulPos + 1u;
            } else if (op == 0x17u) { // XWAIT (1B waitState + 1B endState + 4B uint32 BE waitTimeUs)
                if (6u > size - pos) return false;
                uint8_t waitSt = data[pos];
                uint8_t endSt  = data[pos + 1];
                uint32_t waitUs = DetailEda200::RdU32BE(data + pos + 2);
                pos += 6u;
                if (waitSt > 0x0Fu || endSt > 0x0Fu) return false;
                tap.EmitIdleClocks(waitUs > 0u ? 4u : 1u, waitSt);
                tap.StepTo(endSt, false);
                ++shiftOrStateCmds;
            } else if (op == 0x18u) { // XWAITSTATE (Lattice/ispVM: 1B waitState + 1B endState + 4B clocks + 4B usec)
                if (10u > size - pos) return false;
                uint8_t waitSt = data[pos];
                uint8_t endSt  = data[pos + 1];
                uint32_t clks  = DetailEda200::RdU32BE(data + pos + 2);
                pos += 10u;
                if (waitSt > 0x0Fu || endSt > 0x0Fu) return false;
                tap.EmitIdleClocks(std::max<uint32_t>(clks, 1u), waitSt);
                tap.StepTo(endSt, false);
                ++shiftOrStateCmds;
            } else {
                // Unknown or reserved opcode (0x05, 0x06, >= 0x19) -> strictly reject
                return false;
            }
        }

        if (shiftOrStateCmds == 0 || tap.cycles.empty()) return false;

        out.kind = EdaDocKind::KiCadSch;
        out.formatTitle = L"Xilinx XAPP503 Binary JTAG Scan Vector (.xsvf)";
        std::wstring summary = L"XAPP503 XSVF (" + std::to_wstring(totalOpcodes) + L" ops, " +
                               std::to_wstring(totalShiftedBits) + L" bits)";
        if (!firstComment.empty()) {
            summary += L" - " + PathUtils::Utf8ToWide(firstComment);
        }
        DetailEda200::RenderJtag4LaneWaveforms(out, tap.cycles, summary);
        return true;
    }

    // #80. Intel/Altera FPGA SRAM/PROM Bitstream (.sof / .pof)
    if (ext == L".sof" || ext == L".pof") {
        if (size < 14) return false;
        bool isSofMagic = (std::memcmp(data, "SOF\0", 4) == 0);
        bool isPofMagic = (std::memcmp(data, "POF\0", 4) == 0);
        if (!isSofMagic && !isPofMagic) return false;

        uint32_t packetCount = DetailEda200::RdU32LE(data + 4);
        if (packetCount == 0 || packetCount > 4096) return false;

        std::string devStr;
        std::string creatorStr;
        std::vector<uint8_t> cfgBytes;
        size_t p = 8;
        for (uint32_t pk = 0; pk < packetCount; ++pk) {
            if (p + 6 > size) return false;
            uint16_t tag  = DetailEda200::RdU16LE(data + p);
            uint32_t plen = DetailEda200::RdU32LE(data + p + 2);
            p += 6;
            if (plen > size - p) return false;

            const uint8_t* pay = data + p;
            auto extractCleanAscii = [](const uint8_t* b, size_t n) -> std::string {
                size_t s = 0;
                while (s < n && (b[s] == 0 || b[s] == ' ' || b[s] == '\t')) ++s;
                size_t e = s;
                while (e < n && b[e] >= 32 && b[e] <= 126) ++e;
                while (e > s && b[e - 1] == ' ') --e;
                return (e > s) ? std::string(reinterpret_cast<const char*>(b + s), e - s) : std::string{};
            };

            if (tag == 1 && creatorStr.empty() && plen > 0 && plen <= 512) {
                creatorStr = extractCleanAscii(pay, plen);
            } else if (tag == 2 && devStr.empty() && plen > 0 && plen <= 256) {
                devStr = extractCleanAscii(pay, plen);
            } else if (plen >= 16) {
                if (cfgBytes.size() < 4 * 1024 * 1024) {
                    size_t take = std::min<size_t>(plen, 4 * 1024 * 1024 - cfgBytes.size());
                    cfgBytes.insert(cfgBytes.end(), pay, pay + take);
                }
            }
            if (devStr.empty() && tag != 1 && plen >= 4 && plen <= 64) {
                std::string cand = extractCleanAscii(pay, plen);
                if (cand.size() >= 4 && isalnum(static_cast<unsigned char>(cand[0]))) devStr = cand;
            }
            p += plen;
        }

        if (cfgBytes.size() < 16) return false;
        if (devStr.empty()) devStr = isPofMagic ? "Altera_POF" : "Altera_SOF";

        out.kind = EdaDocKind::KiCadPcb;
        std::wstring wDev = PathUtils::Utf8ToWide(devStr);
        out.formatTitle = (isPofMagic ? L"Intel/Altera POF Flash Bitstream (" : L"Intel/Altera SOF FPGA Bitstream (") + wDev + L")";

        int side = std::clamp(static_cast<int>(std::sqrt(static_cast<double>(cfgBytes.size()) / 4.0)), 4, 16);
        int cols = side, rows = side;
        int numSec = cols * rows;
        float pitch = 2.8f;
        float pkgW = cols * pitch + 5.0f, pkgH = rows * pitch + 5.0f;
        DetailEda100::AddRectOutline(out, 0.0f, 0.0f, pkgW, pkgH, EdaLayerId::EdgeCuts, 0.42f);

        auto popcnt8 = [](uint8_t v) -> int {
            v = static_cast<uint8_t>((v & 0x55u) + ((v >> 1) & 0x55u));
            v = static_cast<uint8_t>((v & 0x33u) + ((v >> 2) & 0x33u));
            return static_cast<int>((v + (v >> 4)) & 0x0Fu);
        };

        std::vector<float> secDensity(static_cast<size_t>(numSec), 0.0f);
        for (int s = 0; s < numSec; ++s) {
            size_t b0 = (static_cast<size_t>(s) * cfgBytes.size()) / static_cast<size_t>(numSec);
            size_t b1 = (static_cast<size_t>(s + 1) * cfgBytes.size()) / static_cast<size_t>(numSec);
            if (b1 <= b0) continue;
            size_t activeBits = 0;
            for (size_t i = b0; i < b1; ++i) {
                uint8_t bv = isPofMagic ? static_cast<uint8_t>(~cfgBytes[i]) : cfgBytes[i];
                activeBits += static_cast<size_t>(popcnt8(bv));
            }
            secDensity[static_cast<size_t>(s)] = static_cast<float>(activeBits) / static_cast<float>((b1 - b0) * 8u);
        }

        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                float sx = (c - (cols - 1) * 0.5f) * pitch;
                float sy = ((rows - 1) * 0.5f - r) * pitch;
                float dens = secDensity[static_cast<size_t>(r * cols + c)];
                if (dens <= 0.0f) continue;

                float padSz = 0.75f + dens * 1.55f;
                EdaLayerId lid = (dens >= 0.55f) ? EdaLayerId::TopCopper :
                                 (dens >= 0.20f) ? EdaLayerId::InnerCopper : EdaLayerId::BottomCopper;
                std::wstring sName = L"S" + std::to_wstring(r) + L"_" + std::to_wstring(c);
                out.pads.push_back({ sx, sy, padSz, padSz, 0.0f, 0.0f, 1, lid, sName, L"CFG" });

                if (c + 1 < cols && secDensity[static_cast<size_t>(r * cols + c + 1)] > 0.0f) {
                    float nx = ((c + 1) - (cols - 1) * 0.5f) * pitch;
                    out.traces.push_back({ sx, sy, nx, sy, 0.12f + dens * 0.22f, lid, r + 1 });
                }
                if (r + 1 < rows && secDensity[static_cast<size_t>((r + 1) * cols + c)] > 0.0f) {
                    float ny = ((rows - 1) * 0.5f - (r + 1)) * pitch;
                    out.traces.push_back({ sx, sy, sx, ny, 0.12f + dens * 0.22f, lid, r + 1 });
                }
            }
        }

        EdaComponent3D die;
        die.refDes = L"U1_FPGA";
        die.value = wDev;
        die.footprint = isPofMagic ? L"CFM_FLASH" : L"FPGA_DIE";
        die.widthMm = cols * pitch;
        die.lengthMm = rows * pitch;
        die.heightMm = 1.6f;
        die.category = EdaPartCategory::IC_Package;
        out.components.push_back(std::move(die));

        out.labels.push_back({ -pkgW * 0.46f, pkgH * 0.5f + 2.0f, 1.25f, EdaLayerId::TopSilk,
            wDev + L" (" + std::to_wstring(packetCount) + L" pkts, " + std::to_wstring(cfgBytes.size()) + L" B)" });
        return true;
    }

    return false;
}

// ============================================================================
// #71–#79. Logic/Scope (.sr, .sal, .wfm/.isf, .mf4/.mdf), Automotive (.ldf, .arxml),
//          Industrial Fieldbus & PLC (.eds/.dcf, .gsdml, .l5x)
// ============================================================================
inline bool ParseLogicScopeAutoPlc200(const uint8_t* data, size_t size, const std::wstring& ext, ParsedEdaBoard& out) {
    std::string_view sv(reinterpret_cast<const char*>(data), size);

    // #71. Sigrok / PulseView Logic & Mixed-Signal Analyzer Session (.sr — ZIP with metadata + multi-chunk logic-1-N & analog-1-M-N)
    if (ext == L".sr") {
        if (size < 22 || data[0] != 'P' || data[1] != 'K') return false;

        std::vector<ZipDeflate::ArchiveItemMeta> srEntries;
        if (!ZipDeflate::ListZipEntriesFromMemory(data, size, srEntries) || srEntries.empty()) {
            return false;
        }

        std::vector<uint8_t> metaBytes;
        ZipDeflate::ExtractZipEntryFromMemory(data, size, "metadata", metaBytes);

        // Parse metadata INI: total probes, unitsize, samplerate, probe<k>=..., analog<k>=...
        int totalProbes = 0;
        int unitSize = 0;
        std::string sampleRateStr;
        std::map<int, std::wstring> digNames; // 1-based probe index
        std::map<int, std::wstring> anaNames; // 1-based analog index (e.g. analog9=VCC)

        if (!metaBytes.empty()) {
            std::string_view msv(reinterpret_cast<const char*>(metaBytes.data()), metaBytes.size());
            size_t pos = 0;
            while (pos < msv.size()) {
                size_t eol = msv.find_first_of("\r\n", pos);
                if (eol == std::string_view::npos) eol = msv.size();
                std::string_view ln = msv.substr(pos, eol - pos);
                pos = eol + 1;
                while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
                while (!ln.empty() && (ln.back() == ' ' || ln.back() == '\t' || ln.back() == '\r')) ln.remove_suffix(1);
                if (ln.empty() || ln[0] == '[' || ln[0] == ';' || ln[0] == '#') continue;

                size_t eq = ln.find('=');
                if (eq == std::string_view::npos) continue;
                std::string key(ln.substr(0, eq));
                std::string_view val = ln.substr(eq + 1);
                while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
                while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.remove_prefix(1);

                if (key == "total probes") {
                    totalProbes = std::clamp(std::atoi(std::string(val).c_str()), 0, 64);
                } else if (key == "unitsize") {
                    unitSize = std::clamp(std::atoi(std::string(val).c_str()), 1, 8);
                } else if (key == "samplerate") {
                    sampleRateStr = std::string(val);
                } else if (key.rfind("probe", 0) == 0 && key.size() > 5) {
                    int idx = std::atoi(key.c_str() + 5);
                    if (idx >= 1 && idx <= 64) {
                        digNames[idx] = PathUtils::Utf8ToWide(val);
                        totalProbes = std::max(totalProbes, idx);
                    }
                } else if (key.rfind("analog", 0) == 0 && key.size() > 6 && key != "analog") {
                    int idx = std::atoi(key.c_str() + 6);
                    if (idx >= 1 && idx <= 128) {
                        anaNames[idx] = PathUtils::Utf8ToWide(val);
                    }
                }
            }
        }

        // Discover multi-chunk logic-1[-N] and analog-1-<ch>[-N] entries
        struct SrChunk { int chunkIdx = 1; std::string zipName; };
        std::vector<SrChunk> logicChunks;
        std::map<int, std::vector<SrChunk>> analogChunks;

        for (const auto& ze : srEntries) {
            std::string nm = PathUtils::WideToUtf8(ze.pathInArchive);
            size_t sl = nm.find_last_of("/\\");
            std::string base = (sl == std::string::npos) ? nm : nm.substr(sl + 1);
            if (base == "logic-1") {
                logicChunks.push_back({ 1, nm });
            } else if (base.rfind("logic-1-", 0) == 0 && base.size() > 8) {
                int cIdx = std::atoi(base.c_str() + 8);
                if (cIdx >= 1) logicChunks.push_back({ cIdx, nm });
            } else if (base.rfind("analog-1-", 0) == 0 && base.size() > 9) {
                std::string rest = base.substr(9);
                size_t dash = rest.find('-');
                int chId = std::atoi(dash == std::string::npos ? rest.c_str() : rest.substr(0, dash).c_str());
                int cIdx = (dash == std::string::npos) ? 1 : std::atoi(rest.c_str() + dash + 1);
                if (chId >= 1 && cIdx >= 1) {
                    analogChunks[chId].push_back({ cIdx, nm });
                }
            }
        }

        std::sort(logicChunks.begin(), logicChunks.end(), [](const SrChunk& a, const SrChunk& b) { return a.chunkIdx < b.chunkIdx; });
        std::vector<uint8_t> logicBytes;
        for (const auto& lc : logicChunks) {
            std::vector<uint8_t> part;
            if (ZipDeflate::ExtractZipEntryFromMemory(data, size, lc.zipName.c_str(), part) && !part.empty()) {
                if (logicBytes.size() + part.size() <= 16 * 1024 * 1024) {
                    logicBytes.insert(logicBytes.end(), part.begin(), part.end());
                }
            }
        }

        struct SrAnalogTrace {
            int chId = 0;
            std::wstring name;
            std::vector<float> samples;
            float vMin = 0.0f, vMax = 0.0f;
        };
        std::vector<SrAnalogTrace> anaTraces;
        for (auto& kv : analogChunks) {
            if (anaTraces.size() >= 8) break;
            auto& vec = kv.second;
            std::sort(vec.begin(), vec.end(), [](const SrChunk& a, const SrChunk& b) { return a.chunkIdx < b.chunkIdx; });
            std::vector<uint8_t> rawAna;
            for (const auto& ac : vec) {
                std::vector<uint8_t> part;
                if (ZipDeflate::ExtractZipEntryFromMemory(data, size, ac.zipName.c_str(), part) && !part.empty()) {
                    if ((part.size() % 4u) != 0) return false; // Corrupt float32 analog chunk!
                    if (rawAna.size() + part.size() <= 8 * 1024 * 1024) {
                        rawAna.insert(rawAna.end(), part.begin(), part.end());
                    }
                }
            }
            if (rawAna.size() >= 8) {
                SrAnalogTrace at;
                at.chId = kv.first;
                auto itN = anaNames.find(kv.first);
                at.name = (itN != anaNames.end() && !itN->second.empty()) ? itN->second : (L"A" + std::to_wstring(kv.first));
                size_t nF = rawAna.size() / 4u;
                at.samples.resize(nF);
                float vmin = 1e30f, vmax = -1e30f;
                for (size_t i = 0; i < nF; ++i) {
                    float fv = 0.0f;
                    std::memcpy(&fv, rawAna.data() + i * 4u, 4);
                    if (!std::isfinite(fv)) return false;
                    vmin = std::min(vmin, fv);
                    vmax = std::max(vmax, fv);
                    at.samples[i] = fv;
                }
                at.vMin = vmin;
                at.vMax = vmax;
                anaTraces.push_back(std::move(at));
            }
        }

        if (unitSize <= 0) {
            unitSize = (totalProbes > 32) ? 8 : ((totalProbes > 16) ? 4 : ((totalProbes > 8) ? 2 : 1));
        }
        const size_t totalLogicSamples = logicBytes.size() / static_cast<size_t>(unitSize);
        if (totalLogicSamples < 2 && anaTraces.empty()) {
            return false; // Strictly reject empty/metadata-only .sr archives!
        }

        int numDigLanes = 0;
        if (totalLogicSamples >= 2) {
            numDigLanes = (totalProbes > 0) ? std::min(totalProbes, 16) : std::min(unitSize * 8, 8);
        }
        const size_t totalLanes = static_cast<size_t>(numDigLanes) + anaTraces.size();
        if (totalLanes == 0) return false;

        out.kind = EdaDocKind::KiCadSch;
        out.formatTitle = L"Sigrok / PulseView Capture (.sr — " +
                          std::to_wstring(numDigLanes) + L"D, " + std::to_wstring(anaTraces.size()) + L"A";
        if (!sampleRateStr.empty()) out.formatTitle += L", " + PathUtils::Utf8ToWide(sampleRateStr);
        out.formatTitle += L")";
        out.netCount = totalLanes;

        DetailEda200::DrawGraticule(out, -40.0f, -24.0f, 80.0f, 48.0f, 16, 8);

        const float laneH = 44.0f / static_cast<float>(totalLanes);
        const float halfAmp = std::min(laneH * 0.36f, 2.4f);
        size_t laneIdx = 0;

        if (numDigLanes > 0 && totalLogicSamples >= 2) {
            const size_t maxPlotPts = 512;
            const size_t stride = std::max<size_t>(1, totalLogicSamples / maxPlotPts);
            std::vector<uint64_t> words;
            for (size_t s = 0; s < totalLogicSamples; s += stride) {
                uint64_t w = 0;
                const uint8_t* p = logicBytes.data() + s * static_cast<size_t>(unitSize);
                for (int b = 0; b < unitSize; ++b) w |= (static_cast<uint64_t>(p[b]) << (b * 8));
                words.push_back(w);
            }
            if (words.size() >= 2) {
                float stepX = 76.0f / static_cast<float>(words.size() - 1);
                for (int ch = 0; ch < numDigLanes; ++ch) {
                    float baseY = 22.0f - (static_cast<float>(laneIdx) + 0.5f) * laneH;
                    auto itNm = digNames.find(ch + 1);
                    std::wstring lbl = (itNm != digNames.end() && !itNm->second.empty())
                        ? itNm->second : (L"D" + std::to_wstring(ch));
                    out.labels.push_back({ -49.0f, baseY, 0.95f, EdaLayerId::TopSilk, lbl });
                    EdaLayerId lid = (ch & 1) ? EdaLayerId::BottomCopper : EdaLayerId::TopCopper;
                    for (size_t s = 1; s < words.size(); ++s) {
                        float x0 = -38.0f + static_cast<float>(s - 1) * stepX;
                        float x1 = -38.0f + static_cast<float>(s) * stepX;
                        int b0 = static_cast<int>((words[s - 1] >> ch) & 1ULL);
                        int b1 = static_cast<int>((words[s] >> ch) & 1ULL);
                        float y0 = baseY + (b0 ? halfAmp : -halfAmp);
                        float y1 = baseY + (b1 ? halfAmp : -halfAmp);
                        if (b0 != b1) out.traces.push_back({ x1, y0, x1, y1, 0.25f, lid, static_cast<int>(laneIdx + 1) });
                        out.traces.push_back({ x0, y0, x1, y0, 0.25f, lid, static_cast<int>(laneIdx + 1) });
                    }
                    ++laneIdx;
                }
            }
        }

        for (const auto& at : anaTraces) {
            float baseY = 22.0f - (static_cast<float>(laneIdx) + 0.5f) * laneH;
            wchar_t albl[128] = {};
            swprintf_s(albl, L"%s [%.2g..%.2gV]", at.name.c_str(), at.vMin, at.vMax);
            out.labels.push_back({ -49.0f, baseY, 0.90f, EdaLayerId::TopSilk, albl });

            const size_t nF = at.samples.size();
            const size_t stride = std::max<size_t>(1, nF / 512);
            float vSpan = (at.vMax - at.vMin > 1e-6f) ? (at.vMax - at.vMin) : 1.0f;
            auto mapAnaY = [&](float v) -> float {
                float u = (at.vMax - at.vMin > 1e-6f) ? ((v - at.vMin) / vSpan) : 0.5f;
                return (baseY - halfAmp) + std::clamp(u, 0.0f, 1.0f) * (2.0f * halfAmp);
            };
            float px = -38.0f, py = mapAnaY(at.samples[0]);
            for (size_t i = stride; i < nF; i += stride) {
                float wx = -38.0f + 76.0f * (static_cast<float>(i) / static_cast<float>(nF - 1));
                float wy = mapAnaY(at.samples[i]);
                out.traces.push_back({ px, py, wx, wy, 0.28f, EdaLayerId::InnerCopper, static_cast<int>(laneIdx + 1) });
                px = wx; py = wy;
            }
            ++laneIdx;
        }
        return !out.traces.empty();
    }

    // #72. Saleae Logic 2 Capture (.sal — ZIP with optional meta.json + digital-<N>.bin & analog-<N>.bin "<SALEAE>")
    if (ext == L".sal") {
        if (size < 22 || data[0] != 'P' || data[1] != 'K') return false;

        std::vector<ZipDeflate::ArchiveItemMeta> zEntries;
        if (!ZipDeflate::ListZipEntriesFromMemory(data, size, zEntries) || zEntries.empty()) {
            return false;
        }

        auto wideToUtf8Local = [](std::wstring_view w) -> std::string {
            std::string outS;
            outS.reserve(w.size());
            for (size_t i = 0; i < w.size(); ++i) {
                uint32_t cp = static_cast<uint16_t>(w[i]);
                if (cp >= 0xD800u && cp <= 0xDBFFu && i + 1 < w.size()) {
                    uint32_t lo = static_cast<uint16_t>(w[i + 1]);
                    if (lo >= 0xDC00u && lo <= 0xDFFFu) {
                        cp = 0x10000u + (((cp - 0xD800u) << 10) | (lo - 0xDC00u));
                        ++i;
                    }
                }
                if (cp < 0x80u) {
                    outS.push_back(static_cast<char>(cp));
                } else if (cp < 0x800u) {
                    outS.push_back(static_cast<char>(0xC0u | (cp >> 6)));
                    outS.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
                } else if (cp < 0x10000u) {
                    outS.push_back(static_cast<char>(0xE0u | (cp >> 12)));
                    outS.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
                    outS.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
                } else {
                    outS.push_back(static_cast<char>(0xF0u | (cp >> 18)));
                    outS.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
                    outS.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
                    outS.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
                }
            }
            return outS;
        };

        auto metaEntryUtf8 = [&](const auto& item) -> std::string {
            using Item = std::decay_t<decltype(item)>;
            if constexpr (MemberDetect::HasRelativePath<Item>::value) {
                if constexpr (std::is_constructible_v<std::string_view, decltype((item.relativePath))>) return std::string(std::string_view(item.relativePath));
                else return wideToUtf8Local(item.relativePath);
            } else if constexpr (MemberDetect::HasName<Item>::value) {
                if constexpr (std::is_constructible_v<std::string_view, decltype((item.name))>) return std::string(std::string_view(item.name));
                else return wideToUtf8Local(item.name);
            } else if constexpr (MemberDetect::HasPath<Item>::value) {
                if constexpr (std::is_constructible_v<std::string_view, decltype((item.path))>) return std::string(std::string_view(item.path));
                else return wideToUtf8Local(item.path);
            } else if constexpr (MemberDetect::HasFileName<Item>::value) {
                if constexpr (std::is_constructible_v<std::string_view, decltype((item.fileName))>) return std::string(std::string_view(item.fileName));
                else return wideToUtf8Local(item.fileName);
            } else {
                return {};
            }
        };

        struct SalFileEntry {
            int chIdx = 0;
            std::string entryName;
        };
        std::string metaEntryName;
        std::vector<SalFileEntry> digFiles;
        std::vector<SalFileEntry> anaFiles;

        auto parseChNum = [](std::string_view s, int& outIdx) -> bool {
            if (s.empty() || s.size() > 4) return false;
            int val = 0;
            for (char c : s) {
                if (c < '0' || c > '9') return false;
                val = val * 10 + (c - '0');
            }
            outIdx = val;
            return val >= 0 && val <= 1024;
        };

        for (const auto& ze : zEntries) {
            std::string fullName = metaEntryUtf8(ze);
            if (fullName.empty()) continue;
            size_t sl = fullName.find_last_of("/\\");
            std::string_view base = (sl == std::string::npos) ? std::string_view(fullName) : std::string_view(fullName).substr(sl + 1);
            if (base.empty()) continue;

            std::string baseLower(base);
            for (char& c : baseLower) {
                if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
            }

            if (baseLower == "meta.json") {
                metaEntryName = fullName;
            } else if (baseLower.size() > 12 && baseLower.rfind("digital-", 0) == 0 && baseLower.substr(baseLower.size() - 4) == ".bin") {
                int idx = 0;
                if (!parseChNum(std::string_view(baseLower).substr(8, baseLower.size() - 12), idx)) return false;
                digFiles.push_back({ idx, fullName });
            } else if (baseLower.size() > 11 && baseLower.rfind("analog-", 0) == 0 && baseLower.substr(baseLower.size() - 4) == ".bin") {
                int idx = 0;
                if (!parseChNum(std::string_view(baseLower).substr(7, baseLower.size() - 11), idx)) return false;
                anaFiles.push_back({ idx, fullName });
            }
        }

        // Strictly reject .sal ZIPs without any digital-*.bin or analog-*.bin channel streams
        if (digFiles.empty() && anaFiles.empty()) return false;

        auto sortAndDedup = [](std::vector<SalFileEntry>& vec) {
            std::sort(vec.begin(), vec.end(), [](const SalFileEntry& a, const SalFileEntry& b) {
                return a.chIdx < b.chIdx;
            });
            vec.erase(std::unique(vec.begin(), vec.end(), [](const SalFileEntry& a, const SalFileEntry& b) {
                return a.chIdx == b.chIdx;
            }), vec.end());
        };
        sortAndDedup(digFiles);
        sortAndDedup(anaFiles);

        // Parse optional meta.json for custom channel names and sample rates
        std::wstring digCustomNames[64];
        std::wstring anaCustomNames[64];
        std::wstring anyCustomNames[64];
        uint64_t metaDigitalRateHz = 0;

        if (!metaEntryName.empty()) {
            std::vector<uint8_t> metaBytes;
            if (ZipDeflate::ExtractZipEntryFromMemory(data, size, metaEntryName.c_str(), metaBytes) && !metaBytes.empty()) {
                std::string_view msv(reinterpret_cast<const char*>(metaBytes.data()), metaBytes.size());

                auto extractJsonStr = [](std::string_view obj, std::string_view key, std::string& outVal) -> bool {
                    size_t pos = 0;
                    while (pos < obj.size()) {
                        size_t q = obj.find('"', pos);
                        if (q == std::string_view::npos || q + key.size() + 1 >= obj.size()) return false;
                        if (obj.substr(q + 1, key.size()) == key && obj[q + 1 + key.size()] == '"') {
                            size_t p = q + 2 + key.size();
                            while (p < obj.size() && (obj[p] == ' ' || obj[p] == '\t' || obj[p] == '\r' || obj[p] == '\n')) ++p;
                            if (p < obj.size() && obj[p] == ':') {
                                ++p;
                                while (p < obj.size() && (obj[p] == ' ' || obj[p] == '\t' || obj[p] == '\r' || obj[p] == '\n')) ++p;
                                if (p < obj.size() && obj[p] == '"') {
                                    ++p;
                                    std::string res;
                                    while (p < obj.size() && obj[p] != '"') {
                                        if (obj[p] == '\\' && p + 1 < obj.size()) ++p;
                                        res.push_back(obj[p++]);
                                    }
                                    outVal = std::move(res);
                                    return true;
                                }
                            }
                        }
                        pos = q + 1;
                    }
                    return false;
                };

                auto extractJsonInt = [](std::string_view obj, std::string_view key, int64_t& outVal) -> bool {
                    size_t pos = 0;
                    while (pos < obj.size()) {
                        size_t q = obj.find('"', pos);
                        if (q == std::string_view::npos || q + key.size() + 1 >= obj.size()) return false;
                        if (obj.substr(q + 1, key.size()) == key && obj[q + 1 + key.size()] == '"') {
                            size_t p = q + 2 + key.size();
                            while (p < obj.size() && (obj[p] == ' ' || obj[p] == '\t' || obj[p] == '\r' || obj[p] == '\n')) ++p;
                            if (p < obj.size() && obj[p] == ':') {
                                ++p;
                                while (p < obj.size() && (obj[p] == ' ' || obj[p] == '\t' || obj[p] == '\r' || obj[p] == '\n')) ++p;
                                if (p < obj.size() && ((obj[p] >= '0' && obj[p] <= '9') || obj[p] == '-')) {
                                    bool neg = (obj[p] == '-');
                                    if (neg) ++p;
                                    if (p >= obj.size() || obj[p] < '0' || obj[p] > '9') return false;
                                    int64_t v = 0;
                                    while (p < obj.size() && obj[p] >= '0' && obj[p] <= '9') {
                                        v = v * 10 + (obj[p++] - '0');
                                    }
                                    outVal = neg ? -v : v;
                                    return true;
                                }
                            }
                        }
                        pos = q + 1;
                    }
                    return false;
                };

                int64_t srVal = 0;
                if (extractJsonInt(msv, "digital", srVal) && srVal > 0) {
                    metaDigitalRateHz = static_cast<uint64_t>(srVal);
                } else if (extractJsonInt(msv, "sampleRate", srVal) && srVal > 0) {
                    metaDigitalRateHz = static_cast<uint64_t>(srVal);
                }

                // Scan shallow JSON objects { ... } containing "name" + channel index
                bool inQuote = false;
                for (size_t i = 0; i < msv.size(); ++i) {
                    char c = msv[i];
                    if (c == '"' && (i == 0 || msv[i - 1] != '\\')) {
                        inQuote = !inQuote;
                        continue;
                    }
                    if (inQuote || c != '{') continue;

                    int depth = 1;
                    int nestedObjs = 0;
                    bool qInner = false;
                    size_t j = i + 1;
                    for (; j < msv.size() && (j - i) <= 1024; ++j) {
                        char cj = msv[j];
                        if (cj == '"' && msv[j - 1] != '\\') {
                            qInner = !qInner;
                            continue;
                        }
                        if (qInner) continue;
                        if (cj == '{') { ++depth; ++nestedObjs; }
                        else if (cj == '}') {
                            --depth;
                            if (depth == 0) break;
                        }
                    }
                    if (depth != 0 || nestedObjs > 1) continue;

                    std::string_view obj = msv.substr(i, j - i + 1);
                    std::string nmStr;
                    if (!extractJsonStr(obj, "name", nmStr) || nmStr.empty()) continue;

                    int64_t chIdx64 = -1;
                    if (!extractJsonInt(obj, "index", chIdx64)) {
                        if (!extractJsonInt(obj, "deviceChannel", chIdx64)) {
                            extractJsonInt(obj, "channel", chIdx64);
                        }
                    }
                    if (chIdx64 < 0 || chIdx64 >= 64) continue;

                    int kind = -1; // 0 = digital, 1 = analog
                    std::string typeStr;
                    if (extractJsonStr(obj, "type", typeStr)) {
                        for (char& tc : typeStr) {
                            if (tc >= 'A' && tc <= 'Z') tc = static_cast<char>(tc + ('a' - 'A'));
                        }
                        if (typeStr.find("analog") != std::string::npos || typeStr == "1") kind = 1;
                        else if (typeStr.find("digital") != std::string::npos || typeStr == "0") kind = 0;
                    }
                    if (kind < 0) {
                        size_t ctxStart = (i > 200) ? (i - 200) : 0;
                        std::string ctxLower(msv.substr(ctxStart, i - ctxStart));
                        for (char& tc : ctxLower) {
                            if (tc >= 'A' && tc <= 'Z') tc = static_cast<char>(tc + ('a' - 'A'));
                        }
                        size_t pAna = ctxLower.rfind("analog");
                        size_t pDig = ctxLower.rfind("digital");
                        if (pAna != std::string::npos && (pDig == std::string::npos || pAna > pDig)) kind = 1;
                        else if (pDig != std::string::npos) kind = 0;
                    }

                    std::wstring wName = PathUtils::Utf8ToWide(nmStr);
                    if (kind == 0) digCustomNames[chIdx64] = wName;
                    else if (kind == 1) anaCustomNames[chIdx64] = wName;
                    else anyCustomNames[chIdx64] = wName;
                }
            }
        }

        // Decode digital-<N>.bin channels ("<SALEAE>", version 0/1, type == 0)
        struct SalDigitalChannel {
            int chIdx = 0;
            std::wstring label;
            uint32_t initialState = 0;
            double beginTime = 0.0;
            double endTime = 0.0;
            uint64_t numTransitions = 0;
            std::vector<std::pair<double, uint8_t>> edges; // (timestamp, stateAfterEdge)
        };
        struct SalAnalogChannel {
            int chIdx = 0;
            std::wstring label;
            double beginTime = 0.0;
            double endTime = 0.0;
            uint64_t sampleRate = 0;
            uint64_t downsample = 1;
            uint64_t numSamples = 0;
            float vMin = 0.0f;
            float vMax = 0.0f;
            std::vector<float> samples;
        };

        std::vector<SalDigitalChannel> digChannels;
        std::vector<SalAnalogChannel> anaChannels;

        for (const auto& df : digFiles) {
            std::vector<uint8_t> dig;
            if (!ZipDeflate::ExtractZipEntryFromMemory(data, size, df.entryName.c_str(), dig)) return false;
            if (dig.size() < 44u || std::memcmp(dig.data(), "<SALEAE>", 8) != 0) return false;

            int32_t ver  = static_cast<int32_t>(DetailEda200::RdU32LE(dig.data() + 8));
            int32_t type = static_cast<int32_t>(DetailEda200::RdU32LE(dig.data() + 12));
            if (type != 0 || (ver != 0 && ver != 1)) return false;

            auto tryParseChunkHeader = [&](size_t hdrOff, uint32_t& outInit, double& outBegin, double& outEnd, uint64_t& outTrans, size_t& outDataOff) -> bool {
                if (hdrOff + 28u > dig.size()) return false;
                uint32_t initRaw = DetailEda200::RdU32LE(dig.data() + hdrOff);
                if (initRaw > 1u) return false;
                double bt = 0.0, et = 0.0;
                std::memcpy(&bt, dig.data() + hdrOff + 4u, 8);
                std::memcpy(&et, dig.data() + hdrOff + 12u, 8);
                if (!std::isfinite(bt) || !std::isfinite(et) || bt > et) return false;
                uint64_t nt = DetailEda200::RdU64LE(dig.data() + hdrOff + 20u);
                size_t dataOff = hdrOff + 28u;
                if (nt > (dig.size() - dataOff) / 8u) return false;
                outInit = initRaw;
                outBegin = bt;
                outEnd = et;
                outTrans = nt;
                outDataOff = dataOff;
                return true;
            };

            uint32_t initState = 0;
            double bTime = 0.0, eTime = 0.0;
            uint64_t nTrans = 0;
            size_t transOff = 44u;

            if (ver == 0) {
                if (!tryParseChunkHeader(16u, initState, bTime, eTime, nTrans, transOff)) return false;
            } else {
                // Version 1: either direct chunk header at +16 or uint64 numChunks at +16 followed by chunk header at +24
                if (!tryParseChunkHeader(16u, initState, bTime, eTime, nTrans, transOff)) {
                    if (dig.size() < 52u) return false;
                    uint64_t numChunks = DetailEda200::RdU64LE(dig.data() + 16u);
                    if (numChunks == 0ULL || numChunks > 65536ULL) return false;
                    if (!tryParseChunkHeader(24u, initState, bTime, eTime, nTrans, transOff)) return false;
                }
            }

            SalDigitalChannel ch;
            ch.chIdx = df.chIdx;
            ch.initialState = initState & 1u;
            ch.beginTime = bTime;
            ch.endTime = eTime;
            ch.numTransitions = nTrans;

            std::wstring baseLbl = L"D" + std::to_wstring(df.chIdx);
            if (df.chIdx >= 0 && df.chIdx < 64) {
                const std::wstring& cname = !digCustomNames[df.chIdx].empty() ? digCustomNames[df.chIdx] : anyCustomNames[df.chIdx];
                if (!cname.empty() && cname != baseLbl) baseLbl += L":" + cname;
            }
            ch.label = std::move(baseLbl);

            const uint64_t maxEdges = 512ULL;
            const uint64_t step = (nTrans > maxEdges) ? std::max<uint64_t>(1ULL, nTrans / maxEdges) : 1ULL;
            ch.edges.reserve(static_cast<size_t>(std::min<uint64_t>(nTrans, maxEdges + 2ULL)));

            for (uint64_t t = 0; t < nTrans; t += step) {
                double ts = 0.0;
                std::memcpy(&ts, dig.data() + transOff + static_cast<size_t>(t) * 8u, 8);
                if (!std::isfinite(ts)) return false;
                // After transition index t (0-based), state is initialState ^ ((t + 1) & 1)
                uint8_t stAfter = static_cast<uint8_t>((ch.initialState ^ static_cast<uint32_t>((t + 1ULL) & 1ULL)) & 1u);
                ch.edges.push_back({ ts, stAfter });
                if (ts < ch.beginTime) ch.beginTime = ts;
                if (ts > ch.endTime) ch.endTime = ts;
            }

            if (digChannels.size() < 16u) {
                digChannels.push_back(std::move(ch));
            }
        }

        // Decode analog-<N>.bin channels ("<SALEAE>", version 0/1, type == 1)
        for (const auto& af : anaFiles) {
            std::vector<uint8_t> ana;
            if (!ZipDeflate::ExtractZipEntryFromMemory(data, size, af.entryName.c_str(), ana)) return false;
            if (ana.size() < 48u || std::memcmp(ana.data(), "<SALEAE>", 8) != 0) return false;

            int32_t ver  = static_cast<int32_t>(DetailEda200::RdU32LE(ana.data() + 8));
            int32_t type = static_cast<int32_t>(DetailEda200::RdU32LE(ana.data() + 12));
            if (type != 1 || (ver != 0 && ver != 1)) return false;

            double bTime = 0.0;
            std::memcpy(&bTime, ana.data() + 16u, 8);
            uint64_t sRate = DetailEda200::RdU64LE(ana.data() + 24u);
            uint64_t down  = DetailEda200::RdU64LE(ana.data() + 32u);
            uint64_t nSamp = DetailEda200::RdU64LE(ana.data() + 40u);

            if (!std::isfinite(bTime) || sRate == 0ULL || nSamp == 0ULL) return false;
            if (nSamp > (ana.size() - 48u) / 4u) return false;

            double effRate = static_cast<double>(sRate) / static_cast<double>(std::max<uint64_t>(1ULL, down));
            if (!std::isfinite(effRate) || effRate <= 0.0) return false;

            SalAnalogChannel ch;
            ch.chIdx = af.chIdx;
            ch.beginTime = bTime;
            ch.sampleRate = sRate;
            ch.downsample = std::max<uint64_t>(1ULL, down);
            ch.numSamples = nSamp;
            ch.endTime = bTime + ((nSamp > 1ULL) ? (static_cast<double>(nSamp - 1ULL) / effRate) : (1.0 / effRate));

            std::wstring baseLbl = L"A" + std::to_wstring(af.chIdx);
            if (af.chIdx >= 0 && af.chIdx < 64) {
                const std::wstring& cname = !anaCustomNames[af.chIdx].empty() ? anaCustomNames[af.chIdx]
                                          : (digFiles.empty() ? anyCustomNames[af.chIdx] : std::wstring{});
                if (!cname.empty() && cname != baseLbl) baseLbl += L":" + cname;
            }
            ch.label = std::move(baseLbl);

            const size_t maxPlotPts = 512u;
            const uint64_t step = (nSamp > maxPlotPts) ? std::max<uint64_t>(1ULL, (nSamp - 1ULL) / (maxPlotPts - 1ULL)) : 1ULL;
            ch.samples.reserve(static_cast<size_t>(std::min<uint64_t>(nSamp, static_cast<uint64_t>(maxPlotPts) + 2ULL)));

            float vMin =  1e30f;
            float vMax = -1e30f;
            for (uint64_t s = 0; s < nSamp && ch.samples.size() < maxPlotPts; s += step) {
                float fv = 0.0f;
                std::memcpy(&fv, ana.data() + 48u + static_cast<size_t>(s) * 4u, 4);
                if (!std::isfinite(fv)) return false;
                vMin = std::min(vMin, fv);
                vMax = std::max(vMax, fv);
                ch.samples.push_back(fv);
            }
            if (nSamp > 1ULL) {
                float lastFv = 0.0f;
                std::memcpy(&lastFv, ana.data() + 48u + static_cast<size_t>(nSamp - 1ULL) * 4u, 4);
                if (!std::isfinite(lastFv)) return false;
                vMin = std::min(vMin, lastFv);
                vMax = std::max(vMax, lastFv);
                if (ch.samples.back() != lastFv && ch.samples.size() < maxPlotPts + 1u) {
                    ch.samples.push_back(lastFv);
                }
            }
            ch.vMin = (vMin <= vMax) ? vMin : 0.0f;
            ch.vMax = (vMin <= vMax) ? vMax : 0.0f;

            if (digChannels.size() + anaChannels.size() < 16u) {
                anaChannels.push_back(std::move(ch));
            }
        }

        const size_t totalCh = digChannels.size() + anaChannels.size();
        if (totalCh == 0u) return false;

        // Compute unified global [globalTMin, globalTMax] time window across all digital & analog channels
        double globalTMin =  1e300;
        double globalTMax = -1e300;
        for (const auto& dc : digChannels) {
            globalTMin = std::min(globalTMin, dc.beginTime);
            globalTMax = std::max(globalTMax, dc.endTime);
        }
        for (const auto& ac : anaChannels) {
            globalTMin = std::min(globalTMin, ac.beginTime);
            globalTMax = std::max(globalTMax, ac.endTime);
        }
        if (!std::isfinite(globalTMin) || !std::isfinite(globalTMax)) {
            globalTMin = 0.0;
            globalTMax = 1.0;
        }
        double globalSpan = globalTMax - globalTMin;
        if (!(globalSpan > 1e-12)) globalSpan = 1.0;

        auto mapTimeToX = [&](double t) -> float {
            double u = std::clamp((t - globalTMin) / globalSpan, 0.0, 1.0);
            return -38.0f + static_cast<float>(u * 76.0);
        };

        out.kind = EdaDocKind::KiCadSch;
        out.netCount = static_cast<int>(totalCh);
        out.formatTitle = L"Saleae Logic 2 Capture (.sal - " +
                          std::to_wstring(digChannels.size()) + L"D, " +
                          std::to_wstring(anaChannels.size()) + L"A)";

        DetailEda200::DrawGraticule(out, -40.0f, -24.0f, 80.0f, 48.0f, 16, 8);

        std::wstring hdrSummary = L"Saleae Logic 2 (" + std::to_wstring(digChannels.size()) + L" digital, " +
                                  std::to_wstring(anaChannels.size()) + L" analog";
        if (metaDigitalRateHz > 0ULL) {
            hdrSummary += L", " + std::to_wstring(metaDigitalRateHz) + L" Hz";
        }
        hdrSummary += L")";
        out.labels.push_back({ -39.0f, 25.5f, 1.05f, EdaLayerId::TopSilk, hdrSummary });

        const float laneStep = 44.0f / static_cast<float>(totalCh);
        const float halfAmp  = std::min(laneStep * 0.36f, 6.5f);
        size_t laneIdx = 0;

        // Render stacked digital waveforms
        for (const auto& dc : digChannels) {
            const float centerY = 22.0f - (static_cast<float>(laneIdx) + 0.5f) * laneStep;
            const EdaLayerId lid = (laneIdx & 1u) ? EdaLayerId::InnerCopper : EdaLayerId::TopCopper;
            const int netId = static_cast<int>(laneIdx + 1u);

            out.labels.push_back({ -49.5f, centerY, 0.95f, EdaLayerId::TopSilk, dc.label });

            float prevX = mapTimeToX(dc.beginTime);
            uint8_t curState = static_cast<uint8_t>(dc.initialState & 1u);

            for (const auto& edge : dc.edges) {
                float tx = mapTimeToX(edge.first);
                float yOld = centerY + (curState ? halfAmp : -halfAmp);
                float yNew = centerY + (edge.second ? halfAmp : -halfAmp);
                if (tx > prevX) {
                    out.traces.push_back({ prevX, yOld, tx, yOld, 0.28f, lid, netId });
                }
                if (yOld != yNew) {
                    out.traces.push_back({ tx, yOld, tx, yNew, 0.28f, lid, netId });
                }
                curState = edge.second;
                prevX = tx;
            }

            float endX = std::max(prevX + 0.4f, mapTimeToX(dc.endTime));
            endX = std::min(endX, 38.0f);
            float yFinal = centerY + (curState ? halfAmp : -halfAmp);
            if (endX > prevX) {
                out.traces.push_back({ prevX, yFinal, endX, yFinal, 0.28f, lid, netId });
            } else if (dc.edges.empty()) {
                out.traces.push_back({ -38.0f, yFinal, 38.0f, yFinal, 0.28f, lid, netId });
            }
            ++laneIdx;
        }

        // Render stacked analog oscilloscope waveforms
        for (const auto& ac : anaChannels) {
            const float centerY = 22.0f - (static_cast<float>(laneIdx) + 0.5f) * laneStep;
            const EdaLayerId lid = EdaLayerId::BottomCopper;
            const int netId = static_cast<int>(laneIdx + 1u);

            out.labels.push_back({ -49.5f, centerY, 0.95f, EdaLayerId::TopSilk, ac.label });

            wchar_t vRangeBuf[64] = {};
            swprintf_s(vRangeBuf, L"[%.2f..%.2fV]", static_cast<double>(ac.vMin), static_cast<double>(ac.vMax));
            out.labels.push_back({ 40.6f, centerY, 0.85f, EdaLayerId::TopSilk, vRangeBuf });

            const float vSpan = ac.vMax - ac.vMin;
            auto mapVoltToY = [&](float v) -> float {
                if (vSpan <= 1e-6f) return centerY;
                float norm = (v - ac.vMin) / vSpan; // 0..1
                return centerY + (norm * 2.0f - 1.0f) * halfAmp;
            };

            const size_t nPts = ac.samples.size();
            if (nPts == 1u) {
                float x0 = mapTimeToX(ac.beginTime);
                float x1 = std::max(x0 + 1.0f, mapTimeToX(ac.endTime));
                float y0 = mapVoltToY(ac.samples[0]);
                out.traces.push_back({ x0, y0, std::min(x1, 38.0f), y0, 0.26f, lid, netId });
            } else if (nPts >= 2u) {
                const double chSpan = ac.endTime - ac.beginTime;
                for (size_t i = 1u; i < nPts; ++i) {
                    double t0 = ac.beginTime + chSpan * (static_cast<double>(i - 1u) / static_cast<double>(nPts - 1u));
                    double t1 = ac.beginTime + chSpan * (static_cast<double>(i) / static_cast<double>(nPts - 1u));
                    float x0 = mapTimeToX(t0);
                    float x1 = mapTimeToX(t1);
                    float y0 = mapVoltToY(ac.samples[i - 1u]);
                    float y1 = mapVoltToY(ac.samples[i]);
                    out.traces.push_back({ x0, y0, x1, y1, 0.26f, lid, netId });
                }
            }
            ++laneIdx;
        }

        return !out.traces.empty();
    }

    // #73. Tektronix / Rigol Digital Oscilloscope Waveform (.wfm / .isf)
    if (ext == L".wfm" || ext == L".isf") {
        if (size < 16) return false;

        std::vector<float> samples;
        std::wstring fmtTitle;
        std::wstring metaLabel;

        // 1. Tektronix Binary WFM (0x0F0F LE / 0xF0F0 BE + ":WFM#001" / ":WFM#002" / ":WFM#003")
        bool isTekLe = (size >= 780 && data[0] == 0x0Fu && data[1] == 0x0Fu);
        bool isTekBe = (size >= 780 && data[0] == 0xF0u && data[1] == 0xF0u);
        if ((isTekLe || isTekBe) && std::memcmp(data + 2, ":WFM#00", 7) == 0 &&
            (data[9] == '1' || data[9] == '2' || data[9] == '3'))
        {
            int wfmVer = data[9] - '0';
            size_t minHdr = (wfmVer == 3) ? 838u : 780u;
            if (size < minHdr) return false;
            bool be = isTekBe;

            auto rdU16 = [&](size_t o) -> uint16_t {
                return be ? static_cast<uint16_t>((static_cast<uint16_t>(data[o]) << 8) | data[o + 1])
                          : DetailEda200::RdU16LE(data + o);
            };
            auto rdU32 = [&](size_t o) -> uint32_t {
                return be ? ((static_cast<uint32_t>(data[o]) << 24) |
                             (static_cast<uint32_t>(data[o + 1]) << 16) |
                             (static_cast<uint32_t>(data[o + 2]) << 8) |
                             static_cast<uint32_t>(data[o + 3]))
                          : DetailEda200::RdU32LE(data + o);
            };
            auto rdU64 = [&](size_t o) -> uint64_t {
                return be ? ((static_cast<uint64_t>(rdU32(o)) << 32) | rdU32(o + 4))
                          : DetailEda200::RdU64LE(data + o);
            };
            auto rdF64 = [&](size_t o) -> double {
                uint64_t bits = rdU64(o);
                double dv = 0.0;
                std::memcpy(&dv, &bits, 8);
                return dv;
            };

            uint8_t bpp = data[15];
            uint32_t curveBufOff = rdU32(16);
            if ((bpp != 1 && bpp != 2 && bpp != 4 && bpp != 8) || curveBufOff < minHdr || curveBufOff >= size) {
                return false;
            }

            size_t ed1 = (wfmVer == 1) ? 154u : 168u;
            double dimScale  = rdF64(ed1 + 0);
            double dimOffset = rdF64(ed1 + 8);
            int32_t expFmt   = static_cast<int32_t>(rdU32(ed1 + 72));
            if (!std::isfinite(dimScale) || std::fabs(dimScale) < 1e-18 || std::fabs(dimScale) > 1e12) dimScale = 1.0;
            if (!std::isfinite(dimOffset) || std::fabs(dimOffset) > 1e12) dimOffset = 0.0;

            size_t sStart = curveBufOff;
            size_t sEnd   = size;
            size_t cobj   = curveBufOff - 30u;
            uint32_t dStartOff = rdU32(cobj + 14);
            uint32_t dPostOff  = rdU32(cobj + 18);
            if (dPostOff > dStartOff && curveBufOff + static_cast<size_t>(dPostOff) <= size &&
                (dPostOff - dStartOff) >= static_cast<uint32_t>(bpp) * 2u)
            {
                sStart = curveBufOff + static_cast<size_t>(dStartOff);
                sEnd   = curveBufOff + static_cast<size_t>(dPostOff);
            }

            size_t totalPts = (sEnd - sStart) / bpp;
            if (totalPts < 2) return false;
            size_t stepPts = std::max<size_t>(1, totalPts / 1024);
            samples.reserve(std::min<size_t>(totalPts, 1025));

            for (size_t idx = 0; idx < totalPts && samples.size() < 1024; idx += stepPts) {
                size_t off = sStart + idx * bpp;
                double raw = 0.0;
                if (bpp == 1) {
                    raw = (expFmt == 7) ? static_cast<double>(data[off]) : static_cast<double>(static_cast<int8_t>(data[off]));
                } else if (bpp == 2) {
                    uint16_t u = rdU16(off);
                    raw = static_cast<double>(static_cast<int16_t>(u));
                } else if (bpp == 4) {
                    uint32_t u = rdU32(off);
                    if (expFmt == 4) {
                        float fv = 0.0f; std::memcpy(&fv, &u, 4);
                        raw = std::isfinite(fv) ? static_cast<double>(fv) : 0.0;
                    } else if (expFmt == 2) {
                        raw = static_cast<double>(u);
                    } else {
                        raw = static_cast<double>(static_cast<int32_t>(u));
                    }
                } else {
                    uint64_t u = rdU64(off);
                    if (expFmt == 5 || expFmt == 6) {
                        double dv = 0.0; std::memcpy(&dv, &u, 8);
                        raw = std::isfinite(dv) ? dv : 0.0;
                    } else {
                        raw = static_cast<double>(static_cast<int64_t>(u));
                    }
                }
                samples.push_back(static_cast<float>(raw * dimScale + dimOffset));
            }
            fmtTitle = L"Tektronix WFM#00" + std::to_wstring(wfmVer) + L" Oscilloscope Waveform (.wfm)";
            metaLabel = L"Tek WFM#00" + std::to_wstring(wfmVer) + L" (" + std::to_wstring(totalPts) + L" pts)";
        }
        // 2. Rigol Binary WFM (0xA5 0xA5 header magic)
        else if (ext == L".wfm" && size >= 260 && data[0] == 0xA5u && data[1] == 0xA5u) {
            size_t dataOff = 256u;
            uint32_t candPts = DetailEda200::RdU32LE(data + 28);
            size_t avail = size - dataOff;
            size_t totalPts = (candPts >= 2 && candPts <= avail) ? static_cast<size_t>(candPts) : avail;
            if (totalPts < 2) return false;

            float vScale = 1.0f;
            uint32_t vsRaw = DetailEda200::RdU32LE(data + 36);
            if (vsRaw >= 100u && vsRaw <= 50000000u) {
                vScale = static_cast<float>(vsRaw) * 1e-6f / 25.0f; // Rigol uV/div -> V/LSB
            }
            size_t stepPts = std::max<size_t>(1, totalPts / 1024);
            for (size_t i = 0; i < totalPts && samples.size() < 1024; i += stepPts) {
                int adc = static_cast<int>(data[dataOff + i]) - 128;
                samples.push_back(static_cast<float>(adc) * vScale);
            }
            fmtTitle = L"Rigol Digital Oscilloscope Waveform (.wfm)";
            metaLabel = L"Rigol WFM (" + std::to_wstring(totalPts) + L" pts)";
        }
        // 3. Tektronix ISF (Internal Save Format: WFMPRE / BYT_NR + CURVE #...)
        else {
            size_t hdrScan = std::min<size_t>(size, 4096);
            std::string upHdr(reinterpret_cast<const char*>(data), hdrScan);
            for (char& c : upHdr) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));

            bool hasIsfPre = (upHdr.find("BYT_N") != std::string::npos ||
                              upHdr.find(":WFMP") != std::string::npos ||
                              upHdr.find("WFMPRE") != std::string::npos);
            size_t curvePos = upHdr.find("CURVE");
            size_t kwLen = 5;
            if (curvePos == std::string::npos) { curvePos = upHdr.find("CURV"); kwLen = 4; }
            if (!hasIsfPre || curvePos == std::string::npos) return false;

            std::string pre = upHdr.substr(0, curvePos);
            auto parseTagF = [&](const char* tag, double defVal) -> double {
                size_t tp = pre.find(tag);
                if (tp == std::string::npos) return defVal;
                tp += std::strlen(tag);
                while (tp < pre.size() && (pre[tp] == ' ' || pre[tp] == '\t' || pre[tp] == ':')) ++tp;
                char* ep = nullptr;
                double v = std::strtod(pre.c_str() + tp, &ep);
                return (ep != pre.c_str() + tp && std::isfinite(v)) ? v : defVal;
            };

            int bytNr = static_cast<int>(parseTagF("BYT_NR", parseTagF("BYT_N", 1.0)));
            if (bytNr != 1 && bytNr != 2 && bytNr != 4) bytNr = 1;
            bool msbFirst = (pre.find("LSB") == std::string::npos); // MSB is Tektronix ISF default
            bool isUnsigned = (pre.find("BN_FMT RP") != std::string::npos || pre.find("BN_F RP") != std::string::npos);
            bool isFloat    = (pre.find("BN_FMT FP") != std::string::npos || pre.find("BN_F FP") != std::string::npos);
            double ymult = parseTagF("YMULT", parseTagF("YMU", 1.0));
            double yoff  = parseTagF("YOFF",  parseTagF("YOF", 0.0));
            double yzero = parseTagF("YZERO", parseTagF("YZE", 0.0));
            if (std::fabs(ymult) < 1e-18) ymult = 1.0;

            size_t cp = curvePos + kwLen;
            while (cp < size && (data[cp] == ' ' || data[cp] == '\t')) ++cp;
            if (cp >= size) return false;

            if (data[cp] == '#') {
                if (cp + 2 > size || data[cp + 1] < '0' || data[cp + 1] > '9') return false;
                int ndig = data[cp + 1] - '0';
                size_t sOff = 0, byteCnt = 0;
                if (ndig == 0) {
                    sOff = cp + 2;
                    byteCnt = size - sOff;
                } else {
                    if (cp + 2 + static_cast<size_t>(ndig) > size) return false;
                    for (int d = 0; d < ndig; ++d) {
                        uint8_t ch = data[cp + 2 + d];
                        if (ch < '0' || ch > '9') return false;
                        byteCnt = byteCnt * 10u + static_cast<size_t>(ch - '0');
                    }
                    sOff = cp + 2 + static_cast<size_t>(ndig);
                }
                if (sOff >= size) return false;
                size_t avail = std::min<size_t>(byteCnt, size - sOff);
                size_t totalPts = avail / static_cast<size_t>(bytNr);
                if (totalPts < 2) return false;
                size_t stepPts = std::max<size_t>(1, totalPts / 1024);
                for (size_t idx = 0; idx < totalPts && samples.size() < 1024; idx += stepPts) {
                    size_t o = sOff + idx * static_cast<size_t>(bytNr);
                    double raw = 0.0;
                    if (bytNr == 1) {
                        raw = isUnsigned ? static_cast<double>(data[o]) : static_cast<double>(static_cast<int8_t>(data[o]));
                    } else if (bytNr == 2) {
                        uint16_t u = msbFirst ? static_cast<uint16_t>((static_cast<uint16_t>(data[o]) << 8) | data[o + 1])
                                              : DetailEda200::RdU16LE(data + o);
                        raw = isUnsigned ? static_cast<double>(u) : static_cast<double>(static_cast<int16_t>(u));
                    } else {
                        uint32_t u = msbFirst ? ((static_cast<uint32_t>(data[o]) << 24) |
                                                 (static_cast<uint32_t>(data[o + 1]) << 16) |
                                                 (static_cast<uint32_t>(data[o + 2]) << 8) |
                                                 static_cast<uint32_t>(data[o + 3]))
                                              : DetailEda200::RdU32LE(data + o);
                        if (isFloat) {
                            float fv = 0.0f; std::memcpy(&fv, &u, 4);
                            raw = std::isfinite(fv) ? static_cast<double>(fv) : 0.0;
                        } else {
                            raw = isUnsigned ? static_cast<double>(u) : static_cast<double>(static_cast<int32_t>(u));
                        }
                    }
                    samples.push_back(static_cast<float>((raw - yoff) * ymult + yzero));
                }
            } else {
                // ASCII CURVE values
                std::string asc(reinterpret_cast<const char*>(data + cp), std::min<size_t>(size - cp, 65536));
                const char* ap = asc.c_str();
                while (*ap && samples.size() < 1024) {
                    while (*ap == ' ' || *ap == '\t' || *ap == ',' || *ap == '\r' || *ap == '\n') ++ap;
                    if (!*ap || *ap == ';') break;
                    char* ep = nullptr;
                    double raw = std::strtod(ap, &ep);
                    if (ep == ap) break;
                    if (std::isfinite(raw)) samples.push_back(static_cast<float>((raw - yoff) * ymult + yzero));
                    ap = ep;
                }
            }
            fmtTitle = L"Tektronix ISF Oscilloscope Trace (.isf)";
            metaLabel = L"Tek ISF (" + std::to_wstring(samples.size()) + L" pts)";
        }

        if (samples.size() < 2) return false;

        out.kind = EdaDocKind::KiCadSch;
        out.formatTitle = fmtTitle;
        DetailEda200::DrawGraticule(out, -40.0f, -25.0f, 80.0f, 50.0f, 10, 8);

        float mn = samples[0], mx = samples[0];
        for (float v : samples) { mn = std::min(mn, v); mx = std::max(mx, v); }
        float span = std::max(1e-6f, mx - mn);

        // Zero-volt ground reference indicator on graticule
        float yZeroLine = std::clamp(-20.0f + ((0.0f - mn) / span) * 40.0f, -24.0f, 24.0f);
        out.traces.push_back({ -40.0f, yZeroLine, -38.5f, yZeroLine, 0.28f, EdaLayerId::InnerCopper, 2 });

        for (size_t i = 1; i < samples.size(); ++i) {
            float x0 = -39.0f + (static_cast<float>(i - 1) / static_cast<float>(samples.size() - 1)) * 78.0f;
            float x1 = -39.0f + (static_cast<float>(i)     / static_cast<float>(samples.size() - 1)) * 78.0f;
            float y0 = -20.0f + ((samples[i - 1] - mn) / span) * 40.0f;
            float y1 = -20.0f + ((samples[i]     - mn) / span) * 40.0f;
            out.traces.push_back({ x0, y0, x1, y1, 0.30f, EdaLayerId::TopCopper, 1 });
        }
        out.labels.push_back({ -39.0f, 26.8f, 1.15f, EdaLayerId::TopSilk, metaLabel });
        return true;
    }

    return false;
}

inline void DrawFieldbusSchRect(ParsedEdaBoard& out, float cx, float cy, float w, float h, float strokeW, EdaLayerId lid, int netId = 0) {
    float x0 = cx - w * 0.5f, x1 = cx + w * 0.5f;
    float y0 = cy - h * 0.5f, y1 = cy + h * 0.5f;
    out.traces.push_back({ x0, y0, x1, y0, strokeW, lid, netId });
    out.traces.push_back({ x1, y0, x1, y1, strokeW, lid, netId });
    out.traces.push_back({ x1, y1, x0, y1, strokeW, lid, netId });
    out.traces.push_back({ x0, y1, x0, y0, strokeW, lid, netId });
}

// #74. ASAM MDF 3.x & MDF 4.x ECU Telemetry (.mdf / .mf4 — "MDF     " / "UnFinMF ")
inline bool ParseAsamMdf200(const uint8_t* data, size_t size, const std::wstring& ext, ParsedEdaBoard& out) {
    if (ext == L".mf4" || ext == L".mdf") {
        if (size < 64) return false;
        if (std::memcmp(data, "MDF     ", 8) != 0 && std::memcmp(data, "UnFinMF ", 8) != 0) return false;

        std::string verStr(reinterpret_cast<const char*>(data + 8), strnlen(reinterpret_cast<const char*>(data + 8), 8));
        while (!verStr.empty() && (verStr.back() == ' ' || verStr.back() == '\0')) verStr.pop_back();
        const uint16_t idVerNum = DetailEda200::RdU16LE(data + 28);

        struct MdfConv {
            uint8_t type = 0; // 0=identity, 1=linear P1+P2*x, 2=rational
            std::vector<double> p;
            std::string unit;

            double Apply(double raw) const {
                if (type == 1 && p.size() >= 2) {
                    return p[0] + p[1] * raw;
                }
                if (type == 2 && p.size() >= 6) {
                    double den = p[3] * raw * raw + p[4] * raw + p[5];
                    return (std::fabs(den) > 1e-18) ? ((p[0] * raw * raw + p[1] * raw + p[2]) / den) : raw;
                }
                return raw;
            }
        };

        struct MdfDecodedCh {
            std::wstring name;
            std::wstring unit;
            std::vector<double> t;
            std::vector<double> y;
            double yMin =  1e300;
            double yMax = -1e300;
            size_t totalRecords = 0;
        };

        auto extractRawSample = [](const uint8_t* rec, size_t recBytes, uint32_t byteOff, uint8_t bitOff,
                                   uint32_t bitCnt, uint8_t dType, double& outVal) -> bool {
            byteOff += static_cast<uint32_t>(bitOff / 8u);
            bitOff  = static_cast<uint8_t>(bitOff % 8u);
            if (bitCnt == 0u || bitCnt > 64u) return false;
            const size_t needBytes = (static_cast<size_t>(bitOff) + static_cast<size_t>(bitCnt) + 7u) / 8u;
            if (static_cast<size_t>(byteOff) + needBytes > recBytes) return false;

            const uint8_t* p = rec + byteOff;
            if (dType == 4u && bitOff == 0u) { // FLOAT_LE
                if (bitCnt == 32u) {
                    uint32_t u = DetailEda200::RdU32LE(p);
                    float fv = 0.0f;
                    std::memcpy(&fv, &u, 4);
                    if (!std::isfinite(fv)) return false;
                    outVal = static_cast<double>(fv);
                    return true;
                }
                if (bitCnt == 64u) {
                    uint64_t u = DetailEda200::RdU64LE(p);
                    double dv = 0.0;
                    std::memcpy(&dv, &u, 8);
                    if (!std::isfinite(dv)) return false;
                    outVal = dv;
                    return true;
                }
                return false;
            }
            if (dType == 5u && bitOff == 0u) { // FLOAT_BE
                if (bitCnt == 32u) {
                    uint32_t u = DetailEda200::RdU32BE(p);
                    float fv = 0.0f;
                    std::memcpy(&fv, &u, 4);
                    if (!std::isfinite(fv)) return false;
                    outVal = static_cast<double>(fv);
                    return true;
                }
                if (bitCnt == 64u) {
                    uint64_t u = (static_cast<uint64_t>(DetailEda200::RdU32BE(p)) << 32) |
                                 static_cast<uint64_t>(DetailEda200::RdU32BE(p + 4));
                    double dv = 0.0;
                    std::memcpy(&dv, &u, 8);
                    if (!std::isfinite(dv)) return false;
                    outVal = dv;
                    return true;
                }
                return false;
            }
            if (dType > 3u) return false;

            uint64_t uval = 0;
            const bool isBE = (dType == 1u || dType == 3u);
            if (!isBE) {
                if (bitOff == 0u && bitCnt == 8u)  uval = p[0];
                else if (bitOff == 0u && bitCnt == 16u) uval = DetailEda200::RdU16LE(p);
                else if (bitOff == 0u && bitCnt == 32u) uval = DetailEda200::RdU32LE(p);
                else if (bitOff == 0u && bitCnt == 64u) uval = DetailEda200::RdU64LE(p);
                else {
                    for (uint32_t b = 0; b < bitCnt; ++b) {
                        uint32_t totBit = static_cast<uint32_t>(bitOff) + b;
                        uint8_t bv = p[totBit >> 3];
                        if ((bv >> (totBit & 7u)) & 1u) {
                            uval |= (1ULL << b);
                        }
                    }
                }
            } else {
                size_t nb = std::min<size_t>(needBytes, 8u);
                uint64_t acc = 0;
                for (size_t k = 0; k < nb; ++k) {
                    acc = (acc << 8) | static_cast<uint64_t>(p[k]);
                }
                uint64_t mask = (bitCnt == 64u) ? ~0ULL : ((1ULL << bitCnt) - 1ULL);
                uval = (acc >> bitOff) & mask;
            }

            if (dType == 2u || dType == 3u) {
                if (bitCnt == 64u) {
                    outVal = static_cast<double>(static_cast<int64_t>(uval));
                } else {
                    uint64_t signBit = 1ULL << (bitCnt - 1u);
                    int64_t sval = (uval & signBit)
                        ? static_cast<int64_t>(uval | (~0ULL << bitCnt))
                        : static_cast<int64_t>(uval);
                    outVal = static_cast<double>(sval);
                }
            } else {
                outVal = static_cast<double>(uval);
            }
            return std::isfinite(outVal);
        };

        std::vector<MdfDecodedCh> channels;
        const bool isMdf4 = (size >= 88 && std::memcmp(data + 64, "##HD", 4) == 0);
        const bool isMdf3 = (!isMdf4 && idVerNum < 400 && size >= 84 && std::memcmp(data + 64, "HD", 2) == 0);
        if (!isMdf4 && !isMdf3) return false;

        if (isMdf4) {
            struct Mdf4Blk {
                char id[4] = {};
                uint64_t dataOff = 0;
                uint64_t dataLen = 0;
                std::vector<uint64_t> links;
            };
            auto rdMdf4Blk = [&](uint64_t off, Mdf4Blk& b) -> bool {
                if (off < 64ULL || off + 24ULL > size) return false;
                const size_t o = static_cast<size_t>(off);
                if (data[o] != '#' || data[o + 1] != '#') return false;
                std::memcpy(b.id, data + o, 4);
                uint64_t blkLen = DetailEda200::RdU64LE(data + o + 8);
                uint64_t lnkCnt = DetailEda200::RdU64LE(data + o + 16);
                if (lnkCnt > 65536ULL) return false;
                uint64_t hdrBytes = 24ULL + lnkCnt * 8ULL;
                if (blkLen < hdrBytes || off + blkLen > size) return false;
                b.links.resize(static_cast<size_t>(lnkCnt));
                for (size_t i = 0; i < b.links.size(); ++i) {
                    b.links[i] = DetailEda200::RdU64LE(data + o + 24 + i * 8);
                }
                b.dataOff = off + hdrBytes;
                b.dataLen = blkLen - hdrBytes;
                return true;
            };

            auto rdMdf4F64 = [&](size_t off) -> double {
                uint64_t u = DetailEda200::RdU64LE(data + off);
                double dv = 0.0;
                std::memcpy(&dv, &u, 8);
                return dv;
            };

            auto rdMdf4Text = [&](uint64_t off) -> std::string {
                Mdf4Blk tb;
                if (!rdMdf4Blk(off, tb) || tb.dataLen == 0) return {};
                const char* p = reinterpret_cast<const char*>(data + static_cast<size_t>(tb.dataOff));
                size_t sl = strnlen(p, static_cast<size_t>(std::min<uint64_t>(tb.dataLen, 4096ULL)));
                std::string raw(p, sl);
                if (std::memcmp(tb.id, "##MD", 4) == 0) {
                    size_t tx0 = raw.find("<TX>");
                    size_t tx1 = (tx0 != std::string::npos) ? raw.find("</TX>", tx0 + 4) : std::string::npos;
                    if (tx0 != std::string::npos && tx1 != std::string::npos) {
                        raw = raw.substr(tx0 + 4, tx1 - (tx0 + 4));
                    } else {
                        std::string stripped;
                        bool inTag = false;
                        for (char c : raw) {
                            if (c == '<') inTag = true;
                            else if (c == '>') inTag = false;
                            else if (!inTag) stripped.push_back(c);
                        }
                        raw = std::move(stripped);
                    }
                }
                while (!raw.empty() && static_cast<unsigned char>(raw.front()) <= 0x20u) raw.erase(raw.begin());
                while (!raw.empty() && static_cast<unsigned char>(raw.back()) <= 0x20u) raw.pop_back();
                return raw;
            };

            auto rdMdf4Conv = [&](uint64_t off) -> MdfConv {
                MdfConv cv;
                Mdf4Blk cb;
                if (!rdMdf4Blk(off, cb) || std::memcmp(cb.id, "##CC", 4) != 0 || cb.dataLen < 8) return cv;
                if (cb.links.size() > 1 && cb.links[1] != 0) {
                    cv.unit = rdMdf4Text(cb.links[1]);
                }
                const size_t doff = static_cast<size_t>(cb.dataOff);
                cv.type = data[doff + 0];
                uint16_t valCnt = DetailEda200::RdU16LE(data + doff + 6);
                size_t paramOff = 0;
                if (cb.dataLen >= 24ULL + static_cast<uint64_t>(valCnt) * 8ULL) {
                    paramOff = doff + 24;
                } else if (cb.dataLen >= 8ULL + static_cast<uint64_t>(valCnt) * 8ULL) {
                    paramOff = doff + 8;
                }
                if (paramOff != 0 && valCnt <= 32u) {
                    cv.p.resize(valCnt);
                    for (uint16_t i = 0; i < valCnt; ++i) {
                        cv.p[i] = rdMdf4F64(paramOff + static_cast<size_t>(i) * 8u);
                    }
                }
                return cv;
            };

            std::set<uint64_t> visitedDataBlks;
            auto appendMdf4DataStream = [&](auto& self, uint64_t off, std::vector<uint8_t>& outStream) -> void {
                static constexpr size_t kMaxDataStream = 16 * 1024 * 1024;
                if (off < 64ULL || outStream.size() >= kMaxDataStream || visitedDataBlks.count(off)) return;
                visitedDataBlks.insert(off);

                Mdf4Blk db;
                if (!rdMdf4Blk(off, db)) return;

                if (std::memcmp(db.id, "##DT", 4) == 0 || std::memcmp(db.id, "##RD", 4) == 0) {
                    size_t take = static_cast<size_t>(std::min<uint64_t>(db.dataLen, kMaxDataStream - outStream.size()));
                    const uint8_t* sp = data + static_cast<size_t>(db.dataOff);
                    outStream.insert(outStream.end(), sp, sp + take);
                } else if (std::memcmp(db.id, "##DZ", 4) == 0) {
                    if (db.dataLen < 24ULL) return;
                    const size_t doff = static_cast<size_t>(db.dataOff);
                    const uint8_t zipType = data[doff + 2];
                    const uint32_t zipParam = DetailEda200::RdU32LE(data + doff + 4);
                    const uint64_t orgLen = DetailEda200::RdU64LE(data + doff + 8);
                    const uint64_t cmpLen = DetailEda200::RdU64LE(data + doff + 16);
                    if (orgLen == 0ULL || orgLen > kMaxDataStream || 24ULL + cmpLen > db.dataLen) return;

                    const uint8_t* cmpPtr = data + doff + 24;
                    std::vector<uint8_t> inflated;
                    bool infOk = Explorer2::ZipDeflate::InflateZlib(cmpPtr, static_cast<size_t>(cmpLen), inflated, static_cast<size_t>(orgLen) + 64u);
                    if (!infOk) {
                        inflated.clear();
                        infOk = Explorer2::ZipDeflate::InflateRaw(cmpPtr, static_cast<size_t>(cmpLen), inflated, static_cast<size_t>(orgLen) + 64u);
                    }
                    if (!infOk || inflated.empty()) return;
                    if (inflated.size() > static_cast<size_t>(orgLen)) {
                        inflated.resize(static_cast<size_t>(orgLen));
                    }

                    if (zipType == 1u && zipParam > 1u && inflated.size() >= static_cast<size_t>(zipParam)) {
                        const size_t M = static_cast<size_t>(zipParam);
                        const size_t N = inflated.size() / M;
                        std::vector<uint8_t> untrans(inflated.size());
                        for (size_t r = 0; r < N; ++r) {
                            for (size_t c = 0; c < M; ++c) {
                                untrans[r * M + c] = inflated[c * N + r];
                            }
                        }
                        const size_t mainBytes = N * M;
                        if (mainBytes < inflated.size()) {
                            std::memcpy(untrans.data() + mainBytes, inflated.data() + mainBytes, inflated.size() - mainBytes);
                        }
                        size_t take = std::min<size_t>(untrans.size(), kMaxDataStream - outStream.size());
                        outStream.insert(outStream.end(), untrans.begin(), untrans.begin() + static_cast<ptrdiff_t>(take));
                    } else {
                        size_t take = std::min<size_t>(inflated.size(), kMaxDataStream - outStream.size());
                        outStream.insert(outStream.end(), inflated.begin(), inflated.begin() + static_cast<ptrdiff_t>(take));
                    }
                } else if (std::memcmp(db.id, "##DL", 4) == 0) {
                    for (size_t li = 1; li < db.links.size(); ++li) {
                        if (db.links[li] != 0ULL) self(self, db.links[li], outStream);
                    }
                    if (!db.links.empty() && db.links[0] != 0ULL) {
                        self(self, db.links[0], outStream);
                    }
                } else if (std::memcmp(db.id, "##HL", 4) == 0) {
                    if (!db.links.empty() && db.links[0] != 0ULL) {
                        self(self, db.links[0], outStream);
                    }
                }
            };

            Mdf4Blk hd;
            if (!rdMdf4Blk(64ULL, hd) || std::memcmp(hd.id, "##HD", 4) != 0 || hd.links.empty()) return false;

            std::set<uint64_t> visitedDG;
            uint64_t dgOff = hd.links[0];
            while (dgOff != 0ULL && !visitedDG.count(dgOff) && channels.size() < 12u) {
                visitedDG.insert(dgOff);
                Mdf4Blk dg;
                if (!rdMdf4Blk(dgOff, dg) || std::memcmp(dg.id, "##DG", 4) != 0) break;
                const uint64_t nextDG = (dg.links.size() > 0) ? dg.links[0] : 0ULL;
                const uint64_t firstCG = (dg.links.size() > 1) ? dg.links[1] : 0ULL;
                const uint64_t dataLink = (dg.links.size() > 2) ? dg.links[2] : 0ULL;
                const uint8_t recIdSize = (dg.dataLen >= 1ULL) ? data[static_cast<size_t>(dg.dataOff)] : 0u;

                std::vector<uint8_t> rawStream;
                visitedDataBlks.clear();
                if (dataLink != 0ULL) {
                    appendMdf4DataStream(appendMdf4DataStream, dataLink, rawStream);
                }

                struct Mdf4ChSpec {
                    std::string name;
                    std::string unit;
                    uint8_t cnType = 0;
                    uint8_t dataType = 0;
                    uint8_t bitOff = 0;
                    uint32_t byteOff = 0;
                    uint32_t bitCnt = 0;
                    MdfConv conv;
                };
                struct Mdf4CgSpec {
                    uint64_t recId = 0;
                    uint64_t cycleCnt = 0;
                    uint32_t dataBytes = 0;
                    uint32_t invalBytes = 0;
                    std::vector<Mdf4ChSpec> chs;
                    int masterIdx = -1;
                };
                std::vector<Mdf4CgSpec> cgs;

                std::set<uint64_t> visitedCG;
                uint64_t cgOff = firstCG;
                while (cgOff != 0ULL && !visitedCG.count(cgOff) && cgs.size() < 16u) {
                    visitedCG.insert(cgOff);
                    Mdf4Blk cg;
                    if (!rdMdf4Blk(cgOff, cg) || std::memcmp(cg.id, "##CG", 4) != 0 || cg.dataLen < 24ULL) break;
                    const size_t cdo = static_cast<size_t>(cg.dataOff);

                    Mdf4CgSpec cgSpec;
                    cgSpec.recId = DetailEda200::RdU64LE(data + cdo + 0);
                    cgSpec.cycleCnt = DetailEda200::RdU64LE(data + cdo + 8);
                    if (cg.dataLen >= 32ULL) {
                        cgSpec.dataBytes  = DetailEda200::RdU32LE(data + cdo + 24);
                        cgSpec.invalBytes = DetailEda200::RdU32LE(data + cdo + 28);
                    } else {
                        cgSpec.dataBytes  = DetailEda200::RdU32LE(data + cdo + 16);
                        cgSpec.invalBytes = DetailEda200::RdU32LE(data + cdo + 20);
                    }

                    std::set<uint64_t> visitedCN;
                    uint64_t cnOff = (cg.links.size() > 1) ? cg.links[1] : 0ULL;
                    while (cnOff != 0ULL && !visitedCN.count(cnOff) && cgSpec.chs.size() < 32u) {
                        visitedCN.insert(cnOff);
                        Mdf4Blk cn;
                        if (!rdMdf4Blk(cnOff, cn) || std::memcmp(cn.id, "##CN", 4) != 0 || cn.dataLen < 12ULL) break;
                        const size_t ndo = static_cast<size_t>(cn.dataOff);

                        Mdf4ChSpec cs;
                        cs.cnType   = data[ndo + 0];
                        cs.dataType = data[ndo + 2];
                        cs.bitOff   = data[ndo + 3];
                        cs.byteOff  = DetailEda200::RdU32LE(data + ndo + 4);
                        cs.bitCnt   = DetailEda200::RdU32LE(data + ndo + 8);
                        if (cn.links.size() > 2 && cn.links[2] != 0ULL) cs.name = rdMdf4Text(cn.links[2]);
                        if (cn.links.size() > 4 && cn.links[4] != 0ULL) cs.conv = rdMdf4Conv(cn.links[4]);
                        if (cn.links.size() > 6 && cn.links[6] != 0ULL && cs.conv.unit.empty()) cs.unit = rdMdf4Text(cn.links[6]);
                        else cs.unit = cs.conv.unit;

                        if (cs.name.empty()) {
                            cs.name = ((cs.cnType == 2u || cs.cnType == 3u) ? "time" : ("ch_" + std::to_string(cgSpec.chs.size())));
                        }
                        if ((cs.cnType == 2u || cs.cnType == 3u) && cgSpec.masterIdx < 0) {
                            cgSpec.masterIdx = static_cast<int>(cgSpec.chs.size());
                        }
                        cgSpec.chs.push_back(std::move(cs));
                        cnOff = !cn.links.empty() ? cn.links[0] : 0ULL;
                    }

                    cgs.push_back(std::move(cgSpec));
                    cgOff = !cg.links.empty() ? cg.links[0] : 0ULL;
                }

                if (!rawStream.empty() && !cgs.empty()) {
                    for (const auto& cgSpec : cgs) {
                        if (channels.size() >= 12u) break;
                        const size_t recPayBytes = static_cast<size_t>(cgSpec.dataBytes) + static_cast<size_t>(cgSpec.invalBytes);
                        if (recPayBytes == 0u || cgSpec.cycleCnt == 0ULL) continue;

                        // Collect matching record pointers (sorted recIdSize==0 or unsorted recIdSize in {1,2,4,8})
                        std::vector<const uint8_t*> recPtrs;
                        if (recIdSize == 0u) {
                            size_t availRecs = rawStream.size() / recPayBytes;
                            size_t nRecs = static_cast<size_t>(std::min<uint64_t>(cgSpec.cycleCnt, availRecs));
                            recPtrs.reserve(std::min<size_t>(nRecs, 65536u));
                            for (size_t r = 0; r < nRecs && recPtrs.size() < 65536u; ++r) {
                                recPtrs.push_back(rawStream.data() + r * recPayBytes);
                            }
                        } else if (recIdSize == 1u || recIdSize == 2u || recIdSize == 4u || recIdSize == 8u) {
                            std::unordered_map<uint64_t, size_t> idToPaySz;
                            for (const auto& ocg : cgs) {
                                idToPaySz[ocg.recId] = static_cast<size_t>(ocg.dataBytes) + static_cast<size_t>(ocg.invalBytes);
                            }
                            size_t rp = 0;
                            while (rp + recIdSize <= rawStream.size() && recPtrs.size() < 65536u) {
                                uint64_t rid = 0;
                                if (recIdSize == 1u) rid = rawStream[rp];
                                else if (recIdSize == 2u) rid = DetailEda200::RdU16LE(rawStream.data() + rp);
                                else if (recIdSize == 4u) rid = DetailEda200::RdU32LE(rawStream.data() + rp);
                                else rid = DetailEda200::RdU64LE(rawStream.data() + rp);

                                auto szIt = idToPaySz.find(rid);
                                if (szIt == idToPaySz.end() || szIt->second == 0u || rp + recIdSize + szIt->second > rawStream.size()) break;
                                if (rid == cgSpec.recId) {
                                    recPtrs.push_back(rawStream.data() + rp + recIdSize);
                                }
                                rp += recIdSize + szIt->second;
                            }
                        }

                        if (recPtrs.empty()) continue;
                        const size_t totalRecs = recPtrs.size();
                        const size_t strideStep = std::max<size_t>(1u, totalRecs / 512u);

                        for (size_t ci = 0; ci < cgSpec.chs.size() && channels.size() < 12u; ++ci) {
                            const auto& cs = cgSpec.chs[ci];
                            if (static_cast<int>(ci) == cgSpec.masterIdx) continue;
                            if (cs.cnType != 0u && cs.cnType != 4u && cs.cnType != 6u) continue;

                            MdfDecodedCh dch;
                            dch.name = PathUtils::Utf8ToWide(cs.name);
                            dch.unit = PathUtils::Utf8ToWide(cs.unit);
                            dch.totalRecords = totalRecs;

                            for (size_t r = 0; r < totalRecs; ++r) {
                                double rawY = 0.0;
                                if (cs.cnType == 6u && cs.bitCnt == 0u) {
                                    rawY = static_cast<double>(r);
                                } else if (!extractRawSample(recPtrs[r], cgSpec.dataBytes, cs.byteOff, cs.bitOff, cs.bitCnt, cs.dataType, rawY)) {
                                    continue;
                                }
                                double phyY = cs.conv.Apply(rawY);
                                if (!std::isfinite(phyY)) continue;
                                dch.yMin = std::min(dch.yMin, phyY);
                                dch.yMax = std::max(dch.yMax, phyY);

                                if ((r % strideStep) == 0u || r + 1u == totalRecs) {
                                    double phyT = static_cast<double>(r);
                                    if (cgSpec.masterIdx >= 0) {
                                        const auto& mc = cgSpec.chs[static_cast<size_t>(cgSpec.masterIdx)];
                                        double rawT = static_cast<double>(r);
                                        if (mc.cnType == 3u) {
                                            phyT = mc.conv.Apply(rawT);
                                        } else if (extractRawSample(recPtrs[r], cgSpec.dataBytes, mc.byteOff, mc.bitOff, mc.bitCnt, mc.dataType, rawT)) {
                                            phyT = mc.conv.Apply(rawT);
                                        }
                                    }
                                    if (!std::isfinite(phyT)) phyT = static_cast<double>(r);
                                    dch.t.push_back(phyT);
                                    dch.y.push_back(phyY);
                                }
                            }

                            if (!dch.y.empty()) {
                                channels.push_back(std::move(dch));
                            }
                        }
                    }
                }
                dgOff = nextDG;
            }
        } else {
            // ASAM MDF 3.x ("HD" / "DG" / "CG" / "CN" / "CC" / "TX")
            const bool be = (DetailEda200::RdU16LE(data + 24) != 0u);
            auto rd16 = [&](size_t o) -> uint16_t {
                return be ? DetailEda200::RdU16BE(data + o) : DetailEda200::RdU16LE(data + o);
            };
            auto rd32 = [&](size_t o) -> uint32_t {
                return be ? DetailEda200::RdU32BE(data + o) : DetailEda200::RdU32LE(data + o);
            };
            auto rdF64 = [&](size_t o) -> double {
                uint64_t u = be
                    ? ((static_cast<uint64_t>(DetailEda200::RdU32BE(data + o)) << 32) |
                        static_cast<uint64_t>(DetailEda200::RdU32BE(data + o + 4)))
                    : DetailEda200::RdU64LE(data + o);
                double dv = 0.0;
                std::memcpy(&dv, &u, 8);
                return dv;
            };
            auto rdFixedStr = [&](size_t o, size_t maxN) -> std::string {
                if (o + maxN > size) return {};
                const char* p = reinterpret_cast<const char*>(data + o);
                size_t sl = strnlen(p, maxN);
                std::string s(p, sl);
                while (!s.empty() && static_cast<unsigned char>(s.back()) <= 0x20u) s.pop_back();
                return s;
            };
            auto rdMdf3Tx = [&](uint32_t off) -> std::string {
                if (off < 64u || static_cast<size_t>(off) + 4u > size) return {};
                if (data[off] != 'T' || data[off + 1] != 'X') return {};
                uint16_t bsz = rd16(off + 2);
                if (bsz <= 4u || static_cast<size_t>(off) + bsz > size) return {};
                return rdFixedStr(static_cast<size_t>(off) + 4u, static_cast<size_t>(bsz - 4u));
            };
            auto rdMdf3Cc = [&](uint32_t off) -> MdfConv {
                MdfConv cv;
                if (off < 64u || static_cast<size_t>(off) + 46u > size) return cv;
                if (data[off] != 'C' || data[off + 1] != 'C') return cv;
                uint16_t bsz = rd16(off + 2);
                if (bsz < 46u || static_cast<size_t>(off) + bsz > size) return cv;
                cv.unit = rdFixedStr(static_cast<size_t>(off) + 22u, 20u);
                uint16_t ccType = rd16(static_cast<size_t>(off) + 42u);
                uint16_t nPar   = rd16(static_cast<size_t>(off) + 44u);
                if (ccType == 0u && nPar >= 2u && static_cast<size_t>(off) + 46u + nPar * 8u <= size) {
                    cv.type = 1u;
                    cv.p = { rdF64(static_cast<size_t>(off) + 46u), rdF64(static_cast<size_t>(off) + 54u) };
                } else if (ccType == 9u && nPar >= 6u && static_cast<size_t>(off) + 46u + nPar * 8u <= size) {
                    cv.type = 2u;
                    for (uint16_t i = 0; i < 6u; ++i) cv.p.push_back(rdF64(static_cast<size_t>(off) + 46u + i * 8u));
                }
                return cv;
            };
            auto mapMdf3Type = [&](uint16_t sdt) -> uint8_t {
                switch (sdt) {
                case 0u:  return be ? 1u : 0u; // UINT default
                case 1u:  return be ? 3u : 2u; // SINT default
                case 2u:
                case 3u:  return be ? 5u : 4u; // FLOAT32/64 default
                case 9u:  return 1u;           // UINT_BE
                case 10u: return 3u;           // SINT_BE
                case 11u:
                case 12u: return 5u;           // FLOAT_BE
                case 13u: return 0u;           // UINT_LE
                case 14u: return 2u;           // SINT_LE
                case 15u:
                case 16u: return 4u;           // FLOAT_LE
                default:  return 255u;
                }
            };

            uint16_t hdSize = rd16(66);
            if (hdSize < 20u || 64u + static_cast<size_t>(hdSize) > size) return false;
            uint32_t dgOff = rd32(68);

            std::set<uint32_t> visitedDG;
            while (dgOff >= 64u && static_cast<size_t>(dgOff) + 24u <= size && !visitedDG.count(dgOff) && channels.size() < 12u) {
                visitedDG.insert(dgOff);
                if (data[dgOff] != 'D' || data[dgOff + 1] != 'G') break;
                uint16_t dgSize = rd16(dgOff + 2);
                if (dgSize < 24u || static_cast<size_t>(dgOff) + dgSize > size) break;

                uint32_t nextDG    = rd32(dgOff + 4);
                uint32_t firstCG   = rd32(dgOff + 8);
                uint32_t dataOff   = rd32(dgOff + 16);
                uint16_t numRecIds = rd16(dgOff + 22);

                struct Mdf3ChSpec {
                    std::string name;
                    uint16_t cnType = 0; // 0=data, 1=time
                    uint32_t byteOff = 0;
                    uint8_t bitOff = 0;
                    uint32_t bitCnt = 0;
                    uint8_t dataType = 0;
                    MdfConv conv;
                };
                struct Mdf3CgSpec {
                    uint16_t recId = 0;
                    uint16_t recBytes = 0;
                    uint32_t numRecs = 0;
                    std::vector<Mdf3ChSpec> chs;
                    int masterIdx = -1;
                };
                std::vector<Mdf3CgSpec> cgs;

                std::set<uint32_t> visitedCG;
                uint32_t cgOff = firstCG;
                while (cgOff >= 64u && static_cast<size_t>(cgOff) + 26u <= size && !visitedCG.count(cgOff) && cgs.size() < 16u) {
                    visitedCG.insert(cgOff);
                    if (data[cgOff] != 'C' || data[cgOff + 1] != 'G') break;
                    uint16_t cgSize = rd16(cgOff + 2);
                    if (cgSize < 26u || static_cast<size_t>(cgOff) + cgSize > size) break;

                    Mdf3CgSpec cgSpec;
                    uint32_t nextCG  = rd32(cgOff + 4);
                    uint32_t firstCN = rd32(cgOff + 8);
                    cgSpec.recId     = rd16(cgOff + 16);
                    cgSpec.recBytes  = rd16(cgOff + 20);
                    cgSpec.numRecs   = rd32(cgOff + 22);

                    std::set<uint32_t> visitedCN;
                    uint32_t cnOff = firstCN;
                    while (cnOff >= 64u && static_cast<size_t>(cnOff) + 216u <= size && !visitedCN.count(cnOff) && cgSpec.chs.size() < 32u) {
                        visitedCN.insert(cnOff);
                        if (data[cnOff] != 'C' || data[cnOff + 1] != 'N') break;
                        uint16_t cnSize = rd16(cnOff + 2);
                        if (cnSize < 216u || static_cast<size_t>(cnOff) + cnSize > size) break;

                        uint32_t nextCN = rd32(cnOff + 4);
                        uint32_t convCC = rd32(cnOff + 8);

                        // Standard ASAM MDF 3.x layout (5 links: +4..+23; cnType at +24, shortName at +26, startBit at +186, numBits at +188, sigType at +190)
                        uint16_t cnType   = rd16(cnOff + 24);
                        std::string sName = rdFixedStr(static_cast<size_t>(cnOff) + 26u, 32u);
                        uint16_t startBit = rd16(cnOff + 186);
                        uint16_t numBits  = rd16(cnOff + 188);
                        uint16_t sigDType = rd16(cnOff + 190);
                        // Fallback if a compact synthetic CN omitted the 5th link
                        if ((numBits == 0u || numBits > 64u) && rd16(cnOff + 186) >= 1u && rd16(cnOff + 186) <= 64u) {
                            cnType   = rd16(cnOff + 22);
                            sName    = rdFixedStr(static_cast<size_t>(cnOff) + 24u, 32u);
                            startBit = rd16(cnOff + 184);
                            numBits  = rd16(cnOff + 186);
                            sigDType = rd16(cnOff + 188);
                        }
                        uint32_t addByteOff = (cnSize >= 228u) ? static_cast<uint32_t>(rd16(cnOff + 226)) : 0u;
                        if (cnSize >= 222u) {
                            uint32_t longTx = rd32(cnOff + 218);
                            std::string lName = rdMdf3Tx(longTx);
                            if (!lName.empty()) sName = std::move(lName);
                        }

                        uint8_t mappedDt = mapMdf3Type(sigDType);
                        if (mappedDt != 255u && numBits >= 1u && numBits <= 64u) {
                            Mdf3ChSpec cs;
                            cs.name = sName.empty() ? ("ch_" + std::to_string(cgSpec.chs.size())) : std::move(sName);
                            cs.cnType = cnType;
                            uint32_t totalBitOff = addByteOff * 8u + static_cast<uint32_t>(startBit);
                            cs.byteOff = totalBitOff / 8u;
                            cs.bitOff  = static_cast<uint8_t>(totalBitOff % 8u);
                            cs.bitCnt  = numBits;
                            cs.dataType = mappedDt;
                            if (convCC != 0u) cs.conv = rdMdf3Cc(convCC);

                            if (cs.cnType == 1u && cgSpec.masterIdx < 0) {
                                cgSpec.masterIdx = static_cast<int>(cgSpec.chs.size());
                            }
                            cgSpec.chs.push_back(std::move(cs));
                        }
                        cnOff = nextCN;
                    }

                    cgs.push_back(std::move(cgSpec));
                    cgOff = nextCG;
                }

                if (dataOff >= 64u && static_cast<size_t>(dataOff) < size && !cgs.empty()) {
                    const size_t streamAvail = size - static_cast<size_t>(dataOff);
                    const uint8_t* streamBase = data + dataOff;

                    for (const auto& cgSpec : cgs) {
                        if (channels.size() >= 12u) break;
                        if (cgSpec.recBytes == 0u || cgSpec.numRecs == 0u) continue;

                        std::vector<const uint8_t*> recPtrs;
                        if (numRecIds == 0u) {
                            size_t availRecs = streamAvail / cgSpec.recBytes;
                            size_t nRecs = std::min<size_t>(cgSpec.numRecs, availRecs);
                            recPtrs.reserve(std::min<size_t>(nRecs, 65536u));
                            for (size_t r = 0; r < nRecs && recPtrs.size() < 65536u; ++r) {
                                recPtrs.push_back(streamBase + r * cgSpec.recBytes);
                            }
                        } else if (numRecIds == 1u || numRecIds == 2u) {
                            std::unordered_map<uint16_t, size_t> idSz;
                            for (const auto& ocg : cgs) idSz[ocg.recId] = ocg.recBytes;
                            size_t suffix = (numRecIds == 2u) ? 1u : 0u;
                            size_t rp = 0;
                            while (rp + 1u <= streamAvail && recPtrs.size() < 65536u) {
                                uint16_t rid = streamBase[rp];
                                auto it = idSz.find(rid);
                                if (it == idSz.end() || it->second == 0u || rp + 1u + it->second + suffix > streamAvail) break;
                                if (rid == cgSpec.recId) recPtrs.push_back(streamBase + rp + 1u);
                                rp += 1u + it->second + suffix;
                            }
                        }

                        if (recPtrs.empty()) continue;
                        const size_t totalRecs = recPtrs.size();
                        const size_t strideStep = std::max<size_t>(1u, totalRecs / 512u);

                        for (size_t ci = 0; ci < cgSpec.chs.size() && channels.size() < 12u; ++ci) {
                            const auto& cs = cgSpec.chs[ci];
                            if (static_cast<int>(ci) == cgSpec.masterIdx || cs.cnType == 1u) continue;

                            MdfDecodedCh dch;
                            dch.name = PathUtils::Utf8ToWide(cs.name);
                            dch.unit = PathUtils::Utf8ToWide(cs.conv.unit);
                            dch.totalRecords = totalRecs;

                            for (size_t r = 0; r < totalRecs; ++r) {
                                double rawY = 0.0;
                                if (!extractRawSample(recPtrs[r], cgSpec.recBytes, cs.byteOff, cs.bitOff, cs.bitCnt, cs.dataType, rawY)) {
                                    continue;
                                }
                                double phyY = cs.conv.Apply(rawY);
                                if (!std::isfinite(phyY)) continue;
                                dch.yMin = std::min(dch.yMin, phyY);
                                dch.yMax = std::max(dch.yMax, phyY);

                                if ((r % strideStep) == 0u || r + 1u == totalRecs) {
                                    double phyT = static_cast<double>(r);
                                    if (cgSpec.masterIdx >= 0) {
                                        const auto& mc = cgSpec.chs[static_cast<size_t>(cgSpec.masterIdx)];
                                        double rawT = 0.0;
                                        if (extractRawSample(recPtrs[r], cgSpec.recBytes, mc.byteOff, mc.bitOff, mc.bitCnt, mc.dataType, rawT)) {
                                            phyT = mc.conv.Apply(rawT);
                                        }
                                    }
                                    if (!std::isfinite(phyT)) phyT = static_cast<double>(r);
                                    dch.t.push_back(phyT);
                                    dch.y.push_back(phyY);
                                }
                            }

                            if (!dch.y.empty()) {
                                channels.push_back(std::move(dch));
                            }
                        }
                    }
                }
                dgOff = nextDG;
            }
        }

        // Zero-Cheat Guarantee: reject files with zero decoded signal channels or zero data samples
        if (channels.empty()) return false;

        double globalTMin =  1e300;
        double globalTMax = -1e300;
        size_t totalPtsCount = 0;
        for (const auto& ch : channels) {
            totalPtsCount += ch.totalRecords;
            for (double tv : ch.t) {
                globalTMin = std::min(globalTMin, tv);
                globalTMax = std::max(globalTMax, tv);
            }
        }
        if (totalPtsCount == 0u) return false;
        if (!std::isfinite(globalTMin) || !std::isfinite(globalTMax)) {
            globalTMin = 0.0;
            globalTMax = 1.0;
        }
        double globalTSpan = (globalTMax > globalTMin + 1e-12) ? (globalTMax - globalTMin) : 1.0;

        out.kind = EdaDocKind::KiCadSch;
        out.netCount = channels.size();
        if (verStr.empty()) verStr = isMdf4 ? "4.10" : "3.30";
        out.formatTitle = L"ASAM MDF " + PathUtils::Utf8ToWide(verStr) + L" ECU Telemetry (" +
                          std::to_wstring(channels.size()) + L" ch, " + ext + L")";

        DetailEda200::DrawGraticule(out, -40.0f, -24.0f, 80.0f, 48.0f, 10, 8);

        wchar_t tSpanBuf[128] = {};
        swprintf_s(tSpanBuf, L"ASAM MDF %hs — %zu ch | t = [%.3g .. %.3g s]",
            verStr.c_str(), channels.size(), globalTMin, globalTMax);
        out.labels.push_back({ -39.0f, 25.6f, 1.05f, EdaLayerId::SchLabel, tSpanBuf });

        const float laneStep = 44.0f / static_cast<float>(channels.size());
        const float halfAmp  = std::min(laneStep * 0.38f, 14.0f);
        static const EdaLayerId kLaneLayers[4] = {
            EdaLayerId::TopCopper,
            EdaLayerId::SchWire,
            EdaLayerId::InnerCopper,
            EdaLayerId::BottomCopper
        };

        for (size_t ci = 0; ci < channels.size(); ++ci) {
            const auto& ch = channels[ci];
            const float centerY = 22.0f - (static_cast<float>(ci) + 0.5f) * laneStep;
            const EdaLayerId lid = kLaneLayers[ci % 4u];
            const int netId = static_cast<int>(ci + 1u);

            std::wstring leftLbl = ch.name + (ch.unit.empty() ? L"" : (L" [" + ch.unit + L"]"));
            out.labels.push_back({ -39.2f, centerY + halfAmp + 0.7f, 0.92f, EdaLayerId::SchLabel, leftLbl });

            wchar_t statBuf[96] = {};
            swprintf_s(statBuf, L"[%.3g..%.3g%ls] (%zu pts)",
                ch.yMin, ch.yMax, ch.unit.empty() ? L"" : (L" " + ch.unit).c_str(), ch.totalRecords);
            out.labels.push_back({ 8.0f, centerY + halfAmp + 0.7f, 0.85f, EdaLayerId::SchLabel, statBuf });

            const double ySpan = ch.yMax - ch.yMin;
            auto mapX = [&](double tv) -> float {
                double u = std::clamp((tv - globalTMin) / globalTSpan, 0.0, 1.0);
                return -38.0f + static_cast<float>(u * 76.0);
            };
            auto mapY = [&](double yv) -> float {
                if (ySpan <= 1e-12) return centerY;
                double norm = std::clamp((yv - ch.yMin) / ySpan, 0.0, 1.0);
                return centerY + static_cast<float>(norm * 2.0 - 1.0) * halfAmp;
            };

            if (ch.y.size() == 1u) {
                float y0 = mapY(ch.y[0]);
                out.traces.push_back({ -38.0f, y0, 38.0f, y0, 0.30f, lid, netId });
            } else {
                for (size_t i = 1u; i < ch.y.size(); ++i) {
                    out.traces.push_back({
                        mapX(ch.t[i - 1u]), mapY(ch.y[i - 1u]),
                        mapX(ch.t[i]),      mapY(ch.y[i]),
                        0.32f, lid, netId
                    });
                }
            }

            EdaComponent3D comp;
            comp.refDes = ch.name;
            comp.value = statBuf;
            comp.footprint = isMdf4 ? L"MDF4_CN" : L"MDF3_CN";
            comp.x = -36.0f;
            comp.y = centerY;
            comp.widthMm = 6.0f;
            comp.lengthMm = std::max(2.0f, halfAmp * 1.5f);
            comp.pinCount = static_cast<int>(std::min<size_t>(ch.totalRecords, 65535u));
            out.components.push_back(std::move(comp));
        }

        return !out.traces.empty();
    }
    return false;
}

// #75. Automotive LIN Network Description File (.ldf) — Schematic Bus Topology
inline bool ParseLinLdf200(std::string_view sv, ParsedEdaBoard& out) {
    auto drawSchRect = [&](float cx, float cy, float w, float h, float strokeW, EdaLayerId lid, int netId = 0) {
        DrawFieldbusSchRect(out, cx, cy, w, h, strokeW, lid, netId);
    };
    {
        if (sv.find("LIN_description_file") == std::string_view::npos) return false;

        // Strip /* ... */ and // ... comments
        std::string clean;
        clean.reserve(sv.size());
        for (size_t i = 0; i < sv.size(); ) {
            if (i + 1 < sv.size() && sv[i] == '/' && sv[i + 1] == '*') {
                size_t endC = sv.find("*/", i + 2);
                i = (endC == std::string_view::npos) ? sv.size() : (endC + 2);
                clean.push_back(' ');
            } else if (i + 1 < sv.size() && sv[i] == '/' && sv[i + 1] == '/') {
                size_t eol = sv.find('\n', i + 2);
                i = (eol == std::string_view::npos) ? sv.size() : (eol + 1);
                clean.push_back('\n');
            } else {
                clean.push_back(sv[i++]);
            }
        }

        auto trimAscii = [](std::string_view s) -> std::string {
            while (!s.empty() && static_cast<unsigned char>(s.front()) <= 0x20u) s.remove_prefix(1);
            while (!s.empty() && static_cast<unsigned char>(s.back()) <= 0x20u) s.remove_suffix(1);
            return std::string(s);
        };
        auto findBlockBody = [&](const char* kw, size_t startFrom = 0) -> std::string_view {
            size_t p = clean.find(kw, startFrom);
            while (p != std::string::npos) {
                bool okBefore = (p == 0 || !std::isalnum(static_cast<unsigned char>(clean[p - 1])) && clean[p - 1] != '_');
                size_t after = p + std::strlen(kw);
                bool okAfter = (after >= clean.size() || (!std::isalnum(static_cast<unsigned char>(clean[after])) && clean[after] != '_'));
                if (okBefore && okAfter) {
                    size_t b0 = clean.find('{', after);
                    if (b0 == std::string::npos) return {};
                    int depth = 0;
                    for (size_t k = b0; k < clean.size(); ++k) {
                        if (clean[k] == '{') ++depth;
                        else if (clean[k] == '}') {
                            if (--depth == 0) return std::string_view(clean).substr(b0 + 1, k - b0 - 1);
                        }
                    }
                    return {};
                }
                p = clean.find(kw, after);
            }
            return {};
        };

        double linSpeedKbps = 0.0;
        {
            size_t sp = clean.find("LIN_speed");
            if (sp != std::string::npos) {
                size_t eq = clean.find('=', sp);
                size_t sc = clean.find(';', sp);
                if (eq != std::string::npos && sc != std::string::npos && eq < sc) {
                    linSpeedKbps = std::strtod(clean.c_str() + eq + 1, nullptr);
                }
            }
        }

        std::string masterName;
        std::vector<std::string> slaveNames;
        {
            std::string_view nodesBlk = findBlockBody("Nodes");
            size_t pos = 0;
            while (pos < nodesBlk.size()) {
                size_t sc = nodesBlk.find(';', pos);
                if (sc == std::string_view::npos) break;
                std::string_view stmt = nodesBlk.substr(pos, sc - pos);
                pos = sc + 1;
                size_t col = stmt.find(':');
                if (col == std::string_view::npos) continue;
                std::string role = trimAscii(stmt.substr(0, col));
                std::string_view rhs = stmt.substr(col + 1);
                if (role == "Master") {
                    size_t cm = rhs.find(',');
                    masterName = trimAscii(cm == std::string_view::npos ? rhs : rhs.substr(0, cm));
                } else if (role == "Slaves") {
                    size_t rp = 0;
                    while (rp < rhs.size() && slaveNames.size() < 24u) {
                        size_t cm = rhs.find(',', rp);
                        std::string sn = trimAscii(cm == std::string_view::npos ? rhs.substr(rp) : rhs.substr(rp, cm - rp));
                        if (!sn.empty()) slaveNames.push_back(std::move(sn));
                        if (cm == std::string_view::npos) break;
                        rp = cm + 1;
                    }
                }
            }
        }

        struct LinSig {
            std::string name;
            int bitSize = 0;
            std::string publisher;
            std::vector<std::string> subscribers;
        };
        std::vector<LinSig> signals;
        {
            std::string_view sigBlk = findBlockBody("Signals");
            size_t pos = 0;
            while (pos < sigBlk.size() && signals.size() < 64u) {
                size_t sc = sigBlk.find(';', pos);
                if (sc == std::string_view::npos) break;
                std::string_view stmt = sigBlk.substr(pos, sc - pos);
                pos = sc + 1;
                // Ignore nested braces if any
                size_t col = stmt.find(':');
                if (col == std::string_view::npos) continue;
                LinSig sg;
                sg.name = trimAscii(stmt.substr(0, col));
                if (sg.name.empty()) continue;

                std::vector<std::string> fields;
                std::string_view rhs = stmt.substr(col + 1);
                int bDepth = 0;
                size_t fs = 0;
                for (size_t k = 0; k <= rhs.size(); ++k) {
                    if (k < rhs.size() && rhs[k] == '{') ++bDepth;
                    else if (k < rhs.size() && rhs[k] == '}') --bDepth;
                    else if (bDepth == 0 && (k == rhs.size() || rhs[k] == ',')) {
                        std::string f = trimAscii(rhs.substr(fs, k - fs));
                        if (!f.empty()) fields.push_back(std::move(f));
                        fs = k + 1;
                    }
                }
                if (fields.size() >= 3) {
                    sg.bitSize = std::atoi(fields[0].c_str());
                    sg.publisher = fields[2];
                    for (size_t fi = 3; fi < fields.size(); ++fi) {
                        sg.subscribers.push_back(fields[fi]);
                    }
                    signals.push_back(std::move(sg));
                }
            }
        }

        struct LinFrame {
            std::string name;
            uint32_t id = 0;
            std::string publisher;
            int byteSize = 0;
        };
        std::vector<LinFrame> frames;
        {
            std::string_view frmBlk = findBlockBody("Frames");
            size_t pos = 0;
            while (pos < frmBlk.size() && frames.size() < 48u) {
                size_t b0 = frmBlk.find('{', pos);
                if (b0 == std::string_view::npos) break;
                size_t b1 = frmBlk.find('}', b0 + 1);
                if (b1 == std::string_view::npos) break;
                std::string_view hdr = frmBlk.substr(pos, b0 - pos);
                pos = b1 + 1;

                size_t col = hdr.find(':');
                if (col == std::string_view::npos) continue;
                LinFrame fr;
                fr.name = trimAscii(hdr.substr(0, col));
                std::string_view rhs = hdr.substr(col + 1);
                size_t c1 = rhs.find(',');
                size_t c2 = (c1 != std::string_view::npos) ? rhs.find(',', c1 + 1) : std::string_view::npos;
                if (!fr.name.empty() && c1 != std::string_view::npos && c2 != std::string_view::npos) {
                    fr.id = static_cast<uint32_t>(std::strtoul(trimAscii(rhs.substr(0, c1)).c_str(), nullptr, 0));
                    fr.publisher = trimAscii(rhs.substr(c1 + 1, c2 - c1 - 1));
                    fr.byteSize = std::atoi(trimAscii(rhs.substr(c2 + 1)).c_str());
                    frames.push_back(std::move(fr));
                }
            }
        }

        size_t schedEntries = 0;
        {
            std::string_view schBlk = findBlockBody("Schedule_tables");
            for (char c : schBlk) {
                if (c == ';') ++schedEntries;
            }
        }

        // Zero-Cheat: reject empty LDFs with no ECUs and no frames/signals
        if (masterName.empty() && slaveNames.empty() && frames.empty() && signals.empty()) {
            return false;
        }

        out.kind = EdaDocKind::KiCadSch;
        std::vector<std::pair<std::string, bool>> ecuList;
        if (!masterName.empty()) ecuList.push_back({ masterName, true });
        for (const auto& sn : slaveNames) ecuList.push_back({ sn, false });
        if (ecuList.empty()) {
            std::set<std::string> inferred;
            for (const auto& f : frames) if (!f.publisher.empty()) inferred.insert(f.publisher);
            for (const auto& s : signals) if (!s.publisher.empty()) inferred.insert(s.publisher);
            for (const auto& nm : inferred) ecuList.push_back({ nm, false });
        }
        if (ecuList.empty()) return false;

        wchar_t titleBuf[160] = {};
        swprintf_s(titleBuf, L"LIN Bus Network Schematic (.ldf — %.1f kbps, %zu ECUs, %zu frames, %zu signals)",
            linSpeedKbps > 0.0 ? linSpeedKbps : 19.2, ecuList.size(), frames.size(), signals.size());
        out.formatTitle = titleBuf;

        const float pitchX = 24.0f;
        const float busHalfW = std::max(32.0f, static_cast<float>(ecuList.size()) * pitchX * 0.55f);
        // Horizontal single-wire LIN trunk bus
        out.traces.push_back({ -busHalfW, 0.0f, busHalfW, 0.0f, 0.45f, EdaLayerId::SchWire, 1 });
        wchar_t busLbl[96] = {};
        swprintf_s(busLbl, L"LIN Trunk Bus (%.2f kbps%ls)",
            linSpeedKbps > 0.0 ? linSpeedKbps : 19.2,
            schedEntries > 0 ? (L", " + std::to_wstring(schedEntries) + L" sched slots").c_str() : L"");
        out.labels.push_back({ -busHalfW, 1.4f, 1.05f, EdaLayerId::SchWire, busLbl });

        std::unordered_map<std::string, std::pair<float, float>> nodePos;
        for (size_t i = 0; i < ecuList.size(); ++i) {
            float nx = (ecuList.size() == 1u)
                ? 0.0f
                : (-busHalfW * 0.80f + (busHalfW * 1.60f * static_cast<float>(i)) / static_cast<float>(ecuList.size() - 1u));
            bool above = ecuList[i].second || ((i % 2u) == 1u);
            float ny = above ? 14.0f : -14.0f;
            float bw = 15.0f, bh = 7.0f;

            drawSchRect(nx, ny, bw, bh, ecuList[i].second ? 0.36f : 0.28f, EdaLayerId::SchSymbol, 0);
            if (ecuList[i].second) {
                drawSchRect(nx, ny, bw - 1.2f, bh - 1.2f, 0.18f, EdaLayerId::SchSymbol, 0);
            }

            float pinY = above ? (ny - bh * 0.5f) : (ny + bh * 0.5f);
            out.traces.push_back({ nx, pinY, nx, 0.0f, 0.26f, EdaLayerId::SchWire, 1 });
            out.circles.push_back({ nx, 0.0f, 0.50f, 0.25f, true, EdaLayerId::SchWire });
            out.pads.push_back({ nx, pinY, 0.6f, 0.6f, 0.0f, 0.0f, 0, EdaLayerId::SchSymbol, L"LIN", L"LIN_BUS" });

            std::wstring roleTag = ecuList[i].second ? L" [MASTER]" : L" [SLAVE]";
            out.labels.push_back({ nx - bw * 0.44f, ny + 0.6f, 0.95f, EdaLayerId::SchLabel,
                                   PathUtils::Utf8ToWide(ecuList[i].first) + roleTag });

            EdaComponent3D comp;
            comp.refDes = PathUtils::Utf8ToWide(ecuList[i].first);
            comp.value = ecuList[i].second ? L"LIN Master" : L"LIN Slave";
            comp.footprint = L"LIN_ECU";
            comp.x = nx; comp.y = ny;
            comp.widthMm = bw; comp.lengthMm = bh;
            comp.category = EdaPartCategory::IC_Package;
            out.components.push_back(std::move(comp));
            nodePos[ecuList[i].first] = { nx, ny };
        }

        // Annotate Frames & Publisher->Subscriber Signal flows below topology
        float infoY = -24.5f;
        for (size_t fi = 0; fi < std::min<size_t>(frames.size(), 8u); ++fi) {
            const auto& fr = frames[fi];
            wchar_t fBuf[128] = {};
            swprintf_s(fBuf, L"Frame 0x%02X %hs [%dB] Pub: %hs",
                fr.id, fr.name.c_str(), fr.byteSize, fr.publisher.c_str());
            out.labels.push_back({ -busHalfW, infoY - static_cast<float>(fi) * 2.6f, 0.88f, EdaLayerId::SchLabel, fBuf });
        }
        for (size_t si = 0; si < std::min<size_t>(signals.size(), 8u); ++si) {
            const auto& sg = signals[si];
            std::string subs;
            for (size_t k = 0; k < sg.subscribers.size(); ++k) {
                if (k > 0) subs += ",";
                subs += sg.subscribers[k];
            }
            wchar_t sBuf[160] = {};
            swprintf_s(sBuf, L"Sig %hs (%db): %hs -> %hs",
                sg.name.c_str(), sg.bitSize, sg.publisher.c_str(), subs.c_str());
            out.labels.push_back({ 2.0f, infoY - static_cast<float>(si) * 2.6f, 0.88f, EdaLayerId::SchWire, sBuf });

            auto itP = nodePos.find(sg.publisher);
            if (itP != nodePos.end() && !sg.subscribers.empty()) {
                auto itS = nodePos.find(sg.subscribers[0]);
                if (itS != nodePos.end() && itP->first != itS->first) {
                    float yArc = -4.0f - static_cast<float>(si % 4u) * 1.1f;
                    out.traces.push_back({ itP->second.first + 1.2f, 0.0f, itP->second.first + 1.2f, yArc, 0.16f, EdaLayerId::InnerCopper, static_cast<int>(si + 2) });
                    out.traces.push_back({ itP->second.first + 1.2f, yArc, itS->second.first - 1.2f, yArc, 0.16f, EdaLayerId::InnerCopper, static_cast<int>(si + 2) });
                    out.traces.push_back({ itS->second.first - 1.2f, yArc, itS->second.first - 1.2f, 0.0f, 0.16f, EdaLayerId::InnerCopper, static_cast<int>(si + 2) });
                }
            }
        }

        out.netCount = std::max<size_t>(1u, signals.size() + frames.size());
        return true;
    }
}

// #76. AUTOSAR System / SWC Architecture Description (.arxml)
inline bool ParseAutosarArxml200(std::string_view sv, ParsedEdaBoard& out) {
    auto drawSchRect = [&](float cx, float cy, float w, float h, float strokeW, EdaLayerId lid, int netId = 0) {
        DrawFieldbusSchRect(out, cx, cy, w, h, strokeW, lid, netId);
    };
    {
        if (sv.find("<AUTOSAR") == std::string_view::npos) return false;

        auto extractChildShortName = [&](size_t openTagEnd, size_t blockEnd) -> std::string {
            std::string_view sub = sv.substr(openTagEnd, blockEnd - openTagEnd);
            size_t sp = sub.find("<SHORT-NAME>");
            if (sp == std::string_view::npos) return {};
            sp += 12;
            size_t se = sub.find("</SHORT-NAME>", sp);
            if (se == std::string_view::npos) return {};
            std::string_view nm = sub.substr(sp, se - sp);
            while (!nm.empty() && static_cast<unsigned char>(nm.front()) <= 0x20u) nm.remove_prefix(1);
            while (!nm.empty() && static_cast<unsigned char>(nm.back()) <= 0x20u) nm.remove_suffix(1);
            return std::string(nm);
        };

        auto scanElements = [&](std::string_view tagName, std::vector<std::pair<std::string, std::string_view>>& outBlocks) {
            std::string openPat = "<" + std::string(tagName);
            std::string closePat = "</" + std::string(tagName) + ">";
            size_t p = 0;
            while ((p = sv.find(openPat, p)) != std::string_view::npos && outBlocks.size() < 32u) {
                size_t after = p + openPat.size();
                if (after < sv.size() && sv[after] != '>' && sv[after] != ' ' && sv[after] != '\t' && sv[after] != '\r' && sv[after] != '\n') {
                    p = after;
                    continue;
                }
                size_t gt = sv.find('>', after);
                if (gt == std::string_view::npos) break;
                if (sv[gt - 1] == '/') { p = gt + 1; continue; }
                size_t ce = sv.find(closePat, gt + 1);
                if (ce == std::string_view::npos) break;
                std::string sn = extractChildShortName(gt + 1, ce);
                if (!sn.empty()) {
                    outBlocks.push_back({ std::move(sn), sv.substr(gt + 1, ce - gt - 1) });
                }
                p = ce + closePat.size();
            }
        };

        struct ArCluster { std::string kind; std::string name; };
        struct ArSwc {
            std::string kind;
            std::string name;
            std::vector<std::string> pPorts;
            std::vector<std::string> rPorts;
        };

        std::vector<ArCluster> clusters;
        const std::pair<const char*, const char*> clusterTypes[] = {
            { "CAN-CLUSTER", "CAN" },
            { "LIN-CLUSTER", "LIN" },
            { "FLEXRAY-CLUSTER", "FlexRay" },
            { "ETHERNET-CLUSTER", "Ethernet" }
        };
        for (const auto& ct : clusterTypes) {
            std::vector<std::pair<std::string, std::string_view>> blks;
            scanElements(ct.first, blks);
            for (auto& b : blks) clusters.push_back({ ct.second, std::move(b.first) });
        }

        std::vector<std::pair<std::string, std::string_view>> ecuBlks;
        scanElements("ECU-INSTANCE", ecuBlks);

        std::vector<ArSwc> swcs;
        const std::pair<const char*, const char*> swcTypes[] = {
            { "APPLICATION-SW-COMPONENT-TYPE", "AppSWC" },
            { "SENSOR-ACTUATOR-SW-COMPONENT-TYPE", "SensorSWC" },
            { "COMPLEX-DEVICE-DRIVER-SW-COMPONENT-TYPE", "CDD" },
            { "COMPOSITION-SW-COMPONENT-TYPE", "Composition" },
            { "ECU-ABSTRACTION-SW-COMPONENT-TYPE", "EcuAb" },
            { "SERVICE-SW-COMPONENT-TYPE", "ServiceSWC" },
            { "SW-COMPONENT-TYPE", "SWC" }
        };
        for (const auto& st : swcTypes) {
            std::vector<std::pair<std::string, std::string_view>> blks;
            scanElements(st.first, blks);
            for (auto& b : blks) {
                ArSwc swc;
                swc.kind = st.second;
                swc.name = std::move(b.first);
                auto extractSubNames = [&](const char* subTag, std::vector<std::string>& dst) {
                    std::string op = std::string("<") + subTag;
                    std::string cp = std::string("</") + subTag + ">";
                    size_t sp = 0;
                    while ((sp = b.second.find(op, sp)) != std::string_view::npos && dst.size() < 8u) {
                        size_t sgt = b.second.find('>', sp);
                        size_t sce = (sgt != std::string_view::npos) ? b.second.find(cp, sgt) : std::string_view::npos;
                        if (sce == std::string_view::npos) break;
                        std::string_view inner = b.second.substr(sgt + 1, sce - sgt - 1);
                        size_t n0 = inner.find("<SHORT-NAME>");
                        size_t n1 = (n0 != std::string_view::npos) ? inner.find("</SHORT-NAME>", n0 + 12) : std::string_view::npos;
                        if (n0 != std::string_view::npos && n1 != std::string_view::npos) {
                            dst.emplace_back(inner.substr(n0 + 12, n1 - n0 - 12));
                        }
                        sp = sce + cp.size();
                    }
                };
                extractSubNames("P-PORT-PROTOTYPE", swc.pPorts);
                extractSubNames("R-PORT-PROTOTYPE", swc.rPorts);
                swcs.push_back(std::move(swc));
            }
        }

        std::vector<std::string> sigNames;
        for (const char* sigTag : { "I-SIGNAL", "SYSTEM-SIGNAL", "PDU-TRIGGERING", "FRAME-TRIGGERING" }) {
            std::vector<std::pair<std::string, std::string_view>> blks;
            scanElements(sigTag, blks);
            for (auto& b : blks) {
                if (sigNames.size() < 24u) sigNames.push_back(std::move(b.first));
            }
        }

        // Zero-Cheat: empty <AUTOSAR></AUTOSAR> or empty <AR-PACKAGE> MUST return false
        if (clusters.empty() && ecuBlks.empty() && swcs.empty() && sigNames.empty()) {
            return false;
        }

        out.kind = EdaDocKind::KiCadSch;
        wchar_t titleBuf[160] = {};
        swprintf_s(titleBuf, L"AUTOSAR Architecture Schematic (.arxml — %zu clusters, %zu ECUs, %zu SWCs, %zu signals)",
            clusters.size(), ecuBlks.size(), swcs.size(), sigNames.size());
        out.formatTitle = titleBuf;

        const size_t nRails = std::max<size_t>(1u, clusters.size());
        const size_t nBlocks = ecuBlks.size() + swcs.size();
        const float busHalfW = std::max(36.0f, static_cast<float>(std::max<size_t>(2u, nBlocks)) * 14.0f);

        // Draw Communication Cluster / VFB Rails
        for (size_t ri = 0; ri < nRails; ++ri) {
            float ry = -static_cast<float>(ri) * 3.8f;
            out.traces.push_back({ -busHalfW, ry, busHalfW, ry, 0.42f, EdaLayerId::SchWire, static_cast<int>(ri + 1) });
            std::wstring railName = !clusters.empty()
                ? (PathUtils::Utf8ToWide(clusters[ri].kind) + L": " + PathUtils::Utf8ToWide(clusters[ri].name))
                : L"AUTOSAR Virtual Function Bus (VFB / RTE)";
            out.labels.push_back({ -busHalfW, ry + 0.9f, 0.95f, EdaLayerId::SchWire, railName });
        }

        // Place ECU-INSTANCE blocks above the cluster rails
        for (size_t ei = 0; ei < ecuBlks.size(); ++ei) {
            float ex = (ecuBlks.size() == 1u)
                ? (-busHalfW * 0.4f)
                : (-busHalfW * 0.78f + (busHalfW * 1.56f * static_cast<float>(ei)) / static_cast<float>(ecuBlks.size() - 1u));
            float ey = 14.0f;
            float bw = 16.0f, bh = 7.2f;
            drawSchRect(ex, ey, bw, bh, 0.34f, EdaLayerId::SchSymbol, 0);
            float targetY = -static_cast<float>(ei % nRails) * 3.8f;
            out.traces.push_back({ ex, ey - bh * 0.5f, ex, targetY, 0.26f, EdaLayerId::SchWire, static_cast<int>((ei % nRails) + 1) });
            out.circles.push_back({ ex, targetY, 0.48f, 0.24f, true, EdaLayerId::SchWire });
            out.pads.push_back({ ex, ey - bh * 0.5f, 0.6f, 0.6f, 0.0f, 0.0f, 1, EdaLayerId::SchSymbol, L"COM", L"" });
            out.labels.push_back({ ex - bw * 0.44f, ey + 0.8f, 0.95f, EdaLayerId::SchLabel, L"ECU: " + PathUtils::Utf8ToWide(ecuBlks[ei].first) });

            EdaComponent3D comp;
            comp.refDes = PathUtils::Utf8ToWide(ecuBlks[ei].first);
            comp.value = L"ECU-INSTANCE";
            comp.footprint = L"AUTOSAR_ECU";
            comp.x = ex; comp.y = ey;
            comp.widthMm = bw; comp.lengthMm = bh;
            comp.category = EdaPartCategory::IC_Package;
            out.components.push_back(std::move(comp));
        }

        // Place SW-COMPONENT blocks below the cluster/VFB rails with R-Ports (left) & P-Ports (right)
        const float swcBaseY = -static_cast<float>(nRails - 1u) * 3.8f - 15.0f;
        for (size_t si = 0; si < swcs.size(); ++si) {
            const auto& sw = swcs[si];
            float sx = (swcs.size() == 1u)
                ? (ecuBlks.empty() ? 0.0f : busHalfW * 0.35f)
                : (-busHalfW * 0.78f + (busHalfW * 1.56f * static_cast<float>(si)) / static_cast<float>(swcs.size() - 1u));
            size_t maxPorts = std::max<size_t>({ 1u, sw.pPorts.size(), sw.rPorts.size() });
            float bw = 17.0f;
            float bh = std::max(7.5f, static_cast<float>(maxPorts) * 2.8f + 3.0f);
            float sy = swcBaseY;

            drawSchRect(sx, sy, bw, bh, 0.30f, EdaLayerId::SchSymbol, 0);
            float targetY = -static_cast<float>(si % nRails) * 3.8f;
            out.traces.push_back({ sx, sy + bh * 0.5f, sx, targetY, 0.24f, EdaLayerId::SchWire, static_cast<int>((si % nRails) + 1) });
            out.circles.push_back({ sx, targetY, 0.45f, 0.22f, true, EdaLayerId::SchWire });

            for (size_t rp = 0; rp < sw.rPorts.size(); ++rp) {
                float py = sy + bh * 0.3f - static_cast<float>(rp) * 2.5f;
                out.traces.push_back({ sx - bw * 0.5f - 2.6f, py, sx - bw * 0.5f, py, 0.24f, EdaLayerId::SchWire, 0 });
                out.pads.push_back({ sx - bw * 0.5f - 2.6f, py, 0.55f, 0.55f, 0.0f, 0.0f, 1, EdaLayerId::SchSymbol, L"R", PathUtils::Utf8ToWide(sw.rPorts[rp]) });
                out.labels.push_back({ sx - bw * 0.5f - 9.0f, py + 0.5f, 0.8f, EdaLayerId::SchWire, L"R:" + PathUtils::Utf8ToWide(sw.rPorts[rp]) });
            }
            for (size_t pp = 0; pp < sw.pPorts.size(); ++pp) {
                float py = sy + bh * 0.3f - static_cast<float>(pp) * 2.5f;
                out.traces.push_back({ sx + bw * 0.5f, py, sx + bw * 0.5f + 2.6f, py, 0.24f, EdaLayerId::SchWire, 0 });
                out.pads.push_back({ sx + bw * 0.5f + 2.6f, py, 0.55f, 0.55f, 0.0f, 0.0f, 1, EdaLayerId::SchSymbol, L"P", PathUtils::Utf8ToWide(sw.pPorts[pp]) });
                out.labels.push_back({ sx + bw * 0.5f + 3.0f, py + 0.5f, 0.8f, EdaLayerId::SchWire, L"P:" + PathUtils::Utf8ToWide(sw.pPorts[pp]) });
            }

            out.labels.push_back({ sx - bw * 0.44f, sy + bh * 0.5f - 1.8f, 0.9f, EdaLayerId::SchLabel,
                                   PathUtils::Utf8ToWide(sw.name) + L" [" + PathUtils::Utf8ToWide(sw.kind) + L"]" });

            EdaComponent3D comp;
            comp.refDes = PathUtils::Utf8ToWide(sw.name);
            comp.value = PathUtils::Utf8ToWide(sw.kind);
            comp.footprint = L"AUTOSAR_SWC";
            comp.x = sx; comp.y = sy;
            comp.widthMm = bw; comp.lengthMm = bh;
            comp.pinCount = static_cast<int>(sw.pPorts.size() + sw.rPorts.size() + 1u);
            comp.category = EdaPartCategory::IC_Package;
            out.components.push_back(std::move(comp));
        }

        // Signal / PDU callouts
        float sigListY = swcs.empty() ? -10.0f : (swcBaseY - 12.0f);
        for (size_t k = 0; k < std::min<size_t>(sigNames.size(), 8u); ++k) {
            float lx = -busHalfW + static_cast<float>(k % 2u) * (busHalfW);
            float ly = sigListY - static_cast<float>(k / 2u) * 2.6f;
            out.labels.push_back({ lx, ly, 0.85f, EdaLayerId::SchWire, L"Signal/PDU: " + PathUtils::Utf8ToWide(sigNames[k]) });
        }

        out.netCount = std::max<size_t>(1u, clusters.size() + sigNames.size());
        return true;
    }
}

// #77. CiA 306 CANopen Electronic Data Sheet / Device Configuration (.eds / .dcf)
inline bool ParseCanOpenEds200(std::string_view sv, const std::wstring& ext, ParsedEdaBoard& out) {
    auto drawSchRect = [&](float cx, float cy, float w, float h, float strokeW, EdaLayerId lid, int netId = 0) {
        DrawFieldbusSchRect(out, cx, cy, w, h, strokeW, lid, netId);
    };
    if (ext == L".eds" || ext == L".dcf") {
        if (sv.find("[DeviceInfo]") == std::string_view::npos &&
            sv.find("[MandatoryObjects]") == std::string_view::npos) {
            return false;
        }

        struct OdObject {
            uint16_t index = 0;
            int subIndex = -1;
            std::string paramName;
            std::string accessType;
            std::string defaultVal;
            uint16_t dataType = 0;
        };

        std::string vendorName, productName;
        int maxBaudKbps = 0;
        std::map<uint16_t, OdObject> mainOd;
        std::map<uint16_t, std::vector<std::string>> pdoMappings;

        std::string curSec;
        uint16_t curIdx = 0;
        int curSub = -1;
        bool curIsOd = false;

        size_t pos = 0;
        while (pos < sv.size()) {
            size_t eol = sv.find('\n', pos);
            if (eol == std::string_view::npos) eol = sv.size();
            std::string_view ln = sv.substr(pos, eol - pos);
            pos = eol + 1;

            size_t sc = ln.find(';');
            if (sc != std::string_view::npos) ln = ln.substr(0, sc);
            while (!ln.empty() && static_cast<unsigned char>(ln.front()) <= 0x20u) ln.remove_prefix(1);
            while (!ln.empty() && static_cast<unsigned char>(ln.back()) <= 0x20u) ln.remove_suffix(1);
            if (ln.empty()) continue;

            if (ln.front() == '[' && ln.back() == ']') {
                curSec.assign(ln.data() + 1, ln.size() - 2);
                curIsOd = false;
                curIdx = 0;
                curSub = -1;
                if (curSec.size() >= 4 && std::isxdigit(static_cast<unsigned char>(curSec[0])) &&
                    std::isxdigit(static_cast<unsigned char>(curSec[1])) &&
                    std::isxdigit(static_cast<unsigned char>(curSec[2])) &&
                    std::isxdigit(static_cast<unsigned char>(curSec[3])))
                {
                    char hex4[5] = { curSec[0], curSec[1], curSec[2], curSec[3], 0 };
                    uint32_t idxVal = std::strtoul(hex4, nullptr, 16);
                    if (idxVal >= 0x1000u && idxVal <= 0x9FFFu) {
                        curIdx = static_cast<uint16_t>(idxVal);
                        if (curSec.size() == 4) {
                            curIsOd = true;
                            mainOd[curIdx].index = curIdx;
                        } else {
                            std::string lowS = PathUtils::ToLowerAscii(curSec.substr(4));
                            if (lowS.rfind("sub", 0) == 0 && lowS.size() > 3) {
                                curSub = static_cast<int>(std::strtol(lowS.c_str() + 3, nullptr, 16));
                                curIsOd = true;
                            }
                        }
                    }
                }
                continue;
            }

            size_t eq = ln.find('=');
            if (eq == std::string_view::npos) continue;
            std::string_view keySv = ln.substr(0, eq);
            std::string_view valSv = ln.substr(eq + 1);
            while (!keySv.empty() && static_cast<unsigned char>(keySv.back()) <= 0x20u) keySv.remove_suffix(1);
            while (!valSv.empty() && static_cast<unsigned char>(valSv.front()) <= 0x20u) valSv.remove_prefix(1);
            std::string key(keySv);
            std::string val(valSv);

            if (curSec == "DeviceInfo") {
                if (key == "VendorName") vendorName = val;
                else if (key == "ProductName") productName = val;
                else if (key.rfind("BaudRate_", 0) == 0 && val == "1") {
                    int b = std::atoi(key.c_str() + 9);
                    maxBaudKbps = std::max(maxBaudKbps, b);
                }
            } else if (curIsOd) {
                if (curSub < 0) {
                    auto& od = mainOd[curIdx];
                    od.index = curIdx;
                    if (key == "ParameterName") od.paramName = val;
                    else if (key == "AccessType") od.accessType = val;
                    else if (key == "DefaultValue") od.defaultVal = val;
                    else if (key == "DataType") od.dataType = static_cast<uint16_t>(std::strtoul(val.c_str(), nullptr, 0));
                } else if (curSub >= 1 && ((curIdx >= 0x1600u && curIdx <= 0x17FFu) || (curIdx >= 0x1A00u && curIdx <= 0x1BFFu))) {
                    if (key == "DefaultValue" && !val.empty()) {
                        uint32_t rawMap = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 0));
                        if (rawMap >= 0x10000001u) {
                            char mbuf[48] = {};
                            sprintf_s(mbuf, "0x%04X:%02X(%ub)", (rawMap >> 16) & 0xFFFFu, (rawMap >> 8) & 0xFFu, rawMap & 0xFFu);
                            pdoMappings[curIdx].emplace_back(mbuf);
                        }
                    }
                }
            }
        }

        // Zero-Cheat: require real Object Dictionary entries [1000]..[9FFF]
        if (mainOd.empty()) return false;

        std::vector<OdObject> rpdoObjs, tpdoObjs, coreObjs;
        for (const auto& kv : mainOd) {
            uint16_t idx = kv.first;
            if (idx >= 0x1400u && idx <= 0x17FFu) rpdoObjs.push_back(kv.second);
            else if (idx >= 0x1800u && idx <= 0x1BFFu) tpdoObjs.push_back(kv.second);
            else coreObjs.push_back(kv.second);
        }

        out.kind = EdaDocKind::KiCadSch;
        std::wstring devTitle = PathUtils::Utf8ToWide(productName.empty() ? (vendorName.empty() ? "CANopen Node" : vendorName) : productName);
        wchar_t titleBuf[192] = {};
        swprintf_s(titleBuf, L"CiA 306 CANopen Node — %ls (%zu OD objects, %zu RPDO, %zu TPDO)",
            devTitle.c_str(), mainOd.size(), rpdoObjs.size(), tpdoObjs.size());
        out.formatTitle = titleBuf;

        const size_t nRows = std::max<size_t>({ 4u, std::min<size_t>(coreObjs.size(), 10u), rpdoObjs.size(), tpdoObjs.size() });
        const float boxW = 34.0f;
        const float boxH = static_cast<float>(nRows) * 4.2f + 8.0f;
        drawSchRect(0.0f, 0.0f, boxW, boxH, 0.38f, EdaLayerId::SchSymbol, 0);

        std::wstring hdrTxt = devTitle;
        if (!vendorName.empty() && productName != vendorName) hdrTxt = PathUtils::Utf8ToWide(vendorName) + L" — " + hdrTxt;
        if (maxBaudKbps > 0) hdrTxt += L" (" + std::to_wstring(maxBaudKbps) + L" kbps)";
        out.labels.push_back({ -boxW * 0.48f, boxH * 0.5f + 2.0f, 1.05f, EdaLayerId::SchLabel, hdrTxt });

        EdaComponent3D devComp;
        devComp.refDes = devTitle;
        devComp.value = std::to_wstring(mainOd.size()) + L" OD entries";
        devComp.footprint = L"CANOPEN_DEVICE";
        devComp.x = 0.0f; devComp.y = 0.0f;
        devComp.widthMm = boxW; devComp.lengthMm = boxH;
        devComp.pinCount = static_cast<int>(rpdoObjs.size() + tpdoObjs.size() + 2u);
        devComp.category = EdaPartCategory::IC_Package;
        out.components.push_back(std::move(devComp));

        // Central Object Dictionary entries list inside device core
        for (size_t ci = 0; ci < std::min<size_t>(coreObjs.size(), 10u); ++ci) {
            const auto& od = coreObjs[ci];
            float ry = boxH * 0.5f - 4.0f - static_cast<float>(ci) * 3.8f;
            wchar_t odLine[128] = {};
            swprintf_s(odLine, L"[0x%04X] %hs%hs",
                od.index,
                od.paramName.empty() ? "Object" : od.paramName.c_str(),
                od.defaultVal.empty() ? "" : (" = " + od.defaultVal).c_str());
            out.labels.push_back({ -boxW * 0.44f, ry, 0.85f, EdaLayerId::SchLabel, odLine });
        }

        // Left side: CAN_H/CAN_L bus + RPDO input pins ([1400]..[17FF])
        const float leftEdge = -boxW * 0.5f;
        const float busX = leftEdge - 20.0f;
        out.traces.push_back({ busX, -boxH * 0.55f, busX, boxH * 0.55f, 0.42f, EdaLayerId::SchWire, 1 });
        out.labels.push_back({ busX - 4.0f, boxH * 0.55f + 1.8f, 0.95f, EdaLayerId::SchWire, L"CAN Bus (RPDO In)" });

        for (size_t ri = 0; ri < std::min<size_t>(rpdoObjs.size(), 8u); ++ri) {
            const auto& rp = rpdoObjs[ri];
            float py = boxH * 0.38f - static_cast<float>(ri) * 4.6f;
            out.traces.push_back({ busX, py, leftEdge, py, 0.26f, EdaLayerId::SchWire, static_cast<int>(ri + 2) });
            out.circles.push_back({ busX, py, 0.45f, 0.22f, true, EdaLayerId::SchWire });
            out.pads.push_back({ leftEdge, py, 0.65f, 0.65f, 0.0f, 0.0f, 1, EdaLayerId::SchSymbol, L"RPDO", L"" });

            std::string mapDesc;
            auto mit = pdoMappings.find(rp.index);
            if (mit != pdoMappings.end() && !mit->second.empty()) mapDesc = " -> " + mit->second.front();
            wchar_t rpLbl[128] = {};
            swprintf_s(rpLbl, L"RPDO [0x%04X] %hs%hs",
                rp.index, rp.paramName.c_str(), mapDesc.c_str());
            out.labels.push_back({ busX + 1.2f, py + 0.8f, 0.82f, EdaLayerId::SchWire, rpLbl });
        }

        // Right side: TPDO output pins ([1800]..[1BFF])
        const float rightEdge = boxW * 0.5f;
        const float outBusX = rightEdge + 20.0f;
        out.traces.push_back({ outBusX, -boxH * 0.55f, outBusX, boxH * 0.55f, 0.42f, EdaLayerId::SchWire, 2 });
        out.labels.push_back({ outBusX - 6.0f, boxH * 0.55f + 1.8f, 0.95f, EdaLayerId::SchWire, L"CAN Bus (TPDO Out)" });

        for (size_t ti = 0; ti < std::min<size_t>(tpdoObjs.size(), 8u); ++ti) {
            const auto& tp = tpdoObjs[ti];
            float py = boxH * 0.38f - static_cast<float>(ti) * 4.6f;
            out.traces.push_back({ rightEdge, py, outBusX, py, 0.26f, EdaLayerId::SchWire, static_cast<int>(ti + 20) });
            out.circles.push_back({ outBusX, py, 0.45f, 0.22f, true, EdaLayerId::SchWire });
            out.pads.push_back({ rightEdge, py, 0.65f, 0.65f, 0.0f, 0.0f, 1, EdaLayerId::SchSymbol, L"TPDO", L"" });

            std::string mapDesc;
            auto mit = pdoMappings.find(tp.index);
            if (mit != pdoMappings.end() && !mit->second.empty()) mapDesc = " <- " + mit->second.front();
            wchar_t tpLbl[128] = {};
            swprintf_s(tpLbl, L"TPDO [0x%04X] %hs%hs",
                tp.index, tp.paramName.c_str(), mapDesc.c_str());
            out.labels.push_back({ rightEdge + 1.2f, py + 0.8f, 0.82f, EdaLayerId::SchWire, tpLbl });
        }

        out.netCount = std::max<size_t>(1u, rpdoObjs.size() + tpdoObjs.size() + 1u);
        return true;
    }
    return false;
}

// #78. PROFINET IO GSDML Device Profile (.gsdml) — Backplane, DAP & IO Module Slots
inline bool ParseProfinetGsdml200(std::string_view sv, ParsedEdaBoard& out) {
    auto drawSchRect = [&](float cx, float cy, float w, float h, float strokeW, EdaLayerId lid, int netId = 0) {
        DrawFieldbusSchRect(out, cx, cy, w, h, strokeW, lid, netId);
    };
    {
        if (sv.find("<ISO15745Profile") == std::string_view::npos &&
            sv.find("<ProfileBody") == std::string_view::npos) {
            return false;
        }

        auto xmlAttr = [](std::string_view tag, std::string_view attr) -> std::string {
            std::string_view v;
            return GetXmlAttr(tag, attr, v) ? std::string(v) : std::string{};
        };

        // 1. Build <ExternalTextList> <PrimaryLanguage> <Text TextId="..." Value="..."/> dictionary
        std::unordered_map<std::string, std::string> textDict;
        {
            size_t p = 0;
            while ((p = sv.find("<Text ", p)) != std::string_view::npos && textDict.size() < 4096u) {
                size_t gt = sv.find('>', p + 6);
                if (gt == std::string_view::npos) break;
                std::string_view tag = sv.substr(p + 1, gt - p - 1);
                std::string tid = xmlAttr(tag, "TextId");
                std::string val = xmlAttr(tag, "Value");
                if (!tid.empty() && !val.empty() && !textDict.count(tid)) {
                    textDict[tid] = val;
                }
                p = gt + 1;
            }
        }
        auto resolveText = [&](const std::string& tid, const std::string& fallback) -> std::string {
            if (!tid.empty()) {
                auto it = textDict.find(tid);
                if (it != textDict.end() && !it->second.empty()) return it->second;
            }
            return fallback;
        };

        // 2. Parse <DeviceIdentity> & <DeviceFunction>
        std::string vendorId, deviceId, vendorName, productFamily;
        {
            size_t dip = sv.find("<DeviceIdentity");
            if (dip != std::string_view::npos) {
                size_t digt = sv.find('>', dip);
                size_t dice = sv.find("</DeviceIdentity>", dip);
                if (digt != std::string_view::npos) {
                    std::string_view dtag = sv.substr(dip + 1, digt - dip - 1);
                    vendorId = xmlAttr(dtag, "VendorID");
                    deviceId = xmlAttr(dtag, "DeviceID");
                    if (dice != std::string_view::npos && dice > digt) {
                        std::string_view inner = sv.substr(digt + 1, dice - digt - 1);
                        size_t vnp = inner.find("<VendorName");
                        if (vnp != std::string_view::npos) {
                            size_t vgt = inner.find('>', vnp);
                            if (vgt != std::string_view::npos) vendorName = xmlAttr(inner.substr(vnp + 1, vgt - vnp - 1), "Value");
                        }
                    }
                }
            }
            size_t fmp = sv.find("<Family ");
            if (fmp != std::string_view::npos) {
                size_t fgt = sv.find('>', fmp);
                if (fgt != std::string_view::npos) {
                    std::string_view ftag = sv.substr(fmp + 1, fgt - fmp - 1);
                    productFamily = xmlAttr(ftag, "ProductFamily");
                    if (productFamily.empty()) productFamily = xmlAttr(ftag, "MainFamily");
                }
            }
        }

        auto calcIoBytes = [&](std::string_view ioSection) -> uint32_t {
            uint32_t bytes = 0;
            size_t dp = 0;
            while ((dp = ioSection.find("<DataItem", dp)) != std::string_view::npos) {
                size_t dgt = ioSection.find('>', dp);
                if (dgt == std::string_view::npos) break;
                std::string_view dtag = ioSection.substr(dp + 1, dgt - dp - 1);
                std::string dt = PathUtils::ToLowerAscii(xmlAttr(dtag, "DataType"));
                std::string lenStr = xmlAttr(dtag, "Length");
                uint32_t explicitLen = lenStr.empty() ? 0u : static_cast<uint32_t>(std::strtoul(lenStr.c_str(), nullptr, 10));
                if (explicitLen > 0u) bytes += explicitLen;
                else if (dt.find("64") != std::string::npos || dt == "float64") bytes += 8u;
                else if (dt.find("32") != std::string::npos || dt == "float32") bytes += 4u;
                else if (dt.find("16") != std::string::npos) bytes += 2u;
                else bytes += 1u;
                dp = dgt + 1;
            }
            return bytes;
        };

        auto parseNameAndIo = [&](std::string_view blk, std::string& outName, uint32_t& outInB, uint32_t& outOutB) {
            size_t np = blk.find("<Name ");
            if (np != std::string_view::npos) {
                size_t ngt = blk.find('>', np);
                if (ngt != std::string_view::npos) {
                    std::string tid = xmlAttr(blk.substr(np + 1, ngt - np - 1), "TextId");
                    outName = resolveText(tid, outName);
                }
            }
            size_t ip0 = blk.find("<Input");
            size_t ip1 = (ip0 != std::string_view::npos) ? blk.find("</Input>", ip0) : std::string_view::npos;
            if (ip0 != std::string_view::npos && ip1 != std::string_view::npos) {
                outInB += calcIoBytes(blk.substr(ip0, ip1 - ip0));
            }
            size_t op0 = blk.find("<Output");
            size_t op1 = (op0 != std::string_view::npos) ? blk.find("</Output>", op0) : std::string_view::npos;
            if (op0 != std::string_view::npos && op1 != std::string_view::npos) {
                outOutB += calcIoBytes(blk.substr(op0, op1 - op0));
            }
        };

        struct GsdSlot {
            bool isDap = false;
            std::string id;
            std::string name;
            std::string slotsAttr;
            std::string minInterval;
            uint32_t inBytes = 0;
            uint32_t outBytes = 0;
        };
        std::vector<GsdSlot> slots;

        // 3. Parse <DeviceAccessPointItem>
        {
            size_t p = 0;
            while ((p = sv.find("<DeviceAccessPointItem", p)) != std::string_view::npos && slots.size() < 16u) {
                size_t gt = sv.find('>', p);
                if (gt == std::string_view::npos) break;
                std::string_view openTag = sv.substr(p + 1, gt - p - 1);
                GsdSlot dap;
                dap.isDap = true;
                dap.id = xmlAttr(openTag, "ID");
                dap.name = dap.id;
                dap.slotsAttr = xmlAttr(openTag, "PhysicalSlots");
                if (dap.slotsAttr.empty()) dap.slotsAttr = xmlAttr(openTag, "ModuleSlots");
                dap.minInterval = xmlAttr(openTag, "MinDeviceInterval");

                if (sv[gt - 1] != '/') {
                    size_t ce = sv.find("</DeviceAccessPointItem>", gt + 1);
                    if (ce == std::string_view::npos) break;
                    parseNameAndIo(sv.substr(gt + 1, ce - gt - 1), dap.name, dap.inBytes, dap.outBytes);
                    p = ce + 24;
                } else {
                    p = gt + 1;
                }
                if (!dap.name.empty()) slots.push_back(std::move(dap));
            }
        }

        // 4. Parse <ModuleItem> (and standalone <VirtualSubmoduleItem> if no ModuleItem)
        {
            size_t p = 0;
            while ((p = sv.find("<ModuleItem", p)) != std::string_view::npos && slots.size() < 20u) {
                size_t gt = sv.find('>', p);
                if (gt == std::string_view::npos) break;
                std::string_view openTag = sv.substr(p + 1, gt - p - 1);
                GsdSlot mod;
                mod.isDap = false;
                mod.id = xmlAttr(openTag, "ID");
                mod.name = mod.id;

                if (sv[gt - 1] != '/') {
                    size_t ce = sv.find("</ModuleItem>", gt + 1);
                    if (ce == std::string_view::npos) break;
                    parseNameAndIo(sv.substr(gt + 1, ce - gt - 1), mod.name, mod.inBytes, mod.outBytes);
                    p = ce + 13;
                } else {
                    p = gt + 1;
                }
                if (!mod.name.empty()) slots.push_back(std::move(mod));
            }
        }

        // Zero-Cheat: reject empty GSDML profiles with zero DAPs and zero IO modules
        if (slots.empty()) return false;

        out.kind = EdaDocKind::KiCadSch;
        std::string devHeader = !productFamily.empty() ? productFamily : (!vendorName.empty() ? vendorName : slots.front().name);
        if (!vendorId.empty() || !deviceId.empty()) {
            devHeader += " [" + vendorId + ":" + deviceId + "]";
        }
        out.formatTitle = L"PROFINET IO Device Profile — " + PathUtils::Utf8ToWide(devHeader) +
                          L" (" + std::to_wstring(slots.size()) + L" slots)";

        const float slotPitch = 20.0f;
        const float backplaneW = std::max(36.0f, static_cast<float>(slots.size()) * slotPitch);
        const float bpX0 = -backplaneW * 0.5f;
        const float bpX1 =  backplaneW * 0.5f;

        // PROFINET Industrial Ethernet top bus + IO Backplane rail
        out.traces.push_back({ bpX0, 14.0f, bpX1, 14.0f, 0.45f, EdaLayerId::SchWire, 1 });
        out.labels.push_back({ bpX0, 15.6f, 1.0f, EdaLayerId::SchWire, L"PROFINET RT/IRT Ethernet (100BASE-TX)" });

        out.traces.push_back({ bpX0,  6.0f, bpX1,  6.0f, 0.36f, EdaLayerId::InnerCopper, 2 });
        out.labels.push_back({ bpX0,  7.3f, 0.9f, EdaLayerId::SchLabel, L"IO Device Backplane Bus — " + PathUtils::Utf8ToWide(devHeader) });

        for (size_t i = 0; i < slots.size(); ++i) {
            const auto& sl = slots[i];
            float sx = (slots.size() == 1u)
                ? 0.0f
                : (bpX0 + 10.0f + ((backplaneW - 20.0f) * static_cast<float>(i)) / static_cast<float>(slots.size() - 1u));
            float sy = -6.0f;
            float sw = 16.5f;
            float sh = 16.0f;

            drawSchRect(sx, sy, sw, sh, sl.isDap ? 0.38f : 0.28f, EdaLayerId::SchSymbol, 0);
            // Backplane connector stub
            out.traces.push_back({ sx, sy + sh * 0.5f, sx, 6.0f, 0.26f, EdaLayerId::InnerCopper, 2 });
            out.circles.push_back({ sx, 6.0f, 0.45f, 0.22f, true, EdaLayerId::InnerCopper });

            if (sl.isDap) {
                // Ethernet uplink from DAP to PROFINET bus
                out.traces.push_back({ sx - 2.5f, sy + sh * 0.5f, sx - 2.5f, 14.0f, 0.30f, EdaLayerId::SchWire, 1 });
                out.circles.push_back({ sx - 2.5f, 14.0f, 0.50f, 0.25f, true, EdaLayerId::SchWire });
            }

            // Field IO Input / Output terminal pins below module
            if (sl.inBytes > 0u) {
                out.traces.push_back({ sx - 3.5f, sy - sh * 0.5f - 3.5f, sx - 3.5f, sy - sh * 0.5f, 0.26f, EdaLayerId::SchWire, 3 });
                out.pads.push_back({ sx - 3.5f, sy - sh * 0.5f - 3.5f, 0.6f, 0.6f, 0.0f, 0.0f, 1, EdaLayerId::SchSymbol, L"IN", L"" });
                out.labels.push_back({ sx - 7.5f, sy - sh * 0.5f - 5.2f, 0.82f, EdaLayerId::SchWire,
                                       L"IN:" + std::to_wstring(sl.inBytes) + L"B" });
            }
            if (sl.outBytes > 0u) {
                out.traces.push_back({ sx + 3.5f, sy - sh * 0.5f, sx + 3.5f, sy - sh * 0.5f - 3.5f, 0.26f, EdaLayerId::TopCopper, 4 });
                out.pads.push_back({ sx + 3.5f, sy - sh * 0.5f - 3.5f, 0.6f, 0.6f, 0.0f, 0.0f, 1, EdaLayerId::SchSymbol, L"OUT", L"" });
                out.labels.push_back({ sx + 0.5f, sy - sh * 0.5f - 5.2f, 0.82f, EdaLayerId::SchLabel,
                                       L"OUT:" + std::to_wstring(sl.outBytes) + L"B" });
            }

            std::wstring slotHdr = sl.isDap ? L"[DAP] " : (L"[Slot " + std::to_wstring(i) + L"] ");
            out.labels.push_back({ sx - sw * 0.45f, sy + 4.5f, 0.88f, EdaLayerId::SchLabel, slotHdr });
            std::wstring humanName = PathUtils::Utf8ToWide(sl.name);
            if (humanName.size() > 18u) humanName = humanName.substr(0, 18u);
            out.labels.push_back({ sx - sw * 0.45f, sy + 1.2f, 0.90f, EdaLayerId::SchLabel, humanName });
            if (sl.isDap && !sl.slotsAttr.empty()) {
                out.labels.push_back({ sx - sw * 0.45f, sy - 2.2f, 0.80f, EdaLayerId::SchLabel,
                                       L"Slots: " + PathUtils::Utf8ToWide(sl.slotsAttr) });
            }

            EdaComponent3D comp;
            comp.refDes = PathUtils::Utf8ToWide(sl.name);
            comp.value = sl.isDap ? L"PROFINET DAP" : (L"IN " + std::to_wstring(sl.inBytes) + L"B / OUT " + std::to_wstring(sl.outBytes) + L"B");
            comp.footprint = sl.isDap ? L"GSDML_DAP" : L"GSDML_MOD";
            comp.x = sx; comp.y = sy;
            comp.widthMm = sw; comp.lengthMm = sh;
            comp.category = EdaPartCategory::IC_Package;
            out.components.push_back(std::move(comp));
        }

        out.netCount = slots.size() + 2u;
        return true;
    }
}

// #79. Rockwell Studio 5000 Logix Designer PLC Ladder Export (.l5x)
inline bool ParseRockwellL5x200(std::string_view sv, ParsedEdaBoard& out) {
    auto drawSchRect = [&](float cx, float cy, float w, float h, float strokeW, EdaLayerId lid, int netId = 0) {
        DrawFieldbusSchRect(out, cx, cy, w, h, strokeW, lid, netId);
    };
    {
        if (sv.find("<RSLogix5000Content") == std::string_view::npos) return false;

        auto xmlAttr = [](std::string_view tag, std::string_view attr) -> std::string {
            std::string_view v;
            return GetXmlAttr(tag, attr, v) ? std::string(v) : std::string{};
        };

        std::string ctrlName, procType;
        {
            size_t cp = sv.find("<Controller ");
            if (cp != std::string_view::npos) {
                size_t gt = sv.find('>', cp);
                if (gt != std::string_view::npos) {
                    std::string_view ctag = sv.substr(cp + 1, gt - cp - 1);
                    ctrlName = xmlAttr(ctag, "Name");
                    procType = xmlAttr(ctag, "ProcessorType");
                }
            }
        }

        struct L5xTag { std::string name; std::string dataType; };
        std::vector<L5xTag> tags;
        {
            size_t tp = 0;
            while ((tp = sv.find("<Tag ", tp)) != std::string_view::npos && tags.size() < 64u) {
                size_t gt = sv.find('>', tp);
                if (gt == std::string_view::npos) break;
                std::string_view ttag = sv.substr(tp + 1, gt - tp - 1);
                std::string tnm = xmlAttr(ttag, "Name");
                std::string tdt = xmlAttr(ttag, "DataType");
                if (!tnm.empty()) {
                    tags.push_back({ std::move(tnm), tdt.empty() ? "BOOL" : std::move(tdt) });
                }
                tp = gt + 1;
            }
        }

        struct L5xInst {
            std::string op;
            std::string primaryArg;
            std::string extraArgs;
        };
        struct L5xRung {
            int number = 0;
            std::vector<std::vector<L5xInst>> branches; // branch[0] = main rung path, branch[1..] = parallel [a,b] paths
        };

        auto parseInstSeq = [](std::string_view seq) -> std::vector<L5xInst> {
            std::vector<L5xInst> res;
            size_t i = 0;
            while (i < seq.size() && res.size() < 10u) {
                while (i < seq.size() && !std::isalpha(static_cast<unsigned char>(seq[i]))) ++i;
                if (i >= seq.size()) break;
                size_t s = i;
                while (i < seq.size() && (std::isalnum(static_cast<unsigned char>(seq[i])) || seq[i] == '_')) ++i;
                std::string opUp(seq.substr(s, i - s));
                for (char& c : opUp) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

                while (i < seq.size() && seq[i] == ' ') ++i;
                if (i >= seq.size() || seq[i] != '(') continue;
                int pDepth = 0;
                size_t argStart = i + 1;
                size_t argEnd = argStart;
                for (; i < seq.size(); ++i) {
                    if (seq[i] == '(') ++pDepth;
                    else if (seq[i] == ')') {
                        if (--pDepth == 0) { argEnd = i++; break; }
                    }
                }
                std::string_view args = seq.substr(argStart, argEnd - argStart);
                size_t cm = args.find(',');
                L5xInst inst;
                inst.op = std::move(opUp);
                inst.primaryArg = std::string(cm == std::string_view::npos ? args : args.substr(0, cm));
                if (cm != std::string_view::npos) inst.extraArgs = std::string(args.substr(cm + 1));
                if (!inst.op.empty()) res.push_back(std::move(inst));
            }
            return res;
        };

        std::vector<L5xRung> rungs;
        {
            size_t rp = 0;
            while ((rp = sv.find("<Rung", rp)) != std::string_view::npos && rungs.size() < 24u) {
                size_t after = rp + 5;
                if (after < sv.size() && sv[after] != ' ' && sv[after] != '>') { rp = after; continue; }
                size_t rgt = sv.find('>', after);
                size_t rce = (rgt != std::string_view::npos) ? sv.find("</Rung>", rgt + 1) : std::string_view::npos;
                if (rce == std::string_view::npos) break;

                int rungNum = static_cast<int>(rungs.size());
                std::string rnAttr = xmlAttr(sv.substr(rp + 1, rgt - rp - 1), "Number");
                if (!rnAttr.empty()) rungNum = std::atoi(rnAttr.c_str());

                std::string_view rBody = sv.substr(rgt + 1, rce - rgt - 1);
                size_t tx0 = rBody.find("<Text>");
                size_t tx1 = (tx0 != std::string_view::npos) ? rBody.find("</Text>", tx0 + 6) : std::string_view::npos;
                if (tx0 != std::string_view::npos && tx1 != std::string_view::npos) {
                    std::string rtxt(rBody.substr(tx0 + 6, tx1 - tx0 - 6));
                    size_t cd = rtxt.find("[CDATA[");
                    if (cd != std::string::npos) {
                        rtxt.erase(0, cd + 7);
                        size_t ce = rtxt.rfind("]]>");
                        if (ce != std::string::npos) rtxt.resize(ce);
                    }

                    // Extract optional parallel branch [branchA,branchB] + series tail
                    L5xRung rg;
                    rg.number = rungNum;
                    size_t lb = rtxt.find('[');
                    size_t rb = (lb != std::string::npos) ? rtxt.rfind(']') : std::string::npos;
                    if (lb != std::string::npos && rb != std::string::npos && rb > lb) {
                        std::string prefix = rtxt.substr(0, lb);
                        std::string suffix = rtxt.substr(rb + 1);
                        std::string_view mid(rtxt.data() + lb + 1, rb - lb - 1);
                        int pDepth = 0;
                        size_t bs = 0;
                        for (size_t k = 0; k <= mid.size(); ++k) {
                            if (k < mid.size() && mid[k] == '(') ++pDepth;
                            else if (k < mid.size() && mid[k] == ')') --pDepth;
                            else if (pDepth == 0 && (k == mid.size() || mid[k] == ',')) {
                                std::string pathStr = prefix + std::string(mid.substr(bs, k - bs)) + (rg.branches.empty() ? suffix : "");
                                auto seq = parseInstSeq(pathStr);
                                if (!seq.empty()) rg.branches.push_back(std::move(seq));
                                bs = k + 1;
                            }
                        }
                    } else {
                        auto seq = parseInstSeq(rtxt);
                        if (!seq.empty()) rg.branches.push_back(std::move(seq));
                    }

                    if (!rg.branches.empty()) {
                        rungs.push_back(std::move(rg));
                    }
                }
                rp = rce + 7;
            }
        }

        // Zero-Cheat: NEVER synthesize fake rungs! Reject if zero rungs and zero controller tags.
        if (rungs.empty() && tags.empty()) {
            return false;
        }

        out.kind = EdaDocKind::KiCadSch;
        std::wstring ctrlHdr = ctrlName.empty() ? L"Logix5000" : PathUtils::Utf8ToWide(ctrlName);
        if (!procType.empty()) ctrlHdr += L" [" + PathUtils::Utf8ToWide(procType) + L"]";
        out.formatTitle = L"Rockwell Studio 5000 PLC Ladder — " + ctrlHdr +
                          L" (" + std::to_wstring(rungs.size()) + L" rungs, " + std::to_wstring(tags.size()) + L" tags)";

        const float leftRailX  = -44.0f;
        const float rightRailX =  44.0f;
        out.labels.push_back({ leftRailX, 14.0f, 1.05f, EdaLayerId::SchLabel, out.formatTitle });

        if (!rungs.empty()) {
            float curY = 6.0f;
            size_t compCounter = 0;

            for (size_t ri = 0; ri < rungs.size(); ++ri) {
                const auto& rg = rungs[ri];
                const float mainY = curY;

                wchar_t rLbl[32] = {};
                swprintf_s(rLbl, L"R%d", rg.number);
                out.labels.push_back({ leftRailX - 7.5f, mainY + 0.5f, 0.95f, EdaLayerId::SchLabel, rLbl });

                float branchJoinX = -10.0f;

                for (size_t bi = 0; bi < rg.branches.size(); ++bi) {
                    const auto& insts = rg.branches[bi];
                    const float ry = mainY - static_cast<float>(bi) * 9.5f;
                    float curX = (bi == 0u) ? leftRailX : (leftRailX + 5.0f);

                    if (bi > 0u) {
                        out.traces.push_back({ leftRailX + 5.0f, mainY, leftRailX + 5.0f, ry, 0.25f, EdaLayerId::SchWire, static_cast<int>(ri + 1) });
                    }

                    size_t nonOutIdx = 0;
                    for (size_t k = 0; k < insts.size(); ++k) {
                        const auto& in = insts[k];
                        const bool isCoil = (in.op == "OTE" || in.op == "OTL" || in.op == "OTU");
                        const bool isContact = (in.op == "XIC" || in.op == "XIO" || in.op == "ONS");
                        const bool isOutBox = !isCoil && !isContact;

                        float symX = isCoil ? 33.0f
                                   : (isOutBox && k + 1 == insts.size()) ? 26.0f
                                   : (-28.0f + static_cast<float>(nonOutIdx++) * 15.5f);

                        if (isContact) {
                            out.traces.push_back({ curX, ry, symX - 1.6f, ry, 0.25f, EdaLayerId::SchWire, static_cast<int>(ri + 1) });
                            out.traces.push_back({ symX - 1.6f, ry - 2.3f, symX - 1.6f, ry + 2.3f, 0.34f, EdaLayerId::SchSymbol, 0 });
                            out.traces.push_back({ symX + 1.6f, ry - 2.3f, symX + 1.6f, ry + 2.3f, 0.34f, EdaLayerId::SchSymbol, 0 });
                            if (in.op == "XIO") {
                                out.traces.push_back({ symX - 1.3f, ry - 2.0f, symX + 1.3f, ry + 2.0f, 0.26f, EdaLayerId::SchSymbol, 0 });
                            } else if (in.op == "ONS") {
                                out.traces.push_back({ symX, ry - 1.6f, symX, ry + 1.6f, 0.24f, EdaLayerId::SchSymbol, 0 });
                            }
                            curX = symX + 1.6f;
                            branchJoinX = std::max(branchJoinX, curX + 3.0f);
                        } else if (isCoil) {
                            out.traces.push_back({ curX, ry, symX - 2.3f, ry, 0.25f, EdaLayerId::SchWire, static_cast<int>(ri + 1) });
                            out.circles.push_back({ symX, ry, 2.3f, 0.30f, false, EdaLayerId::SchSymbol });
                            if (in.op == "OTL" || in.op == "OTU") {
                                out.labels.push_back({ symX - 0.8f, ry + 0.5f, 0.85f, EdaLayerId::SchSymbol,
                                                       (in.op == "OTL") ? L"L" : L"U" });
                            }
                            curX = symX + 2.3f;
                        } else {
                            // Function block instruction box (TON, CTU, MOV, EQU, ...)
                            float bw = 11.5f, bh = 5.6f;
                            out.traces.push_back({ curX, ry, symX - bw * 0.5f, ry, 0.25f, EdaLayerId::SchWire, static_cast<int>(ri + 1) });
                            drawSchRect(symX, ry, bw, bh, 0.30f, EdaLayerId::SchSymbol, 0);
                            out.labels.push_back({ symX - bw * 0.42f, ry + 0.8f, 0.85f, EdaLayerId::SchSymbol, PathUtils::Utf8ToWide(in.op) });
                            if (!in.extraArgs.empty()) {
                                std::string ex = in.extraArgs.substr(0, std::min<size_t>(in.extraArgs.size(), 14u));
                                out.labels.push_back({ symX - bw * 0.42f, ry - 1.3f, 0.75f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(ex) });
                            }
                            curX = symX + bw * 0.5f;
                            branchJoinX = std::max(branchJoinX, curX + 2.5f);
                        }

                        out.labels.push_back({ symX - 4.2f, ry + 3.8f, 0.90f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(in.primaryArg) });

                        if (compCounter < 128u) {
                            EdaComponent3D ic;
                            ic.refDes = PathUtils::Utf8ToWide(in.primaryArg.empty() ? in.op : in.primaryArg);
                            ic.value = PathUtils::Utf8ToWide(in.op);
                            ic.footprint = L"L5X_" + PathUtils::Utf8ToWide(in.op);
                            ic.x = symX; ic.y = ry;
                            ic.widthMm = isOutBox ? 11.5f : 4.5f;
                            ic.lengthMm = 5.0f;
                            out.components.push_back(std::move(ic));
                            ++compCounter;
                        }
                    }

                    if (bi == 0u) {
                        out.traces.push_back({ curX, ry, rightRailX, ry, 0.25f, EdaLayerId::SchWire, static_cast<int>(ri + 1) });
                    } else {
                        float jx = std::min(branchJoinX, 20.0f);
                        out.traces.push_back({ curX, ry, jx, ry, 0.25f, EdaLayerId::SchWire, static_cast<int>(ri + 1) });
                        out.traces.push_back({ jx, ry, jx, mainY, 0.25f, EdaLayerId::SchWire, static_cast<int>(ri + 1) });
                    }
                }

                curY -= static_cast<float>(rg.branches.size()) * 11.0f + 2.5f;
            }

            // Left (+24V / L1) and Right (0V / L2) PLC Ladder Power Rails
            out.traces.push_back({ leftRailX,  11.0f, leftRailX,  curY + 5.0f, 0.55f, EdaLayerId::SchWire, 1 });
            out.traces.push_back({ rightRailX, 11.0f, rightRailX, curY + 5.0f, 0.55f, EdaLayerId::SchWire, 2 });
        } else {
            // Tag-only Controller Data Block schematic
            const size_t nShow = std::min<size_t>(tags.size(), 16u);
            const float bh = static_cast<float>(nShow) * 3.4f + 6.0f;
            drawSchRect(0.0f, -bh * 0.5f + 6.0f, 38.0f, bh, 0.36f, EdaLayerId::SchSymbol, 0);
            for (size_t ti = 0; ti < nShow; ++ti) {
                float ty = 3.0f - static_cast<float>(ti) * 3.2f;
                out.traces.push_back({ -23.0f, ty, -19.0f, ty, 0.24f, EdaLayerId::SchWire, static_cast<int>(ti + 1) });
                out.pads.push_back({ -23.0f, ty, 0.55f, 0.55f, 0.0f, 0.0f, 1, EdaLayerId::SchSymbol, L"TAG", L"" });
                out.labels.push_back({ -17.8f, ty + 0.5f, 0.88f, EdaLayerId::SchLabel,
                                       PathUtils::Utf8ToWide(tags[ti].name + " : " + tags[ti].dataType) });

                EdaComponent3D tc;
                tc.refDes = PathUtils::Utf8ToWide(tags[ti].name);
                tc.value = PathUtils::Utf8ToWide(tags[ti].dataType);
                tc.footprint = L"L5X_TAG";
                tc.x = 0.0f; tc.y = ty;
                tc.widthMm = 36.0f; tc.lengthMm = 2.8f;
                out.components.push_back(std::move(tc));
            }
        }

        out.netCount = std::max<size_t>(1u, rungs.size() + tags.size());
        return true;
    }
}

inline bool ParseExtendedEda200(const uint8_t* data, size_t size, const std::wstring& ext, ParsedEdaBoard& out) {
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    if (ext == L".kicad_sym" || ext == L".ses" || ext == L".pos" || ext == L".cpl" || ext == L".mnt") {
        return ParseKiCadSymSesPos(sv, ext, out);
    }
    if (ext == L".ibs" || ext == L".s3p" || ext == L".s4p" || ext == L".s8p" || ext == L".oas") {
        return ParseIbisTouchstoneOasis(data, size, ext, out);
    }
    if (ext == L".xdc" || ext == L".sdc" || ext == L".qsf" || ext == L".jed" ||
        ext == L".svf" || ext == L".xsvf"|| ext == L".sof" || ext == L".pof")
    {
        return ParseFpgaPldJtag200(data, size, ext, out);
    }
    if (ext == L".sr" || ext == L".sal" || ext == L".wfm" || ext == L".isf") {
        return ParseLogicScopeAutoPlc200(data, size, ext, out);
    }
    if (ext == L".mf4" || ext == L".mdf") return ParseAsamMdf200(data, size, ext, out);
    if (ext == L".ldf")   return ParseLinLdf200(sv, out);
    if (ext == L".arxml") return ParseAutosarArxml200(sv, out);
    if (ext == L".eds" || ext == L".dcf") return ParseCanOpenEds200(sv, ext, out);
    if (ext == L".gsdml") return ParseProfinetGsdml200(sv, out);
    if (ext == L".l5x")   return ParseRockwellL5x200(sv, out);
    return false;
}

} // namespace Formats
