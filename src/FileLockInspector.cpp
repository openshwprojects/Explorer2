#include "FileLockInspector.h"
#include "AppLog.h"
#include "FileEngine.h"
#include "OsCompat.h"
#include "PathUtils.h"
#include "ThemeColors.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#ifdef _WIN32
#include <commctrl.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <winsvc.h>
#pragma comment(lib, "advapi32.lib")
#else
#include <climits>
#include <csignal>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace Explorer2::FileLockInspector {

namespace {

std::wstring TrimSlashesForMatch(std::wstring p) {
    while (p.size() > 1 && (p.back() == L'\\' || p.back() == L'/')) {
#ifdef _WIN32
        if (p.size() == 3 && p[1] == L':') break;
#endif
        p.pop_back();
    }
    return p;
}

std::wstring NormalizeMatchKey(std::wstring p) {
    p = PathUtils::StripExtendedWinPrefix(p);
#ifdef _WIN32
    for (wchar_t& ch : p) {
        if (ch == L'/') ch = L'\\';
    }
    p = TrimSlashesForMatch(std::move(p));
    return PathUtils::ToLower(p);
#else
    for (wchar_t& ch : p) {
        if (ch == L'\\') ch = L'/';
    }
    while (p.size() > 1 && p.back() == L'/') p.pop_back();
    return p;
#endif
}

bool IsDllLikeExtension(const std::wstring& extLow) {
    return extLow == L".dll" || extLow == L".ocx" || extLow == L".pyd" ||
           extLow == L".cpl" || extLow == L".drv" || extLow == L".asi" ||
           extLow == L".sys" || extLow == L".scr" || extLow == L".so";
}

#ifdef _WIN32
std::wstring ResolveFinalDosPath(const std::wstring& rawPath) {
    HANDLE h = CreateFileW(
        PathUtils::ToExtendedWinPath(rawPath).c_str(),
        0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr);
    if (h == INVALID_HANDLE_VALUE) return L"";
    wchar_t buf[2048] = {};
    const DWORD n = GetFinalPathNameByHandleW(h, buf, static_cast<DWORD>(std::size(buf) - 1), VOLUME_NAME_DOS);
    CloseHandle(h);
    if (n == 0 || n >= std::size(buf)) return L"";
    return PathUtils::StripExtendedWinPrefix(std::wstring_view(buf, n));
}
#endif

struct NormalizedTargets {
    // Exact file keys and subtree directory keys (both user-path and canonical DOS volume path for subst/junctions)
    std::unordered_set<std::wstring> exactKeys;
    std::vector<std::wstring>        dirPrefixes;
    // Map canonical DOS prefix back to user-visible prefix when a drive is subst/junction-mapped
    std::vector<std::pair<std::wstring, std::wstring>> canonicalToDisplayDir;

    bool Matches(const std::wstring& rawCandidate, bool* outMatchedDirItself = nullptr) const {
        if (outMatchedDirItself) *outMatchedDirItself = false;
        if (rawCandidate.empty()) return false;
        const std::wstring key = NormalizeMatchKey(rawCandidate);
        if (key.empty()) return false;
        if (exactKeys.find(key) != exactKeys.end()) return true;

#ifdef _WIN32
        const wchar_t sep = L'\\';
#else
        const wchar_t sep = L'/';
#endif
        for (const auto& dp : dirPrefixes) {
            if (key == dp) {
                if (outMatchedDirItself) *outMatchedDirItself = true;
                return true;
            }
            if (dp.size() == 3 && dp[1] == L':' && dp[2] == sep) {
                if (key.rfind(dp, 0) == 0) return true;
            } else if (dp == L"/") {
                return true;
            } else if (key.size() > dp.size() + 1u && key[dp.size()] == sep && key.compare(0, dp.size(), dp) == 0) {
                return true;
            }
        }
        return false;
    }

    std::wstring MapCanonicalToDisplay(const std::wstring& resolvedPath) const {
#ifdef _WIN32
        const std::wstring rk = NormalizeMatchKey(resolvedPath);
        for (const auto& alias : canonicalToDisplayDir) {
            if (rk == alias.first) {
                return alias.second;
            } else if (rk.size() > alias.first.size() + 1u &&
                       rk[alias.first.size()] == L'\\' &&
                       rk.compare(0, alias.first.size(), alias.first) == 0)
            {
                return PathUtils::EnsureTrailingSlash(alias.second) +
                       resolvedPath.substr(alias.first.size() + 1u);
            }
        }
#endif
        return resolvedPath;
    }
};

void AddUniqueLockedPath(std::vector<std::wstring>& vec, const std::wstring& rawPath) {
    if (rawPath.empty() || vec.size() >= 64u) return;
    const std::wstring clean = PathUtils::StripExtendedWinPrefix(rawPath);
    const std::wstring key = NormalizeMatchKey(clean);
    for (const auto& existing : vec) {
        if (NormalizeMatchKey(existing) == key) return;
    }
    vec.push_back(clean);
}

void AddUniqueLockedHandle(std::vector<LockedHandleDescriptor>& vec, const LockedHandleDescriptor& desc) {
    if (desc.handleValue == 0 || vec.size() >= 256u) return;
    for (const auto& existing : vec) {
        if (existing.handleValue == desc.handleValue) return;
    }
    vec.push_back(desc);
}

#ifdef _WIN32

// ============================================================================
// Dynamic Windows Restart Manager (rstrtmgr.dll — Vista+, zero static imports)
// ============================================================================
constexpr DWORD kRmSessionKeyLen = 32;
constexpr DWORD kRmMaxAppName    = 255;
constexpr DWORD kRmMaxSvcName    = 63;

#pragma pack(push, 8)
struct E2_RM_UNIQUE_PROCESS {
    DWORD    dwProcessId;
    FILETIME ProcessStartTime;
};

struct E2_RM_PROCESS_INFO {
    E2_RM_UNIQUE_PROCESS Process;
    WCHAR                strAppName[kRmMaxAppName + 1];
    WCHAR                strServiceShortName[kRmMaxSvcName + 1];
    int                  ApplicationType; // 3 = RmService, 4 = RmExplorer
    ULONG                AppStatus;
    DWORD                TSSessionId;
    BOOL                 bRestartable;
};
#pragma pack(pop)

struct RestartMgrApi {
    using RmStartSessionFn      = DWORD(WINAPI*)(DWORD*, DWORD, WCHAR[]);
    using RmRegisterResourcesFn = DWORD(WINAPI*)(DWORD, UINT, LPCWSTR[], UINT, E2_RM_UNIQUE_PROCESS[], UINT, LPCWSTR[]);
    using RmGetListFn           = DWORD(WINAPI*)(DWORD, UINT*, UINT*, E2_RM_PROCESS_INFO[], LPDWORD);
    using RmEndSessionFn        = DWORD(WINAPI*)(DWORD);

    RmStartSessionFn      StartSession      = nullptr;
    RmRegisterResourcesFn RegisterResources = nullptr;
    RmGetListFn           GetList           = nullptr;
    RmEndSessionFn        EndSession        = nullptr;
    bool                  available         = false;
};

const RestartMgrApi& GetRestartMgrApi() {
    static const RestartMgrApi s_api = []() {
        RestartMgrApi api;
        wchar_t sysDir[MAX_PATH] = {};
        const UINT n = GetSystemDirectoryW(sysDir, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) return api;
        HMODULE mod = LoadLibraryW((std::wstring(sysDir) + L"\\rstrtmgr.dll").c_str());
        if (!mod) return api;
        api.StartSession      = reinterpret_cast<RestartMgrApi::RmStartSessionFn>(GetProcAddress(mod, "RmStartSession"));
        api.RegisterResources = reinterpret_cast<RestartMgrApi::RmRegisterResourcesFn>(GetProcAddress(mod, "RmRegisterResources"));
        api.GetList           = reinterpret_cast<RestartMgrApi::RmGetListFn>(GetProcAddress(mod, "RmGetList"));
        api.EndSession        = reinterpret_cast<RestartMgrApi::RmEndSessionFn>(GetProcAddress(mod, "RmEndSession"));
        api.available = (api.StartSession && api.RegisterResources && api.GetList && api.EndSession);
        return api;
    }();
    return s_api;
}

// ============================================================================
// Dynamic NT Kernel APIs (ntdll.dll — zero static imports, Win7 SP1 safe)
// ============================================================================
struct E2_UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR  Buffer;
};

struct E2_SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX {
    PVOID     Object;
    ULONG_PTR UniqueProcessId;
    ULONG_PTR HandleValue;
    ULONG     GrantedAccess;
    USHORT    CreatorBackTraceIndex;
    USHORT    ObjectTypeIndex;
    ULONG     HandleAttributes; // 0x1 = OBJ_PROTECT_CLOSE
    ULONG     Reserved;
};

struct E2_SYSTEM_EXTENDED_HANDLE_INFORMATION {
    ULONG_PTR                            NumberOfHandles;
    ULONG_PTR                            Reserved;
    E2_SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX Handles[1];
};

using NtQuerySystemInformationFn = LONG(WINAPI*)(ULONG, PVOID, ULONG, PULONG);
using NtQueryObjectFn            = LONG(WINAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
using NtQueryVirtualMemoryFn     = LONG(WINAPI*)(HANDLE, PVOID, int, PVOID, SIZE_T, PSIZE_T);

struct NtdllApi {
    NtQuerySystemInformationFn QuerySystemInformation = nullptr;
    NtQueryObjectFn            QueryObject            = nullptr;
    NtQueryVirtualMemoryFn     QueryVirtualMemory     = nullptr;
};

const NtdllApi& GetNtdllApi() {
    static const NtdllApi s_nt = []() {
        NtdllApi nt;
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (ntdll) {
            nt.QuerySystemInformation = reinterpret_cast<NtQuerySystemInformationFn>(GetProcAddress(ntdll, "NtQuerySystemInformation"));
            nt.QueryObject            = reinterpret_cast<NtQueryObjectFn>(GetProcAddress(ntdll, "NtQueryObject"));
            nt.QueryVirtualMemory     = reinterpret_cast<NtQueryVirtualMemoryFn>(GetProcAddress(ntdll, "NtQueryVirtualMemory"));
        }
        return nt;
    }();
    return s_nt;
}

// ============================================================================
// Dynamic DWM & UxTheme APIs for Dark Mode (Win7 SP1 safe — zero static imports)
// ============================================================================
void ApplyDarkWindowChromeCompat(HWND hwnd, bool dark) {
    using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    static const DwmSetWindowAttributeFn s_dwmSetAttr = []() -> DwmSetWindowAttributeFn {
        wchar_t sysDir[MAX_PATH] = {};
        const UINT n = GetSystemDirectoryW(sysDir, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) return nullptr;
        HMODULE dwm = LoadLibraryW((std::wstring(sysDir) + L"\\dwmapi.dll").c_str());
        return dwm ? reinterpret_cast<DwmSetWindowAttributeFn>(GetProcAddress(dwm, "DwmSetWindowAttribute")) : nullptr;
    }();
    if (!hwnd || !s_dwmSetAttr) return;
    const BOOL val = dark ? TRUE : FALSE;
    // 20 = DWMWA_USE_IMMERSIVE_DARK_MODE (Win10 20H1+ / Win11), 19 = Win10 1809/1903 fallback
    if (FAILED(s_dwmSetAttr(hwnd, 20u, &val, sizeof(val)))) {
        s_dwmSetAttr(hwnd, 19u, &val, sizeof(val));
    }
}

void ApplyControlVisualThemeCompat(HWND hwnd, const wchar_t* subAppName) {
    using SetWindowThemeFn = HRESULT(WINAPI*)(HWND, LPCWSTR, LPCWSTR);
    static const SetWindowThemeFn s_setTheme = []() -> SetWindowThemeFn {
        wchar_t sysDir[MAX_PATH] = {};
        const UINT n = GetSystemDirectoryW(sysDir, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) return nullptr;
        HMODULE ux = LoadLibraryW((std::wstring(sysDir) + L"\\uxtheme.dll").c_str());
        return ux ? reinterpret_cast<SetWindowThemeFn>(GetProcAddress(ux, "SetWindowTheme")) : nullptr;
    }();
    if (hwnd && s_setTheme) {
        s_setTheme(hwnd, subAppName, nullptr);
    }
}

// ============================================================================
// NT Device Prefix (\Device\HarddiskVolumeN) -> DOS Drive (C:) Mapper
// ============================================================================
struct DosDevicePathMapper {
    struct Entry {
        std::wstring ntPrefixLower;
        size_t       ntLen = 0;
        std::wstring dosPrefix;
    };
    std::vector<Entry> entries;

    void Initialize() {
        entries.clear();
        wchar_t drivesBuf[512] = {};
        const DWORD nChars = GetLogicalDriveStringsW(static_cast<DWORD>(std::size(drivesBuf) - 1), drivesBuf);
        if (nChars == 0 || nChars >= std::size(drivesBuf)) return;

        wchar_t devBuf[2048] = {};
        for (const wchar_t* p = drivesBuf; *p; p += std::wcslen(p) + 1) {
            if (p[0] == L'\0' || p[1] != L':') continue;
            const wchar_t driveName[3] = { p[0], L':', L'\0' };
            const DWORD dn = QueryDosDeviceW(driveName, devBuf, static_cast<DWORD>(std::size(devBuf) - 1));
            if (dn == 0) continue;
            std::wstring target(devBuf); // first string of MULTI_SZ
            if (target.rfind(L"\\??\\", 0) == 0) target.erase(0, 4);
            while (target.size() > 1 && target.back() == L'\\') target.pop_back();
            if (!target.empty()) {
                entries.push_back({ PathUtils::ToLower(target), target.size(), driveName });
            }
        }
        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
            return a.ntLen > b.ntLen;
        });
    }

    bool ConvertNtPathToDos(std::wstring_view ntPath, std::wstring& outDos) const {
        outDos.clear();
        if (ntPath.empty()) return false;
        if (ntPath.size() >= 8 && _wcsnicmp(ntPath.data(), L"\\??\\UNC\\", 8) == 0) {
            outDos = L"\\\\" + std::wstring(ntPath.substr(8));
            return true;
        }
        if (ntPath.size() >= 4 && _wcsnicmp(ntPath.data(), L"\\??\\", 4) == 0) {
            outDos = std::wstring(ntPath.substr(4));
            return true;
        }
        if (ntPath.size() >= 12 && _wcsnicmp(ntPath.data(), L"\\Device\\Mup\\", 12) == 0) {
            outDos = L"\\\\" + std::wstring(ntPath.substr(12));
            return true;
        }
        for (const auto& e : entries) {
            if (ntPath.size() >= e.ntLen &&
                (ntPath.size() == e.ntLen || ntPath[e.ntLen] == L'\\') &&
                _wcsnicmp(ntPath.data(), e.ntPrefixLower.c_str(), e.ntLen) == 0)
            {
                outDos = e.dosPrefix + std::wstring(ntPath.substr(e.ntLen));
                if (outDos.size() == 2 && outDos[1] == L':') outDos.push_back(L'\\');
                return true;
            }
        }
        return false;
    }
};

// ============================================================================
// Batched Pipe-Deadlock-Proof Background Handle Path Resolver
// ============================================================================
struct BatchHandleItem {
    ULONG_PTR    handleVal = 0;
    PVOID        kernelObj = nullptr;
    ULONG        access = 0;
    ULONG        handleAttrs = 0;
    std::wstring resolvedPath;
    bool         isDir = false;
    bool         processed = false;
    bool         skip = false;
};

struct PipeSafeBatchState {
    std::mutex                    mtx;
    std::condition_variable       cvReq;
    std::condition_variable       cvDone;
    uint32_t                      reqPid = 0;
    BatchHandleItem*              reqItems = nullptr;
    size_t                        reqStartIdx = 0;
    size_t                        reqEndIdx = 0;
    const NormalizedTargets*      prefilterTargets = nullptr;
    const DosDevicePathMapper*    dosMapper = nullptr;
    std::atomic<size_t>           curIdx{ 0 };
    std::atomic<uint64_t>         progressSeq{ 0 };
    std::atomic<bool>             inKernelFileProbe{ false };
    std::atomic<bool>             procOpenFailed{ false };
    std::atomic<HANDLE>           workerThreadHandle{ nullptr };
    bool                          hasReq = false;
    bool                          reqDone = false;
    bool                          quit = false;
    std::atomic<bool>             abandoned{ false };
    // Keep storage alive if a worker is abandoned mid-batch on a synchronous pipe!
    std::shared_ptr<std::vector<BatchHandleItem>> pinnedItems;
};

inline void CancelThreadSyncIoCompat(HANDLE hThread) {
    if (!hThread) return;
    using CancelSyncIoFn = BOOL(WINAPI*)(HANDLE);
    static const CancelSyncIoFn s_cancel = []() -> CancelSyncIoFn {
        HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
        return k32 ? reinterpret_cast<CancelSyncIoFn>(GetProcAddress(k32, "CancelSynchronousIo")) : nullptr;
    }();
    if (s_cancel) s_cancel(hThread);
}

class PipeSafeHandleResolver {
public:
    static constexpr int kMaxAbandonedWorkers = 64;

    PipeSafeHandleResolver() { SpawnWorker(); }
    ~PipeSafeHandleResolver() { StopCleanWorker(); }

    bool IsExhausted() const { return m_abandonedCount >= kMaxAbandonedWorkers; }

    // Resolves a slice [startIdx, endIdx) of a PID's handles on the background worker thread.
    // Tracks monotonic `progressSeq` and `inKernelFileProbe` so OS thread startup, `OpenProcess`, or 1-handle
    // NTFS path queries NEVER false-stall, while genuinely blocked synchronous named pipes stall only while
    // `inKernelFileProbe == true` for `stallSliceMs` (~25ms), detach cleanly, preserve all earlier resolved handles,
    // and return the exact stalled index so the caller continues immediately with `stallIdx + 1`!
    bool RunPidSubrange(
        uint32_t pid,
        const std::shared_ptr<std::vector<BatchHandleItem>>& items,
        size_t startIdx,
        size_t endIdx,
        const NormalizedTargets* prefilterTargets,
        const DosDevicePathMapper* dosMapper,
        ULONGLONG overallDeadlineTick,
        bool& outProcOpenFailed,
        size_t& outStalledAtIdx,
        DWORD stallSliceMs = 25u)
    {
        outProcOpenFailed = false;
        outStalledAtIdx = SIZE_MAX;
        if (pid == 0 || !items || startIdx >= endIdx || endIdx > items->size() || IsExhausted()) return true;
        if (!m_state) SpawnWorker();

        auto st = m_state;
        {
            std::unique_lock<std::mutex> lk(st->mtx);
            st->reqPid = pid;
            st->reqItems = items->data();
            st->reqStartIdx = startIdx;
            st->reqEndIdx = endIdx;
            st->prefilterTargets = prefilterTargets;
            st->dosMapper = dosMapper;
            st->pinnedItems = items;
            st->curIdx.store(startIdx, std::memory_order_release);
            st->inKernelFileProbe.store(false, std::memory_order_release);
            const uint64_t initialSeq = st->progressSeq.load(std::memory_order_acquire);
            st->procOpenFailed.store(false, std::memory_order_release);
            st->reqDone = false;
            st->hasReq = true;
            st->cvReq.notify_one();

            uint64_t lastSeq = initialSeq;
            while (!st->reqDone) {
                if (st->cvDone.wait_for(lk, std::chrono::milliseconds(stallSliceMs), [&]() { return st->reqDone; })) {
                    break;
                }
                const uint64_t nowSeq = st->progressSeq.load(std::memory_order_acquire);
                const bool inProbe = st->inKernelFileProbe.load(std::memory_order_acquire);
                if (GetTickCount64() < overallDeadlineTick && (nowSeq != lastSeq || !inProbe)) {
                    // Either the worker advanced `progressSeq` OR it hasn't entered a blocking kernel FILE_OBJECT IRP yet
                    lastSeq = nowSeq;
                    continue;
                }
                // Worker stalled inside a synchronous named pipe/driver IRP (or overall deadline expired)
                outStalledAtIdx = st->curIdx.load(std::memory_order_acquire);
                st->abandoned.store(true, std::memory_order_release);
                st->quit = true;
                HANDLE hWk = st->workerThreadHandle.exchange(nullptr, std::memory_order_acq_rel);
                lk.unlock();
                if (hWk) {
                    CancelThreadSyncIoCompat(hWk);
                    CloseHandle(hWk);
                }
                if (m_thread.joinable()) m_thread.detach();
                m_state.reset();
                ++m_abandonedCount;
                if (!IsExhausted()) SpawnWorker();
                return false;
            }

            outProcOpenFailed = st->procOpenFailed.load(std::memory_order_acquire);
            st->pinnedItems.reset();
            st->reqItems = nullptr;
        }
        return true;
    }

    bool VerifySingleDiskHandle(
        uint32_t pid,
        ULONG_PTR handleVal,
        const DosDevicePathMapper& dosMapper,
        std::wstring& outPath,
        bool& outTimedOut)
    {
        outPath.clear();
        outTimedOut = false;
        auto vec = std::make_shared<std::vector<BatchHandleItem>>(1);
        (*vec)[0].handleVal = handleVal;
        bool procFailed = false;
        size_t stalledIdx = SIZE_MAX;
        const bool finished = RunPidSubrange(
            pid, vec, 0, 1, /*prefilterTargets=*/nullptr, &dosMapper,
            GetTickCount64() + 500ULL, procFailed, stalledIdx, /*stallSliceMs=*/150u);
        if (!finished) {
            outTimedOut = true;
            return false;
        }
        if (procFailed || !(*vec)[0].processed || (*vec)[0].resolvedPath.empty()) return false;
        outPath = (*vec)[0].resolvedPath;
        return true;
    }

private:
    void SpawnWorker() {
        auto st = std::make_shared<PipeSafeBatchState>();
        m_state = st;
        m_thread = std::thread([st]() {
            HANDLE hSelfThread = nullptr;
            if (DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
                                &hSelfThread, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
                HANDLE expected = nullptr;
                if (!st->workerThreadHandle.compare_exchange_strong(expected, hSelfThread, std::memory_order_acq_rel)) {
                    CloseHandle(hSelfThread);
                }
            }
            const NtdllApi& nt = GetNtdllApi();
            wchar_t pathBuf[2048] = {};
            alignas(8) uint8_t objNameBuf[4096] = {};
            HANDLE cachedProc = nullptr;
            uint32_t cachedPid = 0;
            auto closeCachedProc = [&]() {
                if (cachedProc) {
                    CloseHandle(cachedProc);
                    cachedProc = nullptr;
                    cachedPid = 0;
                }
                if (HANDLE hTh = st->workerThreadHandle.exchange(nullptr, std::memory_order_acq_rel)) {
                    CloseHandle(hTh);
                }
            };

            while (true) {
                uint32_t pid = 0;
                BatchHandleItem* items = nullptr;
                size_t sIdx = 0, eIdx = 0;
                const NormalizedTargets* prefilter = nullptr;
                const DosDevicePathMapper* mapper = nullptr;
                std::shared_ptr<std::vector<BatchHandleItem>> keepAlive;
                {
                    std::unique_lock<std::mutex> lk(st->mtx);
                    st->cvReq.wait(lk, [&]() { return st->hasReq || st->quit; });
                    if (st->abandoned.load(std::memory_order_acquire) || (st->quit && !st->hasReq)) {
                        closeCachedProc();
                        return;
                    }
                    pid = st->reqPid;
                    items = st->reqItems;
                    sIdx = st->reqStartIdx;
                    eIdx = st->reqEndIdx;
                    prefilter = st->prefilterTargets;
                    mapper = st->dosMapper;
                    keepAlive = st->pinnedItems;
                    st->hasReq = false;
                }
                st->progressSeq.fetch_add(1u, std::memory_order_release);

                if (pid != cachedPid) {
                    closeCachedProc();
                    cachedProc = OpenProcess(PROCESS_DUP_HANDLE, FALSE, pid);
                    cachedPid = cachedProc ? pid : 0;
                }
                st->progressSeq.fetch_add(1u, std::memory_order_release);

                if (!cachedProc) {
                    st->procOpenFailed.store(true, std::memory_order_release);
                } else if (items) {
                    for (size_t i = sIdx; i < eIdx; ++i) {
                        if (st->abandoned.load(std::memory_order_acquire)) {
                            closeCachedProc();
                            return;
                        }
                        if (items[i].skip) continue;
                        st->curIdx.store(i, std::memory_order_release);
                        st->progressSeq.fetch_add(1u, std::memory_order_release);

                        HANDLE hDup = nullptr;
                        if (DuplicateHandle(cachedProc, reinterpret_cast<HANDLE>(items[i].handleVal),
                                            GetCurrentProcess(), &hDup, 0, FALSE, 0) &&
                            hDup && hDup != INVALID_HANDLE_VALUE)
                        {
                            st->progressSeq.fetch_add(1u, std::memory_order_release);
                            st->inKernelFileProbe.store(true, std::memory_order_release);
                            const DWORD fType = GetFileType(hDup);
                            st->progressSeq.fetch_add(1u, std::memory_order_release);

                            if (fType == FILE_TYPE_DISK) {
                                bool mayMatch = true;
                                std::wstring ntFallbackDos;
                                if (nt.QueryObject && mapper) {
                                    ULONG retLen = 0;
                                    const LONG status = nt.QueryObject(
                                        hDup, 1u /*ObjectNameInformation*/, objNameBuf, sizeof(objNameBuf) - sizeof(wchar_t), &retLen);
                                    st->progressSeq.fetch_add(1u, std::memory_order_release);
                                    if (status >= 0 && retLen >= sizeof(E2_UNICODE_STRING)) {
                                        const auto* us = reinterpret_cast<const E2_UNICODE_STRING*>(objNameBuf);
                                        if (!us->Buffer || us->Length == 0) {
                                            mayMatch = false;
                                        } else {
                                            std::wstring_view ntSv(us->Buffer, us->Length / sizeof(wchar_t));
                                            if (mapper->ConvertNtPathToDos(ntSv, ntFallbackDos)) {
                                                if (prefilter && ntFallbackDos.find(L'~') == std::wstring::npos &&
                                                    !prefilter->Matches(ntFallbackDos))
                                                {
                                                    mayMatch = false;
                                                }
                                            }
                                        }
                                    }
                                }

                                if (mayMatch && !st->abandoned.load(std::memory_order_acquire)) {
                                    const DWORD len = GetFinalPathNameByHandleW(
                                        hDup, pathBuf, static_cast<DWORD>(std::size(pathBuf) - 1), VOLUME_NAME_DOS);
                                    st->progressSeq.fetch_add(1u, std::memory_order_release);
                                    std::wstring pathStr;
                                    if (len > 0 && len < std::size(pathBuf)) {
                                        pathStr = TrimSlashesForMatch(PathUtils::StripExtendedWinPrefix(std::wstring_view(pathBuf, len)));
                                    } else if (!ntFallbackDos.empty()) {
                                        pathStr = TrimSlashesForMatch(ntFallbackDos);
                                    }
                                    if (!pathStr.empty()) {
                                        bool isDir = false;
                                        BY_HANDLE_FILE_INFORMATION bhfi = {};
                                        if (GetFileInformationByHandle(hDup, &bhfi)) {
                                            isDir = (bhfi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                                        }
                                        st->progressSeq.fetch_add(1u, std::memory_order_release);
                                        if (!st->abandoned.load(std::memory_order_acquire)) {
                                            items[i].resolvedPath = std::move(pathStr);
                                            items[i].isDir = isDir;
                                        }
                                    }
                                }
                            }
                            st->inKernelFileProbe.store(false, std::memory_order_release);
                            CloseHandle(hDup);
                        }
                        if (!st->abandoned.load(std::memory_order_acquire)) {
                            items[i].processed = true;
                        }
                    }
                }

                keepAlive.reset();
                {
                    std::lock_guard<std::mutex> lk(st->mtx);
                    if (st->abandoned.load(std::memory_order_acquire)) {
                        closeCachedProc();
                        return;
                    }
                    st->reqDone = true;
                }
                st->cvDone.notify_one();
            }
        });
    }

    void StopCleanWorker() {
        if (!m_state) return;
        {
            std::lock_guard<std::mutex> lk(m_state->mtx);
            m_state->quit = true;
        }
        m_state->cvReq.notify_one();
        if (m_thread.joinable()) m_thread.join();
        m_state.reset();
    }

    std::shared_ptr<PipeSafeBatchState> m_state;
    std::thread                         m_thread;
    int                                 m_abandonedCount = 0;
};

bool IsCriticalWindowsProcessName(const std::wstring& exeLeafLower, uint32_t pid) {
    if (pid <= 4u) return true;
    return exeLeafLower == L"system"             ||
           exeLeafLower == L"registry"           ||
           exeLeafLower == L"smss.exe"           ||
           exeLeafLower == L"csrss.exe"          ||
           exeLeafLower == L"wininit.exe"        ||
           exeLeafLower == L"services.exe"       ||
           exeLeafLower == L"lsass.exe"          ||
           exeLeafLower == L"lsm.exe"            ||
           exeLeafLower == L"winlogon.exe"       ||
           exeLeafLower == L"dwm.exe"            ||
           exeLeafLower == L"fontdrvhost.exe"    ||
           exeLeafLower == L"svchost.exe"        ||
           exeLeafLower == L"memory compression";
}

struct ProcessBasicFacts {
    uint64_t     startTicks = 0;
    std::wstring exeFullPath;
    std::wstring exeName;
    bool         canTerminate = false;
    bool         terminateAccessDenied = false;
};

ProcessBasicFacts QueryProcessFactsWin32(uint32_t pid, const std::wstring& fallbackExeName = L"") {
    ProcessBasicFacts pf;
    pf.exeName = fallbackExeName;
    if (pid <= 4u) {
        if (pf.exeName.empty()) pf.exeName = L"System";
        pf.terminateAccessDenied = true;
        return pf;
    }

    HANDLE hQuery = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hQuery) {
        FILETIME ftCreate = {}, ftExit = {}, ftKernel = {}, ftUser = {};
        if (GetProcessTimes(hQuery, &ftCreate, &ftExit, &ftKernel, &ftUser)) {
            pf.startTicks = (static_cast<uint64_t>(ftCreate.dwHighDateTime) << 32) | ftCreate.dwLowDateTime;
        }
        wchar_t imgBuf[2048] = {};
        DWORD sz = static_cast<DWORD>(std::size(imgBuf) - 1);
        if (QueryFullProcessImageNameW(hQuery, 0, imgBuf, &sz) && sz > 0) {
            pf.exeFullPath = PathUtils::StripExtendedWinPrefix(std::wstring_view(imgBuf, sz));
            pf.exeName = PathUtils::GetFileName(pf.exeFullPath);
        }
        CloseHandle(hQuery);
    }

