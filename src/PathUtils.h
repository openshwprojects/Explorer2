#pragma once
#define NOMINMAX
#include <windows.h>
#include <d2d1.h>
#include <shlwapi.h>
#include <string>
#include <string_view>
#include <algorithm>
#include <cwctype>

namespace Explorer2::PathUtils {

inline std::wstring ToLower(std::wstring_view sv) {
    std::wstring out(sv);
    for (wchar_t& ch : out) {
        ch = static_cast<wchar_t>(towlower(ch));
    }
    return out;
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
    const wchar_t* base = PathFindFileNameW(path.c_str());
    return (base && *base) ? std::wstring(base) : path;
}

inline std::wstring GetStem(const std::wstring& fileName, bool isDirectory) {
    if (isDirectory) return fileName;
    size_t dot = fileName.find_last_of(L'.');
    return (dot != std::wstring::npos && dot > 0) ? fileName.substr(0, dot) : fileName;
}

// Computes an aspect-ratio-preserving centered rectangle inside bounds
inline D2D1_RECT_F FitRectCentered(const D2D1_RECT_F& bounds, float srcW, float srcH) {
    float availW = std::max(1.0f, bounds.right - bounds.left);
    float availH = std::max(1.0f, bounds.bottom - bounds.top);
    float scale = std::min(availW / std::max(1.0f, srcW), availH / std::max(1.0f, srcH));
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
