#include "AppLog.h"
#include <shlobj.h>
#include <algorithm>
#include <deque>
#include <mutex>
#include <atomic>

namespace Explorer2 {

namespace {
    std::mutex               g_mutex;
    std::deque<LogEntry>     g_entries;
    std::atomic<uint64_t>    g_version{ 0 };
    HANDLE                   g_file = INVALID_HANDLE_VALUE;
    std::wstring             g_filePath;
    std::wstring             g_dir;
    HWND                     g_notifyHwnd = nullptr;
    UINT                     g_notifyMsg = 0;
    std::atomic<bool>        g_notifyPending{ false };
    constexpr size_t         kMaxMemoryEntries = 4000;
    constexpr int            kKeepFiles = 40;
    constexpr int            kKeepDays = 30;

    std::string ToUtf8(const std::wstring& w) {
        if (w.empty()) return {};
        int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
        std::string s(static_cast<size_t>(n), '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
        return s;
    }

    void AppendToFile(const std::wstring& line) {
        if (g_file == INVALID_HANDLE_VALUE) return;
        std::string u = ToUtf8(line + L"\r\n");
        DWORD written = 0;
        WriteFile(g_file, u.data(), static_cast<DWORD>(u.size()), &written, nullptr);   // unbuffered: survives a crash
    }

    bool IsElevated() {
        HANDLE tok = nullptr;
        bool elevated = false;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
            TOKEN_ELEVATION te = {};
            DWORD sz = 0;
            if (GetTokenInformation(tok, TokenElevation, &te, sizeof(te), &sz)) elevated = te.TokenIsElevated != 0;
            CloseHandle(tok);
        }
        return elevated;
    }

    void PruneOldFiles() {
        struct F { std::wstring path; FILETIME ft; };
        std::vector<F> files;
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((g_dir + L"\\explorer2-*.log").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return;
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) files.push_back({ g_dir + L"\\" + fd.cFileName, fd.ftLastWriteTime });
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        std::sort(files.begin(), files.end(), [](const F& a, const F& b) { return CompareFileTime(&a.ft, &b.ft) > 0; });

        FILETIME nowFt; GetSystemTimeAsFileTime(&nowFt);
        ULARGE_INTEGER now; now.LowPart = nowFt.dwLowDateTime; now.HighPart = nowFt.dwHighDateTime;
        const ULONGLONG maxAge = static_cast<ULONGLONG>(kKeepDays) * 24ULL * 3600ULL * 10000000ULL;
        for (size_t i = 0; i < files.size(); ++i) {
            if (files[i].path == g_filePath) continue;
            ULARGE_INTEGER t; t.LowPart = files[i].ft.dwLowDateTime; t.HighPart = files[i].ft.dwHighDateTime;
            bool tooOld = now.QuadPart > t.QuadPart && (now.QuadPart - t.QuadPart) > maxAge;
            if (static_cast<int>(i) >= kKeepFiles || tooOld) DeleteFileW(files[i].path.c_str());
        }
    }
}

void AppLog::Init() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file != INVALID_HANDLE_VALUE) return;

    PWSTR local = nullptr;
    std::wstring base;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local)) && local) base = local;
    if (local) CoTaskMemFree(local);
    if (base.empty()) return;
    base += L"\\Explorer2";
    CreateDirectoryW(base.c_str(), nullptr);
    g_dir = base + L"\\Logs";
    CreateDirectoryW(g_dir.c_str(), nullptr);

    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t name[96];
    swprintf_s(name, L"\\explorer2-%04d%02d%02d-%02d%02d%02d-%lu.log", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, GetCurrentProcessId());
    g_filePath = g_dir + name;
    g_file = CreateFileW(g_filePath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (g_file != INVALID_HANDLE_VALUE) {
        const unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
        DWORD w = 0; WriteFile(g_file, bom, 3, &w, nullptr);
    }
    PruneOldFiles();
}

void AppLog::SetNotify(HWND hwnd, UINT msg) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_notifyHwnd = hwnd;
    g_notifyMsg = msg;
}

void AppLog::Write(LogLevel level, const std::wstring& category, const std::wstring& message) {
    LogEntry e;
    GetLocalTime(&e.time);
    e.level = level;
    e.category = category;
    e.message = message;
    HWND notify = nullptr; UINT msg = 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        AppendToFile(FormatLine(e));
        g_entries.push_back(e);
        while (g_entries.size() > kMaxMemoryEntries) g_entries.pop_front();
        ++g_version;
        notify = g_notifyHwnd; msg = g_notifyMsg;
    }
    if (notify && msg && !g_notifyPending.exchange(true)) {
        if (!PostMessageW(notify, msg, 0, 0)) g_notifyPending = false;
    }
}

std::vector<LogEntry> AppLog::Snapshot() {
    g_notifyPending = false;   // the UI is about to read everything that was posted so far
    std::lock_guard<std::mutex> lock(g_mutex);
    return std::vector<LogEntry>(g_entries.begin(), g_entries.end());
}

uint64_t AppLog::Version() { return g_version.load(); }

void AppLog::ClearView() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_entries.clear();
    ++g_version;
}

std::wstring AppLog::FilePath() { std::lock_guard<std::mutex> lock(g_mutex); return g_filePath; }
std::wstring AppLog::Directory() { std::lock_guard<std::mutex> lock(g_mutex); return g_dir; }

const wchar_t* AppLog::LevelName(LogLevel l) {
    switch (l) {
    case LogLevel::Ok:    return L"OK";
    case LogLevel::Warn:  return L"WARN";
    case LogLevel::Error: return L"ERROR";
    default:              return L"INFO";
    }
}

std::wstring AppLog::FormatLine(const LogEntry& e) {
    wchar_t ts[40];
    swprintf_s(ts, L"%04d-%02d-%02d %02d:%02d:%02d.%03d", e.time.wYear, e.time.wMonth, e.time.wDay, e.time.wHour, e.time.wMinute, e.time.wSecond, e.time.wMilliseconds);
    std::wstring line = ts;
    line += L"  ";
    std::wstring lv = LevelName(e.level);
    lv.resize(5, L' ');
    line += lv + L"  [" + e.category + L"]  " + e.message;
    return line;
}

} // namespace Explorer2
