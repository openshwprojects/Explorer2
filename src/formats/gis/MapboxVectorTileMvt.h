#pragma once
// MapboxVectorTileMvt.h — Mapbox Vector Tile v2.1 (.mvt) Protobuf Layer & Cursor Geometry Reader

namespace Formats {
namespace DetailMvt {

inline int64_t ZigZag64(uint64_t u) {
    return static_cast<int64_t>((u >> 1) ^ (~(u & 1ULL) + 1ULL));
}

inline int32_t ZigZag32(uint32_t u) {
    return static_cast<int32_t>((u >> 1) ^ (~(u & 1u) + 1u));
}

inline bool ReadPbBytes(const uint8_t* d, size_t sz, size_t& pos, const uint8_t*& outP, size_t& outN) {
    uint64_t ln = 0;
    if (!DetailStructuredDataDoc::ReadVarint64(d, sz, pos, ln) || ln > sz - pos) return false;
    outP = d + pos;
    outN = static_cast<size_t>(ln);
    pos += outN;
    return true;
}

inline bool SkipPb(const uint8_t* d, size_t sz, size_t& pos, uint8_t wt) {
    if (wt == 0) { uint64_t v = 0; return DetailStructuredDataDoc::ReadVarint64(d, sz, pos, v); }
    if (wt == 1) { if (pos + 8 > sz) return false; pos += 8; return true; }
    if (wt == 2) { const uint8_t* p = nullptr; size_t n = 0; return ReadPbBytes(d, sz, pos, p, n); }
    if (wt == 5) { if (pos + 4 > sz) return false; pos += 4; return true; }
    return false;
}

inline bool DecodeMvtValue(const uint8_t* d, size_t sz, std::wstring& outVal) {
    outVal.clear();
    size_t pos = 0;
    while (pos < sz) {
        uint64_t tag = 0;
        if (!DetailStructuredDataDoc::ReadVarint64(d, sz, pos, tag) || tag == 0) return false;
        uint32_t fn = static_cast<uint32_t>(tag >> 3);
        uint8_t wt = static_cast<uint8_t>(tag & 7u);
        if (fn == 1 && wt == 2) { // string_value
            const uint8_t* p = nullptr; size_t n = 0;
            if (!ReadPbBytes(d, sz, pos, p, n)) return false;
            outVal = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(p), n));
        } else if (fn == 2 && wt == 5) { // float_value
            if (pos + 4 > sz) return false;
            outVal = AiDataDetail::FmtDouble(::Explorer2::ByteIO::RdF32LE(d + pos));
            pos += 4;
        } else if (fn == 3 && wt == 1) { // double_value
            if (pos + 8 > sz) return false;
            outVal = AiDataDetail::FmtDouble(::Explorer2::ByteIO::RdF64LE(d + pos));
            pos += 8;
        } else if ((fn == 4 || fn == 5 || fn == 6 || fn == 7) && wt == 0) {
            uint64_t v = 0;
            if (!DetailStructuredDataDoc::ReadVarint64(d, sz, pos, v)) return false;
            if (fn == 4) outVal = std::to_wstring(static_cast<int64_t>(v));
            else if (fn == 5) outVal = std::to_wstring(v);
            else if (fn == 6) outVal = std::to_wstring(ZigZag64(v));
            else outVal = (v != 0) ? L"true" : L"false";
        } else {
            if (!SkipPb(d, sz, pos, wt)) return false;
        }
    }
    return true;
}

