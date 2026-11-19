#pragma once
// RigolWaveformWfm.h — Rigol DS1000Z/DS1000E/DS2000/4000/6000 Binary Oscilloscope Waveform (.wfm) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

inline bool ParseRigolWaveformWfmBoard(const uint8_t* data, size_t size, const std::wstring& ext, ParsedEdaBoard& out) {
    if (!data || size < 16) return false;
        std::vector<float> samples;
        std::wstring fmtTitle;
        std::wstring metaLabel;

        struct RigolChannelWave {
            std::wstring name;
            std::wstring info;
            std::vector<float> vals;
        };
        std::vector<RigolChannelWave> multiChans;

        // 2. Rigol Binary WFM (0xA5 0xA5 header magic: DS1000Z 0x0038, DS1000E/D 0x0000, DS2000/4000/6000 0x01A4, + fallback)
        if (ext == L".wfm" && size >= 260 && data[0] == 0xA5u && data[1] == 0xA5u) {
            const uint16_t rigolSubMagic = DetailEda200::RdU16LE(data + 2);
            auto readAsciiTrim = [&](size_t off, size_t maxLen) -> std::string {
                if (off >= size) return {};
                size_t lim = std::min<size_t>(maxLen, size - off);
                std::string s;
                for (size_t i = 0; i < lim; ++i) {
                    uint8_t c = data[off + i];
                    if (c == 0) break;
                    if (c >= 32 && c < 127) s.push_back(static_cast<char>(c));
                }
                while (!s.empty() && s.back() == ' ') s.pop_back();
                return s;
            };
            auto formatCoupling = [](uint8_t cpl) -> const wchar_t* {
                if (cpl == 1u) return L"AC";
                if (cpl == 2u) return L"GND";
                return L"DC";
            };
            auto formatVdiv = [](float vs) -> std::wstring {
                char buf[48] = {};
                if (vs < 1.0f) std::snprintf(buf, sizeof(buf), "%.0fmV/div", static_cast<double>(vs * 1000.0f));
                else std::snprintf(buf, sizeof(buf), "%.2fV/div", static_cast<double>(vs));
                return PathUtils::Utf8ToWide(buf);
            };

            // 2a. Rigol DS1000Z / MSO1000Z (0xA5 0xA5 0x38 0x00) & DS2000/4000/6000 (0xA5 0xA5 0xA4 0x01)
            if (rigolSubMagic == 0x0038u || rigolSubMagic == 0x01A4u) {
                const bool isDs2k = (rigolSubMagic == 0x01A4u);
                std::string model = readAsciiTrim(4, 20);
                std::string fwVer = readAsciiTrim(24, 20);
                const uint8_t chMask = data[48];

                struct ZChanDesc {
                    int chIdx = 0;
                    uint8_t coupling = 0;
                    bool invert = false;
                    float vScale = 1.0f;
                    float vOffset = 0.0f;
                };
                auto tryParseChanTable = [&](size_t tblOff, std::vector<ZChanDesc>& outCh) -> bool {
                    outCh.clear();
                    if (tblOff + 4u * 32u > size) return false;
                    bool anyValid = false;
                    for (int c = 0; c < 4; ++c) {
                        const uint8_t* cp = data + tblOff + static_cast<size_t>(c) * 32u;
                        uint8_t en = cp[0];
                        bool maskEn = (chMask > 0u && chMask <= 0x0Fu) && ((chMask & (1u << c)) != 0u);
                        if (en > 1u && !maskEn) return false;
                        float vs = ::Explorer2::ByteIO::RdF32LE(cp + 8);
                        float vo = ::Explorer2::ByteIO::RdF32LE(cp + 12);
                        float pr = ::Explorer2::ByteIO::RdF32LE(cp + 4);
                        if (en == 1u || (en == 0u && maskEn && std::isfinite(vs) && vs > 1e-5f && vs < 1e5f)) {
                            if (!std::isfinite(vs) || vs <= 1e-5f || vs > 1e5f) return false;
                            if (!std::isfinite(vo) || std::fabs(vo) > 1e6f) vo = 0.0f;
                            if (std::isfinite(pr) && pr >= 0.05f && pr <= 1000.0f) vs *= pr;
                            outCh.push_back({ c + 1, cp[1], cp[16] == 1u, vs, vo });
                            anyValid = true;
                        }
                    }
                    return anyValid;
                };

                std::vector<ZChanDesc> zChans;
                if (!tryParseChanTable(120u, zChans)) {
                    if (!tryParseChanTable(80u, zChans) && isDs2k) {
                        tryParseChanTable(164u, zChans);
                    }
                }

                if (!zChans.empty()) {
                    const size_t minHdr = isDs2k ? 420u : 256u;
                    uint32_t explOff = DetailEda200::RdU32LE(data + 52);
                    uint32_t hdrPts  = DetailEda200::RdU32LE(data + 56);
                    if (hdrPts == 0u) hdrPts = DetailEda200::RdU32LE(data + 28);
                    float sampleRate = ::Explorer2::ByteIO::RdFiniteF32LE(data + 60);
                    if (sampleRate <= 0.0f && size >= 104u) sampleRate = ::Explorer2::ByteIO::RdFiniteF32LE(data + 100);
                    uint64_t timeScalePs = (size >= 72u) ? DetailEda200::RdU64LE(data + 64) : 0ULL;

                    size_t dataOff = minHdr;
                    if (explOff >= 248u && explOff + 2u <= size) {
                        dataOff = static_cast<size_t>(explOff);
                    } else if (!isDs2k && size >= 3008u + 16u && hdrPts > 0u && 3008u + static_cast<size_t>(hdrPts) <= size) {
                        dataOff = 3008u;
                    }

                    if (dataOff + zChans.size() * 2u <= size) {
                        const size_t avail = size - dataOff;
                        const size_t nAct = zChans.size();
                        size_t stride = nAct;
                        size_t ptsPerCh = avail / stride;
                        if (hdrPts >= 2u) {
                            if (static_cast<size_t>(hdrPts) * 4u == avail && nAct < 4u) {
                                stride = 4u;
                                ptsPerCh = static_cast<size_t>(hdrPts);
                            } else if (static_cast<size_t>(hdrPts) * nAct <= avail) {
                                stride = nAct;
                                ptsPerCh = static_cast<size_t>(hdrPts);
                            }
                        }

                        if (ptsPerCh >= 2u) {
                            size_t stepPts = std::max<size_t>(1, ptsPerCh / 1024u);
                            multiChans.resize(nAct);
                            for (size_t k = 0; k < nAct; ++k) {
                                multiChans[k].name = L"CH" + std::to_wstring(zChans[k].chIdx);
                                multiChans[k].info = multiChans[k].name + L": " + formatVdiv(zChans[k].vScale) +
                                                     L" " + formatCoupling(zChans[k].coupling);
                                multiChans[k].vals.reserve(std::min<size_t>(ptsPerCh, 1025u));
                            }
                            for (size_t i = 0; i < ptsPerCh && multiChans[0].vals.size() < 1024u; i += stepPts) {
                                size_t rowBase = dataOff + i * stride;
                                if (rowBase + nAct > size) break;
                                for (size_t k = 0; k < nAct; ++k) {
                                    int adc = static_cast<int>(data[rowBase + k]) - 128;
                                    if (zChans[k].invert) adc = -adc;
                                    float v = static_cast<float>(adc) * (zChans[k].vScale / 25.0f) - zChans[k].vOffset;
                                    multiChans[k].vals.push_back(v);
                                }
                            }

                            std::wstring modW = model.empty() ? (isDs2k ? L"DS2000" : L"DS1000Z") : PathUtils::Utf8ToWide(model);
                            fmtTitle = L"Rigol " + modW + L" Oscilloscope Waveform (.wfm — " +
                                       std::to_wstring(nAct) + (nAct == 1 ? L" ch)" : L" chs)");
                            metaLabel = L"Rigol " + modW;
                            if (!fwVer.empty()) metaLabel += L" FW " + PathUtils::Utf8ToWide(fwVer);
                            metaLabel += L" | " + std::to_wstring(ptsPerCh) + L" pts";
                            if (sampleRate >= 1.0f) {
                                char srBuf[48] = {};
                                if (sampleRate >= 1e9f) std::snprintf(srBuf, sizeof(srBuf), " @ %.2f GSa/s", sampleRate * 1e-9);
                                else if (sampleRate >= 1e6f) std::snprintf(srBuf, sizeof(srBuf), " @ %.1f MSa/s", sampleRate * 1e-6);
                                else std::snprintf(srBuf, sizeof(srBuf), " @ %.0f Sa/s", static_cast<double>(sampleRate));
                                metaLabel += PathUtils::Utf8ToWide(srBuf);
                            } else if (timeScalePs > 0ULL) {
                                char tbBuf[48] = {};
                                std::snprintf(tbBuf, sizeof(tbBuf), " | %.2f us/div", static_cast<double>(timeScalePs) * 1e-6);
                                metaLabel += PathUtils::Utf8ToWide(tbBuf);
                            }
                        }
                    }
                }
            }
            // 2b. Rigol DS1000E / DS1000D (0xA5 0xA5 0x00 0x00, 276B header + sequential CH1/CH2 arrays)
            else if (rigolSubMagic == 0x0000u && size >= 280u &&
                     data[49] <= 1u && data[73] <= 1u && (data[49] == 1u || data[73] == 1u))
            {
                const size_t dataOff = 276u;
                uint32_t hdrPts = DetailEda200::RdU32LE(data + 28);
                float sampleRate = ::Explorer2::ByteIO::RdFiniteF32LE(data + 100);
                size_t nAct = (data[49] == 1u ? 1u : 0u) + (data[73] == 1u ? 1u : 0u);
                size_t avail = size - dataOff;
                size_t ptsPerCh = (hdrPts >= 2u && static_cast<size_t>(hdrPts) * nAct <= avail)
                                ? static_cast<size_t>(hdrPts)
                                : (avail / nAct);
                if (ptsPerCh >= 2u) {
                    size_t curOff = dataOff;
                    for (int c = 0; c < 2; ++c) {
                        size_t chHdr = (c == 0) ? 36u : 60u;
                        if (data[chHdr + 13u] != 1u) continue;
                        int32_t vsUv = static_cast<int32_t>(DetailEda200::RdU32LE(data + chHdr));
                        float vs = (vsUv > 0 && vsUv <= 100000000) ? (static_cast<float>(vsUv) * 1e-6f) : 1.0f;
                        float pr = ::Explorer2::ByteIO::RdFiniteF32LE(data + chHdr + 8u);
                        if (pr >= 0.05f && pr <= 1000.0f) vs *= pr;
                        int16_t shiftPx = ::Explorer2::ByteIO::RdI16LE(data + chHdr + 4u);
                        float vOff = static_cast<float>(shiftPx) * (vs / 25.0f);
                        bool inv = (data[chHdr + 12u] == 1u);

                        RigolChannelWave cw;
                        cw.name = L"CH" + std::to_wstring(c + 1);
                        cw.info = cw.name + L": " + formatVdiv(vs);
                        size_t stepPts = std::max<size_t>(1, ptsPerCh / 1024u);
                        for (size_t i = 0; i < ptsPerCh && cw.vals.size() < 1024u; i += stepPts) {
                            int adc = 125 - static_cast<int>(data[curOff + i]);
                            if (inv) adc = -adc;
                            cw.vals.push_back(static_cast<float>(adc) * (vs / 25.0f) - vOff);
                        }
                        multiChans.push_back(std::move(cw));
                        curOff += ptsPerCh;
                    }
                    fmtTitle = L"Rigol DS1000E/D Oscilloscope Waveform (.wfm — " +
                               std::to_wstring(multiChans.size()) + (multiChans.size() == 1 ? L" ch)" : L" chs)");
                    metaLabel = L"Rigol DS1000E (" + std::to_wstring(ptsPerCh) + L" pts";
                    if (sampleRate >= 1.0f) {
                        char srBuf[40] = {};
                        std::snprintf(srBuf, sizeof(srBuf), ", %.1f MSa/s", sampleRate * 1e-6);
                        metaLabel += PathUtils::Utf8ToWide(srBuf);
                    }
                    metaLabel += L")";
                }
            }

            // 2c. Clean Fallback for minimal synthetic Rigol .wfm buffers with zero structured channel flags
            if (multiChans.empty()) {
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
        }
        else {
            return false;
        }

        if (!multiChans.empty()) {
            bool anyOk = false;
            float mn = 1e30f, mx = -1e30f;
            for (const auto& ch : multiChans) {
                if (ch.vals.size() >= 2u) {
                    anyOk = true;
                    for (float v : ch.vals) {
                        mn = std::min(mn, v);
                        mx = std::max(mx, v);
                    }
                }
            }
            if (!anyOk) return false;

            out.kind = EdaDocKind::KiCadSch;
            out.formatTitle = fmtTitle;
            out.netCount = multiChans.size();
            DetailEda200::DrawGraticule(out, -40.0f, -25.0f, 80.0f, 50.0f, 10, 8);

            float span = std::max(1e-6f, mx - mn);
            float yZeroLine = std::clamp(-20.0f + ((0.0f - mn) / span) * 40.0f, -24.0f, 24.0f);
            out.traces.push_back({ -40.0f, yZeroLine, -38.5f, yZeroLine, 0.28f, EdaLayerId::InnerCopper, 0 });

            static constexpr EdaLayerId kChLayers[4] = {
                EdaLayerId::TopCopper,
                EdaLayerId::BottomCopper,
                EdaLayerId::InnerCopper,
                EdaLayerId::SchWire
            };
            for (size_t c = 0; c < multiChans.size(); ++c) {
                const auto& ch = multiChans[c];
                if (ch.vals.size() < 2u) continue;
                EdaLayerId lid = kChLayers[c % 4u];
                int netId = static_cast<int>(c + 1u);
                for (size_t i = 1; i < ch.vals.size(); ++i) {
                    float x0 = -39.0f + (static_cast<float>(i - 1u) / static_cast<float>(ch.vals.size() - 1u)) * 78.0f;
                    float x1 = -39.0f + (static_cast<float>(i)      / static_cast<float>(ch.vals.size() - 1u)) * 78.0f;
                    float y0 = -20.0f + ((ch.vals[i - 1u] - mn) / span) * 40.0f;
                    float y1 = -20.0f + ((ch.vals[i]      - mn) / span) * 40.0f;
                    out.traces.push_back({ x0, y0, x1, y1, 0.30f, lid, netId });
                }
                out.labels.push_back({ -39.0f + static_cast<float>(c) * 20.0f, -27.2f, 0.95f, lid, ch.info });
            }
            out.labels.push_back({ -39.0f, 26.8f, 1.15f, EdaLayerId::TopSilk, metaLabel });
            return true;
        }

        return DetailEda200::RenderSingleScopeTrace(out, samples, fmtTitle, metaLabel);
}

} // namespace Formats
