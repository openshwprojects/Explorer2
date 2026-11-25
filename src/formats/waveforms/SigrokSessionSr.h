#pragma once
// SigrokSessionSr.h — Sigrok / PulseView Logic & Mixed-Signal Analyzer Session (.sr) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

inline bool ParseSigrokSessionSrBoard(const uint8_t* data, size_t size, const std::wstring& ext, ParsedEdaBoard& out) {
    // Sigrok / PulseView Logic & Mixed-Signal Analyzer Session (.sr — ZIP with metadata + multi-chunk logic-1-N & analog-1-M-N)
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

        DetailIndustrialAndWaveEda::DrawGraticule(out, -40.0f, -24.0f, 80.0f, 48.0f, 16, 8);

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
    return false;
}

} // namespace Formats
