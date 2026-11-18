#pragma once
// PlatformCompat.h - the parts of <windows.h> and of the Microsoft C library that the file readers use.
//
// Explorer2 is a Windows program, but its file readers (src/formats, the *Readers.cpp files, ImageFormatLoader.cpp
// and the codec helpers) also build on Linux: linux/ turns them into a command-line runner, so that they can be
// fuzzed with the sanitizers there and checked by a second compiler.
//
// On Windows this header is `#include <windows.h>` (with NOMINMAX, so that std::min and std::max keep working).
// Elsewhere it defines the Windows types, constants and functions those readers call, with the meaning they have
// on Windows:
//   - swprintf_s / sprintf_s follow Microsoft's format rules (in a wide format %s is a wide string and %S a narrow
//     one; %I64d is a 64-bit number): the format is translated for the C library, which reads %s as narrow;
//   - MultiByteToWideChar / WideCharToMultiByte convert with iconv. CP_ACP, the "ANSI code page" Windows uses for
//     legacy 8-bit text, is Windows-1252 here; CP_OEMCP is code page 437;
//   - wchar_t is 32 bits (UTF-32) instead of 16 (UTF-16).
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else

#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
#ifndef _LARGEFILE64_SOURCE
#define _LARGEFILE64_SOURCE 1
#endif

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <map>
#include <mutex>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/xattr.h>
#include <unistd.h>
#include <iconv.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

// ---- Types ----------------------------------------------------------------------------------------------------
using BYTE = uint8_t;
using UCHAR = unsigned char;
using CHAR = char;
using WORD = uint16_t;
using USHORT = uint16_t;
using SHORT = int16_t;
using DWORD = uint32_t;
using UINT = unsigned int;
using INT = int;
using LONG = int32_t;
using ULONG = uint32_t;
using BOOL = int;
using LONGLONG = int64_t;
using ULONGLONG = uint64_t;
using DWORD64 = uint64_t;
using INT8 = int8_t;
using INT16 = int16_t;
using INT32 = int32_t;
using INT64 = int64_t;
using UINT8 = uint8_t;
using UINT16 = uint16_t;
using UINT32 = uint32_t;
using UINT64 = uint64_t;
using SIZE_T = size_t;
using FLOAT = float;
using WCHAR = wchar_t;
using LPCWSTR = const wchar_t*;
using LPWSTR = wchar_t*;
using LPCSTR = const char*;
using LPSTR = char*;
using LPCCH = const char*;
using LPVOID = void*;
using LPCVOID = const void*;
using HANDLE = void*;
using HRESULT = int32_t;
using UINT_PTR = uintptr_t;
using INT_PTR = intptr_t;
using ULONG_PTR = uintptr_t;
using LONG_PTR = intptr_t;
using WPARAM = uintptr_t;
using LPARAM = intptr_t;
struct HWND__;
using HWND = HWND__*;

struct FILETIME {
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
};

struct SYSTEMTIME {
    WORD wYear;
    WORD wMonth;
    WORD wDayOfWeek;
    WORD wDay;
    WORD wHour;
    WORD wMinute;
    WORD wSecond;
    WORD wMilliseconds;
};

// ---- Constants ------------------------------------------------------------------------------------------------
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef MAX_PATH
#define MAX_PATH 260
#endif
#define CP_ACP 0
#define CP_OEMCP 1
#define CP_UTF7 65000
#define CP_UTF8 65001
#define MB_PRECOMPOSED 0x00000001
#define MB_ERR_INVALID_CHARS 0x00000008
#define WC_ERR_INVALID_CHARS 0x00000080
#define WC_NO_BEST_FIT_CHARS 0x00000400
#define S_OK (static_cast<HRESULT>(0))
#define S_FALSE (static_cast<HRESULT>(1))
#define E_FAIL (static_cast<HRESULT>(0x80004005u))
#define SUCCEEDED(hr) (static_cast<HRESULT>(hr) >= 0)
#define FAILED(hr) (static_cast<HRESULT>(hr) < 0)

namespace Explorer2::Compat {

// ---- Microsoft printf formats -> C library formats ------------------------------------------------------------
// Rewrites one format string. Wide formats: %s / %c are wide and %S / %C narrow (Microsoft) - the C library wants
// %ls / %lc for wide and %s / %c for narrow. Both: %I64x -> %llx, %I32x -> %x, %Ix -> %zx.
template <typename Ch>
std::basic_string<Ch> TranslateMsFormat(const Ch* fmt, bool wideFormat) {
    std::basic_string<Ch> out;
    if (!fmt) return out;
    for (const Ch* p = fmt; *p; ) {
        if (*p != Ch('%')) { out.push_back(*p++); continue; }
        out.push_back(*p++);
        if (*p == Ch('%')) { out.push_back(*p++); continue; }
        // flags, width, precision
        while (*p && (*p == Ch('-') || *p == Ch('+') || *p == Ch(' ') || *p == Ch('#') || *p == Ch('0') || *p == Ch('\''))) out.push_back(*p++);
        while (*p && ((*p >= Ch('0') && *p <= Ch('9')) || *p == Ch('*'))) out.push_back(*p++);
        if (*p == Ch('.')) {
            out.push_back(*p++);
            while (*p && ((*p >= Ch('0') && *p <= Ch('9')) || *p == Ch('*'))) out.push_back(*p++);
        }
        // length
        std::basic_string<Ch> len;
        if (*p == Ch('I')) {
            if (p[1] == Ch('6') && p[2] == Ch('4')) { len = { Ch('l'), Ch('l') }; p += 3; }
            else if (p[1] == Ch('3') && p[2] == Ch('2')) { p += 3; }
            else { len = { Ch('z') }; p += 1; }
        } else {
            while (*p && (*p == Ch('h') || *p == Ch('l') || *p == Ch('L') || *p == Ch('z') || *p == Ch('j') ||
                          *p == Ch('t') || *p == Ch('q') || *p == Ch('w'))) {
                len.push_back(*p == Ch('w') ? Ch('l') : *p);
                ++p;
            }
        }
        if (!*p) { out += len; break; }
        const Ch conv = *p++;
        if (wideFormat && (conv == Ch('s') || conv == Ch('c') || conv == Ch('S') || conv == Ch('C'))) {
            const bool upper = (conv == Ch('S') || conv == Ch('C'));
            const Ch base = (conv == Ch('S') || conv == Ch('s')) ? Ch('s') : Ch('c');
            bool wide;
            if (len.empty()) wide = !upper;                 // %s wide, %S narrow
            else wide = (len[0] == Ch('l'));                // %ls / %ws wide, %hs narrow
            if (wide) out.push_back(Ch('l'));
            out.push_back(base);
        } else if (!wideFormat && (conv == Ch('S') || conv == Ch('C'))) {
            out.push_back(Ch('l'));                         // %S in a narrow format is a wide string
            out.push_back(conv == Ch('S') ? Ch('s') : Ch('c'));
        } else {
            out += len;
            out.push_back(conv);
        }
    }
    return out;
}

inline int VSwprintfMs(wchar_t* buffer, size_t count, const wchar_t* format, va_list args) {
    if (!buffer || count == 0) return -1;
    const std::wstring fmt = TranslateMsFormat(format, true);
    const int n = vswprintf(buffer, count, fmt.c_str(), args);
    if (n < 0) {
        buffer[0] = L'\0';   // too long for the buffer (Microsoft's _s functions also leave an empty string)
        return -1;
    }
    return n;
}

inline int VSprintfMs(char* buffer, size_t count, const char* format, va_list args) {
    if (!buffer || count == 0) return -1;
    const std::string fmt = TranslateMsFormat(format, false);
    const int n = vsnprintf(buffer, count, fmt.c_str(), args);
    if (n < 0 || static_cast<size_t>(n) >= count) {
        buffer[0] = '\0';
        return -1;
    }
    return n;
}

// ---- Code pages -----------------------------------------------------------------------------------------------
inline const char* IconvNameForCodePage(UINT cp) {
    switch (cp) {
    case CP_ACP: return "CP1252";
    case CP_OEMCP: return "CP437";
    case 437: return "CP437";
    case 737: return "CP737";
    case 775: return "CP775";
    case 850: return "CP850";
    case 852: return "CP852";
    case 855: return "CP855";
    case 857: return "CP857";
    case 860: return "CP860";
    case 861: return "CP861";
    case 862: return "CP862";
    case 863: return "CP863";
    case 864: return "CP864";
    case 865: return "CP865";
    case 866: return "CP866";
    case 869: return "CP869";
    case 874: return "CP874";
    case 932: return "CP932";
    case 936: return "CP936";
    case 949: return "CP949";
    case 950: return "CP950";
    case 1250: return "CP1250";
    case 1251: return "CP1251";
    case 1252: return "CP1252";
    case 1253: return "CP1253";
    case 1254: return "CP1254";
    case 1255: return "CP1255";
    case 1256: return "CP1256";
    case 1257: return "CP1257";
    case 1258: return "CP1258";
    case 1361: return "JOHAB";
    case 10000: return "MACINTOSH";
    case 10007: return "MAC-CYRILLIC";
    case 20127: return "ASCII";
    case 20866: return "KOI8-R";
    case 20932: return "EUC-JP";
    case 21866: return "KOI8-U";
    case 28591: return "ISO-8859-1";
    case 28592: return "ISO-8859-2";
    case 28593: return "ISO-8859-3";
    case 28594: return "ISO-8859-4";
    case 28595: return "ISO-8859-5";
    case 28596: return "ISO-8859-6";
    case 28597: return "ISO-8859-7";
    case 28598: return "ISO-8859-8";
    case 28599: return "ISO-8859-9";
    case 28603: return "ISO-8859-13";
    case 28605: return "ISO-8859-15";
    case 50220: return "ISO-2022-JP";
    case 51932: return "EUC-JP";
    case 51936: return "EUC-CN";
    case 51949: return "EUC-KR";
    case 52936: return "HZ-GB-2312";
    case 54936: return "GB18030";
    case CP_UTF7: return "UTF-7";
    case CP_UTF8: return "UTF-8";
    default: return nullptr;
    }
}

// Decodes UTF-8 into code points. Invalid bytes become U+FFFD, or stop the decoding (returns false) when strict.
inline bool DecodeUtf8(const char* src, size_t n, std::wstring& out, bool strict) {
    const unsigned char* s = reinterpret_cast<const unsigned char*>(src);
    size_t i = 0;
    while (i < n) {
        const unsigned char c = s[i];
        if (c < 0x80u) { out.push_back(static_cast<wchar_t>(c)); ++i; continue; }
        size_t len = 0;
        uint32_t cp = 0, minCp = 0;
        if (c >= 0xC2u && c <= 0xDFu) { len = 2; cp = c & 0x1Fu; minCp = 0x80u; }
        else if (c >= 0xE0u && c <= 0xEFu) { len = 3; cp = c & 0x0Fu; minCp = 0x800u; }
        else if (c >= 0xF0u && c <= 0xF4u) { len = 4; cp = c & 0x07u; minCp = 0x10000u; }
        bool ok = (len != 0 && i + len <= n);
        for (size_t k = 1; ok && k < len; ++k) {
            const unsigned char cc = s[i + k];
            if ((cc & 0xC0u) != 0x80u) ok = false;
            else cp = (cp << 6) | (cc & 0x3Fu);
        }
        if (ok && (cp < minCp || cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu))) ok = false;
        if (!ok) {
            if (strict) return false;
            out.push_back(static_cast<wchar_t>(0xFFFD));
            ++i;
            continue;
        }
        out.push_back(static_cast<wchar_t>(cp));
        i += len;
    }
    return true;
}

