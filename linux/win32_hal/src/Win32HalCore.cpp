// linux/win32_hal/src/Win32HalCore.cpp
// Core Win32, COM, Stream, INI, Sync, File/Volume, and Initial D2D/DWrite/WIC/Windowing Shim for Linux.

#include <windows.h>
#include <windowsx.h>
#include <objbase.h>
#include <ole2.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <commctrl.h>
#include <commoncontrols.h>
#include <commdlg.h>
#include <winioctl.h>
#include <mfapi.h>
#include <mfmediaengine.h>
#include <sddl.h>
#include <winver.h>
#include <tlhelp32.h>

#include "PathUtils.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <deque>
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <limits.h>
#include <map>
#include <memory>
#include <mutex>
#include <pwd.h>
#include <signal.h>
#include <sstream>
#include <string>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace Explorer2::Hal {

namespace {
std::wstring g_cmdLineW = L"explorer2";
std::mutex g_cmdMutex;
} // namespace

void SetCommandLineFromMain(int argc, char** argv) {
    std::lock_guard<std::mutex> lk(g_cmdMutex);
    g_cmdLineW.clear();
    for (int i = 0; i < argc; ++i) {
        if (i > 0) g_cmdLineW.push_back(L' ');
        std::wstring arg = PathUtils::Utf8ToWide(argv[i] ? argv[i] : "");
        const bool needQuote = arg.empty() || arg.find_first_of(L" \t\"") != std::wstring::npos;
        if (needQuote) {
            g_cmdLineW.push_back(L'"');
            for (wchar_t c : arg) {
                if (c == L'"') g_cmdLineW.push_back(L'\\');
                g_cmdLineW.push_back(c);
            }
            g_cmdLineW.push_back(L'"');
        } else {
            g_cmdLineW += arg;
        }
    }
}

__attribute__((weak)) bool RenderPdfPageLinux(
    const std::wstring& /*fullPath*/,
    uint32_t /*pageIndex*/,
    uint32_t /*targetWidth*/,
    std::vector<uint8_t>& outBgraPixels,
    uint32_t& outWidth,
    uint32_t& outHeight,
    uint32_t* outTotalPages)
{
    outBgraPixels.clear();
    outWidth = 0;
    outHeight = 0;
    if (outTotalPages) *outTotalPages = 0;
    return false;
}

} // namespace Explorer2::Hal

// ==============================================================================================================
// 1. Memory: GlobalAlloc / LocalFree / VirtualAlloc / CoTaskMem / BSTR / PROPVARIANT
// ==============================================================================================================
namespace {
struct GlobalMemHeader {
    uint32_t magic;
    size_t size;
};
constexpr uint32_t kGlobalMagic = 0x474D454Du;
} // namespace

HGLOBAL GlobalAlloc(UINT uFlags, SIZE_T dwBytes) {
    const size_t total = sizeof(GlobalMemHeader) + (dwBytes ? dwBytes : 1);
    auto* raw = static_cast<uint8_t*>(std::malloc(total));
    if (!raw) return nullptr;
    if (uFlags & GMEM_ZEROINIT) std::memset(raw, 0, total);
    else std::memset(raw, 0, sizeof(GlobalMemHeader));
    auto* hdr = reinterpret_cast<GlobalMemHeader*>(raw);
    hdr->magic = kGlobalMagic;
    hdr->size = dwBytes;
    return raw + sizeof(GlobalMemHeader);
}

LPVOID GlobalLock(HGLOBAL hMem) { return hMem; }
BOOL GlobalUnlock(HGLOBAL) { return TRUE; }

SIZE_T GlobalSize(HGLOBAL hMem) {
    if (!hMem) return 0;
    const auto* hdr = reinterpret_cast<const GlobalMemHeader*>(static_cast<const uint8_t*>(hMem) - sizeof(GlobalMemHeader));
    return (hdr->magic == kGlobalMagic) ? hdr->size : 0;
}

HGLOBAL GlobalFree(HGLOBAL hMem) {
    if (!hMem) return nullptr;
    auto* raw = static_cast<uint8_t*>(hMem) - sizeof(GlobalMemHeader);
    auto* hdr = reinterpret_cast<GlobalMemHeader*>(raw);
    if (hdr->magic == kGlobalMagic) {
        hdr->magic = 0;
        std::free(raw);
    } else {
        std::free(hMem);
    }
    return nullptr;
}

HLOCAL LocalFree(HLOCAL hMem) {
    std::free(hMem);
    return nullptr;
}

LPVOID VirtualAlloc(LPVOID, SIZE_T dwSize, DWORD, DWORD) {
    if (dwSize == 0) return nullptr;
    void* p = nullptr;
    const size_t aligned = (dwSize + 4095u) & ~static_cast<size_t>(4095u);
    if (::posix_memalign(&p, 4096, aligned) != 0) return nullptr;
    std::memset(p, 0, aligned);
    return p;
}

BOOL VirtualFree(LPVOID lpAddress, SIZE_T, DWORD) {
    std::free(lpAddress);
    return TRUE;
}

LPVOID CoTaskMemAlloc(SIZE_T cb) { return std::malloc(cb ? cb : 1); }
void CoTaskMemFree(LPVOID pv) { std::free(pv); }

BSTR SysAllocString(const OLECHAR* psz) {
    if (!psz) return nullptr;
    const size_t len = std::wcslen(psz);
    auto* raw = static_cast<uint8_t*>(std::malloc(sizeof(uint32_t) + (len + 1) * sizeof(OLECHAR)));
    if (!raw) return nullptr;
    const uint32_t byteLen = static_cast<uint32_t>(len * sizeof(OLECHAR));
    std::memcpy(raw, &byteLen, sizeof(uint32_t));
    auto* str = reinterpret_cast<OLECHAR*>(raw + sizeof(uint32_t));
    std::wmemcpy(str, psz, len + 1);
    return str;
}

void SysFreeString(BSTR bstrString) {
    if (!bstrString) return;
    auto* raw = reinterpret_cast<uint8_t*>(bstrString) - sizeof(uint32_t);
    std::free(raw);
}

HRESULT PropVariantClear(PROPVARIANT* pvar) {
    if (!pvar) return E_INVALIDARG;
    if (pvar->vt == VT_LPWSTR) CoTaskMemFree(pvar->pwszVal);
    else if (pvar->vt == VT_LPSTR) CoTaskMemFree(pvar->pszVal);
    else if (pvar->vt == VT_BSTR) SysFreeString(pvar->bstrVal);
    std::memset(pvar, 0, sizeof(*pvar));
    return S_OK;
}

// ==============================================================================================================
// 2. Module, Command Line, Dynamic Libraries & Environment
// ==============================================================================================================
LPWSTR GetCommandLineW() {
    return Explorer2::Hal::g_cmdLineW.data();
}

LPWSTR* CommandLineToArgvW(LPCWSTR lpCmdLine, int* pNumArgs) {
    if (!pNumArgs) return nullptr;
    std::vector<std::wstring> args;
    const wchar_t* p = lpCmdLine ? lpCmdLine : L"";
    while (*p == L' ' || *p == L'\t') ++p;
    if (*p == L'\0') {
        args.push_back(L"explorer2");
    } else {
        while (*p) {
            while (*p == L' ' || *p == L'\t') ++p;
            if (!*p) break;
            std::wstring cur;
            bool inQuotes = false;
            while (*p) {
                if (*p == L'"') {
                    inQuotes = !inQuotes;
                    ++p;
                } else if (*p == L'\\' && p[1] == L'"') {
                    cur.push_back(L'"');
                    p += 2;
                } else if (!inQuotes && (*p == L' ' || *p == L'\t')) {
                    break;
                } else {
                    cur.push_back(*p++);
                }
            }
            args.push_back(std::move(cur));
        }
    }
    *pNumArgs = static_cast<int>(args.size());
    size_t totalChars = 0;
    for (const auto& a : args) totalChars += a.size() + 1;
    const size_t ptrBytes = (args.size() + 1) * sizeof(LPWSTR);
    auto* block = static_cast<uint8_t*>(std::malloc(ptrBytes + totalChars * sizeof(wchar_t)));
    if (!block) return nullptr;
    auto** argv = reinterpret_cast<LPWSTR*>(block);
    auto* chars = reinterpret_cast<wchar_t*>(block + ptrBytes);
    for (size_t i = 0; i < args.size(); ++i) {
        argv[i] = chars;
        std::wmemcpy(chars, args[i].c_str(), args[i].size() + 1);
        chars += args[i].size() + 1;
    }
    argv[args.size()] = nullptr;
    return argv;
}

namespace Explorer2::Hal {
__attribute__((weak)) bool IsPdfRendererActiveLinux() { return false; }
} // namespace Explorer2::Hal

HMODULE GetModuleHandleW(LPCWSTR lpModuleName) {
    if (!lpModuleName || !*lpModuleName) return reinterpret_cast<HMODULE>(static_cast<uintptr_t>(1));
    if (_wcsicmp(lpModuleName, L"Windows.Data.Pdf.dll") == 0) {
        return Explorer2::Hal::IsPdfRendererActiveLinux()
            ? reinterpret_cast<HMODULE>(static_cast<uintptr_t>(0x504446u))
            : nullptr;
    }
    return nullptr;
}

BOOL GetModuleHandleExW(DWORD dwFlags, LPCWSTR lpModuleName, HMODULE* phModule) {
    if (!phModule) return FALSE;
    if (dwFlags & GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS) {
        Dl_info info = {};
        if (lpModuleName && dladdr(reinterpret_cast<const void*>(lpModuleName), &info) && info.dli_fbase) {
            *phModule = reinterpret_cast<HMODULE>(info.dli_fbase);
            return TRUE;
        }
        return FALSE;
    }
    *phModule = GetModuleHandleW(lpModuleName);
    return *phModule != nullptr ? TRUE : FALSE;
}

DWORD GetModuleFileNameW(HMODULE hModule, LPWSTR lpFilename, DWORD nSize) {
    if (!lpFilename || nSize == 0) return 0;
    std::string pathStr;
    if (!hModule || hModule == reinterpret_cast<HMODULE>(static_cast<uintptr_t>(1))) {
        char buf[PATH_MAX] = {};
        const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (n > 0) pathStr.assign(buf, static_cast<size_t>(n));
    } else {
        Dl_info info = {};
        if (dladdr(reinterpret_cast<const void*>(hModule), &info) && info.dli_fname) {
            pathStr = info.dli_fname;
        }
    }
    if (pathStr.empty()) pathStr = "/proc/self/exe";
    std::wstring w = Explorer2::PathUtils::Utf8ToWide(pathStr);
    const size_t copyLen = std::min<size_t>(w.size(), nSize - 1);
    std::wmemcpy(lpFilename, w.data(), copyLen);
    lpFilename[copyLen] = L'\0';
    return static_cast<DWORD>(copyLen);
}

HMODULE LoadLibraryW(LPCWSTR lpLibFileName) {
    if (!lpLibFileName) return nullptr;
    std::string s = Explorer2::PathUtils::WideToUtf8(lpLibFileName);
    std::string low = s;
    for (char& c : low) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (low == "nvml.dll" || (low.size() >= 9 && low.compare(low.size() - 9, 9, "/nvml.dll") == 0) ||
        (low.size() >= 9 && low.compare(low.size() - 9, 9, "\\nvml.dll") == 0)) {
        void* h = ::dlopen("libnvidia-ml.so.1", RTLD_LAZY);
        if (!h) h = ::dlopen("/usr/lib/wsl/lib/libnvidia-ml.so.1", RTLD_LAZY);
        return reinterpret_cast<HMODULE>(h);
    }
    if (low.find(".dll") != std::string::npos) return nullptr;
    void* h = ::dlopen(s.c_str(), RTLD_LAZY);
    return reinterpret_cast<HMODULE>(h);
}

HMODULE LoadLibraryExW(LPCWSTR lpLibFileName, HANDLE, DWORD) {
    return LoadLibraryW(lpLibFileName);
}

BOOL FreeLibrary(HMODULE hLibModule) {
    if (!hLibModule || hLibModule == reinterpret_cast<HMODULE>(static_cast<uintptr_t>(1))) return FALSE;
    return ::dlclose(reinterpret_cast<void*>(hLibModule)) == 0 ? TRUE : FALSE;
}

void* GetProcAddress(HMODULE hModule, LPCSTR lpProcName) {
    if (!hModule || !lpProcName || hModule == reinterpret_cast<HMODULE>(static_cast<uintptr_t>(1))) return nullptr;
    return ::dlsym(reinterpret_cast<void*>(hModule), lpProcName);
}

BOOL SetEnvironmentVariableW(LPCWSTR lpName, LPCWSTR lpValue) {
    if (!lpName || !*lpName) return FALSE;
    const std::string name = Explorer2::PathUtils::WideToUtf8(lpName);
    if (!lpValue) return ::unsetenv(name.c_str()) == 0 ? TRUE : FALSE;
    const std::string val = Explorer2::PathUtils::WideToUtf8(lpValue);
    return ::setenv(name.c_str(), val.c_str(), 1) == 0 ? TRUE : FALSE;
}

DWORD ExpandEnvironmentStringsW(LPCWSTR lpSrc, LPWSTR lpDst, DWORD nSize) {
    if (!lpSrc) return 0;
    std::wstring out;
    for (const wchar_t* p = lpSrc; *p; ) {
        if (*p == L'%') {
            const wchar_t* end = std::wcschr(p + 1, L'%');
            if (end && end > p + 1) {
                std::wstring varName(p + 1, static_cast<size_t>(end - (p + 1)));
                wchar_t valBuf[2048] = {};
                DWORD vn = GetEnvironmentVariableW(varName.c_str(), valBuf, 2048);
                if (vn == 0 && _wcsicmp(varName.c_str(), L"LOCALAPPDATA") == 0) {
                    const char* xdg = std::getenv("XDG_DATA_HOME");
                    const char* home = std::getenv("HOME");
                    std::string base = (xdg && *xdg) ? xdg : ((home && *home) ? (std::string(home) + "/.local/share") : "/tmp");
                    out += Explorer2::PathUtils::Utf8ToWide(base);
                } else if (vn == 0 && _wcsicmp(varName.c_str(), L"APPDATA") == 0) {
                    const char* xdg = std::getenv("XDG_CONFIG_HOME");
                    const char* home = std::getenv("HOME");
                    std::string base = (xdg && *xdg) ? xdg : ((home && *home) ? (std::string(home) + "/.config") : "/tmp");
                    out += Explorer2::PathUtils::Utf8ToWide(base);
                } else if (vn == 0 && (_wcsicmp(varName.c_str(), L"TEMP") == 0 || _wcsicmp(varName.c_str(), L"TMP") == 0)) {
                    out += L"/tmp";
                } else if (vn == 0 && _wcsicmp(varName.c_str(), L"USERPROFILE") == 0) {
                    const char* home = std::getenv("HOME");
                    out += Explorer2::PathUtils::Utf8ToWide((home && *home) ? home : "/tmp");
                } else if (vn > 0 && vn < 2048) {
                    out.append(valBuf, vn);
                } else {
                    out.append(p, static_cast<size_t>(end - p + 1));
                }
                p = end + 1;
                continue;
            }
        }
        out.push_back(*p++);
    }
    if (!lpDst || nSize <= out.size()) return static_cast<DWORD>(out.size() + 1);
    std::wmemcpy(lpDst, out.c_str(), out.size() + 1);
    return static_cast<DWORD>(out.size() + 1);
}

LPWCH GetEnvironmentStringsW() {
    std::wstring block;
    if (environ) {
        for (char** e = environ; *e; ++e) {
            std::wstring line = Explorer2::PathUtils::Utf8ToWide(*e);
            block.append(line);
            block.push_back(L'\0');
        }
    }
    block.push_back(L'\0');
    auto* buf = static_cast<LPWCH>(std::malloc(block.size() * sizeof(wchar_t)));
    if (buf) std::wmemcpy(buf, block.data(), block.size());
    return buf;
}

BOOL FreeEnvironmentStringsW(LPWCH penv) {
    std::free(penv);
    return TRUE;
}

UINT GetWindowsDirectoryW(LPWSTR lpBuffer, UINT uSize) {
    if (lpBuffer && uSize > 0) lpBuffer[0] = L'\0';
    return 0;
}

DWORD SearchPathW(LPCWSTR lpPath, LPCWSTR lpFileName, LPCWSTR lpExtension, DWORD nBufferLength, LPWSTR lpBuffer, LPWSTR* lpFilePart) {
    if (!lpFileName || !*lpFileName) return 0;
    std::wstring name(lpFileName);
    // Strip .exe on Linux when searching PATH if exact name doesn't exist
    auto stripExe = [](std::wstring s) {
        if (s.size() > 4 && _wcsicmp(s.c_str() + s.size() - 4, L".exe") == 0) s.resize(s.size() - 4);
        return s;
    };

    std::string pathEnv;
    if (lpPath && *lpPath) {
        pathEnv = Explorer2::PathUtils::WideToUtf8(lpPath);
    } else {
        const char* p = std::getenv("PATH");
        pathEnv = p ? p : "/usr/local/bin:/usr/bin:/bin";
    }

    std::vector<std::string> candidates = { Explorer2::PathUtils::WideToUtf8(name) };
    const std::wstring stripped = stripExe(name);
    if (stripped != name) candidates.push_back(Explorer2::PathUtils::WideToUtf8(stripped));
    if (lpExtension && *lpExtension) candidates.push_back(Explorer2::PathUtils::WideToUtf8(name + lpExtension));

    std::stringstream ss(pathEnv);
    std::string dir;
    while (std::getline(ss, dir, ':')) {
        if (dir.empty()) continue;
        for (const auto& c : candidates) {
            std::string full = dir + "/" + c;
            struct stat st = {};
            if (::stat(full.c_str(), &st) == 0 && !S_ISDIR(st.st_mode) && ::access(full.c_str(), X_OK) == 0) {
                std::wstring w = Explorer2::PathUtils::Utf8ToWide(full);
                if (!lpBuffer || nBufferLength <= w.size()) return static_cast<DWORD>(w.size() + 1);
                std::wmemcpy(lpBuffer, w.c_str(), w.size() + 1);
                if (lpFilePart) {
                    wchar_t* sl = std::wcsrchr(lpBuffer, L'/');
                    *lpFilePart = sl ? (sl + 1) : lpBuffer;
                }
                return static_cast<DWORD>(w.size());
            }
        }
    }
    SetLastError(ERROR_FILE_NOT_FOUND);
    return 0;
}

DWORD FormatMessageW(DWORD, LPCVOID, DWORD dwMessageId, DWORD, LPWSTR lpBuffer, DWORD nSize, va_list*) {
    if (!lpBuffer || nSize == 0) return 0;
    const char* msg = "System error";
    switch (dwMessageId) {
    case ERROR_SUCCESS: msg = "Success"; break;
    case ERROR_FILE_NOT_FOUND: msg = "File not found"; break;
    case ERROR_PATH_NOT_FOUND: msg = "Path not found"; break;
    case ERROR_ACCESS_DENIED: msg = "Access denied"; break;
    case ERROR_FILE_EXISTS: case ERROR_ALREADY_EXISTS: msg = "File already exists"; break;
    case ERROR_DISK_FULL: msg = "No space left on device"; break;
    case ERROR_DIR_NOT_EMPTY: msg = "Directory not empty"; break;
    case ERROR_INVALID_HANDLE: msg = "Invalid handle"; break;
    case ERROR_NOT_SAME_DEVICE: msg = "Cannot move the item to a different file system"; break;
    case ERROR_WRITE_FAULT: msg = "Write error"; break;
    case ERROR_SHARING_VIOLATION: msg = "The file is in use"; break;
    case ERROR_NOT_SUPPORTED: msg = "Operation not supported"; break;
    case ERROR_INVALID_PARAMETER: msg = "Invalid argument"; break;
    case ERROR_BROKEN_PIPE: msg = "Broken pipe"; break;
    case ERROR_FILENAME_EXCED_RANGE: msg = "File name too long"; break;
    case ERROR_CANCELLED: msg = "Operation cancelled"; break;
    case ERROR_CANT_RESOLVE_FILENAME: msg = "Too many levels of symbolic links"; break;
    default: break;
    }
    std::wstring w = Explorer2::PathUtils::Utf8ToWide(msg);
    const size_t n = std::min<size_t>(w.size(), nSize - 1);
    std::wmemcpy(lpBuffer, w.data(), n);
    lpBuffer[n] = L'\0';
    return static_cast<DWORD>(n);
}

UINT SetErrorMode(UINT) { return 0; }
BOOL SetThreadErrorMode(DWORD, LPDWORD lpOldMode) { if (lpOldMode) *lpOldMode = 0; return TRUE; }
LPTOP_LEVEL_EXCEPTION_FILTER SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER) { return nullptr; }
BOOL IsBadReadPtr(const void* lp, UINT_PTR ucb) { return (lp == nullptr && ucb > 0) ? TRUE : FALSE; }

