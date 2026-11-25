#pragma once
#include "Types.h"
#include "Localization.h"
#include "FileEngine.h"
#include "ImageCache.h"
#include "ShellMenuEmulator.h"
#include "ViewerCrop.h"
#include "ClipboardHistory.h"
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
#include <mutex>
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
    void FinishRibbonBody(size_t firstHit, const D2D1_RECT_F& body);
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
    // ZIP of the items with 7-Zip when it is installed, else with Windows' own "Compressed (zipped) folder"
    void CompressToZip(const std::vector<std::wstring>& paths);
    // Deletes these items (Recycle Bin or for good) as a job of the transfer queue: the selection, or a drop on "Kosz"
    void DeletePaths(std::vector<std::wstring> toDelete, bool permanent);
    // "E-mail" on the Share tab: a new message with the files attached (MAPI), else a message listing the paths
    void EmailItems(const std::vector<std::wstring>& paths);
    void ShowRenameFailure(const std::wstring& oldName, const std::wstring& newName, const std::wstring& reason, DWORD err);

    // Version-control dock (right side) for folders inside a git repository or a Subversion working copy
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
    // full: also re-read what comes from a server (a Subversion history), for the dock's Refresh button
    void SyncGitRepo(bool force = false, bool full = false);
    // Repository folders (RepoBrowser) and a server's list of shares (\\server) are read-only: when `path` is one,
    // says so in a toast and returns true
    bool RefuseReadOnlyFolder(const std::wstring& path);
    // F5, the refresh button and the context menu: like RefreshCurrentFolder, and a repository folder or a network
    // server is asked again (a listing that arrives by itself refreshes through RefreshCurrentFolder, which keeps it)
    void RefreshFromUser();
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
    bool IsClipboardView() const { return m_viewMode == ViewMode::Hardware && m_activeRibbonTab == RibbonTab::Clipboard; }

    // "Schowek" (Clipboard) tab: what was copied while the program runs, in memory only (ClipboardHistory.h)
    ClipboardHistory m_clipHistory;
    bool m_clipHistoryEnabled = true;
    uint64_t m_clipSelectedId = 0;
    std::unordered_map<uint64_t, ID2D1Bitmap*> m_clipThumbs;    // previews of the pictures, made on first paint
    // The context menu's "Open with" submenu: filled when it opens (WM_INITMENUPOPUP), since asking Windows which
    // programs it recommends takes a moment; the programs it shows, for the command that comes back
    HMENU m_openWithPopup = nullptr;
    std::wstring m_openWithExt;
    bool m_openWithIsImage = false;
    bool m_openWithFilled = false;
    std::vector<OpenWithApp> m_openWithMenuApps;
    bool FillOpenWithPopup(HMENU popup);
    void OnClipboardChanged();
    void DrawClipboardView(const D2D1_RECT_F& rect);
    uint64_t ClipEntryForCommand() const;   // the entry a card button names, else the selected one, else the newest
    ID2D1Bitmap* ClipThumbBitmap(const ClipboardEntry& e);
    void ReleaseClipThumbs();
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
    // Puts text into the search box from code. A single-line EDIT answers that with EN_CHANGE, as it does for typing,
    // and the EN_CHANGE handler starts or clears the search, which sets the text again: m_settingSearchEditText makes
    // the handler leave such a change alone (without it one click on the box ended in a stack overflow).
    void SetSearchEditText(const wchar_t* text);
    bool m_settingSearchEditText = false;
    void RebuildParsedSearchOptions();
    void ShowSearchTypeListMenu(int screenX, int screenY);
    void BeginSearchFieldEdit(int fieldCmdId, const D2D1_RECT_F& fieldRect);
    void EnsureWorkAnimTimerState();

    // Media Player integration
    void SyncMediaPlayerToSelection();
    // Volume and seeking of the transport bars (full viewer and preview pane)
    D2D1_RECT_F m_mediaVolumeBarRect = {};    // where the volume slider was drawn last; empty when there is none
    bool m_draggingMediaVolume = false;
    bool m_openMediaExternally = false;       // double-click / Enter / "Open" on video and audio start the system's player
    bool m_openImagesExternally = false;      // the same for pictures: the system's program instead of the built-in viewer
    void SetMediaVolumeFromX(float x);
    void NudgeMediaVolume(int steps);         // 5% per step; a muted player is unmuted
    void SeekMediaBy(double seconds);
    double m_mediaSeekTargetSec = -1.0;       // where SeekMediaBy last sent the player, for steps that follow quickly
    ULONGLONG m_mediaSeekTick = 0;
    std::wstring m_mediaSeekPath;
    void DrawMediaVolumeControl(const D2D1_RECT_F& area, bool darkBar);

    // Drives plugged in or removed while the window is open (WM_DEVICECHANGE; a look at the mount table on Linux)
    void OnDrivesChanged();
    // Linux: a volume that is listed as "not mounted" (MOUNT_VOLUME:/dev/...) is mounted on a worker thread, and
    // the window goes to its folder when the answer arrives (WM_APP_VOLUME_MOUNTED)
    struct VolumeMountResult {
        std::wstring devNode;
        std::wstring mountPoint;
        std::wstring error;
        bool ok = false;
    };
    void BeginMountVolume(const std::wstring& devNode);
    void OnVolumeMounted(const VolumeMountResult& result);
    std::wstring m_mountingDevice;            // the device being mounted now; empty = none
    std::wstring DriveListSignature() const;
    std::wstring m_driveListSignature;

    // Win+E starts Explorer2 (WinEOverride.h)
    bool m_winEOverrideSet = false;           // read from the registry at start-up and after a change
    void RefreshWinEState(bool repairMovedExe);
    void ToggleWinEOverride();

    // "A newer version is available" (UpdateCheck.h)
    bool m_updateCheckEnabled = true;
    long long m_updateLastCheckUnix = 0;      // when GitHub was last asked (seconds since 1970, UTC)
    std::wstring m_updateLatestTag;           // the newest release tag it reported, e.g. "v1.0.25"
    bool m_updateAvailable = false;
    std::wstring RunningVersionForUpdates() const;
    void StartUpdateCheckIfDue(bool force = false);
    void OnUpdateCheckResult(const std::wstring& tag);

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
    // A file drag that leaves the window continues as a Windows drag and drop, so the files can be dropped on other
    // programs (a browser upload, a chat window, Windows Explorer, the desktop). Returns false when none of the
    // paths is a file on disk (e.g. FTP), and the drag stays inside the window.
    // rightButton: the drag goes on while the right button is held, and the program it ends in shows its own menu
    bool StartShellDragOut(const std::vector<std::wstring>& paths, bool rightButton = false);
    // Ends a drag of items inside the window without dropping them anywhere
    void EndItemDrag();
    // Continues a drag of items as a Windows drag and drop (it left the window, or the window lost the mouse, e.g.
    // to Win+D showing the desktop while the button is still held); ends it when the button is not really held
    void HandOffItemDrag();
    // Whether the button the current drag is done with is still held down
    bool DragButtonHeld() const;
    // The window lost the mouse in the middle of a drag of items: hands it off while the button is held, else ends it
    void InterruptItemDrag();
    // The menu at the end of a drag with the right button: copy, move or create shortcuts here, or cancel
    void ShowDropMenu(const std::vector<std::wstring>& paths, const std::wstring& targetDir, POINT screenPt);
    // A shortcut to target in folder, named "<name> - skrót.lnk" (or " - Shortcut.lnk"), "(2)" etc. when taken
    bool CreateShortcutIn(const std::wstring& folder, const std::wstring& target, std::wstring* outLink = nullptr);
    void DrawDragOverlay();
    D2D1_RECT_F GetFullWorkspaceRect() const;
    D2D1_RECT_F GetSplitPaneOuterRect(int paneIdx) const;
    D2D1_RECT_F GetSplitPaneContentMainRect(int paneIdx) const;
    void SwapWithOtherPane();
    void SetActivePane(int paneIdx);
    void ToggleSplitView();
    void CopyOrMoveToOtherPane(bool move);
    void BeginPaneAddressEdit(int paneIdx);

    D2D1_RECT_F GetMainViewRect() const;
    D2D1_RECT_F GetContentItemsRect() const;
    // The widths the details columns are drawn and hit-tested with: the stored ones (m_colWidths) while they fit the
    // list, narrower ones (some columns left out, width 0) in a list too narrow for them. See DetailsColumns.h.
    static bool DetailsRowButtonsShownFor(const std::wstring& path);   // the "Operacje" column (Kosz / Usuń)
    void GetDetailsColumnWidthsFor(const D2D1_RECT_F& mainViewRect, const std::wstring& path, float out[5]) const;
    void GetDetailsColumnWidths(float out[5]) const;                    // for the pane being drawn / hit-tested
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
    void RestoreRecycleBinItems(bool restoreAll = false, int singleRowIndex = -1);
    void EmptyRecycleBinAction();
    void StartDeleteJob(const std::vector<std::wstring>& paths, bool permanent);
    bool ReportDeleteJobResult(const TransferCompletionEvent& ev);
    void ShowDeleteFailures(const TransferCompletionEvent& ev);
    void ShowTransferLockFailures(const TransferCompletionEvent& ev);
    void OpenLockInspectorForTransferJob(int jobId);
    void OpenLockInspectorForPaths(const std::vector<std::wstring>& rawPaths);
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

    // Display scaling. All layout, hit-testing and drawing works in DIPs (1/96 inch); the render target maps them to
    // the window's pixels at m_dpi. Mouse messages arrive in pixels (PxToDip), while the native edit boxes, popup
    // menus and dialogs need pixels (DipToPx). m_dpi follows the window between monitors (WM_DPICHANGED).
    UINT m_dpi = 96;
    std::map<HWND, D2D1_RECT_F> m_childDipRects;   // where each native edit box was last placed, in DIPs
    float PxToDip(int px) const { return static_cast<float>(px) * 96.0f / static_cast<float>(m_dpi); }
    int DipToPx(float dip) const { return static_cast<int>(dip * static_cast<float>(m_dpi) / 96.0f); }
    D2D1_SIZE_F ClientSizeDip() const;
    POINT ClientDipToScreen(float x, float y) const;
    void PlaceChildDip(HWND child, float left, float top, float width, float height);
    void ApplyDpi(UINT dpi);
    void RecreateEditFont();
    // The user's own scale on top of the display's (Settings tab, Ctrl+Plus / Ctrl+Minus / Ctrl+0): m_dpi is
    // m_systemDpi * m_uiScalePct / 100, so everything that follows m_dpi follows the user's scale too
    UINT m_systemDpi = 96;                    // what Windows reports for the window's monitor
    int m_uiScalePct = 100;
    UINT EffectiveDpi() const;
    void SetUiScale(int pct);
    void ShowUiScaleMenu();

    // Which sections of the navigation tree are shown (FileEngine::NavSection bits; Settings tab)
    unsigned m_navSections = FileEngine::kNavAllSections;
    void ShowNavSectionsMenu();

    // The "Ustawienia" (Settings) tab: the language, and which ribbon tabs the strip leaves out (bit t = the tab
    // whose RibbonTab value is t; RibbonTabLayout.h, RibbonTabShown)
    void SetLanguage(Lang lang);
    unsigned m_hiddenRibbonTabs = 0;
    void ShowRibbonTabsMenu();

    // Width of one line of text in a format, in DIPs. Kept per text and format: the tab strip asks on every paint.
    float MeasureTextWidth(const std::wstring& text, IDWriteTextFormat* fmt);
    std::unordered_map<std::wstring, float> m_textWidthCache;

    // A pinned or cloud folder that Windows shows with an icon of its own (Desktop, Downloads, OneDrive, a folder
    // with a desktop.ini icon) is drawn with that icon; false = an ordinary folder, drawn like every other one
    bool DrawOwnShellFolderIcon(const std::wstring& folderPath, const D2D1_RECT_F& iconR);

