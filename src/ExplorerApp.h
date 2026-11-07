#pragma once
#include "Types.h"
#include "Localization.h"
#include "FileEngine.h"
#include "ImageCache.h"
#include "ShellMenuEmulator.h"
#include "MediaPlayer.h"
#include "HardwareInfo.h"
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <vector>

namespace Explorer2 {

struct RibbonButtonHit {
    int id = 0;
    D2D1_RECT_F rect = {};
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
    void DrawMarqueeOverlay();
    void DrawFullViewerCanvas(const D2D1_RECT_F& rect, bool includeFilmstrip);
    void DrawPreviewPane(const D2D1_RECT_F& rect);
    void DrawStatusBar(const D2D1_RECT_F& rect);

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
    void PerformUndoDelete();
    void CopySelected(bool isCut);
    void PasteIntoCurrent();
    void CopySelectedPaths();
    void CreateNewFolderAction();
    void StartInlineRename();
    void CommitAddressOrRenameEdit(HWND editHwnd);
    void CancelInlineEdits();
    void ShowContextMenu(int screenX, int screenY, int hitIndex);
    void ShowTreeContextMenu(int screenX, int screenY, int treeNodeIndex);
    void ShowBreadcrumbSubfolderMenu(int breadcrumbIndex);
    void ConfirmAndDeleteRowItem(int itemIndex, bool permanent);
    void ShowDeleteDropdownMenu(int screenX, int screenY);

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
    float m_colWidths[5] = { 320.0f, 145.0f, 155.0f, 115.0f, 128.0f };
    int m_resizingCol = -1;
    float m_resizeStartX = 0.0f;
    float m_resizeStartWidth = 0.0f;

    // Splitter state
    float m_treePaneWidth = 235.0f;
    float m_previewPaneWidth = 360.0f;
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
    int m_hoverBreadcrumbIndex = -1;
    int m_hoverBreadcrumbArrowIndex = -1;
    int m_hoverRibbonBtn = 0;
    int m_renamingIndex = -1;

    // Rubber-band / Marquee Box Selection
    bool m_isBoxSelecting = false;
    D2D1_POINT_2F m_boxStartPt = {};
    D2D1_POINT_2F m_boxCurrentPt = {};
    std::vector<bool> m_preBoxSelection;

    // Full Viewer Zoom / Pan state
    float m_viewerZoom = 1.0f;
    bool m_viewerActualPixels = false;
    float m_viewerPanX = 0.0f;
    float m_viewerPanY = 0.0f;
    bool m_viewerDraggingPan = false;
    D2D1_POINT_2F m_viewerDragStart = {};
    float m_filmstripScrollX = 0.0f;

    // Type-ahead buffer
    std::wstring m_typeAheadBuf;
    ULONGLONG m_lastTypeAheadTick = 0;

    // Status message toast
    std::wstring m_statusToast;
    ULONGLONG m_statusToastExpireTick = 0;

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
    float m_hwScrollY = 0.0f;
    float m_hwMaxScrollY = 0.0f;
    std::vector<HardwareCardHit> m_hwCardHits;
    int m_hoverHwCardIndex = -1;

    D2D1_RECT_F m_addressBarRect = {};
    D2D1_RECT_F m_searchBarRect = {};
    D2D1_RECT_F m_ScrollbarThumbRect = {};
    D2D1_RECT_F m_mediaSeekBarRect = {};
};

} // namespace Explorer2
