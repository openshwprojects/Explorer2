#include "BackupManager.h"
#include "PathUtils.h"
#include "FileEngine.h"
#include "AppLog.h"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <cwctype>

namespace Explorer2 {

namespace {

std::wstring TrimWs(const std::wstring& s) {
    size_t a = 0;
    while (a < s.size() && (s[a] == L' ' || s[a] == L'\t' || s[a] == L'\r' || s[a] == L'\n')) ++a;
    size_t b = s.size();
    while (b > a && (s[b - 1] == L' ' || s[b - 1] == L'\t' || s[b - 1] == L'\r' || s[b - 1] == L'\n')) --b;
    return s.substr(a, b - a);
}

std::vector<std::wstring> SplitFilterPatterns(const std::wstring& filterStr) {
    std::vector<std::wstring> out;
    size_t start = 0;
    for (size_t i = 0; i <= filterStr.size(); ++i) {
        if (i == filterStr.size() || filterStr[i] == L';' || filterStr[i] == L',') {
            std::wstring tok = TrimWs(filterStr.substr(start, i - start));
            if (!tok.empty()) {
                for (auto& ch : tok) {
                    if (ch == L'\\') ch = L'/';
                }
                out.push_back(tok);
            }
            start = i + 1;
        }
    }
    return out;
}

bool EnsureDirectoryHierarchy(const std::wstring& dirPath) {
    if (dirPath.empty()) return false;
    DWORD attrs = GetFileAttributesW(dirPath.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) return true;

    std::wstring cur;
    for (size_t i = 0; i < dirPath.size(); ++i) {
        wchar_t ch = dirPath[i];
        cur.push_back(ch);
        if ((ch == L'\\' || ch == L'/') && cur.size() > 3) {
            CreateDirectoryW(cur.c_str(), nullptr);
        }
    }
    CreateDirectoryW(dirPath.c_str(), nullptr);
    attrs = GetFileAttributesW(dirPath.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY));
}

std::wstring FormatNowTimestamp() {
    SYSTEMTIME st = {};
    GetLocalTime(&st);
    wchar_t buf[64] = {};
    swprintf_s(buf, L"%04u-%02u-%02u %02u:%02u:%02u",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

bool AreFileTimesClose(const FILETIME& a, const FILETIME& b) {
    ULARGE_INTEGER ua = {}, ub = {};
    ua.LowPart = a.dwLowDateTime; ua.HighPart = a.dwHighDateTime;
    ub.LowPart = b.dwLowDateTime; ub.HighPart = b.dwHighDateTime;
    uint64_t diff = (ua.QuadPart > ub.QuadPart) ? (ua.QuadPart - ub.QuadPart) : (ub.QuadPart - ua.QuadPart);
    // 2 seconds in 100-ns intervals (20,000,000) to tolerate FAT32/exFAT/SMB timestamp granularity
    return diff <= 20000000ULL;
}

} // namespace

BackupManager::BackupManager() {
    m_configPath = DefaultConfigPath();
    LoadJobs(m_configPath);
}

BackupManager::~BackupManager() {
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
}

uint64_t BackupManager::CurrentEpochSeconds() {
    using namespace std::chrono;
    return static_cast<uint64_t>(duration_cast<seconds>(system_clock::now().time_since_epoch()).count());
}

std::wstring BackupManager::DefaultConfigPath() {
    wchar_t localAppData[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    std::wstring dir = (n > 0 && n < MAX_PATH)
        ? (std::wstring(localAppData) + L"\\Explorer2")
        : L".";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\backups.ini";
}

bool BackupManager::LoadJobs(const std::wstring& iniPathIn) {
    std::wstring path = iniPathIn.empty() ? m_configPath : iniPathIn;
    if (path.empty()) path = DefaultConfigPath();

    std::lock_guard<std::mutex> lk(m_mutex);
    m_configPath = path;
    m_jobs.clear();

    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        // Seed two helpful sample jobs (disabled by default until the user enables or edits them)
        BackupJob j1;
        j1.id = 1;
        j1.name = L"Kopia robocza projektu (przyrostowa)";
        j1.sourcePath = L"W:\\GIT\\Explorer2\\src";
        j1.targetPath = L"D:\\Backups\\Explorer2_src";
        j1.scheduleMinutes = 60;
        j1.whenTargetAvailable = true;
        j1.useRclone = false;
        j1.includeFilter = L"*";
        j1.excludeFilter = L"*.tmp;*.obj;*.pdb;.git/*;build/*";
        j1.enabled = false;
        j1.lastRunTimeStr = L"Nigdy";
        j1.lastStatus = L"Gotowe do konfiguracji";
        m_jobs.push_back(j1);

        BackupJob j2;
        j2.id = 2;
        j2.name = L"Chmura / Rclone Remote (np. gdrive:Backups)";
        j2.sourcePath = L"C:\\Users\\Public\\Documents";
        j2.targetPath = L"remote:Explorer2Backup";
        j2.scheduleMinutes = 360;
        j2.whenTargetAvailable = true;
        j2.useRclone = true;
        j2.includeFilter = L"*.docx;*.xlsx;*.pdf;*.txt";
        j2.excludeFilter = L"~$*;*.tmp";
        j2.enabled = false;
        j2.lastRunTimeStr = L"Nigdy";
        j2.lastStatus = L"Tryb rclone (wymaga rclone w PATH)";
        m_jobs.push_back(j2);
        return true;
    }

    int count = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Backups", L"Count", 0, path.c_str())), 0, 200);
    for (int i = 0; i < count; ++i) {
        wchar_t sec[32] = {};
        swprintf_s(sec, L"Job%d", i);
        wchar_t buf[1024] = {};

        BackupJob j;
        j.id = static_cast<int>(GetPrivateProfileIntW(sec, L"Id", i + 1, path.c_str()));
        GetPrivateProfileStringW(sec, L"Name", L"Zadanie kopii", buf, static_cast<DWORD>(std::size(buf)), path.c_str());
        j.name = buf;
        GetPrivateProfileStringW(sec, L"Source", L"", buf, static_cast<DWORD>(std::size(buf)), path.c_str());
        j.sourcePath = buf;
        GetPrivateProfileStringW(sec, L"Target", L"", buf, static_cast<DWORD>(std::size(buf)), path.c_str());
        j.targetPath = buf;
        j.scheduleMinutes = static_cast<int>(GetPrivateProfileIntW(sec, L"ScheduleMin", 60, path.c_str()));
        j.whenTargetAvailable = GetPrivateProfileIntW(sec, L"WhenTargetAvailable", 1, path.c_str()) != 0;
        j.useRclone = GetPrivateProfileIntW(sec, L"UseRclone", 0, path.c_str()) != 0;
        GetPrivateProfileStringW(sec, L"Include", L"*", buf, static_cast<DWORD>(std::size(buf)), path.c_str());
        j.includeFilter = buf;
        GetPrivateProfileStringW(sec, L"Exclude", L"", buf, static_cast<DWORD>(std::size(buf)), path.c_str());
        j.excludeFilter = buf;
        j.enabled = GetPrivateProfileIntW(sec, L"Enabled", 1, path.c_str()) != 0;
        GetPrivateProfileStringW(sec, L"LastRunTime", L"Nigdy", buf, static_cast<DWORD>(std::size(buf)), path.c_str());
        j.lastRunTimeStr = buf;
        GetPrivateProfileStringW(sec, L"LastStatus", L"Gotowe", buf, static_cast<DWORD>(std::size(buf)), path.c_str());
        j.lastStatus = buf;
        j.lastRunOk = GetPrivateProfileIntW(sec, L"LastRunOk", 1, path.c_str()) != 0;
        j.lastFilesCopied = static_cast<uint32_t>(GetPrivateProfileIntW(sec, L"LastFilesCopied", 0, path.c_str()));
        j.targetCurrentlyOnline = IsTargetAvailable(j.targetPath);
        j.targetWasOnline = j.targetCurrentlyOnline;
        m_jobs.push_back(std::move(j));
    }
    return true;
}

bool BackupManager::SaveJobs(const std::wstring& iniPathIn) const {
    std::wstring path = iniPathIn.empty() ? m_configPath : iniPathIn;
    if (path.empty()) path = DefaultConfigPath();

    std::lock_guard<std::mutex> lk(m_mutex);
    EnsureDirectoryHierarchy(PathUtils::GetParentPath(path));

    // Write UTF-16LE BOM if file does not exist so GetPrivateProfileStringW / WritePrivateProfileStringW
    // preserve Polish Unicode characters cleanly!
    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            const uint16_t bom = 0xFEFF;
            DWORD wr = 0;
            WriteFile(h, &bom, sizeof(bom), &wr, nullptr);
            CloseHandle(h);
        }
    }

