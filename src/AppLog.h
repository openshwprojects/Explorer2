#pragma once
// Per-start application log: every launch creates its own file in %LOCALAPPDATA%\Explorer2\Logs and keeps the
// most recent entries in memory for the "Log" tab. Thread-safe; Write() may be called from any thread.
#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>

namespace Explorer2 {

enum class LogLevel { Info = 0, Ok = 1, Warn = 2, Error = 3 };

struct LogEntry {
    SYSTEMTIME  time = {};
    LogLevel    level = LogLevel::Info;
    std::wstring category;   // e.g. "Rename", "Delete", "Git", "LAN"
    std::wstring message;
};

class AppLog {
public:
    // Creates the log file of this run (header with version / pid / elevation) and prunes old files.
    static void Init();
    // Makes an unhandled exception leave a record at the end of the log file before Windows takes over:
    // the exception code, the faulting address and the call stack, each as "module+offset".
    static void InstallCrashHandler();
    // Where the crash record goes when no log file is open. The self-tests use this: they run without a
    // log file, so that test runs do not push the logs of real sessions out of the log folder.
    static void SetCrashRecordFile(const std::wstring& path);
    // Optional: window that gets `msg` posted (coalesced) whenever a new entry arrives.
    static void SetNotify(HWND hwnd, UINT msg);

    static void Write(LogLevel level, const std::wstring& category, const std::wstring& message);
    static void Info (const std::wstring& category, const std::wstring& message) { Write(LogLevel::Info,  category, message); }
    static void Ok   (const std::wstring& category, const std::wstring& message) { Write(LogLevel::Ok,    category, message); }
    static void Warn (const std::wstring& category, const std::wstring& message) { Write(LogLevel::Warn,  category, message); }
    static void Error(const std::wstring& category, const std::wstring& message) { Write(LogLevel::Error, category, message); }

    static std::vector<LogEntry> Snapshot();     // oldest first
    static uint64_t Version();                   // grows with every entry
    static void ClearView();                     // forgets the in-memory entries only (the file stays intact)
    static std::wstring FilePath();              // current run's file
    static std::wstring Directory();             // folder with all runs
    static std::wstring FormatLine(const LogEntry& e);
    static const wchar_t* LevelName(LogLevel l);
};

} // namespace Explorer2
