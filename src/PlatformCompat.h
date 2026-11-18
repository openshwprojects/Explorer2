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
#include <sys/vfs.h>
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

// Built-in single-byte 0x80..0xFF tables for legacy DOS/Mac codepages omitted from static musl iconv
inline const uint16_t* FallbackSingleByteTable(UINT cp) {
    static constexpr uint16_t kSb_852[128] = {
        0x00C7u, 0x00FCu, 0x00E9u, 0x00E2u, 0x00E4u, 0x016Fu, 0x0107u, 0x00E7u, 0x0142u, 0x00EBu, 0x0150u, 0x0151u, 0x00EEu, 0x0179u, 0x00C4u, 0x0106u,
        0x00C9u, 0x0139u, 0x013Au, 0x00F4u, 0x00F6u, 0x013Du, 0x013Eu, 0x015Au, 0x015Bu, 0x00D6u, 0x00DCu, 0x0164u, 0x0165u, 0x0141u, 0x00D7u, 0x010Du,
        0x00E1u, 0x00EDu, 0x00F3u, 0x00FAu, 0x0104u, 0x0105u, 0x017Du, 0x017Eu, 0x0118u, 0x0119u, 0x00ACu, 0x017Au, 0x010Cu, 0x015Fu, 0x00ABu, 0x00BBu,
        0x2591u, 0x2592u, 0x2593u, 0x2502u, 0x2524u, 0x00C1u, 0x00C2u, 0x011Au, 0x015Eu, 0x2563u, 0x2551u, 0x2557u, 0x255Du, 0x017Bu, 0x017Cu, 0x2510u,
        0x2514u, 0x2534u, 0x252Cu, 0x251Cu, 0x2500u, 0x253Cu, 0x0102u, 0x0103u, 0x255Au, 0x2554u, 0x2569u, 0x2566u, 0x2560u, 0x2550u, 0x256Cu, 0x00A4u,
        0x0111u, 0x0110u, 0x010Eu, 0x00CBu, 0x010Fu, 0x0147u, 0x00CDu, 0x00CEu, 0x011Bu, 0x2518u, 0x250Cu, 0x2588u, 0x2584u, 0x0162u, 0x016Eu, 0x2580u,
        0x00D3u, 0x00DFu, 0x00D4u, 0x0143u, 0x0144u, 0x0148u, 0x0160u, 0x0161u, 0x0154u, 0x00DAu, 0x0155u, 0x0170u, 0x00FDu, 0x00DDu, 0x0163u, 0x00B4u,
        0x00ADu, 0x02DDu, 0x02DBu, 0x02C7u, 0x02D8u, 0x00A7u, 0x00F7u, 0x00B8u, 0x00B0u, 0x00A8u, 0x02D9u, 0x0171u, 0x0158u, 0x0159u, 0x25A0u, 0x00A0u,
    };
    static constexpr uint16_t kSb_874[128] = {
        0x20ACu, 0xFFFDu, 0xFFFDu, 0xFFFDu, 0xFFFDu, 0x2026u, 0xFFFDu, 0xFFFDu, 0xFFFDu, 0xFFFDu, 0xFFFDu, 0xFFFDu, 0xFFFDu, 0xFFFDu, 0xFFFDu, 0xFFFDu,
        0xFFFDu, 0x2018u, 0x2019u, 0x201Cu, 0x201Du, 0x2022u, 0x2013u, 0x2014u, 0xFFFDu, 0xFFFDu, 0xFFFDu, 0xFFFDu, 0xFFFDu, 0xFFFDu, 0xFFFDu, 0xFFFDu,
        0x00A0u, 0x0E01u, 0x0E02u, 0x0E03u, 0x0E04u, 0x0E05u, 0x0E06u, 0x0E07u, 0x0E08u, 0x0E09u, 0x0E0Au, 0x0E0Bu, 0x0E0Cu, 0x0E0Du, 0x0E0Eu, 0x0E0Fu,
        0x0E10u, 0x0E11u, 0x0E12u, 0x0E13u, 0x0E14u, 0x0E15u, 0x0E16u, 0x0E17u, 0x0E18u, 0x0E19u, 0x0E1Au, 0x0E1Bu, 0x0E1Cu, 0x0E1Du, 0x0E1Eu, 0x0E1Fu,
        0x0E20u, 0x0E21u, 0x0E22u, 0x0E23u, 0x0E24u, 0x0E25u, 0x0E26u, 0x0E27u, 0x0E28u, 0x0E29u, 0x0E2Au, 0x0E2Bu, 0x0E2Cu, 0x0E2Du, 0x0E2Eu, 0x0E2Fu,
        0x0E30u, 0x0E31u, 0x0E32u, 0x0E33u, 0x0E34u, 0x0E35u, 0x0E36u, 0x0E37u, 0x0E38u, 0x0E39u, 0x0E3Au, 0xFFFDu, 0xFFFDu, 0xFFFDu, 0xFFFDu, 0x0E3Fu,
        0x0E40u, 0x0E41u, 0x0E42u, 0x0E43u, 0x0E44u, 0x0E45u, 0x0E46u, 0x0E47u, 0x0E48u, 0x0E49u, 0x0E4Au, 0x0E4Bu, 0x0E4Cu, 0x0E4Du, 0x0E4Eu, 0x0E4Fu,
        0x0E50u, 0x0E51u, 0x0E52u, 0x0E53u, 0x0E54u, 0x0E55u, 0x0E56u, 0x0E57u, 0x0E58u, 0x0E59u, 0x0E5Au, 0x0E5Bu, 0xFFFDu, 0xFFFDu, 0xFFFDu, 0xFFFDu,
    };
    static constexpr uint16_t kSb_10000[128] = {
        0x00C4u, 0x00C5u, 0x00C7u, 0x00C9u, 0x00D1u, 0x00D6u, 0x00DCu, 0x00E1u, 0x00E0u, 0x00E2u, 0x00E4u, 0x00E3u, 0x00E5u, 0x00E7u, 0x00E9u, 0x00E8u,
        0x00EAu, 0x00EBu, 0x00EDu, 0x00ECu, 0x00EEu, 0x00EFu, 0x00F1u, 0x00F3u, 0x00F2u, 0x00F4u, 0x00F6u, 0x00F5u, 0x00FAu, 0x00F9u, 0x00FBu, 0x00FCu,
        0x2020u, 0x00B0u, 0x00A2u, 0x00A3u, 0x00A7u, 0x2022u, 0x00B6u, 0x00DFu, 0x00AEu, 0x00A9u, 0x2122u, 0x00B4u, 0x00A8u, 0x2260u, 0x00C6u, 0x00D8u,
        0x221Eu, 0x00B1u, 0x2264u, 0x2265u, 0x00A5u, 0x00B5u, 0x2202u, 0x2211u, 0x220Fu, 0x03C0u, 0x222Bu, 0x00AAu, 0x00BAu, 0x03A9u, 0x00E6u, 0x00F8u,
        0x00BFu, 0x00A1u, 0x00ACu, 0x221Au, 0x0192u, 0x2248u, 0x2206u, 0x00ABu, 0x00BBu, 0x2026u, 0x00A0u, 0x00C0u, 0x00C3u, 0x00D5u, 0x0152u, 0x0153u,
        0x2013u, 0x2014u, 0x201Cu, 0x201Du, 0x2018u, 0x2019u, 0x00F7u, 0x25CAu, 0x00FFu, 0x0178u, 0x2044u, 0x20ACu, 0x2039u, 0x203Au, 0xFB01u, 0xFB02u,
        0x2021u, 0x00B7u, 0x201Au, 0x201Eu, 0x2030u, 0x00C2u, 0x00CAu, 0x00C1u, 0x00CBu, 0x00C8u, 0x00CDu, 0x00CEu, 0x00CFu, 0x00CCu, 0x00D3u, 0x00D4u,
        0xF8FFu, 0x00D2u, 0x00DAu, 0x00DBu, 0x00D9u, 0x0131u, 0x02C6u, 0x02DCu, 0x00AFu, 0x02D8u, 0x02D9u, 0x02DAu, 0x00B8u, 0x02DDu, 0x02DBu, 0x02C7u,
    };
    static constexpr uint16_t kSb_10007[128] = {
        0x0410u, 0x0411u, 0x0412u, 0x0413u, 0x0414u, 0x0415u, 0x0416u, 0x0417u, 0x0418u, 0x0419u, 0x041Au, 0x041Bu, 0x041Cu, 0x041Du, 0x041Eu, 0x041Fu,
        0x0420u, 0x0421u, 0x0422u, 0x0423u, 0x0424u, 0x0425u, 0x0426u, 0x0427u, 0x0428u, 0x0429u, 0x042Au, 0x042Bu, 0x042Cu, 0x042Du, 0x042Eu, 0x042Fu,
        0x2020u, 0x00B0u, 0x0490u, 0x00A3u, 0x00A7u, 0x2022u, 0x00B6u, 0x0406u, 0x00AEu, 0x00A9u, 0x2122u, 0x0402u, 0x0452u, 0x2260u, 0x0403u, 0x0453u,
        0x221Eu, 0x00B1u, 0x2264u, 0x2265u, 0x0456u, 0x00B5u, 0x0491u, 0x0408u, 0x0404u, 0x0454u, 0x0407u, 0x0457u, 0x0409u, 0x0459u, 0x040Au, 0x045Au,
        0x0458u, 0x0405u, 0x00ACu, 0x221Au, 0x0192u, 0x2248u, 0x2206u, 0x00ABu, 0x00BBu, 0x2026u, 0x00A0u, 0x040Bu, 0x045Bu, 0x040Cu, 0x045Cu, 0x0455u,
        0x2013u, 0x2014u, 0x201Cu, 0x201Du, 0x2018u, 0x2019u, 0x00F7u, 0x201Eu, 0x040Eu, 0x045Eu, 0x040Fu, 0x045Fu, 0x2116u, 0x0401u, 0x0451u, 0x044Fu,
        0x0430u, 0x0431u, 0x0432u, 0x0433u, 0x0434u, 0x0435u, 0x0436u, 0x0437u, 0x0438u, 0x0439u, 0x043Au, 0x043Bu, 0x043Cu, 0x043Du, 0x043Eu, 0x043Fu,
        0x0440u, 0x0441u, 0x0442u, 0x0443u, 0x0444u, 0x0445u, 0x0446u, 0x0447u, 0x0448u, 0x0449u, 0x044Au, 0x044Bu, 0x044Cu, 0x044Du, 0x044Eu, 0x20ACu,
    };
    switch (cp) {
    case 852: return kSb_852;
    case 874: return kSb_874;
    case 10000: return kSb_10000;
    case 10007: return kSb_10007;
    default: return nullptr;
    }
}

