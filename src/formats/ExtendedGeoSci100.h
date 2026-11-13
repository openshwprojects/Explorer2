#pragma once
// ExtendedGeoSci100.h — 7 GIS Vector, Garmin FIT, Zipped KML, NetCDF, GRIB2 & NIfTI Neuroimaging Formats (70–76)
// Covers: .shp/.shx, .gpkg, .fit (Garmin), .kmz, .cdf (NetCDF), .grib/.grib2, .nii/.nii.gz

namespace Formats {

inline bool ParseExtendedGeoSciDoc(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    // 70. ESRI Shapefile (.shp / .shx — magic big-endian 0x0000270A = 9994)
    if (extLower == L".shp" || extLower == L".shx" ||
        (raw.size() >= 100 && DetailSys100::RdU32BE(raw.data()) == 9994u))
    {
        uint32_t shapeType = (raw.size() >= 36) ? DetailSys100::RdU32LE(raw.data() + 32) : 0;
        auto rdF64LE = [&](size_t off) -> double {
            if (off + 8 > raw.size()) return 0.0;
            double d = 0.0; std::memcpy(&d, raw.data() + off, 8); return d;
        };
        double xmin = rdF64LE(36), ymin = rdF64LE(44), xmax = rdF64LE(52), ymax = rdF64LE(60);

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"ESRI Vector Shapefile (.shp)";

        SpreadsheetSheet sh;
        sh.name = L"Shapefile_Header";
        sh.maxCols = 4;
        sh.rows.push_back({ L"Property", L"Value", L"Latitude", L"Longitude" });
        sh.rows.push_back({ L"File Code", L"9994 (0x0000270A)", std::to_wstring(ymin), std::to_wstring(xmin) });
        sh.rows.push_back({ L"Shape Geometry Type", std::to_wstring(shapeType), std::to_wstring(ymax), std::to_wstring(xmax) });
        out.sheets.push_back(std::move(sh));
        out.valid = true;
        return true;
    }

    // 72. Garmin FIT Activity (.fit — ".FIT" at offset 8)
    if ((extLower == L".fit" && (raw.size() < 80 || std::memcmp(raw.data(), "SIMPLE", 6) != 0)) ||
        (raw.size() >= 14 && std::memcmp(raw.data() + 8, ".FIT", 4) == 0))
    {
        uint8_t hdrSize  = raw.empty() ? 14 : raw[0];
        uint8_t protoVer = raw.size() >= 2 ? raw[1] : 0x20;
        uint32_t dataLen = raw.size() >= 8 ? DetailSys100::RdU32LE(raw.data() + 4) : 0;

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"Garmin FIT Telemetry Activity (.fit)";
        SpreadsheetSheet sh;
        sh.name = L"Garmin_FIT";
        sh.maxCols = 2;
        sh.rows.push_back({ L"Garmin FIT Field", L"Value" });
        sh.rows.push_back({ L"Header Size", std::to_wstring(hdrSize) + L" B" });
        sh.rows.push_back({ L"Protocol Version", std::to_wstring(protoVer) });
        sh.rows.push_back({ L"Data Records Size", PathUtils::FormatByteSize(dataLen) });
        out.sheets.push_back(std::move(sh));
        out.valid = true;
        return true;
    }

    // 73. Zipped Keyhole Markup Language (.kmz)
    if (extLower == L".kmz" && raw.size() >= 4 && raw[0] == 'P' && raw[1] == 'K') {
        std::vector<ZipDeflate::ArchiveItemMeta> entries;
        if (ZipDeflate::ListZipEntriesFromMemory(raw.data(), raw.size(), entries)) {
            for (const auto& e : entries) {
                if (PathUtils::GetExtension(e.pathInArchive) == L".kml") {
                    std::vector<uint8_t> kmlBytes;
                    if (ZipDeflate::ExtractZipEntryFromMemory(raw.data(), raw.size(), e, kmlBytes) && !kmlBytes.empty()) {
                        return ParseKmlGeography(kmlBytes, out);
                    }
                }
            }
        }
    }

    // 71, 74, 75, 76. GeoPackage (.gpkg), NetCDF (.cdf), WMO Weather (.grib/.grib2), NIfTI Neuroimaging (.nii)
    if (extLower == L".gpkg" || extLower == L".cdf" || extLower == L".grib" || extLower == L".grib2" || extLower == L".nii" ||
        (raw.size() >= 4 && (std::memcmp(raw.data(), "CDF\x01", 4) == 0 || std::memcmp(raw.data(), "CDF\x02", 4) == 0 || std::memcmp(raw.data(), "GRIB", 4) == 0)))
    {
        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        std::wstring title = (extLower == L".gpkg") ? L"OGC GeoPackage Spatial Database (.gpkg)" :
                             (extLower == L".grib" || extLower == L".grib2") ? L"WMO GRIB Meteorological Forecast Grid" :
                             (extLower == L".nii") ? L"NIfTI-1 3D/4D Neuroimaging Volume (.nii)" : L"NetCDF Scientific Array Dataset (.cdf)";
        out.formatTitle = title;

        SpreadsheetSheet sh;
        sh.name = L"Scientific_Dataset";
        sh.maxCols = 2;
        sh.rows.push_back({ L"Dataset Attribute", L"Value" });
        sh.rows.push_back({ L"Format", title });
        sh.rows.push_back({ L"Total File Size", PathUtils::FormatByteSize(raw.size()) });
        if (extLower == L".nii" && raw.size() >= 348) {
            int16_t dim0 = static_cast<int16_t>(DetailSys100::RdU16LE(raw.data() + 40));
            int16_t dimX = static_cast<int16_t>(DetailSys100::RdU16LE(raw.data() + 42));
            int16_t dimY = static_cast<int16_t>(DetailSys100::RdU16LE(raw.data() + 44));
            int16_t dimZ = static_cast<int16_t>(DetailSys100::RdU16LE(raw.data() + 46));
            sh.rows.push_back({ L"Voxel Grid Dimensions", std::to_wstring(dimX) + L" \xD7 " + std::to_wstring(dimY) + L" \xD7 " + std::to_wstring(dimZ) + L" (" + std::to_wstring(dim0) + L"D)" });
        }
        out.sheets.push_back(std::move(sh));
        out.valid = true;
        return true;
    }

    return false;
}

} // namespace Formats
