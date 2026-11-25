#pragma once
// KmzArchiveDoc.h — Google Earth Zipped Keyhole Markup Language (.kmz) Reader

namespace Formats {

inline bool ParseKmzArchiveDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::vector<ZipDeflate::ArchiveItemMeta> entries;
    bool usedFile = false;
    if (!DetailStructuredDataDoc::ListZipPackage(raw, out, entries, usedFile)) return false;

    const ZipDeflate::ArchiveItemMeta* mainKml = nullptr;
    for (const auto& e : entries) {
        if (e.isDirectory || PathUtils::ToLower(PathUtils::GetExtension(e.pathInArchive)) != L".kml") continue;
        std::wstring lp = PathUtils::ToLower(e.pathInArchive);
        std::replace(lp.begin(), lp.end(), L'\\', L'/');
        if (lp == L"doc.kml" || lp == L"/doc.kml") { mainKml = &e; break; }
        if (!mainKml) mainKml = &e;
    }
    if (!mainKml) return false;

    std::vector<uint8_t> kmlBytes;
    if (!DetailStructuredDataDoc::ExtractZipPackageEntry(raw, out, usedFile, *mainKml, kmlBytes) || kmlBytes.empty()) {
        return false;
    }
    if (!ParseKmlGeography(kmlBytes, out)) return false;
    out.formatTitle = L"Google Earth Zipped KMZ (.kmz)";
    out.hasGeoTrack = (out.geoPoints.size() >= 2);
    if (usedFile) out.sourceReadFully = true;
    return true;
}

} // namespace Formats
