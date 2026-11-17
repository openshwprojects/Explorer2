#pragma once
#include <windows.h>
#include <string>

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0   // Windows 8.1+: wParam = new DPI (LOWORD x, HIWORD y), lParam = suggested window RECT*
#endif

namespace Explorer2 {

// mfplat.dll (Media Foundation) is delay-loaded (see /DELAYLOAD in build.bat): the "N" editions of Windows 7, 10
// and 11 do not have it unless the Media Feature Pack is installed, and a plain import would stop Explorer2.exe
// from starting at all there ("MFPlat.DLL was not found"). Calling any MF function without the DLL raises a
// delay-load exception, so every Media Foundation entry point (MFStartup) must check this first.
inline bool IsMediaFoundationAvailable() {
    static const bool available = []() {
        wchar_t sysDir[MAX_PATH] = {};
        const UINT n = GetSystemDirectoryW(sysDir, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) return false;
        // Full System32 path (LOAD_LIBRARY_SEARCH_SYSTEM32 needs KB2533623 on Windows 7). Kept loaded on purpose.
        const std::wstring path = std::wstring(sysDir) + L"\\mfplat.dll";
        return LoadLibraryW(path.c_str()) != nullptr;
    }();
    return available;
}

// The DPI a window is shown at (96 = 100% scaling). res\Explorer2.manifest makes Explorer2 per-monitor DPI aware
// (Windows 8.1+; Windows 7 and 8 only know one system DPI). GetDpiForWindow needs Windows 10 1607 and
// GetDpiForMonitor needs shcore.dll (Windows 8.1+), so both are looked up at run time: a static import of either
// would stop the exe from starting on Windows 7 (tools/check_win7_api.ps1 fails the build on that).
inline UINT GetWindowDpiCompat(HWND hwnd) {
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    static const GetDpiForWindowFn getDpiForWindow = []() -> GetDpiForWindowFn {
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        return user32 ? reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(user32, "GetDpiForWindow")) : nullptr;
    }();
    if (hwnd && getDpiForWindow) {
        const UINT dpi = getDpiForWindow(hwnd);
        if (dpi >= 48) return dpi;
    }

    using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
    static const GetDpiForMonitorFn getDpiForMonitor = []() -> GetDpiForMonitorFn {
        wchar_t sysDir[MAX_PATH] = {};
        const UINT n = GetSystemDirectoryW(sysDir, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) return nullptr;
        // Full System32 path, kept loaded on purpose; the load fails on Windows 7 / 8, which have no shcore.dll
        HMODULE shcore = LoadLibraryW((std::wstring(sysDir) + L"\\shcore.dll").c_str());
        return shcore ? reinterpret_cast<GetDpiForMonitorFn>(GetProcAddress(shcore, "GetDpiForMonitor")) : nullptr;
    }();
    if (hwnd && getDpiForMonitor) {
        UINT dpiX = 0, dpiY = 0;
        const int kEffectiveDpi = 0;   // MDT_EFFECTIVE_DPI
        if (SUCCEEDED(getDpiForMonitor(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), kEffectiveDpi, &dpiX, &dpiY)) && dpiX >= 48) {
            return dpiX;
        }
    }

    // Windows 7 / 8: one DPI for every monitor
    HDC screen = GetDC(nullptr);
    const int systemDpi = screen ? GetDeviceCaps(screen, LOGPIXELSX) : 96;
    if (screen) ReleaseDC(nullptr, screen);
    return systemDpi >= 48 ? static_cast<UINT>(systemDpi) : 96u;
}

} // namespace Explorer2
