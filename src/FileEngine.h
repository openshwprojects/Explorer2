#pragma once
#include "Types.h"
#include "Localization.h"
#include <string>
#include <vector>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <memory>

struct IDataObject;

namespace Explorer2 {

// One item a delete could not remove (error = Win32 code, 0 = unknown)
struct DeleteFailure {
    std::wstring path;
    unsigned long error = 0;
};

struct MergeFolderResult {
    std::wstring srcDir;
    std::wstring dstDir;
    std::wstring srcName;
    std::wstring dstName;
    int movedItems = 0;
    int renamedCollisions = 0;
    bool srcRemoved = false;
    std::vector<DeleteFailure> failures;
    std::vector<std::wstring> collisionNotes;
};

struct FolderSizeTask {
    std::wstring path;
    std::wstring normKey;
    uint64_t generation = 0;
};

struct RecycleBinRecord {
    uint64_t version = 0;       // 1 = Vista / Win7 SP1, 2 = Win10 / Win11
    uint64_t fileSize = 0;
    FILETIME deletedTime = {};
    std::wstring originalPath;
};

class FileEngine {
public:
    FileEngine();
    ~FileEngine();

    void Initialize(HWND notifyHwnd);
    void Shutdown();

    // Live Directory Change Watcher (ReadDirectoryChangesW -> WM_APP_DIR_CHANGED)
    void WatchDirectory(const std::wstring& dirPath);

    // Ultra-fast directory enumeration (< 5ms for 10,000 files)
    std::vector<FileEntry> EnumerateDirectory(
        const std::wstring& dirPath,
        bool showHidden,
        bool showExtensions,
        Lang lang);
    // Why the last EnumerateDirectory call could not list its folder (a Win32 code such as
    // ERROR_ACCESS_DENIED), or 0 when it could. A folder that cannot be read is returned as an empty list,
    // and this tells it apart from a folder that is really empty.
    DWORD LastEnumerateError() const { return m_lastEnumerateError.load(); }

    // Fast in-memory sorting (folders first, natural sort for names)
    static void SortEntries(
        std::vector<FileEntry>& entries,
        SortColumn column,
        bool ascending);

    // Navigation tree builder (Quick Access + Drives) & dynamic subfolder expansion
    static std::vector<std::pair<std::wstring, std::wstring>> GetDefaultQuickAccessFolders(Lang lang);
    std::vector<TreeNode> BuildNavigationTree(
        Lang lang,
        const std::vector<std::pair<std::wstring, std::wstring>>& pinnedQuickAccess);
    static std::vector<TreeNode> EnumerateSubfoldersForTree(
        const std::wstring& parentPath,
        int childIndent,
        bool isQuickAccessBranch);

    // Non-blocking check if a drive root ("X:\", "Z:\", "F:\") is currently offline/empty
    bool IsDriveOffline(const std::wstring& path, Lang lang, std::wstring* outDisplayName = nullptr);
    // "Main (\\NAS191725) (Z:)" for a drive mapped to a share, read locally (no network)
    static std::wstring MappedNetworkDriveLabel(wchar_t driveLetter, Lang lang);

    // Bookkeeping around a delete job (the deleting itself is done by TransferManager::EnqueueDelete).
    // BeginDelete hides the items from listings and, for a Recycle Bin delete, keeps what Ctrl+Z needs;
    // EndDelete shows again whatever the job could not remove.
    void BeginDelete(const std::vector<std::wstring>& paths, bool permanent);
    void EndDelete(const std::vector<std::wstring>& paths);
    void AddPendingHiddenPath(const std::wstring& path);
    void RemovePendingHiddenPath(const std::wstring& path);

    // Undo last Recycle Bin delete (Ctrl+Z)
    bool UndoLastDelete(std::wstring& outRestoredDir);

    // Ctrl+Z works from hidden copies (hard links) of the deleted files, kept in "X:\.explorer2_undo" in a
    // folder of their own for each running Explorer2. A session that is killed or crashes cannot remove its
    // copies, so the next start does it: StartUndoStagingSweep() queues the fixed drives for a background
    // sweep, and any other drive is swept the first time it is used.
    void StartUndoStagingSweep();
    // Removes what earlier sessions left in one staging folder and returns how many items went. A folder
    // whose lock file is held belongs to a running Explorer2 and is left alone, as is `ownSessionDir`, any
    // session folder younger than `minAgeSeconds` (it may be just starting) and anything Explorer2 did not
    // put there.
    static size_t SweepStaleUndoStaging(
        const std::wstring& stagingRoot,
        const std::wstring& ownSessionDir = L"",
        unsigned minAgeSeconds = 15,
        const std::atomic<bool>* keepRunning = nullptr);

