#pragma once
// StataDta.h — Stata Dataset (.dta) Reader

namespace Formats {
namespace AiDataDetail {

// ============================================================================
// 12. Stata Dataset (.dta — Modern 117–120 <stata_dta> & Classic 113–115)
// ============================================================================
inline bool ParseStataDta(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());

    struct DtaCol {
        std::wstring name;
        std::wstring label;
        uint16_t typeCode = 0; // normalized to 117+ codes: 1..2045=strf, 65526=double, 65527=float, 65528=int32, 65529=int16, 65530=int8
        size_t byteWidth = 0;
    };
    std::vector<DtaCol> cols;
    bool be = false;
    uint64_t nobs = 0;
    size_t dataStart = 0;
    size_t dataEnd = raw.size();
    std::wstring dsLabel;
    int releaseVer = 0;

    if (sv.size() >= 32 && sv.substr(0, 11) == "<stata_dta>") {
        auto findTagSpan = [&](std::string_view openTag, std::string_view closeTag, size_t& pStart, size_t& pLen) -> bool {
            size_t a = sv.find(openTag);
            if (a == std::string_view::npos) return false;
            a += openTag.size();
            size_t b = sv.find(closeTag, a);
            if (b == std::string_view::npos) return false;
            pStart = a;
            pLen = b - a;
            return true;
        };

        size_t s = 0, l = 0;
        if (!findTagSpan("<release>", "</release>", s, l)) return false;
        releaseVer = std::atoi(std::string(sv.substr(s, l)).c_str());
        if (releaseVer < 117 || releaseVer > 125) return false;

        if (findTagSpan("<byteorder>", "</byteorder>", s, l)) {
            be = (sv.substr(s, l) == "MSF");
        }

        uint32_t K = 0;
        if (!findTagSpan("<K>", "</K>", s, l)) return false;
        if (l == 2) K = be ? RdU16BE(raw.data() + s) : RdU16LE(raw.data() + s);
        else if (l == 4) K = be ? RdU32BE(raw.data() + s) : RdU32LE(raw.data() + s);
        if (K == 0 || K > 32768) return false;

        if (findTagSpan("<N>", "</N>", s, l)) {
            if (l == 4) nobs = be ? RdU32BE(raw.data() + s) : RdU32LE(raw.data() + s);
            else if (l == 8) nobs = be ? RdU64BE(raw.data() + s) : RdU64LE(raw.data() + s);
        }
        if (findTagSpan("<label>", "</label>", s, l) && l >= 2) {
            size_t hdrBytes = (releaseVer >= 118) ? 2 : 1;
            if (l >= hdrBytes) dsLabel = CleanAsciiFixed(raw.data() + s + hdrBytes, l - hdrBytes);
        }

        if (!findTagSpan("<variable_types>", "</variable_types>", s, l) || l < static_cast<size_t>(K) * 2) return false;
        cols.resize(K);
        for (uint32_t i = 0; i < K; ++i) {
            uint16_t tc = be ? RdU16BE(raw.data() + s + i * 2) : RdU16LE(raw.data() + s + i * 2);
            cols[i].typeCode = tc;
            if (tc >= 1 && tc <= 2045) cols[i].byteWidth = tc;
            else if (tc == 32768) cols[i].byteWidth = 8; // strL (v, o) ref
            else if (tc == 65526) cols[i].byteWidth = 8; // double
            else if (tc == 65527) cols[i].byteWidth = 4; // float
            else if (tc == 65528) cols[i].byteWidth = 4; // long (int32)
            else if (tc == 65529) cols[i].byteWidth = 2; // int (int16)
            else if (tc == 65530) cols[i].byteWidth = 1; // byte (int8)
            else return false;
        }

        if (findTagSpan("<varnames>", "</varnames>", s, l) && K > 0) {
            size_t stride = l / K;
            for (uint32_t i = 0; i < K && (i + 1) * stride <= l; ++i) {
                cols[i].name = CleanAsciiFixed(raw.data() + s + i * stride, stride);
            }
        }
        if (findTagSpan("<varlabels>", "</varlabels>", s, l) && K > 0) {
            size_t stride = l / K;
            for (uint32_t i = 0; i < K && (i + 1) * stride <= l; ++i) {
                cols[i].label = CleanAsciiFixed(raw.data() + s + i * stride, stride);
            }
        }
        if (findTagSpan("<data>", "</data>", s, l)) {
            dataStart = s;
            dataEnd = s + l;
        } else {
            // The bytes handed in end inside the observations (a file past 32 MB): the rows they hold are previewed;
            // without a <data> tag there are none
            const size_t d = sv.find("<data>");
            dataStart = (d == std::string_view::npos) ? 0 : d + 6;
            dataEnd = (d == std::string_view::npos) ? 0 : raw.size();
        }
    } else if (raw.size() >= 118 && (raw[0] == 113 || raw[0] == 114 || raw[0] == 115) &&
               (raw[1] == 1 || raw[1] == 2) && (raw[2] == 1 || raw[2] == 2))
    {
        // Classic Stata 113 / 114 / 115 binary format
        releaseVer = raw[0];
        be = (raw[1] == 1);
        uint16_t K = be ? RdU16BE(raw.data() + 4) : RdU16LE(raw.data() + 4);
        nobs = be ? RdU32BE(raw.data() + 6) : RdU32LE(raw.data() + 6);
        if (K == 0 || K > 32767) return false;

        size_t labelLen = (releaseVer == 113) ? 81 : 81;
        dsLabel = CleanAsciiFixed(raw.data() + 10, labelLen);
        size_t pos = 10 + labelLen + 18; // after timestamp (109B total!)

        if (pos + K > raw.size()) return false;
        cols.resize(K);
        for (uint16_t i = 0; i < K; ++i) {
            uint8_t b = raw[pos + i];
            if (b >= 1 && b <= 244) { cols[i].typeCode = b; cols[i].byteWidth = b; }
            else if (b == 251) { cols[i].typeCode = 65530; cols[i].byteWidth = 1; }
            else if (b == 252) { cols[i].typeCode = 65529; cols[i].byteWidth = 2; }
            else if (b == 253) { cols[i].typeCode = 65528; cols[i].byteWidth = 4; }
            else if (b == 254) { cols[i].typeCode = 65527; cols[i].byteWidth = 4; }
            else if (b == 255) { cols[i].typeCode = 65526; cols[i].byteWidth = 8; }
            else return false;
        }
        pos += K;

        size_t fmtStride = (releaseVer == 113) ? 12 : (releaseVer == 114 ? 49 : 49);
        size_t needDict = static_cast<size_t>(K) * 33 + (static_cast<size_t>(K) + 1) * 2 +
                          static_cast<size_t>(K) * fmtStride + static_cast<size_t>(K) * 33 +
                          static_cast<size_t>(K) * 81;
        if (needDict > raw.size() - pos) return false;

        for (uint16_t i = 0; i < K; ++i) cols[i].name = CleanAsciiFixed(raw.data() + pos + static_cast<size_t>(i) * 33, 33);
        pos += static_cast<size_t>(K) * 33;
        pos += (static_cast<size_t>(K) + 1) * 2; // srtlist
        pos += static_cast<size_t>(K) * fmtStride; // fmtlist
        pos += static_cast<size_t>(K) * 33; // lbllist
        for (uint16_t i = 0; i < K; ++i) cols[i].label = CleanAsciiFixed(raw.data() + pos + static_cast<size_t>(i) * 81, 81);
        pos += static_cast<size_t>(K) * 81;

        // Expansion fields: (byte ov, int32 len, bytes[len]) until ov == 0 && len == 0
        while (pos + 5 <= raw.size()) {
            uint8_t ov = raw[pos];
            uint32_t elen = be ? RdU32BE(raw.data() + pos + 1) : RdU32LE(raw.data() + pos + 1);
            pos += 5;
            if (ov == 0 && elen == 0) break;
            if (elen > raw.size() - pos) return false;
            pos += elen;
        }
        dataStart = pos;
        dataEnd = raw.size();
    } else {
        return false;
    }

