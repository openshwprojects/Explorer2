#pragma once
// SaleaeLogicSal.h — Saleae Logic 2 Digital & Analog Capture (.sal) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

inline bool ParseSaleaeLogicSalBoard(const uint8_t* data, size_t size, const std::wstring& ext, ParsedEdaBoard& out) {
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
    return false;
}

} // namespace Formats