namespace {
// Reads a file of /proc or /sys whole (these never wait for a disk; the size limit is only a safety net)
std::string ReadKernelTextFile(const std::string& path, size_t maxBytes = 65536) {
    std::string out;
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return out;
    char buf[4096];
    for (;;) {
        const ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        out.append(buf, static_cast<size_t>(n));
        if (out.size() > maxBytes) break;
    }
    ::close(fd);
    return out;
}

// The number after "Key:" on the line of /proc/meminfo that starts with it (the key given with its colon, so
// that "Cached:" does not find "SwapCached:")
bool MeminfoValueKb(const std::string& text, const char* key, uint64_t& outKb) {
    const size_t keyLen = std::strlen(key);
    size_t pos = 0;
    while (pos < text.size()) {
        if (text.compare(pos, keyLen, key) == 0) {
            outKb = std::strtoull(text.c_str() + pos + keyLen, nullptr, 10);
            return true;
        }
        const size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
    return false;
}
} // namespace

BOOL GlobalMemoryStatusEx(MEMORYSTATUSEX* lpBuffer) {
    if (!lpBuffer) return FALSE;
    struct sysinfo si = {};
    if (::sysinfo(&si) != 0) return FALSE;
    const uint64_t unit = si.mem_unit ? si.mem_unit : 1;
    lpBuffer->ullTotalPhys = static_cast<ULONGLONG>(si.totalram) * unit;
    // MemAvailable is the kernel's estimate of what programs can still get without swapping: free memory plus the
    // page cache and the kernel caches it can drop. Free plus buffers alone leaves the page cache out, and on a
    // machine that has been up for a while that makes nearly all memory look used.
    const std::string meminfo = ReadKernelTextFile("/proc/meminfo");
    uint64_t availKb = 0, cachedKb = 0;
    if (MeminfoValueKb(meminfo, "MemAvailable:", availKb)) {
        lpBuffer->ullAvailPhys = static_cast<ULONGLONG>(availKb) * 1024ULL;
    } else {
        (void)MeminfoValueKb(meminfo, "Cached:", cachedKb);     // kernels before 3.14 have no MemAvailable
        lpBuffer->ullAvailPhys = (static_cast<ULONGLONG>(si.freeram) + static_cast<ULONGLONG>(si.bufferram)) * unit +
                                 static_cast<ULONGLONG>(cachedKb) * 1024ULL;
    }
    lpBuffer->ullAvailPhys = std::min(lpBuffer->ullAvailPhys, lpBuffer->ullTotalPhys);
    lpBuffer->ullTotalPageFile = lpBuffer->ullTotalPhys + static_cast<ULONGLONG>(si.totalswap) * unit;
    lpBuffer->ullAvailPageFile = lpBuffer->ullAvailPhys + static_cast<ULONGLONG>(si.freeswap) * unit;
    lpBuffer->ullTotalVirtual = lpBuffer->ullTotalPageFile;
    lpBuffer->ullAvailVirtual = lpBuffer->ullAvailPageFile;
    lpBuffer->dwMemoryLoad = lpBuffer->ullTotalPhys
        ? static_cast<DWORD>(((lpBuffer->ullTotalPhys - std::min(lpBuffer->ullAvailPhys, lpBuffer->ullTotalPhys)) * 100ULL) / lpBuffer->ullTotalPhys)
        : 0;
    return TRUE;
}

void GetNativeSystemInfo(LPSYSTEM_INFO lpSystemInfo) {
    if (!lpSystemInfo) return;
    std::memset(lpSystemInfo, 0, sizeof(*lpSystemInfo));
#if defined(__x86_64__)
    lpSystemInfo->wProcessorArchitecture = PROCESSOR_ARCHITECTURE_AMD64;
#elif defined(__aarch64__)
    lpSystemInfo->wProcessorArchitecture = PROCESSOR_ARCHITECTURE_ARM64;
#elif defined(__i386__)
    lpSystemInfo->wProcessorArchitecture = PROCESSOR_ARCHITECTURE_INTEL;
#endif
    lpSystemInfo->dwPageSize = static_cast<DWORD>(::sysconf(_SC_PAGESIZE));
    lpSystemInfo->dwAllocationGranularity = 65536;
    const long nproc = ::sysconf(_SC_NPROCESSORS_ONLN);
    lpSystemInfo->dwNumberOfProcessors = nproc > 0 ? static_cast<DWORD>(nproc) : 1u;
}
void GetSystemInfo(LPSYSTEM_INFO lpSystemInfo) { GetNativeSystemInfo(lpSystemInfo); }
BOOL IsProcessorFeaturePresent(DWORD) { return TRUE; }
BOOL GetLogicalProcessorInformationEx(LOGICAL_PROCESSOR_RELATIONSHIP, SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*, PDWORD ReturnedLength) {
    if (ReturnedLength) *ReturnedLength = 0;
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}
UINT GetSystemFirmwareTable(DWORD, DWORD, PVOID, DWORD) { return 0; }
BOOL GetSystemPowerStatus(SYSTEM_POWER_STATUS* st) {
    if (!st) return FALSE;
    std::memset(st, 0, sizeof(*st));
    st->ACLineStatus = 1;
    st->BatteryFlag = 128; // No system battery
    st->BatteryLifePercent = 255;
    return TRUE;
}

BOOL GetComputerNameW(LPWSTR lpBuffer, LPDWORD nSize) {
    if (!lpBuffer || !nSize || *nSize == 0) return FALSE;
    char host[256] = {};
    if (::gethostname(host, sizeof(host) - 1) != 0) std::strcpy(host, "linux");
    std::wstring w = Explorer2::PathUtils::Utf8ToWide(host);
    if (*nSize <= w.size()) { *nSize = static_cast<DWORD>(w.size() + 1); SetLastError(ERROR_MORE_DATA); return FALSE; }
    std::wmemcpy(lpBuffer, w.c_str(), w.size() + 1);
    *nSize = static_cast<DWORD>(w.size());
    return TRUE;
}

BOOL GetUserNameW(LPWSTR lpBuffer, LPDWORD pcbBuffer) {
    if (!lpBuffer || !pcbBuffer || *pcbBuffer == 0) return FALSE;
    const char* user = std::getenv("USER");
    std::string u = (user && *user) ? user : "";
    if (u.empty()) {
        struct passwd* pw = ::getpwuid(::geteuid());
        if (pw && pw->pw_name) u = pw->pw_name;
        else u = "user";
    }
    std::wstring w = Explorer2::PathUtils::Utf8ToWide(u);
    if (*pcbBuffer <= w.size()) { *pcbBuffer = static_cast<DWORD>(w.size() + 1); SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
    std::wmemcpy(lpBuffer, w.c_str(), w.size() + 1);
    *pcbBuffer = static_cast<DWORD>(w.size() + 1);
    return TRUE;
}

BOOL EnumDisplayDevicesW(LPCWSTR, DWORD, DISPLAY_DEVICEW*, DWORD) { return FALSE; }
BOOL EnumDisplaySettingsExW(LPCWSTR, DWORD, DEVMODEW*, DWORD) { return FALSE; }

// ==============================================================================================================
// 3. File, HardLink, CopyFileExW, Volume, DeviceIoControl & INI Helpers
// ==============================================================================================================
HANDLE FindFirstFileExW(LPCWSTR lpFileName, FINDEX_INFO_LEVELS, LPVOID lpFindFileData, FINDEX_SEARCH_OPS, LPVOID, DWORD) {
    return FindFirstFileW(lpFileName, static_cast<WIN32_FIND_DATAW*>(lpFindFileData));
}

BOOL CreateHardLinkW(LPCWSTR lpFileName, LPCWSTR lpExistingFileName, LPSECURITY_ATTRIBUTES) {
    if (!lpFileName || !lpExistingFileName) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    const std::string dst = Explorer2::Compat::NativePath(lpFileName);
    const std::string src = Explorer2::Compat::NativePath(lpExistingFileName);
    if (::link(src.c_str(), dst.c_str()) != 0) {
        Explorer2::Compat::SetErrorFromErrno();
        return FALSE;
    }
    return TRUE;
}

BOOL CopyFileExW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName, LPPROGRESS_ROUTINE lpProgressRoutine, LPVOID lpData, LPBOOL pbCancel, DWORD dwCopyFlags) {
    if (!lpExistingFileName || !lpNewFileName) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    const std::string src = Explorer2::Compat::NativePath(lpExistingFileName);
    const std::string dst = Explorer2::Compat::NativePath(lpNewFileName);
    struct stat stLink = {}, stDst = {};
    if (::lstat(src.c_str(), &stLink) != 0) { Explorer2::Compat::SetErrorFromErrno(); return FALSE; }
    if ((dwCopyFlags & COPY_FILE_FAIL_IF_EXISTS) && ::lstat(dst.c_str(), &stDst) == 0) {
        SetLastError(ERROR_FILE_EXISTS);
        return FALSE;
    }
    if ((dwCopyFlags & COPY_FILE_COPY_SYMLINK) && S_ISLNK(stLink.st_mode)) {
        char linkTarget[PATH_MAX] = {};
        const ssize_t ln = ::readlink(src.c_str(), linkTarget, sizeof(linkTarget) - 1);
        if (ln < 0) { Explorer2::Compat::SetErrorFromErrno(); return FALSE; }
        ::unlink(dst.c_str());
        if (::symlink(linkTarget, dst.c_str()) != 0) { Explorer2::Compat::SetErrorFromErrno(); return FALSE; }
        return TRUE;
    }
    // Only regular files are copied: opening a FIFO waits for a writer, and a device or a socket has no content to
    // copy. Checked by name first, so that a device is never even opened, then again on the open descriptor.
    struct stat stTarget = {};
    if (::stat(src.c_str(), &stTarget) != 0) { Explorer2::Compat::SetErrorFromErrno(); return FALSE; }
    if (!S_ISREG(stTarget.st_mode)) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    const int fdIn = ::open(src.c_str(), O_RDONLY | O_CLOEXEC | O_NOCTTY | O_NONBLOCK);
    if (fdIn < 0) { Explorer2::Compat::SetErrorFromErrno(); return FALSE; }
    // Size, mode and times come from the file that was opened (through a link, the file it points to: the link's
    // own size and mode 0777 made truncated copies)
    struct stat stSrc = {};
    if (::fstat(fdIn, &stSrc) != 0 || !S_ISREG(stSrc.st_mode)) {
        ::close(fdIn);
        SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    const int inFlags = ::fcntl(fdIn, F_GETFL);
    if (inFlags >= 0) (void)::fcntl(fdIn, F_SETFL, inFlags & ~O_NONBLOCK);
    // A copy onto the file itself (the same name, another spelling of it, or a hard link) would empty the source
    // before a byte of it was read
    if (::stat(dst.c_str(), &stDst) == 0 && stDst.st_dev == stSrc.st_dev && stDst.st_ino == stSrc.st_ino) {
        ::close(fdIn);
        SetLastError(ERROR_SHARING_VIOLATION);
        return FALSE;
    }
    const int fdOut = ::open(dst.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, stSrc.st_mode & 0777);
    if (fdOut < 0) { Explorer2::Compat::SetErrorFromErrno(); ::close(fdIn); return FALSE; }
    // The source's permission bits right away (the umask narrowed them at the create, and a file that existed
    // keeps its old mode until now), so the copy is never more open than its source while the data goes in.
    // FAT, exFAT and some network file systems refuse or ignore modes, which is no reason to fail the copy.
    (void)::fchmod(fdOut, stSrc.st_mode & 0777);

    LARGE_INTEGER totalSz{}, transferred{};
    totalSz.QuadPart = static_cast<LONGLONG>(stSrc.st_size);
    bool ok = true;
    bool done = false;

    // 1. Instant CoW reflink clone (Btrfs/XFS) when no progress callback / cancel pointer is requested
#ifndef FICLONE
#define FICLONE _IOW(0x94, 9, int)
#endif
    if (!lpProgressRoutine && !pbCancel && stSrc.st_size > 0) {
        if (::ioctl(fdOut, FICLONE, fdIn) == 0) {
            done = true;
        }
    }

    // 2. Zero-copy in-kernel copy_file_range() loop with progress/cancel pacing
    if (!done && stSrc.st_size > 0) {
        bool cfrSupported = true;
        while (transferred.QuadPart < totalSz.QuadPart) {
            if (pbCancel && *pbCancel) { ok = false; SetLastError(ERROR_CANCELLED); break; }
            const size_t chunk = static_cast<size_t>(std::min<LONGLONG>(totalSz.QuadPart - transferred.QuadPart, 1024LL * 1024LL));
            const ssize_t copied = ::copy_file_range(fdIn, nullptr, fdOut, nullptr, chunk, 0);
            if (copied < 0) {
                if (errno == EINTR) continue;
                if (transferred.QuadPart == 0 && (errno == EXDEV || errno == ENOSYS || errno == EOPNOTSUPP || errno == EINVAL)) {
                    cfrSupported = false;
                    break;
                }
                ok = false;
                Explorer2::Compat::SetErrorFromErrno();
                break;
            }
            if (copied == 0) {
                // Nothing copied although the file is not finished: some file systems (FUSE and network mounts,
                // files whose size the kernel only estimates) answer copy_file_range this way. Plain reads and
                // writes, from the start, copy what is really there.
                cfrSupported = false;
                break;
            }
            transferred.QuadPart += copied;
            if (lpProgressRoutine) {
                DWORD act = lpProgressRoutine(totalSz, transferred, totalSz, transferred, 1, 0, INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE, lpData);
                if (act == PROGRESS_CANCEL) { ok = false; SetLastError(ERROR_CANCELLED); break; }
            }
        }
        if (cfrSupported && ok) done = true;
    }

    // 3. Portable buffered fallback (cross-device / virtual / older kernel mounts)
    if (!done && ok) {
        ::lseek(fdIn, 0, SEEK_SET);
        ::lseek(fdOut, 0, SEEK_SET);
        // What copy_file_range wrote before it gave up is written again; should the source now be shorter, no
        // stale tail may stay behind
        if (::ftruncate(fdOut, 0) != 0) { ok = false; Explorer2::Compat::SetErrorFromErrno(); }
        transferred.QuadPart = 0;
        std::vector<uint8_t> buf(256 * 1024);
        while (ok) {
            if (pbCancel && *pbCancel) { ok = false; SetLastError(ERROR_CANCELLED); break; }
            const ssize_t got = ::read(fdIn, buf.data(), buf.size());
            if (got < 0) {
                if (errno == EINTR) continue;
                ok = false; Explorer2::Compat::SetErrorFromErrno(); break;
            }
            if (got == 0) break;
            ssize_t off = 0;
            while (off < got) {
                const ssize_t put = ::write(fdOut, buf.data() + off, static_cast<size_t>(got - off));
                if (put < 0) {
                    if (errno == EINTR) continue;
                    ok = false; Explorer2::Compat::SetErrorFromErrno(); break;
                }
                off += put;
            }
            if (!ok) break;
            transferred.QuadPart += got;
            if (lpProgressRoutine) {
                DWORD act = lpProgressRoutine(totalSz, transferred, totalSz, transferred, 1, 0, INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE, lpData);
                if (act == PROGRESS_CANCEL) { ok = false; SetLastError(ERROR_CANCELLED); break; }
            }
        }
    }

    struct timespec ts[2] = { stSrc.st_atim, stSrc.st_mtim };
    if (ok) {
        ::futimens(fdOut, ts);
        // Set-user-ID, set-group-ID and sticky bits after the data: a write by an ordinary user clears them
        if (stSrc.st_mode & 07000) (void)::fchmod(fdOut, stSrc.st_mode & 07777);
    }
    ::close(fdIn);
    if (::close(fdOut) != 0 && ok) { ok = false; Explorer2::Compat::SetErrorFromErrno(); }
    if (!ok) {
        ::unlink(dst.c_str());
        return FALSE;
    }
    return TRUE;
}

BOOL CopyFileW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName, BOOL bFailIfExists) {
    return CopyFileExW(lpExistingFileName, lpNewFileName, nullptr, nullptr, nullptr, bFailIfExists ? COPY_FILE_FAIL_IF_EXISTS : 0);
}

BOOL GetFileInformationByHandle(HANDLE hFile, BY_HANDLE_FILE_INFORMATION* lpFileInformation) {
    const int fd = Explorer2::Compat::FdOf(hFile);
    struct stat st = {};
    if (fd < 0 || !lpFileInformation || ::fstat(fd, &st) != 0) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    auto* fh = static_cast<Explorer2::Compat::FileHandle*>(Explorer2::Compat::BaseOf(hFile));
    std::memset(lpFileInformation, 0, sizeof(*lpFileInformation));
    lpFileInformation->dwFileAttributes = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    if (S_ISLNK(st.st_mode)) {
        // A link opened with FILE_FLAG_OPEN_REPARSE_POINT: a reparse point, and a folder one when it points to a
        // folder, as GetFileAttributesW describes it
        struct stat tst = {};
        const bool toDir = fh && !fh->nativePath.empty() && ::stat(fh->nativePath.c_str(), &tst) == 0 && S_ISDIR(tst.st_mode);
        lpFileInformation->dwFileAttributes = FILE_ATTRIBUTE_REPARSE_POINT | (toDir ? FILE_ATTRIBUTE_DIRECTORY : 0u);
    }
    lpFileInformation->ftCreationTime = Explorer2::Compat::GetInodeCreationTime(fh ? fh->nativePath.c_str() : nullptr, st);
    lpFileInformation->ftLastAccessTime = Explorer2::Compat::FileTimeFromTimespec(st.st_atim);
    lpFileInformation->ftLastWriteTime = Explorer2::Compat::FileTimeFromTimespec(st.st_mtim);
    lpFileInformation->dwVolumeSerialNumber = static_cast<DWORD>(st.st_dev);
    lpFileInformation->nFileSizeHigh = static_cast<DWORD>(static_cast<uint64_t>(st.st_size) >> 32);
    lpFileInformation->nFileSizeLow = static_cast<DWORD>(static_cast<uint64_t>(st.st_size) & 0xFFFFFFFFu);
    lpFileInformation->nNumberOfLinks = static_cast<DWORD>(st.st_nlink);
    lpFileInformation->nFileIndexHigh = static_cast<DWORD>(static_cast<uint64_t>(st.st_ino) >> 32);
    lpFileInformation->nFileIndexLow = static_cast<DWORD>(static_cast<uint64_t>(st.st_ino) & 0xFFFFFFFFu);
    return TRUE;
}

BOOL SetFileInformationByHandle(HANDLE hFile, int FileInformationClass, LPVOID lpFileInformation, DWORD dwBufferSize) {
    const int fd = Explorer2::Compat::FdOf(hFile);
    if (fd < 0 || !lpFileInformation) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    // Buffered data has to be in the file before its size or times change. A flush that fails is a failed write:
    // the handle keeps it for FlushFileBuffers and CloseHandle, and this call fails with it.
    if (!Explorer2::Compat::FlushHandleStream(Explorer2::Compat::FileHandleOf(hFile))) return FALSE;
    if (FileInformationClass == FileEndOfFileInfo && dwBufferSize >= sizeof(FILE_END_OF_FILE_INFO)) {
        const auto* info = static_cast<const FILE_END_OF_FILE_INFO*>(lpFileInformation);
        if (::ftruncate(fd, static_cast<off_t>(info->EndOfFile.QuadPart)) != 0) {
            Explorer2::Compat::SetErrorFromErrno();
            return FALSE;
        }
        return TRUE;
    }
    if (FileInformationClass == FileAllocationInfo && dwBufferSize >= sizeof(FILE_ALLOCATION_INFO)) {
        const auto* info = static_cast<const FILE_ALLOCATION_INFO*>(lpFileInformation);
        if (info->AllocationSize.QuadPart > 0) {
            // Space is reserved without changing the file's size, as on Windows: a copy that stops early must not
            // leave a file of full length padded with zeros (posix_fallocate grows the file, and glibc's version
            // writes those zeros itself where the file system cannot reserve). Where it cannot, nothing happens.
#ifdef FALLOC_FL_KEEP_SIZE
            (void)::fallocate(fd, FALLOC_FL_KEEP_SIZE, 0, static_cast<off_t>(info->AllocationSize.QuadPart));
#endif
        }
        return TRUE;
    }
    if (FileInformationClass == FileBasicInfo && dwBufferSize >= sizeof(FILE_BASIC_INFO)) {
        const auto* info = static_cast<const FILE_BASIC_INFO*>(lpFileInformation);
        FILETIME ftC{}, ftA{}, ftW{};
        ftC.dwLowDateTime = info->CreationTime.LowPart;   ftC.dwHighDateTime = static_cast<DWORD>(info->CreationTime.HighPart);
        ftA.dwLowDateTime = info->LastAccessTime.LowPart; ftA.dwHighDateTime = static_cast<DWORD>(info->LastAccessTime.HighPart);
        ftW.dwLowDateTime = info->LastWriteTime.LowPart;  ftW.dwHighDateTime = static_cast<DWORD>(info->LastWriteTime.HighPart);
        return SetFileTime(hFile, &ftC, &ftA, &ftW);
    }
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

DWORD GetCompressedFileSizeW(LPCWSTR lpFileName, LPDWORD lpFileSizeHigh) {
    struct stat st = {};
    if (!lpFileName || ::lstat(Explorer2::Compat::NativePath(lpFileName).c_str(), &st) != 0) {
        Explorer2::Compat::SetErrorFromErrno();
        return INVALID_FILE_SIZE;
    }
    const uint64_t bytes = static_cast<uint64_t>(st.st_blocks) * 512ULL;
    if (lpFileSizeHigh) *lpFileSizeHigh = static_cast<DWORD>(bytes >> 32);
    return static_cast<DWORD>(bytes & 0xFFFFFFFFu);
}

BOOL DeviceIoControl(HANDLE hDevice, DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize, LPVOID lpOutBuffer, DWORD nOutBufferSize, LPDWORD lpBytesReturned, LPOVERLAPPED) {
    if (lpBytesReturned) *lpBytesReturned = 0;
    if (dwIoControlCode == FSCTL_SET_SPARSE) {
        return (Explorer2::Compat::FdOf(hDevice) >= 0) ? TRUE : FALSE;
    }
    if (dwIoControlCode == FSCTL_QUERY_ALLOCATED_RANGES) {
        Explorer2::Compat::FlushHandleStream(Explorer2::Compat::FileHandleOf(hDevice));   // a failure stays in the handle
        const int fd = Explorer2::Compat::FdOf(hDevice);
        struct stat st = {};
        if (fd < 0 || !lpInBuffer || nInBufferSize < sizeof(FILE_ALLOCATED_RANGE_BUFFER) ||
            !lpOutBuffer || nOutBufferSize < sizeof(FILE_ALLOCATED_RANGE_BUFFER) ||
            ::fstat(fd, &st) != 0) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        const auto* inRange = static_cast<const FILE_ALLOCATED_RANGE_BUFFER*>(lpInBuffer);
        auto* outRanges = static_cast<FILE_ALLOCATED_RANGE_BUFFER*>(lpOutBuffer);
        const size_t maxEntries = nOutBufferSize / sizeof(FILE_ALLOCATED_RANGE_BUFFER);
        const off_t fileEnd = st.st_size;
        const off_t qStart = std::max<off_t>(0, static_cast<off_t>(inRange->FileOffset.QuadPart));
        const off_t qEnd = std::min<off_t>(fileEnd, qStart + std::max<off_t>(0, static_cast<off_t>(inRange->Length.QuadPart)));
        if (qStart >= qEnd || fileEnd <= 0) {
            if (lpBytesReturned) *lpBytesReturned = 0;
            return TRUE;
        }

        const off_t savedPos = ::lseek(fd, 0, SEEK_CUR);
        size_t writtenEntries = 0;
        bool moreData = false;
        off_t cur = qStart;

#ifdef SEEK_DATA
        while (cur < qEnd) {
            const off_t dataStart = ::lseek(fd, cur, SEEK_DATA);
            if (dataStart < 0) {
                if (errno == ENXIO) break; // No more allocated data up to EOF
                // Filesystem does not support SEEK_DATA: report single dense extent
                outRanges[0].FileOffset.QuadPart = qStart;
                outRanges[0].Length.QuadPart = qEnd - qStart;
                writtenEntries = 1;
                break;
            }
            if (dataStart >= qEnd) break;
            off_t holeStart = ::lseek(fd, dataStart, SEEK_HOLE);
            if (holeStart < 0 || holeStart > qEnd) holeStart = qEnd;
            if (holeStart > dataStart) {
                if (writtenEntries >= maxEntries) {
                    moreData = true;
                    break;
                }
                outRanges[writtenEntries].FileOffset.QuadPart = static_cast<LONGLONG>(dataStart);
                outRanges[writtenEntries].Length.QuadPart = static_cast<LONGLONG>(holeStart - dataStart);
                ++writtenEntries;
            }
            cur = (holeStart > dataStart) ? holeStart : (dataStart + 1);
        }
#else
        outRanges[0].FileOffset.QuadPart = qStart;
        outRanges[0].Length.QuadPart = qEnd - qStart;
        writtenEntries = 1;
#endif
        if (savedPos >= 0) (void)::lseek(fd, savedPos, SEEK_SET);
        if (lpBytesReturned) *lpBytesReturned = static_cast<DWORD>(writtenEntries * sizeof(FILE_ALLOCATED_RANGE_BUFFER));
        if (moreData) {
            SetLastError(ERROR_MORE_DATA);
            return FALSE;
        }
        SetLastError(ERROR_SUCCESS);
        return TRUE;
    }
    if (dwIoControlCode == FSCTL_SET_REPARSE_POINT) {
        auto* b = Explorer2::Compat::BaseOf(hDevice);
        if (!b || b->kind != Explorer2::Compat::HandleKind::File || !lpInBuffer || nInBufferSize < 16) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        auto* fh = static_cast<Explorer2::Compat::FileHandle*>(b);
        const auto* raw = static_cast<const uint8_t*>(lpInBuffer);
        uint32_t tag = 0;
        std::memcpy(&tag, raw, 4);
        if (tag == IO_REPARSE_TAG_MOUNT_POINT && !fh->nativePath.empty()) {
            uint16_t printOff = 0, printLen = 0;
            std::memcpy(&printOff, raw + 12, 2);
            std::memcpy(&printLen, raw + 14, 2);
            if (static_cast<size_t>(16) + printOff + printLen <= nInBufferSize && printLen >= sizeof(wchar_t)) {
                std::wstring targetW(printLen / sizeof(wchar_t), L'\0');
                std::memcpy(targetW.data(), raw + 16 + printOff, targetW.size() * sizeof(wchar_t));
                const std::string targetNative = Explorer2::Compat::NativePath(targetW.c_str());
                if (fh->dirFd >= 0) { ::close(fh->dirFd); fh->dirFd = -1; }
                ::rmdir(fh->nativePath.c_str());
                if (::symlink(targetNative.c_str(), fh->nativePath.c_str()) == 0) {
                    return TRUE;
                }
                Explorer2::Compat::SetErrorFromErrno();
                return FALSE;
            }
        }
    }
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

DWORD GetLogicalDrives() { return 0x00000004u; /* C: virtual mapping when asked */ }

DWORD GetLogicalDriveStringsW(DWORD nBufferLength, LPWSTR lpBuffer) {
    const wchar_t roots[] = L"/\0";
    if (!lpBuffer || nBufferLength < 3) return 3;
    std::wmemcpy(lpBuffer, roots, 3);
    return 2;
}

namespace {
struct HalMount {
    std::string point;      // the mount point, with the octal escapes of /proc/self/mounts decoded
    std::string device;
    std::string fsType;
    UINT driveType = 0;     // DRIVE_*, worked out when first asked for (0: not yet)
};
std::mutex g_mountTableMutex;
std::vector<HalMount> g_mountTable;
ULONGLONG g_mountTableTick = 0;

// /proc/self/mounts writes a space as \040, a tab as \011, a newline as \012 and a backslash as \134
std::string DecodeMountField(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 3 < s.size() &&
            s[i + 1] >= '0' && s[i + 1] <= '7' && s[i + 2] >= '0' && s[i + 2] <= '7' && s[i + 3] >= '0' && s[i + 3] <= '7') {
            out.push_back(static_cast<char>(((s[i + 1] - '0') << 6) | ((s[i + 2] - '0') << 3) | (s[i + 3] - '0')));
            i += 3;
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

// "/a/./b//c/../d" -> "/a/b/d", without asking the file system: a symbolic link on the way is not resolved,
// because resolving one can wait for ever on a network mount that stopped answering
std::string NormalizeAbsolutePath(const std::string& p) {
    std::vector<std::string> parts;
    size_t i = 0;
    while (i <= p.size()) {
        size_t j = p.find('/', i);
        if (j == std::string::npos) j = p.size();
        const std::string part = p.substr(i, j - i);
        if (part == "..") {
            if (!parts.empty()) parts.pop_back();
        } else if (!part.empty() && part != ".") {
            parts.push_back(part);
        }
        i = j + 1;
    }
    std::string out;
    for (const auto& part : parts) {
        out.push_back('/');
        out += part;
    }
    return out.empty() ? std::string("/") : out;
}

UINT ClassifyMount(const HalMount& m) {
    static const char* const kRemoteTypes[] = {
        "cifs", "smb3", "smbfs", "nfs", "nfs4", "9p", "ceph", "glusterfs", "fuse.glusterfs", "davfs", "afs",
        "ncpfs", "sshfs", "fuse.sshfs", "fuse.rclone", "fuse.gvfsd-fuse" };
    for (const char* t : kRemoteTypes) {
        if (m.fsType == t) return DRIVE_REMOTE;
    }
    if (m.fsType == "iso9660" || m.fsType == "udf") return DRIVE_CDROM;
    if (m.device.rfind("/dev/", 0) != 0) return DRIVE_FIXED;     // tmpfs, overlay, proc and other virtual file systems
    // /dev/mapper/... and /dev/disk/by-.../... are links to the device node (all of it lives in devtmpfs)
    std::string dev = m.device;
    char resolved[PATH_MAX] = {};
    if (::realpath(dev.c_str(), resolved)) dev = resolved;
    const std::string name = dev.substr(dev.find_last_of('/') + 1);
    if (name.empty()) return DRIVE_FIXED;
    if (name.rfind("sr", 0) == 0) return DRIVE_CDROM;
    char link[PATH_MAX] = {};
    const ssize_t n = ::readlink(("/sys/class/block/" + name).c_str(), link, sizeof(link) - 1);
    const std::string sysPath = (n > 0) ? std::string(link, static_cast<size_t>(n)) : std::string();
    // A disk behind USB (a stick, a card reader, a USB hard disk): its sysfs path runs through the USB device
    if (sysPath.find("/usb") != std::string::npos) return DRIVE_REMOVABLE;
    // A partition's disk is the folder above the partition's own one in sysfs (.../block/sdb/sdb1)
    std::string disk = name;
    struct stat pst = {};
    if (!sysPath.empty() && ::stat(("/sys/class/block/" + name + "/partition").c_str(), &pst) == 0) {
        const size_t last = sysPath.find_last_of('/');
        if (last != std::string::npos && last > 0) {
            const size_t prev = sysPath.find_last_of('/', last - 1);
            const size_t start = (prev == std::string::npos) ? 0 : prev + 1;
            if (last > start) disk = sysPath.substr(start, last - start);
        }
    }
    const std::string removable = ReadKernelTextFile("/sys/block/" + disk + "/removable");
    if (!removable.empty() && removable[0] == '1') return DRIVE_REMOVABLE;
    return DRIVE_FIXED;
}

// The type of the mount that holds `path` (absolute and normalized): the longest mount point that is a prefix of
// it at a '/'. The mount table is read again at most every two seconds; only /proc and /sys are read, which
// never wait for a disk or a server.
UINT DriveTypeOfPath(const std::string& path) {
    std::lock_guard<std::mutex> lk(g_mountTableMutex);
    const ULONGLONG now = GetTickCount64();
    if (g_mountTableTick == 0 || now - g_mountTableTick > 2000) {
        std::vector<HalMount> fresh;
        const std::string text = ReadKernelTextFile("/proc/self/mounts", 8u << 20);   // hosts with containers have thousands of mounts
        std::istringstream lines(text);
        std::string line;
        while (std::getline(lines, line)) {
            std::istringstream iss(line);
            std::string dev, mnt, fs;
            if (!(iss >> dev >> mnt >> fs)) continue;
            HalMount m;
            m.device = DecodeMountField(dev);
            m.point = DecodeMountField(mnt);
            m.fsType = fs;
            if (m.point.empty() || m.point[0] != '/') continue;
            fresh.push_back(std::move(m));
        }
        if (!fresh.empty()) g_mountTable = std::move(fresh);
        g_mountTableTick = now ? now : 1;
    }
    HalMount* best = nullptr;
    for (auto& m : g_mountTable) {
        const std::string& mp = m.point;
        const bool inside = (mp == "/") ||
            (path.compare(0, mp.size(), mp) == 0 && (path.size() == mp.size() || path[mp.size()] == '/'));
        // ">=": of two mounts on the same point the later one, which hides the earlier, wins
        if (inside && (!best || mp.size() >= best->point.size())) best = &m;
    }
    if (!best) return DRIVE_FIXED;
    if (best->driveType == 0) best->driveType = ClassifyMount(*best);
    return best->driveType;
}
} // namespace

UINT GetDriveTypeW(LPCWSTR lpRootPathName) {
    if (!lpRootPathName || !*lpRootPathName) return DRIVE_UNKNOWN;
    std::string native = Explorer2::Compat::NativePath(lpRootPathName);
    if (native.rfind("//", 0) == 0) return DRIVE_REMOTE;                    // \\server\share
    if (native.empty()) return DRIVE_UNKNOWN;
    if (native.size() >= 2 && native[1] == ':') native = "/";                // this port's "C:" is the root
    if (native[0] != '/') {
        char cwd[PATH_MAX] = {};
        native = (::getcwd(cwd, sizeof(cwd)) ? std::string(cwd) : std::string("/")) + "/" + native;
    }
    return DriveTypeOfPath(NormalizeAbsolutePath(native));
}

BOOL GetDiskFreeSpaceExW(LPCWSTR lpDirectoryName, ULARGE_INTEGER* lpFreeBytesAvailableToCaller, ULARGE_INTEGER* lpTotalNumberOfBytes, ULARGE_INTEGER* lpTotalNumberOfFreeBytes) {
    std::string path = lpDirectoryName ? Explorer2::Compat::NativePath(lpDirectoryName) : "/";
    if (path.empty() || (path.size() >= 2 && path[1] == ':')) path = "/";
    struct statvfs vfs = {};
    if (::statvfs(path.c_str(), &vfs) != 0) {
        Explorer2::Compat::SetErrorFromErrno();
        return FALSE;
    }
    const uint64_t bsize = vfs.f_frsize ? vfs.f_frsize : vfs.f_bsize;
    if (lpFreeBytesAvailableToCaller) lpFreeBytesAvailableToCaller->QuadPart = static_cast<ULONGLONG>(vfs.f_bavail) * bsize;
    if (lpTotalNumberOfBytes) lpTotalNumberOfBytes->QuadPart = static_cast<ULONGLONG>(vfs.f_blocks) * bsize;
    if (lpTotalNumberOfFreeBytes) lpTotalNumberOfFreeBytes->QuadPart = static_cast<ULONGLONG>(vfs.f_bfree) * bsize;
    return TRUE;
}

BOOL GetDiskFreeSpaceW(LPCWSTR lpRootPathName, LPDWORD lpSectorsPerCluster, LPDWORD lpBytesPerSector, LPDWORD lpNumberOfFreeClusters, LPDWORD lpTotalNumberOfClusters) {
    ULARGE_INTEGER avail{}, total{}, freeB{};
    if (!GetDiskFreeSpaceExW(lpRootPathName, &avail, &total, &freeB)) return FALSE;
    if (lpBytesPerSector) *lpBytesPerSector = 4096;
    if (lpSectorsPerCluster) *lpSectorsPerCluster = 1;
    if (lpNumberOfFreeClusters) *lpNumberOfFreeClusters = static_cast<DWORD>(std::min<uint64_t>(freeB.QuadPart / 4096ULL, 0xFFFFFFFFULL));
    if (lpTotalNumberOfClusters) *lpTotalNumberOfClusters = static_cast<DWORD>(std::min<uint64_t>(total.QuadPart / 4096ULL, 0xFFFFFFFFULL));
    return TRUE;
}

BOOL GetVolumePathNameW(LPCWSTR lpszFileName, LPWSTR lpszVolumePathName, DWORD cchBufferLength) {
    if (!lpszFileName || !lpszVolumePathName || cchBufferLength < 2) return FALSE;
    std::string cur = Explorer2::Compat::NativePath(lpszFileName);
    if (cur.empty() || cur[0] != '/') {
        wcscpy_s(lpszVolumePathName, cchBufferLength, L"/");
        return TRUE;
    }
    struct stat st = {};
    while (cur.size() > 1 && ::lstat(cur.c_str(), &st) != 0) {
        const size_t sl = cur.find_last_of('/');
        cur = (sl == 0 || sl == std::string::npos) ? "/" : cur.substr(0, sl);
    }
    if (::lstat(cur.c_str(), &st) == 0) {
        const dev_t targetDev = st.st_dev;
        while (cur.size() > 1) {
            const size_t sl = cur.find_last_of('/');
            std::string par = (sl == 0 || sl == std::string::npos) ? "/" : cur.substr(0, sl);
            struct stat pst = {};
            if (::lstat(par.c_str(), &pst) != 0 || pst.st_dev != targetDev) break;
            cur = std::move(par);
        }
    } else {
        cur = "/";
    }
    std::wstring w = Explorer2::PathUtils::Utf8ToWide(cur);
    if (w.size() >= cchBufferLength) w = L"/";
    wcscpy_s(lpszVolumePathName, cchBufferLength, w.c_str());
    return TRUE;
}

BOOL GetVolumeInformationW(LPCWSTR lpRootPathName, LPWSTR lpVolumeNameBuffer, DWORD nVolumeNameSize, LPDWORD lpVolumeSerialNumber, LPDWORD lpMaximumComponentLength, LPDWORD lpFileSystemFlags, LPWSTR lpFileSystemNameBuffer, DWORD nFileSystemNameSize) {
    std::string path = lpRootPathName ? Explorer2::Compat::NativePath(lpRootPathName) : "/";
    if (path.empty() || (path.size() >= 2 && path[1] == ':')) path = "/";
    struct stat st = {};
    if (::stat(path.c_str(), &st) != 0) { Explorer2::Compat::SetErrorFromErrno(); return FALSE; }
    if (lpVolumeNameBuffer && nVolumeNameSize > 0) wcscpy_s(lpVolumeNameBuffer, nVolumeNameSize, path == "/" ? L"Linux Root" : L"Linux Volume");
    if (lpVolumeSerialNumber) *lpVolumeSerialNumber = static_cast<DWORD>(st.st_dev);
    if (lpMaximumComponentLength) *lpMaximumComponentLength = 255;
    if (lpFileSystemFlags) *lpFileSystemFlags = FILE_SUPPORTS_SPARSE_FILES | FILE_SUPPORTS_REPARSE_POINTS;
    if (lpFileSystemNameBuffer && nFileSystemNameSize > 0) wcscpy_s(lpFileSystemNameBuffer, nFileSystemNameSize, L"ext4");
    return TRUE;
}

BOOL ReadDirectoryChangesW(HANDLE, LPVOID, DWORD, BOOL, DWORD, LPDWORD, LPOVERLAPPED, void*) {
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}
BOOL CancelIo(HANDLE) { return TRUE; }
BOOL GetOverlappedResult(HANDLE, LPOVERLAPPED, LPDWORD lpBytes, BOOL) {
    if (lpBytes) *lpBytes = 0;
    return FALSE;
}

// ---- INI File Reader / Writer (UTF-8 & UTF-16LE BOM transparent support!) ------------------------------------
namespace {
struct IniSection {
    std::wstring name;
    std::vector<std::pair<std::wstring, std::wstring>> kv;
};

std::wstring ReadIniTextFile(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return L"";
    std::vector<uint8_t> bytes;
    uint8_t buf[4096];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) bytes.insert(bytes.end(), buf, buf + n);
    std::fclose(f);
    if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
        // UTF-16LE
        std::wstring out;
        for (size_t i = 2; i + 1 < bytes.size(); i += 2) {
            uint16_t u = static_cast<uint16_t>(bytes[i] | (bytes[i + 1] << 8));
            if (u >= 0xD800u && u <= 0xDBFFu && i + 3 < bytes.size()) {
                uint16_t lo = static_cast<uint16_t>(bytes[i + 2] | (bytes[i + 3] << 8));
                if (lo >= 0xDC00u && lo <= 0xDFFFu) {
                    uint32_t cp = 0x10000u + ((uint32_t(u - 0xD800u) << 10) | uint32_t(lo - 0xDC00u));
                    out.push_back(static_cast<wchar_t>(cp));
                    i += 2;
                    continue;
                }
            }
            out.push_back(static_cast<wchar_t>(u));
        }
        return out;
    }
    size_t start = (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) ? 3 : 0;
    return Explorer2::PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(bytes.data() + start), bytes.size() - start));
}

std::vector<IniSection> ParseIni(const std::wstring& text) {
    std::vector<IniSection> secs;
    IniSection cur;
    size_t i = 0;
    while (i < text.size()) {
        size_t eol = text.find_first_of(L"\r\n", i);
        std::wstring line = (eol == std::wstring::npos) ? text.substr(i) : text.substr(i, eol - i);
        i = (eol == std::wstring::npos) ? text.size() : eol + 1;
        std::wstring trimmed = Explorer2::PathUtils::Trim(line);
        if (trimmed.empty() || trimmed[0] == L';' || trimmed[0] == L'#') continue;
        if (trimmed.front() == L'[' && trimmed.back() == L']') {
            if (!cur.name.empty() || !cur.kv.empty()) secs.push_back(std::move(cur));
            cur = IniSection{};
            cur.name = Explorer2::PathUtils::Trim(std::wstring_view(trimmed).substr(1, trimmed.size() - 2));
            continue;
        }
        size_t eq = line.find(L'=');
        if (eq != std::wstring::npos) {
            std::wstring k = Explorer2::PathUtils::Trim(std::wstring_view(line).substr(0, eq));
            std::wstring v = Explorer2::PathUtils::Trim(std::wstring_view(line).substr(eq + 1));
            if (v.size() >= 2 && ((v.front() == L'"' && v.back() == L'"') || (v.front() == L'\'' && v.back() == L'\''))) {
                v = v.substr(1, v.size() - 2);
            }
            cur.kv.emplace_back(std::move(k), std::move(v));
        }
    }
    if (!cur.name.empty() || !cur.kv.empty()) secs.push_back(std::move(cur));
    return secs;
}

std::mutex& IniMutex() {
    static std::mutex m;
    return m;
}
} // namespace