    WritePrivateProfileStringW(L"Backups", L"Count", std::to_wstring(m_jobs.size()).c_str(), path.c_str());
    for (size_t i = 0; i < m_jobs.size(); ++i) {
        const auto& j = m_jobs[i];
        wchar_t sec[32] = {};
        swprintf_s(sec, L"Job%zu", i);
        WritePrivateProfileStringW(sec, L"Id", std::to_wstring(j.id).c_str(), path.c_str());
        WritePrivateProfileStringW(sec, L"Name", j.name.c_str(), path.c_str());
        WritePrivateProfileStringW(sec, L"Source", j.sourcePath.c_str(), path.c_str());
        WritePrivateProfileStringW(sec, L"Target", j.targetPath.c_str(), path.c_str());
        WritePrivateProfileStringW(sec, L"ScheduleMin", std::to_wstring(j.scheduleMinutes).c_str(), path.c_str());
        WritePrivateProfileStringW(sec, L"WhenTargetAvailable", j.whenTargetAvailable ? L"1" : L"0", path.c_str());
        WritePrivateProfileStringW(sec, L"UseRclone", j.useRclone ? L"1" : L"0", path.c_str());
        WritePrivateProfileStringW(sec, L"Include", j.includeFilter.c_str(), path.c_str());
        WritePrivateProfileStringW(sec, L"Exclude", j.excludeFilter.c_str(), path.c_str());
        WritePrivateProfileStringW(sec, L"Enabled", j.enabled ? L"1" : L"0", path.c_str());
        WritePrivateProfileStringW(sec, L"LastRunTime", j.lastRunTimeStr.c_str(), path.c_str());
        WritePrivateProfileStringW(sec, L"LastStatus", j.lastStatus.c_str(), path.c_str());
        WritePrivateProfileStringW(sec, L"LastRunOk", j.lastRunOk ? L"1" : L"0", path.c_str());
        WritePrivateProfileStringW(sec, L"LastFilesCopied", std::to_wstring(j.lastFilesCopied).c_str(), path.c_str());
    }
    return true;
}

