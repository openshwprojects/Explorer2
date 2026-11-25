#pragma once
// DreamSourceLabDsl.h — DreamSourceLab DSView Logic Analyzer Session (.dsl) Reader
// Opens the `.dsl` ZIP archive via `ZipDeflate`, parses the `header` INI metadata (`[version]`,
// `[header]`, `samplerate`, `total probes`, `total blocks`, `probe<N> = <name>`, `trigger*`),
// and unpacks `L-<probe>-<block>` packed 1-bit digital sample streams (`bit b` of byte `i` is sample
// `i * 8 + b`) across all capture blocks into multi-channel digital logic analyzer traces!
// NOTE: Included inside `namespace Explorer2`. Do NOT add `#include` directives here.

namespace Formats {

namespace DetailDreamSourceDsl {

inline uint64_t ParseDslSamplerateHz(std::string_view sv) {
    sv = ReaderText::TrimSv(sv);
    if (sv.empty()) return 0ULL;
    size_t numEnd = 0u;
    while (numEnd < sv.size() && ((sv[numEnd] >= '0' && sv[numEnd] <= '9') || sv[numEnd] == '.')) ++numEnd;
    double val = 0.0;
    if (numEnd == 0u || !ReaderText::ParseDoubleSv(sv.substr(0u, numEnd), val) || !(val > 0.0)) return 0ULL;
    std::string unit = ReaderText::ToLowerAscii(ReaderText::TrimSv(sv.substr(numEnd)));
    if (unit.rfind("ghz", 0) == 0 || unit == "g") val *= 1e9;
    else if (unit.rfind("mhz", 0) == 0 || unit == "m") val *= 1e6;
    else if (unit.rfind("khz", 0) == 0 || unit == "k") val *= 1e3;
    return static_cast<uint64_t>(std::llround(val));
}

template <typename ItemT>
inline std::string ZipItemNameUtf8(const ItemT& item) {
    if constexpr (MemberDetect::HasRelativePath<ItemT>::value) {
        if constexpr (std::is_constructible_v<std::string_view, decltype((item.relativePath))>)
            return std::string(std::string_view(item.relativePath));
        else
            return PathUtils::WideToUtf8(item.relativePath);
    } else if constexpr (MemberDetect::HasName<ItemT>::value) {
        if constexpr (std::is_constructible_v<std::string_view, decltype((item.name))>)
            return std::string(std::string_view(item.name));
        else
            return PathUtils::WideToUtf8(item.name);
    } else if constexpr (MemberDetect::HasPath<ItemT>::value) {
        if constexpr (std::is_constructible_v<std::string_view, decltype((item.path))>)
            return std::string(std::string_view(item.path));
        else
            return PathUtils::WideToUtf8(item.path);
    } else if constexpr (MemberDetect::HasFileName<ItemT>::value) {
        if constexpr (std::is_constructible_v<std::string_view, decltype((item.fileName))>)
            return std::string(std::string_view(item.fileName));
        else
            return PathUtils::WideToUtf8(item.fileName);
    } else {
        return {};
    }
}

} // namespace DetailDreamSourceDsl

inline bool ParseDreamSourceLabDsl(
    const uint8_t* data,
    size_t size,
    ParsedEdaBoard& outBoard)
{
    using namespace DetailDreamSourceDsl;

    // Must be a valid PKZIP archive (`PK\x03\x04`)
    if (!data || size < 30u || size > (64u << 20)) return false;
    if (data[0] != 'P' || data[1] != 'K') return false;

    std::vector<ZipDeflate::ArchiveItemMeta> zEntries;
    if (!ZipDeflate::ListZipEntriesFromMemory(data, size, zEntries) || zEntries.empty()) {
        return false;
    }

    std::string headerEntryName;
    // Map `(probeIdx, blockIdx) -> zipEntryFullName` for `L-<probe>-<block>` digital bitplane files
    std::map<std::pair<int, int>, std::string> logicBlocks;
    std::set<int> discoveredProbes;
    int maxBlockIdx = -1;

    for (const auto& ze : zEntries) {
        const std::string fullName = ZipItemNameUtf8(ze);
        if (fullName.empty()) continue;
        const size_t sl = fullName.find_last_of("/\\");
        std::string_view base = (sl == std::string::npos) ? std::string_view(fullName) : std::string_view(fullName).substr(sl + 1u);
        if (base.empty()) continue;

        if (ReaderText::AsciiEqCI(base, "header")) {
            headerEntryName = fullName;
        } else if (base.size() >= 5u && (base[0] == 'L' || base[0] == 'l') && base[1] == '-') {
            // `L-<probe>-<block>`
            const size_t dash2 = base.find('-', 2u);
            if (dash2 != std::string_view::npos && dash2 > 2u && dash2 + 1u < base.size()) {
                int pIdx = -1, bIdx = -1;
                if (ReaderText::ParseIntSv(base.substr(2u, dash2 - 2u), pIdx) &&
                    ReaderText::ParseIntSv(base.substr(dash2 + 1u), bIdx) &&
                    pIdx >= 0 && pIdx < 128 && bIdx >= 0 && bIdx < 4096)
                {
                    logicBlocks[{ pIdx, bIdx }] = fullName;
                    discoveredProbes.insert(pIdx);
                    maxBlockIdx = std::max(maxBlockIdx, bIdx);
                }
            }
        }
    }

    // Require BOTH DSView `header` INI and at least one `L-<probe>-<block>` stream (distinguishes `.dsl` from `.sr` sigrok `logic-1-1`!)
    if (headerEntryName.empty() || logicBlocks.empty()) return false;

    std::vector<uint8_t> hdrBytes;
    if (!ZipDeflate::ExtractZipEntryFromMemory(data, size, headerEntryName.c_str(), hdrBytes) || hdrBytes.empty()) {
        return false;
    }

    std::string_view hsv(reinterpret_cast<const char*>(hdrBytes.data()), hdrBytes.size());
    if (hsv.find('\0') != std::string_view::npos) return false;

    uint64_t samplerateHz = 0ULL;
    int totalProbes = 0;
    int totalBlocks = 0;
    uint64_t totalSamplesDeclared = 0ULL;
    std::string driverName;
    std::string triggerSpec;
    std::unordered_map<int, std::string> probeNames;
    bool sawHeaderSec = false;

    size_t pos = 0u;
    std::string_view rawLine;
    while (ReaderText::NextLineSv(hsv, pos, rawLine)) {
        std::string_view line = ReaderText::TrimSv(rawLine);
        if (line.empty() || line.front() == ';' || line.front() == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            std::string_view sec = ReaderText::TrimSv(line.substr(1u, line.size() - 2u));
            if (ReaderText::AsciiEqCI(sec, "header") || ReaderText::AsciiEqCI(sec, "version")) {
                sawHeaderSec = true;
            }
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string_view::npos) continue;
        std::string key = ReaderText::ToLowerAscii(ReaderText::TrimSv(line.substr(0u, eq)));
        std::string_view val = ReaderText::TrimSv(line.substr(eq + 1u));

        if (key == "samplerate") {
            samplerateHz = ParseDslSamplerateHz(val);
        } else if (key == "total probes") {
            ReaderText::ParseIntSv(val, totalProbes);
        } else if (key == "total blocks") {
            ReaderText::ParseIntSv(val, totalBlocks);
        } else if (key == "total samples") {
            ReaderText::ParseUint64Sv(val, totalSamplesDeclared, 10);
        } else if (key == "driver") {
            driverName = std::string(val);
        } else if (key.rfind("trigger", 0) == 0 && !val.empty() && val != "0" && triggerSpec.empty()) {
            triggerSpec = key + "=" + std::string(val);
        } else if (key.rfind("probe", 0) == 0 && key.size() > 5u) {
            int pNum = -1;
            if (ReaderText::ParseIntSv(std::string_view(key).substr(5u), pNum) && pNum >= 0 && pNum < 128 && !val.empty()) {
                probeNames[pNum] = std::string(val);
            }
        }
    }

    if (!sawHeaderSec && samplerateHz == 0ULL && probeNames.empty()) return false;

    const int nBlocks = std::max(totalBlocks, maxBlockIdx + 1);
    std::vector<DetailHwBin::DecodedWaveSignal> sigs;
    uint64_t maxSampleIdx = 0ULL;

    // DSView `probe` numbering in `header` can be 0-based (`probe0` matches `L-0-0`) or 1-based (`probe1` matches `L-0-0` when `probe0` is absent):
    const bool oneBasedProbeNames = (discoveredProbes.count(0) > 0 && probeNames.count(0) == 0 && probeNames.count(1) > 0);

    for (int probeIdx : discoveredProbes) {
        if (sigs.size() >= 32u) break;

        DetailHwBin::DecodedWaveSignal ws;
        ws.bitWidth = 1;

        const int nameKey = oneBasedProbeNames ? (probeIdx + 1) : probeIdx;
        auto itNm = probeNames.find(nameKey);
        const std::string pLabel = (itNm != probeNames.end() && !itNm->second.empty())
            ? ("D" + std::to_string(probeIdx) + ":" + itNm->second)
            : ("D" + std::to_string(probeIdx));
        ws.name = PathUtils::Utf8ToWide(pLabel);

        uint64_t sampleCursor = 0ULL;
        int prevBit = -1;

        for (int blkIdx = 0; blkIdx < std::min(nBlocks, 64); ++blkIdx) {
            auto itEntry = logicBlocks.find({ probeIdx, blkIdx });
            if (itEntry == logicBlocks.end()) continue;

            std::vector<uint8_t> packedBits;
            if (!ZipDeflate::ExtractZipEntryFromMemory(data, size, itEntry->second.c_str(), packedBits) || packedBits.empty()) {
                continue;
            }

            // Cap inspected bytes per block to 32 KB (= 262,144 1-bit samples) for crisp interactive rendering
            const size_t inspectBytes = std::min<size_t>(packedBits.size(), 32768u);
            for (size_t bi = 0u; bi < inspectBytes; ++bi) {
                const uint8_t byteVal = packedBits[bi];
                // Fast skip over constant all-0 or all-1 bytes!
                if (prevBit == 0 && byteVal == 0x00u) {
                    sampleCursor += 8ULL;
                    continue;
                }
                if (prevBit == 1 && byteVal == 0xFFu) {
                    sampleCursor += 8ULL;
                    continue;
                }
                for (int b = 0; b < 8; ++b) {
                    const int bit = (byteVal >> b) & 1;
                    if (bit != prevBit && ws.changes.size() < 4096u) {
                        ws.changes.push_back({ sampleCursor, bit ? "1" : "0" });
                        prevBit = bit;
                    }
                    ++sampleCursor;
                }
            }
            if (packedBits.size() > inspectBytes) {
                sampleCursor += static_cast<uint64_t>(packedBits.size() - inspectBytes) * 8ULL;
            }
        }

        if (sampleCursor > 0ULL) {
            maxSampleIdx = std::max(maxSampleIdx, sampleCursor - 1ULL);
            sigs.push_back(std::move(ws));
        }
    }

    if (sigs.empty()) return false;
    if (totalSamplesDeclared > 0ULL && totalSamplesDeclared - 1ULL < maxSampleIdx) {
        maxSampleIdx = totalSamplesDeclared - 1ULL;
    }

    wchar_t srStr[64] = {};
    if (samplerateHz >= 1000000ULL) {
        swprintf_s(srStr, L"%.3g MHz", static_cast<double>(samplerateHz) / 1e6);
    } else if (samplerateHz >= 1000ULL) {
        swprintf_s(srStr, L"%.3g kHz", static_cast<double>(samplerateHz) / 1e3);
    } else if (samplerateHz > 0ULL) {
        swprintf_s(srStr, L"%llu Hz", static_cast<unsigned long long>(samplerateHz));
    } else {
        swprintf_s(srStr, L"DSLogic");
    }

    wchar_t titleBuf[240] = {};
    swprintf_s(titleBuf, L"DreamSourceLab DSView Capture (.dsl — %zu probes @ %ls, %llu samples%ls)",
               sigs.size(), srStr, static_cast<unsigned long long>(maxSampleIdx + 1ULL),
               driverName.empty() ? L"" : (L", " + PathUtils::Utf8ToWide(driverName)).c_str());

    return DetailHwBin::RenderWaveSignalsToBoard(sigs, 0ULL, std::max<uint64_t>(1ULL, maxSampleIdx), titleBuf, outBoard);
}

} // namespace Formats