    if (pid != GetCurrentProcessId()) {
        HANDLE hTerm = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (hTerm) {
            pf.canTerminate = true;
            CloseHandle(hTerm);
        } else if (GetLastError() == ERROR_ACCESS_DENIED) {
            pf.terminateAccessDenied = true;
        }
    }
    return pf;
}

void CollectTopLevelWindowTitles(std::unordered_map<uint32_t, std::wstring>& outTitles) {
    outTitles.clear();
    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        if (!IsWindowVisible(hwnd)) return TRUE;
        const int len = GetWindowTextLengthW(hwnd);
        if (len <= 0) return TRUE;
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == 0) return TRUE;
        auto* mapPtr = reinterpret_cast<std::unordered_map<uint32_t, std::wstring>*>(lParam);
        if (mapPtr->find(pid) != mapPtr->end()) return TRUE;
        std::wstring title(static_cast<size_t>(std::min(len, 256)) + 1u, L'\0');
        const int got = GetWindowTextW(hwnd, title.data(), static_cast<int>(title.size()));
        if (got > 0) {
            title.resize(static_cast<size_t>(got));
            (*mapPtr)[pid] = std::move(title);
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&outTitles));
}

#else // !_WIN32

uint64_t QueryLinuxProcessStartTicks(uint32_t pid) {
    char path[64] = {};
    std::snprintf(path, sizeof(path), "/proc/%u/stat", pid);
    FILE* f = std::fopen(path, "r");
    if (!f) return 0u;
    char buf[2048] = {};
    const size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
    std::fclose(f);
    if (n == 0) return 0u;
    buf[n] = '\0';
    // comm (field 2) is enclosed in (...) and may contain spaces/parens; find the LAST ')'
    const char* rp = std::strrchr(buf, ')');
    if (!rp || rp[1] != ' ') return 0u;
    const char* p = rp + 2; // field 3 (state)
    // Advance 19 fields from field 3 to reach field 22 (starttime)
    for (int field = 3; field < 22; ++field) {
        while (*p && *p != ' ') ++p;
        while (*p == ' ') ++p;
        if (!*p) return 0u;
    }
    return static_cast<uint64_t>(std::strtoull(p, nullptr, 10));
}

void ClassifyLinuxProcessRole(const std::wstring& procName, uint32_t pid, bool& outCritical, bool& outShell) {
    outCritical = (pid <= 2u);
    outShell = false;
    const std::wstring low = PathUtils::ToLower(procName);
    if (low == L"systemd" || low == L"init" || low == L"kthreadd" ||
        low == L"dbus-daemon" || low == L"dbus-broker" || low == L"dbus-broker-launch" ||
        low == L"udevd" || low == L"systemd-udevd" || low == L"systemd-journald" ||
        low == L"systemd-logind" || low == L"polkitd" ||
        low == L"xorg" || low == L"xwayland")
    {
        outCritical = true;
        return;
    }
    if (low == L"gnome-shell" || low == L"plasmashell" ||
        low == L"kwin_x11" || low == L"kwin_wayland" ||
        low == L"xfwm4" || low == L"xfdesktop" ||
        low == L"mutter" || low == L"sway" || low == L"wayfire" || low == L"hyprland" ||
        low == L"cinnamon" || low == L"marco" || low == L"openbox" || low == L"i3" ||
        low == L"pipewire" || low == L"pipewire-pulse" || low == L"wireplumber" || low == L"pulseaudio" ||
        low == L"nautilus" || low == L"dolphin" || low == L"thunar" || low == L"nemo")
    {
        outShell = true;
    }
}

bool IsCriticalLinuxPid(uint32_t pid) {
    if (pid <= 2u) return true;
    char path[64] = {};
    std::snprintf(path, sizeof(path), "/proc/%u/comm", pid);
    std::wstring commW;
    if (FILE* f = std::fopen(path, "r")) {
        char buf[128] = {};
        if (std::fgets(buf, sizeof(buf), f)) {
            size_t len = std::strlen(buf);
            while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) --len;
            commW = PathUtils::Utf8ToWide(std::string(buf, len));
        }
        std::fclose(f);
    }
    bool crit = false, sh = false;
    ClassifyLinuxProcessRole(commW, pid, crit, sh);
    return crit;
}

bool IsLinuxSharedObjectPath(const std::wstring& path) {
    const std::wstring leaf = PathUtils::ToLower(PathUtils::GetFileName(path));
    if (leaf.size() >= 3 && leaf.compare(leaf.size() - 3, 3, L".so") == 0) return true;
    return leaf.find(L".so.") != std::wstring::npos;
}

#endif // _WIN32

} // namespace