// Encodes code points (or UTF-16 surrogate pairs, which some readers build) as UTF-8
inline void EncodeUtf8(const wchar_t* src, size_t n, std::string& out) {
    for (size_t i = 0; i < n; ++i) {
        uint32_t cp = static_cast<uint32_t>(src[i]);
        if (cp >= 0xD800u && cp <= 0xDBFFu && i + 1 < n) {
            const uint32_t lo = static_cast<uint32_t>(src[i + 1]);
            if (lo >= 0xDC00u && lo <= 0xDFFFu) {
                cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
                ++i;
            }
        }
        if (cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) cp = 0xFFFDu;
        if (cp < 0x80u) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800u) {
            out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else if (cp < 0x10000u) {
            out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else {
            out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        }
    }
}

inline const char* IconvFallbackAlias(const char* code) {
    if (!code) return nullptr;
    if (std::strcmp(code, "CP932") == 0) return "SHIFT_JIS";
    if (std::strcmp(code, "CP936") == 0) return "GBK";
    if (std::strcmp(code, "CP949") == 0) return "EUC-KR";
    if (std::strcmp(code, "CP950") == 0) return "BIG5";
    if (std::strcmp(code, "EUC-CN") == 0) return "GB2312";
    return nullptr;
}

// Converts with iconv. Bytes the code page has no character for become U+FFFD (or '?' when encoding), or stop
// the conversion (returns false) when strict.
inline bool IconvConvert(const char* fromCode, const char* toCode, const char* src, size_t n,
                         std::string& outBytes, bool strict, const char* replacement, size_t replacementLen,
                         size_t skipUnit) {
    iconv_t cd = iconv_open(toCode, fromCode);
    if (cd == reinterpret_cast<iconv_t>(-1)) {
        const char* altFrom = IconvFallbackAlias(fromCode);
        const char* altTo   = IconvFallbackAlias(toCode);
        if (altFrom || altTo) {
            cd = iconv_open(altTo ? altTo : toCode, altFrom ? altFrom : fromCode);
        }
    }
    if (cd == reinterpret_cast<iconv_t>(-1)) return false;
    char* in = const_cast<char*>(src);
    size_t inLeft = n;
    char buf[4096];
    bool ok = true;
    while (inLeft > 0) {
        char* outp = buf;
        size_t outLeft = sizeof(buf);
        const size_t r = iconv(cd, &in, &inLeft, &outp, &outLeft);
        outBytes.append(buf, static_cast<size_t>(outp - buf));
        if (r == static_cast<size_t>(-1)) {
            if (errno == E2BIG) continue;
            if (strict) { ok = false; break; }
            // EILSEQ / EINVAL: an unconvertible or cut-off character
            outBytes.append(replacement, replacementLen);
            const size_t skip = (inLeft < skipUnit) ? inLeft : skipUnit;
            in += skip;
            inLeft -= skip;
        }
    }
    iconv_close(cd);
    return ok;
}

inline bool DecodeCodePage(UINT cp, const char* src, size_t n, std::wstring& out, bool strict) {
    if (cp == CP_UTF8) return DecodeUtf8(src, n, out, strict);
    const char* name = IconvNameForCodePage(cp);
    if (!name) return false;
    std::string raw;
    const wchar_t fffd = static_cast<wchar_t>(0xFFFD);
    if (!IconvConvert(name, "WCHAR_T", src, n, raw, strict, reinterpret_cast<const char*>(&fffd), sizeof(fffd), 1)) return false;
    const size_t count = raw.size() / sizeof(wchar_t);
    const size_t start = out.size();
    out.resize(start + count);
    std::memcpy(&out[start], raw.data(), count * sizeof(wchar_t));
    return true;
}

inline bool EncodeCodePage(UINT cp, const wchar_t* src, size_t n, std::string& out) {
    if (cp == CP_UTF8) {
        EncodeUtf8(src, n, out);
        return true;
    }
    const char* name = IconvNameForCodePage(cp);
    if (!name) return false;
    return IconvConvert("WCHAR_T", name, reinterpret_cast<const char*>(src), n * sizeof(wchar_t), out, false, "?", 1,
                        sizeof(wchar_t));
}

} // namespace Explorer2::Compat

// ---- Windows functions ----------------------------------------------------------------------------------------
// MultiByteToWideChar(cp, flags, src, srcLen (-1: up to and with the terminating 0), dst, dstLen (0: size query))
inline int MultiByteToWideChar(UINT codePage, DWORD flags, const char* src, int srcLen, wchar_t* dst, int dstLen) {
    if (!src || srcLen == 0 || dstLen < 0) return 0;
    const size_t n = (srcLen < 0) ? std::strlen(src) + 1 : static_cast<size_t>(srcLen);
    std::wstring w;
    if (!Explorer2::Compat::DecodeCodePage(codePage, src, n, w, (flags & MB_ERR_INVALID_CHARS) != 0)) return 0;
    if (dstLen == 0) return static_cast<int>(w.size());
    if (w.size() > static_cast<size_t>(dstLen)) return 0;
    if (!w.empty()) std::memcpy(dst, w.data(), w.size() * sizeof(wchar_t));
    return static_cast<int>(w.size());
}

inline int WideCharToMultiByte(UINT codePage, DWORD /*flags*/, const wchar_t* src, int srcLen, char* dst, int dstLen,
                               const char* /*defaultChar*/, BOOL* usedDefaultChar) {
    if (usedDefaultChar) *usedDefaultChar = FALSE;
    if (!src || srcLen == 0 || dstLen < 0) return 0;
    const size_t n = (srcLen < 0) ? std::wcslen(src) + 1 : static_cast<size_t>(srcLen);
    std::string s;
    if (!Explorer2::Compat::EncodeCodePage(codePage, src, n, s)) return 0;
    if (dstLen == 0) return static_cast<int>(s.size());
    if (s.size() > static_cast<size_t>(dstLen)) return 0;
    if (!s.empty()) std::memcpy(dst, s.data(), s.size());
    return static_cast<int>(s.size());
}

