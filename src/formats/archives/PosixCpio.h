#pragma once
// PosixCpio.h — POSIX SVR4 newc / crc & octal odc CPIO archive reader (.cpio — "070701", "070702", "070707").

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailArc200 {

// ============================================================================
// #33. POSIX CPIO Archive (.cpio — "070701" newc / "070702" crc / "070707" odc)
// ============================================================================
inline bool ParsePosixCpio(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (size < 76) return false;
    bool isNewc = (size >= 110 && (std::memcmp(data, "070701", 6) == 0 || std::memcmp(data, "070702", 6) == 0));
    bool isOdc  = (!isNewc && std::memcmp(data, "070707", 6) == 0);
    if (!isNewc && !isOdc) return false;

    outInfo.formatName = isNewc ? L"POSIX SVR4 CPIO Archive (.cpio)" : L"POSIX Octal CPIO Archive (.cpio)";
    size_t pos = 0;

    if (isNewc) {
        while (pos + 110 <= size && outInfo.entries.size() < 50000) {
            if (std::memcmp(data + pos, "070701", 6) != 0 && std::memcmp(data + pos, "070702", 6) != 0) break;
            auto hex8 = [&](size_t off) -> uint32_t {
                char b[9] = {};
                std::memcpy(b, data + pos + off, 8);
                return static_cast<uint32_t>(std::strtoul(b, nullptr, 16));
            };
            uint32_t mode     = hex8(14);
            uint32_t fSize    = hex8(54);
            uint32_t nameSize = hex8(94);
            uint32_t check    = hex8(102);
            if (nameSize == 0 || nameSize > 4096 || pos + 110 + nameSize > size) break;

            std::string fname(reinterpret_cast<const char*>(data + pos + 110), strnlen(reinterpret_cast<const char*>(data + pos + 110), nameSize));
            size_t dataStart = (pos + 110 + nameSize + 3u) & ~size_t(3u);
            if (fname == "TRAILER!!!") break;
            if (dataStart + fSize > size) break;

            if (!fname.empty() && fname != ".") {
                ArchiveItemEntry item;
                item.pathInArchive = ArchivePath::Normalize(PathUtils::Utf8ToWide(fname));
                item.name = ArchivePath::BaseName(item.pathInArchive);
                item.isDirectory = ((mode & 0170000u) == 0040000u);
                item.uncompressedSize = item.isDirectory ? 0 : fSize;
                item.compressedSize = item.uncompressedSize;
                item.crc32 = check;
                item.rawDataOffset = dataStart;
                item.compressionMethod = 0; // Stored raw!
                outInfo.entries.push_back(std::move(item));
            }
            pos = (dataStart + fSize + 3u) & ~size_t(3u);
        }
    } else {
        while (pos + 76 <= size && outInfo.entries.size() < 50000) {
            if (std::memcmp(data + pos, "070707", 6) != 0) break;
            auto oct = [&](size_t off, size_t len) -> uint64_t {
                char b[16] = {};
                std::memcpy(b, data + pos + off, len);
                return static_cast<uint64_t>(std::strtoull(b, nullptr, 8));
            };
            uint64_t mode     = oct(18, 6);
            uint64_t nameSize = oct(59, 6);
            uint64_t fSize    = oct(65, 11);
            if (nameSize == 0 || nameSize > 4096 || pos + 76 + nameSize > size) break;
            std::string fname(reinterpret_cast<const char*>(data + pos + 76), strnlen(reinterpret_cast<const char*>(data + pos + 76), static_cast<size_t>(nameSize)));
            if (fname == "TRAILER!!!") break;
            size_t dataStart = pos + 76 + static_cast<size_t>(nameSize);
            if (dataStart + fSize > size) break;
            if (!fname.empty() && fname != ".") {
                ArchiveItemEntry item;
                item.pathInArchive = ArchivePath::Normalize(PathUtils::Utf8ToWide(fname));
                item.name = ArchivePath::BaseName(item.pathInArchive);
                item.isDirectory = ((mode & 0170000u) == 0040000u);
                item.uncompressedSize = item.isDirectory ? 0 : fSize;
                item.compressedSize = item.uncompressedSize;
                item.rawDataOffset = dataStart;
                item.compressionMethod = 0;
                outInfo.entries.push_back(std::move(item));
            }
            pos = dataStart + static_cast<size_t>(fSize);
        }
    }
    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

} // namespace DetailArc200

inline bool InspectPosixCpio(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return DetailArc200::ParsePosixCpio(data, size, outInfo);
}

} // namespace Explorer2::Formats