inline bool DecodeCodePage(UINT cp, const char* src, size_t n, std::wstring& out, bool strict) {
    if (cp == CP_UTF8) return DecodeUtf8(src, n, out, strict);
    const char* name = IconvNameForCodePage(cp);
    if (!name) return false;
    std::string raw;
    const wchar_t fffd = static_cast<wchar_t>(0xFFFD);
    if (IconvConvert(name, "WCHAR_T", src, n, raw, strict, reinterpret_cast<const char*>(&fffd), sizeof(fffd), 1)) {
        const size_t count = raw.size() / sizeof(wchar_t);
        const size_t start = out.size();
        out.resize(start + count);
        std::memcpy(&out[start], raw.data(), count * sizeof(wchar_t));
        return true;
    }
    const uint16_t* tbl = FallbackSingleByteTable(cp);
    if (!tbl) return false;
    out.reserve(out.size() + n);
    for (size_t i = 0; i < n; ++i) {
        const uint8_t b = static_cast<uint8_t>(src[i]);
        if (b < 0x80u) {
            out.push_back(static_cast<wchar_t>(b));
        } else {
            const uint16_t u = tbl[b - 0x80u];
            if (u == 0xFFFDu && strict) return false;
            out.push_back(static_cast<wchar_t>(u));
        }
    }
    return true;
}

