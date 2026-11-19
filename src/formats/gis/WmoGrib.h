#pragma once
// WmoGrib.h — WMO GRIB Edition 1 & 2 (.grib / .grib2 / .grb) Reader

namespace Formats {
namespace GeoSciDetail {

// ============================================================================
// 4. WMO GRIB Edition 1 & Edition 2 Meteorological Grids (.grib / .grib2 / .grb)
// ============================================================================
inline uint32_t ReadBitsBE(const uint8_t* buf, size_t bufBytes, size_t& bitPos, uint8_t nBits) {
    if (nBits == 0) return 0;
    uint32_t val = 0;
    for (uint8_t b = 0; b < nBits; ++b) {
        size_t byteIdx = bitPos >> 3;
        if (byteIdx >= bufBytes) return val;
        uint8_t bitVal = (buf[byteIdx] >> (7 - (bitPos & 7))) & 1u;
        val = (val << 1) | bitVal;
        ++bitPos;
    }
    return val;
}

inline std::wstring Grib2ParamName(uint8_t disc, uint8_t cat, uint8_t num) {
    if (disc == 0) {
        if (cat == 0 && num == 0) return L"Temperature (K)";
        if (cat == 0 && num == 2) return L"Potential Temperature (K)";
        if (cat == 1 && num == 0) return L"Specific Humidity (kg/kg)";
        if (cat == 1 && num == 1) return L"Relative Humidity (%)";
        if (cat == 1 && num == 8) return L"Total Precipitation (kg/m2)";
        if (cat == 2 && num == 2) return L"U-Component of Wind (m/s)";
        if (cat == 2 && num == 3) return L"V-Component of Wind (m/s)";
        if (cat == 3 && num == 0) return L"Pressure (Pa)";
        if (cat == 3 && num == 1) return L"Pressure Reduced to MSL (Pa)";
        if (cat == 3 && num == 5) return L"Geopotential Height (gpm)";
    }
    return L"D" + std::to_wstring(disc) + L":C" + std::to_wstring(cat) + L":P" + std::to_wstring(num);
}

inline bool ParseWmoGrib(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 16) return false;

    SpreadsheetSheet msgSh;
    msgSh.name = L"GRIB_Messages";
    msgSh.rows.push_back({
        L"Msg #", L"Edition", L"Reference Time (UTC)", L"Parameter",
        L"Grid (Ni x Nj)", L"Domain Bounds", L"Min", L"Max", L"Mean"
    });

    SpreadsheetSheet gridSh;
    gridSh.name = L"GRIB_Grid_Preview";

