#pragma once
// SigMfRfCapture.h — DARPA / GNU Radio Signal Metadata & Baseband IQ Capture (.sigmf-meta + .sigmf-data) Reader
// Uses `Json::ParseDocument` to validate `global` (`core:datatype`, `core:sample_rate`), `captures`
// (`core:frequency`), and `annotations` (`core:freq_lower_edge`..`core:freq_upper_edge`), and decodes
// companion `<stem>.sigmf-data` up to 64K samples across `cf32_le`, `ci16_le`, `ci8`, `cu8`, `ri16_le`,
// and `rf32_le`, plotting calibrated I & Q baseband channels + RF frequency-time allocation bands
// cleanly with ZERO fake `SDR_RX0` PCB chips!
// NOTE: Included inside `namespace Explorer2`. Do NOT add `#include` directives here.

namespace Formats {

inline bool ParseSigMfRfCapture(
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    ParsedEdaBoard& out)
{
    std::string_view sv;
    if (!FormatPrologue::PrepareUtf8TextNoNul(data, size, sv, 24u, (16u << 20))) return false;

    Json::Value root;
    if (!Json::ParseDocument(sv, root) || !root.IsObject()) return false;

    const Json::Value* globalObj = root.Find("global");
    if (!globalObj || !globalObj->IsObject()) return false;

    const std::string dataType = ReaderText::ToLowerAscii(globalObj->GetString("core:datatype"));
    if (dataType.empty()) return false;

    double sampleRate = 1e6;
    if (const Json::Value* sr = globalObj->Find("core:sample_rate"); sr && sr->IsNumber() && sr->numVal > 0.0) {
        sampleRate = sr->numVal;
    }
    const std::string hwDesc = globalObj->GetString("core:hw");

    struct RfCaptureSpan {
        uint64_t sampleStart = 0ULL;
        double   centerFreqHz = 0.0;
    };
    std::vector<RfCaptureSpan> captures;
    if (const Json::Value* capArr = root.Find("captures"); capArr && capArr->IsArray()) {
        for (const auto& c : capArr->arrVal) {
            if (!c.IsObject()) return false;
            RfCaptureSpan cs{};
            cs.sampleStart = static_cast<uint64_t>(std::max<int64_t>(0, c.GetInt64("core:sample_start", 0)));
            if (const Json::Value* fq = c.Find("core:frequency"); fq && fq->IsNumber() && std::isfinite(fq->numVal)) {
                cs.centerFreqHz = fq->numVal;
            }
            captures.push_back(cs);
        }
    }

    struct RfAnnotation {
        uint64_t sampleStart = 0ULL;
        uint64_t sampleCount = 1000ULL;
        double   fLowHz  = 0.0;
        double   fHighHz = 0.0;
        bool     hasFreqBounds = false;
        std::string label;
    };
    std::vector<RfAnnotation> annots;
    if (const Json::Value* annArr = root.Find("annotations"); annArr && annArr->IsArray()) {
        for (const auto& a : annArr->arrVal) {
            if (!a.IsObject()) return false;
            RfAnnotation ra{};
            ra.sampleStart = static_cast<uint64_t>(std::max<int64_t>(0, a.GetInt64("core:sample_start", 0)));
            ra.sampleCount = static_cast<uint64_t>(std::max<int64_t>(1, a.GetInt64("core:sample_count", 1000)));
            const Json::Value* fl = a.Find("core:freq_lower_edge");
            const Json::Value* fh = a.Find("core:freq_upper_edge");
            if (fl && fh && fl->IsNumber() && fh->IsNumber() && std::isfinite(fl->numVal) && std::isfinite(fh->numVal)) {
                ra.fLowHz  = fl->numVal;
                ra.fHighHz = std::max(fl->numVal + 1.0, fh->numVal);
                ra.hasFreqBounds = true;
            }
            ra.label = a.GetString("core:label");
            if (ra.label.empty()) ra.label = a.GetString("core:comment", "RF_BURST");
            if (annots.size() < 512u) annots.push_back(std::move(ra));
        }
    }

    // Attempt to load companion `<stem>.sigmf-data` up to 64K samples (max 512 KB read)
    std::vector<float> iChan, qChan;
    size_t decodedSamples = 0u;
    if (fullPath.size() > 11u && ReaderText::EndsWithWideCI(fullPath, L".sigmf-meta")) {
        const std::wstring dataPath = fullPath.substr(0u, fullPath.size() - 11u) + L".sigmf-data";
        std::vector<uint8_t> rawIq;
        if (PathUtils::ReadFileBytes(dataPath, 512u * 1024u, rawIq) && rawIq.size() >= 4u) {
            constexpr size_t kMaxIqSamples = 65536u;
            if (dataType == "cf32_le" || dataType == "cf32") {
                const size_t n = std::min<size_t>(rawIq.size() / 8u, kMaxIqSamples);
                iChan.reserve(n); qChan.reserve(n);
                for (size_t k = 0; k < n; ++k) {
                    iChan.push_back(ByteIO::RdFiniteF32LE(rawIq.data() + k * 8u));
                    qChan.push_back(ByteIO::RdFiniteF32LE(rawIq.data() + k * 8u + 4u));
                }
                decodedSamples = n;
            } else if (dataType == "ci16_le" || dataType == "ci16") {
                const size_t n = std::min<size_t>(rawIq.size() / 4u, kMaxIqSamples);
                iChan.reserve(n); qChan.reserve(n);
                for (size_t k = 0; k < n; ++k) {
                    iChan.push_back(static_cast<float>(ByteIO::RdI16LE(rawIq.data() + k * 4u)) / 32768.0f);
                    qChan.push_back(static_cast<float>(ByteIO::RdI16LE(rawIq.data() + k * 4u + 2u)) / 32768.0f);
                }
                decodedSamples = n;
            } else if (dataType == "ci8") {
                const size_t n = std::min<size_t>(rawIq.size() / 2u, kMaxIqSamples);
                iChan.reserve(n); qChan.reserve(n);
                for (size_t k = 0; k < n; ++k) {
                    iChan.push_back(static_cast<float>(static_cast<int8_t>(rawIq[k * 2u])) / 128.0f);
                    qChan.push_back(static_cast<float>(static_cast<int8_t>(rawIq[k * 2u + 1u])) / 128.0f);
                }
                decodedSamples = n;
            } else if (dataType == "cu8") {
                const size_t n = std::min<size_t>(rawIq.size() / 2u, kMaxIqSamples);
                iChan.reserve(n); qChan.reserve(n);
                for (size_t k = 0; k < n; ++k) {
                    iChan.push_back((static_cast<float>(rawIq[k * 2u]) - 127.5f) / 127.5f);
                    qChan.push_back((static_cast<float>(rawIq[k * 2u + 1u]) - 127.5f) / 127.5f);
                }
                decodedSamples = n;
            } else if (dataType == "ri16_le" || dataType == "ri16") {
                const size_t n = std::min<size_t>(rawIq.size() / 2u, kMaxIqSamples);
                iChan.reserve(n);
                for (size_t k = 0; k < n; ++k) {
                    iChan.push_back(static_cast<float>(ByteIO::RdI16LE(rawIq.data() + k * 2u)) / 32768.0f);
                }
                decodedSamples = n;
            } else if (dataType == "rf32_le" || dataType == "rf32") {
                const size_t n = std::min<size_t>(rawIq.size() / 4u, kMaxIqSamples);
                iChan.reserve(n);
                for (size_t k = 0; k < n; ++k) {
                    iChan.push_back(ByteIO::RdFiniteF32LE(rawIq.data() + k * 4u));
                }
                decodedSamples = n;
            }
        }
    }

    if (captures.empty() && annots.empty() && decodedSamples < 2u) return false;

    out = ParsedEdaBoard{};
    out.kind = EdaDocKind::KiCadSch;

    const double centerHz = (!captures.empty() && captures[0].centerFreqHz > 0.0) ? captures[0].centerFreqHz : 915.0e6;
    const double minFreq  = centerHz - sampleRate * 0.5;
    const double maxFreq  = centerHz + sampleRate * 0.5;
    uint64_t maxSampleEnd = std::max<uint64_t>(1000ULL, decodedSamples);
    for (const auto& a : annots) maxSampleEnd = std::max(maxSampleEnd, a.sampleStart + a.sampleCount);
    for (const auto& c : captures) maxSampleEnd = std::max(maxSampleEnd, c.sampleStart + 1000ULL);

    const float frameW = 84.0f;
    const float specH  = 34.0f;
    const float specCy = (decodedSamples >= 2u) ? 20.0f : 0.0f;

    // 1. RF Time-Frequency Allocation Graticule (NO fake `SDR_RX0` PCB component!)
    DetailIndustrialAndWaveEda::DrawGraticule(out, -frameW * 0.5f, specCy - specH * 0.5f, frameW, specH, 10, 6);
    // Center LO carrier axis
    out.traces.push_back({ -frameW * 0.5f, specCy, frameW * 0.5f, specCy, 0.20f, EdaLayerId::InnerCopper, 1 });

    wchar_t loLbl[256] = {};
    const double loMhz = centerHz / 1e6;
    if (std::fabs(loMhz) >= 1e9)
        std::swprintf(loLbl, std::size(loLbl), L"LO %.4g MHz  |  Fs %.4g MS/s  |  %ls%ls",
                      loMhz, sampleRate / 1e6, PathUtils::Utf8ToWide(dataType).substr(0, 64).c_str(),
                      decodedSamples >= 2u ? (L"  |  " + std::to_wstring(decodedSamples) + L" samples").c_str() : L"");
    else
        std::swprintf(loLbl, std::size(loLbl), L"LO %.4f MHz  |  Fs %.4g MS/s  |  %ls%ls",
                      loMhz, sampleRate / 1e6, PathUtils::Utf8ToWide(dataType).substr(0, 64).c_str(),
                      decodedSamples >= 2u ? (L"  |  " + std::to_wstring(decodedSamples) + L" samples").c_str() : L"");
    loLbl[std::size(loLbl) - 1] = L'\0';
    out.labels.push_back({ -frameW * 0.49f, specCy + specH * 0.5f + 1.8f, 1.05f, EdaLayerId::TopSilk, loLbl });

    // Capture segment markers along spectrogram
    for (size_t ci = 0u; ci < std::min<size_t>(captures.size(), 16u); ++ci) {
        const float cx = -frameW * 0.48f + (static_cast<float>(captures[ci].sampleStart) / static_cast<float>(maxSampleEnd)) * (frameW * 0.96f);
        out.traces.push_back({ cx, specCy - specH * 0.48f, cx, specCy + specH * 0.48f, 0.22f, EdaLayerId::InnerCopper, 1 });
        if (captures[ci].centerFreqHz > 0.0) {
            wchar_t cfBuf[96] = {};
            const double capMhz = captures[ci].centerFreqHz / 1e6;
            if (capMhz >= 1e9) std::swprintf(cfBuf, std::size(cfBuf), L"Cap%zu: %.4g MHz", ci, capMhz);
            else std::swprintf(cfBuf, std::size(cfBuf), L"Cap%zu: %.3f MHz", ci, capMhz);
            cfBuf[std::size(cfBuf) - 1] = L'\0';
            out.labels.push_back({ cx + 0.6f, specCy + specH * 0.42f - static_cast<float>(ci % 3u) * 2.2f, 0.72f, EdaLayerId::SchLabel, cfBuf });
        }
    }

    // Annotation time-frequency allocation bands
    for (size_t ai = 0u; ai < annots.size(); ++ai) {
        const auto& a = annots[ai];
        const float tx0 = -frameW * 0.48f + (static_cast<float>(a.sampleStart) / static_cast<float>(maxSampleEnd)) * (frameW * 0.96f);
        const float tx1 = -frameW * 0.48f + (static_cast<float>(a.sampleStart + a.sampleCount) / static_cast<float>(maxSampleEnd)) * (frameW * 0.96f);
        float fy0 = specCy - 5.0f, fy1 = specCy + 5.0f;
        if (a.hasFreqBounds && maxFreq > minFreq) {
            const double n0 = std::clamp((a.fLowHz  - minFreq) / (maxFreq - minFreq), 0.02, 0.96);
            const double n1 = std::clamp((a.fHighHz - minFreq) / (maxFreq - minFreq), 0.05, 0.99);
            fy0 = static_cast<float>(specCy - specH * 0.46 + n0 * (specH * 0.92));
            fy1 = static_cast<float>(specCy - specH * 0.46 + n1 * (specH * 0.92));
        }
        const float bx1 = std::max(tx0 + 3.5f, tx1);
        const float by1 = std::max(fy0 + 3.0f, fy1);
        DrawEdaBoxRect(out, tx0, fy0, bx1, by1, 0.28f, EdaLayerId::TopCopper, static_cast<int>(ai + 2u));
        out.labels.push_back({ tx0 + 0.5f, (fy0 + by1) * 0.5f - 0.3f, 0.78f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(a.label) });
    }

    // 2. Calibrated Baseband I & Q Waveform Lanes below the spectrogram when `.sigmf-data` is present!
    if (decodedSamples >= 2u) {
        auto plotLane = [&](const std::vector<float>& vec, float laneCy, float laneH, EdaLayerId lid, int netId, const wchar_t* tag) {
            if (vec.size() < 2u) return;
            DrawEdaBoxRect(out, -frameW * 0.5f, laneCy - laneH * 0.5f, frameW * 0.5f, laneCy + laneH * 0.5f, 0.16f, EdaLayerId::EdgeCuts, 0);
            out.traces.push_back({ -frameW * 0.5f, laneCy, frameW * 0.5f, laneCy, 0.08f, EdaLayerId::BottomSilk, 0 });

            float mn = vec[0], mx = vec[0];
            for (float v : vec) { mn = std::min(mn, v); mx = std::max(mx, v); }
            const float span = std::max(1e-6f, mx - mn);
            const size_t stride = std::max<size_t>(1u, vec.size() / 1200u);

            float prevX = 0.0f, prevY = 0.0f;
            bool hasPrev = false;
            for (size_t i = 0u; i < vec.size(); i += stride) {
                const float x = -frameW * 0.49f + (static_cast<float>(i) / static_cast<float>(vec.size() - 1u)) * (frameW * 0.98f);
                const float y = (laneCy - laneH * 0.44f) + ((vec[i] - mn) / span) * (laneH * 0.88f);
                if (hasPrev) out.traces.push_back({ prevX, prevY, x, y, 0.26f, lid, netId });
                prevX = x; prevY = y; hasPrev = true;
            }
            wchar_t cLbl[96] = {};
            swprintf_s(cLbl, L"%ls [%.3g..%.3g]", tag, static_cast<double>(mn), static_cast<double>(mx));
            out.labels.push_back({ -frameW * 0.49f, laneCy + laneH * 0.5f + 0.4f, 0.82f, lid, cLbl });
            out.pads.push_back({ -frameW * 0.5f - 1.2f, laneCy, 0.9f, 0.9f, 0.0f, 0.0f, 1, lid, tag, L"SIGMF_IQ" });
        };

        plotLane(iChan, -10.0f, 14.0f, EdaLayerId::TopCopper, 10, qChan.empty() ? L"REAL" : L"I (In-Phase)");
        if (qChan.size() >= 2u) {
            plotLane(qChan, -28.0f, 14.0f, EdaLayerId::BottomCopper, 11, L"Q (Quadrature)");
        }
    }

    out.netCount = std::max<size_t>(1u, captures.size() + annots.size() + (iChan.empty() ? 0u : 1u) + (qChan.empty() ? 0u : 1u));
    wchar_t title[240] = {};
    swprintf_s(title, L"SigMF RF Capture (.sigmf-meta %ls, %zu captures, %zu annotations%ls)%ls",
               PathUtils::Utf8ToWide(dataType).c_str(), captures.size(), annots.size(),
               decodedSamples >= 2u ? (L", " + std::to_wstring(decodedSamples) + L" IQ pts").c_str() : L"",
               hwDesc.empty() ? L"" : (L" — " + PathUtils::Utf8ToWide(hwDesc)).c_str());
    out.formatTitle = title;
    FinalizeBoardBounds(out);
    out.valid = true;
    return true;
}

} // namespace Formats
