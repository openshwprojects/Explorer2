#include "ExplorerApp.h"
#include "Monitoring.h"
#include <objbase.h>
#include <shellapi.h>
#include <cwchar>

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR lpCmdLine, int nCmdShow) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    // Hidden background recorder (one per user session, shared by every Explorer2 window)
    if (lpCmdLine && wcsstr(lpCmdLine, L"--monitor-daemon")) {
        int code = Explorer2::MonitorDaemon::Run();
        CoUninitialize();
        return code;
    }

    // Diagnostics: `Explorer2.exe --dump-hw <file>` writes raw SMART / thermal / CPU data and exits (no window)
    if (lpCmdLine && wcsstr(lpCmdLine, L"--dump-hw")) {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(lpCmdLine, &argc);
        if (argv && argc >= 2) {
            Explorer2::HardwareInspector hw;
            hw.InitializeStaticInventory(true);
            Sleep(1500);
            hw.UpdateLiveTelemetry(true);
            std::wstring dump = hw.BuildDiagnosticsDump();
            int len = WideCharToMultiByte(CP_UTF8, 0, dump.c_str(), static_cast<int>(dump.size()), nullptr, 0, nullptr, nullptr);
            std::string utf8(static_cast<size_t>(len), '\0');
            WideCharToMultiByte(CP_UTF8, 0, dump.c_str(), static_cast<int>(dump.size()), utf8.data(), len, nullptr, nullptr);
            HANDLE f = CreateFileW(argv[1], GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (f != INVALID_HANDLE_VALUE) {
                DWORD written = 0;
                WriteFile(f, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
                CloseHandle(f);
            }
        }
        if (argv) LocalFree(argv);
        CoUninitialize();
        return 0;
    }

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
