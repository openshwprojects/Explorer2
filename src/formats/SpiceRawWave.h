#pragma once
// SpiceRawWave.h — #23 SPICE Simulation Waveforms (.raw LTspice UTF-16LE/ASCII/Binary/FastAccess + ngspice, and HSPICE .tr0/.ac0/.sw0)

namespace Formats {

struct SpiceWaveVar {
    std::string name;
    std::string unit;
    std::vector<double> values; // Real value, or dB magnitude for complex AC traces
    std::vector<double> phaseDeg;
};

inline bool IsLikelySpiceRawHeaderBytes(const uint8_t* data, size_t size) {
    if (!data || size < 24) return false;
    // Check UTF-16LE BOM or UTF-16LE "Title:" / "Plotname:"
    if ((size >= 14 && data[0] == 0xFF && data[1] == 0xFE &&
         (data[2] == 'T' || data[2] == 'P' || data[2] == '*')) ||
        (size >= 14 && data[0] == 'T' && data[1] == 0 && data[2] == 'i' && data[3] == 0 && data[4] == 't' && data[5] == 0)) {
        return true;
    }
    size_t scan = std::min<size_t>(size, 512);
    std::string_view sv(reinterpret_cast<const char*>(data), scan);
    return sv.rfind("Title:", 0) == 0 ||
           sv.rfind("Plotname:", 0) == 0 ||
           (sv.find("Plotname:") != std::string_view::npos && sv.find("No. Variables:") != std::string_view::npos) ||
           (sv.find("Variables:") != std::string_view::npos && (sv.find("Values:") != std::string_view::npos || sv.find("Binary:") != std::string_view::npos));
}

inline void RenderSpiceWaveformsToBoard(
    const std::wstring& titleStr,
    bool isAcFreqLog,
    const SpiceWaveVar& xVar,
    const std::vector<SpiceWaveVar>& yVars,
    ParsedEdaBoard& out)
{
    out.kind = EdaDocKind::GerberLayer; // Dark oscilloscope background
    out.formatTitle = titleStr;

    const size_t nPts = xVar.values.size();
    const size_t nCh = std::min<size_t>(yVars.size(), 16);
    if (nPts < 2 || nCh == 0) return;

    double xMin = 1e300, xMax = -1e300;
    for (double xv : xVar.values) {
        double mx = isAcFreqLog ? std::log10(std::max(1e-12, std::fabs(xv))) : xv;
        if (std::isfinite(mx)) {
            xMin = std::min(xMin, mx);
            xMax = std::max(xMax, mx);
        }
    }
    if (!(xMax > xMin)) { xMax = xMin + 1.0; }

    const float kX0 = 24.0f;
    const float kX1 = 164.0f;
    const float kLaneH = 14.0f;
    const float kLaneGap = 3.5f;
    const float totalH = static_cast<float>(nCh) * (kLaneH + kLaneGap) + 6.0f;

    // Outer oscilloscope graticule frame & vertical time/freq division lines
    out.traces.push_back({ kX0, 0.0f,   kX1, 0.0f,   0.22f, EdaLayerId::EdgeCuts, 0 });
    out.traces.push_back({ kX1, 0.0f,   kX1, totalH, 0.22f, EdaLayerId::EdgeCuts, 0 });
    out.traces.push_back({ kX1, totalH, kX0, totalH, 0.22f, EdaLayerId::EdgeCuts, 0 });
    out.traces.push_back({ kX0, totalH, kX0, 0.0f,   0.22f, EdaLayerId::EdgeCuts, 0 });

    for (int gx = 1; gx < 10; ++gx) {
        float vx = kX0 + (kX1 - kX0) * (static_cast<float>(gx) / 10.0f);
        out.traces.push_back({ vx, 0.0f, vx, totalH, 0.08f, EdaLayerId::EdgeCuts, 0 });
    }

    wchar_t xAxisLbl[160] = {};
    swprintf_s(xAxisLbl, L"%hs: %.4g .. %.4g (%zu pts)",
               xVar.name.empty() ? (isAcFreqLog ? "frequency" : "time") : xVar.name.c_str(),
               xVar.values.front(), xVar.values.back(), nPts);
    out.labels.push_back({ kX0, totalH + 1.6f, 1.15f, EdaLayerId::TopSilk, xAxisLbl });

    const EdaLayerId kChanLayers[5] = {
        EdaLayerId::TopCopper,
        EdaLayerId::BottomCopper,
        EdaLayerId::SchWire,
        EdaLayerId::InnerCopper,
        EdaLayerId::TopSilk
    };

    // Downsample step if > 2048 points per channel
    const size_t stride = std::max<size_t>(1, nPts / 2048);

    for (size_t ci = 0; ci < nCh; ++ci) {
        const auto& ch = yVars[ci];
        if (ch.values.size() < 2) continue;
        const float laneTop = totalH - 3.0f - static_cast<float>(ci) * (kLaneH + kLaneGap);
        const float laneBot = laneTop - kLaneH;
        const float laneMid = (laneTop + laneBot) * 0.5f;

        // Zero/center reference line
        out.traces.push_back({ kX0, laneMid, kX1, laneMid, 0.08f, EdaLayerId::EdgeCuts, 0 });

        double vMin = 1e300, vMax = -1e300;
        for (double v : ch.values) {
            if (std::isfinite(v)) {
                vMin = std::min(vMin, v);
                vMax = std::max(vMax, v);
            }
        }
        if (vMin > vMax) { vMin = -1.0; vMax = 1.0; }
        double vSpan = vMax - vMin;
        if (vSpan < 1e-12) vSpan = 1.0;

        EdaLayerId lid = kChanLayers[ci % 5];
        int netId = static_cast<int>(ci + 1);

        auto mapPt = [&](size_t idx, float& ox, float& oy) {
            double xv = xVar.values[idx];
            double mx = isAcFreqLog ? std::log10(std::max(1e-12, std::fabs(xv))) : xv;
            double tx = std::clamp((mx - xMin) / (xMax - xMin), 0.0, 1.0);
            double ty = std::clamp((ch.values[idx] - vMin) / vSpan, 0.0, 1.0);
            if (std::fabs(vMax - vMin) < 1e-12) ty = 0.5;
            ox = kX0 + static_cast<float>(tx) * (kX1 - kX0);
            oy = laneBot + 1.0f + static_cast<float>(ty) * (kLaneH - 2.0f);
        };

        float px = 0.0f, py = 0.0f;
        size_t limit = std::min(nPts, ch.values.size());
        for (size_t i = 0; i < limit; i += stride) {
            float wx = 0.0f, wy = 0.0f;
            mapPt(i, wx, wy);
            if (i > 0 && out.traces.size() < 120000) {
                out.traces.push_back({ px, py, wx, wy, 0.28f, lid, netId });
            }
            px = wx; py = wy;
        }
        if ((limit - 1) % stride != 0 && out.traces.size() < 120000) {
            float wx = 0.0f, wy = 0.0f;
            mapPt(limit - 1, wx, wy);
            out.traces.push_back({ px, py, wx, wy, 0.28f, lid, netId });
        }

        wchar_t chLbl[160] = {};
        swprintf_s(chLbl, L"%hs [%.3g..%.3g%hs]",
                   ch.name.c_str(), vMin, vMax,
                   isAcFreqLog ? " dB" : (ch.unit.find("current") != std::string::npos ? " A" : " V"));
        out.labels.push_back({ 0.5f, laneMid - 0.4f, 0.92f, lid, chLbl });
    }

    out.netCount = nCh;
}

inline bool ParseLtspiceOrNgspiceRaw(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    if (!IsLikelySpiceRawHeaderBytes(data, size)) return false;

    // Decode header (ASCII/UTF-8 or UTF-16LE) up to the exact end of "Binary:\n" or "Values:\n"
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
        // Scan 16-bit code units line by line until "Binary:" or "Values:" line ends with '\n'
        std::string curLine;
        size_t p = startOff;
        while (p + 1 < size && headerAscii.size() < 2 * 1024 * 1024) {
            uint16_t cu = static_cast<uint16_t>(data[p]) | (static_cast<uint16_t>(data[p + 1]) << 8);
            p += 2;
            if (cu == 0) return false;
            char ch = (cu < 128) ? static_cast<char>(cu) : '?';
            headerAscii.push_back(ch);
            if (ch == '\n') {
                while (!curLine.empty() && (curLine.back() == '\r' || curLine.back() == ' ' || curLine.back() == '\t')) curLine.pop_back();
                if (_stricmp(curLine.c_str(), "Binary:") == 0) {
                    isBinaryPayload = true;
                    foundDataMarker = true;
                    payloadByteOff = p;
                    break;
                }
                if (_stricmp(curLine.c_str(), "Values:") == 0) {
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
            uint8_t b = data[p++];
            if (b == 0) return false; // Embedded NUL inside text header
            char ch = static_cast<char>(b);
            headerAscii.push_back(ch);
            if (ch == '\n') {
                while (!curLine.empty() && (curLine.back() == '\r' || curLine.back() == ' ' || curLine.back() == '\t')) curLine.pop_back();
                if (_stricmp(curLine.c_str(), "Binary:") == 0) {
                    isBinaryPayload = true;
                    foundDataMarker = true;
                    payloadByteOff = p;
                    break;
                }
                if (_stricmp(curLine.c_str(), "Values:") == 0) {
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

    // Parse header metadata & Variables table
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

        auto matchPrefixCI = [&](const char* pfx, std::string_view& outVal) -> bool {
            size_t plen = std::strlen(pfx);
            if (ln.size() < plen || _strnicmp(ln.data(), pfx, plen) != 0) return false;
            outVal = ln.substr(plen);
            while (!outVal.empty() && (outVal.front() == ' ' || outVal.front() == '\t')) outVal.remove_prefix(1);
            return true;
        };

        std::string_view val;
        if (_stricmp(std::string(ln).c_str(), "Binary:") == 0 || _stricmp(std::string(ln).c_str(), "Values:") == 0) {
            break;
        } else if (_stricmp(std::string(ln).c_str(), "Variables:") == 0) {
            inVarsSection = true;
        } else if (!inVarsSection && matchPrefixCI("Title:", val)) {
            titleLine = std::string(val);
        } else if (!inVarsSection && matchPrefixCI("Plotname:", val)) {
            plotName = std::string(val);
        } else if (!inVarsSection && matchPrefixCI("Flags:", val)) {
            flagsStr = std::string(val);
            for (char& c : flagsStr) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        } else if (!inVarsSection && matchPrefixCI("No. Variables:", val)) {
            numVars = static_cast<size_t>(std::strtoull(std::string(val).c_str(), nullptr, 10));
        } else if (!inVarsSection && matchPrefixCI("No. Points:", val)) {
            numPoints = static_cast<size_t>(std::strtoull(std::string(val).c_str(), nullptr, 10));
        } else if (inVarsSection) {
            // Format: <idx> <name> <type/unit>
            while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
            size_t s1 = ln.find_first_of(" \t");
            if (s1 != std::string_view::npos) {
                std::string_view rest = ln.substr(s1 + 1);
                while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t')) rest.remove_prefix(1);
                size_t s2 = rest.find_first_of(" \t");
                SpiceWaveVar v;
                v.name = std::string(s2 == std::string_view::npos ? rest : rest.substr(0, s2));
                if (s2 != std::string_view::npos) {
                    std::string_view u = rest.substr(s2 + 1);
                    while (!u.empty() && (u.front() == ' ' || u.front() == '\t')) u.remove_prefix(1);
                    v.unit = std::string(u);
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
    const bool isTimeSweep = (!vars[0].name.empty() && (_stricmp(vars[0].name.c_str(), "time") == 0 || vars[0].unit == "time"));

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
            // LTspice default real binary: 8-byte double sweep var + 4-byte float signal vars
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
            // ngspice / LTspice .options numdgt>6 all-double binary
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
            return false; // Truncated binary payload!
        }
    } else {
        // Values: ASCII / UTF-16LE text values
        std::string valText;
        if (isUtf16) {
            size_t nCu = (size - payloadByteOff) / 2;
            valText.reserve(nCu);
            for (size_t i = 0; i < nCu; ++i) {
                uint16_t cu = static_cast<uint16_t>(data[payloadByteOff + i * 2]) |
                              (static_cast<uint16_t>(data[payloadByteOff + i * 2 + 1]) << 8);
                if (cu == 0) return false;
                valText.push_back(cu < 128 ? static_cast<char>(cu) : ' ');
            }
        } else {
            for (size_t i = payloadByteOff; i < size; ++i) {
                if (data[i] == 0) return false;
            }
            valText.assign(reinterpret_cast<const char*>(data + payloadByteOff), size - payloadByteOff);
        }

        // Each point has: <pointIndex> <val0> <val1> ... <val_{numVars-1}>
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
            if (idxTok.empty()) return false; // Truncated ASCII Values!
            for (size_t vi = 0; vi < numVars; ++vi) {
                std::string_view vTok = nextTok();
                if (vTok.empty()) return false;
                size_t comma = vTok.find(',');
                if (comma != std::string_view::npos) {
                    char bRe[64] = {}, bIm[64] = {};
                    std::memcpy(bRe, vTok.data(), std::min<size_t>(comma, 63));
                    std::memcpy(bIm, vTok.data() + comma + 1, std::min<size_t>(vTok.size() - comma - 1, 63));
                    double re = std::strtod(bRe, nullptr);
                    double im = std::strtod(bIm, nullptr);
                    vars[vi].values[pi] = (vi == 0) ? std::hypot(re, im) : (20.0 * std::log10(std::max(1e-18, std::hypot(re, im))));
                } else {
                    char bVal[64] = {};
                    std::memcpy(bVal, vTok.data(), std::min<size_t>(vTok.size(), 63));
                    double dv = std::strtod(bVal, nullptr);
                    vars[vi].values[pi] = (vi == 0 && isTimeSweep) ? std::fabs(dv) : dv;
                }
            }
        }
    }

    const bool isAcLog = isComplex || (_stricmp(vars[0].name.c_str(), "frequency") == 0);
    std::vector<SpiceWaveVar> yVars(vars.begin() + 1, vars.end());
    std::wstring titleW = L"SPICE Raw Waveform";
    if (!plotName.empty()) titleW += L" — " + PathUtils::Utf8ToWide(plotName);
    else if (!titleLine.empty()) titleW += L" — " + PathUtils::Utf8ToWide(titleLine);

    RenderSpiceWaveformsToBoard(titleW, isAcLog, vars[0], yVars, out);
    return !out.traces.empty();
}

inline bool ParseHspiceTr0Ac0Sw0(const uint8_t* data, size_t size, const std::wstring& ext, ParsedEdaBoard& out) {
    if (!data || size < 32 || size > 64 * 1024 * 1024) return false;

    // Locate "$&%#" end-of-header sentinel (present in both HSPICE POST=1 binary and POST=2 ASCII!)
    std::string_view fullSv(reinterpret_cast<const char*>(data), size);
    size_t hdrEndMarker = fullSv.find("$&%#");
    if (hdrEndMarker == std::string_view::npos || hdrEndMarker > 256 * 1024) {
        return false;
    }

    // Check if binary Fortran unformatted records (first 4 bytes == 0x00000004 LE or BE)
    auto rdU32LE = [](const uint8_t* p) -> uint32_t {
        return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
               (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
    };
    auto rdU32BE = [](const uint8_t* p) -> uint32_t {
        return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
               (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
    };
    const bool isBinaryPost1 = (size >= 24 && (rdU32LE(data) == 4u || rdU32BE(data) == 4u));
    const bool bigEndian = isBinaryPost1 && (rdU32BE(data) == 4u);

    // Extract printable ASCII tokens from header region [0 .. hdrEndMarker)
    std::string hdrClean;
    hdrClean.reserve(hdrEndMarker);
    for (size_t i = 0; i < hdrEndMarker; ++i) {
        uint8_t c = data[i];
        if (c >= 32 && c < 127) hdrClean.push_back(static_cast<char>(c));
        else if (c == '\r' || c == '\n' || c == '\t') hdrClean.push_back(' ');
    }

    // In HSPICE header:
    // Line 1 (fixed 4-char integers): nauto(0..3), nprobe(4..7), nsweep(8..11), ... version "9601" or "2001"
    // Followed by date/title/sweep info, then integer type codes (1=time/freq, 2=voltage, 8=current, ...) and signal names right before $&%#!
    std::vector<std::string> tokens;
    {
        size_t p = 0;
        while (p < hdrClean.size()) {
            while (p < hdrClean.size() && hdrClean[p] == ' ') ++p;
            if (p >= hdrClean.size()) break;
            size_t s = p;
            while (p < hdrClean.size() && hdrClean[p] != ' ') ++p;
            tokens.emplace_back(hdrClean.data() + s, p - s);
        }
    }
    if (tokens.size() < 4) return false;

    const bool isDouble2001 = (hdrClean.find("2001") != std::string::npos);

    // Collect trailing identifier names right before $&%# (after the integer type-code list!)
    auto isAllDigits = [](const std::string& s) -> bool {
        if (s.empty()) return false;
        for (char c : s) if (c < '0' || c > '9') return false;
        return true;
    };
    std::vector<std::string> varNames;
    for (int i = static_cast<int>(tokens.size()) - 1; i >= 0; --i) {
        if (isAllDigits(tokens[static_cast<size_t>(i)])) break;
        varNames.push_back(tokens[static_cast<size_t>(i)]);
    }
    std::reverse(varNames.begin(), varNames.end());
    if (varNames.size() < 2 || varNames.size() > 1024) return false;
    const size_t nVars = varNames.size();

    std::vector<double> flatVals;
    bool sawSweepTerminator = false;

    if (isBinaryPost1) {
        // Walk Fortran records: [len:4][hdr4:4][len:4][payload:blockBytes][len:4]
        // Data records start at the first Fortran record after hdrEndMarker + 4
        size_t pos = 0;
        auto rdRecLen = [&](const uint8_t* p) -> uint32_t { return bigEndian ? rdU32BE(p) : rdU32LE(p); };
        while (pos + 8 <= size) {
            uint32_t rLen = rdRecLen(data + pos);
            if (pos + 8 + static_cast<uint64_t>(rLen) > size) return false; // Corrupt Fortran record!
            const uint8_t* pay = data + pos + 4;
            uint32_t trail = rdRecLen(pay + rLen);
            if (trail != rLen) return false;
            if (pos > hdrEndMarker && rLen >= (isDouble2001 ? 8u : 4u) && rLen != 16u) {
                size_t step = isDouble2001 ? 8 : 4;
                for (size_t off = 0; off + step <= rLen; off += step) {
                    uint8_t tmp[8] = {};
                    if (bigEndian) {
                        for (size_t b = 0; b < step; ++b) tmp[b] = pay[off + step - 1 - b];
                    } else {
                        std::memcpy(tmp, pay + off, step);
                    }
                    double v = 0.0;
                    if (isDouble2001) { std::memcpy(&v, tmp, 8); }
                    else { float f = 0.0f; std::memcpy(&f, tmp, 4); v = f; }
                    if (v >= 0.99e30) { sawSweepTerminator = true; break; }
                    flatVals.push_back(v);
                }
                if (sawSweepTerminator) break;
            }
            pos += 8 + rLen;
        }
    } else {
        // POST=2 ASCII floats after "$&%#" (either whitespace separated OR fixed-width 11-char floats like "0.10000E-08-0.2500E+00"!)
        size_t p = hdrEndMarker + 4;
        while (p < size && (data[p] == '\r' || data[p] == '\n' || data[p] == ' ')) ++p;
        for (size_t i = p; i < size; ++i) {
            if (data[i] == 0) return false;
        }
        while (p < size) {
            while (p < size && (data[p] == ' ' || data[p] == '\t' || data[p] == '\r' || data[p] == '\n')) ++p;
            if (p >= size) break;
            // Parse scientific float [+-]?digits(.digits)?([Ee][+-]?digits)?
            size_t s = p;
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
            char bNum[64] = {};
            std::memcpy(bNum, data + s, std::min<size_t>(p - s, 63));
            double v = std::strtod(bNum, nullptr);
            if (v >= 0.99e30) {
                sawSweepTerminator = true;
                break;
            }
            flatVals.push_back(v);
        }
    }

    const size_t nPoints = flatVals.size() / nVars;
    if (nPoints < 2) return false;

    std::vector<SpiceWaveVar> vars(nVars);
    for (size_t vi = 0; vi < nVars; ++vi) {
        vars[vi].name = varNames[vi];
        vars[vi].values.resize(nPoints);
        for (size_t pi = 0; pi < nPoints; ++pi) {
            vars[vi].values[pi] = flatVals[pi * nVars + vi];
        }
    }

    bool isAc = (ext == L".ac0") || (_stricmp(vars[0].name.c_str(), "HERTZ") == 0) || (_stricmp(vars[0].name.c_str(), "freq") == 0);
    std::wstring kindTitle = (ext == L".ac0") ? L"HSPICE AC Sweep (.ac0)"
                           : (ext == L".sw0") ? L"HSPICE DC Sweep (.sw0)"
                                              : L"HSPICE Transient Waveform (.tr0)";
    std::vector<SpiceWaveVar> yVars(vars.begin() + 1, vars.end());
    RenderSpiceWaveformsToBoard(kindTitle, isAc, vars[0], yVars, out);
    return !out.traces.empty();
}

inline bool ParseSpiceWaveformFile(const uint8_t* data, size_t size, const std::wstring& ext, ParsedEdaBoard& out) {
    if (ext == L".tr0" || ext == L".ac0" || ext == L".sw0") {
        if (ParseHspiceTr0Ac0Sw0(data, size, ext, out)) return true;
        return ParseLtspiceOrNgspiceRaw(data, size, out);
    }
    if (ParseLtspiceOrNgspiceRaw(data, size, out)) return true;
    return ParseHspiceTr0Ac0Sw0(data, size, ext, out);
}

} // namespace Formats
