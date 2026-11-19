#pragma once
// OsmPbfMap.h — OpenStreetMap Binary PBF Dataset (.osm.pbf / .pbf) Reader

namespace Formats {

namespace DetailOsmPbf {

inline int64_t ZigZag64(uint64_t v) {
    return static_cast<int64_t>((v >> 1) ^ (~(v & 1ULL) + 1ULL));
}

inline bool SkipPbWire(const uint8_t* d, size_t sz, size_t& pos, uint8_t wt) {
    if (wt == 0) { uint64_t v = 0; return DetailDocs200::ReadVarint64(d, sz, pos, v); }
    if (wt == 1) { if (pos + 8 > sz) return false; pos += 8; return true; }
    if (wt == 2) {
        uint64_t ln = 0;
        if (!DetailDocs200::ReadVarint64(d, sz, pos, ln) || ln > sz - pos) return false;
        pos += static_cast<size_t>(ln);
        return true;
    }
    if (wt == 5) { if (pos + 4 > sz) return false; pos += 4; return true; }
    return false;
}

inline bool ReadPbBytes(const uint8_t* d, size_t sz, size_t& pos, const uint8_t*& outPtr, size_t& outLen) {
    uint64_t ln = 0;
    if (!DetailDocs200::ReadVarint64(d, sz, pos, ln) || ln > sz - pos) return false;
    outPtr = d + pos;
    outLen = static_cast<size_t>(ln);
    pos += outLen;
    return true;
}

inline bool ParseOsmBlobHeader(const uint8_t* d, size_t sz, std::string& outType, uint32_t& outDataSize) {
    outType.clear();
    outDataSize = 0;
    bool hasDataSize = false;
    size_t pos = 0;
    while (pos < sz) {
        uint64_t tag = 0;
        if (!DetailDocs200::ReadVarint64(d, sz, pos, tag) || tag == 0) return false;
        uint32_t fn = static_cast<uint32_t>(tag >> 3);
        uint8_t wt = static_cast<uint8_t>(tag & 0x07u);
        if (fn == 1 && wt == 2) {
            const uint8_t* p = nullptr; size_t n = 0;
            if (!ReadPbBytes(d, sz, pos, p, n)) return false;
            outType.assign(reinterpret_cast<const char*>(p), n);
        } else if (fn == 3 && wt == 0) {
            uint64_t v = 0;
            if (!DetailDocs200::ReadVarint64(d, sz, pos, v) || v > 64ULL * 1024ULL * 1024ULL) return false;
            outDataSize = static_cast<uint32_t>(v);
            hasDataSize = true;
        } else {
            if (!SkipPbWire(d, sz, pos, wt)) return false;
        }
    }
    return !outType.empty() && hasDataSize;
}

inline bool DecompressOsmBlob(const uint8_t* d, size_t sz, std::vector<uint8_t>& outBytes, std::wstring& outCodec) {
    outBytes.clear();
    const uint8_t *rawPtr = nullptr, *zlibPtr = nullptr, *lzmaPtr = nullptr, *zstdPtr = nullptr;
    size_t rawLen = 0, zlibLen = 0, lzmaLen = 0, zstdLen = 0;
    uint64_t rawSizeHint = 0;

    size_t pos = 0;
    while (pos < sz) {
        uint64_t tag = 0;
        if (!DetailDocs200::ReadVarint64(d, sz, pos, tag) || tag == 0) return false;
        uint32_t fn = static_cast<uint32_t>(tag >> 3);
        uint8_t wt = static_cast<uint8_t>(tag & 0x07u);
        if (fn == 1 && wt == 2) { if (!ReadPbBytes(d, sz, pos, rawPtr, rawLen)) return false; }
        else if (fn == 2 && wt == 0) { if (!DetailDocs200::ReadVarint64(d, sz, pos, rawSizeHint)) return false; }
        else if (fn == 3 && wt == 2) { if (!ReadPbBytes(d, sz, pos, zlibPtr, zlibLen)) return false; }
        else if (fn == 4 && wt == 2) { if (!ReadPbBytes(d, sz, pos, lzmaPtr, lzmaLen)) return false; }
        else if (fn == 6 && wt == 2) { if (!ReadPbBytes(d, sz, pos, zstdPtr, zstdLen)) return false; }
        else { if (!SkipPbWire(d, sz, pos, wt)) return false; }
    }

    constexpr size_t kMaxBlobOut = 64u * 1024u * 1024u;
    if (rawPtr) {
        if (rawLen > kMaxBlobOut) return false;
        outCodec = L"Uncompressed";
        outBytes.assign(rawPtr, rawPtr + rawLen);
        return true;
    }
    if (zlibPtr && zlibLen > 0) {
        outCodec = L"Zlib";
        return ZipDeflate::InflateZlib(zlibPtr, zlibLen, outBytes, kMaxBlobOut);
    }
    if (zstdPtr && zstdLen > 0) {
        outCodec = L"Zstandard";
        return ::Explorer2::StreamCodecs::DecompressZstdFrame(zstdPtr, zstdLen, outBytes, kMaxBlobOut);
    }
    if (lzmaPtr && lzmaLen > 0) {
        outCodec = L"LZMA";
        if (::Explorer2::StreamCodecs::DecompressXzStream(lzmaPtr, lzmaLen, outBytes, kMaxBlobOut)) return true;
        if (lzmaLen > 13) {
            uint8_t props = lzmaPtr[0];
            uint32_t dictSz = DetailDocs200::RdU32LE(lzmaPtr + 1);
            uint64_t hdrUncomp = DetailDocs200::RdU64LE(lzmaPtr + 5);
            bool known = (hdrUncomp != UINT64_MAX) || (rawSizeHint > 0);
            uint64_t want = (hdrUncomp != UINT64_MAX) ? hdrUncomp : rawSizeHint;
            if (::Explorer2::StreamCodecs::DecompressLzma1(props, dictSz, lzmaPtr + 13, lzmaLen - 13, outBytes, want, known, kMaxBlobOut)) {
                return true;
            }
        }
        if (lzmaLen > 5 && rawSizeHint > 0) {
            uint8_t props = lzmaPtr[0];
            uint32_t dictSz = DetailDocs200::RdU32LE(lzmaPtr + 1);
            if (::Explorer2::StreamCodecs::DecompressLzma1(props, dictSz, lzmaPtr + 5, lzmaLen - 5, outBytes, rawSizeHint, true, kMaxBlobOut)) {
                return true;
            }
        }
        return false;
    }
    return false;
}

inline void SummarizeOsmTags(const std::vector<std::string>& strTab,
                             const std::vector<uint32_t>& keys,
                             const std::vector<uint32_t>& vals,
                             std::wstring& outName,
                             std::wstring& outFeature,
                             std::wstring& outAllTags) {
    outName.clear();
    outFeature.clear();
    outAllTags.clear();
    size_t n = std::min(keys.size(), vals.size());
    for (size_t i = 0; i < n; ++i) {
        if (keys[i] >= strTab.size() || vals[i] >= strTab.size()) continue;
        const std::string& k = strTab[keys[i]];
        const std::string& v = strTab[vals[i]];
        std::wstring kw = PathUtils::Utf8ToWide(k);
        std::wstring vw = PathUtils::Utf8ToWide(v);
        if (k == "name" || (outName.empty() && k == "name:en")) {
            outName = vw;
        }
        if (outFeature.empty() && (k == "highway" || k == "building" || k == "amenity" || k == "natural" ||
                                   k == "landuse" || k == "railway" || k == "waterway" || k == "shop" ||
                                   k == "tourism" || k == "leisure" || k == "type" || k == "place")) {
            outFeature = kw + L"=" + vw;
        }
        if (outAllTags.size() < 240) {
            if (!outAllTags.empty()) outAllTags += L"; ";
            outAllTags += kw + L"=" + vw;
        }
    }
}

inline bool ParseOsmPbfFile(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    SourceBytes source(raw, out);
    const uint64_t fileSize = source.Size();

    bool hasBbox = false;
    double minLon = 0.0, maxLon = 0.0, minLat = 0.0, maxLat = 0.0;
    std::wstring writingProg, osmSource, reqFeatures, optFeatures, lastCodec;
    size_t blobCount = 0;
    uint64_t totalNodes = 0, totalWays = 0, totalRelations = 0;

    SpreadsheetSheet nodesSh;
    nodesSh.name = L"OSM_Nodes";
    nodesSh.rows.push_back({ L"Node ID", L"Latitude", L"Longitude", L"Name", L"Feature", L"Tags" });

    SpreadsheetSheet waysSh;
    waysSh.name = L"OSM_Ways";
    waysSh.rows.push_back({ L"Way ID", L"Node Count", L"Start -> End Ref", L"Name", L"Feature", L"Tags" });

    SpreadsheetSheet relsSh;
    relsSh.name = L"OSM_Relations";
    relsSh.rows.push_back({ L"Relation ID", L"Members", L"Name", L"Feature / Type", L"Sample Members", L"Tags" });

    std::unordered_map<int64_t, std::pair<double, double>> nodeCoords;
    nodeCoords.reserve(4096);

    auto fmtDeg = [](double d) {
        wchar_t b[32] = {};
        swprintf_s(b, L"%.7f", d);
        return std::wstring(b);
    };

    uint64_t filePos = 0;
    std::vector<uint8_t> ioBuf;
    while (filePos + 4 <= fileSize && blobCount < 1024) {
        const uint8_t* lenBytes = source.Read(filePos, 4, ioBuf);
        if (!lenBytes) break;
        uint32_t hdrLen = DetailDocs200::RdU32BE(lenBytes);
        if (hdrLen < 4 || hdrLen > 65536 || filePos + 4 + hdrLen > fileSize) {
            if (blobCount == 0) return false;
            break;
        }
        const uint8_t* hdrBytes = source.Read(filePos + 4, hdrLen, ioBuf);
        if (!hdrBytes) break;

        std::string btype;
        uint32_t dataSize = 0;
        if (!ParseOsmBlobHeader(hdrBytes, hdrLen, btype, dataSize)) {
            if (blobCount == 0) return false;
            break;
        }
        uint64_t blobOff = filePos + 4 + hdrLen;
        if (blobOff + dataSize > fileSize) {
            if (blobCount == 0) return false;
            break;
        }
        filePos = blobOff + dataSize;

        if (btype != "OSMHeader" && btype != "OSMData") {
            if (blobCount == 0) return false;
            continue;
        }

        const uint8_t* blobRaw = source.Read(blobOff, dataSize, ioBuf);
        if (!blobRaw) {
            if (blobCount == 0) return false;
            break;
        }
        std::vector<uint8_t> blobDec;
        if (!DecompressOsmBlob(blobRaw, dataSize, blobDec, lastCodec)) {
            return false; // Corrupt OSM blob stream
        }
        ++blobCount;

        if (btype == "OSMHeader") {
            size_t hp = 0;
            while (hp < blobDec.size()) {
                uint64_t tag = 0;
                if (!DetailDocs200::ReadVarint64(blobDec.data(), blobDec.size(), hp, tag) || tag == 0) return false;
                uint32_t fn = static_cast<uint32_t>(tag >> 3);
                uint8_t wt = static_cast<uint8_t>(tag & 0x07u);
                if (fn == 1 && wt == 2) { // HeaderBBox
                    const uint8_t* bp = nullptr; size_t bl = 0;
                    if (!ReadPbBytes(blobDec.data(), blobDec.size(), hp, bp, bl)) return false;
                    size_t xp = 0;
                    while (xp < bl) {
                        uint64_t btag = 0, bv = 0;
                        if (!DetailDocs200::ReadVarint64(bp, bl, xp, btag)) break;
                        uint32_t bfn = static_cast<uint32_t>(btag >> 3);
                        uint8_t bwt = static_cast<uint8_t>(btag & 0x07u);
                        if (bwt == 0 && DetailDocs200::ReadVarint64(bp, bl, xp, bv)) {
                            double deg = static_cast<double>(ZigZag64(bv)) * 1e-9;
                            if (bfn == 1) minLon = deg;
                            else if (bfn == 2) maxLon = deg;
                            else if (bfn == 3) maxLat = deg;
                            else if (bfn == 4) minLat = deg;
                            hasBbox = true;
                        } else if (!SkipPbWire(bp, bl, xp, bwt)) break;
                    }
                } else if ((fn == 4 || fn == 5 || fn == 16 || fn == 17) && wt == 2) {
                    const uint8_t* sp = nullptr; size_t sl = 0;
                    if (!ReadPbBytes(blobDec.data(), blobDec.size(), hp, sp, sl)) return false;
                    std::wstring ws = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(sp), sl));
                    if (fn == 4) { if (!reqFeatures.empty()) reqFeatures += L", "; reqFeatures += ws; }
                    else if (fn == 5) { if (!optFeatures.empty()) optFeatures += L", "; optFeatures += ws; }
                    else if (fn == 16) writingProg = ws;
                    else if (fn == 17) osmSource = ws;
                } else {
                    if (!SkipPbWire(blobDec.data(), blobDec.size(), hp, wt)) return false;
                }
            }
        } else if (btype == "OSMData") {
            std::vector<std::string> strTab;
            std::vector<std::pair<const uint8_t*, size_t>> groups;
            int64_t granularity = 100;
            int64_t latOffset = 0;
            int64_t lonOffset = 0;

            // Pass 1: StringTable, coordinate parameters (fields 17/19/20), and PrimitiveGroup slices
            size_t pp = 0;
            while (pp < blobDec.size()) {
                uint64_t tag = 0;
                if (!DetailDocs200::ReadVarint64(blobDec.data(), blobDec.size(), pp, tag) || tag == 0) return false;
                uint32_t fn = static_cast<uint32_t>(tag >> 3);
                uint8_t wt = static_cast<uint8_t>(tag & 0x07u);
                if (fn == 1 && wt == 2) { // StringTable
                    const uint8_t* stp = nullptr; size_t stl = 0;
                    if (!ReadPbBytes(blobDec.data(), blobDec.size(), pp, stp, stl)) return false;
                    size_t sp = 0;
                    while (sp < stl && strTab.size() < 250000) {
                        uint64_t stag = 0;
                        if (!DetailDocs200::ReadVarint64(stp, stl, sp, stag)) break;
                        if ((stag >> 3) == 1 && (stag & 7u) == 2) {
                            const uint8_t* ep = nullptr; size_t el = 0;
                            if (!ReadPbBytes(stp, stl, sp, ep, el)) break;
                            strTab.emplace_back(reinterpret_cast<const char*>(ep), el);
                        } else if (!SkipPbWire(stp, stl, sp, static_cast<uint8_t>(stag & 7u))) break;
                    }
                } else if (fn == 2 && wt == 2) { // PrimitiveGroup
                    const uint8_t* gp = nullptr; size_t gl = 0;
                    if (!ReadPbBytes(blobDec.data(), blobDec.size(), pp, gp, gl)) return false;
                    groups.emplace_back(gp, gl);
                } else if (fn == 17 && wt == 0) {
                    uint64_t v = 0;
                    if (!DetailDocs200::ReadVarint64(blobDec.data(), blobDec.size(), pp, v)) return false;
                    granularity = static_cast<int32_t>(static_cast<uint32_t>(v));
                } else if (fn == 19 && wt == 0) {
                    uint64_t v = 0;
                    if (!DetailDocs200::ReadVarint64(blobDec.data(), blobDec.size(), pp, v)) return false;
                    latOffset = static_cast<int64_t>(v);
                } else if (fn == 20 && wt == 0) {
                    uint64_t v = 0;
                    if (!DetailDocs200::ReadVarint64(blobDec.data(), blobDec.size(), pp, v)) return false;
                    lonOffset = static_cast<int64_t>(v);
                } else {
                    if (!SkipPbWire(blobDec.data(), blobDec.size(), pp, wt)) return false;
                }
            }

            auto readPackedU32 = [](const uint8_t* p, size_t n, std::vector<uint32_t>& dst) {
                size_t k = 0;
                while (k < n && dst.size() < 200000) {
                    uint64_t v = 0;
                    if (!DetailDocs200::ReadVarint64(p, n, k, v)) break;
                    dst.push_back(static_cast<uint32_t>(v));
                }
            };

            // Pass 2: Decode Nodes, DenseNodes, Ways, and Relations
            for (const auto& [gp, gl] : groups) {
                size_t gpos = 0;
                while (gpos < gl) {
                    uint64_t gtag = 0;
                    if (!DetailDocs200::ReadVarint64(gp, gl, gpos, gtag) || gtag == 0) break;
                    uint32_t gfn = static_cast<uint32_t>(gtag >> 3);
                    uint8_t gwt = static_cast<uint8_t>(gtag & 0x07u);
                    if (gwt != 2) {
                        if (!SkipPbWire(gp, gl, gpos, gwt)) break;
                        continue;
                    }
                    const uint8_t* mp = nullptr; size_t ml = 0;
                    if (!ReadPbBytes(gp, gl, gpos, mp, ml)) break;

                    if (gfn == 1) { // Node
                        int64_t nid = 0, nlat = 0, nlon = 0;
                        std::vector<uint32_t> keys, vals;
                        size_t np = 0;
                        while (np < ml) {
                            uint64_t ntag = 0;
                            if (!DetailDocs200::ReadVarint64(mp, ml, np, ntag)) break;
                            uint32_t nfn = static_cast<uint32_t>(ntag >> 3);
                            uint8_t nwt = static_cast<uint8_t>(ntag & 7u);
                            if (nfn == 1 && nwt == 0) { uint64_t v = 0; DetailDocs200::ReadVarint64(mp, ml, np, v); nid = ZigZag64(v); }
                            else if (nfn == 2 && nwt == 2) { const uint8_t* p2; size_t l2; if (ReadPbBytes(mp, ml, np, p2, l2)) readPackedU32(p2, l2, keys); }
                            else if (nfn == 2 && nwt == 0) { uint64_t v = 0; DetailDocs200::ReadVarint64(mp, ml, np, v); keys.push_back(static_cast<uint32_t>(v)); }
                            else if (nfn == 3 && nwt == 2) { const uint8_t* p2; size_t l2; if (ReadPbBytes(mp, ml, np, p2, l2)) readPackedU32(p2, l2, vals); }
                            else if (nfn == 3 && nwt == 0) { uint64_t v = 0; DetailDocs200::ReadVarint64(mp, ml, np, v); vals.push_back(static_cast<uint32_t>(v)); }
                            else if (nfn == 8 && nwt == 0) { uint64_t v = 0; DetailDocs200::ReadVarint64(mp, ml, np, v); nlat = ZigZag64(v); }
                            else if (nfn == 9 && nwt == 0) { uint64_t v = 0; DetailDocs200::ReadVarint64(mp, ml, np, v); nlon = ZigZag64(v); }
                            else if (!SkipPbWire(mp, ml, np, nwt)) break;
                        }
                        double lat = 1e-9 * static_cast<double>(latOffset + granularity * nlat);
                        double lon = 1e-9 * static_cast<double>(lonOffset + granularity * nlon);
                        ++totalNodes;
                        if (nodeCoords.size() < 65536) nodeCoords[nid] = { lat, lon };

                        std::wstring nameW, featW, tagsW;
                        SummarizeOsmTags(strTab, keys, vals, nameW, featW, tagsW);
                        int rowIdx = -1;
                        if (nodesSh.rows.size() <= 1500 && (!tagsW.empty() || nodesSh.rows.size() <= 300)) {
                            rowIdx = static_cast<int>(nodesSh.rows.size());
                            nodesSh.rows.push_back({ std::to_wstring(nid), fmtDeg(lat), fmtDeg(lon), nameW, featW, tagsW });
                        }
                        if (out.geoPoints.size() < 4096) {
                            std::wstring lbl = !nameW.empty() ? nameW : (!featW.empty() ? featW : (L"Node " + std::to_wstring(nid)));
                            Formats::AddGeoPoint(out, lat, lon, 0.0, rowIdx, lbl, 4096);
                        }
                    } else if (gfn == 2) { // DenseNodes (delta-coded sint64 id/lat/lon + 0-delimited keys_vals)
                        std::vector<int64_t> ids, lats, lons;
                        std::vector<uint32_t> kvStream;
                        size_t dp = 0;
                        while (dp < ml) {
                            uint64_t dtag = 0;
                            if (!DetailDocs200::ReadVarint64(mp, ml, dp, dtag)) break;
                            uint32_t dfn = static_cast<uint32_t>(dtag >> 3);
                            uint8_t dwt = static_cast<uint8_t>(dtag & 7u);
                            if ((dfn == 1 || dfn == 8 || dfn == 9) && dwt == 2) {
                                const uint8_t* sp = nullptr; size_t sl = 0;
                                if (!ReadPbBytes(mp, ml, dp, sp, sl)) break;
                                auto& target = (dfn == 1) ? ids : ((dfn == 8) ? lats : lons);
                                int64_t acc = 0;
                                size_t sk = 0;
                                while (sk < sl && target.size() < 200000) {
                                    uint64_t v = 0;
                                    if (!DetailDocs200::ReadVarint64(sp, sl, sk, v)) break;
                                    acc += ZigZag64(v);
                                    target.push_back(acc);
                                }
                            } else if (dfn == 10 && dwt == 2) {
                                const uint8_t* sp = nullptr; size_t sl = 0;
                                if (!ReadPbBytes(mp, ml, dp, sp, sl)) break;
                                readPackedU32(sp, sl, kvStream);
                            } else if (!SkipPbWire(mp, ml, dp, dwt)) break;
                        }

                        size_t nDense = std::min({ ids.size(), lats.size(), lons.size() });
                        size_t kvIdx = 0;
                        std::vector<uint32_t> keys, vals;
                        for (size_t i = 0; i < nDense; ++i) {
                            keys.clear();
                            vals.clear();
                            while (kvIdx < kvStream.size()) {
                                uint32_t kSid = kvStream[kvIdx++];
                                if (kSid == 0) break;
                                if (kvIdx >= kvStream.size()) break;
                                uint32_t vSid = kvStream[kvIdx++];
                                keys.push_back(kSid);
                                vals.push_back(vSid);
                            }
                            double lat = 1e-9 * static_cast<double>(latOffset + granularity * lats[i]);
                            double lon = 1e-9 * static_cast<double>(lonOffset + granularity * lons[i]);
                            ++totalNodes;
                            if (nodeCoords.size() < 65536) nodeCoords[ids[i]] = { lat, lon };

                            std::wstring nameW, featW, tagsW;
                            SummarizeOsmTags(strTab, keys, vals, nameW, featW, tagsW);
                            int rowIdx = -1;
                            if (nodesSh.rows.size() <= 1500 && (!tagsW.empty() || nodesSh.rows.size() <= 300)) {
                                rowIdx = static_cast<int>(nodesSh.rows.size());
                                nodesSh.rows.push_back({ std::to_wstring(ids[i]), fmtDeg(lat), fmtDeg(lon), nameW, featW, tagsW });
                            }
                            if (out.geoPoints.size() < 4096) {
                                std::wstring lbl = !nameW.empty() ? nameW : (!featW.empty() ? featW : (L"Node " + std::to_wstring(ids[i])));
                                Formats::AddGeoPoint(out, lat, lon, 0.0, rowIdx, lbl, 4096);
                            }
                        }
                    } else if (gfn == 3) { // Way
                        int64_t wid = 0;
                        std::vector<uint32_t> keys, vals;
                        std::vector<int64_t> refs;
                        size_t wp = 0;
                        while (wp < ml) {
                            uint64_t wtag = 0;
                            if (!DetailDocs200::ReadVarint64(mp, ml, wp, wtag)) break;
                            uint32_t wfn = static_cast<uint32_t>(wtag >> 3);
                            uint8_t wwt = static_cast<uint8_t>(wtag & 7u);
                            if (wfn == 1 && wwt == 0) { uint64_t v = 0; DetailDocs200::ReadVarint64(mp, ml, wp, v); wid = static_cast<int64_t>(v); }
                            else if (wfn == 2 && wwt == 2) { const uint8_t* p2; size_t l2; if (ReadPbBytes(mp, ml, wp, p2, l2)) readPackedU32(p2, l2, keys); }
                            else if (wfn == 2 && wwt == 0) { uint64_t v = 0; DetailDocs200::ReadVarint64(mp, ml, wp, v); keys.push_back(static_cast<uint32_t>(v)); }
                            else if (wfn == 3 && wwt == 2) { const uint8_t* p2; size_t l2; if (ReadPbBytes(mp, ml, wp, p2, l2)) readPackedU32(p2, l2, vals); }
                            else if (wfn == 3 && wwt == 0) { uint64_t v = 0; DetailDocs200::ReadVarint64(mp, ml, wp, v); vals.push_back(static_cast<uint32_t>(v)); }
                            else if (wfn == 8 && wwt == 2) {
                                const uint8_t* rp = nullptr; size_t rl = 0;
                                if (ReadPbBytes(mp, ml, wp, rp, rl)) {
                                    int64_t rAcc = 0;
                                    size_t rk = 0;
                                    while (rk < rl && refs.size() < 65536) {
                                        uint64_t v = 0;
                                        if (!DetailDocs200::ReadVarint64(rp, rl, rk, v)) break;
                                        rAcc += ZigZag64(v);
                                        refs.push_back(rAcc);
                                    }
                                }
                            } else if (!SkipPbWire(mp, ml, wp, wwt)) break;
                        }
                        ++totalWays;
                        if (waysSh.rows.size() <= 1500) {
                            std::wstring nameW, featW, tagsW;
                            SummarizeOsmTags(strTab, keys, vals, nameW, featW, tagsW);
                            std::wstring spanStr;
                            if (!refs.empty()) {
                                spanStr = std::to_wstring(refs.front()) + L" -> " + std::to_wstring(refs.back());
                                if (refs.size() >= 3 && refs.front() == refs.back()) spanStr += L" (closed)";
                            }
                            waysSh.rows.push_back({ std::to_wstring(wid), std::to_wstring(refs.size()), spanStr, nameW, featW, tagsW });
                        }
                    } else if (gfn == 4) { // Relation
                        int64_t rid = 0;
                        std::vector<uint32_t> keys, vals, roles, mtypes;
                        std::vector<int64_t> memIds;
                        size_t rp = 0;
                        while (rp < ml) {
                            uint64_t rtag = 0;
                            if (!DetailDocs200::ReadVarint64(mp, ml, rp, rtag)) break;
                            uint32_t rfn = static_cast<uint32_t>(rtag >> 3);
                            uint8_t rwt = static_cast<uint8_t>(rtag & 7u);
                            if (rfn == 1 && rwt == 0) { uint64_t v = 0; DetailDocs200::ReadVarint64(mp, ml, rp, v); rid = static_cast<int64_t>(v); }
                            else if (rfn == 2 && rwt == 2) { const uint8_t* p2; size_t l2; if (ReadPbBytes(mp, ml, rp, p2, l2)) readPackedU32(p2, l2, keys); }
                            else if (rfn == 3 && rwt == 2) { const uint8_t* p2; size_t l2; if (ReadPbBytes(mp, ml, rp, p2, l2)) readPackedU32(p2, l2, vals); }
                            else if (rfn == 8 && rwt == 2) { const uint8_t* p2; size_t l2; if (ReadPbBytes(mp, ml, rp, p2, l2)) readPackedU32(p2, l2, roles); }
                            else if (rfn == 9 && rwt == 2) {
                                const uint8_t* p2 = nullptr; size_t l2 = 0;
                                if (ReadPbBytes(mp, ml, rp, p2, l2)) {
                                    int64_t mAcc = 0;
                                    size_t mk = 0;
                                    while (mk < l2 && memIds.size() < 65536) {
                                        uint64_t v = 0;
                                        if (!DetailDocs200::ReadVarint64(p2, l2, mk, v)) break;
                                        mAcc += ZigZag64(v);
                                        memIds.push_back(mAcc);
                                    }
                                }
                            } else if (rfn == 10 && rwt == 2) {
                                const uint8_t* p2; size_t l2; if (ReadPbBytes(mp, ml, rp, p2, l2)) readPackedU32(p2, l2, mtypes);
                            } else if (!SkipPbWire(mp, ml, rp, rwt)) break;
                        }
                        ++totalRelations;
                        if (relsSh.rows.size() <= 1000) {
                            std::wstring nameW, featW, tagsW;
                            SummarizeOsmTags(strTab, keys, vals, nameW, featW, tagsW);
                            std::wstring memPreview;
                            size_t mShow = std::min<size_t>({ memIds.size(), mtypes.size(), size_t(3) });
                            for (size_t mi = 0; mi < mShow; ++mi) {
                                if (mi) memPreview += L", ";
                                const wchar_t* mt = (mtypes[mi] == 0) ? L"node/" : ((mtypes[mi] == 1) ? L"way/" : L"rel/");
                                memPreview += mt + std::to_wstring(memIds[mi]);
                                if (mi < roles.size() && roles[mi] < strTab.size() && !strTab[roles[mi]].empty()) {
                                    memPreview += L" (" + PathUtils::Utf8ToWide(strTab[roles[mi]]) + L")";
                                }
                            }
                            relsSh.rows.push_back({ std::to_wstring(rid), std::to_wstring(memIds.size()), nameW, featW, memPreview, tagsW });
                        }
                    }
                }
            }
        }
    }