// ============================================================================
// InspectFileLocks — Cross-Platform Multi-Engine Inspection
// ============================================================================
void InspectFileLocks(
    const std::vector<std::wstring>& targetPaths,
    FileLockReport& outReport,
    const SelfHoldInfo* selfHold,
    std::atomic<bool>* cancelFlag)
{
    outReport = {};
    const ULONGLONG tStart = GetTickCount64();

    NormalizedTargets normTargets;
    std::vector<std::wstring> cleanTargets;
    {
        std::unordered_set<std::wstring> seen;
        for (const auto& raw : targetPaths) {
            if (raw.empty()) continue;
            std::wstring full = raw;
#ifdef _WIN32
            wchar_t fullBuf[2048] = {};
            const DWORD fn = GetFullPathNameW(
                PathUtils::StripExtendedWinPrefix(raw).c_str(),
                static_cast<DWORD>(std::size(fullBuf)), fullBuf, nullptr);
            if (fn > 0 && fn < std::size(fullBuf)) full.assign(fullBuf, fn);
#endif
            full = TrimSlashesForMatch(PathUtils::StripExtendedWinPrefix(full));
            const std::wstring k = NormalizeMatchKey(full);
            if (!k.empty() && seen.insert(k).second) {
                cleanTargets.push_back(full);
            }
        }
    }
    outReport.targetPaths = cleanTargets;
    if (cleanTargets.empty()) return;

#ifdef _WIN32
    // 1. Classify targets (file vs directory), resolve canonical DOS volume aliases (subst/VHD/junctions),
    //    and collect nested files (up to 4,096 for Restart Manager & attribute checks; unlimited subtree
    //    coverage via Engine 2 NT Handle Table + Engine 3 VirtualQueryEx/NtQueryVirtualMemory!).
    constexpr size_t kMaxRmWalkFiles = 4096u;
    std::vector<std::wstring> rmCandidateFiles;
    std::vector<std::pair<std::wstring, DWORD>> attrCheckItems;

    for (const auto& tgt : cleanTargets) {
        if (cancelFlag && cancelFlag->load()) return;
        const DWORD attrs = GetFileAttributesW(PathUtils::ToExtendedWinPath(tgt).c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) continue;

        const bool isDir = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
        const std::wstring dispKey = NormalizeMatchKey(tgt);
        const std::wstring dosResolved = ResolveFinalDosPath(tgt);
        const std::wstring dosKey = dosResolved.empty() ? L"" : NormalizeMatchKey(dosResolved);

        attrCheckItems.emplace_back(tgt, attrs);

        if (isDir) {
            ++outReport.scannedDirs;
            normTargets.dirPrefixes.push_back(dispKey);
            if (!dosKey.empty() && dosKey != dispKey) {
                normTargets.dirPrefixes.push_back(dosKey);
                normTargets.canonicalToDisplayDir.emplace_back(dosKey, tgt);
            }

            std::vector<std::wstring> dirStack = { tgt };
            size_t totalWalked = 0;
            while (!dirStack.empty() && totalWalked < 32768u) {
                if (cancelFlag && cancelFlag->load()) return;
                std::wstring curDir = std::move(dirStack.back());
                dirStack.pop_back();

                std::wstring pattern = PathUtils::ToExtendedWinPath(PathUtils::EnsureTrailingSlash(curDir) + L"*");
                WIN32_FIND_DATAW ffd = {};
                HANDLE hFind = FindFirstFileW(pattern.c_str(), &ffd);
                if (hFind == INVALID_HANDLE_VALUE) continue;
                do {
                    if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0) continue;
                    ++totalWalked;
                    std::wstring child = PathUtils::EnsureTrailingSlash(curDir) + ffd.cFileName;
                    if (attrCheckItems.size() < 2048u) {
                        attrCheckItems.emplace_back(child, ffd.dwFileAttributes);
                    }
                    if ((ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                        ++outReport.scannedDirs;
                        if ((ffd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0 && dirStack.size() < 4096u) {
                            dirStack.push_back(std::move(child));
                        }
                    } else {
                        ++outReport.scannedFiles;
                        if (rmCandidateFiles.size() < kMaxRmWalkFiles) {
                            rmCandidateFiles.push_back(std::move(child));
                        }
                    }
                } while (FindNextFileW(hFind, &ffd));
                FindClose(hFind);
            }
        } else {
            ++outReport.scannedFiles;
            normTargets.exactKeys.insert(dispKey);
            if (!dosKey.empty()) normTargets.exactKeys.insert(dosKey);
            rmCandidateFiles.push_back(tgt);
        }
    }

    DosDevicePathMapper dosMapper;
    dosMapper.Initialize();

    const uint32_t selfPid = GetCurrentProcessId();
    std::unordered_map<uint32_t, std::wstring> winTitles;
    CollectTopLevelWindowTitles(winTitles);

    std::unordered_map<uint32_t, size_t> procIdxByPid;
    auto upsertProcess = [&](uint32_t pid,
                             uint32_t addReason,
                             const std::wstring& lockedPath,
                             const std::wstring& hintAppName = L"",
                             const std::wstring& hintSvcName = L"") -> LockingProcessEntry&
    {
        auto it = procIdxByPid.find(pid);
        if (it == procIdxByPid.end()) {
            const ProcessBasicFacts pf = QueryProcessFactsWin32(pid, hintAppName);
            LockingProcessEntry ent;
            ent.pid = pid;
            ent.startTimeTicks = pf.startTicks;
            ent.exeFullPath = pf.exeFullPath;
            ent.processName = !pf.exeName.empty() ? pf.exeName
                            : (!hintAppName.empty() ? hintAppName : (L"PID " + std::to_wstring(pid)));
            const std::wstring leafLower = PathUtils::ToLower(PathUtils::GetFileName(
                !pf.exeFullPath.empty() ? pf.exeFullPath : ent.processName));
            ent.isSelfProcess = (pid == selfPid);
            ent.isSystemCritical = IsCriticalWindowsProcessName(leafLower, pid);
            ent.isShellProcess = (leafLower == L"explorer.exe");
            ent.isElevatedOrProtected = !ent.isSelfProcess && pf.terminateAccessDenied;
            auto wt = winTitles.find(pid);
            if (wt != winTitles.end()) ent.windowTitle = wt->second;
            procIdxByPid[pid] = outReport.processes.size();
            outReport.processes.push_back(std::move(ent));
            it = procIdxByPid.find(pid);
        }
        LockingProcessEntry& dst = outReport.processes[it->second];
        dst.reasonMask |= addReason;
        if (!hintSvcName.empty() && dst.serviceName.empty()) dst.serviceName = hintSvcName;
        if (!lockedPath.empty()) AddUniqueLockedPath(dst.lockedPaths, lockedPath);
        return dst;
    };

    // 2. Engine 1: Windows Restart Manager (batched in chunks of 64 file paths; exact file paths are resolved in <1ms
    //    by Engine 2 Pass 0 + Engine 3, falling back to Restart Manager binary-split narrowing only for protected services!)
    const RestartMgrApi& rm = GetRestartMgrApi();
    std::vector<std::pair<size_t, size_t>> rmHitBatches;
    auto queryRmSubrange = [&](size_t beg, size_t cnt) -> bool {
        if (!rm.available || cnt == 0 || beg + cnt > rmCandidateFiles.size()) return false;
        std::vector<LPCWSTR> ptrs(cnt);
        for (size_t i = 0; i < cnt; ++i) ptrs[i] = rmCandidateFiles[beg + i].c_str();

        DWORD session = 0xFFFFFFFFu;
        WCHAR sessionKey[kRmSessionKeyLen + 1] = {};
        if (rm.StartSession(&session, 0, sessionKey) != ERROR_SUCCESS) return false;

        bool foundExternal = false;
        if (rm.RegisterResources(session, static_cast<UINT>(cnt), ptrs.data(), 0, nullptr, 0, nullptr) == ERROR_SUCCESS) {
            UINT needed = 0, count = 0;
            DWORD rebootReasons = 0;
            const DWORD rc = rm.GetList(session, &needed, &count, nullptr, &rebootReasons);
            if (rc == ERROR_MORE_DATA && needed > 0 && needed <= 4096u) {
                count = needed;
                std::vector<E2_RM_PROCESS_INFO> infos(count);
                if (rm.GetList(session, &needed, &count, infos.data(), &rebootReasons) == ERROR_SUCCESS) {
                    const std::wstring singlePath = (cnt == 1u) ? rmCandidateFiles[beg] : L"";
                    uint32_t baseReason = kLockReasonOpenHandle;
                    if (!singlePath.empty()) {
                        const std::wstring extLow = PathUtils::GetExtension(singlePath);
                        if (extLow == L".exe") baseReason = kLockReasonRunningExe;
                        else if (IsDllLikeExtension(extLow)) baseReason = kLockReasonLoadedDll;
                    }
                    for (UINT k = 0; k < count; ++k) {
                        const uint32_t pid = infos[k].Process.dwProcessId;
                        if (pid == 0 || pid == selfPid) continue;
                        foundExternal = true;
                        uint32_t reason = baseReason;
                        if (infos[k].ApplicationType == 3) reason |= kLockReasonService;
                        upsertProcess(pid, reason, singlePath, infos[k].strAppName, infos[k].strServiceShortName);
                    }
                }
            }
        }
        rm.EndSession(session);
        return foundExternal;
    };

    if (rm.available && !rmCandidateFiles.empty()) {
        constexpr size_t kBatchSize = 64u;
        for (size_t off = 0; off < rmCandidateFiles.size(); off += kBatchSize) {
            if (cancelFlag && cancelFlag->load()) return;
            const size_t cnt = std::min<size_t>(kBatchSize, rmCandidateFiles.size() - off);
            if (queryRmSubrange(off, cnt) && cnt > 1u) {
                rmHitBatches.emplace_back(off, cnt);
            }
        }
    }

    // 3. Engine 2: NT System Handle Table (catches CWD "cd" locks, ReadDirectoryChangesW folder watchers,
    //    open folder/file handles, and records handle values for non-destructive DUPLICATE_CLOSE_SOURCE unlock!)
    const NtdllApi& nt = GetNtdllApi();
    if (nt.QuerySystemInformation) {
        HANDLE hProbe = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        std::vector<uint8_t> buf(4u * 1024u * 1024u);
        LONG status = 0;
        for (int attempt = 0; attempt < 8; ++attempt) {
            ULONG retLen = 0;
            status = nt.QuerySystemInformation(64u /*SystemExtendedHandleInformation*/, buf.data(), static_cast<ULONG>(buf.size()), &retLen);
            if (status != static_cast<LONG>(0xC0000004L) /*STATUS_INFO_LENGTH_MISMATCH*/) break;
            const size_t nextSz = std::max<size_t>(buf.size() * 2u, static_cast<size_t>(retLen) + 1024u * 1024u);
            if (nextSz > 128u * 1024u * 1024u) break;
            buf.resize(nextSz);
        }

        if (status >= 0 && buf.size() >= sizeof(E2_SYSTEM_EXTENDED_HANDLE_INFORMATION)) {
            const auto* info = reinterpret_cast<const E2_SYSTEM_EXTENDED_HANDLE_INFORMATION*>(buf.data());
            const size_t maxEntries = (buf.size() - offsetof(E2_SYSTEM_EXTENDED_HANDLE_INFORMATION, Handles)) /
                                      sizeof(E2_SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX);
            const size_t nHandles = std::min<size_t>(static_cast<size_t>(info->NumberOfHandles), maxEntries);

            int fileTypeIdx = -1;
            if (hProbe != INVALID_HANDLE_VALUE) {
                for (size_t i = 0; i < nHandles; ++i) {
                    if (info->Handles[i].UniqueProcessId == static_cast<ULONG_PTR>(selfPid) &&
                        info->Handles[i].HandleValue == reinterpret_cast<ULONG_PTR>(hProbe))
                    {
                        fileTypeIdx = static_cast<int>(info->Handles[i].ObjectTypeIndex);
                        break;
                    }
                }
                CloseHandle(hProbe);
                hProbe = INVALID_HANDLE_VALUE;
            }

            if (fileTypeIdx >= 0) {
                struct HandleRec {
                    ULONG_PTR val;
                    PVOID     obj;
                    ULONG     access;
                    ULONG     attrs;
                };
                std::unordered_map<uint32_t, std::vector<HandleRec>> byPid;
                for (size_t i = 0; i < nHandles; ++i) {
                    const auto& hEnt = info->Handles[i];
                    if (static_cast<int>(hEnt.ObjectTypeIndex) != fileTypeIdx) continue;
                    const uint32_t pid = static_cast<uint32_t>(hEnt.UniqueProcessId);
                    if (pid <= 4u || pid == selfPid) continue;
                    byPid[pid].push_back({ hEnt.HandleValue, hEnt.Object, hEnt.GrantedAccess, hEnt.HandleAttributes });
                }

                std::unordered_map<uint32_t, uint64_t> pidCreateTicks;
                pidCreateTicks.reserve(byPid.size());
                std::vector<std::pair<uint32_t, std::vector<HandleRec>*>> pidList;
                pidList.reserve(byPid.size());
                for (auto& kv : byPid) {
                    uint64_t ct = 0u;
                    if (HANDLE hQt = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, kv.first)) {
                        FILETIME ftCreate = {}, ftExit = {}, ftKernel = {}, ftUser = {};
                        if (GetProcessTimes(hQt, &ftCreate, &ftExit, &ftKernel, &ftUser)) {
                            ct = (static_cast<uint64_t>(ftCreate.dwHighDateTime) << 32) | ftCreate.dwLowDateTime;
                        }
                        CloseHandle(hQt);
                    }
                    pidCreateTicks[kv.first] = ct;
                    pidList.emplace_back(kv.first, &kv.second);
                }
                std::sort(pidList.begin(), pidList.end(), [&](const auto& a, const auto& b) {
                    const bool aKnown = procIdxByPid.find(a.first) != procIdxByPid.end();
                    const bool bKnown = procIdxByPid.find(b.first) != procIdxByPid.end();
                    if (aKnown != bKnown) return aKnown > bKnown;
                    const uint64_t ta = pidCreateTicks[a.first];
                    const uint64_t tb = pidCreateTicks[b.first];
                    if (ta != tb) return ta > tb;
                    return a.first > b.first;
                });

                PipeSafeHandleResolver resolver;
                std::unordered_map<PVOID, std::pair<std::wstring, bool>> kernelObjCache;
                std::unordered_set<uint32_t> unopenablePids;
                const ULONGLONG pass1Deadline = GetTickCount64() + 600ULL;
                ULONGLONG deadlineTick = pass1Deadline;

                auto recordMatchedHandle = [&](uint32_t pid,
                                               ULONG_PTR hVal,
                                               ULONG access,
                                               ULONG hAttrs,
                                               const std::wstring& resolvedPath,
                                               bool isDirObj)
                {
                    if (resolvedPath.empty()) return;
                    bool matchedDir = false;
                    if (!normTargets.Matches(resolvedPath, &matchedDir)) return;

                    std::wstring displayLocked = normTargets.MapCanonicalToDisplay(resolvedPath);
                    if (!isDirObj && !matchedDir) {
                        const DWORD fa = GetFileAttributesW(PathUtils::ToExtendedWinPath(resolvedPath).c_str());
                        if (fa != INVALID_FILE_ATTRIBUTES && (fa & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                            isDirObj = true;
                        }
                    }
                    const bool finalIsDir = (isDirObj || matchedDir);
                    const uint32_t rBit = finalIsDir ? kLockReasonFolderOrCwd : kLockReasonOpenHandle;
                    LockingProcessEntry& ent = upsertProcess(pid, rBit, displayLocked);
                    LockedHandleDescriptor hd;
                    hd.handleValue = static_cast<uintptr_t>(hVal);
                    hd.grantedAccess = static_cast<uint32_t>(access);
                    hd.handleAttributes = static_cast<uint32_t>(hAttrs);
                    hd.path = displayLocked;
                    hd.isDirectory = finalIsDir;
                    AddUniqueLockedHandle(ent.openHandles, hd);
                };

                // `passKind`:
                //   0 = ALL handles of Restart-Manager-confirmed locking PIDs (resolves surgical-close `openHandles` first!)
                //   1 = Zero-pipe-risk CWD / traverse / read-attributes handles (`(access & 0x3) == 0`) across all PIDs
                //   2 = Directory-watch handles (`ReadDirectoryChangesW` / `FILE_LIST_DIRECTORY`) across all PIDs
                //   3 = Remaining read/write file handles across non-RM PIDs
                auto runPassForPid = [&](uint32_t pid, const std::vector<HandleRec>& handles, int passKind) {
                    if (unopenablePids.find(pid) != unopenablePids.end()) return;
                    auto batch = std::make_shared<std::vector<BatchHandleItem>>();
                    batch->reserve(handles.size());

                    for (const auto& hv : handles) {
                        const bool isPureDirOrAttr = ((hv.access & 0x3u) == 0u) || ((hv.access & 0x001F01FFu) == 0x00000080u);
                        const bool isDirListOrWatch = !isPureDirOrAttr && ((hv.access & 0x3u) == 0x1u) && ((hv.access & 0x00020000u) == 0u);
                        if (passKind == 1 && !isPureDirOrAttr) continue;
                        if (passKind == 2 && !isDirListOrWatch) continue;
                        if (passKind == 3 && (isPureDirOrAttr || isDirListOrWatch)) continue;

                        auto cIt = kernelObjCache.find(hv.obj);
                        if (cIt != kernelObjCache.end()) {
                            recordMatchedHandle(pid, hv.val, hv.access, hv.attrs, cIt->second.first, cIt->second.second);
                            continue;
                        }
                        BatchHandleItem bi;
                        bi.handleVal = hv.val;
                        bi.kernelObj = hv.obj;
                        bi.access = hv.access;
                        bi.handleAttrs = hv.attrs;
                        batch->push_back(std::move(bi));
                    }
                    if (batch->empty()) return;

                    std::unordered_map<ULONG, int> pidAccessStalls;
                    size_t curStart = 0;
                    int stallsInPid = 0;
                    while (curStart < batch->size() && stallsInPid < 4 &&
                           !resolver.IsExhausted() && GetTickCount64() <= deadlineTick)
                    {
                        bool procFailed = false;
                        size_t stalledAt = SIZE_MAX;
                        const bool finished = resolver.RunPidSubrange(
                            pid, batch, curStart, batch->size(),
                            &normTargets, &dosMapper, deadlineTick,
                            procFailed, stalledAt);
                        if (procFailed) {
                            unopenablePids.insert(pid);
                            return;
                        }
                        const size_t harvestEnd = finished ? batch->size() : std::min(stalledAt, batch->size());
                        for (size_t k = curStart; k < harvestEnd; ++k) {
                            const auto& bi = (*batch)[k];
                            if (!bi.processed) continue;
                            kernelObjCache.emplace(bi.kernelObj, std::make_pair(bi.resolvedPath, bi.isDir));
                            recordMatchedHandle(pid, bi.handleVal, bi.access, bi.handleAttrs, bi.resolvedPath, bi.isDir);
                        }
                        if (finished) break;

                        // Pipe stall occurred at `stalledAt` — skip ONLY that stalled handle and continue checking
                        // the remaining handles of this PID!
                        ++stallsInPid;
                        if (stalledAt < batch->size()) {
                            const ULONG stalledAcc = (*batch)[stalledAt].access;
                            kernelObjCache.emplace((*batch)[stalledAt].kernelObj, std::make_pair(std::wstring(), false));
                            if (++pidAccessStalls[stalledAcc] >= 2) {
                                // Skip remaining handles with this exact stalled pipe access mask in this PID
                                for (size_t k = stalledAt + 1; k < batch->size(); ++k) {
                                    if ((*batch)[k].access == stalledAcc) {
                                        (*batch)[k].skip = true;
                                    }
                                }
                            }
                            curStart = stalledAt + 1;
                        } else {
                            break;
                        }
                    }
                };

                // Pass 0: Resolve all handles of Restart-Manager-confirmed locking PIDs FIRST (never starved by unrelated pipes!)
                std::unordered_set<uint32_t> rmConfirmedPids;
                for (const auto& kv : procIdxByPid) rmConfirmedPids.insert(kv.first);
                deadlineTick = GetTickCount64() + 500ULL;
                for (auto& pItem : pidList) {
                    if (rmConfirmedPids.find(pItem.first) == rmConfirmedPids.end()) continue;
                    if (resolver.IsExhausted() || (cancelFlag && cancelFlag->load()) || GetTickCount64() > deadlineTick) break;
                    runPassForPid(pItem.first, *pItem.second, /*passKind=*/0);
                }

                // Pass 1a & 1b: Directory CWD / traverse / ReadDirectoryChangesW handles (only directory handles can lock folders!)
                if (!normTargets.dirPrefixes.empty()) {
                    deadlineTick = GetTickCount64() + 200ULL;
                    for (auto& pItem : pidList) {
                        if (rmConfirmedPids.find(pItem.first) != rmConfirmedPids.end()) continue;
                        if (resolver.IsExhausted() || (cancelFlag && cancelFlag->load()) || GetTickCount64() > deadlineTick) break;
                        runPassForPid(pItem.first, *pItem.second, /*passKind=*/1);
                    }

                    deadlineTick = GetTickCount64() + 250ULL;
                    for (auto& pItem : pidList) {
                        if (rmConfirmedPids.find(pItem.first) != rmConfirmedPids.end()) continue;
                        if (resolver.IsExhausted() || (cancelFlag && cancelFlag->load()) || GetTickCount64() > deadlineTick) break;
                        runPassForPid(pItem.first, *pItem.second, /*passKind=*/2);
                    }
                }

                // Pass 2: Remaining read/write file handles across non-RM PIDs (backs up Restart Manager RPC session limits,
                // >4,096-file folders, subst/VHD aliases, and UNC paths; bounded to 250ms with CancelSynchronousIo pipe recovery)
                deadlineTick = GetTickCount64() + 250ULL;
                for (auto& pItem : pidList) {
                    if (rmConfirmedPids.find(pItem.first) != rmConfirmedPids.end()) continue;
                    if (resolver.IsExhausted() || (cancelFlag && cancelFlag->load()) || GetTickCount64() > deadlineTick) break;
                    runPassForPid(pItem.first, *pItem.second, /*passKind=*/3);
                }
            }
        }
        if (hProbe != INVALID_HANDLE_VALUE) CloseHandle(hProbe);
    }

    // 4. Engine 3: Process Image (.exe), Loaded DLLs (.dll/.ocx/.pyd) & Memory-Mapped Data Sections
    //    (Runs unconditionally even when rstrtmgr.dll is present, so kLockReasonLoadedDll, CreateFileMappingW
    //    mapped data files after CloseHandle(hFile), and folders with >4,096 files are 100% detected!)
    {
        wchar_t winDirBuf[MAX_PATH] = {};
        const UINT winDirLen = GetWindowsDirectoryW(winDirBuf, MAX_PATH);
        std::wstring winDirPrefixLower;
        if (winDirLen > 0 && winDirLen < MAX_PATH) {
            winDirPrefixLower = PathUtils::EnsureTrailingSlash(PathUtils::ToLower(std::wstring(winDirBuf, winDirLen)));
        }
        const bool canCacheSysAslrImages = !winDirPrefixLower.empty() &&
                                           !normTargets.Matches(winDirPrefixLower + L"System32\\ntdll.dll");
        std::unordered_set<PVOID> nonMatchingSysAslrBases;
        nonMatchingSysAslrBases.reserve(512);

        struct SnapProcItem {
            uint32_t     pid = 0;
            uint64_t     createTicks = 0;
            std::wstring exeLeaf;
            bool         knownHit = false;
            bool         sysCritical = false;
        };
        std::vector<SnapProcItem> snapProcs;
        snapProcs.reserve(512);

        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe = {};
            pe.dwSize = sizeof(pe);
            if (Process32FirstW(hSnap, &pe)) {
                do {
                    const uint32_t pid = pe.th32ProcessID;
                    if (pid <= 4u || pid == selfPid) continue;
                    SnapProcItem sp;
                    sp.pid = pid;
                    sp.exeLeaf = pe.szExeFile;
                    sp.knownHit = (procIdxByPid.find(pid) != procIdxByPid.end());
                    sp.sysCritical = IsCriticalWindowsProcessName(PathUtils::ToLower(sp.exeLeaf), pid);
                    if (HANDLE hQt = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
                        FILETIME ftCreate = {}, ftExit = {}, ftKernel = {}, ftUser = {};
                        if (GetProcessTimes(hQt, &ftCreate, &ftExit, &ftKernel, &ftUser)) {
                            sp.createTicks = (static_cast<uint64_t>(ftCreate.dwHighDateTime) << 32) | ftCreate.dwLowDateTime;
                        }
                        CloseHandle(hQt);
                    }
                    snapProcs.push_back(std::move(sp));
                } while (Process32NextW(hSnap, &pe));
            }
            CloseHandle(hSnap);
        }

        // Prioritize: (1) Known RM/Engine-2 locking PIDs, (2) non-critical user processes newest-creation-time first, (3) system processes
        std::sort(snapProcs.begin(), snapProcs.end(), [](const SnapProcItem& a, const SnapProcItem& b) {
            if (a.knownHit != b.knownHit) return a.knownHit > b.knownHit;
            if (a.sysCritical != b.sysCritical) return a.sysCritical < b.sysCritical;
            if (a.createTicks != b.createTicks) return a.createTicks > b.createTicks;
            return a.pid > b.pid;
        });

        alignas(8) uint8_t vmNameBuf[4096] = {};
        wchar_t imgBuf[2048] = {};
        const ULONGLONG vmDeadlineTick = GetTickCount64() + 450ULL;

        for (size_t idx = 0; idx < snapProcs.size(); ++idx) {
            if (cancelFlag && cancelFlag->load()) break;
            const auto& sp = snapProcs[idx];
            const uint32_t pid = sp.pid;

            HANDLE hVm = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
            if (!hVm) hVm = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
            if (!hVm) hVm = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);

            if (hVm) {
                DWORD imgSz = static_cast<DWORD>(std::size(imgBuf) - 1);
                if (QueryFullProcessImageNameW(hVm, 0, imgBuf, &imgSz) && imgSz > 0) {
                    std::wstring exeFull = PathUtils::StripExtendedWinPrefix(std::wstring_view(imgBuf, imgSz));
                    if (!exeFull.empty() && normTargets.Matches(exeFull)) {
                        upsertProcess(pid, kLockReasonRunningExe, normTargets.MapCanonicalToDisplay(exeFull), sp.exeLeaf);
                    }
                }
            }

            // Always walk full VADs unconditionally (with zero ASLR address skipping) for known hits and the 4 newest user processes;
            // enforce the 450ms VM budget (including inside large browser VAD walks!) for older background processes.
            const bool isPriorityProc = sp.knownHit || idx < 4u;
            const bool allowVmWalk = isPriorityProc || (GetTickCount64() < vmDeadlineTick);
            bool walkedVm = false;
            if (allowVmWalk && nt.QueryVirtualMemory && hVm) {
                MEMORY_BASIC_INFORMATION mbi = {};
                const uint8_t* addr = nullptr;
                PVOID lastAllocBase = reinterpret_cast<PVOID>(static_cast<uintptr_t>(-1));
                for (size_t step = 0; step < 16384u; ++step) {
                    if (!isPriorityProc && (step & 255u) == 255u && GetTickCount64() >= vmDeadlineTick) break;
                    if (VirtualQueryEx(hVm, addr, &mbi, sizeof(mbi)) != sizeof(mbi)) break;
                    walkedVm = true;
                    if (mbi.State == MEM_COMMIT &&
                        (mbi.Type == MEM_IMAGE || mbi.Type == MEM_MAPPED) &&
                        mbi.AllocationBase != nullptr &&
                        mbi.AllocationBase != lastAllocBase)
                    {
                        lastAllocBase = mbi.AllocationBase;
                        const bool isHighAslrImg = !isPriorityProc && (mbi.Type == MEM_IMAGE) &&
                            (sizeof(uintptr_t) < 8 || reinterpret_cast<uintptr_t>(mbi.AllocationBase) >= 0x00007FF000000000ULL);
                        if (!(canCacheSysAslrImages && isHighAslrImg &&
                              nonMatchingSysAslrBases.find(mbi.AllocationBase) != nonMatchingSysAslrBases.end()))
                        {
                            SIZE_T retLen = 0;
                            const LONG stVm = nt.QueryVirtualMemory(
                                hVm, mbi.BaseAddress, 2 /*MemoryMappedFilenameInformation*/,
                                vmNameBuf, sizeof(vmNameBuf) - sizeof(wchar_t), &retLen);
                            if (stVm >= 0 && retLen >= sizeof(E2_UNICODE_STRING)) {
                                const auto* us = reinterpret_cast<const E2_UNICODE_STRING*>(vmNameBuf);
                                if (us->Buffer && us->Length > 0) {
                                    std::wstring dosMapped;
                                    std::wstring_view ntSv(us->Buffer, us->Length / sizeof(wchar_t));
                                    if (dosMapper.ConvertNtPathToDos(ntSv, dosMapped)) {
                                        if (normTargets.Matches(dosMapped)) {
                                            const std::wstring dispPath = normTargets.MapCanonicalToDisplay(dosMapped);
                                            const std::wstring extLow = PathUtils::GetExtension(dispPath);
                                            uint32_t bit = kLockReasonOpenHandle;
                                            if (extLow == L".exe") bit = kLockReasonRunningExe;
                                            else if (mbi.Type == MEM_IMAGE || IsDllLikeExtension(extLow)) bit = kLockReasonLoadedDll;
                                            upsertProcess(pid, bit, dispPath, sp.exeLeaf);
                                        } else if (canCacheSysAslrImages && isHighAslrImg &&
                                                   dosMapped.size() > winDirPrefixLower.size() &&
                                                   _wcsnicmp(dosMapped.c_str(), winDirPrefixLower.c_str(), winDirPrefixLower.size()) == 0)
                                        {
                                            nonMatchingSysAslrBases.insert(mbi.AllocationBase);
                                        }
                                    }
                                }
                            }
                        }
                    }
                    if (mbi.RegionSize == 0) break;
                    const uint8_t* nextAddr = static_cast<const uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;
                    if (nextAddr <= addr) break;
                    addr = nextAddr;
                }
            }
            if (hVm) CloseHandle(hVm);

            // Fallback to Toolhelp module snapshot if NtQueryVirtualMemory was unavailable or could not walk VM
            if (!walkedVm && (!rm.available || sp.knownHit)) {
                HANDLE hMod = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
                if (hMod != INVALID_HANDLE_VALUE) {
                    MODULEENTRY32W me = {};
                    me.dwSize = sizeof(me);
                    if (Module32FirstW(hMod, &me)) {
                        do {
                            std::wstring modPath = PathUtils::StripExtendedWinPrefix(me.szExePath);
                            if (!modPath.empty() && normTargets.Matches(modPath)) {
                                const std::wstring extLow = PathUtils::GetExtension(modPath);
                                const uint32_t bit = (extLow == L".exe") ? kLockReasonRunningExe : kLockReasonLoadedDll;
                                upsertProcess(pid, bit, normTargets.MapCanonicalToDisplay(modPath), sp.exeLeaf);
                            }
                        } while (Module32NextW(hMod, &me));
                    }
                    CloseHandle(hMod);
                }
            }
        }
    }

    // 4B. Protected-Service Path Narrowing Fallback: if Restart Manager detected a locking PID whose handle table / VADs
    //     could not be opened (e.g., an elevated PPL/SYSTEM service), narrow its exact file path via Restart Manager binary split.
    if (!rmHitBatches.empty()) {
        bool anyMissingPath = false;
        for (const auto& pe : outReport.processes) {
            if (pe.lockedPaths.empty()) { anyMissingPath = true; break; }
        }
        if (anyMissingPath) {
            const ULONGLONG rmNarrowDeadline = GetTickCount64() + 1500ULL;
            size_t narrowedLockedFiles = 0;
            for (const auto& [off, cnt] : rmHitBatches) {
                if (narrowedLockedFiles >= 16u || GetTickCount64() >= rmNarrowDeadline) break;
                std::vector<std::pair<size_t, size_t>> splitStack;
                const size_t mid0 = cnt / 2u;
                splitStack.emplace_back(off + mid0, cnt - mid0);
                splitStack.emplace_back(off, mid0);
                while (!splitStack.empty() && narrowedLockedFiles < 16u && GetTickCount64() < rmNarrowDeadline) {
                    if (cancelFlag && cancelFlag->load()) return;
                    const auto [sBeg, sCnt] = splitStack.back();
                    splitStack.pop_back();
                    if (sCnt == 0 || !queryRmSubrange(sBeg, sCnt)) continue;
                    if (sCnt == 1u) {
                        ++narrowedLockedFiles;
                    } else {
                        const size_t half = sCnt / 2u;
                        splitStack.emplace_back(sBeg + half, sCnt - half);
                        splitStack.emplace_back(sBeg, half);
                    }
                }
            }
        }
    }

    // 5. Engine 4A: Check if this Explorer2 window itself holds any target path (Preview / MediaPlayer / Active Dir)
    if (selfHold) {
        auto checkSelf = [&](const std::wstring& held, uint32_t extraBit) {
            if (!held.empty() && normTargets.Matches(held)) {
                upsertProcess(selfPid, kLockReasonSelfPreview | extraBit, held, L"Explorer2.exe");
            }
        };
        checkSelf(selfHold->mediaPath, kLockReasonOpenHandle);
        checkSelf(selfHold->previewPath, kLockReasonOpenHandle);
        checkSelf(selfHold->activeFolderPath, kLockReasonFolderOrCwd);
    }

    std::unordered_set<std::wstring> processLockedPathKeys;
    for (const auto& p : outReport.processes) {
        for (const auto& lp : p.lockedPaths) {
            processLockedPathKeys.insert(NormalizeMatchKey(lp));
        }
    }

    // 6. Engine 4B: Check Read-Only attributes & NTFS DELETE ACL / Administrator blocks
    size_t roReported = 0, aclReported = 0;
    for (const auto& item : attrCheckItems) {
        const std::wstring& p = item.first;
        const DWORD attrs = item.second;
        const bool isDir = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;

        if (!isDir && (attrs & FILE_ATTRIBUTE_READONLY) != 0) {
            if (roReported++ < 32u) {
                PermissionBlockerEntry b;
                b.path = p;
                b.isReadOnlyAttr = true;
                b.detail = L"FILE_ATTRIBUTE_READONLY";
                outReport.permissionBlockers.push_back(std::move(b));
            }
            if (aclReported < 32u) {
                HANDLE hWa = CreateFileW(
                    PathUtils::ToExtendedWinPath(p).c_str(),
                    FILE_WRITE_ATTRIBUTES,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr,
                    OPEN_EXISTING,
                    FILE_FLAG_OPEN_REPARSE_POINT,
                    nullptr);
                if (hWa != INVALID_HANDLE_VALUE) {
                    CloseHandle(hWa);
                } else if (GetLastError() == ERROR_ACCESS_DENIED &&
                           processLockedPathKeys.find(NormalizeMatchKey(p)) == processLockedPathKeys.end())
                {
                    ++aclReported;
                    PermissionBlockerEntry bAcl;
                    bAcl.path = p;
                    bAcl.win32Error = ERROR_ACCESS_DENIED;
                    bAcl.isAclAccessDenied = true;
                    bAcl.detail = L"ERROR_ACCESS_DENIED (5) — NTFS DACL / Administrator";
                    outReport.permissionBlockers.push_back(std::move(bAcl));
                }
            }
            continue;
        }

        if (aclReported < 32u && roReported + aclReported < 128u) {
            HANDLE hDel = CreateFileW(
                PathUtils::ToExtendedWinPath(p).c_str(),
                DELETE,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr,
                OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                nullptr);
            if (hDel != INVALID_HANDLE_VALUE) {
                CloseHandle(hDel);
            } else {
                const DWORD err = GetLastError();
                if (err == ERROR_ACCESS_DENIED &&
                    processLockedPathKeys.find(NormalizeMatchKey(p)) == processLockedPathKeys.end())
                {
                    ++aclReported;
                    PermissionBlockerEntry b;
                    b.path = p;
                    b.win32Error = err;
                    b.isAclAccessDenied = true;
                    b.detail = L"ERROR_ACCESS_DENIED (5) — NTFS DACL / Administrator";
                    outReport.permissionBlockers.push_back(std::move(b));
                }
            }
        }
    }

#else
    // ========================================================================
    // Linux Implementation: recursive subtree walk + /proc/[pid]/{fd,cwd,exe,maps}
    // ========================================================================
    size_t aclReported = 0;
    auto recordLinuxPermBlocker = [&](const std::wstring& p, const wchar_t* why) {
        if (aclReported++ >= 32u) return;
        PermissionBlockerEntry b;
        b.path = p;
        b.win32Error = ERROR_ACCESS_DENIED;
        b.isAclAccessDenied = true;
        b.detail = why;
        outReport.permissionBlockers.push_back(std::move(b));
    };

    for (const auto& tgt : cleanTargets) {
        if (cancelFlag && cancelFlag->load()) return;
        const std::string nat = Compat::NativePath(tgt);
        struct stat st = {};
        if (::lstat(nat.c_str(), &st) != 0) continue;

        const std::wstring parentW = PathUtils::GetParentPath(tgt);
        if (!parentW.empty() && ::access(Compat::NativePath(parentW).c_str(), W_OK | X_OK) != 0 && errno == EACCES) {
            recordLinuxPermBlocker(tgt, L"EACCES (parent directory lacks write permission)");
        }

        const std::wstring dispKey = NormalizeMatchKey(tgt);
        std::wstring canonKey;
        if (!S_ISLNK(st.st_mode)) {
            char realBuf[PATH_MAX] = {};
            if (::realpath(nat.c_str(), realBuf)) {
                canonKey = NormalizeMatchKey(PathUtils::Utf8ToWide(realBuf));
            }
        }

        if (S_ISDIR(st.st_mode)) {
            ++outReport.scannedDirs;
            normTargets.dirPrefixes.push_back(dispKey);
            if (!canonKey.empty() && canonKey != dispKey) {
                normTargets.dirPrefixes.push_back(canonKey);
            }

            // Iterative BFS walk (never follows symlinks)
            std::vector<std::wstring> dirStack = { tgt };
            size_t visited = 0;
            while (!dirStack.empty() && visited < 16384u) {
                if (cancelFlag && cancelFlag->load()) return;
                std::wstring curDir = std::move(dirStack.back());
                dirStack.pop_back();
                const std::string curNat = Compat::NativePath(curDir);
                if (::access(curNat.c_str(), W_OK | X_OK) != 0 && errno == EACCES) {
                    recordLinuxPermBlocker(curDir, L"EACCES (directory lacks write/traverse permission)");
                }
                DIR* d = ::opendir(curNat.c_str());
                if (!d) continue;
                while (struct dirent* de = ::readdir(d)) {
                    if (std::strcmp(de->d_name, ".") == 0 || std::strcmp(de->d_name, "..") == 0) continue;
                    ++visited;
                    std::wstring childW = curDir;
                    if (!childW.empty() && childW.back() != L'/') childW.push_back(L'/');
                    childW += PathUtils::Utf8ToWide(de->d_name);

                    struct stat cst = {};
                    if (::lstat(Compat::NativePath(childW).c_str(), &cst) != 0) continue;
                    if (S_ISDIR(cst.st_mode)) {
                        ++outReport.scannedDirs;
                        if (dirStack.size() < 4096u) dirStack.push_back(std::move(childW));
                    } else {
                        ++outReport.scannedFiles;
                    }
                }
                ::closedir(d);
            }
        } else {
            ++outReport.scannedFiles;
            normTargets.exactKeys.insert(dispKey);
            if (!canonKey.empty()) normTargets.exactKeys.insert(canonKey);
        }
    }

    const pid_t selfPid = ::getpid();
    std::unordered_map<uint32_t, size_t> procIdxByPid;
    auto readProcLink = [](const std::string& linkPath) -> std::wstring {
        char buf[4096] = {};
        const ssize_t n = ::readlink(linkPath.c_str(), buf, sizeof(buf) - 1);
        if (n <= 0) return L"";
        std::string s(buf, static_cast<size_t>(n));
        const size_t delPos = s.rfind(" (deleted)");
        if (delPos != std::string::npos && delPos + 10u == s.size()) s.resize(delPos);
        return PathUtils::Utf8ToWide(s);
    };

    if (DIR* procDir = ::opendir("/proc")) {
        while (struct dirent* de = ::readdir(procDir)) {
            if (cancelFlag && cancelFlag->load()) break;
            if (de->d_name[0] < '1' || de->d_name[0] > '9') continue;
            char* endp = nullptr;
            const unsigned long pidUl = std::strtoul(de->d_name, &endp, 10);
            if (!endp || *endp != '\0' || pidUl == 0) continue;
            const uint32_t pid = static_cast<uint32_t>(pidUl);
            if (static_cast<pid_t>(pid) == selfPid) continue;

            const std::string base = std::string("/proc/") + de->d_name;
            const std::wstring exePath = readProcLink(base + "/exe");
            const std::wstring cwdPath = readProcLink(base + "/cwd");

            auto addHit = [&](uint32_t reasonBit, const std::wstring& lpath) {
                auto it = procIdxByPid.find(pid);
                if (it == procIdxByPid.end()) {
                    LockingProcessEntry ent;
                    ent.pid = pid;
                    ent.startTimeTicks = QueryLinuxProcessStartTicks(pid);
                    ent.exeFullPath = exePath;
                    ent.processName = !exePath.empty() ? PathUtils::GetFileName(exePath) : (L"pid-" + std::to_wstring(pid));
                    if (FILE* f = std::fopen((base + "/comm").c_str(), "r")) {
                        char comm[128] = {};
                        if (std::fgets(comm, sizeof(comm), f)) {
                            size_t cl = std::strlen(comm);
                            while (cl > 0 && (comm[cl - 1] == '\n' || comm[cl - 1] == '\r')) --cl;
                            if (cl > 0 && ent.exeFullPath.empty()) ent.processName = PathUtils::Utf8ToWide(std::string(comm, cl));
                        }
                        std::fclose(f);
                    }
                    ClassifyLinuxProcessRole(ent.processName, pid, ent.isSystemCritical, ent.isShellProcess);
                    ent.isElevatedOrProtected = (::kill(static_cast<pid_t>(pid), 0) != 0 && errno == EPERM);
                    procIdxByPid[pid] = outReport.processes.size();
                    outReport.processes.push_back(std::move(ent));
                    it = procIdxByPid.find(pid);
                }
                outReport.processes[it->second].reasonMask |= reasonBit;
                AddUniqueLockedPath(outReport.processes[it->second].lockedPaths, lpath);
            };

            if (!exePath.empty() && normTargets.Matches(exePath)) addHit(kLockReasonRunningExe, exePath);
            if (!cwdPath.empty() && normTargets.Matches(cwdPath)) addHit(kLockReasonFolderOrCwd, cwdPath);

            if (DIR* fdDir = ::opendir((base + "/fd").c_str())) {
                while (struct dirent* fde = ::readdir(fdDir)) {
                    if (fde->d_name[0] == '.') continue;
                    const std::wstring fdTarget = readProcLink(base + "/fd/" + fde->d_name);
                    bool matchedDir = false;
                    if (!fdTarget.empty() && normTargets.Matches(fdTarget, &matchedDir)) {
                        if (!matchedDir) {
                            struct stat fst = {};
                            if (::stat(Compat::NativePath(fdTarget).c_str(), &fst) == 0 && S_ISDIR(fst.st_mode)) {
                                matchedDir = true;
                            }
                        }
                        addHit(matchedDir ? kLockReasonFolderOrCwd : kLockReasonOpenHandle, fdTarget);
                    }
                }
                ::closedir(fdDir);
            }

            // Scan /proc/[pid]/maps for mmap()ed data files and dlopen()ed .so shared libraries
            if (FILE* mf = std::fopen((base + "/maps").c_str(), "r")) {
                char line[4096] = {};
                std::unordered_set<std::string> seenMapPaths;
                while (std::fgets(line, sizeof(line), mf)) {
                    // Format: start-end perms offset dev inode pathname
                    const char* p = line;
                    int spaces = 0;
                    while (*p && spaces < 5) {
                        while (*p && *p != ' ' && *p != '\t') ++p;
                        while (*p == ' ' || *p == '\t') ++p;
                        ++spaces;
                    }
                    if (*p != '/') continue;
                    std::string mpath(p);
                    while (!mpath.empty() && (mpath.back() == '\n' || mpath.back() == '\r')) mpath.pop_back();
                    const size_t delPos = mpath.rfind(" (deleted)");
                    if (delPos != std::string::npos && delPos + 10u == mpath.size()) mpath.resize(delPos);
                    if (mpath.empty() || !seenMapPaths.insert(mpath).second) continue;

                    const std::wstring mapW = PathUtils::Utf8ToWide(mpath);
                    if (!mapW.empty() && normTargets.Matches(mapW)) {
                        uint32_t rBit = kLockReasonOpenHandle;
                        if (!exePath.empty() && NormalizeMatchKey(mapW) == NormalizeMatchKey(exePath)) {
                            rBit = kLockReasonRunningExe;
                        } else if (IsLinuxSharedObjectPath(mapW)) {
                            rBit = kLockReasonLoadedDll;
                        }
                        addHit(rBit, mapW);
                    }
                }
                std::fclose(mf);
            }
        }
        ::closedir(procDir);
    }

    if (selfHold) {
        auto checkSelf = [&](const std::wstring& held, uint32_t bit) {
            if (!held.empty() && normTargets.Matches(held)) {
                const uint32_t spid = static_cast<uint32_t>(selfPid);
                auto it = procIdxByPid.find(spid);
                if (it == procIdxByPid.end()) {
                    LockingProcessEntry ent;
                    ent.pid = spid;
                    ent.processName = L"explorer2";
                    ent.isSelfProcess = true;
                    procIdxByPid[spid] = outReport.processes.size();
                    outReport.processes.push_back(std::move(ent));
                    it = procIdxByPid.find(spid);
                }
                outReport.processes[it->second].reasonMask |= (kLockReasonSelfPreview | bit);
                AddUniqueLockedPath(outReport.processes[it->second].lockedPaths, held);
            }
        };
        checkSelf(selfHold->mediaPath, kLockReasonOpenHandle);
        checkSelf(selfHold->previewPath, kLockReasonOpenHandle);
        checkSelf(selfHold->activeFolderPath, kLockReasonFolderOrCwd);
    }
