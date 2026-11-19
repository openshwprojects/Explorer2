#pragma once
// CabinetMscf.h — Microsoft Cabinet archive reader & extractor (.cab — "MSCF").

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailArc100 {

// ============================================================================
// 24. Microsoft Cabinet Archive (.cab — "MSCF")
// ============================================================================
inline bool ParseCabinetMscf(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo, const std::wstring& prefix = L"") {
    if (size < 36 || std::memcmp(data, "MSCF\0\0\0\0", 8) != 0) return false;
    if (prefix.empty()) outInfo.formatName = L"MS Cabinet (CAB)";
    uint32_t cbCabinet = RdU32LE(data + 8);
    uint32_t coffFiles = RdU32LE(data + 16);
    uint16_t cFiles    = RdU16LE(data + 28);
    if (coffFiles >= size || cFiles == 0) return false;

    uint64_t totalUnp = 0;
    size_t scanPos = coffFiles;
    for (uint16_t i = 0; i < cFiles && scanPos + 16 < size; ++i) {
        totalUnp += RdU32LE(data + scanPos);
        scanPos += 16;
        while (scanPos < size && data[scanPos] != 0) ++scanPos;
        if (scanPos >= size) break;
        ++scanPos;
    }
    uint64_t payloadPool = (cbCabinet > coffFiles && cbCabinet <= size) ? (cbCabinet - coffFiles) : (size - coffFiles);

    size_t pos = coffFiles;
    size_t added = 0;
    for (uint16_t i = 0; i < cFiles && pos + 16 < size; ++i) {
        uint32_t cbFile = RdU32LE(data + pos + 0);
        uint16_t date   = RdU16LE(data + pos + 10);
        uint16_t time   = RdU16LE(data + pos + 12);
        uint16_t attribs= RdU16LE(data + pos + 14);
        pos += 16;

        size_t nameEnd = pos;
        while (nameEnd < size && data[nameEnd] != 0) ++nameEnd;
        if (nameEnd >= size) break;
        std::string rawName(reinterpret_cast<const char*>(data + pos), nameEnd - pos);
        pos = nameEnd + 1;

        std::wstring wpath = ArchivePath::Normalize((attribs & 0x80u)
            ? PathUtils::Utf8ToWide(rawName)
            : PathUtils::Utf8ToWide(rawName));
        if (!wpath.empty()) {
            std::wstring full = prefix.empty() ? wpath : ArchivePath::Normalize(prefix + L"/" + wpath);
            ArchiveItemEntry item;
            item.pathInArchive = full;
            item.name = ArchivePath::BaseName(full);
            item.uncompressedSize = cbFile;
            item.compressedSize = (totalUnp > 0) ? ((payloadPool * cbFile) / totalUnp) : cbFile;
            item.dosDate = date;
            item.dosTime = time;
            item.isDirectory = (attribs & 0x10u) != 0;
            item.compressionMethod = 1; // Extracted via SetupIterateCabinetW
            outInfo.entries.push_back(std::move(item));
            ++added;
        }
    }
    if (prefix.empty()) outInfo.valid = (added > 0);
    return added > 0;
}

} // namespace DetailArc100

inline bool InspectCabinetMscf(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return DetailArc100::ParseCabinetMscf(data, size, outInfo);
}

inline bool ExtractCabinetMscfEntry(
    const std::wstring& archivePath,
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    if (size >= 36 && std::memcmp(data, "MSCF\0\0\0\0", 8) == 0) {
        return StreamCodecs::ExtractCabEntryFromFile(archivePath, normTarget, outBytes);
    }
    return false;
}

} // namespace Explorer2::Formats
