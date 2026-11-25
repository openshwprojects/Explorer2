#pragma once
// StataDta.h — Stata Dataset (.dta) Reader (113–120, <map> 64-bit offsets & <strls> GSO resolver)

namespace Formats {
namespace AiDataDetail {

// ============================================================================
// 12. Stata Dataset (.dta — Modern 117–120 <stata_dta> & Classic 113–115)
// ============================================================================
inline bool ParseStataDta(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    SourceBytes source(raw, out);
    const uint64_t fileSize = source.Size();
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());

    struct DtaCol {
        std::wstring name;
        std::wstring label;
        uint16_t typeCode = 0; // normalized to 117+ codes: 1..2045=strf, 32768=strL, 65526=double, 65527=float, 65528=int32, 65529=int16, 65530=int8
        size_t byteWidth = 0;
    };
    std::vector<DtaCol> cols;
    bool be = false;
    uint64_t nobs = 0;
    uint64_t dataStart = 0;
    uint64_t dataEnd = 0;
    uint64_t strlsStart = 0;
    uint64_t strlsEnd = 0;
    bool usedMapOffsets = false;
    std::wstring dsLabel;
    int releaseVer = 0;
    bool hasStrLCol = false;

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
        auto rdU64 = [&](const uint8_t* p) { return be ? RdU64BE(p) : RdU64LE(p); };

        uint32_t K = 0;
        if (!findTagSpan("<K>", "</K>", s, l)) return false;
        if (l == 2) K = be ? RdU16BE(raw.data() + s) : RdU16LE(raw.data() + s);
        else if (l == 4) K = be ? RdU32BE(raw.data() + s) : RdU32LE(raw.data() + s);
        if (K == 0 || K > 32768) return false;