inline ULONGLONG GetTickCount64() {
    return static_cast<ULONGLONG>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
inline DWORD GetTickCount() { return static_cast<DWORD>(GetTickCount64()); }
inline void Sleep(DWORD ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// ---- Microsoft C library --------------------------------------------------------------------------------------
inline int swprintf_s(wchar_t* buffer, size_t count, const wchar_t* format, ...) {
    va_list args;
    va_start(args, format);
    const int n = Explorer2::Compat::VSwprintfMs(buffer, count, format, args);
    va_end(args);
    return n;
}
template <size_t N>
inline int swprintf_s(wchar_t (&buffer)[N], const wchar_t* format, ...) {
    va_list args;
    va_start(args, format);
    const int n = Explorer2::Compat::VSwprintfMs(buffer, N, format, args);
    va_end(args);
    return n;
}
inline int _snwprintf_s(wchar_t* buffer, size_t count, size_t /*maxCount*/, const wchar_t* format, ...) {
    va_list args;
    va_start(args, format);
    const int n = Explorer2::Compat::VSwprintfMs(buffer, count, format, args);
    va_end(args);
    return n;
}
inline int sprintf_s(char* buffer, size_t count, const char* format, ...) {
    va_list args;
    va_start(args, format);
    const int n = Explorer2::Compat::VSprintfMs(buffer, count, format, args);
    va_end(args);
    return n;
}
template <size_t N>
inline int sprintf_s(char (&buffer)[N], const char* format, ...) {
    va_list args;
    va_start(args, format);
    const int n = Explorer2::Compat::VSprintfMs(buffer, N, format, args);
    va_end(args);
    return n;
}
inline int _snprintf_s(char* buffer, size_t count, size_t /*maxCount*/, const char* format, ...) {
    va_list args;
    va_start(args, format);
    const int n = Explorer2::Compat::VSprintfMs(buffer, count, format, args);
    va_end(args);
    return n;
}

inline int _wcsicmp(const wchar_t* a, const wchar_t* b) { return wcscasecmp(a, b); }
inline int _wcsnicmp(const wchar_t* a, const wchar_t* b, size_t n) { return wcsncasecmp(a, b, n); }
inline int _stricmp(const char* a, const char* b) { return strcasecmp(a, b); }
inline int _strnicmp(const char* a, const char* b, size_t n) { return strncasecmp(a, b, n); }
inline int _wtoi(const wchar_t* s) { return static_cast<int>(std::wcstol(s, nullptr, 10)); }
inline long _wtol(const wchar_t* s) { return std::wcstol(s, nullptr, 10); }
inline long long _wtoi64(const wchar_t* s) { return std::wcstoll(s, nullptr, 10); }
inline double _wtof(const wchar_t* s) { return std::wcstod(s, nullptr); }
inline long long _atoi64(const char* s) { return std::strtoll(s, nullptr, 10); }

inline int wcscpy_s(wchar_t* dst, size_t count, const wchar_t* src) {
    if (!dst || count == 0) return EINVAL;
    const size_t n = src ? std::wcslen(src) : 0;
    if (n >= count) { dst[0] = L'\0'; return ERANGE; }
    if (n) std::wmemcpy(dst, src, n);
    dst[n] = L'\0';
    return 0;
}
template <size_t N>
inline int wcscpy_s(wchar_t (&dst)[N], const wchar_t* src) { return wcscpy_s(dst, N, src); }
inline int strcpy_s(char* dst, size_t count, const char* src) {
    if (!dst || count == 0) return EINVAL;
    const size_t n = src ? std::strlen(src) : 0;
    if (n >= count) { dst[0] = '\0'; return ERANGE; }
    if (n) std::memcpy(dst, src, n);
    dst[n] = '\0';
    return 0;
}
template <size_t N>
inline int strcpy_s(char (&dst)[N], const char* src) { return strcpy_s(dst, N, src); }

#ifndef _countof
#define _countof(a) (sizeof(a) / sizeof((a)[0]))
#endif

// sscanf_s / swscanf_s: after the pointer of every %s, %c and %[ comes its buffer size. The size becomes the
// field width (size - 1 for strings; %c reads one character unless a width is given) and is dropped from the
// arguments; wide formats also get Microsoft's %s = wide rule (see TranslateMsFormat).
namespace Explorer2::Compat {
struct ScanArg {
    bool isPointer = false;
    void* pointer = nullptr;
    unsigned long long size = 0;
};
template <typename T>
inline ScanArg MakeScanArg(T* p) { return { true, const_cast<void*>(static_cast<const void*>(p)), 0 }; }
template <typename T, typename std::enable_if<std::is_integral<T>::value, int>::type = 0>
inline ScanArg MakeScanArg(T n) { return { false, nullptr, static_cast<unsigned long long>(n) }; }

template <typename Ch>
int ScanfMs(const Ch* input, const Ch* format, const ScanArg* args, size_t argCount) {
    constexpr size_t kMaxPointers = 40;
    void* ptrs[kMaxPointers] = {};
    size_t np = 0, ai = 0;
    std::basic_string<Ch> out;
    const bool wide = std::is_same<Ch, wchar_t>::value;
    for (const Ch* p = format; p && *p; ) {
        if (*p != Ch('%')) { out.push_back(*p++); continue; }
        out.push_back(*p++);
        if (*p == Ch('%')) { out.push_back(*p++); continue; }
        bool suppress = false;
        if (*p == Ch('*')) { suppress = true; out.push_back(*p++); }
        std::basic_string<Ch> width;
        while (*p >= Ch('0') && *p <= Ch('9')) width.push_back(*p++);
        std::basic_string<Ch> len;
        if (*p == Ch('I') && p[1] == Ch('6') && p[2] == Ch('4')) { len = { Ch('l'), Ch('l') }; p += 3; }
        else if (*p == Ch('I') && p[1] == Ch('3') && p[2] == Ch('2')) { p += 3; }
        else {
            while (*p == Ch('h') || *p == Ch('l') || *p == Ch('L') || *p == Ch('z') || *p == Ch('j') || *p == Ch('t') || *p == Ch('w')) {
                len.push_back(*p == Ch('w') ? Ch('l') : *p);
                ++p;
            }
        }
        if (!*p) break;
        Ch conv = *p++;
        std::basic_string<Ch> set;
        if (conv == Ch('[')) {
            set.push_back(conv);
            if (*p == Ch('^')) set.push_back(*p++);
            if (*p == Ch(']')) set.push_back(*p++);
            while (*p && *p != Ch(']')) set.push_back(*p++);
            if (*p == Ch(']')) set.push_back(*p++);
        }
        const bool textConv = (conv == Ch('s') || conv == Ch('c') || conv == Ch('[') || conv == Ch('S') || conv == Ch('C'));
        if (!suppress && conv != Ch('n') && ai < argCount && args[ai].isPointer && np < kMaxPointers) {
            ptrs[np++] = args[ai++].pointer;
        } else if (!suppress && conv == Ch('n') && ai < argCount && args[ai].isPointer && np < kMaxPointers) {
            ptrs[np++] = args[ai++].pointer;
        } else if (!suppress) {
            return EOF;   // the arguments do not match the format
        }
        if (textConv && !suppress) {
            unsigned long long size = 0;
            if (ai < argCount && !args[ai].isPointer) size = args[ai++].size;
            if (size == 0) return EOF;
            const bool charConv = (conv == Ch('c') || conv == Ch('C'));
            if (width.empty()) {
                const unsigned long long w = charConv ? 1ull : size - 1;
                if (w == 0) return EOF;
                for (const char ch : std::to_string(w)) width.push_back(Ch(ch));
            }
        }
        out += width;
        if (wide && (conv == Ch('s') || conv == Ch('c') || conv == Ch('S') || conv == Ch('C') || conv == Ch('['))) {
            const bool upper = (conv == Ch('S') || conv == Ch('C'));
            const bool wideTarget = len.empty() ? !upper : (len[0] == Ch('l'));
            if (wideTarget) out.push_back(Ch('l'));
            if (conv == Ch('[')) out += set;
            else out.push_back((conv == Ch('S') || conv == Ch('s')) ? Ch('s') : Ch('c'));
        } else if (conv == Ch('[')) {
            out += len;
            out += set;
        } else {
            out += len;
            out.push_back(conv);
        }
    }
    void* const* q = ptrs;
    if constexpr (std::is_same<Ch, wchar_t>::value) {
        return std::swscanf(input, out.c_str(), q[0], q[1], q[2], q[3], q[4], q[5], q[6], q[7], q[8], q[9], q[10], q[11],
                            q[12], q[13], q[14], q[15], q[16], q[17], q[18], q[19], q[20], q[21], q[22], q[23], q[24],
                            q[25], q[26], q[27], q[28], q[29], q[30], q[31], q[32], q[33], q[34], q[35], q[36], q[37],
                            q[38], q[39]);
    } else {
        return std::sscanf(input, out.c_str(), q[0], q[1], q[2], q[3], q[4], q[5], q[6], q[7], q[8], q[9], q[10], q[11],
                           q[12], q[13], q[14], q[15], q[16], q[17], q[18], q[19], q[20], q[21], q[22], q[23], q[24],
                           q[25], q[26], q[27], q[28], q[29], q[30], q[31], q[32], q[33], q[34], q[35], q[36], q[37],
                           q[38], q[39]);
    }
}
} // namespace Explorer2::Compat

template <typename... Args>
inline int sscanf_s(const char* input, const char* format, Args... args) {
    const Explorer2::Compat::ScanArg list[] = { Explorer2::Compat::MakeScanArg(args)..., {} };
    return Explorer2::Compat::ScanfMs(input, format, list, sizeof...(Args));
}
template <typename... Args>
inline int swscanf_s(const wchar_t* input, const wchar_t* format, Args... args) {
    const Explorer2::Compat::ScanArg list[] = { Explorer2::Compat::MakeScanArg(args)..., {} };
    return Explorer2::Compat::ScanfMs(input, format, list, sizeof...(Args));
}

#define _TRUNCATE (static_cast<size_t>(-1))
// strncpy_s / wcsncpy_s: at most `count` characters (_TRUNCATE: as many as fit), always terminated
template <typename Ch>
inline int NcpyS(Ch* dst, size_t dstSize, const Ch* src, size_t count) {
    if (!dst || dstSize == 0) return EINVAL;
    size_t n = 0;
    while (src && n < count && src[n]) ++n;
    if (n >= dstSize) {
        if (count != _TRUNCATE) { dst[0] = Ch(0); return ERANGE; }
        n = dstSize - 1;
    }
    for (size_t i = 0; i < n; ++i) dst[i] = src[i];
    dst[n] = Ch(0);
    return 0;
}
inline int strncpy_s(char* dst, size_t dstSize, const char* src, size_t count) { return NcpyS(dst, dstSize, src, count); }
template <size_t N>
inline int strncpy_s(char (&dst)[N], const char* src, size_t count) { return NcpyS(dst, N, src, count); }
inline int wcsncpy_s(wchar_t* dst, size_t dstSize, const wchar_t* src, size_t count) { return NcpyS(dst, dstSize, src, count); }
template <size_t N>
inline int wcsncpy_s(wchar_t (&dst)[N], const wchar_t* src, size_t count) { return NcpyS(dst, N, src, count); }

inline uint16_t _byteswap_ushort(uint16_t v) { return __builtin_bswap16(v); }
inline unsigned long _byteswap_ulong(unsigned long v) { return __builtin_bswap32(static_cast<uint32_t>(v)); }
inline uint64_t _byteswap_uint64(uint64_t v) { return __builtin_bswap64(v); }
inline unsigned int _rotl(unsigned int v, int s) { s &= 31; return s ? (v << s) | (v >> (32 - s)) : v; }
inline unsigned int _rotr(unsigned int v, int s) { s &= 31; return s ? (v >> s) | (v << (32 - s)) : v; }

// ---- Files: the CreateFileW / ReadFile subset the readers use, on top of stdio ---------------------------------
// Paths are the readers' Windows-style paths: UTF-16/32 text whose '\\' separators become '/'.
#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define FILE_SHARE_READ 0x00000001u
#define FILE_SHARE_WRITE 0x00000002u
#define FILE_SHARE_DELETE 0x00000004u
#define CREATE_NEW 1
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define TRUNCATE_EXISTING 5
#define FILE_ATTRIBUTE_READONLY 0x00000001u
#define FILE_ATTRIBUTE_HIDDEN 0x00000002u
#define FILE_ATTRIBUTE_SYSTEM 0x00000004u
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010u
#define FILE_ATTRIBUTE_ARCHIVE 0x00000020u
#define FILE_ATTRIBUTE_NORMAL 0x00000080u
#define FILE_ATTRIBUTE_TEMPORARY 0x00000100u
#define FILE_ATTRIBUTE_SPARSE_FILE 0x00000200u
#define FILE_ATTRIBUTE_REPARSE_POINT 0x00000400u
#define FILE_FLAG_OPEN_REPARSE_POINT 0x00200000u
#define FILE_FLAG_BACKUP_SEMANTICS 0x02000000u
#define FILE_FLAG_DELETE_ON_CLOSE 0x04000000u
#define FILE_FLAG_SEQUENTIAL_SCAN 0x08000000u
#define FILE_FLAG_RANDOM_ACCESS 0x10000000u
#define FILE_FLAG_NO_BUFFERING 0x20000000u
#define FILE_FLAG_OVERLAPPED 0x40000000u
#define FILE_FLAG_WRITE_THROUGH 0x80000000u
#define IO_REPARSE_TAG_MOUNT_POINT 0xA0000003u
#define IO_REPARSE_TAG_SYMLINK 0xA000000Cu
#define IsReparseTagNameSurrogate(_tag) (((_tag) & 0x20000000u) != 0)
#define FILE_BEGIN 0
#define FILE_CURRENT 1
#define FILE_END 2
#define INVALID_HANDLE_VALUE (reinterpret_cast<HANDLE>(static_cast<intptr_t>(-1)))
#define INVALID_FILE_ATTRIBUTES (static_cast<DWORD>(0xFFFFFFFFu))
#define INVALID_FILE_SIZE (static_cast<DWORD>(0xFFFFFFFFu))

union LARGE_INTEGER {
    __extension__ struct {
        DWORD LowPart;
        LONG HighPart;
    };
    struct {
        DWORD LowPart;
        LONG HighPart;
    } u;
    LONGLONG QuadPart;
};

union ULARGE_INTEGER {
    __extension__ struct {
        DWORD LowPart;
        DWORD HighPart;
    };
    struct {
        DWORD LowPart;
        DWORD HighPart;
    } u;
    ULONGLONG QuadPart;
};

namespace Explorer2::Compat {
inline std::wstring StripExtendedPrefix(const wchar_t* path) {
    if (!path) return {};
    std::wstring p(path);
    if (p.rfind(L"\\\\?\\UNC\\", 0) == 0 || p.rfind(L"//?/UNC/", 0) == 0) {
        p = L"\\\\" + p.substr(8);
    } else if (p.rfind(L"\\\\?\\", 0) == 0 || p.rfind(L"//?/", 0) == 0) {
        p = p.substr(4);
    }
    return p;
}

inline std::string NativePath(const wchar_t* path) {
    std::string out;
    if (!path) return out;
    std::wstring p = StripExtendedPrefix(path);
    for (wchar_t& ch : p) {
        if (ch == L'\\') ch = L'/';
    }
    EncodeUtf8(p.data(), p.size(), out);
    return out;
}

// Handles are small objects that say what they are, so that CloseHandle can close each kind
enum class HandleKind : uint32_t { File = 0x454C4946u, Mapping = 0x5050414Du, Find = 0x444E4946u, Custom = 0x43555354u };
struct HandleBase {
    explicit HandleBase(HandleKind k) : kind(k) {}
    virtual ~HandleBase() = default;
    virtual bool CloseCustom() { return true; }
    HandleKind kind;
};
struct FileHandle : HandleBase {
    FileHandle() : HandleBase(HandleKind::File) {}
    FILE* f = nullptr;
    int dirFd = -1;
    uint64_t dev = 0;
    uint64_t ino = 0;
    DWORD shareMode = 0;
    bool deleteOnClose = false;
    std::string nativePath;
};
struct MappingHandle : HandleBase {
    MappingHandle() : HandleBase(HandleKind::Mapping) {}
    int fd = -1;
    uint64_t size = 0;
};
inline HandleBase* BaseOf(HANDLE h) {
    return (h && h != INVALID_HANDLE_VALUE) ? static_cast<HandleBase*>(h) : nullptr;
}
inline FILE* FileOf(HANDLE h) {
    HandleBase* b = BaseOf(h);
    return (b && b->kind == HandleKind::File) ? static_cast<FileHandle*>(b)->f : nullptr;
}
inline int FdOf(HANDLE h) {
    HandleBase* b = BaseOf(h);
    if (!b || b->kind != HandleKind::File) return -1;
    auto* fh = static_cast<FileHandle*>(b);
    if (fh->dirFd >= 0) return fh->dirFd;
    return fh->f ? fileno(fh->f) : -1;
}

inline std::mutex& OpenFileMutex() {
    static std::mutex m;
    return m;
}
inline std::map<std::pair<uint64_t, uint64_t>, std::vector<FileHandle*>>& OpenFileMap() {
    static std::map<std::pair<uint64_t, uint64_t>, std::vector<FileHandle*>> m;
    return m;
}
inline void RegisterOpenFile(FileHandle* fh) {
    if (!fh || (fh->dev == 0 && fh->ino == 0)) return;
    std::lock_guard<std::mutex> lk(OpenFileMutex());
    OpenFileMap()[{ fh->dev, fh->ino }].push_back(fh);
}
inline void UnregisterOpenFile(FileHandle* fh) {
    if (!fh || (fh->dev == 0 && fh->ino == 0)) return;
    std::lock_guard<std::mutex> lk(OpenFileMutex());
    auto it = OpenFileMap().find({ fh->dev, fh->ino });
    if (it != OpenFileMap().end()) {
        auto& vec = it->second;
        vec.erase(std::remove(vec.begin(), vec.end(), fh), vec.end());
        if (vec.empty()) OpenFileMap().erase(it);
    }
}
inline bool IsDeleteBlockedByOpenHandle(uint64_t dev, uint64_t ino) {
    std::lock_guard<std::mutex> lk(OpenFileMutex());
    auto it = OpenFileMap().find({ dev, ino });
    if (it == OpenFileMap().end()) return false;
    for (const FileHandle* fh : it->second) {
        if ((fh->shareMode & FILE_SHARE_DELETE) == 0) return true;
    }
    return false;
}

// GetLastError: thread-local, set from errno by the functions below
inline DWORD& LastError() {
    thread_local DWORD error = 0;
    return error;
}
inline DWORD Win32ErrorFromErrno(int e) {
    switch (e) {
    case 0: return 0;
    case ENOENT: return 2;          // ERROR_FILE_NOT_FOUND
    case ENOTDIR: return 3;         // ERROR_PATH_NOT_FOUND
    case EACCES: case EPERM: case EROFS: return 5;   // ERROR_ACCESS_DENIED
    case EBADF: return 6;           // ERROR_INVALID_HANDLE
    case ENOMEM: return 8;          // ERROR_NOT_ENOUGH_MEMORY
    case EEXIST: return 183;        // ERROR_ALREADY_EXISTS
    case ENOTEMPTY: return 145;     // ERROR_DIR_NOT_EMPTY
    case ENOSPC: return 112;        // ERROR_DISK_FULL
    case EINVAL: return 87;         // ERROR_INVALID_PARAMETER
    case EBUSY: return 32;          // ERROR_SHARING_VIOLATION
    default: return 31;             // ERROR_GEN_FAILURE
    }
}
inline void SetErrorFromErrno() { LastError() = Win32ErrorFromErrno(errno); }

inline FILETIME FileTimeFromUnix(time_t secs) {
    const uint64_t ticks = static_cast<uint64_t>(static_cast<int64_t>(secs) + 11644473600LL) * 10000000ULL;
    FILETIME ft;
    ft.dwLowDateTime = static_cast<DWORD>(ticks & 0xFFFFFFFFu);
    ft.dwHighDateTime = static_cast<DWORD>(ticks >> 32);
    return ft;
}
inline FILETIME FileTimeFromTimespec(const struct timespec& ts) {
    const uint64_t ticks = static_cast<uint64_t>(static_cast<int64_t>(ts.tv_sec) + 11644473600LL) * 10000000ULL +
                           static_cast<uint64_t>(ts.tv_nsec / 100);
    FILETIME ft;
    ft.dwLowDateTime = static_cast<DWORD>(ticks & 0xFFFFFFFFu);
    ft.dwHighDateTime = static_cast<DWORD>(ticks >> 32);
    return ft;
}

inline std::mutex& CreationTimeMutex() {
    static std::mutex m;
    return m;
}
inline std::map<std::pair<uint64_t, uint64_t>, FILETIME>& CreationTimeMap() {
    static std::map<std::pair<uint64_t, uint64_t>, FILETIME> m;
    return m;
}
inline void SetInodeCreationTime(int fd, uint64_t dev, uint64_t ino, const FILETIME& ft) {
    if (ft.dwLowDateTime == 0 && ft.dwHighDateTime == 0) return;
    {
        std::lock_guard<std::mutex> lk(CreationTimeMutex());
        if (CreationTimeMap().size() > 8192) CreationTimeMap().clear();
        CreationTimeMap()[{ dev, ino }] = ft;
    }
    if (fd >= 0) {
        (void)::fsetxattr(fd, "user.e2_btime", &ft, sizeof(FILETIME), 0);
    }
}
inline FILETIME GetInodeCreationTime(const char* nativePath, const struct stat& st) {
    {
        std::lock_guard<std::mutex> lk(CreationTimeMutex());
        auto it = CreationTimeMap().find({ static_cast<uint64_t>(st.st_dev), static_cast<uint64_t>(st.st_ino) });
        if (it != CreationTimeMap().end()) return it->second;
    }
    if (nativePath) {
        FILETIME xft = {};
        if (::lgetxattr(nativePath, "user.e2_btime", &xft, sizeof(FILETIME)) == static_cast<ssize_t>(sizeof(FILETIME))) {
            return xft;
        }
    }
    return FileTimeFromTimespec(st.st_mtim);
}
} // namespace Explorer2::Compat

#define ERROR_SUCCESS 0L
#define ERROR_FILE_NOT_FOUND 2L
#define ERROR_PATH_NOT_FOUND 3L
#define ERROR_ACCESS_DENIED 5L
#define ERROR_INVALID_HANDLE 6L
#define ERROR_NOT_ENOUGH_MEMORY 8L
#define ERROR_NO_MORE_FILES 18L
#define ERROR_GEN_FAILURE 31L
#define ERROR_SHARING_VIOLATION 32L
#define ERROR_HANDLE_EOF 38L
#define ERROR_NOT_SUPPORTED 50L
#define ERROR_FILE_EXISTS 80L
#define ERROR_INVALID_PARAMETER 87L
#define ERROR_DISK_FULL 112L
#define ERROR_DIR_NOT_EMPTY 145L
#define ERROR_ALREADY_EXISTS 183L
#define INFINITE 0xFFFFFFFFu

inline DWORD GetLastError() { return Explorer2::Compat::LastError(); }
inline void SetLastError(DWORD e) { Explorer2::Compat::LastError() = e; }
inline DWORD GetCurrentProcessId() { return static_cast<DWORD>(::getpid()); }

inline HANDLE CreateFileW(const wchar_t* path, DWORD access, DWORD share, void* /*security*/, DWORD disposition,
                          DWORD flags, HANDLE /*templateFile*/) {
    const std::string native = Explorer2::Compat::NativePath(path);
    struct stat st = {};
    const bool exists = (::stat(native.c_str(), &st) == 0);
    if (exists && S_ISDIR(st.st_mode)) {
        if ((flags & FILE_FLAG_BACKUP_SEMANTICS) && disposition == OPEN_EXISTING) {
            const int dfd = ::open(native.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            if (dfd < 0) { Explorer2::Compat::SetErrorFromErrno(); return INVALID_HANDLE_VALUE; }
            auto* h = new Explorer2::Compat::FileHandle();
            h->dirFd = dfd;
            h->dev = static_cast<uint64_t>(st.st_dev);
            h->ino = static_cast<uint64_t>(st.st_ino);
            h->shareMode = share;
            h->nativePath = native;
            Explorer2::Compat::RegisterOpenFile(h);
            SetLastError(ERROR_SUCCESS);
            return static_cast<HANDLE>(static_cast<Explorer2::Compat::HandleBase*>(h));
        }
        SetLastError(ERROR_ACCESS_DENIED);
        return INVALID_HANDLE_VALUE;
    }
    const char* mode = nullptr;
    const bool wantWrite = (access & (GENERIC_WRITE | 0x0100u /*FILE_WRITE_ATTRIBUTES*/ | 0x0002u /*FILE_WRITE_DATA*/)) != 0;
    const bool wantRead = (access & GENERIC_READ) != 0 || !wantWrite;
    switch (disposition) {
    case OPEN_EXISTING:
        if (!exists) { SetLastError(ERROR_FILE_NOT_FOUND); return INVALID_HANDLE_VALUE; }
        mode = wantWrite ? "r+b" : "rb";
        break;
    case CREATE_ALWAYS:
        mode = wantRead && wantWrite ? "w+b" : "wb";
        break;
    case CREATE_NEW:
        if (exists) { SetLastError(ERROR_FILE_EXISTS); return INVALID_HANDLE_VALUE; }
        mode = wantRead && wantWrite ? "w+b" : "wb";
        break;
    case OPEN_ALWAYS:
        mode = exists ? (wantWrite ? "r+b" : "rb") : (wantRead && wantWrite ? "w+b" : "wb");
        break;
    case TRUNCATE_EXISTING:
        if (!exists) { SetLastError(ERROR_FILE_NOT_FOUND); return INVALID_HANDLE_VALUE; }
        mode = wantRead ? "w+b" : "wb";
        break;
    default:
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    FILE* f = std::fopen(native.c_str(), mode);
    if (!f && wantWrite && disposition == OPEN_EXISTING && (access & GENERIC_WRITE) == 0) {
        // Read-only file opened for FILE_WRITE_ATTRIBUTES only: O_RDONLY fd still allows futimens!
        f = std::fopen(native.c_str(), "rb");
    }
    if (!f) {
        Explorer2::Compat::SetErrorFromErrno();
        return INVALID_HANDLE_VALUE;
    }
    struct stat fst = {};
    ::fstat(fileno(f), &fst);
    auto* h = new Explorer2::Compat::FileHandle();
    h->f = f;
    h->dev = static_cast<uint64_t>(fst.st_dev);
    h->ino = static_cast<uint64_t>(fst.st_ino);
    h->shareMode = share;
    h->deleteOnClose = (flags & FILE_FLAG_DELETE_ON_CLOSE) != 0;
    h->nativePath = native;
    Explorer2::Compat::RegisterOpenFile(h);
    SetLastError((exists && (disposition == CREATE_ALWAYS || disposition == OPEN_ALWAYS)) ? ERROR_ALREADY_EXISTS : ERROR_SUCCESS);
    return static_cast<HANDLE>(static_cast<Explorer2::Compat::HandleBase*>(h));
}

inline BOOL ReadFile(HANDLE h, void* buffer, DWORD toRead, DWORD* read, void* /*overlapped*/) {
    if (read) *read = 0;
    FILE* f = Explorer2::Compat::FileOf(h);
    if (!f) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    const size_t got = toRead ? std::fread(buffer, 1, toRead, f) : 0;
    if (read) *read = static_cast<DWORD>(got);
    if (got != toRead && std::ferror(f)) { Explorer2::Compat::SetErrorFromErrno(); return FALSE; }
    return TRUE;
}

inline BOOL WriteFile(HANDLE h, const void* buffer, DWORD toWrite, DWORD* written, void* /*overlapped*/) {
    if (written) *written = 0;
    FILE* f = Explorer2::Compat::FileOf(h);
    if (!f) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    const size_t put = toWrite ? std::fwrite(buffer, 1, toWrite, f) : 0;
    if (written) *written = static_cast<DWORD>(put);
    if (put != toWrite) { Explorer2::Compat::SetErrorFromErrno(); return FALSE; }
    return TRUE;
}

inline BOOL GetFileSizeEx(HANDLE h, LARGE_INTEGER* size) {
    FILE* f = Explorer2::Compat::FileOf(h);
    struct stat st = {};
    if (!f || !size || ::fstat(fileno(f), &st) != 0) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    size->QuadPart = static_cast<LONGLONG>(st.st_size);
    return TRUE;
}

inline DWORD GetFileSize(HANDLE h, DWORD* high) {
    LARGE_INTEGER li = {};
    if (!GetFileSizeEx(h, &li)) return 0xFFFFFFFFu;
    if (high) *high = static_cast<DWORD>(static_cast<uint64_t>(li.QuadPart) >> 32);
    return static_cast<DWORD>(li.QuadPart & 0xFFFFFFFF);
}

inline BOOL SetFilePointerEx(HANDLE h, LARGE_INTEGER distance, LARGE_INTEGER* newPos, DWORD method) {
    FILE* f = Explorer2::Compat::FileOf(h);
    if (!f) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    const int whence = (method == FILE_CURRENT) ? SEEK_CUR : (method == FILE_END) ? SEEK_END : SEEK_SET;
    if (::fseeko(f, static_cast<off_t>(distance.QuadPart), whence) != 0) { Explorer2::Compat::SetErrorFromErrno(); return FALSE; }
    if (newPos) newPos->QuadPart = static_cast<LONGLONG>(::ftello(f));
    return TRUE;
}

inline BOOL GetFileTime(HANDLE h, FILETIME* creation, FILETIME* access, FILETIME* write) {
    const int fd = Explorer2::Compat::FdOf(h);
    struct stat st = {};
    if (fd < 0 || ::fstat(fd, &st) != 0) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    auto* fh = static_cast<Explorer2::Compat::FileHandle*>(Explorer2::Compat::BaseOf(h));
    if (creation) *creation = Explorer2::Compat::GetInodeCreationTime(fh ? fh->nativePath.c_str() : nullptr, st);
    if (access) *access = Explorer2::Compat::FileTimeFromTimespec(st.st_atim);
    if (write) *write = Explorer2::Compat::FileTimeFromTimespec(st.st_mtim);
    return TRUE;
}

inline BOOL SetFileTime(HANDLE h, const FILETIME* creation, const FILETIME* access, const FILETIME* write) {
    const int fd = Explorer2::Compat::FdOf(h);
    if (fd < 0) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    if (FILE* f = Explorer2::Compat::FileOf(h)) std::fflush(f);
    if (creation && (creation->dwLowDateTime != 0 || creation->dwHighDateTime != 0)) {
        struct stat st = {};
        if (::fstat(fd, &st) == 0) {
            Explorer2::Compat::SetInodeCreationTime(fd, static_cast<uint64_t>(st.st_dev), static_cast<uint64_t>(st.st_ino), *creation);
        }
    }
    if (!access && !write) return TRUE;
    struct timespec ts[2] = {};
    auto toSpec = [](const FILETIME* ft, timespec& out) {
        if (!ft || (ft->dwLowDateTime == 0 && ft->dwHighDateTime == 0)) {
            out.tv_nsec = UTIME_OMIT;
            return;
        }
        const uint64_t ticks = (static_cast<uint64_t>(ft->dwHighDateTime) << 32) | ft->dwLowDateTime;
        out.tv_sec = static_cast<time_t>(static_cast<int64_t>(ticks / 10000000ULL) - 11644473600LL);
        out.tv_nsec = static_cast<long>((ticks % 10000000ULL) * 100ULL);
    };
    toSpec(access, ts[0]);
    toSpec(write, ts[1]);
    if (::futimens(fd, ts) != 0) { Explorer2::Compat::SetErrorFromErrno(); return FALSE; }
    return TRUE;
}

inline BOOL SetEndOfFile(HANDLE h) {
    FILE* f = Explorer2::Compat::FileOf(h);
    if (!f) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    std::fflush(f);
    const off_t pos = ::ftello(f);
    if (pos < 0 || ::ftruncate(fileno(f), pos) != 0) { Explorer2::Compat::SetErrorFromErrno(); return FALSE; }
    return TRUE;
}

inline BOOL FlushFileBuffers(HANDLE h) {
    FILE* f = Explorer2::Compat::FileOf(h);
    if (!f) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    if (std::fflush(f) != 0) { Explorer2::Compat::SetErrorFromErrno(); return FALSE; }
    return TRUE;
}

namespace Explorer2::Compat {
inline bool IsSparseStat(const struct stat& st) {
    if (!S_ISREG(st.st_mode) || st.st_size < 65536) return false;
    const uint64_t logical = static_cast<uint64_t>(st.st_size);
    const uint64_t allocated = static_cast<uint64_t>(st.st_blocks) * 512ULL;
    return allocated + 65536ULL < logical;
}
} // namespace Explorer2::Compat

inline DWORD GetFileAttributesW(const wchar_t* path) {
    const std::string native = Explorer2::Compat::NativePath(path);
    struct stat lst = {};
    if (::lstat(native.c_str(), &lst) != 0) {
        Explorer2::Compat::SetErrorFromErrno();
        return INVALID_FILE_ATTRIBUTES;
    }
    DWORD attrs = 0;
    if (S_ISLNK(lst.st_mode)) {
        attrs |= FILE_ATTRIBUTE_REPARSE_POINT;
        struct stat tst = {};
        if (::stat(native.c_str(), &tst) == 0 && S_ISDIR(tst.st_mode)) attrs |= FILE_ATTRIBUTE_DIRECTORY;
    } else if (S_ISDIR(lst.st_mode)) {
        attrs |= FILE_ATTRIBUTE_DIRECTORY;
        if ((lst.st_mode & 0200) == 0) attrs |= FILE_ATTRIBUTE_READONLY;
    } else {
        if ((lst.st_mode & 0222) == 0) attrs |= FILE_ATTRIBUTE_READONLY;
        if (Explorer2::Compat::IsSparseStat(lst)) attrs |= FILE_ATTRIBUTE_SPARSE_FILE;
    }
    return attrs ? attrs : FILE_ATTRIBUTE_NORMAL;
}

inline BOOL SetFileAttributesW(const wchar_t* path, DWORD attrs) {
    const std::string native = Explorer2::Compat::NativePath(path);
    struct stat st = {};
    if (::lstat(native.c_str(), &st) != 0) {
        Explorer2::Compat::SetErrorFromErrno();
        return FALSE;
    }
    if (S_ISDIR(st.st_mode)) {
        if ((attrs & FILE_ATTRIBUTE_READONLY) == 0 && (st.st_mode & 0700) != 0700) {
            ::chmod(native.c_str(), st.st_mode | 0700);
        }
    } else if (!S_ISLNK(st.st_mode)) {
        mode_t mode = st.st_mode;
        if (attrs & FILE_ATTRIBUTE_READONLY) mode &= ~static_cast<mode_t>(0222);
        else mode |= static_cast<mode_t>(0200);
        if (mode != st.st_mode) ::chmod(native.c_str(), mode);
    }
    return TRUE;
}

inline BOOL CreateDirectoryW(const wchar_t* path, void* /*security*/) {
    if (::mkdir(Explorer2::Compat::NativePath(path).c_str(), 0777) != 0) { Explorer2::Compat::SetErrorFromErrno(); return FALSE; }
    return TRUE;
}
inline BOOL RemoveDirectoryW(const wchar_t* path) {
    const std::string native = Explorer2::Compat::NativePath(path);
    struct stat lst = {};
    if (::lstat(native.c_str(), &lst) == 0 && S_ISLNK(lst.st_mode)) {
        if (::unlink(native.c_str()) != 0) { Explorer2::Compat::SetErrorFromErrno(); return FALSE; }
        return TRUE;
    }
    if (::rmdir(native.c_str()) != 0) { Explorer2::Compat::SetErrorFromErrno(); return FALSE; }
    return TRUE;
}
inline BOOL DeleteFileW(const wchar_t* path) {
    const std::string native = Explorer2::Compat::NativePath(path);
    struct stat lst = {};
    if (::lstat(native.c_str(), &lst) != 0) { Explorer2::Compat::SetErrorFromErrno(); return FALSE; }
    if (!S_ISLNK(lst.st_mode) && Explorer2::Compat::IsDeleteBlockedByOpenHandle(static_cast<uint64_t>(lst.st_dev), static_cast<uint64_t>(lst.st_ino))) {
        SetLastError(ERROR_SHARING_VIOLATION);
        return FALSE;
    }
    if (::unlink(native.c_str()) != 0) { Explorer2::Compat::SetErrorFromErrno(); return FALSE; }
    return TRUE;
}

#define MOVEFILE_REPLACE_EXISTING 0x00000001u
#define MOVEFILE_COPY_ALLOWED 0x00000002u
inline BOOL MoveFileExW(const wchar_t* from, const wchar_t* to, DWORD flags) {
    const std::string a = Explorer2::Compat::NativePath(from);
    const std::string b = Explorer2::Compat::NativePath(to);
    struct stat stSrc = {};
    if (::lstat(a.c_str(), &stSrc) != 0) { Explorer2::Compat::SetErrorFromErrno(); return FALSE; }
    if (!S_ISLNK(stSrc.st_mode) && Explorer2::Compat::IsDeleteBlockedByOpenHandle(static_cast<uint64_t>(stSrc.st_dev), static_cast<uint64_t>(stSrc.st_ino))) {
        SetLastError(ERROR_SHARING_VIOLATION);
        return FALSE;
    }
    struct stat st = {};
    if (!(flags & MOVEFILE_REPLACE_EXISTING) && ::lstat(b.c_str(), &st) == 0) { SetLastError(ERROR_ALREADY_EXISTS); return FALSE; }
    if (::rename(a.c_str(), b.c_str()) == 0) return TRUE;
    if (errno != EXDEV || !(flags & MOVEFILE_COPY_ALLOWED)) { Explorer2::Compat::SetErrorFromErrno(); return FALSE; }
    // another file system: copy, then delete the original
    FILE* in = std::fopen(a.c_str(), "rb");
    if (!in) { Explorer2::Compat::SetErrorFromErrno(); return FALSE; }
    FILE* out = std::fopen(b.c_str(), "wb");
    if (!out) { Explorer2::Compat::SetErrorFromErrno(); std::fclose(in); return FALSE; }
    char buf[65536];
    bool ok = true;
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), in)) > 0) {
        if (std::fwrite(buf, 1, n, out) != n) { ok = false; break; }
    }
    if (std::ferror(in)) ok = false;
    std::fclose(in);
    if (std::fclose(out) != 0) ok = false;
    if (!ok) { Explorer2::Compat::SetErrorFromErrno(); ::unlink(b.c_str()); return FALSE; }
    ::unlink(a.c_str());
    return TRUE;
}

