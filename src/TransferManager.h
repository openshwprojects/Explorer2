#pragma once
#include "Types.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace Explorer2 {

enum class TransferOp : int {
    Copy = 0,
    Move = 1,
    Delete = 2 // Permanent delete with the engine's own deleter, or a checked hand-off to the Recycle Bin
};

enum class TransferQueueMode : int {
    SmartPerDrive = 0, // Sequential on overlapping volumes, parallel on independent drives (CopyHandler default)
    Sequential    = 1, // Strictly 1 active job at a time
    ParallelAll   = 2  // Run up to 4 jobs simultaneously
};

enum class TransferConflictPolicy : int {
    RenameCollision  = 0, // Keep both as "name (2).ext" (zero data loss default)
    OverwriteNewer   = 1, // Incremental sync: skip identical size+mtime, overwrite if different/newer
    OverwriteAll     = 2, // Always replace existing destination file
    SkipExisting     = 3, // Leave existing destination files untouched
    ResumePartial    = 4  // Append remaining bytes if destination / .e2part is smaller than source
};

enum class TransferVerifyMode : int {
    None            = 0, // Maximum raw I/O speed
    SizeAndTime     = 1, // Verify destination size & timestamps on disk
    XxHash64        = 2  // Streamed 64-bit XXH64 hash during read + post-write disk re-read verification
};

enum class TransferJobState : int {
    Queued              = 0,
    Scanning            = 1,
    Running             = 2,
    Paused              = 3,
    Verifying           = 4,
    Completed           = 5,
    CompletedWithErrors = 6,
    Cancelled           = 7
};

struct TransferConfig {
    TransferQueueMode      queueMode            = TransferQueueMode::SmartPerDrive;
    TransferConflictPolicy conflictPolicy       = TransferConflictPolicy::RenameCollision;
    TransferVerifyMode     verifyMode           = TransferVerifyMode::None;
    uint32_t               bufferSizeKB         = 0;    // 0 = Auto (4x4 MB deep async ring), or 1024..32768
    uint32_t               workerThreads        = 0;    // 0 = Auto (NVMe/SSD: up to 8, HDD/USB/UNC: 1), or 1..16
    bool                   usePartFiles         = true; // Write to .e2part and atomically rename on completion (>= 1 MB)
    bool                   unbufferedLargeFiles = false;// Bypass OS RAM standby cache (FILE_FLAG_NO_BUFFERING) for >= 64 MB
    uint32_t               speedLimitMBps       = 0;    // 0 = Unlimited, or 10, 50, 100, 500 MB/s
    uint32_t               maxAutoRetries       = 4;    // Automatic exponential-backoff retries on transient lock/network drop
};

struct TransferFileError {
    std::wstring srcPath;
    std::wstring dstPath;
    DWORD        win32Error = 0;
    std::wstring message;
    uint64_t     resumedOffset = 0;
};

struct TransferJobSnapshot {
    int                    id = 0;
    TransferOp             op = TransferOp::Copy;
    TransferJobState       state = TransferJobState::Queued;
    TransferConflictPolicy conflictPolicy = TransferConflictPolicy::RenameCollision;
    TransferVerifyMode     verifyMode = TransferVerifyMode::None;
    bool                   forceStarted = false;

    std::vector<std::wstring> sources;
    std::wstring           targetDir;
    std::wstring           srcVolumeLabel;
    std::wstring           dstVolumeLabel;
    std::wstring           currentFile;

    uint64_t               totalFiles = 0;
    uint64_t               doneFiles = 0;
    uint64_t               skippedFiles = 0;
    uint64_t               renamedCollisions = 0;
    uint64_t               verifiedFiles = 0;
    uint64_t               resumedFiles = 0;

    uint64_t               totalBytes = 0;
    uint64_t               doneBytes = 0;
    double                 speedBytesPerSec = 0.0;
    double                 peakBytesPerSec = 0.0;
    uint64_t               elapsedActiveMs = 0;
    int64_t                etaSeconds = -1; // -1 = calculating / unknown