    // On-demand Recursive Folder Size Calculator + Hash Table Cache
    bool IsAnyFolderSizeCalculating() const;
    bool IsFolderInFlight(const std::wstring& normKey) const;
    std::wstring GetActiveScanningSamplePath() const;
    bool HasAnyCachedFolderSizes(const std::vector<FileEntry>& entries) const;
    void RequestFolderSizes(const std::vector<FileEntry>& entries, bool forceRefresh);

    // Single-lock batch sync of folder sizes from hash table into FileEntry / TreeNode vectors
    void SyncDirectorySizes(std::vector<FileEntry>& entries);
    void SyncTreeNodeSizes(std::vector<TreeNode>& treeNodes);
#ifndef _WIN32
    // Linux: only the sizes and the offline state of network / FUSE mounts the probe threads have answered for
    // (the Ten komputer list, whose drives get no folder sizes)
    void SyncProbedMountSizes(std::vector<FileEntry>& entries);
#endif
    void RequestTreeFolderSizes(const std::vector<TreeNode>& treeNodes, bool forceRefresh = false);

    // Retrieve cached nested child blocks for WinDirStat/WizTree-style Proporcje treemap
    bool GetFolderTreemapChildren(const std::wstring& normKey, std::vector<TreemapChildNode>& outChildren) const;

    // Ultra-fast Asynchronous Recursive Search Engine
    void StartAsyncSearch(
        const std::wstring& rootDir,
        const SearchOptions& opts,
        bool showHidden,
        bool showExtensions,
        Lang lang);
    void CancelAsyncSearch();
    bool IsSearchRunning() const;
    void SyncSearchResults(std::vector<FileEntry>& outResults, SearchProgressInfo& outProgress);

    // File-type icons. Asking Windows for a type's icon reads the registry and can take several milliseconds,
    // so a type seen for the first time is looked up on a worker thread: its files are listed with a generic
    // icon at first, and WM_APP_EXT_ICONS_READY tells the window when the right icons are known. The window
    // then calls RefreshIconIndices on the listings it holds (true if an entry changed).
    bool RefreshIconIndices(std::vector<FileEntry>& entries) const;
    void AcknowledgeIconNotification() { m_extIconNotifyPending.store(false); }
    // For the self-tests: waits until every type asked for so far has its icon (false on timeout), and
    // whether a type's icon has been looked up in this process
    bool WaitUntilIconsResolved(unsigned int timeoutMs);
    static bool IsTypeIconKnown(const std::wstring& extLower);

    // Clipboard operations (compatible with Windows Explorer CF_HDROP)
    static bool CopyFilesToClipboard(HWND hwnd, const std::vector<std::wstring>& paths, bool isCut);
    static bool ReadFilesFromClipboard(HWND hwnd, std::vector<std::wstring>& outPaths, bool& outIsMove);
    static bool CopyTextToClipboard(HWND hwnd, const std::wstring& text);
    // The data object Windows Explorer puts on a file drag (CF_HDROP, shell ID list, file names...), for a drag out
    // of the window. Paths may come from different folders. Caller releases it; nullptr when no path parses.
    static IDataObject* CreateShellDataObject(const std::vector<std::wstring>& paths);

    // File management operations
    static std::wstring ResolveAddressInput(std::wstring_view rawInput, const std::wstring& currentDir = L"");
    static bool ParseRecycleBinInfoBytes(const std::vector<uint8_t>& raw, RecycleBinRecord& outRecord);
    static bool GetRecycleBinCompanionInfoPath(const std::wstring& dataPath, std::wstring& outInfoPath);
    static bool RestoreRecycleBinItem(const std::wstring& dataPath, Lang lang, std::wstring& outRestoredPath, DWORD* outError = nullptr);
    static bool DeleteRecycleBinItemPermanently(const std::wstring& dataPath, DWORD* outError = nullptr);
    static bool CreateNewFolder(const std::wstring& parentDir, const std::wstring& baseName, std::wstring& outCreatedPath);
    static bool RenameFileOrFolder(const std::wstring& oldPath, const std::wstring& newName, std::wstring& outNewPath, DWORD* outError = nullptr);
    static void MergeDirectoryContents(const std::wstring& srcDir, const std::wstring& dstDir, std::atomic<int>* progressCounter, MergeFolderResult& outResult);
    static void OpenFileOrProperties(HWND hwnd, const std::wstring& path, bool propertiesOnly = false);
    static void OpenInClassicExplorer(const std::wstring& path, bool selectItem);
    static void OpenTerminalInFolder(const std::wstring& dirPath);