#endif

    std::sort(outReport.processes.begin(), outReport.processes.end(), [](const LockingProcessEntry& a, const LockingProcessEntry& b) {
        auto rank = [](const LockingProcessEntry& e) {
            if (e.isSystemCritical) return 3;
            if (e.isShellProcess)   return 2;
            if (e.isSelfProcess)    return 1;
            return 0;
        };
        if (rank(a) != rank(b)) return rank(a) < rank(b);
        return _wcsicmp(a.processName.c_str(), b.processName.c_str()) < 0;
    });

    outReport.elapsedMs = GetTickCount64() - tStart;
}

// ============================================================================
// Non-Destructive Surgical Handle Unlock (DUPLICATE_CLOSE_SOURCE)
// ============================================================================
bool CloseRemoteFileHandles(
    const std::vector<RemoteHandleCloseRequest>& requests,
    size_t* outClosedCount,
    bool* outAnyAccessDenied,
    std::vector<RemoteHandleCloseRequest>* outElevationNeeded)
{
    if (outClosedCount) *outClosedCount = 0u;
    if (outAnyAccessDenied) *outAnyAccessDenied = false;
    if (outElevationNeeded) outElevationNeeded->clear();
    if (requests.empty()) return true;

#ifdef _WIN32
    const uint32_t selfPid = GetCurrentProcessId();
    DosDevicePathMapper dosMapper;
    dosMapper.Initialize();
    PipeSafeHandleResolver resolver;

    // Take a fresh kernel handle-table snapshot so we can double-verify live ObjectTypeIndex & OBJ_PROTECT_CLOSE (0x1)
    // right before closing, preventing STATUS_HANDLE_NOT_CLOSABLE (0xC0000235) even if handle flags changed!
    std::unordered_map<uint64_t, uint32_t> liveFileHandleAttrs;
    bool haveLiveTable = false;
    if (const NtQuerySystemInformationFn ntQuery = GetNtdllApi().QuerySystemInformation) {
        HANDLE hProbe = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        std::vector<uint8_t> buf(4u * 1024u * 1024u);
        LONG status = 0;
        for (int attempt = 0; attempt < 8; ++attempt) {
            ULONG retLen = 0;
            status = ntQuery(64u, buf.data(), static_cast<ULONG>(buf.size()), &retLen);
            if (status != static_cast<LONG>(0xC0000004L)) break;
            const size_t nextSz = std::max<size_t>(buf.size() * 2u, static_cast<size_t>(retLen) + 1024u * 1024u);
            if (nextSz > 128u * 1024u * 1024u) break;
            buf.resize(nextSz);
        }
        if (status >= 0 && buf.size() >= sizeof(E2_SYSTEM_EXTENDED_HANDLE_INFORMATION)) {
            const auto* info = reinterpret_cast<const E2_SYSTEM_EXTENDED_HANDLE_INFORMATION*>(buf.data());
            const size_t maxEntries = (buf.size() - offsetof(E2_SYSTEM_EXTENDED_HANDLE_INFORMATION, Handles)) /
                                      sizeof(E2_SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX);
            const size_t nHandles = std::min<size_t>(static_cast<size_t>(info->NumberOfHandles), maxEntries);
            int fileTypeIdx = -1;
            if (hProbe != INVALID_HANDLE_VALUE) {
                for (size_t i = 0; i < nHandles; ++i) {
                    if (info->Handles[i].UniqueProcessId == static_cast<ULONG_PTR>(selfPid) &&
                        info->Handles[i].HandleValue == reinterpret_cast<ULONG_PTR>(hProbe))
                    {
                        fileTypeIdx = static_cast<int>(info->Handles[i].ObjectTypeIndex);
                        break;
                    }
                }
            }
            if (fileTypeIdx >= 0) {
                haveLiveTable = true;
                std::unordered_set<uint32_t> wantedPids;
                for (const auto& r : requests) wantedPids.insert(r.pid);
                for (size_t i = 0; i < nHandles; ++i) {
                    const auto& he = info->Handles[i];
                    if (static_cast<int>(he.ObjectTypeIndex) != fileTypeIdx) continue;
                    const uint32_t pid = static_cast<uint32_t>(he.UniqueProcessId);
                    if (wantedPids.find(pid) == wantedPids.end()) continue;
                    if ((he.HandleValue & 0xFFFFFFFFULL) == he.HandleValue) {
                        const uint64_t key = (static_cast<uint64_t>(pid) << 32) | static_cast<uint64_t>(he.HandleValue);
                        liveFileHandleAttrs[key] = static_cast<uint32_t>(he.HandleAttributes);
                    }
                }
            }
        }
        if (hProbe != INVALID_HANDLE_VALUE) CloseHandle(hProbe);
    }

    size_t closed = 0u;
    bool allOk = true;

    for (const auto& req : requests) {
        if (req.pid <= 4u || req.pid == selfPid || req.handleValue == 0 || req.expectedPath.empty()) continue;
        if ((req.handleAttributes & 0x1u) != 0u) { // OBJ_PROTECT_CLOSE
            allOk = false;
            continue;
        }

        const ProcessBasicFacts pf = QueryProcessFactsWin32(req.pid);
        if (IsCriticalWindowsProcessName(PathUtils::ToLower(pf.exeName), req.pid)) {
            allOk = false;
            continue;
        }

        if (haveLiveTable && (req.handleValue & 0xFFFFFFFFULL) == req.handleValue) {
            const uint64_t hKey = (static_cast<uint64_t>(req.pid) << 32) | static_cast<uint64_t>(req.handleValue);
            auto itLive = liveFileHandleAttrs.find(hKey);
            if (itLive == liveFileHandleAttrs.end()) {
                // Handle was already closed naturally by the target process!
                continue;
            }
            if ((itLive->second & 0x1u) != 0u) { // OBJ_PROTECT_CLOSE set in kernel handle table!
                allOk = false;
                continue;
            }
        }

        HANDLE hProc = OpenProcess(PROCESS_DUP_HANDLE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, req.pid);
        if (!hProc) {
            const DWORD err = GetLastError();
            if (err == ERROR_INVALID_PARAMETER) continue; // Process already exited
            if (err == ERROR_ACCESS_DENIED) {
                if (outAnyAccessDenied) *outAnyAccessDenied = true;
                if (outElevationNeeded) outElevationNeeded->push_back(req);
            }
            allOk = false;
            continue;
        }

        // TOCTOU Check #1: Verify live process creation timestamp matches expectedStartTicks on the opened process handle
        if (req.expectedStartTicks != 0u) {
            FILETIME ftCreate = {}, ftExit = {}, ftKernel = {}, ftUser = {};
            if (GetProcessTimes(hProc, &ftCreate, &ftExit, &ftKernel, &ftUser)) {
                const uint64_t liveTick = (static_cast<uint64_t>(ftCreate.dwHighDateTime) << 32) | ftCreate.dwLowDateTime;
                if (liveTick != 0u && liveTick != req.expectedStartTicks) {
                    CloseHandle(hProc);
                    continue; // PID was recycled!
                }
            }
        }

        // TOCTOU Check #2: Pipe-safe duplicate WITHOUT DUPLICATE_CLOSE_SOURCE, verify FILE_TYPE_DISK & exact path match!
        std::wstring livePath;
        bool timedOut = false;
        const bool verifiedDisk = resolver.VerifySingleDiskHandle(req.pid, req.handleValue, dosMapper, livePath, timedOut);
        if (!verifiedDisk || timedOut) {
            CloseHandle(hProc);
            allOk = false;
            continue;
        }

        const std::wstring liveKey = NormalizeMatchKey(livePath);
        const std::wstring expKey  = NormalizeMatchKey(req.expectedPath);
        bool pathMatches = (!liveKey.empty() && liveKey == expKey);
        if (!pathMatches) {
            const std::wstring expCanon = ResolveFinalDosPath(req.expectedPath);
            if (!expCanon.empty() && NormalizeMatchKey(expCanon) == liveKey) {
                pathMatches = true;
            }
        }
        if (!pathMatches) {
            // Recycled handle slot now points to a DIFFERENT file — NEVER close it!
            CloseHandle(hProc);
            allOk = false;
            continue;
        }

        // Surgical close in remote process!
        if (DuplicateHandle(hProc, reinterpret_cast<HANDLE>(req.handleValue),
                            nullptr, nullptr, 0, FALSE, DUPLICATE_CLOSE_SOURCE))
        {
            ++closed;
        } else {
            allOk = false;
        }
        CloseHandle(hProc);
    }

    if (outClosedCount) *outClosedCount = closed;
    return allOk;
#else
    (void)requests;
    return false;
#endif
}

// ============================================================================
// Stop Windows Services & Schedule Delete on Reboot
// ============================================================================
bool StopLockingServices(
    const std::vector<std::wstring>& serviceNames,
    DWORD waitTimeoutMs,
    std::vector<std::wstring>* outFailedServices,
    bool* outAnyAccessDenied)
{
    if (outFailedServices) outFailedServices->clear();
    if (outAnyAccessDenied) *outAnyAccessDenied = false;
    if (serviceNames.empty()) return true;

#ifdef _WIN32
    SC_HANDLE hScm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!hScm) {
        if (GetLastError() == ERROR_ACCESS_DENIED && outAnyAccessDenied) *outAnyAccessDenied = true;
        if (outFailedServices) *outFailedServices = serviceNames;
        return false;
    }

    const ULONGLONG deadline = GetTickCount64() + waitTimeoutMs;
    std::unordered_set<std::wstring> seen;
    for (const auto& rawSvc : serviceNames) {
        if (rawSvc.empty()) continue;
        const std::wstring key = PathUtils::ToLower(rawSvc);
        if (!seen.insert(key).second) continue;

        SC_HANDLE hSvc = OpenServiceW(hScm, rawSvc.c_str(), SERVICE_STOP | SERVICE_QUERY_STATUS);
        if (!hSvc) {
            if (GetLastError() == ERROR_ACCESS_DENIED && outAnyAccessDenied) *outAnyAccessDenied = true;
            if (outFailedServices) outFailedServices->push_back(rawSvc);
            continue;
        }

        SERVICE_STATUS_PROCESS ssp = {};
        DWORD needed = 0;
        bool stopped = false;
        if (QueryServiceStatusEx(hSvc, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&ssp), sizeof(ssp), &needed)) {
            if (ssp.dwCurrentState == SERVICE_STOPPED) {
                stopped = true;
            } else {
                bool stopIssued = (ssp.dwCurrentState == SERVICE_STOP_PENDING);
                if (!stopIssued) {
                    SERVICE_STATUS ss = {};
                    if (ControlService(hSvc, SERVICE_CONTROL_STOP, &ss) || GetLastError() == ERROR_SERVICE_NOT_ACTIVE) {
                        stopIssued = true;
                    } else if (GetLastError() == ERROR_ACCESS_DENIED && outAnyAccessDenied) {
                        *outAnyAccessDenied = true;
                    }
                }
                while (stopIssued && GetTickCount64() < deadline) {
                    if (QueryServiceStatusEx(hSvc, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&ssp), sizeof(ssp), &needed) &&
                        ssp.dwCurrentState == SERVICE_STOPPED)
                    {
                        stopped = true;
                        break;
                    }
                    Sleep(60);
                }
            }
        }
        if (!stopped && outFailedServices) outFailedServices->push_back(rawSvc);
        CloseServiceHandle(hSvc);
    }

    CloseServiceHandle(hScm);
    return !outFailedServices || outFailedServices->empty();
#else
    (void)waitTimeoutMs;
    return false;
#endif
}

bool ScheduleDeleteOnReboot(
    const std::vector<std::wstring>& targetPaths,
    size_t* outScheduledCount,
    bool* outAccessDenied)
{
    if (outScheduledCount) *outScheduledCount = 0u;
    if (outAccessDenied) *outAccessDenied = false;
    if (targetPaths.empty()) return true;

#ifdef _WIN32
    // Collect files first and directories in post-order (deepest child directory first, root directory last)
    // so smss.exe removes nested files before attempting directory removal at boot!
    // Pass standard DOS paths ("C:\...") — NOT "\\?\" — so PendingFileRenameOperations stores "\??\C:\...".
    std::vector<std::wstring> filesToSchedule;
    std::vector<std::wstring> dirsBottomUp;

    for (const auto& raw : targetPaths) {
        const std::wstring clean = TrimSlashesForMatch(PathUtils::StripExtendedWinPrefix(raw));
        if (clean.empty() || TransferManager::IsDeleteRefused(clean)) continue;
        const DWORD attrs = GetFileAttributesW(PathUtils::ToExtendedWinPath(clean).c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) continue;

        if ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0 && (attrs & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
            std::vector<std::pair<std::wstring, bool>> stack;
            stack.emplace_back(clean, false);
            size_t visited = 0;
            while (!stack.empty() && visited < 16384u) {
                auto [curDir, expanded] = std::move(stack.back());
                stack.pop_back();
                if (expanded) {
                    dirsBottomUp.push_back(std::move(curDir));
                    continue;
                }
                stack.emplace_back(curDir, true);
                WIN32_FIND_DATAW ffd = {};
                HANDLE hFind = FindFirstFileW(
                    PathUtils::ToExtendedWinPath(PathUtils::EnsureTrailingSlash(curDir) + L"*").c_str(), &ffd);
                if (hFind == INVALID_HANDLE_VALUE) continue;
                do {
                    if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0) continue;
                    ++visited;
                    std::wstring child = PathUtils::EnsureTrailingSlash(curDir) + ffd.cFileName;
                    if ((ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
                        (ffd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0)
                    {
                        stack.emplace_back(std::move(child), false);
                    } else {
                        filesToSchedule.push_back(std::move(child));
                    }
                } while (FindNextFileW(hFind, &ffd));
                FindClose(hFind);
            }
        } else {
            filesToSchedule.push_back(clean);
        }
    }

    ClearReadOnlyAttributes(targetPaths);

    size_t scheduled = 0u;
    bool allOk = true;
    auto scheduleOne = [&](const std::wstring& p) {
        const std::wstring dosPath = PathUtils::StripExtendedWinPrefix(p);
        if (MoveFileExW(dosPath.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT)) {
            ++scheduled;
        } else {
            allOk = false;
            if (GetLastError() == ERROR_ACCESS_DENIED && outAccessDenied) *outAccessDenied = true;
        }
    };

    for (const auto& f : filesToSchedule) scheduleOne(f);
    for (const auto& d : dirsBottomUp)    scheduleOne(d);

    if (outScheduledCount) *outScheduledCount = scheduled;
    return allOk && scheduled > 0u;
#else
    return false;
#endif
}

// ============================================================================
// Graceful Close, Force Kill, Clear Read-Only, and UAC Admin Unlock/Delete
// ============================================================================
bool CloseProcessesGracefully(
    const std::vector<uint32_t>& pids,
    DWORD waitTimeoutMs,
    std::vector<uint32_t>* outStillRunning,
    const std::vector<uint64_t>* expectedStartTicks)
{
    if (outStillRunning) outStillRunning->clear();
    if (pids.empty()) return true;

#ifdef _WIN32
    const uint32_t selfPid = GetCurrentProcessId();
    std::unordered_set<uint32_t> targetSet;
    for (size_t i = 0; i < pids.size(); ++i) {
        const uint32_t pid = pids[i];
        if (pid <= 4u || pid == selfPid) continue;
        const ProcessBasicFacts pf = QueryProcessFactsWin32(pid);
        if (IsCriticalWindowsProcessName(PathUtils::ToLower(pf.exeName), pid)) continue;
        const uint64_t expTick = (expectedStartTicks && i < expectedStartTicks->size()) ? (*expectedStartTicks)[i] : 0u;
        if (expTick != 0u && pf.startTicks != 0u && pf.startTicks != expTick) continue;
        targetSet.insert(pid);
    }
    if (targetSet.empty()) return true;

    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        const auto* setPtr = reinterpret_cast<const std::unordered_set<uint32_t>*>(lParam);
        if (setPtr->find(pid) != setPtr->end()) {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&targetSet));

    std::vector<HANDLE> waitHandles;
    std::vector<uint32_t> waitPids;
    for (uint32_t pid : targetSet) {
        HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid);
        if (h) {
            waitHandles.push_back(h);
            waitPids.push_back(pid);
        }
    }

    const ULONGLONG deadline = GetTickCount64() + waitTimeoutMs;
    for (size_t i = 0; i < waitHandles.size(); ++i) {
        const ULONGLONG now = GetTickCount64();
        const DWORD rem = (now < deadline) ? static_cast<DWORD>(deadline - now) : 0u;
        const DWORD wr = WaitForSingleObject(waitHandles[i], rem);
        if (wr != WAIT_OBJECT_0 && outStillRunning) {
            outStillRunning->push_back(waitPids[i]);
        }
        CloseHandle(waitHandles[i]);
    }
    return !outStillRunning || outStillRunning->empty();
#else
    const pid_t selfPid = ::getpid();
    std::vector<uint32_t> sentPids;
    for (size_t i = 0; i < pids.size(); ++i) {
        const uint32_t pid = pids[i];
        if (pid <= 2u || static_cast<pid_t>(pid) == selfPid || IsCriticalLinuxPid(pid)) continue;
        const uint64_t expTick = (expectedStartTicks && i < expectedStartTicks->size()) ? (*expectedStartTicks)[i] : 0u;
        if (expTick != 0u) {
            const uint64_t liveTick = QueryLinuxProcessStartTicks(pid);
            if (liveTick != 0u && liveTick != expTick) continue;
        }
        ::kill(static_cast<pid_t>(pid), SIGTERM);
        sentPids.push_back(pid);
    }
    const ULONGLONG deadline = GetTickCount64() + waitTimeoutMs;
    bool allGone = true;
    for (uint32_t pid : sentPids) {
        while (GetTickCount64() < deadline) {
            if (::kill(static_cast<pid_t>(pid), 0) != 0 && errno == ESRCH) break;
            Sleep(25);
        }
        if (::kill(static_cast<pid_t>(pid), 0) == 0) {
            allGone = false;
            if (outStillRunning) outStillRunning->push_back(pid);
        }
    }
    return allGone;
#endif
}

bool TerminateLockingProcesses(
    const std::vector<uint32_t>& pids,
    DWORD waitTimeoutMs,
    std::vector<uint32_t>* outFailedPids,
    bool* outAnyAccessDenied,
    const std::vector<uint64_t>* expectedStartTicks)
{
    if (outFailedPids) outFailedPids->clear();
    if (outAnyAccessDenied) *outAnyAccessDenied = false;
    if (pids.empty()) return true;

#ifdef _WIN32
    const uint32_t selfPid = GetCurrentProcessId();
    std::vector<HANDLE> waitHandles;
    for (size_t i = 0; i < pids.size(); ++i) {
        const uint32_t pid = pids[i];
        if (pid <= 4u || pid == selfPid) continue;
        const ProcessBasicFacts pf = QueryProcessFactsWin32(pid);
        const std::wstring leafLow = PathUtils::ToLower(pf.exeName);
        if (IsCriticalWindowsProcessName(leafLow, pid)) {
            if (outFailedPids) outFailedPids->push_back(pid);
            continue;
        }
        const uint64_t expTick = (expectedStartTicks && i < expectedStartTicks->size()) ? (*expectedStartTicks)[i] : 0u;
        if (expTick != 0u && pf.startTicks != 0u && pf.startTicks != expTick) {
            continue;
        }

        HANDLE hProc = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!hProc) hProc = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!hProc) hProc = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
        if (!hProc) hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (!hProc) {
            const DWORD err = GetLastError();
            if (err == ERROR_INVALID_PARAMETER) continue;
            if (err == ERROR_ACCESS_DENIED && outAnyAccessDenied) *outAnyAccessDenied = true;
            if (outFailedPids) outFailedPids->push_back(pid);
            continue;
        }
        if (expTick != 0u) {
            FILETIME ftCreate = {}, ftExit = {}, ftKernel = {}, ftUser = {};
            if (GetProcessTimes(hProc, &ftCreate, &ftExit, &ftKernel, &ftUser)) {
                const uint64_t liveTick = (static_cast<uint64_t>(ftCreate.dwHighDateTime) << 32) | ftCreate.dwLowDateTime;
                if (liveTick != 0u && liveTick != expTick) {
                    CloseHandle(hProc);
                    continue;
                }
            }
        }
        if (!TerminateProcess(hProc, 1u)) {
            const DWORD err = GetLastError();
            DWORD exitCode = 0;
            const bool alreadyDead = GetExitCodeProcess(hProc, &exitCode) && exitCode != STILL_ACTIVE;
            if (!alreadyDead) {
                if (err == ERROR_ACCESS_DENIED && outAnyAccessDenied) *outAnyAccessDenied = true;
                if (outFailedPids) outFailedPids->push_back(pid);
            }
            CloseHandle(hProc);
        } else {
            waitHandles.push_back(hProc);
        }
    }

    const ULONGLONG deadline = GetTickCount64() + waitTimeoutMs;
    for (HANDLE h : waitHandles) {
        const ULONGLONG now = GetTickCount64();
        const DWORD rem = (now < deadline) ? static_cast<DWORD>(deadline - now) : 0u;
        WaitForSingleObject(h, rem);
        CloseHandle(h);
    }
    return !outFailedPids || outFailedPids->empty();
#else
    const pid_t selfPid = ::getpid();
    std::vector<uint32_t> killedPids;
    for (size_t i = 0; i < pids.size(); ++i) {
        const uint32_t pid = pids[i];
        if (pid <= 2u || static_cast<pid_t>(pid) == selfPid) continue;
        if (IsCriticalLinuxPid(pid)) {
            if (outFailedPids) outFailedPids->push_back(pid);
            continue;
        }
        const uint64_t expTick = (expectedStartTicks && i < expectedStartTicks->size()) ? (*expectedStartTicks)[i] : 0u;
        if (expTick != 0u) {
            const uint64_t liveTick = QueryLinuxProcessStartTicks(pid);
            if (liveTick != 0u && liveTick != expTick) continue;
        }
        if (::kill(static_cast<pid_t>(pid), SIGKILL) != 0 && errno != ESRCH) {
            if (errno == EPERM && outAnyAccessDenied) *outAnyAccessDenied = true;
            if (outFailedPids) outFailedPids->push_back(pid);
        } else {
            killedPids.push_back(pid);
        }
    }
    const ULONGLONG deadline = GetTickCount64() + waitTimeoutMs;
    for (uint32_t pid : killedPids) {
        while (GetTickCount64() < deadline && ::kill(static_cast<pid_t>(pid), 0) == 0) Sleep(20);
    }
    return !outFailedPids || outFailedPids->empty();
#endif
}