DWORD GetPrivateProfileStringW(LPCWSTR lpAppName, LPCWSTR lpKeyName, LPCWSTR lpDefault, LPWSTR lpReturnedString, DWORD nSize, LPCWSTR lpFileName) {
    if (!lpReturnedString || nSize == 0) return 0;
    const std::wstring def = lpDefault ? lpDefault : L"";
    if (!lpFileName) {
        const size_t n = std::min<size_t>(def.size(), nSize - 1);
        std::wmemcpy(lpReturnedString, def.data(), n);
        lpReturnedString[n] = L'\0';
        return static_cast<DWORD>(n);
    }
    std::lock_guard<std::mutex> lk(IniMutex());
    const auto secs = ParseIni(ReadIniTextFile(Explorer2::Compat::NativePath(lpFileName)));
    if (!lpAppName) {
        // Return double-NUL terminated list of section names
        size_t pos = 0;
        for (const auto& s : secs) {
            if (pos + s.name.size() + 2 > nSize) break;
            std::wmemcpy(lpReturnedString + pos, s.name.c_str(), s.name.size() + 1);
            pos += s.name.size() + 1;
        }
        lpReturnedString[pos] = L'\0';
        return static_cast<DWORD>(pos > 0 ? pos - 1 : 0);
    }
    for (const auto& s : secs) {
        if (_wcsicmp(s.name.c_str(), lpAppName) != 0) continue;
        if (!lpKeyName) {
            size_t pos = 0;
            for (const auto& kv : s.kv) {
                if (pos + kv.first.size() + 2 > nSize) break;
                std::wmemcpy(lpReturnedString + pos, kv.first.c_str(), kv.first.size() + 1);
                pos += kv.first.size() + 1;
            }
            lpReturnedString[pos] = L'\0';
            return static_cast<DWORD>(pos > 0 ? pos - 1 : 0);
        }
        for (const auto& kv : s.kv) {
            if (_wcsicmp(kv.first.c_str(), lpKeyName) == 0) {
                const size_t n = std::min<size_t>(kv.second.size(), nSize - 1);
                std::wmemcpy(lpReturnedString, kv.second.data(), n);
                lpReturnedString[n] = L'\0';
                return static_cast<DWORD>(n);
            }
        }
    }
    const size_t n = std::min<size_t>(def.size(), nSize - 1);
    std::wmemcpy(lpReturnedString, def.data(), n);
    lpReturnedString[n] = L'\0';
    return static_cast<DWORD>(n);
}

UINT GetPrivateProfileIntW(LPCWSTR lpAppName, LPCWSTR lpKeyName, INT nDefault, LPCWSTR lpFileName) {
    wchar_t buf[64] = {};
    std::wstring def = std::to_wstring(nDefault);
    GetPrivateProfileStringW(lpAppName, lpKeyName, def.c_str(), buf, 64, lpFileName);
    wchar_t* end = nullptr;
    long v = std::wcstol(buf, &end, 0);
    return (end && end != buf) ? static_cast<UINT>(v) : static_cast<UINT>(nDefault);
}

BOOL WritePrivateProfileStringW(LPCWSTR lpAppName, LPCWSTR lpKeyName, LPCWSTR lpString, LPCWSTR lpFileName) {
    if (!lpFileName || !lpAppName) return FALSE;
    std::lock_guard<std::mutex> lk(IniMutex());
    const std::string native = Explorer2::Compat::NativePath(lpFileName);
    auto secs = ParseIni(ReadIniTextFile(native));

    auto secIt = std::find_if(secs.begin(), secs.end(), [&](const IniSection& s) {
        return _wcsicmp(s.name.c_str(), lpAppName) == 0;
    });

    if (!lpKeyName) {
        if (secIt != secs.end()) secs.erase(secIt);
    } else if (!lpString) {
        if (secIt != secs.end()) {
            secIt->kv.erase(std::remove_if(secIt->kv.begin(), secIt->kv.end(), [&](const auto& kv) {
                return _wcsicmp(kv.first.c_str(), lpKeyName) == 0;
            }), secIt->kv.end());
        }
    } else {
        if (secIt == secs.end()) {
            secs.push_back(IniSection{ lpAppName, { { lpKeyName, lpString } } });
        } else {
            auto kvIt = std::find_if(secIt->kv.begin(), secIt->kv.end(), [&](const auto& kv) {
                return _wcsicmp(kv.first.c_str(), lpKeyName) == 0;
            });
            if (kvIt != secIt->kv.end()) kvIt->second = lpString;
            else secIt->kv.emplace_back(lpKeyName, lpString);
        }
    }

    std::wstring out;
    for (const auto& s : secs) {
        out += L"[" + s.name + L"]\r\n";
        for (const auto& kv : s.kv) {
            out += kv.first + L"=" + kv.second + L"\r\n";
        }
        out += L"\r\n";
    }
    const std::string utf8 = Explorer2::PathUtils::WideToUtf8(out);
    FILE* f = std::fopen(native.c_str(), "wb");
    if (!f) { Explorer2::Compat::SetErrorFromErrno(); return FALSE; }
    const bool ok = utf8.empty() || std::fwrite(utf8.data(), 1, utf8.size(), f) == utf8.size();
    std::fclose(f);
    return ok ? TRUE : FALSE;
}

// ==============================================================================================================
// 4. Synchronization: Events, Mutexes, Processes, WaitForSingleObject / WaitForMultipleObjects
// ==============================================================================================================
namespace {

struct SyncState {
    std::mutex mtx;
    std::condition_variable cv;
    bool manualReset = true;
    bool signaled = false;
    bool isMutex = false;
    std::thread::id owner{};
    uint32_t recursion = 0;
};

std::mutex& NamedSyncMutex() {
    static std::mutex m;
    return m;
}
std::map<std::wstring, std::shared_ptr<SyncState>>& NamedEvents() {
    static std::map<std::wstring, std::shared_ptr<SyncState>> m;
    return m;
}
std::map<std::wstring, std::shared_ptr<SyncState>>& NamedMutexes() {
    static std::map<std::wstring, std::shared_ptr<SyncState>> m;
    return m;
}

struct SyncHandle : Explorer2::Compat::HandleBase {
    explicit SyncHandle(std::shared_ptr<SyncState> s)
        : HandleBase(Explorer2::Compat::HandleKind::Custom), state(std::move(s)) {}
    std::shared_ptr<SyncState> state;
};

SyncState* SyncOf(HANDLE h) {
    if (h == reinterpret_cast<HANDLE>(static_cast<intptr_t>(-2))) return nullptr;     // GetCurrentProcess(): no object behind it
    auto* b = Explorer2::Compat::BaseOf(h);
    if (!b || b->kind != Explorer2::Compat::HandleKind::Custom) return nullptr;
    auto* sh = dynamic_cast<SyncHandle*>(b);
    return sh ? sh->state.get() : nullptr;
}

} // namespace

HANDLE GetCurrentProcess() { return reinterpret_cast<HANDLE>(static_cast<intptr_t>(-2)); }

HANDLE CreateEventW(LPSECURITY_ATTRIBUTES, BOOL bManualReset, BOOL bInitialState, LPCWSTR lpName) {
    std::shared_ptr<SyncState> st;
    bool existed = false;
    if (lpName && *lpName) {
        std::lock_guard<std::mutex> lk(NamedSyncMutex());
        auto it = NamedEvents().find(lpName);
        if (it != NamedEvents().end()) {
            st = it->second;
            existed = true;
        } else {
            st = std::make_shared<SyncState>();
            st->manualReset = (bManualReset != FALSE);
            st->signaled = (bInitialState != FALSE);
            NamedEvents()[lpName] = st;
        }
    } else {
        st = std::make_shared<SyncState>();
        st->manualReset = (bManualReset != FALSE);
        st->signaled = (bInitialState != FALSE);
    }
    SetLastError(existed ? ERROR_ALREADY_EXISTS : ERROR_SUCCESS);
    return static_cast<HANDLE>(static_cast<Explorer2::Compat::HandleBase*>(new SyncHandle(std::move(st))));
}

HANDLE OpenEventW(DWORD, BOOL, LPCWSTR lpName) {
    if (!lpName || !*lpName) return nullptr;
    std::lock_guard<std::mutex> lk(NamedSyncMutex());
    auto it = NamedEvents().find(lpName);
    if (it == NamedEvents().end()) { SetLastError(ERROR_FILE_NOT_FOUND); return nullptr; }
    return static_cast<HANDLE>(static_cast<Explorer2::Compat::HandleBase*>(new SyncHandle(it->second)));
}

BOOL SetEvent(HANDLE hEvent) {
    SyncState* st = SyncOf(hEvent);
    if (!st || st->isMutex) return FALSE;
    {
        std::lock_guard<std::mutex> lk(st->mtx);
        st->signaled = true;
    }
    if (st->manualReset) st->cv.notify_all();
    else st->cv.notify_one();
    return TRUE;
}

BOOL ResetEvent(HANDLE hEvent) {
    SyncState* st = SyncOf(hEvent);
    if (!st || st->isMutex) return FALSE;
    std::lock_guard<std::mutex> lk(st->mtx);
    st->signaled = false;
    return TRUE;
}

HANDLE CreateMutexW(LPSECURITY_ATTRIBUTES, BOOL bInitialOwner, LPCWSTR lpName) {
    std::shared_ptr<SyncState> st;
    bool existed = false;
    if (lpName && *lpName) {
        std::lock_guard<std::mutex> lk(NamedSyncMutex());
        auto it = NamedMutexes().find(lpName);
        if (it != NamedMutexes().end()) {
            st = it->second;
            existed = true;
        } else {
            st = std::make_shared<SyncState>();
            st->isMutex = true;
            st->signaled = !bInitialOwner;
            if (bInitialOwner) { st->owner = std::this_thread::get_id(); st->recursion = 1; }
            NamedMutexes()[lpName] = st;
        }
    } else {
        st = std::make_shared<SyncState>();
        st->isMutex = true;
        st->signaled = !bInitialOwner;
        if (bInitialOwner) { st->owner = std::this_thread::get_id(); st->recursion = 1; }
    }
    SetLastError(existed ? ERROR_ALREADY_EXISTS : ERROR_SUCCESS);
    return static_cast<HANDLE>(static_cast<Explorer2::Compat::HandleBase*>(new SyncHandle(std::move(st))));
}

HANDLE OpenMutexW(DWORD, BOOL, LPCWSTR lpName) {
    if (!lpName || !*lpName) return nullptr;
    std::lock_guard<std::mutex> lk(NamedSyncMutex());
    auto it = NamedMutexes().find(lpName);
    if (it == NamedMutexes().end()) { SetLastError(ERROR_FILE_NOT_FOUND); return nullptr; }
    return static_cast<HANDLE>(static_cast<Explorer2::Compat::HandleBase*>(new SyncHandle(it->second)));
}

BOOL ReleaseMutex(HANDLE hMutex) {
    SyncState* st = SyncOf(hMutex);
    if (!st || !st->isMutex) return FALSE;
    {
        std::lock_guard<std::mutex> lk(st->mtx);
        if (st->owner != std::this_thread::get_id() || st->recursion == 0) return FALSE;
        if (--st->recursion == 0) {
            st->owner = std::thread::id{};
            st->signaled = true;
        }
    }
    st->cv.notify_one();
    return TRUE;
}

DWORD WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds) {
    SyncState* st = SyncOf(hHandle);
    if (!st) return WAIT_FAILED;
    std::unique_lock<std::mutex> lk(st->mtx);
    auto canAcquire = [&]() {
        if (st->isMutex) return st->signaled || st->owner == std::this_thread::get_id();
        return st->signaled;
    };
    if (!canAcquire()) {
        if (dwMilliseconds == 0) return WAIT_TIMEOUT;
        if (dwMilliseconds == INFINITE) {
            st->cv.wait(lk, canAcquire);
        } else {
            if (!st->cv.wait_for(lk, std::chrono::milliseconds(dwMilliseconds), canAcquire)) {
                return WAIT_TIMEOUT;
            }
        }
    }
    if (st->isMutex) {
        st->signaled = false;
        st->owner = std::this_thread::get_id();
        ++st->recursion;
    } else if (!st->manualReset) {
        st->signaled = false;
    }
    return WAIT_OBJECT_0;
}

