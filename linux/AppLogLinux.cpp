// AppLogLinux.cpp - AppLog for the Linux build of the readers (src/AppLog.cpp is the Windows one).
// Entries stay in memory; with EXPLORER2_LOG_STDERR set they are also printed to stderr.
#include "AppLog.h"
#include "PathUtils.h"
#include <cstdlib>
#include <ctime>
#include <deque>
#include <mutex>
#include <sys/time.h>

namespace Explorer2 {

namespace {
std::mutex g_mutex;
std::deque<LogEntry> g_entries;
uint64_t g_version = 0;
constexpr size_t kMaxEntries = 2000;

SYSTEMTIME NowLocal() {
    SYSTEMTIME st = {};
    struct timeval tv = {};
    gettimeofday(&tv, nullptr);
    struct tm t = {};
    const time_t secs = tv.tv_sec;
    localtime_r(&secs, &t);
    st.wYear = static_cast<WORD>(t.tm_year + 1900);
    st.wMonth = static_cast<WORD>(t.tm_mon + 1);
    st.wDayOfWeek = static_cast<WORD>(t.tm_wday);
    st.wDay = static_cast<WORD>(t.tm_mday);
    st.wHour = static_cast<WORD>(t.tm_hour);
    st.wMinute = static_cast<WORD>(t.tm_min);
    st.wSecond = static_cast<WORD>(t.tm_sec);
    st.wMilliseconds = static_cast<WORD>(tv.tv_usec / 1000);
    return st;
}
} // namespace

void AppLog::Init() {}
void AppLog::InstallCrashHandler() {}
void AppLog::SetCrashRecordFile(const std::wstring&) {}
void AppLog::SetNotify(HWND, UINT) {}

void AppLog::Write(LogLevel level, const std::wstring& category, const std::wstring& message) {
    LogEntry e;
    e.time = NowLocal();
    e.level = level;
    e.category = category;
    e.message = message;
    static const bool toStderr = std::getenv("EXPLORER2_LOG_STDERR") != nullptr;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (toStderr) std::fprintf(stderr, "%s\n", PathUtils::WideToUtf8(FormatLine(e)).c_str());
    g_entries.push_back(std::move(e));
    if (g_entries.size() > kMaxEntries) g_entries.pop_front();
    ++g_version;
}

std::vector<LogEntry> AppLog::Snapshot() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return std::vector<LogEntry>(g_entries.begin(), g_entries.end());
}

uint64_t AppLog::Version() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_version;
}

void AppLog::ClearView() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_entries.clear();
    ++g_version;
}

std::wstring AppLog::FilePath() { return L""; }
std::wstring AppLog::Directory() { return L""; }

const wchar_t* AppLog::LevelName(LogLevel l) {
    switch (l) {
    case LogLevel::Ok: return L"OK";
    case LogLevel::Warn: return L"WARN";
    case LogLevel::Error: return L"ERROR";
    default: return L"INFO";
    }
}

std::wstring AppLog::FormatLine(const LogEntry& e) {
    wchar_t ts[48] = {};
    swprintf_s(ts, L"%04u-%02u-%02u %02u:%02u:%02u.%03u", e.time.wYear, e.time.wMonth, e.time.wDay,
               e.time.wHour, e.time.wMinute, e.time.wSecond, e.time.wMilliseconds);
    return std::wstring(ts) + L"  " + LevelName(e.level) + L"  [" + e.category + L"]  " + e.message;
}

} // namespace Explorer2
