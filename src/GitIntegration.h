#pragma once
#include "Types.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Explorer2 {

// One line of `git status --porcelain=v1`.
struct GitFileChange {
    std::wstring path;              // relative to the repo root, '/' separators (as printed by git)
    std::wstring origPath;          // source path for renames / copies, empty otherwise
    wchar_t indexStatus = L' ';     // porcelain "X" column (staged side)
    wchar_t workStatus  = L' ';     // porcelain "Y" column (working tree side); '?' for untracked (both columns)
};

struct GitCommit {
    std::wstring hash, shortHash, author, relDate, subject;
    std::wstring refs;              // decoration list, e.g. "HEAD -> main, origin/main, tag: v1.0"
    int64_t unixTime = 0;           // committer time
};

struct GitBranch {
    std::wstring name;              // "main" or "origin/main"
    bool isCurrent = false;
    bool isRemote = false;
    std::wstring upstream;          // "origin/main" (local branches only, may be empty)
    std::wstring track;             // raw git text such as "[ahead 1, behind 2]" or "[gone]" (may be empty)
    std::wstring lastSubject;       // subject of the tip commit
};

struct GitRepoState {
    bool valid = false;             // a repository was read successfully
    bool gitMissing = false;        // git.exe could not be found
    std::wstring root, branch, upstream, remoteUrl, headShort, lastError;
    bool detachedHead = false;      // when true `branch` is empty and `headShort` holds the commit
    int ahead = 0, behind = 0, stashCount = 0;
    int stagedCount = 0, modifiedCount = 0, untrackedCount = 0, conflictCount = 0;
    std::vector<GitFileChange> changes;     // capped at 500 (counters above always cover ALL files)
    std::vector<GitCommit> commits;         // newest first, capped at 60 (empty for a repo with no commits)
    std::vector<GitBranch> branches;        // local first, then remotes (HEAD pointers skipped)
    uint64_t refreshedTick = 0;             // GetTickCount64() of the last successful refresh
};

enum class GitAction {
    Fetch,              // git fetch --all --prune
    Pull,               // git pull --ff-only
    Push,               // git push
    Checkout,           // arg = branch: local name, or remote branch such as "origin/x" (creates a tracking branch)
    ShowCommit,         // arg = hash; output = `git show --stat --format=fuller`
    ShowDiffStat,       // working tree `git diff --stat`
    CopyHashOnlyNoop    // does nothing (the UI copies the hash itself)
};

// UI-agnostic git front end. Shells out to git.exe on one background worker thread; the UI thread never blocks.
// Every public method is thread-safe and returns immediately.
class GitIntegration {
public:
    GitIntegration();
    ~GitIntegration();      // kills a running git process and joins the worker (bounded, never hangs)
    GitIntegration(const GitIntegration&) = delete;
    GitIntegration& operator=(const GitIntegration&) = delete;

    // Cached lookup of git.exe (PATH, then the usual Program Files locations). A negative result is re-tried
    // at most every few seconds so installing git while the app runs is picked up.
    static bool IsGitInstalled();

    // Walks up from `folder` looking for ".git" (directory OR file: worktrees / submodules).
    // Returns the directory containing it, or empty. Synchronous, GetFileAttributes calls only.
    static std::wstring FindRepoRoot(const std::wstring& folder);

    // Asynchronous, coalesced (any number of pending requests collapse into one). The previous snapshot stays
    // visible while it runs, unless it belongs to a different repo root (then it is cleared immediately).
    void RequestRefresh(const std::wstring& repoRoot);

    // Queues an action. Mutating actions (Fetch/Pull/Push/Checkout) auto-refresh the repo afterwards.
    // Queued ShowCommit/ShowDiffStat requests are superseded by a newer one.
    void RunAction(const std::wstring& repoRoot, GitAction action, const std::wstring& arg = L"");

    bool IsBusy() const;                        // refresh or action running or queued
    std::wstring BusyDescription() const;       // e.g. L"git fetch --all --prune" (empty when idle)
    GitRepoState SnapshotState() const;         // thread-safe copy
    std::wstring SnapshotOutput() const;        // result text of the last action (stdout+stderr, trimmed, max 64 KB)
    bool LastActionSucceeded() const;           // exit code of the last action was 0 (false before any action)
    uint64_t StateVersion() const;              // bumps on every state / output / busy change

private:
    struct Request {
        enum class Kind { Refresh, Action } kind = Kind::Refresh;
        std::wstring root;
        GitAction action = GitAction::CopyHashOnlyNoop;
        std::wstring arg;
    };

    void WorkerMain();
    void DoRefresh(const std::wstring& root);
    void DoAction(const Request& req);
    void SetBusy(const std::wstring& description);
    void SetOutput(std::wstring text, bool ok);

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<Request> m_actions;              // guarded by m_mutex
    std::wstring m_pendingRefreshRoot;          // guarded by m_mutex
    bool m_hasPendingRefresh = false;           // guarded by m_mutex
    bool m_running = false;                     // worker is processing a request
    std::wstring m_busyDescription;
    GitRepoState m_state;
    std::wstring m_output;
    bool m_lastOk = false;
    uint64_t m_version = 0;
    std::atomic<bool> m_stop{ false };
    std::thread m_worker;                       // declared last: started after everything else is constructed
};

} // namespace Explorer2