DWORD WaitForMultipleObjects(DWORD nCount, const HANDLE* lpHandles, BOOL bWaitAll, DWORD dwMilliseconds) {
    if (nCount == 0 || !lpHandles || nCount > 64 /* MAXIMUM_WAIT_OBJECTS */) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return WAIT_FAILED;
    }
    // Only events, mutexes and processes become signaled here: a wait on anything else would poll for ever
    std::vector<SyncState*> states(nCount, nullptr);
    for (DWORD i = 0; i < nCount; ++i) {
        states[i] = SyncOf(lpHandles[i]);
        if (!states[i]) { SetLastError(ERROR_INVALID_HANDLE); return WAIT_FAILED; }
    }
    if (nCount == 1) return WaitForSingleObject(lpHandles[0], dwMilliseconds);
    // Wait-all locks every object at once, each once and always in the same (address) order, so that two waits on
    // overlapping sets cannot deadlock each other
    std::vector<SyncState*> distinct(states);
    std::sort(distinct.begin(), distinct.end(), std::less<SyncState*>());
    distinct.erase(std::unique(distinct.begin(), distinct.end()), distinct.end());
    const std::thread::id self = std::this_thread::get_id();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(dwMilliseconds);
    while (true) {
        if (bWaitAll) {
            // All or nothing: the objects are checked and taken while all of them are locked, so an auto-reset
            // event or a mutex is never used up by a wait that does not end with all of them
            std::vector<std::unique_lock<std::mutex>> locks;
            locks.reserve(distinct.size());
            for (SyncState* st : distinct) locks.emplace_back(st->mtx);
            bool allReady = true;
            for (const SyncState* st : distinct) {
                const bool ready = st->isMutex ? (st->signaled || st->owner == self) : st->signaled;
                if (!ready) { allReady = false; break; }
            }
            if (allReady) {
                for (SyncState* st : distinct) {
                    if (st->isMutex) {
                        st->signaled = false;
                        st->owner = self;
                        ++st->recursion;
                    } else if (!st->manualReset) {
                        st->signaled = false;
                    }
                }
                return WAIT_OBJECT_0;
            }
        } else {
            for (DWORD i = 0; i < nCount; ++i) {
                if (WaitForSingleObject(lpHandles[i], 0) == WAIT_OBJECT_0) return WAIT_OBJECT_0 + i;
            }
        }
        if (dwMilliseconds == 0) return WAIT_TIMEOUT;
        if (dwMilliseconds != INFINITE && std::chrono::steady_clock::now() >= deadline) return WAIT_TIMEOUT;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

// POSIX Child Process & Pipe HAL (WaitForSingleObject-compatible ProcessHandle + pipe2)
namespace {
struct ChildProcState {
    pid_t pid = -1;
    std::atomic<bool> exited{ false };
    std::atomic<DWORD> exitCode{ 259u }; // STILL_ACTIVE
    std::atomic<bool> forcedKill{ false };
    std::atomic<DWORD> forcedCode{ 1u };
};

struct ProcessSyncHandle : SyncHandle {
    ProcessSyncHandle(std::shared_ptr<SyncState> s, std::shared_ptr<ChildProcState> p)
        : SyncHandle(std::move(s)), proc(std::move(p)) {}
    std::shared_ptr<ChildProcState> proc;
};
} // namespace

__attribute__((weak)) HANDLE OpenProcess(DWORD, BOOL, DWORD) { return nullptr; }
BOOL SetPriorityClass(HANDLE, DWORD) { return TRUE; }

BOOL TerminateProcess(HANDLE hProcess, UINT uExitCode) {
    if (hProcess == GetCurrentProcess()) ::_exit(static_cast<int>(uExitCode));
    auto* b = Explorer2::Compat::BaseOf(hProcess);
    if (!b || b->kind != Explorer2::Compat::HandleKind::Custom) return FALSE;
    if (auto* ph = dynamic_cast<ProcessSyncHandle*>(b)) {
        if (ph->proc && !ph->proc->exited.load()) {
            ph->proc->forcedCode.store(static_cast<DWORD>(uExitCode));
            ph->proc->forcedKill.store(true);
            (void)::kill(-ph->proc->pid, SIGKILL);
            (void)::kill(ph->proc->pid, SIGKILL);
        }
        return TRUE;
    }
    return FALSE;
}

BOOL GetExitCodeProcess(HANDLE hProcess, LPDWORD lpExitCode) {
    if (!lpExitCode) return FALSE;
    if (hProcess == GetCurrentProcess()) { *lpExitCode = 0; return TRUE; }
    auto* b = Explorer2::Compat::BaseOf(hProcess);
    if (!b || b->kind != Explorer2::Compat::HandleKind::Custom) return FALSE;
    if (auto* ph = dynamic_cast<ProcessSyncHandle*>(b)) {
        if (!ph->proc) return FALSE;
        *lpExitCode = ph->proc->exited.load() ? ph->proc->exitCode.load() : 259u;
        return TRUE;
    }
    *lpExitCode = 0;
    return TRUE;
}

BOOL CreatePipe(HANDLE* hReadPipe, HANDLE* hWritePipe, LPSECURITY_ATTRIBUTES, DWORD) {
    if (!hReadPipe || !hWritePipe) return FALSE;
    *hReadPipe = INVALID_HANDLE_VALUE;
    *hWritePipe = INVALID_HANDLE_VALUE;
    int pfd[2] = { -1, -1 };
    if (::pipe2(pfd, O_CLOEXEC) != 0) {
        Explorer2::Compat::SetErrorFromErrno();
        return FALSE;
    }
    FILE* fr = ::fdopen(pfd[0], "rb");
    FILE* fw = ::fdopen(pfd[1], "wb");
    if (!fr || !fw) {
        if (fr) std::fclose(fr); else ::close(pfd[0]);
        if (fw) std::fclose(fw); else ::close(pfd[1]);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    std::setvbuf(fr, nullptr, _IONBF, 0);
    std::setvbuf(fw, nullptr, _IONBF, 0);
    // Pipe ends: ReadFile returns what has arrived (see isStream), and a failed write to the other end (EPIPE once
    // the reader is gone) is reported like any other failed write
    auto* hr = new Explorer2::Compat::FileHandle();
    hr->f = fr;
    hr->shareMode = FILE_SHARE_READ | FILE_SHARE_WRITE;
    hr->isStream = true;
    auto* hw = new Explorer2::Compat::FileHandle();
    hw->f = fw;
    hw->shareMode = FILE_SHARE_READ | FILE_SHARE_WRITE;
    hw->isStream = true;
    hw->canWrite = true;
    *hReadPipe  = static_cast<HANDLE>(static_cast<Explorer2::Compat::HandleBase*>(hr));
    *hWritePipe = static_cast<HANDLE>(static_cast<Explorer2::Compat::HandleBase*>(hw));
    return TRUE;
}

BOOL PeekNamedPipe(HANDLE hNamedPipe, LPVOID, DWORD, LPDWORD lpBytesRead, LPDWORD lpTotalBytesAvail, LPDWORD lpBytesLeftThisMessage) {
    if (lpBytesRead) *lpBytesRead = 0;
    if (lpTotalBytesAvail) *lpTotalBytesAvail = 0;
    if (lpBytesLeftThisMessage) *lpBytesLeftThisMessage = 0;
    const int fd = Explorer2::Compat::FdOf(hNamedPipe);
    if (fd < 0) return FALSE;
    int avail = 0;
    if (::ioctl(fd, FIONREAD, &avail) != 0) return FALSE;
    if (lpTotalBytesAvail) *lpTotalBytesAvail = static_cast<DWORD>(std::max(0, avail));
    return TRUE;
}

BOOL SetHandleInformation(HANDLE, DWORD, DWORD) { return TRUE; }
BOOL InitializeProcThreadAttributeList(LPPROC_THREAD_ATTRIBUTE_LIST, DWORD, DWORD, SIZE_T* lpSize) { if (lpSize) *lpSize = 16; return TRUE; }
BOOL UpdateProcThreadAttribute(LPPROC_THREAD_ATTRIBUTE_LIST, DWORD, DWORD_PTR, PVOID, SIZE_T, PVOID, SIZE_T*) { return TRUE; }
void DeleteProcThreadAttributeList(LPPROC_THREAD_ATTRIBUTE_LIST) {}

BOOL CreateProcessW(
    LPCWSTR lpApplicationName,
    LPWSTR lpCommandLine,
    LPSECURITY_ATTRIBUTES,
    LPSECURITY_ATTRIBUTES,
    BOOL bInheritHandles,
    DWORD,
    LPVOID,
    LPCWSTR lpCurrentDirectory,
    STARTUPINFOW* lpStartupInfo,
    PROCESS_INFORMATION* lpProcessInformation)
{
    if (!lpProcessInformation) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    std::memset(lpProcessInformation, 0, sizeof(*lpProcessInformation));

    std::vector<std::string> argsUtf8;
    if (lpCommandLine && *lpCommandLine) {
        int argc = 0;
        LPWSTR* argvW = CommandLineToArgvW(lpCommandLine, &argc);
        if (argvW) {
            for (int i = 0; i < argc; ++i) {
                argsUtf8.push_back(Explorer2::PathUtils::WideToUtf8(argvW[i]));
            }
            LocalFree(argvW);
        }
    }
    std::string exeNative;
    if (lpApplicationName && *lpApplicationName) {
        exeNative = Explorer2::Compat::NativePath(lpApplicationName);
    } else if (!argsUtf8.empty()) {
        exeNative = Explorer2::Compat::NativePath(Explorer2::PathUtils::Utf8ToWide(argsUtf8[0]).c_str());
    }
    if (exeNative.empty()) { SetLastError(ERROR_FILE_NOT_FOUND); return FALSE; }
    if (argsUtf8.empty()) argsUtf8.push_back(exeNative);
    else argsUtf8[0] = exeNative;

    std::vector<char*> argvPtrs;
    argvPtrs.reserve(argsUtf8.size() + 1);
    for (auto& s : argsUtf8) argvPtrs.push_back(s.data());
    argvPtrs.push_back(nullptr);

    const std::string cwdNative = (lpCurrentDirectory && *lpCurrentDirectory)
        ? Explorer2::Compat::NativePath(lpCurrentDirectory)
        : std::string();

    int stdInFd = -1, stdOutFd = -1, stdErrFd = -1;
    if (bInheritHandles && lpStartupInfo && (lpStartupInfo->dwFlags & STARTF_USESTDHANDLES)) {
        stdInFd  = Explorer2::Compat::FdOf(lpStartupInfo->hStdInput);
        stdOutFd = Explorer2::Compat::FdOf(lpStartupInfo->hStdOutput);
        stdErrFd = Explorer2::Compat::FdOf(lpStartupInfo->hStdError);
    }

    int errPipe[2] = { -1, -1 };
    if (::pipe2(errPipe, O_CLOEXEC) != 0) {
        Explorer2::Compat::SetErrorFromErrno();
        return FALSE;
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        Explorer2::Compat::SetErrorFromErrno();
        ::close(errPipe[0]);
        ::close(errPipe[1]);
        return FALSE;
    }
    if (pid == 0) {
        // main() ignores SIGPIPE, and an ignored signal stays ignored across exec: the program started here gets
        // the default action back, as every program expects
        ::signal(SIGPIPE, SIG_DFL);
        ::close(errPipe[0]);
        (void)::setpgid(0, 0);
        if (!cwdNative.empty()) {
            if (::chdir(cwdNative.c_str()) != 0) {}
        }
        const int devNull = ::open("/dev/null", O_RDWR);
        if (stdInFd >= 0)  ::dup2(stdInFd, STDIN_FILENO);
        else if (devNull >= 0) ::dup2(devNull, STDIN_FILENO);

        if (stdOutFd >= 0) ::dup2(stdOutFd, STDOUT_FILENO);
        else if (devNull >= 0) ::dup2(devNull, STDOUT_FILENO);

        if (stdErrFd >= 0) ::dup2(stdErrFd, STDERR_FILENO);
        else if (devNull >= 0) ::dup2(devNull, STDERR_FILENO);

        if (devNull > 2) ::close(devNull);
        const long maxFd = ::sysconf(_SC_OPEN_MAX);
        const int limitFd = (maxFd > 3 && maxFd < 4096) ? static_cast<int>(maxFd) : 1024;
        for (int fd = 3; fd < limitFd; ++fd) {
            if (fd != errPipe[1]) ::close(fd);
        }

        ::execvp(exeNative.c_str(), argvPtrs.data());
        const int execErr = errno ? errno : ENOENT;
        const ssize_t wr = ::write(errPipe[1], &execErr, sizeof(execErr));
        (void)wr;
        ::_exit(127);
    }

    ::close(errPipe[1]);
    (void)::setpgid(pid, pid);

    int childExecErr = 0;
    ssize_t nRead = 0;
    while ((nRead = ::read(errPipe[0], &childExecErr, sizeof(childExecErr))) < 0 && errno == EINTR) {}
    ::close(errPipe[0]);

    if (nRead > 0 && childExecErr != 0) {
        int wst = 0;
        while (::waitpid(pid, &wst, 0) < 0 && errno == EINTR) {}
        SetLastError(Explorer2::Compat::Win32ErrorFromErrno(childExecErr));
        return FALSE;
    }

    auto syncSt = std::make_shared<SyncState>();
    syncSt->manualReset = true;
    syncSt->signaled = false;

    auto procSt = std::make_shared<ChildProcState>();
    procSt->pid = pid;

    std::thread([pid, syncSt, procSt]() {
        int wst = 0;
        while (::waitpid(pid, &wst, 0) < 0 && errno == EINTR) {}
        DWORD code = 1u;
        if (procSt->forcedKill.load()) {
            code = procSt->forcedCode.load();
        } else if (WIFEXITED(wst)) {
            code = static_cast<DWORD>(WEXITSTATUS(wst));
        } else if (WIFSIGNALED(wst)) {
            code = 128u + static_cast<DWORD>(WTERMSIG(wst));
        }
        procSt->exitCode.store(code);
        procSt->exited.store(true);
        {
            std::lock_guard<std::mutex> lk(syncSt->mtx);
            syncSt->signaled = true;
        }
        syncSt->cv.notify_all();
    }).detach();

    lpProcessInformation->hProcess = static_cast<HANDLE>(static_cast<Explorer2::Compat::HandleBase*>(
        new ProcessSyncHandle(std::move(syncSt), std::move(procSt))));
    lpProcessInformation->hThread = CreateEventW(nullptr, TRUE, TRUE, nullptr);
    lpProcessInformation->dwProcessId = static_cast<DWORD>(pid);
    lpProcessInformation->dwThreadId  = static_cast<DWORD>(pid);
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}
DWORD ResumeThread(HANDLE) { return 0; }
HANDLE CreateJobObjectW(LPSECURITY_ATTRIBUTES, LPCWSTR) { return nullptr; }
BOOL SetInformationJobObject(HANDLE, JOBOBJECTINFOCLASS, LPVOID, DWORD) { return TRUE; }
BOOL AssignProcessToJobObject(HANDLE, HANDLE) { return TRUE; }
BOOL TerminateJobObject(HANDLE, UINT) { return TRUE; }

// Security, Registry, Toolhelp, Version stubs
BOOL OpenProcessToken(HANDLE, DWORD, HANDLE*) { return FALSE; }
BOOL GetTokenInformation(HANDLE, TOKEN_INFORMATION_CLASS, LPVOID, DWORD, LPDWORD) { return FALSE; }
extern "C" BOOL ConvertSidToStringSidW(PSID, LPWSTR*) { return FALSE; }
extern "C" BOOL ConvertStringSecurityDescriptorToSecurityDescriptorW(
    LPCWSTR, DWORD, PSECURITY_DESCRIPTOR* sd, PULONG sz)
{
    if (!sd) return FALSE;
    // The caller frees the descriptor with LocalFree, which is std::free here: a GlobalAlloc block (a pointer
    // into the middle of a malloc block) corrupted the heap when the monitoring daemon exited
    *sd = std::calloc(1, 16);
    if (sz) *sz = 16;
    return *sd ? TRUE : FALSE;
}

DWORD GetLongPathNameW(LPCWSTR lpszShortPath, LPWSTR lpszLongPath, DWORD cchBuffer) {
    if (!lpszShortPath) { SetLastError(ERROR_INVALID_PARAMETER); return 0; }
    std::string native = Explorer2::Compat::NativePath(lpszShortPath);
    char resolved[4096] = {};
    std::wstring result = (::realpath(native.c_str(), resolved) != nullptr)
        ? Explorer2::PathUtils::Utf8ToWide(resolved)
        : std::wstring(lpszShortPath);
    if (!lpszLongPath || cchBuffer == 0) return static_cast<DWORD>(result.size() + 1);
    if (cchBuffer <= result.size()) return static_cast<DWORD>(result.size() + 1);
    std::wmemcpy(lpszLongPath, result.c_str(), result.size() + 1);
    return static_cast<DWORD>(result.size());
}

LONG RegOpenKeyExW(HKEY, LPCWSTR, DWORD, REGSAM, PHKEY) { return ERROR_FILE_NOT_FOUND; }
LONG RegCreateKeyExW(HKEY, LPCWSTR, DWORD, LPWSTR, DWORD, REGSAM, LPSECURITY_ATTRIBUTES, PHKEY, LPDWORD) { return ERROR_ACCESS_DENIED; }
LONG RegQueryValueExW(HKEY, LPCWSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD) { return ERROR_FILE_NOT_FOUND; }
LONG RegGetValueW(HKEY, LPCWSTR, LPCWSTR, DWORD, LPDWORD, PVOID, LPDWORD) { return ERROR_FILE_NOT_FOUND; }
LONG RegSetValueExW(HKEY, LPCWSTR, DWORD, DWORD, const BYTE*, DWORD) { return ERROR_ACCESS_DENIED; }
LONG RegDeleteValueW(HKEY, LPCWSTR) { return ERROR_FILE_NOT_FOUND; }
LONG RegEnumKeyExW(HKEY, DWORD, LPWSTR, LPDWORD, LPDWORD, LPWSTR, LPDWORD, FILETIME*) { return ERROR_NO_MORE_ITEMS; }
LONG RegEnumValueW(HKEY, DWORD, LPWSTR, LPDWORD, LPDWORD, LPDWORD, LPBYTE, LPDWORD) { return ERROR_NO_MORE_ITEMS; }
LONG RegCloseKey(HKEY) { return ERROR_SUCCESS; }

DWORD GetFileVersionInfoSizeW(LPCWSTR lptstrFilename, LPDWORD lpdwHandle) {
    if (lpdwHandle) *lpdwHandle = 0;
    if (!lptstrFilename || !*lptstrFilename) return 0;
    struct stat st = {};
    if (::stat(Explorer2::Compat::NativePath(lptstrFilename).c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return 0;
    return static_cast<DWORD>(sizeof(VS_FIXEDFILEINFO));
}
BOOL GetFileVersionInfoW(LPCWSTR lptstrFilename, DWORD, DWORD dwLen, LPVOID lpData) {
    if (!lptstrFilename || !lpData || dwLen < sizeof(VS_FIXEDFILEINFO)) return FALSE;
    struct stat st = {};
    if (::stat(Explorer2::Compat::NativePath(lptstrFilename).c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return FALSE;
    auto* ffi = static_cast<VS_FIXEDFILEINFO*>(lpData);
    std::memset(ffi, 0, sizeof(*ffi));
    ffi->dwSignature        = 0xFEEF04BDu;
    ffi->dwStrucVersion     = 0x00010000u;
    ffi->dwFileVersionMS    = 0x00010000u;
    ffi->dwFileVersionLS    = 0x00000000u;
    ffi->dwProductVersionMS = 0x00010000u;
    ffi->dwProductVersionLS = 0x00000000u;
    ffi->dwFileOS           = 0x00040004u;
    ffi->dwFileType         = 1u;
    return TRUE;
}
BOOL VerQueryValueW(LPCVOID pBlock, LPCWSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen) {
    if (!pBlock || !lplpBuffer || !puLen) return FALSE;
    if (!lpSubBlock || lpSubBlock[0] == L'\0' || (lpSubBlock[0] == L'\\' && lpSubBlock[1] == L'\0')) {
        *lplpBuffer = const_cast<LPVOID>(pBlock);
        *puLen = sizeof(VS_FIXEDFILEINFO);
        return TRUE;
    }
    return FALSE;
}

__attribute__((weak)) HANDLE CreateToolhelp32Snapshot(DWORD, DWORD) { return INVALID_HANDLE_VALUE; }
__attribute__((weak)) BOOL Process32FirstW(HANDLE, PROCESSENTRY32W*) { return FALSE; }
__attribute__((weak)) BOOL Process32NextW(HANDLE, PROCESSENTRY32W*) { return FALSE; }

// ==============================================================================================================
// 5. Shlwapi & Shell Helpers (StrCmpLogicalW, PathMatchSpecW, SHCreateDirectoryExW, SHGetKnownFolderPath, Streams)
// ==============================================================================================================
namespace {
// Lower case for comparing. Latin-1 and Latin Extended-A are mapped here: the C library's towlower knows them
// only in a UTF-8 locale, and this program never calls setlocale.
wchar_t LowerForCompare(wchar_t c) {
    if (c < 0x80) return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c + 32) : c;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return static_cast<wchar_t>(c + 32);
    if (c >= 0x100 && c <= 0x17F) {
        if (c == 0x130) return L'i';                // capital I with dot
        if (c == 0x178) return 0xFF;                // capital Y with diaeresis
        if ((c <= 0x137) || (c >= 0x14A && c <= 0x177)) return static_cast<wchar_t>(c | 1);   // pairs: even upper, odd lower
        if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) {                        // pairs: odd upper, even lower
            return (c & 1) ? static_cast<wchar_t>(c + 1) : c;
        }
        return c;                                   // kra, n preceded by apostrophe, long s
    }
    return static_cast<wchar_t>(std::towlower(static_cast<wint_t>(c)));
}

// Lower case without diacritics: a letter of Latin-1 or Latin Extended-A becomes its base letter ("ą" -> "a",
// "Ł" -> "l", "ż" -> "z"), so that names starting with Polish (or French, German, Czech...) letters sort among
// the others instead of after "z"
wchar_t FoldToBaseLetter(wchar_t c) {
    // U+00C0..U+00FF; '.' keeps the character (the multiplication and division signs)
    static const char kLatin1[] = "aaaaaaaceeeeiiiidnooooo.ouuuuytsaaaaaaaceeeeiiiidnooooo.ouuuuyty";
    // U+0100..U+017F
    static const char kLatinExtA[] =
        "aaaaaaccccccccdd" "ddeeeeeeeeeegggg" "gggghhhhiiiiiiii" "iiiijjkkklllllll"
        "lllnnnnnnnnnoooo" "oooorrrrrrssssss" "ssttttttuuuuuuuu" "uuuuwwyyyzzzzzzs";
    if (c >= 0xC0 && c <= 0xFF) {
        const char base = kLatin1[c - 0xC0];
        return (base == '.') ? c : static_cast<wchar_t>(base);
    }
    if (c >= 0x100 && c <= 0x17F) return static_cast<wchar_t>(kLatinExtA[c - 0x100]);
    return LowerForCompare(c);
}

bool IsAsciiDigit(wchar_t c) { return c >= L'0' && c <= L'9'; }

// Explorer's "logical" order: runs of digits compare by their value, other characters one by one after `fold`.
// With zerosCount, of two equal numbers the one with more leading zeros comes first.
template <typename Fold>
int CompareNatural(const wchar_t* a, const wchar_t* b, Fold fold, bool zerosCount) {
    while (*a && *b) {
        if (IsAsciiDigit(*a) && IsAsciiDigit(*b)) {
            const wchar_t* az = a;
            const wchar_t* bz = b;
            while (*a == L'0') ++a;
            while (*b == L'0') ++b;
            const wchar_t* an = a;
            const wchar_t* bn = b;
            while (IsAsciiDigit(*a)) ++a;
            while (IsAsciiDigit(*b)) ++b;
            const size_t alen = static_cast<size_t>(a - an);
            const size_t blen = static_cast<size_t>(b - bn);
            if (alen != blen) return (alen < blen) ? -1 : 1;
            for (size_t i = 0; i < alen; ++i) {
                if (an[i] != bn[i]) return (an[i] < bn[i]) ? -1 : 1;
            }
            if (zerosCount) {
                const size_t azLen = static_cast<size_t>(an - az);
                const size_t bzLen = static_cast<size_t>(bn - bz);
                if (azLen != bzLen) return (azLen > bzLen) ? -1 : 1;
            }
            continue;
        }
        const wchar_t ca = fold(*a);
        const wchar_t cb = fold(*b);
        if (ca != cb) return (ca < cb) ? -1 : 1;
        ++a;
        ++b;
    }
    return (*a == *b) ? 0 : (*a ? 1 : -1);
}
} // namespace

// Three keys, each deciding only where the ones before it tie: letters without diacritics and case, then with
// diacritics (an unaccented letter first), then the exact characters, so that the order is total and stable
int StrCmpLogicalW(LPCWSTR psz1, LPCWSTR psz2) {
    if (!psz1 && !psz2) return 0;
    if (!psz1) return -1;
    if (!psz2) return 1;
    int r = CompareNatural(psz1, psz2, FoldToBaseLetter, false);
    if (r == 0) r = CompareNatural(psz1, psz2, LowerForCompare, true);
    if (r == 0) {
        const int c = std::wcscmp(psz1, psz2);
        r = (c < 0) ? -1 : (c > 0 ? 1 : 0);
    }
    return r;
}

BOOL PathMatchSpecW(LPCWSTR pszFile, LPCWSTR pszSpec) {
    if (!pszFile || !pszSpec) return FALSE;
    std::wstring specs(pszSpec);
    size_t start = 0;
    while (start <= specs.size()) {
        size_t semi = specs.find(L';', start);
        std::wstring one = (semi == std::wstring::npos) ? specs.substr(start) : specs.substr(start, semi - start);
        one = Explorer2::PathUtils::Trim(one);
        if (!one.empty() && Explorer2::Compat::WildcardMatch(one.c_str(), pszFile)) return TRUE;
        if (semi == std::wstring::npos) break;
        start = semi + 1;
    }
    return FALSE;
}

BOOL PathFileExistsW(LPCWSTR pszPath) {
    return (pszPath && GetFileAttributesW(pszPath) != INVALID_FILE_ATTRIBUTES) ? TRUE : FALSE;
}

BOOL PathIsDirectoryW(LPCWSTR pszPath) {
    if (!pszPath) return FALSE;
    const DWORD a = GetFileAttributesW(pszPath);
    return (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0) ? TRUE : FALSE;
}

int SHCreateDirectoryExW(HWND, LPCWSTR pszPath, const SECURITY_ATTRIBUTES*) {
    if (!pszPath || !*pszPath) return ERROR_INVALID_PARAMETER;
    std::string native = Explorer2::Compat::NativePath(pszPath);
    while (native.size() > 1 && native.back() == '/') native.pop_back();
    struct stat st = {};
    if (::stat(native.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) return ERROR_ALREADY_EXISTS;

    for (size_t i = 1; i <= native.size(); ++i) {
        if (i == native.size() || native[i] == '/') {
            std::string sub = native.substr(0, i);
            if (sub.empty()) continue;
            if (::mkdir(sub.c_str(), 0777) != 0 && errno != EEXIST) {
                return static_cast<int>(Explorer2::Compat::Win32ErrorFromErrno(errno));
            }
        }
    }
    return ERROR_SUCCESS;
}

namespace {
std::string LookupXdgUserDir(const std::string& baseHome, const char* xdgKey, const char* fallbackSubdir) {
    const char* xdgCfg = std::getenv("XDG_CONFIG_HOME");
    std::string dirsFile = (xdgCfg && *xdgCfg) ? (std::string(xdgCfg) + "/user-dirs.dirs") : (baseHome + "/.config/user-dirs.dirs");
    std::ifstream f(dirsFile);
    std::string line;
    const std::string prefix = std::string(xdgKey) + "=";
    while (std::getline(f, line)) {
        size_t p = line.find_first_not_of(" \t");
        if (p == std::string::npos || line[p] == '#') continue;
        if (line.compare(p, prefix.size(), prefix) != 0) continue;
        std::string val = line.substr(p + prefix.size());
        while (!val.empty() && (val.back() == '\r' || val.back() == '\n' || val.back() == ' ' || val.back() == '\t')) val.pop_back();
        if (val.size() >= 2 && val.front() == '"' && val.back() == '"') val = val.substr(1, val.size() - 2);
        if (val.rfind("$HOME/", 0) == 0) val = baseHome + val.substr(5);
        else if (val == "$HOME") val = baseHome;
        if (!val.empty() && val[0] == '/') return val;
    }
    return baseHome + "/" + fallbackSubdir;
}
} // namespace

HRESULT SHGetKnownFolderPath(REFKNOWNFOLDERID rfid, DWORD, HANDLE, PWSTR* ppszPath) {
    if (!ppszPath) return E_POINTER;
    *ppszPath = nullptr;
    const char* home = std::getenv("HOME");
    std::string baseHome = (home && *home) ? home : "/tmp";
    std::string result;
    if (rfid == FOLDERID_LocalAppData) {
        const char* xdg = std::getenv("XDG_DATA_HOME");
        result = (xdg && *xdg) ? xdg : (baseHome + "/.local/share");
    } else if (rfid == FOLDERID_RoamingAppData) {
        const char* xdg = std::getenv("XDG_CONFIG_HOME");
        result = (xdg && *xdg) ? xdg : (baseHome + "/.config");
    } else if (rfid == FOLDERID_Profile) {
        result = baseHome;
    } else if (rfid == FOLDERID_Desktop) {
        result = LookupXdgUserDir(baseHome, "XDG_DESKTOP_DIR", "Desktop");
    } else if (rfid == FOLDERID_Downloads) {
        result = LookupXdgUserDir(baseHome, "XDG_DOWNLOAD_DIR", "Downloads");
    } else if (rfid == FOLDERID_Documents) {
        result = LookupXdgUserDir(baseHome, "XDG_DOCUMENTS_DIR", "Documents");
    } else if (rfid == FOLDERID_Pictures) {
        result = LookupXdgUserDir(baseHome, "XDG_PICTURES_DIR", "Pictures");
    } else if (rfid == FOLDERID_Music) {
        result = LookupXdgUserDir(baseHome, "XDG_MUSIC_DIR", "Music");
    } else if (rfid == FOLDERID_Videos) {
        result = LookupXdgUserDir(baseHome, "XDG_VIDEOS_DIR", "Videos");
    } else if (rfid == FOLDERID_SendTo) {
        result = baseHome + "/.local/share/Explorer2/SendTo";
    } else {
        return E_FAIL;
    }
    std::wstring w = Explorer2::PathUtils::Utf8ToWide(result);
    auto* mem = static_cast<PWSTR>(CoTaskMemAlloc((w.size() + 1) * sizeof(wchar_t)));
    if (!mem) return E_OUTOFMEMORY;
    std::wmemcpy(mem, w.c_str(), w.size() + 1);
    *ppszPath = mem;
    return S_OK;
}

// ---- Memory IStream (CreateStreamOnHGlobal / GetHGlobalFromStream / SHCreateMemStream) -----------------------
namespace {
// Opens a regular file as a stdio stream. A FIFO, a socket or a device is refused without waiting: an image
// preview of a FIFO named "x.png" must not hang the program.
FILE* OpenRegularFileStream(const std::string& native, bool forWrite) {
    struct stat st = {};
    if (::stat(native.c_str(), &st) == 0 && !S_ISREG(st.st_mode)) return nullptr;
    const int accessFlags = forWrite ? (O_RDWR | O_CREAT | O_TRUNC) : O_RDONLY;
    const int fd = ::open(native.c_str(), accessFlags | O_CLOEXEC | O_NOCTTY | O_NONBLOCK, 0666);
    if (fd < 0) return nullptr;
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) { ::close(fd); return nullptr; }
    const int fl = ::fcntl(fd, F_GETFL);
    if (fl >= 0) (void)::fcntl(fd, F_SETFL, fl & ~O_NONBLOCK);
    FILE* f = ::fdopen(fd, forWrite ? "w+b" : "rb");
    if (!f) ::close(fd);
    return f;
}

class MemStreamImpl final : public IWICStream {
public:
    MemStreamImpl() = default;
    explicit MemStreamImpl(HGLOBAL hGlobal, bool deleteOnRelease)
        : m_hGlobal(hGlobal), m_deleteHGlobal(deleteOnRelease)
    {
        if (hGlobal) {
            const size_t sz = GlobalSize(hGlobal);
            const auto* p = static_cast<const uint8_t*>(GlobalLock(hGlobal));
            if (p && sz > 0) m_buf.assign(p, p + sz);
        }
    }
    ~MemStreamImpl() override {
        if (m_file) std::fclose(m_file);
        if (m_hGlobal && m_deleteHGlobal) GlobalFree(m_hGlobal);
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(ISequentialStream) ||
            riid == __uuidof(IStream) || riid == __uuidof(IWICStream))
        {
            *ppv = static_cast<IWICStream*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG r = --m_ref;
        if (r == 0) delete this;
        return r;
    }

    HRESULT STDMETHODCALLTYPE Read(void* pv, ULONG cb, ULONG* pcbRead) override {
        if (pcbRead) *pcbRead = 0;
        if (!pv && cb > 0) return E_POINTER;
        if (m_file) {
            const size_t got = std::fread(pv, 1, cb, m_file);
            if (pcbRead) *pcbRead = static_cast<ULONG>(got);
            return (got == cb) ? S_OK : S_FALSE;
        }
        const size_t avail = (m_pos < m_buf.size()) ? (m_buf.size() - m_pos) : 0;
        const size_t take = std::min<size_t>(avail, cb);
        if (take > 0) {
            std::memcpy(pv, m_buf.data() + m_pos, take);
            m_pos += take;
        }
        if (pcbRead) *pcbRead = static_cast<ULONG>(take);
        return (take == cb) ? S_OK : S_FALSE;
    }

    HRESULT STDMETHODCALLTYPE Write(const void* pv, ULONG cb, ULONG* pcbWritten) override {
        if (pcbWritten) *pcbWritten = 0;
        if (!pv && cb > 0) return E_POINTER;
        if (m_file) {
            const size_t put = std::fwrite(pv, 1, cb, m_file);
            if (pcbWritten) *pcbWritten = static_cast<ULONG>(put);
            return (put == cb) ? S_OK : E_FAIL;
        }
        if (m_pos + cb > m_buf.size()) m_buf.resize(m_pos + cb);
        if (cb > 0) {
            std::memcpy(m_buf.data() + m_pos, pv, cb);
            m_pos += cb;
        }
        SyncToHGlobal();
        if (pcbWritten) *pcbWritten = cb;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER dlibMove, DWORD dwOrigin, ULARGE_INTEGER* plibNewPosition) override {
        if (m_file) {
            const int wh = (dwOrigin == STREAM_SEEK_CUR) ? SEEK_CUR : (dwOrigin == STREAM_SEEK_END) ? SEEK_END : SEEK_SET;
            if (::fseeko(m_file, static_cast<off_t>(dlibMove.QuadPart), wh) != 0) return E_FAIL;
            if (plibNewPosition) plibNewPosition->QuadPart = static_cast<ULONGLONG>(::ftello(m_file));
            return S_OK;
        }
        int64_t base = (dwOrigin == STREAM_SEEK_CUR) ? static_cast<int64_t>(m_pos)
                     : (dwOrigin == STREAM_SEEK_END) ? static_cast<int64_t>(m_buf.size()) : 0;
        int64_t next = base + dlibMove.QuadPart;
        if (next < 0) return E_INVALIDARG;
        m_pos = static_cast<size_t>(next);
        if (plibNewPosition) plibNewPosition->QuadPart = m_pos;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER libNewSize) override {
        if (m_file) return ::ftruncate(fileno(m_file), static_cast<off_t>(libNewSize.QuadPart)) == 0 ? S_OK : E_FAIL;
        m_buf.resize(static_cast<size_t>(libNewSize.QuadPart));
        SyncToHGlobal();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CopyTo(IStream*, ULARGE_INTEGER, ULARGE_INTEGER*, ULARGE_INTEGER*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Commit(DWORD) override {
        if (m_file) std::fflush(m_file);
        SyncToHGlobal();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Revert() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return E_NOTIMPL; }

    HRESULT STDMETHODCALLTYPE Stat(STATSTG* pstatstg, DWORD) override {
        if (!pstatstg) return E_POINTER;
        std::memset(pstatstg, 0, sizeof(*pstatstg));
        if (m_file) {
            struct stat st = {};
            if (::fstat(fileno(m_file), &st) == 0) pstatstg->cbSize.QuadPart = static_cast<ULONGLONG>(st.st_size);
        } else {
            pstatstg->cbSize.QuadPart = m_buf.size();
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Clone(IStream**) override { return E_NOTIMPL; }

    HRESULT STDMETHODCALLTYPE InitializeFromIStream(IStream* pIStream) override {
        if (!pIStream) return E_INVALIDARG;
        m_buf.clear();
        m_pos = 0;
        LARGE_INTEGER z{};
        pIStream->Seek(z, STREAM_SEEK_SET, nullptr);
        uint8_t tmp[16384];
        while (true) {
            ULONG got = 0;
            HRESULT hr = pIStream->Read(tmp, sizeof(tmp), &got);
            if (got > 0) m_buf.insert(m_buf.end(), tmp, tmp + got);
            if (FAILED(hr) || got < sizeof(tmp)) break;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE InitializeFromFilename(LPCWSTR wzFilename, DWORD dwDesiredAccess) override {
        if (!wzFilename) return E_INVALIDARG;
        if (m_file) { std::fclose(m_file); m_file = nullptr; }
        const std::string native = Explorer2::Compat::NativePath(wzFilename);
        const bool wantWrite = (dwDesiredAccess & GENERIC_WRITE) != 0;
        m_file = OpenRegularFileStream(native, wantWrite);
        return m_file ? S_OK : E_FAIL;
    }

    HRESULT STDMETHODCALLTYPE InitializeFromMemory(BYTE* pbBuffer, DWORD cbBufferSize) override {
        if (!pbBuffer && cbBufferSize > 0) return E_INVALIDARG;
        if (m_file) { std::fclose(m_file); m_file = nullptr; }
        m_buf.assign(pbBuffer, pbBuffer + cbBufferSize);
        m_pos = 0;
        return S_OK;
    }

    HGLOBAL EnsureHGlobal() {
        SyncToHGlobal(true);
        return m_hGlobal;
    }

private:
    void SyncToHGlobal(bool createIfNull = false) {
        if (!m_hGlobal && !createIfNull) return;
        if (m_hGlobal) GlobalFree(m_hGlobal);
        m_hGlobal = GlobalAlloc(GMEM_MOVEABLE, m_buf.size());
        if (m_hGlobal && !m_buf.empty()) {
            std::memcpy(GlobalLock(m_hGlobal), m_buf.data(), m_buf.size());
        }
    }

    std::atomic<ULONG> m_ref{ 1 };
    std::vector<uint8_t> m_buf;
    size_t m_pos = 0;
    FILE* m_file = nullptr;
    HGLOBAL m_hGlobal = nullptr;
    bool m_deleteHGlobal = true;
};
} // namespace

HRESULT CreateStreamOnHGlobal(HGLOBAL hGlobal, BOOL fDeleteOnRelease, LPSTREAM* ppstm) {
    if (!ppstm) return E_POINTER;
    *ppstm = new MemStreamImpl(hGlobal, fDeleteOnRelease != FALSE);
    return S_OK;
}

HRESULT GetHGlobalFromStream(LPSTREAM pstm, HGLOBAL* phglobal) {
    if (!pstm || !phglobal) return E_POINTER;
    auto* ms = dynamic_cast<MemStreamImpl*>(pstm);
    if (!ms) return E_INVALIDARG;
    *phglobal = ms->EnsureHGlobal();
    return *phglobal ? S_OK : E_OUTOFMEMORY;
}

IStream* SHCreateMemStream(const BYTE* pInit, UINT cbInit) {
    auto* ms = new MemStreamImpl();
    if (pInit && cbInit > 0) ms->InitializeFromMemory(const_cast<BYTE*>(pInit), cbInit);
    return ms;
}

void ReleaseStgMedium(LPSTGMEDIUM pmedium) {
    if (!pmedium) return;
    if (pmedium->pUnkForRelease) {
        pmedium->pUnkForRelease->Release();
    } else if (pmedium->tymed == TYMED_HGLOBAL && pmedium->hGlobal) {
        GlobalFree(pmedium->hGlobal);
    } else if (pmedium->tymed == TYMED_ISTREAM && pmedium->pstm) {
        pmedium->pstm->Release();
    }
    std::memset(pmedium, 0, sizeof(*pmedium));
}

// ==============================================================================================================
// 6. COM, Shell, Direct2D, DirectWrite, WIC, Media & Windowing Initial Shims (weak/extensible for later iters)
// ==============================================================================================================
HRESULT CoInitializeEx(LPVOID, DWORD) { return S_OK; }
HRESULT CoInitialize(LPVOID) { return S_OK; }
void CoUninitialize() {}
HRESULT OleInitialize(LPVOID) { return S_OK; }
void OleUninitialize() {}
HRESULT DoDragDrop(IDataObject*, IDropSource*, DWORD, LPDWORD pdwEffect) {
    if (pdwEffect) *pdwEffect = DROPEFFECT_NONE;
    return DRAGDROP_S_CANCEL;
}
HRESULT CoWaitForMultipleHandles(DWORD, DWORD dwTimeout, ULONG cHandles, LPHANDLE pHandles, LPDWORD lpdwIndex) {
    if (cHandles == 0 || !pHandles) return E_INVALIDARG;
    DWORD r = WaitForMultipleObjects(cHandles, pHandles, FALSE, dwTimeout);
    if (r >= WAIT_OBJECT_0 && r < WAIT_OBJECT_0 + cHandles) {
        if (lpdwIndex) *lpdwIndex = r - WAIT_OBJECT_0;
        return S_OK;
    }
    return E_FAIL;
}
void CoFreeUnusedLibrariesEx(DWORD, DWORD) {}

namespace Explorer2::Hal {
__attribute__((weak)) HRESULT CreateWicImagingFactory(REFIID riid, void** ppv) {
    (void)riid;
    if (ppv) *ppv = nullptr;
    return E_NOINTERFACE;
}
} // namespace Explorer2::Hal

namespace {

std::string GetFreeDesktopTrashBase() {
    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (xdg && *xdg) return std::string(xdg) + "/Trash";
    const char* home = std::getenv("HOME");
    if (home && *home) return std::string(home) + "/.local/share/Trash";
    return "/tmp/Trash-" + std::to_string(::geteuid());
}

std::string UrlEncodeTrashPath(const std::string& raw) {
    std::string out;
    out.reserve(raw.size() + 16);
    static const char kHex[] = "0123456789ABCDEF";
    for (unsigned char c : raw) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '/' || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(kHex[(c >> 4) & 0xF]);
            out.push_back(kHex[c & 0xF]);
        }
    }
    return out;
}

// Creates a folder and any missing folders above it with mode 0700, the mode the trash specification asks for
// (the names in the trash say what the user deleted). True when the folder exists afterwards.
bool MakePrivateDirs(const std::string& path) {
    for (size_t i = 1; i <= path.size(); ++i) {
        if (i < path.size() && path[i] != '/') continue;
        const std::string sub = path.substr(0, i);
        if (sub.back() == '/') continue;
        if (::mkdir(sub.c_str(), 0700) != 0 && errno != EEXIST) return false;
    }
    struct stat st = {};
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// Moves one item into the freedesktop.org trash: its .trashinfo first (created exclusively, so that the name is
// ours), then the item itself by rename(), never by a copy. Returns S_OK or the failure; on a failure the item
// stays where it was and no .trashinfo is left behind.
HRESULT MoveItemToTrash(const std::string& srcNative, const std::string& trashFiles, const std::string& trashInfo) {
    // Path= names the item itself: its folder resolved, its own name as it is, so that a symbolic link is
    // restored as that link and not as the file it points to
    std::string clean = srcNative;
    while (clean.size() > 1 && clean.back() == '/') clean.pop_back();
    const size_t sl = clean.find_last_of('/');
    const std::string parentDir = (sl == std::string::npos || sl == 0) ? std::string("/") : clean.substr(0, sl);
    const std::string leafName = (sl == std::string::npos) ? clean : clean.substr(sl + 1);
    char realBuf[PATH_MAX] = {};
    const std::string parentAbs = ::realpath(parentDir.c_str(), realBuf) ? std::string(realBuf) : parentDir;
    const std::string origAbs = (!parentAbs.empty() && parentAbs.back() == '/') ? parentAbs + leafName : parentAbs + "/" + leafName;

    const std::string leaf = leafName.empty() ? std::string("trashed_item") : leafName;
    std::string stem = leaf, ext;
    const size_t dot = leaf.find_last_of('.');
    if (dot != std::string::npos && dot > 0) {
        stem = leaf.substr(0, dot);
        ext = leaf.substr(dot);
    }
    std::string chosenName;
    int infoFd = -1;
    int lastErr = EEXIST;
    for (int attempt = 1; attempt < 10000; ++attempt) {
        const std::string cand = (attempt == 1) ? leaf : (stem + "." + std::to_string(attempt) + ext);
        struct stat stExist = {};
        if (::lstat((trashFiles + "/" + cand).c_str(), &stExist) == 0) continue;
        infoFd = ::open((trashInfo + "/" + cand + ".trashinfo").c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (infoFd >= 0) {
            chosenName = cand;
            break;
        }
        lastErr = errno;
        if (lastErr != EEXIST) break;           // the info folder itself is the problem: another name does not help
    }
    if (infoFd < 0) {
        const DWORD e = (lastErr == EEXIST) ? static_cast<DWORD>(ERROR_FILE_EXISTS) : Explorer2::Compat::Win32ErrorFromErrno(lastErr);
        return HRESULT_FROM_WIN32(e);
    }
    const std::string infoPath = trashInfo + "/" + chosenName + ".trashinfo";

    time_t now = ::time(nullptr);
    struct tm lt = {};
    localtime_r(&now, &lt);
    char dtBuf[64] = {};
    std::snprintf(dtBuf, sizeof(dtBuf), "%04d-%02d-%02dT%02d:%02d:%02d",
                  lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec);
    const std::string infoBody = "[Trash Info]\nPath=" + UrlEncodeTrashPath(origAbs) + "\nDeletionDate=" + dtBuf + "\n";
    // An item in the trash without its complete .trashinfo could not be restored, so it is only moved once the
    // .trashinfo is written (a full disk shows up here)
    int infoErr = 0;
    size_t off = 0;
    while (off < infoBody.size()) {
        const ssize_t wr = ::write(infoFd, infoBody.data() + off, infoBody.size() - off);
        if (wr < 0) {
            if (errno == EINTR) continue;
            infoErr = errno;
            break;
        }
        off += static_cast<size_t>(wr);
    }
    if (::close(infoFd) != 0 && infoErr == 0) infoErr = errno;
    if (infoErr != 0) {
        ::unlink(infoPath.c_str());
        const DWORD e = Explorer2::Compat::Win32ErrorFromErrno(infoErr);
        return HRESULT_FROM_WIN32(e ? e : static_cast<DWORD>(ERROR_WRITE_FAULT));
    }

    // rename() only. Should the item be on another file system after all (a bind mount of the same disk, which
    // rename refuses with EXDEV), it stays where it is instead of being copied into the home trash.
    DWORD mvErr = 0;
    struct stat stNow = {};
    if (::lstat(clean.c_str(), &stNow) != 0) {
        mvErr = Explorer2::Compat::Win32ErrorFromErrno(errno);
    } else if (!S_ISLNK(stNow.st_mode) &&
               Explorer2::Compat::IsDeleteBlockedByOpenHandle(static_cast<uint64_t>(stNow.st_dev), static_cast<uint64_t>(stNow.st_ino))) {
        mvErr = ERROR_SHARING_VIOLATION;
    } else if (::rename(clean.c_str(), (trashFiles + "/" + chosenName).c_str()) != 0) {
        mvErr = Explorer2::Compat::Win32ErrorFromErrno(errno);
    }
    if (mvErr != 0) {
        ::unlink(infoPath.c_str());
        return HRESULT_FROM_WIN32(mvErr);
    }
    return S_OK;
}

class LinuxShellItem final : public IShellItem {
public:
    explicit LinuxShellItem(std::wstring p) : m_path(std::move(p)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IShellItem)) {
            *ppv = static_cast<IShellItem*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = --m_ref; if (!r) delete this; return r; }

    HRESULT STDMETHODCALLTYPE BindToHandler(void*, REFGUID, REFIID, void** ppv) override { if (ppv) *ppv = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetParent(IShellItem** ppsi) override {
        if (!ppsi) return E_POINTER;
        std::wstring par = Explorer2::PathUtils::GetParentPath(m_path);
        if (par.empty()) { *ppsi = nullptr; return E_FAIL; }
        *ppsi = new LinuxShellItem(std::move(par));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDisplayName(SIGDN sigdnName, LPWSTR* ppszName) override {
        if (!ppszName) return E_POINTER;
        std::wstring s = (sigdnName == SIGDN_NORMALDISPLAY) ? Explorer2::PathUtils::GetFileName(m_path) : m_path;
        auto* mem = static_cast<LPWSTR>(CoTaskMemAlloc((s.size() + 1) * sizeof(wchar_t)));
        if (!mem) return E_OUTOFMEMORY;
        std::wmemcpy(mem, s.c_str(), s.size() + 1);
        *ppszName = mem;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetAttributes(ULONG, ULONG* psfgaoAttribs) override { if (psfgaoAttribs) *psfgaoAttribs = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE Compare(IShellItem* psi, DWORD, int* piOrder) override {
        if (!psi || !piOrder) return E_POINTER;
        LPWSTR other = nullptr;
        if (FAILED(psi->GetDisplayName(SIGDN_FILESYSPATH, &other)) || !other) return E_FAIL;
        *piOrder = std::wcscmp(m_path.c_str(), other);
        CoTaskMemFree(other);
        return (*piOrder == 0) ? S_OK : S_FALSE;
    }
private:
    std::atomic<ULONG> m_ref{ 1 };
    std::wstring m_path;
};

class LinuxTaskbarList3 final : public ITaskbarList3 {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(ITaskbarList3)) { *ppv = static_cast<ITaskbarList3*>(this); AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = --m_ref; if (!r) delete this; return r; }
    HRESULT STDMETHODCALLTYPE HrInit() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetProgressValue(HWND, ULONGLONG, ULONGLONG) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetProgressState(HWND, TBPFLAG) override { return S_OK; }
private:
    std::atomic<ULONG> m_ref{ 1 };
};

class LinuxFileOperation final : public IFileOperation {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IFileOperation)) { *ppv = static_cast<IFileOperation*>(this); AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --m_ref;
        if (!r) {
            for (auto& q : m_items) {
                if (q.first) q.first->Release();
                if (q.second) q.second->Release();
            }
            delete this;
        }
        return r;
    }
    HRESULT STDMETHODCALLTYPE Advise(IFileOperationProgressSink*, DWORD* pdwCookie) override { if (pdwCookie) *pdwCookie = 1; return S_OK; }
    HRESULT STDMETHODCALLTYPE Unadvise(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetOperationFlags(DWORD dwOperationFlags) override { m_flags = dwOperationFlags; return S_OK; }
    HRESULT STDMETHODCALLTYPE DeleteItem(IShellItem* psiItem, IFileOperationProgressSink* pfopsItem) override {
        if (!psiItem) return E_INVALIDARG;
        psiItem->AddRef();
        if (pfopsItem) pfopsItem->AddRef();
        m_items.emplace_back(psiItem, pfopsItem);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE PerformOperations() override {
        const bool recycle = (m_flags & (FOF_ALLOWUNDO | FOFX_RECYCLEONDELETE)) != 0;
        const std::string trashBase = GetFreeDesktopTrashBase();
        const std::string trashFiles = trashBase + "/files";
        const std::string trashInfo  = trashBase + "/info";
        // The file system of the trash: items reach it by rename() only, so only from that file system
        struct stat stTrash = {};
        bool haveTrash = false;
        if (recycle) {
            haveTrash = MakePrivateDirs(trashFiles) && MakePrivateDirs(trashInfo) && ::stat(trashFiles.c_str(), &stTrash) == 0;
        }

        for (auto& [item, sink] : m_items) {
            LPWSTR rawW = nullptr;
            if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &rawW)) || !rawW) return E_FAIL;
            std::wstring srcW(rawW);
            CoTaskMemFree(rawW);

            std::string srcNative = Explorer2::Compat::NativePath(srcW.c_str());
            const bool wantRecycle = recycle && !srcNative.empty() && srcNative[0] == '/' &&
                                     srcNative.rfind("/proc/", 0) != 0 && srcNative.rfind("/sys/", 0) != 0 && srcNative.rfind("/dev/", 0) != 0;
            // The home trash takes only items of its own file system. One on a USB stick, another partition or a
            // network share would have to be copied into it (filling the home disk, and slow): for such an item
            // there is no recycle bin, as for a Windows drive without one. The sink hears TSF_NORMAL (the shell
            // would delete the item for good), which Explorer2 refuses and then offers a permanent delete.
            struct stat stSrc = {};
            const bool srcKnown = wantRecycle && ::lstat(srcNative.c_str(), &stSrc) == 0;
            const bool noBinForItem = srcKnown && (!haveTrash || stSrc.st_dev != stTrash.st_dev);
            const bool canRecycle = wantRecycle && !noBinForItem;

            const DWORD tsf = canRecycle ? TSF_DELETE_RECYCLE_IF_POSSIBLE : TSF_NORMAL;
            if (sink) {
                HRESULT preHr = sink->PreDeleteItem(tsf, item);
                if (FAILED(preHr)) {
                    m_aborted = TRUE;
                    return preHr;
                }
            }

            HRESULT opHr = S_OK;
            if (canRecycle) {
                opHr = MoveItemToTrash(srcNative, trashFiles, trashInfo);
            } else if (wantRecycle) {
                // The trash was asked for and cannot take this item: nothing is deleted
                opHr = HRESULT_FROM_WIN32(ERROR_NOT_SAME_DEVICE);
            } else {
                if (!DeleteFileW(srcW.c_str()) && !RemoveDirectoryW(srcW.c_str())) {
                    DWORD delErr = GetLastError();
                    opHr = HRESULT_FROM_WIN32(delErr ? delErr : ERROR_ACCESS_DENIED);
                }
            }

            if (sink) sink->PostDeleteItem(tsf, item, opHr, nullptr);
            if (FAILED(opHr)) {
                m_aborted = TRUE;
                return opHr;
            }
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetAnyOperationsAborted(BOOL* pfAnyOperationsAborted) override {
        if (pfAnyOperationsAborted) *pfAnyOperationsAborted = m_aborted;
        return S_OK;
    }
private:
    std::atomic<ULONG> m_ref{ 1 };
    DWORD m_flags = 0;
    BOOL m_aborted = FALSE;
    std::vector<std::pair<IShellItem*, IFileOperationProgressSink*>> m_items;
};

} // namespace

HRESULT CoCreateInstance(REFCLSID rclsid, LPUNKNOWN, DWORD, REFIID riid, LPVOID* ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (rclsid == CLSID_WICImagingFactory) {
        return Explorer2::Hal::CreateWicImagingFactory(riid, ppv);
    }
    if (rclsid == CLSID_FileOperation) {
        auto* op = new LinuxFileOperation();
        HRESULT hr = op->QueryInterface(riid, ppv);
        op->Release();
        return hr;
    }
    if (rclsid == CLSID_TaskbarList) {
        auto* tb = new LinuxTaskbarList3();
        HRESULT hr = tb->QueryInterface(riid, ppv);
        tb->Release();
        return hr;
    }
    return E_NOINTERFACE;
}

HRESULT CreateWicStreamInstance(IWICStream** ppStream) {
    if (!ppStream) return E_POINTER;
    *ppStream = new MemStreamImpl();
    return S_OK;
}

// ---- Minimal Initial DirectWrite + Direct2D Shims (Upgraded to Full Rasterizer & Cairo/Pango in Iter 7-8) -----
namespace {

class StubInlineObject final : public IDWriteInlineObject {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteInlineObject)) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = --m_ref; if (!r) delete this; return r; }
private:
    std::atomic<ULONG> m_ref{ 1 };
};

class StubTextFormat : public IDWriteTextLayout {
public:
    StubTextFormat(const WCHAR* family, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STYLE style, FLOAT size)
        : m_family(family ? family : L"Sans"), m_weight(weight), m_style(style), m_size(size) {}

    StubTextFormat(const WCHAR* text, UINT32 len, IDWriteTextFormat* baseFmt, FLOAT maxW, FLOAT maxH)
        : m_text(text ? std::wstring(text, len) : std::wstring()), m_maxW(maxW), m_maxH(maxH)
    {
        if (baseFmt) {
            m_family = baseFmt->GetFontFamilyNamePtr() ? baseFmt->GetFontFamilyNamePtr() : L"Sans";
            m_weight = baseFmt->GetFontWeight();
            m_style = baseFmt->GetFontStyle();
            m_size = baseFmt->GetFontSize();
            m_textAlign = baseFmt->GetTextAlignment();
            m_paraAlign = baseFmt->GetParagraphAlignment();
            m_wrap = baseFmt->GetWordWrapping();
        }
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteTextFormat) || riid == __uuidof(IDWriteTextLayout)) {
            *ppv = static_cast<IDWriteTextLayout*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = --m_ref; if (!r) delete this; return r; }

    HRESULT STDMETHODCALLTYPE SetTextAlignment(DWRITE_TEXT_ALIGNMENT a) override { m_textAlign = a; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT a) override { m_paraAlign = a; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetWordWrapping(DWRITE_WORD_WRAPPING w) override { m_wrap = w; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetTrimming(const DWRITE_TRIMMING* t, IDWriteInlineObject*) override {
        if (t) m_trim = *t;
        return S_OK;
    }
    DWRITE_TEXT_ALIGNMENT STDMETHODCALLTYPE GetTextAlignment() override { return m_textAlign; }
    DWRITE_PARAGRAPH_ALIGNMENT STDMETHODCALLTYPE GetParagraphAlignment() override { return m_paraAlign; }
    DWRITE_WORD_WRAPPING STDMETHODCALLTYPE GetWordWrapping() override { return m_wrap; }
    FLOAT STDMETHODCALLTYPE GetFontSize() override { return m_size; }
    const WCHAR* STDMETHODCALLTYPE GetFontFamilyNamePtr() override { return m_family.c_str(); }
    DWRITE_FONT_WEIGHT STDMETHODCALLTYPE GetFontWeight() override { return m_weight; }
    DWRITE_FONT_STYLE STDMETHODCALLTYPE GetFontStyle() override { return m_style; }

    HRESULT STDMETHODCALLTYPE GetMetrics(DWRITE_TEXT_METRICS* m) override {
        if (!m) return E_POINTER;
        std::memset(m, 0, sizeof(*m));
        m->width = std::min(m_maxW, static_cast<FLOAT>(m_text.size()) * m_size * 0.55f);
        m->widthIncludingTrailingWhitespace = m->width;
        m->height = m_size * 1.35f;
        m->layoutWidth = m_maxW;
        m->layoutHeight = m_maxH;
        m->lineCount = 1;
        return S_OK;
    }
    const WCHAR* STDMETHODCALLTYPE GetTextPtr() override { return m_text.c_str(); }
    UINT32 STDMETHODCALLTYPE GetTextLength() override { return static_cast<UINT32>(m_text.size()); }
    FLOAT STDMETHODCALLTYPE GetMaxWidth() override { return m_maxW; }
    FLOAT STDMETHODCALLTYPE GetMaxHeight() override { return m_maxH; }

private:
    std::atomic<ULONG> m_ref{ 1 };
    std::wstring m_family = L"Sans";
    std::wstring m_text;
    DWRITE_FONT_WEIGHT m_weight = DWRITE_FONT_WEIGHT_NORMAL;
    DWRITE_FONT_STYLE m_style = DWRITE_FONT_STYLE_NORMAL;
    FLOAT m_size = 12.0f;
    FLOAT m_maxW = 0.0f;
    FLOAT m_maxH = 0.0f;
    DWRITE_TEXT_ALIGNMENT m_textAlign = DWRITE_TEXT_ALIGNMENT_LEADING;
    DWRITE_PARAGRAPH_ALIGNMENT m_paraAlign = DWRITE_PARAGRAPH_ALIGNMENT_NEAR;
    DWRITE_WORD_WRAPPING m_wrap = DWRITE_WORD_WRAPPING_WRAP;
    DWRITE_TRIMMING m_trim = {};
};

class StubDWriteFactory final : public IDWriteFactory {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteFactory)) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = --m_ref; if (!r) delete this; return r; }

    HRESULT STDMETHODCALLTYPE CreateTextFormat(const WCHAR* family, IDWriteFontCollection*, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STYLE style, DWRITE_FONT_STRETCH, FLOAT size, const WCHAR*, IDWriteTextFormat** out) override {
        if (!out) return E_POINTER;
        *out = new StubTextFormat(family, weight, style, size);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateEllipsisTrimmingSign(IDWriteTextFormat*, IDWriteInlineObject** out) override {
        if (!out) return E_POINTER;
        *out = new StubInlineObject();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateTextLayout(const WCHAR* str, UINT32 len, IDWriteTextFormat* fmt, FLOAT maxW, FLOAT maxH, IDWriteTextLayout** out) override {
        if (!out) return E_POINTER;
        *out = new StubTextFormat(str, len, fmt, maxW, maxH);
        return S_OK;
    }
private:
    std::atomic<ULONG> m_ref{ 1 };
};

class StubD2DBitmap final : public ID2D1Bitmap {
public:
    StubD2DBitmap(D2D1_SIZE_U sz, const void* src, UINT32 pitch, FLOAT dpiX, FLOAT dpiY)
        : m_sz(sz), m_dpiX(dpiX > 0 ? dpiX : 96.f), m_dpiY(dpiY > 0 ? dpiY : 96.f)
    {
        m_pixels.resize(static_cast<size_t>(sz.width) * sz.height * 4, 0);
        if (src && pitch >= sz.width * 4) {
            for (UINT32 y = 0; y < sz.height; ++y) {
                std::memcpy(m_pixels.data() + static_cast<size_t>(y) * sz.width * 4,
                            static_cast<const uint8_t*>(src) + static_cast<size_t>(y) * pitch,
                            static_cast<size_t>(sz.width) * 4);
            }
        }
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(ID2D1Bitmap)) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = --m_ref; if (!r) delete this; return r; }

    D2D1_SIZE_F STDMETHODCALLTYPE GetSize() const override {
        return { m_sz.width * 96.0f / m_dpiX, m_sz.height * 96.0f / m_dpiY };
    }
    D2D1_SIZE_U STDMETHODCALLTYPE GetPixelSize() const override { return m_sz; }
    HRESULT STDMETHODCALLTYPE CopyFromMemory(const D2D1_RECT_U* dstRect, const void* srcData, UINT32 pitch) override {
        if (!srcData) return E_INVALIDARG;
        D2D1_RECT_U r = dstRect ? *dstRect : D2D1::RectU(0, 0, m_sz.width, m_sz.height);
        if (r.right > m_sz.width || r.bottom > m_sz.height || r.left >= r.right || r.top >= r.bottom) return E_INVALIDARG;
        const UINT32 copyW = r.right - r.left;
        const UINT32 copyH = r.bottom - r.top;
        for (UINT32 y = 0; y < copyH; ++y) {
            std::memcpy(m_pixels.data() + (static_cast<size_t>(r.top + y) * m_sz.width + r.left) * 4,
                        static_cast<const uint8_t*>(srcData) + static_cast<size_t>(y) * pitch,
                        static_cast<size_t>(copyW) * 4);
        }
        return S_OK;
    }
private:
    std::atomic<ULONG> m_ref{ 1 };
    D2D1_SIZE_U m_sz{};
    FLOAT m_dpiX = 96.f, m_dpiY = 96.f;
    std::vector<uint8_t> m_pixels;
};

class StubD2DBrush final : public ID2D1SolidColorBrush {
public:
    explicit StubD2DBrush(const D2D1_COLOR_F& c) : m_color(c) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(ID2D1SolidColorBrush)) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = --m_ref; if (!r) delete this; return r; }
    void STDMETHODCALLTYPE SetColor(const D2D1_COLOR_F& c) override { m_color = c; }
    D2D1_COLOR_F STDMETHODCALLTYPE GetColor() const override { return m_color; }
private:
    std::atomic<ULONG> m_ref{ 1 };
    D2D1_COLOR_F m_color{};
};

class StubGeometrySink final : public ID2D1GeometrySink {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(ID2D1GeometrySink)) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = --m_ref; if (!r) delete this; return r; }
    void STDMETHODCALLTYPE BeginFigure(D2D1_POINT_2F, D2D1_FIGURE_BEGIN) override {}
    void STDMETHODCALLTYPE AddLine(D2D1_POINT_2F) override {}
    void STDMETHODCALLTYPE AddArc(const D2D1_ARC_SEGMENT&) override {}
    void STDMETHODCALLTYPE EndFigure(D2D1_FIGURE_END) override {}
    HRESULT STDMETHODCALLTYPE Close() override { return S_OK; }
private:
    std::atomic<ULONG> m_ref{ 1 };
};

class StubPathGeometry final : public ID2D1PathGeometry {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(ID2D1PathGeometry)) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = --m_ref; if (!r) delete this; return r; }
    HRESULT STDMETHODCALLTYPE Open(ID2D1GeometrySink** sink) override {
        if (!sink) return E_POINTER;
        *sink = new StubGeometrySink();
        return S_OK;
    }
private:
    std::atomic<ULONG> m_ref{ 1 };
};

class StubRenderTarget final : public ID2D1HwndRenderTarget, public ID2D1DCRenderTarget {
public:
    StubRenderTarget(HWND hwnd, D2D1_SIZE_U sz) : m_hwnd(hwnd), m_sz(sz.width ? sz : D2D1::SizeU(1280, 800)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(ID2D1RenderTarget) || riid == __uuidof(ID2D1HwndRenderTarget)) {
            *ppv = static_cast<ID2D1HwndRenderTarget*>(this); AddRef(); return S_OK;
        }
        if (riid == __uuidof(ID2D1DCRenderTarget)) {
            *ppv = static_cast<ID2D1DCRenderTarget*>(this); AddRef(); return S_OK;
        }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = --m_ref; if (!r) delete this; return r; }

    HRESULT STDMETHODCALLTYPE CreateBitmap(D2D1_SIZE_U size, const void* srcData, UINT32 pitch, const D2D1_BITMAP_PROPERTIES* props, ID2D1Bitmap** bitmap) override {
        if (!bitmap || size.width == 0 || size.height == 0) return E_INVALIDARG;
        FLOAT dx = (props && props->dpiX > 0) ? props->dpiX : m_dpiX;
        FLOAT dy = (props && props->dpiY > 0) ? props->dpiY : m_dpiY;
        *bitmap = new StubD2DBitmap(size, srcData, pitch, dx, dy);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateSolidColorBrush(const D2D1_COLOR_F* color, const D2D1_BRUSH_PROPERTIES*, ID2D1SolidColorBrush** brush) override {
        if (!brush) return E_POINTER;
        *brush = new StubD2DBrush(color ? *color : D2D1::ColorF(D2D1::ColorF::Black));
        return S_OK;
    }
    void STDMETHODCALLTYPE DrawLine(D2D1_POINT_2F, D2D1_POINT_2F, ID2D1Brush*, FLOAT, ID2D1StrokeStyle*) override {}
    void STDMETHODCALLTYPE DrawRectangle(const D2D1_RECT_F*, ID2D1Brush*, FLOAT, ID2D1StrokeStyle*) override {}
    void STDMETHODCALLTYPE FillRectangle(const D2D1_RECT_F*, ID2D1Brush*) override {}
    void STDMETHODCALLTYPE DrawRoundedRectangle(const D2D1_ROUNDED_RECT*, ID2D1Brush*, FLOAT, ID2D1StrokeStyle*) override {}
    void STDMETHODCALLTYPE FillRoundedRectangle(const D2D1_ROUNDED_RECT*, ID2D1Brush*) override {}
    void STDMETHODCALLTYPE DrawEllipse(const D2D1_ELLIPSE*, ID2D1Brush*, FLOAT, ID2D1StrokeStyle*) override {}
    void STDMETHODCALLTYPE FillEllipse(const D2D1_ELLIPSE*, ID2D1Brush*) override {}
    void STDMETHODCALLTYPE DrawGeometry(ID2D1Geometry*, ID2D1Brush*, FLOAT, ID2D1StrokeStyle*) override {}
    void STDMETHODCALLTYPE FillGeometry(ID2D1Geometry*, ID2D1Brush*, ID2D1Brush*) override {}
    void STDMETHODCALLTYPE DrawBitmap(ID2D1Bitmap*, const D2D1_RECT_F*, FLOAT, D2D1_BITMAP_INTERPOLATION_MODE, const D2D1_RECT_F*) override {}
    void STDMETHODCALLTYPE DrawText(const WCHAR*, UINT32, IDWriteTextFormat*, const D2D1_RECT_F*, ID2D1Brush*, D2D1_DRAW_TEXT_OPTIONS, DWRITE_MEASURING_MODE) override {}
    void STDMETHODCALLTYPE DrawTextLayout(D2D1_POINT_2F, IDWriteTextLayout*, ID2D1Brush*, D2D1_DRAW_TEXT_OPTIONS) override {}
    void STDMETHODCALLTYPE SetTransform(const D2D1_MATRIX_3X2_F*) override {}
    void STDMETHODCALLTYPE Clear(const D2D1_COLOR_F*) override {}
    void STDMETHODCALLTYPE BeginDraw() override {}
    HRESULT STDMETHODCALLTYPE EndDraw(D2D1_TAG*, D2D1_TAG*) override { return S_OK; }
    void STDMETHODCALLTYPE PushAxisAlignedClip(const D2D1_RECT_F*, D2D1_ANTIALIAS_MODE) override {}
    void STDMETHODCALLTYPE PopAxisAlignedClip() override {}
    void STDMETHODCALLTYPE SetDpi(FLOAT dx, FLOAT dy) override { m_dpiX = dx > 0 ? dx : 96.f; m_dpiY = dy > 0 ? dy : 96.f; }
    void STDMETHODCALLTYPE GetDpi(FLOAT* dx, FLOAT* dy) const override { if (dx) *dx = m_dpiX; if (dy) *dy = m_dpiY; }
    D2D1_SIZE_F STDMETHODCALLTYPE GetSize() const override { return { m_sz.width * 96.f / m_dpiX, m_sz.height * 96.f / m_dpiY }; }
    D2D1_SIZE_U STDMETHODCALLTYPE GetPixelSize() const override { return m_sz; }
    HRESULT STDMETHODCALLTYPE Resize(const D2D1_SIZE_U* sz) override { if (sz) m_sz = *sz; return S_OK; }
    HWND STDMETHODCALLTYPE GetHwnd() const override { return m_hwnd; }
    HRESULT STDMETHODCALLTYPE BindDC(const HDC, const RECT* r) override {
        if (r && r->right > r->left && r->bottom > r->top) {
            m_sz = { static_cast<UINT32>(r->right - r->left), static_cast<UINT32>(r->bottom - r->top) };
        }
        return S_OK;
    }
private:
    std::atomic<ULONG> m_ref{ 1 };
    HWND m_hwnd = nullptr;
    D2D1_SIZE_U m_sz{ 1280, 800 };
    FLOAT m_dpiX = 96.f, m_dpiY = 96.f;
};

class StubD2DFactory final : public ID2D1Factory {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(ID2D1Factory)) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = --m_ref; if (!r) delete this; return r; }
    HRESULT STDMETHODCALLTYPE CreatePathGeometry(ID2D1PathGeometry** pg) override {
        if (!pg) return E_POINTER;
        *pg = new StubPathGeometry();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateHwndRenderTarget(const D2D1_RENDER_TARGET_PROPERTIES*, const D2D1_HWND_RENDER_TARGET_PROPERTIES* hp, ID2D1HwndRenderTarget** rt) override {
        if (!rt || !hp) return E_POINTER;
        *rt = new StubRenderTarget(hp->hwnd, hp->pixelSize);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateDCRenderTarget(const D2D1_RENDER_TARGET_PROPERTIES*, ID2D1DCRenderTarget** rt) override {
        if (!rt) return E_POINTER;
        *rt = new StubRenderTarget(nullptr, D2D1::SizeU(512, 512));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateWicBitmapRenderTarget(IWICBitmap* target, const D2D1_RENDER_TARGET_PROPERTIES*, ID2D1RenderTarget** rt) override {
        if (!rt || !target) return E_POINTER;
        UINT w = 0, h = 0;
        target->GetSize(&w, &h);
        *rt = static_cast<ID2D1HwndRenderTarget*>(new StubRenderTarget(nullptr, D2D1::SizeU(w ? w : 64, h ? h : 64)));
        return S_OK;
    }
private:
    std::atomic<ULONG> m_ref{ 1 };
};

} // namespace

__attribute__((weak)) HRESULT DWriteCreateFactory(DWRITE_FACTORY_TYPE, REFIID, IUnknown** factory) {
    if (!factory) return E_POINTER;
    *factory = new StubDWriteFactory();
    return S_OK;
}

__attribute__((weak)) HRESULT D2D1CreateFactory(D2D1_FACTORY_TYPE, REFIID, const void*, void** ppIFactory) {
    if (!ppIFactory) return E_POINTER;
    *ppIFactory = new StubD2DFactory();
    return S_OK;
}

// ---- Minimal Windowing, GDI, Menus, Clipboard, Shell & Media Stubs (weak for Iter 7-8 override) -------------
__attribute__((weak)) WORD RegisterClassExW(const WNDCLASSEXW*) { return 1; }
__attribute__((weak)) HWND CreateWindowExW(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID) {
    static intptr_t s_nextHwnd = 0x1000;
    return reinterpret_cast<HWND>(++s_nextHwnd);
}
__attribute__((weak)) BOOL DestroyWindow(HWND) { return TRUE; }
__attribute__((weak)) BOOL ShowWindow(HWND, int) { return TRUE; }
__attribute__((weak)) BOOL UpdateWindow(HWND) { return TRUE; }
__attribute__((weak)) BOOL IsWindow(HWND h) { return h != nullptr ? TRUE : FALSE; }
__attribute__((weak)) BOOL IsWindowVisible(HWND) { return TRUE; }
__attribute__((weak)) BOOL EnableWindow(HWND, BOOL) { return TRUE; }
__attribute__((weak)) BOOL SetForegroundWindow(HWND) { return TRUE; }
__attribute__((weak)) HWND SetFocus(HWND h) { return h; }
__attribute__((weak)) HWND GetFocus() { return nullptr; }
__attribute__((weak)) HWND GetParent(HWND) { return nullptr; }
__attribute__((weak)) HWND GetAncestor(HWND h, UINT) { return h; }
__attribute__((weak)) HWND WindowFromPoint(POINT) { return nullptr; }
__attribute__((weak)) BOOL GetClientRect(HWND, LPRECT r) { if (!r) return FALSE; *r = { 0, 0, 1280, 800 }; return TRUE; }
__attribute__((weak)) BOOL GetWindowRect(HWND, LPRECT r) { if (!r) return FALSE; *r = { 0, 0, 1280, 800 }; return TRUE; }
__attribute__((weak)) BOOL ClientToScreen(HWND, LPPOINT) { return TRUE; }
__attribute__((weak)) BOOL ScreenToClient(HWND, LPPOINT) { return TRUE; }
__attribute__((weak)) int MapWindowPoints(HWND, HWND, LPPOINT, UINT) { return 0; }
__attribute__((weak)) BOOL SetWindowPos(HWND, HWND, int, int, int, int, UINT) { return TRUE; }
__attribute__((weak)) BOOL GetWindowPlacement(HWND, WINDOWPLACEMENT* wp) { if (wp) std::memset(wp, 0, sizeof(*wp)); return TRUE; }
__attribute__((weak)) BOOL SetWindowPlacement(HWND, const WINDOWPLACEMENT*) { return TRUE; }
__attribute__((weak)) LONG GetWindowLongW(HWND, int) { return WS_OVERLAPPEDWINDOW; }
__attribute__((weak)) LONG SetWindowLongW(HWND, int, LONG v) { return v; }
__attribute__((weak)) LONG_PTR GetWindowLongPtrW(HWND, int) { return 0; }
__attribute__((weak)) LONG_PTR SetWindowLongPtrW(HWND, int, LONG_PTR v) { return v; }
__attribute__((weak)) LRESULT CallWindowProcW(WNDPROC fn, HWND h, UINT m, WPARAM w, LPARAM l) { return fn ? fn(h, m, w, l) : 0; }
__attribute__((weak)) LRESULT DefWindowProcW(HWND, UINT, WPARAM, LPARAM) { return 0; }
__attribute__((weak)) int GetWindowTextLengthW(HWND) { return 0; }
__attribute__((weak)) int GetWindowTextW(HWND, LPWSTR s, int n) { if (s && n > 0) s[0] = L'\0'; return 0; }
__attribute__((weak)) BOOL SetWindowTextW(HWND, LPCWSTR) { return TRUE; }
__attribute__((weak)) int GetDlgCtrlID(HWND) { return 0; }
__attribute__((weak)) BOOL InvalidateRect(HWND, const RECT*, BOOL) { return TRUE; }
__attribute__((weak)) BOOL ValidateRect(HWND, const RECT*) { return TRUE; }
__attribute__((weak)) HDC BeginPaint(HWND, LPPAINTSTRUCT ps) { if (ps) { std::memset(ps, 0, sizeof(*ps)); ps->rcPaint = {0,0,1280,800}; } return reinterpret_cast<HDC>(1); }
__attribute__((weak)) BOOL EndPaint(HWND, const PAINTSTRUCT*) { return TRUE; }

// Thread-safe Win32 message queue (supports worker-thread PostMessageW -> UI PeekMessageW/GetMessageW!)
namespace {
std::mutex g_msgQueueMutex;
std::condition_variable g_msgQueueCv;
std::deque<MSG> g_msgQueue;

bool MsgMatchesFilter(const MSG& m, HWND hWnd, UINT fMin, UINT fMax) {
    if (hWnd != nullptr && m.hwnd != hWnd) return false;
    if (fMin == 0 && fMax == 0) return true;
    return m.message >= fMin && m.message <= fMax;
}
} // namespace

__attribute__((weak)) BOOL PeekMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg) {
    if (!lpMsg) return FALSE;
    std::lock_guard<std::mutex> lk(g_msgQueueMutex);
    for (auto it = g_msgQueue.begin(); it != g_msgQueue.end(); ++it) {
        if (MsgMatchesFilter(*it, hWnd, wMsgFilterMin, wMsgFilterMax)) {
            *lpMsg = *it;
            if (wRemoveMsg & PM_REMOVE) g_msgQueue.erase(it);
            return TRUE;
        }
    }
    return FALSE;
}
__attribute__((weak)) BOOL GetMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax) {
    if (!lpMsg) return FALSE;
    std::unique_lock<std::mutex> lk(g_msgQueueMutex);
    for (;;) {
        for (auto it = g_msgQueue.begin(); it != g_msgQueue.end(); ++it) {
            if (MsgMatchesFilter(*it, hWnd, wMsgFilterMin, wMsgFilterMax)) {
                *lpMsg = *it;
                g_msgQueue.erase(it);
                return (lpMsg->message == WM_QUIT) ? FALSE : TRUE;
            }
        }
        g_msgQueueCv.wait_for(lk, std::chrono::milliseconds(50));
    }
}
__attribute__((weak)) BOOL TranslateMessage(const MSG*) { return TRUE; }
__attribute__((weak)) LRESULT DispatchMessageW(const MSG*) { return 0; }
__attribute__((weak)) BOOL IsDialogMessageW(HWND, LPMSG) { return FALSE; }
__attribute__((weak)) LRESULT SendMessageW(HWND, UINT, WPARAM, LPARAM) { return 0; }
__attribute__((weak)) BOOL PostMessageW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    {
        std::lock_guard<std::mutex> lk(g_msgQueueMutex);
        if (g_msgQueue.size() > 16384) g_msgQueue.pop_front();
        MSG m = {};
        m.hwnd = hWnd;
        m.message = Msg;
        m.wParam = wParam;
        m.lParam = lParam;
        m.time = GetTickCount();
        g_msgQueue.push_back(m);
    }
    g_msgQueueCv.notify_all();
    return TRUE;
}
__attribute__((weak)) void PostQuitMessage(int nExitCode) {
    PostMessageW(nullptr, WM_QUIT, static_cast<WPARAM>(nExitCode), 0);
}
__attribute__((weak)) UINT RegisterWindowMessageW(LPCWSTR) { static std::atomic<UINT> id{ 0xC000 }; return ++id; }
__attribute__((weak)) BOOL MessageBeep(UINT) { return TRUE; }
__attribute__((weak)) int MessageBoxW(HWND, LPCWSTR, LPCWSTR, UINT) { return IDOK; }

