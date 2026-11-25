#pragma once
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d2d1.h>
#include <string>
#include <vector>
#include <cstdint>

namespace Explorer2 {

// Custom window messages for async background notifications
constexpr UINT WM_APP_THUMB_READY         = WM_APP + 101;
constexpr UINT WM_APP_FULLIMG_READY       = WM_APP + 102;
constexpr UINT WM_APP_DIR_CHANGED         = WM_APP + 104;
constexpr UINT WM_APP_FOLDER_SIZE_UPDATED = WM_APP + 105;
constexpr UINT WM_APP_IMAGE_ROTATED       = WM_APP + 106;
constexpr UINT WM_APP_SEARCH_UPDATED      = WM_APP + 107;
constexpr UINT WM_APP_LOG_ADDED           = WM_APP + 108;   // new AppLog entry (coalesced)
constexpr UINT WM_APP_RENAME_DONE         = WM_APP + 109;   // lParam = heap RenameJobResult*
constexpr UINT WM_APP_MERGE_DONE          = WM_APP + 110;   // lParam = heap MergeFolderResult*
constexpr UINT WM_APP_EXTRACT_DONE        = WM_APP + 112;   // lParam = heap ExtractJobResult*
constexpr UINT WM_APP_EXT_ICONS_READY     = WM_APP + 113;   // file-type icons were looked up (FileEngine::RefreshIconIndices)
constexpr UINT WM_APP_PREVIEW_READY      = WM_APP + 114;   // the preview pane's worker has read a file (FileContentPane::OnPreviewLoaded)
constexpr UINT WM_APP_TRANSFER_UPDATE     = WM_APP + 111;   // CopyHandler-style TransferManager state/completion update
constexpr UINT WM_APP_NET_REACHED         = WM_APP + 115;   // a network server asked in the background has answered (NetShares)
constexpr UINT WM_APP_UPDATE_INFO         = WM_APP + 116;   // lParam = heap std::wstring*: newest release tag ("" = no answer)
constexpr UINT WM_APP_VOLUME_MOUNTED      = WM_APP + 117;   // lParam = heap VolumeMountResult*: a volume was mounted, or not
constexpr UINT WM_APP_DRAG_HANDOFF        = WM_APP + 118;   // a drag of items lost the mouse with the button held: go on as an OLE drag

enum class ViewMode : int {
    Details = 0,         // Szczegóły (table with columns)
    List = 1,            // Lista (compact multi-column list)
    SmallIcons = 2,      // Małe ikony (20px)
    MediumIcons = 3,     // Średnie ikony (72px)
    LargeIcons = 4,      // Duże ikony (140px)
    ExtraLargeIcons = 5, // Bardzo duże ikony (256px)
    JumboIcons = 6,      // Gigantyczne ikony (380px)
    Proportions = 7,     // Proporcje (WinDirStat / WizTree Squarified Treemap)
    PieChart = 8,        // Kołowy (Multi-ring Sunburst / Radial Pie Disk Space Chart)
    Hardware = 9,        // Sprzęt (Pro Mode Hardware & Real-Time Telemetry Workstation)
    FullViewer = 10      // Pełny podgląd w oknie (In-Window Image/Media Viewer + filmstrip)
};

enum class SortColumn : int {
    Name = 0,
    DateModified = 1,
    Type = 2,
    Size = 3
};

enum class GroupByMode : int {
    None = 0,
    NameInitial = 1,
    DateBucket = 2,
    FileType = 3,
    SizeTier = 4
};

enum class RibbonTab : int {
    File = 0,
    Home = 1,
    Share = 2,
    View = 3,
    Search = 4,
    Hardware = 5,
    Programs = 6,  // "Programy": installed programs list (rendered through the dashboard view host)
    Monitoring = 7, // "Monitoring": long-term system recording (rendered through the dashboard view host)
    Lan = 8,        // "LAN": network scanner + SSDP (rendered through the dashboard view host)
    Log = 9,        // "Log": this run's operation log (rendered through the dashboard view host)
    Backups = 10,   // "Kopie zapasowe": rclone + incremental sync backup manager
    Transfers = 11, // "Transfery": CopyHandler-style high-speed file transfer queue & engine
    Settings = 12,  // "Ustawienia": language, text size, how files open, which tabs and tree sections are shown
    Clipboard = 13  // "Schowek": what was copied while the program runs (ClipboardHistory.h)
};
constexpr int kRibbonTabCount = 14;

// The tabs the user cannot switch off in "Ustawienia" > "Karty wstążki" (RibbonTabLayout.h, RibbonTabShown): the
// strip always has File and Home, and Settings is where a hidden tab is brought back
constexpr unsigned kRibbonTabsAlwaysShown = (1u << static_cast<int>(RibbonTab::File)) | (1u << static_cast<int>(RibbonTab::Home)) |
                                            (1u << static_cast<int>(RibbonTab::Settings));

enum class HardwareSubTab : int {
    All = 0,
    Cpu = 1,
    Gpu = 2,
    Ram = 3,
    Motherboard = 4,
    Disks = 5,
    Network = 6,
    System = 7,
    Thermal = 8,
    UsbCom = 9
};

enum class SearchSizeUnit : int {
    Bytes = 0,
    KB = 1,
    MB = 2,
    GB = 3
};

enum SearchTypeCategoryFlags : uint32_t {
    SEARCH_CAT_NONE     = 0,
    SEARCH_CAT_IMAGES   = 1 << 0,
    SEARCH_CAT_MEDIA    = 1 << 1,
    SEARCH_CAT_CODE     = 1 << 2,
    SEARCH_CAT_ARCHIVES = 1 << 3,
    SEARCH_CAT_MODELS   = 1 << 4,
    SEARCH_CAT_APPS     = 1 << 5,
    SEARCH_CAT_DOCS     = 1 << 6
};

struct SearchOptions {
    std::wstring query;
    bool recursive = true;
    bool includeFiles = true;
    bool includeFolders = true;
    bool caseSensitive = false;
    bool searchContent = false;

