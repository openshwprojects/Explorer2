#pragma once
// YokogawaWaveformWvf.h — Yokogawa DL-Series Digital Oscilloscope Waveform (.wvf + .hdr) Reader
// Parses Yokogawa `$PublicInfo` ASCII metadata from companion `<stem>.hdr` / `<stem>.HDR` (or inline
// `$PublicInfo` header embedded at the top of single-file `.wvf` captures):
//   - `$Format`, `$Endian` (`Big` / `Little`), `$DataOffset`, `GroupNumber`, `TraceTotalNumber`
//   - Per-group / per-trace `TraceName`, `BlockSize`, `VResolution`, `VOffset`, `VUnit`,
//     `HResolution`, `HOffset`, `HUnit`, `DataType` (`IS1`/`IS2`/`IS4`/`IU1`/`IU2`/`IU4`/`FS4`/`FD8`)
// Decodes calibrated multi-channel binary `.wvf` sample blocks and renders via `WaveformPlotCommon.h`!
// NOTE: Included inside `namespace Explorer2`. Do NOT add `#include` directives here.

namespace Formats {

namespace DetailYokogawaWvf {

struct WvfTraceSpec {
    std::string name;
    std::string dataType = "IS2"; // IS1, IS2, IS4, IU1, IU2, IU4, FS4, FD8
    std::string vUnit = "V";
    std::string hUnit = "s";
    size_t blockSize = 0u;
    double vResolution = 1.0;
    double vOffset = 0.0;
    double hResolution = 1e-6;
    double hOffset = 0.0;
};

inline size_t BytesPerWvfSample(std::string_view dt) {
    if (ReaderText::StartsWithCI(dt, "IS1") || ReaderText::StartsWithCI(dt, "IU1") || ReaderText::StartsWithCI(dt, "B1")) return 1u;
    if (ReaderText::StartsWithCI(dt, "IS2") || ReaderText::StartsWithCI(dt, "IU2")) return 2u;
    if (ReaderText::StartsWithCI(dt, "IS4") || ReaderText::StartsWithCI(dt, "IU4") || ReaderText::StartsWithCI(dt, "FS4") || ReaderText::StartsWithCI(dt, "F4")) return 4u;
    if (ReaderText::StartsWithCI(dt, "FD8") || ReaderText::StartsWithCI(dt, "F8")) return 8u;
    return 0u;
}

inline double DecodeWvfSample(const uint8_t* p, std::string_view dt, bool bigEndian) {
    if (ReaderText::StartsWithCI(dt, "IS1")) return static_cast<double>(static_cast<int8_t>(p[0]));
    if (ReaderText::StartsWithCI(dt, "IU1") || ReaderText::StartsWithCI(dt, "B1")) return static_cast<double>(p[0]);
    if (ReaderText::StartsWithCI(dt, "IS2")) {
        const uint16_t u = bigEndian ? ByteIO::RdU16BE(p) : ByteIO::RdU16LE(p);
        return static_cast<double>(static_cast<int16_t>(u));
    }
    if (ReaderText::StartsWithCI(dt, "IU2")) {
        return static_cast<double>(bigEndian ? ByteIO::RdU16BE(p) : ByteIO::RdU16LE(p));
    }
    if (ReaderText::StartsWithCI(dt, "IS4")) {
        const uint32_t u = bigEndian ? ByteIO::RdU32BE(p) : ByteIO::RdU32LE(p);
        return static_cast<double>(static_cast<int32_t>(u));
    }
    if (ReaderText::StartsWithCI(dt, "IU4")) {
        return static_cast<double>(bigEndian ? ByteIO::RdU32BE(p) : ByteIO::RdU32LE(p));
    }
    if (ReaderText::StartsWithCI(dt, "FS4") || ReaderText::StartsWithCI(dt, "F4")) {
        const uint32_t u = bigEndian ? ByteIO::RdU32BE(p) : ByteIO::RdU32LE(p);
        float f = 0.0f;
        std::memcpy(&f, &u, 4);
        return std::isfinite(f) ? static_cast<double>(f) : 0.0;
    }
    if (ReaderText::StartsWithCI(dt, "FD8") || ReaderText::StartsWithCI(dt, "F8")) {
        const uint64_t u = bigEndian ? ByteIO::RdU64BE(p) : ByteIO::RdU64LE(p);
        double d = 0.0;
        std::memcpy(&d, &u, 8);
        return std::isfinite(d) ? d : 0.0;
    }
    return 0.0;
}

inline bool ParseYokogawaHdrAscii(
    std::string_view hdrSv,
    bool& outBigEndian,
    size_t& outDataOffset,
    std::string& outModel,
    std::vector<WvfTraceSpec>& outTraces)
{
    outBigEndian = false; // Yokogawa PC-saved `.wvf` default = Little, DL708/750 Motorola = Big
    outDataOffset = 0u;
    outModel.clear();
    outTraces.clear();

    hdrSv = ReaderText::StripUtf8Bom(hdrSv);
    if (!ContainsTextCI(hdrSv, "$PublicInfo")) return false;

    // Yokogawa `.hdr` organizes channels into `$Group1` .. `$GroupN`, where each key line has `Key valTrace1 valTrace2 ...`!
    struct GroupTable {
        int traceCount = 0;
        std::map<std::string, std::vector<std::string>> rows;
    };
    std::vector<GroupTable> groups;
    size_t declaredTotalTraces = 0u;

    size_t pos = 0u;
    std::string_view rawLine;
    while (ReaderText::NextLineSv(hdrSv, pos, rawLine)) {
        // Stop if inline binary section marker `$PrivateInfo` or NUL bytes follow `$End`
        if (rawLine.find('\0') != std::string_view::npos) break;
        std::string_view line = ReaderText::TrimSv(rawLine);
        if (line.empty() || line.front() == '/' || line.front() == ';') continue;

        if (ReaderText::StartsWithCI(line, "$Group")) {
            groups.emplace_back();
            continue;
        }
        if (ReaderText::StartsWithCI(line, "$PrivateInfo") || ReaderText::AsciiEqCI(line, "$EndOfHeader")) {
            break;
        }

        std::string_view rem = line, keyTok;
        if (!ReaderText::NextTokenSv(rem, keyTok)) continue;

        std::vector<std::string> vals;
        std::string_view vt;
        while (ReaderText::NextTokenSv(rem, vt)) {
            // Also split comma-separated values if formatted as `CH1,CH2`
            size_t cp = 0u;
            while (cp < vt.size()) {
                const size_t cm = vt.find(',', cp);
                std::string_view sub = ReaderText::TrimSv((cm == std::string_view::npos) ? vt.substr(cp) : vt.substr(cp, cm - cp));
                if (!sub.empty()) vals.emplace_back(sub);
                if (cm == std::string_view::npos) break;
                cp = cm + 1u;
            }
        }
        if (vals.empty()) continue;

        const std::string upKey = ReaderText::ToUpperAscii(keyTok);
        if (upKey == "ENDIAN" || upKey == "$ENDIAN") {
            outBigEndian = ReaderText::StartsWithCI(vals[0], "BIG");
        } else if (upKey == "DATAOFFSET" || upKey == "$DATAOFFSET") {
            uint64_t off = 0;
            if (ReaderText::ParseUint64Sv(vals[0], off, 10)) outDataOffset = static_cast<size_t>(off);
        } else if (upKey == "MODEL") {
            outModel = vals[0];
        } else if (upKey == "TRACETOTALNUMBER") {
            uint64_t tt = 0;
            if (ReaderText::ParseUint64Sv(vals[0], tt, 10)) declaredTotalTraces = static_cast<size_t>(tt);
        } else {
            if (groups.empty()) groups.emplace_back();
            if (upKey == "TRACENUMBER") {
                int tc = 0;
                if (ReaderText::ParseIntSv(vals[0], tc) && tc > 0) groups.back().traceCount = tc;
            } else {
                groups.back().rows[upKey] = std::move(vals);
            }
        }
    }

    for (size_t gi = 0; gi < groups.size(); ++gi) {
        const auto& g = groups[gi];
        auto findRow = [&](const char* k) -> const std::vector<std::string>* {
            auto it = g.rows.find(k);
            return (it != g.rows.end() && !it->second.empty()) ? &it->second : nullptr;
        };

        const auto* rNames = findRow("TRACENAME");
        const auto* rBlk   = findRow("BLOCKSIZE");
        const auto* rDt    = findRow("VDataType");
        if (!rDt) rDt      = findRow("VDATATYPE");
        if (!rDt) rDt      = findRow("DATATYPE");
        const auto* rVRes  = findRow("VRESOLUTION");
        const auto* rVOff  = findRow("VOFFSET");
        const auto* rVUnit = findRow("VUNIT");
        const auto* rHRes  = findRow("HRESOLUTION");
        const auto* rHOff  = findRow("HOFFSET");
        const auto* rHUnit = findRow("HUNIT");

        size_t nTr = static_cast<size_t>(std::max(0, g.traceCount));
        if (nTr == 0u && rNames) nTr = rNames->size();
        if (nTr == 0u && rBlk)   nTr = rBlk->size();
        if (nTr == 0u && declaredTotalTraces > 0u && groups.size() == 1u) nTr = declaredTotalTraces;
        nTr = std::min<size_t>(nTr, 32u);

        auto pickCol = [](const std::vector<std::string>* vec, size_t idx) -> std::string_view {
            if (!vec || vec->empty()) return {};
            return (*vec)[std::min(idx, vec->size() - 1u)];
        };

        for (size_t ti = 0; ti < nTr; ++ti) {
            WvfTraceSpec ts;
            std::string_view nm = pickCol(rNames, ti);
            ts.name = nm.empty() ? ("CH" + std::to_string(outTraces.size() + 1u)) : std::string(nm);
            std::string_view dt = pickCol(rDt, ti);
            if (!dt.empty()) ts.dataType = std::string(dt);
            if (BytesPerWvfSample(ts.dataType) == 0u) return false;

            uint64_t bs = 0;
            if (ReaderText::ParseUint64Sv(pickCol(rBlk, ti), bs, 10)) ts.blockSize = static_cast<size_t>(bs);
            ReaderText::ParseDoubleSv(pickCol(rVRes, ti), ts.vResolution);
            ReaderText::ParseDoubleSv(pickCol(rVOff, ti), ts.vOffset);
            ReaderText::ParseDoubleSv(pickCol(rHRes, ti), ts.hResolution);
            ReaderText::ParseDoubleSv(pickCol(rHOff, ti), ts.hOffset);

            std::string_view vu = pickCol(rVUnit, ti);
            if (!vu.empty()) ts.vUnit = std::string(vu);
            std::string_view hu = pickCol(rHUnit, ti);
            if (!hu.empty()) ts.hUnit = std::string(hu);

            if (ts.blockSize >= 2u && outTraces.size() < 16u) {
                outTraces.push_back(std::move(ts));
            }
        }
    }

    return !outTraces.empty();
}

} // namespace DetailYokogawaWvf

inline bool ParseYokogawaWaveformWvf(
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    ParsedEdaBoard& outBoard)
{
    using namespace DetailYokogawaWvf;

    if (!data || size < 8u || size > (64u << 20)) return false;

    bool bigEndian = false;
    size_t dataOffset = 0u;
    std::string scopeModel;
    std::vector<WvfTraceSpec> traces;
    bool parsedHdr = false;

    // 1. Check inline `$PublicInfo` ASCII header at top of `.wvf` (single-file `.wvf` variant)
    {
        const size_t scanLen = std::min<size_t>(size, 65536u);
        std::string_view topSv(reinterpret_cast<const char*>(data), scanLen);
        if (ContainsTextCI(topSv.substr(0u, std::min<size_t>(scanLen, 256u)), "$PublicInfo")) {
            // Find end of ASCII header (`#` IEEE block, `$EndOfHeader`, or explicit `DataOffset`)
            size_t hdrEnd = scanLen;
            const size_t eoh = ReaderText::ToUpperAscii(topSv).find("$ENDOFHEADER");
            if (eoh != std::string::npos) {
                hdrEnd = topSv.find('\n', eoh);
                hdrEnd = (hdrEnd == std::string_view::npos) ? scanLen : (hdrEnd + 1u);
            }
            if (ParseYokogawaHdrAscii(topSv.substr(0u, hdrEnd), bigEndian, dataOffset, scopeModel, traces)) {
                if (dataOffset == 0u && eoh != std::string::npos) dataOffset = hdrEnd;
                parsedHdr = true;
            }
        }
    }

    // 2. Otherwise load companion `<stem>.hdr` / `<stem>.HDR` from the same directory!
    if (!parsedHdr && !fullPath.empty()) {
        const size_t dot = fullPath.find_last_of(L'.');
        const size_t sep = fullPath.find_last_of(L"\\/");
        if (dot != std::wstring::npos && (sep == std::wstring::npos || dot > sep)) {
            const std::wstring stem = fullPath.substr(0u, dot);
            std::vector<uint8_t> hdrBytes;
            if (PathUtils::ReadFileBytes(stem + L".hdr", 4u * 1024u * 1024u, hdrBytes) ||
                PathUtils::ReadFileBytes(stem + L".HDR", 4u * 1024u * 1024u, hdrBytes))
            {
                std::string_view hsv(reinterpret_cast<const char*>(hdrBytes.data()), hdrBytes.size());
                parsedHdr = ParseYokogawaHdrAscii(hsv, bigEndian, dataOffset, scopeModel, traces);
            }
        }
    }

    if (!parsedHdr || traces.empty() || dataOffset >= size) return false;

    // Verify binary `.wvf` payload contains the declared trace sample blocks (`Block` layout = contiguous trace blocks)
    size_t curByteOff = dataOffset;
    const size_t commonPts = std::min<size_t>(traces[0].blockSize, 65536u);
    if (commonPts < 2u) return false;

    std::vector<SpiceWaveVar> plotVars;
    SpiceWaveVar timeVar;
    timeVar.name = "Time (" + traces[0].hUnit + ")";
    timeVar.unit = traces[0].hUnit;

    const double hRes = (traces[0].hResolution > 0.0) ? traces[0].hResolution : 1e-6;
    const double hOff = traces[0].hOffset;

    for (size_t ti = 0; ti < traces.size(); ++ti) {
        const auto& ts = traces[ti];
        const size_t bps = BytesPerWvfSample(ts.dataType);
        if (bps == 0u) return false;
        const size_t traceBytes = ts.blockSize * bps;
        if (curByteOff + traceBytes > size) {
            if (ti == 0u) return false; // First trace truncated -> invalid `.wvf`!
            break;
        }

        const size_t nRead = std::min<size_t>(ts.blockSize, commonPts);
        if (plotVars.empty()) {
            timeVar.values.resize(nRead);
            for (size_t k = 0; k < nRead; ++k) {
                timeVar.values[k] = hOff + static_cast<double>(k) * hRes;
            }
            plotVars.push_back(std::move(timeVar));
        }

        SpiceWaveVar yVar;
        yVar.name = ts.name;
        yVar.unit = ts.vUnit;
        yVar.values.resize(plotVars[0].values.size(), 0.0);

        const size_t limit = std::min(nRead, yVar.values.size());
        for (size_t k = 0; k < limit; ++k) {
            const double rawAdc = DecodeWvfSample(data + curByteOff + k * bps, ts.dataType, bigEndian);
            yVar.values[k] = ts.vResolution * rawAdc + ts.vOffset;
        }
        plotVars.push_back(std::move(yVar));
        curByteOff += traceBytes;
    }

    if (plotVars.size() < 2u) return false;

    wchar_t subTitle[180] = {};
    swprintf_s(subTitle, L"%ls%zu ch, %zu pts, %ls-endian",
               scopeModel.empty() ? L"" : (PathUtils::Utf8ToWide(scopeModel) + L", ").c_str(),
               plotVars.size() - 1u, plotVars[0].values.size(),
               bigEndian ? L"Big" : L"Little");

    return RenderSpiceWaveformsToBoard(subTitle, L"Yokogawa DL-Series Waveform (.wvf)", plotVars, outBoard);
}

} // namespace Formats