inline void DecodeMvtGeometryStream(
    const std::vector<uint32_t>& geom,
    uint32_t geomType,
    size_t& outParts,
    size_t& outVerts,
    std::wstring& outSummary,
    std::vector<std::pair<int64_t, int64_t>>* outAllPts = nullptr)
{
    outParts = 0;
    outVerts = 0;
    int64_t cx = 0, cy = 0;
    std::vector<std::pair<int64_t, int64_t>> samplePts;
    size_t closedRings = 0;

    size_t i = 0;
    while (i < geom.size()) {
        uint32_t cmdInt = geom[i++];
        uint32_t cmdId  = cmdInt & 0x07u;
        uint32_t count  = cmdInt >> 3;
        if (cmdId == 1 || cmdId == 2) { // MoveTo(1) or LineTo(2)
            if (cmdId == 1) ++outParts;
            for (uint32_t c = 0; c < count && i + 1 < geom.size(); ++c) {
                cx += ZigZag32(geom[i++]);
                cy += ZigZag32(geom[i++]);
                ++outVerts;
                if (samplePts.size() < 4) samplePts.emplace_back(cx, cy);
                if (outAllPts && outAllPts->size() < 4096) outAllPts->emplace_back(cx, cy);
            }
        } else if (cmdId == 7) { // ClosePath(7)
            closedRings += std::max<uint32_t>(1u, count);
        } else {
            break;
        }
    }

    const wchar_t* gname = (geomType == 1) ? L"POINT" : (geomType == 2 ? L"LINESTRING" : (geomType == 3 ? L"POLYGON" : L"UNKNOWN"));
    outSummary = std::wstring(gname) + L" (";
    for (size_t k = 0; k < samplePts.size(); ++k) {
        if (k) outSummary += L", ";
        outSummary += std::to_wstring(samplePts[k].first) + L" " + std::to_wstring(samplePts[k].second);
    }
    if (outVerts > samplePts.size()) outSummary += L", ...";
    if (closedRings > 0) outSummary += L" [closed x" + std::to_wstring(closedRings) + L"]";
    outSummary += L")";
}

} // namespace DetailMvt

