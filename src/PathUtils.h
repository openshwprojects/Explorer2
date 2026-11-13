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
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <shlwapi.h>
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

inline std::wstring TrimTrailingSlashes(std::wstring_view sv) {
    std::wstring out(sv);
    while (!out.empty() && (out.back() == L'\\' || out.back() == L'/')) {
        if (out.size() == 3 && out[1] == L':') break; // Preserve "C:\"
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
        out.push_back(L'\\');
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

inline std::wstring GetParentPath(std::wstring_view sv) {
    std::wstring clean = TrimTrailingSlashes(sv);
    size_t slash = clean.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return L"";
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

// Fast UTF-8 <-> UTF-16 conversions
inline std::wstring Utf8ToWide(std::string_view utf8) {
    if (utf8.empty()) return L"";
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (wlen <= 0) {
        wlen = MultiByteToWideChar(CP_ACP, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
        if (wlen <= 0) return L"";
        std::wstring out(static_cast<size_t>(wlen), L'\0');
        MultiByteToWideChar(CP_ACP, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), wlen);
        return out;
    }
    std::wstring out(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), wlen);
    return out;
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

// Shared Direct2D color & UI helpers
inline D2D1_COLOR_F ColorFromRgb(uint32_t rgb, float a = 1.0f) {
    return D2D1::ColorF(
        ((rgb >> 16) & 0xFF) / 255.0f,
        ((rgb >> 8) & 0xFF) / 255.0f,
        (rgb & 0xFF) / 255.0f,
        a);
}

inline void DrawToolbarButton(
    ID2D1RenderTarget* rt,
    ID2D1SolidColorBrush* brush,
    IDWriteTextFormat* fmt,
    const D2D1_RECT_F& r,
    const wchar_t* label,
    bool active,
    bool hovered,
    bool danger = false)
{
    if (!rt || !brush || !fmt || !label) return;
    if (danger) {
        brush->SetColor(hovered ? ColorFromRgb(0xFEE2E2) : ColorFromRgb(0xFFF1F2));
        rt->FillRoundedRectangle(D2D1::RoundedRect(r, 3.0f, 3.0f), brush);
        brush->SetColor(hovered ? ColorFromRgb(0xEF4444) : ColorFromRgb(0xFECDD3));
        rt->DrawRoundedRectangle(D2D1::RoundedRect(r, 3.0f, 3.0f), brush, 1.0f);
        brush->SetColor(ColorFromRgb(0xDC2626));
    } else {
        brush->SetColor(active ? ColorFromRgb(0xDBEAFE) : (hovered ? ColorFromRgb(0xE2E8F0) : ColorFromRgb(0xFFFFFF)));
        rt->FillRoundedRectangle(D2D1::RoundedRect(r, 3.0f, 3.0f), brush);
        brush->SetColor(active ? ColorFromRgb(0x0067B8) : ColorFromRgb(0xCBD5E1));
        rt->DrawRoundedRectangle(D2D1::RoundedRect(r, 3.0f, 3.0f), brush, 1.0f);
        brush->SetColor(active ? ColorFromRgb(0x005A9E) : ColorFromRgb(0x1E293B));
    }
    DWRITE_TEXT_ALIGNMENT prevAlign = fmt->GetTextAlignment();
    fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    rt->DrawText(label, static_cast<UINT32>(wcslen(label)), fmt, r, brush);
    fmt->SetTextAlignment(prevAlign);
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

} // namespace Explorer2::PathUtils