inline bool EncodeCodePage(UINT cp, const wchar_t* src, size_t n, std::string& out) {
    if (cp == CP_UTF8) {
        EncodeUtf8(src, n, out);
        return true;
    }
    const char* name = IconvNameForCodePage(cp);
    if (!name) return false;
    if (IconvConvert("WCHAR_T", name, reinterpret_cast<const char*>(src), n * sizeof(wchar_t), out, false, "?", 1,
                     sizeof(wchar_t))) {
        return true;
    }
    const uint16_t* tbl = FallbackSingleByteTable(cp);
    if (!tbl) return false;
    out.reserve(out.size() + n);
    for (size_t i = 0; i < n; ++i) {
        const uint32_t u = static_cast<uint32_t>(src[i]);
        if (u < 0x80u) {
            out.push_back(static_cast<char>(u));
        } else {
            char mapped = '?';
            for (int k = 0; k < 128; ++k) {
                if (tbl[k] == u) { mapped = static_cast<char>(0x80 + k); break; }
            }
            out.push_back(mapped);
        }
    }
    return true;
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
    int dirFd = -1;               // a descriptor without a stdio stream: a folder, or a symbolic link opened as itself
    uint64_t dev = 0;
    uint64_t ino = 0;
    DWORD shareMode = 0;
    bool deleteOnClose = false;
    bool canWrite = false;        // opened with a right to write, so the stdio buffer may hold data still to be written
    bool isStream = false;        // a pipe or a socket: ReadFile returns what has arrived instead of filling the buffer
    bool writeFailed = false;     // a write, or a later flush of buffered data, failed (FlushFileBuffers and CloseHandle say so)
    DWORD writeError = 0;         // the Win32 error of that first failure
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
inline FileHandle* FileHandleOf(HANDLE h) {
    HandleBase* b = BaseOf(h);
    return (b && b->kind == HandleKind::File) ? static_cast<FileHandle*>(b) : nullptr;
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
    case EISDIR: return 5;          // ERROR_ACCESS_DENIED, which Windows gives for a folder opened as a file
    case EBADF: return 6;           // ERROR_INVALID_HANDLE
    case ENOMEM: return 8;          // ERROR_NOT_ENOUGH_MEMORY
    case EXDEV: return 17;          // ERROR_NOT_SAME_DEVICE
    case EEXIST: return 183;        // ERROR_ALREADY_EXISTS
    case ENOTEMPTY: return 145;     // ERROR_DIR_NOT_EMPTY
    case ENOSPC: case EDQUOT: return 112;   // ERROR_DISK_FULL (a used-up quota is a full disk to the user)
    case EINVAL: return 87;         // ERROR_INVALID_PARAMETER
    case EBUSY: return 32;          // ERROR_SHARING_VIOLATION
    case EPIPE: return 109;         // ERROR_BROKEN_PIPE
    case ENAMETOOLONG: return 206;  // ERROR_FILENAME_EXCED_RANGE
    case ELOOP: return 1921;        // ERROR_CANT_RESOLVE_FILENAME (too many levels of symbolic links)
    case ENOTSUP: return 50;        // ERROR_NOT_SUPPORTED
#if defined(EOPNOTSUPP) && EOPNOTSUPP != ENOTSUP
    case EOPNOTSUPP: return 50;     // ERROR_NOT_SUPPORTED (on Linux the same number as ENOTSUP, so no case of its own)
#endif
    default: return 31;             // ERROR_GEN_FAILURE
    }
}
inline void SetErrorFromErrno() { LastError() = Win32ErrorFromErrno(errno); }

// A failed write is remembered by its handle. stdio keeps data in a buffer, so a WriteFile call can report
// success for data that a later flush fails to write: that failure must still reach the caller, through
// FlushFileBuffers or CloseHandle. Sets the last error to this failure and returns it.
inline DWORD NoteWriteFailure(FileHandle* fh, int err) {
    DWORD code = Win32ErrorFromErrno(err);
    if (code == 0) code = 29;       // ERROR_WRITE_FAULT: the C library said nothing more precise
    if (fh && !fh->writeFailed) {
        fh->writeFailed = true;
        fh->writeError = code;
    }
    LastError() = code;
    return code;
}

// Writes out what the stdio buffer of a writable handle holds. A failure is recorded in the handle.
inline bool FlushHandleStream(FileHandle* fh) {
    if (!fh || !fh->f || !fh->canWrite) return true;
    if (std::fflush(fh->f) == 0) return true;
    NoteWriteFailure(fh, errno);
    return false;
}

// Network and FUSE file systems, where every extended attribute read is a round trip to a server or to a
// user-space daemon
inline bool IsRemoteFileSystem(int fd) {
    struct statfs sfs = {};
    if (fd < 0 || ::fstatfs(fd, &sfs) != 0) return false;
    switch (static_cast<uint32_t>(static_cast<unsigned long>(sfs.f_type))) {
    case 0x00006969u:   // NFS
    case 0x0000517Bu:   // SMB
    case 0xFF534D42u:   // CIFS
    case 0xFE534D42u:   // SMB2
    case 0x65735546u:   // FUSE (sshfs, rclone, gvfs and others)
    case 0x01021997u:   // 9p
    case 0x00C36400u:   // Ceph
    case 0x5346414Fu:   // AFS
    case 0x6B414653u:   // kAFS
    case 0x73757245u:   // Coda
    case 0x0000564Cu:   // NCP
        return true;
    default:
        return false;
    }
}

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
#define ERROR_NOT_SAME_DEVICE 17L
#define ERROR_NO_MORE_FILES 18L
#define ERROR_WRITE_FAULT 29L
#define ERROR_GEN_FAILURE 31L
#define ERROR_SHARING_VIOLATION 32L
#define ERROR_HANDLE_EOF 38L
#define ERROR_NOT_SUPPORTED 50L
#define ERROR_FILE_EXISTS 80L
#define ERROR_INVALID_PARAMETER 87L
#define ERROR_BROKEN_PIPE 109L
#define ERROR_DISK_FULL 112L
#define ERROR_DIR_NOT_EMPTY 145L
#define ERROR_ALREADY_EXISTS 183L
#define ERROR_FILENAME_EXCED_RANGE 206L
#define ERROR_NO_DATA 232L
#define ERROR_CANT_RESOLVE_FILENAME 1921L
#define INFINITE 0xFFFFFFFFu

inline DWORD GetLastError() { return Explorer2::Compat::LastError(); }
inline void SetLastError(DWORD e) { Explorer2::Compat::LastError() = e; }
inline DWORD GetCurrentProcessId() { return static_cast<DWORD>(::getpid()); }

