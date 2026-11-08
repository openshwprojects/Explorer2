#include "ArchiveExplorer.h"
#include "ZipDeflate.h"
#include "PathUtils.h"
#include "FileEngine.h"
#include <shellapi.h>
#include <algorithm>
#include <unordered_map>
#include <cstring>
#include <cstdio>

namespace Explorer2 {

namespace {

static D2D1_COLOR_F ArcColor(uint32_t rgb, float a = 1.0f) {
    return PathUtils::ColorFromRgb(rgb, a);
}

inline uint16_t ReadU16LE(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

inline uint32_t ReadU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

inline uint64_t ReadU64LE(const uint8_t* p) {
    return static_cast<uint64_t>(ReadU32LE(p)) |
           (static_cast<uint64_t>(ReadU32LE(p + 4)) << 32);
}

// Reads a RAR 5.0 variable-length integer (7 bits per byte, MSB = continuation)
bool ReadRar5Vint(const uint8_t* data, size_t size, size_t& pos, uint64_t& outVal) {
    outVal = 0;
    int shift = 0;
    for (int i = 0; i < 10; ++i) {
        if (pos >= size) return false;
        uint8_t b = data[pos++];
        outVal |= static_cast<uint64_t>(b & 0x7F) << shift;
        if ((b & 0x80) == 0) return true;
        shift += 7;
    }
    return false;
}

std::wstring NormalizeArchiveInternalPath(std::wstring p) {
    for (wchar_t& ch : p) {
        if (ch == L'\\') ch = L'/';
    }
    while (!p.empty() && p.front() == L'/') p.erase(p.begin());
    while (!p.empty() && p.back() == L'/') p.pop_back();
    if (p.rfind(L"./", 0) == 0) p.erase(0, 2);
    return p;
}

std::wstring GetBaseNameFromInternalPath(const std::wstring& p) {
    size_t slash = p.find_last_of(L'/');
    return (slash == std::wstring::npos) ? p : p.substr(slash + 1);
}

// ============================================================================
// RAR 4.x Binary Block Header Parser ("Rar!\x1A\x07\x00")
// ============================================================================
bool ParseRar4Headers(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (size < 7 || std::memcmp(data, "Rar!\x1A\x07\x00", 7) != 0) return false;
    outInfo.formatName = L"RAR4";

    size_t pos = 7;
    while (pos + 7 <= size) {
        uint8_t  headType  = data[pos + 2];
        uint16_t headFlags = ReadU16LE(data + pos + 3);
        uint16_t headSize  = ReadU16LE(data + pos + 5);
        if (headSize < 7 || pos + headSize > size) break;

        uint64_t addSize = 0;
        if ((headFlags & 0x8000) != 0 || headType == 0x74) {
            if (pos + 11 <= size) {
                addSize = ReadU32LE(data + pos + 7);
            }
        }

        if (headType == 0x74 && headSize >= 32 && pos + 32 <= size) {
            uint32_t packLow = ReadU32LE(data + pos + 7);
            uint32_t unpLow  = ReadU32LE(data + pos + 11);
            uint32_t fileCrc = ReadU32LE(data + pos + 16);
            uint32_t ftime   = ReadU32LE(data + pos + 20);
            uint8_t  method  = data[pos + 25];
            uint16_t nameLen = ReadU16LE(data + pos + 26);
            uint32_t attr    = ReadU32LE(data + pos + 28);

            size_t nameOff = pos + 32;
            uint64_t packHigh = 0, unpHigh = 0;
            if ((headFlags & 0x0100) != 0 && nameOff + 8 <= pos + headSize) {
                packHigh = ReadU32LE(data + nameOff);
                unpHigh  = ReadU32LE(data + nameOff + 4);
                nameOff += 8;
            }
            uint64_t packSize = (packHigh << 32) | packLow;
            uint64_t unpSize  = (unpHigh << 32) | unpLow;
            addSize = packSize;

            if (nameOff + nameLen <= pos + headSize && nameLen > 0) {
                // RAR4 filename up to first null byte if unicode flag set
                size_t asciiLen = 0;
                while (asciiLen < nameLen && data[nameOff + asciiLen] != 0) ++asciiLen;
                std::string rawName(reinterpret_cast<const char*>(data + nameOff), asciiLen);
                std::wstring normPath = NormalizeArchiveInternalPath(PathUtils::Utf8ToWide(rawName));
                if (!normPath.empty()) {
                    ArchiveItemEntry item;
                    item.pathInArchive = normPath;
                    item.name = GetBaseNameFromInternalPath(normPath);
                    item.uncompressedSize = unpSize;
                    item.compressedSize = packSize;
                    item.crc32 = fileCrc;
                    item.dosTime = static_cast<uint16_t>(ftime & 0xFFFF);
                    item.dosDate = static_cast<uint16_t>((ftime >> 16) & 0xFFFF);
                    item.isDirectory = ((headFlags & 0x00E0) == 0x00E0) || ((attr & 0x10) != 0);
                    item.rawDataOffset = pos + headSize;
                    item.compressionMethod = method; // 0x30 = Store
                    outInfo.entries.push_back(std::move(item));
                }
            }
        } else if (headType == 0x7B) {
            // End of archive block
            break;
        }

        size_t nextPos = pos + headSize + static_cast<size_t>(addSize);
        if (nextPos <= pos || nextPos > size) break;
        pos = nextPos;
    }

    outInfo.valid = true;
    return true;
}

// ============================================================================
// RAR 5.0 Binary Block Header Parser ("Rar!\x1A\x07\x01\x00")
// ============================================================================
bool ParseRar5Headers(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (size < 8 || std::memcmp(data, "Rar!\x1A\x07\x01\x00", 8) != 0) return false;
    outInfo.formatName = L"RAR5";

    size_t pos = 8;
    while (pos + 7 <= size) {
        pos += 4; // Skip Header CRC32
        uint64_t hdrSize = 0;
        if (!ReadRar5Vint(data, size, pos, hdrSize) || hdrSize == 0) break;
        size_t hdrEnd = pos + static_cast<size_t>(hdrSize);
        if (hdrEnd > size) break;

        size_t hpos = pos;
        uint64_t hdrType = 0, hdrFlags = 0, extraAreaSize = 0, dataSize = 0;
        if (!ReadRar5Vint(data, hdrEnd, hpos, hdrType)) break;
        if (!ReadRar5Vint(data, hdrEnd, hpos, hdrFlags)) break;
        if ((hdrFlags & 0x0001) != 0 && !ReadRar5Vint(data, hdrEnd, hpos, extraAreaSize)) break;
        if ((hdrFlags & 0x0002) != 0 && !ReadRar5Vint(data, hdrEnd, hpos, dataSize)) break;

        if (hdrType == 2) { // File / Directory Header
            uint64_t fileFlags = 0, unpSize = 0, attr = 0, compInfo = 0, hostOs = 0, nameLen = 0;
            if (ReadRar5Vint(data, hdrEnd, hpos, fileFlags) &&
                ReadRar5Vint(data, hdrEnd, hpos, unpSize) &&
                ReadRar5Vint(data, hdrEnd, hpos, attr))
            {
                if ((fileFlags & 0x0002) != 0 && hpos + 4 <= hdrEnd) hpos += 4; // mtime (unix uint32)
                uint32_t dataCrc = 0;
                if ((fileFlags & 0x0004) != 0 && hpos + 4 <= hdrEnd) {
                    dataCrc = ReadU32LE(data + hpos);
                    hpos += 4;
                }
                if (ReadRar5Vint(data, hdrEnd, hpos, compInfo) &&
                    ReadRar5Vint(data, hdrEnd, hpos, hostOs) &&
                    ReadRar5Vint(data, hdrEnd, hpos, nameLen))
                {
                    if (hpos + nameLen <= hdrEnd && nameLen > 0 && nameLen < 4096) {
                        std::string utf8Name(reinterpret_cast<const char*>(data + hpos), static_cast<size_t>(nameLen));
                        std::wstring normPath = NormalizeArchiveInternalPath(PathUtils::Utf8ToWide(utf8Name));
                        if (!normPath.empty()) {
                            ArchiveItemEntry item;
                            item.pathInArchive = normPath;
                            item.name = GetBaseNameFromInternalPath(normPath);
                            item.uncompressedSize = unpSize;
                            item.compressedSize = dataSize;
                            item.crc32 = dataCrc;
                            item.isDirectory = (fileFlags & 0x0001) != 0;
                            item.rawDataOffset = hdrEnd;
                            // In RAR5, bits 7..9 of compInfo store the compression method (0 = Store)
                            item.compressionMethod = static_cast<uint16_t>((compInfo >> 7) & 0x07);
                            outInfo.entries.push_back(std::move(item));
                        }
                    }
                }
            }
        } else if (hdrType == 5) { // End of Archive Header
            break;
        }

        size_t nextBlock = hdrEnd + static_cast<size_t>(dataSize);
        if (nextBlock <= pos || nextBlock > size) break;
        pos = nextBlock;
    }

    outInfo.valid = true;
    return true;
}

// ============================================================================
// 7-Zip Binary Header Parser ("7z\xBC\xAF\x27\x1C")
// ============================================================================
uint64_t Read7zNumber(const uint8_t* data, size_t size, size_t& pos) {
    if (pos >= size) return 0;
    uint8_t first = data[pos++];
    uint8_t mask = 0x80;
    uint64_t val = 0;
    for (int i = 0; i < 8; ++i) {
        if ((first & mask) == 0) {
            uint64_t high = first & (mask - 1);
            val |= (high << (i * 8));
            return val;
        }
        if (pos >= size) return 0;
        val |= (static_cast<uint64_t>(data[pos++]) << (i * 8));
        mask >>= 1;
    }
    return val;
}

bool Parse7zHeaders(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    static const uint8_t k7zSig[6] = { '7', 'z', 0xBC, 0xAF, 0x27, 0x1C };
    if (size < 32 || std::memcmp(data, k7zSig, 6) != 0) return false;
    outInfo.formatName = L"7-Zip";

    uint64_t nextHdrOffset = ReadU64LE(data + 12);
    uint64_t nextHdrSize   = ReadU64LE(data + 20);
    uint64_t absHdrPos     = 32ULL + nextHdrOffset;
    if (absHdrPos > size || absHdrPos + nextHdrSize > size) {
        outInfo.valid = true;
        return true;
    }

    const uint8_t* hdr = data + static_cast<size_t>(absHdrPos);
    size_t hSize = static_cast<size_t>(nextHdrSize);

    // Scan NextHeader for kFilesInfo (0x05) -> kName (0x11) UTF-16LE entries
    size_t pos = 0;
    std::vector<std::wstring> fileNames;
    while (pos < hSize) {
        uint8_t id = hdr[pos++];
        if (id == 0x11 && pos + 2 <= hSize) { // kName property inside kFilesInfo
            size_t propPos = pos;
            uint64_t propSize = Read7zNumber(hdr, hSize, propPos);
            if (propSize >= 3 && propPos + propSize <= hSize && hdr[propPos] == 0) {
                size_t strStart = propPos + 1;
                size_t strEnd = propPos + static_cast<size_t>(propSize);
                std::wstring cur;
                for (size_t sp = strStart; sp + 2 <= strEnd; sp += 2) {
                    wchar_t wc = static_cast<wchar_t>(ReadU16LE(hdr + sp));
                    if (wc == 0) {
                        if (!cur.empty()) {
                            fileNames.push_back(cur);
                            cur.clear();
                        }
                    } else {
                        cur.push_back(wc);
                    }
                }
                if (!cur.empty()) fileNames.push_back(cur);
                pos = strEnd;
                break;
            }
        }
    }

    uint64_t payloadBytes = nextHdrOffset;
    uint64_t perFileEst = fileNames.empty() ? 0 : (payloadBytes / fileNames.size());
    uint64_t curRawOff = 32;
    for (const auto& fn : fileNames) {
        std::wstring norm = NormalizeArchiveInternalPath(fn);
        if (norm.empty()) continue;
        ArchiveItemEntry item;
        item.pathInArchive = norm;
        item.name = GetBaseNameFromInternalPath(norm);
        item.isDirectory = (!fn.empty() && (fn.back() == L'/' || fn.back() == L'\\'));
        if (!item.isDirectory) {
            item.uncompressedSize = perFileEst;
            item.compressedSize = perFileEst;
            item.rawDataOffset = curRawOff;
            curRawOff += perFileEst;
        }
        outInfo.entries.push_back(std::move(item));
    }

    outInfo.valid = true;
    return true;
}

// ============================================================================
// Single-file GZIP (.gz) Inspector
// ============================================================================
bool ParseSingleGzip(const std::wstring& archivePath, const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (size < 18 || data[0] != 0x1F || data[1] != 0x8B || data[2] != 8) return false;
    outInfo.formatName = L"GZIP";

    uint8_t flags = data[3];
    size_t pos = 10;
    if ((flags & 0x04) != 0) { // FEXTRA
        if (pos + 2 > size) return false;
        uint16_t xlen = ReadU16LE(data + pos);
        pos += 2 + xlen;
    }
    std::wstring innerName;
    if ((flags & 0x08) != 0) { // FNAME
        size_t start = pos;
        while (pos < size && data[pos] != 0) ++pos;
        if (pos > start) {
            innerName = PathUtils::Utf8ToWide(std::string(reinterpret_cast<const char*>(data + start), pos - start));
        }
        if (pos < size) ++pos;
    }
    if (innerName.empty()) {
        std::wstring fn = PathUtils::GetFileName(archivePath);
        if (fn.size() > 3 && PathUtils::ToLower(fn.substr(fn.size() - 3)) == L".gz") {
            innerName = fn.substr(0, fn.size() - 3);
        } else {
            innerName = fn + L".extracted";
        }
    }

    uint32_t isize = ReadU32LE(data + size - 4);
    uint32_t crc   = ReadU32LE(data + size - 8);

    ArchiveItemEntry item;
    item.pathInArchive = GetBaseNameFromInternalPath(NormalizeArchiveInternalPath(innerName));
    item.name = item.pathInArchive;
    item.uncompressedSize = isize;
    item.compressedSize = size;
    item.crc32 = crc;
    item.isDirectory = false;
    outInfo.entries.push_back(std::move(item));
    outInfo.valid = true;
    return true;
}

} // namespace

bool ArchiveExplorer::IsArchiveExtension(const std::wstring& extLower) {
    return extLower == L".zip"   || extLower == L".rar" || extLower == L".7z"  ||
           extLower == L".tar"   || extLower == L".gz"  || extLower == L".tgz" ||
           extLower == L".jar"   || extLower == L".apk" || extLower == L".whl" ||
           extLower == L".nupkg" || extLower == L".cbz" || extLower == L".cbr" ||
           extLower == L".pk3";
}

bool ArchiveExplorer::SplitVirtualArchivePath(
    const std::wstring& fullPath,
    std::wstring& outArchiveFile,
    std::wstring& outInnerPath)
{
    outArchiveFile.clear();
    outInnerPath.clear();
    if (fullPath.empty() || fullPath == L"THIS_PC") return false;

    std::wstring clean = PathUtils::TrimTrailingSlashes(fullPath);
    // Walk slash positions from left to right to find if any prefix is an archive file on disk
    size_t pos = (clean.size() >= 3 && clean[1] == L':') ? 3 : 0;
    while (pos <= clean.size()) {
        size_t nextSlash = clean.find_first_of(L"\\/", pos);
        std::wstring prefix = (nextSlash == std::wstring::npos) ? clean : clean.substr(0, nextSlash);
        std::wstring ext = PathUtils::GetExtension(prefix);
        if (IsArchiveExtension(ext)) {
            DWORD attrs = GetFileAttributesW(prefix.c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                outArchiveFile = prefix;
                if (nextSlash != std::wstring::npos && nextSlash + 1 < clean.size()) {
                    outInnerPath = NormalizeArchiveInternalPath(clean.substr(nextSlash + 1));
                }
                return true;
            }
        }
        if (nextSlash == std::wstring::npos) break;
        pos = nextSlash + 1;
    }
    return false;
}

bool ArchiveExplorer::IsVirtualArchivePath(const std::wstring& fullPath) {
    std::wstring arc, inner;
    return SplitVirtualArchivePath(fullPath, arc, inner);
}

bool ArchiveExplorer::InspectArchiveFile(const std::wstring& archivePath, ArchiveSummaryInfo& outInfo) {
    outInfo = {};
    std::vector<uint8_t> bytes;
    if (!PathUtils::ReadFileBytes(archivePath, 256ULL * 1024 * 1024, bytes) || bytes.size() < 4) {
        return false;
    }

    const uint8_t* data = bytes.data();
    size_t size = bytes.size();
    std::wstring extLower = PathUtils::GetExtension(archivePath);
    std::wstring nameLower = PathUtils::ToLower(PathUtils::GetFileName(archivePath));
    bool isTarGzName = (nameLower.size() > 7 && nameLower.substr(nameLower.size() - 7) == L".tar.gz") ||
                       (extLower == L".tgz");

    bool parsed = false;
    if (size >= 8 && std::memcmp(data, "Rar!\x1A\x07\x01\x00", 8) == 0) {
        parsed = ParseRar5Headers(data, size, outInfo);
    } else if (size >= 7 && std::memcmp(data, "Rar!\x1A\x07\x00", 7) == 0) {
        parsed = ParseRar4Headers(data, size, outInfo);
    } else if (size >= 6 && std::memcmp(data, "7z\xBC\xAF\x27\x1C", 6) == 0) {
        parsed = Parse7zHeaders(data, size, outInfo);
    } else if (size >= 4 && data[0] == 'P' && data[1] == 'K' &&
               ((data[2] == 3 && data[3] == 4) || (data[2] == 5 && data[3] == 6))) {
        std::vector<ZipDeflate::ArchiveItemMeta> zipEntries;
        if (ZipDeflate::ListZipEntriesFromMemory(data, size, zipEntries)) {
            outInfo.formatName = (extLower == L".jar") ? L"JAR (ZIP)" :
                                 (extLower == L".apk") ? L"APK (ZIP)" :
                                 (extLower == L".nupkg") ? L"NuGet (ZIP)" : L"ZIP";
            for (const auto& ze : zipEntries) {
                std::wstring norm = NormalizeArchiveInternalPath(ze.pathInArchive);
                if (norm.empty()) continue;
                ArchiveItemEntry item;
                item.pathInArchive = norm;
                item.name = GetBaseNameFromInternalPath(norm);
                item.uncompressedSize = ze.uncompressedSize;
                item.compressedSize = ze.compressedSize;
                item.crc32 = ze.crc32;
                item.dosTime = ze.dosTime;
                item.dosDate = ze.dosDate;
                item.isDirectory = ze.isDirectory;
                item.rawDataOffset = ze.localHeaderOffset;
                item.compressionMethod = ze.compressionMethod;
                outInfo.entries.push_back(std::move(item));
            }
            outInfo.valid = true;
            parsed = true;
        }
    } else if (extLower == L".tar" || isTarGzName ||
               (size >= 265 && std::memcmp(data + 257, "ustar", 5) == 0)) {
        std::vector<ZipDeflate::ArchiveItemMeta> tarEntries;
        if (ZipDeflate::ListTarEntriesFromFile(archivePath, tarEntries)) {
            outInfo.formatName = (data[0] == 0x1F && data[1] == 0x8B) ? L"TAR.GZ" : L"TAR";
            for (const auto& te : tarEntries) {
                std::wstring norm = NormalizeArchiveInternalPath(te.pathInArchive);
                if (norm.empty()) continue;
                ArchiveItemEntry item;
                item.pathInArchive = norm;
                item.name = GetBaseNameFromInternalPath(norm);
                item.uncompressedSize = te.uncompressedSize;
                item.compressedSize = te.compressedSize;
                item.isDirectory = te.isDirectory;
                item.rawDataOffset = te.localHeaderOffset;
                outInfo.entries.push_back(std::move(item));
            }
            outInfo.valid = true;
            parsed = true;
        }
    } else if (size >= 18 && data[0] == 0x1F && data[1] == 0x8B && data[2] == 8) {
        // Check if it's actually a tar.gz inside even without .tar.gz extension
        std::vector<ZipDeflate::ArchiveItemMeta> tarEntries;
        if (ZipDeflate::ListTarEntriesFromFile(archivePath, tarEntries) && !tarEntries.empty()) {
            outInfo.formatName = L"TAR.GZ";
            for (const auto& te : tarEntries) {
                std::wstring norm = NormalizeArchiveInternalPath(te.pathInArchive);
                if (norm.empty()) continue;
                ArchiveItemEntry item;
                item.pathInArchive = norm;
                item.name = GetBaseNameFromInternalPath(norm);
                item.uncompressedSize = te.uncompressedSize;
                item.compressedSize = te.compressedSize;
                item.isDirectory = te.isDirectory;
                item.rawDataOffset = te.localHeaderOffset;
                outInfo.entries.push_back(std::move(item));
            }
            outInfo.valid = true;
            parsed = true;
        } else {
            parsed = ParseSingleGzip(archivePath, data, size, outInfo);
        }
    }

    if (!parsed || !outInfo.valid) return false;

    for (const auto& e : outInfo.entries) {
        if (e.isDirectory) {
            ++outInfo.totalFolders;
        } else {
            ++outInfo.totalFiles;
            outInfo.totalUncompressedBytes += e.uncompressedSize;
            outInfo.totalCompressedBytes += e.compressedSize;
        }
    }
    return true;
}

bool ArchiveExplorer::EnumerateArchiveFolder(
    const std::wstring& archivePath,
    const std::wstring& innerDir,
    std::vector<ArchiveItemEntry>& outImmediateChildren,
    ArchiveSummaryInfo* outFullSummary)
{
    outImmediateChildren.clear();
    ArchiveSummaryInfo summary;
    if (!InspectArchiveFile(archivePath, summary)) return false;

    std::wstring normDir = NormalizeArchiveInternalPath(innerDir);
    std::wstring prefix = normDir.empty() ? L"" : (normDir + L"/");

    // Map immediate child nameLower -> index in outImmediateChildren
    std::unordered_map<std::wstring, size_t> childMap;

    for (const auto& entry : summary.entries) {
        if (!prefix.empty()) {
            if (entry.pathInArchive.size() <= prefix.size()) continue;
            if (_wcsnicmp(entry.pathInArchive.c_str(), prefix.c_str(), prefix.size()) != 0) continue;
        }
        std::wstring rel = entry.pathInArchive.substr(prefix.size());
        if (rel.empty()) continue;

        size_t slash = rel.find(L'/');
        if (slash == std::wstring::npos) {
            // Immediate child file or explicit directory
            std::wstring key = PathUtils::ToLower(rel);
            auto it = childMap.find(key);
            if (it == childMap.end()) {
                ArchiveItemEntry child = entry;
                child.name = rel;
                childMap[key] = outImmediateChildren.size();
                outImmediateChildren.push_back(std::move(child));
            } else if (entry.isDirectory) {
                outImmediateChildren[it->second].isDirectory = true;
            }
        } else {
            // Nested entry inside an immediate child subfolder -> synthesize or accumulate folder size
            std::wstring folderName = rel.substr(0, slash);
            std::wstring key = PathUtils::ToLower(folderName);
            auto it = childMap.find(key);
            if (it == childMap.end()) {
                ArchiveItemEntry folderItem;
                folderItem.name = folderName;
                folderItem.pathInArchive = prefix + folderName;
                folderItem.isDirectory = true;
                folderItem.uncompressedSize = entry.isDirectory ? 0 : entry.uncompressedSize;
                folderItem.compressedSize = entry.isDirectory ? 0 : entry.compressedSize;
                childMap[key] = outImmediateChildren.size();
                outImmediateChildren.push_back(std::move(folderItem));
            } else {
                auto& existing = outImmediateChildren[it->second];
                existing.isDirectory = true;
                if (!entry.isDirectory) {
                    existing.uncompressedSize += entry.uncompressedSize;
                    existing.compressedSize += entry.compressedSize;
                }
            }
        }
    }

    std::sort(outImmediateChildren.begin(), outImmediateChildren.end(),
        [](const ArchiveItemEntry& a, const ArchiveItemEntry& b) {
            if (a.isDirectory != b.isDirectory) return a.isDirectory > b.isDirectory;
            return CompareStringOrdinal(a.name.c_str(), -1, b.name.c_str(), -1, TRUE) == CSTR_LESS_THAN;
        });

    if (outFullSummary) *outFullSummary = std::move(summary);
    return true;
}

bool ArchiveExplorer::ExtractSingleEntryToMemory(
    const std::wstring& archivePath,
    const std::wstring& innerPath,
    std::vector<uint8_t>& outBytes)
{
    outBytes.clear();
    std::vector<uint8_t> arcBytes;
    if (!PathUtils::ReadFileBytes(archivePath, 256ULL * 1024 * 1024, arcBytes) || arcBytes.size() < 4) {
        return false;
    }

    const uint8_t* data = arcBytes.data();
    size_t size = arcBytes.size();
    std::wstring normTarget = NormalizeArchiveInternalPath(innerPath);

    // 1. ZIP family
    if (size >= 4 && data[0] == 'P' && data[1] == 'K') {
        return ZipDeflate::ExtractZipFileByName(archivePath, normTarget, outBytes);
    }

    // 2. RAR4 / RAR5 / 7-Zip
    if ((size >= 7 && std::memcmp(data, "Rar!\x1A\x07", 6) == 0) ||
        (size >= 6 && std::memcmp(data, "7z\xBC\xAF\x27\x1C", 6) == 0))
    {
        ArchiveSummaryInfo info;
        if (InspectArchiveFile(archivePath, info)) {
            for (const auto& e : info.entries) {
                if (!e.isDirectory && _wcsicmp(e.pathInArchive.c_str(), normTarget.c_str()) == 0) {
                    // Direct in-memory extraction for Stored RAR4 (0x30), Stored RAR5 (0), or raw 7z stream
                    if ((info.formatName == L"RAR4" && e.compressionMethod == 0x30) ||
                        (info.formatName == L"RAR5" && e.compressionMethod == 0) ||
                        (info.formatName == L"7-Zip" && e.rawDataOffset >= 32))
                    {
                        if (e.rawDataOffset + e.uncompressedSize <= size) {
                            outBytes.assign(
                                data + static_cast<size_t>(e.rawDataOffset),
                                data + static_cast<size_t>(e.rawDataOffset + e.uncompressedSize));
                            return true;
                        }
                    }
                }
            }
        }
        return false;
    }

    // 3. TAR / TAR.GZ
    std::vector<ZipDeflate::ArchiveItemMeta> tarEntries;
    if (ZipDeflate::ListTarEntriesFromFile(archivePath, tarEntries) && !tarEntries.empty()) {
        for (const auto& te : tarEntries) {
            if (!te.isDirectory && _wcsicmp(NormalizeArchiveInternalPath(te.pathInArchive).c_str(), normTarget.c_str()) == 0) {
                return ZipDeflate::ExtractTarEntryFromFile(archivePath, te, outBytes);
            }
        }
        return false;
    }

    // 4. Single-file GZIP (.gz)
    if (size >= 18 && data[0] == 0x1F && data[1] == 0x8B) {
        return ZipDeflate::InflateGzip(data, size, outBytes);
    }

    return false;
}

bool ArchiveExplorer::ExtractSingleEntryToTempFile(
    const std::wstring& archivePath,
    const std::wstring& innerPath,
    std::wstring& outTempFilePath)
{
    outTempFilePath.clear();
    std::vector<uint8_t> extracted;
    if (!ExtractSingleEntryToMemory(archivePath, innerPath, extracted)) {
        return false;
    }

    wchar_t tmpRoot[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmpRoot);
    std::wstring previewDir = std::wstring(tmpRoot) + L"Explorer2_ArcPreview";
    CreateDirectoryW(previewDir.c_str(), nullptr);

    std::wstring baseName = GetBaseNameFromInternalPath(NormalizeArchiveInternalPath(innerPath));
    if (baseName.empty()) baseName = L"extracted.bin";
    outTempFilePath = previewDir + L"\\" + baseName;
    return PathUtils::WriteFileBytes(outTempFilePath, extracted.data(), extracted.size());
}

bool ArchiveExplorer::ExtractEntireArchiveToFolder(
    const std::wstring& archivePath,
    const std::wstring& destFolderPath)
{
    ArchiveSummaryInfo info;
    if (!InspectArchiveFile(archivePath, info) || !info.valid) return false;
    CreateDirectoryW(destFolderPath.c_str(), nullptr);

    bool anyExtracted = false;
    for (const auto& entry : info.entries) {
        std::wstring relWin = entry.pathInArchive;
        for (wchar_t& ch : relWin) {
            if (ch == L'/') ch = L'\\';
        }
        std::wstring fullDest = PathUtils::EnsureTrailingSlash(destFolderPath) + relWin;
        if (entry.isDirectory) {
            CreateDirectoryW(fullDest.c_str(), nullptr);
            continue;
        }
        // Ensure parent subdirectories exist
        size_t pos = destFolderPath.size() + 1;
        while (pos < fullDest.size()) {
            size_t nextSlash = fullDest.find(L'\\', pos);
            if (nextSlash == std::wstring::npos) break;
            std::wstring sub = fullDest.substr(0, nextSlash);
            CreateDirectoryW(sub.c_str(), nullptr);
            pos = nextSlash + 1;
        }
        std::vector<uint8_t> fileData;
        if (ExtractSingleEntryToMemory(archivePath, entry.pathInArchive, fileData)) {
            if (PathUtils::WriteFileBytes(fullDest, fileData.data(), fileData.size())) {
                anyExtracted = true;
            }
        }
    }
    return anyExtracted;
}

// ============================================================================
// Right-Side FileContentPane UI Implementation
// ============================================================================
void ArchiveExplorer::Initialize(HWND parentHwnd) {
    m_parentHwnd = parentHwnd;
}

void ArchiveExplorer::Shutdown() {
    Clear();
}

void ArchiveExplorer::Clear() {
    m_archivePath.clear();
    m_currentInnerDir.clear();
    m_summary = {};
    m_visibleItems.clear();
    m_scrollY = 0.0f;
    m_selectedIdx = -1;
    m_hoverIdx = -1;
}

void ArchiveExplorer::LoadFile(const std::wstring& fullPath, bool forceReload) {
    if (!forceReload && m_archivePath == fullPath && m_summary.valid) {
        return;
    }
    Clear();
    m_archivePath = fullPath;
    m_currentInnerDir.clear();
    EnumerateArchiveFolder(fullPath, L"", m_visibleItems, &m_summary);
}

void ArchiveExplorer::RebuildCurrentSubfolderView() {
    m_visibleItems.clear();
    m_scrollY = 0.0f;
    m_selectedIdx = -1;
    m_hoverIdx = -1;
    EnumerateArchiveFolder(m_archivePath, m_currentInnerDir, m_visibleItems, nullptr);
}

void ArchiveExplorer::Draw(
    ID2D1RenderTarget* rt,
    ID2D1SolidColorBrush* brush,
    IDWriteTextFormat* fmtRegular,
    IDWriteTextFormat* fmtSmall,
    IDWriteTextFormat* fmtBold,
    const D2D1_RECT_F& rect,
    Lang lang,
    D2D1_POINT_2F mousePt)
{
    (void)mousePt;
    m_toolbarHits.clear();
    const bool pl = (lang == Lang::PL);

    if (!m_summary.valid) {
        const wchar_t* msg = pl ? L"Nie udało się odczytać struktury archiwum." : L"Failed to read archive structure.";
        brush->SetColor(ArcColor(0x64748B));
        rt->DrawText(msg, static_cast<UINT32>(wcslen(msg)), fmtRegular,
            D2D1::RectF(rect.left + 16.0f, rect.top + 24.0f, rect.right - 16.0f, rect.bottom), brush);
        return;
    }

    // 1. Top Toolbar (32px): [↑ W górę] [Otwórz w panelu ➔] [Rozpakuj tutaj] [Rozpakuj do podfolderu]
    float barH = 32.0f;
    D2D1_RECT_F barR = D2D1::RectF(rect.left, rect.top, rect.right, rect.top + barH);
    brush->SetColor(ArcColor(0xF8FAFC));
    rt->FillRectangle(barR, brush);
    brush->SetColor(ArcColor(0xE2E8F0));
    rt->DrawLine(D2D1::Point2F(barR.left, barR.bottom - 0.5f), D2D1::Point2F(barR.right, barR.bottom - 0.5f), brush, 1.0f);

    float bx = barR.left + 8.0f;
    auto addBtn = [&](int id, float w, const wchar_t* label, bool enabled = true) {
        D2D1_RECT_F r = D2D1::RectF(bx, barR.top + 4.0f, bx + w, barR.bottom - 4.0f);
        if (enabled) m_toolbarHits.push_back({ id, r });
        PathUtils::DrawToolbarButton(rt, brush, fmtSmall, r, label, false, enabled && (m_hoverBtnId == id));
        bx += w + 5.0f;
    };

    if (!m_currentInnerDir.empty()) {
        addBtn(BTN_UP_DIR, pl ? 68.0f : 56.0f, pl ? L"\x2191 W górę" : L"\x2191 Up");
    }
    addBtn(BTN_OPEN_AS_FOLDER, pl ? 114.0f : 108.0f, pl ? L"Otwórz w panelu \x2794" : L"Browse in pane \x2794");
    addBtn(BTN_EXTRACT_HERE, pl ? 98.0f : 90.0f, pl ? L"\xD83D\xDCE6 Rozpakuj tu" : L"\xD83D\xDCE6 Extract Here");
    addBtn(BTN_EXTRACT_SUBFOLDER, pl ? 118.0f : 114.0f, pl ? L"\xD83D\xDCC2 Do podfolderu" : L"\xD83D\xDCC2 To Subfolder");

    // 2. Archive Info & Breadcrumb Banner (26px)
    float infoH = 26.0f;
    D2D1_RECT_F infoR = D2D1::RectF(rect.left, barR.bottom, rect.right, barR.bottom + infoH);
    brush->SetColor(ArcColor(0xEFF6FF));
    rt->FillRectangle(infoR, brush);
    brush->SetColor(ArcColor(0xCBD5E1));
    rt->DrawLine(D2D1::Point2F(infoR.left, infoR.bottom - 0.5f), D2D1::Point2F(infoR.right, infoR.bottom - 0.5f), brush, 1.0f);

    int ratioPct = (m_summary.totalUncompressedBytes > 0)
        ? static_cast<int>((m_summary.totalCompressedBytes * 100ULL) / m_summary.totalUncompressedBytes)
        : 100;
    std::wstring unpStr = PathUtils::FormatByteSize(m_summary.totalUncompressedBytes);
    std::wstring cmpStr = PathUtils::FormatByteSize(m_summary.totalCompressedBytes);
    std::wstring pathBadge = m_currentInnerDir.empty() ? L"/" : (L"/" + m_currentInnerDir);

    wchar_t infoBuf[384] = {};
    swprintf_s(infoBuf,
        pl ? L"[%s]  \xD83D\xDCC2 %s   \x2022   %zu plików, %zu fold.   \x2022   %s \x2192 %s (%d%%)   (2\xD7klik = podgląd)"
           : L"[%s]  \xD83D\xDCC2 %s   \x2022   %zu files, %zu dirs   \x2022   %s \x2192 %s (%d%%)   (double-click = preview)",
        m_summary.formatName.c_str(), pathBadge.c_str(),
        m_summary.totalFiles, m_summary.totalFolders,
        unpStr.c_str(), cmpStr.c_str(), ratioPct);
    brush->SetColor(ArcColor(0x1E3A8A));
    rt->DrawText(infoBuf, static_cast<UINT32>(wcslen(infoBuf)), fmtSmall,
        D2D1::RectF(infoR.left + 8.0f, infoR.top + 4.0f, infoR.right - 8.0f, infoR.bottom), brush);

    // 3. Column Headers (22px)
    float colH = 22.0f;
    D2D1_RECT_F colR = D2D1::RectF(rect.left, infoR.bottom, rect.right, infoR.bottom + colH);
    brush->SetColor(ArcColor(0xF1F5F9));
    rt->FillRectangle(colR, brush);
    brush->SetColor(ArcColor(0xCBD5E1));
    rt->DrawLine(D2D1::Point2F(colR.left, colR.bottom - 0.5f), D2D1::Point2F(colR.right, colR.bottom - 0.5f), brush, 1.0f);

    float scrollW = 12.0f;
    float listRight = rect.right - scrollW;
    float packColW = 84.0f;
    float unpColW  = 84.0f;
    float nameRight = std::max(colR.left + 80.0f, listRight - packColW - unpColW);

    brush->SetColor(ArcColor(0x475569));
    rt->DrawText(pl ? L"Nazwa w archiwum" : L"Name in archive", pl ? 16 : 15, fmtBold,
        D2D1::RectF(colR.left + 10.0f, colR.top + 2.0f, nameRight - 4.0f, colR.bottom), brush);
    fmtBold->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    if (nameRight + unpColW <= listRight) {
        rt->DrawText(pl ? L"Rozmiar" : L"Size", pl ? 7 : 4, fmtBold,
            D2D1::RectF(nameRight, colR.top + 2.0f, nameRight + unpColW - 6.0f, colR.bottom), brush);
    }
    rt->DrawText(pl ? L"Skompres." : L"Packed", pl ? 9 : 6, fmtBold,
        D2D1::RectF(listRight - packColW, colR.top + 2.0f, listRight - 8.0f, colR.bottom), brush);
    fmtBold->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    // 4. Scrollable Archive Entries List
    m_listRect = D2D1::RectF(rect.left, colR.bottom, listRight, rect.bottom);
    float rowH = 22.0f;
    float listH = std::max(1.0f, m_listRect.bottom - m_listRect.top);
    float totalH = static_cast<float>(m_visibleItems.size()) * rowH;
    float maxScroll = std::max(0.0f, totalH - listH);
    m_scrollY = std::clamp(m_scrollY, 0.0f, maxScroll);

    rt->PushAxisAlignedClip(m_listRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    int firstIdx = std::max(0, static_cast<int>(m_scrollY / rowH));
    int lastIdx = std::min(static_cast<int>(m_visibleItems.size()) - 1,
                           static_cast<int>((m_scrollY + listH) / rowH) + 1);
    for (int i = firstIdx; i <= lastIdx; ++i) {
        const auto& item = m_visibleItems[i];
        float ry = m_listRect.top + i * rowH - m_scrollY;
        D2D1_RECT_F rowR = D2D1::RectF(m_listRect.left, ry, m_listRect.right, ry + rowH);

        if (i == m_selectedIdx) {
            brush->SetColor(ArcColor(0xCCE8FF));
            rt->FillRectangle(rowR, brush);
            brush->SetColor(ArcColor(0x0078D7));
            rt->DrawRectangle(D2D1::RectF(rowR.left + 0.5f, rowR.top + 0.5f, rowR.right - 0.5f, rowR.bottom - 0.5f), brush, 1.0f);
        } else if (i == m_hoverIdx) {
            brush->SetColor(ArcColor(0xE5F3FF));
            rt->FillRectangle(rowR, brush);
        } else if (i % 2 == 1) {
            brush->SetColor(ArcColor(0xF8FAFC));
            rt->FillRectangle(rowR, brush);
        }

        D2D1_RECT_F icoR = D2D1::RectF(rowR.left + 8.0f, rowR.top + 4.0f, rowR.left + 21.0f, rowR.bottom - 4.0f);
        if (item.isDirectory) {
            brush->SetColor(ArcColor(0xF59E0B));
            rt->FillRoundedRectangle(D2D1::RoundedRect(icoR, 2.0f, 2.0f), brush);
        } else {
            brush->SetColor(ArcColor(0x3B82F6));
            rt->DrawRoundedRectangle(D2D1::RoundedRect(icoR, 1.5f, 1.5f), brush, 1.2f);
        }

        brush->SetColor(ArcColor(0x0F172A));
        rt->DrawText(item.name.c_str(), static_cast<UINT32>(item.name.size()), fmtRegular,
            D2D1::RectF(rowR.left + 26.0f, rowR.top + 1.0f, nameRight - 4.0f, rowR.bottom), brush);

        fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        std::wstring uStr = PathUtils::FormatByteSize(item.uncompressedSize);
        std::wstring cStr = PathUtils::FormatByteSize(item.compressedSize);
        if (nameRight + unpColW <= listRight) {
            brush->SetColor(ArcColor(0x475569));
            rt->DrawText(uStr.c_str(), static_cast<UINT32>(uStr.size()), fmtSmall,
                D2D1::RectF(nameRight, rowR.top + 2.0f, nameRight + unpColW - 6.0f, rowR.bottom), brush);
        }
        brush->SetColor(ArcColor(0x64748B));
        rt->DrawText(cStr.c_str(), static_cast<UINT32>(cStr.size()), fmtSmall,
            D2D1::RectF(listRight - packColW, rowR.top + 2.0f, listRight - 8.0f, rowR.bottom), brush);
        fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }
    rt->PopAxisAlignedClip();

    // Scrollbar
    m_scrollTrackRect = D2D1::RectF(listRight, colR.bottom, rect.right, rect.bottom);
    brush->SetColor(ArcColor(0xF1F5F9));
    rt->FillRectangle(m_scrollTrackRect, brush);
    if (totalH > listH) {
        float thumbH = std::max(24.0f, listH * (listH / totalH));
        float frac = (maxScroll > 0.0f) ? (m_scrollY / maxScroll) : 0.0f;
        float thumbTop = m_scrollTrackRect.top + frac * (listH - thumbH);
        m_scrollThumbRect = D2D1::RectF(m_scrollTrackRect.left + 2.0f, thumbTop, m_scrollTrackRect.right - 2.0f, thumbTop + thumbH);
        brush->SetColor(m_draggingScroll ? ArcColor(0x0078D7) : ArcColor(0x94A3B8));
        rt->FillRoundedRectangle(D2D1::RoundedRect(m_scrollThumbRect, 3.0f, 3.0f), brush);
    } else {
        m_scrollThumbRect = {};
    }
}

bool ArchiveExplorer::OnMouseDown(float x, float y, bool rightButton, Lang lang, std::wstring* outStatusToast) {
    if (rightButton) return false;
    const bool pl = (lang == Lang::PL);

    for (const auto& hit : m_toolbarHits) {
        if (PathUtils::PtInRectF(x, y, hit.rect)) {
            switch (hit.id) {
            case BTN_UP_DIR: {
                size_t slash = m_currentInnerDir.find_last_of(L'/');
                if (slash == std::wstring::npos) m_currentInnerDir.clear();
                else m_currentInnerDir = m_currentInnerDir.substr(0, slash);
                RebuildCurrentSubfolderView();
                return true;
            }
            case BTN_OPEN_AS_FOLDER: {
                std::wstring target = m_archivePath;
                if (!m_currentInnerDir.empty()) {
                    std::wstring sub = m_currentInnerDir;
                    for (wchar_t& ch : sub) if (ch == L'/') ch = L'\\';
                    target = PathUtils::EnsureTrailingSlash(target) + sub;
                }
                m_pendingNavigateArchivePath = target;
                return true;
            }
            case BTN_EXTRACT_HERE: {
                std::wstring parentDir = PathUtils::GetParentPath(m_archivePath);
                if (ExtractEntireArchiveToFolder(m_archivePath, parentDir)) {
                    if (outStatusToast) *outStatusToast = (pl ? L"\x2713 Rozpakowano archiwum do: " : L"\x2713 Extracted archive to: ") + parentDir;
                }
                return true;
            }
            case BTN_EXTRACT_SUBFOLDER: {
                std::wstring parentDir = PathUtils::GetParentPath(m_archivePath);
                std::wstring stem = PathUtils::GetStem(PathUtils::GetFileName(m_archivePath), false);
                std::wstring subDir = PathUtils::EnsureTrailingSlash(parentDir) + stem;
                if (ExtractEntireArchiveToFolder(m_archivePath, subDir)) {
                    if (outStatusToast) *outStatusToast = (pl ? L"\x2713 Rozpakowano do folderu: " : L"\x2713 Extracted to subfolder: ") + subDir;
                }
                return true;
            }
            }
        }
    }

    if (PathUtils::PtInRectF(x, y, m_scrollTrackRect)) {
        if (PathUtils::PtInRectF(x, y, m_scrollThumbRect)) {
            m_draggingScroll = true;
            m_scrollDragOffset = y - m_scrollThumbRect.top;
        } else {
            float rowH = 22.0f;
            float listH = std::max(1.0f, m_listRect.bottom - m_listRect.top);
            float maxScroll = std::max(0.0f, static_cast<float>(m_visibleItems.size()) * rowH - listH);
            float frac = std::clamp((y - m_scrollTrackRect.top) / listH, 0.0f, 1.0f);
            m_scrollY = frac * maxScroll;
        }
        return true;
    }

    if (PathUtils::PtInRectF(x, y, m_listRect)) {
        int idx = static_cast<int>((y - m_listRect.top + m_scrollY) / 22.0f);
        if (idx >= 0 && idx < static_cast<int>(m_visibleItems.size())) {
            ULONGLONG now = GetTickCount64();
            bool isDbl = (idx == m_lastClickIdx && (now - m_lastClickTick) <= GetDoubleClickTime());
            m_selectedIdx = idx;
            m_lastClickIdx = idx;
            m_lastClickTick = now;

            if (isDbl) {
                const auto itemCopy = m_visibleItems[idx];
                if (itemCopy.isDirectory) {
                    m_currentInnerDir = itemCopy.pathInArchive;
                    RebuildCurrentSubfolderView();
                } else {
                    std::wstring tempFile;
                    if (ExtractSingleEntryToTempFile(m_archivePath, itemCopy.pathInArchive, tempFile)) {
                        m_pendingPreviewTempFile = tempFile;
                        if (outStatusToast) {
                            *outStatusToast = (pl ? L"Podgląd z archiwum: " : L"Preview from archive: ") + itemCopy.name;
                        }
                    }
                }
            }
            return true;
        }
    }

    return false;
}

bool ArchiveExplorer::OnMouseMove(float x, float y) {
    bool changed = false;
    int newBtn = 0;
    for (const auto& hit : m_toolbarHits) {
        if (PathUtils::PtInRectF(x, y, hit.rect)) {
            newBtn = hit.id;
            break;
        }
    }
    if (newBtn != m_hoverBtnId) {
        m_hoverBtnId = newBtn;
        changed = true;
    }

    if (m_draggingScroll) {
        float rowH = 22.0f;
        float listH = std::max(1.0f, m_listRect.bottom - m_listRect.top);
        float maxScroll = std::max(0.0f, static_cast<float>(m_visibleItems.size()) * rowH - listH);
        float thumbH = m_scrollThumbRect.bottom - m_scrollThumbRect.top;
        float usable = std::max(1.0f, listH - thumbH);
        float frac = std::clamp((y - m_scrollDragOffset - m_scrollTrackRect.top) / usable, 0.0f, 1.0f);
        m_scrollY = frac * maxScroll;
        return true;
    }

    int newHov = -1;
    if (PathUtils::PtInRectF(x, y, m_listRect)) {
        int idx = static_cast<int>((y - m_listRect.top + m_scrollY) / 22.0f);
        if (idx >= 0 && idx < static_cast<int>(m_visibleItems.size())) {
            newHov = idx;
        }
    }
    if (newHov != m_hoverIdx) {
        m_hoverIdx = newHov;
        changed = true;
    }
    return changed;
}

bool ArchiveExplorer::OnMouseUp() {
    if (m_draggingScroll) {
        m_draggingScroll = false;
        return true;
    }
    return false;
}

bool ArchiveExplorer::OnMouseWheel(short delta, float x, float y) {
    if (!PathUtils::PtInRectF(x, y, m_listRect)) return false;
    int notches = delta / WHEEL_DELTA;
    if (notches == 0) notches = (delta > 0) ? 1 : -1;
    float rowH = 22.0f;
    float listH = std::max(1.0f, m_listRect.bottom - m_listRect.top);
    float maxScroll = std::max(0.0f, static_cast<float>(m_visibleItems.size()) * rowH - listH);
    m_scrollY = std::clamp(m_scrollY - notches * rowH * 3.0f, 0.0f, maxScroll);
    return true;
}

} // namespace Explorer2
