// ArchiveReaders.cpp - the archive readers behind ArchiveExplorer: listing and unpacking ZIP, RAR, 7z, TAR, CAB,
// ISO and the other containers (native decoders here and in formats/*.h, 7-Zip / UnRAR / tar for the rest).
// Split from the pane's UI (ArchiveExplorer.cpp) so that they build without it, also on Linux (see linux/).
#include "ArchiveReaders.h"
#include "VirtualFolders.h"
#include "FormatSupport.h"
#include "ReaderGuard.h"
#include "ZipDeflate.h"
#include "LzxDecoder.h"
#include "Ole2Compound.h"
#include "PathUtils.h"
#ifdef _WIN32
#include <setupapi.h>   // CAB (SetupIterateCabinetW)
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <map>
#include <set>
#include "formats/common/ArchiveFormatCommon.h"
#include "formats/archives/Iso9660.h"
#include "formats/archives/CabinetMscf.h"
#include "formats/archives/WindowsInstallerMsi.h"
#include "formats/archives/DebianDeb.h"
#include "formats/archives/RedHatRpm.h"
#include "formats/archives/Qcow2Disk.h"
#include "formats/archives/VhdxDisk.h"
#include "formats/archives/VhdDisk.h"
#include "formats/archives/VmdkDisk.h"
#include "formats/archives/VdiDisk.h"
#include "formats/archives/AppleDmgDisk.h"
#include "formats/common/VirtualDiskCommon.h"
#include "formats/archives/PosixTarArchive.h"
#include "formats/archives/WindowsWim.h"
#include "formats/archives/QuakePak.h"
#include "formats/archives/SilkroadPk2.h"
#include "formats/archives/GtaImg.h"
#include "formats/archives/DoomWad.h"
#include "formats/archives/ValveVpk.h"
#include "formats/archives/BethesdaBsa.h"
#include "formats/archives/BethesdaBa2.h"
#include "formats/archives/ElectronAsar.h"
#include "formats/archives/UnityPackage.h"
#include "formats/archives/UnityFsBundle.h"
#include "formats/archives/GodotPck.h"
#include "formats/archives/RpgMakerRgss.h"
#include "formats/archives/KiriKiriXp3.h"
#include "formats/archives/BlizzardMpq.h"
#include "formats/archives/EaBigArchive.h"
#include "formats/archives/BuildEngineGrp.h"
#include "formats/archives/BohemiaPbo.h"
#include "formats/archives/ScsPrismArchive.h"
#include "formats/archives/LinuxSquashFs.h"
#include "formats/archives/LinuxErofs.h"
#include "formats/archives/PosixCpio.h"
#include "formats/archives/UnixCoffAr.h"
#include "formats/archives/AppleXarPkg.h"
#include "formats/archives/LhaArchive.h"
#include "formats/archives/ArjArchive.h"
#include "formats/archives/WinAceArchive.h"
#include "formats/archives/UdfImage.h"
#include "formats/archives/NeroNrg.h"
#include "formats/archives/BrotliArchive.h"
#include "formats/archives/WebArchiveWarc.h"
#include "formats/archives/AndroidSparseImg.h"
#include "formats/archives/Rar4Archive.h"
#include "formats/archives/Rar5Archive.h"
#include "formats/archives/SevenZipArchive.h"
#include "formats/archives/GzipSingleArchive.h"
#include "formats/archives/BioWareErfArchive.h"
#include "formats/archives/BioWareRimArchive.h"
#include "formats/archives/BioWareBifArchive.h"
#include "formats/archives/QuakeHlWadArchive.h"
#ifdef _WIN32
#include <shellapi.h>
#endif
#include <algorithm>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <cstring>
#include <cstdio>

