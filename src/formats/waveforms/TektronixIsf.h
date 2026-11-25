#pragma once
// TektronixIsf.h — Tektronix ISF (Internal Save Format) Oscilloscope Waveform (.isf) Reader
// Implements a genuine `;`-delimited SCPI `KEY VALUE` header parser (handling `:WFMPRE:` / `:WFMP:`
// hierarchical prefixes, `BYT_NR`, `BIT_NR`, `ENC`, `BN_FMT`, `BYT_OR`, `NR_PT`, `XINCR`, `XZERO`,
// `PT_OFF`, `XUNIT`, `YMULT`, `YZERO`, `YOFF`, `YUNIT`, `WFID`, and IEEE 488.2 `#<N><len>` binary
// and ASCII `CURVE` payloads), calibrating both vertical (`(raw - YOFF)*YMULT + YZERO`) and horizontal
// (`XZERO + (i - PT_OFF)*XINCR`) axes!
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

inline bool ParseTektronixIsfBoard(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    if (!data || size < 16u || size > (64u << 20)) return false;

    // Scan ASCII header up to `CURVE` / `:CURVE` / `CURV` command (bounded within first 8 KB)
    const size_t hdrLimit = std::min<size_t>(size, 8192u);
    std::string_view hdrRaw(reinterpret_cast<const char*>(data), hdrLimit);

    int bytNr = 0;
    int bitNr = 0;
    bool msbFirst = true;  // Tektronix ISF default is MSB
    bool isUnsigned = false;
    bool isFloat = false;
    bool isAsciiEnc = false;
    size_t nrPtDeclared = 0u;
    double xincr = 1e-6, xzero = 0.0, ptOff = 0.0;
    double ymult = 1.0,  yzero = 0.0, yoff  = 0.0;
    bool sawIsfKey = false;
    bool foundCurve = false;
    size_t curvePayloadOff = 0u;
    std::string wfidStr, xUnit = "s", yUnit = "V";

    auto unquoteSv = [](std::string_view s) -> std::string {
        s = ReaderText::TrimSv(s);
        if (s.size() >= 2u && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\''))) {
            s = s.substr(1u, s.size() - 2u);
        }
        return std::string(s);
    };

    // Walk `;`-delimited SCPI statements while tracking current `:WFMPRE:` header path prefix
    size_t pos = 0u;
    while (pos < hdrLimit) {
        while (pos < hdrLimit && (static_cast<unsigned char>(hdrRaw[pos]) <= 0x20u || hdrRaw[pos] == ';')) ++pos;
        if (pos >= hdrLimit) break;

        // Read key token up to whitespace or `;` or `#`
        const size_t keyStart = pos;
        while (pos < hdrLimit && static_cast<unsigned char>(hdrRaw[pos]) > 0x20u &&
               hdrRaw[pos] != ';' && hdrRaw[pos] != '#') {
            ++pos;
        }
        std::string_view rawKey = hdrRaw.substr(keyStart, pos - keyStart);
        // Strip leading SCPI `:WFMPRE:` / `:WFMP:` path components to get leaf mnemonic
        const size_t lastColon = rawKey.find_last_of(':');
        if (lastColon != std::string_view::npos && ReaderText::StartsWithCI(rawKey, ":WFM")) {
            sawIsfKey = true;
        }
        std::string leaf = ReaderText::ToUpperAscii(
            (lastColon == std::string_view::npos) ? rawKey : rawKey.substr(lastColon + 1u));

        if (leaf == "CURVE" || leaf == "CURV") {
            while (pos < size && (data[pos] == ' ' || data[pos] == '\t')) ++pos;
            foundCurve = true;
            curvePayloadOff = pos;
            break;
        }

        // Read value up to `;` (respecting `"..."` quoted strings such as `WFID "Ch1, DC coupling..."`)
        while (pos < hdrLimit && (hdrRaw[pos] == ' ' || hdrRaw[pos] == '\t')) ++pos;
        const size_t valStart = pos;
        bool inQ = false;
        while (pos < hdrLimit) {
            const char c = hdrRaw[pos];
            if (c == '"') inQ = !inQ;
            else if (!inQ && (c == ';' || c == '\r' || c == '\n')) break;
            ++pos;
        }
        std::string_view valSv = ReaderText::TrimSv(hdrRaw.substr(valStart, pos - valStart));
        if (pos < hdrLimit && hdrRaw[pos] == ';') ++pos;

        double dv = 0.0;
        if (leaf == "BYT_NR" || leaf == "BYT_N") {
            if (ReaderText::ParseDoubleSv(valSv, dv)) { bytNr = static_cast<int>(dv); sawIsfKey = true; }
        } else if (leaf == "BIT_NR" || leaf == "BIT_N") {
            if (ReaderText::ParseDoubleSv(valSv, dv)) { bitNr = static_cast<int>(dv); sawIsfKey = true; }
        } else if (leaf == "ENC" || leaf == "ENCDG") {
            sawIsfKey = true;
            if (ReaderText::StartsWithCI(valSv, "ASC")) isAsciiEnc = true;
        } else if (leaf == "BN_FMT" || leaf == "BN_F") {
            sawIsfKey = true;
            if (ReaderText::StartsWithCI(valSv, "RP")) isUnsigned = true;
            else if (ReaderText::StartsWithCI(valSv, "FP")) isFloat = true;
            else isUnsigned = false; // RI (signed integer)
        } else if (leaf == "BYT_OR" || leaf == "BYT_O") {
            sawIsfKey = true;
            msbFirst = !ReaderText::StartsWithCI(valSv, "LSB");
        } else if (leaf == "NR_PT" || leaf == "NR_P") {
            uint64_t np = 0;
            if (ReaderText::ParseUint64Sv(valSv, np, 10)) { nrPtDeclared = static_cast<size_t>(np); sawIsfKey = true; }
        } else if (leaf == "XINCR" || leaf == "XIN") {
            if (ReaderText::ParseDoubleSv(valSv, dv) && dv > 0.0) { xincr = dv; sawIsfKey = true; }
        } else if (leaf == "XZERO" || leaf == "XZE") {
            if (ReaderText::ParseDoubleSv(valSv, dv)) { xzero = dv; sawIsfKey = true; }
        } else if (leaf == "PT_OFF" || leaf == "PT_O") {
            if (ReaderText::ParseDoubleSv(valSv, dv)) { ptOff = dv; sawIsfKey = true; }
        } else if (leaf == "YMULT" || leaf == "YMU" || leaf == "YSCALE") {
            if (ReaderText::ParseDoubleSv(valSv, dv) && std::fabs(dv) > 1e-24) { ymult = dv; sawIsfKey = true; }
        } else if (leaf == "YZERO" || leaf == "YZE") {
            if (ReaderText::ParseDoubleSv(valSv, dv)) { yzero = dv; sawIsfKey = true; }
        } else if (leaf == "YOFF" || leaf == "YOF") {
            if (ReaderText::ParseDoubleSv(valSv, dv)) { yoff = dv; sawIsfKey = true; }
        } else if (leaf == "WFID" || leaf == "WFI") {
            wfidStr = unquoteSv(valSv);
            sawIsfKey = true;
        } else if (leaf == "XUNIT" || leaf == "XUN") {
            std::string u = unquoteSv(valSv);
            if (!u.empty()) xUnit = std::move(u);
        } else if (leaf == "YUNIT" || leaf == "YUN") {
            std::string u = unquoteSv(valSv);
            if (!u.empty()) yUnit = std::move(u);
        }
    }

    if (!sawIsfKey || !foundCurve || curvePayloadOff >= size) return false;
    if (bytNr == 0) bytNr = (bitNr == 16) ? 2 : ((bitNr == 32) ? 4 : 1);
    if (bytNr != 1 && bytNr != 2 && bytNr != 4) return false;

    std::vector<float> samples;
    size_t totalSourcePts = 0u;
    size_t cp = curvePayloadOff;

    if (!isAsciiEnc && data[cp] == '#') {
        if (cp + 2u > size || data[cp + 1u] < '0' || data[cp + 1u] > '9') return false;
        const int ndig = data[cp + 1u] - '0';
        size_t sOff = 0u, byteCnt = 0u;
        if (ndig == 0) {
            sOff = cp + 2u;
            byteCnt = size - sOff;
        } else {
            if (cp + 2u + static_cast<size_t>(ndig) > size) return false;
            for (int d = 0; d < ndig; ++d) {
                const uint8_t ch = data[cp + 2u + static_cast<size_t>(d)];
                if (ch < '0' || ch > '9') return false;
                byteCnt = byteCnt * 10u + static_cast<size_t>(ch - '0');
            }
            sOff = cp + 2u + static_cast<size_t>(ndig);
        }
        if (sOff >= size) return false;
        const size_t avail = std::min<size_t>(byteCnt, size - sOff);
        totalSourcePts = avail / static_cast<size_t>(bytNr);
        if (nrPtDeclared > 0u && nrPtDeclared < totalSourcePts) totalSourcePts = nrPtDeclared;
        if (totalSourcePts < 2u) return false;

        const size_t stepPts = std::max<size_t>(1u, totalSourcePts / 1024u);
        samples.reserve(std::min<size_t>(totalSourcePts, 1025u));

        for (size_t idx = 0u; idx < totalSourcePts && samples.size() < 1024u; idx += stepPts) {
            const size_t o = sOff + idx * static_cast<size_t>(bytNr);
            double raw = 0.0;
            if (bytNr == 1) {
                raw = isUnsigned ? static_cast<double>(data[o]) : static_cast<double>(static_cast<int8_t>(data[o]));
            } else if (bytNr == 2) {
                const uint16_t u = msbFirst ? ByteIO::RdU16BE(data + o) : ByteIO::RdU16LE(data + o);
                raw = isUnsigned ? static_cast<double>(u) : static_cast<double>(static_cast<int16_t>(u));
            } else {
                const uint32_t u = msbFirst ? ByteIO::RdU32BE(data + o) : ByteIO::RdU32LE(data + o);
                if (isFloat) {
                    float fv = 0.0f;
                    std::memcpy(&fv, &u, 4);
                    raw = std::isfinite(fv) ? static_cast<double>(fv) : 0.0;
                } else {
                    raw = isUnsigned ? static_cast<double>(u) : static_cast<double>(static_cast<int32_t>(u));
                }
            }
            samples.push_back(static_cast<float>((raw - yoff) * ymult + yzero));
        }
    } else {
        std::string_view asc(reinterpret_cast<const char*>(data + cp), std::min<size_t>(size - cp, 262144u));
        size_t ap = 0u;
        while (ap < asc.size() && samples.size() < 2048u) {
            while (ap < asc.size() && (asc[ap] == ' ' || asc[ap] == '\t' || asc[ap] == ',' || asc[ap] == '\r' || asc[ap] == '\n')) ++ap;
            if (ap >= asc.size() || asc[ap] == ';') break;
            const size_t st = ap;
            while (ap < asc.size() && asc[ap] != ' ' && asc[ap] != '\t' && asc[ap] != ',' &&
                   asc[ap] != ';' && asc[ap] != '\r' && asc[ap] != '\n') ++ap;
            double raw = 0.0;
            if (!ReaderText::ParseDoubleSv(asc.substr(st, ap - st), raw)) break;
            samples.push_back(static_cast<float>((raw - yoff) * ymult + yzero));
        }
        totalSourcePts = samples.size();
    }

    if (samples.size() < 2u) return false;

    const double tStart = xzero + (0.0 - ptOff) * xincr;
    const double tEnd   = xzero + (static_cast<double>(totalSourcePts - 1u) - ptOff) * xincr;

    wchar_t metaBuf[220] = {};
    swprintf_s(metaBuf, L"Tek ISF%ls (%zu pts, t=[%.3g..%.3g %hs], dT=%.3g %hs)",
               wfidStr.empty() ? L"" : (L" " + PathUtils::Utf8ToWide(wfidStr.substr(0, std::min<size_t>(wfidStr.size(), 28u)))).c_str(),
               totalSourcePts, tStart, tEnd, xUnit.c_str(), xincr, xUnit.c_str());

    const std::wstring fmtTitle = L"Tektronix ISF Oscilloscope Trace (.isf — " + std::to_wstring(totalSourcePts) + L" pts)";
    return DetailIndustrialAndWaveEda::RenderSingleScopeTrace(out, samples, fmtTitle, metaBuf);
}

} // namespace Formats
