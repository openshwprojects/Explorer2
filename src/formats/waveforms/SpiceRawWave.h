#pragma once
// SpiceRawWave.h — LTspice & ngspice Simulation Waveform (.raw) Reader
// Supports UTF-16LE and UTF-8/ASCII headers, Binary (mixed 64/32-bit, all-double, complex AC, FastAccess)
// and ASCII Values sections, rendering via WaveformPlotCommon.h.
// Synopsys HSPICE `.tr0`/`.ac0`/`.sw0` waveforms live in `HspiceBinaryTr0.h`.
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

inline bool IsLikelySpiceRawHeaderBytes(const uint8_t* data, size_t size) {
    if (!data || size < 24) return false;
    if ((size >= 14 && data[0] == 0xFF && data[1] == 0xFE &&
         (data[2] == 'T' || data[2] == 'P' || data[2] == '*')) ||
        (size >= 14 && data[0] == 'T' && data[1] == 0 && data[2] == 'i' && data[3] == 0 && data[4] == 't' && data[5] == 0)) {
        return true;
    }
    const size_t scan = std::min<size_t>(size, 512);
    std::string_view sv(reinterpret_cast<const char*>(data), scan);
    return sv.rfind("Title:", 0) == 0 ||
           sv.rfind("Plotname:", 0) == 0 ||
           (sv.find("Plotname:") != std::string_view::npos && sv.find("No. Variables:") != std::string_view::npos) ||
           (sv.find("Variables:") != std::string_view::npos && (sv.find("Values:") != std::string_view::npos || sv.find("Binary:") != std::string_view::npos));
}

