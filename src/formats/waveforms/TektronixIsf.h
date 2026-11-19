#pragma once
// TektronixIsf.h — Tektronix ISF (Internal Save Format) Oscilloscope Waveform (.isf) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

inline bool ParseTektronixIsfBoard(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    if (!data || size < 16) return false;
        std::vector<float> samples;
        std::wstring fmtTitle;
        std::wstring metaLabel;

        // 3. Tektronix ISF (Internal Save Format: WFMPRE / BYT_NR + CURVE #...)
        {
            size_t hdrScan = std::min<size_t>(size, 4096);
            std::string upHdr(reinterpret_cast<const char*>(data), hdrScan);
            for (char& c : upHdr) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));

            bool hasIsfPre = (upHdr.find("BYT_N") != std::string::npos ||
                              upHdr.find(":WFMP") != std::string::npos ||
                              upHdr.find("WFMPRE") != std::string::npos);
            size_t curvePos = upHdr.find("CURVE");
            size_t kwLen = 5;
            if (curvePos == std::string::npos) { curvePos = upHdr.find("CURV"); kwLen = 4; }
            if (!hasIsfPre || curvePos == std::string::npos) return false;

            std::string pre = upHdr.substr(0, curvePos);
            auto parseTagF = [&](const char* tag, double defVal) -> double {
                size_t tp = pre.find(tag);
                if (tp == std::string::npos) return defVal;
                tp += std::strlen(tag);
                while (tp < pre.size() && (pre[tp] == ' ' || pre[tp] == '\t' || pre[tp] == ':')) ++tp;
                char* ep = nullptr;
                double v = std::strtod(pre.c_str() + tp, &ep);
                return (ep != pre.c_str() + tp && std::isfinite(v)) ? v : defVal;
            };

            int bytNr = static_cast<int>(parseTagF("BYT_NR", parseTagF("BYT_N", 1.0)));
            if (bytNr != 1 && bytNr != 2 && bytNr != 4) bytNr = 1;
            bool msbFirst = (pre.find("LSB") == std::string::npos); // MSB is Tektronix ISF default
            bool isUnsigned = (pre.find("BN_FMT RP") != std::string::npos || pre.find("BN_F RP") != std::string::npos);
            bool isFloat    = (pre.find("BN_FMT FP") != std::string::npos || pre.find("BN_F FP") != std::string::npos);
            double ymult = parseTagF("YMULT", parseTagF("YMU", 1.0));
            double yoff  = parseTagF("YOFF",  parseTagF("YOF", 0.0));
            double yzero = parseTagF("YZERO", parseTagF("YZE", 0.0));
            if (std::fabs(ymult) < 1e-18) ymult = 1.0;

            size_t cp = curvePos + kwLen;
            while (cp < size && (data[cp] == ' ' || data[cp] == '\t')) ++cp;
            if (cp >= size) return false;

            if (data[cp] == '#') {
                if (cp + 2 > size || data[cp + 1] < '0' || data[cp + 1] > '9') return false;
                int ndig = data[cp + 1] - '0';
                size_t sOff = 0, byteCnt = 0;
                if (ndig == 0) {
                    sOff = cp + 2;
                    byteCnt = size - sOff;
                } else {
                    if (cp + 2 + static_cast<size_t>(ndig) > size) return false;
                    for (int d = 0; d < ndig; ++d) {
                        uint8_t ch = data[cp + 2 + d];
                        if (ch < '0' || ch > '9') return false;
                        byteCnt = byteCnt * 10u + static_cast<size_t>(ch - '0');
                    }
                    sOff = cp + 2 + static_cast<size_t>(ndig);
                }
                if (sOff >= size) return false;
                size_t avail = std::min<size_t>(byteCnt, size - sOff);
                size_t totalPts = avail / static_cast<size_t>(bytNr);
                if (totalPts < 2) return false;
                size_t stepPts = std::max<size_t>(1, totalPts / 1024);
                for (size_t idx = 0; idx < totalPts && samples.size() < 1024; idx += stepPts) {
                    size_t o = sOff + idx * static_cast<size_t>(bytNr);
                    double raw = 0.0;
                    if (bytNr == 1) {
                        raw = isUnsigned ? static_cast<double>(data[o]) : static_cast<double>(static_cast<int8_t>(data[o]));
                    } else if (bytNr == 2) {
                        uint16_t u = msbFirst ? static_cast<uint16_t>((static_cast<uint16_t>(data[o]) << 8) | data[o + 1])
                                              : DetailEda200::RdU16LE(data + o);
                        raw = isUnsigned ? static_cast<double>(u) : static_cast<double>(static_cast<int16_t>(u));
                    } else {
                        uint32_t u = msbFirst ? ((static_cast<uint32_t>(data[o]) << 24) |
                                                 (static_cast<uint32_t>(data[o + 1]) << 16) |
                                                 (static_cast<uint32_t>(data[o + 2]) << 8) |
                                                 static_cast<uint32_t>(data[o + 3]))
                                              : DetailEda200::RdU32LE(data + o);
                        if (isFloat) {
                            float fv = 0.0f; std::memcpy(&fv, &u, 4);
                            raw = std::isfinite(fv) ? static_cast<double>(fv) : 0.0;
                        } else {
                            raw = isUnsigned ? static_cast<double>(u) : static_cast<double>(static_cast<int32_t>(u));
                        }
                    }
                    samples.push_back(static_cast<float>((raw - yoff) * ymult + yzero));
                }
            } else {
                // ASCII CURVE values
                std::string asc(reinterpret_cast<const char*>(data + cp), std::min<size_t>(size - cp, 65536));
                const char* ap = asc.c_str();
                while (*ap && samples.size() < 1024) {
                    while (*ap == ' ' || *ap == '\t' || *ap == ',' || *ap == '\r' || *ap == '\n') ++ap;
                    if (!*ap || *ap == ';') break;
                    char* ep = nullptr;
                    double raw = std::strtod(ap, &ep);
                    if (ep == ap) break;
                    if (std::isfinite(raw)) samples.push_back(static_cast<float>((raw - yoff) * ymult + yzero));
                    ap = ep;
                }
            }
            fmtTitle = L"Tektronix ISF Oscilloscope Trace (.isf)";
            metaLabel = L"Tek ISF (" + std::to_wstring(samples.size()) + L" pts)";
        }

        return DetailEda200::RenderSingleScopeTrace(out, samples, fmtTitle, metaLabel);
}

} // namespace Formats