    size_t rowWidth = 0;
    for (const auto& c : cols) rowWidth += c.byteWidth;

    SpreadsheetSheet dataSh;
    dataSh.name = L"Stata_Data";
    std::vector<std::wstring> hdr;
    for (size_t i = 0; i < cols.size(); ++i) {
        hdr.push_back(cols[i].name.empty() ? (L"var" + std::to_wstring(i + 1)) : cols[i].name);
    }
    dataSh.rows.push_back(std::move(hdr));

    if (rowWidth > 0 && dataStart < dataEnd) {
        size_t maxObs = static_cast<size_t>(std::min<uint64_t>(nobs, 500));
        size_t pos = dataStart;
        for (size_t r = 0; r < maxObs && pos + rowWidth <= dataEnd; ++r, pos += rowWidth) {
            std::vector<std::wstring> row(cols.size());
            size_t off = pos;
            for (size_t c = 0; c < cols.size(); ++c) {
                const uint8_t* p = raw.data() + off;
                uint16_t tc = cols[c].typeCode;
                if (tc >= 1 && tc <= 2045) {
                    row[c] = CleanAsciiFixed(p, cols[c].byteWidth);
                } else if (tc == 65530) { // byte (missing >= 101)
                    int8_t v = static_cast<int8_t>(p[0]);
                    row[c] = (v >= 101) ? L"." : std::to_wstring(v);
                } else if (tc == 65529) { // int16 (missing >= 32741)
                    int16_t v = be ? RdI16BE(p) : RdI16LE(p);
                    row[c] = (v >= 32741) ? L"." : std::to_wstring(v);
                } else if (tc == 65528) { // int32 (missing >= 2147483621)
                    int32_t v = be ? RdI32BE(p) : RdI32LE(p);
                    row[c] = (v >= 2147483621) ? L"." : std::to_wstring(v);
                } else if (tc == 65527) { // float
                    float v = be ? RdF32BE(p) : RdF32LE(p);
                    row[c] = (!std::isfinite(v) || v > 1.7e38f) ? L"." : FmtDouble(v);
                } else if (tc == 65526) { // double
                    double v = be ? RdF64BE(p) : RdF64LE(p);
                    row[c] = (!std::isfinite(v) || v > 8.9e307) ? L"." : FmtDouble(v);
                } else {
                    row[c] = L"(strL)";
                }
                off += cols[c].byteWidth;
            }
            dataSh.rows.push_back(std::move(row));
        }
    }
    FinalizeSheetCols(dataSh);

