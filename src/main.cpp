#include "ExplorerApp.h"
#include "AppLog.h"
#include "Monitoring.h"
#include "PdfViewer.h"
#include "SelfTestSuite.h"
#include "Version.h"
#include <objbase.h>
#include <ole2.h>
#include <shellapi.h>
#include <cwchar>
#include <cstring>
#include <memory>
#include <string>

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR lpCmdLine, int nCmdShow) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    // Version query: `Explorer2.exe --version [outFile]` writes UTF-8 version string and exits immediately
    if (lpCmdLine && wcsstr(lpCmdLine, L"--version")) {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        std::wstring outFile;
        if (argv) {
            for (int i = 1; i < argc; ++i) {
                if (wcscmp(argv[i], L"--version") == 0 && i + 1 < argc) {
                    outFile = argv[i + 1];
                    break;
                }
            }
            LocalFree(argv);
        }
        if (!outFile.empty()) {
            const std::string verLine = std::string("Explorer2 ") + E2_VERSION_FULL_STR + " [" + E2_VERSION_FILE_STR + "]\r\n";
            HANDLE f = CreateFileW(outFile.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (f != INVALID_HANDLE_VALUE) {
                DWORD written = 0;
                WriteFile(f, verLine.data(), static_cast<DWORD>(verLine.size()), &written, nullptr);
                CloseHandle(f);
            }
        }
        CoUninitialize();
        return 0;
    }

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
        // A test that takes the process down must not go unnoticed: the crash record goes next to the report
        const std::wstring crashRecord = reportFile + L".crash.txt";
        DeleteFileW(crashRecord.c_str());
        Explorer2::AppLog::SetCrashRecordFile(crashRecord);
        Explorer2::AppLog::InstallCrashHandler();
        int code = Explorer2::SelfTestSuite::RunStageTests(stage, reportFile);
        Explorer2::PdfViewer::ReleaseRenderer();
        CoUninitialize();
        return code;
    }

    // Robustness diagnostics: `Explorer2.exe --inspect-files <listFile> <resultFile>` runs the file readers on every
    // file named in the list and records what each of them did (see SelfTestSuite.h and tools/fuzz_samples.py)
    if (lpCmdLine && wcsstr(lpCmdLine, L"--inspect-files")) {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(lpCmdLine, &argc);
        int code = 2;
        if (argv) {
            for (int i = 0; i + 2 < argc; ++i) {
                if (wcscmp(argv[i], L"--inspect-files") == 0) {
                    code = Explorer2::SelfTestSuite::InspectFiles(argv[i + 1], argv[i + 2]);
                    break;
                }
            }
            LocalFree(argv);
        }
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
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv && argc > 1) {
            initialPath = argv[1];
            // Windows Explorer drive-root shell verb ("C:\") escapes the closing quote into a trailing '"'
            while (!initialPath.empty() && (initialPath.back() == L'"' || initialPath.back() == L' ')) {
                initialPath.pop_back();
            }
            if (initialPath.size() == 2 && initialPath[1] == L':') {
                initialPath.push_back(L'\\');
            }
        }
        if (argv) LocalFree(argv);
    }

    // OLE (on top of COM) for drag and drop out of the window: DoDragDrop needs it on the UI thread
    const HRESULT oleHr = OleInitialize(nullptr);

    auto app = std::make_unique<Explorer2::ExplorerApp>();
    int exitCode = 0;
    if (app->Initialize(hInstance, nCmdShow, initialPath)) {
        exitCode = app->RunMessageLoop();
    }
    app.reset();
    if (SUCCEEDED(oleHr)) OleUninitialize();

    // Unload what was only loaded on demand while the process is still whole (see PdfViewer::ReleaseRenderer)
    Explorer2::PdfViewer::ReleaseRenderer();
    CoUninitialize();
    return exitCode;
}