    std::wstring           summaryText;
    std::vector<TransferFileError> errors;

    // Delete jobs only
    bool                   recycle = false;     // true = to the Recycle Bin, false = permanent
    double                 itemsPerSec = 0.0;

    // Progress 0..1. A delete is measured in items (files + folders), because the size of a file says
    // nothing about how long it takes to remove; copies and moves are measured in bytes.
    double Fraction() const {
        double f = 0.0;
        if (op == TransferOp::Delete || totalBytes == 0) {
            f = (totalFiles > 0)
                ? static_cast<double>(doneFiles) / static_cast<double>(totalFiles)
                : (state == TransferJobState::Completed ? 1.0 : 0.0);
        } else {
            f = static_cast<double>(doneBytes) / static_cast<double>(totalBytes);
        }
        return f < 0.0 ? 0.0 : (f > 1.0 ? 1.0 : f);
    }
};

struct TransferCompletionEvent {
    int              jobId = 0;
    TransferOp       op = TransferOp::Copy;
    TransferJobState finalState = TransferJobState::Completed;
    std::wstring     targetDir;
    std::vector<std::wstring> sourceDirs;
    uint64_t         doneFiles = 0;
    uint64_t         skippedFiles = 0;
    uint64_t         doneBytes = 0;
    uint64_t         elapsedMs = 0;
    double           avgBytesPerSec = 0.0;
    size_t           errorCount = 0;

    // Delete jobs only
    bool             recycle = false;
    std::vector<std::wstring> sources;           // the items that were asked to be deleted
    std::vector<TransferFileError> firstErrors;  // up to 8 failures, for the summary shown to the user
};

struct TransferAggregateTelemetry {
    size_t             activeJobs = 0;
    size_t             pausedJobs = 0;
    size_t             queuedJobs = 0;
    size_t             finishedJobs = 0;
    size_t             totalFailedFiles = 0;
    double             liveBytesPerSec = 0.0;
    double             peakBytesPerSec = 0.0;
    uint64_t           sessionBytesTransferred = 0;
    uint64_t           sessionFilesTransferred = 0;
    std::vector<float> speedHistoryMBps; // 60-sample rolling history for oscilloscope
};

class TransferManager {
public:
    TransferManager();
    ~TransferManager();
    TransferManager(const TransferManager&) = delete;
    TransferManager& operator=(const TransferManager&) = delete;

    void Initialize(HWND notifyHwnd);
    void Shutdown();

    // Global engine configuration (thread-safe, takes effect live!)
    TransferConfig GetConfig() const;
    void SetConfig(const TransferConfig& cfg);

    // Formatting helpers for UI labels
    static std::wstring FormatQueueMode(TransferQueueMode m, bool pl);
    static std::wstring FormatConflictPolicy(TransferConflictPolicy p, bool pl);
    static std::wstring FormatVerifyMode(TransferVerifyMode v, bool pl);
    static std::wstring FormatBufferSize(uint32_t kb, bool pl);
    static std::wstring FormatWorkers(uint32_t w, bool pl);
    static std::wstring FormatSpeedLimit(uint32_t mbps, bool pl);
    static std::wstring FormatSpeed(double bytesPerSec);
    static std::wstring FormatEta(int64_t etaSec, bool pl);

    // Enqueue a Copy or Move batch asynchronously (returns jobId immediately in < 0.01ms)
    int EnqueueTransfer(
        const std::vector<std::wstring>& sources,
        const std::wstring& targetDir,
        TransferOp op,
        const TransferConfig* overrideCfg = nullptr);

    // Enqueue a delete batch; returns the jobId, or 0 when no usable path was given.
    //   permanent = true : the engine's own deleter (no shell). It lists the tree without recursion, never
    //                      enters junctions or folder symlinks (the link goes, its target stays), works with
    //                      long paths, read-only items and names Explorer cannot handle, waits out short locks
    //                      and reports every item it could not remove.
    //   permanent = false: moves the items to the Recycle Bin. An item the bin cannot take is reported as an
    //                      error and left where it is; it is never deleted for good behind the user's back.
    // A delete job starts at once (it does not wait behind copies), can be paused, cancelled and retried like
    // any other job, and is never written to the persistent queue, so it cannot resume after a restart.
    int EnqueueDelete(
        const std::vector<std::wstring>& paths,
        bool permanent,
        const TransferConfig* overrideCfg = nullptr,
        bool startPaused = false);

