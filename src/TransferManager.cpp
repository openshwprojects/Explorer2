#include "TransferManager.h"
#include "AppLog.h"
#include "FileEngine.h"
#include "FtpClient.h"
#include "PathUtils.h"
#include <winioctl.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#ifndef COPY_FILE_REQUEST_COMPRESSED_TRAFFIC
#define COPY_FILE_REQUEST_COMPRESSED_TRAFFIC 0x10000000
#endif

#ifndef FSCTL_DUPLICATE_EXTENTS_TO_FILE
#define FSCTL_DUPLICATE_EXTENTS_TO_FILE CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 209, METHOD_BUFFERED, FILE_WRITE_DATA)
struct MY_DUPLICATE_EXTENTS_DATA {
    HANDLE FileHandle;
    LARGE_INTEGER SourceFileOffset;
    LARGE_INTEGER TargetFileOffset;
    LARGE_INTEGER ByteCount;
};
#else
using MY_DUPLICATE_EXTENTS_DATA = DUPLICATE_EXTENTS_DATA;
#endif

#ifndef FILE_SUPPORTS_BLOCK_REFCOUNTING
#define FILE_SUPPORTS_BLOCK_REFCOUNTING 0x08000000
#endif

namespace Explorer2 {

namespace {

// ============================================================================
// Streaming 64-bit xxHash (XXH64) — >12 GB/s integrity checksum engine
// ============================================================================
class XxHash64Stream {
public:
    explicit XxHash64Stream(uint64_t seed = 0) : m_seed(seed) {
        m_v1 = seed + kPrime1 + kPrime2;
        m_v2 = seed + kPrime2;
        m_v3 = seed + 0;
        m_v4 = seed - kPrime1;
    }

    void Update(const void* input, size_t len) {
        if (!input || len == 0) return;
        const uint8_t* p = static_cast<const uint8_t*>(input);
        const uint8_t* const bEnd = p + len;
        m_totalLen += len;

        if (m_memSize + len < 32) {
            std::memcpy(m_mem + m_memSize, p, len);
            m_memSize += static_cast<uint32_t>(len);
            return;
        }

        if (m_memSize > 0) {
            size_t fill = 32 - m_memSize;
            std::memcpy(m_mem + m_memSize, p, fill);
            const uint8_t* mp = m_mem;
            m_v1 = Round(m_v1, Read64LE(mp));      mp += 8;
            m_v2 = Round(m_v2, Read64LE(mp));      mp += 8;
            m_v3 = Round(m_v3, Read64LE(mp));      mp += 8;
            m_v4 = Round(m_v4, Read64LE(mp));
            p += fill;
            m_memSize = 0;
        }

        if (p + 32 <= bEnd) {
            const uint8_t* const limit = bEnd - 32;
            uint64_t v1 = m_v1, v2 = m_v2, v3 = m_v3, v4 = m_v4;
            do {
                v1 = Round(v1, Read64LE(p)); p += 8;
                v2 = Round(v2, Read64LE(p)); p += 8;
                v3 = Round(v3, Read64LE(p)); p += 8;
                v4 = Round(v4, Read64LE(p)); p += 8;
            } while (p <= limit);
            m_v1 = v1; m_v2 = v2; m_v3 = v3; m_v4 = v4;
        }

        if (p < bEnd) {
            size_t rem = static_cast<size_t>(bEnd - p);
            std::memcpy(m_mem, p, rem);
            m_memSize = static_cast<uint32_t>(rem);
        }
    }

    uint64_t Digest() const {
        uint64_t h64 = 0;
        if (m_totalLen >= 32) {
            h64 = Rotl64(m_v1, 1) + Rotl64(m_v2, 7) + Rotl64(m_v3, 12) + Rotl64(m_v4, 18);
            h64 = MergeRound(h64, m_v1);
            h64 = MergeRound(h64, m_v2);
            h64 = MergeRound(h64, m_v3);
            h64 = MergeRound(h64, m_v4);
        } else {
            h64 = m_seed + kPrime5;
        }

        h64 += m_totalLen;

        const uint8_t* p = m_mem;
        const uint8_t* const bEnd = m_mem + m_memSize;
        while (p + 8 <= bEnd) {
            uint64_t k1 = Round(0, Read64LE(p));
            h64 ^= k1;
            h64 = Rotl64(h64, 27) * kPrime1 + kPrime4;
            p += 8;
        }
        if (p + 4 <= bEnd) {
            h64 ^= static_cast<uint64_t>(Read32LE(p)) * kPrime1;
            h64 = Rotl64(h64, 23) * kPrime2 + kPrime3;
            p += 4;
        }
        while (p < bEnd) {
            h64 ^= static_cast<uint64_t>(*p) * kPrime5;
            h64 = Rotl64(h64, 11) * kPrime1;
            ++p;
        }

        h64 ^= h64 >> 33;
        h64 *= kPrime2;
        h64 ^= h64 >> 29;
        h64 *= kPrime3;
        h64 ^= h64 >> 32;
        return h64;
    }

private:
    static constexpr uint64_t kPrime1 = 11400714785074694791ULL;
    static constexpr uint64_t kPrime2 = 14029467366897019727ULL;
    static constexpr uint64_t kPrime3 =  1609587929392839161ULL;
    static constexpr uint64_t kPrime4 =  9650029242287828579ULL;
    static constexpr uint64_t kPrime5 =  2870177450012600261ULL;

    static uint64_t Rotl64(uint64_t x, int r) {
        return (x << r) | (x >> (64 - r));
    }
    static uint64_t Read64LE(const uint8_t* p) {
        uint64_t v = 0;
        std::memcpy(&v, p, sizeof(v));
        return v;
    }
    static uint32_t Read32LE(const uint8_t* p) {
        uint32_t v = 0;
        std::memcpy(&v, p, sizeof(v));
        return v;
    }
    static uint64_t Round(uint64_t acc, uint64_t input) {
        acc += input * kPrime2;
        acc = Rotl64(acc, 31);
        acc *= kPrime1;
        return acc;
    }
    static uint64_t MergeRound(uint64_t acc, uint64_t val) {
        val = Round(0, val);
        acc ^= val;
        acc = acc * kPrime1 + kPrime4;
        return acc;
    }

    uint64_t m_seed = 0;
    uint64_t m_totalLen = 0;
    uint64_t m_v1 = 0, m_v2 = 0, m_v3 = 0, m_v4 = 0;
    uint8_t  m_mem[32] = {};
    uint32_t m_memSize = 0;
};

// ============================================================================
// Extended Win32 path & error helpers
// ============================================================================
std::wstring ToExtPath(const std::wstring& raw) {
    if (raw.empty()) return L"";
    if (raw.rfind(L"\\\\?\\", 0) == 0) return raw;
    std::wstring p = raw;
    for (wchar_t& ch : p) {
        if (ch == L'/') ch = L'\\';
    }
    if (p.rfind(L"\\\\", 0) == 0) {
        return L"\\\\?\\UNC\\" + p.substr(2);
    }
    if (p.size() >= 2 && p[1] == L':') {
        if (p.size() == 2) p.push_back(L'\\');
        return L"\\\\?\\" + p;
    }
    return p;
}

std::wstring FormatWin32Err(DWORD err) {
    if (err == 0) return L"OK";
    wchar_t buf[512] = {};
    DWORD n = FormatMessageW(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err, 0, buf, static_cast<DWORD>(std::size(buf)), nullptr);
    std::wstring s = (n > 0) ? PathUtils::Trim(buf) : L"";
    if (s.empty()) return L"Win32 Error " + std::to_wstring(err);
    return s + L" (" + std::to_wstring(err) + L")";
}

bool IsTransientIoError(DWORD err) {
    switch (err) {
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
    case ERROR_NETWORK_BUSY:
    case ERROR_NETNAME_DELETED:
    case ERROR_UNEXP_NET_ERR:
    case ERROR_REM_NOT_LIST:
    case ERROR_SEM_TIMEOUT:
    case ERROR_VC_DISCONNECTED:
    case ERROR_BAD_NET_RESP:
    case ERROR_DEV_NOT_EXIST:
    case ERROR_TOO_MANY_CMDS:
        return true;
    default:
        return false;
    }
}

bool AreFileTimesWithin2Sec(const FILETIME& a, const FILETIME& b) {
    ULARGE_INTEGER ua = {}, ub = {};
    ua.LowPart = a.dwLowDateTime; ua.HighPart = a.dwHighDateTime;
    ub.LowPart = b.dwLowDateTime; ub.HighPart = b.dwHighDateTime;
    uint64_t diff = (ua.QuadPart > ub.QuadPart) ? (ua.QuadPart - ub.QuadPart) : (ub.QuadPart - ua.QuadPart);
    return diff <= 20000000ULL; // 2 seconds in 100ns ticks (FAT32/exFAT/SMB tolerance)
}

bool EnsureDirHierarchyExt(const std::wstring& dirPath) {
    std::wstring clean = PathUtils::TrimTrailingSlashes(dirPath);
    if (clean.empty()) return false;
    std::wstring ext = ToExtPath(clean);
    DWORD attrs = GetFileAttributesW(ext.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        return true;
    }
    std::wstring parent = PathUtils::GetParentPath(clean);
    if (!parent.empty() && parent != clean) {
        EnsureDirHierarchyExt(parent);
    }
    if (CreateDirectoryW(ext.c_str(), nullptr)) return true;
    DWORD err = GetLastError();
    return err == ERROR_ALREADY_EXISTS;
}

bool EnsureDirHierarchyTrackFresh(
    const std::wstring& dirPath,
    std::unordered_set<std::wstring>& knownExistingNorm,
    std::unordered_set<std::wstring>& newlyCreatedNorm)
{
    std::wstring clean = PathUtils::TrimTrailingSlashes(dirPath);
    if (clean.empty()) return false;
    std::wstring norm = PathUtils::NormalizePathKey(clean);
    if (knownExistingNorm.find(norm) != knownExistingNorm.end()) {
        return true;
    }
    if (clean.size() == 2 && clean[1] == L':') {
        knownExistingNorm.insert(std::move(norm));
        return true;
    }
    std::wstring parent = PathUtils::GetParentPath(clean);
    if (!parent.empty() && parent != clean) {
        EnsureDirHierarchyTrackFresh(parent, knownExistingNorm, newlyCreatedNorm);
    }
    std::wstring ext = ToExtPath(clean);
    if (CreateDirectoryW(ext.c_str(), nullptr)) {
        newlyCreatedNorm.insert(norm);
        knownExistingNorm.insert(std::move(norm));
        return true;
    }
    DWORD err = GetLastError();
    if (err == ERROR_ALREADY_EXISTS) {
        knownExistingNorm.insert(std::move(norm));
        return true;
    }
    DWORD attrs = GetFileAttributesW(ext.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        knownExistingNorm.insert(std::move(norm));
        return true;
    }
    return false;
}

std::wstring NextFreeCollisionFilePath(
    const std::wstring& fullDstPath,
    const std::unordered_set<std::wstring>* reservedNormKeys = nullptr)
{
    std::wstring dir = PathUtils::GetParentPath(fullDstPath);
    std::wstring fileName = PathUtils::GetFileName(fullDstPath);
    std::wstring stem = fileName;
    std::wstring ext;
    size_t dot = fileName.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0) {
        stem = fileName.substr(0, dot);
        ext = fileName.substr(dot);
    }
    std::wstring base = PathUtils::EnsureTrailingSlash(dir);
    for (int idx = 2; idx < 50000; ++idx) {
        std::wstring cand = base + stem + L" (" + std::to_wstring(idx) + L")" + ext;
        if (reservedNormKeys && reservedNormKeys->find(PathUtils::NormalizePathKey(cand)) != reservedNormKeys->end()) {
            continue;
        }
        if (GetFileAttributesW(ToExtPath(cand).c_str()) == INVALID_FILE_ATTRIBUTES &&
            GetFileAttributesW(ToExtPath(cand + L".e2part").c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            return cand;
        }
    }
    return base + stem + L"_" + std::to_wstring(GetTickCount64()) + ext;
}

struct VolumeIdentity {
    std::wstring key;       // Unique volume key for SmartPerDrive queue
    std::wstring label;     // Short UI label ("C:", "\\NAS\Share")
    DWORD serial = 0;
    DWORD fsFlags = 0;
    uint32_t sectorAlign = 4096;
    bool isRemoteNetwork = false;
    bool isRemoteOrRemovable = false;
    bool isRotationalHdd = false;
    bool valid = false;
};

uint32_t NormalizeSectorAlign(DWORD rawBytesPerSector) {
    uint32_t a = 4096;
    while (a < rawBytesPerSector && a < 65536u) {
        a <<= 1;
    }
    return a;
}

bool ProbeDriveHasSeekPenalty(const std::wstring& driveColon) {
    if (driveColon.size() != 2 || driveColon[1] != L':') return false;
    std::wstring devPath = L"\\\\.\\" + driveColon;
    HANDLE hDev = CreateFileW(devPath.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (hDev == INVALID_HANDLE_VALUE) return false;

    STORAGE_PROPERTY_QUERY spq = {};
    spq.PropertyId = StorageDeviceSeekPenaltyProperty;
    spq.QueryType = PropertyStandardQuery;
    DEVICE_SEEK_PENALTY_DESCRIPTOR dspd = {};
    DWORD bytesRet = 0;
    bool seekPenalty = false;
    if (DeviceIoControl(hDev, IOCTL_STORAGE_QUERY_PROPERTY, &spq, sizeof(spq), &dspd, sizeof(dspd), &bytesRet, nullptr) &&
        bytesRet >= sizeof(DEVICE_SEEK_PENALTY_DESCRIPTOR))
    {
        seekPenalty = (dspd.IncursSeekPenalty != FALSE);
    }
    CloseHandle(hDev);
    return seekPenalty;
}

VolumeIdentity QueryVolumeIdentity(const std::wstring& path) {
    VolumeIdentity vi;
    if (path.empty()) return vi;

    std::wstring clean = PathUtils::TrimTrailingSlashes(path);
    if (clean.rfind(L"\\\\", 0) == 0 && clean.rfind(L"\\\\?\\", 0) != 0) {
        // UNC path \\Server\Share\...
        size_t s1 = clean.find(L'\\', 2);
        size_t s2 = (s1 != std::wstring::npos) ? clean.find(L'\\', s1 + 1) : std::wstring::npos;
        std::wstring shareRoot = (s2 != std::wstring::npos) ? clean.substr(0, s2 + 1) : (clean + L"\\");
        vi.label = PathUtils::TrimTrailingSlashes(shareRoot);
        vi.key = L"UNC:" + PathUtils::ToLower(vi.label);
        vi.isRemoteNetwork = true;
        vi.isRemoteOrRemovable = true;
        DWORD serial = 0, flags = 0;
        if (GetVolumeInformationW(shareRoot.c_str(), nullptr, 0, &serial, nullptr, &flags, nullptr, 0)) {
            vi.serial = serial;
            vi.fsFlags = flags;
            vi.valid = true;
        }
        DWORD spc = 0, bps = 0, fc = 0, tc = 0;
        if (GetDiskFreeSpaceW(shareRoot.c_str(), &spc, &bps, &fc, &tc) && bps > 0) {
            vi.sectorAlign = NormalizeSectorAlign(bps);
        }
        return vi;
    }

    // Fast O(1) cache for standard local fixed drives (e.g. C:\..., W:\...) — eliminates 10 volume/device syscalls per job!
    int fastDriveIdx = -1;
    if (clean.size() >= 2 && clean[1] == L':') {
        wchar_t u = static_cast<wchar_t>(towupper(clean[0]));
        if (u >= L'A' && u <= L'Z') fastDriveIdx = static_cast<int>(u - L'A');
    }
    static std::mutex s_volCacheMtx;
    static VolumeIdentity s_fixedVolCache[26];
    static bool s_fixedVolCached[26] = {};
    if (fastDriveIdx >= 0) {
        std::lock_guard<std::mutex> vlk(s_volCacheMtx);
        if (s_fixedVolCached[fastDriveIdx]) {
            return s_fixedVolCache[fastDriveIdx];
        }
    }

    wchar_t volMount[MAX_PATH] = {};
    if (GetVolumePathNameW(clean.c_str(), volMount, MAX_PATH) && volMount[0]) {
        vi.label = PathUtils::TrimTrailingSlashes(volMount);
        if (vi.label.size() == 3 && vi.label[1] == L':' && vi.label[2] == L'\\') {
            vi.label.pop_back(); // "C:"
        }
        UINT dtype = GetDriveTypeW(volMount);
        vi.isRemoteNetwork = (dtype == DRIVE_REMOTE);
        vi.isRemoteOrRemovable = (dtype == DRIVE_REMOTE || dtype == DRIVE_REMOVABLE || dtype == DRIVE_CDROM);
        if (dtype == DRIVE_FIXED && vi.label.size() == 2 && vi.label[1] == L':') {
            vi.isRotationalHdd = ProbeDriveHasSeekPenalty(vi.label);
        }
        DWORD serial = 0, flags = 0;
        if (GetVolumeInformationW(volMount, nullptr, 0, &serial, nullptr, &flags, nullptr, 0)) {
            vi.serial = serial;
            vi.fsFlags = flags;
            vi.valid = true;
            wchar_t keyBuf[64] = {};
            swprintf_s(keyBuf, L"VOL:%08X", static_cast<unsigned>(serial));
            vi.key = keyBuf;
        } else {
            vi.key = L"DRV:" + PathUtils::ToLower(vi.label);
        }
        DWORD spc = 0, bps = 0, fc = 0, tc = 0;
        if (GetDiskFreeSpaceW(volMount, &spc, &bps, &fc, &tc) && bps > 0) {
            vi.sectorAlign = NormalizeSectorAlign(bps);
        }
        // Cache root-mounted fixed drives (non-mountpoint folders match the drive letter root)
        if (dtype == DRIVE_FIXED && fastDriveIdx >= 0 && vi.label.size() == 2 && vi.label[1] == L':') {
            std::lock_guard<std::mutex> vlk(s_volCacheMtx);
            s_fixedVolCache[fastDriveIdx] = vi;
            s_fixedVolCached[fastDriveIdx] = true;
        }
    } else if (clean.size() >= 2 && clean[1] == L':') {
        vi.label = std::wstring(1, static_cast<wchar_t>(towupper(clean[0]))) + L":";
        vi.key = L"DRV:" + PathUtils::ToLower(vi.label);
    }
    return vi;
}

void ApplyHandleTimestampsAndAttrs(
    HANDLE hFile,
    const FILETIME& creationFt,
    const FILETIME& accessFt,
    const FILETIME& writeFt,
    DWORD attrs)
{
    FILE_BASIC_INFO fbi = {};
    fbi.CreationTime.LowPart   = creationFt.dwLowDateTime;
    fbi.CreationTime.HighPart  = static_cast<LONG>(creationFt.dwHighDateTime);
    fbi.LastAccessTime.LowPart = accessFt.dwLowDateTime;
    fbi.LastAccessTime.HighPart= static_cast<LONG>(accessFt.dwHighDateTime);
    fbi.LastWriteTime.LowPart  = writeFt.dwLowDateTime;
    fbi.LastWriteTime.HighPart = static_cast<LONG>(writeFt.dwHighDateTime);
    fbi.ChangeTime             = fbi.LastWriteTime;
    DWORD cleanAttrs = attrs & (FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED);
    if (cleanAttrs != 0) {
        fbi.FileAttributes = cleanAttrs;
    } else if ((attrs & FILE_ATTRIBUTE_SPARSE_FILE) != 0) {
        fbi.FileAttributes = 0; // Keep FILE_ATTRIBUTE_SPARSE_FILE intact (0 = do not alter attributes)
    } else {
        fbi.FileAttributes = FILE_ATTRIBUTE_NORMAL;
    }
    SetFileInformationByHandle(hFile, FileBasicInfo, &fbi, sizeof(fbi));
}

bool MoveItemAtomicWithRetry(const std::wstring& srcPath, const std::wstring& dstPath, bool isDir, bool replaceExisting, DWORD& outErr) {
    outErr = 0;
    const std::wstring extSrc = ToExtPath(srcPath);
    const std::wstring extDst = ToExtPath(dstPath);
    // Avoid MOVEFILE_WRITE_THROUGH so NTFS MFT renames and .e2part commits execute in ~15us instead of stalling on NVMe hardware cache flushes
    const DWORD flags = isDir ? 0 : (replaceExisting ? MOVEFILE_REPLACE_EXISTING : 0);

    for (int attempt = 0; attempt < 5; ++attempt) {
        if (MoveFileExW(extSrc.c_str(), extDst.c_str(), flags)) return true;
        DWORD err = GetLastError();
        outErr = err;
        if (replaceExisting && !isDir && (err == ERROR_ACCESS_DENIED || err == ERROR_SHARING_VIOLATION)) {
            DWORD dAttr = GetFileAttributesW(extDst.c_str());
            if (dAttr != INVALID_FILE_ATTRIBUTES && (dAttr & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))) {
                SetFileAttributesW(extDst.c_str(), FILE_ATTRIBUTE_NORMAL);
            }
        }
        if (err == ERROR_ACCESS_DENIED) {
            DWORD sAttr = GetFileAttributesW(extSrc.c_str());
            if (sAttr != INVALID_FILE_ATTRIBUTES && (sAttr & FILE_ATTRIBUTE_READONLY)) {
                SetFileAttributesW(extSrc.c_str(), sAttr & ~FILE_ATTRIBUTE_READONLY);
            }
        }
        if (attempt < 4 && IsTransientIoError(err)) {
            Sleep(30 * (attempt + 1));
            continue;
        }
        break;
    }
    return false;
}

// Process-wide warm 64KB-aligned page pool so workers reuse already-committed pages with 0 kernel page-zeroing / TLB shootdown overhead!
struct WarmIoBufferPool {
    struct Block {
        uint8_t* ptr = nullptr;
        size_t   cap = 0;
    };
    std::mutex mtx;
    std::vector<Block> freeBlocks;

    ~WarmIoBufferPool() {
        for (auto& b : freeBlocks) {
            if (b.ptr) VirtualFree(b.ptr, 0, MEM_RELEASE);
        }
    }

    Block Acquire(size_t minBytes) {
        size_t reqCap = (minBytes + 65535ULL) & ~65535ULL;
        {
            std::lock_guard<std::mutex> lk(mtx);
            size_t bestIdx = SIZE_MAX;
            for (size_t i = 0; i < freeBlocks.size(); ++i) {
                if (freeBlocks[i].cap >= reqCap && freeBlocks[i].cap <= reqCap * 2) {
                    if (bestIdx == SIZE_MAX || freeBlocks[i].cap < freeBlocks[bestIdx].cap) {
                        bestIdx = i;
                    }
                }
            }
            if (bestIdx != SIZE_MAX) {
                Block out = freeBlocks[bestIdx];
                freeBlocks[bestIdx] = freeBlocks.back();
                freeBlocks.pop_back();
                return out;
            }
        }
        Block fresh;
        fresh.cap = reqCap;
        fresh.ptr = static_cast<uint8_t*>(VirtualAlloc(nullptr, reqCap, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (!fresh.ptr) fresh.cap = 0;
        return fresh;
    }

    void Release(Block b) {
        if (!b.ptr || b.cap == 0) return;
        if (b.cap <= 16ULL * 1024ULL * 1024ULL) {
            std::lock_guard<std::mutex> lk(mtx);
            if (freeBlocks.size() < 12) {
                freeBlocks.push_back(b);
                return;
            }
        }
        VirtualFree(b.ptr, 0, MEM_RELEASE);
    }
};

WarmIoBufferPool& GetWarmIoBufferPool() {
    static WarmIoBufferPool s_pool;
    return s_pool;
}

class ScopedVirtualBuffer {
public:
    explicit ScopedVirtualBuffer(size_t bytes) {
        if (bytes > 0) {
            auto blk = GetWarmIoBufferPool().Acquire(bytes);
            m_ptr = blk.ptr;
            m_cap = blk.cap;
        }
    }
    ~ScopedVirtualBuffer() {
        if (m_ptr) GetWarmIoBufferPool().Release({ m_ptr, m_cap });
    }
    ScopedVirtualBuffer(const ScopedVirtualBuffer&) = delete;
    ScopedVirtualBuffer& operator=(const ScopedVirtualBuffer&) = delete;

    void EnsureCapacity(size_t minBytes) {
        if (m_cap >= minBytes) return;
        if (m_ptr) {
            GetWarmIoBufferPool().Release({ m_ptr, m_cap });
            m_ptr = nullptr;
            m_cap = 0;
        }
        auto blk = GetWarmIoBufferPool().Acquire(minBytes);
        m_ptr = blk.ptr;
        m_cap = blk.cap;
    }

    uint8_t* Data() const { return m_ptr; }
    size_t   Capacity() const { return m_ptr ? m_cap : 0; }
private:
    uint8_t* m_ptr = nullptr;
    size_t   m_cap = 0;
};

bool ComputeXxHash64FileBuffered(
    const std::wstring& path,
    uint8_t* callerBuf,
    size_t callerBufCap,
    uint64_t& outHash,
    std::atomic<bool>* cancelFlag)
{
    outHash = 0;
    std::wstring ext = ToExtPath(path);
    HANDLE hFile = CreateFileW(
        ext.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    ScopedVirtualBuffer fallbackBuf((callerBuf && callerBufCap >= 65536) ? 0 : (2 * 1024 * 1024));
    uint8_t* bufPtr = (callerBuf && callerBufCap >= 65536) ? callerBuf : fallbackBuf.Data();
    size_t bufCap   = (callerBuf && callerBufCap >= 65536) ? std::min<size_t>(callerBufCap, 4 * 1024 * 1024) : fallbackBuf.Capacity();
    if (!bufPtr || bufCap == 0) {
        CloseHandle(hFile);
        return false;
    }

    XxHash64Stream hasher;
    DWORD got = 0;
    bool ok = true;
    while (ReadFile(hFile, bufPtr, static_cast<DWORD>(bufCap), &got, nullptr) && got > 0) {
        if (cancelFlag && cancelFlag->load(std::memory_order_relaxed)) {
            ok = false;
            break;
        }
        hasher.Update(bufPtr, got);
    }
    CloseHandle(hFile);
    if (!ok) return false;
    outHash = hasher.Digest();
    return true;
}

} // namespace

// ============================================================================
// Persistent 4-Slot Asynchronous Producer-Consumer Ring (zero per-file thread spawn)
// ============================================================================
class TransferManager::WorkerAsyncRing {
public:
    static constexpr int kNumSlots = 4;

    WorkerAsyncRing() = default;
    ~WorkerAsyncRing() {
        if (m_started) {
            {
                std::lock_guard<std::mutex> lk(m_mtx);
                m_shutdown = true;
                m_abort = true;
            }
            m_cv.notify_all();
            if (m_reader.joinable()) m_reader.join();
        }
    }
    WorkerAsyncRing(const WorkerAsyncRing&) = delete;
    WorkerAsyncRing& operator=(const WorkerAsyncRing&) = delete;

    bool StreamFile(
        TransferManager& mgr,
        InternalJob& job,
        HANDLE hSrc,
        HANDLE hDst,
        uint8_t* bufBase,
        size_t slotCap,
        size_t sliceCap,
        uint64_t remainingToCopy,
        bool computeHash,
        XxHash64Stream& srcHasher,
        uint64_t& inOutBytesAdded,
        DWORD& outErr)
    {
        if (!m_started) {
            m_started = true;
            m_reader = std::thread(&WorkerAsyncRing::ReaderLoop, this);
        }

        {
            std::lock_guard<std::mutex> lk(m_mtx);
            for (int i = 0; i < kNumSlots; ++i) {
                m_slots[i] = {};
                m_slotPtrs[i] = bufBase + static_cast<size_t>(i) * slotCap;
            }
            m_hSrc = hSrc;
            m_sliceCap = std::min(sliceCap, slotCap);
            m_remaining = remainingToCopy;
            m_computeHash = computeHash;
            m_hasher = &srcHasher;
            m_abort = false;
            m_taskActive = true;
        }
        m_cv.notify_one();

        bool ioOk = true;
        outErr = 0;
        int cIdx = 0;

        for (;;) {
            if (!mgr.WaitIfPausedOrCancelled(job)) {
                ioOk = false;
                outErr = ERROR_CANCELLED;
                AbortReader();
                break;
            }

            size_t chunkLen = 0;
            DWORD rErr = 0;
            bool isEof = false;
            {
                std::unique_lock<std::mutex> lk(m_mtx);
                m_cv.wait(lk, [&]() { return m_slots[cIdx].ready || !m_taskActive || m_abort; });
                if (!m_slots[cIdx].ready) break;
                chunkLen = m_slots[cIdx].len;
                rErr = m_slots[cIdx].err;
                isEof = m_slots[cIdx].eof;
            }

            if (rErr != 0) {
                ioOk = false;
                outErr = rErr;
                AbortReader();
                break;
            }

            if (chunkLen > 0) {
                DWORD written = 0;
                if (!WriteFile(hDst, m_slotPtrs[cIdx], static_cast<DWORD>(chunkLen), &written, nullptr) ||
                    written != static_cast<DWORD>(chunkLen))
                {
                    outErr = GetLastError();
                    ioOk = false;
                    AbortReader();
                    break;
                }
            }

            {
                std::lock_guard<std::mutex> lk(m_mtx);
                m_slots[cIdx].ready = false;
            }
            m_cv.notify_one();

            if (chunkLen > 0) {
                inOutBytesAdded += chunkLen;
                mgr.RecordProgressBytes(job, chunkLen);
                mgr.ThrottleBytes(chunkLen, job);
            }
            if (isEof) break;
            cIdx = (cIdx + 1) & (kNumSlots - 1);
        }

        // Wait until the persistent reader finishes detaching from hSrc & bufBase before returning
        {
            std::unique_lock<std::mutex> lk(m_mtx);
            m_cv.wait(lk, [&]() { return !m_taskActive; });
        }
        return ioOk;
    }

private:
    struct RingSlot {
        size_t len = 0;
        DWORD  err = 0;
        bool   ready = false;
        bool   eof = false;
    };

    void AbortReader() {
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            m_abort = true;
        }
        m_cv.notify_all();
    }

    void ReaderLoop() {
        std::unique_lock<std::mutex> lk(m_mtx);
        for (;;) {
            m_cv.wait(lk, [&]() { return m_taskActive || m_shutdown; });
            if (m_shutdown) break;

            HANDLE hSrc = m_hSrc;
            size_t sliceCap = m_sliceCap;
            uint64_t rem = m_remaining;
            bool computeHash = m_computeHash;
            XxHash64Stream* hasher = m_hasher;
            uint8_t* slotPtrs[kNumSlots] = { m_slotPtrs[0], m_slotPtrs[1], m_slotPtrs[2], m_slotPtrs[3] };
            int pIdx = 0;

            while (rem > 0) {
                m_cv.wait(lk, [&]() { return !m_slots[pIdx].ready || m_abort; });
                if (m_abort) break;
                lk.unlock();

                DWORD req = static_cast<DWORD>(std::min<uint64_t>(sliceCap, rem));
                DWORD got = 0;
                DWORD rErr = 0;
                BOOL rOk = ReadFile(hSrc, slotPtrs[pIdx], req, &got, nullptr);
                if (!rOk) rErr = GetLastError();

                size_t valid = rOk ? static_cast<size_t>(std::min<uint64_t>(got, rem)) : 0;
                if (rOk && valid > 0 && computeHash && hasher) {
                    hasher->Update(slotPtrs[pIdx], valid);
                }

                lk.lock();
                if (m_abort) break;
                m_slots[pIdx].len = valid;
                m_slots[pIdx].err = rErr;
                m_slots[pIdx].eof = (!rOk || got == 0 || valid == rem);
                m_slots[pIdx].ready = true;
                m_cv.notify_one();

                if (!rOk || got == 0) break;
                rem -= valid;
                pIdx = (pIdx + 1) & (kNumSlots - 1);
            }

            m_taskActive = false;
            m_cv.notify_all();
        }
    }

    bool m_started = false;
    bool m_shutdown = false;
    bool m_taskActive = false;
    bool m_abort = false;
    HANDLE m_hSrc = INVALID_HANDLE_VALUE;
    size_t m_sliceCap = 0;
    uint64_t m_remaining = 0;
    bool m_computeHash = false;
    XxHash64Stream* m_hasher = nullptr;
    uint8_t* m_slotPtrs[kNumSlots] = {};
    RingSlot m_slots[kNumSlots] = {};
    std::mutex m_mtx;
    std::condition_variable m_cv;
    std::thread m_reader;
};

struct TransferManager::PoolBatch {
    std::function<void(WorkerAsyncRing&)> fn;
    std::atomic<uint32_t> remainingShares{ 0 };
    std::atomic<uint32_t> unfinishedShares{ 0 };
    std::mutex doneMtx;
    std::condition_variable doneCv;
};

// ============================================================================
// Internal Job Representation
// ============================================================================
struct TransferManager::InternalJob {
    int id = 0;
    TransferOp op = TransferOp::Copy;
    TransferConfig cfg;
    std::atomic<int> conflictPolicyAtomic{ static_cast<int>(TransferConflictPolicy::RenameCollision) };

