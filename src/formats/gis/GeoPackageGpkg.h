#pragma once
// GeoPackageGpkg.h — OGC GeoPackage (.gpkg) SQLite Spatial Database Reader

namespace Formats {

inline bool ParseGeoPackageGpkg(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
        if (raw.size() < 100 || std::memcmp(raw.data(), "SQLite format 3\0", 16) != 0) return false;
        return ParseSqliteDatabase(raw, out, /*isGeoPackage=*/true);

}

} // namespace Formats