size_t ClearReadOnlyAttributes(const std::vector<std::wstring>& targetPaths) {
    size_t cleared = 0;
#ifdef _WIN32
    std::vector<std::wstring> dirStack;
    for (const auto& raw : targetPaths) {
        if (raw.empty()) continue;
        const std::wstring clean = PathUtils::StripExtendedWinPrefix(raw);
        const std::wstring ext = PathUtils::ToExtendedWinPath(clean);
        const DWORD attrs = GetFileAttributesW(ext.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) continue;
        if ((attrs & FILE_ATTRIBUTE_READONLY) != 0) {
            DWORD cleanAttr = attrs & ~FILE_ATTRIBUTE_READONLY;
            if (cleanAttr == 0) cleanAttr = FILE_ATTRIBUTE_NORMAL;
            if (SetFileAttributesW(ext.c_str(), cleanAttr)) ++cleared;
        }
        if ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0 && (attrs & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
            dirStack.push_back(clean);
        }
    }

    size_t visited = 0;
    while (!dirStack.empty() && visited < 16384u) {
        std::wstring cur = std::move(dirStack.back());
        dirStack.pop_back();
        WIN32_FIND_DATAW ffd = {};
        HANDLE hFind = FindFirstFileW(PathUtils::ToExtendedWinPath(PathUtils::EnsureTrailingSlash(cur) + L"*").c_str(), &ffd);
        if (hFind == INVALID_HANDLE_VALUE) continue;
        do {
            if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0) continue;
            ++visited;
            std::wstring child = PathUtils::EnsureTrailingSlash(cur) + ffd.cFileName;
            if ((ffd.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0) {
                DWORD cleanAttr = ffd.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY;
                if (cleanAttr == 0) cleanAttr = FILE_ATTRIBUTE_NORMAL;
                if (SetFileAttributesW(PathUtils::ToExtendedWinPath(child).c_str(), cleanAttr)) ++cleared;
            }
            if ((ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
                (ffd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0 &&
                dirStack.size() < 4096u)
            {
                dirStack.push_back(std::move(child));
            }
        } while (FindNextFileW(hFind, &ffd));
        FindClose(hFind);
    }
#else
    (void)targetPaths;
#endif
    return cleared;
}

bool RunElevatedAdminRequest(
    HWND ownerHwnd,
    const ElevatedUnlockRequest& req,
    std::wstring* outErrorSummary)
{
    if (outErrorSummary) outErrorSummary->clear();
#ifdef _WIN32
    std::wstring reqText;
    for (const auto& hc : req.handlesToClose) {
        if (hc.pid > 4u && hc.pid != GetCurrentProcessId() && hc.handleValue != 0 && !hc.expectedPath.empty()) {
            reqText += L"CLOSE_HANDLE\t" + std::to_wstring(hc.pid) + L"\t" +
                       std::to_wstring(hc.expectedStartTicks) + L"\t" +
                       std::to_wstring(static_cast<unsigned long long>(hc.handleValue)) + L"\t" +
                       PathUtils::StripExtendedWinPrefix(hc.expectedPath) + L"\n";
        }
    }
    for (const auto& svc : req.servicesToStop) {
        if (!svc.empty()) {
            reqText += L"STOP_SVC\t" + svc + L"\n";
        }
    }
    for (size_t i = 0; i < req.pidsToKill.size(); ++i) {
        const uint32_t pid = req.pidsToKill[i];
        const uint64_t stTick = (i < req.pidStartTicks.size()) ? req.pidStartTicks[i] : 0u;
        if (pid > 4u && pid != GetCurrentProcessId()) {
            reqText += L"KILL\t" + std::to_wstring(pid) + L"\t" + std::to_wstring(stTick) + L"\n";
        }
    }
    for (const auto& p : req.pathsToClearReadOnly) {
        if (!p.empty()) {
            reqText += L"CLEAR_RO\t" + PathUtils::StripExtendedWinPrefix(p) + L"\n";
        }
    }
    for (const auto& p : req.pathsToDeleteNow) {
        if (!p.empty() && !TransferManager::IsDeleteRefused(p)) {
            reqText += L"DEL\t" + PathUtils::StripExtendedWinPrefix(p) + L"\n";
        }
    }
    for (const auto& p : req.pathsToDeleteOnReboot) {
        if (!p.empty() && !TransferManager::IsDeleteRefused(p)) {
            reqText += L"DELAY_DEL\t" + PathUtils::StripExtendedWinPrefix(p) + L"\n";
        }
    }
    if (reqText.empty()) return false;

    wchar_t tmpDir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmpDir);
    wchar_t tag[64] = {};
    swprintf_s(tag, L"E2AdminUnlock_%lu_%llu", GetCurrentProcessId(), static_cast<unsigned long long>(GetTickCount64()));
    const std::wstring reqFile = std::wstring(tmpDir) + tag + L".req";
    const std::wstring resFile = std::wstring(tmpDir) + tag + L".res";

    const std::string reqUtf8 = PathUtils::WideToUtf8(reqText);
    if (!PathUtils::WriteFileBytes(reqFile, reqUtf8.data(), reqUtf8.size())) {
        return false;
    }

    wchar_t selfExe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, selfExe, MAX_PATH);
    const std::wstring params = L"--admin-unlock \"" + reqFile + L"\" \"" + resFile + L"\"";

    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd = ownerHwnd;
    sei.lpVerb = L"runas";
    sei.lpFile = selfExe;
    sei.lpParameters = params.c_str();
    sei.nShow = SW_HIDE;

    if (!ShellExecuteExW(&sei) || !sei.hProcess) {
        DeleteFileW(reqFile.c_str());
        if (outErrorSummary) *outErrorSummary = L"UAC cancelled or declined.";
        return false;
    }

    // Pump window messages via MsgWaitForMultipleObjects while disabling ownerHwnd against re-entrancy
    // so neither LockInspectorDlg nor Explorer2 ever enters "(Not Responding)" during UAC work!
    const BOOL wasOwnerEnabled = (ownerHwnd && IsWindow(ownerHwnd)) ? IsWindowEnabled(ownerHwnd) : FALSE;
    if (ownerHwnd && wasOwnerEnabled) EnableWindow(ownerHwnd, FALSE);

    DWORD wr = WAIT_TIMEOUT;
    const ULONGLONG deadline = GetTickCount64() + 45000ULL;
    while (true) {
        const ULONGLONG now = GetTickCount64();
        if (now >= deadline) { wr = WAIT_TIMEOUT; break; }
        const DWORD sliceMs = static_cast<DWORD>(std::min<ULONGLONG>(deadline - now, 250ULL));
        const DWORD mw = MsgWaitForMultipleObjects(1u, &sei.hProcess, FALSE, sliceMs, QS_ALLINPUT);
        if (mw == WAIT_OBJECT_0) {
            wr = WAIT_OBJECT_0;
            break;
        }
        if (mw == WAIT_OBJECT_0 + 1u) {
            MSG msg = {};
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    PostQuitMessage(static_cast<int>(msg.wParam));
                    break;
                }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        } else if (mw == WAIT_FAILED) {
            break;
        }
    }

    if (ownerHwnd && wasOwnerEnabled && IsWindow(ownerHwnd)) {
        EnableWindow(ownerHwnd, TRUE);
        SetForegroundWindow(ownerHwnd);
    }

    DWORD exitCode = 1u;
    if (wr == WAIT_OBJECT_0) {
        GetExitCodeProcess(sei.hProcess, &exitCode);
    } else {
        TerminateProcess(sei.hProcess, 2u);
    }
    CloseHandle(sei.hProcess);
    DeleteFileW(reqFile.c_str());

    std::vector<uint8_t> resBytes;
    if (PathUtils::ReadFileBytes(resFile, 1024 * 1024, resBytes) && !resBytes.empty()) {
        std::string resUtf8(reinterpret_cast<const char*>(resBytes.data()), resBytes.size());
        if (outErrorSummary) *outErrorSummary = PathUtils::Utf8ToWide(resUtf8);
    }
    DeleteFileW(resFile.c_str());
    return (wr == WAIT_OBJECT_0 && exitCode == 0u);
#else
    (void)ownerHwnd; (void)req;
    return false;
#endif
}

bool RunElevatedUnlockAndDelete(
    HWND ownerHwnd,
    const std::vector<uint32_t>& elevatedPidsToKill,
    const std::vector<std::wstring>& pathsToDelete,
    std::wstring* outErrorSummary,
    const std::vector<uint64_t>* elevatedStartTicks)
{
    if (elevatedPidsToKill.empty() && pathsToDelete.empty()) {
        if (outErrorSummary) outErrorSummary->clear();
        return true;
    }
    ElevatedUnlockRequest req;
    req.pidsToKill = elevatedPidsToKill;
    if (elevatedStartTicks) req.pidStartTicks = *elevatedStartTicks;
    req.pathsToDeleteNow = pathsToDelete;
    return RunElevatedAdminRequest(ownerHwnd, req, outErrorSummary);
}

int HandleAdminUnlockCli(const std::wstring& reqFilePath, const std::wstring& resFilePath) {
    std::vector<uint8_t> raw;
    if (!PathUtils::ReadFileBytes(reqFilePath, 4 * 1024 * 1024, raw) || raw.empty()) return 2;
    const std::wstring text = PathUtils::Utf8ToWide(std::string(reinterpret_cast<const char*>(raw.data()), raw.size()));

    std::vector<RemoteHandleCloseRequest> handlesToClose;
    std::vector<std::wstring> svcsToStop;
    std::vector<uint32_t> pidsToKill;
    std::vector<uint64_t> startTicksToKill;
    std::vector<std::wstring> pathsToClearRo;
    std::vector<std::wstring> pathsToDel;
    std::vector<std::wstring> pathsToDelayDel;

    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find(L'\n', pos);
        if (nl == std::wstring::npos) nl = text.size();
        std::wstring line = text.substr(pos, nl - pos);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        pos = nl + 1u;

        if (line.rfind(L"CLOSE_HANDLE\t", 0) == 0) {
            // CLOSE_HANDLE\t<pid>\t<startTick>\t<handleVal>\t<path>
            const size_t t1 = 13u;
            const size_t t2 = line.find(L'\t', t1);
            const size_t t3 = (t2 != std::wstring::npos) ? line.find(L'\t', t2 + 1u) : std::wstring::npos;
            const size_t t4 = (t3 != std::wstring::npos) ? line.find(L'\t', t3 + 1u) : std::wstring::npos;
            if (t4 != std::wstring::npos) {
                RemoteHandleCloseRequest hc;
                hc.pid = static_cast<uint32_t>(std::wcstoul(line.c_str() + t1, nullptr, 10));
                hc.expectedStartTicks = static_cast<uint64_t>(std::wcstoull(line.c_str() + t2 + 1u, nullptr, 10));
                hc.handleValue = static_cast<uintptr_t>(std::wcstoull(line.c_str() + t3 + 1u, nullptr, 10));
                hc.expectedPath = line.substr(t4 + 1u);
                if (hc.pid > 4u && hc.handleValue != 0 && !hc.expectedPath.empty()) {
                    handlesToClose.push_back(std::move(hc));
                }
            }
        } else if (line.rfind(L"STOP_SVC\t", 0) == 0) {
            std::wstring svc = line.substr(9);
            if (!svc.empty()) svcsToStop.push_back(std::move(svc));
        } else if (line.rfind(L"KILL\t", 0) == 0) {
            wchar_t* endPid = nullptr;
            const unsigned long pid = std::wcstoul(line.c_str() + 5, &endPid, 10);
            uint64_t stTick = 0u;
            if (endPid && *endPid == L'\t') {
                stTick = static_cast<uint64_t>(std::wcstoull(endPid + 1, nullptr, 10));
            }
            if (pid > 4u) {
                pidsToKill.push_back(static_cast<uint32_t>(pid));
                startTicksToKill.push_back(stTick);
            }
        } else if (line.rfind(L"CLEAR_RO\t", 0) == 0) {
            std::wstring p = line.substr(9);
            if (!p.empty()) pathsToClearRo.push_back(std::move(p));
        } else if (line.rfind(L"DEL\t", 0) == 0) {
            std::wstring p = line.substr(4);
            if (!p.empty() && !TransferManager::IsDeleteRefused(p)) pathsToDel.push_back(std::move(p));
        } else if (line.rfind(L"DELAY_DEL\t", 0) == 0) {
            std::wstring p = line.substr(10);
            if (!p.empty() && !TransferManager::IsDeleteRefused(p)) pathsToDelayDel.push_back(std::move(p));
        }
    }

    std::wstring errReport;
    bool allOk = true;

    if (!handlesToClose.empty()) {
        size_t closedCnt = 0u;
        if (!CloseRemoteFileHandles(handlesToClose, &closedCnt, nullptr, nullptr) && closedCnt == 0u) {
            allOk = false;
            errReport += L"Could not close one or more remote handles.\n";
        }
    }

    if (!svcsToStop.empty()) {
        std::vector<std::wstring> failedSvcs;
        if (!StopLockingServices(svcsToStop, 7000u, &failedSvcs, nullptr)) {
            allOk = false;
            for (const auto& fs : failedSvcs) {
                errReport += L"Failed to stop service: " + fs + L"\n";
            }
        }
    }

    std::vector<uint32_t> failedPids;
    if (!pidsToKill.empty()) {
        TerminateLockingProcesses(pidsToKill, 2500u, &failedPids, nullptr, &startTicksToKill);
        if (!failedPids.empty()) allOk = false;
        for (uint32_t fp : failedPids) {
            errReport += L"Failed to terminate PID " + std::to_wstring(fp) + L"\n";
        }
    }

    if (!pathsToClearRo.empty()) {
        ClearReadOnlyAttributes(pathsToClearRo);
    }

    for (const auto& p : pathsToDel) {
        ClearReadOnlyAttributes({ p });
        std::vector<TransferFileError> errs;
        if (!TransferManager::DeleteTreeNow(p, &errs)) {
            allOk = false;
            for (size_t i = 0; i < std::min<size_t>(errs.size(), 8u); ++i) {
                errReport += errs[i].srcPath + L" -> " + errs[i].message + L"\n";
            }
        }
    }

    if (!pathsToDelayDel.empty()) {
        size_t schedCnt = 0u;
        if (!ScheduleDeleteOnReboot(pathsToDelayDel, &schedCnt, nullptr)) {
            allOk = false;
            errReport += L"Failed to schedule one or more items for deletion on reboot.\n";
        }
    }

    if (!resFilePath.empty()) {
        const std::string outUtf8 = PathUtils::WideToUtf8(errReport);
        PathUtils::WriteFileBytes(resFilePath, outUtf8.data(), outUtf8.size());
    }
    return allOk ? 0 : 1;
}

// ============================================================================
// Cross-Platform Bounded Self-Test Lock Holder (`--hold-lock-test`)
// ============================================================================
#ifndef _WIN32
namespace {
volatile sig_atomic_t g_posixLockHelperTerm = 0;
void PosixLockHelperSigTerm(int) { g_posixLockHelperTerm = 1; }
} // namespace
#endif