        if (findTagSpan("<N>", "</N>", s, l)) {
            if (l == 4) nobs = be ? RdU32BE(raw.data() + s) : RdU32LE(raw.data() + s);
            else if (l == 8) nobs = rdU64(raw.data() + s);
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
            else if (tc == 32768) { cols[i].byteWidth = 8; hasStrLCol = true; } // strL (v, o) ref
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

        // Prefer 64-bit <map> section offsets when present (14 x uint64 in Stata 117-120)
        if (findTagSpan("<map>", "</map>", s, l) && l >= 12 * 8) {
            const uint64_t mapData  = rdU64(raw.data() + s + 9 * 8);
            const uint64_t mapStrls = rdU64(raw.data() + s + 10 * 8);
            const uint64_t mapVlbls = rdU64(raw.data() + s + 11 * 8);
            std::vector<uint8_t> tagChk;
            const uint8_t* dp = (mapData > 0 && mapData + 6 <= fileSize) ? source.Read(mapData, 6, tagChk) : nullptr;
            if (dp && std::memcmp(dp, "<data>", 6) == 0) {
                dataStart = mapData + 6;
                dataEnd = (mapStrls >= mapData + 13 && mapStrls <= fileSize) ? (mapStrls - 7) : fileSize;
                usedMapOffsets = true;
            }
            const uint8_t* sp = (mapStrls > 0 && mapStrls + 7 <= fileSize) ? source.Read(mapStrls, 7, tagChk) : nullptr;
            if (sp && std::memcmp(sp, "<strls>", 7) == 0) {
                strlsStart = mapStrls + 7;
                strlsEnd = (mapVlbls >= mapStrls + 15 && mapVlbls <= fileSize) ? (mapVlbls - 8) : fileSize;
            }
        }

        if (dataStart == 0 && dataEnd == 0) {
            if (findTagSpan("<data>", "</data>", s, l)) {
                dataStart = s;
                dataEnd = s + l;
            } else {
                const size_t d = sv.find("<data>");
                dataStart = (d == std::string_view::npos) ? 0 : d + 6;
                dataEnd = (d == std::string_view::npos) ? 0 : raw.size();
            }
        }
        if (strlsStart == 0 && strlsEnd == 0 && findTagSpan("<strls>", "</strls>", s, l)) {
            strlsStart = s;
            strlsEnd = s + l;
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

        size_t labelLen = 81;
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

        size_t fmtStride = (releaseVer == 113) ? 12 : 49;
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

    // Parse <strls> GSO records if any strL columns exist and <strls> span is non-empty
    std::map<std::pair<uint32_t, uint64_t>, std::wstring> strlMap;
    if (hasStrLCol && strlsStart < strlsEnd && strlsEnd <= fileSize) {
        const uint64_t strlSpan = std::min<uint64_t>(strlsEnd - strlsStart, 32ULL * 1024ULL * 1024ULL);
        std::vector<uint8_t> strlBuf;
        const uint8_t* sp = source.Read(strlsStart, static_cast<size_t>(strlSpan), strlBuf);
        if (sp) {
            const size_t gsoHdrLen = (releaseVer >= 118) ? 20u : 16u;
            size_t p = 0;
            while (p + gsoHdrLen <= strlSpan && strlMap.size() < 65536) {
                if (std::memcmp(sp + p, "GSO", 3) != 0) break;
                uint32_t gv = be ? RdU32BE(sp + p + 3) : RdU32LE(sp + p + 3);
                uint64_t go = 0;
                uint8_t  gt = 0;
                uint32_t glen = 0;
                if (releaseVer >= 118) {
                    go   = be ? RdU64BE(sp + p + 7) : RdU64LE(sp + p + 7);
                    gt   = sp[p + 15];
                    glen = be ? RdU32BE(sp + p + 16) : RdU32LE(sp + p + 16);
                } else {
                    go   = be ? RdU32BE(sp + p + 7) : RdU32LE(sp + p + 7);
                    gt   = sp[p + 11];
                    glen = be ? RdU32BE(sp + p + 12) : RdU32LE(sp + p + 12);
                }
                p += gsoHdrLen;
                if (glen > strlSpan - p) break;
                const uint8_t* valP = sp + p;
                size_t valN = glen;
                if (gt == 0x82u || DetailStructuredDataDoc::IsMostlyPrintableUtf8(valP, std::min<size_t>(valN, 256))) {
                    while (valN > 0 && valP[valN - 1] == 0) --valN;
                    size_t showN = std::min<size_t>(valN, 4096u);
                    std::wstring ws = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(valP), showN));
                    if (valN > showN) ws += L"...";
                    strlMap[{ gv, go }] = std::move(ws);
                } else {
                    strlMap[{ gv, go }] = DetailStructuredDataDoc::HexBytes(valP, valN, 24);
                }
                p += glen;
            }
        }
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

    if (rowWidth > 0 && rowWidth <= 4u * 1024u * 1024u && dataStart < dataEnd) {
        const size_t maxObs = static_cast<size_t>(std::min<uint64_t>(nobs, 500));
        const uint64_t availBytes = dataEnd - dataStart;
        const size_t rowsToRead = static_cast<size_t>(std::min<uint64_t>(maxObs, availBytes / rowWidth));
        const size_t totalReadBytes = rowsToRead * rowWidth;
        std::vector<uint8_t> obsBuf;
        const uint8_t* obsBase = (totalReadBytes > 0 && totalReadBytes <= 32u * 1024u * 1024u)
            ? source.Read(dataStart, totalReadBytes, obsBuf)
            : nullptr;

        if (obsBase) {
            for (size_t r = 0; r < rowsToRead; ++r) {
                std::vector<std::wstring> row(cols.size());
                size_t off = r * rowWidth;
                for (size_t c = 0; c < cols.size(); ++c) {
                    const uint8_t* p = obsBase + off;
                    uint16_t tc = cols[c].typeCode;
                    if (tc >= 1 && tc <= 2045) {
                        row[c] = CleanAsciiFixed(p, cols[c].byteWidth);
                    } else if (tc == 32768) { // strL (v, o) reference
                        uint32_t rv = 0;
                        uint64_t ro = 0;
                        if (releaseVer >= 118) {
                            if (!be) {
                                rv = RdU16LE(p);
                                ro = RdU64LE(p) >> 16;
                            } else {
                                rv = RdU16BE(p);
                                ro = RdU64BE(p) & 0x0000FFFFFFFFFFFFULL;
                            }
                        } else {
                            rv = be ? RdU32BE(p) : RdU32LE(p);
                            ro = be ? RdU32BE(p + 4) : RdU32LE(p + 4);
                        }
                        if (rv == 0 && ro == 0) {
                            row[c] = L"";
                        } else {
                            auto it = strlMap.find({ rv, ro });
                            if (it == strlMap.end() && releaseVer >= 118 && be) {
                                uint32_t altV = RdU16BE(p + 6);
                                uint64_t altO = RdU64BE(p) >> 16;
                                it = strlMap.find({ altV, altO });
                            }
                            if (it != strlMap.end()) {
                                row[c] = it->second;
                            } else {
                                row[c] = L"(strL " + std::to_wstring(rv) + L"," + std::to_wstring(ro) + L")";
                            }
                        }
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
                    }
                    off += cols[c].byteWidth;
                }
                dataSh.rows.push_back(std::move(row));
            }
        }
    }
    FinalizeSheetCols(dataSh);

    SpreadsheetSheet varSh;
    varSh.name = L"Stata_Variables";
    varSh.rows.push_back({ L"#", L"Name", L"Stata Type", L"Variable Label" });
    varSh.rows.push_back({ L"(meta)", L"Release " + std::to_wstring(releaseVer), L"Obs: " + std::to_wstring(nobs), dsLabel });
    for (size_t i = 0; i < cols.size(); ++i) {
        std::wstring tname = (cols[i].typeCode <= 2045) ? (L"str" + std::to_wstring(cols[i].byteWidth)) :
                             (cols[i].typeCode == 32768) ? L"strL" :
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
    if ((usedMapOffsets || source.ReadPastHead()) && !source.ReadFailed()) out.sourceReadFully = true;
    out.sheets.push_back(std::move(dataSh));
    out.sheets.push_back(std::move(varSh));
    out.valid = true;
    return true;
}

} // namespace AiDataDetail

inline bool ParseStataDta(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) { return AiDataDetail::ParseStataDta(raw, out); }

} // namespace Formats