inline bool ParseLtspiceOrNgspiceRaw(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    if (!IsLikelySpiceRawHeaderBytes(data, size)) return false;

    bool isUtf16 = false;
    size_t startOff = 0;
    if (size >= 2 && data[0] == 0xFF && data[1] == 0xFE) {
        isUtf16 = true;
        startOff = 2;
    } else if (size >= 12 && data[1] == 0 && data[3] == 0 && data[5] == 0) {
        isUtf16 = true;
    }

    std::string headerAscii;
    bool isBinaryPayload = false;
    bool foundDataMarker = false;
    size_t payloadByteOff = 0;

    if (isUtf16) {
        std::string curLine;
        size_t p = startOff;
        while (p + 1 < size && headerAscii.size() < 2 * 1024 * 1024) {
            const uint16_t cu = ByteIO::RdU16LE(data + p);
            p += 2;
            if (cu == 0) return false;
            const char ch = (cu < 128) ? static_cast<char>(cu) : '?';
            headerAscii.push_back(ch);
            if (ch == '\n') {
                while (!curLine.empty() && (curLine.back() == '\r' || curLine.back() == ' ' || curLine.back() == '\t')) curLine.pop_back();
                if (ReaderText::AsciiEqCI(curLine, "Binary:")) {
                    isBinaryPayload = true;
                    foundDataMarker = true;
                    payloadByteOff = p;
                    break;
                }
                if (ReaderText::AsciiEqCI(curLine, "Values:")) {
                    isBinaryPayload = false;
                    foundDataMarker = true;
                    payloadByteOff = p;
                    break;
                }
                curLine.clear();
            } else {
                curLine.push_back(ch);
            }
        }
    } else {
        if (size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) startOff = 3;
        std::string curLine;
        size_t p = startOff;
        while (p < size && headerAscii.size() < 2 * 1024 * 1024) {
            const uint8_t b = data[p++];
            if (b == 0) return false;
            const char ch = static_cast<char>(b);
            headerAscii.push_back(ch);
            if (ch == '\n') {
                while (!curLine.empty() && (curLine.back() == '\r' || curLine.back() == ' ' || curLine.back() == '\t')) curLine.pop_back();
                if (ReaderText::AsciiEqCI(curLine, "Binary:")) {
                    isBinaryPayload = true;
                    foundDataMarker = true;
                    payloadByteOff = p;
                    break;
                }
                if (ReaderText::AsciiEqCI(curLine, "Values:")) {
                    isBinaryPayload = false;
                    foundDataMarker = true;
                    payloadByteOff = p;
                    break;
                }
                curLine.clear();
            } else {
                curLine.push_back(ch);
            }
        }
    }

    if (!foundDataMarker || payloadByteOff >= size) return false;

    std::string plotName, titleLine, flagsStr;
    size_t numVars = 0, numPoints = 0;
    bool inVarsSection = false;
    std::vector<SpiceWaveVar> vars;

    size_t hp = 0;
    while (hp < headerAscii.size()) {
        size_t eol = headerAscii.find('\n', hp);
        if (eol == std::string::npos) eol = headerAscii.size();
        std::string_view ln(headerAscii.data() + hp, eol - hp);
        hp = eol + 1;
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ' || ln.back() == '\t')) ln.remove_suffix(1);
        if (ln.empty()) continue;

        auto matchPrefixCI = [&](std::string_view pfx, std::string_view& outVal) -> bool {
            if (!ReaderText::StartsWithCI(ln, pfx)) return false;
            outVal = ReaderText::TrimSv(ln.substr(pfx.size()));
            return true;
        };

        std::string_view val;
        if (ReaderText::AsciiEqCI(ln, "Binary:") || ReaderText::AsciiEqCI(ln, "Values:")) {
            break;
        } else if (ReaderText::AsciiEqCI(ln, "Variables:")) {
            inVarsSection = true;
        } else if (!inVarsSection && matchPrefixCI("Title:", val)) {
            titleLine = std::string(val);
        } else if (!inVarsSection && matchPrefixCI("Plotname:", val)) {
            plotName = std::string(val);
        } else if (!inVarsSection && matchPrefixCI("Flags:", val)) {
            flagsStr = ReaderText::ToLowerAscii(val);
        } else if (!inVarsSection && matchPrefixCI("No. Variables:", val)) {
            uint64_t nv = 0;
            if (ReaderText::ParseUint64Sv(val, nv, 10)) numVars = static_cast<size_t>(nv);
        } else if (!inVarsSection && matchPrefixCI("No. Points:", val)) {
            uint64_t np = 0;
            if (ReaderText::ParseUint64Sv(val, np, 10)) numPoints = static_cast<size_t>(np);
        } else if (inVarsSection) {
            ln = ReaderText::TrimSv(ln);
            size_t s1 = ln.find_first_of(" \t");
            if (s1 != std::string_view::npos) {
                std::string_view rest = ReaderText::TrimSv(ln.substr(s1 + 1));
                size_t s2 = rest.find_first_of(" \t");
                SpiceWaveVar v;
                v.name = std::string(s2 == std::string_view::npos ? rest : rest.substr(0, s2));
                if (s2 != std::string_view::npos) {
                    v.unit = std::string(ReaderText::TrimSv(rest.substr(s2 + 1)));
                }
                vars.push_back(std::move(v));
            }
        }
    }

    if (numVars < 2 || numVars > 4096 || numPoints < 2 || numPoints > 5000000 || vars.size() != numVars) {
        return false;
    }

    const bool isComplex = (flagsStr.find("complex") != std::string::npos);
    const bool isFastAccess = (flagsStr.find("fastaccess") != std::string::npos);
    const bool explicitDouble = (flagsStr.find("double") != std::string::npos);
    const bool isTimeSweep = (!vars[0].name.empty() && (ReaderText::AsciiEqCI(vars[0].name, "time") || vars[0].unit == "time"));

    for (auto& v : vars) v.values.resize(numPoints, 0.0);

    auto rdF64 = [](const uint8_t* p) -> double {
        double d = 0.0;
        std::memcpy(&d, p, 8);
        return d;
    };
    auto rdF32 = [](const uint8_t* p) -> double {
        float f = 0.0f;
        std::memcpy(&f, p, 4);
        return static_cast<double>(f);
    };

    if (isBinaryPayload) {
        const size_t rem = size - payloadByteOff;
        const uint8_t* bin = data + payloadByteOff;
        const uint64_t szComplex = static_cast<uint64_t>(numPoints) * static_cast<uint64_t>(numVars) * 16ULL;
        const uint64_t szAllDbl  = static_cast<uint64_t>(numPoints) * static_cast<uint64_t>(numVars) * 8ULL;
        const uint64_t szLtMixed = static_cast<uint64_t>(numPoints) * (8ULL + static_cast<uint64_t>(numVars - 1) * 4ULL);

        if (isComplex) {
            if (rem < szComplex) return false;
            if (isFastAccess) {
                for (size_t vi = 0; vi < numVars; ++vi) {
                    const uint8_t* col = bin + vi * numPoints * 16;
                    for (size_t pi = 0; pi < numPoints; ++pi) {
                        double re = rdF64(col + pi * 16);
                        double im = rdF64(col + pi * 16 + 8);
                        vars[vi].values[pi] = (vi == 0) ? std::hypot(re, im) : (20.0 * std::log10(std::max(1e-18, std::hypot(re, im))));
                    }
                }
            } else {
                const size_t rowStride = numVars * 16;
                for (size_t pi = 0; pi < numPoints; ++pi) {
                    const uint8_t* row = bin + pi * rowStride;
                    for (size_t vi = 0; vi < numVars; ++vi) {
                        double re = rdF64(row + vi * 16);
                        double im = rdF64(row + vi * 16 + 8);
                        vars[vi].values[pi] = (vi == 0) ? std::hypot(re, im) : (20.0 * std::log10(std::max(1e-18, std::hypot(re, im))));
                    }
                }
            }
        } else if (!explicitDouble && rem >= szLtMixed && rem < szAllDbl) {
            if (isFastAccess) {
                for (size_t pi = 0; pi < numPoints; ++pi) {
                    double tv = rdF64(bin + pi * 8);
                    vars[0].values[pi] = isTimeSweep ? std::fabs(tv) : tv;
                }
                const uint8_t* sigBase = bin + numPoints * 8;
                for (size_t vi = 1; vi < numVars; ++vi) {
                    const uint8_t* col = sigBase + (vi - 1) * numPoints * 4;
                    for (size_t pi = 0; pi < numPoints; ++pi) {
                        vars[vi].values[pi] = rdF32(col + pi * 4);
                    }
                }
            } else {
                const size_t rowStride = 8 + (numVars - 1) * 4;
                for (size_t pi = 0; pi < numPoints; ++pi) {
                    const uint8_t* row = bin + pi * rowStride;
                    double tv = rdF64(row);
                    vars[0].values[pi] = isTimeSweep ? std::fabs(tv) : tv;
                    for (size_t vi = 1; vi < numVars; ++vi) {
                        vars[vi].values[pi] = rdF32(row + 8 + (vi - 1) * 4);
                    }
                }
            }
        } else if (rem >= szAllDbl) {
            if (isFastAccess) {
                for (size_t vi = 0; vi < numVars; ++vi) {
                    const uint8_t* col = bin + vi * numPoints * 8;
                    for (size_t pi = 0; pi < numPoints; ++pi) {
                        double dv = rdF64(col + pi * 8);
                        vars[vi].values[pi] = (vi == 0 && isTimeSweep) ? std::fabs(dv) : dv;
                    }
                }
            } else {
                const size_t rowStride = numVars * 8;
                for (size_t pi = 0; pi < numPoints; ++pi) {
                    const uint8_t* row = bin + pi * rowStride;
                    for (size_t vi = 0; vi < numVars; ++vi) {
                        double dv = rdF64(row + vi * 8);
                        vars[vi].values[pi] = (vi == 0 && isTimeSweep) ? std::fabs(dv) : dv;
                    }
                }
            }
        } else {
            return false;
        }
    } else {
        std::string valText;
        if (isUtf16) {
            size_t nCu = (size - payloadByteOff) / 2;
            valText.reserve(nCu);
            for (size_t i = 0; i < nCu; ++i) {
                const uint16_t cu = ByteIO::RdU16LE(data + payloadByteOff + i * 2);
                if (cu == 0) return false;
                valText.push_back(cu < 128 ? static_cast<char>(cu) : ' ');
            }
        } else {
            for (size_t i = payloadByteOff; i < size; ++i) {
                if (data[i] == 0) return false;
            }
            valText.assign(reinterpret_cast<const char*>(data + payloadByteOff), size - payloadByteOff);
        }

        size_t tp = 0;
        auto nextTok = [&]() -> std::string_view {
            while (tp < valText.size() && static_cast<unsigned char>(valText[tp]) <= 0x20u) ++tp;
            if (tp >= valText.size()) return {};
            size_t s = tp;
            while (tp < valText.size() && static_cast<unsigned char>(valText[tp]) > 0x20u) ++tp;
            return std::string_view(valText.data() + s, tp - s);
        };

        for (size_t pi = 0; pi < numPoints; ++pi) {
            std::string_view idxTok = nextTok();
            if (idxTok.empty()) return false;
            for (size_t vi = 0; vi < numVars; ++vi) {
                std::string_view vTok = nextTok();
                if (vTok.empty()) return false;
                size_t comma = vTok.find(',');
                if (comma != std::string_view::npos) {
                    double re = 0.0, im = 0.0;
                    ReaderText::ParseDoubleSv(vTok.substr(0, comma), re);
                    ReaderText::ParseDoubleSv(vTok.substr(comma + 1), im);
                    vars[vi].values[pi] = (vi == 0) ? std::hypot(re, im) : (20.0 * std::log10(std::max(1e-18, std::hypot(re, im))));
                } else {
                    double dv = 0.0;
                    ReaderText::ParseDoubleSv(vTok, dv);
                    vars[vi].values[pi] = (vi == 0 && isTimeSweep) ? std::fabs(dv) : dv;
                }
            }
        }
    }

    const bool isAcLog = isComplex || ReaderText::AsciiEqCI(vars[0].name, "frequency");
    std::vector<SpiceWaveVar> yVars(vars.begin() + 1, vars.end());
    std::wstring titleW = L"SPICE Raw Waveform";
    if (!plotName.empty()) titleW += L" — " + PathUtils::Utf8ToWide(plotName);
    else if (!titleLine.empty()) titleW += L" — " + PathUtils::Utf8ToWide(titleLine);

    RenderSpiceWaveformsToBoard(titleW, isAcLog, vars[0], yVars, out);
    return !out.traces.empty();
}

} // namespace Formats
