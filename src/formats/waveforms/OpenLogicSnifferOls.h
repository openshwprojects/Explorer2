#pragma once
// OpenLogicSnifferOls.h — OpenBench Logic Sniffer / SUMP Logic Analyzer Capture (.ols)
// Parses `;Rate:`, `;Channels:`, `;EnabledChannels:`, and `<hexValue>@<sampleIdx>` capture events,
// rendering digital logic analyzer waveforms via shared `DetailHwBin::RenderWaveSignalsToBoard`.

namespace Formats {

inline bool ParseOpenLogicSnifferOls(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    std::string_view sv;
    if (!FormatPrologue::PrepareUtf8TextNoNul(data, size, sv, 16u, (64u << 20))) return false;

    uint64_t sampleRateHz = 0ULL;
    int declaredChannels  = 8;
    uint32_t enabledMask  = 0u;
    bool sawHeaderMeta    = false;

    struct OlsSample {
        uint64_t tick = 0ULL;
        uint32_t val  = 0u;
    };
    std::vector<OlsSample> samples;

    size_t linePos = 0u;
    std::string_view rawLine;
    while (ReaderText::NextLineSv(sv, linePos, rawLine)) {
        std::string_view line = ReaderText::TrimSv(rawLine);
        if (line.empty()) continue;

        if (line.front() == ';') {
            std::string_view meta = ReaderText::TrimSv(line.substr(1u));
            const size_t colon = meta.find(':');
            if (colon == std::string_view::npos) continue;
            const std::string key = ReaderText::ToUpperAscii(ReaderText::TrimSv(meta.substr(0u, colon)));
            std::string_view val  = ReaderText::TrimSv(meta.substr(colon + 1u));
            if (key == "RATE") {
                if (!ReaderText::ParseUint64Sv(val, sampleRateHz) || sampleRateHz == 0ULL) return false;
                sawHeaderMeta = true;
            } else if (key == "CHANNELS") {
                if (!ReaderText::ParseIntSv(val, declaredChannels) || declaredChannels < 1 || declaredChannels > 32) {
                    return false;
                }
                sawHeaderMeta = true;
            } else if (key == "ENABLEDCHANNELS") {
                char buf[32] = {};
                std::memcpy(buf, val.data(), std::min<size_t>(val.size(), 31u));
                enabledMask = static_cast<uint32_t>(std::strtoul(buf, nullptr, 0));
                sawHeaderMeta = true;
            } else if (key == "SIZE" || key == "DIVIDER" || key == "FLAGS" || key == "TRIGGERPOSITION") {
                sawHeaderMeta = true;
            }
            continue;
        }

        // Data line MUST be `<hexValue>@<decimalSampleNumber>`
        const size_t atPos = line.find('@');
        if (atPos == std::string_view::npos || atPos == 0u || atPos + 1u >= line.size()) return false;
        std::string_view hexPart  = line.substr(0u, atPos);
        std::string_view tickPart = line.substr(atPos + 1u);

        uint32_t wordVal = 0u;
        if (hexPart.size() > 8u) return false;
        for (char c : hexPart) {
            const int hv = ReaderText::HexVal(c);
            if (hv < 0) return false;
            wordVal = (wordVal << 4u) | static_cast<uint32_t>(hv);
        }

        uint64_t tick = 0ULL;
        if (!ReaderText::ParseUint64Sv(tickPart, tick)) return false;
        if (!samples.empty() && tick < samples.back().tick) return false; // Monotonic timestamps required!
        if (samples.size() < 131072u) {
            samples.push_back({ tick, wordVal });
        }
    }

    if (!sawHeaderMeta || samples.size() < 2u) return false;

    const int nCh = std::clamp(declaredChannels, 1, 16);
    if (enabledMask == 0u) enabledMask = (nCh >= 32) ? 0xFFFFFFFFu : ((1u << nCh) - 1u);

    std::vector<DetailHwBin::DecodedWaveSignal> sigs;
    for (int ch = 0; ch < 32 && sigs.size() < 16u; ++ch) {
        if ((enabledMask & (1u << ch)) == 0u) continue;
        DetailHwBin::DecodedWaveSignal ws{};
        ws.name = L"CH" + std::to_wstring(ch);
        ws.bitWidth = 1;
        ws.isReal = false;
        char prevBit = 0;
        for (const auto& s : samples) {
            const char bit = ((s.val >> ch) & 1u) ? '1' : '0';
            if (ws.changes.empty() || bit != prevBit) {
                ws.changes.emplace_back(s.tick, std::string(1u, bit));
                prevBit = bit;
            }
        }
        sigs.push_back(std::move(ws));
    }
    if (sigs.empty()) return false;

    const uint64_t minT = samples.front().tick;
    const uint64_t maxT = std::max(minT + 1ULL, samples.back().tick);

    out = ParsedEdaBoard{};
    wchar_t title[200] = {};
    if (sampleRateHz >= 1000000ULL) {
        swprintf_s(title, L"OpenBench Logic Sniffer Capture (.ols, %zu ch, %zu events @ %.3g MHz)",
                   sigs.size(), samples.size(), static_cast<double>(sampleRateHz) / 1e6);
    } else {
        swprintf_s(title, L"OpenBench Logic Sniffer Capture (.ols, %zu ch, %zu events)",
                   sigs.size(), samples.size());
    }
    if (!DetailHwBin::RenderWaveSignalsToBoard(sigs, minT, maxT, title, out)) return false;
    out.netCount = sigs.size();
    return true;
}

} // namespace Formats