std::vector<BackupJob> BackupManager::SnapshotJobs() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_jobs;
}

int BackupManager::AddJob(const BackupJob& jobIn) {
    int assignedId = 1;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        for (const auto& j : m_jobs) {
            if (j.id >= assignedId) assignedId = j.id + 1;
        }
        BackupJob j = jobIn;
        j.id = assignedId;
        if (j.lastRunTimeStr.empty()) j.lastRunTimeStr = L"Nigdy";
        if (j.lastStatus.empty()) j.lastStatus = L"Gotowe";
        j.targetCurrentlyOnline = IsTargetAvailable(j.targetPath);
        j.targetWasOnline = j.targetCurrentlyOnline;
        m_jobs.push_back(std::move(j));
    }
    SaveJobs();
    return assignedId;
}

bool BackupManager::UpdateJob(const BackupJob& job) {
    bool found = false;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        for (auto& existing : m_jobs) {
            if (existing.id == job.id) {
                existing = job;
                existing.targetCurrentlyOnline = IsTargetAvailable(existing.targetPath);
                found = true;
                break;
            }
        }
    }
    if (found) SaveJobs();
    return found;
}

bool BackupManager::DeleteJob(int jobId) {
    bool removed = false;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        auto it = std::remove_if(m_jobs.begin(), m_jobs.end(), [&](const BackupJob& j) { return j.id == jobId; });
        if (it != m_jobs.end()) {
            m_jobs.erase(it, m_jobs.end());
            removed = true;
        }
    }
    if (removed) SaveJobs();
    return removed;
}