    // Date Modified filter (Edytowany od - do)
    bool useDateFilter = false;
    std::wstring dateFromStr = L"2025-01-01";
    std::wstring dateToStr   = L"2026-12-31";
    FILETIME dateFromFt = {};
    FILETIME dateToFt   = {};
    bool hasDateFrom = false;
    bool hasDateTo   = false;

    // Size filter (Rozmiar od - do)
    bool useSizeFilter = false;
    std::wstring sizeMinStr = L"0";
    std::wstring sizeMaxStr = L"";
    SearchSizeUnit sizeUnit = SearchSizeUnit::MB;
    uint64_t sizeMinBytes = 0;
    uint64_t sizeMaxBytes = UINT64_MAX;

    // Type filter (Typy / Lista)
    bool useTypeFilter = false;
    uint32_t categoryMask = SEARCH_CAT_NONE;
    std::wstring customExtList; // e.g. L".cs;.cpp;.h"

    bool HasAnyActiveFilter() const {
        return useDateFilter || useSizeFilter || useTypeFilter ||
               !includeFiles || !includeFolders || caseSensitive || searchContent;
    }
};

struct SearchProgressInfo {
    bool isRunning = false;
    bool isFinished = false;
    uint64_t matchedCount = 0;
    uint64_t scannedFiles = 0;
    uint64_t scannedFolders = 0;
    uint64_t elapsedMs = 0;
    std::wstring currentFolder;
};

struct FileEntry {
    std::wstring name;
    std::wstring displayName;
    std::wstring fullPath;
    std::wstring originalPath; // Original path before Recycle Bin deletion (populated in RECYCLE_BIN)
    std::wstring normKey;   // Precomputed normalized lowercase path key for O(1) hash lookups
    std::wstring ext;       // Lowercase including dot, e.g. L".png"
    std::wstring typeName;  // Cached per extension
    std::wstring dateStr;   // Formatted "dd.MM.yyyy HH:mm"
    std::wstring sizeStr;   // Formatted "1 234 KB"
    uint64_t size = 0;
    FILETIME writeTime = {};
    DWORD attributes = 0;
    int sysIconIndex = -1;
    bool isDirectory = false;
    bool isDrive = false;
    bool isNetworkDrive = false;
    bool isOpticalDrive = false;
    bool isOffline = false;
    bool isImage = false;
    bool isVideo = false;
    bool isAudio = false;
    bool isHidden = false;
    bool isSelected = false;
    bool isCut = false;
};

// What double-click, Enter and "Open" do with a file: true shows it in the built-in viewer, which takes the place
// of the file list; false hands it to the program the system opens that kind of file with. Pictures, video and
// audio go to the viewer unless the user asked for their own programs ("Ustawienia" tab); everything else has
// always gone to its program.
inline bool OpensInBuiltInViewer(const FileEntry& item, bool imagesInOwnProgram, bool mediaInOwnProgram) {
    if (item.isDirectory) return false;
    if (item.isImage) return !imagesInOwnProgram;
    if (item.isVideo || item.isAudio) return !mediaInOwnProgram;
    return false;
}

struct TreeNode {
    std::wstring label;
    std::wstring path; // Empty for section headers ("Szybki dostęp"), or "THIS_PC", or directory path
    std::wstring normKey;
    int indent = 0;
    bool isHeader = false;
    bool isDrive = false;
    bool isNetworkDrive = false;
    bool isOpticalDrive = false;
    bool isOffline = false;
    bool isQuickAccess = false;
    bool isFtpNode = false;
    bool hasChildren = true;
    bool isExpanded = false;
    uint64_t size = 0;       // Folder size (or used bytes on drive)
    uint64_t totalBytes = 0; // Total capacity for drives
    uint64_t freeBytes = 0;  // Free space for drives
    std::wstring sizeStr;    // Formatted size badge (e.g. "184 / 465 GB" or "12.4 GB")
    D2D1_RECT_F hitRect = {};
    D2D1_RECT_F chevronRect = {};
};

struct BreadcrumbSegment {
    std::wstring label;
    std::wstring path;
    D2D1_RECT_F hitRect = {};
    D2D1_RECT_F arrowRect = {};
};

struct TreemapChildNode {
    std::wstring name;
    std::wstring fullPath;
    std::wstring normKey;
    std::wstring ext;
    uint64_t size = 0;
    bool isDirectory = false;
};

struct TreemapHitNode {
    D2D1_RECT_F rect = {};
    int topItemIndex = -1;
    int depth = 0;
    uint32_t color = 0x0078D7;
    uint64_t size = 0;
    bool isDirectory = false;
    bool isInFlight = false;
    std::wstring name;
    std::wstring relPath;
    std::wstring fullPath;
};

struct PieSectorHitNode {
    float startAngleDeg = 0.0f;
    float sweepAngleDeg = 0.0f;
    float innerRadius = 0.0f;
    float outerRadius = 0.0f;
    D2D1_RECT_F legendRect = {};
    bool hasLegendRect = false;
    int topItemIndex = -1;
    int depth = 0;
    uint32_t color = 0x546E7A;
    uint64_t size = 0;
    bool isDirectory = false;
    bool isInFlight = false;
    std::wstring name;
    std::wstring relPath;
    std::wstring fullPath;
};

struct HardwareCardHit {
    D2D1_RECT_F rect = {};
    int cmdId = 0;
    int actionIndex = 0;        // e.g. physical disk index for CMD_HW_SELECT_DISK
    std::wstring actionArg;     // e.g. network connection name for CMD_HW_NET_SETTINGS
    std::wstring copyText;
    std::wstring label;
};

struct UndoDeleteBatch {
    std::vector<std::pair<std::wstring, std::wstring>> originalToTrashPaths; // <originalFullPath, stagingTrashPath>
    // Folders sent to the Recycle Bin (whole, as the shell takes them): no undo copy can be made of a folder, so
    // undoing restores the newest Recycle Bin item that was there (FileEngine::UndoLastDelete)
    std::vector<std::wstring> recycledFolders;
};

} // namespace Explorer2

#include "VirtualFolders.h"   // IsVirtualFolder
#include "ImageExtensions.h"  // ImageExt::IsAnyImageExt

namespace Explorer2 {

static inline bool IsImageExtension(const std::wstring& extLower) {
    return ImageExt::IsAnyImageExt(extLower);
}

inline bool IsVideoExtension(const std::wstring& extLower) {
    return extLower == L".mp4"  || extLower == L".mkv"  || extLower == L".webm" ||
           extLower == L".mov"  || extLower == L".avi"  || extLower == L".wmv"  ||
           extLower == L".m4v";
}

inline bool IsAudioExtension(const std::wstring& extLower) {
    return extLower == L".mp3"  || extLower == L".wav"  || extLower == L".flac" ||
           extLower == L".ogg"  || extLower == L".m4a"  || extLower == L".aac"  ||
           extLower == L".wma"  || extLower == L".opus";
}

} // namespace Explorer2
