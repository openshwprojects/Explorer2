#pragma once
#include <windows.h>
#include <string>

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

} // namespace Explorer2