bool BackupManager::GetJobById(int jobId, BackupJob& outJob) const {
    std::lock_guard<std::mutex> lk(m_mutex);
    for (const auto& j : m_jobs) {
        if (j.id == jobId) {
            outJob = j;
            return true;
        }
    }
    return false;
}

int BackupManager::CycleScheduleMinutes(int currentMinutes) {
    static const int kSchedules[] = { 0, 5, 15, 60, 360, 1440 };
    for (size_t i = 0; i < std::size(kSchedules); ++i) {
        if (kSchedules[i] == currentMinutes) {
            return kSchedules[(i + 1) % std::size(kSchedules)];
        }
    }
    return 60;
}

std::wstring BackupManager::FormatScheduleLabel(int scheduleMinutes, bool pl) {
    switch (scheduleMinutes) {
    case 0:    return pl ? L"Tylko ręcznie" : L"Manual only";
    case 5:    return pl ? L"Co 5 min"      : L"Every 5 min";
    case 15:   return pl ? L"Co 15 min"     : L"Every 15 min";
    case 60:   return pl ? L"Co 1 godz."    : L"Every 1 hour";
    case 360:  return pl ? L"Co 6 godz."    : L"Every 6 hours";
    case 1440: return pl ? L"Codziennie (24h)" : L"Daily (24h)";
    default:   return (pl ? L"Co " : L"Every ") + std::to_wstring(scheduleMinutes) + L" min";
    }
}

bool BackupManager::WildcardMatch(const std::wstring& text, const std::wstring& pattern) {
    size_t ti = 0, pi = 0;
    size_t starIdx = std::wstring::npos, matchIdx = 0;
    while (ti < text.size()) {
        wchar_t tc = static_cast<wchar_t>(std::towlower(text[ti]));
        wchar_t pc = (pi < pattern.size()) ? static_cast<wchar_t>(std::towlower(pattern[pi])) : 0;
        if (pi < pattern.size() && (pc == L'?' || pc == tc)) {
            ++ti;
            ++pi;
        } else if (pi < pattern.size() && pc == L'*') {
            starIdx = pi++;
            matchIdx = ti;
        } else if (starIdx != std::wstring::npos) {
            pi = starIdx + 1;
            ti = ++matchIdx;
        } else {
            return false;
        }
    }
    while (pi < pattern.size() && pattern[pi] == L'*') ++pi;
    return pi == pattern.size();
}

bool BackupManager::MatchesFilterRules(
    const std::wstring& relativePathIn,
    const std::wstring& includeFilter,
    const std::wstring& excludeFilter) {
    std::wstring rel = relativePathIn;
    for (auto& ch : rel) {
        if (ch == L'\\') ch = L'/';
    }
    while (!rel.empty() && rel.front() == L'/') rel.erase(rel.begin());

    std::wstring fileName = rel;
    size_t lastSlash = rel.find_last_of(L'/');
    if (lastSlash != std::wstring::npos) {
        fileName = rel.substr(lastSlash + 1);
    }

    auto matchesPattern = [&](const std::wstring& pat) -> bool {
        if (pat == L"*" || pat == L"*.*") return true;
        if (WildcardMatch(rel, pat) || WildcardMatch(fileName, pat)) return true;
        // Also support prefix folder patterns like "node_modules/*" matching "sub/node_modules/pkg/index.js"
        if (pat.size() > 2 && pat.substr(pat.size() - 2) == L"/*") {
            std::wstring dirPrefix = pat.substr(0, pat.size() - 1); // "node_modules/"
            std::wstring relLower = PathUtils::ToLower(rel);
            std::wstring prefLower = PathUtils::ToLower(dirPrefix);
            if (relLower.rfind(prefLower, 0) == 0 || relLower.find(L"/" + prefLower) != std::wstring::npos) {
                return true;
            }
        }
        return false;
    };

    // 1. Exclude patterns take precedence
    auto excludes = SplitFilterPatterns(excludeFilter);
    for (const auto& ex : excludes) {
        if (matchesPattern(ex)) return false;
    }

    // 2. Include patterns (empty or "*" means include all non-excluded files)
    auto includes = SplitFilterPatterns(includeFilter);
    if (includes.empty()) return true;
    for (const auto& inc : includes) {
        if (matchesPattern(inc)) return true;
    }
    return false;
}

