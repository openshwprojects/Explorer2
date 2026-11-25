#pragma once
// IeeeComtradeCff.h — IEEE Std C37.111 Power System Transient Data Exchange Single-File (.cff) reader
// Parses `--- file type: CFG ---`, optional `INF`/`HDR`, and `--- file type: DAT ASCII|BINARY ---`
// sections: station/IED metadata, `TT,nA##A,nD##D` channel counts, `y = a * x + b` analog scaling,
// 16-bit packed binary & CSV ASCII oscillogram records, and protection relay trip status channels.
// Included inside namespace Explorer2 in EdaReadersVlsiWave.cpp (do NOT put #include <...> here).

namespace Formats {

namespace DetailComtradeCff {

struct AnalogCh {
    std::wstring name;
    std::wstring unit;
    double scaleA = 1.0;
    double offsetB = 0.0;
};

struct DigitalCh {
    std::wstring name;
};

inline void SplitCsvFields(std::string_view line, std::vector<std::string_view>& outCols) {
    outCols.clear();
    size_t p = 0;
    while (true) {
        const size_t c = line.find(',', p);
        if (c == std::string_view::npos) {
            outCols.push_back(ReaderText::TrimSv(line.substr(p)));
            break;
        }
        outCols.push_back(ReaderText::TrimSv(line.substr(p, c - p)));
        p = c + 1u;
    }
}

inline bool ParseCountWithSuffix(std::string_view tok, char suffixUpper, uint32_t& outCount) {
    tok = ReaderText::TrimSv(tok);
    if (tok.size() < 2) return false;
    const char last = static_cast<char>(std::toupper(static_cast<unsigned char>(tok.back())));
    if (last != suffixUpper) return false;
    uint64_t v = 0;
    if (!ReaderText::ParseUint64Sv(tok.substr(0, tok.size() - 1), v, 10) || v > 4096ULL) return false;
    outCount = static_cast<uint32_t>(v);
    return true;
}

} // namespace DetailComtradeCff

inline bool ParseIeeeComtradeCff(
    const uint8_t* data,
    size_t size,
    ParsedEdaBoard& out)
{
    if (!data || size < 40 || size > 64u * 1024u * 1024u) return false;
    std::string_view fullSv = ReaderText::StripUtf8Bom(
        std::string_view(reinterpret_cast<const char*>(data), size));

    // Scan `--- file type: <SEC> ---` section headers (note: DAT BINARY section may legitimately contain NUL bytes!)
    std::string_view cfgSv;
    std::string_view datSv;
    bool datIsBinary = false;
    uint64_t datDeclaredBytes = 0;
    bool sawCfg = false, sawDat = false;

    size_t scanPos = 0;
    while (scanPos < fullSv.size()) {
        const size_t lineEnd = fullSv.find('\n', scanPos);
        std::string_view rawHdr = (lineEnd == std::string_view::npos)
            ? fullSv.substr(scanPos)
            : fullSv.substr(scanPos, lineEnd - scanPos);
        const size_t afterHdr = (lineEnd == std::string_view::npos) ? fullSv.size() : (lineEnd + 1u);

        std::string_view hdr = ReaderText::TrimSv(rawHdr);
        if (hdr.size() >= 16 && hdr.substr(0, 3) == "---" && hdr.substr(hdr.size() - 3) == "---") {
            std::string_view inner = ReaderText::TrimSv(hdr.substr(3, hdr.size() - 6));
            if (ReaderText::StartsWithCI(inner, "file type:")) {
                std::string_view spec = ReaderText::TrimSv(inner.substr(10));
                if (ReaderText::StartsWithCI(spec, "DAT BINARY")) {
                    // Optional byte count after DAT BINARY: `DAT BINARY: 240` or `DAT BINARY 240`
                    std::string_view rest = ReaderText::TrimSv(spec.substr(10));
                    if (!rest.empty() && rest.front() == ':') rest = ReaderText::TrimSv(rest.substr(1));
                    if (!rest.empty()) {
                        std::string_view cntTok;
                        ReaderText::NextTokenSv(rest, cntTok);
                        ReaderText::ParseUint64Sv(cntTok, datDeclaredBytes, 10);
                    }
                    datIsBinary = true;
                    sawDat = true;
                    if (datDeclaredBytes > 0) {
                        if (datDeclaredBytes > fullSv.size() - afterHdr) return false;
                        datSv = fullSv.substr(afterHdr, static_cast<size_t>(datDeclaredBytes));
                        scanPos = afterHdr + static_cast<size_t>(datDeclaredBytes);
                    } else {
                        datSv = fullSv.substr(afterHdr);
                        break;
                    }
                    continue;
                }

                // Find next `--- file type:` delimiter for text sections
                size_t nextDelim = fullSv.size();
                size_t probe = afterHdr;
                while (probe < fullSv.size()) {
                    const size_t nl = fullSv.find('\n', probe);
                    std::string_view cand = ReaderText::TrimSv(
                        (nl == std::string_view::npos) ? fullSv.substr(probe) : fullSv.substr(probe, nl - probe));
                    if (cand.size() >= 16 && cand.substr(0, 3) == "---" && cand.substr(cand.size() - 3) == "---" &&
                        ReaderText::StartsWithCI(ReaderText::TrimSv(cand.substr(3, cand.size() - 6)), "file type:")) {
                        nextDelim = probe;
                        break;
                    }
                    probe = (nl == std::string_view::npos) ? fullSv.size() : (nl + 1u);
                }

                std::string_view body = fullSv.substr(afterHdr, nextDelim - afterHdr);
                if (ReaderText::StartsWithCI(spec, "CFG")) {
                    cfgSv = body;
                    sawCfg = true;
                } else if (ReaderText::StartsWithCI(spec, "DAT")) {
                    datSv = body;
                    datIsBinary = (spec.find("BINARY") != std::string_view::npos ||
                                   spec.find("binary") != std::string_view::npos);
                    sawDat = true;
                }
                scanPos = nextDelim;
                continue;
            }
        }
        scanPos = afterHdr;
    }

    if (!sawCfg || !sawDat || cfgSv.find('\0') != std::string_view::npos) return false;

    // Parse CFG lines
    std::vector<std::string_view> cfgLines;
    {
        size_t p = 0;
        std::string_view ln;
        while (ReaderText::NextLineSv(cfgSv, p, ln)) {
            if (!ln.empty() && ln.back() == '\r') ln.remove_prefix(0), ln = ln.substr(0, ln.size() - 1);
            if (!ReaderText::TrimSv(ln).empty()) cfgLines.push_back(ReaderText::TrimSv(ln));
        }
    }
    if (cfgLines.size() < 6) return false;

    std::vector<std::string_view> cols;
    DetailComtradeCff::SplitCsvFields(cfgLines[0], cols);
    std::wstring stationName = PathUtils::Utf8ToWide(cols.empty() ? std::string_view("STATION") : cols[0]);
    std::wstring recDevId    = (cols.size() >= 2) ? PathUtils::Utf8ToWide(cols[1]) : L"IED";

    DetailComtradeCff::SplitCsvFields(cfgLines[1], cols);
    uint64_t totalCh = 0;
    uint32_t nAnalog = 0, nDigital = 0;
    if (cols.size() < 3 ||
        !ReaderText::ParseUint64Sv(cols[0], totalCh, 10) ||
        !DetailComtradeCff::ParseCountWithSuffix(cols[1], 'A', nAnalog) ||
        !DetailComtradeCff::ParseCountWithSuffix(cols[2], 'D', nDigital)) {
        return false;
    }
    if (totalCh == 0 || totalCh != static_cast<uint64_t>(nAnalog) + static_cast<uint64_t>(nDigital)) {
        return false;
    }
    if (cfgLines.size() < 2u + nAnalog + nDigital + 2u) return false;

    std::vector<DetailComtradeCff::AnalogCh> analogChs;
    analogChs.reserve(nAnalog);
    for (uint32_t i = 0; i < nAnalog; ++i) {
        DetailComtradeCff::SplitCsvFields(cfgLines[2u + i], cols);
        if (cols.size() < 7) return false;
        DetailComtradeCff::AnalogCh ach;
        ach.name = PathUtils::Utf8ToWide(cols[1].empty() ? cols[0] : cols[1]);
        ach.unit = PathUtils::Utf8ToWide(cols[4]);
        if (!ReaderText::ParseDoubleSv(cols[5], ach.scaleA) || !std::isfinite(ach.scaleA) ||
            !ReaderText::ParseDoubleSv(cols[6], ach.offsetB) || !std::isfinite(ach.offsetB)) {
            return false;
        }
        analogChs.push_back(std::move(ach));
    }

    std::vector<DetailComtradeCff::DigitalCh> digitalChs;
    digitalChs.reserve(nDigital);
    for (uint32_t i = 0; i < nDigital; ++i) {
        DetailComtradeCff::SplitCsvFields(cfgLines[2u + nAnalog + i], cols);
        if (cols.size() < 2) return false;
        DetailComtradeCff::DigitalCh dch;
        dch.name = PathUtils::Utf8ToWide(cols[1].empty() ? cols[0] : cols[1]);
        digitalChs.push_back(std::move(dch));
    }

    double lineFreqHz = 50.0;
    ReaderText::ParseDoubleSv(cfgLines[2u + nAnalog + nDigital], lineFreqHz);

    // Prepare DecodedWaveSignals (analog first, then digital status channels)
    std::vector<DetailHwBin::DecodedWaveSignal> sigs(nAnalog + nDigital);
    for (uint32_t i = 0; i < nAnalog; ++i) {
        sigs[i].isReal = true;
        sigs[i].name = analogChs[i].name + (analogChs[i].unit.empty() ? L"" : (L" (" + analogChs[i].unit + L")"));
    }
    for (uint32_t i = 0; i < nDigital; ++i) {
        sigs[nAnalog + i].isReal = false;
        sigs[nAnalog + i].bitWidth = 1;
        sigs[nAnalog + i].name = digitalChs[i].name;
    }

    size_t sampleRows = 0;
    uint64_t minT = UINT64_MAX, maxT = 0;

    if (datIsBinary) {
        const size_t digWords = (static_cast<size_t>(nDigital) + 15u) / 16u;
        const size_t stride = 8u + static_cast<size_t>(nAnalog) * 2u + digWords * 2u;
        if (datSv.size() < stride) return false;
        // Trim optional trailing CRLF at very end of CFF file if present
        size_t usableBytes = (datSv.size() / stride) * stride;
        if (datSv.size() - usableBytes > 4u) return false;

        const uint8_t* p = reinterpret_cast<const uint8_t*>(datSv.data());
        const size_t nRecs = std::min<size_t>(usableBytes / stride, 131072u);
        for (size_t r = 0; r < nRecs; ++r) {
            const uint8_t* rec = p + r * stride;
            uint32_t rawTs = ByteIO::RdU32LE(rec + 4);
            const uint64_t ts = (rawTs == 0xFFFFFFFFu) ? static_cast<uint64_t>(r * 100u) : static_cast<uint64_t>(rawTs);
            minT = std::min(minT, ts);
            maxT = std::max(maxT, ts);

            for (uint32_t a = 0; a < nAnalog; ++a) {
                const int16_t rawI16 = static_cast<int16_t>(ByteIO::RdU16LE(rec + 8u + a * 2u));
                const double phys = analogChs[a].scaleA * static_cast<double>(rawI16) + analogChs[a].offsetB;
                sigs[a].realChanges.emplace_back(ts, phys);
            }
            const size_t digBase = 8u + static_cast<size_t>(nAnalog) * 2u;
            for (uint32_t d = 0; d < nDigital; ++d) {
                const uint16_t w = ByteIO::RdU16LE(rec + digBase + (d / 16u) * 2u);
                const char bitCh = ((w >> (d & 15u)) & 1u) ? '1' : '0';
                auto& chg = sigs[nAnalog + d].changes;
                if (chg.empty() || chg.back().second[0] != bitCh) {
                    chg.emplace_back(ts, std::string(1, bitCh));
                }
            }
            ++sampleRows;
        }
    } else {
        if (datSv.find('\0') != std::string_view::npos) return false;
        size_t dp = 0;
        std::string_view dLine;
        while (ReaderText::NextLineSv(datSv, dp, dLine)) {
            dLine = ReaderText::TrimSv(dLine);
            if (dLine.empty() || dLine == "\x1A") continue;
            DetailComtradeCff::SplitCsvFields(dLine, cols);
            if (cols.size() != 2u + nAnalog + nDigital) return false;

            uint64_t sampIdx = 0;
            double tsDouble = 0.0;
            if (!ReaderText::ParseUint64Sv(cols[0], sampIdx, 10)) return false;
            if (!cols[1].empty()) {
                if (!ReaderText::ParseDoubleSv(cols[1], tsDouble) || !std::isfinite(tsDouble)) return false;
            } else {
                tsDouble = static_cast<double>(sampleRows) * 100.0;
            }
            const uint64_t ts = (tsDouble >= 0.0 && tsDouble < 1e15)
                ? static_cast<uint64_t>(std::llround(tsDouble))
                : static_cast<uint64_t>(sampleRows * 100ULL);
            minT = std::min(minT, ts);
            maxT = std::max(maxT, ts);

            for (uint32_t a = 0; a < nAnalog; ++a) {
                double rawX = 0.0;
                if (!ReaderText::ParseDoubleSv(cols[2u + a], rawX) || !std::isfinite(rawX)) return false;
                const double phys = analogChs[a].scaleA * rawX + analogChs[a].offsetB;
                sigs[a].realChanges.emplace_back(ts, phys);
            }
            for (uint32_t d = 0; d < nDigital; ++d) {
                std::string_view dv = cols[2u + nAnalog + d];
                if (dv != "0" && dv != "1") return false;
                auto& chg = sigs[nAnalog + d].changes;
                if (chg.empty() || chg.back().second != dv) {
                    chg.emplace_back(ts, std::string(dv));
                }
            }
            if (++sampleRows >= 131072u) break;
        }
    }

    if (sampleRows < 2) return false;
    if (minT >= maxT) {
        // Ensure strictly monotonic time axis when timestamps were omitted or identical
        minT = 0;
        maxT = static_cast<uint64_t>(sampleRows - 1);
        for (uint32_t a = 0; a < nAnalog; ++a) {
            for (size_t r = 0; r < sigs[a].realChanges.size(); ++r) sigs[a].realChanges[r].first = r;
        }
    }

    wchar_t freqBuf[64] = {};
    if (!std::isfinite(lineFreqHz) || std::fabs(lineFreqHz) >= 1e9)
        std::swprintf(freqBuf, std::size(freqBuf), L"%.4g", lineFreqHz);
    else
        std::swprintf(freqBuf, std::size(freqBuf), L"%.0f", lineFreqHz);
    freqBuf[std::size(freqBuf) - 1] = L'\0';
    std::wstring title = L"IEEE C37.111 COMTRADE [" + stationName.substr(0, 64) + L" / " + recDevId.substr(0, 64) +
                         L"] (" + std::to_wstring(nAnalog) + L"A + " + std::to_wstring(nDigital) + L"D ch, " +
                         std::to_wstring(sampleRows) + L" samples, " + freqBuf + L" Hz)";
    return DetailHwBin::RenderWaveSignalsToBoard(sigs, minT, maxT, title, out);
}

} // namespace Formats
