#include "ExplorerApp.h"
#include <objbase.h>
#include <shellapi.h>

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR lpCmdLine, int nCmdShow) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    std::wstring initialPath;
    if (lpCmdLine && *lpCmdLine) {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(lpCmdLine, &argc);
        if (argv && argc > 0) {
            initialPath = argv[0];
            LocalFree(argv);
        }
    }

    Explorer2::ExplorerApp app;
    int exitCode = 0;
    if (app.Initialize(hInstance, nCmdShow, initialPath)) {
        exitCode = app.RunMessageLoop();
    }

    CoUninitialize();
    return exitCode;
}