namespace Explorer2 {

namespace {

// Byte loads under this file's names (ByteIO holds the one implementation)
inline uint16_t ReadU16LE(const uint8_t* p) { return ::Explorer2::ByteIO::RdU16LE(p); }
inline uint32_t ReadU32LE(const uint8_t* p) { return ::Explorer2::ByteIO::RdU32LE(p); }
inline uint64_t ReadU64LE(const uint8_t* p) { return ::Explorer2::ByteIO::RdU64LE(p); }

inline uint32_t HighestBit32(uint32_t v) {
    uint32_t r = 0;
    while (v > 1u) {
        v >>= 1;
        ++r;
    }
    return r;
}

} // namespace


namespace {

// Native Stream + Inner TAR (.xz/.txz/.tar.xz, .bz2/.tbz2/.tbz/.tar.bz2,
// .lz4/.tlz4/.tar.lz4, .zst/.tzst/.tar.zst) Inspector & Extractor
// ============================================================================
enum class StreamKind { None, Xz, Bzip2, Lz4, Zstd };

StreamKind DetectStreamKind(const uint8_t* data, size_t size) {
    if (size >= 12 && std::memcmp(data, "\xFD" "7zXZ\x00", 6) == 0) return StreamKind::Xz;
    if (size >= 10 && data[0] == 'B' && data[1] == 'Z' && data[2] == 'h' && data[3] >= '1' && data[3] <= '9') {
        return StreamKind::Bzip2;
    }
    if (size >= 7) {
        uint32_t m = ReadU32LE(data);
        if (m == 0x184D2204u || m == 0x184C2102u) return StreamKind::Lz4;
        if (size >= 8 && m == 0xFD2FB528u) return StreamKind::Zstd;
    }
    return StreamKind::None;
}

struct CachedStreamBuffer {
    std::wstring normKey;
    uint64_t fileSize = 0;
    FILETIME ftWrite = {};
    std::vector<uint8_t> decompressed;
};
static std::mutex g_streamCacheMutex;
static CachedStreamBuffer g_streamCache;

bool DecompressStreamWithCache(
    const std::wstring& archivePath,
    const uint8_t* data,
    size_t size,
    FILETIME ftWrite,
    StreamKind kind,
    std::vector<uint8_t>& outBuf)
{
    std::wstring normKey = PathUtils::NormalizePathKey(archivePath);
    {
        std::lock_guard<std::mutex> lock(g_streamCacheMutex);
        if (g_streamCache.normKey == normKey &&
            g_streamCache.fileSize == size &&
            CompareFileTime(&g_streamCache.ftWrite, &ftWrite) == 0 &&
            !g_streamCache.decompressed.empty())
        {
            outBuf = g_streamCache.decompressed;
            return true;
        }
    }

    bool ok = false;
    switch (kind) {
    case StreamKind::Xz:    ok = StreamCodecs::DecompressXzStream(data, size, outBuf); break;
    case StreamKind::Bzip2: ok = StreamCodecs::DecompressBzip2Stream(data, size, outBuf); break;
    case StreamKind::Lz4:   ok = StreamCodecs::DecompressLz4Frame(data, size, outBuf); break;
    case StreamKind::Zstd:  ok = StreamCodecs::DecompressZstdFrame(data, size, outBuf); break;
    default: break;
    }
    if (!ok) return false;

    if (outBuf.size() <= 64 * 1024 * 1024) {
        std::lock_guard<std::mutex> lock(g_streamCacheMutex);
        g_streamCache.normKey = std::move(normKey);
        g_streamCache.fileSize = size;
        g_streamCache.ftWrite = ftWrite;
        g_streamCache.decompressed = outBuf;
    }
    return true;
}

std::wstring DeriveSingleStreamInnerName(const std::wstring& archivePath) {
    std::wstring fn = PathUtils::GetFileName(archivePath);
    std::wstring low = PathUtils::ToLower(fn);
    auto endsWith = [&](const wchar_t* suf) -> size_t {
        size_t sl = wcslen(suf);
        if (low.size() > sl && low.substr(low.size() - sl) == suf) return sl;
        return 0;
    };
    if (size_t n = endsWith(L".txz"))  return fn.substr(0, fn.size() - n) + L".tar";
    if (size_t n = endsWith(L".tbz2")) return fn.substr(0, fn.size() - n) + L".tar";
    if (size_t n = endsWith(L".tbz"))  return fn.substr(0, fn.size() - n) + L".tar";
    if (size_t n = endsWith(L".tlz4")) return fn.substr(0, fn.size() - n) + L".tar";
    if (size_t n = endsWith(L".tzst")) return fn.substr(0, fn.size() - n) + L".tar";
    if (size_t n = endsWith(L".xz"))   return fn.substr(0, fn.size() - n);
    if (size_t n = endsWith(L".bz2"))  return fn.substr(0, fn.size() - n);
    if (size_t n = endsWith(L".lz4"))  return fn.substr(0, fn.size() - n);
    if (size_t n = endsWith(L".zst"))  return fn.substr(0, fn.size() - n);
    return fn + L".extracted";
}

bool InspectCompressedStreamOrTar(
    const std::wstring& archivePath,
    const uint8_t* data,
    size_t size,
    FILETIME ftWrite,
    StreamKind kind,
    ArchiveSummaryInfo& outInfo)
{
    std::vector<uint8_t> dec;
    if (!DecompressStreamWithCache(archivePath, data, size, ftWrite, kind, dec)) return false;

    // Check if the decompressed stream is a POSIX TAR archive (.txz, .tbz2, .tzst, .tar.*, or magic ustar)
    ArchiveSummaryInfo tarInfo;
    if (dec.size() >= 512 && StreamCodecs::ParsePosixTarFromMemory(dec.data(), dec.size(), tarInfo) && !tarInfo.entries.empty()) {
        outInfo = std::move(tarInfo);
        switch (kind) {
        case StreamKind::Xz:    outInfo.formatName = L"TAR.XZ"; break;
        case StreamKind::Bzip2: outInfo.formatName = L"TAR.BZ2"; break;
        case StreamKind::Lz4:   outInfo.formatName = L"TAR.LZ4"; break;
        case StreamKind::Zstd:  outInfo.formatName = L"TAR.ZST"; break;
        default:                outInfo.formatName = L"TAR"; break;
        }
        uint64_t totalUnp = 0;
        for (const auto& e : outInfo.entries) if (!e.isDirectory) totalUnp += e.uncompressedSize;
        for (auto& e : outInfo.entries) {
            if (!e.isDirectory) {
                e.compressedSize = (totalUnp > 0) ? ((static_cast<uint64_t>(size) * e.uncompressedSize) / totalUnp) : size;
                e.compressionMethod = 8; // Compressed outer stream
            }
        }
        outInfo.valid = true;
        return true;
    }

    switch (kind) {
    case StreamKind::Xz:    outInfo.formatName = L"XZ Stream"; break;
    case StreamKind::Bzip2: outInfo.formatName = L"BZip2 Stream"; break;
    case StreamKind::Lz4:   outInfo.formatName = L"LZ4 Frame"; break;
    case StreamKind::Zstd:  outInfo.formatName = L"Zstandard (ZSTD)"; break;
    default: return false;
    }

    ArchiveItemEntry item;
    item.pathInArchive = ArchivePath::BaseName(ArchivePath::Normalize(DeriveSingleStreamInnerName(archivePath)));
    item.name = item.pathInArchive;
    item.uncompressedSize = dec.size();
    item.compressedSize = size;
    item.isDirectory = false;
    item.compressionMethod = 8;
    outInfo.entries.push_back(std::move(item));
    outInfo.valid = true;
    return true;
}

bool ExtractCompressedStreamOrTar(
    const std::wstring& archivePath,
    const uint8_t* data,
    size_t size,
    FILETIME ftWrite,
    StreamKind kind,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    std::vector<uint8_t> dec;
    if (!DecompressStreamWithCache(archivePath, data, size, ftWrite, kind, dec)) return false;

    ArchiveSummaryInfo tarProbe;
    if (dec.size() >= 512 && StreamCodecs::ParsePosixTarFromMemory(dec.data(), dec.size(), tarProbe) && !tarProbe.entries.empty()) {
        ArchiveSummaryInfo dummy;
        return StreamCodecs::ParsePosixTarFromMemory(dec.data(), dec.size(), dummy, &normTarget, &outBytes);
    }

    std::wstring expectedName = ArchivePath::BaseName(ArchivePath::Normalize(DeriveSingleStreamInnerName(archivePath)));
    if (_wcsicmp(normTarget.c_str(), expectedName.c_str()) == 0) {
        outBytes = std::move(dec);
        return true;
    }
    return false;
}

} // namespace

bool ArchiveReaders::IsArchiveExtension(const std::wstring& extLower) {
    return extLower == L".zip"   || extLower == L".rar" || extLower == L".7z"  ||
           extLower == L".tar"   || extLower == L".gz"  || extLower == L".tgz" ||
           extLower == L".jar"   || extLower == L".apk" || extLower == L".whl" ||
           extLower == L".nupkg" || extLower == L".cbz" || extLower == L".cbr" ||
           extLower == L".pk3"   || extLower == L".iso" || extLower == L".zst" ||
           extLower == L".pk4"   || extLower == L".iwd" || extLower == L".ipk3" ||   // ZIP: Doom 3, Call of Duty, GZDoom
           extLower == L".pk2"   || extLower == L".img" ||                           // Joymax PK2, GTA IMG (and raw disk images)
           extLower == L".tzst"  || extLower == L".xz"  || extLower == L".txz" ||
           extLower == L".bz2"   || extLower == L".tbz2"|| extLower == L".tbz" ||
           extLower == L".lz4"   || extLower == L".tlz4"|| extLower == L".br"  ||
           extLower == L".tbr"   || extLower == L".udf" || extLower == L".nrg" ||
           extLower == L".warc"  || extLower == L".simg"||
           extLower == L".cab"   || extLower == L".msi" || extLower == L".msp" ||
           extLower == L".deb"   || extLower == L".udeb"|| extLower == L".rpm" || extLower == L".dmg" ||
           extLower == L".hfs"   || extLower == L".ext2"|| extLower == L".ext3"|| extLower == L".ext4" ||
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
           extLower == L".aab"   || extLower == L".xapk"|| extLower == L".ova" ||
           extLower == L".erf"   || extLower == L".mod" || extLower == L".hak" ||
           extLower == L".nwm"   || extLower == L".sav" || extLower == L".rim" || extLower == L".bif" ||
           IsGamePakArchiveExtension(extLower);
}

bool ArchiveReaders::SplitVirtualArchivePath(
    const std::wstring& fullPath,
    std::wstring& outArchiveFile,
    std::wstring& outInnerPath)
{
    outArchiveFile.clear();
    outInnerPath.clear();
    if (fullPath.empty() || IsVirtualFolder(fullPath)) return false;

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
                    outInnerPath = ArchivePath::Normalize(clean.substr(nextSlash + 1));
                }
                return true;
            }
        }
        if (nextSlash == std::wstring::npos) break;
        pos = nextSlash + 1;
    }
    return false;
}