inline HANDLE CreateFileW(const wchar_t* path, DWORD access, DWORD share, void* /*security*/, DWORD disposition,
                          DWORD flags, HANDLE /*templateFile*/) {
    const std::string native = Explorer2::Compat::NativePath(path);
    // FILE_FLAG_OPEN_REPARSE_POINT opens a symbolic link itself, as Windows opens the reparse point: a link whose
    // target is gone is still there to be deleted, and must not be reported as missing.
    const bool openLink = (flags & FILE_FLAG_OPEN_REPARSE_POINT) != 0;
    struct stat st = {};
    bool exists = ((openLink ? ::lstat(native.c_str(), &st) : ::stat(native.c_str(), &st)) == 0);
    if (exists && S_ISLNK(st.st_mode)) {
        if (disposition == CREATE_NEW) { SetLastError(ERROR_FILE_EXISTS); return INVALID_HANDLE_VALUE; }
        if (disposition == OPEN_EXISTING || disposition == OPEN_ALWAYS) {
#ifdef O_PATH
            // A descriptor of the link, not of what it points to: enough for its times and to delete it by name
            const int lfd = ::open(native.c_str(), O_PATH | O_NOFOLLOW | O_CLOEXEC);
            if (lfd < 0) { Explorer2::Compat::SetErrorFromErrno(); return INVALID_HANDLE_VALUE; }
            auto* h = new Explorer2::Compat::FileHandle();
            h->dirFd = lfd;
            h->dev = static_cast<uint64_t>(st.st_dev);
            h->ino = static_cast<uint64_t>(st.st_ino);
            h->shareMode = share;
            h->deleteOnClose = (flags & FILE_FLAG_DELETE_ON_CLOSE) != 0;
            h->nativePath = native;
            Explorer2::Compat::RegisterOpenFile(h);
            SetLastError(disposition == OPEN_ALWAYS ? ERROR_ALREADY_EXISTS : ERROR_SUCCESS);
            return static_cast<HANDLE>(static_cast<Explorer2::Compat::HandleBase*>(h));
#else
            SetLastError(ERROR_ACCESS_DENIED);
            return INVALID_HANDLE_VALUE;
#endif
        }
        // CREATE_ALWAYS and TRUNCATE_EXISTING write through the link, as they always did
        exists = (::stat(native.c_str(), &st) == 0);
    }
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
    if (exists && !S_ISREG(st.st_mode)) {
        // A named pipe, a socket or a device. Opening one can wait for ever (a FIFO nobody writes to) or act on
        // hardware (a serial port, a watchdog), and reading one may never end: none of them is a file to open here.
        SetLastError(ERROR_ACCESS_DENIED);
        return INVALID_HANDLE_VALUE;
    }
    constexpr DWORD kFileWriteData = 0x0002u, kFileAppendData = 0x0004u, kFileWriteAttributes = 0x0100u;
    // FILE_APPEND_DATA without a right to write anywhere else: every write goes to the end of the file (the log,
    // the monitoring recorder's CSV files)
    const bool appendOnly = (access & kFileAppendData) != 0 && (access & (GENERIC_WRITE | kFileWriteData)) == 0;
    const bool wantWrite = appendOnly || (access & (GENERIC_WRITE | kFileWriteAttributes | kFileWriteData)) != 0;
    const bool wantRead = (access & GENERIC_READ) != 0 || !wantWrite;
    int accMode = O_RDONLY;
    int createFlags = 0;
    switch (disposition) {
    case OPEN_EXISTING:
        if (!exists) { SetLastError(ERROR_FILE_NOT_FOUND); return INVALID_HANDLE_VALUE; }
        accMode = wantWrite ? O_RDWR : O_RDONLY;
        break;
    case CREATE_ALWAYS:
        accMode = (wantRead && wantWrite) ? O_RDWR : O_WRONLY;
        createFlags = O_CREAT | O_TRUNC;
        break;
    case CREATE_NEW:
        if (exists) { SetLastError(ERROR_FILE_EXISTS); return INVALID_HANDLE_VALUE; }
        accMode = (wantRead && wantWrite) ? O_RDWR : O_WRONLY;
        createFlags = O_CREAT | O_EXCL;
        break;
    case OPEN_ALWAYS:
        accMode = exists ? (wantWrite ? O_RDWR : O_RDONLY) : ((wantRead && wantWrite) ? O_RDWR : O_WRONLY);
        createFlags = O_CREAT;
        break;
    case TRUNCATE_EXISTING:
        if (!exists) { SetLastError(ERROR_FILE_NOT_FOUND); return INVALID_HANDLE_VALUE; }
        accMode = wantRead ? O_RDWR : O_WRONLY;
        createFlags = O_TRUNC;
        break;
    default:
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    if (appendOnly) {
        // CREATE_ALWAYS and TRUNCATE_EXISTING still empty the file first; reading needs GENERIC_READ as well
        accMode = wantRead ? O_RDWR : O_WRONLY;
        createFlags |= O_APPEND;
    }
    // O_NONBLOCK in case the name has turned into a FIFO since the stat above: the open must not wait for a
    // writer. It is cleared again below, once the descriptor is known to be a regular file.
    auto openAs = [&](int accessMode) {
        int r = ::open(native.c_str(), accessMode | createFlags | O_CLOEXEC | O_NOCTTY | O_NONBLOCK, 0666);
        if (r < 0 && errno == EAGAIN && exists) {
            // A lease on the file (Samba, the NFS server): only a blocking open waits for its holder to let go.
            // The stat above found a regular file, and opening one does not hang.
            r = ::open(native.c_str(), accessMode | createFlags | O_CLOEXEC | O_NOCTTY, 0666);
        }
        return r;
    };
    int fd = openAs(accMode);
    if (fd < 0 && wantWrite && !appendOnly && disposition == OPEN_EXISTING && (access & GENERIC_WRITE) == 0) {
        // Read-only file opened for FILE_WRITE_ATTRIBUTES only: an O_RDONLY descriptor still allows futimens
        accMode = O_RDONLY;
        fd = openAs(O_RDONLY);
    }
    if (fd < 0) {
        if (disposition == CREATE_NEW && errno == EEXIST) SetLastError(ERROR_FILE_EXISTS);
        else Explorer2::Compat::SetErrorFromErrno();
        return INVALID_HANDLE_VALUE;
    }
    struct stat fst = {};
    if (::fstat(fd, &fst) != 0 || !S_ISREG(fst.st_mode)) {
        ::close(fd);
        SetLastError(ERROR_ACCESS_DENIED);
        return INVALID_HANDLE_VALUE;
    }
    const int fdFlags = ::fcntl(fd, F_GETFL);
    if (fdFlags >= 0) (void)::fcntl(fd, F_SETFL, fdFlags & ~O_NONBLOCK);
    // fdopen neither creates nor truncates: the mode only has to match the descriptor's access
    const char* mode = (accMode == O_RDONLY) ? "rb"
                     : (accMode == O_WRONLY) ? (appendOnly ? "ab" : "wb")
                     : (appendOnly ? "a+b" : "r+b");
    FILE* f = ::fdopen(fd, mode);
    if (!f) {
        ::close(fd);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return INVALID_HANDLE_VALUE;
    }
    if (appendOnly) {
        // Unbuffered, so that every WriteFile is one write(2): a log line is in the file the moment it is written,
        // and survives the program being killed
        std::setvbuf(f, nullptr, _IONBF, 0);
    }
    auto* h = new Explorer2::Compat::FileHandle();
    h->f = f;
    h->dev = static_cast<uint64_t>(fst.st_dev);
    h->ino = static_cast<uint64_t>(fst.st_ino);
    h->shareMode = share;
    h->deleteOnClose = (flags & FILE_FLAG_DELETE_ON_CLOSE) != 0;
    h->canWrite = (accMode != O_RDONLY);
    h->nativePath = native;
    Explorer2::Compat::RegisterOpenFile(h);
    SetLastError((exists && (disposition == CREATE_ALWAYS || disposition == OPEN_ALWAYS)) ? ERROR_ALREADY_EXISTS : ERROR_SUCCESS);
    return static_cast<HANDLE>(static_cast<Explorer2::Compat::HandleBase*>(h));
}

inline BOOL ReadFile(HANDLE h, void* buffer, DWORD toRead, DWORD* read, void* /*overlapped*/) {
    if (read) *read = 0;
    Explorer2::Compat::FileHandle* fh = Explorer2::Compat::FileHandleOf(h);
    FILE* f = fh ? fh->f : nullptr;
    if (!f) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    if (fh->isStream) {
        // A pipe or a socket returns what has arrived, as on Windows. fread would wait until the whole buffer is
        // full, which never happens while the program at the other end waits for an answer.
        if (toRead == 0) return TRUE;
        ssize_t got = 0;
        do {
            got = ::read(fileno(f), buffer, toRead);
        } while (got < 0 && errno == EINTR);
        if (got < 0) {
            if (errno == EAGAIN) SetLastError(ERROR_NO_DATA);
            else Explorer2::Compat::SetErrorFromErrno();
            return FALSE;
        }
        if (got == 0) { SetLastError(ERROR_BROKEN_PIPE); return FALSE; }   // the writing end is closed
        if (read) *read = static_cast<DWORD>(got);
        return TRUE;
    }
    const size_t got = toRead ? std::fread(buffer, 1, toRead, f) : 0;
    if (read) *read = static_cast<DWORD>(got);
    if (got != toRead && std::ferror(f)) { Explorer2::Compat::SetErrorFromErrno(); return FALSE; }
    return TRUE;
}

inline BOOL WriteFile(HANDLE h, const void* buffer, DWORD toWrite, DWORD* written, void* /*overlapped*/) {
    if (written) *written = 0;
    Explorer2::Compat::FileHandle* fh = Explorer2::Compat::FileHandleOf(h);
    FILE* f = fh ? fh->f : nullptr;
    if (!f) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    errno = 0;
    const size_t put = toWrite ? std::fwrite(buffer, 1, toWrite, f) : 0;
    if (written) *written = static_cast<DWORD>(put);
    if (put != toWrite) {
        const int e = errno;
        if (!fh->canWrite) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }   // a handle opened for reading
        Explorer2::Compat::NoteWriteFailure(fh, e);
        return FALSE;
    }
    return TRUE;
}

