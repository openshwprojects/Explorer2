// linux/win32_hal/src/WinMainEntry.cpp
// Linux main() entry point forwarding argc/argv UTF-8 command line to Explorer2's wWinMain().

#include <windows.h>
#include <string>

namespace Explorer2::Hal {
void SetCommandLineFromMain(int argc, char** argv);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow);

int main(int argc, char** argv) {
    Explorer2::Hal::SetCommandLineFromMain(argc, argv);

    // Build lpCmdLine (everything after argv[0], matching Win32 wWinMain semantics)
    std::wstring full = GetCommandLineW();
    const wchar_t* p = full.c_str();
    while (*p == L' ' || *p == L'\t') ++p;
    if (*p == L'"') {
        ++p;
        while (*p && *p != L'"') {
            if (*p == L'\\' && p[1]) ++p;
            ++p;
        }
        if (*p == L'"') ++p;
    } else {
        while (*p && *p != L' ' && *p != L'\t') ++p;
    }
    while (*p == L' ' || *p == L'\t') ++p;
    std::wstring argsOnly(p);

    return wWinMain(GetModuleHandleW(nullptr), nullptr, argsOnly.data(), SW_SHOWNORMAL);
}