bool BackupManager::IsRcloneRemoteSyntax(const std::wstring& path) {
    // e.g. "gdrive:Backups" or "s3:bucket/folder" (colon at index >= 2, not a single drive letter "C:\")
    size_t colon = path.find(L':');
    if (colon == std::wstring::npos || colon <= 1) return false;
    size_t slash = path.find_first_of(L"\\/");
    return (slash == std::wstring::npos || colon < slash);
}

bool BackupManager::IsRcloneInstalled(std::wstring* outVersion) {
    static int s_cachedState = -1;
    static std::wstring s_cachedVer;
    if (s_cachedState >= 0) {
        if (outVersion) *outVersion = s_cachedVer;
        return s_cachedState == 1;
    }
    wchar_t foundPath[MAX_PATH] = {};
    DWORD n = SearchPathW(nullptr, L"rclone.exe", nullptr, MAX_PATH, foundPath, nullptr);
    if (n > 0 && n < MAX_PATH) {
        s_cachedState = 1;
        s_cachedVer = foundPath;
    } else {
        s_cachedState = 0;
    }
    if (outVersion) *outVersion = s_cachedVer;
    return s_cachedState == 1;
}

bool BackupManager::IsTargetAvailable(const std::wstring& targetPath) {
    std::wstring clean = TrimWs(targetPath);
    if (clean.empty()) return false;
    if (IsRcloneRemoteSyntax(clean)) {
        return IsRcloneInstalled();
    }
    DWORD attrs = GetFileAttributesW(clean.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        return true;
    }
    // If the target subfolder doesn't exist yet on an attached drive, check whether its root/parent drive is online!
    if (clean.size() >= 2 && clean[1] == L':') {
        std::wstring driveRoot = clean.substr(0, 2) + L"\\";
        DWORD rootAttrs = GetFileAttributesW(driveRoot.c_str());
        return (rootAttrs != INVALID_FILE_ATTRIBUTES && (rootAttrs & FILE_ATTRIBUTE_DIRECTORY));
    }
    return false;
}

std::wstring BackupManager::BuildRcloneCommandLine(const BackupJob& job, bool dryRun) {
    std::wstring cmd = L"rclone.exe copy \"" + job.sourcePath + L"\" \"" + job.targetPath + L"\"";
    if (dryRun) {
        cmd += L" --dry-run";
    }
    auto excludes = SplitFilterPatterns(job.excludeFilter);
    for (const auto& ex : excludes) {
        cmd += L" --exclude \"" + ex + L"\"";
    }
    auto includes = SplitFilterPatterns(job.includeFilter);
    for (const auto& inc : includes) {
        if (inc != L"*" && inc != L"*.*") {
            cmd += L" --include \"" + inc + L"\"";
        }
    }
    cmd += L" --stats-one-line";
    return cmd;
}

bool BackupManager::ShouldAutoRunJob(
    BackupJob& job,
    uint64_t nowEpochSec,
    bool targetNowOnline,
    std::wstring* outTriggerReason) {
    bool prevOnline = job.targetWasOnline;
    job.targetCurrentlyOnline = targetNowOnline;
    job.targetWasOnline = targetNowOnline;

    if (!job.enabled || !targetNowOnline) {
        return false;
    }

    // 1. Rising edge: target drive/folder just became available!
    if (job.whenTargetAvailable && !prevOnline && targetNowOnline) {
        if (outTriggerReason) *outTriggerReason = L"Cel stał się dostępny (Auto-Run)";
        return true;
    }

    // 2. Periodic schedule elapsed
    if (job.scheduleMinutes > 0) {
        uint64_t periodSec = static_cast<uint64_t>(job.scheduleMinutes) * 60ULL;
        if (job.lastRunEpochSec == 0 || (nowEpochSec >= job.lastRunEpochSec && (nowEpochSec - job.lastRunEpochSec) >= periodSec)) {
            if (outTriggerReason) *outTriggerReason = L"Harmonogram (" + FormatScheduleLabel(job.scheduleMinutes, true) + L")";
            return true;
        }
    }
    return false;
}