__attribute__((weak)) HWND SetCapture(HWND h) { return h; }
__attribute__((weak)) BOOL ReleaseCapture() { return TRUE; }
__attribute__((weak)) HWND GetCapture() { return nullptr; }
__attribute__((weak)) BOOL GetCursorPos(LPPOINT p) { if (p) *p = { 0, 0 }; return TRUE; }
__attribute__((weak)) HCURSOR SetCursor(HCURSOR c) { return c; }
__attribute__((weak)) HCURSOR LoadCursorW(HINSTANCE, LPCWSTR) { return reinterpret_cast<HCURSOR>(1); }
__attribute__((weak)) UINT GetDoubleClickTime() { return 500; }
__attribute__((weak)) SHORT GetKeyState(int) { return 0; }
__attribute__((weak)) SHORT GetAsyncKeyState(int) { return 0; }
__attribute__((weak)) UINT_PTR SetTimer(HWND, UINT_PTR nIDEvent, UINT, TIMERPROC) { return nIDEvent ? nIDEvent : 1; }
__attribute__((weak)) BOOL KillTimer(HWND, UINT_PTR) { return TRUE; }
__attribute__((weak)) HMONITOR MonitorFromWindow(HWND, DWORD) { return reinterpret_cast<HMONITOR>(1); }
__attribute__((weak)) BOOL GetMonitorInfoW(HMONITOR, LPMONITORINFO mi) {
    if (!mi) return FALSE;
    mi->rcMonitor = { 0, 0, 1920, 1080 };
    mi->rcWork = { 0, 0, 1920, 1040 };
    mi->dwFlags = 1;
    return TRUE;
}
__attribute__((weak)) int GetSystemMetrics(int idx) {
    switch (idx) {
    case SM_CXSCREEN: return 1920;
    case SM_CYSCREEN: return 1080;
    case SM_CXICON: case SM_CYICON: return 32;
    case SM_CXSMICON: case SM_CYSMICON: return 16;
    default: return 0;
    }
}
__attribute__((weak)) BOOL SystemParametersInfoW(UINT, UINT, PVOID, UINT) { return FALSE; }

