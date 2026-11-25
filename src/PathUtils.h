#pragma once
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "PlatformCompat.h"   // <windows.h> on Windows
#include "ThemeColors.h"
#if defined(_WIN32) || defined(E2_WIN32_HAL)
#include <d2d1.h>
#include <dwrite.h>
#include <shlwapi.h>
#endif
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <cwctype>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace Explorer2::PathUtils {

inline std::wstring ToLower(std::wstring_view sv) {
    std::wstring out(sv);
    for (wchar_t& ch : out) {
        ch = static_cast<wchar_t>(towlower(ch));
    }
    return out;
}

inline std::wstring ToUpper(std::wstring_view sv) {
    std::wstring out(sv);
    for (wchar_t& ch : out) {
        ch = static_cast<wchar_t>(towupper(ch));
    }
    return out;
}

inline std::string ToLowerAscii(std::string_view sv) {
    std::string out(sv);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

inline std::wstring GetExtension(const std::wstring& path) {
    const wchar_t* extPtr = PathFindExtensionW(path.c_str());
    return (extPtr && *extPtr) ? ToLower(extPtr) : L"";
}

inline std::wstring GetExtensionLower(const std::wstring& path) {
    return GetExtension(path);
}

inline std::wstring Trim(std::wstring_view sv) {
    size_t start = 0;
    while (start < sv.size() && (sv[start] == L' ' || sv[start] == L'\t' || sv[start] == L'\r' || sv[start] == L'\n')) {
        ++start;
    }
    size_t end = sv.size();
    while (end > start && (sv[end - 1] == L' ' || sv[end - 1] == L'\t' || sv[end - 1] == L'\r' || sv[end - 1] == L'\n')) {
        --end;
    }
    return std::wstring(sv.substr(start, end - start));
}

// ASCII whitespace (space, tab, CR, LF) removed from both ends of a narrow string
inline std::string_view TrimAscii(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.remove_suffix(1);
    return s;
}

inline std::wstring TrimTrailingSlashes(std::wstring_view sv) {
    std::wstring out(sv);
    while (!out.empty() && (out.back() == L'\\' || out.back() == L'/')) {
        if (out.size() == 3 && out[1] == L':') break; // Preserve "C:\"
        if (out.size() == 1 && out[0] == L'/') break; // Preserve "/" on POSIX
        out.pop_back();
    }
    if (out.size() == 2 && out[1] == L':') {
        out.push_back(L'\\');
    }
    return out;
}

inline std::wstring EnsureTrailingSlash(std::wstring_view sv) {
    std::wstring out(sv);
    if (!out.empty() && out.back() != L'\\' && out.back() != L'/') {
        out.push_back((out[0] == L'/') ? L'/' : L'\\');
    }
    return out;
}

inline std::wstring NormalizePathKey(std::wstring_view sv) {
    std::wstring key = TrimTrailingSlashes(sv);
    for (wchar_t& ch : key) {
        if (ch == L'/') ch = L'\\';
        else ch = static_cast<wchar_t>(towlower(ch));
    }
    return key;
}

// Strips "\\?\" or "\\?\UNC\" extended-length Win32 prefix back to a display/DOS/UNC path.
inline std::wstring StripExtendedWinPrefix(std::wstring_view sv) {
    if (sv.size() >= 8 &&
        (sv.substr(0, 8) == L"\\\\?\\UNC\\" || sv.substr(0, 8) == L"\\\\?\\unc\\"))
    {
        return L"\\\\" + std::wstring(sv.substr(8));
    }
    if (sv.size() >= 4 && sv.substr(0, 4) == L"\\\\?\\") {
        return std::wstring(sv.substr(4));
    }
    return std::wstring(sv);
}

// Converts an absolute DOS ("C:\...") or UNC ("\\server\share...") path to "\\?\" extended Win32 form on Windows.
inline std::wstring ToExtendedWinPath(std::wstring_view sv) {
    if (sv.empty()) return {};
#ifdef _WIN32
    if (sv.size() >= 4 && sv.substr(0, 4) == L"\\\\?\\") return std::wstring(sv);
    if (sv.size() >= 2 && sv[0] == L'\\' && sv[1] == L'\\') {
        return L"\\\\?\\UNC\\" + std::wstring(sv.substr(2));
    }
    if (sv.size() >= 2 && sv[1] == L':') {
        return L"\\\\?\\" + std::wstring(sv);
    }
#endif
    return std::wstring(sv);
}

inline std::wstring GetParentPath(std::wstring_view sv) {
    std::wstring clean = TrimTrailingSlashes(sv);
    if (clean == L"/") return L"";
    size_t slash = clean.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return L"";
    if (slash == 0 && clean[0] == L'/') return L"/";
    std::wstring parent = clean.substr(0, slash);
    if (parent.size() == 2 && parent[1] == L':') {
        parent.push_back(L'\\');
    }
    return parent;
}

inline std::wstring GetFileName(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos && slash + 1 < path.size()) {
        return path.substr(slash + 1);
    }
    const wchar_t* base = PathFindFileNameW(path.c_str());
    return (base && *base) ? std::wstring(base) : path;
}

inline std::wstring GetStem(const std::wstring& fileName, bool isDirectory) {
    if (isDirectory) return fileName;
    size_t dot = fileName.find_last_of(L'.');
    return (dot != std::wstring::npos && dot > 0) ? fileName.substr(0, dot) : fileName;
}

// Number of leading bytes of s that form complete, valid UTF-8 sequences
inline size_t ValidUtf8PrefixLength(std::string_view s) {
    size_t i = 0;
    const size_t n = s.size();
    while (i < n) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80u) { ++i; continue; }
        size_t len = 0;
        uint32_t minCp = 0;
        if (c >= 0xC2u && c <= 0xDFu) { len = 2; minCp = 0x80u; }
        else if (c >= 0xE0u && c <= 0xEFu) { len = 3; minCp = 0x800u; }
        else if (c >= 0xF0u && c <= 0xF4u) { len = 4; minCp = 0x10000u; }
        else return i;
        if (i + len > n) return i;
        uint32_t cp = c & (0xFFu >> (len + 1));
        for (size_t k = 1; k < len; ++k) {
            unsigned char cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0u) != 0x80u) return i;
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        if (cp < minCp || cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) return i;
        i += len;
    }
    return n;
}