    // Format helpers
    static std::wstring FormatSizeKB(uint64_t bytes);
    static std::wstring FormatSizeDetailed(uint64_t bytes);
    static std::wstring FormatFileTime(const FILETIME& ft);

private:
    static bool DeletePathPermanently(const std::wstring& path, std::vector<DeleteFailure>* failures = nullptr);
    static std::wstring GetUndoStagingDirForPath(const std::wstring& path);
    void CleanupAllUndoStaging();

    struct ExtTypeInfo {
        std::wstring typeName;
        int sysIconIndex = -1;
        bool iconPending = false;       // generic icon for now; the worker is looking the real one up
    };

    // A copy, taken under the lock: the icon worker updates the cached entries while listings are made
    ExtTypeInfo GetExtensionInfo(const std::wstring& extLower, bool isDir, Lang lang);

    HWND m_notifyHwnd = nullptr;
    std::mutex m_extMutex;
    std::unordered_map<std::wstring, ExtTypeInfo> m_extCache;
    int m_folderIconIndex = -1;

    std::atomic<bool> m_running{ false };
    std::atomic<DWORD> m_lastEnumerateError{ 0 };   // see LastEnumerateError

    // Items a delete job is working on (hidden from listings until the job reports back)
    mutable std::mutex m_deleteStateMutex;
    std::unordered_set<std::wstring> m_pendingDeleteKeys;

    // Undo stack for Ctrl+Z, and this session's own folder (with its lock file) in each staging folder it uses
    struct UndoSession {
        std::wstring dir;
        HANDLE lock = INVALID_HANDLE_VALUE;
    };
    std::wstring EnsureUndoSessionDir(const std::wstring& stagingRoot);
    void QueueUndoSweep(const std::wstring& stagingRoot);
    void UndoSweepWorkerLoop();

    std::mutex m_undoMutex;
    std::vector<UndoDeleteBatch> m_undoStack;
    std::unordered_map<std::wstring, UndoSession> m_undoSessions;      // by staging folder
    std::unordered_set<std::wstring> m_undoSweepRequested;             // staging folders already queued in this run
    std::deque<std::wstring> m_undoSweepQueue;
    std::condition_variable m_undoSweepCv;
    std::thread m_undoSweepThread;

    // Folder Size Hash Table + Background Worker Pool
    void FolderSizeWorkerLoop();
    uint64_t ComputeFolderSizeRecursive(const std::wstring& dirPath, int depth, uint64_t generation, bool& outAborted);

    mutable std::mutex m_sizeMutex;
    std::condition_variable m_sizeCv;
    std::vector<std::thread> m_sizeThreads;
    std::deque<FolderSizeTask> m_sizeQueue;
    std::unordered_map<std::wstring, uint64_t> m_folderSizeCache;   // Hash table: normKey -> recursive bytes
    std::unordered_map<std::wstring, std::vector<TreemapChildNode>> m_folderChildrenCache; // normKey -> top child blocks
    std::unordered_map<std::wstring, bool> m_folderSizeInFlight;    // Currently calculating
    std::wstring m_currentScanSample;                               // Live subfolder path currently being scanned
    std::atomic<uint64_t> m_lastPartialNotifyTick{ 0 };
    std::atomic<uint64_t> m_sizeGeneration{ 1 };
    std::atomic<int> m_activeSizeWorkers{ 0 };
    std::atomic<bool> m_sizeNotifyPending{ false };

    // File-type icons looked up on a worker thread (see RefreshIconIndices)
    void QueueExtIconLookup(const std::wstring& extLower);
    void ExtIconWorkerLoop();
    std::mutex m_extIconMutex;                          // guards the four members below
    std::condition_variable m_extIconCv;
    std::deque<std::wstring> m_extIconQueue;
    std::unordered_set<std::wstring> m_extIconQueued;
    bool m_extIconBusy = false;
    std::thread m_extIconThread;
    std::atomic<bool> m_extIconNotifyPending{ false };
    int m_genericIconIndex = 0;                         // the icon of a file whose type has none of its own