__attribute__((weak)) HMENU CreatePopupMenu() { static intptr_t m = 0x2000; return reinterpret_cast<HMENU>(++m); }
__attribute__((weak)) BOOL DestroyMenu(HMENU) { return TRUE; }
__attribute__((weak)) BOOL AppendMenuW(HMENU, UINT, UINT_PTR, LPCWSTR) { return TRUE; }
__attribute__((weak)) BOOL SetMenuItemInfoW(HMENU, UINT, BOOL, LPCMENUITEMINFOW) { return TRUE; }
__attribute__((weak)) BOOL SetMenuDefaultItem(HMENU, UINT, UINT) { return TRUE; }
__attribute__((weak)) int GetMenuItemCount(HMENU) { return 0; }
__attribute__((weak)) BOOL TrackPopupMenu(HMENU, UINT, int, int, int, HWND, const RECT*) { return 0; }

// In-process Clipboard (backed by GlobalAlloc handles; bridged to X11 in Iter 8)
namespace {
std::mutex g_clipMutex;
std::map<UINT, HGLOBAL> g_clipData;
std::map<std::wstring, UINT> g_clipFormats;
UINT g_nextClipFmt = 0xD000u;
} // namespace

__attribute__((weak)) BOOL OpenClipboard(HWND) { return TRUE; }
__attribute__((weak)) BOOL EmptyClipboard() {
    std::lock_guard<std::mutex> lk(g_clipMutex);
    for (auto& kv : g_clipData) GlobalFree(kv.second);
    g_clipData.clear();
    return TRUE;
}
__attribute__((weak)) BOOL CloseClipboard() { return TRUE; }
__attribute__((weak)) HANDLE GetClipboardData(UINT uFormat) {
    std::lock_guard<std::mutex> lk(g_clipMutex);
    auto it = g_clipData.find(uFormat);
    return (it != g_clipData.end()) ? it->second : nullptr;
}
__attribute__((weak)) HANDLE SetClipboardData(UINT uFormat, HANDLE hMem) {
    std::lock_guard<std::mutex> lk(g_clipMutex);
    auto it = g_clipData.find(uFormat);
    if (it != g_clipData.end() && it->second != hMem) GlobalFree(it->second);
    g_clipData[uFormat] = hMem;
    return hMem;
}
__attribute__((weak)) BOOL IsClipboardFormatAvailable(UINT format) {
    std::lock_guard<std::mutex> lk(g_clipMutex);
    return g_clipData.find(format) != g_clipData.end() ? TRUE : FALSE;
}
__attribute__((weak)) UINT RegisterClipboardFormatW(LPCWSTR lpszFormat) {
    if (!lpszFormat) return 0;
    std::lock_guard<std::mutex> lk(g_clipMutex);
    auto it = g_clipFormats.find(lpszFormat);
    if (it != g_clipFormats.end()) return it->second;
    const UINT id = ++g_nextClipFmt;
    g_clipFormats[lpszFormat] = id;
    return id;
}
// The name a format was registered under (a MIME type such as "image/png" is also the X11 target of that name)
int GetClipboardFormatNameW(UINT format, LPWSTR lpszFormatName, int cchMaxCount) {
    if (!lpszFormatName || cchMaxCount <= 0) return 0;
    std::lock_guard<std::mutex> lk(g_clipMutex);
    for (const auto& [name, id] : g_clipFormats) {
        if (id != format) continue;
        const int n = static_cast<int>(std::min<size_t>(name.size(), static_cast<size_t>(cchMaxCount - 1)));
        std::wmemcpy(lpszFormatName, name.c_str(), static_cast<size_t>(n));
        lpszFormatName[n] = L'\0';
        return n;
    }
    lpszFormatName[0] = L'\0';
    return 0;
}
// Without the X11 layer (the command-line programs) nothing announces clipboard changes
__attribute__((weak)) BOOL AddClipboardFormatListener(HWND) { return TRUE; }
__attribute__((weak)) BOOL RemoveClipboardFormatListener(HWND) { return TRUE; }
__attribute__((weak)) DWORD GetClipboardSequenceNumber() { return 0; }

// GDI DIBSection & Objects
namespace {
struct DibRecord {
    int width = 0;
    int height = 0;
    int bpp = 32;
    std::vector<uint8_t> bits;
};
std::mutex g_dibMutex;
std::map<HBITMAP, std::unique_ptr<DibRecord>> g_dibs;
std::map<HBRUSH, DWORD> g_brushes;
std::map<HPEN, DWORD> g_pens;
struct DcState {
    HBITMAP selectedBmp = nullptr;
    DWORD brushColor = 0x00FFFFFFu; // COLORREF 0x00BBGGRR (white)
    DWORD penColor   = 0x00000000u; // black
    bool nullBrush   = false;
    bool nullPen     = false;
};
std::map<HDC, DcState> g_dcStates;
intptr_t g_nextDibId = 0x50000;
intptr_t g_nextBrushId = 0x60000;
intptr_t g_nextPenId = 0x68000;
} // namespace

namespace Explorer2::Hal {
bool BlitBgraToDcDib(HDC hdc, const RECT& subRect, const uint8_t* srcBgra, int srcW, int srcH) {
    if (!hdc || !srcBgra || srcW <= 0 || srcH <= 0) return false;
    std::lock_guard<std::mutex> lk(g_dibMutex);
    auto dcIt = g_dcStates.find(hdc);
    if (dcIt == g_dcStates.end() || !dcIt->second.selectedBmp) return false;
    auto dibIt = g_dibs.find(dcIt->second.selectedBmp);
    if (dibIt == g_dibs.end() || !dibIt->second) return false;
    auto& dib = *dibIt->second;
    for (int y = 0; y < srcH; ++y) {
        const int dy = subRect.top + y;
        if (dy < 0 || dy >= dib.height) continue;
        for (int x = 0; x < srcW; ++x) {
            const int dx = subRect.left + x;
            if (dx < 0 || dx >= dib.width) continue;
            std::memcpy(&dib.bits[(static_cast<size_t>(dy) * dib.width + dx) * 4],
                        &srcBgra[(static_cast<size_t>(y) * srcW + x) * 4], 4);
        }
    }
    return true;
}
} // namespace Explorer2::Hal

__attribute__((weak)) HDC GetDC(HWND) { return reinterpret_cast<HDC>(1); }
__attribute__((weak)) int ReleaseDC(HWND, HDC) { return 1; }
__attribute__((weak)) HDC CreateCompatibleDC(HDC) {
    static std::atomic<intptr_t> id{ 10 };
    HDC hdc = reinterpret_cast<HDC>(++id);
    std::lock_guard<std::mutex> lk(g_dibMutex);
    g_dcStates[hdc] = DcState{};
    return hdc;
}
__attribute__((weak)) BOOL DeleteDC(HDC hdc) {
    std::lock_guard<std::mutex> lk(g_dibMutex);
    g_dcStates.erase(hdc);
    return TRUE;
}
__attribute__((weak)) int GetDeviceCaps(HDC, int index) { return (index == LOGPIXELSX || index == LOGPIXELSY) ? 96 : 32; }
__attribute__((weak)) HGDIOBJ GetStockObject(int idx) {
    return reinterpret_cast<HGDIOBJ>(static_cast<intptr_t>(0x100 + idx));
}
__attribute__((weak)) HGDIOBJ SelectObject(HDC hdc, HGDIOBJ h) {
    std::lock_guard<std::mutex> lk(g_dibMutex);
    auto& st = g_dcStates[hdc];
    const intptr_t raw = reinterpret_cast<intptr_t>(h);
    if (raw >= 0x100 && raw < 0x120) {
        const int stock = static_cast<int>(raw - 0x100);
        if (stock == WHITE_BRUSH)      { st.brushColor = 0x00FFFFFFu; st.nullBrush = false; }
        else if (stock == LTGRAY_BRUSH){ st.brushColor = 0x00C0C0C0u; st.nullBrush = false; }
        else if (stock == GRAY_BRUSH)  { st.brushColor = 0x00808080u; st.nullBrush = false; }
        else if (stock == DKGRAY_BRUSH){ st.brushColor = 0x00404040u; st.nullBrush = false; }
        else if (stock == BLACK_BRUSH) { st.brushColor = 0x00000000u; st.nullBrush = false; }
        else if (stock == NULL_BRUSH)  { st.nullBrush = true; }
        else if (stock == WHITE_PEN)   { st.penColor = 0x00FFFFFFu; st.nullPen = false; }
        else if (stock == BLACK_PEN)   { st.penColor = 0x00000000u; st.nullPen = false; }
        else if (stock == NULL_PEN)    { st.nullPen = true; }
        return h;
    }
    if (g_dibs.find(reinterpret_cast<HBITMAP>(h)) != g_dibs.end()) {
        HBITMAP prev = st.selectedBmp;
        st.selectedBmp = reinterpret_cast<HBITMAP>(h);
        return prev ? reinterpret_cast<HGDIOBJ>(prev) : h;
    }
    auto brIt = g_brushes.find(reinterpret_cast<HBRUSH>(h));
    if (brIt != g_brushes.end()) {
        st.brushColor = brIt->second;
        st.nullBrush = false;
        return h;
    }
    auto penIt = g_pens.find(reinterpret_cast<HPEN>(h));
    if (penIt != g_pens.end()) {
        st.penColor = penIt->second;
        st.nullPen = false;
        return h;
    }
    return h;
}
__attribute__((weak)) BOOL DeleteObject(HGDIOBJ ho) {
    std::lock_guard<std::mutex> lk(g_dibMutex);
    g_dibs.erase(reinterpret_cast<HBITMAP>(ho));
    g_brushes.erase(reinterpret_cast<HBRUSH>(ho));
    g_pens.erase(reinterpret_cast<HPEN>(ho));
    return TRUE;
}
__attribute__((weak)) int GetObjectW(HANDLE h, int c, LPVOID pv) {
    if (!pv || c < static_cast<int>(sizeof(BITMAP))) return 0;
    std::lock_guard<std::mutex> lk(g_dibMutex);
    auto it = g_dibs.find(reinterpret_cast<HBITMAP>(h));
    if (it == g_dibs.end()) return 0;
    auto* bm = static_cast<BITMAP*>(pv);
    std::memset(bm, 0, sizeof(*bm));
    bm->bmWidth = it->second->width;
    bm->bmHeight = it->second->height;
    bm->bmWidthBytes = it->second->width * 4;
    bm->bmPlanes = 1;
    bm->bmBitsPixel = static_cast<WORD>(it->second->bpp);
    bm->bmBits = it->second->bits.data();
    return sizeof(BITMAP);
}
__attribute__((weak)) HBITMAP CreateDIBSection(HDC, const BITMAPINFO* pbmi, UINT, VOID** ppvBits, HANDLE, DWORD) {
    if (!pbmi || pbmi->bmiHeader.biWidth <= 0 || pbmi->bmiHeader.biHeight == 0) return nullptr;
    const int w = pbmi->bmiHeader.biWidth;
    const int h = std::abs(pbmi->bmiHeader.biHeight);
    auto rec = std::make_unique<DibRecord>();
    rec->width = w;
    rec->height = h;
    rec->bpp = pbmi->bmiHeader.biBitCount ? pbmi->bmiHeader.biBitCount : 32;
    rec->bits.resize(static_cast<size_t>(w) * h * 4, 0);
    void* ptr = rec->bits.data();
    std::lock_guard<std::mutex> lk(g_dibMutex);
    HBITMAP hbmp = reinterpret_cast<HBITMAP>(++g_nextDibId);
    g_dibs[hbmp] = std::move(rec);
    if (ppvBits) *ppvBits = ptr;
    return hbmp;
}
__attribute__((weak)) BOOL IsProcessDPIAware() { return TRUE; }
__attribute__((weak)) HANDLE LoadImageW(HINSTANCE, LPCWSTR, UINT, int, int, UINT) { return reinterpret_cast<HANDLE>(1); }
__attribute__((weak)) HICON LoadIconW(HINSTANCE, LPCWSTR) { return reinterpret_cast<HICON>(1); }