// UTF-8 -> UTF-16. Text that is not UTF-8 (legacy 8-bit files: Windows-1250 subtitles, CUE sheets,
// CSV exports, FTP listings) is decoded with the system ANSI code page instead of turning every
// accented letter into U+FFFD. A multi-byte character cut off at the very end (a reader truncated the
// text) still counts as UTF-8.
// Appends one Unicode code point as UTF-16 (a surrogate pair above U+FFFF); 0, surrogates and values past
// U+10FFFF are dropped
inline void AppendUtf16(std::wstring& out, uint32_t cp) {
    if (cp == 0 || cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) return;
    if constexpr (sizeof(wchar_t) == 4) {   // Linux: wchar_t holds any code point (UTF-32)
        out.push_back(static_cast<wchar_t>(cp));
        return;
    }
    if (cp <= 0xFFFFu) {
        out.push_back(static_cast<wchar_t>(cp));
    } else {
        cp -= 0x10000u;
        out.push_back(static_cast<wchar_t>(0xD800u + (cp >> 10)));
        out.push_back(static_cast<wchar_t>(0xDC00u + (cp & 0x3FFu)));
    }
}

inline std::wstring Utf8ToWide(std::string_view utf8) {
    if (utf8.empty()) return L"";
    auto convert = [](UINT cp, DWORD flags, std::string_view s) -> std::wstring {
        int wlen = MultiByteToWideChar(cp, flags, s.data(), static_cast<int>(s.size()), nullptr, 0);
        if (wlen <= 0) return std::wstring();
        std::wstring out(static_cast<size_t>(wlen), L'\0');
        MultiByteToWideChar(cp, flags, s.data(), static_cast<int>(s.size()), out.data(), wlen);
        return out;
    };
    std::wstring w = convert(CP_UTF8, MB_ERR_INVALID_CHARS, utf8);
    if (!w.empty()) return w;
    size_t valid = ValidUtf8PrefixLength(utf8);
    size_t tail = utf8.size() - valid;
    if (tail < 4) {
        unsigned char lead = static_cast<unsigned char>(utf8[valid]);
        size_t need = (lead >= 0xC2u && lead <= 0xDFu) ? 2 : (lead >= 0xE0u && lead <= 0xEFu) ? 3 : (lead >= 0xF0u && lead <= 0xF4u) ? 4 : 0;
        bool contOnly = true;
        for (size_t k = valid + 1; k < utf8.size(); ++k) {
            if ((static_cast<unsigned char>(utf8[k]) & 0xC0u) != 0x80u) contOnly = false;
        }
        if (need > tail && contOnly) {
            // Cut-off final character: keep the UTF-8 text, drop the partial character
            return valid ? convert(CP_UTF8, 0, utf8.substr(0, valid)) : std::wstring();
        }
    }
    w = convert(CP_ACP, 0, utf8);
    return w.empty() ? convert(CP_UTF8, 0, utf8) : w;
}

inline std::string WideToUtf8(std::wstring_view wide) {
    if (wide.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), len, nullptr, nullptr);
    return out;
}