inline BOOL GetFileSizeEx(HANDLE h, LARGE_INTEGER* size) {
    Explorer2::Compat::FileHandle* fh = Explorer2::Compat::FileHandleOf(h);
    FILE* f = fh ? fh->f : nullptr;
    struct stat st = {};
    if (!f || !size) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    // Data still in the stdio buffer is part of the file, as everything WriteFile accepted is on Windows
    Explorer2::Compat::FlushHandleStream(fh);
    if (::fstat(fileno(f), &st) != 0) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
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
    Explorer2::Compat::FileHandle* fh = Explorer2::Compat::FileHandleOf(h);
    FILE* f = fh ? fh->f : nullptr;
    if (!f) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    // A seek writes out the stdio buffer first; done here, so that a failure is known to be a failed write
    if (!Explorer2::Compat::FlushHandleStream(fh)) return FALSE;
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
    // Buffered data written after futimens would move the modification time again. A flush that fails is a
    // failed write, which the handle keeps for FlushFileBuffers and CloseHandle; the times are set all the same.
    Explorer2::Compat::FlushHandleStream(Explorer2::Compat::FileHandleOf(h));
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
    Explorer2::Compat::FileHandle* fh = Explorer2::Compat::FileHandleOf(h);
    FILE* f = fh ? fh->f : nullptr;
    if (!f) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    // The end is where the data written so far ends, so that data has to be in the file first
    if (!Explorer2::Compat::FlushHandleStream(fh)) return FALSE;
    const off_t pos = ::ftello(f);
    if (pos < 0 || ::ftruncate(fileno(f), pos) != 0) { Explorer2::Compat::SetErrorFromErrno(); return FALSE; }
    return TRUE;
}