inline bool ParseMapboxVectorTileMvt(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 4) return false;

    std::vector<uint8_t> inflated;
    const uint8_t* tilePtr = raw.data();
    size_t tileLen = raw.size();
    std::wstring wrapper = L"Raw Protobuf";
    if (raw.size() >= 10 && raw[0] == 0x1Fu && raw[1] == 0x8Bu) {
        if (!ZipDeflate::InflateGzip(raw.data(), raw.size(), inflated, 64u * 1024u * 1024u)) return false;
        tilePtr = inflated.data();
        tileLen = inflated.size();
        wrapper = L"Gzip";
    } else if (raw.size() >= 6 && raw[0] == 0x78u && (raw[1] == 0x01u || raw[1] == 0x5Eu || raw[1] == 0x9Cu || raw[1] == 0xDAu)) {
        if (ZipDeflate::InflateZlib(raw.data(), raw.size(), inflated, 64u * 1024u * 1024u)) {
            tilePtr = inflated.data();
            tileLen = inflated.size();
            wrapper = L"Zlib";
        }
    }

    SpreadsheetSheet layerSh;
    layerSh.name = L"MVT_Layers";
    layerSh.rows.push_back({ L"#", L"Layer Name", L"Version", L"Extent", L"Features", L"Keys", L"Values" });

    SpreadsheetSheet featSh;
    featSh.name = L"MVT_Features";
    featSh.rows.push_back({ L"Layer", L"Feature ID", L"Geometry Type", L"Parts / Vertices", L"Decoded Tile Geometry", L"Properties" });

    size_t pos = 0;
    size_t layerCount = 0;
    while (pos < tileLen) {
        uint64_t tag = 0;
        if (!DetailStructuredDataDoc::ReadVarint64(tilePtr, tileLen, pos, tag) || tag == 0) return false;
        uint32_t fn = static_cast<uint32_t>(tag >> 3);
        uint8_t wt = static_cast<uint8_t>(tag & 7u);
        if (fn != 3 || wt != 2) {
            if (!DetailMvt::SkipPb(tilePtr, tileLen, pos, wt)) return false;
            continue;
        }
        const uint8_t* lp = nullptr; size_t ll = 0;
        if (!DetailMvt::ReadPbBytes(tilePtr, tileLen, pos, lp, ll)) return false;

        // Parse Layer: pass 1 collects name, version, extent, keys, values, and raw Feature slices
        std::wstring layerName;
        uint32_t version = 1;
        uint32_t extent = 4096;
        std::vector<std::wstring> keys;
        std::vector<std::wstring> vals;
        std::vector<std::pair<const uint8_t*, size_t>> rawFeats;

        size_t lpos = 0;
        while (lpos < ll) {
            uint64_t ltag = 0;
            if (!DetailStructuredDataDoc::ReadVarint64(lp, ll, lpos, ltag) || ltag == 0) return false;
            uint32_t lfn = static_cast<uint32_t>(ltag >> 3);
            uint8_t lwt = static_cast<uint8_t>(ltag & 7u);
            if (lfn == 1 && lwt == 2) { // name (required)
                const uint8_t* p = nullptr; size_t n = 0;
                if (!DetailMvt::ReadPbBytes(lp, ll, lpos, p, n)) return false;
                layerName = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(p), n));
            } else if (lfn == 2 && lwt == 2) { // features
                const uint8_t* p = nullptr; size_t n = 0;
                if (!DetailMvt::ReadPbBytes(lp, ll, lpos, p, n)) return false;
                rawFeats.emplace_back(p, n);
            } else if (lfn == 3 && lwt == 2) { // keys
                const uint8_t* p = nullptr; size_t n = 0;
                if (!DetailMvt::ReadPbBytes(lp, ll, lpos, p, n)) return false;
                if (keys.size() < 65536) keys.push_back(PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(p), n)));
            } else if (lfn == 4 && lwt == 2) { // values
                const uint8_t* p = nullptr; size_t n = 0;
                if (!DetailMvt::ReadPbBytes(lp, ll, lpos, p, n)) return false;
                std::wstring v;
                if (!DetailMvt::DecodeMvtValue(p, n, v)) return false;
                if (vals.size() < 65536) vals.push_back(std::move(v));
            } else if ((lfn == 5 || lfn == 15) && lwt == 0) {
                uint64_t v = 0;
                if (!DetailStructuredDataDoc::ReadVarint64(lp, ll, lpos, v)) return false;
                if (lfn == 5) extent = static_cast<uint32_t>(v);
                else version = static_cast<uint32_t>(v);
            } else {
                if (!DetailMvt::SkipPb(lp, ll, lpos, lwt)) return false;
            }
        }

        if (layerName.empty()) return false;
        ++layerCount;
        layerSh.rows.push_back({
            std::to_wstring(layerCount),
            layerName,
            std::to_wstring(version),
            std::to_wstring(extent),
            std::to_wstring(rawFeats.size()),
            std::to_wstring(keys.size()),
            std::to_wstring(vals.size())
        });

        // Pass 2: decode Features against Layer keys[] and vals[]
        for (const auto& [fp, fl] : rawFeats) {
            if (featSh.rows.size() > 3000) break;
            uint64_t fid = 0;
            bool hasFid = false;
            uint32_t gtype = 0;
            std::vector<uint32_t> ftags;
            std::vector<uint32_t> fgeom;

            auto readPackedOrSingleU32 = [](const uint8_t* d, size_t sz, size_t& p, uint8_t wt, std::vector<uint32_t>& dst) -> bool {
                if (wt == 0) {
                    uint64_t v = 0;
                    if (!DetailStructuredDataDoc::ReadVarint64(d, sz, p, v)) return false;
                    if (dst.size() < 65536) dst.push_back(static_cast<uint32_t>(v));
                    return true;
                }
                if (wt == 2) {
                    const uint8_t* bp = nullptr; size_t bn = 0;
                    if (!DetailMvt::ReadPbBytes(d, sz, p, bp, bn)) return false;
                    size_t k = 0;
                    while (k < bn && dst.size() < 65536) {
                        uint64_t v = 0;
                        if (!DetailStructuredDataDoc::ReadVarint64(bp, bn, k, v)) return false;
                        dst.push_back(static_cast<uint32_t>(v));
                    }
                    return true;
                }
                return false;
            };

            size_t fpos = 0;
            while (fpos < fl) {
                uint64_t ftag = 0;
                if (!DetailStructuredDataDoc::ReadVarint64(fp, fl, fpos, ftag) || ftag == 0) break;
                uint32_t ffn = static_cast<uint32_t>(ftag >> 3);
                uint8_t fwt = static_cast<uint8_t>(ftag & 7u);
                if (ffn == 1 && fwt == 0) {
                    if (!DetailStructuredDataDoc::ReadVarint64(fp, fl, fpos, fid)) break;
                    hasFid = true;
                } else if (ffn == 2 && (fwt == 0 || fwt == 2)) {
                    if (!readPackedOrSingleU32(fp, fl, fpos, fwt, ftags)) break;
                } else if (ffn == 3 && fwt == 0) {
                    uint64_t v = 0;
                    if (!DetailStructuredDataDoc::ReadVarint64(fp, fl, fpos, v)) break;
                    gtype = static_cast<uint32_t>(v);
                } else if (ffn == 4 && (fwt == 0 || fwt == 2)) {
                    if (!readPackedOrSingleU32(fp, fl, fpos, fwt, fgeom)) break;
                } else {
                    if (!DetailMvt::SkipPb(fp, fl, fpos, fwt)) break;
                }
            }

            size_t parts = 0, verts = 0;
            std::wstring geomStr;
            std::vector<std::pair<int64_t, int64_t>> tilePts;
            DetailMvt::DecodeMvtGeometryStream(fgeom, gtype, parts, verts, geomStr, &tilePts);

            std::wstring props;
            for (size_t ti = 0; ti + 1 < ftags.size(); ti += 2) {
                uint32_t ki = ftags[ti], vi = ftags[ti + 1];
                if (ki < keys.size() && vi < vals.size() && props.size() < 280) {
                    if (!props.empty()) props += L"; ";
                    props += keys[ki] + L"=" + vals[vi];
                }
            }

            const wchar_t* gtLabel = (gtype == 1) ? L"POINT" : (gtype == 2 ? L"LINESTRING" : (gtype == 3 ? L"POLYGON" : L"UNKNOWN"));
            const int featRowIdx = static_cast<int>(featSh.rows.size());
            featSh.rows.push_back({
                layerName,
                hasFid ? std::to_wstring(fid) : L"-",
                gtLabel,
                std::to_wstring(parts) + L" / " + std::to_wstring(verts),
                geomStr,
                props
            });

            const double extDenom = (extent > 0) ? static_cast<double>(extent) : 4096.0;
            for (const auto& [tx, ty] : tilePts) {
                const double normX = std::clamp(static_cast<double>(tx) / extDenom, -0.25, 1.25);
                const double normY = std::clamp(static_cast<double>(ty) / extDenom, -0.25, 1.25);
                const double previewLat = std::clamp((0.5 - normY) * 60.0, -85.0, 85.0);
                const double previewLon = std::clamp((normX - 0.5) * 120.0, -179.0, 179.0);
                Formats::AddGeoPoint(out, previewLat, previewLon, 0.0, featRowIdx, layerName, 8192);
            }
        }
    }

    if (layerCount == 0) return false;

    FinalizeSheetCols(featSh);
    FinalizeSheetCols(layerSh);
    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Mapbox Vector Tile (" + wrapper + L", " + std::to_wstring(layerCount) + L" layer(s))";
    if (featSh.rows.size() > 1) out.sheets.push_back(std::move(featSh));
    out.sheets.push_back(std::move(layerSh));
    if (!out.geoPoints.empty()) out.hasGeoTrack = true;
    out.valid = true;
    return true;
}

} // namespace Formats