// Non-locking shared file reader
// Bytes as hex pairs ("4D 5A 90"), sep between them (0 for none); past maxBytes, " ..." ends the text
inline std::wstring HexBytes(const uint8_t* p, size_t n, wchar_t sep = L' ', size_t maxBytes = SIZE_MAX) {
    static const wchar_t kHex[] = L"0123456789ABCDEF";
    const size_t lim = (n < maxBytes) ? n : maxBytes;
    std::wstring s;
    s.reserve(lim * 3 + 4);
    for (size_t i = 0; i < lim; ++i) {
        if (i > 0 && sep) s.push_back(sep);
        s.push_back(kHex[p[i] >> 4]);
        s.push_back(kHex[p[i] & 0x0F]);
    }
    if (n > lim) s += L" ...";
    return s;
}

inline bool ReadFileBytes(const std::wstring& fullPath, size_t maxBytes, std::vector<uint8_t>& outBytes, uint64_t* outTotalFileSize = nullptr) {
    outBytes.clear();
    if (outTotalFileSize) *outTotalFileSize = 0;
    if (fullPath.empty()) return false;

    HANDLE hFile = CreateFileW(fullPath.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER li = {};
    if (!GetFileSizeEx(hFile, &li) || li.QuadPart < 0) {
        CloseHandle(hFile);
        return false;
    }

    uint64_t totalSz = static_cast<uint64_t>(li.QuadPart);
    if (outTotalFileSize) *outTotalFileSize = totalSz;

    size_t toRead = static_cast<size_t>(std::min<uint64_t>(totalSz, maxBytes));
    if (toRead > 0) {
        outBytes.resize(toRead);
        DWORD bytesRead = 0;
        if (!ReadFile(hFile, outBytes.data(), static_cast<DWORD>(toRead), &bytesRead, nullptr)) {
            CloseHandle(hFile);
            outBytes.clear();
            return false;
        }
        outBytes.resize(bytesRead);
    }
    CloseHandle(hFile);
    return true;
}

inline bool WriteFileBytes(const std::wstring& fullPath, const void* data, size_t byteCount) {
    if (fullPath.empty()) return false;
    HANDLE hFile = CreateFileW(fullPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = (byteCount == 0) ? TRUE : WriteFile(hFile, data, static_cast<DWORD>(byteCount), &written, nullptr);
    CloseHandle(hFile);
    return ok != FALSE && written == static_cast<DWORD>(byteCount);
}

#if defined(_WIN32) || defined(E2_WIN32_HAL)
inline bool CopyWideToClipboard(HWND ownerHwnd, const std::wstring& text) {
    if (!OpenClipboard(ownerHwnd)) return false;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!hMem) {
        CloseClipboard();
        return false;
    }
    void* ptr = GlobalLock(hMem);
    if (ptr) {
        std::memcpy(ptr, text.c_str(), bytes);
        GlobalUnlock(hMem);
        SetClipboardData(CF_UNICODETEXT, hMem);
    } else {
        GlobalFree(hMem);
    }
    CloseClipboard();
    return true;
}
#endif

inline std::wstring FormatByteSize(uint64_t bytes) {
    wchar_t buf[64] = {};
    if (bytes < 1024ULL) {
        swprintf_s(buf, L"%llu B", static_cast<unsigned long long>(bytes));
    } else if (bytes < 1024ULL * 1024ULL) {
        swprintf_s(buf, L"%.1f KB", static_cast<double>(bytes) / 1024.0);
    } else if (bytes < 1024ULL * 1024ULL * 1024ULL) {
        swprintf_s(buf, L"%.2f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    } else {
        swprintf_s(buf, L"%.2f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    }
    return buf;
}

#if defined(_WIN32) || defined(E2_WIN32_HAL)
// Shared Direct2D color & UI helpers
inline D2D1_COLOR_F ColorFromRgb(uint32_t rgb, float a = 1.0f) {
    return D2D1::ColorF(
        ((rgb >> 16) & 0xFF) / 255.0f,
        ((rgb >> 8) & 0xFF) / 255.0f,
        (rgb & 0xFF) / 255.0f,
        a);
}

// A toolbar button of the preview pane's viewers. themed = false keeps the light look in the dark theme too, for a
// toolbar whose own background is not themed (the PCB viewer).
inline void DrawToolbarButton(
    ID2D1RenderTarget* rt,
    ID2D1SolidColorBrush* brush,
    IDWriteTextFormat* fmt,
    const D2D1_RECT_F& r,
    const wchar_t* label,
    bool active,
    bool hovered,
    bool danger = false,
    bool themed = true)
{
    if (!rt || !brush || !fmt || !label) return;
    auto col = [themed](uint32_t rgb) { return ColorFromRgb(themed ? ThemedPaneRgb(rgb) : rgb); };
    if (danger) {
        brush->SetColor(hovered ? col(0xFEE2E2) : col(0xFFF1F2));
        rt->FillRoundedRectangle(D2D1::RoundedRect(r, 3.0f, 3.0f), brush);
        brush->SetColor(hovered ? col(0xEF4444) : col(0xFECDD3));
        rt->DrawRoundedRectangle(D2D1::RoundedRect(r, 3.0f, 3.0f), brush, 1.0f);
        brush->SetColor(col(0xDC2626));
    } else {
        brush->SetColor(active ? col(0xDBEAFE) : (hovered ? col(0xE2E8F0) : col(0xFFFFFF)));
        rt->FillRoundedRectangle(D2D1::RoundedRect(r, 3.0f, 3.0f), brush);
        brush->SetColor(active ? col(0x0067B8) : col(0xCBD5E1));
        rt->DrawRoundedRectangle(D2D1::RoundedRect(r, 3.0f, 3.0f), brush, 1.0f);
        brush->SetColor(active ? col(0x005A9E) : col(0x1E293B));
    }
    DWRITE_TEXT_ALIGNMENT prevAlign = fmt->GetTextAlignment();
    fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    rt->DrawText(label, static_cast<UINT32>(wcslen(label)), fmt, r, brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    fmt->SetTextAlignment(prevAlign);
}

// Creates a single-line, vertically centered text format that trims overflowing text with an ellipsis
inline void CreateTrimmedTextFormat(
    IDWriteFactory* dwriteFactory,
    const wchar_t* family,
    DWRITE_FONT_WEIGHT weight,
    float fontSize,
    IDWriteTextFormat** outFmt)
{
    if (!outFmt) return;
    *outFmt = nullptr;
    if (!dwriteFactory) return;
    dwriteFactory->CreateTextFormat(
        family, nullptr,
        weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        fontSize, L"pl-PL", outFmt);
    if (*outFmt) {
        (*outFmt)->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        (*outFmt)->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        DWRITE_TRIMMING trim = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
        IDWriteInlineObject* ellipsis = nullptr;
        dwriteFactory->CreateEllipsisTrimmingSign(*outFmt, &ellipsis);
        (*outFmt)->SetTrimming(&trim, ellipsis);
        if (ellipsis) ellipsis->Release();
    }
}

// Returns s directly when already single-line, or flattens CR/LF/TAB runs into spaces in scratch
inline std::wstring_view SingleLineView(std::wstring_view s, std::wstring& scratch) {
    if (s.find_first_of(L"\r\n\t") == std::wstring_view::npos) {
        return s;
    }
    scratch.clear();
    scratch.reserve(s.size());
    bool prevSpace = false;
    for (size_t i = 0; i < s.size(); ++i) {
        wchar_t ch = s[i];
        if (ch == L'\r' || ch == L'\n' || ch == L'\t') {
            if (ch == L'\r' && i + 1 < s.size() && s[i + 1] == L'\n') ++i;
            if (!prevSpace && !scratch.empty()) {
                scratch.push_back(L' ');
                prevSpace = true;
            }
        } else {
            scratch.push_back(ch);
            prevSpace = (ch == L' ');
        }
    }
    return scratch;
}

// Draws text strictly on one line and clipped to rect so a cell or column never bleeds into neighbors
inline void DrawSingleLineClipped(
    ID2D1RenderTarget* rt,
    std::wstring_view text,
    IDWriteTextFormat* fmt,
    const D2D1_RECT_F& rect,
    ID2D1Brush* brush,
    std::wstring& scratch)
{
    if (!rt || !fmt || !brush || text.empty() || rect.right <= rect.left || rect.bottom <= rect.top) return;
    std::wstring_view line = SingleLineView(text, scratch);
    if (!line.empty()) {
        rt->DrawText(line.data(), static_cast<UINT32>(line.size()), fmt, rect, brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
}

// Computes an aspect-ratio-preserving centered rectangle inside bounds
inline D2D1_RECT_F FitRectCentered(const D2D1_RECT_F& bounds, float srcW, float srcH) {
    float availW = (std::max)(1.0f, bounds.right - bounds.left);
    float availH = (std::max)(1.0f, bounds.bottom - bounds.top);
    float scale = (std::min)(availW / (std::max)(1.0f, srcW), availH / (std::max)(1.0f, srcH));
    float dw = srcW * scale;
    float dh = srcH * scale;
    float cx = (bounds.left + bounds.right) * 0.5f;
    float cy = (bounds.top + bounds.bottom) * 0.5f;
    return D2D1::RectF(cx - dw * 0.5f, cy - dh * 0.5f, cx + dw * 0.5f, cy + dh * 0.5f);
}

inline bool PtInRectF(float x, float y, const D2D1_RECT_F& r) {
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}
#endif // _WIN32

} // namespace Explorer2::PathUtils
