#pragma once
// Heroes3LodArchive.h — New World Computing Heroes of Might and Magic III "LOD\0" Archive (.lod)
// Parses 92-byte (`0x5C`) header + 32-byte directory records, supporting both raw stored lumps
// and RFC 1950 zlib-compressed lumps (`ZipDeflate::InflateZlib`).

#include "../common/GamePakArchiveCommon.h"

namespace Explorer2::Formats {

inline bool InspectHeroes3LodArchive(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (!data || size < 124u) return false;
    if (std::memcmp(data, "LOD\0", 4) != 0) return false;

    const uint32_t totalFiles = ByteIO::RdU32LE(data + 8u);
    if (totalFiles == 0u || totalFiles > 65536u) return false;

    const uint64_t dirOff = 0x5CULL;
    const uint64_t dirEnd = dirOff + static_cast<uint64_t>(totalFiles) * 32ULL;
    if (dirEnd > size) return false;

    outInfo = {};
    outInfo.formatName = L"Heroes of Might and Magic III LOD Archive (.lod)";
    outInfo.entries.reserve(totalFiles);

    for (uint32_t i = 0u; i < totalFiles; ++i) {
        const uint8_t* ent = data + static_cast<size_t>(dirOff) + static_cast<size_t>(i) * 32u;
        std::string rawName;
        if (!FormatPrologue::ReadPrintableFixedAscii(ent + 0x00u, 16u, rawName, true) || rawName.empty()) {
            outInfo = {};
            return false;
        }
        if (!GamePakCommon::IsSafeSegmentAscii(rawName)) {
            outInfo = {};
            return false;
        }

        const uint32_t fOff     = ByteIO::RdU32LE(ent + 0x10u);
        const uint32_t uncompSz = ByteIO::RdU32LE(ent + 0x14u);
        const uint32_t compSz   = ByteIO::RdU32LE(ent + 0x1Cu);
        const uint32_t storedSz = (compSz > 0u) ? compSz : uncompSz;

        if (fOff < dirEnd || fOff > size || storedSz > size - fOff || uncompSz > (256u << 20)) {
            outInfo = {};
            return false;
        }

        ArchiveItemEntry item{};
        item.pathInArchive     = ArchivePath::Normalize(PathUtils::Utf8ToWide(rawName));
        item.name              = ArchivePath::BaseName(item.pathInArchive);
        item.isDirectory       = false;
        item.uncompressedSize  = uncompSz;
        item.compressedSize    = storedSz;
        item.rawDataOffset     = fOff;
        item.compressionMethod = (compSz > 0u) ? 8u : 0u;
        outInfo.entries.push_back(std::move(item));
    }

    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

inline bool ExtractHeroes3LodEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    outBytes.clear();
    ArchiveSummaryInfo info{};
    if (!InspectHeroes3LodArchive(data, size, info) || !info.valid) return false;

    for (const auto& e : info.entries) {
        if (!e.isDirectory && DetailDiskAndGameArc::PathEqualsCI(e.pathInArchive, normTarget)) {
            if (e.rawDataOffset > size || e.compressedSize > size - e.rawDataOffset) return false;
            const uint8_t* src = data + static_cast<size_t>(e.rawDataOffset);
            if (e.compressionMethod == 0u) {
                outBytes.assign(src, src + static_cast<size_t>(e.uncompressedSize));
                return true;
            }
            if (!ZipDeflate::InflateZlib(
                    src,
                    static_cast<size_t>(e.compressedSize),
                    outBytes,
                    static_cast<size_t>(e.uncompressedSize) + 64u)) {
                outBytes.clear();
                return false;
            }
            return outBytes.size() == e.uncompressedSize;
        }
    }
    return false;
}

} // namespace Explorer2::Formats