int HandleLockTestHelperCli(
    const std::wstring& mode,
    const std::wstring& targetPath,
    const std::wstring& readyFilePath,
    int maxDurationMs)
{
    const int clampedMs = std::clamp(maxDurationMs, 100, 15000);
#ifdef _WIN32
    HANDLE hLockedFile = INVALID_HANDLE_VALUE;
    HANDLE hWatchEvent = nullptr;
    OVERLAPPED watchOv = {};
    alignas(8) uint8_t watchBuf[1024] = {};
    LPVOID pMappedView = nullptr;
    HMODULE hLoadedDll = nullptr;
    HANDLE hPipeServer = INVALID_HANDLE_VALUE;
    HANDLE hPipeClient = INVALID_HANDLE_VALUE;
    std::thread pipeReaderThread;
    HWND hMsgWin = nullptr;

    if (mode == L"file" || mode == L"window") {
        hLockedFile = CreateFileW(
            PathUtils::ToExtendedWinPath(targetPath).c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0 /* exclusive lock: no share! */,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (hLockedFile == INVALID_HANDLE_VALUE) return 2;
    } else if (mode == L"cwd") {
        if (!SetCurrentDirectoryW(targetPath.c_str())) return 2;
    } else if (mode == L"mmap") {
        HANDLE hf = CreateFileW(
            PathUtils::ToExtendedWinPath(targetPath).c_str(),
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (hf == INVALID_HANDLE_VALUE) return 2;
        LARGE_INTEGER sz = {};
        if (!GetFileSizeEx(hf, &sz) || sz.QuadPart < 64) {
            char initBytes[64] = { 'E', '2', 'M', 'M', 'A', 'P' };
            DWORD wr = 0;
            WriteFile(hf, initBytes, sizeof(initBytes), &wr, nullptr);
        }
        HANDLE hMap = CreateFileMappingW(hf, nullptr, PAGE_READWRITE, 0, 64, nullptr);
        // Intentionally close BOTH hFile and hMap after mapping the view so ONLY the section view in VM holds the file!
        CloseHandle(hf);
        if (!hMap) return 2;
        pMappedView = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 64);
        CloseHandle(hMap);
        if (!pMappedView) return 2;
    } else if (mode == L"dirwatch_pipe") {
        hLockedFile = CreateFileW(
            PathUtils::ToExtendedWinPath(targetPath).c_str(),
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE, // omits FILE_SHARE_DELETE -> blocks rmdir!
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
            nullptr);
        if (hLockedFile == INVALID_HANDLE_VALUE) return 2;
        hWatchEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        watchOv.hEvent = hWatchEvent;
        DWORD bytesRet = 0;
        ReadDirectoryChangesW(
            hLockedFile, watchBuf, sizeof(watchBuf), TRUE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME,
            &bytesRet, &watchOv, nullptr);

        // Also open a synchronous named pipe in the SAME process with an active blocked ReadFile IRP!
        wchar_t pipeName[128] = {};
        swprintf_s(pipeName, L"\\\\.\\pipe\\E2LockTestPipe_%lu_%llu",
                   GetCurrentProcessId(), static_cast<unsigned long long>(GetTickCount64()));
        hPipeServer = CreateNamedPipeW(
            pipeName, PIPE_ACCESS_OUTBOUND, PIPE_TYPE_BYTE | PIPE_WAIT,
            1, 256, 256, 0, nullptr);
        if (hPipeServer != INVALID_HANDLE_VALUE) {
            hPipeClient = CreateFileW(pipeName, GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0 /*synchronous!*/, nullptr);
            if (hPipeClient != INVALID_HANDLE_VALUE) {
                ConnectNamedPipe(hPipeServer, nullptr);
                pipeReaderThread = std::thread([hPipeClient]() {
                    char b = 0;
                    DWORD got = 0;
                    ReadFile(hPipeClient, &b, 1, &got, nullptr);
                });
                Sleep(20); // Ensure ReadFile IRP is actively parked in npfs.sys before signalling ready!
            }
        }
    } else if (mode == L"dll") {
        hLoadedDll = LoadLibraryExW(PathUtils::ToExtendedWinPath(targetPath).c_str(), nullptr, 0);
        if (!hLoadedDll) return 2;
    } else {
        return 3;
    }

    if (mode == L"window") {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = [](HWND hw, UINT msg, WPARAM wp, LPARAM lp) -> LRESULT {
            if (msg == WM_CLOSE) { DestroyWindow(hw); return 0; }
            if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
            return DefWindowProcW(hw, msg, wp, lp);
        };
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"Explorer2LockTestHelperWnd";
        RegisterClassExW(&wc);
        hMsgWin = CreateWindowExW(0, wc.lpszClassName, L"E2LockTest", WS_OVERLAPPEDWINDOW,
                                  0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);
        if (hMsgWin) SetTimer(hMsgWin, 1, static_cast<UINT>(clampedMs), nullptr);
    }

    const char readyByte = '1';
    PathUtils::WriteFileBytes(readyFilePath, &readyByte, 1u);

    const ULONGLONG deadline = GetTickCount64() + static_cast<ULONGLONG>(clampedMs);
    if (hMsgWin) {
        MSG msg = {};
        while (GetTickCount64() < deadline) {
            const BOOL gm = GetMessageW(&msg, nullptr, 0, 0);
            if (gm <= 0) break;
            if (msg.message == WM_TIMER && msg.hwnd == hMsgWin) {
                DestroyWindow(hMsgWin);
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    } else {
        while (GetTickCount64() < deadline) {
            Sleep(35);
        }
    }

    if (mode == L"cwd") {
        wchar_t tmpDir[MAX_PATH] = {};
        if (GetTempPathW(MAX_PATH, tmpDir)) SetCurrentDirectoryW(tmpDir);
    }
    if (hPipeServer != INVALID_HANDLE_VALUE) {
        const char wake = 'x';
        DWORD wr = 0;
        WriteFile(hPipeServer, &wake, 1, &wr, nullptr);
        FlushFileBuffers(hPipeServer);
        DisconnectNamedPipe(hPipeServer);
    }
    if (pipeReaderThread.joinable()) pipeReaderThread.join();
    if (hPipeClient != INVALID_HANDLE_VALUE) CloseHandle(hPipeClient);
    if (hPipeServer != INVALID_HANDLE_VALUE) CloseHandle(hPipeServer);
    // Note: if the test surgically closed hLockedFile via DUPLICATE_CLOSE_SOURCE, CancelIo / CloseHandle will
    // benignly return FALSE without raising SEH as long as HANDLE_FLAG_PROTECT_FROM_CLOSE was not set.
    if (hWatchEvent) {
        if (hLockedFile != INVALID_HANDLE_VALUE) CancelIo(hLockedFile);
        CloseHandle(hWatchEvent);
    }
    if (hLockedFile != INVALID_HANDLE_VALUE) CloseHandle(hLockedFile);
    if (pMappedView) UnmapViewOfFile(pMappedView);
    if (hLoadedDll) FreeLibrary(hLoadedDll);
    return 0;
#else
    const std::string natTarget = Compat::NativePath(targetPath);
    int heldFd = -1;
    void* mappedPtr = nullptr;
    size_t mappedLen = 0;
    void* dlHandle = nullptr;
    int pipeFds[2] = { -1, -1 };
    std::thread pipeThread;

    g_posixLockHelperTerm = 0;
    struct sigaction sa = {};
    sa.sa_handler = PosixLockHelperSigTerm;
    sigemptyset(&sa.sa_mask);
    ::sigaction(SIGTERM, &sa, nullptr);

    if (mode == L"file" || mode == L"window") {
        heldFd = ::open(natTarget.c_str(), O_RDWR | O_CREAT, 0644);
        if (heldFd < 0) return 2;
    } else if (mode == L"cwd") {
        if (::chdir(natTarget.c_str()) != 0) return 2;
    } else if (mode == L"mmap" || mode == L"dll") {
        int fd = ::open(natTarget.c_str(), O_RDWR | O_CREAT, 0644);
        if (fd < 0) fd = ::open(natTarget.c_str(), O_RDONLY);
        if (fd < 0) return 2;
        struct stat st = {};
        if (::fstat(fd, &st) != 0 || st.st_size < 64) {
            const char pad[64] = { 'E', '2', 'M', 'A', 'P' };
            const ssize_t wr = ::write(fd, pad, sizeof(pad));
            (void)wr;
            ::fstat(fd, &st);
        }
        if (mode == L"dll") {
            dlHandle = ::dlopen(natTarget.c_str(), RTLD_LAZY | RTLD_LOCAL);
        }
        if (!dlHandle) {
            mappedLen = static_cast<size_t>(std::max<off_t>(64, std::min<off_t>(st.st_size, 65536)));
            mappedPtr = ::mmap(nullptr, mappedLen, PROT_READ, MAP_PRIVATE, fd, 0);
            if (mappedPtr == MAP_FAILED) { mappedPtr = nullptr; ::close(fd); return 2; }
        }
        ::close(fd); // Close fd so ONLY /proc/[pid]/maps holds the path!
    } else if (mode == L"dirwatch_pipe") {
        heldFd = ::open(natTarget.c_str(), O_RDONLY | O_DIRECTORY);
        if (heldFd < 0) return 2;
        if (::pipe(pipeFds) == 0) {
            const int rfd = pipeFds[0];
            pipeThread = std::thread([rfd]() {
                char c = 0;
                const ssize_t r = ::read(rfd, &c, 1);
                (void)r;
            });
        }
    } else {
        return 3;
    }

    const char readyByte = '1';
    PathUtils::WriteFileBytes(readyFilePath, &readyByte, 1u);

    const ULONGLONG deadline = GetTickCount64() + static_cast<ULONGLONG>(clampedMs);
    while (!g_posixLockHelperTerm && GetTickCount64() < deadline) {
        Sleep(25);
    }

    if (mode == L"cwd") {
        const int rc = ::chdir("/tmp");
        (void)rc;
    }
    if (pipeFds[1] >= 0) {
        const char wake = 'x';
        const ssize_t wr = ::write(pipeFds[1], &wake, 1);
        (void)wr;
        ::close(pipeFds[1]);
    }
    if (pipeThread.joinable()) pipeThread.join();
    if (pipeFds[0] >= 0) ::close(pipeFds[0]);
    if (heldFd >= 0) ::close(heldFd);
    if (mappedPtr && mappedLen > 0) ::munmap(mappedPtr, mappedLen);
    if (dlHandle) ::dlclose(dlHandle);
    return 0;
#endif
}

// ============================================================================
// Formatting Helpers
// ============================================================================
std::wstring FormatLockReasonMask(uint32_t mask, bool pl) {
    std::vector<const wchar_t*> parts;
    if (mask & kLockReasonSelfPreview) parts.push_back(pl ? L"Podgląd Explorer2" : L"Explorer2 Preview");
    if (mask & kLockReasonRunningExe)  parts.push_back(pl ? L"Uruchomiony .exe" : L"Running .exe");
    if (mask & kLockReasonLoadedDll)   parts.push_back(pl ? L"Załadowany .dll/.so" : L"Loaded .dll/.so");
    if (mask & kLockReasonFolderOrCwd) parts.push_back(pl ? L"Katalog roboczy / folder (CWD)" : L"Working Dir / Folder");
    if (mask & kLockReasonService)     parts.push_back(pl ? L"Usługa Windows" : L"Windows Service");
    if (mask & kLockReasonOpenHandle)  parts.push_back(pl ? L"Otwarty uchwyt / sekcja" : L"Open Handle / Mapped");
    if (parts.empty()) return pl ? L"Uchwyt" : L"Handle";

    std::wstring res;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) res += L" + ";
        res += parts[i];
    }
    return res;
}

std::wstring FormatLockReportText(const FileLockReport& report, bool pl) {
    std::wstring s;
    s += pl ? L"=== Raport blokad plików / folderów ===\r\n"
            : L"=== File / Folder Lock Report ===\r\n";
    for (const auto& t : report.targetPaths) {
        s += (pl ? L"Cel: " : L"Target: ") + t + L"\r\n";
    }
    wchar_t stats[192] = {};
    swprintf_s(stats,
        pl ? L"Przeskanowano: %zu plik(ów), %zu folder(ów) w %llu ms\r\n\r\n"
           : L"Scanned: %zu file(s), %zu folder(s) in %llu ms\r\n\r\n",
        report.scannedFiles, report.scannedDirs, static_cast<unsigned long long>(report.elapsedMs));
    s += stats;

    if (!report.HasAnyBlockers()) {
        s += pl ? L"\x2713 Brak aktywnych blokad procesów ani blokad uprawnień.\r\n"
                : L"\x2713 No active process locks or permission blockers detected.\r\n";
        return s;
    }

    for (const auto& p : report.processes) {
        s += L"\x2022 " + p.processName + L" (PID " + std::to_wstring(p.pid);
        if (p.isSelfProcess) s += pl ? L", ten proces Explorer2" : L", this Explorer2 process";
        else if (p.isElevatedOrProtected) s += pl ? L", Administrator / Chroniony" : L", Administrator / Protected";
        if (!p.serviceName.empty()) s += (pl ? L", Usługa: " : L", Service: ") + p.serviceName;
        if (!p.openHandles.empty()) {
            s += (pl ? L", uchwyty: " : L", handles: ") + std::to_wstring(p.openHandles.size());
        }
        s += L")  —  " + FormatLockReasonMask(p.reasonMask, pl) + L"\r\n";
        if (!p.exeFullPath.empty()) s += (pl ? L"    Ścieżka programu: " : L"    Executable: ") + p.exeFullPath + L"\r\n";
        if (!p.windowTitle.empty()) s += (pl ? L"    Tytuł okna: " : L"    Window Title: ") + p.windowTitle + L"\r\n";
        for (size_t i = 0; i < std::min<size_t>(p.lockedPaths.size(), 12u); ++i) {
            s += (pl ? L"    Blokuje: " : L"    Locks: ") + p.lockedPaths[i] + L"\r\n";
        }
        if (p.lockedPaths.size() > 12u) {
            s += L"    ... (+" + std::to_wstring(p.lockedPaths.size() - 12u) + (pl ? L" więcej)\r\n" : L" more)\r\n");
        }
    }

    for (const auto& b : report.permissionBlockers) {
        if (b.isReadOnlyAttr) {
            s += (pl ? L"\x2022 [Atrybut Tylko do odczytu] " : L"\x2022 [Read-Only Attribute] ") + b.path + L"\r\n";
        } else {
            s += (pl ? L"\x2022 [Brak uprawnień / Wymaga Administratora] " : L"\x2022 [Access Denied / Needs Administrator] ") +
                 b.path + L"\r\n";
        }
    }
    return s;
}

// ============================================================================
// Interactive Modal Lock Inspector & Surgical Unlocker Dialog
// ============================================================================
namespace {

enum LockDlgCtrlId : int {
    IDC_LOCK_LIST               = 2101,
    IDC_LOCK_DETAILS            = 2102,
    IDC_LOCK_BTN_RESCAN         = 2103,
    IDC_LOCK_BTN_COPY           = 2104,
    IDC_LOCK_BTN_GRACE          = 2105,
    IDC_LOCK_BTN_KILL           = 2106,
    IDC_LOCK_BTN_ADMIN          = 2107,
    IDC_LOCK_BTN_CHKALL         = 2109,
    IDC_LOCK_BTN_UNLOCK_HANDLES = 2110,
    IDC_LOCK_BTN_REBOOT_DEL     = 2111,

    // Row right-click context menu commands
    IDM_LOCK_CTX_UNLOCK_HANDLES = 2150,
    IDM_LOCK_CTX_STOP_SERVICE   = 2151,
    IDM_LOCK_CTX_CLOSE_WIN      = 2152,
    IDM_LOCK_CTX_KILL_PROC      = 2153,
    IDM_LOCK_CTX_OPEN_EXE_LOC   = 2154,
    IDM_LOCK_CTX_COPY_PID       = 2155,
    IDM_LOCK_CTX_COPY_PATH      = 2156,
    IDM_LOCK_CTX_DELAY_REBOOT   = 2157
};

struct LockRowItem {
    bool   isProcess = true;
    size_t indexInVec = 0; // index into report.processes or report.permissionBlockers
    bool   defaultChecked = false;
};

struct LockDialogState {
    HWND                      ownerHwnd = nullptr;
    HWND                      hDlg = nullptr;
    HWND                      hHeaderLbl = nullptr;
    HWND                      hList = nullptr;
    HWND                      hDetailEdit = nullptr;
    HWND                      hBtnRescan = nullptr;
    HWND                      hBtnCopy = nullptr;
    HWND                      hBtnCheckToggle = nullptr;
    HWND                      hBtnRebootDel = nullptr;
    HWND                      hBtnUnlockHandles = nullptr;
    HWND                      hBtnGraceful = nullptr;
    HWND                      hBtnKill = nullptr;
    HWND                      hBtnAdmin = nullptr;
    HWND                      hBtnClose = nullptr;
    HFONT                     hFont = nullptr;
    HFONT                     hBoldFont = nullptr;
    HBRUSH                    hBrDlgBg = nullptr;
    HBRUSH                    hBrEditBg = nullptr;
#if defined(_WIN32) && !defined(E2_WIN32_HAL)
    HIMAGELIST                hImgList = nullptr;
#endif
    bool                      pl = false;
    bool                      closed = false;
    int                       sortCol = -1;
    bool                      sortAsc = true;
    int                       dpi = 96;
    std::vector<std::wstring> targetPaths;
    LockDialogCallbacks       cb;
    FileLockReport            report;
    std::vector<LockRowItem>  rows;

    int S(int dip) const { return MulDiv(dip, dpi, 96); }

    bool HasRetryableFailure() const {
        return cb.EffectiveOp() != LockFailureOpKind::None && static_cast<bool>(cb.retryDeleteJob);
    }
    bool AllowDestructiveDelete() const {
        return cb.EffectiveOp() == LockFailureOpKind::Delete;
    }
};

void CloseLockDialogWindow(LockDialogState& st) {
    st.closed = true;
    // Re-enable ownerHwnd BEFORE DestroyWindow(hDlg) so Win32 never transfers activation to an unrelated app window!
    if (st.ownerHwnd && IsWindow(st.ownerHwnd)) {
        EnableWindow(st.ownerHwnd, TRUE);
    }
    if (st.hDlg && IsWindow(st.hDlg)) {
        DestroyWindow(st.hDlg);
    }
}

#if defined(_WIN32) && !defined(E2_WIN32_HAL)
int GetRowVecIndexFromListItem(HWND hList, int listIdx) {
    if (!hList || listIdx < 0) return -1;
    LVITEMW lvi = {};
    lvi.mask = LVIF_PARAM;
    lvi.iItem = listIdx;
    if (!ListView_GetItem(hList, &lvi)) return -1;
    return static_cast<int>(lvi.lParam);
}

std::wstring GetRowSortText(const LockDialogState& st, size_t ri, int col) {
    if (ri >= st.rows.size()) return L"";
    const LockRowItem& row = st.rows[ri];
    if (row.isProcess && row.indexInVec < st.report.processes.size()) {
        const auto& p = st.report.processes[row.indexInVec];
        switch (col) {
        case 0: return p.processName;
        case 1: return std::to_wstring(p.pid);
        case 2: return FormatLockReasonMask(p.reasonMask, st.pl);
        case 3: return !p.lockedPaths.empty() ? p.lockedPaths[0] : p.exeFullPath;
        default: break;
        }
    } else if (!row.isProcess && row.indexInVec < st.report.permissionBlockers.size()) {
        const auto& b = st.report.permissionBlockers[row.indexInVec];
        switch (col) {
        case 0: return b.isReadOnlyAttr ? L"[Attribute] Read-Only" : L"[Permissions] Access Denied";
        case 1: return b.isReadOnlyAttr ? L"READONLY" : L"UAC";
        case 2: return b.detail;
        case 3: return b.path;
        default: break;
        }
    }
    return L"";
}

int CALLBACK CompareLockListItems(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort) {
    const auto* st = reinterpret_cast<const LockDialogState*>(lParamSort);
    if (!st || st->sortCol < 0) return 0;
    const size_t ri1 = static_cast<size_t>(lParam1);
    const size_t ri2 = static_cast<size_t>(lParam2);
    if (ri1 >= st->rows.size() || ri2 >= st->rows.size()) return 0;

    int cmp = 0;
    if (st->sortCol == 1) {
        const LockRowItem& r1 = st->rows[ri1];
        const LockRowItem& r2 = st->rows[ri2];
        const uint64_t k1 = (r1.isProcess && r1.indexInVec < st->report.processes.size())
            ? st->report.processes[r1.indexInVec].pid : (0x100000000ULL + r1.indexInVec);
        const uint64_t k2 = (r2.isProcess && r2.indexInVec < st->report.processes.size())
            ? st->report.processes[r2.indexInVec].pid : (0x100000000ULL + r2.indexInVec);
        cmp = (k1 < k2) ? -1 : (k1 > k2 ? 1 : 0);
    } else {
        const std::wstring s1 = GetRowSortText(*st, ri1, st->sortCol);
        const std::wstring s2 = GetRowSortText(*st, ri2, st->sortCol);
        cmp = _wcsicmp(s1.c_str(), s2.c_str());
    }
    return st->sortAsc ? cmp : -cmp;
}

void ApplyLockListSort(LockDialogState& st) {
    if (!st.hList || st.sortCol < 0) return;
    ListView_SortItems(st.hList, CompareLockListItems, reinterpret_cast<LPARAM>(&st));
    if (HWND hHdr = ListView_GetHeader(st.hList)) {
        const int nCols = Header_GetItemCount(hHdr);
        for (int c = 0; c < nCols; ++c) {
            HDITEMW hdi = {};
            hdi.mask = HDI_FORMAT;
            if (Header_GetItem(hHdr, c, &hdi)) {
                hdi.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
                if (c == st.sortCol) hdi.fmt |= (st.sortAsc ? HDF_SORTUP : HDF_SORTDOWN);
                Header_SetItem(hHdr, c, &hdi);
            }
        }
    }
}
#endif

void UpdateLockDetailPane(LockDialogState& st, int selRowVecIdx) {
    std::wstring txt;
    const LockFailureOpKind op = st.cb.EffectiveOp();
    if (op != LockFailureOpKind::None && !st.cb.deleteErrors.empty()) {
        if (op == LockFailureOpKind::Rename) {
            txt += st.pl ? L"Błąd zmiany nazwy:\r\n" : L"Rename failure:\r\n";
        } else if (op == LockFailureOpKind::Move || op == LockFailureOpKind::Copy) {
            txt += st.pl ? L"Błędy transferu (zablokowane pliki):\r\n" : L"Transfer failures (locked files):\r\n";
        } else {
            txt += st.pl ? L"Błędy usuwania:\r\n" : L"Delete failures:\r\n";
        }
        for (size_t i = 0; i < std::min<size_t>(st.cb.deleteErrors.size(), 8u); ++i) {
            txt += L"  " + st.cb.deleteErrors[i].srcPath + L"  \x2192  " + st.cb.deleteErrors[i].message + L"\r\n";
        }
        txt += L"\r\n";
    }

    if (selRowVecIdx >= 0 && static_cast<size_t>(selRowVecIdx) < st.rows.size()) {
        const LockRowItem& r = st.rows[static_cast<size_t>(selRowVecIdx)];
        if (r.isProcess && r.indexInVec < st.report.processes.size()) {
            const auto& p = st.report.processes[r.indexInVec];
            txt += (st.pl ? L"Wybrany proces: " : L"Selected process: ") + p.processName +
                   L" (PID " + std::to_wstring(p.pid) + L")\r\n";
            if (!p.exeFullPath.empty()) txt += (st.pl ? L"Plik wykonywalny: " : L"Executable: ") + p.exeFullPath + L"\r\n";
            if (!p.windowTitle.empty()) txt += (st.pl ? L"Okno: " : L"Window: ") + p.windowTitle + L"\r\n";
            if (!p.serviceName.empty()) txt += (st.pl ? L"Usługa Windows: " : L"Windows Service: ") + p.serviceName + L"\r\n";
            txt += (st.pl ? L"Typ blokady: " : L"Lock kind: ") + FormatLockReasonMask(p.reasonMask, st.pl) + L"\r\n";
            if (p.HasClosableHandles()) {
                txt += (st.pl ? L"Chirurgiczne odblokowanie: " : L"Surgical handle unlock: ") +
                       std::to_wstring(p.openHandles.size()) +
                       (st.pl ? L" otwarty(ch) uchwyt(ów) można zamknąć przyciskiem [Odblokuj uchwyty] bez zamykania programu!\r\n"
                              : L" open handle(s) can be closed via [Unlock Handles] while keeping this app alive!\r\n");
            } else if ((p.reasonMask & (kLockReasonRunningExe | kLockReasonLoadedDll)) != 0) {
                txt += st.pl
                    ? L"Uwaga: Plik .exe/.dll jest zamapowany w pamięci obrazu — wymaga zamknięcia procesu lub usunięcia przy restarcie.\r\n"
                    : L"Note: Mapped .exe/.dll image section cannot be closed remotely — close the process or schedule delete on reboot.\r\n";
            }
            if (p.isElevatedOrProtected) {
                txt += st.pl ? L"Uprawnienia: Proces działa jako Administrator / Chroniony (użyj przycisku z tarczą UAC).\r\n"
                             : L"Privileges: Process is running Elevated / Protected (use the UAC Admin button if needed).\r\n";
            }
            for (const auto& lp : p.lockedPaths) {
                txt += (st.pl ? L"  \x2022 Zablokowane: " : L"  \x2022 Locked: ") + lp + L"\r\n";
            }
        } else if (!r.isProcess && r.indexInVec < st.report.permissionBlockers.size()) {
            const auto& b = st.report.permissionBlockers[r.indexInVec];
            txt += (st.pl ? L"Blokada atrybutów / uprawnień:\r\n  " : L"Attribute / permission blocker:\r\n  ") + b.path + L"\r\n";
            if (b.isReadOnlyAttr) {
                txt += st.pl ? L"  Przyczyna: ustawiony atrybut Tylko do odczytu (Read-Only). Zaznacz wiersz i kliknij [Odblokuj uchwyty] lub [Zamknij].\r\n"
                             : L"  Cause: FILE_ATTRIBUTE_READONLY is set. Keep checked and click [Unlock Handles] or [Close].\r\n";
            } else if (st.AllowDestructiveDelete()) {
                txt += st.pl ? L"  Przyczyna: brak uprawnienia DELETE na liście ACL NTFS (kod 5). Kliknij [Usuń jako Admin (UAC)].\r\n"
                             : L"  Cause: NTFS DACL denies DELETE permission (error 5). Click [Delete as Admin (UAC)].\r\n";
            } else {
                txt += st.pl ? L"  Przyczyna: brak uprawnień zapisu / modyfikacji na liście ACL (kod 5 / wymaga Administratora).\r\n"
                             : L"  Cause: ACL denies write / modify permission (error 5 / requires Administrator).\r\n";
            }
        }
    } else {
        txt += FormatLockReportText(st.report, st.pl);
    }

    if (st.hDetailEdit) SetWindowTextW(st.hDetailEdit, txt.c_str());
}

void PopulateLockDialogScan(LockDialogState& st) {
    SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    SelfHoldInfo sh;
    if (st.cb.querySelfHold) sh = st.cb.querySelfHold();
    InspectFileLocks(st.targetPaths, st.report, &sh, nullptr);
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));

    st.rows.clear();
    for (size_t i = 0; i < st.report.processes.size(); ++i) {
        const auto& p = st.report.processes[i];
        // Services with a known short name (even inside svchost.exe) can be safely stopped via ControlService!
        const bool defaultChk = p.DefaultCheckedForUnlock() || (!p.serviceName.empty() && !p.isSelfProcess && !p.isShellProcess);
        st.rows.push_back({ true, i, defaultChk });
    }
    for (size_t i = 0; i < st.report.permissionBlockers.size(); ++i) {
        const auto& b = st.report.permissionBlockers[i];
        st.rows.push_back({ false, i, b.isReadOnlyAttr });
    }

    std::wstring targetDesc;
    if (!st.cb.operationSummary.empty()) {
        targetDesc = st.cb.operationSummary;
    } else if (st.targetPaths.size() == 1u) {
        targetDesc = st.targetPaths[0];
    } else {
        targetDesc = std::to_wstring(st.targetPaths.size()) +
                     (st.pl ? L" wybranych elementów w: " : L" selected items in: ") +
                     PathUtils::GetParentPath(st.targetPaths[0]);
    }

    std::wstring hdr;
    const LockFailureOpKind op = st.cb.EffectiveOp();
    if (op == LockFailureOpKind::Delete) {
        hdr = st.pl
            ? (L"\x26A0 Nie udało się usunąć " + std::to_wstring(std::max<size_t>(1u, st.cb.failedDeleteCount)) + L" element(ów)  \x2022  Cel: " + targetDesc + L"\n")
            : (L"\x26A0 Could not delete " + std::to_wstring(std::max<size_t>(1u, st.cb.failedDeleteCount)) + L" item(s)  \x2022  Target: " + targetDesc + L"\n");
    } else if (op == LockFailureOpKind::Rename) {
        hdr = st.pl
            ? (L"\x26A0 Nie można zmienić nazwy (element jest zablokowany): " + targetDesc + L"\n")
            : (L"\x26A0 Could not rename (item is locked): " + targetDesc + L"\n");
    } else if (op == LockFailureOpKind::Move || op == LockFailureOpKind::Copy) {
        hdr = st.pl
            ? (L"\x26A0 Transfer napotkał zablokowane pliki (" + std::to_wstring( std::max<size_t>(1u, st.cb.failedDeleteCount) ) + L")  \x2022  " + targetDesc + L"\n")
            : (L"\x26A0 Transfer encountered locked file(s) (" + std::to_wstring( std::max<size_t>(1u, st.cb.failedDeleteCount) ) + L")  \x2022  " + targetDesc + L"\n");
    } else {
        hdr = (st.pl ? L"Cel inspekcji: " : L"Inspected target: ") + targetDesc + L"\n";
    }

    if (!st.report.HasAnyBlockers()) {
        hdr += st.pl
            ? L"\x2713 Brak aktywnych blokad procesów ani blokad uprawnień — operację można bezpiecznie wykonać."
            : L"\x2713 No active process locks or permission blocks found — safe to proceed.";
    } else {
        wchar_t sumBuf[256] = {};
        swprintf_s(sumBuf,
            st.pl ? L"Wykryto: %zu proces(ów) blokujących oraz %zu blokad(ę) uprawnień/atrybutów (skan %zu plików / %zu folderów w %llu ms)."
                  : L"Detected: %zu locking process(es) and %zu permission/attribute block(s) (scanned %zu files / %zu folders in %llu ms).",
            st.report.processes.size(),
            st.report.permissionBlockers.size(),
            st.report.scannedFiles,
            st.report.scannedDirs,
            static_cast<unsigned long long>(st.report.elapsedMs));
        hdr += sumBuf;
    }
    if (st.hHeaderLbl) SetWindowTextW(st.hHeaderLbl, hdr.c_str());

#if defined(_WIN32) && !defined(E2_WIN32_HAL)
    if (st.hList) {
        SendMessageW(st.hList, WM_SETREDRAW, FALSE, 0);
        ListView_DeleteAllItems(st.hList);
        if (st.hImgList) ImageList_RemoveAll(st.hImgList);

        int fallbackAppIcon = -1;
        int shieldOrWarnIcon = -1;
        if (st.hImgList) {
            SHFILEINFOW sfi = {};
            if (SHGetFileInfoW(L"x.exe", FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi),
                               SHGFI_USEFILEATTRIBUTES | SHGFI_ICON | SHGFI_SMALLICON) && sfi.hIcon) {
                fallbackAppIcon = ImageList_AddIcon(st.hImgList, sfi.hIcon);
                DestroyIcon(sfi.hIcon);
            }
            HICON hWarn = LoadIconW(nullptr, IDI_WARNING);
            if (hWarn) shieldOrWarnIcon = ImageList_AddIcon(st.hImgList, hWarn);
        }

        for (size_t ri = 0; ri < st.rows.size(); ++ri) {
            const LockRowItem& row = st.rows[ri];
            std::wstring col0, col1, col2, col3;
            int iconIdx = fallbackAppIcon;

            if (row.isProcess) {
                const auto& p = st.report.processes[row.indexInVec];
                col0 = p.processName;
                if (!p.windowTitle.empty()) col0 += L"  [" + p.windowTitle + L"]";

                col1 = L"PID " + std::to_wstring(p.pid);
                if (p.isSelfProcess) col1 += st.pl ? L" (Ten Explorer2)" : L" (This Explorer2)";
                else if (!p.serviceName.empty()) col1 += L" [Svc: " + p.serviceName + L"]";
                else if (p.isSystemCritical) col1 += L" [System]";
                else if (p.isElevatedOrProtected) col1 += L" [Admin]";

                col2 = FormatLockReasonMask(p.reasonMask, st.pl);
                if (!p.lockedPaths.empty()) {
                    col3 = p.lockedPaths[0];
                    if (p.lockedPaths.size() > 1u) {
                        col3 += L"  (+" + std::to_wstring(p.lockedPaths.size() - 1u) + L")";
                    }
                } else {
                    col3 = !p.exeFullPath.empty() ? p.exeFullPath : st.targetPaths[0];
                }

                if (st.hImgList && !p.exeFullPath.empty()) {
                    SHFILEINFOW sfi = {};
                    if (SHGetFileInfoW(p.exeFullPath.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_SMALLICON) && sfi.hIcon) {
                        iconIdx = ImageList_AddIcon(st.hImgList, sfi.hIcon);
                        DestroyIcon(sfi.hIcon);
                    }
                }
            } else {
                const auto& b = st.report.permissionBlockers[row.indexInVec];
                iconIdx = shieldOrWarnIcon;
                if (b.isReadOnlyAttr) {
                    col0 = st.pl ? L"[Atrybut] Tylko do odczytu" : L"[Attribute] Read-Only";
                    col1 = L"READONLY";
                    col2 = st.pl ? L"Zdejmij atrybut R/O" : L"Clear Read-Only flag";
                } else {
                    col0 = st.pl ? L"[Uprawnienia] Odmowa dostępu (ACL)" : L"[Permissions] Access Denied (ACL)";
                    col1 = st.pl ? L"Wymaga Admina (UAC)" : L"Needs Admin (UAC)";
                    col2 = st.pl ? L"Brak prawa modyfikacji (kod 5)" : L"Missing modify/DELETE right (err 5)";
                }
                col3 = b.path;
            }

            LVITEMW lvi = {};
            lvi.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
            lvi.iItem = static_cast<int>(ri);
            lvi.iImage = std::max(0, iconIdx);
            lvi.lParam = static_cast<LPARAM>(ri);
            lvi.pszText = const_cast<wchar_t*>(col0.c_str());
            const int insIdx = ListView_InsertItem(st.hList, &lvi);
            if (insIdx >= 0) {
                ListView_SetItemText(st.hList, insIdx, 1, const_cast<wchar_t*>(col1.c_str()));
                ListView_SetItemText(st.hList, insIdx, 2, const_cast<wchar_t*>(col2.c_str()));
                ListView_SetItemText(st.hList, insIdx, 3, const_cast<wchar_t*>(col3.c_str()));
                ListView_SetCheckState(st.hList, insIdx, row.defaultChecked ? TRUE : FALSE);
            }
        }

        if (st.sortCol >= 0) ApplyLockListSort(st);
        SendMessageW(st.hList, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(st.hList, nullptr, TRUE);
    }
#endif

    const bool hasAny = st.report.HasAnyBlockers();
    const bool hasRetry = st.HasRetryableFailure();
    bool anyDeletableTarget = false;
    for (const auto& tp : st.targetPaths) {
        if (!tp.empty() && !TransferManager::IsDeleteRefused(tp)) {
            anyDeletableTarget = true;
            break;
        }
    }

    if (st.hBtnUnlockHandles) {
        const bool canUnlockH = st.report.HasClosableHandles() || st.report.HasReadOnlyBlockers() || hasRetry;
        EnableWindow(st.hBtnUnlockHandles, canUnlockH ? TRUE : FALSE);
    }
    if (st.hBtnGraceful) EnableWindow(st.hBtnGraceful, (hasAny || hasRetry) ? TRUE : FALSE);
    if (st.hBtnKill)     EnableWindow(st.hBtnKill,     (hasAny || hasRetry) ? TRUE : FALSE);
    if (st.hBtnRebootDel) {
        EnableWindow(st.hBtnRebootDel, (st.AllowDestructiveDelete() && anyDeletableTarget) ? TRUE : FALSE);
    }
    if (st.hBtnAdmin) {
        bool enableAdmin = false;
        if (st.AllowDestructiveDelete()) {
            enableAdmin = anyDeletableTarget && (st.report.HasAclOrElevatedBlockers() || st.report.HasServices() || hasRetry);
        } else {
            // Pure Unlock-as-Admin mode (NEVER deletes files!): enabled when elevated/service/RO/closable blockers exist
            enableAdmin = st.report.HasAclOrElevatedBlockers() || st.report.HasServices() || st.report.HasReadOnlyBlockers();
        }
        EnableWindow(st.hBtnAdmin, enableAdmin ? TRUE : FALSE);
    }

    UpdateLockDetailPane(st, -1);
}

