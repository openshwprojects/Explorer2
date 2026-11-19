#pragma once
// BethesdaBsa.h — Bethesda Softworks Archive v103/v104/v105 reader & extractor (.bsa — "BSA\0").

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailArc100 {

inline bool ParseOrExtractBethesdaBsa(
    const uint8_t* data,
    size_t size,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm,
    std::vector<uint8_t>* outExtracted)
{
    if (size < 36 || std::memcmp(data, "BSA\0", 4) != 0) return false;
    uint32_t version      = RdU32LE(data + 4);
    uint32_t folderOffset = RdU32LE(data + 8);
    uint32_t archiveFlags = RdU32LE(data + 12);
    uint32_t folderCount  = RdU32LE(data + 16);
    uint32_t fileCount    = RdU32LE(data + 20);
    uint32_t totalFolderNameLen = RdU32LE(data + 24);
    uint32_t totalFileNameLen   = RdU32LE(data + 28);
    (void)totalFolderNameLen;

    if ((version != 103 && version != 104 && version != 105) ||
        folderOffset < 36 || folderOffset >= size ||
        folderCount == 0 || folderCount > 200000 ||
        fileCount == 0 || fileCount > 500000)
    {
        return false;
    }

    size_t folderRecSize = (version == 105) ? 24 : 16;
    if (folderOffset + static_cast<uint64_t>(folderCount) * folderRecSize > size) return false;
    size_t folderTableEnd = folderOffset + static_cast<size_t>(folderCount) * folderRecSize;

    struct BsaFolderHdr {
        uint64_t hash;
        uint32_t count;
        uint64_t rawOffset;
    };
    std::vector<BsaFolderHdr> folders(folderCount);
    for (uint32_t f = 0; f < folderCount; ++f) {
        const uint8_t* fp = data + folderOffset + static_cast<size_t>(f) * folderRecSize;
        folders[f].hash  = RdU64LE(fp + 0);
        folders[f].count = RdU32LE(fp + 8);
        folders[f].rawOffset = (version == 105) ? RdU64LE(fp + 16) : RdU32LE(fp + 12);
    }

    bool hasDirNames  = (archiveFlags & 0x001u) != 0;
    bool hasFileNames = (archiveFlags & 0x002u) != 0;
    bool defaultComp  = (archiveFlags & 0x004u) != 0;
    bool embedNames   = (version == 104 || version == 105) && ((archiveFlags & 0x100u) != 0);

    struct BsaFileRec {
        std::wstring dirPath;
        uint64_t fileHash;
        uint32_t rawSize;
        uint32_t dataOffset;
        bool compressed;
    };
    std::vector<BsaFileRec> files;
    files.reserve(std::min<uint32_t>(fileCount, 16384));

    size_t maxBlockEnd = folderTableEnd;
    for (uint32_t f = 0; f < folderCount; ++f) {
        uint64_t rawOff = folders[f].rawOffset;
        uint64_t realOff = (hasFileNames && rawOff >= folderTableEnd + totalFileNameLen)
            ? (rawOff - totalFileNameLen)
            : rawOff;
        if (realOff >= size) return false;
        size_t bp = static_cast<size_t>(realOff);

        std::wstring dirPath;
        if (hasDirNames) {
            if (bp >= size) return false;
            uint8_t bzLen = data[bp++];
            if (bp + bzLen > size) return false;
            if (bzLen > 0) {
                std::string ds(reinterpret_cast<const char*>(data + bp), strnlen(reinterpret_cast<const char*>(data + bp), bzLen));
                dirPath = ArchivePath::Normalize(PathUtils::Utf8ToWide(ds));
            }
            bp += bzLen;
        } else {
            wchar_t hb[32] = {};
            swprintf_s(hb, L"dir_%016llx", static_cast<unsigned long long>(folders[f].hash));
            dirPath = hb;
        }

        uint32_t cnt = folders[f].count;
        if (cnt > fileCount || bp + static_cast<uint64_t>(cnt) * 16 > size) return false;
        for (uint32_t k = 0; k < cnt && files.size() < fileCount; ++k) {
            const uint8_t* fr = data + bp + static_cast<size_t>(k) * 16;
            uint64_t fHash = RdU64LE(fr + 0);
            uint32_t szFlg = RdU32LE(fr + 8);
            uint32_t dOff  = RdU32LE(fr + 12);
            bool flipComp  = (szFlg & 0x40000000u) != 0;
            bool isComp    = defaultComp ^ flipComp;
            uint32_t rSz   = szFlg & 0x3FFFFFFFu;
            files.push_back({ dirPath, fHash, rSz, dOff, isComp });
        }
        bp += static_cast<size_t>(cnt) * 16;
        if (bp > maxBlockEnd) maxBlockEnd = bp;
    }

    // Read fileCount null-terminated filenames right after all folder record blocks
    std::vector<std::wstring> fileNames;
    if (hasFileNames && maxBlockEnd < size) {
        fileNames.reserve(files.size());
        size_t np = maxBlockEnd;
        size_t npEnd = std::min<size_t>(size, maxBlockEnd + totalFileNameLen + 64);
        for (size_t i = 0; i < files.size() && np < npEnd; ++i) {
            size_t e = np;
            while (e < npEnd && data[e] != 0) ++e;
            std::string fn(reinterpret_cast<const char*>(data + np), e - np);
            fileNames.push_back(ArchivePath::Normalize(PathUtils::Utf8ToWide(fn)));
            np = (e < npEnd) ? (e + 1) : npEnd;
        }
    }

    if (outInfo) {
        outInfo->formatName = L"Bethesda BSA v" + std::to_wstring(version);
    }

    for (size_t i = 0; i < files.size(); ++i) {
        const auto& fr = files[i];
        std::wstring leaf;
        if (i < fileNames.size() && !fileNames[i].empty()) {
            leaf = fileNames[i];
        } else {
            wchar_t hb[32] = {};
            swprintf_s(hb, L"file_%016llx.bin", static_cast<unsigned long long>(fr.fileHash));
            leaf = hb;
        }
        std::wstring full = ArchivePath::Normalize(fr.dirPath.empty() ? leaf : (fr.dirPath + L"/" + leaf));
        if (full.empty()) continue;

        // Inspect exact payload slice (accounting for optional bString embedded name and compressed 4B size prefix)
        size_t p = fr.dataOffset;
        size_t pEnd = p + fr.rawSize;
        uint64_t unpSz = fr.rawSize;
        bool validSlice = (fr.dataOffset <= size && fr.rawSize <= size - fr.dataOffset);
        if (validSlice) {
            if (embedNames && p < pEnd) {
                uint8_t bLen = data[p++];
                p = std::min<size_t>(pEnd, p + bLen);
            }
            if (fr.compressed) {
                if (p + 4 <= pEnd) {
                    unpSz = RdU32LE(data + p);
                    p += 4;
                } else {
                    validSlice = false;
                }
            } else {
                unpSz = pEnd - p;
            }
        }

        if (extractTargetNorm && outExtracted) {
            if (PathEqualsCI(full, *extractTargetNorm)) {
                if (!validSlice) return false;
                if (!fr.compressed) {
                    outExtracted->assign(data + p, data + pEnd);
                    return true;
                }
                size_t cmpLen = pEnd - p;
                if (cmpLen >= 4 && RdU32LE(data + p) == 0x184D2204u) {
                    return StreamCodecs::DecompressLz4Frame(data + p, cmpLen, *outExtracted, 128 * 1024 * 1024);
                }
                if (ZipDeflate::InflateZlib(data + p, cmpLen, *outExtracted, 128 * 1024 * 1024)) return true;
                return ZipDeflate::InflateRaw(data + p, cmpLen, *outExtracted, 128 * 1024 * 1024);
            }
        } else if (outInfo) {
            ArchiveItemEntry item;
            item.pathInArchive = full;
            item.name = ArchivePath::BaseName(full);
            item.uncompressedSize = unpSz;
            item.compressedSize   = fr.rawSize;
            item.rawDataOffset    = p;
            item.compressionMethod = (!fr.compressed && validSlice) ? 0 : 8;
            outInfo->entries.push_back(std::move(item));
        }
    }

    if (extractTargetNorm) return false;
    if (outInfo) {
        outInfo->valid = !outInfo->entries.empty();
        return outInfo->valid;
    }
    return false;
}

} // namespace DetailArc100

inline bool InspectBethesdaBsa(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return DetailArc100::ParseOrExtractBethesdaBsa(data, size, &outInfo, nullptr, nullptr);
}

inline bool ExtractBethesdaBsaEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    return DetailArc100::ParseOrExtractBethesdaBsa(data, size, nullptr, &normTarget, &outBytes);
}

} // namespace Explorer2::Formats