    size_t pos = 0;
    size_t msgCount = 0;
    while (pos + 16 <= raw.size() && msgCount < 256) {
        // Locate "GRIB" indicator
        if (std::memcmp(raw.data() + pos, "GRIB", 4) != 0) {
            bool foundNext = false;
            for (size_t scan = pos + 1; scan + 16 <= raw.size() && scan < pos + 4096; ++scan) {
                if (std::memcmp(raw.data() + scan, "GRIB", 4) == 0 && (raw[scan + 7] == 1 || raw[scan + 7] == 2)) {
                    pos = scan;
                    foundNext = true;
                    break;
                }
            }
            if (!foundNext) break;
        }

        uint8_t edition = raw[pos + 7];
        if (edition == 2) {
            uint8_t discipline = raw[pos + 6];
            uint64_t msgLen = RdU64BE(raw.data() + pos + 8);
            if (msgLen < 20 || msgLen > raw.size() - pos) break;
            size_t msgEnd = pos + static_cast<size_t>(msgLen);
            if (std::memcmp(raw.data() + msgEnd - 4, "7777", 4) != 0) break;

            uint16_t year = 0; uint8_t month = 0, day = 0, hour = 0, minute = 0;
            uint32_t ni = 0, nj = 0, numVals = 0;
            double la1 = 0.0, lo1 = 0.0, la2 = 0.0, lo2 = 0.0;
            bool hasBounds = false;
            uint8_t pCat = 255, pNum = 255;
            uint16_t reprTmpl = 0xFFFFu;
            double refR = 0.0;
            int16_t binE = 0, decD = 0;
            uint8_t bitsPerVal = 0;
            std::vector<double> unpacked;

            size_t spos = pos + 16;
            while (spos + 5 <= msgEnd - 4) {
                uint32_t secLen = RdU32BE(raw.data() + spos);
                uint8_t secNum  = raw[spos + 4];
                if (secLen < 5 || secLen > (msgEnd - 4) - spos) break;
                const uint8_t* sp = raw.data() + spos;

                if (secNum == 1 && secLen >= 19) {
                    year = RdU16BE(sp + 12); month = sp[14]; day = sp[15]; hour = sp[16]; minute = sp[17];
                } else if (secNum == 3 && secLen >= 14) {
                    numVals = RdU32BE(sp + 6);
                    uint16_t gtmpl = RdU16BE(sp + 12);
                    if (gtmpl == 0 && secLen >= 63) {
                        ni = RdU32BE(sp + 30);
                        nj = RdU32BE(sp + 34);
                        la1 = RdGribSignMag32(sp + 46) / 1e6;
                        lo1 = RdGribSignMag32(sp + 50) / 1e6;
                        la2 = RdGribSignMag32(sp + 55) / 1e6;
                        lo2 = RdGribSignMag32(sp + 59) / 1e6;
                        hasBounds = true;
                    }
                } else if (secNum == 4 && secLen >= 11) {
                    pCat = sp[9];
                    pNum = sp[10];
                } else if (secNum == 5 && secLen >= 11) {
                    numVals = RdU32BE(sp + 5);
                    reprTmpl = RdU16BE(sp + 9);
                    if (reprTmpl == 0 && secLen >= 20) {
                        refR = RdF32BE(sp + 11);
                        binE = RdGribSignMag16(sp + 15);
                        decD = RdGribSignMag16(sp + 17);
                        bitsPerVal = sp[19];
                    }
                } else if (secNum == 7 && reprTmpl == 0 && bitsPerVal <= 32) {
                    double scale2E = std::ldexp(1.0, binE);
                    double inv10D  = std::pow(10.0, -static_cast<double>(decD));
                    size_t cap = std::min<size_t>(numVals > 0 ? numVals : 1, 20000);
                    size_t bitPos = 0;
                    size_t avail = secLen - 5;
                    for (size_t i = 0; i < cap; ++i) {
                        if (bitsPerVal > 0 && (bitPos + bitsPerVal) > avail * 8) break;
                        uint32_t x = ReadBitsBE(sp + 5, avail, bitPos, bitsPerVal);
                        unpacked.push_back((refR + static_cast<double>(x) * scale2E) * inv10D);
                    }
                }
                spos += secLen;
            }

            ++msgCount;
            wchar_t tBuf[64] = {};
            swprintf_s(tBuf, L"%04u-%02u-%02u %02u:%02u", year, month, day, hour, minute);
            std::wstring bStr = hasBounds ? (FmtNum(la1, 4) + L"," + FmtNum(lo1, 4) + L" .. " + FmtNum(la2, 4) + L"," + FmtNum(lo2, 4)) : L"";
            std::wstring mnS, mxS, avS;
            if (!unpacked.empty()) {
                double mn = unpacked[0], mx = unpacked[0], sm = 0.0;
                for (double v : unpacked) { mn = std::min(mn, v); mx = std::max(mx, v); sm += v; }
                mnS = FmtNum(mn); mxS = FmtNum(mx); avS = FmtNum(sm / static_cast<double>(unpacked.size()));

                if (gridSh.rows.empty()) {
                    uint32_t cols = (ni > 0 && nj > 0) ? std::min<uint32_t>(ni, 32) : 1;
                    uint32_t rows = (ni > 0 && nj > 0) ? std::min<uint32_t>(nj, 128) : static_cast<uint32_t>(std::min<size_t>(unpacked.size(), 256));
                    if (cols > 1) {
                        std::vector<std::wstring> hr = { L"Lat \\ LonIdx" };
                        for (uint32_t c = 0; c < cols; ++c) hr.push_back(L"[" + std::to_wstring(c) + L"]");
                        gridSh.rows.push_back(std::move(hr));
                        for (uint32_t r = 0; r < rows; ++r) {
                            std::vector<std::wstring> rv = { L"Row " + std::to_wstring(r) };
                            for (uint32_t c = 0; c < cols; ++c) {
                                size_t idx = static_cast<size_t>(r) * ni + c;
                                rv.push_back(idx < unpacked.size() ? FmtNum(unpacked[idx]) : L"");
                            }
                            gridSh.rows.push_back(std::move(rv));
                        }
                    } else {
                        gridSh.rows.push_back({ L"Grid Point #", Grib2ParamName(discipline, pCat, pNum) });
                        for (size_t i = 0; i < std::min<size_t>(unpacked.size(), 256); ++i) {
                            gridSh.rows.push_back({ std::to_wstring(i), FmtNum(unpacked[i]) });
                        }
                    }
                    FinalizeCols(gridSh);
                }
            }
            msgSh.rows.push_back({
                std::to_wstring(msgCount), L"GRIB2", tBuf, Grib2ParamName(discipline, pCat, pNum),
                (ni > 0 && nj > 0) ? (std::to_wstring(ni) + L"x" + std::to_wstring(nj)) : std::to_wstring(numVals),
                bStr, mnS, mxS, avS
            });
            pos = msgEnd;
        } else if (edition == 1) {
            uint32_t msgLen = RdU24BE(raw.data() + pos + 4);
            if (msgLen < 40 || msgLen > raw.size() - pos) break;
            size_t msgEnd = pos + msgLen;
            if (std::memcmp(raw.data() + msgEnd - 4, "7777", 4) != 0) break;

            size_t spos = pos + 8;
            if (spos + 28 > msgEnd - 4) break;
            uint32_t pdsLen = RdU24BE(raw.data() + spos);
            if (pdsLen < 28 || pdsLen > (msgEnd - 4) - spos) break;
            const uint8_t* pds = raw.data() + spos;
            uint8_t flags = pds[7];
            uint8_t paramId = pds[8];
            uint16_t year = static_cast<uint16_t>((pds[24] > 0 ? (pds[24] - 1) * 100 : 1900) + pds[12]);
            uint8_t month = pds[13], day = pds[14], hour = pds[15], minute = pds[16];
            int16_t decD = RdGribSignMag16(pds + 26);
            spos += pdsLen;

            uint16_t ni = 0, nj = 0;
            if ((flags & 0x80u) != 0 && spos + 16 <= msgEnd - 4) { // GDS
                uint32_t gdsLen = RdU24BE(raw.data() + spos);
                if (gdsLen < 6 || gdsLen > (msgEnd - 4) - spos) break;
                if (gdsLen >= 10) {
                    ni = RdU16BE(raw.data() + spos + 6);
                    nj = RdU16BE(raw.data() + spos + 8);
                }
                spos += gdsLen;
            }
            if ((flags & 0x40u) != 0 && spos + 6 <= msgEnd - 4) { // BMS
                uint32_t bmsLen = RdU24BE(raw.data() + spos);
                if (bmsLen < 6 || bmsLen > (msgEnd - 4) - spos) break;
                spos += bmsLen;
            }

            std::vector<double> unpacked;
            if (spos + 11 <= msgEnd - 4) { // BDS
                uint32_t bdsLen = RdU24BE(raw.data() + spos);
                if (bdsLen >= 11 && bdsLen <= (msgEnd - 4) - spos) {
                    const uint8_t* bds = raw.data() + spos;
                    int16_t binE = RdGribSignMag16(bds + 4);
                    double refR  = RdIbmFloat32(bds + 6);
                    uint8_t bpv  = bds[10];
                    double scale2E = std::ldexp(1.0, binE);
                    double inv10D  = std::pow(10.0, -static_cast<double>(decD));
                    size_t avail = bdsLen - 11;
                    size_t nPts = (ni > 0 && nj > 0) ? std::min<size_t>(static_cast<size_t>(ni) * nj, 20000)
                                                     : (bpv > 0 ? std::min<size_t>((avail * 8) / bpv, 20000) : 1);
                    size_t bitPos = 0;
                    for (size_t i = 0; i < nPts && bpv <= 32; ++i) {
                        if (bpv > 0 && bitPos + bpv > avail * 8) break;
                        uint32_t x = ReadBitsBE(bds + 11, avail, bitPos, bpv);
                        unpacked.push_back((refR + static_cast<double>(x) * scale2E) * inv10D);
                    }
                }
            }

            ++msgCount;
            wchar_t tBuf[64] = {};
            swprintf_s(tBuf, L"%04u-%02u-%02u %02u:%02u", year, month, day, hour, minute);
            std::wstring mnS, mxS, avS;
            if (!unpacked.empty()) {
                double mn = unpacked[0], mx = unpacked[0], sm = 0.0;
                for (double v : unpacked) { mn = std::min(mn, v); mx = std::max(mx, v); sm += v; }
                mnS = FmtNum(mn); mxS = FmtNum(mx); avS = FmtNum(sm / static_cast<double>(unpacked.size()));
            }
            msgSh.rows.push_back({
                std::to_wstring(msgCount), L"GRIB1", tBuf, L"Param #" + std::to_wstring(paramId),
                (ni > 0 && nj > 0) ? (std::to_wstring(ni) + L"x" + std::to_wstring(nj)) : L"",
                L"", mnS, mxS, avS
            });
            pos = msgEnd;
        } else {
            break;
        }
    }

    if (msgCount == 0) return false;
    FinalizeCols(msgSh);

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"WMO GRIB Meteorological Forecast Grid";
    out.sheets.push_back(std::move(msgSh));
    if (!gridSh.rows.empty()) out.sheets.push_back(std::move(gridSh));
    out.valid = true;
    return true;
}

} // namespace GeoSciDetail

inline bool ParseWmoGribDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) { return GeoSciDetail::ParseWmoGrib(raw, out); }

} // namespace Formats