    std::vector<std::wstring> sources;
    std::wstring targetDir;
    std::wstring dstNormKey;
    std::vector<std::wstring> srcNormKeys;
    std::vector<std::wstring> srcParentNormKeys;
    std::wstring srcVolKey;
    std::wstring dstVolKey;
    std::wstring srcVolLabel;
    std::wstring dstVolLabel;
    bool isFtpJob = false;
    bool recycle = false;   // Delete jobs: true = Recycle Bin, false = permanent
    bool sameVolume = false;
    bool isBothRemoteNetwork = false;
    bool isRemoteOrRemovable = false;
    bool isRotationalHdd = false;
    bool supportsBlockClone = false;
    bool dstSupportsSparse = false;
    bool hasIntraBatchDupes = false;
    uint32_t sectorAlign = 4096;

    std::atomic<TransferJobState> state{ TransferJobState::Queued };
    std::atomic<bool> paused{ false };
    std::atomic<bool> cancelled{ false };
    std::atomic<bool> forceStart{ false };
    std::atomic<bool> workerSpawned{ false };
    std::atomic<bool> threadFinished{ false };

    std::atomic<uint64_t> totalFiles{ 0 };
    std::atomic<uint64_t> doneFiles{ 0 };
    std::atomic<uint64_t> skippedFiles{ 0 };
    std::atomic<uint64_t> renamedCollisions{ 0 };
    std::atomic<uint64_t> verifiedFiles{ 0 };
    std::atomic<uint64_t> resumedFiles{ 0 };

    std::atomic<uint64_t> totalBytes{ 0 };
    std::atomic<uint64_t> doneBytes{ 0 };

    mutable std::mutex jobMutex;
    std::condition_variable pauseCv;
    std::mutex dstReserveMutex;
    std::unordered_set<std::wstring> reservedDstKeys;
    std::wstring currentFile;
    std::wstring summaryText;
    std::vector<InternalFileItem> fileItems;
    std::vector<InternalDirItem>  dirItems;
    std::vector<TransferFileError> errors;
    std::vector<std::wstring> pendingScanSources;   // Delete jobs: requested items that still have to be listed

    ULONGLONG startTick = 0;
    ULONGLONG pauseBeginTick = 0;
    uint64_t  pausedAccumMs = 0;
    uint64_t  finalElapsedMs = 0;

    // Per-job rolling speed estimation (atomically gated so workers don't contend on jobMutex)
    std::atomic<ULONGLONG> lastSpeedTickAtomic{ 0 };
    std::atomic<ULONGLONG> lastCurFileTickAtomic{ 0 };
    ULONGLONG lastSpeedTick = 0;
    uint64_t  lastSpeedBytes = 0;
    double    liveSpeedBps = 0.0;
    double    peakSpeedBps = 0.0;
};

namespace {

// Keeps the list of unread completions short without ever dropping a delete: the window relies on a
// delete's completion to show its items again when something could not be removed.
void TrimCompletionEvents(std::vector<TransferCompletionEvent>& events) {
    while (events.size() > 32) {
        auto victim = std::find_if(events.begin(), events.end(),
            [](const TransferCompletionEvent& e) { return e.op != TransferOp::Delete; });
        if (victim == events.end()) {
            if (events.size() <= 1024) break;
            victim = events.begin();
        }
        events.erase(victim);
    }
}

std::wstring QuickVolumeTag(const std::wstring& p, std::wstring& outLbl) {
    if (FtpClient::IsFtpPath(p)) {
        FtpUrlInfo fi;
        if (FtpClient::ParseFtpUrl(p, fi) && !fi.host.empty()) {
            outLbl = (fi.useFtps ? L"FTPS:" : L"FTP:") + fi.host;
            return L"FTP:" + PathUtils::ToLower(fi.host) + L":" + std::to_wstring(fi.port);
        }
        outLbl = L"FTP";
        return L"FTP:DEFAULT";
    }
    if (p.size() >= 2 && p[1] == L':') {
        wchar_t dl = static_cast<wchar_t>(towupper(p[0]));
        outLbl = std::wstring(1, dl) + L":";
        return L"DRV:" + PathUtils::ToLower(outLbl);
    }
    if (p.rfind(L"\\\\", 0) == 0) {
        size_t s1 = p.find(L'\\', 2);
        size_t s2 = (s1 != std::wstring::npos) ? p.find(L'\\', s1 + 1) : std::wstring::npos;
        outLbl = (s2 != std::wstring::npos) ? p.substr(0, s2) : p;
        return L"UNC:" + PathUtils::ToLower(outLbl);
    }
    outLbl = L"Local";
    return L"LOCAL";
}

void PopulateJobNormWatchKeys(
    const std::vector<std::wstring>& sources,
    const std::wstring& targetDir,
    std::wstring& outDstNorm,
    std::vector<std::wstring>& outSrcNorms,
    std::vector<std::wstring>& outSrcParentNorms)
{
    outDstNorm = PathUtils::NormalizePathKey(targetDir);
    outSrcNorms.clear();
    outSrcParentNorms.clear();
    outSrcNorms.reserve(sources.size());
    outSrcParentNorms.reserve(sources.size());
    for (const auto& s : sources) {
        outSrcNorms.push_back(PathUtils::NormalizePathKey(s));
        std::wstring par = FtpClient::IsFtpPath(s) ? FtpClient::GetParentFtpUrl(s) : PathUtils::GetParentPath(s);
        if (!par.empty()) {
            std::wstring pNorm = PathUtils::NormalizePathKey(par);
            if (std::find(outSrcParentNorms.begin(), outSrcParentNorms.end(), pNorm) == outSrcParentNorms.end()) {
                outSrcParentNorms.push_back(std::move(pNorm));
            }
        }
    }
}

} // namespace

// ============================================================================
// TransferManager Lifecycle, Persistent Worker Pool & Queue Persistence
// ============================================================================
TransferManager::TransferManager() = default;

TransferManager::~TransferManager() {
    Shutdown();
}

void TransferManager::Initialize(HWND notifyHwnd) {
    if (m_running.exchange(true)) return;
    {
        std::lock_guard<std::mutex> plk(m_poolMutex);
        m_poolShutdown = false;
    }
    m_notifyHwnd = notifyHwnd;
    m_lastTelemetrySampleTick = GetTickCount64();
    m_schedulerThread = std::thread(&TransferManager::SchedulerLoop, this);
}

void TransferManager::Shutdown() {
    if (!m_running.load()) return;

    // Persist any unfinished jobs BEFORE stopping workers so .e2part + transfer_queue.dat resume seamlessly on next launch!
    SaveQueueState();

    if (!m_running.exchange(false)) return;

    std::vector<std::shared_ptr<InternalJob>> snapshot;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        snapshot = m_jobs;
    }
    // Wake sleeping/paused workers WITHOUT setting cancelled=true so .e2part files are preserved on disk!
    for (auto& j : snapshot) {
        j->pauseCv.notify_all();
    }
    m_cv.notify_all();

    if (m_schedulerThread.joinable()) {
        m_schedulerThread.join();
    }
    for (auto& pair : m_jobThreads) {
        if (pair.second.joinable()) pair.second.join();
    }
    m_jobThreads.clear();

    // Shut down persistent parallel worker pool & release warm async rings
    {
        std::lock_guard<std::mutex> plk(m_poolMutex);
        m_poolShutdown = true;
        m_poolBatches.clear();
    }
    m_poolCv.notify_all();
    for (auto& pt : m_poolThreads) {
        if (pt.joinable()) pt.join();
    }
    m_poolThreads.clear();
    {
        std::lock_guard<std::mutex> plk(m_poolMutex);
        m_callerRings.clear();
    }
}

void TransferManager::PoolWorkerLoop() {
    WorkerAsyncRing persistentRing;
    for (;;) {
        std::shared_ptr<PoolBatch> claimedBatch;
        {
            std::unique_lock<std::mutex> plk(m_poolMutex);
            m_poolCv.wait(plk, [this]() {
                if (m_poolShutdown) return true;
                for (const auto& b : m_poolBatches) {
                    if (b->remainingShares.load(std::memory_order_relaxed) > 0) return true;
                }
                return false;
            });
            if (m_poolShutdown) break;

            for (auto it = m_poolBatches.begin(); it != m_poolBatches.end(); ) {
                auto& b = *it;
                uint32_t cur = b->remainingShares.load(std::memory_order_relaxed);
                bool claimed = false;
                while (cur > 0) {
                    if (b->remainingShares.compare_exchange_weak(cur, cur - 1, std::memory_order_acquire, std::memory_order_relaxed)) {
                        claimed = true;
                        break;
                    }
                }
                if (claimed) {
                    claimedBatch = b;
                    if (cur == 1) {
                        m_poolBatches.erase(it);
                    }
                    break;
                }
                if (b->remainingShares.load(std::memory_order_relaxed) == 0) {
                    it = m_poolBatches.erase(it);
                } else {
                    ++it;
                }
            }
        }

        if (claimedBatch) {
            claimedBatch->fn(persistentRing);
            if (claimedBatch->unfinishedShares.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                std::lock_guard<std::mutex> dlk(claimedBatch->doneMtx);
                claimedBatch->doneCv.notify_all();
            }
        }
    }
}

void TransferManager::RunParallelWorkers(uint32_t numWorkers, const std::function<void(WorkerAsyncRing&)>& fn) {
    std::unique_ptr<WorkerAsyncRing> callerRing;
    {
        std::lock_guard<std::mutex> plk(m_poolMutex);
        if (!m_callerRings.empty()) {
            callerRing = std::move(m_callerRings.back());
            m_callerRings.pop_back();
        }
    }
    if (!callerRing) {
        callerRing = std::make_unique<WorkerAsyncRing>();
    }

    struct ReturnCallerRing {
        TransferManager* mgr;
        std::unique_ptr<WorkerAsyncRing>& ring;
        ~ReturnCallerRing() {
            if (ring) {
                std::lock_guard<std::mutex> plk(mgr->m_poolMutex);
                if (!mgr->m_poolShutdown && mgr->m_callerRings.size() < 8) {
                    mgr->m_callerRings.push_back(std::move(ring));
                }
            }
        }
    } returnGuard{ this, callerRing };

    if (numWorkers <= 1) {
        fn(*callerRing);
        return;
    }

    const uint32_t extra = numWorkers - 1;
    auto batch = std::make_shared<PoolBatch>();
    batch->fn = fn;
    batch->remainingShares.store(extra, std::memory_order_relaxed);
    batch->unfinishedShares.store(extra, std::memory_order_relaxed);

    {
        std::lock_guard<std::mutex> plk(m_poolMutex);
        if (!m_poolShutdown) {
            const size_t desiredPoolSize = std::min<size_t>(extra, 15);
            while (m_poolThreads.size() < desiredPoolSize) {
                m_poolThreads.emplace_back(&TransferManager::PoolWorkerLoop, this);
            }
            m_poolBatches.push_back(batch);
        } else {
            batch->remainingShares.store(0, std::memory_order_relaxed);
            batch->unfinishedShares.store(0, std::memory_order_relaxed);
        }
    }
    m_poolCv.notify_all();

    // Execute caller's primary worker share
    fn(*callerRing);

    // Work-steal any unstarted shares of our own batch so even saturated ParallelAll queues never deadlock
    for (;;) {
        uint32_t cur = batch->remainingShares.load(std::memory_order_relaxed);
        if (cur == 0) break;
        if (batch->remainingShares.compare_exchange_weak(cur, cur - 1, std::memory_order_acquire, std::memory_order_relaxed)) {
            fn(*callerRing);
            if (batch->unfinishedShares.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                std::lock_guard<std::mutex> dlk(batch->doneMtx);
                batch->doneCv.notify_all();
            }
        }
    }

    if (batch->unfinishedShares.load(std::memory_order_acquire) > 0) {
        std::unique_lock<std::mutex> dlk(batch->doneMtx);
        batch->doneCv.wait(dlk, [&]() {
            return batch->unfinishedShares.load(std::memory_order_acquire) == 0;
        });
    }
}

void TransferManager::SaveQueueState() {
    std::wstring persistPath;
    struct PersistedEntry {
        TransferOp op;
        TransferConfig cfg;
        bool wasStarted;
        std::wstring targetDir;
        std::vector<std::wstring> sources;
    };
    std::vector<PersistedEntry> entries;

    {
        std::lock_guard<std::mutex> lk(m_mutex);
        persistPath = m_queuePersistPath;
        if (persistPath.empty()) return;

        for (const auto& j : m_jobs) {
            if (j->cancelled.load(std::memory_order_relaxed)) continue;
            // A delete is never persisted: it must not come back and carry on after a restart
            if (j->op == TransferOp::Delete) continue;
            TransferJobState st = j->state.load(std::memory_order_relaxed);
            if (st == TransferJobState::Queued || st == TransferJobState::Scanning ||
                st == TransferJobState::Running || st == TransferJobState::Paused ||
                st == TransferJobState::Verifying)
            {
                PersistedEntry pe;
                pe.op = j->op;
                pe.cfg = j->cfg;
                pe.cfg.conflictPolicy = static_cast<TransferConflictPolicy>(j->conflictPolicyAtomic.load(std::memory_order_relaxed));
                pe.wasStarted = j->workerSpawned.load(std::memory_order_relaxed) ||
                                (j->doneFiles.load(std::memory_order_relaxed) > 0) ||
                                (j->doneBytes.load(std::memory_order_relaxed) > 0);
                pe.targetDir = j->targetDir;
                pe.sources = j->sources;
                entries.push_back(std::move(pe));
            }
        }
    }

    std::lock_guard<std::mutex> wlk(m_persistWriteMutex);
    if (entries.empty()) {
        DeleteFileW(ToExtPath(persistPath).c_str());
        DeleteFileW(ToExtPath(persistPath + L".tmp").c_str());
        return;
    }

    std::string out = "E2QUEUE_V1\n";
    out.reserve(entries.size() * 256);
    for (const auto& pe : entries) {
        out += "JOB\n";
        out += "OP=" + std::to_string(static_cast<int>(pe.op)) + "\n";
        out += "CONFLICT=" + std::to_string(static_cast<int>(pe.cfg.conflictPolicy)) + "\n";
        out += "VERIFY=" + std::to_string(static_cast<int>(pe.cfg.verifyMode)) + "\n";
        out += "BUFKB=" + std::to_string(pe.cfg.bufferSizeKB) + "\n";
        out += "WORKERS=" + std::to_string(pe.cfg.workerThreads) + "\n";
        out += "PART=" + std::to_string(pe.cfg.usePartFiles ? 1 : 0) + "\n";
        out += "UNBUF=" + std::to_string(pe.cfg.unbufferedLargeFiles ? 1 : 0) + "\n";
        out += "SPEED=" + std::to_string(pe.cfg.speedLimitMBps) + "\n";
        out += "RETRIES=" + std::to_string(pe.cfg.maxAutoRetries) + "\n";
        out += "WAS_STARTED=" + std::to_string(pe.wasStarted ? 1 : 0) + "\n";
        out += "TARGET=" + PathUtils::WideToUtf8(pe.targetDir) + "\n";
        for (const auto& s : pe.sources) {
            out += "SRC=" + PathUtils::WideToUtf8(s) + "\n";
        }
        out += "ENDJOB\n";
    }

    std::wstring parentDir = PathUtils::GetParentPath(persistPath);
    if (!parentDir.empty()) {
        EnsureDirHierarchyExt(parentDir);
    }
    std::wstring tmpPath = persistPath + L".tmp";
    if (PathUtils::WriteFileBytes(tmpPath, out.data(), out.size())) {
        MoveFileExW(ToExtPath(tmpPath).c_str(), ToExtPath(persistPath).c_str(), MOVEFILE_REPLACE_EXISTING);
    }
}

size_t TransferManager::EnableQueuePersistence(const std::wstring& queueFilePath) {
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_queuePersistPath = queueFilePath;
    }
    if (queueFilePath.empty()) return 0;

    std::vector<uint8_t> rawBytes;
    if (!PathUtils::ReadFileBytes(queueFilePath, 8 * 1024 * 1024, rawBytes) || rawBytes.empty()) {
        return 0;
    }

    std::string text(reinterpret_cast<const char*>(rawBytes.data()), rawBytes.size());
    size_t pos = 0;
    auto nextLine = [&](std::string& line) -> bool {
        if (pos >= text.size()) return false;
        size_t nl = text.find('\n', pos);
        size_t end = (nl == std::string::npos) ? text.size() : nl;
        line = text.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        pos = (nl == std::string::npos) ? text.size() : (nl + 1);
        return true;
    };

    std::string header;
    if (!nextLine(header) || header != "E2QUEUE_V1") return 0;

    std::vector<std::shared_ptr<InternalJob>> recoveredJobs;
    std::string line;
    while (nextLine(line)) {
        if (line != "JOB") continue;

        TransferOp op = TransferOp::Copy;
        TransferConfig cfg = GetConfig();
        bool wasStarted = false;
        bool knownOp = true;
        std::wstring targetDir;
        std::vector<std::wstring> sources;

        while (nextLine(line) && line != "ENDJOB") {
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = line.substr(0, eq);
            std::string v = line.substr(eq + 1);
            if (k == "OP") {
                // Only copies and moves are ever restored; anything else in the file is ignored, not guessed
                const int opCode = std::atoi(v.c_str());
                knownOp = (opCode == 0 || opCode == 1);
                op = (opCode == 1) ? TransferOp::Move : TransferOp::Copy;
            }
            else if (k == "CONFLICT") cfg.conflictPolicy = static_cast<TransferConflictPolicy>(std::clamp(std::atoi(v.c_str()), 0, 4));
            else if (k == "VERIFY")   cfg.verifyMode = static_cast<TransferVerifyMode>(std::clamp(std::atoi(v.c_str()), 0, 2));
            else if (k == "BUFKB")    cfg.bufferSizeKB = static_cast<uint32_t>(std::max(0, std::atoi(v.c_str())));
            else if (k == "WORKERS")  cfg.workerThreads = static_cast<uint32_t>(std::clamp(std::atoi(v.c_str()), 0, 16));
            else if (k == "PART")     cfg.usePartFiles = (std::atoi(v.c_str()) != 0);
            else if (k == "UNBUF")    cfg.unbufferedLargeFiles = (std::atoi(v.c_str()) != 0);
            else if (k == "SPEED")    cfg.speedLimitMBps = static_cast<uint32_t>(std::max(0, std::atoi(v.c_str())));
            else if (k == "RETRIES")  cfg.maxAutoRetries = static_cast<uint32_t>(std::clamp(std::atoi(v.c_str()), 0, 16));
            else if (k == "WAS_STARTED") wasStarted = (std::atoi(v.c_str()) != 0);
            else if (k == "TARGET")   targetDir = PathUtils::TrimTrailingSlashes(PathUtils::Utf8ToWide(v));
            else if (k == "SRC") {
                std::wstring s = PathUtils::TrimTrailingSlashes(PathUtils::Utf8ToWide(v));
                if (!s.empty()) sources.push_back(std::move(s));
            }
        }

        if (!knownOp || sources.empty() || targetDir.empty()) continue;

        auto job = std::make_shared<InternalJob>();
        job->id = m_nextJobId.fetch_add(1);
        job->op = op;
        job->isFtpJob = FtpClient::IsFtpPath(targetDir) || FtpClient::IsFtpPath(sources[0]);

        // If the job was interrupted mid-run, upgrade RenameCollision -> OverwriteNewer so already-copied files
        // are skipped in 1 stat (0 duplicate "(2)" files!) while interrupted .e2part files resume from exact byte offset!
        if (wasStarted && !job->isFtpJob && cfg.conflictPolicy == TransferConflictPolicy::RenameCollision) {
            cfg.conflictPolicy = TransferConflictPolicy::OverwriteNewer;
            cfg.usePartFiles = true;
        }

        job->cfg = cfg;
        job->conflictPolicyAtomic.store(static_cast<int>(cfg.conflictPolicy));
        job->sources = std::move(sources);
        job->targetDir = std::move(targetDir);
        PopulateJobNormWatchKeys(job->sources, job->targetDir, job->dstNormKey, job->srcNormKeys, job->srcParentNormKeys);
        job->srcVolKey = QuickVolumeTag(job->sources[0], job->srcVolLabel);
        job->dstVolKey = QuickVolumeTag(job->targetDir, job->dstVolLabel);

        job->paused.store(true);
        job->pauseBeginTick = GetTickCount64();
        job->state.store(TransferJobState::Paused);
        job->summaryText = L"↻ Przywrócono niedokończony transfer — kliknij Wznów";
        recoveredJobs.push_back(std::move(job));
    }

    if (recoveredJobs.empty()) return 0;

    if (!m_running.load()) {
        Initialize(m_notifyHwnd);
    }
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        for (auto& rj : recoveredJobs) {
            m_jobs.push_back(std::move(rj));
        }
    }
    NotifyUi(false);
    return recoveredJobs.size();
}

bool TransferManager::IsTouchingDirectory(const std::wstring& normDirKey) const {
    if (normDirKey.empty()) return false;
    auto isSameOrSubPath = [](const std::wstring& dir, const std::wstring& root) -> bool {
        if (root.empty()) return false;
        if (dir == root) return true;
        return dir.size() > root.size() &&
               dir.compare(0, root.size(), root) == 0 &&
               dir[root.size()] == L'\\';
    };

    std::lock_guard<std::mutex> lk(m_mutex);
    for (const auto& j : m_jobs) {
        TransferJobState st = j->state.load(std::memory_order_relaxed);
        if (st != TransferJobState::Scanning && st != TransferJobState::Running && st != TransferJobState::Verifying) {
            continue;
        }
        if (isSameOrSubPath(normDirKey, j->dstNormKey)) {
            return true;
        }
        if (j->op == TransferOp::Move || j->op == TransferOp::Delete) {
            for (const auto& sp : j->srcParentNormKeys) {
                if (normDirKey == sp) return true;
            }
            for (const auto& sn : j->srcNormKeys) {
                if (isSameOrSubPath(normDirKey, sn)) return true;
            }
        }
    }
    return false;
}

int TransferManager::GetTaskbarProgressInfo(uint64_t& outCompleted, uint64_t& outTotal) const {
    outCompleted = 0;
    outTotal = 0;

    bool hasRunning = false;
    bool hasScanning = false;
    bool hasPaused = false;
    bool hasQueued = false;
    bool hasErrors = false;

    // A delete progresses by item, not by byte. So that it can share one taskbar bar with copies, each
    // item counts as one 4 KB cluster.
    auto addProgress = [&](const InternalJob& j) {
        if (j.op == TransferOp::Delete) {
            outCompleted += j.doneFiles.load(std::memory_order_relaxed) * 4096ULL;
            outTotal += j.totalFiles.load(std::memory_order_relaxed) * 4096ULL;
        } else {
            outCompleted += j.doneBytes.load(std::memory_order_relaxed);
            outTotal += j.totalBytes.load(std::memory_order_relaxed);
        }
    };

    std::lock_guard<std::mutex> lk(m_mutex);
    for (const auto& j : m_jobs) {
        TransferJobState st = j->state.load(std::memory_order_relaxed);
        switch (st) {
        case TransferJobState::Running:
        case TransferJobState::Verifying:
            hasRunning = true;
            addProgress(*j);
            break;
        case TransferJobState::Scanning:
            hasScanning = true;
            addProgress(*j);
            break;
        case TransferJobState::Paused:
            hasPaused = true;
            addProgress(*j);
            break;
        case TransferJobState::Queued:
            hasQueued = true;
            addProgress(*j);
            break;
        case TransferJobState::CompletedWithErrors:
            hasErrors = true;
            break;
        default:
            break;
        }
    }

    if (outCompleted > outTotal) outCompleted = outTotal;

    if (hasRunning) {
        return (outTotal > 0) ? 2 : 1; // 2 = TBPF_NORMAL, 1 = TBPF_INDETERMINATE
    }
    if (hasScanning || hasQueued) {
        if (hasPaused && !hasScanning) return 3; // 3 = TBPF_PAUSED
        return (outTotal > 0) ? 2 : 1;
    }
    if (hasPaused) {
        return 3; // 3 = TBPF_PAUSED
    }
    if (hasErrors) {
        outCompleted = 100;
        outTotal = 100;
        return 4; // 4 = TBPF_ERROR
    }
    return 0; // 0 = TBPF_NOPROGRESS
}

void TransferManager::SetSimulatedFreeSpaceForTesting(uint64_t freeBytes, bool enabled) {
    m_simFreeSpaceBytes.store(freeBytes, std::memory_order_relaxed);
    m_simFreeSpaceEnabled.store(enabled, std::memory_order_relaxed);
}

TransferConfig TransferManager::GetConfig() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_config;
}

void TransferManager::SetConfig(const TransferConfig& cfg) {
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_config = cfg;
        m_speedLimitMBpsAtomic.store(cfg.speedLimitMBps, std::memory_order_relaxed);
    }
    m_cv.notify_all();
}

std::wstring TransferManager::FormatQueueMode(TransferQueueMode m, bool pl) {
    switch (m) {
    case TransferQueueMode::SmartPerDrive: return pl ? L"Smart (per dysk)" : L"Smart (per drive)";
    case TransferQueueMode::Sequential:    return pl ? L"Sekwencyjna (1 naraz)" : L"Sequential (1 at a time)";
    case TransferQueueMode::ParallelAll:   return pl ? L"Równoległa (do 4)" : L"Parallel (up to 4)";
    }
    return L"Smart";
}

std::wstring TransferManager::FormatConflictPolicy(TransferConflictPolicy p, bool pl) {
    switch (p) {
    case TransferConflictPolicy::RenameCollision: return pl ? L"Zachowaj oba: nazwa (2)" : L"Keep both: name (2)";
    case TransferConflictPolicy::OverwriteNewer:  return pl ? L"Nadpisz nowsze/inne" : L"Overwrite newer/diff";
    case TransferConflictPolicy::OverwriteAll:    return pl ? L"Nadpisz wszystko" : L"Overwrite all";
    case TransferConflictPolicy::SkipExisting:    return pl ? L"Pomiń istniejące" : L"Skip existing";
    case TransferConflictPolicy::ResumePartial:   return pl ? L"Wznów częściowe" : L"Resume partial";
    }
    return L"Keep both";
}

std::wstring TransferManager::FormatVerifyMode(TransferVerifyMode v, bool pl) {
    switch (v) {
    case TransferVerifyMode::None:        return pl ? L"Brak (Max szybkość)" : L"None (Max speed)";
    case TransferVerifyMode::SizeAndTime: return pl ? L"Rozmiar + Data" : L"Size + Timestamp";
    case TransferVerifyMode::XxHash64:    return L"XXH64 Checksum";
    }
    return L"None";
}

