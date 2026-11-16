// Win7ApiShims.cpp - Windows 8+ functions that the C++ runtime library calls directly, made safe for Windows 7.
//
// Newer MSVC toolsets (the C++ standard library in VS 2022 17.x after Microsoft dropped Windows 7 as a target)
// call GetSystemTimePreciseAsFileTime directly, e.g. from std::chrono::system_clock::now() and timed waits. That
// puts it into Explorer2.exe's import table, and Windows 7 then refuses to start the exe ("The procedure entry
// point GetSystemTimePreciseAsFileTime could not be located in KERNEL32.dll"). tools/check_win7_api.ps1 in CI
// catches this. Older toolsets (14.36, used locally) look the function up themselves and never get here.
//
// A call to an imported function goes through the pointer __imp_<name>. The linker takes symbols from the
// program's own object files before searching import libraries, so defining __imp_GetSystemTimePreciseAsFileTime
// here satisfies every call in the program (ours and the C++ library's) and kernel32.lib is never asked for it.
// The pointer leads to a function that uses the real one where Windows has it (Windows 8+) and otherwise
// GetSystemTimeAsFileTime: the same UTC time, with timer-tick (~1-16 ms) instead of sub-microsecond resolution.
//
// Do not call GetSystemTimePreciseAsFileTime in this file: that call would go through the pointer defined below.
#include <windows.h>

#if defined(_M_X64) || defined(_M_ARM64)   // undecorated C names; Explorer2 is built for x64 only

namespace {

using PreciseTimeFn = VOID(WINAPI*)(LPFILETIME);

VOID WINAPI GetSystemTimePreciseAsFileTimeCompat(LPFILETIME ft) {
    static const PreciseTimeFn real = []() -> PreciseTimeFn {
        HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
        return kernel32 ? reinterpret_cast<PreciseTimeFn>(GetProcAddress(kernel32, "GetSystemTimePreciseAsFileTime")) : nullptr;
    }();
    if (real) real(ft);
    else GetSystemTimeAsFileTime(ft);      // Windows 7
}

} // namespace

extern "C" PreciseTimeFn __imp_GetSystemTimePreciseAsFileTime = &GetSystemTimePreciseAsFileTimeCompat;

#endif