    if (blobCount == 0) return false;

    if (out.geoPoints.empty() && hasBbox) {
        Formats::AddGeoPoint(out, minLat, minLon, 0.0, -1, L"BBox SW", 4096);
        Formats::AddGeoPoint(out, maxLat, minLon, 0.0, -1, L"BBox NW", 4096);
        Formats::AddGeoPoint(out, maxLat, maxLon, 0.0, -1, L"BBox NE", 4096);
        Formats::AddGeoPoint(out, minLat, maxLon, 0.0, -1, L"BBox SE", 4096);
        Formats::AddGeoPoint(out, minLat, minLon, 0.0, -1, L"BBox Close", 4096);
    }

    SpreadsheetSheet sumSh;
    sumSh.name = L"OSM_Summary";
    sumSh.rows.push_back({ L"Property", L"Value" });
    if (!writingProg.empty()) sumSh.rows.push_back({ L"Writing Program", writingProg });
    if (!osmSource.empty())   sumSh.rows.push_back({ L"Source", osmSource });
    if (hasBbox) {
        sumSh.rows.push_back({ L"Bounding Box (Lon, Lat)",
            L"[" + fmtDeg(minLon) + L", " + fmtDeg(minLat) + L"] .. [" + fmtDeg(maxLon) + L", " + fmtDeg(maxLat) + L"]" });
    }
    if (!reqFeatures.empty()) sumSh.rows.push_back({ L"Required Features", reqFeatures });
    if (!optFeatures.empty()) sumSh.rows.push_back({ L"Optional Features", optFeatures });
    if (!lastCodec.empty())   sumSh.rows.push_back({ L"Blob Compression", lastCodec });
    sumSh.rows.push_back({ L"Decoded Blobs", std::to_wstring(blobCount) });
    sumSh.rows.push_back({ L"Total Nodes", std::to_wstring(totalNodes) });
    sumSh.rows.push_back({ L"Total Ways", std::to_wstring(totalWays) });
    sumSh.rows.push_back({ L"Total Relations", std::to_wstring(totalRelations) });

