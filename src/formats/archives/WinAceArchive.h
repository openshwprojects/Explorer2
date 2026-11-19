#pragma once
// WinAceArchive.h — WinACE archive reader (.ace — "**ACE**").

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailArc200 {

inline bool ParseWinAceArchive(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    // #38. WinACE Archive — Magic "**ACE**" at offset 7
    if (size >= 32 && std::memcmp(data + 7, "**ACE**", 7) == 0) {
        outInfo.formatName = L"WinACE Archive (.ace)";
        size_t pos = 0;
        while (pos + 7 <= size && outInfo.entries.size() < 40000) {
            uint16_t hdrSz   = RdU16LE(data + pos + 2);
            if (hdrSz < 3 || pos + 4 + hdrSz > size) break;
            uint8_t  hdrType = data[pos + 4];
            uint16_t hdrFlgs = RdU16LE(data + pos + 5);
            uint64_t addSz   = (hdrFlgs & 0x0001u) ? RdU32LE(data + pos + 7) : 0;

            if (hdrType == 1 && hdrSz >= 31) { // File block
                const uint8_t* fh = data + pos + 4;
                uint32_t packSz  = RdU32LE(fh + 3);
                uint32_t origSz  = RdU32LE(fh + 7);
                uint32_t dosTime = RdU32LE(fh + 11);
                uint32_t attr    = RdU32LE(fh + 15);
                uint32_t crc32   = RdU32LE(fh + 19);
                uint8_t  compTp  = fh[23];
                uint16_t fnLen   = RdU16LE(fh + 29);
                if (31 + fnLen <= hdrSz) {
                    std::string fname(reinterpret_cast<const char*>(fh + 31), fnLen);
                    ArchiveItemEntry item;
                    item.pathInArchive = ArchivePath::Normalize(PathUtils::Utf8ToWide(fname));
                    item.name = ArchivePath::BaseName(item.pathInArchive);
                    item.isDirectory = (attr & 0x10u) != 0;
                    item.compressedSize = packSz;
                    item.uncompressedSize = origSz;
                    item.crc32 = crc32;
                    item.dosTime = static_cast<uint16_t>(dosTime & 0xFFFF);
                    item.dosDate = static_cast<uint16_t>((dosTime >> 16) & 0xFFFF);
                    item.rawDataOffset = pos + 4 + hdrSz;
                    item.compressionMethod = (compTp == 0) ? 0 : compTp;
                    outInfo.entries.push_back(std::move(item));
                }
            }
            size_t step = 4 + hdrSz + static_cast<size_t>(addSz);
            if (step == 0 || pos + step > size) break;
            pos += step;
        }
        outInfo.valid = !outInfo.entries.empty();
        return outInfo.valid;
    }
    return false;
}

} // namespace DetailArc200

inline bool InspectWinAceArchive(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return DetailArc200::ParseWinAceArchive(data, size, outInfo);
}

} // namespace Explorer2::Formats
