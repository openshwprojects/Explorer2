#pragma once
#include "Types.h"
#include "Localization.h"
#include "FileEngine.h"
#include "ImageCache.h"
#include "ShellMenuEmulator.h"
#include "MediaPlayer.h"
#include "HardwareInfo.h"
#include "ProgramsInfo.h"
#include "Monitoring.h"
#include "LanScanner.h"
#include "GitIntegration.h"
#include "AppLog.h"
#include "TextLayoutCache.h"
#include "FileContentPane.h"
#include "BackupManager.h"
#include "TransferManager.h"
#include <atomic>
#include <memory>
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <vector>
#include <map>
#include <unordered_set>

struct ITaskbarList3;

namespace Explorer2 {

struct RibbonButtonHit {
    int id = 0;
    D2D1_RECT_F rect = {};
    int jobId = 0;          // a transfer card's button: the job the command is for (0: none)
};

struct TabBarHit {
    int tabIndex = -1;
    D2D1_RECT_F tabRect = {};
    D2D1_RECT_F closeRect = {};
    bool isNewTabBtn = false;
};

struct FolderViewState {
    float scrollY = 0.0f;
    float filmstripScrollX = 0.0f;
    std::wstring focusedPath;
};

struct ExplorerPaneState {
    std::wstring currentPath;
    std::wstring currentNormKey;
    std::vector<std::wstring> history;
    int historyIndex = -1;
    std::wstring filterText;
    bool isSearchActive = false;
    std::vector<FileEntry> allItems;
    std::vector<FileEntry> items;
    std::vector<BreadcrumbSegment> breadcrumbs;
    SortColumn sortCol = SortColumn::Name;
    bool sortAsc = true;
    GroupByMode groupBy = GroupByMode::None;
    float scrollY = 0.0f;
    float filmstripScrollX = 0.0f;
    int focusedIndex = -1;
    int anchorIndex = -1;
    int hoverIndex = -1;
    int hoverRowActionKind = 0;
    D2D1_RECT_F scrollbarThumbRect = {};
    std::map<std::wstring, FolderViewState> folderViewStates;
    std::vector<TreemapHitNode> treemapHits;
    int hoverTreemapHitIndex = -1;
    std::vector<PieSectorHitNode> pieHits;
    int hoverPieHitIndex = -1;
    D2D1_POINT_2F pieCenterPt = {};
    float pieCenterRadius = 0.0f;
};

class ExplorerApp {
public:
    ExplorerApp();
    ~ExplorerApp();