// GetTempPathW: $TMPDIR (else /tmp) with a trailing separator, as Windows gives it
inline DWORD GetTempPathW(DWORD bufferLength, wchar_t* buffer) {
    const char* env = std::getenv("TMPDIR");
    std::string dir = (env && *env) ? env : "/tmp";
    if (dir.back() != '/') dir.push_back('/');
    std::wstring w;
    Explorer2::Compat::DecodeUtf8(dir.data(), dir.size(), w, false);
    if (!buffer || bufferLength <= w.size()) return static_cast<DWORD>(w.size() + 1);
    std::wmemcpy(buffer, w.c_str(), w.size() + 1);
    return static_cast<DWORD>(w.size());
}

inline DWORD GetEnvironmentVariableW(const wchar_t* name, wchar_t* buffer, DWORD size) {
    std::string narrow;
    if (name) Explorer2::Compat::EncodeUtf8(name, std::wcslen(name), narrow);
    const char* value = std::getenv(narrow.c_str());
    if (!value) { SetLastError(203 /* ERROR_ENVVAR_NOT_FOUND */); return 0; }
    std::wstring w;
    Explorer2::Compat::DecodeUtf8(value, std::strlen(value), w, false);
    if (!buffer || size <= w.size()) return static_cast<DWORD>(w.size() + 1);
    std::wmemcpy(buffer, w.c_str(), w.size() + 1);
    return static_cast<DWORD>(w.size());
}

