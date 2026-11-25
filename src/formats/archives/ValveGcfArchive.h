#pragma once
// ValveGcfArchive.h — Valve Steam Legacy Grid Cache File v6 (0x00000001, 0x00000001, 0x00000006)
// Block Entry Table, Fragmentation Map, Directory Tree, and multi-cluster file extractor (.gcf).

#include "../common/ArchiveFormatCommon.h"
#include <unordered_set>

namespace Explorer2::Formats {

namespace DetailValveGcf {

struct GcfLayout {
    uint32_t minorVer = 0;
    uint32_t blockSize = 0;
    uint32_t blockCount = 0;
    uint32_t fragTerminator = 0xFFFFFFFFu;
    uint32_t dirItemCount = 0;
    uint32_t firstDataBlockOffset = 0;

    size_t blockEntriesOff = 0; // 28 * blockCount
    size_t fragMapOff      = 0; // 4 * blockCount
    size_t dirEntriesOff   = 0; // 28 * dirItemCount
    size_t dirNamesOff     = 0;
    size_t dirSectionEnd   = 0;
    size_t dirMapEntriesOff = 0; // 4 * dirItemCount
};

inline bool ParseGcfLayout(const uint8_t* data, size_t size, GcfLayout& out) {
    out = {};
    if (!data || size < 160u) return false;

    // 1. GCFHeader (44 bytes = 11 * u32)
    const uint32_t dummy0     = ByteIO::RdU32LE(data + 0u);
    const uint32_t majorVer   = ByteIO::RdU32LE(data + 4u);
    const uint32_t minorVer   = ByteIO::RdU32LE(data + 8u);
    const uint32_t blockSize  = ByteIO::RdU32LE(data + 32u);
    const uint32_t blockCount = ByteIO::RdU32LE(data + 36u);

    if (dummy0 != 1u || majorVer != 1u || (minorVer != 6u && minorVer != 5u)) return false;
    if (blockSize < 512u || blockSize > 1048576u || (blockSize & (blockSize - 1u)) != 0u) return false;
    if (blockCount > 2097152u) return false;

    // 2. GCFBlockEntryHeader (32 bytes at offset 44) + GCFBlockEntry[blockCount] (28 bytes each)
    size_t pos = 44u;
    if (size - pos < 32u) return false;
    if (ByteIO::RdU32LE(data + pos + 0u) != blockCount) return false;
    pos += 32u;

    const uint64_t blockEntriesBytes = static_cast<uint64_t>(blockCount) * 28ULL;
    if (blockEntriesBytes > size - pos) return false;
    out.blockEntriesOff = pos;
    pos += static_cast<size_t>(blockEntriesBytes);

    // 3. GCFFragmentationMapHeader (16 bytes) + uint32[blockCount]
    if (size - pos < 16u) return false;
    if (ByteIO::RdU32LE(data + pos + 0u) != blockCount) return false;
    const uint32_t termMode = ByteIO::RdU32LE(data + pos + 8u);
    out.fragTerminator = (termMode == 0u) ? 0x0000FFFFu : 0xFFFFFFFFu;
    pos += 16u;

    const uint64_t fragMapBytes = static_cast<uint64_t>(blockCount) * 4ULL;
    if (fragMapBytes > size - pos) return false;
    out.fragMapOff = pos;
    pos += static_cast<size_t>(fragMapBytes);

    // 4. GCFDirectoryHeader (28 bytes) + GCFDirectoryEntry[dirItemCount] (28 bytes) + String Pool + Info tables
    if (size - pos < 28u) return false;
    const size_t dirSectionStart = pos;
    const uint32_t itemCount     = ByteIO::RdU32LE(data + dirSectionStart + 12u);
    const uint32_t dirTotalBytes = ByteIO::RdU32LE(data + dirSectionStart + 24u);
    if (itemCount == 0u || itemCount > 524288u) return false;
    const uint64_t dirEntriesBytes = static_cast<uint64_t>(itemCount) * 28ULL;
    if (dirTotalBytes < 28ULL + dirEntriesBytes || dirTotalBytes > size - dirSectionStart) return false;

    out.dirItemCount  = itemCount;
    out.dirEntriesOff = dirSectionStart + 28u;
    out.dirNamesOff   = out.dirEntriesOff + static_cast<size_t>(dirEntriesBytes);
    out.dirSectionEnd = dirSectionStart + dirTotalBytes;
    pos = out.dirSectionEnd;

    // 5. GCFDirectoryMapHeader (8 bytes) + uint32[itemCount]
    if (size - pos < 8u) return false;
    pos += 8u;
    const uint64_t dirMapBytes = static_cast<uint64_t>(itemCount) * 4ULL;
    if (dirMapBytes > size - pos) return false;
    out.dirMapEntriesOff = pos;
    pos += static_cast<size_t>(dirMapBytes);

    // 6. GCFChecksumHeader (8 bytes: u32 dummy0, u32 checksumSize) + checksum body
    if (size - pos < 8u) return false;
    const uint32_t checksumBytes = ByteIO::RdU32LE(data + pos + 4u);
    pos += 8u;
    if (checksumBytes > size - pos) return false;
    pos += checksumBytes;

    // 7. GCFDataBlockHeader (24 bytes in v5/v6: [lastVer, blockCount, blockSize, firstBlockOffset, blocksUsed, checksum])
    if (size - pos < 24u) return false;
    const uint32_t dbCount = ByteIO::RdU32LE(data + pos + 4u);
    const uint32_t dbSize  = ByteIO::RdU32LE(data + pos + 8u);
    const uint32_t firstBlkOff = ByteIO::RdU32LE(data + pos + 12u);
    if (dbCount != blockCount || dbSize != blockSize) return false;
    if (blockCount > 0u && firstBlkOff > size) return false;

    out.minorVer             = minorVer;
    out.blockSize            = blockSize;
    out.blockCount           = blockCount;
    out.firstDataBlockOffset = firstBlkOff;
    return true;
}

inline std::string ReadGcfDirString(const uint8_t* data, const GcfLayout& lay, uint32_t nameOff) {
    if (nameOff >= lay.dirSectionEnd - lay.dirNamesOff) return {};
    const size_t absPos = lay.dirNamesOff + nameOff;
    const size_t maxLen = std::min<size_t>(lay.dirSectionEnd - absPos, 1024u);
    const size_t n = strnlen(reinterpret_cast<const char*>(data + absPos), maxLen);
    return std::string(reinterpret_cast<const char*>(data + absPos), n);
}

inline bool ExtractGcfFileByDirIndex(
    const uint8_t* data,
    size_t size,
    const GcfLayout& lay,
    uint32_t dirIdx,
    uint32_t fileSize,
    uint32_t dirFlags,
    std::vector<uint8_t>& outBytes)
{
    outBytes.clear();
    if ((dirFlags & 0x00000100u) != 0u) return false; // Encrypted GCF item
    if (fileSize == 0u) return true;
    if (fileSize > 128u * 1024u * 1024u) return false;
    if (lay.blockCount == 0u) return false;

    uint32_t firstBlockEntry = ByteIO::RdU32LE(data + lay.dirMapEntriesOff + static_cast<size_t>(dirIdx) * 4u);
    if (firstBlockEntry >= lay.blockCount) {
        // Fallback scan for BlockEntry referencing dirIdx at offset 0
        for (uint32_t b = 0; b < lay.blockCount; ++b) {
            const uint8_t* be = data + lay.blockEntriesOff + static_cast<size_t>(b) * 28u;
            if (ByteIO::RdU32LE(be + 24u) == dirIdx && ByteIO::RdU32LE(be + 4u) == 0u) {
                firstBlockEntry = b;
                break;
            }
        }
    }
    if (firstBlockEntry >= lay.blockCount) return false;

    outBytes.assign(fileSize, 0u);
    uint64_t totalWritten = 0;
    std::unordered_set<uint32_t> visitedBlockEntries;
    std::unordered_set<uint32_t> visitedClusters;

    uint32_t curBe = firstBlockEntry;
    while (curBe < lay.blockCount) {
        if (!visitedBlockEntries.insert(curBe).second) return false;

        const uint8_t* be = data + lay.blockEntriesOff + static_cast<size_t>(curBe) * 28u;
        const uint32_t entryFlags   = ByteIO::RdU32LE(be + 0u);
        const uint32_t fileDataOff  = ByteIO::RdU32LE(be + 4u);
        const uint32_t fileDataSize = ByteIO::RdU32LE(be + 8u);
        uint32_t       clusterIdx   = ByteIO::RdU32LE(be + 12u);
        const uint32_t nextBe       = ByteIO::RdU32LE(be + 16u);

        if ((entryFlags & 0x00008000u) == 0u && fileDataSize == 0u) break;
        if (fileDataOff > fileSize || fileDataSize > fileSize - fileDataOff) return false;

        uint32_t curPosInFile = fileDataOff;
        uint32_t remInEntry   = fileDataSize;

        while (remInEntry > 0u) {
            if (clusterIdx >= lay.blockCount || clusterIdx == lay.fragTerminator) return false;
            if (!visitedClusters.insert(clusterIdx).second) return false;

            const uint64_t physOff = static_cast<uint64_t>(lay.firstDataBlockOffset) +
                                     static_cast<uint64_t>(clusterIdx) * lay.blockSize;
            const uint32_t take = std::min<uint32_t>(lay.blockSize, remInEntry);
            if (physOff > size || take > size - static_cast<size_t>(physOff)) return false;

            std::memcpy(outBytes.data() + curPosInFile, data + static_cast<size_t>(physOff), take);
            curPosInFile += take;
            remInEntry   -= take;
            totalWritten += take;

            if (remInEntry > 0u) {
                clusterIdx = ByteIO::RdU32LE(data + lay.fragMapOff + static_cast<size_t>(clusterIdx) * 4u);
            }
        }

        curBe = nextBe;
    }

    if (totalWritten != fileSize) {
        outBytes.clear();
        return false;
    }
    return true;
}

inline bool ParseOrExtractValveGcf(
    const uint8_t* data,
    size_t size,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm,
    std::vector<uint8_t>* outExtracted)
{
    GcfLayout lay;
    if (!ParseGcfLayout(data, size, lay)) return false;

    if (outInfo) {
        outInfo->formatName = L"Valve Steam GCF v" + std::to_wstring(lay.minorVer) + L" (.gcf)";
    }

    const std::wstring wantNorm = extractTargetNorm ? ArchivePath::Normalize(*extractTargetNorm) : std::wstring{};
    std::unordered_set<uint32_t> visitedItems;
    size_t totalListed = 0;
    bool extractedOk = false;

    const uint8_t* rootEnt = data + lay.dirEntriesOff;
    const uint32_t rootFirstChild = ByteIO::RdU32LE(rootEnt + 24u);

    auto walkDir = [&](auto& self, uint32_t firstChildIdx, const std::wstring& prefix, int depth) -> bool {
        if (depth > 32 || totalListed >= 65536u) return true;
        uint32_t curIdx = firstChildIdx;
        while (curIdx != 0u && curIdx < lay.dirItemCount && totalListed < 65536u) {
            if (!visitedItems.insert(curIdx).second) return false; // Corrupt sibling/child cycle

            const uint8_t* de = data + lay.dirEntriesOff + static_cast<size_t>(curIdx) * 28u;
            const uint32_t nameOff   = ByteIO::RdU32LE(de + 0u);
            const uint32_t itemSize  = ByteIO::RdU32LE(de + 4u);
            const uint32_t dirFlags  = ByteIO::RdU32LE(de + 12u);
            const uint32_t nextIdx   = ByteIO::RdU32LE(de + 20u);
            const uint32_t childIdx  = ByteIO::RdU32LE(de + 24u);

            const std::string rawName = ReadGcfDirString(data, lay, nameOff);
            if (!rawName.empty()) {
                const std::wstring nameW = PathUtils::Utf8ToWide(rawName);
                const std::wstring fullRel = ArchivePath::Normalize(prefix.empty() ? nameW : (prefix + L"/" + nameW));
                const bool isFile = (dirFlags & 0x00004000u) != 0u;

                if (!fullRel.empty()) {
                    if (!isFile) {
                        if (outInfo && !extractTargetNorm) {
                            ArchiveItemEntry dirItem;
                            dirItem.pathInArchive     = fullRel;
                            dirItem.name              = ArchivePath::BaseName(fullRel);
                            dirItem.isDirectory       = true;
                            dirItem.compressionMethod = 0u;
                            outInfo->entries.push_back(std::move(dirItem));
                            ++totalListed;
                        }
                        if (childIdx != 0u && childIdx < lay.dirItemCount) {
                            if (!self(self, childIdx, fullRel, depth + 1)) return false;
                            if (extractedOk) return true;
                        }
                    } else {
                        if (extractTargetNorm && outExtracted) {
                            if (_wcsicmp(fullRel.c_str(), wantNorm.c_str()) == 0) {
                                extractedOk = ExtractGcfFileByDirIndex(data, size, lay, curIdx, itemSize, dirFlags, *outExtracted);
                                return extractedOk;
                            }
                        } else if (outInfo) {
                            const bool isEnc = (dirFlags & 0x00000100u) != 0u;
                            ArchiveItemEntry item;
                            item.pathInArchive     = fullRel;
                            item.name              = ArchivePath::BaseName(fullRel);
                            item.isDirectory       = false;
                            item.uncompressedSize  = itemSize;
                            item.compressedSize    = itemSize;
                            item.rawDataOffset     = 0ULL;
                            item.compressionMethod = isEnc ? 99u : 1u; // Fragmented cluster chain
                            outInfo->entries.push_back(std::move(item));
                            ++totalListed;
                        }
                    }
                }
            }

            curIdx = nextIdx;
        }
        return true;
    };

    if (rootFirstChild != 0u && rootFirstChild < lay.dirItemCount) {
        if (!walkDir(walkDir, rootFirstChild, L"", 0)) {
            if (outInfo) *outInfo = {};
            return false;
        }
    }

    if (extractTargetNorm) return extractedOk;
    if (!outInfo || outInfo->entries.empty()) return false;
    outInfo->valid = true;
    return true;
}

} // namespace DetailValveGcf

inline bool InspectValveGcfArchive(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return DetailValveGcf::ParseOrExtractValveGcf(data, size, &outInfo, nullptr, nullptr);
}

inline bool ExtractValveGcfEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    return DetailValveGcf::ParseOrExtractValveGcf(data, size, nullptr, &normTarget, &outBytes);
}

} // namespace Explorer2::Formats
