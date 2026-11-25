#pragma once
// PosixCpio.h — POSIX SVR4 newc / crc & octal odc CPIO archive reader & extractor (.cpio — "070701", "070702", "070707").
// Shared CPIO walker is reused by RedHatRpm.h for RPM payload streams.

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailUnixAndLegacyArc {

inline bool WalkOrExtractPosixCpioBuffer(
    const uint8_t* cpio,
    size_t cpioLen,
    bool directHostBuffer,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm = nullptr,
    std::vector<uint8_t>* outExtracted = nullptr)
{
    if (!cpio || cpioLen < 76) return false;
    const bool isNewc = (cpioLen >= 110 && (std::memcmp(cpio, "070701", 6) == 0 || std::memcmp(cpio, "070702", 6) == 0));
    const bool isOdc  = (!isNewc && std::memcmp(cpio, "070707", 6) == 0);
    if (!isNewc && !isOdc) return false;

    size_t pos = 0;
    size_t count = 0;
    if (isNewc) {
        while (pos <= cpioLen && cpioLen - pos >= 110u && count < 50000) {
            if (std::memcmp(cpio + pos, "070701", 6) != 0 && std::memcmp(cpio + pos, "070702", 6) != 0) break;
            auto hex8 = [&](size_t off) -> uint32_t {
                char b[9] = {};
                std::memcpy(b, cpio + pos + off, 8);
                return static_cast<uint32_t>(std::strtoul(b, nullptr, 16));
            };
            const uint32_t mode     = hex8(14);
            const uint32_t mtime    = hex8(46);
            const uint32_t fSize    = hex8(54);
            const uint32_t nameSize = hex8(94);
            const uint32_t check    = hex8(102);
            if (nameSize == 0 || nameSize > 4096 || nameSize > cpioLen - (pos + 110u)) break;

            std::string fname(
                reinterpret_cast<const char*>(cpio + pos + 110u),
                strnlen(reinterpret_cast<const char*>(cpio + pos + 110u), nameSize));
            const uint64_t dataStart64 = (static_cast<uint64_t>(pos) + 110ULL + nameSize + 3ULL) & ~3ULL;
            if (fname == "TRAILER!!!") break;
            if (dataStart64 > cpioLen || fSize > cpioLen - static_cast<size_t>(dataStart64)) break;
            const size_t dataStart = static_cast<size_t>(dataStart64);

            std::wstring norm = ArchivePath::Normalize(PathUtils::Utf8ToWide(fname));
            const bool isDir = ((mode & 0170000u) == 0040000u);
            if (!norm.empty() && norm != L".") {
                if (extractTargetNorm && outExtracted) {
                    if (!isDir && DetailDiskAndGameArc::PathEqualsCI(norm, *extractTargetNorm)) {
                        outExtracted->assign(cpio + dataStart, cpio + dataStart + fSize);
                        return true;
                    }
                } else if (outInfo) {
                    ArchiveItemEntry item;
                    item.pathInArchive     = std::move(norm);
                    item.name              = ArchivePath::BaseName(item.pathInArchive);
                    item.isDirectory       = isDir;
                    item.uncompressedSize  = isDir ? 0 : fSize;
                    item.compressedSize    = item.uncompressedSize;
                    item.crc32             = check;
                    item.rawDataOffset     = directHostBuffer ? dataStart : 0u;
                    item.compressionMethod = directHostBuffer ? 0u : 1u;
                    DetailDiskAndGameArc::UnixTimeU32ToDos(mtime, item.dosDate, item.dosTime);
                    outInfo->entries.push_back(std::move(item));
                }
                ++count;
            }
            const uint64_t nextPos64 = (dataStart64 + fSize + 3ULL) & ~3ULL;
            if (nextPos64 > cpioLen) break;
            pos = static_cast<size_t>(nextPos64);
        }
    } else {
        while (pos <= cpioLen && cpioLen - pos >= 76u && count < 50000) {
            if (std::memcmp(cpio + pos, "070707", 6) != 0) break;
            auto oct = [&](size_t off, size_t len) -> uint64_t {
                char b[16] = {};
                std::memcpy(b, cpio + pos + off, len);
                return static_cast<uint64_t>(std::strtoull(b, nullptr, 8));
            };
            const uint64_t mode     = oct(18, 6);
            const uint64_t mtime    = oct(48, 11);
            const uint64_t nameSize = oct(59, 6);
            const uint64_t fSize    = oct(65, 11);
            if (nameSize == 0 || nameSize > 4096 || nameSize > cpioLen - (pos + 76u)) break;

            std::string fname(
                reinterpret_cast<const char*>(cpio + pos + 76u),
                strnlen(reinterpret_cast<const char*>(cpio + pos + 76u), static_cast<size_t>(nameSize)));
            if (fname == "TRAILER!!!") break;
            const size_t dataStart = pos + 76u + static_cast<size_t>(nameSize);
            if (fSize > cpioLen - dataStart) break;

            std::wstring norm = ArchivePath::Normalize(PathUtils::Utf8ToWide(fname));
            const bool isDir = ((mode & 0170000u) == 0040000u);
            if (!norm.empty() && norm != L".") {
                if (extractTargetNorm && outExtracted) {
                    if (!isDir && DetailDiskAndGameArc::PathEqualsCI(norm, *extractTargetNorm)) {
                        outExtracted->assign(cpio + dataStart, cpio + dataStart + static_cast<size_t>(fSize));
                        return true;
                    }
                } else if (outInfo) {
                    ArchiveItemEntry item;
                    item.pathInArchive     = std::move(norm);
                    item.name              = ArchivePath::BaseName(item.pathInArchive);
                    item.isDirectory       = isDir;
                    item.uncompressedSize  = isDir ? 0 : fSize;
                    item.compressedSize    = item.uncompressedSize;
                    item.rawDataOffset     = directHostBuffer ? dataStart : 0u;
                    item.compressionMethod = directHostBuffer ? 0u : 1u;
                    DetailDiskAndGameArc::UnixTimeU32ToDos(static_cast<uint32_t>(mtime), item.dosDate, item.dosTime);
                    outInfo->entries.push_back(std::move(item));
                }
                ++count;
            }
            pos = dataStart + static_cast<size_t>(fSize);
        }
    }
    return extractTargetNorm ? false : (count > 0);
}

inline bool ParsePosixCpio(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (!data || size < 76) return false;
    const bool isNewc = (size >= 110 && (std::memcmp(data, "070701", 6) == 0 || std::memcmp(data, "070702", 6) == 0));
    const bool isOdc  = (!isNewc && std::memcmp(data, "070707", 6) == 0);
    if (!isNewc && !isOdc) return false;

    outInfo.formatName = isNewc ? L"POSIX SVR4 CPIO Archive (.cpio)" : L"POSIX Octal CPIO Archive (.cpio)";
    if (!WalkOrExtractPosixCpioBuffer(data, size, true, &outInfo, nullptr, nullptr)) return false;
    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

} // namespace DetailUnixAndLegacyArc

inline bool InspectPosixCpio(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return DetailUnixAndLegacyArc::ParsePosixCpio(data, size, outInfo);
}

inline bool ExtractPosixCpioEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    outBytes.clear();
    return DetailUnixAndLegacyArc::WalkOrExtractPosixCpioBuffer(data, size, true, nullptr, &normTarget, &outBytes);
}

} // namespace Explorer2::Formats