// No Windows system folder here: callers that look for tar.exe / 7z.exe there find nothing
inline UINT GetSystemDirectoryW(wchar_t* buffer, UINT size) {
    if (buffer && size) buffer[0] = L'\0';
    return 0;
}

// ---- Read-only file mapping on mmap ---------------------------------------------------------------------------
#define PAGE_READONLY 0x02u
#define FILE_MAP_READ 0x0004u
namespace Explorer2::Compat {
inline std::mutex& MapRegistryMutex() {
    static std::mutex m;
    return m;
}
inline std::map<const void*, size_t>& MapRegistry() {
    static std::map<const void*, size_t> m;
    return m;
}
} // namespace Explorer2::Compat

inline HANDLE CreateFileMappingW(HANDLE file, void* /*security*/, DWORD /*protect*/, DWORD maxHigh, DWORD maxLow, const wchar_t* /*name*/) {
    FILE* f = Explorer2::Compat::FileOf(file);
    struct stat st = {};
    if (!f || ::fstat(fileno(f), &st) != 0) { SetLastError(ERROR_INVALID_HANDLE); return nullptr; }
    const uint64_t wanted = (static_cast<uint64_t>(maxHigh) << 32) | maxLow;
    auto* m = new Explorer2::Compat::MappingHandle();
    m->size = wanted ? wanted : static_cast<uint64_t>(st.st_size);
    if (m->size == 0) { delete m; SetLastError(1006 /* ERROR_FILE_INVALID */); return nullptr; }
    m->fd = ::dup(fileno(f));
    if (m->fd < 0) { delete m; Explorer2::Compat::SetErrorFromErrno(); return nullptr; }
    return static_cast<HANDLE>(static_cast<Explorer2::Compat::HandleBase*>(m));
}

