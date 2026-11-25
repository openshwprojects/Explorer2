#pragma once
// WaveformPlotCommon.h — Shared Multi-Channel Analog, AC Frequency-Sweep & Oscilloscope Waveform Renderer
// Used by SpiceRawWave, HspiceBinaryTr0, YokogawaWaveformWvf, and multi-channel analog trace readers.
// NOTE: Included inside `namespace Explorer2`. Do NOT add `#include` directives here.

namespace Formats {

struct SpiceWaveVar {
    std::string name;
    std::string unit;
    std::vector<double> values; // Real value, or dB magnitude for complex AC traces
    std::vector<double> phaseDeg;
};

inline void RenderSpiceWaveformsToBoard(
    const std::wstring& titleStr,
    bool isAcFreqLog,
    const SpiceWaveVar& xVar,
    const std::vector<SpiceWaveVar>& yVars,
    ParsedEdaBoard& out)
{
    out.kind = EdaDocKind::GerberLayer; // Dark oscilloscope graticule background
    out.formatTitle = titleStr;

    const size_t nPts = xVar.values.size();
    const size_t nCh = std::min<size_t>(yVars.size(), 16u);
    if (nPts < 2u || nCh == 0u) return;

    double xMin = 1e300, xMax = -1e300;
    for (double xv : xVar.values) {
        const double mx = isAcFreqLog ? std::log10(std::max(1e-12, std::fabs(xv))) : xv;
        if (std::isfinite(mx)) {
            xMin = std::min(xMin, mx);
            xMax = std::max(xMax, mx);
        }
    }
    if (!(xMax > xMin)) { xMax = xMin + 1.0; }

    const float kX0 = 24.0f;
    const float kX1 = 164.0f;
    const float kLaneH = 14.0f;
    const float kLaneGap = 3.5f;
    const float totalH = static_cast<float>(nCh) * (kLaneH + kLaneGap) + 6.0f;

    DrawEdaBoxRect(out, kX0, 0.0f, kX1, totalH, 0.22f, EdaLayerId::EdgeCuts, 0);

    for (int gx = 1; gx < 10; ++gx) {
        const float vx = kX0 + (kX1 - kX0) * (static_cast<float>(gx) / 10.0f);
        out.traces.push_back({ vx, 0.0f, vx, totalH, 0.08f, EdaLayerId::EdgeCuts, 0 });
    }

    wchar_t xAxisLbl[160] = {};
    swprintf_s(xAxisLbl, L"%hs: %.4g .. %.4g (%zu pts)",
               xVar.name.empty() ? (isAcFreqLog ? "frequency" : "time") : xVar.name.c_str(),
               xVar.values.front(), xVar.values.back(), nPts);
    out.labels.push_back({ kX0, totalH + 1.6f, 1.15f, EdaLayerId::TopSilk, xAxisLbl });

    static constexpr EdaLayerId kChanLayers[5] = {
        EdaLayerId::TopCopper,
        EdaLayerId::BottomCopper,
        EdaLayerId::SchWire,
        EdaLayerId::InnerCopper,
        EdaLayerId::TopSilk
    };

    const size_t stride = std::max<size_t>(1u, nPts / 2048u);

    for (size_t ci = 0; ci < nCh; ++ci) {
        const auto& ch = yVars[ci];
        if (ch.values.size() < 2u) continue;
        const float laneTop = totalH - 3.0f - static_cast<float>(ci) * (kLaneH + kLaneGap);
        const float laneBot = laneTop - kLaneH;
        const float laneMid = (laneTop + laneBot) * 0.5f;

        out.traces.push_back({ kX0, laneMid, kX1, laneMid, 0.08f, EdaLayerId::EdgeCuts, 0 });

        double vMin = 1e300, vMax = -1e300;
        for (double v : ch.values) {
            if (std::isfinite(v)) {
                vMin = std::min(vMin, v);
                vMax = std::max(vMax, v);
            }
        }
        if (vMin > vMax) { vMin = -1.0; vMax = 1.0; }
        double vSpan = vMax - vMin;
        if (vSpan < 1e-12) vSpan = 1.0;

        const EdaLayerId lid = kChanLayers[ci % 5u];
        const int netId = static_cast<int>(ci + 1u);

        auto mapPt = [&](size_t idx, float& ox, float& oy) {
            const double xv = xVar.values[idx];
            const double mx = isAcFreqLog ? std::log10(std::max(1e-12, std::fabs(xv))) : xv;
            const double tx = std::clamp((mx - xMin) / (xMax - xMin), 0.0, 1.0);
            double ty = std::clamp((ch.values[idx] - vMin) / vSpan, 0.0, 1.0);
            if (std::fabs(vMax - vMin) < 1e-12) ty = 0.5;
            ox = kX0 + static_cast<float>(tx) * (kX1 - kX0);
            oy = laneBot + 1.0f + static_cast<float>(ty) * (kLaneH - 2.0f);
        };

        float px = 0.0f, py = 0.0f;
        const size_t limit = std::min(nPts, ch.values.size());
        for (size_t i = 0; i < limit; i += stride) {
            float wx = 0.0f, wy = 0.0f;
            mapPt(i, wx, wy);
            if (i > 0 && out.traces.size() < 120000u) {
                out.traces.push_back({ px, py, wx, wy, 0.28f, lid, netId });
            }
            px = wx; py = wy;
        }
        if ((limit - 1u) % stride != 0u && out.traces.size() < 120000u) {
            float wx = 0.0f, wy = 0.0f;
            mapPt(limit - 1u, wx, wy);
            out.traces.push_back({ px, py, wx, wy, 0.28f, lid, netId });
        }

        out.pads.push_back({
            kX0 - 1.5f, laneMid, 1.1f, 1.1f, 0.0f, 0.0f, 1, lid,
            PathUtils::Utf8ToWide(ch.name), PathUtils::Utf8ToWide(ch.name)
        });

        const char* unitSuf = isAcFreqLog ? " dB"
            : (!ch.unit.empty() && ch.unit.size() <= 8 && !ContainsTextCI(ch.unit, "voltage") && !ContainsTextCI(ch.unit, "current"))
                ? ""
                : (ContainsTextCI(ch.unit, "current") ? " A" : " V");
        wchar_t chLbl[192] = {};
        if (!ch.unit.empty() && unitSuf[0] == '\0') {
            swprintf_s(chLbl, L"%hs [%.3g..%.3g %hs]", ch.name.c_str(), vMin, vMax, ch.unit.c_str());
        } else {
            swprintf_s(chLbl, L"%hs [%.3g..%.3g%hs]", ch.name.c_str(), vMin, vMax, unitSuf);
        }
        out.labels.push_back({ 0.5f, laneMid - 0.4f, 0.92f, lid, chLbl });
    }

    out.netCount = nCh;
}

inline bool RenderSpiceWaveformsToBoard(
    const std::wstring& titleStr,
    const std::wstring& formatKindLabel,
    const std::vector<SpiceWaveVar>& vars,
    ParsedEdaBoard& outBoard)
{
    if (vars.size() < 2u || vars[0].values.size() < 2u) return false;
    std::vector<SpiceWaveVar> yVars(vars.begin() + 1, vars.end());
    const std::wstring fullTitle = formatKindLabel + (titleStr.empty() ? L"" : (L" — " + titleStr));
    RenderSpiceWaveformsToBoard(fullTitle, /*isAcFreqLog=*/false, vars[0], yVars, outBoard);
    return !outBoard.traces.empty();
}

} // namespace Formats