bool ArchiveReaders::IsVirtualArchivePath(const std::wstring& fullPath) {
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

} // namespace

void ArchiveReaders::FilterArchiveSummaryFolder(
    const ArchiveSummaryInfo& summary,
    const std::wstring& innerDir,
    std::vector<ArchiveItemEntry>& outImmediateChildren)
{
    outImmediateChildren.clear();
    std::wstring normDir = ArchivePath::Normalize(innerDir);
    std::wstring prefix = normDir.empty() ? L"" : (normDir + L"/");

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

namespace {

using ToolRun = ArchiveReaders::ToolRun;

ToolRun RunSilentProcessWait(const std::wstring& exePath, const std::wstring& args, DWORD timeoutMs) {
#ifndef _WIN32
    if (exePath.empty()) return ToolRun::Failed;
    const std::string exeUtf8 = Compat::NativePath(exePath.c_str());
    if (::access(exeUtf8.c_str(), X_OK) != 0) return ToolRun::Failed;

    std::vector<std::string> argvStorage;
    argvStorage.push_back(exeUtf8);
    // Split `args` with standard double-quote handling and normalize mixed "/tmp/a\\b" local paths
    const wchar_t* p = args.c_str();
    while (*p) {
        while (*p == L' ' || *p == L'\t') ++p;
        if (!*p) break;
        std::wstring cur;
        bool inQ = false;
        while (*p) {
            if (*p == L'"') { inQ = !inQ; ++p; }
            else if (*p == L'\\' && p[1] == L'"') { cur.push_back(L'"'); p += 2; }
            else if (!inQ && (*p == L' ' || *p == L'\t')) break;
            else cur.push_back(*p++);
        }
        std::string a = PathUtils::WideToUtf8(cur);
        auto fixLocal = [](std::string& s, size_t pfx) {
            if (s.size() > pfx + 1 && s[pfx] == '/' && s.find('\\', pfx) != std::string::npos) {
                std::replace(s.begin() + static_cast<std::ptrdiff_t>(pfx), s.end(), '\\', '/');
            }
        };
        fixLocal(a, 0);
        if (a.rfind("-o/", 0) == 0) fixLocal(a, 2);
        argvStorage.push_back(std::move(a));
    }
    std::vector<char*> argvPtrs;
    for (auto& s : argvStorage) argvPtrs.push_back(s.data());
    argvPtrs.push_back(nullptr);

    const pid_t pid = ::fork();
    if (pid < 0) return ToolRun::Failed;
    if (pid == 0) {
        ::setpgid(0, 0);
        // The app ignores SIGPIPE; an ignored signal stays ignored across exec, and the archiver expects the default
        ::signal(SIGPIPE, SIG_DFL);
        const int nul = ::open("/dev/null", O_RDWR);
        if (nul >= 0) {
            ::dup2(nul, STDIN_FILENO);
            ::dup2(nul, STDOUT_FILENO);
            ::dup2(nul, STDERR_FILENO);
            if (nul > 2) ::close(nul);
        }
        ::execvp(exeUtf8.c_str(), argvPtrs.data());
        ::_exit(127);
    }
    (void)::setpgid(pid, pid);
    const ULONGLONG start = GetTickCount64();
    int wstatus = 0;
    for (;;) {
        const pid_t w = ::waitpid(pid, &wstatus, WNOHANG);
        if (w == pid) {
            return (WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0) ? ToolRun::Succeeded : ToolRun::Failed;
        }
        if (w < 0 && errno != EINTR) return ToolRun::Failed;
        if (timeoutMs != INFINITE && GetTickCount64() - start > timeoutMs) {
            (void)::kill(-pid, SIGKILL);
            (void)::kill(pid, SIGKILL);
            while (::waitpid(pid, &wstatus, 0) < 0 && errno == EINTR) {}
            AppLog::Warn(L"Archive", L"Stopped " + PathUtils::GetFileName(exePath) + L" after " +
                                     std::to_wstring(timeoutMs / 1000) + L" s without a result");
            return ToolRun::TimedOut;
        }
        ::usleep(5000);
    }
#else
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
        WaitForSingleObject(pi.hProcess, 5000);
        result = ToolRun::TimedOut;
        AppLog::Warn(L"Archive", L"Stopped " + PathUtils::GetFileName(exePath) + L" after " +
                                 std::to_wstring(timeoutMs / 1000) + L" s without a result");
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return result;
#endif
}

std::wstring Find7ZipExePath() {
#ifndef _WIN32
    static const wchar_t* kLinuxCandidates[] = {
        L"/usr/bin/7z",
        L"/usr/bin/7zz",
        L"/usr/bin/7za",
        L"/usr/local/bin/7z"
    };
    for (const wchar_t* c : kLinuxCandidates) {
        if (::access(Compat::NativePath(c).c_str(), X_OK) == 0) return c;
    }
    return L"";
#else
    static const wchar_t* kCandidates[] = {
        L"C:\\Program Files\\7-Zip\\7z.exe",
        L"C:\\Program Files (x86)\\7-Zip\\7z.exe"
    };
    for (const wchar_t* c : kCandidates) {
        if (GetFileAttributesW(c) != INVALID_FILE_ATTRIBUTES) return c;
    }
    return L"";
#endif
}

std::wstring FindUnRarExePath() {
#ifndef _WIN32
    static const wchar_t* kLinuxCandidates[] = {
        L"/usr/bin/unrar",
        L"/usr/bin/unrar-nonfree",
        L"/usr/local/bin/unrar"
    };
    for (const wchar_t* c : kLinuxCandidates) {
        if (::access(Compat::NativePath(c).c_str(), X_OK) == 0) return c;
    }
    return L"";
#else
    static const wchar_t* kCandidates[] = {
        L"C:\\Program Files\\WinRAR\\UnRAR.exe",
        L"C:\\Program Files\\WinRAR\\Rar.exe",
        L"C:\\Program Files (x86)\\WinRAR\\UnRAR.exe"
    };
    for (const wchar_t* c : kCandidates) {
        if (GetFileAttributesW(c) != INVALID_FILE_ATTRIBUTES) return c;
    }
    return L"";
#endif
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
                DeleteFileW(candidate.c_str());
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

// Single-pass Win32 SetupIterateCabinetW bulk extractor for standalone .cab files
struct CabFolderExtractCtx {
    std::wstring destFolder;
    std::wstring scratchFile;
    std::wstring curRelWin;
    size_t keptExisting = 0;
    size_t refused = 0;
    size_t extractedCount = 0;
};

#ifdef _WIN32   // CAB extraction (SetupAPI): not part of the Linux build of the readers
UINT CALLBACK CabBulkIterateCallback(PVOID Context, UINT Notification, UINT_PTR Param1, UINT_PTR Param2) {
    (void)Param2;
    auto* ctx = static_cast<CabFolderExtractCtx*>(Context);
    switch (Notification) {
    case SPFILENOTIFY_CABINETINFO:
        return NO_ERROR;

    case SPFILENOTIFY_FILEINCABINET: {
        auto* info = reinterpret_cast<FILE_IN_CABINET_INFO_W*>(Param1);
        if (!info || !info->NameInCabinet) return FILEOP_SKIP;
        std::wstring entryNorm = ArchivePath::Normalize(info->NameInCabinet);
        if (!SafeRelativeEntryPath(entryNorm, ctx->curRelWin)) {
            ++ctx->refused;
            return FILEOP_SKIP;
        }
        if (ctx->scratchFile.size() >= MAX_PATH) return FILEOP_ABORT;
        DeleteFileW(ctx->scratchFile.c_str());
        wcscpy_s(info->FullTargetName, MAX_PATH, ctx->scratchFile.c_str());
        return FILEOP_DOIT;
    }

    case SPFILENOTIFY_FILEEXTRACTED: {
        auto* fp = reinterpret_cast<FILEPATHS_W*>(Param1);
        if (!fp || fp->Win32Error != NO_ERROR) {
            DeleteFileW(ctx->scratchFile.c_str());
            return fp ? fp->Win32Error : ERROR_GEN_FAILURE;
        }
        std::wstring fullDest = PathUtils::EnsureTrailingSlash(ctx->destFolder) + ctx->curRelWin;
        size_t pos = ctx->destFolder.size() + 1;
        while (pos < fullDest.size()) {
            size_t nextSlash = fullDest.find(L'\\', pos);
            if (nextSlash == std::wstring::npos) break;
            std::wstring sub = fullDest.substr(0, nextSlash);
            CreateDirectoryW(sub.c_str(), nullptr);
            pos = nextSlash + 1;
        }
        std::vector<uint8_t> bytes;
        if (PathUtils::ReadFileBytes(ctx->scratchFile, 256 * 1024 * 1024, bytes)) {
            std::wstring writtenPath;
            if (WriteNewFileKeepExisting(fullDest, bytes.data(), bytes.size(), writtenPath)) {
                ++ctx->extractedCount;
                if (_wcsicmp(writtenPath.c_str(), fullDest.c_str()) != 0) ++ctx->keptExisting;
            }
        }
        DeleteFileW(ctx->scratchFile.c_str());
        return NO_ERROR;
    }

    case SPFILENOTIFY_NEEDNEWCABINET:
        return ERROR_NOT_SUPPORTED;

    default:
        return NO_ERROR;
    }
}
#endif

bool DispatchVirtualDiskContainer(
    const uint8_t* data,
    size_t size,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* normTarget,
    std::vector<uint8_t>* outBytes)
{
    if (size >= 72 && std::memcmp(data, "QFI\xFB", 4) == 0) {
        return outInfo ? Formats::InspectQcow2Disk(data, size, *outInfo)
                       : Formats::ExtractQcow2DiskEntry(data, size, *normTarget, *outBytes);
    }
    if (size >= 65536 && std::memcmp(data, "vhdxfile", 8) == 0) {
        return outInfo ? Formats::InspectVhdxDisk(data, size, *outInfo)
                       : Formats::ExtractVhdxDiskEntry(data, size, *normTarget, *outBytes);
    }
    if (size >= 1024 && (std::memcmp(data + size - 512, "conectix", 8) == 0 || std::memcmp(data, "conectix", 8) == 0)) {
        return outInfo ? Formats::InspectVhdDisk(data, size, *outInfo)
                       : Formats::ExtractVhdDiskEntry(data, size, *normTarget, *outBytes);
    }
    if (size >= 512 && std::memcmp(data, "KDMV", 4) == 0) {
        return outInfo ? Formats::InspectVmdkDisk(data, size, *outInfo)
                       : Formats::ExtractVmdkDiskEntry(data, size, *normTarget, *outBytes);
    }
    if (size >= 512 && ByteIO::RdU32LE(data + 64) == 0xBEDA107Fu) {
        return outInfo ? Formats::InspectVdiDisk(data, size, *outInfo)
                       : Formats::ExtractVdiDiskEntry(data, size, *normTarget, *outBytes);
    }
    if (size >= 512 && std::memcmp(data + size - 512, "koly", 4) == 0) {
        return outInfo ? Formats::InspectAppleDmgDisk(data, size, *outInfo)
                       : Formats::ExtractAppleDmgDiskEntry(data, size, *normTarget, *outBytes);
    }
    return false;
}

bool DispatchUdfOrNrgImage(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* normTarget,
    std::vector<uint8_t>* outBytes)
{
    Formats::DetailDiskAndGameArc::VirtualDiskReader probeNrg;
    if (Formats::DetailUdfNrg::BuildNeroNrgReader(data, size, probeNrg)) {
        return Formats::ParseOrExtractNeroNrg(data, size, outInfo, normTarget, outBytes);
    }
    if (extLower == L".iso") {
        return outInfo ? Formats::InspectIsoOrHybridUdf(data, size, *outInfo)
                       : Formats::ExtractIsoOrHybridUdfEntry(data, size, *normTarget, *outBytes);
    }
    return Formats::ParseOrExtractUdfImage(data, size, extLower, outInfo, normTarget, outBytes);
}

} // namespace

bool ArchiveReaders::InspectArchiveFile(const std::wstring& archivePath, ArchiveSummaryInfo& outInfo) try {
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

    StreamKind streamKind = DetectStreamKind(data, size);

    bool parsed = false;
    if (Formats::InspectUnityPackage(data, size, extLower, outInfo)) {
        parsed = true;
    } else if (size >= 8 && std::memcmp(data, "Rar!\x1A\x07\x01\x00", 8) == 0) {
        parsed = Formats::ParseRar5Headers(data, size, outInfo);
    } else if (size >= 7 && std::memcmp(data, "Rar!\x1A\x07\x00", 7) == 0) {
        parsed = Formats::ParseRar4Headers(data, size, outInfo);
    } else if (size >= 6 && std::memcmp(data, "7z\xBC\xAF\x27\x1C", 6) == 0) {
        parsed = Formats::Parse7zHeaders(data, size, outInfo);
    } else if (streamKind != StreamKind::None) {
        parsed = InspectCompressedStreamOrTar(archivePath, data, size, mapped.ftWrite, streamKind, outInfo);
    } else if (size >= 4 && data[0] == 'P' && data[1] == 'K' &&
               ((data[2] == 3 && data[3] == 4) || (data[2] == 5 && data[3] == 6))) {
        std::vector<ZipDeflate::ArchiveItemMeta> zipEntries;
        if (ZipDeflate::ListZipEntriesFromMemory(data, size, zipEntries)) {
            outInfo.formatName = (extLower == L".jar")   ? L"JAR (ZIP)" :
                                 (extLower == L".pk3")   ? L"PK3 (Quake III engine package, ZIP)" :
                                 (extLower == L".pk4")   ? L"PK4 (Doom 3 engine package, ZIP)" :
                                 (extLower == L".iwd")   ? L"IWD (Call of Duty package, ZIP)" :
                                 (extLower == L".ipk3")  ? L"IPK3 (GZDoom package, ZIP)" :
                                 (extLower == L".apk")   ? L"APK (ZIP)" :
                                 (extLower == L".ipa")   ? L"iOS App Package (.ipa)" :
                                 (extLower == L".aab")   ? L"Android App Bundle (.aab)" :
                                 (extLower == L".xapk")  ? L"XAPK Package (.xapk)" :
                                 (extLower == L".scs")   ? L"SCS Mod Archive (.scs)" :
                                 (extLower == L".nupkg") ? L"NuGet (ZIP)" : L"ZIP";
            for (const auto& ze : zipEntries) {
                std::wstring norm = ArchivePath::Normalize(ze.pathInArchive);
                if (norm.empty()) continue;
                ArchiveItemEntry item;
                item.pathInArchive = norm;
                item.name = ArchivePath::BaseName(norm);
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
        parsed = Formats::InspectPosixTarOrTarGz(data, size, extLower, outInfo);
    } else if ((extLower == L".br" || extLower == L".tbr") &&
               Formats::ParseOrExtractBrotli(archivePath, data, size, extLower, &outInfo, nullptr, nullptr)) {
        parsed = true;
    } else if ((extLower == L".warc" ||
                (nameLower.size() > 8 && nameLower.substr(nameLower.size() - 8) == L".warc.gz")) &&
               Formats::ParseOrExtractWarc(archivePath, data, size, extLower, &outInfo, nullptr, nullptr)) {
        parsed = true;
    } else if (extLower == L".br" || extLower == L".tbr" || extLower == L".warc") {
        parsed = false;
    } else if (size >= 18 && data[0] == 0x1F && data[1] == 0x8B && data[2] == 8) {
        if (Formats::InspectPosixTarOrTarGz(data, size, extLower, outInfo)) {
            parsed = true;
        } else {
            parsed = Formats::ParseSingleGzip(archivePath, data, size, outInfo);
        }
    } else {
        parsed = Formats::InspectGtaImg(&archivePath, data, size, extLower, outInfo) ||
                 Formats::InspectSilkroadPk2(data, size, outInfo) ||
                 Formats::InspectQuakeHlWadArchive(data, size, outInfo) ||
                 Formats::InspectBioWareErfArchive(data, size, extLower, outInfo) ||
                 Formats::InspectBioWareRimArchive(data, size, outInfo) ||
                 Formats::InspectBioWareBifArchive(archivePath, data, size, outInfo) ||
                 (size >= 40u && ByteIO::RdU32LE(data) == 0xED26FF3Au &&
                  Formats::ParseOrExtractAndroidSimg(archivePath, data, size, &outInfo, nullptr, nullptr)) ||
                 Formats::ParseOrExtractWarc(archivePath, data, size, extLower, &outInfo, nullptr, nullptr) ||
                 DispatchUdfOrNrgImage(data, size, extLower, &outInfo, nullptr, nullptr) ||
                 Formats::InspectUnityFsBundle(data, size, outInfo) ||
                 Formats::InspectGodotPck(data, size, outInfo) ||
                 Formats::InspectRpgMakerRgss(data, size, outInfo) ||
                 Formats::InspectKiriKiriXp3(data, size, outInfo) ||
                 Formats::InspectBlizzardMpq(data, size, outInfo) ||
                 Formats::InspectEaBigArchive(data, size, outInfo) ||
                 Formats::InspectBuildEngineGrp(data, size, outInfo) ||
                 Formats::InspectBohemiaPbo(data, size, extLower, outInfo) ||
                 Formats::InspectScsPrismArchive(data, size, outInfo) ||
                 Formats::InspectLinuxSquashFs(data, size, extLower, outInfo) ||
                 Formats::InspectLinuxErofs(data, size, outInfo) ||
                 Formats::InspectPosixCpio(data, size, outInfo) ||
                 Formats::InspectUnixCoffAr(data, size, extLower, outInfo) ||
                 Formats::InspectAppleXarPkg(data, size, extLower, outInfo) ||
                 Formats::InspectLhaArchive(data, size, outInfo) ||
                 Formats::InspectArjArchive(data, size, outInfo) ||
                 Formats::InspectWinAceArchive(data, size, outInfo) ||
                 Formats::InspectIsoOrHybridUdf(data, size, outInfo) ||
                 Formats::InspectCabinetMscf(data, size, outInfo) ||
                 Formats::InspectDebianDeb(data, size, extLower, outInfo) ||
                 Formats::InspectRedHatRpm(data, size, outInfo) ||
                 Formats::InspectWindowsWim(data, size, outInfo) ||
                 Formats::InspectQuakePak(data, size, outInfo) ||
                 Formats::InspectDoomWad(data, size, outInfo) ||
                 Formats::InspectValveVpk(data, size, outInfo, &archivePath) ||
                 Formats::InspectBethesdaBsa(data, size, outInfo) ||
                 Formats::InspectBethesdaBa2(data, size, outInfo) ||
                 Formats::InspectElectronAsar(data, size, outInfo, &archivePath) ||
                 Formats::InspectWindowsInstallerMsi(data, size, extLower, outInfo) ||
                 DispatchVirtualDiskContainer(data, size, &outInfo, nullptr, nullptr) ||
                 TryInspectGamePakArchive(archivePath, data, size, extLower, outInfo);
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

bool ArchiveReaders::EnumerateArchiveFolder(
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

bool ArchiveReaders::ExtractSingleEntryToMemory(
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
    std::wstring extLower = PathUtils::GetExtension(archivePath);
    std::wstring normTarget = ArchivePath::Normalize(innerPath);

    // 1. ZIP family
    if (size >= 4 && data[0] == 'P' && data[1] == 'K') {
        return ZipDeflate::ExtractZipFileByName(archivePath, normTarget, outBytes);
    }

    // 2. RAR5 (in-process Store + v5 LZ+Huffman+Filter extractor) & RAR4 (Store slice)
    if (size >= 8 && std::memcmp(data, "Rar!\x1A\x07\x01\x00", 8) == 0) {
        return Formats::ExtractRar5EntryInProcess(data, size, normTarget, outBytes);
    }
    if (size >= 7 && std::memcmp(data, "Rar!\x1A\x07\x00", 7) == 0) {
        ArchiveSummaryInfo info;
        if (InspectArchiveFile(archivePath, info)) {
            for (const auto& e : info.entries) {
                if (!e.isDirectory && _wcsicmp(e.pathInArchive.c_str(), normTarget.c_str()) == 0) {
                    if (e.compressionMethod == 0x30 &&
                        e.rawDataOffset <= size && e.uncompressedSize <= size - e.rawDataOffset)
                    {
                        const uint8_t* p = data + static_cast<size_t>(e.rawDataOffset);
                        outBytes.assign(p, p + static_cast<size_t>(e.uncompressedSize));
                        return true;
                    }
                }
            }
        }
        return false;
    }

    // 3. 7-Zip (native in-process extraction for Copy, LZMA1, LZMA2, BCJ x86, BCJ2, Delta, Deflate, BZip2)
    if (size >= 32 && std::memcmp(data, "7z\xBC\xAF\x27\x1C", 6) == 0) {
        return Formats::Extract7zEntryInProcess(archivePath, data, size, normTarget, outBytes);
    }

    // 4. Native Compressed Streams & TAR.* (.xz/.txz, .bz2/.tbz2, .lz4/.tlz4, .zst/.tzst)
    StreamKind streamKind = DetectStreamKind(data, size);
    if (streamKind != StreamKind::None) {
        return ExtractCompressedStreamOrTar(archivePath, data, size, mapped.ftWrite, streamKind, normTarget, outBytes);
    }

    // 5. UnityPackage (.unitypackage -> <GUID>/pathname + <GUID>/asset)
    if (Formats::ExtractUnityPackageEntry(data, size, extLower, normTarget, outBytes)) {
        return true;
    }

    // 5b. Brotli (.br/.tbr/.tar.br), Android Sparse (.simg), WARC (.warc), and UDF / Nero (.udf/.nrg/.iso)
    if (extLower == L".br" || extLower == L".tbr") {
        return Formats::ParseOrExtractBrotli(archivePath, data, size, extLower, nullptr, &normTarget, &outBytes);
    }
    if (size >= 40u && ByteIO::RdU32LE(data) == 0xED26FF3Au) {
        if (Formats::ParseOrExtractAndroidSimg(archivePath, data, size, nullptr, &normTarget, &outBytes)) return true;
    }
    if (Formats::ParseOrExtractWarc(archivePath, data, size, extLower, nullptr, &normTarget, &outBytes)) {
        return true;
    }
    if (extLower == L".warc") {
        return false;
    }
    if (DispatchUdfOrNrgImage(data, size, extLower, nullptr, &normTarget, &outBytes)) {
        return true;
    }

    // 6. Specialized single-format extractors (.cab, .wim/.esd, .vpk, .asar, .bsa/.ba2, .deb, .rpm, .msi/.msp, .iso, .wad, virtual disks, game & FS archives)
    auto tryExtractDiskOrGameArc = [&]() -> bool {
        if (size >= 36 && std::memcmp(data, "MSCF\0\0\0\0", 8) == 0) {
            return Formats::ExtractCabinetMscfEntry(archivePath, data, size, normTarget, outBytes);
        }
        if (size >= 208 && std::memcmp(data, "MSWIM\0\0\0", 8) == 0) {
            return Formats::ExtractWindowsWimEntry(data, size, normTarget, outBytes);
        }
        if (size >= 12 && ByteIO::RdU32LE(data) == 0x55AA1234u) {
            return Formats::ExtractValveVpkEntry(archivePath, data, size, normTarget, outBytes);
        }
        if (extLower == L".asar" || (size >= 20 && ByteIO::RdU32LE(data) == 4u && data[16] == '{')) {
            if (Formats::ExtractElectronAsarEntry(archivePath, data, size, normTarget, outBytes)) return true;
            if (extLower == L".asar") return false;
        }
        if (size >= 36 && std::memcmp(data, "BSA\0", 4) == 0) {
            return Formats::ExtractBethesdaBsaEntry(data, size, normTarget, outBytes);
        }
        if (size >= 24 && std::memcmp(data, "BTDX", 4) == 0) {
            return Formats::ExtractBethesdaBa2Entry(data, size, normTarget, outBytes);
        }
        if (size >= 12 && (std::memcmp(data, "WAD2", 4) == 0 || std::memcmp(data, "WAD3", 4) == 0)) {
            return Formats::ExtractQuakeHlWadEntry(data, size, normTarget, outBytes);
        }
        if (size >= 12 && (std::memcmp(data, "IWAD", 4) == 0 || std::memcmp(data, "PWAD", 4) == 0)) {
            return Formats::ExtractDoomWadEntry(data, size, normTarget, outBytes);
        }
        if (Formats::DetailDiskAndGameArc::IsDebianDebArchive(data, size, extLower)) {
            return Formats::ExtractDebianDebEntry(data, size, extLower, normTarget, outBytes);
        }
        if (size >= 112 && data[0] == 0xED && data[1] == 0xAB && data[2] == 0xEE && data[3] == 0xDB) {
            return Formats::ExtractRedHatRpmEntry(data, size, normTarget, outBytes);
        }
        if (extLower == L".msi" || extLower == L".msp") {
            if (Formats::ExtractWindowsInstallerMsiEntry(data, size, extLower, normTarget, outBytes)) return true;
        }
        if (Formats::ExtractIsoOrHybridUdfEntry(data, size, normTarget, outBytes)) {
            return true;
        }
        return DispatchVirtualDiskContainer(data, size, nullptr, &normTarget, &outBytes);
    };
    if (tryExtractDiskOrGameArc()) {
        return true;
    }

    auto tryExtractUnixOrLegacyArc = [&]() -> bool {
        if (normTarget.empty()) return false;
        if (size >= 32 && std::memcmp(data, "UnityFS\0", 8) == 0) {
            return Formats::ExtractUnityFsBundleEntry(data, size, normTarget, outBytes);
        }
        if (size >= 12 && std::memcmp(data, "RGSSAD\0", 7) == 0) {
            return Formats::ExtractRpgMakerRgssEntry(data, size, normTarget, outBytes);
        }
        static const uint8_t kXp3Sig[11] = { 'X','P','3','\r','\n',' ','\n',0x1A,0x8B,0x67,0x01 };
        if (size >= 28 && std::memcmp(data, kXp3Sig, 11) == 0) {
            return Formats::ExtractKiriKiriXp3Entry(data, size, normTarget, outBytes);
        }
        if (size >= 32 && (std::memcmp(data, "MPQ\x1A", 4) == 0 || std::memcmp(data, "MPQ\x1B", 4) == 0)) {
            return Formats::ExtractBlizzardMpqEntry(data, size, normTarget, outBytes);
        }
        if (size >= 20 && std::memcmp(data, "SCS#", 4) == 0) {
            return Formats::ExtractScsPrismArchiveEntry(data, size, normTarget, outBytes);
        }
        if ((size >= 96 && std::memcmp(data, "hsqs", 4) == 0) ||
            extLower == L".squashfs" || extLower == L".snap" || extLower == L".appimage")
        {
            if (Formats::ExtractLinuxSquashFsEntry(data, size, extLower, normTarget, outBytes)) return true;
        }
        if ((size >= 1152 && ByteIO::RdU32LE(data + 1024) == 0xE0F5E1E2u) || extLower == L".erofs") {
            if (Formats::ExtractLinuxErofsEntry(data, size, normTarget, outBytes)) return true;
        }
        if (size >= 28 && std::memcmp(data, "xar!", 4) == 0) {
            return Formats::ExtractAppleXarPkgEntry(data, size, extLower, normTarget, outBytes);
        }
        if (size >= 16 && (std::memcmp(data, "BIGF", 4) == 0 || std::memcmp(data, "BIG4", 4) == 0)) {
            return Formats::ExtractEaBigArchiveEntry(data, size, normTarget, outBytes);
        }
        if (extLower == L".pbo") {
            return Formats::ExtractBohemiaPboEntry(data, size, extLower, normTarget, outBytes);
        }
        if (Formats::ExtractPosixCpioEntry(data, size, normTarget, outBytes)) {
            return true;
        }
        if (Formats::DetailUnixAndLegacyArc::IsUnixArButNotDeb(data, size, extLower)) {
            return Formats::ExtractUnixCoffArEntry(data, size, extLower, normTarget, outBytes);
        }
        if (size >= 24 && data[2] == '-' && (data[3] == 'l' || data[3] == 'p') && data[6] == '-') {
            return Formats::ExtractLhaArchiveEntry(data, size, normTarget, outBytes);
        }
        if (size >= 34 && data[0] == 0x60 && data[1] == 0xEA) {
            return Formats::ExtractArjArchiveEntry(data, size, normTarget, outBytes);
        }
        return false;
    };
    if (tryExtractUnixOrLegacyArc()) {
        return true;
    }
    if (IsGamePakArchiveExtension(extLower)) {
        return TryExtractGamePakArchive(archivePath, data, size, extLower, normTarget, outBytes);
    }
    if (TryExtractGamePakArchive(archivePath, data, size, extLower, normTarget, outBytes)) {
        return true;
    }

    // 7. TAR / TAR.GZ / OVA
    std::wstring nameLower = PathUtils::ToLower(PathUtils::GetFileName(archivePath));
    bool isTarGzName = (nameLower.size() > 7 && nameLower.substr(nameLower.size() - 7) == L".tar.gz") ||
                       (extLower == L".tgz");
    if (extLower == L".tar" || extLower == L".ova" || isTarGzName ||
        (size >= 265 && std::memcmp(data + 257, "ustar", 5) == 0) ||
        (size >= 18 && data[0] == 0x1F && data[1] == 0x8B && data[2] == 8))
    {
        if (Formats::ExtractPosixTarOrTarGzEntry(data, size, normTarget, outBytes)) {
            return true;
        }
        if (extLower == L".tar" || extLower == L".ova" || isTarGzName) {
            return false;
        }
    }

    // 8. Single-file (or multi-member) GZIP (.gz)
    if (size >= 18 && data[0] == 0x1F && data[1] == 0x8B) {
        return ZipDeflate::InflateGzip(data, size, outBytes);
    }

    // 9. Universal stored-slice extraction for PAK, BioWare ERF/RIM/BIF, PCK, GRP, etc.
    ArchiveSummaryInfo extInfo;
    if (InspectArchiveFile(archivePath, extInfo)) {
        for (const auto& e : extInfo.entries) {
            if (!e.isDirectory && e.compressionMethod == 0 &&
                _wcsicmp(e.pathInArchive.c_str(), normTarget.c_str()) == 0 &&
                e.rawDataOffset <= size && e.uncompressedSize <= size - e.rawDataOffset)
            {
                const uint8_t* p = data + static_cast<size_t>(e.rawDataOffset);
                outBytes.assign(p, p + static_cast<size_t>(e.uncompressedSize));
                return true;
            }
        }
    }

    return false;
} E2_READER_CATCH(L"Archive reader", archivePath, { outBytes.clear(); return false; })

bool ArchiveReaders::ExtractSingleEntryToTempFile(
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

    std::wstring normInner = ArchivePath::Normalize(innerPath);
    std::wstring baseName = SafeCacheFileName(ArchivePath::BaseName(normInner));

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

    std::wstring previewDir = PathUtils::EnsureTrailingSlash(previewRoot) + hashHex;
    CreateDirectoryW(previewDir.c_str(), nullptr);
    std::wstring targetTemp = PathUtils::EnsureTrailingSlash(previewDir) + baseName;

    bool allowZeroByteEntry = false;
    {
        ArchiveSummaryInfo summaryProbe;
        if (InspectArchiveFile(archivePath, summaryProbe)) {
            for (const auto& e : summaryProbe.entries) {
                if (!e.isDirectory && _wcsicmp(e.pathInArchive.c_str(), normInner.c_str()) == 0) {
                    if (e.uncompressedSize == 0ULL) allowZeroByteEntry = true;
                    break;
                }
            }
        }
    }

    auto isCachedValid = [allowZeroByteEntry](const std::wstring& p) -> bool {
        WIN32_FILE_ATTRIBUTE_DATA fad = {};
        if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fad)) return false;
        if (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return false;
        return allowZeroByteEntry || (fad.nFileSizeHigh > 0 || fad.nFileSizeLow > 0);
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

    // 1. Try fast in-memory extraction first (ZIP, 7z, RAR5, CAB, TAR.*, XZ, BZ2, LZ4, ZST, BSA, BA2, RPM, MSI, Disk Images, Retro/System Archives)
    std::vector<uint8_t> extracted;
    if (ExtractSingleEntryToMemory(archivePath, normInner, extracted)) {
        const uint8_t dummyByte = 0u;
        const uint8_t* writePtr = extracted.empty() ? &dummyByte : extracted.data();
        if (PathUtils::WriteFileBytes(targetTemp, writePtr, extracted.size())) {
            outTempFilePath = targetTemp;
            return true;
        }
    }

    // 2. Compressed RAR4 / RAR5 / AES-encrypted 7z fallback via 7z.exe, UnRAR.exe, or Windows bsdtar (tar.exe)
    DeleteFileW(targetTemp.c_str());
    std::wstring winInner = normInner;
    for (wchar_t& ch : winInner) {
        if (ch == L'/') ch = L'\\';
    }

    std::wstring safeRelative;
    if (!IsSafeToolArgument(winInner) || !SafeRelativeEntryPath(normInner, safeRelative)) return false;

    const DWORD kPreviewToolLimitMs = 8000;
    auto producedFile = [&](ToolRun run, const std::wstring& path) {
        if (run != ToolRun::TimedOut && isCachedValid(path)) return true;
        DeleteFileW(path.c_str());
        return false;
    };

    std::wstring sevenZ = Find7ZipExePath();
    if (!sevenZ.empty()) {
#ifndef _WIN32
        const std::wstring innerArg = normInner;
#else
        const std::wstring& innerArg = winInner;
#endif
        std::wstring args = L"e -y -bd -o\"" + previewDir + L"\" -- \"" + archivePath + L"\" \"" + innerArg + L"\"";
        if (producedFile(RunSilentProcessWait(sevenZ, args, kPreviewToolLimitMs), targetTemp)) {
            outTempFilePath = targetTemp;
            return true;
        }
    }

    std::wstring unrar = FindUnRarExePath();
    if (!unrar.empty()) {
#ifndef _WIN32
        const std::wstring innerArg = normInner;
#else
        const std::wstring& innerArg = winInner;
#endif
        std::wstring args = L"e -y -inul -- \"" + archivePath + L"\" \"" + innerArg + L"\" \"" + PathUtils::EnsureTrailingSlash(previewDir) + L"\"";
        if (producedFile(RunSilentProcessWait(unrar, args, kPreviewToolLimitMs), targetTemp)) {
            outTempFilePath = targetTemp;
            return true;
        }
    }

#ifndef _WIN32
    std::wstring tarExe = (::access("/bin/tar", X_OK) == 0) ? L"/bin/tar" : L"/usr/bin/tar";
#else
    wchar_t sysDir[MAX_PATH] = {};
    GetSystemDirectoryW(sysDir, MAX_PATH);
    std::wstring tarExe = std::wstring(sysDir) + L"\\tar.exe";
#endif
    if (GetFileAttributesW(tarExe.c_str()) != INVALID_FILE_ATTRIBUTES) {
        std::wstring args = L"-xf \"" + archivePath + L"\" -C \"" + previewDir + L"\" -- \"" + normInner + L"\"";
        std::wstring fullNested = PathUtils::EnsureTrailingSlash(previewDir) + safeRelative;
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

ArchiveReaders::ToolRun ArchiveReaders::RunToolAndWait(const std::wstring& exePath, const std::wstring& args, DWORD timeoutMs) {
    return RunSilentProcessWait(exePath, args, timeoutMs);
}

bool ArchiveReaders::ExtractEntireArchiveToFolder(
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

#ifdef _WIN32
    // Fast single-pass SetupIterateCabinetW path for standalone Windows Cabinet (.cab) archives
    if (info.formatName == L"MS Cabinet (CAB)") {
        CabFolderExtractCtx cabCtx;
        cabCtx.destFolder = destFolderPath;
        cabCtx.scratchFile = StreamCodecs::MakeUniqueCabScratchPath(L".tmp");
        BOOL cabOk = SetupIterateCabinetW(archivePath.c_str(), 0, CabBulkIterateCallback, &cabCtx);
        DeleteFileW(cabCtx.scratchFile.c_str());
        if (cabOk != FALSE && cabCtx.extractedCount > 0) {
            if (outKeptExisting) *outKeptExisting = cabCtx.keptExisting;
            if (outRefused) *outRefused = cabCtx.refused;
            return true;
        }
    }
#endif

    bool anyExtracted = false;
    bool allExtractedInMemory = true;
    size_t keptExisting = 0;
    size_t refused = 0;
    for (const auto& entry : info.entries) {
        std::wstring relWin;
        if (!SafeRelativeEntryPath(entry.pathInArchive, relWin)) {
            ++refused;
            continue;
        }
        std::wstring fullDest = PathUtils::EnsureTrailingSlash(destFolderPath) + relWin;
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
#ifndef _WIN32
        std::wstring tarExe = (::access("/bin/tar", X_OK) == 0) ? L"/bin/tar" : L"/usr/bin/tar";
#else
        wchar_t sysDir[MAX_PATH] = {};
        GetSystemDirectoryW(sysDir, MAX_PATH);
        std::wstring tarExe = std::wstring(sysDir) + L"\\tar.exe";
#endif
        if (GetFileAttributesW(tarExe.c_str()) != INVALID_FILE_ATTRIBUTES) {
            std::wstring args = L"-xkf \"" + archivePath + L"\" -C \"" + destFolderPath + L"\"";
            if (RunSilentProcessWait(tarExe, args, INFINITE) == ToolRun::Succeeded) return true;
        }
    }
    return anyExtracted;
}

} // namespace Explorer2