inline void* MapViewOfFile(HANDLE mapping, DWORD /*access*/, DWORD offsetHigh, DWORD offsetLow, SIZE_T bytes) {
    Explorer2::Compat::HandleBase* b = Explorer2::Compat::BaseOf(mapping);
    if (!b || b->kind != Explorer2::Compat::HandleKind::Mapping) { SetLastError(ERROR_INVALID_HANDLE); return nullptr; }
    auto* m = static_cast<Explorer2::Compat::MappingHandle*>(b);
    const uint64_t offset = (static_cast<uint64_t>(offsetHigh) << 32) | offsetLow;
    if (offset >= m->size) { SetLastError(ERROR_INVALID_PARAMETER); return nullptr; }
    const uint64_t rem = m->size - offset;
    if (!bytes && rem > static_cast<uint64_t>(~size_t(0))) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return nullptr; }
    const size_t len = bytes ? bytes : static_cast<size_t>(rem);
    void* p = ::mmap(nullptr, len, PROT_READ, MAP_PRIVATE, m->fd, static_cast<off_t>(offset));
    if (p == MAP_FAILED) { Explorer2::Compat::SetErrorFromErrno(); return nullptr; }
    std::lock_guard<std::mutex> lock(Explorer2::Compat::MapRegistryMutex());
    Explorer2::Compat::MapRegistry()[p] = len;
    return p;
}

inline BOOL UnmapViewOfFile(const void* p) {
    size_t len = 0;
    {
        std::lock_guard<std::mutex> lock(Explorer2::Compat::MapRegistryMutex());
        auto it = Explorer2::Compat::MapRegistry().find(p);
        if (it == Explorer2::Compat::MapRegistry().end()) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
        len = it->second;
        Explorer2::Compat::MapRegistry().erase(it);
    }
    return ::munmap(const_cast<void*>(p), len) == 0 ? TRUE : FALSE;
}

// FindFirstFileW / FindNextFileW / FindClose on opendir: "dir\\*.md5mesh"-style masks, matched like Windows does
// (case-insensitive, '*' and '?', "*.*" for everything)
struct WIN32_FIND_DATAW {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
    DWORD dwReserved0;
    DWORD dwReserved1;
    wchar_t cFileName[MAX_PATH];
    wchar_t cAlternateFileName[14];
};

namespace Explorer2::Compat {
struct FindState : HandleBase {
    FindState() : HandleBase(HandleKind::Find) {}
    DIR* dir = nullptr;
    std::string dirNative;
    std::wstring mask;
};

inline bool WildcardMatch(const wchar_t* mask, const wchar_t* name) {
    if (*mask == L'\0') return *name == L'\0';
    if (*mask == L'*') {
        while (*mask == L'*') ++mask;
        if (*mask == L'\0') return true;
        for (const wchar_t* n = name; *n; ++n) {
            if (WildcardMatch(mask, n)) return true;
        }
        return WildcardMatch(mask, name + std::wcslen(name));
    }
    if (*name == L'\0') return false;
    if (*mask == L'?' || std::towlower(*mask) == std::towlower(*name)) return WildcardMatch(mask + 1, name + 1);
    return false;
}

inline bool NextMatch(FindState& st, WIN32_FIND_DATAW* data) {
    while (dirent* e = ::readdir(st.dir)) {
        std::wstring name;
        DecodeUtf8(e->d_name, std::strlen(e->d_name), name, false);
        if (!WildcardMatch(st.mask.c_str(), name.c_str())) continue;
        std::memset(data, 0, sizeof(*data));
        const std::string full = st.dirNative + "/" + e->d_name;
        struct stat lst = {};
        if (::lstat(full.c_str(), &lst) == 0) {
            DWORD attrs = 0;
            uint64_t sz = static_cast<uint64_t>(lst.st_size);
            if (S_ISLNK(lst.st_mode)) {
                attrs |= FILE_ATTRIBUTE_REPARSE_POINT;
                data->dwReserved0 = IO_REPARSE_TAG_SYMLINK;
                struct stat tst = {};
                if (::stat(full.c_str(), &tst) == 0) {
                    if (S_ISDIR(tst.st_mode)) attrs |= FILE_ATTRIBUTE_DIRECTORY;
                    else sz = static_cast<uint64_t>(tst.st_size);
                }
            } else if (S_ISDIR(lst.st_mode)) {
                attrs |= FILE_ATTRIBUTE_DIRECTORY;
                if ((lst.st_mode & 0200) == 0) attrs |= FILE_ATTRIBUTE_READONLY;
            } else {
                if ((lst.st_mode & 0222) == 0) attrs |= FILE_ATTRIBUTE_READONLY;
                if (IsSparseStat(lst)) attrs |= FILE_ATTRIBUTE_SPARSE_FILE;
            }
            if (e->d_name[0] == '.' && std::strcmp(e->d_name, ".") != 0 && std::strcmp(e->d_name, "..") != 0) {
                attrs |= FILE_ATTRIBUTE_HIDDEN;
            }
            data->dwFileAttributes = attrs ? attrs : FILE_ATTRIBUTE_NORMAL;
            data->nFileSizeHigh = static_cast<DWORD>(sz >> 32);
            data->nFileSizeLow = static_cast<DWORD>(sz & 0xFFFFFFFFu);
            data->ftLastWriteTime = FileTimeFromTimespec(lst.st_mtim);
            data->ftCreationTime = GetInodeCreationTime(full.c_str(), lst);
            data->ftLastAccessTime = FileTimeFromTimespec(lst.st_atim);
        } else {
            data->dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
        }
        const size_t n = (name.size() < MAX_PATH - 1) ? name.size() : MAX_PATH - 1;
        std::wmemcpy(data->cFileName, name.data(), n);
        data->cFileName[n] = L'\0';
        return true;
    }
    return false;
}
} // namespace Explorer2::Compat

inline HANDLE FindFirstFileW(const wchar_t* pattern, WIN32_FIND_DATAW* data) {
    if (!pattern || !data) { SetLastError(ERROR_INVALID_PARAMETER); return INVALID_HANDLE_VALUE; }
    std::wstring p = Explorer2::Compat::StripExtendedPrefix(pattern);
    const size_t sep = p.find_last_of(L"\\/");
    std::wstring dir = (sep == std::wstring::npos) ? std::wstring(L".") : p.substr(0, sep);
    if (dir.empty()) dir = L"/";
    auto* st = new Explorer2::Compat::FindState();
    st->mask = (sep == std::wstring::npos) ? p : p.substr(sep + 1);
    if (st->mask == L"*.*") st->mask = L"*";
    st->dirNative = Explorer2::Compat::NativePath(dir.c_str());
    st->dir = ::opendir(st->dirNative.c_str());
    if (!st->dir || !Explorer2::Compat::NextMatch(*st, data)) {
        if (st->dir) {
            ::closedir(st->dir);
            SetLastError(ERROR_FILE_NOT_FOUND);
        } else {
            Explorer2::Compat::SetErrorFromErrno();
        }
        delete st;
        return INVALID_HANDLE_VALUE;
    }
    return static_cast<HANDLE>(static_cast<Explorer2::Compat::HandleBase*>(st));
}

