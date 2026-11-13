#include "ArchiveExplorer.h"
#include "ReaderGuard.h"
#include "ZipDeflate.h"
#include "PathUtils.h"
#include "FileEngine.h"
#include "formats/ExtendedArchives100.h"
#include "formats/ExtendedArchives200.h"
#include <shellapi.h>
#include <algorithm>
#include <unordered_map>
#include <mutex>
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
           extLower == L".pk3"   || extLower == L".iso" || extLower == L".zst" ||
           extLower == L".tzst"  || extLower == L".xz"  || extLower == L".txz" ||
           extLower == L".bz2"   || extLower == L".tbz2"|| extLower == L".lz4" ||
           extLower == L".cab"   || extLower == L".msi" || extLower == L".msp" ||
           extLower == L".deb"   || extLower == L".rpm" || extLower == L".dmg" ||
           extLower == L".vhd"   || extLower == L".vhdx"|| extLower == L".vmdk"||
           extLower == L".vdi"   || extLower == L".qcow2"|| extLower == L".wim"||
           extLower == L".esd"   || extLower == L".pak" || extLower == L".wad" ||
           extLower == L".vpk"   || extLower == L".bsa" || extLower == L".ba2" ||
           extLower == L".asar"  || extLower == L".unitypackage" ||
           extLower == L".bundle"|| extLower == L".unity3d" || extLower == L".pck" ||
           extLower == L".rgss3a"|| extLower == L".rgss2a"  || extLower == L".rgssad" ||
           extLower == L".xp3"   || extLower == L".mpq" || extLower == L".big" ||
           extLower == L".grp"   || extLower == L".pbo" || extLower == L".scs" ||
           extLower == L".squashfs" || extLower == L".snap" || extLower == L".appimage" ||
           extLower == L".erofs" || extLower == L".cpio"|| extLower == L".ar"  ||
           extLower == L".a"     || extLower == L".lib" || extLower == L".xar" ||
           extLower == L".pkg"   || extLower == L".lzh" || extLower == L".lha" ||
           extLower == L".arj"   || extLower == L".ace" || extLower == L".ipa" ||
           extLower == L".aab"   || extLower == L".xapk"|| extLower == L".ova";
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

namespace {

// Zero-copy read-only memory-mapped file view for instant archive header scanning without heap allocation
struct MappedArchiveView {
    HANDLE hFile = INVALID_HANDLE_VALUE;
    HANDLE hMap  = nullptr;
    const uint8_t* data = nullptr;
    size_t size = 0;
    FILETIME ftWrite = {};

