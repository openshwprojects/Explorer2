#pragma once
// PmTilesMap.h — Protomaps PMTiles v3 Cloud-Native Tile Archive (.pmtiles) Reader

namespace Formats {

namespace DetailDocs200 {

inline bool DecompressPmTilesStream(const uint8_t* src, size_t srcLen, uint8_t comp, std::vector<uint8_t>& out,
                                    size_t maxOut = 32u * 1024u * 1024u) {
    out.clear();
    if (!src || srcLen == 0) return false;
    if (comp == 1u) { // None
        if (srcLen > maxOut) return false;
        out.assign(src, src + srcLen);
        return true;
    }
    if (comp == 2u) { // Gzip
        return ZipDeflate::InflateGzip(src, srcLen, out, maxOut);
    }
    if (comp == 3u) { // Brotli
        return ::Explorer2::StreamCodecs::DecompressBrotliStream(src, srcLen, out, maxOut);
    }
    if (comp == 4u) { // Zstandard
        return ::Explorer2::StreamCodecs::DecompressZstdFrame(src, srcLen, out, maxOut);
    }
    return false;
}

// Exact PMTiles v3 inverse Hilbert curve: TileID -> (Z, X, Y)
inline bool PmTilesTileIdToZXY(uint64_t tileId, uint8_t& outZ, uint32_t& outX, uint32_t& outY) {
    uint64_t acc = 0;
    for (uint8_t z = 0; z <= 26; ++z) {
        uint64_t numTiles = 1ULL << (2u * z);
        if (tileId < acc + numTiles) {
            uint64_t t = tileId - acc;
            uint64_t n = 1ULL << z;
            uint64_t x = 0, y = 0;
            for (uint64_t s = 1; s < n; s <<= 1) {
                uint64_t rx = 1ULL & (t >> 1);
                uint64_t ry = 1ULL & (t ^ rx);
                if (ry == 0) {
                    if (rx == 1) {
                        x = (s - 1ULL) - x;
                        y = (s - 1ULL) - y;
                    }
                    std::swap(x, y);
                }
                x += s * rx;
                y += s * ry;
                t >>= 2;
            }
            outZ = z;
            outX = static_cast<uint32_t>(x);
            outY = static_cast<uint32_t>(y);
            return true;
        }
        acc += numTiles;
    }
    return false;
}

struct PmTilesDirEntry {
    uint64_t tileId = 0;
    uint32_t runLength = 0;
    uint32_t length = 0;
    uint64_t offset = 0;
};

inline bool DecodePmTilesDirectory(const uint8_t* dir, size_t dirSize, std::vector<PmTilesDirEntry>& outEntries) {
    outEntries.clear();
    if (!dir || dirSize == 0) return false;
    size_t pos = 0;
    uint64_t nEntries64 = 0;
    if (!ReadVarint64(dir, dirSize, pos, nEntries64) || nEntries64 > 500000ULL) return false;
    size_t nEntries = static_cast<size_t>(nEntries64);
    if (nEntries == 0) return pos == dirSize;

    outEntries.resize(nEntries);
    uint64_t lastId = 0;
    for (size_t i = 0; i < nEntries; ++i) {
        uint64_t delta = 0;
        if (!ReadVarint64(dir, dirSize, pos, delta)) return false;
        if (i > 0 && delta == 0) return false; // Tile IDs in PMTiles v3 directory are strictly ascending
        lastId += delta;
        outEntries[i].tileId = lastId;
    }
    for (size_t i = 0; i < nEntries; ++i) {
        uint64_t rl = 0;
        if (!ReadVarint64(dir, dirSize, pos, rl) || rl > 0xFFFFFFFFULL) return false;
        outEntries[i].runLength = static_cast<uint32_t>(rl);
    }
    for (size_t i = 0; i < nEntries; ++i) {
        uint64_t ln = 0;
        if (!ReadVarint64(dir, dirSize, pos, ln) || ln == 0 || ln > 0xFFFFFFFFULL) return false;
        outEntries[i].length = static_cast<uint32_t>(ln);
    }
    for (size_t i = 0; i < nEntries; ++i) {
        uint64_t rawOff = 0;
        if (!ReadVarint64(dir, dirSize, pos, rawOff)) return false;
        if (i > 0 && rawOff == 0) {
            outEntries[i].offset = outEntries[i - 1].offset + outEntries[i - 1].length;
        } else {
            if (rawOff == 0) return false;
            outEntries[i].offset = rawOff - 1ULL;
        }
    }
    return true;
}

} // namespace DetailDocs200


inline bool ParsePmTilesMapDoc(const std::vector<uint8_t>& raw, const std::wstring& ext, ParsedOfficeDocument& out) {
    // PMTiles v3 ("PMTiles\x03", 127-byte header)
    if ((raw.size() >= 127 && std::memcmp(raw.data(), "PMTiles\x03", 8) == 0) || ext == L".pmtiles") {
        if (raw.size() < 127 || std::memcmp(raw.data(), "PMTiles\x03", 8) != 0) return false;
        SourceBytes source(raw, out);
        const uint64_t fileSize = source.Size();
        const uint8_t* h = raw.data();

        uint64_t rootDirOff     = DetailDocs200::RdU64LE(h + 8);
        uint64_t rootDirLen     = DetailDocs200::RdU64LE(h + 16);
        uint64_t jsonOff        = DetailDocs200::RdU64LE(h + 24);
        uint64_t jsonLen        = DetailDocs200::RdU64LE(h + 32);
        uint64_t leafDirsOff    = DetailDocs200::RdU64LE(h + 40);
        uint64_t leafDirsLen    = DetailDocs200::RdU64LE(h + 48);
        uint64_t tileDataOff    = DetailDocs200::RdU64LE(h + 56);
        uint64_t tileDataLen    = DetailDocs200::RdU64LE(h + 64);
        uint64_t addressedTiles = DetailDocs200::RdU64LE(h + 72);
        uint64_t tileEntries    = DetailDocs200::RdU64LE(h + 80);
        uint64_t tileContents   = DetailDocs200::RdU64LE(h + 88);
        uint8_t clustered       = h[96];
        uint8_t internalComp    = h[97];
        uint8_t tileComp        = h[98];
        uint8_t tileType        = h[99];
        uint8_t minZoom         = h[100];
        uint8_t maxZoom         = h[101];
        double minLon = DetailDocs200::RdI32LE(h + 102) * 1e-7;
        double minLat = DetailDocs200::RdI32LE(h + 106) * 1e-7;
        double maxLon = DetailDocs200::RdI32LE(h + 110) * 1e-7;
        double maxLat = DetailDocs200::RdI32LE(h + 114) * 1e-7;
        uint8_t centerZoom = h[118];
        double centerLon = DetailDocs200::RdI32LE(h + 119) * 1e-7;
        double centerLat = DetailDocs200::RdI32LE(h + 123) * 1e-7;

        if (internalComp > 4u || minZoom > maxZoom || maxZoom > 30u) return false;
        const bool headerOnlyEmptyDir = (rootDirOff == 0ULL && rootDirLen == 0ULL && internalComp == 0u && tileEntries == 0ULL);

        std::vector<uint8_t> rootRawBuf, rootDec;
        std::vector<DetailDocs200::PmTilesDirEntry> dirEntries;
        if (!headerOnlyEmptyDir) {
            if (internalComp < 1u || rootDirOff < 127ULL || rootDirLen == 0 || rootDirLen > 32ULL * 1024ULL * 1024ULL ||
                rootDirOff > fileSize || rootDirLen > fileSize - rootDirOff) {
                return false;
            }
            // Decompress & decode PMTiles v3 Root Directory
            const uint8_t* rootRaw = source.Read(rootDirOff, static_cast<size_t>(rootDirLen), rootRawBuf);
            if (!rootRaw || !DetailDocs200::DecompressPmTilesStream(rootRaw, static_cast<size_t>(rootDirLen), internalComp, rootDec)) {
                return false;
            }
            if (!DetailDocs200::DecodePmTilesDirectory(rootDec.data(), rootDec.size(), dirEntries)) {
                return false;
            }
            if (dirEntries.empty() && tileEntries > 0) return false;
        }

        auto compLabel = [](uint8_t c) -> const wchar_t* {
            switch (c) {
                case 1: return L"None";
                case 2: return L"Gzip";
                case 3: return L"Brotli";
                case 4: return L"Zstandard";
                default: return L"Unknown";
            }
        };
        const wchar_t* tTypeName = (tileType == 1) ? L"MVT (Mapbox Vector Tile)" :
                                   (tileType == 2) ? L"PNG Raster" :
                                   (tileType == 3) ? L"JPEG Raster" :
                                   (tileType == 4) ? L"WebP Raster" :
                                   (tileType == 5) ? L"AVIF Raster" : L"Unknown";

        auto fmtCoord = [](double c) {
            wchar_t b[32] = {};
            swprintf_s(b, L"%.6f", c);
            return std::wstring(b);
        };

        SpreadsheetSheet sh;
        sh.name = L"PMTiles_Header";
        sh.rows.push_back({ L"Property", L"Value", L"Latitude", L"Longitude" });
        sh.rows.push_back({ L"SW Corner", L"Bounding Box SW", fmtCoord(minLat), fmtCoord(minLon) });
        sh.rows.push_back({ L"NW Corner", L"Bounding Box NW", fmtCoord(maxLat), fmtCoord(minLon) });
        sh.rows.push_back({ L"NE Corner", L"Bounding Box NE", fmtCoord(maxLat), fmtCoord(maxLon) });
        sh.rows.push_back({ L"SE Corner", L"Bounding Box SE", fmtCoord(minLat), fmtCoord(maxLon) });
        sh.rows.push_back({ L"SW Close",  L"Bounding Box Close", fmtCoord(minLat), fmtCoord(minLon) });
        sh.rows.push_back({ L"Map Center (z" + std::to_wstring(centerZoom) + L")", tTypeName, fmtCoord(centerLat), fmtCoord(centerLon) });
        sh.rows.push_back({ L"Zoom Range", L"z" + std::to_wstring(minZoom) + L" .. z" + std::to_wstring(maxZoom), L"", L"" });
        sh.rows.push_back({ L"Internal Compression", compLabel(internalComp), L"", L"" });
        sh.rows.push_back({ L"Tile Compression", compLabel(tileComp), L"", L"" });
        sh.rows.push_back({ L"Tile Payload Type", tTypeName, L"", L"" });
        sh.rows.push_back({ L"Clustered", clustered ? L"Yes" : L"No", L"", L"" });
        sh.rows.push_back({ L"Addressed Tiles", std::to_wstring(addressedTiles), L"", L"" });
        sh.rows.push_back({ L"Unique Tile Blobs", std::to_wstring(tileContents) + L" (" + std::to_wstring(tileEntries) + L" Hilbert runs)", L"", L"" });
        sh.rows.push_back({ L"Root Directory Entries", std::to_wstring(dirEntries.size()) + L" (" + PathUtils::FormatByteSize(rootDec.size()) + L" decompressed)", L"", L"" });
        if (leafDirsLen > 0) {
            sh.rows.push_back({ L"Leaf Directories Span", L"@" + std::to_wstring(leafDirsOff) + L" (" + PathUtils::FormatByteSize(leafDirsLen) + L")", L"", L"" });
        }
        sh.rows.push_back({ L"Tile Data Span", L"@" + std::to_wstring(tileDataOff) + L" (" + PathUtils::FormatByteSize(tileDataLen) + L")", L"", L"" });

        // Populate interactive Direct2D Map View bounds + center
        if (std::fabs(minLat) <= 90.0 && std::fabs(maxLat) <= 90.0 &&
            std::fabs(minLon) <= 180.0 && std::fabs(maxLon) <= 180.0) {
            Formats::AddGeoPoint(out, minLat, minLon, 0.0, 1, L"SW Corner", 4096);
            Formats::AddGeoPoint(out, maxLat, minLon, 0.0, 2, L"NW Corner", 4096);
            Formats::AddGeoPoint(out, maxLat, maxLon, 0.0, 3, L"NE Corner", 4096);
            Formats::AddGeoPoint(out, minLat, maxLon, 0.0, 4, L"SE Corner", 4096);
            Formats::AddGeoPoint(out, minLat, minLon, 0.0, 5, L"SW Close",  4096);
        }
        if (std::fabs(centerLat) <= 90.0 && std::fabs(centerLon) <= 180.0) {
            Formats::AddGeoPoint(out, centerLat, centerLon, 0.0, 6, L"Center (z" + std::to_wstring(centerZoom) + L")", 4096);
        }

        // Decompress & parse JSON Metadata + vector_layers
        SpreadsheetSheet vlSh;
        vlSh.name = L"Vector_Layers";
        vlSh.rows.push_back({ L"Layer ID", L"Zoom Range", L"Description", L"Fields" });

        if (jsonLen > 0 && jsonLen <= 32ULL * 1024ULL * 1024ULL && jsonOff <= fileSize && jsonLen <= fileSize - jsonOff) {
            std::vector<uint8_t> metaRawBuf, metaDec;
            const uint8_t* metaRaw = source.Read(jsonOff, static_cast<size_t>(jsonLen), metaRawBuf);
            if (metaRaw && DetailDocs200::DecompressPmTilesStream(metaRaw, static_cast<size_t>(jsonLen), internalComp, metaDec)) {
                std::string_view jsv(reinterpret_cast<const char*>(metaDec.data()), metaDec.size());
                ::Explorer2::Json::Value jroot;
                if (::Explorer2::Json::ParseDocument(jsv, jroot) && jroot.IsObject()) {
                    auto addMetaStr = [&](std::string_view key, const wchar_t* label) {
                        std::string v = jroot.GetString(key);
                        if (!v.empty()) sh.rows.push_back({ label, PathUtils::Utf8ToWide(v), L"", L"" });
                    };
                    addMetaStr("name", L"Metadata Name");
                    addMetaStr("description", L"Metadata Description");
                    addMetaStr("attribution", L"Attribution");
                    addMetaStr("version", L"Dataset Version");
                    addMetaStr("type", L"Layer Overlay Type");
                    addMetaStr("format", L"Metadata Format");

                    if (const auto* vl = jroot.Find("vector_layers"); vl && vl->IsArray()) {
                        for (const auto& item : vl->arrVal) {
                            if (!item.IsObject() || vlSh.rows.size() > 1000) continue;
                            std::wstring lid = PathUtils::Utf8ToWide(item.GetString("id"));
                            std::wstring ldesc = PathUtils::Utf8ToWide(item.GetString("description"));
                            int lmin = item.GetInt("minzoom", -1);
                            int lmax = item.GetInt("maxzoom", -1);
                            std::wstring lz = (lmin >= 0 || lmax >= 0)
                                ? (L"z" + (lmin >= 0 ? std::to_wstring(lmin) : L"?") + L"..z" + (lmax >= 0 ? std::to_wstring(lmax) : L"?"))
                                : L"";
                            std::wstring fieldsDesc;
                            if (const auto* fobj = item.Find("fields"); fobj && fobj->IsObject()) {
                                for (size_t fi = 0; fi < fobj->objVal.size(); ++fi) {
                                    if (fi > 0) fieldsDesc += L", ";
                                    fieldsDesc += PathUtils::Utf8ToWide(fobj->objVal[fi].first);
                                    if (fobj->objVal[fi].second.IsString() && !fobj->objVal[fi].second.strVal.empty()) {
                                        fieldsDesc += L": " + PathUtils::Utf8ToWide(fobj->objVal[fi].second.strVal);
                                    }
                                    if (fieldsDesc.size() > 240) { fieldsDesc += L"..."; break; }
                                }
                            }
                            vlSh.rows.push_back({ lid, lz, ldesc, fieldsDesc });
                        }
                    }
                } else if (DetailDocs200::IsMostlyPrintableUtf8(metaDec.data(), std::min<size_t>(metaDec.size(), 512u))) {
                    sh.rows.push_back({ L"JSON Metadata", PathUtils::Utf8ToWide(jsv.substr(0, std::min<size_t>(jsv.size(), 512u))), L"", L"" });
                }
            }
        }

        // Build Tile_Directory sheet from decoded Hilbert Root Directory
        SpreadsheetSheet dirSh;
        dirSh.name = L"Tile_Directory";
        dirSh.rows.push_back({ L"Entry #", L"Tile ID", L"Z/X/Y", L"Kind", L"Run Length", L"Offset", L"Bytes" });
        const size_t maxDirRows = std::min<size_t>(dirEntries.size(), 2000u);
        for (size_t i = 0; i < maxDirRows; ++i) {
            const auto& de = dirEntries[i];
            uint8_t z = 0; uint32_t x = 0, y = 0;
            std::wstring zxyStr = DetailDocs200::PmTilesTileIdToZXY(de.tileId, z, x, y)
                ? (std::to_wstring(z) + L"/" + std::to_wstring(x) + L"/" + std::to_wstring(y))
                : L"(invalid)";
            bool isLeaf = (de.runLength == 0);
            uint64_t absOff = (isLeaf ? leafDirsOff : tileDataOff) + de.offset;
            dirSh.rows.push_back({
                std::to_wstring(i + 1),
                std::to_wstring(de.tileId),
                zxyStr,
                isLeaf ? L"LeafDir" : L"Tile",
                std::to_wstring(de.runLength),
                std::to_wstring(de.offset) + L" (@" + std::to_wstring(absOff) + L")",
                std::to_wstring(de.length)
            });
        }

        FinalizeSheetCols(sh);
        FinalizeSheetCols(dirSh);
        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"Protomaps PMTiles v3 Cloud-Native Tile Archive (.pmtiles)";
        out.sheets.push_back(std::move(sh));
        out.sheets.push_back(std::move(dirSh));
        if (vlSh.rows.size() > 1) {
            FinalizeSheetCols(vlSh);
            out.sheets.push_back(std::move(vlSh));
        }
        out.sourceReadFully = !source.ReadFailed();
        out.valid = true;
        return true;
    }

    return false;
}

} // namespace Formats