    // Non-blocking Network Drive online/offline state cache.
    // Kept in a block that the probe threads share with the engine. Asking a drive that is offline can take
    // half a minute, and the engine may be gone by the time the answer arrives; a thread that only knew the
    // engine's address would then write into freed memory. This way the thread keeps the block alive.
    struct NetDriveInfo {
        bool probed = false;
        bool online = false;
        uint64_t totalBytes = 0;
        uint64_t freeBytes = 0;
        bool running = false;           // a probe thread is asking the drive now
        ULONGLONG tick = 0;             // when the last answer came (asked again after a while)
    };
    struct NetDriveState {
        std::mutex mutex;
        std::unordered_map<wchar_t, NetDriveInfo> drives;
#ifndef _WIN32
        std::unordered_map<std::wstring, NetDriveInfo> mounts;   // Linux network and FUSE mounts, by mount point
#endif
        std::atomic<bool> wanted{ true };       // false once the engine has no use for the answers any more
    };
    void ProbeNetworkDriveAsync(wchar_t driveLetter);
#ifndef _WIN32
    // Linux: the size of a network or FUSE mount is asked on a probe thread of its own (statvfs waits as long
    // as the server does not answer) and kept in m_net->mounts; the view is told with
    // WM_APP_FOLDER_SIZE_UPDATED. GetProbedMountSize gives the last answer, false while there is none yet.
    void ProbeLinuxMountAsync(const std::wstring& mountPoint);
    bool GetProbedMountSize(const std::wstring& mountPoint, uint64_t& outTotal, uint64_t& outFree, bool& outOnline) const;
#endif
    std::shared_ptr<NetDriveState> m_net = std::make_shared<NetDriveState>();

    // Async Recursive Search Worker
    void SearchWorkerLoop();
    std::thread m_searchThread;
    mutable std::mutex m_searchMutex;
    std::condition_variable m_searchCv;
    std::wstring m_searchRootDir;
    SearchOptions m_searchOpts;
    bool m_searchShowHidden = true;
    bool m_searchShowExtensions = true;
    Lang m_searchLang = Lang::PL;
    bool m_hasPendingSearchJob = false;
    std::atomic<uint64_t> m_searchGeneration{ 1 };
    std::atomic<bool> m_searchRunning{ false };
    std::atomic<bool> m_searchNotifyPending{ false };
    std::vector<FileEntry> m_searchAccumResults;
    SearchProgressInfo m_searchProgress;

    // Live Directory Watcher Worker
    void DirWatcherWorkerLoop();
    std::thread m_watcherThread;
    mutable std::mutex m_watcherMutex;
    std::condition_variable m_watcherCv;
    std::wstring m_watchedDir;
    std::atomic<uint64_t> m_watcherGeneration{ 0 };
    HANDLE m_watcherWakeEvent = nullptr;
};

#ifndef _WIN32
// Linux: the mount table, for the drive list and for the folder walkers below (FileEngine.cpp)
namespace LinuxMounts {

struct Mount {
    std::string mountPoint;     // as /proc/self/mountinfo spells it, its "\040"-style escapes decoded
    std::string fsType;
    std::string source;         // the device or share, such as "/dev/sda1" or "server:/export"
    uint64_t device = 0;        // the st_dev of what is mounted there
};

// The current mount table, read again when the copy at hand is more than a second old. Reading it never
// waits for a mounted file system.
std::shared_ptr<const std::vector<Mount>> Current();

// Network file systems, and FUSE file systems ("fuse.*"), whose server may take long to answer or never do
bool IsNetworkFsType(const std::string& fsType);

// The mount whose device is `device` (the one mounted last when there are several), or nullptr
const Mount* FindByDevice(const std::vector<Mount>& mounts, uint64_t device);

} // namespace LinuxMounts

// Linux: tells, while a folder tree is walked without following links, which of its folders is the root of
// another mount: another file system (its device differs from the tree's) or a bind mount of a folder of the
// same one (listed in the mount table). The walkers that delete, move, copy, measure or back up a tree treat
// such a folder as a leaf and never enter it, so nothing on a USB disk or a network share mounted inside the
// tree is touched.
class MountFence {
public:
    explicit MountFence(const std::wstring& rootDir);

    // Whether the folder `childPath`, found in the tree, is the root of another mount. A folder that cannot
    // be checked counts as one, so that it is not entered; outCannotTell says when that was the reason.
    bool IsMountRoot(const std::wstring& childPath, bool* outCannotTell = nullptr) const;
    // Whether the tree's root itself is a folder and not a link to one
    bool RootIsRealFolder() const { return m_rootIsFolder; }

    // Whether `path` itself is a mount point (a link never is: removing it removes only the link)
    static bool IsMountPoint(const std::wstring& path);

private:
    std::string m_rootNative;   // the root as the walk spells it
    std::string m_rootReal;     // the same with every link resolved, as the mount table spells paths
    uint64_t m_rootDevice = 0;
    bool m_rootKnown = false;
    bool m_rootIsFolder = false;
    std::unordered_set<std::string> m_mountsBelow;   // mount points inside the tree, by their real path
};
#endif

} // namespace Explorer2