    bool Open(const std::wstring& path) {
        Close();
        hFile = CreateFileW(
            path.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (hFile == INVALID_HANDLE_VALUE) return false;

        LARGE_INTEGER li = {};
        if (!GetFileSizeEx(hFile, &li) || li.QuadPart < 4) {
            Close();
            return false;
        }
        GetFileTime(hFile, nullptr, nullptr, &ftWrite);
        size = static_cast<size_t>(li.QuadPart);

        hMap = CreateFileMappingW(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (!hMap) {
            Close();
            return false;
        }
        data = static_cast<const uint8_t*>(MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0));
        if (!data) {
            Close();
            return false;
        }
        return true;
    }

    void Close() {
        if (data) {
            UnmapViewOfFile(data);
            data = nullptr;
        }
        if (hMap) {
            CloseHandle(hMap);
            hMap = nullptr;
        }
        if (hFile != INVALID_HANDLE_VALUE) {
            CloseHandle(hFile);
            hFile = INVALID_HANDLE_VALUE;
        }
        size = 0;
    }

    ~MappedArchiveView() {
        Close();
    }
};

struct CachedArchiveSummary {
    uint64_t fileSize = 0;
    FILETIME ftWrite = {};
    ArchiveSummaryInfo info;
};

static std::mutex g_archiveCacheMutex;
static std::unordered_map<std::wstring, CachedArchiveSummary> g_archiveSummaryCache;

void FilterArchiveSummaryFolder(
    const ArchiveSummaryInfo& summary,
    const std::wstring& innerDir,
    std::vector<ArchiveItemEntry>& outImmediateChildren)
{
    outImmediateChildren.clear();
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
}

using ToolRun = ArchiveExplorer::ToolRun;

ToolRun RunSilentProcessWait(const std::wstring& exePath, const std::wstring& args, DWORD timeoutMs) {
    std::wstring cmdLine = L"\"" + exePath + L"\" " + args;
    std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back(L'\0');

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(
            exePath.c_str(),
            cmdBuf.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &si,
            &pi))
    {
        return ToolRun::Failed;
    }
    ToolRun result = ToolRun::Failed;
    if (WaitForSingleObject(pi.hProcess, timeoutMs) == WAIT_OBJECT_0) {
        DWORD exitCode = 1;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        if (exitCode == 0) result = ToolRun::Succeeded;
    } else {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 5000);     // its files are closed once it is gone
        result = ToolRun::TimedOut;
        AppLog::Warn(L"Archive", L"Stopped " + PathUtils::GetFileName(exePath) + L" after " +
                                 std::to_wstring(timeoutMs / 1000) + L" s without a result");
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return result;
}

std::wstring Find7ZipExePath() {
    static const wchar_t* kCandidates[] = {
        L"C:\\Program Files\\7-Zip\\7z.exe",
        L"C:\\Program Files (x86)\\7-Zip\\7z.exe"
    };
    for (const wchar_t* c : kCandidates) {
        if (GetFileAttributesW(c) != INVALID_FILE_ATTRIBUTES) return c;
    }
    return L"";
}

std::wstring FindUnRarExePath() {
    static const wchar_t* kCandidates[] = {
        L"C:\\Program Files\\WinRAR\\UnRAR.exe",
        L"C:\\Program Files\\WinRAR\\Rar.exe",
        L"C:\\Program Files (x86)\\WinRAR\\UnRAR.exe"
    };
    for (const wchar_t* c : kCandidates) {
        if (GetFileAttributesW(c) != INVALID_FILE_ATTRIBUTES) return c;
    }
    return L"";
}

// An archive decides the names of its entries, so they are never trusted. This turns an entry's path into
// a path below the destination folder and refuses anything that would land elsewhere: "..", an absolute
// path, or a name with a colon (a drive letter or an alternate data stream).
bool SafeRelativeEntryPath(const std::wstring& pathInArchive, std::wstring& outRelative) {
    outRelative.clear();
    size_t pos = 0;
    while (pos <= pathInArchive.size()) {
        size_t end = pathInArchive.find_first_of(L"/\\", pos);
        if (end == std::wstring::npos) end = pathInArchive.size();
        const std::wstring part = pathInArchive.substr(pos, end - pos);
        pos = end + 1;
        if (part.empty() || part == L".") continue;
        if (part == L".." || part.find(L':') != std::wstring::npos) return false;
        if (!outRelative.empty()) outRelative += L'\\';
        outRelative += part;
    }
    return !outRelative.empty();
}

// Entry names are also handed to 7-Zip, UnRAR and tar on a command line. A double quote would end the
// quoted argument and let the rest of the name be read as options, so such a name never reaches a tool.
bool IsSafeToolArgument(const std::wstring& name) {
    if (name.empty()) return false;
    for (wchar_t ch : name) {
        if (ch == L'"' || ch < 0x20) return false;
    }
    return true;
}

// A file name for the preview cache made from an entry's base name: characters Windows does not allow in
// a name (and the colon, which would address a stream) become underscores.
std::wstring SafeCacheFileName(std::wstring name) {
    for (wchar_t& ch : name) {
        if (ch < 0x20 || wcschr(L"<>:\"/\\|?*", ch) != nullptr) ch = L'_';
    }
    while (!name.empty() && (name.back() == L'.' || name.back() == L' ')) name.pop_back();
    if (name.empty() || name == L"." || name == L"..") name = L"extracted.bin";
    return name;
}

// Writes a new file and never replaces one that is already there: when the name is taken it uses
// "name (2).ext", "name (3).ext" and so on. Returns the path it wrote to.
bool WriteNewFileKeepExisting(const std::wstring& wantedPath, const uint8_t* data, size_t size, std::wstring& outWrittenPath) {
    std::wstring candidate = wantedPath;
    for (int attempt = 2; attempt < 1000; ++attempt) {
        HANDLE h = CreateFileW(candidate.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            bool ok = true;
            size_t left = size;
            const uint8_t* p = data;
            while (ok && left > 0) {
                const DWORD chunk = static_cast<DWORD>(std::min<size_t>(left, 16u * 1024u * 1024u));
                DWORD written = 0;
                ok = WriteFile(h, p, chunk, &written, nullptr) && written == chunk;
                p += chunk;
                left -= chunk;
            }
            CloseHandle(h);
            if (!ok) {
                DeleteFileW(candidate.c_str());   // an incomplete file would look like a good one
                return false;
            }
            outWrittenPath = candidate;
            return true;
        }
        const DWORD err = GetLastError();
        if (err != ERROR_FILE_EXISTS && err != ERROR_ALREADY_EXISTS) return false;

        const size_t slash = wantedPath.find_last_of(L'\\');
        size_t dot = wantedPath.find_last_of(L'.');
        if (dot == std::wstring::npos || (slash != std::wstring::npos && dot <= slash + 1)) dot = wantedPath.size();
        candidate = wantedPath.substr(0, dot) + L" (" + std::to_wstring(attempt) + L")" + wantedPath.substr(dot);
    }
    return false;
}

} // namespace