    // The delete engine without a job: removes one file, link or whole folder tree before returning.
    // Same rules as a permanent delete job. Returns true when nothing is left of the path; every item
    // that could not be removed is appended to outErrors when it is given.
    static bool DeleteTreeNow(const std::wstring& path, std::vector<TransferFileError>* outErrors = nullptr);

    // Paths the delete engine refuses outright: empty or relative paths, anything with "." or ".." in it,
    // whole drives ("C:\") and network share roots ("\\server\share").
    static bool IsDeleteRefused(const std::wstring& path);

    // Self-test hook: wait this long before each deleted item, so pause and cancel can be tested mid-run
    void SetDeleteItemDelayForTesting(uint32_t ms);

    // Job & Queue controls (CopyHandler-grade flexibility)
    bool PauseJob(int jobId);
    bool ResumeJob(int jobId);
    bool TogglePauseJob(int jobId);
    bool CancelOrRemoveJob(int jobId);
    bool ForceStartJob(int jobId);
    bool BumpJobToTop(int jobId);
    bool SetJobConflictPolicy(int jobId, TransferConflictPolicy policy);
    bool RetryFailedInJob(int jobId);

    void PauseAll();
    void ResumeAll();
    bool TogglePauseActive();
    void CancelActive();
    int  RetryAllFailed();
    void ClearFinishedJobs();

    // Crash-safe persistent queue (.e2part + transfer_queue.dat) — returns number of unfinished jobs recovered as Paused
    size_t EnableQueuePersistence(const std::wstring& queueFilePath);

    // Watcher storm guard & Windows 11 Taskbar progress state (0=None, 1=Indeterminate, 2=Normal, 3=Paused, 4=Error)
    bool IsTouchingDirectory(const std::wstring& normDirKey) const;
    int  GetTaskbarProgressInfo(uint64_t& outCompleted, uint64_t& outTotal) const;

    // Deterministic low-disk simulation hook for SelfTestSuite
    void SetSimulatedFreeSpaceForTesting(uint64_t freeBytes, bool enabled);

    // Fast polling / UI state snapshots
    bool HasActiveOrPausedJobs() const;
    bool HasRunningJobs() const;
    bool GetPrimaryHudJob(TransferJobSnapshot& outSnap, size_t* outTotalActiveAndQueued = nullptr) const;
    std::vector<TransferJobSnapshot> SnapshotJobs() const;
    // The jobs still to finish, for the floating cards: running ones first, then paused ones, then queued
    // ones, at most maxJobs of them; outTotal gets how many there are in all. Cheap enough for every paint
    // (finished jobs in the history are not copied).
    std::vector<TransferJobSnapshot> SnapshotPendingJobs(size_t maxJobs, size_t* outTotal = nullptr) const;
    TransferAggregateTelemetry GetTelemetry();
    std::vector<TransferCompletionEvent> TakeCompletionEvents();

    // Synchronous wait helper (used by automated SelfTestSuite)
    bool WaitForJob(int jobId, DWORD timeoutMs = 30000);

    // Fast public XXH64 helper (also exposed for verification & self-tests)
    static uint64_t ComputeXxHash64Memory(const void* data, size_t len, uint64_t seed = 0);
    static bool ComputeXxHash64File(const std::wstring& path, uint64_t& outHash, std::atomic<bool>* cancelFlag = nullptr);

private:
    struct InternalFileItem {
        std::wstring srcPath;
        std::wstring dstPath;
        uint64_t     size = 0;
        FILETIME     creationTime = {};
        FILETIME     lastAccessTime = {};
        FILETIME     lastWriteTime = {};
        DWORD        attributes = 0;
        bool         isReparse = false;
        bool         parentEnsured = false;
        bool         dstKnownFresh = false;
        bool         dstReserved = false;
        bool         done = false;
        bool         failed = false;
    };