    bool Initialize(HINSTANCE hInstance, int nCmdShow, const std::wstring& initialPath);
    int RunMessageLoop();

private:
    static LRESULT CALLBACK WndProcStatic(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    // Direct2D / DirectWrite lifecycle
    bool CreateDeviceIndependentResources();
    bool CreateDeviceResources();
    void DiscardDeviceResources();
    void OnResize(UINT width, UINT height);
    void OnPaint();

    // Rendering sections
    void DrawRibbon(const D2D1_RECT_F& rect);
    void DrawNavBar(const D2D1_RECT_F& rect);
    void DrawTreePane(const D2D1_RECT_F& rect);
    void DrawMainContent(const D2D1_RECT_F& rect);
    void DrawDetailsView(const D2D1_RECT_F& rect);
    void DrawGridOrListView(const D2D1_RECT_F& rect);
    void DrawProportionsView(const D2D1_RECT_F& rect);
    void DrawPieChartView(const D2D1_RECT_F& rect);
    void DrawHardwareProView(const D2D1_RECT_F& rect);
    void DrawProgramsView(const D2D1_RECT_F& rect);
    void DrawMonitoringView(const D2D1_RECT_F& rect);
    void DrawLanView(const D2D1_RECT_F& rect);
    void DrawLogView(const D2D1_RECT_F& rect);
    void DrawBackupsView(const D2D1_RECT_F& rect);
    void DrawTransfersView(const D2D1_RECT_F& rect);
    void MergeFolderInto(const std::wstring& srcDir, const std::wstring& dstDir, const std::wstring& srcName, const std::wstring& dstName);
    void StartRenameJob(const std::wstring& oldPath, const std::wstring& oldName, const std::wstring& newName);
    // Unpacks a whole archive on a worker thread; the outcome arrives as WM_APP_EXTRACT_DONE
    void StartExtractJob(const std::wstring& archivePath, const std::wstring& targetFolder, bool toSubfolder);
    void ShowRenameFailure(const std::wstring& oldName, const std::wstring& newName, const std::wstring& reason, DWORD err);

    // Git dock (right side) for folders inside a git repository
    void DrawGitDock(const D2D1_RECT_F& dock);
    D2D1_RECT_F GetGitDockRect() const;
    // 0 = none, 1 = navigation tree splitter, 2 = git dock splitter, 3 = details column edge
    int HitResizeHandle(float x, float y) const;
    bool GitDockVisible() const {
        return m_gitEnabled && !m_gitRoot.empty() && !m_isFullScreen &&
               m_viewMode != ViewMode::FullViewer && m_viewMode != ViewMode::Hardware;
    }
    bool PreviewPaneVisible() const {
        return m_showPreviewPane && !m_isFullScreen &&
               m_viewMode != ViewMode::FullViewer && m_viewMode != ViewMode::Hardware;
    }
    D2D1_RECT_F GetPreviewPaneRect() const;
    void SyncGitRepo(bool force = false);
    bool HandleGitDockClick(float x, float y, bool rightButton);
    // Saves the settings file. The write normally happens in the background; with waitUntilWritten the
    // call returns only when the file is on disk, which is what the program does on its way out.
    void SaveUiSettings(bool waitUntilWritten = false) const;

    // Draws text in the main window like m_rt->DrawText, with the layout reused from earlier paints
    // (see TextLayoutCache.h)
    void DrawTextCached(const wchar_t* text, UINT32 length, IDWriteTextFormat* format, const D2D1_RECT_F& rect, ID2D1Brush* brush) {
        m_textLayouts.Draw(m_dwriteFactory, m_rt, text, length, format, rect, brush);
    }
    TextLayoutCache m_textLayouts;
    void LoadUiSettings();
    bool IsLanView() const { return m_viewMode == ViewMode::Hardware && m_activeRibbonTab == RibbonTab::Lan; }
    bool IsLogView() const { return m_viewMode == ViewMode::Hardware && m_activeRibbonTab == RibbonTab::Log; }
    bool IsBackupsView() const { return m_viewMode == ViewMode::Hardware && m_activeRibbonTab == RibbonTab::Backups; }
    bool IsTransfersView() const { return m_viewMode == ViewMode::Hardware && m_activeRibbonTab == RibbonTab::Transfers; }
    bool IsMonitoringView() const { return m_viewMode == ViewMode::Hardware && m_activeRibbonTab == RibbonTab::Monitoring; }
    void DrawCardShell(float x, float y, float w, float h, const std::wstring& title, uint32_t badgeHex, bool showCopyHint, float extraRightPad = 0.0f);
    // Shared building blocks of the card-based views (Sprzęt / Programy)
    void DrawCardTinyButton(const D2D1_RECT_F& r, const std::wstring& label, int cmdId, const std::wstring& arg, int actionIndex = 0,
                            const D2D1_RECT_F* hitClip = nullptr);
    void DrawClippedText(const std::wstring& text, const D2D1_RECT_F& r, IDWriteTextFormat* fmt, uint32_t colorHex);
    // The "Programy" tab is hosted by the same scrollable card-hit view as "Sprzęt".
    bool IsProgramsView() const { return m_viewMode == ViewMode::Hardware && m_activeRibbonTab == RibbonTab::Programs; }
    void ScheduleProgramsRefresh(int attempts);
    void DrawMarqueeOverlay();
    void DrawFullViewerCanvas(const D2D1_RECT_F& rect, bool includeFilmstrip);
    void DrawPreviewPane(const D2D1_RECT_F& rect);
    void DrawStatusBar(const D2D1_RECT_F& rect);
    void DrawOperationToastBanner(const D2D1_RECT_F& mainRect);

    // Custom vector icons for Ribbon & Chrome
    void DrawFolderVectorIcon(const D2D1_RECT_F& r, bool smallIcon = false);
    void DrawFolderWithContentPreview(const D2D1_RECT_F& r, const FileEntry& folderItem);
    void DrawDriveVectorIcon(const D2D1_RECT_F& r, bool isNetwork, bool isOptical, bool isOffline);
    void DrawRibbonIcon(int cmdId, const D2D1_RECT_F& r);

    // Navigation & Directory loading
    void NavigateTo(const std::wstring& path, bool addToHistory = true, const std::wstring& selectFileName = L"");
    void NavigateBack();
    void NavigateForward();
    void NavigateUp();
    void RefreshCurrentFolder(const std::wstring& preserveSelectedPath = L"");
    void RebuildBreadcrumbs();
    void ApplyFilterAndSort();

    // Expandable / Collapsible Left Navigation Tree ("zwiń / rozwiń") & Quick Access Pins
    void RebuildTreeNodes();
    void ToggleTreeNodeExpand(int nodeIndex);
    void PinFolderToQuickAccess(const std::wstring& folderPath);
    void UnpinFolderFromQuickAccess(const std::wstring& folderPath);
    bool IsFolderPinnedToQuickAccess(const std::wstring& folderPath) const;

    // Folder Size Hash Table integration
    void TriggerFolderSizeCalculation();
    void SyncFolderSizesFromCache();

    // Advanced Search ("Wyszukiwanie") integration
    void TriggerAsyncSearch();
    void ClearSearchAndRestoreFolder();
    void RebuildParsedSearchOptions();
    void ShowSearchTypeListMenu(int screenX, int screenY);
    void BeginSearchFieldEdit(int fieldCmdId, const D2D1_RECT_F& fieldRect);
    void EnsureWorkAnimTimerState();

    // Media Player integration
    void SyncMediaPlayerToSelection();

    // Layout & Hit-testing
    bool IsSplitViewActive() const {
        return m_splitView && !m_isFullScreen &&
               m_viewMode != ViewMode::Hardware && m_viewMode != ViewMode::FullViewer;
    }
    bool IsTabBarVisible() const {
        return m_tabs.size() > 1 && !IsSplitViewActive() && !m_isFullScreen &&
               m_viewMode != ViewMode::Hardware && m_viewMode != ViewMode::FullViewer;
    }
    void SwapStateWith(ExplorerPaneState& state);
    void CreateNewTab(const std::wstring& initialPath = L"", bool switchToNew = true);
    void CloseTab(int tabIndex);
    void SwitchToTab(int tabIndex);
    void DrawTabBar(const D2D1_RECT_F& barRect);
    std::wstring ResolveDropTargetFolder(float x, float y, std::wstring* outTargetLabel = nullptr) const;
    void ExecuteDropFiles(const std::vector<std::wstring>& srcPaths, const std::wstring& targetDir, bool forceCopy, bool forceMove);
    void DrawDragOverlay();
    D2D1_RECT_F GetFullWorkspaceRect() const;
    D2D1_RECT_F GetSplitPaneOuterRect(int paneIdx) const;
    D2D1_RECT_F GetSplitPaneContentMainRect(int paneIdx) const;
    void DrawSplitPaneHeader(int paneIdx, const D2D1_RECT_F& headerRect, bool isActivePane);
    void SwapWithOtherPane();
    void SetActivePane(int paneIdx);
    void ToggleSplitView();
    void CopyOrMoveToOtherPane(bool move);
    void BeginPaneAddressEdit(int paneIdx);

    D2D1_RECT_F GetMainViewRect() const;
    D2D1_RECT_F GetContentItemsRect() const;
    D2D1_RECT_F GetItemRect(int index, const D2D1_RECT_F& contentRect) const;
    int HitTestItem(float x, float y, const D2D1_RECT_F& contentRect, bool* outOnLabelOrIcon = nullptr) const;
    void GetGridMetrics(const D2D1_RECT_F& contentRect, float& outCellW, float& outCellH, float& outIconSize, int& outCols) const;
    float GetTotalContentHeight(const D2D1_RECT_F& contentRect) const;
    void ClampScrollOffset();
    void EnsureItemVisible(int index);

    // Selection & Input handlers
    void OnMouseDown(float x, float y, bool rightButton, WPARAM keyFlags);
    void OnMouseMove(float x, float y, WPARAM keyFlags);
    void OnMouseUp(float x, float y, bool rightButton, WPARAM keyFlags);
    void OnMouseDoubleClick(float x, float y, WPARAM keyFlags);
    void OnMouseWheel(short delta, float x, float y, WPARAM keyFlags);
    void OnKeyDown(WPARAM vk);
    void OnChar(wchar_t ch);
    void UpdateMarqueeSelection(bool ctrlHeld, bool shiftHeld);

    // Selection helpers
    void SelectSingle(int index, bool makeAnchor = true);
    void SelectRange(int fromIndex, int toIndex, bool keepExisting);
    void SelectAll();
    void SelectNone();
    void InvertSelection();
    std::vector<std::wstring> GetSelectedPaths() const;
    int GetSelectedCount(uint64_t* outTotalBytes = nullptr) const;

    // Actions & Commands
    void ExecuteRibbonCommand(int cmdId);
    void ActivateFocusedOrSelectedItem();
    void DeleteSelectedItems(bool permanent);
    void StartDeleteJob(const std::vector<std::wstring>& paths, bool permanent);
    bool ReportDeleteJobResult(const TransferCompletionEvent& ev);
    void ShowDeleteFailures(const TransferCompletionEvent& ev);
    void PerformUndoDelete();
    void CopySelected(bool isCut);
    void PasteIntoCurrent(bool forceVerifyXxHash = false);
    // Pastes the files on the clipboard into targetDir (copy, or move after a cut)
    void PasteInto(const std::wstring& targetDir, bool forceVerifyXxHash = false);
    void StartManagedTransfer(const std::vector<std::wstring>& srcPaths, const std::wstring& targetDir, bool isMove, bool forceVerifyXxHash = false);
    void UpdateTaskbarProgress();
    void CopySelectedPaths();
    void CreateNewFolderAction();
    void StartInlineRename();
    void CommitAddressOrRenameEdit(HWND editHwnd);
    void CancelInlineEdits(bool byEscape = false);
    void ShowContextMenu(int screenX, int screenY, int hitIndex);
    void ShowTreeContextMenu(int screenX, int screenY, int treeNodeIndex);
    void ShowBreadcrumbSubfolderMenu(int breadcrumbIndex);
    void RecordVisitedDirectory(const std::wstring& path);
    void ShowRecentDirsDropdown(int triggerCmdId = 827);
    void ConfirmAndDeleteRowItem(int itemIndex, bool permanent);
    void ShowDeleteDropdownMenu(int screenX, int screenY);
    std::vector<std::wstring> GetSelectedImagePaths(int fallbackHitIndex = -1) const;
    HMENU BuildImageToolsSubmenu(const std::vector<std::wstring>& imgPaths) const;
    void ShowImageToolsMenu(int screenX, int screenY, int fallbackHitIndex = -1);
    bool HandleImageToolCommand(int cmdId, int fallbackHitIndex = -1);

    // Full Viewer & Fullscreen controls
    void ToggleFullViewerMode();
    void ToggleFullScreen();
    void StepViewerImage(int delta);
    void ResetViewerZoom();
    void SetViewerZoom100();

    // Window & D2D handles
    HINSTANCE m_hInstance = nullptr;
    HWND m_hwnd = nullptr;
    HWND m_addressEdit = nullptr;
    HWND m_searchEdit = nullptr;
    HWND m_renameEdit = nullptr;
    HFONT m_editFont = nullptr;

    ID2D1Factory* m_d2dFactory = nullptr;
    IDWriteFactory* m_dwriteFactory = nullptr;
    ID2D1HwndRenderTarget* m_rt = nullptr;

    IDWriteTextFormat* m_fmtRegular = nullptr;
    IDWriteTextFormat* m_fmtSmall = nullptr;
    IDWriteTextFormat* m_fmtBold = nullptr;
    IDWriteTextFormat* m_fmtCenterSmall = nullptr;
    IDWriteTextFormat* m_fmtViewerHud = nullptr;

    ID2D1SolidColorBrush* m_solidBrush = nullptr;

    // Engines
    FileEngine m_fileEngine;
    ImageCache m_imageCache;
    ShellMenuEmulator m_shellEmulator;
    MediaPlayer m_mediaPlayer;
    std::vector<std::pair<std::wstring, std::wstring>> m_pinnedFolders; // <label, path>

    // UI State
    Lang m_lang = Lang::PL;
    RibbonTab m_activeRibbonTab = RibbonTab::Home;
    bool m_ribbonCollapsed = false;
    ViewMode m_viewMode = ViewMode::Details;
    ViewMode m_lastNonViewerMode = ViewMode::Details;
    bool m_showPreviewPane = false;
    bool m_showNavPane = true;
    bool m_showHidden = true;
    bool m_showExtensions = true;
    bool m_confirmDelete = false;
    bool m_isFullScreen = false;
    WINDOWPLACEMENT m_prevWndPlacement = { sizeof(WINDOWPLACEMENT) };

    // Navigation state
    std::wstring m_currentPath;
    std::wstring m_currentNormKey;
    std::vector<std::wstring> m_history;
    int m_historyIndex = -1;
    std::vector<std::wstring> m_recentDirs; // Last 30 unique visited folders (MRU order, persisted in settings.ini)
    std::wstring m_filterText;
    SearchOptions m_searchOpts;
    SearchProgressInfo m_searchProgress;
    bool m_isSearchActive = false;
    int m_editingSearchFieldCmd = 0; // CMD_SEARCH_EDIT_DATE_FROM / TO / SIZE_MIN / SIZE_MAX / CUSTOM_EXT
    ULONGLONG m_folderScanFinishedTick = 0;
    bool m_wasFolderScanning = false;

    // Data model
    std::vector<FileEntry> m_allItems;
    std::vector<FileEntry> m_items;
    std::vector<TreeNode> m_treeNodes;
    std::vector<BreadcrumbSegment> m_breadcrumbs;

    // Sorting & Column widths (Nazwa, Data modyfikacji, Typ, Rozmiar, Operacje)
    SortColumn m_sortCol = SortColumn::Name;
    bool m_sortAsc = true;
    GroupByMode m_groupBy = GroupByMode::None;
    float m_colWidths[5] = { 320.0f, 145.0f, 155.0f, 115.0f, 128.0f };
    int m_resizingCol = -1;
    float m_resizeStartX = 0.0f;
    float m_resizeStartWidth = 0.0f;

    // Splitter state
    float m_treePaneWidth = 265.0f;
    float m_previewPaneWidth = 450.0f;
    bool m_draggingTreeSplitter = false;
    bool m_draggingPreviewSplitter = false;

    // Scrolling & Selection state
    float m_scrollY = 0.0f;
    float m_treeScrollY = 0.0f;
    bool m_draggingScrollbar = false;
    float m_scrollbarDragOffsetY = 0.0f;

    D2D1_POINT_2F m_mousePt = {};
    int m_focusedIndex = -1;
    int m_anchorIndex = -1;
    int m_hoverIndex = -1;
    int m_hoverRowActionKind = 0; // 0=none, 1=Kosz button, 2=Usuń button
    int m_hoverTreeIndex = -1;
    int m_hoverBreadcrumbPane = -1;
    int m_hoverBreadcrumbIndex = -1;
    int m_hoverBreadcrumbArrowIndex = -1;
    int m_hoverRibbonBtn = 0;
    int m_renamingIndex = -1;
    std::wstring m_renamingPath;
    std::wstring m_renamingOldName;

    // Rename & merge job tracking (both run on worker threads; the status bar and floating banner show their state)
    std::atomic<int> m_renamePending{ 0 };
    std::wstring m_renamePendingText;     // "old -> new" shown while the job runs (UI thread only)
    std::atomic<int> m_mergePending{ 0 };
    // Shared with the merge thread, which counts in it and may still be running when the window is gone
    std::shared_ptr<std::atomic<int>> m_mergeMovedCount = std::make_shared<std::atomic<int>>(0);
    std::wstring m_mergePendingText;      // "src -> dst" shown while the merge runs (UI thread only)
    std::atomic<int> m_extractPending{ 0 };
    std::wstring m_extractPendingText;    // archive name shown while it is being unpacked (UI thread only)
    // Extractions under way ("archive|target"), and the one that finished last, so that a second click on
    // the same button does not unpack the same archive to the same place twice (UI thread only)
    std::unordered_set<std::wstring> m_extractActive;
    std::wstring m_extractLastDoneKey;
    ULONGLONG m_extractLastDoneTick = 0;
    int m_logFilter = 0;                  // Log tab: 0 = everything, 1 = warnings and errors, 2 = errors only
    bool m_gitWasBusy = false;            // for logging the end of a git action
    bool m_gitActionLogPending = false;   // a fetch / pull / push / switch was started and its result is not logged yet
    bool m_lanWasScanning = false;        // for logging the end of a LAN scan

    // Rubber-band / Marquee Box Selection & Drag-and-Drop state
    bool m_isBoxSelecting = false;
    D2D1_POINT_2F m_boxStartPt = {};
    D2D1_POINT_2F m_boxCurrentPt = {};
    std::vector<bool> m_preBoxSelection;

    bool m_dragCandidate = false;
    bool m_dragDeferSingleSelect = false;
    int m_dragSourceItemIdx = -1;
    D2D1_POINT_2F m_dragStartPt = {};
    bool m_isDraggingItems = false;
    std::vector<std::wstring> m_dragPaths;
    std::wstring m_dragTargetDir;
    std::wstring m_dragTargetLabel;

    // Multi-Tab state
    std::vector<ExplorerPaneState> m_tabs;
    int m_activeTab = 0;
    std::vector<TabBarHit> m_tabHits;
    int m_hoverTabIndex = -1;
    int m_hoverTabCloseIndex = -1;
    bool m_hoverNewTabBtn = false;
    bool m_darkMode = false;

    // Full Viewer Zoom / Pan state
    float m_viewerZoom = 1.0f;
    bool m_viewerActualPixels = false;
    float m_viewerPanX = 0.0f;
    float m_viewerPanY = 0.0f;
    bool m_viewerDraggingPan = false;
    D2D1_POINT_2F m_viewerDragStart = {};
    float m_filmstripScrollX = 0.0f;
    bool m_viewerCompareMode = false;
    bool m_viewerShowHistogram = false;
    bool m_viewerSlideshow = false;
    std::wstring m_viewerHistPath;
    uint32_t m_viewerHistR[64] = {};
    uint32_t m_viewerHistG[64] = {};
    uint32_t m_viewerHistB[64] = {};
    uint32_t m_viewerHistMax = 1;

    // Type-ahead buffer
    std::wstring m_typeAheadBuf;
    ULONGLONG m_lastTypeAheadTick = 0;

    // Status message toast
    std::wstring m_statusToast;
    ULONGLONG m_statusToastExpireTick = 0;
    bool m_imgToolsOverwrite = false; // false = save as copy (safe default), true = overwrite original in-place

    // Interactive hit regions rebuilt each frame
    std::vector<RibbonButtonHit> m_ribbonHits;
    std::vector<TreemapHitNode> m_treemapHits;
    int m_hoverTreemapHitIndex = -1;
    std::vector<PieSectorHitNode> m_pieHits;
    int m_hoverPieHitIndex = -1;
    D2D1_POINT_2F m_pieCenterPt = {};
    float m_pieCenterRadius = 0.0f;

    // Hardware Pro Mode state & hit regions
    HardwareInspector m_hwInspector;
    HardwareSubTab m_hwSubTab = HardwareSubTab::All;
    bool m_hwLiveRefresh = true;
    ULONGLONG m_lastHwLiveUpdateTick = 0;
    UINT m_workAnimIntervalMs = 0;      // the work timer's interval; 0 = not running (EnsureWorkAnimTimerState)
    float m_hwScrollY = 0.0f;
    float m_hwMaxScrollY = 0.0f;
    std::vector<HardwareCardHit> m_hwCardHits;
    int m_hoverHwCardIndex = -1;
    int m_hwSelectedDisk = -1;     // physical disk index shown in the SMART panel
    bool m_hwSmartInit = false;    // SMART for all disks read once on first visit
    std::wstring m_hwSelectedUsbId;      // selected USB node deviceInstanceId
    std::wstring m_hwSelectedComPort;    // selected COMx port name
    bool m_hwInspectComMode = false;     // true = inspect selected COM port, false = inspect selected USB node
    bool m_hwShowUsbInterfaces = true;   // true = show child MI_xx/HID interfaces in USB TreeView
    std::unordered_set<std::wstring> m_hwCollapsedUsbNodes; // collapsed parent deviceInstanceIds in USB TreeView

    // Payload of the card-hit that was just clicked (Sprzęt / Programy views)
    int m_hwActionIndex = 0;
    std::wstring m_hwActionArg;

    // "Programy" tab state
    ProgramInventory m_programs;
    bool m_progLoaded = false;
    int m_progSortCol = 0;
    bool m_progSortAsc = true;
    std::wstring m_progSelectedKey;
    std::wstring m_progFilter;             // text typed into the top-right search box while on the Programy tab
    ULONGLONG m_progRefreshAtTick = 0;     // pending auto-refresh (after install / uninstall was launched)
    int m_progRefreshesLeft = 0;

    // "Monitoring" tab state (the recorder itself runs in a separate background process)
    int m_monRangeIdx = 2;                         // 0:1h 1:6h 2:24h 3:7d 4:30d
    int m_monLoadedRangeIdx = -1;
    MonitorHistory m_monHistory;                   // incremental reader of the recorder's CSV files
    std::vector<MonitorSmartPoint> m_monSmart;     // hourly SMART snapshots in the selected range
    struct MonitorSeriesCacheEntry {
        uint64_t version = 0;
        int range = -1;
        int buckets = 0;
        std::vector<float> a, b;                   // bucketed series (NaN = no data)
        bool hasData = false;
    };
    std::map<int, MonitorSeriesCacheEntry> m_monSeriesCache;   // chart id -> bucketed series
    int m_monZoomChart = -1;                       // chart id shown "fill window", -1 = overview
    ULONGLONG m_monLoadTick = 0;
    MonitorSettings m_monSettings;
    bool m_monDaemonRunning = false;
    int m_monAutostartMode = -1;                   // -1 = needs refresh, 0 off, 1 Run key, 2 elevated scheduled task
    uint64_t m_monDataBytes = 0;
    bool m_monForceReload = true;

    // "LAN" tab state (scanner threads live inside LanScanner)
    LanScanner m_lan;
    std::vector<LanAdapterInfo> m_lanAdapters;
    int m_lanAdapterIdx = 0;
    bool m_lanInit = false;
    std::wstring m_lanSelectedIp;

    // Git dock state (the git CLI work happens on GitIntegration's worker thread)
    enum GitHitKind { GH_Tab = 1, GH_Refresh, GH_Fetch, GH_Pull, GH_Push, GH_DiffStat, GH_File, GH_Commit,
                      GH_CopyHash, GH_Branch, GH_CloseOutput, GH_CopyOutput };
    struct GitDockHit {
        D2D1_RECT_F rect = {};
        int kind = 0;
        std::wstring arg;
        int index = 0;
    };
    GitIntegration m_git;
    bool m_gitEnabled = true;                     // "Integracja GIT" checkbox (persisted)
    std::wstring m_gitRoot;                       // repo root of the current folder, empty = not a repo
    GitRepoState m_gitState;
    std::wstring m_gitOutput;
    bool m_gitOutputOk = true;
    uint64_t m_gitStateVersion = ~0ull;
    int m_gitTab = 0;                             // 0 changes, 1 commits, 2 branches
    float m_gitScrollY = 0.0f;
    float m_gitMaxScrollY = 0.0f;
    float m_gitDockWidth = 340.0f;
    bool m_draggingGitSplitter = false;
    bool m_gitShowOutput = false;
    bool m_gitReloadFolderWhenIdle = false;       // folder listing refresh after pull / checkout finished
    std::wstring m_gitSelectedCommit;
    ULONGLONG m_gitLastRequestTick = 0;
    std::vector<GitDockHit> m_gitHits;

    D2D1_RECT_F m_addressBarRect = {};
    D2D1_RECT_F m_searchBarRect = {};
    D2D1_RECT_F m_ScrollbarThumbRect = {};
    D2D1_RECT_F m_mediaSeekBarRect = {};

    // Per-folder scroll/selection memory & Dual-Pane Split View state
    std::map<std::wstring, FolderViewState> m_folderViewStates;
    bool m_splitView = false;
    int m_activePane = 0;
    float m_splitRatio = 0.5f;
    bool m_draggingSplitSplitter = false;
    bool m_inSplitRefresh = false;
    mutable bool m_overrideMainRect = false;
    mutable D2D1_RECT_F m_customMainRect = {};
    D2D1_RECT_F m_paneHeaderAddressRect[2] = {};
    ExplorerPaneState m_otherPane;
    FileContentPane m_contentPane;
    BackupManager m_backups;
    int m_backupSelectedJobId = 1;
    ULONGLONG m_lastBackupTickMs = 0;
    TransferManager m_transfers;
    int m_xferSelectedJobId = 0;
    ULONGLONG m_lastDirChangedWhileXferTick = 0;
    bool m_pendingDeferredDirRefresh = false;
    ITaskbarList3* m_taskbarList = nullptr;
    UINT m_wmTaskbarBtnCreated = 0;
    int m_lastTbState = 0;
    int m_lastTbPermille = -1;
};

} // namespace Explorer2
