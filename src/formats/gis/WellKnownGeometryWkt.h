#pragma once
// WellKnownGeometryWkt.h — OGC ISO 13249-3 Well-Known Text & Well-Known Binary / PostGIS EWKB (.wkt, .wkb) Reader

namespace Formats {
namespace DetailWktWkb {

struct GeomFeature {
    std::wstring encoding;
    std::wstring geomType;
    uint32_t srid = 0;
    bool hasZ = false;
    bool hasM = false;
    size_t parts = 0;
    size_t vertices = 0;
    double minX = 0.0, maxX = 0.0, minY = 0.0, maxY = 0.0;
    bool hasBounds = false;
    std::wstring preview;
};

struct CoordRow {
    size_t featIdx = 0;
    size_t partIdx = 0;
    size_t vertIdx = 0;
    double x = 0.0, y = 0.0, z = 0.0;
    bool hasZ = false;
};

inline const wchar_t* BaseWkbTypeName(uint32_t bt) {
    switch (bt) {
        case 1: return L"POINT";
        case 2: return L"LINESTRING";
        case 3: return L"POLYGON";
        case 4: return L"MULTIPOINT";
        case 5: return L"MULTILINESTRING";
        case 6: return L"MULTIPOLYGON";
        case 7: return L"GEOMETRYCOLLECTION";
        default: return nullptr;
    }
}

inline void RecordVertex(GeomFeature& f, std::vector<CoordRow>& coords, size_t featIdx, size_t partIdx, size_t vertIdx,
                         double x, double y, double z, bool hasZ) {
    if (!std::isfinite(x) || !std::isfinite(y)) return;
    ++f.vertices;
    if (!f.hasBounds) {
        f.minX = f.maxX = x;
        f.minY = f.maxY = y;
        f.hasBounds = true;
    } else {
        f.minX = std::min(f.minX, x); f.maxX = std::max(f.maxX, x);
        f.minY = std::min(f.minY, y); f.maxY = std::max(f.maxY, y);
    }
    if (coords.size() < 3000) {
        coords.push_back({ featIdx, partIdx, vertIdx, x, y, z, hasZ });
    }
}

inline bool DecodeWkbRec(
    const uint8_t* d,
    size_t sz,
    size_t& pos,
    GeomFeature& rootFeat,
    std::vector<CoordRow>& coords,
    size_t featIdx,
    int depth)
{
    if (depth > 24 || pos + 5 > sz) return false;
    uint8_t bo = d[pos++];
    if (bo != 0 && bo != 1) return false;
    const bool be = (bo == 0);
    auto rdU32 = [&](const uint8_t* p) { return be ? GeoSciDetail::RdU32BE(p) : GeoSciDetail::RdU32LE(p); };
    auto rdF64 = [&](const uint8_t* p) { return be ? GeoSciDetail::RdF64BE(p) : GeoSciDetail::RdF64LE(p); };

    uint32_t rawType = rdU32(d + pos);
    pos += 4;

    bool hasZ = (rawType & 0x80000000u) != 0;
    bool hasM = (rawType & 0x40000000u) != 0;
    bool hasSrid = (rawType & 0x20000000u) != 0;
    uint32_t lowType = rawType & 0x1FFFFFFFu;
    uint32_t baseType = lowType;
    if (lowType >= 3000 && lowType < 4000) { hasZ = true; hasM = true; baseType = lowType - 3000; }
    else if (lowType >= 2000 && lowType < 3000) { hasM = true; baseType = lowType - 2000; }
    else if (lowType >= 1000 && lowType < 2000) { hasZ = true; baseType = lowType - 1000; }

    const wchar_t* tname = BaseWkbTypeName(baseType);
    if (!tname) return false;

    uint32_t srid = 0;
    if (hasSrid) {
        if (pos + 4 > sz) return false;
        srid = rdU32(d + pos);
        pos += 4;
    }

    if (depth == 0) {
        rootFeat.geomType = tname;
        if (hasZ && hasM) rootFeat.geomType += L" ZM";
        else if (hasZ)    rootFeat.geomType += L" Z";
        else if (hasM)    rootFeat.geomType += L" M";
        rootFeat.hasZ = hasZ;
        rootFeat.hasM = hasM;
        rootFeat.srid = srid;
    }

    const size_t dims = 2u + (hasZ ? 1u : 0u) + (hasM ? 1u : 0u);
    const size_t ptBytes = dims * 8u;

    auto readPointSeq = [&](uint32_t nPts, size_t partNo) -> bool {
        if (static_cast<uint64_t>(nPts) * ptBytes > sz - pos) return false;
        for (uint32_t i = 0; i < nPts; ++i) {
            double x = rdF64(d + pos);
            double y = rdF64(d + pos + 8);
            double z = hasZ ? rdF64(d + pos + 16) : 0.0;
            pos += ptBytes;
            RecordVertex(rootFeat, coords, featIdx, partNo, i + 1, x, y, z, hasZ);
        }
        return true;
    };

    switch (baseType) {
        case 1: // POINT
            ++rootFeat.parts;
            return readPointSeq(1, rootFeat.parts);
        case 2: { // LINESTRING
            if (pos + 4 > sz) return false;
            uint32_t nPts = rdU32(d + pos);
            pos += 4;
            ++rootFeat.parts;
            return readPointSeq(nPts, rootFeat.parts);
        }
        case 3: { // POLYGON
            if (pos + 4 > sz) return false;
            uint32_t nRings = rdU32(d + pos);
            pos += 4;
            if (nRings > 1000000) return false;
            for (uint32_t r = 0; r < nRings; ++r) {
                if (pos + 4 > sz) return false;
                uint32_t nPts = rdU32(d + pos);
                pos += 4;
                ++rootFeat.parts;
                if (!readPointSeq(nPts, rootFeat.parts)) return false;
            }
            return true;
        }
        case 4:
        case 5:
        case 6:
        case 7: {
            if (pos + 4 > sz) return false;
            uint32_t nSub = rdU32(d + pos);
            pos += 4;
            if (nSub > 1000000) return false;
            for (uint32_t i = 0; i < nSub; ++i) {
                if (!DecodeWkbRec(d, sz, pos, rootFeat, coords, featIdx, depth + 1)) return false;
            }
            return true;
        }
        default:
            return false;
    }
}

inline int HexNib(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

inline bool TryDecodeHexWkbLine(std::string_view line, std::vector<uint8_t>& outBytes) {
    outBytes.clear();
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1);
    while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r')) line.remove_suffix(1);
    if (line.size() >= 2 && line[0] == '\\' && (line[1] == 'x' || line[1] == 'X')) line.remove_prefix(2);
    else if (line.size() >= 2 && line[0] == '0' && (line[1] == 'x' || line[1] == 'X') && line.size() > 12) {
        // Note: "01010000..." starts with "01", not "0x", whereas "\\x0101..." or "0x0101..." has 'x'
        line.remove_prefix(2);
    }
    if (line.size() < 10 || (line.size() & 1u) != 0) return false;
    outBytes.reserve(line.size() / 2);
    for (size_t i = 0; i < line.size(); i += 2) {
        int hi = HexNib(line[i]), lo = HexNib(line[i + 1]);
        if (hi < 0 || lo < 0) return false;
        outBytes.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return outBytes.size() >= 5 && (outBytes[0] == 0 || outBytes[0] == 1);
}

struct WktScanner {
    std::string_view sv;
    size_t pos = 0;

    void SkipWs() {
        while (pos < sv.size() && (sv[pos] == ' ' || sv[pos] == '\t' || sv[pos] == '\r' || sv[pos] == '\n')) ++pos;
    }

    std::string ReadWordUpper() {
        SkipWs();
        size_t s = pos;
        while (pos < sv.size() && ((sv[pos] >= 'A' && sv[pos] <= 'Z') || (sv[pos] >= 'a' && sv[pos] <= 'z'))) ++pos;
        std::string w(sv.substr(s, pos - s));
        for (char& c : w) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        return w;
    }

    bool ConsumeChar(char expect) {
        SkipWs();
        if (pos < sv.size() && sv[pos] == expect) { ++pos; return true; }
        return false;
    }

    bool ReadDouble(double& outV) {
        SkipWs();
        if (pos >= sv.size()) return false;
        const char* start = sv.data() + pos;
        char* endPtr = nullptr;
        std::string tmp(sv.substr(pos, std::min<size_t>(64, sv.size() - pos)));
        double v = std::strtod(tmp.c_str(), &endPtr);
        if (endPtr == tmp.c_str()) return false;
        pos += static_cast<size_t>(endPtr - tmp.c_str());
        (void)start;
        outV = v;
        return true;
    }

    bool ParseCoordTuple(GeomFeature& f, std::vector<CoordRow>& coords, size_t featIdx, size_t partIdx, size_t vertIdx) {
        double x = 0.0, y = 0.0, z = 0.0, m = 0.0;
        if (!ReadDouble(x) || !ReadDouble(y)) return false;
        bool gotZ = false;
        SkipWs();
        if (pos < sv.size() && sv[pos] != ',' && sv[pos] != ')') {
            if (ReadDouble(z)) {
                gotZ = true;
                SkipWs();
                if (pos < sv.size() && sv[pos] != ',' && sv[pos] != ')') {
                    ReadDouble(m);
                    f.hasM = true;
                }
            }
        }
        if (gotZ && !f.hasZ && !f.hasM) f.hasZ = true;
        RecordVertex(f, coords, featIdx, partIdx, vertIdx, x, y, z, f.hasZ || gotZ);
        return true;
    }

    bool ParseCoordListInParens(GeomFeature& f, std::vector<CoordRow>& coords, size_t featIdx) {
        if (!ConsumeChar('(')) return false;
        ++f.parts;
        size_t partNo = f.parts;
        size_t vIdx = 0;
        for (;;) {
            if (!ParseCoordTuple(f, coords, featIdx, partNo, ++vIdx)) return false;
            if (ConsumeChar(',')) continue;
            if (ConsumeChar(')')) return true;
            return false;
        }
    }

    bool ParsePolygonBody(GeomFeature& f, std::vector<CoordRow>& coords, size_t featIdx) {
        if (!ConsumeChar('(')) return false;
        for (;;) {
            if (!ParseCoordListInParens(f, coords, featIdx)) return false;
            if (ConsumeChar(',')) continue;
            if (ConsumeChar(')')) return true;
            return false;
        }
    }

    bool CheckEmpty() {
        size_t save = pos;
        if (ReadWordUpper() == "EMPTY") return true;
        pos = save;
        return false;
    }

    bool ParseGeometry(GeomFeature& f, std::vector<CoordRow>& coords, size_t featIdx, int depth) {
        if (depth > 24) return false;
        SkipWs();
        // Optional PostGIS EWKT "SRID=4326;"
        if (depth == 0 && pos + 5 < sv.size()) {
            size_t save = pos;
            if (ReadWordUpper() == "SRID" && ConsumeChar('=')) {
                double s = 0.0;
                if (ReadDouble(s) && ConsumeChar(';')) {
                    f.srid = static_cast<uint32_t>(std::max(0.0, s));
                    f.encoding = L"EWKT";
                } else {
                    pos = save;
                }
            } else {
                pos = save;
            }
        }

        std::string gtype = ReadWordUpper();
        if (gtype != "POINT" && gtype != "LINESTRING" && gtype != "POLYGON" &&
            gtype != "MULTIPOINT" && gtype != "MULTILINESTRING" &&
            gtype != "MULTIPOLYGON" && gtype != "GEOMETRYCOLLECTION") {
            return false;
        }

        // Optional Z / M / ZM modifier
        size_t modSave = pos;
        std::string mod = ReadWordUpper();
        if (mod == "Z") f.hasZ = true;
        else if (mod == "M") f.hasM = true;
        else if (mod == "ZM") { f.hasZ = true; f.hasM = true; }
        else pos = modSave;

        if (depth == 0) f.geomType = PathUtils::Utf8ToWide(gtype);
        if (CheckEmpty()) return true;

        if (gtype == "POINT") {
            return ParseCoordListInParens(f, coords, featIdx);
        }
        if (gtype == "LINESTRING") {
            return ParseCoordListInParens(f, coords, featIdx);
        }
        if (gtype == "POLYGON") {
            return ParsePolygonBody(f, coords, featIdx);
        }
        if (gtype == "MULTIPOINT") {
            if (!ConsumeChar('(')) return false;
            for (;;) {
                SkipWs();
                if (pos < sv.size() && sv[pos] == '(') {
                    if (!ParseCoordListInParens(f, coords, featIdx)) return false;
                } else {
                    ++f.parts;
                    if (!ParseCoordTuple(f, coords, featIdx, f.parts, 1)) return false;
                }
                if (ConsumeChar(',')) continue;
                if (ConsumeChar(')')) return true;
                return false;
            }
        }
        if (gtype == "MULTILINESTRING") {
            return ParsePolygonBody(f, coords, featIdx);
        }
        if (gtype == "MULTIPOLYGON") {
            if (!ConsumeChar('(')) return false;
            for (;;) {
                if (!ParsePolygonBody(f, coords, featIdx)) return false;
                if (ConsumeChar(',')) continue;
                if (ConsumeChar(')')) return true;
                return false;
            }
        }
        if (gtype == "GEOMETRYCOLLECTION") {
            if (!ConsumeChar('(')) return false;
            for (;;) {
                if (!ParseGeometry(f, coords, featIdx, depth + 1)) return false;
                if (ConsumeChar(',')) continue;
                if (ConsumeChar(')')) return true;
                return false;
            }
        }
        return false;
    }
};

} // namespace DetailWktWkb

inline bool ParseWellKnownGeometryDoc(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    if (raw.size() < 5) return false;

    std::vector<DetailWktWkb::GeomFeature> features;
    std::vector<DetailWktWkb::CoordRow> coords;
    bool isBinaryStream = false;

    // 1. Binary WKB / EWKB stream (starts with endian byte 0x00 or 0x01)
    if (raw[0] == 0x00u || raw[0] == 0x01u) {
        size_t pos = 0;
        while (pos + 5 <= raw.size() && features.size() < 2000) {
            const size_t coordsBefore = coords.size();
            DetailWktWkb::GeomFeature f;
            f.encoding = (raw[pos] == 0x00u) ? L"WKB (XDR Big-Endian)" : L"WKB (NDR Little-Endian)";
            if (!DetailWktWkb::DecodeWkbRec(raw.data(), raw.size(), pos, f, coords, features.size() + 1, 0)) {
                coords.resize(coordsBefore);
                break;
            }
            if (f.srid != 0) f.encoding = L"EWKB (" + std::wstring(f.encoding.find(L"Big") != std::wstring::npos ? L"BE" : L"LE") + L")";
            features.push_back(std::move(f));
        }
        if (!features.empty()) isBinaryStream = true;
    }

    // 2. Text WKT / EWKT or Hex-encoded WKB / EWKB
    if (features.empty()) {
        coords.clear();
        std::string_view sv;
        if (!Formats::FormatPrologue::PrepareUtf8TextNoNul(raw.data(), raw.size(), sv, 5u)) return false;

        // First check line-by-line Hex-WKB
        size_t lp = 0;
        bool anyHexTried = false;
        while (lp < sv.size() && features.size() < 2000) {
            size_t eol = sv.find('\n', lp);
            std::string_view ln = (eol == std::string_view::npos) ? sv.substr(lp) : sv.substr(lp, eol - lp);
            lp = (eol == std::string_view::npos) ? sv.size() : (eol + 1);
            while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t' || ln.front() == '\r')) ln.remove_prefix(1);
            if (ln.empty() || ln[0] == '#' || (ln.size() >= 2 && ln[0] == '-' && ln[1] == '-')) continue;

            std::vector<uint8_t> wkbBytes;
            if (DetailWktWkb::TryDecodeHexWkbLine(ln, wkbBytes)) {
                anyHexTried = true;
                const size_t coordsBefore = coords.size();
                size_t bpos = 0;
                DetailWktWkb::GeomFeature f;
                f.encoding = L"Hex-WKB";
                if (DetailWktWkb::DecodeWkbRec(wkbBytes.data(), wkbBytes.size(), bpos, f, coords, features.size() + 1, 0) && bpos == wkbBytes.size()) {
                    if (f.srid != 0) f.encoding = L"Hex-EWKB";
                    f.preview = PathUtils::Utf8ToWide(ln.substr(0, std::min<size_t>(ln.size(), 96))) + (ln.size() > 96 ? L"..." : L"");
                    features.push_back(std::move(f));
                } else {
                    coords.resize(coordsBefore);
                }
            } else if (!anyHexTried) {
                break;
            }
        }

        // Otherwise parse WKT / EWKT geometries
        if (features.empty()) {
            coords.clear();
            DetailWktWkb::WktScanner sc{ sv, 0 };
            while (sc.pos < sc.sv.size() && features.size() < 2000) {
                sc.SkipWs();
                if (sc.pos >= sc.sv.size()) break;
                if (sc.sv[sc.pos] == '#' || (sc.pos + 1 < sc.sv.size() && sc.sv[sc.pos] == '-' && sc.sv[sc.pos + 1] == '-')) {
                    while (sc.pos < sc.sv.size() && sc.sv[sc.pos] != '\n') ++sc.pos;
                    continue;
                }
                if (sc.sv[sc.pos] == ';') { ++sc.pos; continue; }

                const size_t startOff = sc.pos;
                const size_t coordsBefore = coords.size();
                DetailWktWkb::GeomFeature f;
                f.encoding = L"WKT";
                if (!sc.ParseGeometry(f, coords, features.size() + 1, 0)) {
                    coords.resize(coordsBefore);
                    if (features.empty()) return false;
                    break;
                }
                const size_t spanLen = (sc.pos >= startOff) ? (sc.pos - startOff) : 0u;
                std::string_view span = sc.sv.substr(startOff, std::min<size_t>(spanLen, 120));
                f.preview = PathUtils::Trim(PathUtils::Utf8ToWide(span));
                if (spanLen > 120) f.preview += L"...";
                features.push_back(std::move(f));
                sc.ConsumeChar(';');
            }
        }
    }

    if (features.empty()) return false;

    SpreadsheetSheet featSh;
    featSh.name = L"WKT_Geometries";
    featSh.rows.push_back({ L"#", L"Encoding", L"Geometry Type", L"SRID", L"Dims", L"Parts / Rings", L"Vertices", L"Bounds (X, Y)", L"Preview" });
    for (size_t i = 0; i < features.size(); ++i) {
        const auto& f = features[i];
        std::wstring dims = L"2D (XY)";
        if (f.hasZ && f.hasM) dims = L"4D (XYZM)";
        else if (f.hasZ) dims = L"3D (XYZ)";
        else if (f.hasM) dims = L"3D (XYM)";

        std::wstring bbox = L"EMPTY";
        if (f.hasBounds) {
            bbox = L"[" + GeoSciDetail::FmtCoord(f.minX) + L", " + GeoSciDetail::FmtCoord(f.minY) + L"] .. [" +
                   GeoSciDetail::FmtCoord(f.maxX) + L", " + GeoSciDetail::FmtCoord(f.maxY) + L"]";
        }
        featSh.rows.push_back({
            std::to_wstring(i + 1),
            f.encoding,
            f.geomType,
            f.srid ? std::to_wstring(f.srid) : L"(unspecified)",
            dims,
            std::to_wstring(f.parts),
            std::to_wstring(f.vertices),
            bbox,
            f.preview.empty() ? f.geomType : f.preview
        });
    }
    FinalizeSheetCols(featSh);

    SpreadsheetSheet ptSh;
    ptSh.name = L"WKT_Coordinates";
    ptSh.rows.push_back({ L"Feature #", L"Part / Ring", L"Vertex #", L"Longitude / X", L"Latitude / Y", L"Elevation / Z" });
    for (size_t i = 0; i < coords.size(); ++i) {
        const auto& c = coords[i];
        int rowIdx = static_cast<int>(ptSh.rows.size());
        ptSh.rows.push_back({
            std::to_wstring(c.featIdx),
            std::to_wstring(c.partIdx),
            std::to_wstring(c.vertIdx),
            GeoSciDetail::FmtCoord(c.x),
            GeoSciDetail::FmtCoord(c.y),
            c.hasZ ? GeoSciDetail::FmtNum(c.z, 4) : L""
        });

        if (c.featIdx == 0 || c.featIdx > features.size()) continue;
        const auto& owner = features[c.featIdx - 1];
        const bool isGeographic = (owner.srid == 0 || owner.srid == 4326 || owner.srid == 4269 || owner.srid == 4979);
        if (isGeographic && std::fabs(c.y) <= 90.0 && std::fabs(c.x) <= 180.0) {
            Formats::AddGeoPoint(out, c.y, c.x, c.hasZ ? c.z : 0.0, rowIdx,
                owner.geomType + L" #" + std::to_wstring(c.featIdx), 8192);
        }
    }
    FinalizeSheetCols(ptSh);

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = isBinaryStream
        ? (L"OGC Well-Known Binary Geometry (" + extLower + L")")
        : (L"OGC Well-Known Text Geometry (" + extLower + L")");
    out.sheets.push_back(std::move(featSh));
    if (ptSh.rows.size() > 1) out.sheets.push_back(std::move(ptSh));
    if (!out.geoPoints.empty()) out.hasGeoTrack = true;
    out.valid = true;
    return true;
}

} // namespace Formats