BackupSyncResult BackupManager::ExecuteJobSync(const BackupJob& job, bool dryRun) {
    BackupSyncResult res;
    res.dryRun = dryRun;

    std::wstring srcRoot = PathUtils::TrimTrailingSlashes(job.sourcePath);
    std::wstring dstRoot = PathUtils::TrimTrailingSlashes(job.targetPath);

    if (srcRoot.empty() || dstRoot.empty()) {
        res.ok = false;
        res.summaryText = L"Błąd: pusta ścieżka źródłowa lub docelowa";
        return res;
    }

    // Check if rclone CLI should and can be used
    if ((job.useRclone || IsRcloneRemoteSyntax(dstRoot)) && IsRcloneInstalled()) {
        res.usedRclone = true;
        std::wstring cmdLine = BuildRcloneCommandLine(job, dryRun);
        STARTUPINFOW si = { sizeof(si) };
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi = {};
        std::vector<wchar_t> mutableCmd(cmdLine.begin(), cmdLine.end());
        mutableCmd.push_back(L'\0');
        if (CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, 120000);
            DWORD exitCode = 1;
            GetExitCodeProcess(pi.hProcess, &exitCode);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            res.ok = (exitCode == 0);
            res.summaryText = res.ok
                ? (dryRun ? L"Rclone Dry-Run zakończony pomyślnie" : L"Synchronizacja rclone zakończona pomyślnie")
                : (L"Rclone zwrócił kod błędu " + std::to_wstring(exitCode));
            return res;
        }
    }

    // Native high-speed C++ incremental directory sync
    DWORD srcAttrs = GetFileAttributesW(srcRoot.c_str());
    if (srcAttrs == INVALID_FILE_ATTRIBUTES || !(srcAttrs & FILE_ATTRIBUTE_DIRECTORY)) {
        res.ok = false;
        res.summaryText = L"Błąd: folder źródłowy nie istnieje (" + srcRoot + L")";
        return res;
    }

    if (!dryRun && !EnsureDirectoryHierarchy(dstRoot)) {
        res.ok = false;
        res.summaryText = L"Błąd: nie można utworzyć folderu docelowego (" + dstRoot + L")";
        return res;
    }

    auto syncRecursive = [&](auto& self, const std::wstring& curSrc, const std::wstring& curDst, const std::wstring& relPrefix) -> void {
        std::wstring searchPattern = curSrc + L"\\*";
        WIN32_FIND_DATAW fd = {};
        HANDLE hFind = FindFirstFileExW(searchPattern.c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
        if (hFind == INVALID_HANDLE_VALUE) return;

        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            std::wstring relPath = relPrefix.empty() ? fd.cFileName : (relPrefix + L"/" + fd.cFileName);
            std::wstring fullSrc = curSrc + L"\\" + fd.cFileName;
            std::wstring fullDst = curDst + L"\\" + fd.cFileName;

            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                // Skip whole directory if excluded by pattern (e.g. ".git/*" or "node_modules/*")
                if (!MatchesFilterRules(relPath + L"/__probe__", L"*", job.excludeFilter)) {
                    ++res.filesFilteredOut;
                    continue;
                }
                self(self, fullSrc, fullDst, relPath);
            } else {
                if (!MatchesFilterRules(relPath, job.includeFilter, job.excludeFilter)) {
                    ++res.filesFilteredOut;
                    continue;
                }

                uint64_t srcSize = (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
                bool needsCopy = true;
                WIN32_FILE_ATTRIBUTE_DATA dstData = {};
                if (GetFileAttributesExW(fullDst.c_str(), GetFileExInfoStandard, &dstData) &&
                    !(dstData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    uint64_t dstSize = (static_cast<uint64_t>(dstData.nFileSizeHigh) << 32) | dstData.nFileSizeLow;
                    if (dstSize == srcSize && AreFileTimesClose(fd.ftLastWriteTime, dstData.ftLastWriteTime)) {
                        needsCopy = false;
                    }
                }

                if (!needsCopy) {
                    ++res.filesSkipped;
                } else if (dryRun) {
                    ++res.filesCopied;
                    res.bytesCopied += srcSize;
                    if (res.transferredFiles.size() < 64) res.transferredFiles.push_back(relPath);
                } else {
                    EnsureDirectoryHierarchy(curDst);
                    if (CopyFileExW(fullSrc.c_str(), fullDst.c_str(), nullptr, nullptr, nullptr, 0)) {
                        ++res.filesCopied;
                        res.bytesCopied += srcSize;
                        if (res.transferredFiles.size() < 64) res.transferredFiles.push_back(relPath);
                    }
                }
            }
        } while (FindNextFileW(hFind, &fd));

        FindClose(hFind);
    };

    syncRecursive(syncRecursive, srcRoot, dstRoot, L"");
    res.ok = true;

    wchar_t summary[256] = {};
    if (dryRun) {
        swprintf_s(summary, L"Dry-Run: %u plików do skopiowania (%s), %u bez zmian, %u pominięto filtrem",
            res.filesCopied, FileEngine::FormatSizeDetailed(res.bytesCopied).c_str(),
            res.filesSkipped, res.filesFilteredOut);
    } else {
        swprintf_s(summary, L"OK: skopiowano %u plików (%s), %u aktualnych, %u odfiltrowanych",
            res.filesCopied, FileEngine::FormatSizeDetailed(res.bytesCopied).c_str(),
            res.filesSkipped, res.filesFilteredOut);
    }
    res.summaryText = summary;
    return res;
}