inline BOOL FindNextFileW(HANDLE h, WIN32_FIND_DATAW* data) {
    Explorer2::Compat::HandleBase* b = Explorer2::Compat::BaseOf(h);
    if (!b || b->kind != Explorer2::Compat::HandleKind::Find || !data) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    if (Explorer2::Compat::NextMatch(*static_cast<Explorer2::Compat::FindState*>(b), data)) return TRUE;
    SetLastError(ERROR_NO_MORE_FILES);
    return FALSE;
}

inline BOOL FindClose(HANDLE h) {
    Explorer2::Compat::HandleBase* b = Explorer2::Compat::BaseOf(h);
    if (!b || b->kind != Explorer2::Compat::HandleKind::Find) return FALSE;
    auto* st = static_cast<Explorer2::Compat::FindState*>(b);
    if (st->dir) ::closedir(st->dir);
    delete st;
    return TRUE;
}

inline BOOL CloseHandle(HANDLE h) {
    Explorer2::Compat::HandleBase* b = Explorer2::Compat::BaseOf(h);
    if (!b) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    switch (b->kind) {
    case Explorer2::Compat::HandleKind::File: {
        auto* fh = static_cast<Explorer2::Compat::FileHandle*>(b);
        Explorer2::Compat::UnregisterOpenFile(fh);
        bool ok = true;
        if (fh->f) ok = (std::fclose(fh->f) == 0);
        if (fh->dirFd >= 0) ok = (::close(fh->dirFd) == 0) && ok;
        if (fh->deleteOnClose && !fh->nativePath.empty()) {
            (void)::unlink(fh->nativePath.c_str());
        }
        delete fh;
        return ok ? TRUE : FALSE;
    }
    case Explorer2::Compat::HandleKind::Mapping: {
        auto* m = static_cast<Explorer2::Compat::MappingHandle*>(b);
        if (m->fd >= 0) ::close(m->fd);
        delete m;
        return TRUE;
    }
    case Explorer2::Compat::HandleKind::Find:
        return FindClose(h);
    case Explorer2::Compat::HandleKind::Custom: {
        const bool ok = b->CloseCustom();
        delete b;
        return ok ? TRUE : FALSE;
    }
    }
    return FALSE;
}

// ---- Strings ----------------------------------------------------------------------------------------------------
#define CSTR_LESS_THAN 1
#define CSTR_EQUAL 2
#define CSTR_GREATER_THAN 3
// CompareStringOrdinal: code unit by code unit (upper-cased when ignoreCase), lengths of -1 meaning "up to the 0"
inline int CompareStringOrdinal(const wchar_t* a, int aLen, const wchar_t* b, int bLen, BOOL ignoreCase) {
    const size_t na = (aLen < 0) ? std::wcslen(a) : static_cast<size_t>(aLen);
    const size_t nb = (bLen < 0) ? std::wcslen(b) : static_cast<size_t>(bLen);
    const size_t n = (na < nb) ? na : nb;
    for (size_t i = 0; i < n; ++i) {
        uint32_t x = static_cast<uint32_t>(a[i]), y = static_cast<uint32_t>(b[i]);
        if (ignoreCase) {
            x = static_cast<uint32_t>(std::towupper(static_cast<wint_t>(x)));
            y = static_cast<uint32_t>(std::towupper(static_cast<wint_t>(y)));
        }
        if (x != y) return (x < y) ? CSTR_LESS_THAN : CSTR_GREATER_THAN;
    }
    return (na == nb) ? CSTR_EQUAL : (na < nb) ? CSTR_LESS_THAN : CSTR_GREATER_THAN;
}

// ---- shlwapi path parts ---------------------------------------------------------------------------------------
// PathFindExtensionW: the last '.' of the last path part unless a space follows it, else the terminating 0.
// '/' separates parts too.
inline wchar_t* PathFindExtensionW(const wchar_t* path) {
    if (!path) return nullptr;
    const wchar_t* dot = nullptr;
    const wchar_t* p = path;
    for (; *p; ++p) {
        if (*p == L'\\' || *p == L'/' || *p == L' ') dot = nullptr;
        else if (*p == L'.') dot = p;
    }
    return const_cast<wchar_t*>(dot ? dot : p);
}

// PathFindFileNameW: the last path part ("dir\\sub\\" gives "sub\\")
inline wchar_t* PathFindFileNameW(const wchar_t* path) {
    if (!path) return nullptr;
    const wchar_t* name = path;
    for (const wchar_t* p = path; *p; ++p) {
        if ((*p == L'\\' || *p == L'/' || *p == L':') && p[1] && p[1] != L'\\' && p[1] != L'/') name = p + 1;
    }
    return const_cast<wchar_t*>(name);
}

// ---- Times ----------------------------------------------------------------------------------------------------
// FILETIME = 100 ns ticks since 1601-01-01 UTC
inline BOOL DosDateTimeToFileTime(WORD dosDate, WORD dosTime, FILETIME* ft) {
    if (!ft) return FALSE;
    struct tm t = {};
    t.tm_year = ((dosDate >> 9) & 0x7F) + 80;
    t.tm_mon = ((dosDate >> 5) & 0x0F) - 1;
    t.tm_mday = dosDate & 0x1F;
    t.tm_hour = (dosTime >> 11) & 0x1F;
    t.tm_min = (dosTime >> 5) & 0x3F;
    t.tm_sec = (dosTime & 0x1F) * 2;
    if (t.tm_mon < 0 || t.tm_mon > 11 || t.tm_mday < 1 || t.tm_hour > 23 || t.tm_min > 59 || t.tm_sec > 59) return FALSE;
    const int64_t secs = static_cast<int64_t>(timegm(&t));   // the DOS fields as they are (a "local" FILETIME)
    const uint64_t ticks = static_cast<uint64_t>(secs + 11644473600LL) * 10000000ULL;
    ft->dwLowDateTime = static_cast<DWORD>(ticks & 0xFFFFFFFFu);
    ft->dwHighDateTime = static_cast<DWORD>(ticks >> 32);
    return TRUE;
}

inline BOOL FileTimeToSystemTime(const FILETIME* ft, SYSTEMTIME* st) {
    if (!ft || !st) return FALSE;
    const uint64_t ticks = (static_cast<uint64_t>(ft->dwHighDateTime) << 32) | ft->dwLowDateTime;
    if (ticks >= 0x8000000000000000ull) return FALSE;
    const time_t secs = static_cast<time_t>(static_cast<int64_t>(ticks / 10000000ULL) - 11644473600LL);
    struct tm t = {};
    if (!gmtime_r(&secs, &t)) return FALSE;
    st->wYear = static_cast<WORD>(t.tm_year + 1900);
    st->wMonth = static_cast<WORD>(t.tm_mon + 1);
    st->wDayOfWeek = static_cast<WORD>(t.tm_wday);
    st->wDay = static_cast<WORD>(t.tm_mday);
    st->wHour = static_cast<WORD>(t.tm_hour);
    st->wMinute = static_cast<WORD>(t.tm_min);
    st->wSecond = static_cast<WORD>(t.tm_sec);
    st->wMilliseconds = static_cast<WORD>((ticks / 10000ULL) % 1000ULL);
    return TRUE;
}

// Like Windows: shifts by the bias the local time zone has now (not the one it had at that date)
inline BOOL FileTimeToLocalFileTime(const FILETIME* utc, FILETIME* local) {
    if (!utc || !local) return FALSE;
    const time_t now = time(nullptr);
    struct tm parts = {};
    localtime_r(&now, &parts);
    const int64_t biasTicks = static_cast<int64_t>(parts.tm_gmtoff) * 10000000LL;
    const uint64_t ticks = (static_cast<uint64_t>(utc->dwHighDateTime) << 32) | utc->dwLowDateTime;
    const uint64_t out = static_cast<uint64_t>(static_cast<int64_t>(ticks) + biasTicks);
    local->dwLowDateTime = static_cast<DWORD>(out & 0xFFFFFFFFu);
    local->dwHighDateTime = static_cast<DWORD>(out >> 32);
    return TRUE;
}

// FILETIME (the time the fields should show, as given) -> MS-DOS date and time; 1980..2107 only, like Windows
inline BOOL FileTimeToDosDateTime(const FILETIME* ft, WORD* dosDate, WORD* dosTime) {
    if (!ft || !dosDate || !dosTime) return FALSE;
    const uint64_t ticks = (static_cast<uint64_t>(ft->dwHighDateTime) << 32) | ft->dwLowDateTime;
    const time_t secs = static_cast<time_t>(static_cast<int64_t>(ticks / 10000000ULL) - 11644473600LL);
    struct tm t = {};
    if (!gmtime_r(&secs, &t)) return FALSE;
    const int year = t.tm_year + 1900;
    if (year < 1980 || year > 2107) return FALSE;
    *dosDate = static_cast<WORD>(((year - 1980) << 9) | ((t.tm_mon + 1) << 5) | t.tm_mday);
    *dosTime = static_cast<WORD>((t.tm_hour << 11) | (t.tm_min << 5) | (t.tm_sec / 2));
    return TRUE;
}

inline LONG CompareFileTime(const FILETIME* a, const FILETIME* b) {
    const uint64_t x = (static_cast<uint64_t>(a->dwHighDateTime) << 32) | a->dwLowDateTime;
    const uint64_t y = (static_cast<uint64_t>(b->dwHighDateTime) << 32) | b->dwLowDateTime;
    return (x < y) ? -1 : (x > y) ? 1 : 0;
}

struct WIN32_FILE_ATTRIBUTE_DATA {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
};
enum GET_FILEEX_INFO_LEVELS { GetFileExInfoStandard = 0 };