void LayoutLockDialogControls(LockDialogState& st, int clientW, int clientH) {
    const int pad = st.S(12);
    const int gap = st.S(6);
    const int hdrH = st.S(46);
    const int btnH = st.S(30);
    const int contentW = std::max(st.S(200), clientW - pad * 2);

    const int wRescan = st.hBtnRescan ? st.S(78) : 0;
    const int wCopy   = st.hBtnCopy ? st.S(98) : 0;
    const int wChk    = st.hBtnCheckToggle ? st.S(112) : 0;
    const int wReboot = st.hBtnRebootDel ? st.S(142) : 0;
    const int leftGroupW = wRescan + (wCopy ? gap + wCopy : 0) + (wChk ? gap + wChk : 0) + (wReboot ? gap + wReboot : 0);

    const int wUnlockH = st.hBtnUnlockHandles ? (st.HasRetryableFailure() ? st.S(156) : st.S(134)) : 0;
    const int wGrace   = st.hBtnGraceful ? st.S(128) : 0;
    const int wKill    = st.hBtnKill ? (st.HasRetryableFailure() ? st.S(156) : st.S(134)) : 0;
    const int wAdmin   = st.hBtnAdmin ? st.S(158) : 0;
    const int wClose   = st.hBtnClose ? st.S(78) : 0;
    const int rightGroupW = wUnlockH + (wGrace ? gap + wGrace : 0) + (wKill ? gap + wKill : 0) +
                            (wAdmin ? gap + wAdmin : 0) + (wClose ? gap + wClose : 0);

    const bool twoBtnRows = (leftGroupW + st.S(14) + rightGroupW > contentW);
    const int rowActionY = clientH - pad - btnH;
    const int rowUtilY   = twoBtnRows ? (rowActionY - gap - btnH) : rowActionY;

    const int availMidH = std::max(st.S(160), rowUtilY - pad * 3 - hdrH);
    const int detailH = std::max(st.S(86), availMidH * 34 / 100);
    const int detailY = rowUtilY - pad - detailH;
    const int listY = pad + hdrH + st.S(4);
    const int listH = std::max(st.S(76), detailY - pad - listY);

    auto placeCtrl = [](HWND hCtl, int x, int y, int w, int h) {
        if (hCtl) SetWindowPos(hCtl, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    };

    placeCtrl(st.hHeaderLbl, pad, pad, contentW, hdrH);
    if (st.hList) {
        placeCtrl(st.hList, pad, listY, contentW, listH);
#if defined(_WIN32) && !defined(E2_WIN32_HAL)
        const int c0 = st.S(200);
        const int c1 = st.S(140);
        const int c2 = st.S(180);
        const int c3 = std::max(st.S(180), contentW - c0 - c1 - c2 - st.S(22));
        ListView_SetColumnWidth(st.hList, 0, c0);
        ListView_SetColumnWidth(st.hList, 1, c1);
        ListView_SetColumnWidth(st.hList, 2, c2);
        ListView_SetColumnWidth(st.hList, 3, c3);
#endif
    } else if (st.hDetailEdit) {
        placeCtrl(st.hDetailEdit, pad, listY, contentW, (rowUtilY - pad) - listY);
    }

    if (st.hList && st.hDetailEdit) {
        placeCtrl(st.hDetailEdit, pad, detailY, contentW, detailH);
    }

    int xL = pad;
    if (st.hBtnRescan) {
        placeCtrl(st.hBtnRescan, xL, rowUtilY, wRescan, btnH);
        xL += wRescan + gap;
    }
    if (st.hBtnCopy) {
        placeCtrl(st.hBtnCopy, xL, rowUtilY, wCopy, btnH);
        xL += wCopy + gap;
    }
    if (st.hBtnCheckToggle) {
        placeCtrl(st.hBtnCheckToggle, xL, rowUtilY, wChk, btnH);
        xL += wChk + gap;
    }
    if (st.hBtnRebootDel) {
        placeCtrl(st.hBtnRebootDel, xL, rowUtilY, wReboot, btnH);
    }

    int xR = clientW - pad;
    if (st.hBtnClose) {
        xR -= wClose;
        placeCtrl(st.hBtnClose, xR, rowActionY, wClose, btnH);
        xR -= gap;
    }
    if (st.hBtnAdmin) {
        xR -= wAdmin;
        placeCtrl(st.hBtnAdmin, xR, rowActionY, wAdmin, btnH);
        xR -= gap;
    }
    if (st.hBtnKill) {
        xR -= wKill;
        placeCtrl(st.hBtnKill, xR, rowActionY, wKill, btnH);
        xR -= gap;
    }
    if (st.hBtnGraceful) {
        xR -= wGrace;
        placeCtrl(st.hBtnGraceful, xR, rowActionY, wGrace, btnH);
        xR -= gap;
    }
    if (st.hBtnUnlockHandles) {
        xR -= wUnlockH;
        placeCtrl(st.hBtnUnlockHandles, xR, rowActionY, wUnlockH, btnH);
    }
}

struct CheckedActionPlan {
    std::vector<RemoteHandleCloseRequest> handlesToClose;
    std::vector<std::wstring>             servicesToStop;
    std::vector<uint32_t>                 userPids;
    std::vector<uint64_t>                 userTicks;
    std::vector<uint32_t>                 elevatedPids;
    std::vector<uint64_t>                 elevatedTicks;
    std::vector<std::wstring>             procNames;
    bool                                  releaseSelf = false;
    bool                                  clearReadOnly = false;
    bool                                  checkedExplorerShell = false;
};

void GatherCheckedItems(LockDialogState& st, CheckedActionPlan& out) {
    out = {};
    auto processRowIndex = [&](size_t ri) {
        if (ri >= st.rows.size()) return;
        const LockRowItem& row = st.rows[ri];
        if (row.isProcess && row.indexInVec < st.report.processes.size()) {
            const auto& p = st.report.processes[row.indexInVec];
            if (p.isSelfProcess) {
                out.releaseSelf = true;
                return;
            }
            if (!p.serviceName.empty()) {
                out.servicesToStop.push_back(p.serviceName);
            }
            if (!p.isSystemCritical) {
                for (const auto& h : p.openHandles) {
                    if (h.handleValue != 0 && (h.handleAttributes & 0x1u) == 0u && !h.path.empty()) {
                        out.handlesToClose.push_back({ p.pid, p.startTimeTicks, h.handleValue, h.handleAttributes, h.path });
                    }
                }
                if (p.isShellProcess) out.checkedExplorerShell = true;
                if (p.isElevatedOrProtected) {
                    out.elevatedPids.push_back(p.pid);
                    out.elevatedTicks.push_back(p.startTimeTicks);
                } else {
                    out.userPids.push_back(p.pid);
                    out.userTicks.push_back(p.startTimeTicks);
                }
                out.procNames.push_back(p.processName + L" (PID " + std::to_wstring(p.pid) + L")");
            }
        } else if (!row.isProcess && row.indexInVec < st.report.permissionBlockers.size()) {
            if (st.report.permissionBlockers[row.indexInVec].isReadOnlyAttr) {
                out.clearReadOnly = true;
            }
        }
    };

#if defined(_WIN32) && !defined(E2_WIN32_HAL)
    if (st.hList) {
        const int nItems = ListView_GetItemCount(st.hList);
        for (int li = 0; li < nItems; ++li) {
            if (ListView_GetCheckState(st.hList, li) == 0) continue;
            const int ri = GetRowVecIndexFromListItem(st.hList, li);
            if (ri >= 0) processRowIndex(static_cast<size_t>(ri));
        }
        return;
    }
#endif
    for (size_t ri = 0; ri < st.rows.size(); ++ri) {
        if (st.rows[ri].defaultChecked) processRowIndex(ri);
    }
}

void MaybeRetryOperationAfterUnlock(LockDialogState& st) {
    if (!st.HasRetryableFailure()) return;
    if (!st.report.HasAnyBlockers() ||
        MessageBoxW(
            st.hDlg,
            st.pl ? L"Nadal wykryto niektóre blokady. Czy mimo to ponowić operację teraz?"
                  : L"Some blockers are still present. Retry the operation anyway?",
            st.pl ? L"Ponów operację" : L"Retry Operation",
            MB_YESNO | MB_ICONQUESTION) == IDYES)
    {
        st.cb.retryDeleteJob();
        CloseLockDialogWindow(st);
    }
}

void ExecuteSurgicalHandleUnlock(LockDialogState& st) {
    CheckedActionPlan plan;
    GatherCheckedItems(st, plan);

    if ((plan.releaseSelf || st.HasRetryableFailure()) && st.cb.releaseSelfHold) {
        st.cb.releaseSelfHold();
    }
    if (plan.clearReadOnly || (st.HasRetryableFailure() && st.report.HasReadOnlyBlockers())) {
        ClearReadOnlyAttributes(st.targetPaths);
    }

    if (!plan.handlesToClose.empty()) {
        SetCursor(LoadCursorW(nullptr, IDC_WAIT));
        size_t closedCnt = 0u;
        bool anyDenied = false;
        std::vector<RemoteHandleCloseRequest> elevHandles;
        CloseRemoteFileHandles(plan.handlesToClose, &closedCnt, &anyDenied, &elevHandles);
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));

        if (anyDenied && !elevHandles.empty()) {
            const bool askUac = (MessageBoxW(
                st.hDlg,
                st.pl ? L"Część uchwytów należy do procesu z uprawnieniami Administratora.\nCzy odblokować te uchwyty z użyciem UAC (bez zamykania programu)?"
                      : L"Some handles belong to an elevated Administrator process.\nUnlock those handles via UAC (without closing the app)?",
                st.pl ? L"Odblokuj uchwyty jako Administrator" : L"Unlock Handles as Administrator",
                MB_YESNO | MB_ICONQUESTION) == IDYES);
            if (askUac) {
                ElevatedUnlockRequest req;
                req.handlesToClose = std::move(elevHandles);
                RunElevatedAdminRequest(st.hDlg, req, nullptr);
            }
        }
    }

    PopulateLockDialogScan(st);
    MaybeRetryOperationAfterUnlock(st);
}

void ExecuteUnlockAction(LockDialogState& st, bool forceKill, bool retryAfter) {
    CheckedActionPlan plan;
    GatherCheckedItems(st, plan);

    if ((plan.releaseSelf || retryAfter) && st.cb.releaseSelfHold) {
        st.cb.releaseSelfHold();
    }
    if (plan.clearReadOnly || (retryAfter && st.report.HasReadOnlyBlockers())) {
        ClearReadOnlyAttributes(st.targetPaths);
    }

    if (forceKill && !plan.procNames.empty()) {
        std::wstring confirmMsg = st.pl
            ? L"Czy na pewno chcesz wymusić zakończenie (Kill) zaznaczonych procesów?\n(Niezapisane dane w tych programach mogą zostać utracone)\n\n"
            : L"Force-terminate (Kill) the checked locking processes?\n(Unsaved data in those apps may be lost)\n\n";
        for (size_t i = 0; i < std::min<size_t>(plan.procNames.size(), 10u); ++i) {
            confirmMsg += L"  \x2022 " + plan.procNames[i] + L"\n";
        }
        if (plan.checkedExplorerShell) {
            confirmMsg += st.pl ? L"\nUWAGA: Zaznaczono powłokę pulpitu (explorer.exe)!\n"
                                : L"\nWARNING: The desktop shell (explorer.exe) is checked!\n";
        }
        const wchar_t* cTitle = st.pl ? L"Zakończ procesy blokujące" : L"Terminate Locking Processes";
        if (MessageBoxW(st.hDlg, confirmMsg.c_str(), cTitle, MB_YESNO | MB_ICONWARNING) != IDYES) {
            return;
        }
    }

    SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    bool svcDenied = false;
    std::vector<std::wstring> failedSvcs;
    if (!plan.servicesToStop.empty()) {
        StopLockingServices(plan.servicesToStop, 5000u, &failedSvcs, &svcDenied);
    }

    if (!plan.userPids.empty() || !plan.elevatedPids.empty()) {
        if (!forceKill) {
            std::vector<uint32_t> allGraceful = plan.userPids;
            std::vector<uint64_t> allTicks = plan.userTicks;
            allGraceful.insert(allGraceful.end(), plan.elevatedPids.begin(), plan.elevatedPids.end());
            allTicks.insert(allTicks.end(), plan.elevatedTicks.begin(), plan.elevatedTicks.end());
            CloseProcessesGracefully(allGraceful, 2200u, nullptr, &allTicks);
        } else {
            bool anyDenied = false;
            std::vector<uint32_t> failedUser;
            TerminateLockingProcesses(plan.userPids, 1500u, &failedUser, &anyDenied, &plan.userTicks);
            if (anyDenied) {
                for (uint32_t fp : failedUser) {
                    plan.elevatedPids.push_back(fp);
                    uint64_t tickForFp = 0u;
                    for (size_t k = 0; k < plan.userPids.size(); ++k) {
                        if (plan.userPids[k] == fp) { tickForFp = plan.userTicks[k]; break; }
                    }
                    plan.elevatedTicks.push_back(tickForFp);
                }
            }
        }
    }
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));

    if ((forceKill && !plan.elevatedPids.empty()) || (svcDenied && !failedSvcs.empty())) {
        const bool askUac = (MessageBoxW(
            st.hDlg,
            st.pl ? L"Co najmniej jeden zaznaczony proces lub usługa działa jako Administrator.\nCzy uruchomić odblokowanie z uprawnieniami Administratora (UAC)?"
                  : L"At least one checked process or service requires Administrator privileges.\nRequest Administrator privileges (UAC) to stop/terminate it?",
            st.pl ? L"Wymagane uprawnienia Administratora" : L"Administrator Privileges Required",
            MB_YESNO | MB_ICONQUESTION) == IDYES);
        if (askUac) {
            ElevatedUnlockRequest req;
            req.servicesToStop = std::move(failedSvcs);
            if (forceKill) {
                req.pidsToKill = std::move(plan.elevatedPids);
                req.pidStartTicks = std::move(plan.elevatedTicks);
            }
            RunElevatedAdminRequest(st.hDlg, req, nullptr);
        }
    }

    PopulateLockDialogScan(st);
    if (retryAfter) MaybeRetryOperationAfterUnlock(st);
}

// Inspection / Rename / Move / Copy UAC button handler: NEVER deletes any user files!
void ExecuteAdminUnlockOnlyAction(LockDialogState& st) {
    CheckedActionPlan plan;
    GatherCheckedItems(st, plan);

    std::wstring prompt = st.pl
        ? L"Czy odblokować zaznaczone blokady z uprawnieniami Administratora (UAC)?\n"
          L"(Żadne pliki użytkownika NIE zostaną usunięte — zostaną tylko zamknięte uchwyty / zatrzymane usługi / wyczyszczony atrybut Tylko do odczytu.)"
        : L"Unlock the checked blockers using Administrator privileges (UAC)?\n"
          L"(NO user files will be deleted — this only closes handles, stops checked services, and clears Read-Only attributes.)";
    bool alsoKillElevated = false;
    if (!plan.elevatedPids.empty() && plan.handlesToClose.empty() && plan.servicesToStop.empty()) {
        prompt += st.pl
            ? L"\n\nUwaga: Zaznaczony proces trzyma blokadę obrazu (.exe/.dll) — jego odblokowanie zakończy ten proces."
            : L"\n\nNote: Checked process holds a mapped image lock (.exe/.dll) — unlocking it will terminate that process.";
        alsoKillElevated = true;
    }

    if (MessageBoxW(st.hDlg, prompt.c_str(),
                    st.pl ? L"Odblokuj jako Administrator (UAC)" : L"Unlock as Administrator (UAC)",
                    MB_YESNO | MB_ICONQUESTION) != IDYES)
    {
        return;
    }

    if (st.cb.releaseSelfHold) st.cb.releaseSelfHold();

    ElevatedUnlockRequest req;
    req.handlesToClose = std::move(plan.handlesToClose);
    req.servicesToStop = std::move(plan.servicesToStop);
    if (alsoKillElevated) {
        req.pidsToKill = std::move(plan.elevatedPids);
        req.pidStartTicks = std::move(plan.elevatedTicks);
    }
    if (plan.clearReadOnly || st.report.HasReadOnlyBlockers()) {
        req.pathsToClearReadOnly = st.targetPaths;
    }
    // CRITICAL INVARIANT: req.pathsToDeleteNow is ALWAYS EMPTY in Unlock-Only mode!

    std::wstring errSummary;
    const bool ok = RunElevatedAdminRequest(st.hDlg, req, &errSummary);
    PopulateLockDialogScan(st);
    if (ok && st.HasRetryableFailure()) {
        MaybeRetryOperationAfterUnlock(st);
        return;
    }
    if (!ok && !errSummary.empty()) {
        MessageBoxW(st.hDlg, errSummary.c_str(),
                    st.pl ? L"Odblokowanie jako Administrator" : L"Administrator Unlock",
                    MB_OK | MB_ICONWARNING);
    }
}

void ExecuteAdminDeleteAction(LockDialogState& st) {
    if (!st.AllowDestructiveDelete()) {
        ExecuteAdminUnlockOnlyAction(st);
        return;
    }

    std::vector<std::wstring> deletableTargets;
    for (const auto& tp : st.targetPaths) {
        if (!tp.empty() && !TransferManager::IsDeleteRefused(tp)) {
            deletableTargets.push_back(tp);
        }
    }
    if (deletableTargets.empty()) return;

    CheckedActionPlan plan;
    GatherCheckedItems(st, plan);

    std::wstring prompt = st.pl
        ? L"Czy chcesz TRWALE usunąć zablokowane elementy z uprawnieniami Administratora (UAC)?\n"
          L"(Zaznaczone uchwyty / usługi / procesy blokujące zostaną najpierw odblokowane.)\n\n"
        : L"PERMANENTLY delete the blocked items using Administrator privileges (UAC)?\n"
          L"(Checked handles / services / locking processes will be unlocked first.)\n\n";
    for (size_t i = 0; i < std::min<size_t>(deletableTargets.size(), 6u); ++i) {
        prompt += L"  \x2022 " + deletableTargets[i] + L"\n";
    }
    if (MessageBoxW(st.hDlg, prompt.c_str(),
                    st.pl ? L"Usuń jako Administrator (UAC)" : L"Delete as Administrator (UAC)",
                    MB_YESNO | MB_ICONWARNING) != IDYES)
    {
        return;
    }

    if (st.cb.releaseSelfHold) st.cb.releaseSelfHold();
    if (!plan.handlesToClose.empty()) CloseRemoteFileHandles(plan.handlesToClose, nullptr, nullptr, nullptr);
    if (!plan.userPids.empty()) TerminateLockingProcesses(plan.userPids, 1200u, nullptr, nullptr, &plan.userTicks);

    ElevatedUnlockRequest req;
    req.handlesToClose = std::move(plan.handlesToClose);
    req.servicesToStop = std::move(plan.servicesToStop);
    req.pidsToKill = plan.userPids;
    req.pidStartTicks = plan.userTicks;
    req.pidsToKill.insert(req.pidsToKill.end(), plan.elevatedPids.begin(), plan.elevatedPids.end());
    req.pidStartTicks.insert(req.pidStartTicks.end(), plan.elevatedTicks.begin(), plan.elevatedTicks.end());
    req.pathsToDeleteNow = std::move(deletableTargets);

    std::wstring errSummary;
    if (RunElevatedAdminRequest(st.hDlg, req, &errSummary)) {
        if (st.cb.onAdminDeleteSucceeded) st.cb.onAdminDeleteSucceeded();
        CloseLockDialogWindow(st);
        return;
    }

    PopulateLockDialogScan(st);
    if (!errSummary.empty()) {
        MessageBoxW(st.hDlg, errSummary.c_str(),
                    st.pl ? L"Usuwanie jako Administrator" : L"Administrator Delete",
                    MB_OK | MB_ICONWARNING);
    }
}

void ExecuteScheduleRebootDeleteAction(LockDialogState& st) {
    if (!st.AllowDestructiveDelete()) return;
    std::vector<std::wstring> deletableTargets;
    for (const auto& tp : st.targetPaths) {
        if (!tp.empty() && !TransferManager::IsDeleteRefused(tp)) deletableTargets.push_back(tp);
    }
    if (deletableTargets.empty()) return;

    std::wstring prompt = st.pl
        ? L"Czy zaplanować usunięcie tych elementów podczas następnego uruchomienia systemu Windows (PendingFileRenameOperations)?\n"
          L"To bezpieczne rozwiązanie dla zablokowanych bibliotek .dll i uruchomionych usług systemowych.\n\n"
        : L"Schedule these items for automatic deletion on the next Windows reboot (PendingFileRenameOperations)?\n"
          L"This safely removes stubborn in-use .dll / .exe files at boot before apps start.\n\n";
    for (size_t i = 0; i < std::min<size_t>(deletableTargets.size(), 6u); ++i) {
        prompt += L"  \x2022 " + deletableTargets[i] + L"\n";
    }
    if (MessageBoxW(st.hDlg, prompt.c_str(),
                    st.pl ? L"Usuń przy restarcie systemu" : L"Schedule Delete on Reboot",
                    MB_YESNO | MB_ICONQUESTION) != IDYES)
    {
        return;
    }

    size_t schedCnt = 0u;
    bool accessDenied = false;
    bool ok = ScheduleDeleteOnReboot(deletableTargets, &schedCnt, &accessDenied);
    if (!ok && accessDenied) {
        ElevatedUnlockRequest req;
        req.pathsToDeleteOnReboot = deletableTargets;
        std::wstring err;
        ok = RunElevatedAdminRequest(st.hDlg, req, &err);
    }

    if (ok) {
        MessageBoxW(
            st.hDlg,
            st.pl ? L"\x2713 Zaplanowano usunięcie elementów przy następnym uruchomieniu systemu Windows."
                  : L"\x2713 Items have been scheduled for deletion on the next Windows reboot.",
            st.pl ? L"Zaplanowano usunięcie" : L"Scheduled for Reboot",
            MB_OK | MB_ICONINFORMATION);
        CloseLockDialogWindow(st);
    } else {
        MessageBoxW(
            st.hDlg,
            st.pl ? L"Nie udało się zapisać operacji usuwania przy restarcie (wymagane uprawnienia Administratora)."
                  : L"Could not schedule reboot deletion (Administrator privileges are required).",
            st.pl ? L"Błąd planowania" : L"Schedule Failed",
            MB_OK | MB_ICONWARNING);
    }
}