std::wstring TransferManager::FormatBufferSize(uint32_t kb, bool pl) {
    if (kb == 0) return pl ? L"Auto (4x4 MB Ring)" : L"Auto (4x4 MB Ring)";
    if (kb >= 1024) return std::to_wstring(kb / 1024) + L" MB";
    return std::to_wstring(kb) + L" KB";
}

std::wstring TransferManager::FormatWorkers(uint32_t w, bool pl) {
    if (w == 0) return pl ? L"Auto (NVMe:8 / HDD:1)" : L"Auto (NVMe:8 / HDD:1)";
    return std::to_wstring(w) + (pl ? L" wątk." : L" thr.");
}

std::wstring TransferManager::FormatSpeedLimit(uint32_t mbps, bool pl) {
    if (mbps == 0) return pl ? L"Bez limitu" : L"Unlimited";
    return std::to_wstring(mbps) + L" MB/s";
}

std::wstring TransferManager::FormatSpeed(double bytesPerSec) {
    wchar_t buf[64] = {};
    if (bytesPerSec <= 0.5) return L"0 KB/s";
    if (bytesPerSec >= 1024.0 * 1024.0 * 1024.0) {
        swprintf_s(buf, L"%.2f GB/s", bytesPerSec / (1024.0 * 1024.0 * 1024.0));
    } else if (bytesPerSec >= 1024.0 * 1024.0) {
        swprintf_s(buf, L"%.1f MB/s", bytesPerSec / (1024.0 * 1024.0));
    } else {
        swprintf_s(buf, L"%.0f KB/s", bytesPerSec / 1024.0);
    }
    return buf;
}

std::wstring TransferManager::FormatEta(int64_t etaSec, bool pl) {
    if (etaSec < 0) return pl ? L"obliczanie..." : L"calculating...";
    if (etaSec == 0) return L"< 1 s";
    wchar_t buf[64] = {};
    if (etaSec >= 3600) {
        int64_t h = etaSec / 3600;
        int64_t m = (etaSec % 3600) / 60;
        int64_t s = etaSec % 60;
        swprintf_s(buf, L"%lld:%02lld:%02lld", static_cast<long long>(h), static_cast<long long>(m), static_cast<long long>(s));
    } else if (etaSec >= 60) {
        int64_t m = etaSec / 60;
        int64_t s = etaSec % 60;
        swprintf_s(buf, L"%02lld:%02lld", static_cast<long long>(m), static_cast<long long>(s));
    } else {
        swprintf_s(buf, L"%lld s", static_cast<long long>(etaSec));
    }
    return buf;
}

uint64_t TransferManager::ComputeXxHash64Memory(const void* data, size_t len, uint64_t seed) {
    XxHash64Stream stream(seed);
    stream.Update(data, len);
    return stream.Digest();
}

bool TransferManager::ComputeXxHash64File(const std::wstring& path, uint64_t& outHash, std::atomic<bool>* cancelFlag) {
    return ComputeXxHash64FileBuffered(path, nullptr, 0, outHash, cancelFlag);
}

// ============================================================================
// Enqueue & Queue Manipulation
// ============================================================================
int TransferManager::EnqueueTransfer(
    const std::vector<std::wstring>& sources,
    const std::wstring& targetDir,
    TransferOp op,
    const TransferConfig* overrideCfg)
{
    std::wstring cleanTarget = PathUtils::TrimTrailingSlashes(targetDir);
    if (sources.empty() || cleanTarget.empty() || cleanTarget == L"THIS_PC") return 0;

    if (!m_running.load()) {
        Initialize(m_notifyHwnd);
    }

    std::wstring dstNorm = PathUtils::NormalizePathKey(cleanTarget);
    std::vector<std::wstring> validSources;
    validSources.reserve(sources.size());

    for (const auto& rawSrc : sources) {
        std::wstring s = PathUtils::TrimTrailingSlashes(rawSrc);
        if (s.empty() || s == L"THIS_PC") continue;
        std::wstring spNorm = PathUtils::NormalizePathKey(s);

        // Moving an item into its own current parent folder is a no-op;
        // Copying an item into its own parent folder is valid ("Copy of / name (2).ext")!
        if (op == TransferOp::Move) {
            std::wstring parentRaw = FtpClient::IsFtpPath(s) ? FtpClient::GetParentFtpUrl(s) : PathUtils::GetParentPath(s);
            std::wstring parentNorm = PathUtils::NormalizePathKey(parentRaw);
            if (spNorm == dstNorm || parentNorm == dstNorm) continue;
        }
        validSources.push_back(std::move(s));
    }
    if (validSources.empty()) return 0;

    auto job = std::make_shared<InternalJob>();
    job->id = m_nextJobId.fetch_add(1);
    job->op = op;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        job->cfg = overrideCfg ? *overrideCfg : m_config;
    }
    job->conflictPolicyAtomic.store(static_cast<int>(job->cfg.conflictPolicy));
    job->sources = std::move(validSources);
    job->targetDir = cleanTarget;
    job->isFtpJob = FtpClient::IsFtpPath(job->targetDir) || FtpClient::IsFtpPath(job->sources[0]);
    PopulateJobNormWatchKeys(job->sources, job->targetDir, job->dstNormKey, job->srcNormKeys, job->srcParentNormKeys);

    // Quick non-blocking drive letter / UNC / FTP host tag for immediate queue scheduling before worker scans volume serials
    job->srcVolKey = QuickVolumeTag(job->sources[0], job->srcVolLabel);
    job->dstVolKey = QuickVolumeTag(job->targetDir, job->dstVolLabel);

    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_jobs.push_back(job);
    }
    SaveQueueState();
    m_cv.notify_all();
    NotifyUi(false);
    return job->id;
}

void TransferManager::SetDeleteItemDelayForTesting(uint32_t ms) {
    m_testDeleteDelayMs.store(ms, std::memory_order_relaxed);
}

int TransferManager::EnqueueDelete(
    const std::vector<std::wstring>& paths,
    bool permanent,
    const TransferConfig* overrideCfg,
    bool startPaused)
{
    if (!m_running.load()) {
        Initialize(m_notifyHwnd);
    }

    // Clean the list: no empty entries, no duplicates, and nothing that lies inside another selected
    // folder (it goes with that folder anyway, and listing it twice would only double the work).
    struct Candidate {
        std::wstring path;
        std::wstring key;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(paths.size());
    std::unordered_set<std::wstring> allKeys;
    for (const auto& raw : paths) {
        std::wstring p = PathUtils::TrimTrailingSlashes(raw);
        if (p.empty() || p == L"THIS_PC") continue;
        if (IsDeleteRefused(p)) {
            // A whole drive, a share root or a relative path never even becomes part of a job
            AppLog::Warn(L"Delete", L"Refused to delete: " + p);
            continue;
        }
        std::wstring key = PathUtils::NormalizePathKey(p);
        allKeys.insert(key);
        candidates.push_back({ std::move(p), std::move(key) });
    }

    std::vector<std::wstring> valid;
    valid.reserve(candidates.size());
    std::unordered_set<std::wstring> seen;
    for (const auto& c : candidates) {
        if (!seen.insert(c.key).second) continue;
        bool insideAnotherItem = false;
        if (!FtpClient::IsFtpPath(c.path)) {
            std::wstring up = PathUtils::GetParentPath(c.key);
            while (!up.empty()) {
                if (allKeys.count(up) != 0) {
                    insideAnotherItem = true;
                    break;
                }
                std::wstring next = PathUtils::GetParentPath(up);
                if (next == up) break;
                up = std::move(next);
            }
        }
        if (!insideAnotherItem) valid.push_back(c.path);
    }
    if (valid.empty()) return 0;

    auto job = std::make_shared<InternalJob>();
    job->id = m_nextJobId.fetch_add(1);
    job->op = TransferOp::Delete;
    job->recycle = !permanent;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        job->cfg = overrideCfg ? *overrideCfg : m_config;
    }
    job->sources = std::move(valid);
    job->pendingScanSources = job->sources;
    PopulateJobNormWatchKeys(job->sources, L"", job->dstNormKey, job->srcNormKeys, job->srcParentNormKeys);
    job->srcVolKey = QuickVolumeTag(job->sources[0], job->srcVolLabel);
    if (startPaused) {
        job->paused.store(true);
        job->state.store(TransferJobState::Paused);
    }

    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_jobs.push_back(job);
    }
    m_cv.notify_all();
    NotifyUi(false);
    return job->id;
}

bool TransferManager::PauseJob(int jobId) {
    std::shared_ptr<InternalJob> target;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        for (auto& j : m_jobs) {
            if (j->id == jobId) { target = j; break; }
        }
    }
    if (!target) return false;

    {
        std::lock_guard<std::mutex> jlk(target->jobMutex);
        TransferJobState st = target->state.load();
        if (st == TransferJobState::Completed || st == TransferJobState::CompletedWithErrors || st == TransferJobState::Cancelled) {
            return false;
        }
        if (!target->paused.exchange(true)) {
            target->pauseBeginTick = GetTickCount64();
            target->liveSpeedBps = 0.0;
            target->state.store(TransferJobState::Paused);
        }
    }
    m_cv.notify_all(); // Wake scheduler so next queued job on same drive can start while this one is paused!
    NotifyUi(false);
    return true;
}

bool TransferManager::ResumeJob(int jobId) {
    std::shared_ptr<InternalJob> target;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        for (auto& j : m_jobs) {
            if (j->id == jobId) { target = j; break; }
        }
    }
    if (!target || !target->paused.load()) return false;

    {
        std::lock_guard<std::mutex> jlk(target->jobMutex);
        if (target->pauseBeginTick > 0) {
            ULONGLONG now = GetTickCount64();
            if (now > target->pauseBeginTick) {
                target->pausedAccumMs += (now - target->pauseBeginTick);
            }
            target->pauseBeginTick = 0;
        }
        target->lastSpeedTick = GetTickCount64();
        target->lastSpeedBytes = target->doneBytes.load();
        target->paused.store(false);
        if (target->workerSpawned.load()) {
            target->state.store(TransferJobState::Running);
        } else {
            target->state.store(TransferJobState::Queued);
        }
    }
    target->pauseCv.notify_all();
    m_cv.notify_all();
    NotifyUi(false);
    return true;
}

bool TransferManager::TogglePauseJob(int jobId) {
    std::shared_ptr<InternalJob> target;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        for (auto& j : m_jobs) {
            if (j->id == jobId) { target = j; break; }
        }
    }
    if (!target) return false;
    return target->paused.load() ? ResumeJob(jobId) : PauseJob(jobId);
}

bool TransferManager::CancelOrRemoveJob(int jobId) {
    std::shared_ptr<InternalJob> target;
    bool erasedImmediately = false;
    bool cancelledBeforeSpawn = false;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        for (auto it = m_jobs.begin(); it != m_jobs.end(); ++it) {
            if ((*it)->id == jobId) {
                target = *it;
                TransferJobState st = target->state.load();
                if (st == TransferJobState::Completed || st == TransferJobState::CompletedWithErrors || st == TransferJobState::Cancelled) {
                    if (target->threadFinished.load() || !target->workerSpawned.load()) {
                        m_jobs.erase(it);
                        erasedImmediately = true;
                        break;
                    }
                }
                if (!target->workerSpawned.load()) {
                    target->cancelled.store(true);
                    target->state.store(TransferJobState::Cancelled);
                    if (target->op == TransferOp::Delete) {
                        // The window hides the items of a delete until the job reports back. A job that is
                        // dropped before it ever ran still has to report, or they would stay hidden.
                        TransferCompletionEvent ev;
                        ev.jobId = target->id;
                        ev.op = TransferOp::Delete;
                        ev.recycle = target->recycle;
                        ev.finalState = TransferJobState::Cancelled;
                        ev.sources = target->sources;
                        for (const auto& s : target->sources) {
                            std::wstring parent = FtpClient::IsFtpPath(s) ? FtpClient::GetParentFtpUrl(s) : PathUtils::GetParentPath(s);
                            if (!parent.empty() && std::find(ev.sourceDirs.begin(), ev.sourceDirs.end(), parent) == ev.sourceDirs.end()) {
                                ev.sourceDirs.push_back(std::move(parent));
                            }
                        }
                        ev.doneFiles = target->doneFiles.load();
                        ev.doneBytes = target->doneBytes.load();
                        m_completions.push_back(std::move(ev));
                        TrimCompletionEvents(m_completions);
                    }
                    m_jobs.erase(it);
                    cancelledBeforeSpawn = true;
                    break;
                }
                break;
            }
        }
    }
    if (erasedImmediately) {
        SaveQueueState();
        NotifyUi(false);
        return true;
    }
    if (cancelledBeforeSpawn) {
        SaveQueueState();
        m_cv.notify_all();
        NotifyUi(target->op == TransferOp::Delete);   // a dropped delete carries a completion event
        return true;
    }
    if (!target) return false;

    target->cancelled.store(true);
    target->paused.store(false);
    target->state.store(TransferJobState::Cancelled);
    SaveQueueState();
    target->pauseCv.notify_all();
    m_cv.notify_all();
    NotifyUi(false);
    return true;
}

bool TransferManager::ForceStartJob(int jobId) {
    std::shared_ptr<InternalJob> target;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        for (auto& j : m_jobs) {
            if (j->id == jobId) {
                target = j;
                break;
            }
        }
    }
    if (!target) return false;

    target->forceStart.store(true);
    {
        std::lock_guard<std::mutex> jlk(target->jobMutex);
        TransferJobState st = target->state.load();
        if (st != TransferJobState::Completed && st != TransferJobState::CompletedWithErrors && st != TransferJobState::Cancelled) {
            if (target->paused.load()) {
                if (target->pauseBeginTick > 0) {
                    ULONGLONG now = GetTickCount64();
                    if (now > target->pauseBeginTick) target->pausedAccumMs += (now - target->pauseBeginTick);
                    target->pauseBeginTick = 0;
                }
                target->lastSpeedTick = GetTickCount64();
                target->lastSpeedBytes = target->doneBytes.load();
                target->paused.store(false);
                if (!target->workerSpawned.load()) target->state.store(TransferJobState::Queued);
                else target->state.store(TransferJobState::Running);
            }
        }
    }
    target->pauseCv.notify_all();
    m_cv.notify_all();
    NotifyUi(false);
    return true;
}

bool TransferManager::BumpJobToTop(int jobId) {
    std::lock_guard<std::mutex> lk(m_mutex);
    for (size_t i = 0; i < m_jobs.size(); ++i) {
        if (m_jobs[i]->id == jobId) {
            auto j = m_jobs[i];
            m_jobs.erase(m_jobs.begin() + static_cast<ptrdiff_t>(i));
            // Insert right before the first Queued job
            auto pos = m_jobs.begin();
            while (pos != m_jobs.end() && (*pos)->workerSpawned.load()) {
                ++pos;
            }
            m_jobs.insert(pos, j);
            m_cv.notify_all();
            NotifyUi(false);
            return true;
        }
    }
    return false;
}

bool TransferManager::SetJobConflictPolicy(int jobId, TransferConflictPolicy policy) {
    bool updated = false;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        for (auto& j : m_jobs) {
            if (j->id == jobId) {
                j->conflictPolicyAtomic.store(static_cast<int>(policy));
                j->cfg.conflictPolicy = policy;
                updated = true;
                break;
            }
        }
    }
    if (updated) {
        SaveQueueState();
        NotifyUi(false);
    }
    return updated;
}

bool TransferManager::RetryFailedInJob(int jobId) {
    std::shared_ptr<InternalJob> oldJob;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        for (auto& j : m_jobs) {
            if (j->id == jobId && j->state.load() == TransferJobState::CompletedWithErrors && j->threadFinished.load()) {
                oldJob = j;
                break;
            }
        }
    }
    if (!oldJob) return false;

    // Reset failed items on the existing job and re-queue it (preserving .e2part files for byte-offset resume!)
    {
        std::lock_guard<std::mutex> jlk(oldJob->jobMutex);
        bool anyFailed = false;
        uint64_t remainingBytes = 0;
        uint64_t alreadyDoneBytes = 0;
        uint64_t alreadyDoneFiles = 0;
        for (auto& fi : oldJob->fileItems) {
            if (!fi.done) {
                fi.failed = false;
                fi.dstKnownFresh = false;
                anyFailed = true;
                remainingBytes += fi.size;
            } else {
                alreadyDoneBytes += fi.size;
                ++alreadyDoneFiles;
            }
        }
        if (oldJob->op == TransferOp::Delete) {
            // A delete also counts folders, and may still have requested items it could not even list
            for (const auto& di : oldJob->dirItems) {
                if (di.done) ++alreadyDoneFiles;
                else anyFailed = true;
            }
            if (!oldJob->pendingScanSources.empty()) anyFailed = true;
        }
        if (!anyFailed) return false;
        oldJob->errors.clear();
        oldJob->doneBytes.store(alreadyDoneBytes);
        oldJob->doneFiles.store(alreadyDoneFiles);
        oldJob->lastSpeedTickAtomic.store(0);
        oldJob->lastCurFileTickAtomic.store(0);
        oldJob->cancelled.store(false);
        oldJob->paused.store(false);
        oldJob->workerSpawned.store(false);
        oldJob->threadFinished.store(false);
        oldJob->state.store(TransferJobState::Queued);
        oldJob->summaryText.clear();
    }
    SaveQueueState();
    m_cv.notify_all();
    NotifyUi(false);
    return true;
}

void TransferManager::PauseAll() {
    std::vector<int> ids;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        for (auto& j : m_jobs) {
            TransferJobState st = j->state.load();
            if (st == TransferJobState::Running || st == TransferJobState::Scanning ||
                st == TransferJobState::Verifying || st == TransferJobState::Queued) {
                ids.push_back(j->id);
            }
        }
    }
    for (int id : ids) PauseJob(id);
}

void TransferManager::ResumeAll() {
    std::vector<int> ids;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        for (auto& j : m_jobs) {
            if (j->paused.load()) ids.push_back(j->id);
        }
    }
    for (int id : ids) ResumeJob(id);
}

bool TransferManager::TogglePauseActive() {
    int runningId = 0;
    int pausedId = 0;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        for (auto& j : m_jobs) {
            TransferJobState st = j->state.load();
            if (st == TransferJobState::Running || st == TransferJobState::Scanning || st == TransferJobState::Verifying) {
                runningId = j->id;
                break;
            }
            if (st == TransferJobState::Paused && pausedId == 0) {
                pausedId = j->id;
            }
        }
    }
    if (runningId != 0) {
        PauseAll();
        return true; // now paused
    }
    if (pausedId != 0) {
        ResumeAll();
        return false; // now resumed
    }
    return false;
}

void TransferManager::CancelActive() {
    int targetId = 0;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        for (auto& j : m_jobs) {
            TransferJobState st = j->state.load();
            if (st == TransferJobState::Running || st == TransferJobState::Scanning ||
                st == TransferJobState::Verifying || st == TransferJobState::Paused) {
                targetId = j->id;
                break;
            }
        }
    }
    if (targetId != 0) CancelOrRemoveJob(targetId);
}

int TransferManager::RetryAllFailed() {
    std::vector<int> ids;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        for (auto& j : m_jobs) {
            if (j->state.load() == TransferJobState::CompletedWithErrors) ids.push_back(j->id);
        }
    }
    int count = 0;
    for (int id : ids) {
        if (RetryFailedInJob(id)) ++count;
    }
    return count;
}

void TransferManager::ClearFinishedJobs() {
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_jobs.erase(std::remove_if(m_jobs.begin(), m_jobs.end(), [](const std::shared_ptr<InternalJob>& j) {
            TransferJobState st = j->state.load();
            return (st == TransferJobState::Completed || st == TransferJobState::CompletedWithErrors || st == TransferJobState::Cancelled) &&
                   (j->threadFinished.load() || !j->workerSpawned.load());
        }), m_jobs.end());
    }
    SaveQueueState();
    NotifyUi(false);
}

// ============================================================================
// Scheduler Loop (Smart Per-Drive / Sequential / Parallel Queue)
// ============================================================================
void TransferManager::SchedulerLoop() {
    while (m_running.load()) {
        std::vector<std::shared_ptr<InternalJob>> toLaunch;
        {
            std::unique_lock<std::mutex> lk(m_mutex);
            m_cv.wait_for(lk, std::chrono::milliseconds(250), [this]() {
                if (!m_running.load()) return true;
                for (const auto& j : m_jobs) {
                    if (!j->workerSpawned.load() && !j->paused.load() && !j->cancelled.load()) return true;
                }
                return false;
            });
            if (!m_running.load()) break;

            // Count currently active (unpaused) jobs & busy volumes
            size_t activeUnpaused = 0;
            std::vector<std::wstring> busyVolumes;
            for (const auto& j : m_jobs) {
                if (j->workerSpawned.load() && !j->threadFinished.load() && !j->paused.load() && !j->cancelled.load()) {
                    ++activeUnpaused;
                    if (!j->srcVolKey.empty()) busyVolumes.push_back(j->srcVolKey);
                    if (!j->dstVolKey.empty()) busyVolumes.push_back(j->dstVolKey);
                }
            }

            TransferQueueMode qMode = m_config.queueMode;
            for (auto& j : m_jobs) {
                if (j->workerSpawned.load() || j->paused.load() || j->cancelled.load()) continue;

                bool canStart = false;
                if (j->forceStart.load() || j->op == TransferOp::Delete) {
                    // A delete never waits in line behind copies: the user expects it to happen now
                    canStart = true;
                } else if (qMode == TransferQueueMode::Sequential) {
                    canStart = (activeUnpaused == 0);
                } else if (qMode == TransferQueueMode::ParallelAll) {
                    canStart = (activeUnpaused < 4);
                } else {
                    // SmartPerDrive: run immediately if neither source nor destination volume is busy!
                    bool overlap = false;
                    for (const auto& bv : busyVolumes) {
                        if (bv == j->srcVolKey || bv == j->dstVolKey) {
                            overlap = true;
                            break;
                        }
                    }
                    canStart = !overlap && (activeUnpaused < 6);
                }

                if (canStart) {
                    j->workerSpawned.store(true);
                    j->state.store(TransferJobState::Scanning);
                    toLaunch.push_back(j);
                    ++activeUnpaused;
                    if (!j->srcVolKey.empty()) busyVolumes.push_back(j->srcVolKey);
                    if (!j->dstVolKey.empty()) busyVolumes.push_back(j->dstVolKey);
                }
            }
        }

        // Reap finished worker threads so long sessions never accumulate dead thread handles
        for (auto it = m_jobThreads.begin(); it != m_jobThreads.end(); ) {
            if (it->first && it->first->load(std::memory_order_acquire)) {
                if (it->second.joinable()) it->second.join();
                it = m_jobThreads.erase(it);
            } else {
                ++it;
            }
        }

        if (!toLaunch.empty()) {
            SaveQueueState();
        }
        for (auto& job : toLaunch) {
            auto doneFlag = std::make_shared<std::atomic<bool>>(false);
            m_jobThreads.emplace_back(doneFlag, std::thread([this, job, doneFlag]() {
                ExecuteJobWorker(job);
                doneFlag->store(true, std::memory_order_release);
            }));
        }
    }
}

// ============================================================================
// Pause / Cancel & Token-Bucket Rate Limiter
// ============================================================================
bool TransferManager::WaitIfPausedOrCancelled(InternalJob& job) {
    if (job.cancelled.load(std::memory_order_relaxed) || !m_running.load(std::memory_order_relaxed)) {
        return false;
    }
    if (job.paused.load(std::memory_order_relaxed)) {
        std::unique_lock<std::mutex> jlk(job.jobMutex);
        job.pauseCv.wait(jlk, [&]() {
            return !job.paused.load() || job.cancelled.load() || !m_running.load();
        });
    }
    return !job.cancelled.load(std::memory_order_relaxed) && m_running.load(std::memory_order_relaxed);
}

void TransferManager::RecordProgressBytes(InternalJob& job, uint64_t chunkWritten) {
    uint64_t newDone = job.doneBytes.fetch_add(chunkWritten, std::memory_order_relaxed) + chunkWritten;
    m_sessionBytes.fetch_add(chunkWritten, std::memory_order_relaxed);

    ULONGLONG now = GetTickCount64();
    ULONGLONG prevTick = job.lastSpeedTickAtomic.load(std::memory_order_relaxed);
    if (now - prevTick >= 180 &&
        job.lastSpeedTickAtomic.compare_exchange_strong(prevTick, now, std::memory_order_relaxed))
    {
        std::lock_guard<std::mutex> jlk(job.jobMutex);
        if (now > job.lastSpeedTick) {
            double dtSec = static_cast<double>(now - job.lastSpeedTick) / 1000.0;
            uint64_t dBytes = (newDone >= job.lastSpeedBytes) ? (newDone - job.lastSpeedBytes) : 0ULL;
            double instBps = (dtSec > 0.001) ? (static_cast<double>(dBytes) / dtSec) : 0.0;
            job.liveSpeedBps = (job.liveSpeedBps <= 1.0) ? instBps : (job.liveSpeedBps * 0.65 + instBps * 0.35);
            if (job.liveSpeedBps > job.peakSpeedBps) job.peakSpeedBps = job.liveSpeedBps;
            job.lastSpeedTick = now;
            job.lastSpeedBytes = newDone;
        }
    }
}

void TransferManager::ThrottleBytes(uint64_t bytesJustTransferred, InternalJob& job) {
    uint32_t limitMBps = m_speedLimitMBpsAtomic.load(std::memory_order_relaxed);
    if (limitMBps == 0) limitMBps = job.cfg.speedLimitMBps;
    if (limitMBps == 0 || bytesJustTransferred == 0) return;

    const uint64_t maxBytesPer100Ms = (static_cast<uint64_t>(limitMBps) * 1024ULL * 1024ULL) / 10ULL;
    if (maxBytesPer100Ms == 0) return;

    DWORD sleepMs = 0;
    {
        std::lock_guard<std::mutex> tlk(m_throttleMutex);
        ULONGLONG now = GetTickCount64();
        if (now - m_throttleWindowTick >= 100) {
            m_throttleWindowTick = now;
            m_throttleBytesInWindow = 0;
        }
        m_throttleBytesInWindow += bytesJustTransferred;
        if (m_throttleBytesInWindow > maxBytesPer100Ms) {
            uint64_t excess = m_throttleBytesInWindow - maxBytesPer100Ms;
            uint64_t waitMs = (excess * 100ULL) / maxBytesPer100Ms;
            sleepMs = static_cast<DWORD>(std::clamp<uint64_t>(waitMs, 5ULL, 250ULL));
            m_throttleWindowTick = now + sleepMs;
            m_throttleBytesInWindow = 0;
        }
    }
    if (sleepMs > 0 && !job.cancelled.load(std::memory_order_relaxed)) {
        Sleep(sleepMs);
    }
}

void TransferManager::NotifyUi(bool isCompletion) {
    if (!m_notifyHwnd) return;
    if (isCompletion || !m_uiNotifyPending.exchange(true, std::memory_order_relaxed)) {
        PostMessageW(m_notifyHwnd, WM_APP_TRANSFER_UPDATE, isCompletion ? 1 : 0, 0);
    }
}