bool ArchiveExplorer::InspectArchiveFile(const std::wstring& archivePath, ArchiveSummaryInfo& outInfo) try {
    outInfo = {};
    std::wstring normKey = PathUtils::NormalizePathKey(archivePath);

    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    if (GetFileAttributesExW(archivePath.c_str(), GetFileExInfoStandard, &fad)) {
        uint64_t fsz = (static_cast<uint64_t>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
        std::lock_guard<std::mutex> lock(g_archiveCacheMutex);
        auto it = g_archiveSummaryCache.find(normKey);
        if (it != g_archiveSummaryCache.end() &&
            it->second.fileSize == fsz &&
            CompareFileTime(&it->second.ftWrite, &fad.ftLastWriteTime) == 0)
        {
            outInfo = it->second.info;
            return outInfo.valid;
        }
    }

    MappedArchiveView mapped;
    if (!mapped.Open(archivePath) || mapped.size < 4) {
        return false;
    }

    const uint8_t* data = mapped.data;
    size_t size = mapped.size;
    std::wstring extLower = PathUtils::GetExtension(archivePath);
    std::wstring nameLower = PathUtils::ToLower(PathUtils::GetFileName(archivePath));
    bool isTarGzName = (nameLower.size() > 7 && nameLower.substr(nameLower.size() - 7) == L".tar.gz") ||
                       (extLower == L".tgz");

    bool parsed = false;
    if (extLower == L".unitypackage" && Formats::DetailArc200::ParseUnityPackageFromMemory(data, size, outInfo)) {
        parsed = true;
    } else if (size >= 8 && std::memcmp(data, "Rar!\x1A\x07\x01\x00", 8) == 0) {
        parsed = ParseRar5Headers(data, size, outInfo);
    } else if (size >= 7 && std::memcmp(data, "Rar!\x1A\x07\x00", 7) == 0) {
        parsed = ParseRar4Headers(data, size, outInfo);
    } else if (size >= 6 && std::memcmp(data, "7z\xBC\xAF\x27\x1C", 6) == 0) {
        parsed = Parse7zHeaders(data, size, outInfo);
    } else if (size >= 4 && data[0] == 'P' && data[1] == 'K' &&
               ((data[2] == 3 && data[3] == 4) || (data[2] == 5 && data[3] == 6))) {
        std::vector<ZipDeflate::ArchiveItemMeta> zipEntries;
        if (ZipDeflate::ListZipEntriesFromMemory(data, size, zipEntries)) {
            outInfo.formatName = (extLower == L".jar")   ? L"JAR (ZIP)" :
                                 (extLower == L".apk")   ? L"APK (ZIP)" :
                                 (extLower == L".ipa")   ? L"iOS App Package (.ipa)" :
                                 (extLower == L".aab")   ? L"Android App Bundle (.aab)" :
                                 (extLower == L".xapk")  ? L"XAPK Package (.xapk)" :
                                 (extLower == L".scs")   ? L"SCS Mod Archive (.scs)" :
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
    } else if (extLower == L".tar" || extLower == L".ova" || isTarGzName ||
               (size >= 265 && std::memcmp(data + 257, "ustar", 5) == 0)) {
        std::vector<ZipDeflate::ArchiveItemMeta> tarEntries;
        if (ZipDeflate::ListTarEntriesFromFile(archivePath, tarEntries)) {
            outInfo.formatName = (extLower == L".ova") ? L"OVA Virtual Appliance (.ova)" :
                                 ((data[0] == 0x1F && data[1] == 0x8B) ? L"TAR.GZ" : L"TAR");
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
    } else {
        parsed = Formats::InspectExtendedArchive200(data, size, extLower, outInfo) ||
                 Formats::InspectExtendedArchive100(archivePath, data, size, extLower, outInfo);
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

    {
        std::lock_guard<std::mutex> lock(g_archiveCacheMutex);
        if (g_archiveSummaryCache.size() >= 32) {
            g_archiveSummaryCache.clear();
        }
        CachedArchiveSummary cached;
        cached.fileSize = mapped.size;
        cached.ftWrite = mapped.ftWrite;
        cached.info = outInfo;
        g_archiveSummaryCache[normKey] = std::move(cached);
    }
    return true;
} E2_READER_CATCH(L"Archive reader", archivePath, { outInfo = ArchiveSummaryInfo(); return false; })

bool ArchiveExplorer::EnumerateArchiveFolder(
    const std::wstring& archivePath,
    const std::wstring& innerDir,
    std::vector<ArchiveItemEntry>& outImmediateChildren,
    ArchiveSummaryInfo* outFullSummary)
try {
    outImmediateChildren.clear();
    ArchiveSummaryInfo summary;
    if (!InspectArchiveFile(archivePath, summary)) return false;

    FilterArchiveSummaryFolder(summary, innerDir, outImmediateChildren);

    if (outFullSummary) *outFullSummary = std::move(summary);
    return true;
} E2_READER_CATCH(L"Archive reader", archivePath, { outImmediateChildren.clear(); return false; })

bool ArchiveExplorer::ExtractSingleEntryToMemory(
    const std::wstring& archivePath,
    const std::wstring& innerPath,
    std::vector<uint8_t>& outBytes)
try {
    outBytes.clear();
    MappedArchiveView mapped;
    if (!mapped.Open(archivePath) || mapped.size < 4) {
        return false;
    }

    const uint8_t* data = mapped.data;
    size_t size = mapped.size;
    std::wstring normTarget = NormalizeArchiveInternalPath(innerPath);

    // 1. ZIP family
    if (size >= 4 && data[0] == 'P' && data[1] == 'K') {
        return ZipDeflate::ExtractZipFileByName(archivePath, normTarget, outBytes);
    }

    // 2. RAR4 / RAR5 (direct zero-copy slice for Stored uncompressed entries)
    if (size >= 7 && std::memcmp(data, "Rar!\x1A\x07", 6) == 0) {
        ArchiveSummaryInfo info;
        if (InspectArchiveFile(archivePath, info)) {
            for (const auto& e : info.entries) {
                if (!e.isDirectory && _wcsicmp(e.pathInArchive.c_str(), normTarget.c_str()) == 0) {
                    if ((info.formatName == L"RAR4" && e.compressionMethod == 0x30) ||
                        (info.formatName == L"RAR5" && e.compressionMethod == 0))
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

    // 3. 7-Zip (uncompressed raw stream without kStreamsInfo/kEncodedHeader, else decompressed via 7z.exe in ExtractSingleEntryToTempFile)
    if (size >= 32 && std::memcmp(data, "7z\xBC\xAF\x27\x1C", 6) == 0) {
        uint64_t nextHdrOffset = ReadU64LE(data + 12);
        uint64_t nextHdrSize   = ReadU64LE(data + 20);
        uint64_t absHdrPos     = 32ULL + nextHdrOffset;
        if (absHdrPos + nextHdrSize <= size && nextHdrSize > 0 && data[absHdrPos] == 0x01) {
            bool hasPackedStreams = false;
            for (size_t i = 0; i < nextHdrSize && i < 16; ++i) {
                if (data[absHdrPos + i] == 0x04 || data[absHdrPos + i] == 0x06 || data[absHdrPos + i] == 0x07) {
                    hasPackedStreams = true;
                    break;
                }
            }
            if (!hasPackedStreams) {
                ArchiveSummaryInfo info;
                if (InspectArchiveFile(archivePath, info)) {
                    for (const auto& e : info.entries) {
                        if (!e.isDirectory && _wcsicmp(e.pathInArchive.c_str(), normTarget.c_str()) == 0 &&
                            e.rawDataOffset + e.uncompressedSize <= size) {
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

    // 4. UnityPackage (.unitypackage -> <GUID>/pathname + <GUID>/asset)
    if (PathUtils::GetExtension(archivePath) == L".unitypackage") {
        ArchiveSummaryInfo dummy;
        if (Formats::DetailArc200::ParseUnityPackageFromMemory(data, size, dummy, &normTarget, &outBytes)) {
            return true;
        }
    }

    // 4b. TAR / TAR.GZ / OVA
    std::vector<ZipDeflate::ArchiveItemMeta> tarEntries;
    if (ZipDeflate::ListTarEntriesFromFile(archivePath, tarEntries) && !tarEntries.empty()) {
        for (const auto& te : tarEntries) {
            if (!te.isDirectory && _wcsicmp(NormalizeArchiveInternalPath(te.pathInArchive).c_str(), normTarget.c_str()) == 0) {
                return ZipDeflate::ExtractTarEntryFromFile(archivePath, te, outBytes);
            }
        }
        return false;
    }

    // 5. Single-file GZIP (.gz)
    if (size >= 18 && data[0] == 0x1F && data[1] == 0x8B) {
        return ZipDeflate::InflateGzip(data, size, outBytes);
    }

    // 6. Universal stored-slice extraction for ISO, PAK, WAD, ASAR, DEB, VPK, MSI/OLE2, WIM
    ArchiveSummaryInfo extInfo;
    if (InspectArchiveFile(archivePath, extInfo)) {
        for (const auto& e : extInfo.entries) {
            if (!e.isDirectory && e.compressionMethod == 0 &&
                _wcsicmp(e.pathInArchive.c_str(), normTarget.c_str()) == 0 &&
                e.rawDataOffset + e.uncompressedSize <= size)
            {
                outBytes.assign(
                    data + static_cast<size_t>(e.rawDataOffset),
                    data + static_cast<size_t>(e.rawDataOffset + e.uncompressedSize));
                return true;
            }
        }
    }

    return false;
} E2_READER_CATCH(L"Archive reader", archivePath, { outBytes.clear(); return false; })

bool ArchiveExplorer::ExtractSingleEntryToTempFile(
    const std::wstring& archivePath,
    const std::wstring& innerPath,
    std::wstring& outTempFilePath)
{
    outTempFilePath.clear();
    if (archivePath.empty() || innerPath.empty()) return false;

    wchar_t tmpRoot[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmpRoot);
    std::wstring previewRoot = std::wstring(tmpRoot) + L"Explorer2_ArcPreview";
    CreateDirectoryW(previewRoot.c_str(), nullptr);

    std::wstring normInner = NormalizeArchiveInternalPath(innerPath);
    std::wstring baseName = SafeCacheFileName(GetBaseNameFromInternalPath(normInner));

    // Build a deterministic per-(archive + entry + mtime + size) cache folder so concurrent worker threads
    // never collide on filenames and repeated thumbnail/preview/open requests hit the disk cache in 0 ms!
    uint64_t arcSize = 0;
    uint64_t arcMtime = 0;
    WIN32_FILE_ATTRIBUTE_DATA arcFad = {};
    if (GetFileAttributesExW(archivePath.c_str(), GetFileExInfoStandard, &arcFad)) {
        arcSize = (static_cast<uint64_t>(arcFad.nFileSizeHigh) << 32) | arcFad.nFileSizeLow;
        arcMtime = (static_cast<uint64_t>(arcFad.ftLastWriteTime.dwHighDateTime) << 32) | arcFad.ftLastWriteTime.dwLowDateTime;
    }

    std::wstring hashKey = PathUtils::NormalizePathKey(archivePath) + L"|" +
                           PathUtils::ToLower(normInner) + L"|" +
                           std::to_wstring(arcSize) + L"|" +
                           std::to_wstring(arcMtime);
    uint64_t fnv = 14695981039346656037ULL;
    for (wchar_t ch : hashKey) {
        fnv ^= static_cast<uint64_t>(ch);
        fnv *= 1099511628211ULL;
    }
    wchar_t hashHex[32] = {};
    swprintf_s(hashHex, L"%016llx", static_cast<unsigned long long>(fnv));

    std::wstring previewDir = previewRoot + L"\\" + hashHex;
    CreateDirectoryW(previewDir.c_str(), nullptr);
    std::wstring targetTemp = previewDir + L"\\" + baseName;

    auto isCachedValid = [](const std::wstring& p) -> bool {
        WIN32_FILE_ATTRIBUTE_DATA fad = {};
        if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fad)) return false;
        if (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return false;
        return (fad.nFileSizeHigh > 0 || fad.nFileSizeLow > 0);
    };

    if (isCachedValid(targetTemp)) {
        outTempFilePath = targetTemp;
        return true;
    }

    static std::mutex s_extractMutex;
    std::lock_guard<std::mutex> extractLock(s_extractMutex);

    if (isCachedValid(targetTemp)) {
        outTempFilePath = targetTemp;
        return true;
    }

    // 1. Try fast in-memory extraction first (ZIP, TAR, GZ, Stored RAR)
    std::vector<uint8_t> extracted;
    if (ExtractSingleEntryToMemory(archivePath, normInner, extracted)) {
        if (PathUtils::WriteFileBytes(targetTemp, extracted.data(), extracted.size())) {
            outTempFilePath = targetTemp;
            return true;
        }
    }

    // 2. Compressed RAR4 / RAR5 / 7z fallback via 7z.exe, UnRAR.exe, or Windows bsdtar (tar.exe)
    DeleteFileW(targetTemp.c_str());
    std::wstring winInner = normInner;
    for (wchar_t& ch : winInner) {
        if (ch == L'/') ch = L'\\';
    }

    // The entry name comes from the archive and goes onto a command line: refuse one that could be read as
    // anything but a name, and put "--" in front of the names so that a leading "-" or "@" is not an option.
    std::wstring safeRelative;
    if (!IsSafeToolArgument(winInner) || !SafeRelativeEntryPath(normInner, safeRelative)) return false;

    // This is for a preview, so a tool gets a few seconds. One that needs longer is stopped, and what it
    // wrote so far is removed: half a file must not be shown, and must not be found here as "already
    // unpacked" the next time.
    const DWORD kPreviewToolLimitMs = 8000;
    auto producedFile = [&](ToolRun run, const std::wstring& path) {
        if (run != ToolRun::TimedOut && isCachedValid(path)) return true;
        DeleteFileW(path.c_str());
        return false;
    };

    std::wstring sevenZ = Find7ZipExePath();
    if (!sevenZ.empty()) {
        std::wstring args = L"e -y -bd -o\"" + previewDir + L"\" -- \"" + archivePath + L"\" \"" + winInner + L"\"";
        if (producedFile(RunSilentProcessWait(sevenZ, args, kPreviewToolLimitMs), targetTemp)) {
            outTempFilePath = targetTemp;
            return true;
        }
    }

    std::wstring unrar = FindUnRarExePath();
    if (!unrar.empty()) {
        std::wstring args = L"e -y -inul -- \"" + archivePath + L"\" \"" + winInner + L"\" \"" + previewDir + L"\\\"";
        if (producedFile(RunSilentProcessWait(unrar, args, kPreviewToolLimitMs), targetTemp)) {
            outTempFilePath = targetTemp;
            return true;
        }
    }

    // Windows 10/11 built-in bsdtar (C:\Windows\System32\tar.exe) supports .rar, .7z, .zip, .tar.gz
    wchar_t sysDir[MAX_PATH] = {};
    GetSystemDirectoryW(sysDir, MAX_PATH);
    std::wstring tarExe = std::wstring(sysDir) + L"\\tar.exe";
    if (GetFileAttributesW(tarExe.c_str()) != INVALID_FILE_ATTRIBUTES) {
        std::wstring args = L"-xf \"" + archivePath + L"\" -C \"" + previewDir + L"\" -- \"" + normInner + L"\"";
        std::wstring fullNested = previewDir + L"\\" + safeRelative;
        if (producedFile(RunSilentProcessWait(tarExe, args, kPreviewToolLimitMs), fullNested)) {
            if (_wcsicmp(fullNested.c_str(), targetTemp.c_str()) != 0) {
                MoveFileExW(fullNested.c_str(), targetTemp.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);
            }
            if (GetFileAttributesW(targetTemp.c_str()) != INVALID_FILE_ATTRIBUTES) {
                outTempFilePath = targetTemp;
                return true;
            }
            outTempFilePath = fullNested;
            return true;
        }
    }

    return false;
}

ArchiveExplorer::ToolRun ArchiveExplorer::RunToolAndWait(const std::wstring& exePath, const std::wstring& args, DWORD timeoutMs) {
    return RunSilentProcessWait(exePath, args, timeoutMs);
}

bool ArchiveExplorer::ExtractEntireArchiveToFolder(
    const std::wstring& archivePath,
    const std::wstring& destFolderPath,
    size_t* outKeptExisting,
    size_t* outRefused)
{
    if (outKeptExisting) *outKeptExisting = 0;
    if (outRefused) *outRefused = 0;
    ArchiveSummaryInfo info;
    if (!InspectArchiveFile(archivePath, info) || !info.valid) return false;
    CreateDirectoryW(destFolderPath.c_str(), nullptr);

    bool anyExtracted = false;
    bool allExtractedInMemory = true;
    size_t keptExisting = 0;
    size_t refused = 0;
    for (const auto& entry : info.entries) {
        // The archive chooses these names, so each one is checked: nothing may be written outside the destination
        std::wstring relWin;
        if (!SafeRelativeEntryPath(entry.pathInArchive, relWin)) {
            ++refused;
            continue;
        }
        std::wstring fullDest = PathUtils::EnsureTrailingSlash(destFolderPath) + relWin;
        // Ensure the folders on the way exist (for a folder entry: the folder itself as well)
        size_t pos = destFolderPath.size() + 1;
        while (pos < fullDest.size()) {
            size_t nextSlash = fullDest.find(L'\\', pos);
            if (nextSlash == std::wstring::npos) break;
            std::wstring sub = fullDest.substr(0, nextSlash);
            CreateDirectoryW(sub.c_str(), nullptr);
            pos = nextSlash + 1;
        }
        if (entry.isDirectory) {
            CreateDirectoryW(fullDest.c_str(), nullptr);
            continue;
        }
        std::vector<uint8_t> fileData;
        if (ExtractSingleEntryToMemory(archivePath, entry.pathInArchive, fileData)) {
            // A file that is already in the destination is never replaced: the entry gets the next free name
            std::wstring writtenPath;
            if (WriteNewFileKeepExisting(fullDest, fileData.data(), fileData.size(), writtenPath)) {
                anyExtracted = true;
                if (_wcsicmp(writtenPath.c_str(), fullDest.c_str()) != 0) ++keptExisting;
            }
        } else {
            allExtractedInMemory = false;
        }
    }
    if (outRefused) *outRefused = refused;
    if (outKeptExisting) *outKeptExisting = keptExisting;

    if (!allExtractedInMemory) {
        // Some entries need an external tool. It is told to skip whatever is already in the destination, so
        // it adds what is missing, leaves alone what was unpacked above and never replaces a file of the user's.
        // No time limit: a large archive takes as long as it takes, and a tool that was cut off would leave
        // a half-written file that looks like a finished one. The caller runs this on a worker thread.
        std::wstring sevenZ = Find7ZipExePath();
        if (!sevenZ.empty()) {
            std::wstring args = L"x -y -aos -bd -o\"" + destFolderPath + L"\" -- \"" + archivePath + L"\"";
            if (RunSilentProcessWait(sevenZ, args, INFINITE) == ToolRun::Succeeded) return true;
        }
        std::wstring unrar = FindUnRarExePath();
        if (!unrar.empty()) {
            std::wstring args = L"x -o- -inul -- \"" + archivePath + L"\" \"" + PathUtils::EnsureTrailingSlash(destFolderPath) + L"\"";
            if (RunSilentProcessWait(unrar, args, INFINITE) == ToolRun::Succeeded) return true;
        }
        wchar_t sysDir[MAX_PATH] = {};
        GetSystemDirectoryW(sysDir, MAX_PATH);
        std::wstring tarExe = std::wstring(sysDir) + L"\\tar.exe";
        if (GetFileAttributesW(tarExe.c_str()) != INVALID_FILE_ATTRIBUTES) {
            std::wstring args = L"-xkf \"" + archivePath + L"\" -C \"" + destFolderPath + L"\"";
            if (RunSilentProcessWait(tarExe, args, INFINITE) == ToolRun::Succeeded) return true;
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

void ArchiveExplorer::ShowLoadedArchive(const std::wstring& fullPath, ArchiveSummaryInfo&& summary, std::vector<ArchiveItemEntry>&& rootItems) {
    Clear();
    m_archivePath = fullPath;
    m_currentInnerDir.clear();
    m_summary = std::move(summary);
    m_visibleItems = std::move(rootItems);
}

void ArchiveExplorer::RebuildCurrentSubfolderView() {
    m_visibleItems.clear();
    m_scrollY = 0.0f;
    m_selectedIdx = -1;
    m_hoverIdx = -1;
    if (m_summary.valid) {
        FilterArchiveSummaryFolder(m_summary, m_currentInnerDir, m_visibleItems);
    } else {
        EnumerateArchiveFolder(m_archivePath, m_currentInnerDir, m_visibleItems, &m_summary);
    }
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
            case BTN_EXTRACT_HERE:
            case BTN_EXTRACT_SUBFOLDER: {
                std::wstring target = PathUtils::GetParentPath(m_archivePath);
                if (hit.id == BTN_EXTRACT_SUBFOLDER) {
                    std::wstring stem = PathUtils::GetStem(PathUtils::GetFileName(m_archivePath), false);
                    target = PathUtils::EnsureTrailingSlash(target) + stem;
                }
                // Unpacking can take minutes, so it is not done here, on the UI thread: the pane records what
                // is wanted and its owner runs it in the background (see ExplorerApp::StartExtractJob)
                m_pendingExtract = { m_archivePath, target, hit.id == BTN_EXTRACT_SUBFOLDER };
                m_hasPendingExtract = true;
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
                m_lastClickIdx = -1;
                const auto itemCopy = m_visibleItems[idx];
                if (itemCopy.isDirectory) {
                    m_currentInnerDir = itemCopy.pathInArchive;
                    RebuildCurrentSubfolderView();
                } else {
                    m_pendingPreviewEntry.archivePath = m_archivePath;
                    m_pendingPreviewEntry.innerPath = itemCopy.pathInArchive;
                    m_pendingPreviewEntry.name = itemCopy.name;
                    m_pendingPreviewEntry.size = itemCopy.uncompressedSize;
                    m_hasPendingPreviewEntry = true;
                    if (outStatusToast) {
                        *outStatusToast = (pl ? L"Podgląd z archiwum: " : L"Preview from archive: ") + itemCopy.name;
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
