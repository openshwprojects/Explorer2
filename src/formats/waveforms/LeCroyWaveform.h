#pragma once
// LeCroyWaveform.h — Zero-dependency C++20 Teledyne LeCroy Oscilloscope Waveform (.trc, WAVEDESC binary template) reader.
// Included inside `namespace Explorer2` in src/EdaReaders.cpp.

namespace Formats {

namespace DetailLeCroyTrc {

using ::Explorer2::DrawEdaBoxRect;
inline void DrawBoxRect(ParsedEdaBoard& out, float x0, float y0, float x1, float y1, float strokeW, EdaLayerId lid, int netId = 0) { DrawEdaBoxRect(out, x0, y0, x1, y1, strokeW, lid, netId); }

inline void DrawScopeGraticule(ParsedEdaBoard& out, float x0, float y0, float w, float h, int nx = 10, int ny = 8) {
    DrawBoxRect(out, x0, y0, x0 + w, y0 + h, 0.35f, EdaLayerId::EdgeCuts, 0);
    for (int i = 1; i < nx; ++i) {
        float x = x0 + (w * static_cast<float>(i)) / static_cast<float>(nx);
        out.traces.push_back({ x, y0, x, y0 + h, 0.08f, EdaLayerId::BottomSilk, 0 });
    }
    for (int j = 1; j < ny; ++j) {
        float y = y0 + (h * static_cast<float>(j)) / static_cast<float>(ny);
        out.traces.push_back({ x0, y, x0 + w, y, 0.08f, EdaLayerId::BottomSilk, 0 });
    }
}

} // namespace DetailLeCroyTrc

inline bool ParseLeCroyWaveformBoard(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    if (!data || size < 348u) return false;

    // Locate "WAVEDESC" template header at offset 0 or after SCPI #9xxxxxxxxx / DESC, prefix within first 64 bytes
    size_t wavedesc = SIZE_MAX;
    const size_t maxSearch = std::min<size_t>(64u, size - 346u);
    for (size_t i = 0; i <= maxSearch; ++i) {
        if (std::memcmp(data + i, "WAVEDESC", 8) == 0) {
            wavedesc = i;
            break;
        }
    }
    if (wavedesc == SIZE_MAX || wavedesc + 346u > size) return false;

    const uint8_t* wd = data + wavedesc;

    // COMM_ORDER at +34: 0 = HIFIRST (big-endian), 1 = LOFIRST (little-endian)
    bool bigEndian = false;
    if (wd[34] == 0u && wd[35] == 0u) {
        bigEndian = true;
    } else if (wd[34] == 1u && wd[35] == 0u) {
        bigEndian = false;
    } else if (wd[34] == 0u && wd[35] == 1u) {
        bigEndian = false;
    } else {
        return false;
    }

    const uint16_t commType  = ByteIO::RdU16(wd + 32, bigEndian); // 0 = signed 8-bit, 1 = signed 16-bit
    const uint16_t commOrder = ByteIO::RdU16(wd + 34, bigEndian);
    if (commType > 1u || commOrder > 1u) return false;

    const int32_t waveDescLen  = static_cast<int32_t>(ByteIO::RdU32(wd + 36, bigEndian));
    const int32_t userTextLen  = static_cast<int32_t>(ByteIO::RdU32(wd + 40, bigEndian));
    const int32_t trigTimeLen  = static_cast<int32_t>(ByteIO::RdU32(wd + 48, bigEndian));
    const int32_t risTimeLen   = static_cast<int32_t>(ByteIO::RdU32(wd + 52, bigEndian));
    const int32_t resArray1Len = static_cast<int32_t>(ByteIO::RdU32(wd + 56, bigEndian));
    const int32_t waveArr1Len  = static_cast<int32_t>(ByteIO::RdU32(wd + 60, bigEndian));
    const int32_t waveArrCount = static_cast<int32_t>(ByteIO::RdU32(wd + 116, bigEndian));

    if (waveDescLen < 346 || userTextLen < 0 || trigTimeLen < 0 || risTimeLen < 0 || resArray1Len < 0 || waveArr1Len < 2) {
        return false;
    }

    const uint64_t dataOff64 = static_cast<uint64_t>(wavedesc) +
                               static_cast<uint64_t>(waveDescLen) +
                               static_cast<uint64_t>(userTextLen) +
                               static_cast<uint64_t>(trigTimeLen) +
                               static_cast<uint64_t>(risTimeLen) +
                               static_cast<uint64_t>(resArray1Len);
    if (dataOff64 >= size || dataOff64 + static_cast<uint64_t>(waveArr1Len) > size) {
        return false;
    }

    const size_t dataOff = static_cast<size_t>(dataOff64);
    const size_t bps = (commType == 1u) ? 2u : 1u;
    size_t totalPts = static_cast<size_t>(waveArr1Len) / bps;
    if (waveArrCount > 0 && static_cast<size_t>(waveArrCount) < totalPts) {
        totalPts = static_cast<size_t>(waveArrCount);
    }
    if (totalPts < 2u) return false;

    auto readFixedAscii = [](const uint8_t* p, size_t maxLen) -> std::string {
        std::string s;
        for (size_t i = 0; i < maxLen; ++i) {
            uint8_t c = p[i];
            if (c == 0) break;
            if (c >= 32u && c < 127u) s.push_back(static_cast<char>(c));
        }
        while (!s.empty() && s.back() == ' ') s.pop_back();
        return s;
    };

    std::string instName  = readFixedAscii(wd + 76, 16);
    std::string vertUnit  = readFixedAscii(wd + 196, 48);
    std::string horUnit   = readFixedAscii(wd + 244, 48);
    if (vertUnit.empty()) vertUnit = "V";
    if (horUnit.empty())  horUnit  = "s";

    float vertGain     = ByteIO::RdF32(wd + 156, bigEndian);
    float vertOffset   = ByteIO::RdF32(wd + 160, bigEndian);
    float horizInterv  = ByteIO::RdF32(wd + 176, bigEndian);
    double horizOffset = ByteIO::RdF64(wd + 180, bigEndian);
    uint16_t waveSrc   = ByteIO::RdU16(wd + 344, bigEndian);

    if (!std::isfinite(vertGain) || std::fabs(vertGain) < 1e-18f || std::fabs(vertGain) > 1e12f) vertGain = 1.0f;
    if (!std::isfinite(vertOffset) || std::fabs(vertOffset) > 1e12f) vertOffset = 0.0f;
    if (!std::isfinite(horizInterv) || horizInterv <= 0.0f || horizInterv > 1e6f) horizInterv = 1e-6f;
    if (!std::isfinite(horizOffset) || std::fabs(horizOffset) > 1e12) horizOffset = 0.0;

    auto sampleVolt = [&](size_t idx) -> float {
        const uint8_t* sp = data + dataOff + idx * bps;
        int16_t raw = (commType == 1u)
            ? static_cast<int16_t>(ByteIO::RdU16(sp, bigEndian))
            : static_cast<int16_t>(static_cast<int8_t>(sp[0]));
        return static_cast<float>(static_cast<double>(vertGain) * static_cast<double>(raw) - static_cast<double>(vertOffset));
    };
    auto sampleTime = [&](size_t idx) -> double {
        return static_cast<double>(idx) * static_cast<double>(horizInterv) + horizOffset;
    };

    struct WaveVertex {
        double t = 0.0;
        float v = 0.0f;
    };
    std::vector<WaveVertex> pts;

    if (totalPts <= 4096u) {
        pts.reserve(totalPts);
        for (size_t i = 0; i < totalPts; ++i) {
            pts.push_back({ sampleTime(i), sampleVolt(i) });
        }
    } else {
        // Envelope-preserving min/max downsampling into 2048 buckets (<= 4096 vertices)
        constexpr size_t kBuckets = 2048u;
        pts.reserve(kBuckets * 2u);
        for (size_t b = 0; b < kBuckets; ++b) {
            size_t s0 = (b * totalPts) / kBuckets;
            size_t s1 = ((b + 1u) * totalPts) / kBuckets;
            if (s1 <= s0) s1 = s0 + 1u;
            size_t minIdx = s0, maxIdx = s0;
            float minV = sampleVolt(s0), maxV = minV;
            for (size_t i = s0 + 1u; i < s1; ++i) {
                float v = sampleVolt(i);
                if (v < minV) { minV = v; minIdx = i; }
                if (v > maxV) { maxV = v; maxIdx = i; }
            }
            if (minIdx == maxIdx) {
                pts.push_back({ sampleTime(minIdx), minV });
            } else if (minIdx < maxIdx) {
                pts.push_back({ sampleTime(minIdx), minV });
                pts.push_back({ sampleTime(maxIdx), maxV });
            } else {
                pts.push_back({ sampleTime(maxIdx), maxV });
                pts.push_back({ sampleTime(minIdx), minV });
            }
        }
    }

    if (pts.size() < 2u) return false;

    float vMin = pts[0].v, vMax = pts[0].v;
    for (const auto& p : pts) {
        vMin = std::min(vMin, p.v);
        vMax = std::max(vMax, p.v);
    }
    const float vSpan = std::max(1e-6f, vMax - vMin);
    const double tStart = pts.front().t;
    const double tEnd   = pts.back().t;
    const double tSpan  = std::max(1e-18, tEnd - tStart);

    const int chNum = (waveSrc <= 3u) ? static_cast<int>(waveSrc + 1u) : 1;
    const EdaLayerId traceLayer = (chNum == 2) ? EdaLayerId::BottomCopper
                                : (chNum == 3) ? EdaLayerId::InnerCopper
                                : (chNum == 4) ? EdaLayerId::SchWire
                                               : EdaLayerId::TopCopper;

    out.kind = EdaDocKind::KiCadSch;
    out.netCount = 1u;

    std::wstring chTag = L"CH" + std::to_wstring(chNum);
    std::wstring fmtOrder = (commType == 1u ? L"16-bit " : L"8-bit ") + std::wstring(bigEndian ? L"HIFIRST" : L"LOFIRST");
    std::wstring instW = instName.empty() ? L"LeCroy" : PathUtils::Utf8ToWide(instName);
    out.formatTitle = L"Teledyne LeCroy Oscilloscope Waveform (.trc — " + instW + L" " + chTag + L", " + fmtOrder + L")";

    DetailLeCroyTrc::DrawScopeGraticule(out, -40.0f, -25.0f, 80.0f, 50.0f, 10, 8);

    // Zero-volt reference tick on left graticule edge
    float yZero = std::clamp(-20.0f + ((0.0f - vMin) / vSpan) * 40.0f, -24.0f, 24.0f);
    out.traces.push_back({ -40.0f, yZero, -38.4f, yZero, 0.30f, EdaLayerId::InnerCopper, 0 });

    // Trigger t=0 marker on top graticule edge when within capture window
    if (tStart <= 0.0 && tEnd >= 0.0) {
        float xTrig = -39.0f + static_cast<float>((0.0 - tStart) / tSpan) * 78.0f;
        out.traces.push_back({ xTrig, 23.6f, xTrig, 25.0f, 0.32f, EdaLayerId::SchSymbol, 0 });
    }

    for (size_t i = 1u; i < pts.size(); ++i) {
        float x0 = -39.0f + static_cast<float>((pts[i - 1u].t - tStart) / tSpan) * 78.0f;
        float x1 = -39.0f + static_cast<float>((pts[i].t      - tStart) / tSpan) * 78.0f;
        float y0 = -20.0f + ((pts[i - 1u].v - vMin) / vSpan) * 40.0f;
        float y1 = -20.0f + ((pts[i].v      - vMin) / vSpan) * 40.0f;
        out.traces.push_back({ x0, y0, x1, y1, 0.28f, traceLayer, chNum });
    }

    char vInfoBuf[96] = {};
    std::snprintf(vInfoBuf, sizeof(vInfoBuf), "[%.3g .. %.3g %s, %.3g %s/div]",
                  static_cast<double>(vMin), static_cast<double>(vMax), vertUnit.c_str(),
                  static_cast<double>(vSpan / 8.0f), vertUnit.c_str());

    char tInfoBuf[96] = {};
    double sampleRate = 1.0 / static_cast<double>(horizInterv);
    if (sampleRate >= 1e9) {
        std::snprintf(tInfoBuf, sizeof(tInfoBuf), "%zu pts @ %.2f GSa/s (dt=%.3g %s)",
                      totalPts, sampleRate * 1e-9, static_cast<double>(horizInterv), horUnit.c_str());
    } else if (sampleRate >= 1e6) {
        std::snprintf(tInfoBuf, sizeof(tInfoBuf), "%zu pts @ %.2f MSa/s (dt=%.3g %s)",
                      totalPts, sampleRate * 1e-6, static_cast<double>(horizInterv), horUnit.c_str());
    } else {
        std::snprintf(tInfoBuf, sizeof(tInfoBuf), "%zu pts, span=%.4g %s",
                      totalPts, tSpan, horUnit.c_str());
    }

    out.labels.push_back({ -39.0f, 26.8f, 1.10f, EdaLayerId::TopSilk,
                           instW + L" " + chTag + L" (" + fmtOrder + L") — " + PathUtils::Utf8ToWide(tInfoBuf) });
    out.labels.push_back({ -39.0f, -27.2f, 0.95f, traceLayer,
                           chTag + L" " + PathUtils::Utf8ToWide(vInfoBuf) });
    return true;
}

} // namespace Formats