void BackupManager::StartJobAsync(int jobId, bool dryRun) {
    if (m_busy.exchange(true)) return;
    BackupJob jobCopy;
    if (!GetJobById(jobId, jobCopy)) {
        m_busy = false;
        return;
    }
    m_runningJobId = jobId;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_progressStatus = (dryRun ? L"Dry-Run: " : L"Synchronizacja: ") + jobCopy.name + L"...";
    }

    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
    m_workerThread = std::thread([this, jobCopy, dryRun]() {
        BackupSyncResult res = ExecuteJobSync(jobCopy, dryRun);
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            for (auto& j : m_jobs) {
                if (j.id == jobCopy.id) {
                    if (!dryRun) {
                        j.lastRunEpochSec = CurrentEpochSeconds();
                        j.lastRunTimeStr = FormatNowTimestamp();
                    }
                    j.lastStatus = res.summaryText;
                    j.lastRunOk = res.ok;
                    j.lastFilesCopied = res.filesCopied;
                    j.lastFilesSkipped = res.filesSkipped;
                    j.lastBytesCopied = res.bytesCopied;
                    break;
                }
            }
            m_progressStatus = res.summaryText;
        }
        SaveJobs();
        if (res.ok) AppLog::Ok(L"Backup", jobCopy.name + L": " + res.summaryText);
        else AppLog::Error(L"Backup", jobCopy.name + L": " + res.summaryText);
        m_runningJobId = 0;
        m_busy = false;
    });
}

void BackupManager::TickScheduler(uint64_t nowEpochSec) {
    if (m_busy.load()) return;
    if (nowEpochSec == 0) nowEpochSec = CurrentEpochSeconds();

    int triggerJobId = 0;
    std::wstring triggerReason;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        for (auto& j : m_jobs) {
            bool online = IsTargetAvailable(j.targetPath);
            std::wstring reason;
            if (ShouldAutoRunJob(j, nowEpochSec, online, &reason) && triggerJobId == 0) {
                triggerJobId = j.id;
                triggerReason = reason;
                j.lastRunEpochSec = nowEpochSec;
            }
        }
    }
    if (triggerJobId != 0) {
        AppLog::Info(L"Backup", L"Auto-starting backup job #" + std::to_wstring(triggerJobId) + L" (" + triggerReason + L")");
        StartJobAsync(triggerJobId, false);
    }
}

std::wstring BackupManager::GetProgressStatus() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_progressStatus;
}

} // namespace Explorer2