#if defined(_WIN32) && !defined(E2_WIN32_HAL)
void ShowLockRowContextMenu(LockDialogState& st, int screenX, int screenY) {
    if (!st.hList) return;
    const int selListIdx = ListView_GetNextItem(st.hList, -1, LVNI_SELECTED);
    const int ri = GetRowVecIndexFromListItem(st.hList, selListIdx);
    const bool hasRow = (ri >= 0 && static_cast<size_t>(ri) < st.rows.size());

    if (screenX == -1 && screenY == -1) {
        RECT itemRc = {};
        if (hasRow && ListView_GetItemRect(st.hList, selListIdx, &itemRc, LVIR_BOUNDS)) {
            POINT pt = { itemRc.left + st.S(24), itemRc.bottom };
            ClientToScreen(st.hList, &pt);
            screenX = pt.x;
            screenY = pt.y;
        } else {
            POINT pt = { st.S(24), st.S(24) };
            ClientToScreen(st.hList, &pt);
            screenX = pt.x;
            screenY = pt.y;
        }
    }

    const LockRowItem* row = hasRow ? &st.rows[static_cast<size_t>(ri)] : nullptr;
    const LockingProcessEntry* proc = (row && row->isProcess && row->indexInVec < st.report.processes.size())
        ? &st.report.processes[row->indexInVec] : nullptr;
    const PermissionBlockerEntry* perm = (row && !row->isProcess && row->indexInVec < st.report.permissionBlockers.size())
        ? &st.report.permissionBlockers[row->indexInVec] : nullptr;

    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    if (proc) {
        const UINT fUnlockH = (proc->HasClosableHandles() || proc->isSelfProcess) ? MF_STRING : (MF_STRING | MF_GRAYED);
        AppendMenuW(hMenu, fUnlockH, IDM_LOCK_CTX_UNLOCK_HANDLES,
                    st.pl ? L"Odblokuj same uchwyty pliku/folderu (bez zamykania programu)"
                          : L"Unlock Handle(s) Only (keep app running)");

        if (!proc->serviceName.empty()) {
            const std::wstring svcLbl = (st.pl ? L"Zatrzymaj usługę: " : L"Stop Service: ") + proc->serviceName;
            AppendMenuW(hMenu, MF_STRING, IDM_LOCK_CTX_STOP_SERVICE, svcLbl.c_str());
        } else {
            AppendMenuW(hMenu, MF_STRING | MF_GRAYED, IDM_LOCK_CTX_STOP_SERVICE,
                        st.pl ? L"Zatrzymaj usługę" : L"Stop Service");
        }

        const UINT fProcAct = (!proc->isSystemCritical && !proc->isSelfProcess) ? MF_STRING : (MF_STRING | MF_GRAYED);
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, fProcAct, IDM_LOCK_CTX_CLOSE_WIN,
                    st.pl ? L"Zamknij okno programu (WM_CLOSE)" : L"Close Window (WM_CLOSE)");
        AppendMenuW(hMenu, fProcAct, IDM_LOCK_CTX_KILL_PROC,
                    st.pl ? L"Wymuś zakończenie procesu (Kill)" : L"Terminate Process (Kill)");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, !proc->exeFullPath.empty() ? MF_STRING : (MF_STRING | MF_GRAYED),
                    IDM_LOCK_CTX_OPEN_EXE_LOC,
                    st.pl ? L"Pokaż plik wykonywalny w Eksploratorze" : L"Open Executable Location");
        AppendMenuW(hMenu, MF_STRING, IDM_LOCK_CTX_COPY_PID,
                    st.pl ? L"Kopiuj PID" : L"Copy PID");
    }

    if (hasRow || !st.targetPaths.empty()) {
        AppendMenuW(hMenu, MF_STRING, IDM_LOCK_CTX_COPY_PATH,
                    st.pl ? L"Kopiuj zablokowaną ścieżkę" : L"Copy Locked Path");
    }

    if (GetMenuItemCount(hMenu) > 0) {
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    }
    AppendMenuW(hMenu, MF_STRING, IDC_LOCK_BTN_RESCAN, st.pl ? L"Odśwież" : L"Refresh");
    AppendMenuW(hMenu, MF_STRING, IDC_LOCK_BTN_COPY,   st.pl ? L"Kopiuj raport" : L"Copy Report");

    if (st.AllowDestructiveDelete()) {
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, IDM_LOCK_CTX_DELAY_REBOOT,
                    st.pl ? L"Zaplanuj usunięcie przy restarcie systemu (UAC)..."
                          : L"Schedule Delete on Reboot (UAC)...");
    }

    const int cmd = static_cast<int>(TrackPopupMenu(
        hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenX, screenY, 0, st.hDlg, nullptr));
    DestroyMenu(hMenu);
    if (cmd == 0) return;

    switch (cmd) {
    case IDC_LOCK_BTN_RESCAN:
        PopulateLockDialogScan(st);
        break;
    case IDC_LOCK_BTN_COPY:
        FileEngine::CopyTextToClipboard(st.hDlg, FormatLockReportText(st.report, st.pl));
        break;
    case IDM_LOCK_CTX_UNLOCK_HANDLES:
        if (proc) {
            if (proc->isSelfProcess) {
                if (st.cb.releaseSelfHold) st.cb.releaseSelfHold();
            } else {
                std::vector<RemoteHandleCloseRequest> reqs;
                for (const auto& h : proc->openHandles) {
                    if (h.handleValue != 0 && (h.handleAttributes & 0x1u) == 0u && !h.path.empty()) {
                        reqs.push_back({ proc->pid, proc->startTimeTicks, h.handleValue, h.handleAttributes, h.path });
                    }
                }
                bool denied = false;
                std::vector<RemoteHandleCloseRequest> elevReqs;
                CloseRemoteFileHandles(reqs, nullptr, &denied, &elevReqs);
                if (denied && !elevReqs.empty()) {
                    ElevatedUnlockRequest uacReq;
                    uacReq.handlesToClose = std::move(elevReqs);
                    RunElevatedAdminRequest(st.hDlg, uacReq, nullptr);
                }
            }
            PopulateLockDialogScan(st);
            MaybeRetryOperationAfterUnlock(st);
        }
        break;
    case IDM_LOCK_CTX_STOP_SERVICE:
        if (proc && !proc->serviceName.empty()) {
            bool denied = false;
            std::vector<std::wstring> failed;
            if (!StopLockingServices({ proc->serviceName }, 6000u, &failed, &denied) && denied) {
                ElevatedUnlockRequest uacReq;
                uacReq.servicesToStop = { proc->serviceName };
                RunElevatedAdminRequest(st.hDlg, uacReq, nullptr);
            }
            PopulateLockDialogScan(st);
            MaybeRetryOperationAfterUnlock(st);
        }
        break;
    case IDM_LOCK_CTX_CLOSE_WIN:
        if (proc && !proc->isSystemCritical && !proc->isSelfProcess) {
            const std::vector<uint64_t> ticks = { proc->startTimeTicks };
            CloseProcessesGracefully({ proc->pid }, 2200u, nullptr, &ticks);
            PopulateLockDialogScan(st);
            MaybeRetryOperationAfterUnlock(st);
        }
        break;
    case IDM_LOCK_CTX_KILL_PROC:
        if (proc && !proc->isSystemCritical && !proc->isSelfProcess) {
            const std::wstring msg = (st.pl ? L"Wymusić zakończenie procesu " : L"Force-terminate process ") +
                                     proc->processName + L" (PID " + std::to_wstring(proc->pid) + L")?";
            if (MessageBoxW(st.hDlg, msg.c_str(), st.pl ? L"Zakończ proces" : L"Terminate Process", MB_YESNO | MB_ICONWARNING) == IDYES) {
                const std::vector<uint64_t> ticks = { proc->startTimeTicks };
                bool denied = false;
                if (!TerminateLockingProcesses({ proc->pid }, 1500u, nullptr, &denied, &ticks) && denied) {
                    ElevatedUnlockRequest uacReq;
                    uacReq.pidsToKill = { proc->pid };
                    uacReq.pidStartTicks = ticks;
                    RunElevatedAdminRequest(st.hDlg, uacReq, nullptr);
                }
                PopulateLockDialogScan(st);
                MaybeRetryOperationAfterUnlock(st);
            }
        }
        break;
    case IDM_LOCK_CTX_OPEN_EXE_LOC:
        if (proc && !proc->exeFullPath.empty()) {
            const std::wstring arg = L"/select,\"" + proc->exeFullPath + L"\"";
            ShellExecuteW(st.hDlg, L"open", L"explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
        }
        break;
    case IDM_LOCK_CTX_COPY_PID:
        if (proc) FileEngine::CopyTextToClipboard(st.hDlg, std::to_wstring(proc->pid));
        break;
    case IDM_LOCK_CTX_COPY_PATH:
        if (proc && !proc->lockedPaths.empty()) {
            std::wstring allPaths;
            for (size_t i = 0; i < proc->lockedPaths.size(); ++i) {
                if (i > 0) allPaths += L"\r\n";
                allPaths += proc->lockedPaths[i];
            }
            FileEngine::CopyTextToClipboard(st.hDlg, allPaths);
        } else if (perm) {
            FileEngine::CopyTextToClipboard(st.hDlg, perm->path);
        } else if (!st.targetPaths.empty()) {
            FileEngine::CopyTextToClipboard(st.hDlg, st.targetPaths[0]);
        }
        break;
    case IDM_LOCK_CTX_DELAY_REBOOT:
        ExecuteScheduleRebootDeleteAction(st);
        break;
    default:
        break;
    }
}
#endif

LRESULT CALLBACK LockInspectorDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* st = reinterpret_cast<LockDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
#if defined(_WIN32) && !defined(E2_WIN32_HAL)
    case DM_GETDEFID:
        return MAKELONG(IDCANCEL, DC_HASDEFID);
    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        if (mmi) {
            const int dpi = st ? st->dpi : 96;
            mmi->ptMinTrackSize.x = MulDiv(740, dpi, 96);
            mmi->ptMinTrackSize.y = MulDiv(420, dpi, 96);
        }
        return 0;
    }
#endif
    case WM_SIZE:
        if (st && wParam != SIZE_MINIMIZED) {
            LayoutLockDialogControls(*st, LOWORD(lParam), HIWORD(lParam));
        }
        return 0;

#if defined(_WIN32) && !defined(E2_WIN32_HAL)
    case WM_ERASEBKGND:
        if (st && st->hBrDlgBg) {
            RECT rc = {};
            GetClientRect(hwnd, &rc);
            FillRect(reinterpret_cast<HDC>(wParam), &rc, st->hBrDlgBg);
            return 1;
        }
        break;

    case WM_CTLCOLORSTATIC: {
        if (st) {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            HWND hCtl = reinterpret_cast<HWND>(lParam);
            if (hCtl == st->hDetailEdit && st->hBrEditBg) {
                SetTextColor(hdc, g_darkTheme ? RGB(0xE2, 0xE8, 0xF0) : RGB(0x1E, 0x29, 0x3B));
                SetBkColor(hdc, g_darkTheme ? RGB(0x1E, 0x22, 0x2A) : RGB(0xFF, 0xFF, 0xFF));
                return reinterpret_cast<LRESULT>(st->hBrEditBg);
            }
            if (st->hBrDlgBg) {
                SetTextColor(hdc, g_darkTheme ? RGB(0xE2, 0xE8, 0xF0) : GetSysColor(COLOR_WINDOWTEXT));
                SetBkColor(hdc, g_darkTheme ? RGB(0x18, 0x1B, 0x20) : GetSysColor(COLOR_BTNFACE));
                return reinterpret_cast<LRESULT>(st->hBrDlgBg);
            }
        }
        break;
    }

    case WM_CTLCOLOREDIT: {
        if (st && st->hBrEditBg) {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetTextColor(hdc, g_darkTheme ? RGB(0xE2, 0xE8, 0xF0) : RGB(0x1E, 0x29, 0x3B));
            SetBkColor(hdc, g_darkTheme ? RGB(0x1E, 0x22, 0x2A) : RGB(0xFF, 0xFF, 0xFF));
            return reinterpret_cast<LRESULT>(st->hBrEditBg);
        }
        break;
    }

    case WM_CTLCOLORBTN:
        if (g_darkTheme && st && st->hBrDlgBg) {
            return reinterpret_cast<LRESULT>(st->hBrDlgBg);
        }
        break;

    case WM_CONTEXTMENU:
        if (st && st->hList && reinterpret_cast<HWND>(wParam) == st->hList) {
            const int sx = static_cast<short>(LOWORD(lParam));
            const int sy = static_cast<short>(HIWORD(lParam));
            ShowLockRowContextMenu(*st, sx, sy);
            return 0;
        }
        break;

    case WM_NOTIFY:
        if (st && wParam == IDC_LOCK_LIST) {
            const auto* nmh = reinterpret_cast<const NMHDR*>(lParam);
            if (!nmh) break;
            if (nmh->code == LVN_ITEMCHANGED) {
                const auto* nmlv = reinterpret_cast<const NMLISTVIEW*>(lParam);
                if (nmlv && (nmlv->uChanged & LVIF_STATE) &&
                    ((nmlv->uNewState ^ nmlv->uOldState) & LVIS_SELECTED) != 0)
                {
                    const int selList = ListView_GetNextItem(st->hList, -1, LVNI_SELECTED);
                    const int selVec  = GetRowVecIndexFromListItem(st->hList, selList);
                    UpdateLockDetailPane(*st, selVec);
                }
            } else if (nmh->code == LVN_COLUMNCLICK) {
                const auto* nmlv = reinterpret_cast<const NMLISTVIEW*>(lParam);
                if (nmlv) {
                    if (st->sortCol == nmlv->iSubItem) st->sortAsc = !st->sortAsc;
                    else { st->sortCol = nmlv->iSubItem; st->sortAsc = true; }
                    ApplyLockListSort(*st);
                }
            } else if (nmh->code == NM_RCLICK) {
                POINT pt = {};
                GetCursorPos(&pt);
                ShowLockRowContextMenu(*st, pt.x, pt.y);
                return TRUE;
            }
        }
        break;
#endif

    case WM_COMMAND:
        if (!st) break;
        switch (LOWORD(wParam)) {
        case IDC_LOCK_BTN_RESCAN:
            PopulateLockDialogScan(*st);
            return 0;
        case IDC_LOCK_BTN_COPY:
            FileEngine::CopyTextToClipboard(hwnd, FormatLockReportText(st->report, st->pl));
            return 0;
        case IDC_LOCK_BTN_CHKALL: {
#if defined(_WIN32) && !defined(E2_WIN32_HAL)
            if (st->hList) {
                const int nItems = ListView_GetItemCount(st->hList);
                bool anyCurrentlyChecked = false;
                for (int li = 0; li < nItems; ++li) {
                    if (ListView_GetCheckState(st->hList, li) != 0) {
                        anyCurrentlyChecked = true;
                        break;
                    }
                }
                for (int li = 0; li < nItems; ++li) {
                    bool newChk = false;
                    if (!anyCurrentlyChecked) {
                        const int ri = GetRowVecIndexFromListItem(st->hList, li);
                        if (ri >= 0 && static_cast<size_t>(ri) < st->rows.size()) {
                            const LockRowItem& r = st->rows[static_cast<size_t>(ri)];
                            if (r.isProcess && r.indexInVec < st->report.processes.size()) {
                                const auto& p = st->report.processes[r.indexInVec];
                                newChk = (!p.isSystemCritical || !p.serviceName.empty()) && !p.isShellProcess;
                            } else if (!r.isProcess && r.indexInVec < st->report.permissionBlockers.size()) {
                                newChk = st->report.permissionBlockers[r.indexInVec].isReadOnlyAttr;
                            }
                        }
                    }
                    ListView_SetCheckState(st->hList, li, newChk ? TRUE : FALSE);
                }
            }
#endif
            return 0;
        }
        case IDC_LOCK_BTN_UNLOCK_HANDLES:
            ExecuteSurgicalHandleUnlock(*st);
            return 0;
        case IDC_LOCK_BTN_GRACE:
            ExecuteUnlockAction(*st, /*forceKill=*/false, /*retryAfter=*/st->HasRetryableFailure());
            return 0;
        case IDC_LOCK_BTN_KILL:
            ExecuteUnlockAction(*st, /*forceKill=*/true, /*retryAfter=*/st->HasRetryableFailure());
            return 0;
        case IDC_LOCK_BTN_ADMIN:
            if (st->AllowDestructiveDelete()) ExecuteAdminDeleteAction(*st);
            else                              ExecuteAdminUnlockOnlyAction(*st);
            return 0;
        case IDC_LOCK_BTN_REBOOT_DEL:
            ExecuteScheduleRebootDeleteAction(*st);
            return 0;
        case IDCANCEL:
        case IDOK:
            CloseLockDialogWindow(*st);
            return 0;
        }
        break;

    case WM_CLOSE:
        if (st) CloseLockDialogWindow(*st);
        else DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

void ShowFileLockInspectorDialog(
    HWND ownerHwnd,
    HINSTANCE hInst,
    HFONT fallbackFont,
    Lang lang,
    const std::vector<std::wstring>& targetPaths,
    const LockDialogCallbacks& callbacks)
{
    if (targetPaths.empty()) return;
    if (!hInst) hInst = GetModuleHandleW(nullptr);

    static bool s_clsRegistered = false;
    const wchar_t* clsName = L"Explorer2FileLockInspectorDlg";
    if (!s_clsRegistered) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = LockInspectorDlgProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
#ifdef COLOR_BTNFACE
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
#else
        wc.hbrBackground = nullptr;
#endif
        wc.lpszClassName = clsName;
        RegisterClassExW(&wc);
        s_clsRegistered = true;
    }

    LockDialogState st;
    st.ownerHwnd = ownerHwnd;
    st.pl = (lang == Lang::PL);
    st.dpi = static_cast<int>(GetWindowDpiCompat(ownerHwnd));
    st.targetPaths = targetPaths;
    st.cb = callbacks;

    st.hFont = CreateFontW(
        -st.S(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    st.hBoldFont = CreateFontW(
        -st.S(13), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
#if defined(_WIN32) && !defined(E2_WIN32_HAL)
    st.hBrDlgBg  = CreateSolidBrush(g_darkTheme ? RGB(0x18, 0x1B, 0x20) : GetSysColor(COLOR_BTNFACE));
    st.hBrEditBg = CreateSolidBrush(g_darkTheme ? RGB(0x1E, 0x22, 0x2A) : RGB(0xFF, 0xFF, 0xFF));
#endif
    HFONT activeFont = st.hFont ? st.hFont : fallbackFont;

    RECT ownerRc = {};
    if (!ownerHwnd || !GetWindowRect(ownerHwnd, &ownerRc)) {
        ownerRc = { 100, 100, 1300, 860 };
    }

    int dlgW = st.S(990);
    int dlgH = st.S(550);
#ifdef _WIN32
    HMONITOR hMon = MonitorFromWindow(ownerHwnd ? ownerHwnd : GetDesktopWindow(), MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    if (hMon && GetMonitorInfoW(hMon, &mi)) {
        const int workW = mi.rcWork.right - mi.rcWork.left;
        const int workH = mi.rcWork.bottom - mi.rcWork.top;
        dlgW = std::min(dlgW, std::max(st.S(740), workW - st.S(40)));
        dlgH = std::min(dlgH, std::max(st.S(420), workH - st.S(40)));
    }
#endif
    int dlgX = ownerRc.left + ((ownerRc.right - ownerRc.left) - dlgW) / 2;
    int dlgY = ownerRc.top + ((ownerRc.bottom - ownerRc.top) - dlgH) / 2;
#ifdef _WIN32
    if (mi.rcWork.right > mi.rcWork.left) {
        dlgX = std::clamp(dlgX, static_cast<int>(mi.rcWork.left), std::max<int>(mi.rcWork.left, mi.rcWork.right - dlgW));
        dlgY = std::clamp(dlgY, static_cast<int>(mi.rcWork.top),  std::max<int>(mi.rcWork.top,  mi.rcWork.bottom - dlgH));
    }
#endif

    const LockFailureOpKind op = st.cb.EffectiveOp();
    const wchar_t* dlgTitle = nullptr;
    switch (op) {
    case LockFailureOpKind::Delete:
        dlgTitle = st.pl ? L"Nie wszystko zostało usunięte — Wykryte blokady i odblokowanie"
                         : L"Not Everything Was Deleted — Detected File Locks & Unlocker";
        break;
    case LockFailureOpKind::Rename:
        dlgTitle = st.pl ? L"Nie można zmienić nazwy — Plik lub folder jest zablokowany"
                         : L"Could Not Rename — File or Folder Is Locked";
        break;
    case LockFailureOpKind::Move:
    case LockFailureOpKind::Copy:
        dlgTitle = st.pl ? L"Błąd transferu — Wykryte blokady plików i odblokowanie"
                         : L"Transfer Blocked — Detected File Locks & Unlocker";
        break;
    default:
        dlgTitle = st.pl ? L"Inspektor blokad plików i folderów (Co trzyma uchwyty?)"
                         : L"File & Folder Lock Inspector & Unlocker";
        break;
    }

    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        clsName, dlgTitle,
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MAXIMIZEBOX,
        dlgX, dlgY, dlgW, dlgH,
        ownerHwnd, nullptr, hInst, nullptr);
    if (!hDlg) {
        if (st.hBrEditBg) DeleteObject(st.hBrEditBg);
        if (st.hBrDlgBg) DeleteObject(st.hBrDlgBg);
        if (st.hBoldFont) DeleteObject(st.hBoldFont);
        if (st.hFont) DeleteObject(st.hFont);
        return;
    }
    st.hDlg = hDlg;
    SetWindowLongPtrW(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&st));
#if defined(_WIN32) && !defined(E2_WIN32_HAL)
    ApplyDarkWindowChromeCompat(hDlg, g_darkTheme);
#endif

    st.hHeaderLbl = CreateWindowExW(
        0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
        0, 0, 100, 40, hDlg, nullptr, hInst, nullptr);

#if defined(_WIN32) && !defined(E2_WIN32_HAL)
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&icc);

    st.hList = CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        0, 0, 100, 100, hDlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LOCK_LIST)), hInst, nullptr);
    if (st.hList) {
        ListView_SetExtendedListViewStyle(
            st.hList,
            LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
        ApplyControlVisualThemeCompat(st.hList, g_darkTheme ? L"DarkMode_Explorer" : L"Explorer");
        const COLORREF lvBg = g_darkTheme ? RGB(0x1E, 0x22, 0x2A) : RGB(0xFF, 0xFF, 0xFF);
        const COLORREF lvFg = g_darkTheme ? RGB(0xE2, 0xE8, 0xF0) : RGB(0x1E, 0x1E, 0x1E);
        ListView_SetBkColor(st.hList, lvBg);
        ListView_SetTextBkColor(st.hList, lvBg);
        ListView_SetTextColor(st.hList, lvFg);

        const int iconPx = st.S(16);
        st.hImgList = ImageList_Create(iconPx, iconPx, ILC_COLOR32 | ILC_MASK, 8, 16);
        if (st.hImgList) ListView_SetImageList(st.hList, st.hImgList, LVSIL_SMALL);

        auto addCol = [&](int idx, int w, const wchar_t* text) {
            LVCOLUMNW col = {};
            col.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
            col.fmt = LVCFMT_LEFT;
            col.cx = w;
            col.pszText = const_cast<wchar_t*>(text);
            col.iSubItem = idx;
            ListView_InsertColumn(st.hList, idx, &col);
        };
        addCol(0, st.S(200), st.pl ? L"Program / Blokada" : L"Process / Blocker");
        addCol(1, st.S(140), st.pl ? L"PID / Status"      : L"PID / Status");
        addCol(2, st.S(180), st.pl ? L"Rodzaj blokady"    : L"Lock Kind");
        addCol(3, st.S(280), st.pl ? L"Zablokowana ścieżka" : L"Locked Path");
    }
#endif

    st.hDetailEdit = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        0, 0, 100, 80, hDlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LOCK_DETAILS)), hInst, nullptr);

    st.hBtnRescan = CreateWindowExW(0, L"BUTTON", st.pl ? L"Odśwież" : L"Rescan",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, 78, 28, hDlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LOCK_BTN_RESCAN)), hInst, nullptr);
    st.hBtnCopy = CreateWindowExW(0, L"BUTTON", st.pl ? L"Kopiuj raport" : L"Copy Report",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, 98, 28, hDlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LOCK_BTN_COPY)), hInst, nullptr);
#if defined(_WIN32) && !defined(E2_WIN32_HAL)
    st.hBtnCheckToggle = CreateWindowExW(0, L"BUTTON", st.pl ? L"Zaznacz / Odznacz" : L"Check / Uncheck",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, 112, 28, hDlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LOCK_BTN_CHKALL)), hInst, nullptr);
#endif

#ifdef _WIN32
    if (st.AllowDestructiveDelete()) {
        st.hBtnRebootDel = CreateWindowExW(0, L"BUTTON",
            st.pl ? L"Usuń przy restarcie" : L"Delete on Reboot",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            0, 0, 140, 28, hDlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LOCK_BTN_REBOOT_DEL)), hInst, nullptr);
        if (st.hBtnRebootDel) SendMessageW(st.hBtnRebootDel, BCM_SETSHIELD, 0, TRUE);
    }

    st.hBtnUnlockHandles = CreateWindowExW(0, L"BUTTON",
        st.HasRetryableFailure()
            ? (st.pl ? L"Odblokuj uchwyty i ponów" : L"Unlock Handles & Retry")
            : (st.pl ? L"Odblokuj uchwyty" : L"Unlock Handles"),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, 140, 28, hDlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LOCK_BTN_UNLOCK_HANDLES)), hInst, nullptr);
#endif

    st.hBtnGraceful = CreateWindowExW(0, L"BUTTON",
        st.HasRetryableFailure()
            ? (st.pl ? L"Zamknij i ponów" : L"Close & Retry")
            : (st.pl ? L"Zamknij programy" : L"Close Checked"),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, 128, 28, hDlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LOCK_BTN_GRACE)), hInst, nullptr);

    st.hBtnKill = CreateWindowExW(0, L"BUTTON",
        st.HasRetryableFailure()
            ? (st.pl ? L"Kill i ponów" : L"Kill & Retry")
            : (st.pl ? L"Zakończ (Kill)" : L"Kill Checked"),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, 140, 28, hDlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LOCK_BTN_KILL)), hInst, nullptr);

#ifdef _WIN32
    // CRITICAL UX FIX: NEVER show "Delete as Admin (UAC)" unless EffectiveOp() == LockFailureOpKind::Delete!
    st.hBtnAdmin = CreateWindowExW(0, L"BUTTON",
        st.AllowDestructiveDelete()
            ? (st.pl ? L"Usuń jako Admin (UAC)" : L"Delete as Admin (UAC)")
            : (st.pl ? L"Odblokuj jako Admin (UAC)" : L"Unlock as Admin (UAC)"),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, 158, 28, hDlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LOCK_BTN_ADMIN)), hInst, nullptr);
    if (st.hBtnAdmin) {
        SendMessageW(st.hBtnAdmin, BCM_SETSHIELD, 0, TRUE);
    }
#endif

    st.hBtnClose = CreateWindowExW(0, L"BUTTON", st.pl ? L"Zamknij" : L"Close",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        0, 0, 78, 28, hDlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)), hInst, nullptr);

    HWND ctrls[] = {
        st.hList, st.hDetailEdit, st.hBtnRescan, st.hBtnCopy, st.hBtnCheckToggle,
        st.hBtnRebootDel, st.hBtnUnlockHandles, st.hBtnGraceful, st.hBtnKill, st.hBtnAdmin, st.hBtnClose
    };
    if (activeFont) {
        for (HWND hc : ctrls) {
            if (hc) SendMessageW(hc, WM_SETFONT, reinterpret_cast<WPARAM>(activeFont), TRUE);
        }
        if (st.hHeaderLbl) {
            SendMessageW(st.hHeaderLbl, WM_SETFONT, reinterpret_cast<WPARAM>(st.hBoldFont ? st.hBoldFont : activeFont), TRUE);
        }
    }
#if defined(_WIN32) && !defined(E2_WIN32_HAL)
    if (g_darkTheme) {
        for (HWND hc : ctrls) {
            if (hc && hc != st.hList) ApplyControlVisualThemeCompat(hc, L"DarkMode_Explorer");
        }
    }
#endif

    RECT cli = {};
    GetClientRect(hDlg, &cli);
    LayoutLockDialogControls(st, cli.right - cli.left, cli.bottom - cli.top);
    ShowWindow(hDlg, SW_SHOW);
    UpdateWindow(hDlg);

    PopulateLockDialogScan(st);

    if (ownerHwnd) EnableWindow(ownerHwnd, FALSE);
    SetFocus(st.hList ? st.hList : st.hBtnClose);

    MSG msg = {};
    while (!st.closed) {
        const BOOL got = GetMessageW(&msg, nullptr, 0, 0);
        if (got <= 0) {
            if (got == 0) PostQuitMessage(static_cast<int>(msg.wParam));
            if (IsWindow(hDlg)) DestroyWindow(hDlg);
            break;
        }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            CloseLockDialogWindow(st);
            break;
        }
        if (!IsDialogMessageW(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (ownerHwnd) {
        EnableWindow(ownerHwnd, TRUE);
        SetForegroundWindow(ownerHwnd);
        SetFocus(ownerHwnd);
    }
#if defined(_WIN32) && !defined(E2_WIN32_HAL)
    if (st.hImgList) ImageList_Destroy(st.hImgList);
#endif
    if (st.hBrEditBg) DeleteObject(st.hBrEditBg);
    if (st.hBrDlgBg) DeleteObject(st.hBrDlgBg);
    if (st.hBoldFont) DeleteObject(st.hBoldFont);
    if (st.hFont) DeleteObject(st.hFont);
}

} // namespace Explorer2::FileLockInspector
