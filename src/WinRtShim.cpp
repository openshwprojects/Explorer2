// WinRtShim.cpp - the two Windows Runtime functions C++/WinRT needs, looked up in combase.dll at run time.
//
// Explorer2 must start on Windows 7, which has no combase.dll / Windows Runtime. Newer C++/WinRT headers
// (Windows SDK 10.0.22621+, as on the GitHub runners) call WINRT_IMPL_RoGetActivationFactory and
// WINRT_IMPL_RoOriginateLanguageException, which the linker maps (/alternatename) to RoGetActivationFactory /
// RoOriginateLanguageException from WindowsApp.lib or runtimeobject.lib. Those libraries would put combase /
// api-ms-win-core-winrt-*.dll into the import table and Windows 7 would refuse to start the exe, so they are not
// linked (see build.bat). Defining the WINRT_IMPL_ symbols here wins over the /alternatename fallback.
// Older C++/WinRT headers (SDK 10.0.22000 and before) find these functions by themselves and never call the
// definitions below; they are harmless there.
//
// Without combase.dll the functions fail with E_NOTIMPL; C++/WinRT then throws winrt::hresult_error, and the PDF
// renderer (the only Windows Runtime user, PdfViewer.cpp) catches it and shows no page picture.
//
// The signatures must match base.h ("std::int32_t __stdcall WINRT_IMPL_RoGetActivationFactory(void* classId,
// winrt::guid const& iid, void** factory) noexcept" and "...RoOriginateLanguageException(std::int32_t error,
// void* message, void* exception) noexcept"). This file deliberately does not include the C++/WinRT headers.
#include <windows.h>
#include <cstdint>
#include <string>

namespace {

FARPROC GetCombaseProc(const char* name) {
    static const HMODULE combase = []() -> HMODULE {
        wchar_t sysDir[MAX_PATH] = {};
        const UINT n = GetSystemDirectoryW(sysDir, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) return nullptr;
        // Full System32 path: LOAD_LIBRARY_SEARCH_SYSTEM32 needs KB2533623 on Windows 7. Kept loaded on purpose.
        return LoadLibraryW((std::wstring(sysDir) + L"\\combase.dll").c_str());
    }();
    return combase ? GetProcAddress(combase, name) : nullptr;
}

} // namespace

extern "C" std::int32_t __stdcall WINRT_IMPL_RoGetActivationFactory(void* classId, GUID const& iid, void** factory) noexcept {
    using Fn = HRESULT(WINAPI*)(void*, REFIID, void**);
    static const Fn fn = reinterpret_cast<Fn>(GetCombaseProc("RoGetActivationFactory"));
    if (!fn) {
        if (factory) *factory = nullptr;
        return E_NOTIMPL;           // no Windows Runtime (Windows 7)
    }
    return fn(classId, iid, factory);
}

extern "C" std::int32_t __stdcall WINRT_IMPL_RoOriginateLanguageException(std::int32_t error, void* message, void* exception) noexcept {
    using Fn = BOOL(WINAPI*)(HRESULT, void*, void*);
    static const Fn fn = reinterpret_cast<Fn>(GetCombaseProc("RoOriginateLanguageException"));
    return fn ? fn(error, message, exception) : FALSE;     // only error reporting: nothing to do without it
}
