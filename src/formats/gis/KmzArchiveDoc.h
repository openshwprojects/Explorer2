#pragma once
// KmzArchiveDoc.h — Google Earth Zipped Keyhole Markup Language (.kmz) Reader

namespace Formats {

inline bool ParseKmzArchiveDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
        if (raw.size() < 22 || raw[0] != 'P' || raw[1] != 'K') return false;
        std::vector<ZipDeflate::ArchiveItemMeta> entries;
        if (!ZipDeflate::ListZipEntriesFromMemory(raw.data(), raw.size(), entries)) return false;
        const ZipDeflate::ArchiveItemMeta* mainKml = nullptr;
        for (const auto& e : entries) {
            if (e.isDirectory || PathUtils::ToLower(PathUtils::GetExtension(e.pathInArchive)) != L".kml") continue;
            if (PathUtils::ToLower(e.pathInArchive) == L"doc.kml") { mainKml = &e; break; }
            if (!mainKml) mainKml = &e;
        }
        if (!mainKml) return false;
        std::vector<uint8_t> kmlBytes;
        if (!ZipDeflate::ExtractZipEntryFromMemory(raw.data(), raw.size(), *mainKml, kmlBytes) || kmlBytes.empty()) return false;
        if (!ParseKmlGeography(kmlBytes, out)) return false;
        out.formatTitle = L"Google Earth Zipped KMZ (.kmz)";
        out.hasGeoTrack = (out.geoPoints.size() >= 2);
        return true;

}

} // namespace Formats
