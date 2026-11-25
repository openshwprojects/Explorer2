#pragma once
// RomFsFilesystem.h — Linux ROMFS v1 ("-rom1fs-") big-endian filesystem reader & in-memory extractor (.romfs).

#include "../common/ArchiveFormatCommon.h"
#include <unordered_set>

namespace Explorer2::Formats {

namespace DetailRomFs {

inline bool VerifyRomFsHeaderChecksum(const uint8_t* data, size_t size, uint32_t fullSize) {
    if (!data || size < 16u || fullSize < 16u) return false;
    const size_t chkBytes = std::min<size_t>({ 512u, size, static_cast<size_t>(fullSize) });
    uint32_t sum = 0;
    size_t i = 0;
    for (; i + 4u <= chkBytes; i += 4u) {
        sum += ByteIO::RdU32BE(data + i);
    }
    if (i < chkBytes) {
        uint8_t tail[4] = {};
        for (size_t k = 0; i + k < chkBytes && k < 4u; ++k) tail[k] = data[i + k];
        sum += ByteIO::RdU32BE(tail);
    }
    return sum == 0u;
}

inline bool ReadRomFsPaddedString(const uint8_t* data, size_t fsLim, size_t strOff, std::string& outStr, size_t& outAlignedEnd) {
    outStr.clear();
    outAlignedEnd = 0;
    if (strOff >= fsLim) return false;
    size_t nulPos = strOff;
    while (nulPos < fsLim && data[nulPos] != 0u && nulPos - strOff < 1024u) {
        ++nulPos;
    }
    if (nulPos >= fsLim || data[nulPos] != 0u) return false;
    outStr.assign(reinterpret_cast<const char*>(data + strOff), nulPos - strOff);
    const size_t rawEnd = nulPos + 1u;
    if (rawEnd > fsLim - 15u && ((rawEnd + 15u) & ~size_t(15u)) > fsLim) {
        outAlignedEnd = fsLim;
    } else {
        outAlignedEnd = (rawEnd + 15u) & ~size_t(15u);
    }
    return outAlignedEnd <= fsLim;
}

inline bool ResolveRomFsInodePayload(
    const uint8_t* data,
    size_t fsLim,
    uint32_t inodeOff,
    uint32_t& outType,
    size_t& outDataOff,
    uint32_t& outFileSize)
{
    uint32_t cur = inodeOff & ~0x0Fu;
    for (int hop = 0; hop < 8; ++hop) {
        if (cur < 16u || cur > fsLim || fsLim - cur < 16u) return false;
        const uint32_t nextFh   = ByteIO::RdU32BE(data + cur + 0u);
        const uint32_t specInfo = ByteIO::RdU32BE(data + cur + 4u);
        const uint32_t fSize    = ByteIO::RdU32BE(data + cur + 8u);
        const uint32_t fType    = nextFh & 0x07u;

        std::string dummyName;
        size_t dataOff = 0;
        if (!ReadRomFsPaddedString(data, fsLim, cur + 16u, dummyName, dataOff)) return false;

        if (fType == 0u) { // Hard link -> follow specInfo target inode
            const uint32_t target = specInfo & ~0x0Fu;
            if (target == 0u || target == cur) return false;
            cur = target;
            continue;
        }
        outType     = fType;
        outDataOff  = dataOff;
        outFileSize = fSize;
        return true;
    }
    return false;
}

inline bool ParseOrExtractRomFs(
    const uint8_t* data,
    size_t size,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm,
    std::vector<uint8_t>* outExtracted)
{
    if (!data || size < 32u || std::memcmp(data, "-rom1fs-", 8) != 0) return false;

    const uint32_t fullSize = ByteIO::RdU32BE(data + 8u);
    if (fullSize < 32u || fullSize > size) return false;
    if (!VerifyRomFsHeaderChecksum(data, size, fullSize)) return false;

    const size_t fsLim = static_cast<size_t>(fullSize);
    std::string volName;
    size_t rootFirstInode = 0;
    if (!ReadRomFsPaddedString(data, fsLim, 16u, volName, rootFirstInode)) return false;

    if (outInfo) {
        std::wstring fmt = L"Linux ROMFS v1";
        if (!volName.empty()) {
            fmt += L" (" + PathUtils::Utf8ToWide(volName) + L")";
        }
        outInfo->formatName = std::move(fmt);
    }

    const std::wstring wantNorm = extractTargetNorm ? ArchivePath::Normalize(*extractTargetNorm) : std::wstring{};
    std::unordered_set<uint32_t> visitedDirs;
    size_t totalListed = 0;
    bool extractedOk = false;

    auto walkDir = [&](auto& self, uint32_t firstChildOff, const std::wstring& prefix, int depth) -> bool {
        if (depth > 32 || totalListed >= 50000u || firstChildOff == 0u) return true;
        const uint32_t dirStart = firstChildOff & ~0x0Fu;
        if (!visitedDirs.insert(dirStart).second) return true;

        std::unordered_set<uint32_t> visitedSiblings;
        uint32_t cur = dirStart;

        while (cur != 0u && totalListed < 50000u) {
            if ((cur & 0x0Fu) != 0u || cur < rootFirstInode || cur > fsLim || fsLim - cur < 16u) return false;
            if (!visitedSiblings.insert(cur).second) break;

            const uint32_t nextFh   = ByteIO::RdU32BE(data + cur + 0u);
            const uint32_t specInfo = ByteIO::RdU32BE(data + cur + 4u);
            const uint32_t fileSize = ByteIO::RdU32BE(data + cur + 8u);
            const uint32_t nextOff  = nextFh & ~0x0Fu;
            const uint32_t fileType = nextFh & 0x07u;

            std::string entryUtf8;
            size_t dataOff = 0;
            if (!ReadRomFsPaddedString(data, fsLim, cur + 16u, entryUtf8, dataOff)) return false;

            if (!entryUtf8.empty() && entryUtf8 != "." && entryUtf8 != "..") {
                const std::wstring childW = PathUtils::Utf8ToWide(entryUtf8);
                const std::wstring fullRel = ArchivePath::Normalize(prefix.empty() ? childW : (prefix + L"/" + childW));
                if (!fullRel.empty()) {
                    if (fileType == 1u) { // Directory
                        if (outInfo && !extractTargetNorm) {
                            ArchiveItemEntry item;
                            item.pathInArchive     = fullRel;
                            item.name              = ArchivePath::BaseName(fullRel);
                            item.isDirectory       = true;
                            item.compressionMethod = 0;
                            outInfo->entries.push_back(std::move(item));
                            ++totalListed;
                        }
                        const uint32_t childDirOff = specInfo & ~0x0Fu;
                        if (childDirOff != 0u && childDirOff != cur) {
                            if (!self(self, childDirOff, fullRel, depth + 1)) return false;
                            if (extractedOk) return true;
                        }
                    } else if (fileType == 2u || fileType == 3u || fileType == 0u) {
                        uint32_t resolvedType = fileType;
                        size_t   resolvedOff  = dataOff;
                        uint32_t resolvedSize = fileSize;
                        bool resolvedOk = true;
                        if (fileType == 0u) {
                            resolvedOk = ResolveRomFsInodePayload(data, fsLim, specInfo, resolvedType, resolvedOff, resolvedSize);
                        }
                        if (resolvedOk && resolvedType == 1u) {
                            // Hardlink to directory (e.g. parent/self link): do not emit as regular file
                        } else if (resolvedOk && (resolvedType == 2u || resolvedType == 3u)) {
                            if (resolvedOff > fsLim || resolvedSize > fsLim - resolvedOff) return false;
                            if (extractTargetNorm && outExtracted) {
                                if (_wcsicmp(fullRel.c_str(), wantNorm.c_str()) == 0) {
                                    outExtracted->assign(
                                        data + resolvedOff,
                                        data + resolvedOff + static_cast<size_t>(resolvedSize));
                                    extractedOk = true;
                                    return true;
                                }
                            } else if (outInfo) {
                                ArchiveItemEntry item;
                                item.pathInArchive     = fullRel;
                                item.name              = ArchivePath::BaseName(fullRel);
                                item.isDirectory       = false;
                                item.uncompressedSize  = resolvedSize;
                                item.compressedSize    = resolvedSize;
                                item.rawDataOffset     = resolvedOff;
                                item.compressionMethod = 0;
                                outInfo->entries.push_back(std::move(item));
                                ++totalListed;
                            }
                        }
                    }
                }
            }

            cur = nextOff;
        }
        return true;
    };

    if (rootFirstInode < fsLim) {
        if (!walkDir(walkDir, static_cast<uint32_t>(rootFirstInode), L"", 0)) {
            if (outInfo) *outInfo = {};
            return false;
        }
    }

    if (extractTargetNorm) return extractedOk;
    if (!outInfo || outInfo->entries.empty()) return false;
    outInfo->valid = true;
    return true;
}

} // namespace DetailRomFs

inline bool InspectRomFsFilesystem(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return DetailRomFs::ParseOrExtractRomFs(data, size, &outInfo, nullptr, nullptr);
}

inline bool ExtractRomFsFilesystemEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    return DetailRomFs::ParseOrExtractRomFs(data, size, nullptr, &normTarget, &outBytes);
}

} // namespace Explorer2::Formats
