#include "ExplorerApp.h"
#include "Monitoring.h"
#include "SelfTestSuite.h"
#include <objbase.h>
#include <shellapi.h>
#include <cwchar>
#include <memory>

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR lpCmdLine, int nCmdShow) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    // Automated self-test runner: `Explorer2.exe --self-test <stage> <reportFile>`
    if (lpCmdLine && wcsstr(lpCmdLine, L"--self-test")) {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(lpCmdLine, &argc);
        std::wstring stage = L"all";
        std::wstring reportFile = L"build\\selftest_report.txt";
        if (argv) {
            for (int i = 0; i < argc; ++i) {
                if (wcscmp(argv[i], L"--self-test") == 0) {
                    if (i + 1 < argc) stage = argv[i + 1];
                    if (i + 2 < argc) reportFile = argv[i + 2];
                    break;
                }
            }
            LocalFree(argv);
        }
        int code = Explorer2::SelfTestSuite::RunStageTests(stage, reportFile);
        CoUninitialize();
        return code;
    }

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

    auto app = std::make_unique<Explorer2::ExplorerApp>();
    int exitCode = 0;
    if (app->Initialize(hInstance, nCmdShow, initialPath)) {
        exitCode = app->RunMessageLoop();
    }
    app.reset();

    CoUninitialize();
    return exitCode;
}