    SpreadsheetSheet varSh;
    varSh.name = L"Stata_Variables";
    varSh.rows.push_back({ L"#", L"Name", L"Stata Type", L"Variable Label" });
    varSh.rows.push_back({ L"(meta)", L"Release " + std::to_wstring(releaseVer), L"Obs: " + std::to_wstring(nobs), dsLabel });
    for (size_t i = 0; i < cols.size(); ++i) {
        std::wstring tname = (cols[i].typeCode <= 2045) ? (L"str" + std::to_wstring(cols[i].byteWidth)) :
                             (cols[i].typeCode == 65526) ? L"double" :
                             (cols[i].typeCode == 65527) ? L"float" :
                             (cols[i].typeCode == 65528) ? L"long" :
                             (cols[i].typeCode == 65529) ? L"int" : L"byte";
        varSh.rows.push_back({ std::to_wstring(i + 1), cols[i].name, tname, cols[i].label });
    }
    FinalizeSheetCols(varSh);

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Stata Statistical Dataset (.dta)";
    out.sheets.push_back(std::move(dataSh));
    out.sheets.push_back(std::move(varSh));
    out.valid = true;
    return true;
}

} // namespace AiDataDetail

inline bool ParseStataDta(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) { return AiDataDetail::ParseStataDta(raw, out); }

} // namespace Formats
