#pragma once
// HspiceBinaryTr0.h — Synopsys HSPICE Transient / AC / DC Sweep Waveform (.tr0 / .ac0 / .sw0) Reader
// Parses both POST=1 Fortran unformatted binary records (`9601` 32-bit float and `2001` 64-bit double,
// little- and big-endian) and POST=2 fixed-width ASCII records delimited by the `$&%#` sentinel,
// rendering via WaveformPlotCommon.h.
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

inline bool ParseHspiceBinaryTr0Board(
    const uint8_t* data,
    size_t size,
    const std::wstring& ext,
    ParsedEdaBoard& out)
{
    if (!data || size < 32 || size > 64u * 1024u * 1024u) return false;

    std::string_view fullSv(reinterpret_cast<const char*>(data), size);
    const size_t hdrEndMarker = fullSv.find("$&%#");
    if (hdrEndMarker == std::string_view::npos || hdrEndMarker > 256u * 1024u) {
        return false;
    }

    const bool isBinaryPost1 = (size >= 24u && (ByteIO::RdU32LE(data) == 4u || ByteIO::RdU32BE(data) == 4u));
    const bool bigEndian = isBinaryPost1 && (ByteIO::RdU32BE(data) == 4u);

    std::string hdrClean;
    hdrClean.reserve(hdrEndMarker);
    for (size_t i = 0; i < hdrEndMarker; ++i) {
        const uint8_t c = data[i];
        if (c >= 32u && c < 127u) hdrClean.push_back(static_cast<char>(c));
        else if (c == '\r' || c == '\n' || c == '\t') hdrClean.push_back(' ');
    }

    std::vector<std::string> tokens;
    {
        size_t p = 0;
        while (p < hdrClean.size()) {
            while (p < hdrClean.size() && hdrClean[p] == ' ') ++p;
            if (p >= hdrClean.size()) break;
            const size_t s = p;
            while (p < hdrClean.size() && hdrClean[p] != ' ') ++p;
            tokens.emplace_back(hdrClean.data() + s, p - s);
        }
    }
    if (tokens.size() < 4u) return false;

    const bool isDouble2001 = (hdrClean.find("2001") != std::string::npos);

    auto isAllDigits = [](const std::string& s) -> bool {
        if (s.empty()) return false;
        for (char c : s) {
            if (c < '0' || c > '9') return false;
        }
        return true;
    };

    std::vector<std::string> varNames;
    for (int i = static_cast<int>(tokens.size()) - 1; i >= 0; --i) {
        if (isAllDigits(tokens[static_cast<size_t>(i)])) break;
        varNames.push_back(tokens[static_cast<size_t>(i)]);
    }
    std::reverse(varNames.begin(), varNames.end());
    if (varNames.size() < 2u || varNames.size() > 1024u) return false;
    const size_t nVars = varNames.size();

    std::vector<double> flatVals;
    bool sawSweepTerminator = false;

    if (isBinaryPost1) {
        size_t pos = 0;
        auto rdRecLen = [&](const uint8_t* p) -> uint32_t {
            return bigEndian ? ByteIO::RdU32BE(p) : ByteIO::RdU32LE(p);
        };
        while (pos + 8u <= size) {
            const uint32_t rLen = rdRecLen(data + pos);
            if (pos + 8u + static_cast<uint64_t>(rLen) > size) return false;
            const uint8_t* pay = data + pos + 4u;
            const uint32_t trail = rdRecLen(pay + rLen);
            if (trail != rLen) return false;
            if (pos > hdrEndMarker && rLen >= (isDouble2001 ? 8u : 4u) && rLen != 16u) {
                const size_t step = isDouble2001 ? 8u : 4u;
                for (size_t off = 0; off + step <= rLen; off += step) {
                    uint8_t tmp[8] = {};
                    if (bigEndian) {
                        for (size_t b = 0; b < step; ++b) tmp[b] = pay[off + step - 1u - b];
                    } else {
                        std::memcpy(tmp, pay + off, step);
                    }
                    double v = 0.0;
                    if (isDouble2001) {
                        std::memcpy(&v, tmp, 8);
                    } else {
                        float f = 0.0f;
                        std::memcpy(&f, tmp, 4);
                        v = static_cast<double>(f);
                    }
                    if (v >= 0.99e30) { sawSweepTerminator = true; break; }
                    flatVals.push_back(v);
                }
                if (sawSweepTerminator) break;
            }
            pos += 8u + rLen;
        }
    } else {
        size_t p = hdrEndMarker + 4u;
        while (p < size && (data[p] == '\r' || data[p] == '\n' || data[p] == ' ')) ++p;
        for (size_t i = p; i < size; ++i) {
            if (data[i] == 0) return false;
        }
        while (p < size) {
            while (p < size && (data[p] == ' ' || data[p] == '\t' || data[p] == '\r' || data[p] == '\n')) ++p;
            if (p >= size) break;
            const size_t s = p;
            if (data[p] == '+' || data[p] == '-') ++p;
            bool sawDigit = false;
            while (p < size && ((data[p] >= '0' && data[p] <= '9') || data[p] == '.')) {
                if (data[p] >= '0' && data[p] <= '9') sawDigit = true;
                ++p;
            }
            if (!sawDigit) return false;
            if (p < size && (data[p] == 'E' || data[p] == 'e')) {
                ++p;
                if (p < size && (data[p] == '+' || data[p] == '-')) ++p;
                while (p < size && data[p] >= '0' && data[p] <= '9') ++p;
            }
            double v = 0.0;
            ReaderText::ParseDoubleSv(std::string_view(reinterpret_cast<const char*>(data + s), p - s), v);
            if (v >= 0.99e30) {
                sawSweepTerminator = true;
                break;
            }
            flatVals.push_back(v);
        }
    }

    const size_t nPoints = flatVals.size() / nVars;
    if (nPoints < 2u) return false;

    std::vector<SpiceWaveVar> vars(nVars);
    for (size_t vi = 0; vi < nVars; ++vi) {
        vars[vi].name = varNames[vi];
        vars[vi].values.resize(nPoints);
        for (size_t pi = 0; pi < nPoints; ++pi) {
            vars[vi].values[pi] = flatVals[pi * nVars + vi];
        }
    }

    const bool isAc = (ext == L".ac0") ||
                      ReaderText::AsciiEqCI(vars[0].name, "HERTZ") ||
                      ReaderText::AsciiEqCI(vars[0].name, "freq");
    const std::wstring kindTitle = (ext == L".ac0") ? L"HSPICE AC Sweep (.ac0)"
                                 : (ext == L".sw0") ? L"HSPICE DC Sweep (.sw0)"
                                                    : L"HSPICE Transient Waveform (.tr0)";
    std::vector<SpiceWaveVar> yVars(vars.begin() + 1, vars.end());
    RenderSpiceWaveformsToBoard(kindTitle, isAc, vars[0], yVars, out);
    return !out.traces.empty();
}

} // namespace Formats