#ifdef _WIN32
    HBRUSH m_darkEditBrush = nullptr;         // background of the edit boxes in the dark theme
#endif

    ID2D1Factory* m_d2dFactory = nullptr;
    IDWriteFactory* m_dwriteFactory = nullptr;
    ID2D1HwndRenderTarget* m_rt = nullptr;

    IDWriteTextFormat* m_fmtRegular = nullptr;
    IDWriteTextFormat* m_fmtSmall = nullptr;
    IDWriteTextFormat* m_fmtSmallTrim = nullptr;      // m_fmtSmall that cuts a text too long for its box with an ellipsis
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
    // A window narrower than the active tab's buttons (e.g. on a 1024 x 768 screen) scrolls the ribbon body sideways
    float m_ribbonScrollX = 0.0f;
    float m_ribbonContentW = 0.0f;          // width of the active tab's buttons, measured at the last paint
    RibbonTab m_ribbonScrollTab = RibbonTab::Home;
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
    // A network folder whose server is still being asked (NetShares::CheckReachable): NavigateTo goes there when
    // the answer comes (WM_APP_NET_REACHED), unless another navigation came first
    struct PendingNetNav {
        std::wstring path;
        std::wstring selectName;
        bool addToHistory = false;
    };
    PendingNetNav m_pendingNetNav;
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
    ULONGLONG m_nextFolderSizeSyncTick = 0;     // the work timer fetches new folder sizes no sooner than this

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
    bool m_dragWithRightButton = false;     // the drag (or the press that may start one) is done with the right button
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

    // Full viewer: turning and cropping a picture, saved into its file (the work itself: TransformImageFile).
    // The crop rectangle is in pixels of the picture as it is shown; while it is on screen the picture is kept
    // fitted to the window, so that the whole of it can be reached.
    bool m_viewerCropMode = false;
    std::wstring m_viewerCropPath;                  // the file being cropped; showing another one ends the crop
    D2D1_RECT_F m_viewerCropRect = {};
    int m_viewerCropDrag = 0;                       // what the mouse holds: ViewerCropGrip (ViewerCrop.h), 0 = nothing
    D2D1_POINT_2F m_viewerCropDragStart = {};       // where it was taken, in picture pixels
    D2D1_POINT_2F m_viewerCropPressAt = {};         // the same in window DIPs, to tell a click from a drag
    bool m_viewerCropDragMoved = false;
    D2D1_RECT_F m_viewerCropDragRect = {};          // the rectangle at that moment
    D2D1_RECT_F m_viewerImageRect = {};             // where the picture was drawn last (window DIPs); empty = none
    uint32_t m_viewerImageW = 0;                    // its size in pixels
    uint32_t m_viewerImageH = 0;
    bool IsViewerCropping() const;                  // the crop rectangle is on, for the picture that is shown
    bool ViewerPictureIsEditable(std::wstring* outPath = nullptr);   // a picture file on a disk, not busy
    void RotateViewerPicture(bool clockwise);
    void BeginViewerCrop();
    void EndViewerCrop(bool save, bool asCopy);
    bool OnViewerCropMouseDown(float x, float y);
    void OnViewerCropMouseMove(float x, float y);
    int ViewerCropGripAt(float x, float y) const;   // what a press there would take hold of (ViewerCrop.h)
    // Clicking the sides of the picture: -1 previous file, +1 next, 0 none (see ViewerSideZone in ViewerCrop.h)
    D2D1_RECT_F m_viewerCanvasRect = {};            // the picture area of the last paint (window DIPs)
    int m_viewerSidePress = 0;                      // the side a press began on; 0 once it moved (a pan)
    D2D1_POINT_2F m_viewerSidePressAt = {};
    int m_viewerSideHover = 0;                      // the side the mouse is over: its arrow is drawn
    int ViewerSideForClick(float x, float y) const;

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
    bool m_progLoaded = false;             // false: (re)read the list on the next paint
    // The registry read behind the list runs on a worker thread; the paint takes its result once done is set
    struct ProgramListLoad {
        std::mutex mutex;
        bool done = false;
        std::vector<InstalledProgram> items;
        ULONGLONG startTick = 0;
    };
    std::shared_ptr<ProgramListLoad> m_progLoad;   // the read in progress, if any
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
    bool m_lanInit = false;            // the adapters were listed (first visit of the tab; that starts no scan)
    bool m_lanScanStarted = false;     // the user started a host scan ("Skanuj sieć"): the button then says "ponownie"
    std::wstring m_lanSelectedIp;
    void StartLanScan();               // the chosen adapter's network, only on the user's click (CMD_LAN_SCAN / _ADAPTER)

    // Version-control dock state (the git / svn CLI work happens on GitIntegration's worker thread)
    enum GitHitKind { GH_Tab = 1, GH_Refresh, GH_Fetch, GH_Pull, GH_Push, GH_DiffStat, GH_File, GH_Commit,
                      GH_CopyHash, GH_Branch, GH_CloseOutput, GH_CopyOutput, GH_CheckUpdates, GH_CopyText, GH_BrowseRepo };
    struct GitDockHit {
        D2D1_RECT_F rect = {};
        int kind = 0;
        std::wstring arg;
        int index = 0;
    };
    GitIntegration m_git;
    bool m_gitEnabled = true;                     // "Integracja Git / SVN" checkbox (persisted)
    std::wstring m_gitRoot;                       // repo root of the current folder, empty = not a repo
    VcsKind m_gitKind = VcsKind::Git;             // git repository or Subversion working copy
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