// Hands the stdio buffer to the kernel, without an fsync: the copy engine calls this after every file, and
// waiting for the disk there would make copies much slower. Fails when this or any earlier write failed.
inline BOOL FlushFileBuffers(HANDLE h) {
    Explorer2::Compat::FileHandle* fh = Explorer2::Compat::FileHandleOf(h);
    if (!fh || !fh->f) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    Explorer2::Compat::FlushHandleStream(fh);
    if (fh->writeFailed) { SetLastError(fh->writeError); return FALSE; }
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
namespace Explorer2::Compat {
// Gives one directory entry a new spelling of its name ("IMG_0001.JPG" -> "img_0001.jpg" on FAT, exFAT or SMB,
// where both spellings name the same entry). rename() between two names of one file does nothing on Linux, so
// the entry goes through a free temporary name in its folder. Returns 0 on success, else the Win32 error.
inline DWORD RespellEntry(const std::string& from, const std::string& to) {
    std::string clean = from;
    while (clean.size() > 1 && clean.back() == '/') clean.pop_back();
    const size_t sl = clean.find_last_of('/');
    const std::string dir = (sl == std::string::npos) ? std::string(".") : (sl == 0 ? std::string("/") : clean.substr(0, sl));
    for (int n = 0; n < 100; ++n) {
        const std::string tmp = dir + (dir == "/" ? "" : "/") + ".e2-rename-" + std::to_string(::getpid()) + "-" + std::to_string(n);
        struct stat stTmp = {};
        if (::lstat(tmp.c_str(), &stTmp) == 0 || errno != ENOENT) continue;
        if (::rename(clean.c_str(), tmp.c_str()) != 0) return Win32ErrorFromErrno(errno);
        if (::rename(tmp.c_str(), to.c_str()) == 0) return 0;
        const DWORD err = Win32ErrorFromErrno(errno);
        (void)::rename(tmp.c_str(), clean.c_str());     // the old name back
        return err;
    }
    return 183;     // ERROR_ALREADY_EXISTS: no free temporary name
}

// MoveFileExW's copy to another file system. The content, the permission bits, the access and modification times
// and the creation time Explorer2 keeps go with the file; a symbolic link is moved as a link. A folder or a
// special file is not copied: Windows moves folders only within a volume. Returns 0 on success, else the error.
inline DWORD CopyForMove(const std::string& from, const std::string& to, const struct stat& stFrom, bool replace) {
    if (S_ISLNK(stFrom.st_mode)) {
        char target[PATH_MAX] = {};
        const ssize_t n = ::readlink(from.c_str(), target, sizeof(target) - 1);
        if (n < 0) return Win32ErrorFromErrno(errno);
        target[n] = '\0';
        struct stat stTo = {};
        if (replace && ::lstat(to.c_str(), &stTo) == 0 && !S_ISDIR(stTo.st_mode)) (void)::unlink(to.c_str());
        if (::symlink(target, to.c_str()) != 0) return Win32ErrorFromErrno(errno);
        return 0;
    }
    if (!S_ISREG(stFrom.st_mode)) return 17;            // ERROR_NOT_SAME_DEVICE
    const int in = ::open(from.c_str(), O_RDONLY | O_CLOEXEC | O_NOCTTY | O_NOFOLLOW | O_NONBLOCK);
    if (in < 0) return Win32ErrorFromErrno(errno);
    struct stat stIn = {};
    if (::fstat(in, &stIn) != 0 || !S_ISREG(stIn.st_mode)) { ::close(in); return 5; }   // ERROR_ACCESS_DENIED
    const int inFlags = ::fcntl(in, F_GETFL);
    if (inFlags >= 0) (void)::fcntl(in, F_SETFL, inFlags & ~O_NONBLOCK);
    // A link in the way is replaced, as rename() would replace it, instead of being written through
    struct stat stTo = {};
    if (replace && ::lstat(to.c_str(), &stTo) == 0 && S_ISLNK(stTo.st_mode)) (void)::unlink(to.c_str());
    const int out = ::open(to.c_str(), O_WRONLY | O_CREAT | O_CLOEXEC | (replace ? O_TRUNC : O_EXCL), stIn.st_mode & 0777);
    if (out < 0) { const DWORD e = Win32ErrorFromErrno(errno); ::close(in); return e; }
    DWORD err = 0;
    std::vector<char> buf(256 * 1024);
    for (;;) {
        const ssize_t got = ::read(in, buf.data(), buf.size());
        if (got < 0) {
            if (errno == EINTR) continue;
            err = Win32ErrorFromErrno(errno);
            break;
        }
        if (got == 0) break;
        ssize_t off = 0;
        while (off < got) {
            const ssize_t put = ::write(out, buf.data() + off, static_cast<size_t>(got - off));
            if (put < 0) {
                if (errno == EINTR) continue;
                err = Win32ErrorFromErrno(errno);
                break;
            }
            off += put;
        }
        if (err != 0) break;
    }
    if (err == 0) {
        // The mode after the data (the umask narrowed it at the create, and a write clears set-id bits); FAT and
        // exFAT refuse some modes, which is no reason to fail the move
        (void)::fchmod(out, stIn.st_mode & 07777);
        const struct timespec times[2] = { stIn.st_atim, stIn.st_mtim };
        (void)::futimens(out, times);
        FILETIME created = {};
        if (::lgetxattr(from.c_str(), "user.e2_btime", &created, sizeof(created)) == static_cast<ssize_t>(sizeof(created))) {
            (void)::fsetxattr(out, "user.e2_btime", &created, sizeof(created), 0);
        }
    }
    ::close(in);
    if (::close(out) != 0 && err == 0) err = Win32ErrorFromErrno(errno);
    if (err != 0) (void)::unlink(to.c_str());
    return err;
}
} // namespace Explorer2::Compat

inline BOOL MoveFileExW(const wchar_t* from, const wchar_t* to, DWORD flags) {
    const std::string a = Explorer2::Compat::NativePath(from);
    const std::string b = Explorer2::Compat::NativePath(to);
    struct stat stSrc = {};
    if (::lstat(a.c_str(), &stSrc) != 0) { Explorer2::Compat::SetErrorFromErrno(); return FALSE; }
    if (!S_ISLNK(stSrc.st_mode) && Explorer2::Compat::IsDeleteBlockedByOpenHandle(static_cast<uint64_t>(stSrc.st_dev), static_cast<uint64_t>(stSrc.st_ino))) {
        SetLastError(ERROR_SHARING_VIOLATION);
        return FALSE;
    }
    struct stat stDst = {};
    const bool dstExists = (::lstat(b.c_str(), &stDst) == 0);
    // On a file system that ignores letter case (FAT, exFAT, SMB) the new spelling of a name finds the item being
    // renamed. With one link to the inode (or a folder, which has no hard links) the same inode can only be the
    // same entry: a rename, not a clash with another file.
    if (dstExists && stDst.st_dev == stSrc.st_dev && stDst.st_ino == stSrc.st_ino &&
        (S_ISDIR(stSrc.st_mode) || stSrc.st_nlink <= 1)) {
        if (a == b) return TRUE;
        const DWORD err = Explorer2::Compat::RespellEntry(a, b);
        if (err != 0) { SetLastError(err); return FALSE; }
        return TRUE;
    }
    if (dstExists && !(flags & MOVEFILE_REPLACE_EXISTING)) { SetLastError(ERROR_ALREADY_EXISTS); return FALSE; }
    if (::rename(a.c_str(), b.c_str()) == 0) return TRUE;
    if (errno != EXDEV || !(flags & MOVEFILE_COPY_ALLOWED)) { Explorer2::Compat::SetErrorFromErrno(); return FALSE; }
    // Another file system: copy, then delete the original. As on Windows, a copy whose original cannot be deleted
    // still counts as done.
    const DWORD err = Explorer2::Compat::CopyForMove(a, b, stSrc, (flags & MOVEFILE_REPLACE_EXISTING) != 0);
    if (err != 0) { SetLastError(err); return FALSE; }
    (void)::unlink(a.c_str());
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
// ---- Network and FUSE mount points -----------------------------------------------------------------------------
// A folder listing must not stat the mount point of a network or FUSE file system, nor follow a link into one: when
// its server stops answering (a NAS switched off), that stat waits for minutes, and so does the window that lists
// /mnt, /media/<user> or a home folder with a link to the NAS.
inline bool IsRemoteMountType(const char* type) {
    static const char* const kTypes[] = { "nfs", "nfs4", "cifs", "smb3", "smbfs", "9p", "ceph", "glusterfs", "afs",
                                          "coda", "ncpfs", "lustre", "gpfs", "beegfs", "davfs", "sshfs", "fuse" };
    for (const char* t : kTypes) {
        if (std::strcmp(type, t) == 0) return true;
    }
    return std::strncmp(type, "fuse.", 5) == 0;      // sshfs, rclone, gvfsd-fuse, s3fs... ("fuseblk" is a local disk)
}

// A /proc/self/mounts field: a space, tab, newline or backslash in it is written as an octal escape ("\040")
inline std::string DecodeMountsField(const char* s) {
    std::string out;
    for (size_t i = 0; s[i] != '\0'; ++i) {
        if (s[i] == '\\' && s[i + 1] >= '0' && s[i + 1] <= '3' && s[i + 2] >= '0' && s[i + 2] <= '7' &&
            s[i + 3] >= '0' && s[i + 3] <= '7') {
            out.push_back(static_cast<char>(((s[i + 1] - '0') << 6) | ((s[i + 2] - '0') << 3) | (s[i + 3] - '0')));
            i += 3;
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

// The mount points of network and FUSE file systems, read from /proc/self/mounts at most every 2 seconds (reading
// that file never waits for a server)
inline std::vector<std::string> RemoteMountPoints() {
    static std::mutex s_mutex;
    static std::vector<std::string> s_points;
    static bool s_loaded = false;
    static std::chrono::steady_clock::time_point s_readAt;
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lk(s_mutex);
    if (!s_loaded || now - s_readAt > std::chrono::seconds(2)) {
        s_points.clear();
        if (FILE* f = std::fopen("/proc/self/mounts", "re")) {
            char line[4096];
            while (std::fgets(line, sizeof(line), f)) {
                char* save = nullptr;
                const char* device = ::strtok_r(line, " \t\n", &save);
                const char* where = device ? ::strtok_r(nullptr, " \t\n", &save) : nullptr;
                const char* type = where ? ::strtok_r(nullptr, " \t\n", &save) : nullptr;
                if (type && IsRemoteMountType(type)) s_points.push_back(DecodeMountsField(where));
            }
            std::fclose(f);
        }
        s_loaded = true;
        s_readAt = now;
    }
    return s_points;
}

// Whether a path is one of those mount points (*isMountPoint) or lies inside one. Only the text is compared:
// nothing is looked up on disk.
inline bool IsUnderRemoteMount(const std::string& nativePath, const std::vector<std::string>& points, bool* isMountPoint) {
    if (isMountPoint) *isMountPoint = false;
    for (const auto& p : points) {
        if (p.size() < 2 || nativePath.size() < p.size() || nativePath.compare(0, p.size(), p) != 0) continue;
        if (nativePath.size() == p.size()) {
            if (isMountPoint) *isMountPoint = true;
            return true;
        }
        if (nativePath[p.size()] == '/') return true;
    }
    return false;
}

struct FindState : HandleBase {
    FindState() : HandleBase(HandleKind::Find) {}
    DIR* dir = nullptr;
    std::string dirNative;
    std::wstring mask;
    bool remoteFs = false;    // the folder is on a network or FUSE file system
    std::vector<std::string> remoteMounts;    // network / FUSE mount points when the listing started
    mutable std::string dirResolved;          // the folder as a canonical path, compared with those mount points
    mutable bool dirResolvedKnown = false;    // worked out only when an entry has a mount point's name
    bool exact = false;       // the mask names one item (no '*' or '?'): it is looked up by exactly that name
    bool exactDone = false;
    int exactErrno = 0;       // why that lookup failed, when it was not simply "no such item"
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

// Whether the folder entry nativeName is a network share's mount point (looked at without a stat). The folder's
// canonical path is worked out only for an entry whose name is the last part of such a mount point.
inline bool IsRemoteMountEntry(const FindState& st, const char* nativeName) {
    if (st.remoteMounts.empty()) return false;
    bool leafMatches = false;
    for (const auto& p : st.remoteMounts) {
        const size_t slash = p.find_last_of('/');
        if (slash != std::string::npos && p.compare(slash + 1, std::string::npos, nativeName) == 0) {
            leafMatches = true;
            break;
        }
    }
    if (!leafMatches) return false;
    if (!st.dirResolvedKnown) {
        char resolved[PATH_MAX];
        st.dirResolved = ::realpath(st.dirNative.c_str(), resolved) ? std::string(resolved) : st.dirNative;
        while (st.dirResolved.size() > 1 && st.dirResolved.back() == '/') st.dirResolved.pop_back();
        st.dirResolvedKnown = true;
    }
    bool isMountPoint = false;
    const std::string entry = (st.dirResolved == "/" ? std::string() : st.dirResolved) + "/" + nativeName;
    return IsUnderRemoteMount(entry, st.remoteMounts, &isMountPoint) && isMountPoint;
}

// Fills *data for the entry nativeName of the listed folder. lst: its lstat, or null when that failed.
inline void FillFindEntry(const FindState& st, int dfd, const char* nativeName, const std::wstring& name,
                          const struct stat* lst, WIN32_FIND_DATAW* data) {
    std::memset(data, 0, sizeof(*data));
    const std::string full = st.dirNative + "/" + nativeName;
    if (IsRemoteMountEntry(st, nativeName)) {
        // The mount point of a network share: shown as a folder without asking its server anything
        data->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
    } else if (lst) {
        DWORD attrs = 0;
        uint64_t sz = static_cast<uint64_t>(lst->st_size);
        if (S_ISLNK(lst->st_mode)) {
            attrs |= FILE_ATTRIBUTE_REPARSE_POINT;
            data->dwReserved0 = IO_REPARSE_TAG_SYMLINK;
            // A link into a network share is not followed (its target is only known as text then)
            bool intoShare = false, toMountPoint = false;
            if (!st.remoteMounts.empty()) {
                char target[PATH_MAX];
                const ssize_t len = (dfd >= 0) ? ::readlinkat(dfd, nativeName, target, sizeof(target) - 1)
                                               : ::readlink(full.c_str(), target, sizeof(target) - 1);
                if (len > 0 && target[0] == '/') {
                    std::string t(target, static_cast<size_t>(len));
                    while (t.size() > 1 && t.back() == '/') t.pop_back();
                    intoShare = IsUnderRemoteMount(t, st.remoteMounts, &toMountPoint);
                }
            }
            if (intoShare) {
                if (toMountPoint) attrs |= FILE_ATTRIBUTE_DIRECTORY;
            } else {
                struct stat tst = {};
                if (((dfd >= 0) ? ::fstatat(dfd, nativeName, &tst, 0) : ::stat(full.c_str(), &tst)) == 0) {
                    if (S_ISDIR(tst.st_mode)) attrs |= FILE_ATTRIBUTE_DIRECTORY;
                    else sz = static_cast<uint64_t>(tst.st_size);
                }
            }
        } else if (S_ISDIR(lst->st_mode)) {
            attrs |= FILE_ATTRIBUTE_DIRECTORY;
            if ((lst->st_mode & 0200) == 0) attrs |= FILE_ATTRIBUTE_READONLY;
        } else {
            if ((lst->st_mode & 0222) == 0) attrs |= FILE_ATTRIBUTE_READONLY;
            if (IsSparseStat(*lst)) attrs |= FILE_ATTRIBUTE_SPARSE_FILE;
        }
        data->dwFileAttributes = attrs ? attrs : FILE_ATTRIBUTE_NORMAL;
        data->nFileSizeHigh = static_cast<DWORD>(sz >> 32);
        data->nFileSizeLow = static_cast<DWORD>(sz & 0xFFFFFFFFu);
        data->ftLastWriteTime = FileTimeFromTimespec(lst->st_mtim);
        // On a network or FUSE file system each extended attribute read is one more round trip per entry, so
        // the listing takes the creation time from memory or the modification time there. On a local disk the
        // attribute is read, because the copy engine takes creation times from these listings and would lose
        // the ones kept in it.
        data->ftCreationTime = GetInodeCreationTime(st.remoteFs ? nullptr : full.c_str(), *lst);
        data->ftLastAccessTime = FileTimeFromTimespec(lst->st_atim);
    } else {
        data->dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
    }
    if (nativeName[0] == '.' && std::strcmp(nativeName, ".") != 0 && std::strcmp(nativeName, "..") != 0) {
        data->dwFileAttributes = (data->dwFileAttributes & ~static_cast<DWORD>(FILE_ATTRIBUTE_NORMAL)) | FILE_ATTRIBUTE_HIDDEN;
    }
    const size_t n = (name.size() < MAX_PATH - 1) ? name.size() : MAX_PATH - 1;
    std::wmemcpy(data->cFileName, name.data(), n);
    data->cFileName[n] = L'\0';
}

inline bool NextMatch(FindState& st, WIN32_FIND_DATAW* data) {
    const int dfd = ::dirfd(st.dir);
    if (st.exact) {
        // A name without wildcards is looked up as it is written. On Linux "file.txt" and "File.txt" are two
        // different items, and whichever one readdir returned first must not stand in for the one asked for.
        if (st.exactDone) return false;
        st.exactDone = true;
        std::string nativeName;
        EncodeUtf8(st.mask.data(), st.mask.size(), nativeName);
        if (!nativeName.empty() && nativeName.find('/') == std::string::npos) {
            if (IsRemoteMountEntry(st, nativeName.c_str())) {
                FillFindEntry(st, dfd, nativeName.c_str(), st.mask, nullptr, data);
                return true;
            }
            struct stat lst = {};
            if (((dfd >= 0) ? ::fstatat(dfd, nativeName.c_str(), &lst, AT_SYMLINK_NOFOLLOW)
                            : ::lstat((st.dirNative + "/" + nativeName).c_str(), &lst)) == 0) {
                FillFindEntry(st, dfd, nativeName.c_str(), st.mask, &lst, data);
                return true;
            }
            if (errno != ENOENT && errno != EILSEQ) {
                st.exactErrno = errno;
                return false;
            }
        }
        // A name stored in another encoding than UTF-8 is found by its decoded form, still case by case
        while (dirent* e = ::readdir(st.dir)) {
            std::wstring name;
            DecodeUtf8(e->d_name, std::strlen(e->d_name), name, false);
            if (name != st.mask) continue;
            struct stat lst = {};
            const bool ok = ((dfd >= 0) ? ::fstatat(dfd, e->d_name, &lst, AT_SYMLINK_NOFOLLOW)
                                        : ::lstat((st.dirNative + "/" + e->d_name).c_str(), &lst)) == 0;
            FillFindEntry(st, dfd, e->d_name, name, ok ? &lst : nullptr, data);
            return true;
        }
        return false;
    }
    while (dirent* e = ::readdir(st.dir)) {
        std::wstring name;
        DecodeUtf8(e->d_name, std::strlen(e->d_name), name, false);
        if (!WildcardMatch(st.mask.c_str(), name.c_str())) continue;
        if (IsRemoteMountEntry(st, e->d_name)) {
            FillFindEntry(st, dfd, e->d_name, name, nullptr, data);
            return true;
        }
        // Looked up relative to the open folder, so the whole path is not walked again for every entry
        struct stat lst = {};
        const bool ok = ((dfd >= 0) ? ::fstatat(dfd, e->d_name, &lst, AT_SYMLINK_NOFOLLOW)
                                    : ::lstat((st.dirNative + "/" + e->d_name).c_str(), &lst)) == 0;
        FillFindEntry(st, dfd, e->d_name, name, ok ? &lst : nullptr, data);
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
    st->exact = !st->mask.empty() && st->mask.find_first_of(L"*?") == std::wstring::npos;
    st->dirNative = Explorer2::Compat::NativePath(dir.c_str());
    st->dir = ::opendir(st->dirNative.c_str());
    if (st->dir) {
        st->remoteFs = Explorer2::Compat::IsRemoteFileSystem(::dirfd(st->dir));
        st->remoteMounts = Explorer2::Compat::RemoteMountPoints();
    }
    if (!st->dir || !Explorer2::Compat::NextMatch(*st, data)) {
        if (st->dir) {
            ::closedir(st->dir);
            SetLastError(st->exactErrno ? Explorer2::Compat::Win32ErrorFromErrno(st->exactErrno) : ERROR_FILE_NOT_FOUND);
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
        DWORD err = 0;
        if (fh->f) {
            // The last buffered data is written here. If that, or any write before it, failed, the file is not
            // what the caller wrote, and CloseHandle says so.
            Explorer2::Compat::FlushHandleStream(fh);
            if (std::fclose(fh->f) != 0) {
                const DWORD e = Explorer2::Compat::Win32ErrorFromErrno(errno);
                err = e ? e : ERROR_GEN_FAILURE;
            }
        }
        if (fh->dirFd >= 0 && ::close(fh->dirFd) != 0 && err == 0) {
            const DWORD e = Explorer2::Compat::Win32ErrorFromErrno(errno);
            err = e ? e : ERROR_GEN_FAILURE;
        }
        if (fh->writeFailed) err = fh->writeError;      // the first failure is the one to report
        if (fh->deleteOnClose && !fh->nativePath.empty()) {
            (void)::unlink(fh->nativePath.c_str());
        }
        delete fh;
        if (err != 0) { SetLastError(err); return FALSE; }
        return TRUE;
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