__attribute__((weak)) int GetDIBits(HDC, HBITMAP hbm, UINT start, UINT cLines, LPVOID lpvBits, BITMAPINFO* lpbmi, UINT) {
    if (!hbm || !lpbmi) return 0;
    std::lock_guard<std::mutex> lk(g_dibMutex);
    auto it = g_dibs.find(hbm);
    if (it == g_dibs.end()) return 0;
    const auto& rec = *it->second;
    if (!lpvBits) {
        lpbmi->bmiHeader.biWidth = rec.width;
        lpbmi->bmiHeader.biHeight = rec.height;
        lpbmi->bmiHeader.biPlanes = 1;
        lpbmi->bmiHeader.biBitCount = 32;
        lpbmi->bmiHeader.biSizeImage = static_cast<DWORD>(rec.width * rec.height * 4);
        return 1;
    }
    if (rec.width <= 0 || rec.height <= 0 || cLines == 0) return 0;
    const UINT maxLines = (start < static_cast<UINT>(rec.height))
        ? std::min<UINT>(cLines, static_cast<UINT>(rec.height) - start)
        : 0u;
    const bool topDown = (lpbmi->bmiHeader.biHeight < 0);
    const size_t rowBytes = static_cast<size_t>(rec.width) * 4;
    auto* dst = static_cast<uint8_t*>(lpvBits);
    for (UINT i = 0; i < maxLines; ++i) {
        const UINT srcY = topDown ? (start + i) : (static_cast<UINT>(rec.height - 1) - (start + i));
        std::memcpy(dst + static_cast<size_t>(i) * rowBytes,
                    rec.bits.data() + static_cast<size_t>(srcY) * rowBytes,
                    rowBytes);
    }
    return static_cast<int>(maxLines);
}

__attribute__((weak)) BOOL GdiFlush() { return TRUE; }
__attribute__((weak)) HBRUSH CreateSolidBrush(DWORD color) {
    std::lock_guard<std::mutex> lk(g_dibMutex);
    HBRUSH h = reinterpret_cast<HBRUSH>(++g_nextBrushId);
    g_brushes[h] = color;
    return h;
}
__attribute__((weak)) HPEN CreatePen(int, int, DWORD color) {
    std::lock_guard<std::mutex> lk(g_dibMutex);
    HPEN h = reinterpret_cast<HPEN>(++g_nextPenId);
    g_pens[h] = color;
    return h;
}
__attribute__((weak)) BOOL FillRect(HDC hDC, const RECT* lprc, HBRUSH hbr) {
    if (!hDC || !lprc) return FALSE;
    std::lock_guard<std::mutex> lk(g_dibMutex);
    auto dcIt = g_dcStates.find(hDC);
    if (dcIt == g_dcStates.end() || !dcIt->second.selectedBmp) return FALSE;
    auto dibIt = g_dibs.find(dcIt->second.selectedBmp);
    if (dibIt == g_dibs.end() || !dibIt->second) return FALSE;
    DWORD cref = dcIt->second.brushColor;
    const intptr_t raw = reinterpret_cast<intptr_t>(hbr);
    if (raw >= 0x100 && raw < 0x120) {
        const int stock = static_cast<int>(raw - 0x100);
        if (stock == WHITE_BRUSH) cref = 0x00FFFFFFu;
        else if (stock == BLACK_BRUSH) cref = 0x00000000u;
        else if (stock == GRAY_BRUSH) cref = 0x00808080u;
        else if (stock == LTGRAY_BRUSH) cref = 0x00C0C0C0u;
        else if (stock == DKGRAY_BRUSH) cref = 0x00404040u;
    } else {
        auto brIt = g_brushes.find(hbr);
        if (brIt != g_brushes.end()) cref = brIt->second;
    }
    const uint8_t r = static_cast<uint8_t>(cref & 0xFFu);
    const uint8_t g = static_cast<uint8_t>((cref >> 8) & 0xFFu);
    const uint8_t b = static_cast<uint8_t>((cref >> 16) & 0xFFu);
    auto& dib = *dibIt->second;
    const int x0 = std::clamp<int>(lprc->left, 0, dib.width);
    const int x1 = std::clamp<int>(lprc->right, 0, dib.width);
    const int y0 = std::clamp<int>(lprc->top, 0, dib.height);
    const int y1 = std::clamp<int>(lprc->bottom, 0, dib.height);
    for (int y = y0; y < y1; ++y) {
        uint8_t* row = dib.bits.data() + static_cast<size_t>(y) * dib.width * 4;
        for (int x = x0; x < x1; ++x) {
            row[x * 4 + 0] = b;
            row[x * 4 + 1] = g;
            row[x * 4 + 2] = r;
            row[x * 4 + 3] = 255;
        }
    }
    return TRUE;
}
__attribute__((weak)) HFONT CreateFontW(int, int, int, int, int, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, LPCWSTR) {
    static intptr_t s_fontId = 0x3000;
    return reinterpret_cast<HFONT>(++s_fontId);
}

// Genuine [MS-EMF] Binary Metafile Recorder & Handle Store
namespace {
struct EmfRecording {
    std::string filename;
    int32_t minX = 0, minY = 0, maxX = 128, maxY = 128;
    bool hasBounds = false;
    uint32_t recordCount = 1; // EMR_HEADER
    std::vector<uint8_t> body;
    void PushU32(uint32_t v) {
        body.push_back(static_cast<uint8_t>(v & 0xFFu));
        body.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
        body.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
        body.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
    }
    void PushI32(int32_t v) { PushU32(static_cast<uint32_t>(v)); }
    void ExpandBounds(int l, int t, int r, int b) {
        if (!hasBounds) {
            minX = std::min(l, r); minY = std::min(t, b);
            maxX = std::max(l, r); maxY = std::max(t, b);
            hasBounds = true;
        } else {
            minX = std::min<int32_t>(minX, std::min(l, r));
            minY = std::min<int32_t>(minY, std::min(t, b));
            maxX = std::max<int32_t>(maxX, std::max(l, r));
            maxY = std::max<int32_t>(maxY, std::max(t, b));
        }
    }
};
std::mutex g_emfMutex;
std::map<HDC, std::unique_ptr<EmfRecording>> g_emfOpenDcs;
std::map<HENHMETAFILE, std::vector<uint8_t>> g_emfHandles;
intptr_t g_nextEmfId = 0x70000;
} // namespace

__attribute__((weak)) BOOL Rectangle(HDC hdc, int left, int top, int right, int bottom) {
    std::lock_guard<std::mutex> lk(g_emfMutex);
    auto it = g_emfOpenDcs.find(hdc);
    if (it != g_emfOpenDcs.end()) {
        auto& rec = *it->second;
        rec.ExpandBounds(left, top, right, bottom);
        rec.PushU32(43u); // EMR_RECTANGLE
        rec.PushU32(24u);
        rec.PushI32(left); rec.PushI32(top); rec.PushI32(right); rec.PushI32(bottom);
        ++rec.recordCount;
    }
    return TRUE;
}
__attribute__((weak)) BOOL Ellipse(HDC hdc, int left, int top, int right, int bottom) {
    std::lock_guard<std::mutex> lk(g_emfMutex);
    auto it = g_emfOpenDcs.find(hdc);
    if (it != g_emfOpenDcs.end()) {
        auto& rec = *it->second;
        rec.ExpandBounds(left, top, right, bottom);
        rec.PushU32(42u); // EMR_ELLIPSE
        rec.PushU32(24u);
        rec.PushI32(left); rec.PushI32(top); rec.PushI32(right); rec.PushI32(bottom);
        ++rec.recordCount;
    }
    return TRUE;
}
__attribute__((weak)) HDC CreateEnhMetaFileW(HDC, LPCWSTR lpFilename, const RECT* lprc, LPCWSTR) {
    auto rec = std::make_unique<EmfRecording>();
    if (lpFilename && *lpFilename) rec->filename = Explorer2::Compat::NativePath(lpFilename);
    if (lprc) rec->ExpandBounds(lprc->left, lprc->top, lprc->right, lprc->bottom);
    std::lock_guard<std::mutex> lk(g_emfMutex);
    HDC hdc = reinterpret_cast<HDC>(++g_nextEmfId);
    g_emfOpenDcs[hdc] = std::move(rec);
    return hdc;
}
__attribute__((weak)) HENHMETAFILE CloseEnhMetaFile(HDC hdc) {
    std::lock_guard<std::mutex> lk(g_emfMutex);
    auto it = g_emfOpenDcs.find(hdc);
    if (it == g_emfOpenDcs.end()) return nullptr;
    std::unique_ptr<EmfRecording> rec = std::move(it->second);
    g_emfOpenDcs.erase(it);

    // Append EMR_EOF (type 14, size 20)
    rec->PushU32(14u);
    rec->PushU32(20u);
    rec->PushU32(0u);  // nPalEntries
    rec->PushU32(16u); // offPalEntries
    rec->PushU32(20u); // nSizeLast
    ++rec->recordCount;

    const uint32_t hdrSize = 108u;
    const uint32_t totalBytes = hdrSize + static_cast<uint32_t>(rec->body.size());
    std::vector<uint8_t> emf;
    emf.reserve(totalBytes);
    auto push32 = [&](uint32_t v) {
        emf.push_back(static_cast<uint8_t>(v & 0xFFu));
        emf.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
        emf.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
        emf.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
    };
    const int32_t bx0 = rec->hasBounds ? std::min<int32_t>(0, rec->minX) : 0;
    const int32_t by0 = rec->hasBounds ? std::min<int32_t>(0, rec->minY) : 0;
    const int32_t bx1 = std::max<int32_t>(bx0 + 1, rec->maxX + 10);
    const int32_t by1 = std::max<int32_t>(by0 + 1, rec->maxY + 10);
    push32(1u);                  // iType = EMR_HEADER
    push32(hdrSize);             // nSize = 108
    push32(static_cast<uint32_t>(bx0)); push32(static_cast<uint32_t>(by0));
    push32(static_cast<uint32_t>(bx1)); push32(static_cast<uint32_t>(by1));
    push32(static_cast<uint32_t>(bx0 * 26)); push32(static_cast<uint32_t>(by0 * 26));
    push32(static_cast<uint32_t>(bx1 * 26)); push32(static_cast<uint32_t>(by1 * 26));
    push32(0x464D4520u);         // dSignature = " EMF"
    push32(0x00010000u);         // nVersion
    push32(totalBytes);          // nBytes
    push32(rec->recordCount);    // nRecords
    push32(1u);                  // nHandles=1, sReserved=0
    push32(0u); push32(0u); push32(0u); // nDescription, offDescription, nPalEntries
    push32(1920u); push32(1080u);       // szlDevice
    push32(508u);  push32(286u);        // szlMillimeters
    push32(0u); push32(0u); push32(0u); // cbPixelFormat, offPixelFormat, bOpenGL
    push32(508000u); push32(286000u);   // szlMicrometers
    emf.insert(emf.end(), rec->body.begin(), rec->body.end());

    if (!rec->filename.empty()) {
        if (FILE* f = std::fopen(rec->filename.c_str(), "wb")) {
            std::fwrite(emf.data(), 1, emf.size(), f);
            std::fclose(f);
        }
    }
    HENHMETAFILE hemf = reinterpret_cast<HENHMETAFILE>(++g_nextEmfId);
    g_emfHandles[hemf] = std::move(emf);
    return hemf;
}
__attribute__((weak)) UINT GetEnhMetaFileBits(HENHMETAFILE hemf, UINT nSize, LPBYTE lpData) {
    std::lock_guard<std::mutex> lk(g_emfMutex);
    auto it = g_emfHandles.find(hemf);
    if (it == g_emfHandles.end()) return 0;
    const auto& bytes = it->second;
    if (!lpData) return static_cast<UINT>(bytes.size());
    const size_t n = std::min<size_t>(nSize, bytes.size());
    std::memcpy(lpData, bytes.data(), n);
    return static_cast<UINT>(n);
}
__attribute__((weak)) BOOL DeleteEnhMetaFile(HENHMETAFILE hemf) {
    std::lock_guard<std::mutex> lk(g_emfMutex);
    return g_emfHandles.erase(hemf) > 0 ? TRUE : FALSE;
}

__attribute__((weak)) HENHMETAFILE SetEnhMetaFileBits(UINT nSize, const BYTE* pb) {
    if (!pb || nSize < 88) return nullptr;
    uint32_t recType = 0, sig = 0;
    std::memcpy(&recType, pb, 4);
    std::memcpy(&sig, pb + 40, 4);
    if (recType != 1u || sig != 0x464D4520u) return nullptr;
    std::lock_guard<std::mutex> lk(g_emfMutex);
    HENHMETAFILE hemf = reinterpret_cast<HENHMETAFILE>(++g_nextEmfId);
    g_emfHandles[hemf].assign(pb, pb + nSize);
    return hemf;
}

__attribute__((weak)) HENHMETAFILE SetWinMetaFileBits(UINT nSize, const BYTE* lpMeta16Data, HDC, const void*) {
    if (!lpMeta16Data || nSize < 18) return nullptr;
    uint16_t mtType = 0, mtHdrWords = 0;
    std::memcpy(&mtType, lpMeta16Data, 2);
    std::memcpy(&mtHdrWords, lpMeta16Data + 2, 2);
    if ((mtType != 1 && mtType != 2) || mtHdrWords < 9) return nullptr;

    HDC hdc = CreateEnhMetaFileW(nullptr, nullptr, nullptr, nullptr);
    if (!hdc) return nullptr;
    size_t off = static_cast<size_t>(mtHdrWords) * 2;
    bool drewAny = false;
    while (off + 6 <= nSize) {
        uint32_t recWords = 0;
        uint16_t fn = 0;
        std::memcpy(&recWords, lpMeta16Data + off, 4);
        std::memcpy(&fn, lpMeta16Data + off + 4, 2);
        if (fn == 0 || recWords < 3) break;
        const size_t recBytes = static_cast<size_t>(recWords) * 2;
        if (off + recBytes > nSize) break;
        const uint8_t* p = lpMeta16Data + off + 6;
        const size_t pLen = recBytes - 6;
        auto rdI16 = [&](size_t idx) -> int16_t {
            int16_t v = 0;
            if ((idx + 1) * 2 <= pLen) std::memcpy(&v, p + idx * 2, 2);
            return v;
        };
        if (fn == 0x041B && pLen >= 8) { // META_RECTANGLE (bottom, right, top, left)
            Rectangle(hdc, rdI16(3), rdI16(2), rdI16(1), rdI16(0));
            drewAny = true;
        } else if (fn == 0x0418 && pLen >= 8) { // META_ELLIPSE
            Ellipse(hdc, rdI16(3), rdI16(2), rdI16(1), rdI16(0));
            drewAny = true;
        } else if ((fn == 0x0324 || fn == 0x0325) && pLen >= 6) { // META_POLYGON / META_POLYLINE
            const int cnt = rdI16(0);
            if (cnt >= 2 && static_cast<size_t>(1 + cnt * 2) * 2 <= pLen) {
                int minX = rdI16(1), maxX = minX, minY = rdI16(2), maxY = minY;
                for (int i = 1; i < cnt; ++i) {
                    minX = std::min<int>(minX, rdI16(1 + i * 2));
                    maxX = std::max<int>(maxX, rdI16(1 + i * 2));
                    minY = std::min<int>(minY, rdI16(2 + i * 2));
                    maxY = std::max<int>(maxY, rdI16(2 + i * 2));
                }
                Rectangle(hdc, minX, minY, std::max(minX + 2, maxX), std::max(minY + 2, maxY));
                drewAny = true;
            }
        }
        off += recBytes;
    }
    if (!drewAny) Rectangle(hdc, 8, 8, 120, 96);
    return CloseEnhMetaFile(hdc);
}

__attribute__((weak)) UINT GetEnhMetaFileHeader(HENHMETAFILE hemf, UINT nSize, ENHMETAHEADER* lpEnhMetaHeader) {
    std::lock_guard<std::mutex> lk(g_emfMutex);
    auto it = g_emfHandles.find(hemf);
    if (it == g_emfHandles.end() || it->second.size() < 88) return 0;
    const UINT copyBytes = static_cast<UINT>(std::min<size_t>(sizeof(ENHMETAHEADER), it->second.size()));
    if (!lpEnhMetaHeader) return copyBytes;
    if (nSize < 88) return 0;
    std::memset(lpEnhMetaHeader, 0, std::min<size_t>(nSize, sizeof(ENHMETAHEADER)));
    std::memcpy(lpEnhMetaHeader, it->second.data(), std::min<UINT>(nSize, copyBytes));
    return std::min<UINT>(nSize, copyBytes);
}

__attribute__((weak)) BOOL PlayEnhMetaFile(HDC hdc, HENHMETAFILE hmf, const RECT* lprect) {
    if (!hdc || !hmf || !lprect) return FALSE;
    std::vector<uint8_t> emf;
    {
        std::lock_guard<std::mutex> lk(g_emfMutex);
        auto it = g_emfHandles.find(hmf);
        if (it == g_emfHandles.end() || it->second.size() < 88) return FALSE;
        emf = it->second;
    }

    std::lock_guard<std::mutex> lkDib(g_dibMutex);
    auto dcIt = g_dcStates.find(hdc);
    if (dcIt == g_dcStates.end() || !dcIt->second.selectedBmp) return FALSE;
    auto dibIt = g_dibs.find(dcIt->second.selectedBmp);
    if (dibIt == g_dibs.end() || !dibIt->second) return FALSE;
    auto& dib = *dibIt->second;

    ENHMETAHEADER hdr = {};
    std::memcpy(&hdr, emf.data(), std::min<size_t>(sizeof(hdr), emf.size()));
    double srcX0 = hdr.rclBounds.left;
    double srcY0 = hdr.rclBounds.top;
    double srcW  = std::max<double>(1.0, hdr.rclBounds.right - hdr.rclBounds.left);
    double srcH  = std::max<double>(1.0, hdr.rclBounds.bottom - hdr.rclBounds.top);
    const double dstX0 = lprect->left;
    const double dstY0 = lprect->top;
    const double dstW  = std::max<double>(1.0, lprect->right - lprect->left);
    const double dstH  = std::max<double>(1.0, lprect->bottom - lprect->top);

    auto mapX = [&](double x) -> int { return static_cast<int>(std::lround(dstX0 + (x - srcX0) * dstW / srcW)); };
    auto mapY = [&](double y) -> int { return static_cast<int>(std::lround(dstY0 + (y - srcY0) * dstH / srcH)); };

    auto putPx = [&](int x, int y, DWORD cref) {
        if (x < 0 || x >= dib.width || y < 0 || y >= dib.height) return;
        uint8_t* p = dib.bits.data() + (static_cast<size_t>(y) * dib.width + x) * 4;
        p[0] = static_cast<uint8_t>((cref >> 16) & 0xFFu);
        p[1] = static_cast<uint8_t>((cref >> 8) & 0xFFu);
        p[2] = static_cast<uint8_t>(cref & 0xFFu);
        p[3] = 255;
    };
    auto drawLine = [&](int x0, int y0, int x1, int y1, DWORD cref) {
        int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        for (int guard = 0; guard < 16384; ++guard) {
            putPx(x0, y0, cref);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    };

    struct EmfObj { bool isBrush = false; DWORD color = 0; bool isNull = false; };
    std::map<uint32_t, EmfObj> objTable;
    DWORD curBrush = 0x00F0D8B0u;
    DWORD curPen   = 0x00202020u;
    bool nullBrush = false, nullPen = false;
    int curX = 0, curY = 0;

    auto rdU32 = [&](size_t off) -> uint32_t {
        uint32_t v = 0;
        if (off + 4 <= emf.size()) std::memcpy(&v, emf.data() + off, 4);
        return v;
    };
    auto rdI32 = [&](size_t off) -> int32_t { return static_cast<int32_t>(rdU32(off)); };
    auto rdI16 = [&](size_t off) -> int16_t {
        int16_t v = 0;
        if (off + 2 <= emf.size()) std::memcpy(&v, emf.data() + off, 2);
        return v;
    };

    size_t pos = 0;
    while (pos + 8 <= emf.size()) {
        const uint32_t rType = rdU32(pos);
        const uint32_t rSize = rdU32(pos + 4);
        if (rSize < 8 || pos + rSize > emf.size()) break;
        if (rType == 14u) break; // EMR_EOF

        if (rType == 38u && rSize >= 28) { // EMR_CREATEPEN
            uint32_t idx = rdU32(pos + 8);
            uint32_t style = rdU32(pos + 12);
            uint32_t col = rdU32(pos + 24) & 0x00FFFFFFu;
            objTable[idx] = { false, col, (style & 0xFu) == 5u };
        } else if (rType == 39u && rSize >= 24) { // EMR_CREATEBRUSHINDIRECT
            uint32_t idx = rdU32(pos + 8);
            uint32_t style = rdU32(pos + 12);
            uint32_t col = rdU32(pos + 16) & 0x00FFFFFFu;
            objTable[idx] = { true, col, style == 1u };
        } else if (rType == 37u && rSize >= 12) { // EMR_SELECTOBJECT
            uint32_t idx = rdU32(pos + 8);
            if (idx & 0x80000000u) {
                uint32_t st = idx & 0x7FFFFFFFu;
                if (st == 0) { curBrush = 0x00FFFFFFu; nullBrush = false; }
                else if (st == 4) { curBrush = 0x00000000u; nullBrush = false; }
                else if (st == 5) { nullBrush = true; }
                else if (st == 6) { curPen = 0x00FFFFFFu; nullPen = false; }
                else if (st == 7) { curPen = 0x00000000u; nullPen = false; }
                else if (st == 8) { nullPen = true; }
            } else {
                auto oit = objTable.find(idx);
                if (oit != objTable.end()) {
                    if (oit->second.isBrush) { curBrush = oit->second.color; nullBrush = oit->second.isNull; }
                    else { curPen = oit->second.color; nullPen = oit->second.isNull; }
                }
            }
        } else if (rType == 105u && rSize >= 12) { // EMR_SETDCBRUSHCOLOR
            curBrush = rdU32(pos + 8) & 0x00FFFFFFu; nullBrush = false;
        } else if (rType == 106u && rSize >= 12) { // EMR_SETDCPENCOLOR
            curPen = rdU32(pos + 8) & 0x00FFFFFFu; nullPen = false;
        } else if ((rType == 43u || rType == 44u) && rSize >= 24) { // EMR_RECTANGLE / EMR_ROUNDRECT
            int x0 = mapX(rdI32(pos + 8)),  y0 = mapY(rdI32(pos + 12));
            int x1 = mapX(rdI32(pos + 16)), y1 = mapY(rdI32(pos + 20));
            if (x0 > x1) std::swap(x0, x1);
            if (y0 > y1) std::swap(y0, y1);
            if (!nullBrush) {
                for (int y = std::max(0, y0); y <= std::min(dib.height - 1, y1); ++y)
                    for (int x = std::max(0, x0); x <= std::min(dib.width - 1, x1); ++x)
                        putPx(x, y, curBrush);
            }
            if (!nullPen) {
                drawLine(x0, y0, x1, y0, curPen); drawLine(x1, y0, x1, y1, curPen);
                drawLine(x1, y1, x0, y1, curPen); drawLine(x0, y1, x0, y0, curPen);
            }
        } else if (rType == 42u && rSize >= 24) { // EMR_ELLIPSE
            int x0 = mapX(rdI32(pos + 8)),  y0 = mapY(rdI32(pos + 12));
            int x1 = mapX(rdI32(pos + 16)), y1 = mapY(rdI32(pos + 20));
            if (x0 > x1) std::swap(x0, x1);
            if (y0 > y1) std::swap(y0, y1);
            const double cx = 0.5 * (x0 + x1), cy = 0.5 * (y0 + y1);
            const double rx = std::max(1.0, 0.5 * (x1 - x0)), ry = std::max(1.0, 0.5 * (y1 - y0));
            for (int y = std::max(0, y0); y <= std::min(dib.height - 1, y1); ++y) {
                for (int x = std::max(0, x0); x <= std::min(dib.width - 1, x1); ++x) {
                    const double nx = (x - cx) / rx, ny = (y - cy) / ry;
                    const double d2 = nx * nx + ny * ny;
                    if (d2 <= 1.0) {
                        if (!nullPen && d2 >= 0.85) putPx(x, y, curPen);
                        else if (!nullBrush) putPx(x, y, curBrush);
                    }
                }
            }
        } else if (rType == 27u && rSize >= 16) { // EMR_MOVETOEX
            curX = mapX(rdI32(pos + 8));
            curY = mapY(rdI32(pos + 12));
        } else if (rType == 54u && rSize >= 16) { // EMR_LINETO
            int nx = mapX(rdI32(pos + 8)), ny = mapY(rdI32(pos + 12));
            if (!nullPen) drawLine(curX, curY, nx, ny, curPen);
            curX = nx; curY = ny;
        } else if ((rType == 3u || rType == 4u) && rSize >= 28) { // EMR_POLYGON / EMR_POLYLINE (32-bit pts)
            uint32_t cpt = rdU32(pos + 24);
            if (cpt >= 2 && 28u + cpt * 8u <= rSize) {
                for (uint32_t i = 0; i + 1 < cpt; ++i) {
                    drawLine(mapX(rdI32(pos + 28 + i * 8)), mapY(rdI32(pos + 32 + i * 8)),
                             mapX(rdI32(pos + 28 + (i + 1) * 8)), mapY(rdI32(pos + 32 + (i + 1) * 8)), curPen);
                }
                if (rType == 3u) {
                    drawLine(mapX(rdI32(pos + 28 + (cpt - 1) * 8)), mapY(rdI32(pos + 32 + (cpt - 1) * 8)),
                             mapX(rdI32(pos + 28)), mapY(rdI32(pos + 32)), curPen);
                }
            }
        } else if ((rType == 86u || rType == 87u) && rSize >= 28) { // EMR_POLYGON16 / EMR_POLYLINE16
            uint32_t cpt = rdU32(pos + 24);
            if (cpt >= 2 && 28u + cpt * 4u <= rSize) {
                for (uint32_t i = 0; i + 1 < cpt; ++i) {
                    drawLine(mapX(rdI16(pos + 28 + i * 4)), mapY(rdI16(pos + 30 + i * 4)),
                             mapX(rdI16(pos + 28 + (i + 1) * 4)), mapY(rdI16(pos + 30 + (i + 1) * 4)), curPen);
                }
                if (rType == 86u) {
                    drawLine(mapX(rdI16(pos + 28 + (cpt - 1) * 4)), mapY(rdI16(pos + 30 + (cpt - 1) * 4)),
                             mapX(rdI16(pos + 28)), mapY(rdI16(pos + 30)), curPen);
                }
            }
        }
        pos += rSize;
    }
    return TRUE;
}
// Procedural BGRA32 System Icon Store & IImageList Implementation
namespace {
struct IconDesc {
    int iconIndex = 2;
    int dim = 32;
};
std::mutex g_iconMutex;
std::map<HICON, IconDesc> g_icons;
intptr_t g_nextIconId = 0x90000;

HBITMAP RenderProceduralIconDib(int iconIndex, int dim) {
    const int s = std::clamp(dim, 16, 256);
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = s;
    bmi.bmiHeader.biHeight = -s;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* rawBits = nullptr;
    HBITMAP hbm = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &rawBits, nullptr, 0);
    if (!hbm || !rawBits) return nullptr;
    auto* px = static_cast<uint8_t*>(rawBits);
    std::memset(px, 0, static_cast<size_t>(s) * s * 4);

    auto put = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        if (x < 0 || y < 0 || x >= s || y >= s) return;
        uint8_t* p = px + (static_cast<size_t>(y) * s + x) * 4;
        p[0] = b; p[1] = g; p[2] = r; p[3] = a;
    };

    if (iconIndex == 1 || iconIndex == 4 || iconIndex == 5) {
        // Crisp amber folder icon
        const int x0 = s / 10, x1 = s - std::max(1, s / 10);
        const int yTab = s / 6, yBody = s * 3 / 10, y1 = s - std::max(1, s / 8);
        const int tabW = (x1 - x0) * 9 / 20;
        for (int y = yTab; y < yBody; ++y)
            for (int x = x0; x < x0 + tabW; ++x) put(x, y, 232, 168, 46);
        for (int y = yBody; y < y1; ++y)
            for (int x = x0; x < x1; ++x) {
                const bool border = (x == x0 || x == x1 - 1 || y == yBody || y == y1 - 1);
                put(x, y, border ? 186 : 246, border ? 128 : 196, border ? 24 : 68);
            }
    } else {
        // Crisp document sheet with folded corner + deterministic extension accent stripe
        static const uint8_t kPalette[][3] = {
            { 110, 128, 152 }, { 216,  62,  54 }, {  48, 128, 218 }, {  52, 162,  92 },
            { 156,  88, 208 }, { 224, 140,  40 }, {  44, 166, 178 }, { 196,  72, 134 },
            {  92, 108, 204 }, { 128, 156,  52 }, { 208,  98,  56 }, {  68, 146, 136 }
        };
        const auto& acc = kPalette[static_cast<unsigned>(std::abs(iconIndex)) % (sizeof(kPalette) / sizeof(kPalette[0]))];
        const int x0 = std::max(2, s * 3 / 16), x1 = s - std::max(2, s * 3 / 16);
        const int y0 = std::max(1, s / 10),     y1 = s - std::max(1, s / 10);
        const int fold = std::max(3, (x1 - x0) / 4);
        const int bandY0 = y0 + (y1 - y0) * 5 / 10;
        const int bandY1 = std::min(y1 - 2, bandY0 + std::max(3, (y1 - y0) * 3 / 10));
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                if ((x - (x1 - fold)) + (y0 - y) > 0) continue; // Dog-ear cut
                const bool inFold = (x >= x1 - fold && y < y0 + fold);
                const bool border = (x == x0 || x == x1 - 1 || y == y0 || y == y1 - 1);
                if (border) put(x, y, 140, 150, 164);
                else if (inFold) put(x, y, 212, 218, 228);
                else if (y >= bandY0 && y < bandY1 && x > x0 + 1 && x < x1 - 2) put(x, y, acc[0], acc[1], acc[2]);
                else put(x, y, 246, 248, 252);
            }
        }
    }
    return hbm;
}

