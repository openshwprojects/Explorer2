#pragma once
#include "Types.h"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Explorer2 {

struct BackupJob {
    int id = 1;
    std::wstring name;
    std::wstring sourcePath;
    std::wstring targetPath;
    int scheduleMinutes = 60;          // 0 = Manual, 5, 15, 60, 360, 1440
    bool whenTargetAvailable = true;   // Auto-run when target drive/folder transitions offline -> online
    bool useRclone = false;            // Use rclone CLI when available (or remote:path syntax)
    std::wstring includeFilter = L"*"; // Semicolon-separated wildcards (e.g. "*.cpp;*.h;*.docx" or "*")
    std::wstring excludeFilter = L"*.tmp;*.bak;.git/*;node_modules/*"; // Semicolon-separated wildcards
    bool enabled = true;

    // Runtime & telemetry state
    bool targetCurrentlyOnline = false;
    bool targetWasOnline = false;
    uint64_t lastRunEpochSec = 0;
    std::wstring lastRunTimeStr;
    std::wstring lastStatus;
    bool lastRunOk = true;
    uint32_t lastFilesCopied = 0;
    uint32_t lastFilesSkipped = 0;
    uint64_t lastBytesCopied = 0;
};

struct BackupSyncResult {
    bool ok = false;
    bool dryRun = false;
    bool usedRclone = false;
    uint32_t filesCopied = 0;
    uint32_t filesSkipped = 0;
    uint32_t filesFilteredOut = 0;
    uint64_t bytesCopied = 0;
    std::wstring summaryText;
    std::vector<std::wstring> transferredFiles;
};

class BackupManager {
public:
    BackupManager();
    ~BackupManager();
    BackupManager(const BackupManager&) = delete;
    BackupManager& operator=(const BackupManager&) = delete;

    // Persistence (defaults to %LOCALAPPDATA%\Explorer2\backups.ini)
    static std::wstring DefaultConfigPath();
    bool LoadJobs(const std::wstring& iniPath = L"");
    bool SaveJobs(const std::wstring& iniPath = L"") const;

    // Thread-safe job list accessors & mutators
    std::vector<BackupJob> SnapshotJobs() const;
    int AddJob(const BackupJob& job);
    bool UpdateJob(const BackupJob& job);
    bool DeleteJob(int jobId);
    bool GetJobById(int jobId, BackupJob& outJob) const;
    static int CycleScheduleMinutes(int currentMinutes);
    static std::wstring FormatScheduleLabel(int scheduleMinutes, bool pl);

    // Filter matching (supports *, ?, semicolon/comma lists, filename or relative path matching)
    static bool WildcardMatch(const std::wstring& text, const std::wstring& pattern);
    static bool MatchesFilterRules(
        const std::wstring& relativePath,
        const std::wstring& includeFilter,
        const std::wstring& excludeFilter);

    // Target availability & auto-run evaluation
    static bool IsRcloneRemoteSyntax(const std::wstring& path);
    static bool IsRcloneInstalled(std::wstring* outVersion = nullptr);
    static bool IsTargetAvailable(const std::wstring& targetPath);
    static std::wstring BuildRcloneCommandLine(const BackupJob& job, bool dryRun);

    // Evaluates whether `job` should auto-run right now (either because target just became available
    // or because its periodic schedule elapsed while target is online). Updates `job.targetWasOnline`
    // and `job.targetCurrentlyOnline`.
    static bool ShouldAutoRunJob(
        BackupJob& job,
        uint64_t nowEpochSec,
        bool targetNowOnline,
        std::wstring* outTriggerReason = nullptr);

    // Synchronous execution (used by background worker and unit tests)
    static BackupSyncResult ExecuteJobSync(const BackupJob& job, bool dryRun = false);

    // Asynchronous execution & background scheduler tick
    void StartJobAsync(int jobId, bool dryRun = false);
    void TickScheduler(uint64_t nowEpochSec = 0);
    bool IsBusy() const { return m_busy.load(); }
    int GetRunningJobId() const { return m_runningJobId.load(); }
    std::wstring GetProgressStatus() const;

    static uint64_t CurrentEpochSeconds();

private:
    mutable std::mutex m_mutex;
    std::vector<BackupJob> m_jobs;
    std::wstring m_configPath;
    std::wstring m_progressStatus;

    std::atomic<bool> m_busy{false};
    std::atomic<int> m_runningJobId{0};
    std::thread m_workerThread;
};

} // namespace Explorer2