inline BOOL GetFileAttributesExW(const wchar_t* path, GET_FILEEX_INFO_LEVELS, void* info) {
    const std::string native = Explorer2::Compat::NativePath(path);
    struct stat lst = {};
    if (!info || ::lstat(native.c_str(), &lst) != 0) { Explorer2::Compat::SetErrorFromErrno(); return FALSE; }
    auto* d = static_cast<WIN32_FILE_ATTRIBUTE_DATA*>(info);
    std::memset(d, 0, sizeof(*d));
    DWORD attrs = 0;
    uint64_t sz = static_cast<uint64_t>(lst.st_size);
    if (S_ISLNK(lst.st_mode)) {
        attrs |= FILE_ATTRIBUTE_REPARSE_POINT;
        struct stat tst = {};
        if (::stat(native.c_str(), &tst) == 0) {
            if (S_ISDIR(tst.st_mode)) attrs |= FILE_ATTRIBUTE_DIRECTORY;
            else sz = static_cast<uint64_t>(tst.st_size);
        }
    } else if (S_ISDIR(lst.st_mode)) {
        attrs |= FILE_ATTRIBUTE_DIRECTORY;
        if ((lst.st_mode & 0200) == 0) attrs |= FILE_ATTRIBUTE_READONLY;
    } else {
        if ((lst.st_mode & 0222) == 0) attrs |= FILE_ATTRIBUTE_READONLY;
        if (Explorer2::Compat::IsSparseStat(lst)) attrs |= FILE_ATTRIBUTE_SPARSE_FILE;
    }
    d->dwFileAttributes = attrs ? attrs : FILE_ATTRIBUTE_NORMAL;
    d->nFileSizeHigh = static_cast<DWORD>(sz >> 32);
    d->nFileSizeLow = static_cast<DWORD>(sz & 0xFFFFFFFFu);
    d->ftLastWriteTime = Explorer2::Compat::FileTimeFromTimespec(lst.st_mtim);
    d->ftCreationTime = Explorer2::Compat::GetInodeCreationTime(native.c_str(), lst);
    d->ftLastAccessTime = Explorer2::Compat::FileTimeFromTimespec(lst.st_atim);
    return TRUE;
}

// GetLocaleInfoW: only LOCALE_IDEFAULTANSICODEPAGE (the legacy code page of a language), as Windows maps it
using LCID = DWORD;
using LCTYPE = DWORD;
#define LOCALE_IDEFAULTANSICODEPAGE 0x00001004u
inline int GetLocaleInfoW(LCID lcid, LCTYPE type, wchar_t* data, int cch) {
    if (type != LOCALE_IDEFAULTANSICODEPAGE) return 0;
    const unsigned primary = lcid & 0x3FFu;
    const unsigned sub = (lcid >> 10) & 0x3Fu;
    unsigned cp = 1252;
    switch (primary) {
    case 0x05: case 0x0E: case 0x15: case 0x18: case 0x1B: case 0x1C: case 0x24: cp = 1250; break;
    case 0x1A: cp = (sub == 0x03 || sub == 0x07) ? 1251 : 1250; break;   // Serbian Cyrillic / Latin, Croatian
    case 0x02: case 0x19: case 0x22: case 0x23: case 0x2F: case 0x3F: case 0x40: case 0x44: case 0x50: cp = 1251; break;
    case 0x08: cp = 1253; break;
    case 0x1F: case 0x2C: case 0x43: cp = 1254; break;
    case 0x0D: cp = 1255; break;
    case 0x01: case 0x20: case 0x29: cp = 1256; break;
    case 0x25: case 0x26: case 0x27: cp = 1257; break;
    case 0x2A: cp = 1258; break;
    case 0x1E: cp = 874; break;
    case 0x11: cp = 932; break;
    case 0x04: cp = (sub == 0x02 || sub == 0x04) ? 936 : 950; break;
    case 0x12: cp = 949; break;
    default: break;
    }
    const std::wstring text = std::to_wstring(cp);
    if (cch == 0) return static_cast<int>(text.size() + 1);
    if (!data || cch < static_cast<int>(text.size() + 1)) return 0;
    std::wmemcpy(data, text.c_str(), text.size() + 1);
    return static_cast<int>(text.size() + 1);
}

inline BOOL LocalFileTimeToFileTime(const FILETIME* local, FILETIME* utc) {
    if (!local || !utc) return FALSE;
    const uint64_t ticks = (static_cast<uint64_t>(local->dwHighDateTime) << 32) | local->dwLowDateTime;
    const int64_t localSecs = static_cast<int64_t>(ticks / 10000000ULL) - 11644473600LL;
    time_t asLocal = static_cast<time_t>(localSecs);
    struct tm parts = {};
    gmtime_r(&asLocal, &parts);           // the wall-clock fields...
    parts.tm_isdst = -1;
    const time_t utcSecs = mktime(&parts);  // ...read as local time
    const uint64_t out = static_cast<uint64_t>(static_cast<int64_t>(utcSecs) + 11644473600LL) * 10000000ULL + ticks % 10000000ULL;
    utc->dwLowDateTime = static_cast<DWORD>(out & 0xFFFFFFFFu);
    utc->dwHighDateTime = static_cast<DWORD>(out >> 32);
    return TRUE;
}

inline int localtime_s(struct tm* out, const time_t* t) {
    if (!out || !t) return EINVAL;
    return localtime_r(t, out) ? 0 : EINVAL;
}

inline void GetSystemTimeAsFileTime(FILETIME* ft) {
    if (!ft) return;
    struct timespec ts = {};
    clock_gettime(CLOCK_REALTIME, &ts);
    const uint64_t ticks = static_cast<uint64_t>(static_cast<int64_t>(ts.tv_sec) + 11644473600LL) * 10000000ULL +
                           static_cast<uint64_t>(ts.tv_nsec / 100);
    ft->dwLowDateTime = static_cast<DWORD>(ticks & 0xFFFFFFFFu);
    ft->dwHighDateTime = static_cast<DWORD>(ticks >> 32);
}

inline void GetLocalTime(SYSTEMTIME* st) {
    if (!st) return;
    struct timespec ts = {};
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm t = {};
    localtime_r(&ts.tv_sec, &t);
    st->wYear = static_cast<WORD>(t.tm_year + 1900);
    st->wMonth = static_cast<WORD>(t.tm_mon + 1);
    st->wDayOfWeek = static_cast<WORD>(t.tm_wday);
    st->wDay = static_cast<WORD>(t.tm_mday);
    st->wHour = static_cast<WORD>(t.tm_hour);
    st->wMinute = static_cast<WORD>(t.tm_min);
    st->wSecond = static_cast<WORD>(t.tm_sec);
    st->wMilliseconds = static_cast<WORD>(ts.tv_nsec / 1000000L);
}

inline void GetSystemTime(SYSTEMTIME* st) {
    if (!st) return;
    struct timespec ts = {};
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm t = {};
    gmtime_r(&ts.tv_sec, &t);
    st->wYear = static_cast<WORD>(t.tm_year + 1900);
    st->wMonth = static_cast<WORD>(t.tm_mon + 1);
    st->wDayOfWeek = static_cast<WORD>(t.tm_wday);
    st->wDay = static_cast<WORD>(t.tm_mday);
    st->wHour = static_cast<WORD>(t.tm_hour);
    st->wMinute = static_cast<WORD>(t.tm_min);
    st->wSecond = static_cast<WORD>(t.tm_sec);
    st->wMilliseconds = static_cast<WORD>(ts.tv_nsec / 1000000L);
}

inline BOOL SystemTimeToFileTime(const SYSTEMTIME* st, FILETIME* ft) {
    if (!st || !ft) return FALSE;
    struct tm t = {};
    t.tm_year = static_cast<int>(st->wYear) - 1900;
    t.tm_mon = static_cast<int>(st->wMonth) - 1;
    t.tm_mday = static_cast<int>(st->wDay);
    t.tm_hour = static_cast<int>(st->wHour);
    t.tm_min = static_cast<int>(st->wMinute);
    t.tm_sec = static_cast<int>(st->wSecond);
    const time_t secs = timegm(&t);
    const uint64_t ticks = static_cast<uint64_t>(static_cast<int64_t>(secs) + 11644473600LL) * 10000000ULL +
                           static_cast<uint64_t>(st->wMilliseconds) * 10000ULL;
    ft->dwLowDateTime = static_cast<DWORD>(ticks & 0xFFFFFFFFu);
    ft->dwHighDateTime = static_cast<DWORD>(ticks >> 32);
    return TRUE;
}

inline BOOL SystemTimeToTzSpecificLocalTime(const void* /*tz*/, const SYSTEMTIME* utc, SYSTEMTIME* local) {
    if (!utc || !local) return FALSE;
    struct tm t = {};
    t.tm_year = static_cast<int>(utc->wYear) - 1900;
    t.tm_mon = static_cast<int>(utc->wMonth) - 1;
    t.tm_mday = static_cast<int>(utc->wDay);
    t.tm_hour = static_cast<int>(utc->wHour);
    t.tm_min = static_cast<int>(utc->wMinute);
    t.tm_sec = static_cast<int>(utc->wSecond);
    const time_t secs = timegm(&t);
    struct tm lt = {};
    if (!localtime_r(&secs, &lt)) return FALSE;
    local->wYear = static_cast<WORD>(lt.tm_year + 1900);
    local->wMonth = static_cast<WORD>(lt.tm_mon + 1);
    local->wDayOfWeek = static_cast<WORD>(lt.tm_wday);
    local->wDay = static_cast<WORD>(lt.tm_mday);
    local->wHour = static_cast<WORD>(lt.tm_hour);
    local->wMinute = static_cast<WORD>(lt.tm_min);
    local->wSecond = static_cast<WORD>(lt.tm_sec);
    local->wMilliseconds = utc->wMilliseconds;
    return TRUE;
}

inline DWORD GetCurrentThreadId() {
#ifdef SYS_gettid
    return static_cast<DWORD>(::syscall(SYS_gettid));
#else
    return static_cast<DWORD>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
#endif
}

inline DWORD GetCurrentDirectoryW(DWORD bufferLength, wchar_t* buffer) {
    char cwd[PATH_MAX] = {};
    if (!::getcwd(cwd, sizeof(cwd))) { Explorer2::Compat::SetErrorFromErrno(); return 0; }
    std::wstring w;
    Explorer2::Compat::DecodeUtf8(cwd, std::strlen(cwd), w, false);
    if (!buffer || bufferLength <= w.size()) return static_cast<DWORD>(w.size() + 1);
    std::wmemcpy(buffer, w.c_str(), w.size() + 1);
    return static_cast<DWORD>(w.size());
}

inline BOOL SetCurrentDirectoryW(const wchar_t* path) {
    if (!path || ::chdir(Explorer2::Compat::NativePath(path).c_str()) != 0) {
        Explorer2::Compat::SetErrorFromErrno();
        return FALSE;
    }
    return TRUE;
}

inline DWORD GetFullPathNameW(const wchar_t* fileName, DWORD bufferLength, wchar_t* buffer, wchar_t** filePart) {
    if (!fileName || !*fileName) { SetLastError(ERROR_INVALID_PARAMETER); return 0; }
    std::string native = Explorer2::Compat::NativePath(fileName);
    char resolved[PATH_MAX] = {};
    std::string full;
    if (::realpath(native.c_str(), resolved)) {
        full = resolved;
    } else if (!native.empty() && native[0] == '/') {
        full = native;
    } else {
        char cwd[PATH_MAX] = {};
        if (::getcwd(cwd, sizeof(cwd))) full = std::string(cwd) + "/" + native;
        else full = native;
    }
    std::wstring w;
    Explorer2::Compat::DecodeUtf8(full.data(), full.size(), w, false);
    if (!buffer || bufferLength <= w.size()) return static_cast<DWORD>(w.size() + 1);
    std::wmemcpy(buffer, w.c_str(), w.size() + 1);
    if (filePart) {
        wchar_t* slash = std::wcsrchr(buffer, L'/');
        *filePart = slash ? (slash + 1) : buffer;
    }
    return static_cast<DWORD>(w.size());
}

#endif // _WIN32
