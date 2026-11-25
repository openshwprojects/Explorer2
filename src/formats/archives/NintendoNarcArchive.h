#pragma once
// NintendoNarcArchive.h — Nintendo DS Nitro Archive ("NARC" -> "BTAF" + "BTNF" + "GMIF") reader & extractor (.narc).

#include "../common/ArchiveFormatCommon.h"
#include <unordered_set>

namespace Explorer2::Formats {

namespace DetailNintendoNarc {

inline bool ParseOrExtractNarc(
    const uint8_t* data,
    size_t size,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm,
    std::vector<uint8_t>* outExtracted)
{
    if (!data || size < 44u || std::memcmp(data, "NARC", 4) != 0) return false;

    const uint16_t bom        = ByteIO::RdU16LE(data + 4u);
    const uint32_t narcFileSz = ByteIO::RdU32LE(data + 8u);
    const uint16_t hdrSize    = ByteIO::RdU16LE(data + 12u);
    const uint16_t numChunks  = ByteIO::RdU16LE(data + 14u);
    if (bom != 0xFFFEu && bom != 0xFEFFu) return false;
    if (hdrSize < 16u || hdrSize > size || numChunks < 3u) return false;

    const size_t effSize = (narcFileSz >= hdrSize && narcFileSz <= size) ? narcFileSz : size;

    // 1. BTAF / FATB (File Allocation Table)
    const size_t btafPos = hdrSize;
    if (effSize - btafPos < 12u) return false;
    if (std::memcmp(data + btafPos, "BTAF", 4) != 0 && std::memcmp(data + btafPos, "FATB", 4) != 0) {
        return false;
    }
    const uint32_t btafSize  = ByteIO::RdU32LE(data + btafPos + 4u);
    const uint32_t fileCount = ByteIO::RdU16LE(data + btafPos + 8u);
    if (btafSize < 12u || btafSize > effSize - btafPos || fileCount == 0u || fileCount > 65535u) {
        return false;
    }
    if (static_cast<uint64_t>(fileCount) * 8ULL > btafSize - 12u) return false;
    const uint8_t* fatEntries = data + btafPos + 12u;

    // 2. BTNF / FNTB (Filename Table)
    const size_t btnfPos = btafPos + btafSize;
    if (effSize - btnfPos < 8u) return false;
    if (std::memcmp(data + btnfPos, "BTNF", 4) != 0 && std::memcmp(data + btnfPos, "FNTB", 4) != 0) {
        return false;
    }
    const uint32_t btnfSize = ByteIO::RdU32LE(data + btnfPos + 4u);
    if (btnfSize < 8u || btnfSize > effSize - btnfPos) return false;
    const uint8_t* fntBase = data + btnfPos + 8u;
    const size_t   fntSize = btnfSize - 8u;

    // 3. GMIF / FIMG (File Image Data)
    const size_t gmifPos = btnfPos + btnfSize;
    if (effSize - gmifPos < 8u) return false;
    if (std::memcmp(data + gmifPos, "GMIF", 4) != 0 && std::memcmp(data + gmifPos, "FIMG", 4) != 0) {
        return false;
    }
    const uint32_t gmifSize = ByteIO::RdU32LE(data + gmifPos + 4u);
    if (gmifSize < 8u) return false;
    const size_t imgBase = gmifPos + 8u;
    const size_t imgAvail = effSize - imgBase;

    if (outInfo) {
        outInfo->formatName = L"Nintendo DS Nitro Archive (.narc)";
        outInfo->entries.reserve(fileCount);
    }

    const std::wstring wantNorm = extractTargetNorm ? ArchivePath::Normalize(*extractTargetNorm) : std::wstring{};
    bool extractedOk = false;
    std::vector<bool> fileNamed(fileCount, false);
    size_t namedCount = 0;

    auto emitOrExtractFile = [&](uint32_t fileId, const std::wstring& relPath) -> bool {
        if (fileId >= fileCount) return false;
        const uint32_t startOff = ByteIO::RdU32LE(fatEntries + static_cast<size_t>(fileId) * 8u + 0u);
        const uint32_t endOff   = ByteIO::RdU32LE(fatEntries + static_cast<size_t>(fileId) * 8u + 4u);
        if (endOff < startOff || startOff > imgAvail || endOff > imgAvail) return false;
        const uint32_t fLen = endOff - startOff;
        const size_t absOff = imgBase + startOff;

        fileNamed[fileId] = true;
        ++namedCount;

        if (extractTargetNorm && outExtracted) {
            if (_wcsicmp(relPath.c_str(), wantNorm.c_str()) == 0) {
                outExtracted->assign(data + absOff, data + absOff + fLen);
                extractedOk = true;
                return true;
            }
            return true;
        }
        if (outInfo) {
            ArchiveItemEntry item;
            item.pathInArchive     = relPath;
            item.name              = ArchivePath::BaseName(relPath);
            item.isDirectory       = false;
            item.uncompressedSize  = fLen;
            item.compressedSize    = fLen;
            item.rawDataOffset     = absOff;
            item.compressionMethod = 0u;
            outInfo->entries.push_back(std::move(item));
        }
        return true;
    };

    // Walk Nitro hierarchical FNT if directory main table is present
    if (fntSize >= 8u) {
        const uint32_t rootSubOff = ByteIO::RdU32LE(fntBase + 0u);
        const uint16_t totalDirs  = ByteIO::RdU16LE(fntBase + 6u);
        if (totalDirs >= 1u && totalDirs <= 4096u &&
            static_cast<size_t>(totalDirs) * 8u <= fntSize &&
            rootSubOff >= static_cast<size_t>(totalDirs) * 8u &&
            rootSubOff < fntSize)
        {
            std::unordered_set<uint16_t> visitedDirs;
            auto walkFntDir = [&](auto& self, uint16_t dirIdx, const std::wstring& prefix, int depth) -> bool {
                if (depth > 32 || dirIdx >= totalDirs) return true;
                if (!visitedDirs.insert(dirIdx).second) return true;

                const uint8_t* dm = fntBase + static_cast<size_t>(dirIdx) * 8u;
                const uint32_t subOff      = ByteIO::RdU32LE(dm + 0u);
                uint16_t       curFileId   = ByteIO::RdU16LE(dm + 4u);
                if (subOff >= fntSize) return false;

                size_t p = subOff;
                while (p < fntSize) {
                    const uint8_t typeLen = fntBase[p++];
                    if (typeLen == 0x00u) break; // End of subtable
                    if (typeLen == 0x80u) return false;

                    const bool isSubDir = (typeLen & 0x80u) != 0u;
                    const size_t nameLen = static_cast<size_t>(typeLen & 0x7Fu);
                    if (nameLen == 0u || nameLen > fntSize - p) return false;

                    std::string rawName(reinterpret_cast<const char*>(fntBase + p), nameLen);
                    p += nameLen;

                    const std::wstring nameW = PathUtils::Utf8ToWide(rawName);
                    const std::wstring fullRel = ArchivePath::Normalize(prefix.empty() ? nameW : (prefix + L"/" + nameW));

                    if (isSubDir) {
                        if (fntSize - p < 2u) return false;
                        const uint16_t subDirId  = ByteIO::RdU16LE(fntBase + p);
                        p += 2u;
                        const uint16_t subDirIdx = static_cast<uint16_t>(subDirId & 0x0FFFu);
                        if (!fullRel.empty()) {
                            if (outInfo && !extractTargetNorm) {
                                ArchiveItemEntry dirItem;
                                dirItem.pathInArchive     = fullRel;
                                dirItem.name              = ArchivePath::BaseName(fullRel);
                                dirItem.isDirectory       = true;
                                dirItem.compressionMethod = 0u;
                                outInfo->entries.push_back(std::move(dirItem));
                            }
                            if (!self(self, subDirIdx, fullRel, depth + 1)) return false;
                            if (extractedOk) return true;
                        }
                    } else {
                        if (!fullRel.empty()) {
                            if (!emitOrExtractFile(curFileId, fullRel)) return false;
                            if (extractedOk) return true;
                        }
                        ++curFileId;
                    }
                }
                return true;
            };

            if (!walkFntDir(walkFntDir, 0u, L"", 0)) {
                if (outInfo) *outInfo = {};
                return false;
            }
            if (extractedOk) return true;
        }
    }

    // Fallback for nameless NARC entries (very common in Nintendo DS commercial games where BTNF is empty)
    if (namedCount < fileCount) {
        for (uint32_t i = 0; i < fileCount; ++i) {
            if (fileNamed[i]) continue;
            wchar_t fb[32] = {};
            swprintf_s(fb, L"file_%04u.bin", static_cast<unsigned>(i));
            if (!emitOrExtractFile(i, fb)) {
                if (outInfo) *outInfo = {};
                return false;
            }
            if (extractedOk) return true;
        }
    }

    if (extractTargetNorm) return extractedOk;
    if (!outInfo || outInfo->entries.empty()) return false;
    outInfo->valid = true;
    return true;
}

} // namespace DetailNintendoNarc

inline bool InspectNintendoNarcArchive(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return DetailNintendoNarc::ParseOrExtractNarc(data, size, &outInfo, nullptr, nullptr);
}

inline bool ExtractNintendoNarcEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    return DetailNintendoNarc::ParseOrExtractNarc(data, size, nullptr, &normTarget, &outBytes);
}

} // namespace Explorer2::Formats