// ============================================================================
// Pre-Flight Free Disk Space Check (Auto-Pause Before Filling Destination Drive)
// ============================================================================
bool TransferManager::CheckPreFlightDiskSpaceOrAutoPause(InternalJob& job) {
    if (FtpClient::IsFtpPath(job.targetDir)) return true;
    if (job.sameVolume && job.op == TransferOp::Move) return true;
    if (job.supportsBlockClone && job.cfg.verifyMode != TransferVerifyMode::XxHash64) return true;

    while (!job.cancelled.load(std::memory_order_relaxed) && m_running.load(std::memory_order_relaxed)) {
        if (!WaitIfPausedOrCancelled(job)) return false;
        if (job.forceStart.load(std::memory_order_relaxed)) return true;
        if (job.fileItems.empty()) return true;

        uint64_t availBytes = UINT64_MAX;
        if (m_simFreeSpaceEnabled.load(std::memory_order_relaxed)) {
            availBytes = m_simFreeSpaceBytes.load(std::memory_order_relaxed);
        } else {
            EnsureDirHierarchyExt(job.targetDir);
            ULARGE_INTEGER freeCaller = {};
            if (GetDiskFreeSpaceExW(ToExtPath(job.targetDir).c_str(), &freeCaller, nullptr, nullptr)) {
                availBytes = freeCaller.QuadPart;
            }
        }

        // O(N) zero-syscall upper bound first (~0 overhead when drive has ample room)
        uint64_t rawRemaining = 0;
        for (const auto& fi : job.fileItems) {
            if (!fi.done) rawRemaining += fi.size;
        }
        if (availBytes >= rawRemaining) return true;

        // Space looks tight: compute exact net bytes needed (accounting for sparse allocation, SkipExisting, OverwriteNewer & .e2part)
        TransferConflictPolicy pol = static_cast<TransferConflictPolicy>(job.conflictPolicyAtomic.load(std::memory_order_relaxed));
        uint64_t netNeeded = 0;
        for (const auto& fi : job.fileItems) {
            if (fi.done) continue;
            uint64_t itemNeed = fi.size;

            if (!FtpClient::IsFtpPath(fi.srcPath) &&
                (fi.attributes & FILE_ATTRIBUTE_SPARSE_FILE) != 0 &&
                job.dstSupportsSparse)
            {
                DWORD hi = 0;
                DWORD lo = GetCompressedFileSizeW(ToExtPath(fi.srcPath).c_str(), &hi);
                if (lo != INVALID_FILE_SIZE || GetLastError() == NO_ERROR) {
                    uint64_t allocSz = (static_cast<uint64_t>(hi) << 32) | lo;
                    if (allocSz < itemNeed) itemNeed = allocSz;
                }
            }

            if (!fi.dstKnownFresh) {
                WIN32_FILE_ATTRIBUTE_DATA dData = {};
                if (GetFileAttributesExW(ToExtPath(fi.dstPath).c_str(), GetFileExInfoStandard, &dData) &&
                    !(dData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                {
                    uint64_t existSz = (static_cast<uint64_t>(dData.nFileSizeHigh) << 32) | dData.nFileSizeLow;
                    if (pol == TransferConflictPolicy::SkipExisting) {
                        itemNeed = 0;
                    } else if (pol == TransferConflictPolicy::OverwriteNewer &&
                               existSz == fi.size && AreFileTimesWithin2Sec(fi.lastWriteTime, dData.ftLastWriteTime)) {
                        itemNeed = 0;
                    } else if (pol == TransferConflictPolicy::ResumePartial && existSz < itemNeed) {
                        itemNeed -= existSz;
                    } else if ((pol == TransferConflictPolicy::OverwriteAll || pol == TransferConflictPolicy::OverwriteNewer) && !job.cfg.usePartFiles) {
                        itemNeed = (itemNeed > existSz) ? (itemNeed - existSz) : 0;
                    }
                }
                if (itemNeed > 0 && job.cfg.usePartFiles) {
                    WIN32_FILE_ATTRIBUTE_DATA pData = {};
                    if (GetFileAttributesExW(ToExtPath(fi.dstPath + L".e2part").c_str(), GetFileExInfoStandard, &pData) &&
                        !(pData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                    {
                        uint64_t partSz = (static_cast<uint64_t>(pData.nFileSizeHigh) << 32) | pData.nFileSizeLow;
                        if (partSz < itemNeed && AreFileTimesWithin2Sec(pData.ftCreationTime, fi.lastWriteTime)) {
                            itemNeed -= partSz;
                        }
                    }
                }
            }
            netNeeded += itemNeed;
        }

        if (availBytes >= netNeeded) return true;

        {
            std::lock_guard<std::mutex> jlk(job.jobMutex);
            if (!job.paused.exchange(true)) {
                job.pauseBeginTick = GetTickCount64();
                job.liveSpeedBps = 0.0;
            }
            job.state.store(TransferJobState::Paused);
            job.summaryText = L"⚠ Brak miejsca na dysku (" + job.dstVolLabel + L"): wymagane " +
                              FileEngine::FormatSizeDetailed(netNeeded) + L", wolne " +
                              FileEngine::FormatSizeDetailed(availBytes) +
                              L" — zwolnij miejsce i kliknij Wznów";
        }
        SaveQueueState();
        m_cv.notify_all(); // Let queued jobs targeting other drives run while this job waits for space!
        NotifyUi(false);

        if (!WaitIfPausedOrCancelled(job)) return false;

        {
            std::lock_guard<std::mutex> jlk(job.jobMutex);
            job.summaryText.clear();
            if (!job.paused.load() && !job.cancelled.load()) {
                job.state.store(TransferJobState::Running);
            }
        }
    }
    return !job.cancelled.load(std::memory_order_relaxed) && m_running.load(std::memory_order_relaxed);
}

// ============================================================================
// Non-Blocking Async FTP / FTPS Worker Execution (Download, Upload & Server-to-Server)
// ============================================================================
void TransferManager::ExecuteFtpJobWorker(std::shared_ptr<InternalJob> job) {
    job->startTick = GetTickCount64();
    job->lastSpeedTick = job->startTick;
    job->lastSpeedTickAtomic.store(job->startTick, std::memory_order_relaxed);
    job->lastCurFileTickAtomic.store(0, std::memory_order_relaxed);
    job->lastSpeedBytes = job->doneBytes.load();

    const bool dstIsFtp = FtpClient::IsFtpPath(job->targetDir);
    if (!dstIsFtp) {
        EnsureDirHierarchyExt(job->targetDir);
        VolumeIdentity dstVi = QueryVolumeIdentity(job->targetDir);
        std::lock_guard<std::mutex> lk(m_mutex);
        if (!dstVi.key.empty()) job->dstVolKey = dstVi.key;
        if (!dstVi.label.empty()) job->dstVolLabel = dstVi.label;
        job->dstSupportsSparse = ((dstVi.fsFlags & FILE_SUPPORTS_SPARSE_FILES) != 0);
    }

    bool isRetryRun = false;
    {
        std::lock_guard<std::mutex> jlk(job->jobMutex);
        isRetryRun = !job->fileItems.empty();
    }

    // PHASE 1: Recursive FTP / Local Tree Scan
    if (!isRetryRun && !job->cancelled.load() && m_running.load()) {
        job->state.store(job->paused.load() ? TransferJobState::Paused : TransferJobState::Scanning);
        std::vector<InternalFileItem> scannedFiles;
        std::vector<InternalDirItem>  scannedDirs;

        auto makeChildDst = [&](const std::wstring& parentDst, const std::wstring& childName) -> std::wstring {
            return dstIsFtp
                ? FtpClient::JoinFtpPath(parentDst, childName)
                : (PathUtils::EnsureTrailingSlash(parentDst) + childName);
        };

        auto scanFtpDirRec = [&](auto& self, const std::wstring& curSrcUrl, const std::wstring& curDst) -> void {
            if (!WaitIfPausedOrCancelled(*job)) return;
            scannedDirs.push_back(InternalDirItem{ curSrcUrl, curDst, {}, {}, {}, FILE_ATTRIBUTE_DIRECTORY });

            std::vector<FileEntry> entries;
            std::wstring errStr;
            if (!FtpClient::EnumerateFtpDirectory(curSrcUrl, true, true, Lang::EN, entries, &errStr)) {
                std::lock_guard<std::mutex> jlk(job->jobMutex);
                job->errors.push_back({ curSrcUrl, curDst, ERROR_CONNECTION_ABORTED,
                    errStr.empty() ? L"Nie można odczytać katalogu FTP" : errStr, 0 });
                return;
            }
            for (const auto& fe : entries) {
                if (!WaitIfPausedOrCancelled(*job)) return;
                std::wstring childDst = makeChildDst(curDst, fe.name);
                if (fe.isDirectory) {
                    self(self, fe.fullPath, childDst);
                } else {
                    InternalFileItem fi;
                    fi.srcPath = fe.fullPath;
                    fi.dstPath = childDst;
                    fi.size = fe.size;
                    fi.lastWriteTime = fe.writeTime;
                    scannedFiles.push_back(std::move(fi));
                    job->totalFiles.fetch_add(1, std::memory_order_relaxed);
                    job->totalBytes.fetch_add(fe.size, std::memory_order_relaxed);
                }
            }
        };

        auto scanLocalDirRec = [&](auto& self, const std::wstring& curSrcDir, const std::wstring& curDstUrl) -> void {
            if (!WaitIfPausedOrCancelled(*job)) return;
            scannedDirs.push_back(InternalDirItem{ curSrcDir, curDstUrl, {}, {}, {}, FILE_ATTRIBUTE_DIRECTORY });

            std::wstring pattern = PathUtils::EnsureTrailingSlash(ToExtPath(curSrcDir)) + L"*";
            WIN32_FIND_DATAW ffd = {};
            HANDLE hFind = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &ffd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
            if (hFind == INVALID_HANDLE_VALUE) return;

            std::vector<std::wstring> subDirs;
            const std::wstring srcPrefix = PathUtils::EnsureTrailingSlash(curSrcDir);
            do {
                if (ffd.cFileName[0] == L'.' &&
                    (ffd.cFileName[1] == L'\0' || (ffd.cFileName[1] == L'.' && ffd.cFileName[2] == L'\0'))) {
                    continue;
                }
                if (wcscmp(ffd.cFileName, L".explorer2_undo") == 0) continue;
                bool isDir = (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                bool isReparse = (ffd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
                if (isDir) {
                    if (!isReparse) subDirs.push_back(ffd.cFileName);
                } else {
                    uint64_t fsz = (static_cast<uint64_t>(ffd.nFileSizeHigh) << 32) | ffd.nFileSizeLow;
                    InternalFileItem fi;
                    fi.srcPath = srcPrefix + ffd.cFileName;
                    fi.dstPath = makeChildDst(curDstUrl, ffd.cFileName);
                    fi.size = fsz;
                    fi.creationTime = ffd.ftCreationTime;
                    fi.lastAccessTime = ffd.ftLastAccessTime;
                    fi.lastWriteTime = ffd.ftLastWriteTime;
                    fi.attributes = ffd.dwFileAttributes;
                    scannedFiles.push_back(std::move(fi));
                    job->totalFiles.fetch_add(1, std::memory_order_relaxed);
                    job->totalBytes.fetch_add(fsz, std::memory_order_relaxed);
                }
            } while (FindNextFileW(hFind, &ffd));
            FindClose(hFind);

            for (const auto& sdName : subDirs) {
                if (!WaitIfPausedOrCancelled(*job)) return;
                self(self, srcPrefix + sdName, makeChildDst(curDstUrl, sdName));
            }
        };

        for (const auto& topSrc : job->sources) {
            if (!WaitIfPausedOrCancelled(*job)) break;
            if (FtpClient::IsFtpPath(topSrc)) {
                bool isDir = false;
                uint64_t fsz = 0;
                FILETIME wt = {};
                FtpClient::ProbeFtpItemInfo(topSrc, isDir, fsz, &wt);
                std::wstring leaf = FtpClient::GetFtpLeafName(topSrc);
                if (leaf.empty()) leaf = L"ftp_item";
                std::wstring topDst = makeChildDst(job->targetDir, leaf);
                if (isDir) {
                    scanFtpDirRec(scanFtpDirRec, topSrc, topDst);
                } else {
                    InternalFileItem fi;
                    fi.srcPath = topSrc;
                    fi.dstPath = topDst;
                    fi.size = fsz;
                    fi.lastWriteTime = wt;
                    scannedFiles.push_back(std::move(fi));
                    job->totalFiles.fetch_add(1, std::memory_order_relaxed);
                    job->totalBytes.fetch_add(fsz, std::memory_order_relaxed);
                }
            } else {
                WIN32_FILE_ATTRIBUTE_DATA fad = {};
                std::wstring leaf = PathUtils::GetFileName(topSrc);
                std::wstring topDst = makeChildDst(job->targetDir, leaf);
                if (!GetFileAttributesExW(ToExtPath(topSrc).c_str(), GetFileExInfoStandard, &fad)) {
                    DWORD err = GetLastError();
                    std::lock_guard<std::mutex> jlk(job->jobMutex);
                    job->errors.push_back({ topSrc, topDst, err, FormatWin32Err(err), 0 });
                    continue;
                }
                if (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    scanLocalDirRec(scanLocalDirRec, topSrc, topDst);
                } else {
                    uint64_t fsz = (static_cast<uint64_t>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
                    InternalFileItem fi;
                    fi.srcPath = topSrc;
                    fi.dstPath = topDst;
                    fi.size = fsz;
                    fi.creationTime = fad.ftCreationTime;
                    fi.lastAccessTime = fad.ftLastAccessTime;
                    fi.lastWriteTime = fad.ftLastWriteTime;
                    fi.attributes = fad.dwFileAttributes;
                    scannedFiles.push_back(std::move(fi));
                    job->totalFiles.fetch_add(1, std::memory_order_relaxed);
                    job->totalBytes.fetch_add(fsz, std::memory_order_relaxed);
                }
            }
        }

        std::lock_guard<std::mutex> jlk(job->jobMutex);
        job->dirItems = std::move(scannedDirs);
        job->fileItems = std::move(scannedFiles);
    }

    // PHASE 1.5: Pre-Flight Disk Space Check (for FTP -> Local downloads) & Destination Folder Creation
    if (!job->cancelled.load() && m_running.load()) {
        if (CheckPreFlightDiskSpaceOrAutoPause(*job)) {
            if (dstIsFtp) {
                FtpClient::CreateFtpDirectory(job->targetDir, nullptr);
                for (const auto& d : job->dirItems) {
                    if (!WaitIfPausedOrCancelled(*job)) break;
                    FtpClient::CreateFtpDirectory(d.dstPath, nullptr);
                }
            } else {
                EnsureDirHierarchyExt(job->targetDir);
                for (const auto& d : job->dirItems) {
                    if (!WaitIfPausedOrCancelled(*job)) break;
                    EnsureDirHierarchyExt(d.dstPath);
                }
            }
        }
    }

    // PHASE 2: Chunked Streaming FTP Execution with Live Telemetry, Throttle, Pause/Resume & Auto-Retry
    if (!job->cancelled.load() && m_running.load()) {
        job->state.store(job->paused.load() ? TransferJobState::Paused : TransferJobState::Running);

        for (size_t i = 0; i < job->fileItems.size(); ++i) {
            if (!WaitIfPausedOrCancelled(*job)) break;
            auto& fi = job->fileItems[i];
            if (fi.done) continue;

            const bool srcIsFtp = FtpClient::IsFtpPath(fi.srcPath);
            std::wstring leafName = srcIsFtp ? FtpClient::GetFtpLeafName(fi.srcPath) : PathUtils::GetFileName(fi.srcPath);
            {
                std::lock_guard<std::mutex> jlk(job->jobMutex);
                job->currentFile = leafName;
            }

            TransferConflictPolicy pol = static_cast<TransferConflictPolicy>(job->conflictPolicyAtomic.load(std::memory_order_relaxed));
            std::wstring finalDst = fi.dstPath;

            // Conflict resolution on destination
            if (!dstIsFtp) {
                WIN32_FILE_ATTRIBUTE_DATA dData = {};
                if (GetFileAttributesExW(ToExtPath(finalDst).c_str(), GetFileExInfoStandard, &dData) &&
                    !(dData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                {
                    uint64_t existSz = (static_cast<uint64_t>(dData.nFileSizeHigh) << 32) | dData.nFileSizeLow;
                    if (pol == TransferConflictPolicy::SkipExisting) {
                        fi.done = true;
                        job->skippedFiles.fetch_add(1, std::memory_order_relaxed);
                        job->doneBytes.fetch_add(fi.size, std::memory_order_relaxed);
                        continue;
                    }
                    if (pol == TransferConflictPolicy::OverwriteNewer &&
                        fi.size > 0 && existSz == fi.size &&
                        (fi.lastWriteTime.dwHighDateTime != 0 || fi.lastWriteTime.dwLowDateTime != 0) &&
                        AreFileTimesWithin2Sec(fi.lastWriteTime, dData.ftLastWriteTime))
                    {
                        fi.done = true;
                        job->skippedFiles.fetch_add(1, std::memory_order_relaxed);
                        job->doneBytes.fetch_add(fi.size, std::memory_order_relaxed);
                        if (job->op == TransferOp::Move && srcIsFtp) {
                            FtpClient::DeleteFtpItem(fi.srcPath, false, nullptr);
                        }
                        continue;
                    }
                    if (pol == TransferConflictPolicy::RenameCollision) {
                        finalDst = NextFreeCollisionFilePath(finalDst);
                        fi.dstPath = finalDst;
                        job->renamedCollisions.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            } else {
                bool remIsDir = false;
                uint64_t remSz = 0;
                if (FtpClient::ProbeFtpItemInfo(finalDst, remIsDir, remSz, nullptr) && !remIsDir) {
                    if (pol == TransferConflictPolicy::SkipExisting) {
                        fi.done = true;
                        job->skippedFiles.fetch_add(1, std::memory_order_relaxed);
                        job->doneBytes.fetch_add(fi.size, std::memory_order_relaxed);
                        continue;
                    }
                    if (pol == TransferConflictPolicy::RenameCollision) {
                        std::wstring origLeaf = FtpClient::GetFtpLeafName(finalDst);
                        std::wstring parentUrl = FtpClient::GetParentFtpUrl(finalDst);
                        size_t dot = origLeaf.find_last_of(L'.');
                        std::wstring stem = (dot != std::wstring::npos && dot > 0) ? origLeaf.substr(0, dot) : origLeaf;
                        std::wstring ext  = (dot != std::wstring::npos && dot > 0) ? origLeaf.substr(dot) : L"";
                        for (int cIdx = 2; cIdx < 500; ++cIdx) {
                            std::wstring candUrl = FtpClient::JoinFtpPath(parentUrl, stem + L" (" + std::to_wstring(cIdx) + L")" + ext);
                            bool cDir = false; uint64_t cSz = 0;
                            if (!FtpClient::ProbeFtpItemInfo(candUrl, cDir, cSz, nullptr)) {
                                finalDst = candUrl;
                                fi.dstPath = finalDst;
                                job->renamedCollisions.fetch_add(1, std::memory_order_relaxed);
                                break;
                            }
                        }
                    }
                }
            }

            bool ok = false;
            std::wstring errMsg;
            const uint32_t maxRetries = std::min<uint32_t>(job->cfg.maxAutoRetries, 3u);

            for (uint32_t attempt = 0; attempt <= maxRetries; ++attempt) {
                if (!WaitIfPausedOrCancelled(*job)) break;
                if (attempt > 0) Sleep(250 * attempt);

                errMsg.clear();
                uint64_t attemptBytes = 0;
                auto onChunk = [&](uint64_t chunk) -> bool {
                    if (chunk > 0) {
                        attemptBytes += chunk;
                        // Dynamically expand totalBytes if FTP server didn't report exact file size in LIST
                        uint64_t curDone = job->doneBytes.load(std::memory_order_relaxed);
                        uint64_t curTot  = job->totalBytes.load(std::memory_order_relaxed);
                        if (curDone + chunk > curTot) {
                            job->totalBytes.store(curDone + chunk, std::memory_order_relaxed);
                        }
                        RecordProgressBytes(*job, chunk);
                        ThrottleBytes(chunk, *job);
                    }
                    return WaitIfPausedOrCancelled(*job);
                };

                if (srcIsFtp && !dstIsFtp) {
                    const bool usePart = job->cfg.usePartFiles && fi.size >= (1024ULL * 1024ULL);
                    std::wstring writePath = usePart ? (finalDst + L".e2part") : finalDst;
                    ok = FtpClient::DownloadFtpFileToLocalEx(fi.srcPath, writePath, onChunk, &errMsg);
                    if (ok && (fi.lastWriteTime.dwHighDateTime != 0 || fi.lastWriteTime.dwLowDateTime != 0)) {
                        HANDLE hFix = CreateFileW(ToExtPath(writePath).c_str(), FILE_WRITE_ATTRIBUTES,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                        if (hFix != INVALID_HANDLE_VALUE) {
                            SetFileTime(hFix, nullptr, nullptr, &fi.lastWriteTime);
                            CloseHandle(hFix);
                        }
                    }
                    if (ok && usePart) {
                        DWORD renErr = 0;
                        if (!MoveItemAtomicWithRetry(writePath, finalDst, false, true, renErr)) {
                            ok = false;
                            errMsg = L"Atomic .e2part commit failed: " + FormatWin32Err(renErr);
                        }
                    } else if (!ok && usePart && job->cancelled.load()) {
                        DeleteFileW(ToExtPath(writePath).c_str());
                    }
                } else if (!srcIsFtp && dstIsFtp) {
                    std::wstring dstParentUrl = FtpClient::GetParentFtpUrl(finalDst);
                    std::wstring dstLeafName  = FtpClient::GetFtpLeafName(finalDst);
                    ok = FtpClient::UploadLocalFileToFtpEx(fi.srcPath, dstParentUrl, onChunk, &errMsg, dstLeafName);
                } else {
                    // FTP -> FTP bridge via temporary local staging file
                    wchar_t tmpDir[MAX_PATH] = {};
                    GetTempPathW(MAX_PATH, tmpDir);
                    std::wstring stagePath = std::wstring(tmpDir) + L"E2FtpBridge_" +
                                             std::to_wstring(job->id) + L"_" + std::to_wstring(i) + L".tmp";
                    auto halfChunk = [&](uint64_t chunk) -> bool {
                        return onChunk(chunk / 2);
                    };
                    if (FtpClient::DownloadFtpFileToLocalEx(fi.srcPath, stagePath, halfChunk, &errMsg)) {
                        ok = FtpClient::UploadLocalFileToFtpEx(
                            stagePath,
                            FtpClient::GetParentFtpUrl(finalDst),
                            halfChunk,
                            &errMsg,
                            FtpClient::GetFtpLeafName(finalDst));
                    }
                    DeleteFileW(stagePath.c_str());
                }

                if (ok) {
                    if (fi.size > attemptBytes) {
                        // Reconcile rounding (e.g. FTP->FTP halfChunk integer division)
                        RecordProgressBytes(*job, fi.size - attemptBytes);
                    }
                    break;
                }

                if (attemptBytes > 0) {
                    job->doneBytes.fetch_sub(attemptBytes, std::memory_order_relaxed);
                }
                if (job->cancelled.load() || !m_running.load()) break;
            }

            if (job->cancelled.load() || !m_running.load()) break;

            if (ok && job->op == TransferOp::Move) {
                if (srcIsFtp) {
                    FtpClient::DeleteFtpItem(fi.srcPath, false, nullptr);
                } else {
                    std::wstring extSrc = ToExtPath(fi.srcPath);
                    SetFileAttributesW(extSrc.c_str(), FILE_ATTRIBUTE_NORMAL);
                    DeleteFileW(extSrc.c_str());
                }
            }

            fi.done = ok;
            fi.failed = !ok;
            if (!ok) {
                std::lock_guard<std::mutex> jlk(job->jobMutex);
                job->errors.push_back({ fi.srcPath, finalDst, ERROR_CONNECTION_ABORTED,
                    errMsg.empty() ? L"Błąd transferu FTP/FTPS" : errMsg, 0 });
            } else {
                job->doneFiles.fetch_add(1, std::memory_order_relaxed);
                m_sessionFiles.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    // PHASE 3: Source Directory Cleanup on Move
    if (!job->cancelled.load() && m_running.load() && job->op == TransferOp::Move) {
        std::vector<InternalDirItem> dirsCopy;
        bool hasErrorsOrSkips = false;
        {
            std::lock_guard<std::mutex> jlk(job->jobMutex);
            dirsCopy = job->dirItems;
            hasErrorsOrSkips = !job->errors.empty() || (job->skippedFiles.load() > 0);
        }
        if (!hasErrorsOrSkips) {
            for (auto it = dirsCopy.rbegin(); it != dirsCopy.rend(); ++it) {
                if (FtpClient::IsFtpPath(it->srcPath)) {
                    FtpClient::DeleteFtpItem(it->srcPath, true, nullptr);
                } else {
                    RemoveDirectoryW(ToExtPath(it->srcPath).c_str());
                }
            }
        }
    }

    // Graceful app shutdown: keep unfinished job in transfer_queue.dat for next launch!
    if (!m_running.load() && !job->cancelled.load()) {
        std::lock_guard<std::mutex> lk(m_mutex);
        job->threadFinished.store(true);
        m_cv.notify_all();
        return;
    }

    // PHASE 4: Finalize FTP Job State & Notify UI
    FtpClient::InvalidateListingCache();
    ULONGLONG endTick = GetTickCount64();
    uint64_t activeMs = 1ULL;
    size_t errCount = 0;
    TransferJobState finalSt = TransferJobState::Completed;
    {
        std::lock_guard<std::mutex> jlk(job->jobMutex);
        if (job->pauseBeginTick > 0 && endTick > job->pauseBeginTick) {
            job->pausedAccumMs += (endTick - job->pauseBeginTick);
            job->pauseBeginTick = 0;
        }
        uint64_t rawElapsed = (endTick > job->startTick) ? (endTick - job->startTick) : 1ULL;
        activeMs = (rawElapsed > job->pausedAccumMs) ? (rawElapsed - job->pausedAccumMs) : 1ULL;
        if (activeMs == 0) activeMs = 1;
        job->finalElapsedMs = activeMs;

        errCount = job->errors.size();
        job->liveSpeedBps = 0.0;
        job->currentFile.clear();

        if (job->cancelled.load()) finalSt = TransferJobState::Cancelled;
        else if (errCount > 0)     finalSt = TransferJobState::CompletedWithErrors;
        job->state.store(finalSt);
    }

    double avgBps = (static_cast<double>(job->doneBytes.load()) * 1000.0) / static_cast<double>(activeMs);
    {
        wchar_t sumBuf[384] = {};
        if (finalSt == TransferJobState::Cancelled) {
            swprintf_s(sumBuf, L"Anulowano FTP (%llu/%llu plików)",
                static_cast<unsigned long long>(job->doneFiles.load()),
                static_cast<unsigned long long>(job->totalFiles.load()));
        } else if (finalSt == TransferJobState::CompletedWithErrors) {
            swprintf_s(sumBuf, L"FTP z błędami (%zu): %llu/%llu plików (%s) • %s",
                errCount,
                static_cast<unsigned long long>(job->doneFiles.load()),
                static_cast<unsigned long long>(job->totalFiles.load()),
                FileEngine::FormatSizeDetailed(job->doneBytes.load()).c_str(),
                FormatSpeed(avgBps).c_str());
        } else {
            swprintf_s(sumBuf, L"FTP OK: %llu plików (%s) w %.2f s • Śr. %s",
                static_cast<unsigned long long>(job->doneFiles.load()),
                FileEngine::FormatSizeDetailed(job->doneBytes.load()).c_str(),
                static_cast<double>(activeMs) / 1000.0,
                FormatSpeed(avgBps).c_str());
        }
        std::lock_guard<std::mutex> jlk(job->jobMutex);
        job->summaryText = sumBuf;
    }

    TransferCompletionEvent ev;
    ev.jobId = job->id;
    ev.op = job->op;
    ev.finalState = finalSt;
    ev.targetDir = job->targetDir;
    for (const auto& s : job->sources) {
        std::wstring p = FtpClient::IsFtpPath(s) ? FtpClient::GetParentFtpUrl(s) : PathUtils::GetParentPath(s);
        if (!p.empty()) ev.sourceDirs.push_back(p);
    }
    ev.doneFiles = job->doneFiles.load();
    ev.skippedFiles = job->skippedFiles.load();
    ev.doneBytes = job->doneBytes.load();
    ev.elapsedMs = activeMs;
    ev.avgBytesPerSec = avgBps;
    ev.errorCount = errCount;

    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_completions.push_back(std::move(ev));
        TrimCompletionEvents(m_completions);
        job->threadFinished.store(true);
    }
    SaveQueueState();
    m_cv.notify_all();
    NotifyUi(true);
}

// ============================================================================
// High-Throughput Worker Execution (O(1) Move, Block Clone, Sparse Fast-Path, Persistent Pool & 4-Slot Ring)
// ============================================================================
void TransferManager::ExecuteJobWorker(std::shared_ptr<InternalJob> job) {
    if (job->op == TransferOp::Delete) {
        ExecuteDeleteJobWorker(job);
        return;
    }
    if (job->isFtpJob) {
        ExecuteFtpJobWorker(job);
        return;
    }

    job->startTick = GetTickCount64();
    job->lastSpeedTick = job->startTick;
    job->lastSpeedTickAtomic.store(job->startTick, std::memory_order_relaxed);
    job->lastCurFileTickAtomic.store(0, std::memory_order_relaxed);
    job->lastSpeedBytes = job->doneBytes.load();

    std::unordered_set<std::wstring> knownExistingNormDirs;
    std::unordered_set<std::wstring> newlyCreatedNormDirs;
    knownExistingNormDirs.reserve(256);
    newlyCreatedNormDirs.reserve(256);

    // Resolve accurate volume identities & capabilities while tracking whether targetDir itself is newly created
    EnsureDirHierarchyTrackFresh(job->targetDir, knownExistingNormDirs, newlyCreatedNormDirs);
    VolumeIdentity srcVi = QueryVolumeIdentity(job->sources[0]);
    VolumeIdentity dstVi = QueryVolumeIdentity(job->targetDir);
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (!srcVi.key.empty()) job->srcVolKey = srcVi.key;
        if (!dstVi.key.empty()) job->dstVolKey = dstVi.key;
        if (!srcVi.label.empty()) job->srcVolLabel = srcVi.label;
        if (!dstVi.label.empty()) job->dstVolLabel = dstVi.label;
    }
    job->sameVolume = (srcVi.valid && dstVi.valid && srcVi.serial != 0 && srcVi.serial == dstVi.serial && srcVi.key == dstVi.key);
    job->isBothRemoteNetwork = srcVi.isRemoteNetwork && dstVi.isRemoteNetwork;
    job->isRemoteOrRemovable = srcVi.isRemoteOrRemovable || dstVi.isRemoteOrRemovable;
    job->isRotationalHdd = srcVi.isRotationalHdd || dstVi.isRotationalHdd;
    job->supportsBlockClone = job->sameVolume && ((dstVi.fsFlags & FILE_SUPPORTS_BLOCK_REFCOUNTING) != 0);
    job->dstSupportsSparse = ((dstVi.fsFlags & FILE_SUPPORTS_SPARSE_FILES) != 0);
    job->sectorAlign = std::max(srcVi.sectorAlign, dstVi.sectorAlign);

    bool isRetryRun = false;
    {
        std::lock_guard<std::mutex> rlk(job->dstReserveMutex);
        job->reservedDstKeys.clear();
        std::lock_guard<std::mutex> jlk(job->jobMutex);
        isRetryRun = !job->fileItems.empty();
        if (isRetryRun) {
            for (const auto& fi : job->fileItems) {
                if ((fi.done || fi.dstReserved) && !fi.dstPath.empty()) {
                    job->reservedDstKeys.insert(PathUtils::NormalizePathKey(fi.dstPath));
                }
            }
        }
    }

    // ------------------------------------------------------------------------
    // PHASE 1: Zero-Lock / Zero-Extra-Stat Pre-Scan & O(1) Top-Level Atomic Same-Volume Move
    // ------------------------------------------------------------------------
    if (!isRetryRun && m_running.load()) {
        job->state.store(job->paused.load() ? TransferJobState::Paused : TransferJobState::Scanning);
        std::wstring dstRootNorm = PathUtils::NormalizePathKey(job->targetDir);

        std::vector<InternalFileItem> scannedFiles;
        std::vector<InternalDirItem>  scannedDirs;
        scannedFiles.reserve(512);
        scannedDirs.reserve(64);

        auto scanDirRecursive = [&](auto& self,
                                    const std::wstring& curSrcDir,
                                    const std::wstring& curDstDir,
                                    const FILETIME& dirCr,
                                    const FILETIME& dirAc,
                                    const FILETIME& dirWr,
                                    DWORD dirAttr) -> void {
            if (!WaitIfPausedOrCancelled(*job)) return;

            scannedDirs.push_back(InternalDirItem{
                curSrcDir, curDstDir, dirCr, dirAc, dirWr, dirAttr
            });

            std::wstring pattern = PathUtils::EnsureTrailingSlash(ToExtPath(curSrcDir)) + L"*";
            WIN32_FIND_DATAW ffd = {};
            HANDLE hFind = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &ffd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
            if (hFind == INVALID_HANDLE_VALUE) {
                DWORD err = GetLastError();
                if (err != ERROR_FILE_NOT_FOUND) {
                    std::lock_guard<std::mutex> jlk(job->jobMutex);
                    job->errors.push_back({ curSrcDir, curDstDir, err, FormatWin32Err(err), 0 });
                }
                return;
            }

            std::vector<WIN32_FIND_DATAW> subDirs;
            const std::wstring srcPrefix = PathUtils::EnsureTrailingSlash(curSrcDir);
            const std::wstring dstPrefix = PathUtils::EnsureTrailingSlash(curDstDir);
            uint64_t dirFiles = 0;
            uint64_t dirBytes = 0;

            do {
                if (ffd.cFileName[0] == L'.' &&
                    (ffd.cFileName[1] == L'\0' || (ffd.cFileName[1] == L'.' && ffd.cFileName[2] == L'\0'))) {
                    continue;
                }
                if (wcscmp(ffd.cFileName, L".explorer2_undo") == 0) continue;

                bool isDir = (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                bool isReparse = (ffd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
                if (isDir) {
                    if (isReparse) {
                        EnsureDirHierarchyTrackFresh(dstPrefix + ffd.cFileName, knownExistingNormDirs, newlyCreatedNormDirs);
                    } else {
                        subDirs.push_back(ffd);
                    }
                } else {
                    uint64_t fsz = (static_cast<uint64_t>(ffd.nFileSizeHigh) << 32) | ffd.nFileSizeLow;
                    InternalFileItem fi;
                    fi.srcPath = srcPrefix + ffd.cFileName;
                    fi.dstPath = dstPrefix + ffd.cFileName;
                    fi.size = fsz;
                    fi.creationTime = ffd.ftCreationTime;
                    fi.lastAccessTime = ffd.ftLastAccessTime;
                    fi.lastWriteTime = ffd.ftLastWriteTime;
                    fi.attributes = ffd.dwFileAttributes;
                    fi.isReparse = isReparse;
                    scannedFiles.push_back(std::move(fi));
                    ++dirFiles;
                    dirBytes += fsz;
                }
            } while (FindNextFileW(hFind, &ffd));
            FindClose(hFind);

            if (dirFiles > 0) {
                job->totalFiles.fetch_add(dirFiles, std::memory_order_relaxed);
                job->totalBytes.fetch_add(dirBytes, std::memory_order_relaxed);
            }

            for (const auto& sd : subDirs) {
                if (!WaitIfPausedOrCancelled(*job)) return;
                self(self, srcPrefix + sd.cFileName, dstPrefix + sd.cFileName,
                     sd.ftCreationTime, sd.ftLastAccessTime, sd.ftLastWriteTime, sd.dwFileAttributes);
            }
        };

        const std::wstring targetPrefix = PathUtils::EnsureTrailingSlash(job->targetDir);
        for (const auto& topSrc : job->sources) {
            if (!WaitIfPausedOrCancelled(*job)) break;

            std::wstring srcNorm = PathUtils::NormalizePathKey(topSrc);
            // Prevent copying/moving a folder into itself or its own descendant!
            if (dstRootNorm == srcNorm ||
                (dstRootNorm.size() > srcNorm.size() &&
                 dstRootNorm.compare(0, srcNorm.size(), srcNorm) == 0 &&
                 dstRootNorm[srcNorm.size()] == L'\\'))
            {
                std::lock_guard<std::mutex> jlk(job->jobMutex);
                job->errors.push_back({ topSrc, job->targetDir, ERROR_INVALID_PARAMETER,
                    L"Folder docelowy znajduje się wewnątrz folderu źródłowego (rekurencja zablokowana)", 0 });
                continue;
            }

            std::wstring leaf = PathUtils::GetFileName(topSrc);
            std::wstring topDst = targetPrefix + leaf;
            // Copying into the exact same folder -> automatically generate "name (2).ext"
            if (PathUtils::NormalizePathKey(topDst) == srcNorm && job->op == TransferOp::Copy) {
                topDst = NextFreeCollisionFilePath(topDst);
            }

            WIN32_FILE_ATTRIBUTE_DATA fad = {};
            if (!GetFileAttributesExW(ToExtPath(topSrc).c_str(), GetFileExInfoStandard, &fad)) {
                DWORD err = GetLastError();
                std::lock_guard<std::mutex> jlk(job->jobMutex);
                job->errors.push_back({ topSrc, topDst, err, FormatWin32Err(err), 0 });
                continue;
            }

            bool isDir = (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            bool isReparse = (fad.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;

            // O(1) Instant Metadata Move Fast-Path on Same Volume when destination does not exist!
            if (job->op == TransferOp::Move && job->sameVolume) {
                DWORD dstAttr = GetFileAttributesW(ToExtPath(topDst).c_str());
                if (dstAttr == INVALID_FILE_ATTRIBUTES) {
                    DWORD mvErr = 0;
                    if (MoveItemAtomicWithRetry(topSrc, topDst, isDir, false, mvErr)) {
                        uint64_t fsz = isDir ? 0ULL : ((static_cast<uint64_t>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow);
                        job->totalFiles.fetch_add(1, std::memory_order_relaxed);
                        job->doneFiles.fetch_add(1, std::memory_order_relaxed);
                        job->totalBytes.fetch_add(fsz, std::memory_order_relaxed);
                        job->doneBytes.fetch_add(fsz, std::memory_order_relaxed);
                        m_sessionFiles.fetch_add(1, std::memory_order_relaxed);
                        m_sessionBytes.fetch_add(fsz, std::memory_order_relaxed);
                        continue;
                    }
                }
            }

            if (isDir) {
                if (isReparse) {
                    EnsureDirHierarchyTrackFresh(topDst, knownExistingNormDirs, newlyCreatedNormDirs);
                } else {
                    scanDirRecursive(scanDirRecursive, topSrc, topDst,
                                     fad.ftCreationTime, fad.ftLastAccessTime, fad.ftLastWriteTime, fad.dwFileAttributes);
                }
            } else {
                uint64_t fsz = (static_cast<uint64_t>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
                InternalFileItem fi;
                fi.srcPath = topSrc;
                fi.dstPath = topDst;
                fi.size = fsz;
                fi.creationTime = fad.ftCreationTime;
                fi.lastAccessTime = fad.ftLastAccessTime;
                fi.lastWriteTime = fad.ftLastWriteTime;
                fi.attributes = fad.dwFileAttributes;
                fi.isReparse = isReparse;
                scannedFiles.push_back(std::move(fi));
                job->totalFiles.fetch_add(1, std::memory_order_relaxed);
                job->totalBytes.fetch_add(fsz, std::memory_order_relaxed);
            }
        }

        if (!scannedDirs.empty() || !scannedFiles.empty()) {
            std::lock_guard<std::mutex> jlk(job->jobMutex);
            job->dirItems = std::move(scannedDirs);
            job->fileItems = std::move(scannedFiles);
        }
    }

    // ------------------------------------------------------------------------
    // PHASE 1.5: Pre-Flight Disk Space Check, Upfront Directory Creation & Freshness Indexing
    // ------------------------------------------------------------------------
    if (!job->cancelled.load() && m_running.load()) {
        if (CheckPreFlightDiskSpaceOrAutoPause(*job)) {
            for (const auto& d : job->dirItems) {
                if (!WaitIfPausedOrCancelled(*job)) break;
                EnsureDirHierarchyTrackFresh(d.dstPath, knownExistingNormDirs, newlyCreatedNormDirs);
            }

            if (!isRetryRun && !job->fileItems.empty() && !job->cancelled.load() && m_running.load()) {
                std::unordered_set<std::wstring> seenDstNorm;
                seenDstNorm.reserve(job->fileItems.size() * 2 + 16);
                bool foundDupe = false;

                std::wstring cachedParentRaw;
                bool cachedParentEnsured = false;
                bool cachedParentFresh = false;

                for (auto& fi : job->fileItems) {
                    std::wstring parentRaw = PathUtils::GetParentPath(fi.dstPath);
                    if (parentRaw != cachedParentRaw) {
                        cachedParentRaw = parentRaw;
                        std::wstring pNorm = PathUtils::NormalizePathKey(parentRaw);
                        cachedParentEnsured = (knownExistingNormDirs.find(pNorm) != knownExistingNormDirs.end());
                        cachedParentFresh = (newlyCreatedNormDirs.find(pNorm) != newlyCreatedNormDirs.end());
                    }
                    fi.parentEnsured = cachedParentEnsured;
                    fi.dstKnownFresh = cachedParentFresh;

                    if (!seenDstNorm.insert(PathUtils::NormalizePathKey(fi.dstPath)).second) {
                        foundDupe = true;
                    }
                }
                job->hasIntraBatchDupes = foundDupe;
            } else if (isRetryRun) {
                job->hasIntraBatchDupes = true;
            }
        }
    }

    // ------------------------------------------------------------------------
    // PHASE 2: Transfer Execution (Persistent NVMe/HDD Worker Pool + Warm 4-Slot Async Rings)
    // ------------------------------------------------------------------------
    if (!job->cancelled.load() && m_running.load()) {
        job->state.store(job->paused.load() ? TransferJobState::Paused : TransferJobState::Running);

        const size_t itemCount = job->fileItems.size();
        if (itemCount > 0) {
            constexpr uint64_t kLargeFileThreshold = 8ULL * 1024ULL * 1024ULL;
            std::vector<size_t> largeIndices;
            std::vector<size_t> smallIndices;
            largeIndices.reserve(std::min<size_t>(itemCount, 256));
            smallIndices.reserve(itemCount);
            uint64_t maxFileSizeInJob = 0;

            for (size_t i = 0; i < itemCount; ++i) {
                if (job->fileItems[i].done) continue;
                uint64_t sz = job->fileItems[i].size;
                if (sz > maxFileSizeInJob) maxFileSizeInJob = sz;
                if (sz >= kLargeFileThreshold) largeIndices.push_back(i);
                else smallIndices.push_back(i);
            }

            const size_t pendingCount = largeIndices.size() + smallIndices.size();
            if (pendingCount > 0) {
                uint32_t cfgWorkers = job->cfg.workerThreads;
                uint32_t hwThreads = std::max(2u, std::thread::hardware_concurrency());
                uint32_t autoWorkers = (job->isRemoteOrRemovable || job->isRotationalHdd)
                    ? 1u
                    : std::clamp(hwThreads / 2, 4u, 8u);
                uint32_t numWorkers = (cfgWorkers == 0)
                    ? autoWorkers
                    : std::clamp(cfgWorkers, 1u, 16u);
                if (pendingCount < numWorkers) numWorkers = static_cast<uint32_t>(pendingCount);
                if (numWorkers == 0) numWorkers = 1;

                // Allow up to 2 concurrent 4-slot large streams when copying across two distinct local SSD/NVMe drives;
                // serialize large streams to 1 on same-volume / HDD / UNC while other workers steal small files in parallel!
                const uint32_t maxConcurrentLarge = (!job->sameVolume && !job->isRotationalHdd && !job->isRemoteOrRemovable) ? 2u : 1u;
                std::mutex largeSlotMtx;
                std::condition_variable largeSlotCv;
                std::atomic<uint32_t> activeLargeStreams{ 0 };
                std::atomic<size_t> nextLarge{ 0 };
                std::atomic<size_t> nextSmall{ 0 };
                std::atomic<bool> blockCloneFlag{ job->supportsBlockClone };

                // Auto-tune ring slot size: 1 MB x 4 slots for 8..128 MB files (4x faster pipeline fill!), 4 MB x 4 slots for >= 128 MB files
                size_t autoSlotBytes = (maxFileSizeInJob >= 128ULL * 1024ULL * 1024ULL)
                    ? (4ULL * 1024ULL * 1024ULL)
                    : (1024ULL * 1024ULL);
                size_t chunkBytes = (job->cfg.bufferSizeKB == 0)
                    ? autoSlotBytes
                    : (static_cast<size_t>(job->cfg.bufferSizeKB) * 1024ULL);
                chunkBytes = std::clamp<size_t>(chunkBytes, 256ULL * 1024ULL, 32ULL * 1024ULL * 1024ULL);

                const size_t smallWorkerBytes = std::clamp<size_t>(
                    (static_cast<size_t>(std::min<uint64_t>(maxFileSizeInJob, kLargeFileThreshold)) + 65535ULL) & ~65535ULL,
                    262144ULL, 4ULL * 1024ULL * 1024ULL);
                const size_t largeRingBytes = chunkBytes * WorkerAsyncRing::kNumSlots;

                auto tryAcquireLargeSlot = [&]() -> bool {
                    uint32_t cur = activeLargeStreams.load(std::memory_order_relaxed);
                    while (cur < maxConcurrentLarge) {
                        if (activeLargeStreams.compare_exchange_weak(cur, cur + 1, std::memory_order_acquire, std::memory_order_relaxed)) {
                            return true;
                        }
                    }
                    return false;
                };

                auto releaseLargeSlot = [&]() {
                    {
                        std::lock_guard<std::mutex> llk(largeSlotMtx);
                        activeLargeStreams.fetch_sub(1, std::memory_order_release);
                    }
                    largeSlotCv.notify_one();
                };

                struct ScopedLargeSlot {
                    bool held = false;
                    decltype(releaseLargeSlot)& rel;
                    ~ScopedLargeSlot() { if (held) rel(); }
                };

                auto workerFn = [&](WorkerAsyncRing& asyncRing) {
                    // Start with compact warm small-file buffer; upgrade lazily to 4-slot ring ONLY if this worker acquires a >= 8 MB stream!
                    ScopedVirtualBuffer workerBuf(smallWorkerBytes);

                    for (;;) {
                        if (!WaitIfPausedOrCancelled(*job)) break;

                        size_t idx = SIZE_MAX;
                        ScopedLargeSlot slotGuard{ false, releaseLargeSlot };

                        // 1. Opportunistically stream a >= 8 MB file if a large-stream slot is open
                        if (nextLarge.load(std::memory_order_relaxed) < largeIndices.size() && tryAcquireLargeSlot()) {
                            size_t li = nextLarge.fetch_add(1, std::memory_order_relaxed);
                            if (li < largeIndices.size()) {
                                idx = largeIndices[li];
                                slotGuard.held = true;
                            } else {
                                releaseLargeSlot();
                            }
                        }

                        // 2. Otherwise steal small files (< 8 MB) at full multi-core parallelism without waiting on large streams!
                        if (idx == SIZE_MAX) {
                            size_t si = nextSmall.fetch_add(1, std::memory_order_relaxed);
                            if (si < smallIndices.size()) {
                                idx = smallIndices[si];
                            }
                        }

                        // 3. Once all small files are drained, help finish any remaining >= 8 MB files
                        if (idx == SIZE_MAX) {
                            if (nextLarge.load(std::memory_order_relaxed) >= largeIndices.size()) break;
                            bool gotSlot = false;
                            {
                                std::unique_lock<std::mutex> llk(largeSlotMtx);
                                largeSlotCv.wait_for(llk, std::chrono::milliseconds(50), [&]() {
                                    if (tryAcquireLargeSlot()) { gotSlot = true; return true; }
                                    return nextLarge.load(std::memory_order_relaxed) >= largeIndices.size() ||
                                           job->cancelled.load(std::memory_order_relaxed) ||
                                           job->paused.load(std::memory_order_relaxed) ||
                                           !m_running.load(std::memory_order_relaxed);
                                });
                            }
                            if (!gotSlot) continue;
                            size_t li = nextLarge.fetch_add(1, std::memory_order_relaxed);
                            if (li < largeIndices.size()) {
                                idx = largeIndices[li];
                                slotGuard.held = true;
                            } else {
                                releaseLargeSlot();
                                break;
                            }
                        }

                        // Exclusive lock-free access to fileItems[idx] (immutable vector capacity during Phase 2)
                        InternalFileItem itemCopy = job->fileItems[idx];
                        if (itemCopy.done) continue;
                        if (slotGuard.held || itemCopy.size >= kLargeFileThreshold) {
                            workerBuf.EnsureCapacity(largeRingBytes);
                        }

                        ULONGLONG nowTick = GetTickCount64();
                        ULONGLONG prevCurTick = job->lastCurFileTickAtomic.load(std::memory_order_relaxed);
                        if (nowTick - prevCurTick >= 90 &&
                            job->lastCurFileTickAtomic.compare_exchange_strong(prevCurTick, nowTick, std::memory_order_relaxed))
                        {
                            std::lock_guard<std::mutex> jlk(job->jobMutex);
                            job->currentFile = PathUtils::GetFileName(itemCopy.srcPath);
                        }

                        TransferFileError fileErr;
                        bool ok = false;
                        uint32_t maxRetries = job->cfg.maxAutoRetries;
                        bool unbufFallbackUsed = false;
                        for (uint32_t attempt = 0; ; ++attempt) {
                            if (!WaitIfPausedOrCancelled(*job)) break;
                            if (attempt > 0) itemCopy.dstKnownFresh = false;
                            fileErr = {};
                            const bool allowUnbuffered = (attempt == 0 && !unbufFallbackUsed);
                            ok = CopyOrMoveSingleItem(*job, itemCopy, job->sameVolume, allowUnbuffered, blockCloneFlag,
                                                      workerBuf.Data(), workerBuf.Capacity(), &asyncRing, fileErr);
                            if (ok || job->cancelled.load() || !m_running.load()) break;
                            if (allowUnbuffered && job->cfg.unbufferedLargeFiles && fileErr.win32Error == ERROR_INVALID_PARAMETER && !unbufFallbackUsed) {
                                // Immediate zero-delay fallback to standard buffered I/O on exotic sector/filter driver volumes
                                unbufFallbackUsed = true;
                                if (attempt > 0) --attempt;
                                continue;
                            }
                            if (attempt < maxRetries && IsTransientIoError(fileErr.win32Error)) {
                                Sleep(60 * (attempt + 1));
                                continue;
                            }
                            break;
                        }

                        if (job->cancelled.load() || !m_running.load()) break;

                        job->fileItems[idx].done = ok;
                        job->fileItems[idx].failed = !ok;
                        job->fileItems[idx].dstReserved = itemCopy.dstReserved;
                        job->fileItems[idx].dstPath = std::move(itemCopy.dstPath);
                        if (!ok) {
                            std::lock_guard<std::mutex> jlk(job->jobMutex);
                            job->errors.push_back(std::move(fileErr));
                        } else {
                            job->doneFiles.fetch_add(1, std::memory_order_relaxed);
                            m_sessionFiles.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                };

                RunParallelWorkers(numWorkers, workerFn);
            }
        }
    }

    // ------------------------------------------------------------------------
    // PHASE 3: Directory Timestamps & Source Folder Cleanup (for Move)
    // ------------------------------------------------------------------------
    if (!job->cancelled.load() && m_running.load()) {
        std::vector<InternalDirItem> dirsCopy;
        bool hasErrorsOrSkips = false;
        {
            std::lock_guard<std::mutex> jlk(job->jobMutex);
            dirsCopy = job->dirItems;
            hasErrorsOrSkips = !job->errors.empty() || (job->skippedFiles.load() > 0);
        }

        // Apply original folder timestamps (bottom-up so parent writeTime isn't clobbered by child creation!)
        for (auto it = dirsCopy.rbegin(); it != dirsCopy.rend(); ++it) {
            HANDLE hDir = CreateFileW(
                ToExtPath(it->dstPath).c_str(),
                FILE_WRITE_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
            if (hDir != INVALID_HANDLE_VALUE) {
                ApplyHandleTimestampsAndAttrs(hDir, it->creationTime, it->lastAccessTime, it->lastWriteTime, it->attributes);
                CloseHandle(hDir);
            }
        }

        // For Move: remove empty source directories bottom-up (never deletes non-empty folders if any file was skipped or failed!)
        if (job->op == TransferOp::Move && !dirsCopy.empty()) {
            for (auto it = dirsCopy.rbegin(); it != dirsCopy.rend(); ++it) {
                std::wstring extSrcDir = ToExtPath(it->srcPath);
                DWORD sAttr = GetFileAttributesW(extSrcDir.c_str());
                if (sAttr != INVALID_FILE_ATTRIBUTES && (sAttr & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN))) {
                    if (!hasErrorsOrSkips) SetFileAttributesW(extSrcDir.c_str(), FILE_ATTRIBUTE_NORMAL);
                }
                for (int r = 0; r < 3; ++r) {
                    if (RemoveDirectoryW(extSrcDir.c_str())) break;
                    DWORD e = GetLastError();
                    if (e == ERROR_DIR_NOT_EMPTY || e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) break;
                    Sleep(25);
                }
            }
        }
    }

    // Graceful engine shutdown: keep unfinished job & .e2part files intact on disk for seamless recovery!
    if (!m_running.load() && !job->cancelled.load()) {
        std::lock_guard<std::mutex> lk(m_mutex);
        job->threadFinished.store(true);
        m_cv.notify_all();
        return;
    }

    // ------------------------------------------------------------------------
    // PHASE 4: Finalize State, Summary, Log & Completion Notification
    // ------------------------------------------------------------------------
    ULONGLONG endTick = GetTickCount64();
    uint64_t activeMs = 1ULL;
    size_t errCount = 0;
    TransferJobState finalSt = TransferJobState::Completed;
    {
        std::lock_guard<std::mutex> jlk(job->jobMutex);
        if (job->pauseBeginTick > 0 && endTick > job->pauseBeginTick) {
            job->pausedAccumMs += (endTick - job->pauseBeginTick);
            job->pauseBeginTick = 0;
        }
        uint64_t rawElapsed = (endTick > job->startTick) ? (endTick - job->startTick) : 1ULL;
        activeMs = (rawElapsed > job->pausedAccumMs) ? (rawElapsed - job->pausedAccumMs) : 1ULL;
        if (activeMs == 0) activeMs = 1;
        job->finalElapsedMs = activeMs;

        errCount = job->errors.size();
        job->liveSpeedBps = 0.0;
        job->currentFile.clear();

        if (job->cancelled.load()) {
            finalSt = TransferJobState::Cancelled;
        } else if (errCount > 0) {
            finalSt = TransferJobState::CompletedWithErrors;
        }
        job->state.store(finalSt);
    }

    double avgBps = (static_cast<double>(job->doneBytes.load()) * 1000.0) / static_cast<double>(activeMs);
    {
        wchar_t sumBuf[384] = {};
        if (finalSt == TransferJobState::Cancelled) {
            swprintf_s(sumBuf, L"Anulowano (%llu/%llu plików)",
                static_cast<unsigned long long>(job->doneFiles.load()),
                static_cast<unsigned long long>(job->totalFiles.load()));
        } else if (finalSt == TransferJobState::CompletedWithErrors) {
            swprintf_s(sumBuf, L"Zakończono z błędami (%zu): %llu/%llu plików (%s) • %s",
                errCount,
                static_cast<unsigned long long>(job->doneFiles.load()),
                static_cast<unsigned long long>(job->totalFiles.load()),
                FileEngine::FormatSizeDetailed(job->doneBytes.load()).c_str(),
                FormatSpeed(avgBps).c_str());
        } else {
            swprintf_s(sumBuf, L"OK: %llu plików (%s) w %.2f s • Śr. %s",
                static_cast<unsigned long long>(job->doneFiles.load()),
                FileEngine::FormatSizeDetailed(job->doneBytes.load()).c_str(),
                static_cast<double>(activeMs) / 1000.0,
                FormatSpeed(avgBps).c_str());
        }
        std::lock_guard<std::mutex> jlk(job->jobMutex);
        job->summaryText = sumBuf;
    }

    TransferCompletionEvent ev;
    ev.jobId = job->id;
    ev.op = job->op;
    ev.finalState = finalSt;
    ev.targetDir = job->targetDir;
    for (const auto& s : job->sources) {
        std::wstring p = PathUtils::GetParentPath(s);
        if (!p.empty()) ev.sourceDirs.push_back(p);
    }
    ev.doneFiles = job->doneFiles.load();
    ev.skippedFiles = job->skippedFiles.load();
    ev.doneBytes = job->doneBytes.load();
    ev.elapsedMs = activeMs;
    ev.avgBytesPerSec = avgBps;
    ev.errorCount = errCount;

    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_completions.push_back(std::move(ev));
        TrimCompletionEvents(m_completions);
        job->threadFinished.store(true);
    }
    SaveQueueState();
    m_cv.notify_all();
    NotifyUi(true);
}

// ============================================================================
// Delete Engine: own iterative deleter (permanent) and a checked hand-off to the Recycle Bin
// ============================================================================
namespace {

// FileDispositionInfoEx (Windows 10 1709+), declared here so the build does not depend on the SDK version
constexpr int   kFileDispositionInfoExClass = 21;
constexpr DWORD kDispositionDelete          = 0x00000001;
constexpr DWORD kDispositionPosixSemantics  = 0x00000002;
constexpr DWORD kDispositionIgnoreReadOnly  = 0x00000010;
struct DispositionInfoEx {
    DWORD Flags;
};

bool IsGoneError(DWORD err) {
    return err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND;
}

// Failures that usually pass on their own: an antivirus, the indexer, a thumbnailer or an editor holds
// the item for a moment, or a folder still shows a child whose deletion is pending.
bool IsTransientDeleteError(DWORD err) {
    return err == ERROR_SHARING_VIOLATION || err == ERROR_LOCK_VIOLATION || err == ERROR_DIR_NOT_EMPTY ||
           err == ERROR_ACCESS_DENIED || IsTransientIoError(err);
}

// True for junctions, folder symlinks and volume mount points: names that stand for something that lives
// elsewhere. Other reparse points on folders (cloud placeholders, ProjFS) are ordinary folders that own
// their content, so they are listed and emptied like any other folder.
bool IsLinkReparseTag(DWORD attrs, DWORD reparseTag) {
    return (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0 && IsReparseTagNameSurrogate(reparseTag);
}

// True when something on the way to `path` is a file rather than a folder, as with an entry that is
// shown inside an archive: such a path never named an item on disk.
bool PathRunsThroughAFile(const std::wstring& path) {
    std::wstring up = PathUtils::GetParentPath(path);
    while (!up.empty()) {
        const DWORD attrs = GetFileAttributesW(ToExtPath(up).c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES) return (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
        std::wstring next = PathUtils::GetParentPath(up);
        if (next == up) break;
        up = std::move(next);
    }
    return false;
}

// Removes one file or file symlink. Returns 0 when it is gone, else the Win32 error.
DWORD DeleteFileOnce(const std::wstring& extPath, DWORD attrs) {
    HANDLE h = CreateFileW(extPath.c_str(), DELETE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        // POSIX semantics: the name disappears at once even if another program still has the file open,
        // so the parent folder can be removed straight away. Read-only files need no extra step.
        DispositionInfoEx posix = { kDispositionDelete | kDispositionPosixSemantics | kDispositionIgnoreReadOnly };
        BOOL ok = SetFileInformationByHandle(h, static_cast<FILE_INFO_BY_HANDLE_CLASS>(kFileDispositionInfoExClass),
                                             &posix, sizeof(posix));
        if (!ok) {
            // Older Windows, FAT / exFAT and most network shares: classic delete-on-close
            FILE_DISPOSITION_INFO classic = { TRUE };
            ok = SetFileInformationByHandle(h, FileDispositionInfo, &classic, sizeof(classic));
        }
        CloseHandle(h);
        if (ok) return 0;
    } else if (IsGoneError(GetLastError())) {
        return 0;
    }

    // Path-based fallback. The read-only bit is cleared only for the attempt and put back if the attempt
    // fails, so a file that survives is left exactly as it was.
    if (attrs == INVALID_FILE_ATTRIBUTES || attrs == 0) {
        attrs = GetFileAttributesW(extPath.c_str());
    }
    bool clearedReadOnly = false;
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_READONLY)) {
        DWORD writable = attrs & ~static_cast<DWORD>(FILE_ATTRIBUTE_READONLY);
        if (writable == 0) writable = FILE_ATTRIBUTE_NORMAL;
        clearedReadOnly = SetFileAttributesW(extPath.c_str(), writable) != FALSE;
    }
    if (DeleteFileW(extPath.c_str())) return 0;
    const DWORD err = GetLastError();
    if (IsGoneError(err)) return 0;
    if (clearedReadOnly) SetFileAttributesW(extPath.c_str(), attrs);
    return err;
}

// Removes an empty folder, or a junction / folder symlink (the link only, never what it points to).
// Returns 0 when it is gone, else the Win32 error.
DWORD RemoveDirOnce(const std::wstring& extPath, DWORD attrs) {
    if (RemoveDirectoryW(extPath.c_str())) return 0;
    DWORD err = GetLastError();
    if (IsGoneError(err)) return 0;
    if (err == ERROR_ACCESS_DENIED && attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_READONLY)) {
        // A read-only folder refuses to go; lift the bit for one more try and restore it if that fails too
        if (SetFileAttributesW(extPath.c_str(), attrs & ~static_cast<DWORD>(FILE_ATTRIBUTE_READONLY))) {
            if (RemoveDirectoryW(extPath.c_str())) return 0;
            err = GetLastError();
            if (IsGoneError(err)) return 0;
            SetFileAttributesW(extPath.c_str(), attrs);
        }
    }
    return err;
}

// Runs one delete step, waiting and trying again while the failure looks temporary.
// Returns the final Win32 error (0 = gone).
template <typename Once>
DWORD DeleteWithRetry(Once&& once, uint32_t maxRetries, const std::atomic<bool>& cancelled) {
    for (uint32_t attempt = 0; ; ++attempt) {
        const DWORD err = once();
        if (err == 0) return 0;
        if (cancelled.load(std::memory_order_relaxed)) return err;
        // "Access denied" is normally final (permissions). It gets one short second chance only, because a
        // file that another program is deleting at this very moment reports the same code.
        const uint32_t allowed = (err == ERROR_ACCESS_DENIED) ? std::min<uint32_t>(maxRetries, 1u) : maxRetries;
        if (attempt >= allowed || !IsTransientDeleteError(err)) return err;
        Sleep(std::min<DWORD>(40u * (attempt + 1u) * (attempt + 1u), 400u));   // 40, 160, 360, 400 ms
    }
}

// Lists a folder tree for deletion without recursion, so the depth is limited by memory and not by the
// thread stack. Junctions and folder symlinks are reported as leaves and never entered. Folders are
// reported parents-first, which makes walking that list backwards a bottom-up order.
//   onDir(path, attrs)   onLeaf(path, size, attrs)   onError(folder, win32Error)   keepGoing() -> bool
template <typename OnDir, typename OnLeaf, typename OnError, typename KeepGoing>
void WalkTreeForDelete(const std::wstring& rootDir, DWORD rootAttrs,
                       OnDir&& onDir, OnLeaf&& onLeaf, OnError&& onError, KeepGoing&& keepGoing) {
    std::vector<std::pair<std::wstring, DWORD>> pending;
    pending.emplace_back(rootDir, rootAttrs);
    WIN32_FIND_DATAW ffd = {};

    while (!pending.empty()) {
        if (!keepGoing()) return;
        std::pair<std::wstring, DWORD> cur = std::move(pending.back());
        pending.pop_back();
        onDir(cur.first, cur.second);

        const std::wstring prefix = PathUtils::EnsureTrailingSlash(cur.first);
        const std::wstring pattern = ToExtPath(prefix) + L"*";
        HANDLE hFind = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &ffd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
        if (hFind == INVALID_HANDLE_VALUE) {
            const DWORD err = GetLastError();
            if (!IsGoneError(err)) onError(cur.first, err);
            continue;
        }

        DWORD endErr = ERROR_NO_MORE_FILES;
        for (;;) {
            const wchar_t* name = ffd.cFileName;
            const bool isDots = name[0] == L'.' && (name[1] == L'\0' || (name[1] == L'.' && name[2] == L'\0'));
            if (!isDots) {
                const DWORD attrs = ffd.dwFileAttributes;
                const bool isDir = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
                if (isDir && !IsLinkReparseTag(attrs, ffd.dwReserved0)) {
                    pending.emplace_back(prefix + name, attrs);
                } else {
                    const uint64_t size = isDir ? 0ULL : ((static_cast<uint64_t>(ffd.nFileSizeHigh) << 32) | ffd.nFileSizeLow);
                    onLeaf(prefix + name, size, attrs);
                }
            }
            if (!FindNextFileW(hFind, &ffd)) {
                endErr = GetLastError();
                break;
            }
        }
        FindClose(hFind);
        if (endErr != ERROR_NO_MORE_FILES) onError(cur.first, endErr);   // the listing stopped early
    }
}

struct TreeDeleteFailure {
    std::wstring path;
    DWORD error = 0;
};

// Marks every folder above `path` as still holding something, so that no time is spent trying to remove
// folders that cannot be empty and no second error is reported for them.
void BlockAncestors(std::unordered_set<std::wstring>& blocked, const std::wstring& path) {
    std::wstring up = PathUtils::NormalizePathKey(PathUtils::GetParentPath(path));
    while (!up.empty() && blocked.insert(up).second) {
        std::wstring next = PathUtils::GetParentPath(up);
        if (next == up) break;
        up = std::move(next);
    }
}

// Deletes everything below one folder, and the folder itself when removeRoot is set, before returning.
// Returns true when all of it is gone; a cancelled run returns false without adding failures.
bool DeleteTreeSync(const std::wstring& rootDir, DWORD rootAttrs, bool removeRoot, uint32_t maxRetries,
                    const std::atomic<bool>& cancelled, std::vector<TreeDeleteFailure>& failures) {
    struct Leaf {
        std::wstring path;
        DWORD attrs = 0;
    };
    std::vector<Leaf> leaves;
    std::vector<std::pair<std::wstring, DWORD>> dirs;
    std::unordered_set<std::wstring> blocked;
    bool allOk = true;

    WalkTreeForDelete(rootDir, rootAttrs,
        [&](const std::wstring& p, DWORD a) { dirs.emplace_back(p, a); },
        [&](std::wstring&& p, uint64_t, DWORD a) { leaves.push_back({ std::move(p), a }); },
        [&](const std::wstring& p, DWORD e) {
            failures.push_back({ p, e });
            blocked.insert(PathUtils::NormalizePathKey(p));
            BlockAncestors(blocked, p);
            allOk = false;
        },
        [&]() { return !cancelled.load(std::memory_order_relaxed); });

    for (const auto& leaf : leaves) {
        if (cancelled.load(std::memory_order_relaxed)) return false;
        const std::wstring ext = ToExtPath(leaf.path);
        const bool isDirLink = (leaf.attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
        const DWORD err = DeleteWithRetry(
            [&]() { return isDirLink ? RemoveDirOnce(ext, leaf.attrs) : DeleteFileOnce(ext, leaf.attrs); },
            maxRetries, cancelled);
        if (err != 0) {
            failures.push_back({ leaf.path, err });
            BlockAncestors(blocked, leaf.path);
            allOk = false;
        }
    }

    const size_t firstDir = removeRoot ? 0 : 1;
    for (size_t i = dirs.size(); i-- > firstDir; ) {
        if (cancelled.load(std::memory_order_relaxed)) return false;
        if (blocked.count(PathUtils::NormalizePathKey(dirs[i].first)) != 0) continue;   // its content is already reported
        const std::wstring ext = ToExtPath(dirs[i].first);
        const DWORD err = DeleteWithRetry([&]() { return RemoveDirOnce(ext, dirs[i].second); }, maxRetries, cancelled);
        if (err != 0) {
            failures.push_back({ dirs[i].first, err });
            BlockAncestors(blocked, dirs[i].first);
            allOk = false;
        }
    }
    return allOk && !cancelled.load(std::memory_order_relaxed);
}

std::wstring ShellItemPath(IShellItem* item) {
    std::wstring out;
    if (item) {
        PWSTR psz = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &psz)) && psz) {
            out = psz;
            CoTaskMemFree(psz);
        }
    }
    return out;
}

// Watches the shell while it moves items to the Recycle Bin. Its one important job: an item the bin cannot
// take (too large, or a volume without a bin) would be deleted for good by the shell, so that item is
// refused here and reported instead.
class RecycleSink final : public IFileOperationProgressSink {
public:
    explicit RecycleSink(const std::atomic<bool>& cancelled) : m_cancelled(cancelled) {}

    std::vector<std::wstring> notRecyclable;                    // normalized path keys
    std::vector<std::pair<std::wstring, HRESULT>> failed;       // normalized path key -> shell error

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IFileOperationProgressSink)) {
            *ppv = static_cast<IFileOperationProgressSink*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++m_refs; }
    IFACEMETHODIMP_(ULONG) Release() override {
        const ULONG left = --m_refs;
        if (left == 0) delete this;
        return left;
    }

    IFACEMETHODIMP PreDeleteItem(DWORD flags, IShellItem* item) override {
        if (m_cancelled.load(std::memory_order_relaxed)) return HRESULT_FROM_WIN32(ERROR_CANCELLED);
        if (!(flags & TSF_DELETE_RECYCLE_IF_POSSIBLE)) {
            // The shell is about to delete this item permanently. A failure here stops it, and everything
            // queued behind it in this batch; the caller puts those back in line.
            notRecyclable.push_back(PathUtils::NormalizePathKey(ShellItemPath(item)));
            return E_ABORT;
        }
        return S_OK;
    }
    IFACEMETHODIMP PostDeleteItem(DWORD, IShellItem* item, HRESULT hrDelete, IShellItem*) override {
        if (FAILED(hrDelete) && failed.size() < 4096) {
            failed.emplace_back(PathUtils::NormalizePathKey(ShellItemPath(item)), hrDelete);
        }
        return S_OK;
    }

    IFACEMETHODIMP StartOperations() override { return S_OK; }
    IFACEMETHODIMP FinishOperations(HRESULT) override { return S_OK; }
    IFACEMETHODIMP PreRenameItem(DWORD, IShellItem*, LPCWSTR) override { return S_OK; }
    IFACEMETHODIMP PostRenameItem(DWORD, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override { return S_OK; }
    IFACEMETHODIMP PreMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override { return S_OK; }
    IFACEMETHODIMP PostMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override { return S_OK; }
    IFACEMETHODIMP PreCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override { return S_OK; }
    IFACEMETHODIMP PostCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override { return S_OK; }
    IFACEMETHODIMP PreNewItem(DWORD, IShellItem*, LPCWSTR) override { return S_OK; }
    IFACEMETHODIMP PostNewItem(DWORD, IShellItem*, LPCWSTR, LPCWSTR, DWORD, HRESULT, IShellItem*) override { return S_OK; }
    IFACEMETHODIMP UpdateProgress(UINT, UINT) override { return S_OK; }
    IFACEMETHODIMP ResetTimer() override { return S_OK; }
    IFACEMETHODIMP PauseTimer() override { return S_OK; }
    IFACEMETHODIMP ResumeTimer() override { return S_OK; }

private:
    ~RecycleSink() = default;
    std::atomic<ULONG> m_refs{ 1 };
    const std::atomic<bool>& m_cancelled;
};

struct RecycleBatchResult {
    HRESULT setupError = S_OK;      // the shell operation could not even be created
    bool stoppedEarly = false;      // an item was refused or failed and the shell dropped the rest of the batch
    std::vector<std::wstring> notRecyclable;
    std::vector<std::pair<std::wstring, HRESULT>> failed;
};

// Hands one batch of top-level items to the shell's Recycle Bin. Runs on a thread that has COM initialised.
RecycleBatchResult RecycleBatchViaShell(const std::vector<std::wstring>& paths, const std::atomic<bool>& cancelled) {
    RecycleBatchResult result;

    IFileOperation* op = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(FileOperation), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&op));
    if (FAILED(hr) || !op) {
        result.setupError = FAILED(hr) ? hr : E_FAIL;
        return result;
    }
    // No dialogs of any kind. FOFX_RECYCLEONDELETE asks for the Recycle Bin; whether an item really goes
    // there is checked item by item in RecycleSink::PreDeleteItem.
    hr = op->SetOperationFlags(FOF_NO_UI | FOFX_RECYCLEONDELETE);
    if (FAILED(hr)) {
        op->Release();
        result.setupError = hr;
        return result;
    }

    RecycleSink* sink = new RecycleSink(cancelled);
    size_t queued = 0;
    for (const auto& p : paths) {
        IShellItem* item = nullptr;
        hr = SHCreateItemFromParsingName(p.c_str(), nullptr, IID_PPV_ARGS(&item));
        if (SUCCEEDED(hr) && item) {
            hr = op->DeleteItem(item, sink);
            item->Release();
        }
        if (FAILED(hr)) {
            result.failed.emplace_back(PathUtils::NormalizePathKey(p), hr);
        } else {
            ++queued;
        }
    }

    if (queued > 0) {
        hr = op->PerformOperations();
        BOOL anyAborted = FALSE;
        op->GetAnyOperationsAborted(&anyAborted);
        result.stoppedEarly = FAILED(hr) || anyAborted != FALSE;
    }
    result.notRecyclable = std::move(sink->notRecyclable);
    for (auto& f : sink->failed) result.failed.push_back(std::move(f));

    sink->Release();
    op->Release();
    return result;
}

std::wstring DescribeShellError(HRESULT hr, DWORD& outWin32) {
    if (HRESULT_FACILITY(hr) == FACILITY_WIN32) {
        outWin32 = static_cast<DWORD>(HRESULT_CODE(hr));
        return FormatWin32Err(outWin32);
    }
    outWin32 = ERROR_GEN_FAILURE;
    wchar_t buf[48] = {};
    swprintf_s(buf, L"HRESULT 0x%08X", static_cast<unsigned>(hr));
    return buf;
}

} // namespace

bool TransferManager::IsDeleteRefused(const std::wstring& rawPath) {
    if (FtpClient::IsFtpPath(rawPath)) return false;   // FTP items are removed one by one through the FTP client

    std::wstring p = PathUtils::TrimTrailingSlashes(rawPath);
    for (wchar_t& ch : p) {
        if (ch == L'/') ch = L'\\';
    }
    if (p.empty() || p == L"THIS_PC") return true;

    // Judge "\\?\C:\x" like "C:\x" and "\\?\UNC\server\share\x" like "\\server\share\x"
    if (p.rfind(L"\\\\?\\UNC\\", 0) == 0) p = L"\\\\" + p.substr(8);
    else if (p.rfind(L"\\\\?\\", 0) == 0) p = p.substr(4);
    p = PathUtils::TrimTrailingSlashes(p);

    size_t firstComponent = 0;
    if (p.size() >= 2 && p[1] == L':') {
        // "C:" and "C:\" are a whole drive; "C:name" is relative to that drive's current folder
        if (p.size() <= 3 || p[2] != L'\\') return true;
        firstComponent = 3;
    } else if (p.rfind(L"\\\\", 0) == 0) {
        // "\\server\share" is a whole share; something below it is needed
        const size_t serverEnd = p.find(L'\\', 2);
        if (serverEnd == std::wstring::npos || serverEnd == 2) return true;
        const size_t shareEnd = p.find(L'\\', serverEnd + 1);
        if (shareEnd == std::wstring::npos || shareEnd == serverEnd + 1 || shareEnd + 1 >= p.size()) return true;
        firstComponent = shareEnd + 1;
    } else {
        return true;   // relative, or a form this engine does not know
    }

    // "." and ".." could make a path mean something other than what it shows
    size_t pos = firstComponent;
    while (pos <= p.size()) {
        size_t end = p.find(L'\\', pos);
        if (end == std::wstring::npos) end = p.size();
        const std::wstring_view part(p.data() + pos, end - pos);
        if (part.empty() || part == L"." || part == L"..") return true;
        pos = end + 1;
    }
    return false;
}

bool TransferManager::DeleteTreeNow(const std::wstring& path, std::vector<TransferFileError>* outErrors) {
    static const std::atomic<bool> s_neverCancelled{ false };
    const uint32_t kRetries = 2;
    std::vector<TreeDeleteFailure> failures;
    bool ok = false;

    const std::wstring clean = PathUtils::TrimTrailingSlashes(path);
    if (IsDeleteRefused(clean) || FtpClient::IsFtpPath(clean)) {
        failures.push_back({ path, ERROR_INVALID_PARAMETER });
    } else {
        WIN32_FIND_DATAW fd = {};
        HANDLE hFind = FindFirstFileExW(ToExtPath(clean).c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
        if (hFind == INVALID_HANDLE_VALUE) {
            const DWORD err = GetLastError();
            if (IsGoneError(err)) ok = true;   // nothing there: the goal is already met
            else failures.push_back({ clean, err });
        } else {
            FindClose(hFind);
            const DWORD attrs = fd.dwFileAttributes;
            const bool isDir = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if (isDir && !IsLinkReparseTag(attrs, fd.dwReserved0)) {
                ok = DeleteTreeSync(clean, attrs, true, kRetries, s_neverCancelled, failures);
            } else {
                const std::wstring ext = ToExtPath(clean);
                const DWORD err = DeleteWithRetry(
                    [&]() { return isDir ? RemoveDirOnce(ext, attrs) : DeleteFileOnce(ext, attrs); },
                    kRetries, s_neverCancelled);
                ok = (err == 0);
                if (!ok) failures.push_back({ clean, err });
            }
        }
    }

    if (outErrors) {
        for (const auto& f : failures) {
            outErrors->push_back({ f.path, L"", f.error, FormatWin32Err(f.error), 0 });
        }
    }
    return ok;
}

// Moves the job's top-level items to the Recycle Bin in batches, checking after each batch what is really gone
void TransferManager::RunRecycleBinPhase(InternalJob& job) {
    auto addError = [&](const std::wstring& path, DWORD err, const std::wstring& message) {
        std::lock_guard<std::mutex> jlk(job.jobMutex);
        job.errors.push_back({ path, L"", err, message.empty() ? FormatWin32Err(err) : message, 0 });
    };
    auto markDone = [&](InternalFileItem& item) {
        item.done = true;
        item.failed = false;
        job.doneFiles.fetch_add(1, std::memory_order_relaxed);
        job.doneBytes.fetch_add(item.size, std::memory_order_relaxed);
    };
    auto stillExists = [](const std::wstring& path) {
        if (GetFileAttributesW(ToExtPath(path).c_str()) != INVALID_FILE_ATTRIBUTES) return true;
        return !IsGoneError(GetLastError());
    };

    std::deque<size_t> todo;
    for (size_t i = 0; i < job.fileItems.size(); ++i) {
        InternalFileItem& item = job.fileItems[i];
        if (item.done) continue;
        if (FtpClient::IsFtpPath(item.srcPath)) {
            // A server has no Recycle Bin: FTP items are removed directly, as they always were
            if (!WaitIfPausedOrCancelled(job)) return;
            std::wstring message;
            if (FtpClient::DeleteFtpItem(item.srcPath, false, &message) || FtpClient::DeleteFtpItem(item.srcPath, true, &message)) {
                markDone(item);
            } else {
                item.failed = true;
                addError(item.srcPath, ERROR_ACCESS_DENIED, message);
            }
            continue;
        }
        todo.push_back(i);
    }
    if (todo.empty()) return;

    const HRESULT coHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool coOwned = SUCCEEDED(coHr);
    const uint32_t testDelayMs = m_testDeleteDelayMs.load(std::memory_order_relaxed);

    static const wchar_t* kNotRecyclable =
        L"Kosz nie przyjmie tego elementu (za duży albo dysk bez Kosza). Nic nie usunięto: Shift+Delete usuwa trwale. / "
        L"The Recycle Bin cannot take this item (too large, or the drive has no bin). Nothing was deleted: "
        L"Shift+Delete deletes it permanently.";

    size_t batchSize = 32;
    while (!todo.empty()) {
        if (!WaitIfPausedOrCancelled(job)) break;
        if (testDelayMs > 0) {
            Sleep(testDelayMs);
            if (!WaitIfPausedOrCancelled(job)) break;
        }

        std::vector<size_t> batch;
        std::vector<std::wstring> batchPaths;
        while (!todo.empty() && batch.size() < batchSize) {
            batch.push_back(todo.front());
            batchPaths.push_back(job.fileItems[todo.front()].srcPath);
            todo.pop_front();
        }
        {
            std::lock_guard<std::mutex> jlk(job.jobMutex);
            job.currentFile = PathUtils::GetFileName(batchPaths.front());
        }

        RecycleBatchResult res = RecycleBatchViaShell(batchPaths, job.cancelled);

        std::vector<size_t> putBack;
        for (size_t idx : batch) {
            InternalFileItem& item = job.fileItems[idx];
            if (!stillExists(item.srcPath)) {
                markDone(item);
                continue;
            }
            const std::wstring key = PathUtils::NormalizePathKey(item.srcPath);
            if (FAILED(res.setupError)) {
                DWORD code = 0;
                std::wstring msg = DescribeShellError(res.setupError, code);
                item.failed = true;
                addError(item.srcPath, code, msg);
            } else if (std::find(res.notRecyclable.begin(), res.notRecyclable.end(), key) != res.notRecyclable.end()) {
                item.failed = true;
                addError(item.srcPath, ERROR_NOT_SUPPORTED, kNotRecyclable);
            } else if (auto it = std::find_if(res.failed.begin(), res.failed.end(),
                                              [&](const std::pair<std::wstring, HRESULT>& f) { return f.first == key; });
                       it != res.failed.end()) {
                DWORD code = 0;
                std::wstring msg = DescribeShellError(it->second, code);
                item.failed = true;
                addError(item.srcPath, code, msg);
            } else if (job.cancelled.load(std::memory_order_relaxed)) {
                // left in place, not an error
            } else if (res.stoppedEarly && batch.size() > 1) {
                putBack.push_back(idx);   // never reached: an earlier item ended the batch
            } else {
                item.failed = true;
                addError(item.srcPath, ERROR_GEN_FAILURE,
                         L"Element nadal istnieje po przeniesieniu do Kosza / The item is still there after the move to the Recycle Bin");
            }
        }

        if (!putBack.empty()) {
            // Go on one item at a time, so that one refused item cannot hold the others back again
            todo.insert(todo.begin(), putBack.begin(), putBack.end());
            batchSize = 1;
        } else if (batchSize < 32) {
            batchSize = std::min<size_t>(32, batchSize * 2);
        }
    }

    if (coOwned) CoUninitialize();
}

void TransferManager::ExecuteDeleteJobWorker(std::shared_ptr<InternalJob> job) {
    job->startTick = GetTickCount64();
    job->lastSpeedTick = job->startTick;
    job->lastSpeedTickAtomic.store(job->startTick, std::memory_order_relaxed);
    job->lastCurFileTickAtomic.store(0, std::memory_order_relaxed);

    if (!FtpClient::IsFtpPath(job->sources[0])) {
        VolumeIdentity vi = QueryVolumeIdentity(job->sources[0]);
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            if (!vi.key.empty()) job->srcVolKey = vi.key;
            if (!vi.label.empty()) job->srcVolLabel = vi.label;
        }
        job->isRemoteOrRemovable = vi.isRemoteOrRemovable;
        job->isRotationalHdd = vi.isRotationalHdd;
    }

    auto addError = [&](const std::wstring& path, DWORD err, const std::wstring& message = std::wstring()) {
        std::lock_guard<std::mutex> jlk(job->jobMutex);
        job->errors.push_back({ path, L"", err, message.empty() ? FormatWin32Err(err) : message, 0 });
    };
    auto showCurrent = [&](const std::wstring& path) {
        const ULONGLONG now = GetTickCount64();
        ULONGLONG prev = job->lastCurFileTickAtomic.load(std::memory_order_relaxed);
        if (now - prev >= 90 && job->lastCurFileTickAtomic.compare_exchange_strong(prev, now, std::memory_order_relaxed)) {
            std::lock_guard<std::mutex> jlk(job->jobMutex);
            job->currentFile = PathUtils::GetFileName(path);
        }
    };

    // ------------------------------------------------------------------------
    // PHASE 1: list what has to go. Runs for every requested item on the first run, and again on a retry
    // for the items a previous run could not list.
    // ------------------------------------------------------------------------
    std::vector<std::wstring> toScan;
    std::vector<std::wstring> unlistable;   // folders whose content could not be read: reported once, never emptied blindly
    {
        std::lock_guard<std::mutex> jlk(job->jobMutex);
        toScan.swap(job->pendingScanSources);
    }
    if (!toScan.empty() && m_running.load()) {
        job->state.store(job->paused.load() ? TransferJobState::Paused : TransferJobState::Scanning);

        std::vector<InternalFileItem> leaves;
        std::vector<InternalDirItem> dirs;
        std::vector<std::wstring> scanAgain;

        auto addLeaf = [&](std::wstring&& path, uint64_t size, DWORD attrs) {
            InternalFileItem fi;
            fi.srcPath = std::move(path);
            fi.size = size;
            fi.attributes = attrs;
            leaves.push_back(std::move(fi));
            job->totalFiles.fetch_add(1, std::memory_order_relaxed);
            job->totalBytes.fetch_add(size, std::memory_order_relaxed);
        };

        for (size_t si = 0; si < toScan.size(); ++si) {
            const std::wstring& top = toScan[si];
            if (!WaitIfPausedOrCancelled(*job)) {
                scanAgain.insert(scanAgain.end(), toScan.begin() + static_cast<ptrdiff_t>(si), toScan.end());
                break;
            }
            showCurrent(top);

            if (FtpClient::IsFtpPath(top)) {
                addLeaf(std::wstring(top), 0, 0);   // removed through the FTP client, one item at a time
                continue;
            }
            if (IsDeleteRefused(top)) {
                addError(top, ERROR_INVALID_PARAMETER,
                    L"Odmowa: nie usuwa się całego dysku, udziału sieciowego ani ścieżki względnej / "
                    L"Refused: a whole drive, a share root or a relative path is never deleted");
                scanAgain.push_back(top);
                continue;
            }

            WIN32_FIND_DATAW fd = {};
            HANDLE hFind = FindFirstFileExW(ToExtPath(top).c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
            if (hFind == INVALID_HANDLE_VALUE) {
                const DWORD err = GetLastError();
                if (!IsGoneError(err)) {   // already gone means there is nothing left to do for this item
                    addError(top, err);
                    scanAgain.push_back(top);
                } else if (PathRunsThroughAFile(top)) {
                    // Not "already gone": this never was an item on disk, so reporting it as deleted would be untrue
                    addError(top, ERROR_DIRECTORY,
                        L"To nie jest element na dysku (zawartość archiwum?). Nic nie usunięto. / "
                        L"Not an item on disk (the content of an archive?). Nothing was deleted.");
                    scanAgain.push_back(top);
                }
                continue;
            }
            FindClose(hFind);

            const DWORD attrs = fd.dwFileAttributes;
            const bool isDir = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if (job->recycle || !isDir || IsLinkReparseTag(attrs, fd.dwReserved0)) {
                // A file, a link, or (for the Recycle Bin) a whole item that the shell moves in one piece
                const uint64_t size = isDir ? 0ULL : ((static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow);
                addLeaf(std::wstring(top), size, attrs);
                continue;
            }

            WalkTreeForDelete(top, attrs,
                [&](const std::wstring& p, DWORD a) {
                    InternalDirItem di;
                    di.srcPath = p;
                    di.attributes = a;
                    dirs.push_back(std::move(di));
                    job->totalFiles.fetch_add(1, std::memory_order_relaxed);
                    showCurrent(p);
                },
                addLeaf,
                [&](const std::wstring& p, DWORD e) {
                    addError(p, e);
                    unlistable.push_back(p);
                },
                [&]() { return WaitIfPausedOrCancelled(*job); });
        }

        std::lock_guard<std::mutex> jlk(job->jobMutex);
        job->fileItems.insert(job->fileItems.end(),
                              std::make_move_iterator(leaves.begin()), std::make_move_iterator(leaves.end()));
        job->dirItems.insert(job->dirItems.end(),
                             std::make_move_iterator(dirs.begin()), std::make_move_iterator(dirs.end()));
        job->pendingScanSources = std::move(scanAgain);
    }

    // ------------------------------------------------------------------------
    // PHASE 2: remove files and links, then PHASE 3: remove the emptied folders bottom-up
    // ------------------------------------------------------------------------
    if (!job->cancelled.load() && m_running.load()) {
        job->state.store(job->paused.load() ? TransferJobState::Paused : TransferJobState::Running);

        if (job->recycle) {
            RunRecycleBinPhase(*job);
        } else {
            const uint32_t maxRetries = std::min<uint32_t>(job->cfg.maxAutoRetries, 8u);
            const uint32_t testDelayMs = m_testDeleteDelayMs.load(std::memory_order_relaxed);
            std::mutex blockedMtx;
            std::unordered_set<std::wstring> blockedDirs;   // folders that still hold something that could not be removed
            for (const auto& p : unlistable) {
                blockedDirs.insert(PathUtils::NormalizePathKey(p));
                BlockAncestors(blockedDirs, p);
            }

            const size_t leafCount = job->fileItems.size();
            size_t pendingLeaves = 0;
            bool anyFtp = false;
            for (const auto& fi : job->fileItems) {
                if (fi.done) continue;
                ++pendingLeaves;
                if (!anyFtp && FtpClient::IsFtpPath(fi.srcPath)) anyFtp = true;
            }

            std::atomic<size_t> nextLeaf{ 0 };
            std::atomic<uint32_t> stubbornItems{ 0 };

            auto leafWorker = [&]() {
                for (;;) {
                    if (!WaitIfPausedOrCancelled(*job)) break;
                    const size_t idx = nextLeaf.fetch_add(1, std::memory_order_relaxed);
                    if (idx >= leafCount) break;
                    InternalFileItem& item = job->fileItems[idx];   // each index is claimed by exactly one worker
                    if (item.done) continue;
                    showCurrent(item.srcPath);
                    if (testDelayMs > 0) {
                        Sleep(testDelayMs);
                        if (!WaitIfPausedOrCancelled(*job)) break;
                    }

                    DWORD err = 0;
                    std::wstring message;
                    if (FtpClient::IsFtpPath(item.srcPath)) {
                        if (!FtpClient::DeleteFtpItem(item.srcPath, false, &message) &&
                            !FtpClient::DeleteFtpItem(item.srcPath, true, &message)) {
                            err = ERROR_ACCESS_DENIED;
                        }
                    } else {
                        const std::wstring ext = ToExtPath(item.srcPath);
                        const bool isDirLink = (item.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                        // Once several items have stayed locked through all their retries, the tree is
                        // evidently in use: the rest gets a single retry so the job does not crawl
                        const uint32_t retries = (stubbornItems.load(std::memory_order_relaxed) >= 8)
                            ? std::min<uint32_t>(maxRetries, 1u) : maxRetries;
                        uint32_t attempts = 0;
                        err = DeleteWithRetry([&]() {
                            ++attempts;
                            return isDirLink ? RemoveDirOnce(ext, item.attributes) : DeleteFileOnce(ext, item.attributes);
                        }, retries, job->cancelled);
                        if (err != 0 && attempts > 1) stubbornItems.fetch_add(1, std::memory_order_relaxed);
                    }

                    if (err == 0) {
                        item.done = true;
                        item.failed = false;
                        job->doneFiles.fetch_add(1, std::memory_order_relaxed);
                        job->doneBytes.fetch_add(item.size, std::memory_order_relaxed);
                    } else if (!job->cancelled.load(std::memory_order_relaxed)) {
                        item.failed = true;
                        addError(item.srcPath, err, message);
                        std::lock_guard<std::mutex> blk(blockedMtx);
                        BlockAncestors(blockedDirs, item.srcPath);
                    }
                }
            };

            if (pendingLeaves > 0) {
                // Several threads help on SSDs once there is enough to do; a hard disk, a network share
                // and an FTP server get one stream of deletes
                uint32_t workers = 1;
                if (!anyFtp && !job->isRemoteOrRemovable && !job->isRotationalHdd && pendingLeaves >= 512) {
                    workers = std::clamp<uint32_t>(std::thread::hardware_concurrency() / 2, 2u, 4u);
                }
                if (job->cfg.workerThreads != 0) {
                    workers = std::min<uint32_t>(workers, std::clamp<uint32_t>(job->cfg.workerThreads, 1u, 4u));
                }
                if (testDelayMs > 0) workers = 1;

                std::vector<std::thread> helpers;
                for (uint32_t w = 1; w < workers; ++w) {
                    try {
                        helpers.emplace_back(leafWorker);
                    } catch (...) {
                        break;   // no more threads available: the ones that exist do the work
                    }
                }
                leafWorker();
                for (auto& t : helpers) t.join();
            }

            const uint32_t dirRetries = std::min<uint32_t>(maxRetries, 3u);
            for (size_t di = job->dirItems.size(); di-- > 0; ) {
                if (!WaitIfPausedOrCancelled(*job)) break;
                InternalDirItem& dir = job->dirItems[di];
                if (dir.done) continue;
                if (blockedDirs.count(PathUtils::NormalizePathKey(dir.srcPath)) != 0) continue;   // what is left inside is already reported
                showCurrent(dir.srcPath);
                if (testDelayMs > 0) {
                    Sleep(testDelayMs);
                    if (!WaitIfPausedOrCancelled(*job)) break;
                }

                const std::wstring ext = ToExtPath(dir.srcPath);
                DWORD err = DeleteWithRetry([&]() { return RemoveDirOnce(ext, dir.attributes); }, dirRetries, job->cancelled);
                if (err == ERROR_DIR_NOT_EMPTY && !job->cancelled.load()) {
                    // Something arrived in the folder after it was listed (a thumbnail cache, a log, a late
                    // download). Take one more pass over what is in there now, then try the folder again.
                    std::vector<TreeDeleteFailure> late;
                    if (DeleteTreeSync(dir.srcPath, dir.attributes, false, dirRetries, job->cancelled, late)) {
                        err = DeleteWithRetry([&]() { return RemoveDirOnce(ext, dir.attributes); }, dirRetries, job->cancelled);
                    } else if (!late.empty()) {
                        for (const auto& f : late) addError(f.path, f.error);
                        BlockAncestors(blockedDirs, dir.srcPath);
                        continue;   // the items inside explain why the folder stays
                    }
                }

                if (err == 0) {
                    dir.done = true;
                    job->doneFiles.fetch_add(1, std::memory_order_relaxed);
                } else if (!job->cancelled.load()) {
                    addError(dir.srcPath, err);
                    BlockAncestors(blockedDirs, dir.srcPath);
                }
            }
        }
    }

    // The engine is shutting down: the job simply ends here. A delete is never persisted or resumed.
    if (!m_running.load() && !job->cancelled.load()) {
        std::lock_guard<std::mutex> lk(m_mutex);
        job->threadFinished.store(true);
        m_cv.notify_all();
        return;
    }

    // ------------------------------------------------------------------------
    // PHASE 4: check the result against the disk, then state, summary and completion notification
    // ------------------------------------------------------------------------
    bool reportedErrors = false;
    {
        std::lock_guard<std::mutex> jlk(job->jobMutex);
        reportedErrors = !job->errors.empty();
    }
    if (!job->cancelled.load() && !reportedErrors) {
        // "Completed" must mean that every requested item is gone, so look once more
        for (const auto& top : job->sources) {
            if (FtpClient::IsFtpPath(top)) continue;
            const DWORD attrs = GetFileAttributesW(ToExtPath(top).c_str());
            const DWORD probeErr = (attrs == INVALID_FILE_ATTRIBUTES) ? GetLastError() : 0;
            if (attrs != INVALID_FILE_ATTRIBUTES || probeErr == ERROR_ACCESS_DENIED) {
                addError(top, ERROR_SHARING_VIOLATION,
                    L"Element nadal istnieje: usunięcie czeka, aż inny program zamknie plik / "
                    L"Still present: the deletion is waiting for another program to close the file");
                std::lock_guard<std::mutex> jlk(job->jobMutex);
                job->pendingScanSources.push_back(top);
            }
        }
    }

    const ULONGLONG endTick = GetTickCount64();
    uint64_t activeMs = 1ULL;
    size_t errCount = 0;
    TransferJobState finalSt = TransferJobState::Completed;
    std::vector<TransferFileError> firstErrors;
    {
        std::lock_guard<std::mutex> jlk(job->jobMutex);
        if (job->pauseBeginTick > 0 && endTick > job->pauseBeginTick) {
            job->pausedAccumMs += (endTick - job->pauseBeginTick);
            job->pauseBeginTick = 0;
        }
        const uint64_t rawElapsed = (endTick > job->startTick) ? (endTick - job->startTick) : 1ULL;
        activeMs = (rawElapsed > job->pausedAccumMs) ? (rawElapsed - job->pausedAccumMs) : 1ULL;
        if (activeMs == 0) activeMs = 1;
        job->finalElapsedMs = activeMs;

        errCount = job->errors.size();
        firstErrors.assign(job->errors.begin(), job->errors.begin() + static_cast<ptrdiff_t>(std::min<size_t>(errCount, 8)));
        job->liveSpeedBps = 0.0;
        job->currentFile.clear();

        if (job->cancelled.load()) {
            finalSt = TransferJobState::Cancelled;
        } else if (errCount > 0) {
            finalSt = TransferJobState::CompletedWithErrors;
        }
        job->state.store(finalSt);
    }

    {
        const unsigned long long doneItems = static_cast<unsigned long long>(job->doneFiles.load());
        const unsigned long long totalItems = static_cast<unsigned long long>(job->totalFiles.load());
        wchar_t sumBuf[384] = {};
        if (finalSt == TransferJobState::Cancelled) {
            swprintf_s(sumBuf, L"Anulowano usuwanie (%llu/%llu el.)", doneItems, totalItems);
        } else if (finalSt == TransferJobState::CompletedWithErrors) {
            swprintf_s(sumBuf, L"Usuwanie z błędami (%zu): %llu/%llu el.", errCount, doneItems, totalItems);
        } else if (job->recycle) {
            swprintf_s(sumBuf, L"OK: przeniesiono do Kosza %llu el. w %.2f s", doneItems, static_cast<double>(activeMs) / 1000.0);
        } else {
            swprintf_s(sumBuf, L"OK: usunięto trwale %llu el. (%s) w %.2f s", doneItems,
                FileEngine::FormatSizeDetailed(job->doneBytes.load()).c_str(), static_cast<double>(activeMs) / 1000.0);
        }
        std::lock_guard<std::mutex> jlk(job->jobMutex);
        job->summaryText = sumBuf;
    }

    TransferCompletionEvent ev;
    ev.jobId = job->id;
    ev.op = TransferOp::Delete;
    ev.recycle = job->recycle;
    ev.finalState = finalSt;
    ev.sources = job->sources;
    for (const auto& s : job->sources) {
        std::wstring parent = FtpClient::IsFtpPath(s) ? FtpClient::GetParentFtpUrl(s) : PathUtils::GetParentPath(s);
        if (!parent.empty() && std::find(ev.sourceDirs.begin(), ev.sourceDirs.end(), parent) == ev.sourceDirs.end()) {
            ev.sourceDirs.push_back(std::move(parent));
        }
    }
    ev.doneFiles = job->doneFiles.load();
    ev.doneBytes = job->doneBytes.load();
    ev.elapsedMs = activeMs;
    ev.errorCount = errCount;
    ev.firstErrors = std::move(firstErrors);

    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_completions.push_back(std::move(ev));
        TrimCompletionEvents(m_completions);
        job->threadFinished.store(true);
    }
    m_cv.notify_all();
    NotifyUi(true);
}

// ============================================================================
// Single File Transfer: Zero-Lock Fresh Fast-Path, Block Clone, SMB Offload, 4-Slot Async Ring & XXH64 Verify
// ============================================================================
bool TransferManager::CopyOrMoveSingleItem(
    InternalJob& job,
    InternalFileItem& item,
    bool sameVolume,
    bool allowUnbuffered,
    std::atomic<bool>& volumeSupportsBlockClone,
    uint8_t* workerBuf,
    size_t workerBufCap,
    WorkerAsyncRing* asyncRing,
    TransferFileError& outErr)
{
    if (!workerBuf || workerBufCap < 65536) {
        outErr = { item.srcPath, item.dstPath, ERROR_NOT_ENOUGH_MEMORY, L"Out of I/O buffer memory", 0 };
        return false;
    }

    if (!item.parentEnsured) {
        EnsureDirHierarchyExt(PathUtils::GetParentPath(item.dstPath));
        item.parentEnsured = true;
    }

    TransferConflictPolicy policy = static_cast<TransferConflictPolicy>(job.conflictPolicyAtomic.load(std::memory_order_relaxed));
    std::wstring finalDst = item.dstPath;
    std::wstring extFinalDst = ToExtPath(finalDst);

    // 1. Resolve destination collisions:
    //    - Ultra-fast zero-syscall/zero-lock path when parent directory was newly created by this job & no intra-batch dupes exist!
    //    - Otherwise stat destination OUTSIDE dstReserveMutex so parallel workers never serialize on NTFS/SMB metadata I/O!
    WIN32_FILE_ATTRIBUTE_DATA dstData = {};
    bool dstExists = false;
    uint64_t existingDstSize = 0ULL;

    if (item.dstKnownFresh && !job.hasIntraBatchDupes && !item.dstReserved) {
        item.dstReserved = true;
    } else if (!job.hasIntraBatchDupes) {
        dstExists = (GetFileAttributesExW(extFinalDst.c_str(), GetFileExInfoStandard, &dstData) != FALSE);
        existingDstSize = dstExists ? ((static_cast<uint64_t>(dstData.nFileSizeHigh) << 32) | dstData.nFileSizeLow) : 0ULL;

        if (!item.dstReserved) {
            if (dstExists) {
                if (policy == TransferConflictPolicy::SkipExisting) {
                    job.skippedFiles.fetch_add(1, std::memory_order_relaxed);
                    job.doneBytes.fetch_add(item.size, std::memory_order_relaxed);
                    return true;
                }
                if (policy == TransferConflictPolicy::OverwriteNewer) {
                    if (!(dstData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                        existingDstSize == item.size &&
                        AreFileTimesWithin2Sec(item.lastWriteTime, dstData.ftLastWriteTime))
                    {
                        job.skippedFiles.fetch_add(1, std::memory_order_relaxed);
                        job.doneBytes.fetch_add(item.size, std::memory_order_relaxed);
                        if (job.op == TransferOp::Move && PathUtils::NormalizePathKey(item.srcPath) != PathUtils::NormalizePathKey(finalDst)) {
                            std::wstring extSrc = ToExtPath(item.srcPath);
                            SetFileAttributesW(extSrc.c_str(), FILE_ATTRIBUTE_NORMAL);
                            DeleteFileW(extSrc.c_str());
                        }
                        return true;
                    }
                }
                if (policy == TransferConflictPolicy::RenameCollision) {
                    std::lock_guard<std::mutex> rlk(job.dstReserveMutex);
                    finalDst = NextFreeCollisionFilePath(finalDst, &job.reservedDstKeys);
                    job.reservedDstKeys.insert(PathUtils::NormalizePathKey(finalDst));
                    extFinalDst = ToExtPath(finalDst);
                    item.dstPath = finalDst;
                    dstExists = false;
                    existingDstSize = 0ULL;
                    item.dstKnownFresh = true;
                    job.renamedCollisions.fetch_add(1, std::memory_order_relaxed);
                } else if (policy == TransferConflictPolicy::OverwriteAll || policy == TransferConflictPolicy::OverwriteNewer) {
                    if (dstData.dwFileAttributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) {
                        SetFileAttributesW(extFinalDst.c_str(), FILE_ATTRIBUTE_NORMAL);
                    }
                }
            }
            item.dstReserved = true;
        } else if (dstExists && (policy == TransferConflictPolicy::OverwriteAll || policy == TransferConflictPolicy::OverwriteNewer)) {
            if (dstData.dwFileAttributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) {
                SetFileAttributesW(extFinalDst.c_str(), FILE_ATTRIBUTE_NORMAL);
            }
        }
    } else {
        // Intra-batch duplicate names or retry run: stat outside lock, then coordinate reservation in RAM
        if (!item.dstKnownFresh) {
            dstExists = (GetFileAttributesExW(extFinalDst.c_str(), GetFileExInfoStandard, &dstData) != FALSE);
            existingDstSize = dstExists ? ((static_cast<uint64_t>(dstData.nFileSizeHigh) << 32) | dstData.nFileSizeLow) : 0ULL;
        }
        std::lock_guard<std::mutex> rlk(job.dstReserveMutex);
        if (!item.dstReserved) {
            std::wstring normKey = PathUtils::NormalizePathKey(finalDst);
            const bool reservedByPeer = (job.reservedDstKeys.find(normKey) != job.reservedDstKeys.end());

            if (reservedByPeer) {
                if (policy == TransferConflictPolicy::SkipExisting) {
                    job.skippedFiles.fetch_add(1, std::memory_order_relaxed);
                    job.doneBytes.fetch_add(item.size, std::memory_order_relaxed);
                    return true;
                }
                finalDst = NextFreeCollisionFilePath(finalDst, &job.reservedDstKeys);
                extFinalDst = ToExtPath(finalDst);
                item.dstPath = finalDst;
                dstExists = false;
                existingDstSize = 0ULL;
                item.dstKnownFresh = true;
                job.renamedCollisions.fetch_add(1, std::memory_order_relaxed);
            } else if (dstExists) {
                if (policy == TransferConflictPolicy::SkipExisting) {
                    job.skippedFiles.fetch_add(1, std::memory_order_relaxed);
                    job.doneBytes.fetch_add(item.size, std::memory_order_relaxed);
                    return true;
                }
                if (policy == TransferConflictPolicy::OverwriteNewer) {
                    if (!(dstData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                        existingDstSize == item.size &&
                        AreFileTimesWithin2Sec(item.lastWriteTime, dstData.ftLastWriteTime))
                    {
                        job.skippedFiles.fetch_add(1, std::memory_order_relaxed);
                        job.doneBytes.fetch_add(item.size, std::memory_order_relaxed);
                        if (job.op == TransferOp::Move && PathUtils::NormalizePathKey(item.srcPath) != normKey) {
                            std::wstring extSrc = ToExtPath(item.srcPath);
                            SetFileAttributesW(extSrc.c_str(), FILE_ATTRIBUTE_NORMAL);
                            DeleteFileW(extSrc.c_str());
                        }
                        return true;
                    }
                }
                if (policy == TransferConflictPolicy::RenameCollision) {
                    finalDst = NextFreeCollisionFilePath(finalDst, &job.reservedDstKeys);
                    extFinalDst = ToExtPath(finalDst);
                    item.dstPath = finalDst;
                    dstExists = false;
                    existingDstSize = 0ULL;
                    item.dstKnownFresh = true;
                    job.renamedCollisions.fetch_add(1, std::memory_order_relaxed);
                } else if (policy == TransferConflictPolicy::OverwriteAll || policy == TransferConflictPolicy::OverwriteNewer) {
                    if (dstData.dwFileAttributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) {
                        SetFileAttributesW(extFinalDst.c_str(), FILE_ATTRIBUTE_NORMAL);
                    }
                }
            }

            job.reservedDstKeys.insert(PathUtils::NormalizePathKey(finalDst));
            item.dstReserved = true;
        } else if (dstExists && (policy == TransferConflictPolicy::OverwriteAll || policy == TransferConflictPolicy::OverwriteNewer)) {
            if (dstData.dwFileAttributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) {
                SetFileAttributesW(extFinalDst.c_str(), FILE_ATTRIBUTE_NORMAL);
            }
        }
    }

    // 2. Fast-path A: Same-Volume File Move via O(1) metadata MoveFileExW
    if (job.op == TransferOp::Move && sameVolume && !(policy == TransferConflictPolicy::ResumePartial && dstExists && existingDstSize < item.size)) {
        DWORD mvErr = 0;
        if (MoveItemAtomicWithRetry(item.srcPath, finalDst, false, !dstExists || policy != TransferConflictPolicy::RenameCollision, mvErr)) {
            RecordProgressBytes(job, item.size);
            if (job.cfg.verifyMode == TransferVerifyMode::SizeAndTime) {
                job.verifiedFiles.fetch_add(1, std::memory_order_relaxed);
            }
            return true;
        }
    }

    const bool computeHash = (job.cfg.verifyMode == TransferVerifyMode::XxHash64);

    // 3. Fast-path B: ReFS / Dev Drive Copy-on-Write Block Cloning (chunked in 1 GB extents for >4 GB support!)
    if (sameVolume && volumeSupportsBlockClone.load(std::memory_order_relaxed) && !computeHash &&
        item.size > 0 && !(policy == TransferConflictPolicy::ResumePartial && dstExists))
    {
        HANDLE hSrc = CreateFileW(ToExtPath(item.srcPath).c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hSrc != INVALID_HANDLE_VALUE) {
            HANDLE hDst = CreateFileW(extFinalDst.c_str(), GENERIC_READ | GENERIC_WRITE,
                0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hDst != INVALID_HANDLE_VALUE) {
                FILE_END_OF_FILE_INFO eof = {};
                eof.EndOfFile.QuadPart = static_cast<LONGLONG>(item.size);
                bool cloned = false;
                if (SetFileInformationByHandle(hDst, FileEndOfFileInfo, &eof, sizeof(eof))) {
                    const uint64_t totalAligned = (item.size + 65535ULL) & ~65535ULL;
                    constexpr uint64_t kMaxCloneSlice = 1ULL << 30; // 1 GB cluster-aligned slice
                    uint64_t curOff = 0;
                    cloned = true;
                    while (curOff < totalAligned) {
                        uint64_t sliceBytes = std::min<uint64_t>(kMaxCloneSlice, totalAligned - curOff);
                        MY_DUPLICATE_EXTENTS_DATA ded = {};
                        ded.FileHandle = hSrc;
                        ded.SourceFileOffset.QuadPart = static_cast<LONGLONG>(curOff);
                        ded.TargetFileOffset.QuadPart = static_cast<LONGLONG>(curOff);
                        ded.ByteCount.QuadPart = static_cast<LONGLONG>(sliceBytes);
                        DWORD retBytes = 0;
                        if (!DeviceIoControl(hDst, FSCTL_DUPLICATE_EXTENTS_TO_FILE, &ded, sizeof(ded), nullptr, 0, &retBytes, nullptr)) {
                            DWORD dErr = GetLastError();
                            if (dErr == ERROR_INVALID_FUNCTION || dErr == ERROR_NOT_SUPPORTED) {
                                volumeSupportsBlockClone.store(false, std::memory_order_relaxed);
                            }
                            cloned = false;
                            break;
                        }
                        curOff += sliceBytes;
                    }
                    if (cloned) {
                        SetFileInformationByHandle(hDst, FileEndOfFileInfo, &eof, sizeof(eof));
                        ApplyHandleTimestampsAndAttrs(hDst, item.creationTime, item.lastAccessTime, item.lastWriteTime, item.attributes);
                    }
                }
                CloseHandle(hDst);
                CloseHandle(hSrc);
                if (cloned) {
                    RecordProgressBytes(job, item.size);
                    if (job.cfg.verifyMode == TransferVerifyMode::SizeAndTime) {
                        job.verifiedFiles.fetch_add(1, std::memory_order_relaxed);
                    }
                    if (job.op == TransferOp::Move) {
                        std::wstring extSrc = ToExtPath(item.srcPath);
                        SetFileAttributesW(extSrc.c_str(), FILE_ATTRIBUTE_NORMAL);
                        DeleteFileW(extSrc.c_str());
                    }
                    return true;
                }
                DeleteFileW(extFinalDst.c_str());
            } else {
                CloseHandle(hSrc);
            }
        }
    }

    // 4. Determine write target (.e2part atomic staging for >= 1 MB or ResumePartial, direct MFT write for < 1 MB!)
    bool usePart = job.cfg.usePartFiles && (item.size >= (1024ULL * 1024ULL) || policy == TransferConflictPolicy::ResumePartial);
    std::wstring writePath = usePart ? (finalDst + L".e2part") : finalDst;
    std::wstring extWritePath = usePart ? ToExtPath(writePath) : extFinalDst;
    uint64_t resumeOffset = 0;

    if (usePart && !item.dstKnownFresh) {
        WIN32_FILE_ATTRIBUTE_DATA partData = {};
        if (GetFileAttributesExW(extWritePath.c_str(), GetFileExInfoStandard, &partData) &&
            !(partData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            uint64_t partSz = (static_cast<uint64_t>(partData.nFileSizeHigh) << 32) | partData.nFileSizeLow;
            // Verify .e2part belongs to the exact unmodified source file (we store source's lastWriteTime in .e2part's CreationTime!)
            if (partSz > 0 && partSz < item.size && AreFileTimesWithin2Sec(partData.ftCreationTime, item.lastWriteTime)) {
                resumeOffset = partSz;
            } else {
                SetFileAttributesW(extWritePath.c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(extWritePath.c_str());
            }
        }
    }
    if (resumeOffset == 0 && policy == TransferConflictPolicy::ResumePartial && dstExists &&
        !(dstData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && existingDstSize > 0 && existingDstSize < item.size)
    {
        // Resume directly onto partial destination file
        usePart = false;
        writePath = finalDst;
        extWritePath = extFinalDst;
        resumeOffset = existingDstSize;
    }

    std::wstring extSrc = ToExtPath(item.srcPath);
    uint32_t curSpeedLimit = m_speedLimitMBpsAtomic.load(std::memory_order_relaxed);
    if (curSpeedLimit == 0) curSpeedLimit = job.cfg.speedLimitMBps;

    // 4.5. Fast-path C: SMB3 / NAS Server-Side Copy Offload (FSCTL_SRV_COPYCHUNK + Compressed Traffic)
    //      Transfers multi-GB files between UNC/NAS shares at internal NAS speed (0 LAN round-trip) while preserving Pause/Cancel & live HUD speed!
    if (job.isBothRemoteNetwork && resumeOffset == 0 && !computeHash && curSpeedLimit == 0 && item.size >= 262144ULL) {
        struct SmbProgressCtx {
            TransferManager* mgr;
            InternalJob* job;
            uint64_t reported;
        } smbCtx{ this, &job, 0ULL };

        auto smbProgressCb = [](LARGE_INTEGER, LARGE_INTEGER totalTransferred, LARGE_INTEGER, LARGE_INTEGER,
                                DWORD, DWORD, HANDLE, HANDLE, LPVOID lpData) -> DWORD {
            auto* ctx = static_cast<SmbProgressCtx*>(lpData);
            uint64_t cur = (totalTransferred.QuadPart > 0) ? static_cast<uint64_t>(totalTransferred.QuadPart) : 0ULL;
            if (cur > ctx->reported) {
                ctx->mgr->RecordProgressBytes(*ctx->job, cur - ctx->reported);
                ctx->reported = cur;
            }
            return ctx->mgr->WaitIfPausedOrCancelled(*ctx->job) ? PROGRESS_CONTINUE : PROGRESS_CANCEL;
        };

        BOOL smbOk = CopyFileExW(extSrc.c_str(), extWritePath.c_str(), smbProgressCb, &smbCtx, nullptr, COPY_FILE_REQUEST_COMPRESSED_TRAFFIC);
        if (!smbOk && GetLastError() == ERROR_INVALID_PARAMETER && !job.cancelled.load(std::memory_order_relaxed)) {
            smbOk = CopyFileExW(extSrc.c_str(), extWritePath.c_str(), smbProgressCb, &smbCtx, nullptr, 0);
        }
        if (smbOk) {
            if (item.size > smbCtx.reported) {
                RecordProgressBytes(job, item.size - smbCtx.reported);
            }
            if (job.cfg.verifyMode == TransferVerifyMode::SizeAndTime) {
                WIN32_FILE_ATTRIBUTE_DATA vData = {};
                if (!GetFileAttributesExW(extWritePath.c_str(), GetFileExInfoStandard, &vData) ||
                    (((static_cast<uint64_t>(vData.nFileSizeHigh) << 32) | vData.nFileSizeLow) != item.size))
                {
                    job.doneBytes.fetch_sub(item.size, std::memory_order_relaxed);
                    outErr = { item.srcPath, finalDst, ERROR_INVALID_DATA, L"SMB copy size mismatch", 0 };
                    return false;
                }
                job.verifiedFiles.fetch_add(1, std::memory_order_relaxed);
            }
            if (usePart) {
                DWORD renErr = 0;
                if (!MoveItemAtomicWithRetry(writePath, finalDst, false, true, renErr)) {
                    job.doneBytes.fetch_sub(item.size, std::memory_order_relaxed);
                    outErr = { item.srcPath, finalDst, renErr, L"Atomic .e2part commit failed: " + FormatWin32Err(renErr), 0 };
                    return false;
                }
            }
            if (job.op == TransferOp::Move) {
                SetFileAttributesW(extSrc.c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(extSrc.c_str());
            }
            return true;
        }
        // Roll back partial SMB callback progress and fall back to manual streaming (or exit if cancelled)
        if (smbCtx.reported > 0) {
            job.doneBytes.fetch_sub(smbCtx.reported, std::memory_order_relaxed);
        }
        if (!WaitIfPausedOrCancelled(job)) {
            if (usePart) DeleteFileW(extWritePath.c_str());
            outErr = { item.srcPath, finalDst, ERROR_CANCELLED, FormatWin32Err(ERROR_CANCELLED), 0 };
            return false;
        }
        DeleteFileW(extWritePath.c_str());
    }

    // 5. Open Source & Destination handles (with optional NO_BUFFERING for huge >= 64 MB files)
    const size_t slotCap = (workerBufCap >= 262144ULL * WorkerAsyncRing::kNumSlots)
        ? ((workerBufCap / WorkerAsyncRing::kNumSlots) & ~65535ULL)
        : (workerBufCap & ~65535ULL);
    const size_t directCap = std::min<size_t>(workerBufCap & ~65535ULL, 8ULL * 1024ULL * 1024ULL);
    const uint64_t secMask = static_cast<uint64_t>(std::max<uint32_t>(job.sectorAlign, 4096u)) - 1ULL;
    const bool specialAttrs = (item.attributes & (FILE_ATTRIBUTE_COMPRESSED | FILE_ATTRIBUTE_ENCRYPTED | FILE_ATTRIBUTE_SPARSE_FILE)) != 0;
    const bool wantUnbuffered = allowUnbuffered && job.cfg.unbufferedLargeFiles && !specialAttrs &&
                                (item.size >= 64ULL * 1024ULL * 1024ULL) && (resumeOffset == 0) && (directCap > secMask);

    bool usingUnbufferedSrc = false;
    bool usingUnbufferedDst = false;

    HANDLE hSrc = INVALID_HANDLE_VALUE;
    if (wantUnbuffered) {
        hSrc = CreateFileW(extSrc.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (hSrc != INVALID_HANDLE_VALUE) usingUnbufferedSrc = true;
    }
    if (hSrc == INVALID_HANDLE_VALUE) {
        hSrc = CreateFileW(extSrc.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    }
    if (hSrc == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        outErr = { item.srcPath, finalDst, err, FormatWin32Err(err), 0 };
        return false;
    }

    HANDLE hDst = INVALID_HANDLE_VALUE;
    DWORD disp = (resumeOffset > 0) ? OPEN_EXISTING : CREATE_ALWAYS;
    if (wantUnbuffered && resumeOffset == 0) {
        hDst = CreateFileW(extWritePath.c_str(), GENERIC_WRITE,
            FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
            FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, nullptr);
        if (hDst != INVALID_HANDLE_VALUE) usingUnbufferedDst = true;
    }
    if (hDst == INVALID_HANDLE_VALUE) {
        hDst = CreateFileW(extWritePath.c_str(),
            (resumeOffset > 0 && computeHash) ? (GENERIC_READ | GENERIC_WRITE) : GENERIC_WRITE,
            FILE_SHARE_READ, nullptr, disp, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    }
    if (hDst == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        CloseHandle(hSrc);
        outErr = { item.srcPath, finalDst, err, FormatWin32Err(err), 0 };
        return false;
    }

    // Tag .e2part CreationTime with source's LastWriteTime so future crash/network-drop resumes validate source identity!
    if (usePart && resumeOffset == 0 && !usingUnbufferedDst) {
        FILE_BASIC_INFO tagInfo = {};
        tagInfo.CreationTime.LowPart  = item.lastWriteTime.dwLowDateTime;
        tagInfo.CreationTime.HighPart = static_cast<LONG>(item.lastWriteTime.dwHighDateTime);
        SetFileInformationByHandle(hDst, FileBasicInfo, &tagInfo, sizeof(tagInfo));
    }

    XxHash64Stream srcHasher;
    const bool wantSparseFastPath = ((item.attributes & FILE_ATTRIBUTE_SPARSE_FILE) != 0) &&
                                    job.dstSupportsSparse &&
                                    (resumeOffset == 0) &&
                                    (item.size >= 65536ULL) &&
                                    !usingUnbufferedSrc &&
                                    !usingUnbufferedDst;

    // 6. Seek & hash prefix if resuming from non-zero byte offset, or pre-allocate contiguous extents for >= 256 KB non-sparse files!
    if (resumeOffset > 0) {
        if (computeHash) {
            LARGE_INTEGER zero = {};
            SetFilePointerEx(hDst, zero, nullptr, FILE_BEGIN);
            uint64_t remPrefix = resumeOffset;
            while (remPrefix > 0) {
                DWORD toRead = static_cast<DWORD>(std::min<uint64_t>(remPrefix, directCap));
                DWORD got = 0;
                if (!ReadFile(hDst, workerBuf, toRead, &got, nullptr) || got == 0) {
                    CloseHandle(hDst);
                    CloseHandle(hSrc);
                    outErr = { item.srcPath, finalDst, GetLastError(), L"Failed reading partial resume prefix", 0 };
                    return false;
                }
                srcHasher.Update(workerBuf, got);
                remPrefix -= got;
            }
        }
        LARGE_INTEGER seekPos = {};
        seekPos.QuadPart = static_cast<LONGLONG>(resumeOffset);
        if (!SetFilePointerEx(hSrc, seekPos, nullptr, FILE_BEGIN) ||
            !SetFilePointerEx(hDst, seekPos, nullptr, FILE_BEGIN))
        {
            DWORD err = GetLastError();
            CloseHandle(hDst);
            CloseHandle(hSrc);
            outErr = { item.srcPath, finalDst, err, FormatWin32Err(err), 0 };
            return false;
        }
        job.doneBytes.fetch_add(resumeOffset, std::memory_order_relaxed);
        job.resumedFiles.fetch_add(1, std::memory_order_relaxed);
    } else if (item.size >= 262144ULL && !usingUnbufferedDst && !wantSparseFastPath) {
        // Pre-allocate contiguous disk extents WITHOUT extending logical EOF (preserves true written byte length on interruption!)
        FILE_ALLOCATION_INFO allocInfo = {};
        allocInfo.AllocationSize.QuadPart = static_cast<LONGLONG>(item.size);
        SetFileInformationByHandle(hDst, FileAllocationInfo, &allocInfo, sizeof(allocInfo));
    }

    // 7. Stream Data:
    //    - Sparse files (VHDX/WSL/Docker/Torrent): FSCTL_SET_SPARSE + FSCTL_QUERY_ALLOCATED_RANGES (skips unallocated zero holes!)
    //    - Files < 8 MB: Direct zero-overhead single-buffer loop
    //    - Files >= 8 MB: 4-Slot Persistent Producer-Consumer Ring (up to 3x4 MB = 12 MB async read-ahead + parallel XXH64, zero thread spawn!)
    uint64_t bytesAddedThisAttempt = resumeOffset;
    bool ioOk = true;
    DWORD ioErr = 0;

    size_t sliceCap = slotCap;
    if (curSpeedLimit > 0 && sliceCap > 1024ULL * 1024ULL) {
        sliceCap = 1024ULL * 1024ULL; // Finer 1 MB pacing slices when bandwidth limit is active
    }

    const uint64_t remainingToCopy = (item.size > resumeOffset) ? (item.size - resumeOffset) : 0ULL;
    bool handledBySparseFastPath = false;

    if (remainingToCopy > 0 && wantSparseFastPath) {
        DWORD spRet = 0;
        if (DeviceIoControl(hDst, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &spRet, nullptr)) {
            FILE_ALLOCATED_RANGE_BUFFER queryRange = {};
            queryRange.FileOffset.QuadPart = 0;
            queryRange.Length.QuadPart = static_cast<LONGLONG>(item.size);
            FILE_ALLOCATED_RANGE_BUFFER ranges[128];
            uint64_t logicalCursor = 0;
            bool querySupported = true;
            std::vector<uint8_t> zeroChunk(computeHash ? 65536ULL : 0ULL, 0);
            const size_t maxSparseChunk = (curSpeedLimit > 0 && directCap > 1024ULL * 1024ULL) ? (1024ULL * 1024ULL) : directCap;

            for (;;) {
                DWORD retBytes = 0;
                BOOL qOk = DeviceIoControl(
                    hSrc, FSCTL_QUERY_ALLOCATED_RANGES,
                    &queryRange, sizeof(queryRange),
                    ranges, sizeof(ranges),
                    &retBytes, nullptr);
                DWORD qErr = qOk ? ERROR_SUCCESS : GetLastError();
                if (!qOk && qErr != ERROR_MORE_DATA) {
                    if (logicalCursor == 0) {
                        querySupported = false; // Fall back to normal dense copy
                    } else {
                        ioOk = false;
                        ioErr = qErr;
                    }
                    break;
                }

                const size_t count = retBytes / sizeof(FILE_ALLOCATED_RANGE_BUFFER);
                if (count == 0) break;

                for (size_t ri = 0; ri < count; ++ri) {
                    uint64_t rStart = static_cast<uint64_t>(ranges[ri].FileOffset.QuadPart);
                    uint64_t rLen   = static_cast<uint64_t>(ranges[ri].Length.QuadPart);
                    if (rStart >= item.size || rLen == 0) continue;
                    if (rStart + rLen > item.size) rLen = item.size - rStart;
                    if (rStart < logicalCursor) {
                        uint64_t overlap = logicalCursor - rStart;
                        if (overlap >= rLen) continue;
                        rStart += overlap;
                        rLen -= overlap;
                    }

                    // Skip unallocated sparse hole [logicalCursor, rStart) in O(1) disk I/O!
                    if (rStart > logicalCursor) {
                        if (!WaitIfPausedOrCancelled(job)) { ioOk = false; ioErr = ERROR_CANCELLED; break; }
                        uint64_t holeBytes = rStart - logicalCursor;
                        if (computeHash) {
                            uint64_t hRem = holeBytes;
                            while (hRem > 0) {
                                size_t step = static_cast<size_t>(std::min<uint64_t>(hRem, zeroChunk.size()));
                                srcHasher.Update(zeroChunk.data(), step);
                                hRem -= step;
                            }
                        }
                        bytesAddedThisAttempt += holeBytes;
                        RecordProgressBytes(job, holeBytes);
                        logicalCursor = rStart;
                    }

                    LARGE_INTEGER seekPos = {};
                    seekPos.QuadPart = static_cast<LONGLONG>(rStart);
                    if (!SetFilePointerEx(hSrc, seekPos, nullptr, FILE_BEGIN) ||
                        !SetFilePointerEx(hDst, seekPos, nullptr, FILE_BEGIN))
                    {
                        ioErr = GetLastError();
                        ioOk = false;
                        break;
                    }

                    uint64_t left = rLen;
                    while (left > 0) {
                        if (!WaitIfPausedOrCancelled(job)) { ioOk = false; ioErr = ERROR_CANCELLED; break; }
                        DWORD toRead = static_cast<DWORD>(std::min<uint64_t>(maxSparseChunk, left));
                        DWORD got = 0;
                        if (!ReadFile(hSrc, workerBuf, toRead, &got, nullptr) || got == 0) {
                            ioErr = GetLastError();
                            if (ioErr == 0) ioErr = ERROR_HANDLE_EOF;
                            ioOk = false;
                            break;
                        }
                        DWORD validBytes = static_cast<DWORD>(std::min<uint64_t>(got, left));
                        if (computeHash) srcHasher.Update(workerBuf, validBytes);
                        DWORD written = 0;
                        if (!WriteFile(hDst, workerBuf, validBytes, &written, nullptr) || written != validBytes) {
                            ioErr = GetLastError();
                            ioOk = false;
                            break;
                        }
                        left -= validBytes;
                        bytesAddedThisAttempt += validBytes;
                        RecordProgressBytes(job, validBytes);
                        ThrottleBytes(validBytes, job);
                    }
                    if (!ioOk) break;
                    logicalCursor = rStart + rLen;
                }

                if (!ioOk || qOk) break;

                // ERROR_MORE_DATA: advance query window past last returned extent
                uint64_t nextOff = static_cast<uint64_t>(ranges[count - 1].FileOffset.QuadPart) +
                                   static_cast<uint64_t>(ranges[count - 1].Length.QuadPart);
                if (nextOff >= item.size) break;
                queryRange.FileOffset.QuadPart = static_cast<LONGLONG>(nextOff);
                queryRange.Length.QuadPart = static_cast<LONGLONG>(item.size - nextOff);
            }

            if (querySupported) {
                handledBySparseFastPath = true;
                if (ioOk && logicalCursor < item.size) {
                    if (!WaitIfPausedOrCancelled(job)) {
                        ioOk = false;
                        ioErr = ERROR_CANCELLED;
                    } else {
                        uint64_t tailHole = item.size - logicalCursor;
                        if (computeHash) {
                            uint64_t hRem = tailHole;
                            while (hRem > 0) {
                                size_t step = static_cast<size_t>(std::min<uint64_t>(hRem, zeroChunk.size()));
                                srcHasher.Update(zeroChunk.data(), step);
                                hRem -= step;
                            }
                        }
                        bytesAddedThisAttempt += tailHole;
                        RecordProgressBytes(job, tailHole);
                    }
                }
                if (ioOk) {
                    // Explicitly commit logical EOF so trailing sparse holes remain unallocated on disk!
                    FILE_END_OF_FILE_INFO eof = {};
                    eof.EndOfFile.QuadPart = static_cast<LONGLONG>(item.size);
                    if (!SetFileInformationByHandle(hDst, FileEndOfFileInfo, &eof, sizeof(eof))) {
                        ioErr = GetLastError();
                        ioOk = false;
                    }
                }
            } else {
                LARGE_INTEGER zero = {};
                SetFilePointerEx(hSrc, zero, nullptr, FILE_BEGIN);
                SetFilePointerEx(hDst, zero, nullptr, FILE_BEGIN);
            }
        }
    }

    if (remainingToCopy == 0 || handledBySparseFastPath) {
        // Empty file, already 100% present in .e2part, or completed via Sparse Fast-Path!
    } else if (remainingToCopy >= 8ULL * 1024ULL * 1024ULL && !usingUnbufferedSrc && !usingUnbufferedDst &&
               asyncRing != nullptr && slotCap >= 262144ULL && workerBufCap >= slotCap * WorkerAsyncRing::kNumSlots)
    {
        ioOk = asyncRing->StreamFile(*this, job, hSrc, hDst, workerBuf, slotCap, sliceCap,
                                     remainingToCopy, computeHash, srcHasher, bytesAddedThisAttempt, ioErr);
    } else {
        // Direct single-buffer sequential loop (ultra-fast for < 8 MB files and sector-aligned NO_BUFFERING)
        const size_t maxDirectChunk = (curSpeedLimit > 0 && directCap > 1024ULL * 1024ULL) ? (1024ULL * 1024ULL) : directCap;
        uint64_t left = remainingToCopy;
        while (left > 0) {
            if (!WaitIfPausedOrCancelled(job)) { ioOk = false; ioErr = ERROR_CANCELLED; break; }

            DWORD toRead = static_cast<DWORD>(std::min<uint64_t>(usingUnbufferedSrc ? directCap : maxDirectChunk,
                                                                 usingUnbufferedSrc ? ((left + secMask) & ~secMask) : left));
            DWORD got = 0;
            if (!ReadFile(hSrc, workerBuf, toRead, &got, nullptr)) {
                ioErr = GetLastError();
                ioOk = false;
                break;
            }
            if (got == 0) break;

            DWORD validBytes = static_cast<DWORD>(std::min<uint64_t>(got, left));
            if (computeHash) srcHasher.Update(workerBuf, validBytes);

            const DWORD secMask32 = static_cast<DWORD>(secMask);
            DWORD toWrite = usingUnbufferedDst ? ((validBytes + secMask32) & ~secMask32) : validBytes;
            if (usingUnbufferedDst && toWrite > validBytes) {
                std::memset(workerBuf + validBytes, 0, toWrite - validBytes);
            }

            DWORD written = 0;
            if (!WriteFile(hDst, workerBuf, toWrite, &written, nullptr) || written != toWrite) {
                ioErr = GetLastError();
                ioOk = false;
                break;
            }

            left -= validBytes;
            bytesAddedThisAttempt += validBytes;
            RecordProgressBytes(job, validBytes);
            ThrottleBytes(validBytes, job);
        }
    }

    CloseHandle(hSrc);

    if (!ioOk) {
        if (!usingUnbufferedDst) {
            if (!job.cancelled.load()) {
                FILE_END_OF_FILE_INFO truncEof = {};
                truncEof.EndOfFile.QuadPart = static_cast<LONGLONG>(bytesAddedThisAttempt);
                SetFileInformationByHandle(hDst, FileEndOfFileInfo, &truncEof, sizeof(truncEof));
            }
            FlushFileBuffers(hDst);
        }
        CloseHandle(hDst);

        // Roll back this attempt's progress counter so retry starts from exact confirmed on-disk size
        job.doneBytes.fetch_sub(bytesAddedThisAttempt, std::memory_order_relaxed);

        if (job.cancelled.load() || (usingUnbufferedDst && bytesAddedThisAttempt == 0)) {
            if (usePart) {
                SetFileAttributesW(extWritePath.c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(extWritePath.c_str());
            }
        } else if (usingUnbufferedDst && usePart && bytesAddedThisAttempt > 0) {
            // Preserve valid unbuffered prefix for instant buffered byte-offset resume!
            HANDLE hFix = CreateFileW(extWritePath.c_str(), FILE_WRITE_DATA | FILE_WRITE_ATTRIBUTES,
                FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFix != INVALID_HANDLE_VALUE) {
                FILE_END_OF_FILE_INFO truncEof = {};
                truncEof.EndOfFile.QuadPart = static_cast<LONGLONG>(bytesAddedThisAttempt);
                SetFileInformationByHandle(hFix, FileEndOfFileInfo, &truncEof, sizeof(truncEof));
                FILE_BASIC_INFO tagInfo = {};
                tagInfo.CreationTime.LowPart  = item.lastWriteTime.dwLowDateTime;
                tagInfo.CreationTime.HighPart = static_cast<LONG>(item.lastWriteTime.dwHighDateTime);
                SetFileInformationByHandle(hFix, FileBasicInfo, &tagInfo, sizeof(tagInfo));
                CloseHandle(hFix);
            }
        }
        outErr = { item.srcPath, finalDst, ioErr, FormatWin32Err(ioErr), job.cancelled.load() ? 0ULL : bytesAddedThisAttempt };
        return false;
    }

    // 8. Finalize EOF (only needed if NO_BUFFERING padded last sector or size mismatch) & apply original timestamps + attributes!
    if (usingUnbufferedDst) {
        CloseHandle(hDst);
        HANDLE hFix = CreateFileW(extWritePath.c_str(), FILE_WRITE_DATA | FILE_WRITE_ATTRIBUTES,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFix != INVALID_HANDLE_VALUE) {
            FILE_END_OF_FILE_INFO eof = {};
            eof.EndOfFile.QuadPart = static_cast<LONGLONG>(item.size);
            SetFileInformationByHandle(hFix, FileEndOfFileInfo, &eof, sizeof(eof));
            ApplyHandleTimestampsAndAttrs(hFix, item.creationTime, item.lastAccessTime, item.lastWriteTime, item.attributes);
            CloseHandle(hFix);
        }
    } else {
        if (bytesAddedThisAttempt != item.size) {
            FILE_END_OF_FILE_INFO eof = {};
            eof.EndOfFile.QuadPart = static_cast<LONGLONG>(item.size);
            SetFileInformationByHandle(hDst, FileEndOfFileInfo, &eof, sizeof(eof));
        }
        ApplyHandleTimestampsAndAttrs(hDst, item.creationTime, item.lastAccessTime, item.lastWriteTime, item.attributes);
        CloseHandle(hDst);
    }

    // 9. Optional Post-Copy Integrity Verification (Size+Time or full XXH64 disk re-read reusing workerBuf!)
    if (job.cfg.verifyMode != TransferVerifyMode::None) {
        WIN32_FILE_ATTRIBUTE_DATA vData = {};
        if (!GetFileAttributesExW(extWritePath.c_str(), GetFileExInfoStandard, &vData)) {
            DWORD err = GetLastError();
            job.doneBytes.fetch_sub(bytesAddedThisAttempt, std::memory_order_relaxed);
            outErr = { item.srcPath, finalDst, err, L"Verification stat failed: " + FormatWin32Err(err), resumeOffset };
            return false;
        }
        uint64_t diskSz = (static_cast<uint64_t>(vData.nFileSizeHigh) << 32) | vData.nFileSizeLow;
        if (diskSz != item.size) {
            job.doneBytes.fetch_sub(bytesAddedThisAttempt, std::memory_order_relaxed);
            outErr = { item.srcPath, finalDst, ERROR_INVALID_DATA, L"Size mismatch after write", resumeOffset };
            return false;
        }
        if (computeHash) {
            TransferJobState prevSt = job.state.load();
            if (!job.paused.load()) job.state.store(TransferJobState::Verifying);
            uint64_t dstHash = 0;
            bool hashOk = ComputeXxHash64FileBuffered(writePath, workerBuf, workerBufCap, dstHash, &job.cancelled) &&
                          (dstHash == srcHasher.Digest());
            if (!job.paused.load() && !job.cancelled.load()) job.state.store(prevSt);
            if (!hashOk) {
                SetFileAttributesW(extWritePath.c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(extWritePath.c_str());
                job.doneBytes.fetch_sub(bytesAddedThisAttempt, std::memory_order_relaxed);
                outErr = { item.srcPath, finalDst, ERROR_CRC, L"XXH64 checksum mismatch on destination!", 0 };
                return false;
            }
        }
        job.verifiedFiles.fetch_add(1, std::memory_order_relaxed);
    }

    // 10. Atomic commit of .e2part -> final destination filename
    if (usePart) {
        DWORD renErr = 0;
        if (!MoveItemAtomicWithRetry(writePath, finalDst, false, true, renErr)) {
            job.doneBytes.fetch_sub(bytesAddedThisAttempt, std::memory_order_relaxed);
            outErr = { item.srcPath, finalDst, renErr, L"Atomic .e2part commit failed: " + FormatWin32Err(renErr), resumeOffset };
            return false;
        }
    }

    // 11. Cross-volume Move: delete source ONLY AFTER destination is 100% committed & verified!
    if (job.op == TransferOp::Move) {
        DWORD sAttr = GetFileAttributesW(extSrc.c_str());
        if (sAttr != INVALID_FILE_ATTRIBUTES && (sAttr & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN))) {
            SetFileAttributesW(extSrc.c_str(), FILE_ATTRIBUTE_NORMAL);
        }
        if (!DeleteFileW(extSrc.c_str())) {
            DWORD delErr = GetLastError();
            if (delErr != ERROR_FILE_NOT_FOUND) {
                for (int r = 0; r < 3; ++r) {
                    Sleep(40);
                    if (DeleteFileW(extSrc.c_str())) { delErr = 0; break; }
                    delErr = GetLastError();
                }
                if (delErr != 0 && delErr != ERROR_FILE_NOT_FOUND) {
                    outErr = { item.srcPath, finalDst, delErr, L"Copied OK, but could not remove source: " + FormatWin32Err(delErr), 0 };
                    return false;
                }
            }
        }
    }

    return true;
}

// ============================================================================
// UI Snapshots, Telemetry & Wait Helpers
// ============================================================================
TransferJobSnapshot TransferManager::BuildSnapshot(const TransferManager::InternalJob& j) {
    TransferJobSnapshot s;
    s.id = j.id;
    s.op = j.op;
    s.state = j.state.load();
    s.conflictPolicy = static_cast<TransferConflictPolicy>(j.conflictPolicyAtomic.load());
    s.verifyMode = j.cfg.verifyMode;
    s.forceStarted = j.forceStart.load();
    s.sources = j.sources;
    s.targetDir = j.targetDir;
    s.srcVolumeLabel = j.srcVolLabel;
    s.dstVolumeLabel = j.dstVolLabel;

    s.totalFiles = j.totalFiles.load();
    s.doneFiles = j.doneFiles.load();
    s.skippedFiles = j.skippedFiles.load();
    s.renamedCollisions = j.renamedCollisions.load();
    s.verifiedFiles = j.verifiedFiles.load();
    s.resumedFiles = j.resumedFiles.load();
    s.totalBytes = j.totalBytes.load();
    s.doneBytes = std::min(s.totalBytes > 0 ? s.totalBytes : UINT64_MAX, j.doneBytes.load());

    {
        std::lock_guard<std::mutex> jlk(j.jobMutex);
        s.currentFile = j.currentFile;
        s.summaryText = j.summaryText;
        s.errors = j.errors;
        s.speedBytesPerSec = j.liveSpeedBps;
        s.peakBytesPerSec = j.peakSpeedBps;

        if (j.finalElapsedMs > 0) {
            s.elapsedActiveMs = j.finalElapsedMs;
        } else if (j.startTick > 0) {
            ULONGLONG now = GetTickCount64();
            uint64_t raw = (now > j.startTick) ? (now - j.startTick) : 0ULL;
            uint64_t extraPause = (j.paused.load() && j.pauseBeginTick > 0 && now > j.pauseBeginTick)
                ? (now - j.pauseBeginTick) : 0ULL;
            uint64_t totPause = j.pausedAccumMs + extraPause;
            s.elapsedActiveMs = (raw > totPause) ? (raw - totPause) : 0ULL;
        }
    }

    s.recycle = j.recycle;
    if (j.op == TransferOp::Delete) {
        // Items per second since the job started: enough for an ETA on a queue row
        if (s.elapsedActiveMs > 200 && s.doneFiles > 0) {
            s.itemsPerSec = (static_cast<double>(s.doneFiles) * 1000.0) / static_cast<double>(s.elapsedActiveMs);
        }
        if (s.state == TransferJobState::Running && s.totalFiles > s.doneFiles && s.itemsPerSec > 0.5) {
            s.etaSeconds = static_cast<int64_t>(std::ceil(static_cast<double>(s.totalFiles - s.doneFiles) / s.itemsPerSec));
        } else if (s.state == TransferJobState::Completed) {
            s.etaSeconds = 0;
        }
        return s;
    }

    if ((s.state == TransferJobState::Running || s.state == TransferJobState::Verifying) && s.totalBytes > s.doneBytes) {
        double spd = s.speedBytesPerSec;
        if (spd < 1024.0 && s.elapsedActiveMs > 300 && s.doneBytes > 0) {
            spd = (static_cast<double>(s.doneBytes) * 1000.0) / static_cast<double>(s.elapsedActiveMs);
            s.speedBytesPerSec = spd;
        }
        if (spd > 1024.0) {
            uint64_t rem = s.totalBytes - s.doneBytes;
            s.etaSeconds = static_cast<int64_t>(std::ceil(static_cast<double>(rem) / spd));
        }
    } else if (s.state == TransferJobState::Completed) {
        s.etaSeconds = 0;
    }

    return s;
}

bool TransferManager::HasActiveOrPausedJobs() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    for (const auto& j : m_jobs) {
        TransferJobState st = j->state.load();
        if (st == TransferJobState::Queued || st == TransferJobState::Scanning ||
            st == TransferJobState::Running || st == TransferJobState::Paused ||
            st == TransferJobState::Verifying)
        {
            return true;
        }
    }
    return false;
}

bool TransferManager::HasRunningJobs() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    for (const auto& j : m_jobs) {
        TransferJobState st = j->state.load();
        if (st == TransferJobState::Queued || st == TransferJobState::Scanning ||
            st == TransferJobState::Running || st == TransferJobState::Verifying)
        {
            return true;
        }
    }
    return false;
}

bool TransferManager::GetPrimaryHudJob(TransferJobSnapshot& outSnap, size_t* outTotalActiveAndQueued) const {
    std::shared_ptr<InternalJob> best;
    size_t count = 0;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        for (const auto& j : m_jobs) {
            TransferJobState st = j->state.load();
            if (st == TransferJobState::Running || st == TransferJobState::Verifying || st == TransferJobState::Scanning) {
                ++count;
                if (!best || best->state.load() == TransferJobState::Paused || best->state.load() == TransferJobState::Queued) {
                    best = j;
                }
            } else if (st == TransferJobState::Paused || st == TransferJobState::Queued) {
                ++count;
                if (!best) best = j;
            }
        }
    }
    if (outTotalActiveAndQueued) *outTotalActiveAndQueued = count;
    if (!best) return false;
    outSnap = BuildSnapshot(*best);
    return true;
}

std::vector<TransferJobSnapshot> TransferManager::SnapshotPendingJobs(size_t maxJobs, size_t* outTotal) const {
    std::vector<std::shared_ptr<InternalJob>> byRank[3];
    size_t total = 0;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        for (const auto& j : m_jobs) {
            const TransferJobState st = j->state.load();
            int rank = -1;
            if (st == TransferJobState::Running || st == TransferJobState::Verifying || st == TransferJobState::Scanning) rank = 0;
            else if (st == TransferJobState::Paused) rank = 1;
            else if (st == TransferJobState::Queued) rank = 2;
            if (rank < 0) continue;
            ++total;
            if (byRank[rank].size() < maxJobs) byRank[rank].push_back(j);
        }
    }
    if (outTotal) *outTotal = total;
    std::vector<TransferJobSnapshot> out;
    for (const auto& group : byRank) {
        for (const auto& j : group) {
            if (out.size() >= maxJobs) return out;
            out.push_back(BuildSnapshot(*j));
        }
    }
    return out;
}

std::vector<TransferJobSnapshot> TransferManager::SnapshotJobs() const {
    std::vector<std::shared_ptr<InternalJob>> copy;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        copy = m_jobs;
    }
    std::vector<TransferJobSnapshot> out;
    out.reserve(copy.size());
    for (const auto& j : copy) {
        out.push_back(BuildSnapshot(*j));
    }
    return out;
}

TransferAggregateTelemetry TransferManager::GetTelemetry() {
    m_uiNotifyPending.store(false, std::memory_order_relaxed);
    TransferAggregateTelemetry tel;
    std::vector<std::shared_ptr<InternalJob>> copy;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        copy = m_jobs;
    }

    double sumJobSpeed = 0.0;
    for (const auto& j : copy) {
        TransferJobState st = j->state.load();
        switch (st) {
        case TransferJobState::Running:
        case TransferJobState::Scanning:
        case TransferJobState::Verifying:
            ++tel.activeJobs;
            {
                std::lock_guard<std::mutex> jlk(j->jobMutex);
                sumJobSpeed += j->liveSpeedBps;
                tel.totalFailedFiles += j->errors.size();
            }
            break;
        case TransferJobState::Paused:
            ++tel.pausedJobs;
            {
                std::lock_guard<std::mutex> jlk(j->jobMutex);
                tel.totalFailedFiles += j->errors.size();
            }
            break;
        case TransferJobState::Queued:
            ++tel.queuedJobs;
            break;
        default:
            ++tel.finishedJobs;
            {
                std::lock_guard<std::mutex> jlk(j->jobMutex);
                tel.totalFailedFiles += j->errors.size();
            }
            break;
        }
    }

    ULONGLONG now = GetTickCount64();
    uint64_t curBytes = m_sessionBytes.load(std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (now - m_lastTelemetrySampleTick >= 350) {
            double dt = static_cast<double>(now - m_lastTelemetrySampleTick) / 1000.0;
            uint64_t db = (curBytes >= m_lastSampleSessionBytes) ? (curBytes - m_lastSampleSessionBytes) : 0ULL;
            double instBps = (dt > 0.01) ? (static_cast<double>(db) / dt) : 0.0;
            if (tel.activeJobs == 0 && db == 0) {
                m_smoothedSpeedBps = 0.0;
            } else {
                m_smoothedSpeedBps = (m_smoothedSpeedBps < 1.0) ? instBps : (m_smoothedSpeedBps * 0.55 + instBps * 0.45);
            }
            if (m_smoothedSpeedBps > m_peakBytesPerSec) m_peakBytesPerSec = m_smoothedSpeedBps;
            m_speedHistoryMBps.push_back(static_cast<float>(m_smoothedSpeedBps / (1024.0 * 1024.0)));
            while (m_speedHistoryMBps.size() > 60) m_speedHistoryMBps.pop_front();
            m_lastTelemetrySampleTick = now;
            m_lastSampleSessionBytes = curBytes;
        }
        tel.liveBytesPerSec = std::max(m_smoothedSpeedBps, sumJobSpeed);
        if (tel.liveBytesPerSec > m_peakBytesPerSec) m_peakBytesPerSec = tel.liveBytesPerSec;
        tel.peakBytesPerSec = m_peakBytesPerSec;
        tel.speedHistoryMBps.assign(m_speedHistoryMBps.begin(), m_speedHistoryMBps.end());
    }

    tel.sessionBytesTransferred = curBytes;
    tel.sessionFilesTransferred = m_sessionFiles.load(std::memory_order_relaxed);
    return tel;
}

std::vector<TransferCompletionEvent> TransferManager::TakeCompletionEvents() {
    std::lock_guard<std::mutex> lk(m_mutex);
    std::vector<TransferCompletionEvent> out;
    out.swap(m_completions);
    return out;
}

bool TransferManager::WaitForJob(int jobId, DWORD timeoutMs) {
    const ULONGLONG start = GetTickCount64();
    std::unique_lock<std::mutex> lk(m_mutex);
    std::shared_ptr<InternalJob> target;
    for (const auto& j : m_jobs) {
        if (j->id == jobId) { target = j; break; }
    }
    if (!target) return false;

    while (!target->threadFinished.load()) {
        ULONGLONG elapsed = GetTickCount64() - start;
        if (elapsed >= timeoutMs) return false;
        DWORD waitSlice = static_cast<DWORD>(std::min<ULONGLONG>(timeoutMs - elapsed, 50ULL));
        m_cv.wait_for(lk, std::chrono::milliseconds(waitSlice), [&]() {
            return target->threadFinished.load();
        });
    }
    TransferJobState st = target->state.load();
    return st == TransferJobState::Completed || st == TransferJobState::CompletedWithErrors || st == TransferJobState::Cancelled;
}

} // namespace Explorer2