class LinuxImageList final : public IImageList {
public:
    explicit LinuxImageList(int dim) : m_dim(dim) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IImageList)) { *ppv = static_cast<IImageList*>(this); AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = --m_ref; if (!r) delete this; return r; }

    HRESULT STDMETHODCALLTYPE GetIcon(int i, UINT, HICON* picon) override {
        if (!picon) return E_POINTER;
        if (i < 0) { *picon = nullptr; return E_INVALIDARG; }
        std::lock_guard<std::mutex> lk(g_iconMutex);
        HICON h = reinterpret_cast<HICON>(++g_nextIconId);
        g_icons[h] = { i, m_dim };
        *picon = h;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetIconSize(int* cx, int* cy) override {
        if (cx) *cx = m_dim;
        if (cy) *cy = m_dim;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetImageCount(int* pi) override {
        if (pi) *pi = 512;
        return S_OK;
    }
private:
    std::atomic<ULONG> m_ref{ 1 };
    int m_dim = 32;
};

int IconIndexForPathOrExt(LPCWSTR pszPath, bool isDir) {
    if (isDir) return 4; // SIID_FOLDER + 1
    if (!pszPath || !*pszPath) return 1; // SIID_DOCNOASSOC + 1
    std::wstring ext = Explorer2::PathUtils::GetExtension(pszPath);
    if (ext.empty()) return 1;
    // Deterministic distinct non-colliding icon index in [10..499] per extension
    uint32_t h = 2166136261u;
    for (wchar_t c : ext) {
        h ^= static_cast<uint32_t>(std::towlower(c));
        h *= 16777619u;
    }
    return 10 + static_cast<int>(h % 480u);
}
} // namespace

__attribute__((weak)) BOOL DestroyIcon(HICON hIcon) {
    std::lock_guard<std::mutex> lk(g_iconMutex);
    g_icons.erase(hIcon);
    return TRUE;
}
__attribute__((weak)) BOOL GetIconInfo(HICON hIcon, ICONINFO* piconinfo) {
    if (!hIcon || !piconinfo) return FALSE;
    IconDesc desc;
    {
        std::lock_guard<std::mutex> lk(g_iconMutex);
        auto it = g_icons.find(hIcon);
        if (it == g_icons.end()) desc = { 2, 32 };
        else desc = it->second;
    }
    HBITMAP hbm = RenderProceduralIconDib(desc.iconIndex, desc.dim);
    if (!hbm) return FALSE;
    std::memset(piconinfo, 0, sizeof(*piconinfo));
    piconinfo->fIcon = TRUE;
    piconinfo->hbmColor = hbm;
    piconinfo->hbmMask = nullptr;
    return TRUE;
}
__attribute__((weak)) BOOL DrawIconEx(HDC, int, int, HICON, int, int, UINT, HBRUSH, UINT) { return FALSE; }

// Common Controls, Dialogs, Shell, Media stubs
__attribute__((weak)) BOOL SetWindowSubclass(HWND, SUBCLASSPROC, UINT_PTR, DWORD_PTR) { return TRUE; }
__attribute__((weak)) BOOL RemoveWindowSubclass(HWND, SUBCLASSPROC, UINT_PTR) { return TRUE; }
__attribute__((weak)) LRESULT DefSubclassProc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }
__attribute__((weak)) HICON ImageList_GetIcon(HIMAGELIST, int i, UINT) {
    std::lock_guard<std::mutex> lk(g_iconMutex);
    HICON h = reinterpret_cast<HICON>(++g_nextIconId);
    g_icons[h] = { i, 32 };
    return h;
}
__attribute__((weak)) int ImageList_GetImageCount(HIMAGELIST) { return 512; }
__attribute__((weak)) BOOL ImageList_GetIconSize(HIMAGELIST, int* cx, int* cy) { if (cx) *cx = 32; if (cy) *cy = 32; return TRUE; }
__attribute__((weak)) HRESULT SHGetImageList(int iImageList, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    int dim = 32;
    switch (iImageList) {
    case SHIL_SMALL: case SHIL_SYSSMALL: dim = 16; break;
    case SHIL_LARGE: dim = 32; break;
    case SHIL_EXTRALARGE: dim = 48; break;
    case SHIL_JUMBO: dim = 64; break; // 64px crisp vector-procedural icon scales cleanly in FitRectCentered
    default: dim = 32; break;
    }
    auto* il = new LinuxImageList(dim);
    HRESULT hr = il->QueryInterface(riid, ppv);
    il->Release();
    return hr;
}

__attribute__((weak)) BOOL GetOpenFileNameW(LPOPENFILENAMEW) { return FALSE; }
__attribute__((weak)) BOOL GetSaveFileNameW(LPOPENFILENAMEW) { return FALSE; }
__attribute__((weak)) DWORD CommDlgExtendedError() { return 0; }

__attribute__((weak)) DWORD_PTR SHGetFileInfoW(LPCWSTR pszPath, DWORD dwFileAttributes, SHFILEINFOW* psfi, UINT, UINT uFlags) {
    if (!psfi) return 0;
    std::memset(psfi, 0, sizeof(*psfi));
    const bool useAttrs = (uFlags & SHGFI_USEFILEATTRIBUTES) != 0;
    const bool isDir = (dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 || (!useAttrs && pszPath && PathIsDirectoryW(pszPath));
    psfi->iIcon = IconIndexForPathOrExt(pszPath, isDir);
    wcscpy_s(psfi->szTypeName, isDir ? L"Folder" : L"File");
    if (pszPath) {
        std::wstring nm = Explorer2::PathUtils::GetFileName(pszPath);
        wcscpy_s(psfi->szDisplayName, nm.c_str());
    }
    if (uFlags & SHGFI_ICON) {
        std::lock_guard<std::mutex> lk(g_iconMutex);
        HICON h = reinterpret_cast<HICON>(++g_nextIconId);
        g_icons[h] = { psfi->iIcon, (uFlags & SHGFI_SMALLICON) ? 16 : 32 };
        psfi->hIcon = h;
    }
    return 1;
}
__attribute__((weak)) HRESULT SHGetStockIconInfo(SHSTOCKICONID siid, UINT, SHSTOCKICONINFO* psii) {
    if (!psii) return E_POINTER;
    psii->iSysImageIndex = static_cast<int>(siid) + 1;
    psii->iIcon = psii->iSysImageIndex;
    return S_OK;
}
namespace {
// True for a file that is a program to run: an ELF binary or a script with a "#!" line. The execute bit alone
// says little, as every file on an NTFS, exFAT or SMB mount has it.
bool LooksLikeProgram(const std::string& native) {
    const int fd = ::open(native.c_str(), O_RDONLY | O_CLOEXEC | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return false;
    unsigned char head[4] = {};
    ssize_t got = 0;
    do {
        got = ::read(fd, head, sizeof(head));
    } while (got < 0 && errno == EINTR);
    ::close(fd);
    if (got >= 4 && head[0] == 0x7F && head[1] == 'E' && head[2] == 'L' && head[3] == 'F') return true;
    return got >= 2 && head[0] == '#' && head[1] == '!';
}

// A double-quoted argument for the command line CreateProcessW splits (the HAL's CommandLineToArgvW reads \" as a
// quote inside the argument)
std::wstring QuoteForCommandLine(const std::wstring& arg) {
    std::wstring out = L"\"";
    for (wchar_t c : arg) {
        if (c == L'"') out += L"\\\"";
        else out.push_back(c);
    }
    out.push_back(L'"');
    return out;
}

constexpr DWORD kErrorNoAssociation = 1155;     // ERROR_NO_ASSOCIATION
} // namespace

__attribute__((weak)) BOOL ShellExecuteExW(SHELLEXECUTEINFOW* pExecInfo) {
    if (!pExecInfo) return FALSE;
    pExecInfo->hProcess = nullptr;

    // There is no properties sheet here. Opening the item instead would run a program or open a document the
    // user only wanted to look at, so the verb fails as Windows fails a verb that nothing handles.
    if (pExecInfo->lpVerb && _wcsicmp(pExecInfo->lpVerb, L"properties") == 0) {
        SetLastError(kErrorNoAssociation);
        return FALSE;
    }

    std::wstring fileW;
    if (pExecInfo->lpFile && *pExecInfo->lpFile) {
        fileW = pExecInfo->lpFile;
    } else if (pExecInfo->lpIDList) {
        wchar_t idPath[MAX_PATH] = {};
        if (SHGetPathFromIDListW(static_cast<PCIDLIST_ABSOLUTE>(pExecInfo->lpIDList), idPath) && idPath[0]) {
            fileW = idPath;
        }
    }
    if (fileW.empty()) return FALSE;

    const std::wstring paramsW = (pExecInfo->lpParameters && *pExecInfo->lpParameters) ? pExecInfo->lpParameters : L"";
    const LPCWSTR cwdW = (pExecInfo->lpDirectory && *pExecInfo->lpDirectory) ? pExecInfo->lpDirectory : nullptr;

    auto spawnDirect = [&](const std::wstring& exePath, const std::wstring& rawArgs) -> BOOL {
        std::wstring cmdLine = QuoteForCommandLine(exePath);
        if (!rawArgs.empty()) cmdLine += L" " + rawArgs;
        std::vector<wchar_t> mutCmd(cmdLine.begin(), cmdLine.end());
        mutCmd.push_back(L'\0');
        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {};
        if (!CreateProcessW(exePath.c_str(), mutCmd.data(), nullptr, nullptr, FALSE, 0, nullptr, cwdW, &si, &pi)) {
            return FALSE;
        }
        if (pi.hThread) CloseHandle(pi.hThread);
        if (pExecInfo->fMask & SEE_MASK_NOCLOSEPROCESS) {
            pExecInfo->hProcess = pi.hProcess;
        } else if (pi.hProcess) {
            CloseHandle(pi.hProcess);
        }
        return TRUE;
    };

    auto findFirstExec = [](std::initializer_list<const char*> candidates) -> std::wstring {
        for (const char* c : candidates) {
            struct stat st = {};
            if (::stat(c, &st) == 0 && S_ISREG(st.st_mode) && ::access(c, X_OK) == 0) {
                return Explorer2::PathUtils::Utf8ToWide(c);
            }
        }
        return L"";
    };

    // Map common Win32 utility verbs/binaries used by ExplorerApp onto native Linux equivalents
    if (_wcsicmp(fileW.c_str(), L"tar.exe") == 0 || _wcsicmp(fileW.c_str(), L"tar") == 0) {
        const std::wstring tarBin = findFirstExec({ "/usr/bin/tar", "/bin/tar" });
        if (!tarBin.empty()) return spawnDirect(tarBin, paramsW);
    }
    if ((_wcsicmp(fileW.c_str(), L"cmd.exe") == 0 || _wcsicmp(fileW.c_str(), L"powershell.exe") == 0) && paramsW.empty()) {
        const std::wstring termBin = findFirstExec({
            "/usr/bin/x-terminal-emulator", "/usr/bin/gnome-terminal",
            "/usr/bin/konsole", "/usr/bin/xfce4-terminal", "/usr/bin/xterm" });
        if (!termBin.empty()) return spawnDirect(termBin, L"");
        return FALSE;
    }
    if (_wcsicmp(fileW.c_str(), L"notepad.exe") == 0 && !paramsW.empty()) {
        std::wstring doc = paramsW;
        if (doc.size() >= 2 && doc.front() == L'"' && doc.back() == L'"') doc = doc.substr(1, doc.size() - 2);
        fileW = doc;
    }

    // Run the file itself only when it is a program (an ELF binary or a "#!" script) with the execute bit; a
    // document, even one with that bit, goes to xdg-open like everything else
    const std::string nativeFile = Explorer2::Compat::NativePath(fileW.c_str());
    struct stat st = {};
    const bool exists = !nativeFile.empty() && ::stat(nativeFile.c_str(), &st) == 0;
    if (exists && S_ISREG(st.st_mode) && ::access(nativeFile.c_str(), X_OK) == 0 && LooksLikeProgram(nativeFile)) {
        if (spawnDirect(Explorer2::PathUtils::Utf8ToWide(nativeFile), paramsW)) return TRUE;
        // A program that cannot start here (another architecture, a missing interpreter) still gets xdg-open
    }

    // Folders, documents, URLs (http/https/mailto) -> xdg-open (without shell evaluation!)
    const std::wstring xdgOpen = findFirstExec({ "/usr/bin/xdg-open", "/bin/xdg-open" });
    if (xdgOpen.empty()) return FALSE;
    const std::wstring argTarget = exists ? Explorer2::PathUtils::Utf8ToWide(nativeFile) : fileW;
    return spawnDirect(xdgOpen, QuoteForCommandLine(argTarget));
}
__attribute__((weak)) HINSTANCE ShellExecuteW(HWND hwnd, LPCWSTR lpOperation, LPCWSTR lpFile, LPCWSTR lpParameters, LPCWSTR lpDirectory, int nShowCmd) {
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.hwnd = hwnd;
    sei.lpVerb = lpOperation;
    sei.lpFile = lpFile;
    sei.lpParameters = lpParameters;
    sei.lpDirectory = lpDirectory;
    sei.nShow = nShowCmd;
    return ShellExecuteExW(&sei) ? reinterpret_cast<HINSTANCE>(33) : reinterpret_cast<HINSTANCE>(2);
}
__attribute__((weak)) int SHFileOperationW(SHFILEOPSTRUCTW*) { return ERROR_NOT_SUPPORTED; }
__attribute__((weak)) UINT ExtractIconExW(LPCWSTR, int, HICON*, HICON*, UINT) { return 0; }
__attribute__((weak)) void DragAcceptFiles(HWND, BOOL) {}
__attribute__((weak)) UINT DragQueryFileW(HDROP hDrop, UINT iFile, LPWSTR lpszFile, UINT cch) {
    if (!hDrop) return 0;
    const auto* df = reinterpret_cast<const DROPFILES*>(GlobalLock(reinterpret_cast<HGLOBAL>(hDrop)));
    if (!df || !df->fWide) return 0;
    const auto* p = reinterpret_cast<const wchar_t*>(reinterpret_cast<const uint8_t*>(df) + df->pFiles);
    std::vector<std::wstring> files;
    while (*p) {
        std::wstring s(p);
        p += s.size() + 1;
        files.push_back(std::move(s));
    }
    if (iFile == 0xFFFFFFFFu) return static_cast<UINT>(files.size());
    if (iFile >= files.size()) return 0;
    const auto& f = files[iFile];
    if (!lpszFile) return static_cast<UINT>(f.size());
    const size_t n = std::min<size_t>(f.size(), cch > 0 ? cch - 1 : 0);
    if (cch > 0) {
        std::wmemcpy(lpszFile, f.data(), n);
        lpszFile[n] = L'\0';
    }
    return static_cast<UINT>(n);
}
__attribute__((weak)) BOOL DragQueryPoint(HDROP, LPPOINT p) { if (p) *p = {0,0}; return TRUE; }
__attribute__((weak)) void DragFinish(HDROP) {}

__attribute__((weak)) HRESULT SHQueryRecycleBinW(LPCWSTR, SHQUERYRBINFO* pSHQueryRBInfo) {
    if (!pSHQueryRBInfo) return E_POINTER;
    pSHQueryRBInfo->i64Size = 0;
    pSHQueryRBInfo->i64NumItems = 0;
    const std::string trashFiles = GetFreeDesktopTrashBase() + "/files";      // the trash the delete code fills
    if (DIR* d = ::opendir(trashFiles.c_str())) {
        while (struct dirent* de = ::readdir(d)) {
            if (std::strcmp(de->d_name, ".") == 0 || std::strcmp(de->d_name, "..") == 0) continue;
            ++pSHQueryRBInfo->i64NumItems;
            struct stat st = {};
            if (::lstat((trashFiles + "/" + de->d_name).c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
                pSHQueryRBInfo->i64Size += st.st_size;
            }
        }
        ::closedir(d);
    }
    return S_OK;
}

namespace {
// The names in an open folder, without "." and "..". Takes over the descriptor and closes it. The names are read
// before anything in the folder is removed, so no descriptor per level stays open while a deep tree is walked.
std::vector<std::string> ListFolderNamesAt(int dfd) {
    std::vector<std::string> names;
    DIR* d = ::fdopendir(dfd);
    if (!d) {
        ::close(dfd);
        return names;
    }
    while (struct dirent* de = ::readdir(d)) {
        if (std::strcmp(de->d_name, ".") == 0 || std::strcmp(de->d_name, "..") == 0) continue;
        names.emplace_back(de->d_name);
    }
    ::closedir(d);
    return names;
}

std::vector<std::string> ListFolderNames(const std::string& dir) {
    const int dfd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd < 0) return {};
    return ListFolderNamesAt(dfd);
}

// Removes `p` and everything below it without ever leaving the file system `dev` (the trash's). A folder that
// belongs to another file system, something mounted inside the trash, is never entered: it is only rmdir()-ed,
// which fails while it is mounted, so nothing on that file system is deleted. Symbolic links are removed, never
// followed. Returns 0 when `p` is gone, else the errno of the first failure.
int RemovePosixTree(const std::string& p, dev_t dev) {
    struct stat st = {};
    if (::lstat(p.c_str(), &st) != 0) return (errno == ENOENT) ? 0 : errno;
    if (!S_ISDIR(st.st_mode)) return (::unlink(p.c_str()) == 0 || errno == ENOENT) ? 0 : errno;
    int firstErr = 0;
    if (st.st_dev == dev) {
        // Opened without following a link and checked to be the folder lstat saw, so that a folder swapped for a
        // link in the meantime cannot lead the walk somewhere else
        const int dfd = ::open(p.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        struct stat dst = {};
        if (dfd >= 0 && ::fstat(dfd, &dst) == 0 && dst.st_dev == st.st_dev && dst.st_ino == st.st_ino) {
            for (const std::string& name : ListFolderNamesAt(dfd)) {
                const int e = RemovePosixTree(p + "/" + name, dev);
                if (e != 0 && firstErr == 0) firstErr = e;
            }
        } else if (dfd >= 0) {
            ::close(dfd);
        }
    }
    if (::rmdir(p.c_str()) == 0 || errno == ENOENT) return 0;
    return firstErr ? firstErr : errno;
}

// True when the system's language is Polish (LC_ALL, then LC_MESSAGES, then LANG, as the C library picks them)
bool SystemLocaleIsPolish() {
    for (const char* var : { "LC_ALL", "LC_MESSAGES", "LANG" }) {
        const char* v = std::getenv(var);
        if (v && *v) return (v[0] == 'p' || v[0] == 'P') && (v[1] == 'l' || v[1] == 'L');
    }
    return false;
}
} // namespace

__attribute__((weak)) HRESULT SHEmptyRecycleBinW(HWND hwnd, LPCWSTR, DWORD dwFlags) {
    const std::string base = GetFreeDesktopTrashBase();
    const std::string filesDir = base + "/files";
    const std::string infoDir = base + "/info";

    // Emptying deletes everything in the trash for good. Windows asks first unless told not to, and so does this.
    if (!(dwFlags & SHERB_NOCONFIRMATION)) {
        SHQUERYRBINFO info = {};
        info.cbSize = sizeof(info);
        SHQueryRecycleBinW(nullptr, &info);
        if (info.i64NumItems <= 0) return S_OK;
        const long long n = info.i64NumItems;
        std::wstring text, caption;
        if (SystemLocaleIsPolish()) {
            const long long ones = n % 10, tens = n % 100;
            const wchar_t* noun = (n == 1) ? L"element"
                                : (ones >= 2 && ones <= 4 && (tens < 12 || tens > 14)) ? L"elementy" : L"element\x00F3w";
            text = L"Czy na pewno trwale usun\x0105\x0107 " + std::to_wstring(n) + L" " + noun + L" z Kosza?";
            caption = L"Opr\x00F3\x017Cnij Kosz";
        } else {
            text = L"Are you sure you want to permanently delete " + std::to_wstring(n) + (n == 1 ? L" item" : L" items") +
                   L" from the Recycle Bin?";
            caption = L"Empty Recycle Bin";
        }
        if (MessageBoxW(hwnd, text.c_str(), caption.c_str(), MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
            return HRESULT_FROM_WIN32(ERROR_CANCELLED);
        }
    }

    DWORD firstErr = 0;
    struct stat stFiles = {};
    if (::lstat(filesDir.c_str(), &stFiles) == 0 && S_ISDIR(stFiles.st_mode)) {
        for (const std::string& name : ListFolderNames(filesDir)) {
            const int e = RemovePosixTree(filesDir + "/" + name, stFiles.st_dev);
            if (e == 0) {
                // The .trashinfo goes only after its item: an item that stays keeps what it needs to be restored
                ::unlink((infoDir + "/" + name + ".trashinfo").c_str());
            } else if (firstErr == 0) {
                const DWORD w = Explorer2::Compat::Win32ErrorFromErrno(e);
                firstErr = w ? w : static_cast<DWORD>(ERROR_ACCESS_DENIED);
            }
        }
    }
    // A .trashinfo whose item is gone already describes nothing. Anything else in info/ is not ours to delete.
    static const char kInfoSuffix[] = ".trashinfo";
    const size_t suffixLen = sizeof(kInfoSuffix) - 1;
    for (const std::string& name : ListFolderNames(infoDir)) {
        if (name.size() <= suffixLen || name.compare(name.size() - suffixLen, suffixLen, kInfoSuffix) != 0) continue;
        struct stat st = {};
        const std::string item = filesDir + "/" + name.substr(0, name.size() - suffixLen);
        if (::lstat(item.c_str(), &st) != 0 && errno == ENOENT) ::unlink((infoDir + "/" + name).c_str());
    }
    return firstErr ? HRESULT_FROM_WIN32(firstErr) : S_OK;
}

__attribute__((weak)) HRESULT SHGetSpecialFolderLocation(HWND, int, PIDLIST_ABSOLUTE* ppidl) { if (ppidl) *ppidl = nullptr; return E_NOTIMPL; }
__attribute__((weak)) HRESULT SHParseDisplayName(LPCWSTR, void*, PIDLIST_ABSOLUTE* ppidl, ULONG, ULONG*) { if (ppidl) *ppidl = nullptr; return E_NOTIMPL; }
__attribute__((weak)) PIDLIST_ABSOLUTE ILCreateFromPathW(LPCWSTR) { return nullptr; }
__attribute__((weak)) PIDLIST_ABSOLUTE ILClone(PCIDLIST_ABSOLUTE) { return nullptr; }
__attribute__((weak)) BOOL ILRemoveLastID(LPITEMIDLIST) { return FALSE; }
__attribute__((weak)) BOOL ILIsParent(PCIDLIST_ABSOLUTE, PCIDLIST_ABSOLUTE, BOOL) { return FALSE; }
__attribute__((weak)) PCUITEMID_CHILD ILFindLastID(PCIDLIST_ABSOLUTE) { return nullptr; }
__attribute__((weak)) void ILFree(LPITEMIDLIST p) { CoTaskMemFree(p); }
__attribute__((weak)) HRESULT SHCreateDataObject(PCIDLIST_ABSOLUTE, UINT, PCUITEMID_CHILD_ARRAY, IDataObject*, REFIID, void** ppv) { if (ppv) *ppv = nullptr; return E_NOTIMPL; }
__attribute__((weak)) HRESULT SHDoDragDrop(HWND, IDataObject*, IDropSource*, DWORD, DWORD* pdwEffect) { if (pdwEffect) *pdwEffect = DROPEFFECT_NONE; return DRAGDROP_S_CANCEL; }
__attribute__((weak)) PIDLIST_ABSOLUTE SHBrowseForFolderW(BROWSEINFOW*) { return nullptr; }
__attribute__((weak)) BOOL SHGetPathFromIDListW(PCIDLIST_ABSOLUTE, LPWSTR) { return FALSE; }
__attribute__((weak)) BOOL IsUserAnAdmin() { return ::geteuid() == 0 ? TRUE : FALSE; }
__attribute__((weak)) HRESULT SHCreateItemFromParsingName(LPCWSTR pszPath, void*, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (!pszPath || !*pszPath) return E_INVALIDARG;
    auto* item = new LinuxShellItem(pszPath);
    HRESULT hr = item->QueryInterface(riid, ppv);
    item->Release();
    return hr;
}
__attribute__((weak)) HRESULT SHGetDesktopFolder(IShellFolder** ppshf) { if (ppshf) *ppshf = nullptr; return E_NOTIMPL; }
__attribute__((weak)) HRESULT SHBindToParent(PCIDLIST_ABSOLUTE, REFIID, void** ppv, PCUITEMID_CHILD*) { if (ppv) *ppv = nullptr; return E_NOTIMPL; }

__attribute__((weak)) HRESULT StrRetToBufW(STRRET* pstr, const ITEMIDLIST*, LPWSTR pszBuf, UINT cchBuf) {
    if (!pszBuf || cchBuf == 0) return E_INVALIDARG;
    pszBuf[0] = L'\0';
    if (!pstr) return E_INVALIDARG;
    if (pstr->uType == 0 && pstr->pOleStr) {
        wcsncpy_s(pszBuf, cchBuf, pstr->pOleStr, _TRUNCATE);
        CoTaskMemFree(pstr->pOleStr);
        pstr->pOleStr = nullptr;
        return S_OK;
    }
    return E_NOTIMPL;
}
__attribute__((weak)) HRESULT SHLoadIndirectString(LPCWSTR pszSource, LPWSTR pszOutBuf, UINT cchOutBuf, void**) {
    if (!pszSource || !pszOutBuf || cchOutBuf == 0) return E_INVALIDARG;
    wcsncpy_s(pszOutBuf, cchOutBuf, pszSource, _TRUNCATE);
    return S_OK;
}
__attribute__((weak)) HRESULT SHAutoComplete(HWND, DWORD) { return S_OK; }

__attribute__((weak)) HRESULT MFStartup(ULONG, DWORD) { return E_NOTIMPL; }
__attribute__((weak)) HRESULT MFShutdown() { return S_OK; }
__attribute__((weak)) HRESULT MFCreateAttributes(IMFAttributes** pp, UINT32) { if (pp) *pp = nullptr; return E_NOTIMPL; }
__attribute__((weak)) HRESULT MFCreateMFByteStreamOnStream(IStream*, IMFByteStream** pp) { if (pp) *pp = nullptr; return E_NOTIMPL; }
