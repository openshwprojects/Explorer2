#pragma once
// ScsPrismArchive.h — SCS Software Prism3D HashFS archive reader & zlib extractor (.scs — "SCS#").

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailArc200 {

// ============================================================================
// #30. SCS Software Prism3D HashFS Archive (.scs — "SCS#")
// ============================================================================
inline bool ParseScsPrismArchive(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (size < 20 || std::memcmp(data, "SCS#", 4) != 0) return false;
    uint16_t ver = RdU16LE(data + 4);
    outInfo.formatName = L"SCS Prism3D HashFS v" + std::to_wstring(ver) + L" (.scs)";

    uint32_t numEntries = RdU32LE(data + 12);
    uint64_t startOff   = RdU32LE(data + 16);
    if (ver == 1 && startOff + static_cast<uint64_t>(numEntries) * 32 <= size) {
        for (uint32_t i = 0; i < numEntries && i < 30000; ++i) {
            const uint8_t* e = data + startOff + static_cast<size_t>(i) * 32;
            uint64_t hash   = RdU64LE(e + 0);
            uint64_t fOff   = RdU64LE(e + 8);
            uint32_t flags  = RdU32LE(e + 16);
            uint32_t crc    = RdU32LE(e + 20);
            uint32_t origSz = RdU32LE(e + 24);
            uint32_t compSz = RdU32LE(e + 28);

            wchar_t nameBuf[64] = {};
            swprintf_s(nameBuf, (flags & 1u) ? L"dir_%016llx" : L"asset_%016llx.bin", static_cast<unsigned long long>(hash));
            ArchiveItemEntry item;
            item.pathInArchive = nameBuf;
            item.name = nameBuf;
            item.isDirectory = (flags & 1u) != 0;
            item.crc32 = crc;
            item.uncompressedSize = origSz;
            item.compressedSize = compSz;
            item.rawDataOffset = fOff;
            item.compressionMethod = (flags & 2u) ? 8 : 0;
            outInfo.entries.push_back(std::move(item));
        }
    }
    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

} // namespace DetailArc200

inline bool InspectScsPrismArchive(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return DetailArc200::ParseScsPrismArchive(data, size, outInfo);
}

inline bool ExtractScsPrismArchiveEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& want,
    std::vector<uint8_t>& outBytes)
{
    if (size >= 20 && std::memcmp(data, "SCS#", 4) == 0) {
        ArchiveSummaryInfo info;
        if (DetailArc200::ParseScsPrismArchive(data, size, info)) {
            for (const auto& e : info.entries) {
                if (!e.isDirectory && _wcsicmp(e.pathInArchive.c_str(), want.c_str()) == 0) {
                    if (e.rawDataOffset + e.compressedSize > size || e.uncompressedSize > 256 * 1024 * 1024) return false;
                    if (e.compressionMethod == 0) {
                        if (e.rawDataOffset + e.uncompressedSize > size) return false;
                        outBytes.assign(
                            data + static_cast<size_t>(e.rawDataOffset),
                            data + static_cast<size_t>(e.rawDataOffset + e.uncompressedSize));
                        return true;
                    }
                    if (e.compressionMethod == 8) {
                        return ZipDeflate::InflateZlib(
                                   data + static_cast<size_t>(e.rawDataOffset),
                                   static_cast<size_t>(e.compressedSize),
                                   outBytes,
                                   static_cast<size_t>(e.uncompressedSize) + 64) ||
                               ZipDeflate::InflateRaw(
                                   data + static_cast<size_t>(e.rawDataOffset),
                                   static_cast<size_t>(e.compressedSize),
                                   outBytes,
                                   static_cast<size_t>(e.uncompressedSize) + 64);
                    }
                    return false;
                }
            }
        }
        return false;
    }
    return false;
}

} // namespace Explorer2::Formats
