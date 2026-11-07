#pragma once
#include "Types.h"
#include "Localization.h"
#include <string>
#include <vector>
#include <deque>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <atomic>

namespace Explorer2 {

struct DeleteTask {
    std::vector<std::wstring> paths;
    bool permanent = false;
};

struct FolderSizeTask {
    std::wstring path;
    std::wstring normKey;
    uint64_t generation = 0;
};

class FileEngine {
public:
    FileEngine();
    ~FileEngine();

    void Initialize(HWND notifyHwnd);
    void Shutdown();

    // Ultra-fast directory enumeration (< 5ms for 10,000 files)
    std::vector<FileEntry> EnumerateDirectory(
        const std::wstring& dirPath,
        bool showHidden,
        bool showExtensions,
        Lang lang);

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

    // Async non-blocking delete (Recycle Bin or Permanent POSIX unlink)
    void EnqueueDelete(const std::vector<std::wstring>& paths, bool permanent);

    // Undo last Recycle Bin delete (Ctrl+Z)
    bool UndoLastDelete(std::wstring& outRestoredDir);

    // On-demand Recursive Folder Size Calculator + Hash Table Cache
    bool IsAnyFolderSizeCalculating() const;
    bool IsFolderInFlight(const std::wstring& normKey) const;
    std::wstring GetActiveScanningSamplePath() const;
    bool HasAnyCachedFolderSizes(const std::vector<FileEntry>& entries) const;
    void RequestFolderSizes(const std::vector<FileEntry>& entries, bool forceRefresh);

    // Single-lock batch sync of folder sizes from hash table into FileEntry vector
    void SyncDirectorySizes(std::vector<FileEntry>& entries);

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

    // Clipboard operations (compatible with Windows Explorer CF_HDROP)
    static bool CopyFilesToClipboard(HWND hwnd, const std::vector<std::wstring>& paths, bool isCut);
    static bool PasteFilesFromClipboard(HWND hwnd, const std::wstring& targetDir, bool& outWasCut);
    static bool CopyTextToClipboard(HWND hwnd, const std::wstring& text);

    // File management operations
    static bool CreateNewFolder(const std::wstring& parentDir, const std::wstring& baseName, std::wstring& outCreatedPath);
    static bool RenameFileOrFolder(const std::wstring& oldPath, const std::wstring& newName, std::wstring& outNewPath);
    static void OpenFileOrProperties(HWND hwnd, const std::wstring& path, bool propertiesOnly = false);
    static void OpenInClassicExplorer(const std::wstring& path, bool selectItem);
    static void OpenTerminalInFolder(const std::wstring& dirPath);

    // Format helpers
    static std::wstring FormatSizeKB(uint64_t bytes);
    static std::wstring FormatSizeDetailed(uint64_t bytes);
    static std::wstring FormatFileTime(const FILETIME& ft);

private:
    void DeleteWorkerLoop();
    static bool DeletePathPermanently(const std::wstring& path);
    static std::wstring GetUndoStagingDirForPath(const std::wstring& path);
    void CleanupAllUndoStaging();

    struct ExtTypeInfo {
        std::wstring typeName;
        int sysIconIndex = -1;
    };

    const ExtTypeInfo& GetExtensionInfo(const std::wstring& extLower, bool isDir, Lang lang);

    HWND m_notifyHwnd = nullptr;
    std::mutex m_extMutex;
    std::unordered_map<std::wstring, ExtTypeInfo> m_extCache;
    int m_folderIconIndex = -1;

    // Background delete thread
    std::thread m_deleteThread;
    std::mutex m_deleteMutex;
    std::condition_variable m_deleteCv;
    std::deque<DeleteTask> m_deleteQueue;
    std::atomic<bool> m_running{ false };

    // Undo stack for Ctrl+Z
    std::mutex m_undoMutex;
    std::vector<UndoDeleteBatch> m_undoStack;
    std::vector<std::wstring> m_stagingDirsCreated;

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

    // Non-blocking Network Drive online/offline state cache
    struct NetDriveInfo {
        bool probed = false;
        bool online = false;
        uint64_t totalBytes = 0;
    };
    void ProbeNetworkDriveAsync(wchar_t driveLetter);
    mutable std::mutex m_netDriveMutex;
    std::unordered_map<wchar_t, NetDriveInfo> m_netDrives;

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
};

} // namespace Explorer2
