#pragma once
// NetCdfData.h — NetCDF Classic & NetCDF-4 (.nc / .cdf) Reader

namespace Formats {
namespace GeoSciDetail {

// ============================================================================
// 3. NetCDF Classic (CDF\x01, CDF\x02, CDF\x05) & NetCDF-4 (HDF5)
// ============================================================================
struct NcReader {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t pos = 0;
    uint8_t ver = 1; // 1=32bit, 2=64bit-offset, 5=64bit-data

    bool RdU32(uint32_t& v) {
        if (pos + 4 > size) return false;
        v = RdU32BE(data + pos);
        pos += 4;
        return true;
    }
    bool RdNonNeg(uint64_t& v) {
        if (ver == 5) {
            if (pos + 8 > size) return false;
            v = RdU64BE(data + pos);
            pos += 8;
            return true;
        }
        uint32_t u = 0;
        if (!RdU32(u)) return false;
        v = u;
        return true;
    }
    bool RdOffset(uint64_t& v) {
        if (ver == 2 || ver == 5) {
            if (pos + 8 > size) return false;
            v = RdU64BE(data + pos);
            pos += 8;
            return true;
        }
        uint32_t u = 0;
        if (!RdU32(u)) return false;
        v = u;
        return true;
    }
    bool RdName(std::string& out) {
        uint64_t n = 0;
        if (!RdNonNeg(n) || n > size - pos || n > 4096) return false;
        out.assign(reinterpret_cast<const char*>(data + pos), static_cast<size_t>(n));
        size_t padded = (static_cast<size_t>(n) + 3u) & ~size_t(3u);
        if (padded > size - pos) return false;
        pos += padded;
        return true;
    }
};

inline size_t NcTypeSize(uint32_t ncType) {
    switch (ncType) {
        case 1: case 2: case 7: return 1; // BYTE, CHAR, UBYTE
        case 3: case 8:         return 2; // SHORT, USHORT
        case 4: case 5: case 9: return 4; // INT, FLOAT, UINT
        case 6: case 10: case 11: return 8; // DOUBLE, INT64, UINT64
        default: return 0;
    }
}

inline const wchar_t* NcTypeName(uint32_t ncType) {
    switch (ncType) {
        case 1: return L"byte";   case 2: return L"char";   case 3: return L"short";
        case 4: return L"int";    case 5: return L"float";  case 6: return L"double";
        case 7: return L"ubyte";  case 8: return L"ushort"; case 9: return L"uint";
        case 10: return L"int64"; case 11: return L"uint64";
        default: return L"unknown";
    }
}

inline bool ReadNcAttrList(NcReader& r, std::vector<std::pair<std::string, std::wstring>>& outAttrs) {
    uint32_t tag = 0;
    uint64_t nelems = 0;
    if (!r.RdU32(tag) || !r.RdNonNeg(nelems)) return false;
    if (tag == 0 && nelems == 0) return true;
    if (tag != 0x0000000Cu || nelems > 4096) return false;

    for (uint64_t i = 0; i < nelems; ++i) {
        std::string name;
        uint32_t ncType = 0;
        uint64_t nvals = 0;
        if (!r.RdName(name) || !r.RdU32(ncType) || !r.RdNonNeg(nvals)) return false;
        size_t esz = NcTypeSize(ncType);
        if (esz == 0 || nvals > (r.size - r.pos) / esz) return false;
        size_t rawBytes = static_cast<size_t>(nvals) * esz;
        size_t padded = (rawBytes + 3u) & ~size_t(3u);
        if (padded > r.size - r.pos) return false;

        const uint8_t* vp = r.data + r.pos;
        std::wstring valStr;
        if (ncType == 2) {
            std::string s(reinterpret_cast<const char*>(vp), rawBytes);
            while (!s.empty() && (s.back() == '\0' || s.back() == ' ')) s.pop_back();
            valStr = PathUtils::Utf8ToWide(s);
        } else {
            for (uint64_t k = 0; k < std::min<uint64_t>(nvals, 6); ++k) {
                if (k > 0) valStr += L", ";
                const uint8_t* p = vp + static_cast<size_t>(k) * esz;
                if (ncType == 1) valStr += std::to_wstring(static_cast<int8_t>(p[0]));
                else if (ncType == 3) valStr += std::to_wstring(RdI16BE(p));
                else if (ncType == 4) valStr += std::to_wstring(RdI32BE(p));
                else if (ncType == 5) valStr += FmtNum(RdF32BE(p));
                else if (ncType == 6) valStr += FmtNum(RdF64BE(p));
                else valStr += std::to_wstring(p[0]);
            }
        }
        outAttrs.emplace_back(std::move(name), std::move(valStr));
        r.pos += padded;
    }
    return true;
}

inline bool ParseNetCdf(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    // NetCDF-4 uses HDF5 container
    if ((raw.size() >= 36 && std::memcmp(raw.data(), "\x89HDF\r\n\x1A\n", 8) == 0) ||
        (raw.size() >= 548 && std::memcmp(raw.data() + 512, "\x89HDF\r\n\x1A\n", 8) == 0))
    {
        return ParseHdf5Doc(raw, extLower, out);
    }

    if (raw.size() < 16 || std::memcmp(raw.data(), "CDF", 3) != 0) return false;
    uint8_t ver = raw[3];
    if (ver != 1 && ver != 2 && ver != 5) return false;

    NcReader r{ raw.data(), raw.size(), 4, ver };
    uint64_t numRecs = 0;
    if (!r.RdNonNeg(numRecs)) return false;
    if (numRecs == 0xFFFFFFFFULL || numRecs == UINT64_MAX) numRecs = 0;

    // 1. Dimensions
    struct NcDim { std::string name; uint64_t len = 0; bool isUnlimited = false; };
    std::vector<NcDim> dims;
    int recDimId = -1;
    {
        uint32_t tag = 0; uint64_t nd = 0;
        if (!r.RdU32(tag) || !r.RdNonNeg(nd)) return false;
        if (tag == 0x0000000Au) {
            if (nd > 4096) return false;
            for (uint64_t i = 0; i < nd; ++i) {
                NcDim d;
                if (!r.RdName(d.name) || !r.RdNonNeg(d.len)) return false;
                if (d.len == 0) { d.isUnlimited = true; d.len = numRecs; recDimId = static_cast<int>(i); }
                dims.push_back(std::move(d));
            }
        } else if (tag != 0 || nd != 0) {
            return false;
        }
    }

    // 2. Global Attributes
    std::vector<std::pair<std::string, std::wstring>> globalAttrs;
    if (!ReadNcAttrList(r, globalAttrs)) return false;

    // 3. Variables
    struct NcVar {
        std::string name;
        std::vector<uint64_t> dimIds;
        std::vector<std::pair<std::string, std::wstring>> attrs;
        uint32_t ncType = 0;
        uint64_t vsize = 0;
        uint64_t begin = 0;
        bool isRecordVar = false;
    };
    std::vector<NcVar> vars;
    {
        uint32_t tag = 0; uint64_t nv = 0;
        if (!r.RdU32(tag) || !r.RdNonNeg(nv)) return false;
        if (tag == 0x0000000Bu) {
            if (nv > 4096) return false;
            for (uint64_t i = 0; i < nv; ++i) {
                NcVar v;
                uint64_t vndims = 0;
                if (!r.RdName(v.name) || !r.RdNonNeg(vndims) || vndims > 64) return false;
                for (uint64_t d = 0; d < vndims; ++d) {
                    uint64_t did = 0;
                    if (!r.RdNonNeg(did)) return false;
                    v.dimIds.push_back(did);
                }
                if (!v.dimIds.empty() && recDimId >= 0 && v.dimIds[0] == static_cast<uint64_t>(recDimId)) {
                    v.isRecordVar = true;
                }
                if (!ReadNcAttrList(r, v.attrs)) return false;
                if (!r.RdU32(v.ncType) || !r.RdNonNeg(v.vsize) || !r.RdOffset(v.begin)) return false;
                if (NcTypeSize(v.ncType) == 0) return false;
                vars.push_back(std::move(v));
            }
        } else if (tag != 0 || nv != 0) {
            return false;
        }
    }

    uint64_t recSlabSize = 0;
    for (const auto& v : vars) {
        if (v.isRecordVar) recSlabSize += v.vsize;
    }

    auto readVarDoubles = [&](const NcVar& v, size_t maxCount, std::vector<double>& outVals) {
        outVals.clear();
        size_t esz = NcTypeSize(v.ncType);
        if (esz == 0 || v.ncType == 2) return;
        uint64_t elemCnt = 1;
        for (uint64_t did : v.dimIds) {
            if (did < dims.size()) elemCnt *= dims[static_cast<size_t>(did)].len;
        }
        size_t want = static_cast<size_t>(std::min<uint64_t>(elemCnt, maxCount));
        if (want == 0) return;

        auto rdOne = [&](size_t byteOff) -> double {
            const uint8_t* p = raw.data() + byteOff;
            switch (v.ncType) {
                case 1:  return static_cast<int8_t>(p[0]);
                case 3:  return RdI16BE(p);
                case 4:  return RdI32BE(p);
                case 5:  return RdF32BE(p);
                case 6:  return RdF64BE(p);
                case 7:  return p[0];
                case 8:  return RdU16BE(p);
                case 9:  return RdU32BE(p);
                case 10: return static_cast<double>(RdI64BE(p));
                case 11: return static_cast<double>(RdU64BE(p));
                default: return 0.0;
            }
        };

        if (!v.isRecordVar) {
            for (size_t i = 0; i < want && v.begin + (i + 1) * esz <= raw.size(); ++i) {
                outVals.push_back(rdOne(static_cast<size_t>(v.begin) + i * esz));
            }
        } else {
            uint64_t perRec = 1;
            for (size_t d = 1; d < v.dimIds.size(); ++d) {
                if (v.dimIds[d] < dims.size()) perRec *= dims[static_cast<size_t>(v.dimIds[d])].len;
            }
            for (uint64_t rec = 0; rec < numRecs && outVals.size() < want; ++rec) {
                uint64_t recBase = v.begin + rec * recSlabSize;
                for (uint64_t j = 0; j < perRec && outVals.size() < want; ++j) {
                    uint64_t boff = recBase + j * esz;
                    if (boff + esz > raw.size()) return;
                    outVals.push_back(rdOne(static_cast<size_t>(boff)));
                }
            }
        }
    };

    SpreadsheetSheet schemaSh;
    schemaSh.name = L"NetCDF_Variables";
    schemaSh.rows.push_back({ L"Variable / Attribute", L"Type", L"Dimensions", L"Units / Value", L"Statistics / Preview" });
    schemaSh.rows.push_back({ L"(format)", ver == 5 ? L"CDF-5 (64-bit data)" : (ver == 2 ? L"CDF-2 (64-bit offset)" : L"CDF-1 (Classic)"),
        L"numrecs=" + std::to_wstring(numRecs), L"dims=" + std::to_wstring(dims.size()), L"vars=" + std::to_wstring(vars.size()) });

    for (const auto& ga : globalAttrs) {
        schemaSh.rows.push_back({ L"@" + PathUtils::Utf8ToWide(ga.first), L"Global Attr", L"", ga.second, L"" });
    }

    int latVarIdx = -1, lonVarIdx = -1;
    std::vector<std::vector<double>> varSamples(vars.size());

    for (size_t i = 0; i < vars.size(); ++i) {
        const auto& v = vars[i];
        std::string low = PathUtils::ToLower(PathUtils::Utf8ToWide(v.name)) == L"lat" || PathUtils::ToLower(PathUtils::Utf8ToWide(v.name)) == L"latitude" ? "lat" :
                          (PathUtils::ToLower(PathUtils::Utf8ToWide(v.name)) == L"lon" || PathUtils::ToLower(PathUtils::Utf8ToWide(v.name)) == L"longitude" ? "lon" : "");
        if (low == "lat") latVarIdx = static_cast<int>(i);
        if (low == "lon") lonVarIdx = static_cast<int>(i);

        std::wstring dstr = L"(";
        for (size_t d = 0; d < v.dimIds.size(); ++d) {
            if (d > 0) dstr += L", ";
            uint64_t did = v.dimIds[d];
            if (did < dims.size()) dstr += PathUtils::Utf8ToWide(dims[static_cast<size_t>(did)].name) + L"=" + std::to_wstring(dims[static_cast<size_t>(did)].len);
            else dstr += std::to_wstring(did);
        }
        dstr += L")";

        std::wstring units;
        for (const auto& va : v.attrs) {
            if (va.first == "units") units = va.second;
            else if (va.first == "long_name" && units.empty()) units = va.second;
        }

        readVarDoubles(v, 512, varSamples[i]);
        std::wstring statStr;
        if (!varSamples[i].empty()) {
            double mn = varSamples[i][0], mx = varSamples[i][0];
            for (double dv : varSamples[i]) {
                if (std::isfinite(dv)) { mn = std::min(mn, dv); mx = std::max(mx, dv); }
            }
            statStr = L"min=" + FmtNum(mn) + L", max=" + FmtNum(mx) + L" [e.g. " + FmtNum(varSamples[i][0]) + L"]";
        }
        schemaSh.rows.push_back({ PathUtils::Utf8ToWide(v.name), NcTypeName(v.ncType), dstr, units, statStr });
    }
    FinalizeCols(schemaSh);

    // Populate geoPoints if lat & lon arrays exist
    if (latVarIdx >= 0 && lonVarIdx >= 0) {
        const auto& lats = varSamples[static_cast<size_t>(latVarIdx)];
        const auto& lons = varSamples[static_cast<size_t>(lonVarIdx)];
        if (lats.size() == lons.size()) {
            for (size_t i = 0; i < lats.size() && out.geoPoints.size() < 5000; ++i) {
                double lonNorm = (lons[i] > 180.0 && lons[i] <= 360.0) ? (lons[i] - 360.0) : lons[i];
                if (std::fabs(lats[i]) <= 90.0 && std::fabs(lonNorm) <= 180.0) {
                    GpsTrackPoint gp; gp.lat = lats[i]; gp.lon = lonNorm; gp.rowIndex = static_cast<int>(i + 1);
                    out.geoPoints.push_back(std::move(gp));
                }
            }
        }
    }

    SpreadsheetSheet dataSh;
    dataSh.name = L"NetCDF_Data_Preview";
    std::vector<std::wstring> dhdr = { L"Index" };
    size_t maxLen = 0;
    for (size_t i = 0; i < vars.size() && dhdr.size() <= 32; ++i) {
        if (!varSamples[i].empty()) {
            dhdr.push_back(PathUtils::Utf8ToWide(vars[i].name));
            maxLen = std::max(maxLen, varSamples[i].size());
        }
    }
    if (maxLen > 0) {
        dataSh.rows.push_back(std::move(dhdr));
        for (size_t idx = 0; idx < std::min<size_t>(maxLen, 500); ++idx) {
            std::vector<std::wstring> row = { std::to_wstring(idx) };
            for (size_t i = 0; i < vars.size() && row.size() <= 32; ++i) {
                if (varSamples[i].empty()) continue;
                row.push_back(idx < varSamples[i].size() ? FmtNum(varSamples[i][idx]) : L"");
            }
            dataSh.rows.push_back(std::move(row));
        }
        FinalizeCols(dataSh);
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"NetCDF Scientific Array Dataset (" + extLower + L")";
    out.sheets.push_back(std::move(schemaSh));
    if (dataSh.rows.size() > 1) out.sheets.push_back(std::move(dataSh));
    out.valid = true;
    return true;
}

} // namespace GeoSciDetail

inline bool ParseNetCdfData(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) { return GeoSciDetail::ParseNetCdf(raw, extLower, out); }

} // namespace Formats