    struct InternalDirItem {
        std::wstring srcPath;
        std::wstring dstPath;
        FILETIME     creationTime = {};
        FILETIME     lastAccessTime = {};
        FILETIME     lastWriteTime = {};
        DWORD        attributes = 0;
        bool         done = false;   // Delete jobs: the folder has been removed
    };

    struct InternalJob;
    struct PoolBatch;
    class WorkerAsyncRing;
    static TransferJobSnapshot BuildSnapshot(const InternalJob& j);

    void SchedulerLoop();
    void PoolWorkerLoop();
    void RunParallelWorkers(uint32_t numWorkers, const std::function<void(WorkerAsyncRing&)>& fn);
    void ExecuteJobWorker(std::shared_ptr<InternalJob> job);
    void ExecuteFtpJobWorker(std::shared_ptr<InternalJob> job);
    void ExecuteDeleteJobWorker(std::shared_ptr<InternalJob> job);
    void RunRecycleBinPhase(InternalJob& job);
    bool CheckPreFlightDiskSpaceOrAutoPause(InternalJob& job);
    void SaveQueueState();
    bool CopyOrMoveSingleItem(
        InternalJob& job,
        InternalFileItem& item,
        bool sameVolume,
        bool allowUnbuffered,
        std::atomic<bool>& volumeSupportsBlockClone,
        uint8_t* workerBuf,
        size_t workerBufCap,
        WorkerAsyncRing* asyncRing,
        TransferFileError& outErr);
    void RecordProgressBytes(InternalJob& job, uint64_t chunkWritten);
    bool WaitIfPausedOrCancelled(InternalJob& job);
    void ThrottleBytes(uint64_t bytesJustTransferred, InternalJob& job);
    void NotifyUi(bool isCompletion);

    HWND m_notifyHwnd = nullptr;
    std::atomic<bool> m_running{ false };
    std::atomic<int>  m_nextJobId{ 1 };
    std::atomic<uint32_t> m_speedLimitMBpsAtomic{ 0 };
    std::atomic<bool>     m_simFreeSpaceEnabled{ false };
    std::atomic<uint64_t> m_simFreeSpaceBytes{ UINT64_MAX };
    std::atomic<uint32_t> m_testDeleteDelayMs{ 0 };

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    TransferConfig m_config;
    std::vector<std::shared_ptr<InternalJob>> m_jobs;
    std::vector<TransferCompletionEvent> m_completions;
    std::wstring m_queuePersistPath;
    std::mutex   m_persistWriteMutex;

    // Persistent worker pool & warm WorkerAsyncRings (eliminates per-job thread & reader-thread churn)
    std::mutex m_poolMutex;
    std::condition_variable m_poolCv;
    bool m_poolShutdown = false;
    std::deque<std::shared_ptr<PoolBatch>> m_poolBatches;
    std::vector<std::thread> m_poolThreads;
    std::vector<std::unique_ptr<WorkerAsyncRing>> m_callerRings;

    // Speed telemetry & token-bucket rate limiter
    std::atomic<uint64_t> m_sessionBytes{ 0 };
    std::atomic<uint64_t> m_sessionFiles{ 0 };
    double m_peakBytesPerSec = 0.0;
    uint64_t m_lastTelemetrySampleTick = 0;
    uint64_t m_lastSampleSessionBytes = 0;
    double m_smoothedSpeedBps = 0.0;
    std::deque<float> m_speedHistoryMBps;

    std::mutex m_throttleMutex;
    uint64_t m_throttleWindowTick = 0;
    uint64_t m_throttleBytesInWindow = 0;

    std::atomic<bool> m_uiNotifyPending{ false };
    std::thread m_schedulerThread;
    std::vector<std::pair<std::shared_ptr<std::atomic<bool>>, std::thread>> m_jobThreads;
};

} // namespace Explorer2