    FinalizeSheetCols(sumSh);
    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"OpenStreetMap Binary PBF Dataset (.pbf)";
    if (nodesSh.rows.size() > 1) { FinalizeSheetCols(nodesSh); out.sheets.push_back(std::move(nodesSh)); }
    if (waysSh.rows.size() > 1)  { FinalizeSheetCols(waysSh);  out.sheets.push_back(std::move(waysSh));  }
    if (relsSh.rows.size() > 1)  { FinalizeSheetCols(relsSh);  out.sheets.push_back(std::move(relsSh));  }
    out.sheets.push_back(std::move(sumSh));
    if (filePos >= fileSize && !source.ReadFailed()) out.sourceReadFully = true;
    out.valid = true;
    return true;
}

} // namespace DetailOsmPbf


inline bool ParseOsmPbfMapDoc(const std::vector<uint8_t>& raw, const std::wstring& ext, ParsedOfficeDocument& out) {
    // Check if .pbf is an OpenStreetMap Binary PBF stream ([uint32BE hdrLen][BlobHeader { type = "OSMHeader"/"OSMData" }])
    if (ext == L".pbf" && raw.size() >= 12) {
        uint32_t hdrLen = DetailDocs200::RdU32BE(raw.data());
        if (hdrLen >= 4 && hdrLen <= 65536 && 4u + hdrLen <= raw.size()) {
            std::string btype;
            uint32_t dataSize = 0;
            if (DetailOsmPbf::ParseOsmBlobHeader(raw.data() + 4, hdrLen, btype, dataSize) &&
                (btype == "OSMHeader" || btype == "OSMData")) {
                return DetailOsmPbf::ParseOsmPbfFile(raw, out);
            }
        }
    }

    return false;
}

} // namespace Formats
