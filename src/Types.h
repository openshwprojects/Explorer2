#pragma once
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define NOMINMAX
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
    Transfers = 11  // "Transfery": CopyHandler-style high-speed file transfer queue & engine
};

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
};

inline bool IsImageExtension(const std::wstring& extLower) {
    return extLower == L".png"  || extLower == L".jpg"  || extLower == L".jpeg" ||
           extLower == L".webp" || extLower == L".bmp"  || extLower == L".gif"  ||
           extLower == L".tif"  || extLower == L".tiff" || extLower == L".ico"  ||
           extLower == L".jfif" || extLower == L".wdp"  || extLower == L".dds"  ||
           extLower == L".tga"  || extLower == L".icb"  || extLower == L".vda"  || extLower == L".vst" ||
           extLower == L".psd"  || extLower == L".psb"  || extLower == L".xcf"  ||
           extLower == L".hdr"  || extLower == L".pic"  ||
           extLower == L".ppm"  || extLower == L".pgm"  || extLower == L".pbm"  || extLower == L".pnm" ||
           extLower == L".pcx"  || extLower == L".qoi"  ||
           extLower == L".svg"  || extLower == L".xbm"  || extLower == L".xpm"  ||
           extLower == L".pam"  || extLower == L".pfm"  || extLower == L".sgi"  ||
           extLower == L".rgb"  || extLower == L".bw"   || extLower == L".rgba" ||
           extLower == L".ras"  || extLower == L".sun"  || extLower == L".ktx"  ||
           extLower == L".vtf"  || extLower == L".ff"   || extLower == L".farbfeld" ||
           extLower == L".fits" || extLower == L".fts"  || extLower == L".pkm"  ||
           extLower == L".astc" || extLower == L".pvr"  || extLower == L".iff"  ||
           extLower == L".ilbm" || extLower == L".lbm"  || extLower == L".tim"  ||
           extLower == L".wbmp" || extLower == L".bdf"  || extLower == L".psf"  ||
           extLower == L".y4m"  || extLower == L".ani"  || extLower == L".gpl"  ||
           extLower == L".pal"  || extLower == L".act"  || extLower == L".aco"  ||
           extLower == L".dcm"  || extLower == L".dicom"|| extLower == L".exr"  ||
           extLower == L".ktx2" || extLower == L".basis"|| extLower == L".kra"  ||
           extLower == L".ora"  || extLower == L".clip" || extLower == L".procreate" ||
           extLower == L".aseprite" || extLower == L".icns" || extLower == L".jxl" ||
           extLower == L".bpg"  || extLower == L".blp"  || extLower == L".utx"  ||
           extLower == L".dpx"  || extLower == L".cin"  || extLower == L".cube" ||
           extLower == L".3dl"  || extLower == L".ies"  || extLower == L".ttf"  ||
           extLower == L".otf"  || extLower == L".woff" || extLower == L".woff2"||
           extLower == L".cr2"  || extLower == L".cr3"  || extLower == L".nef"  ||
           extLower == L".arw"  || extLower == L".dng"  || extLower == L".raf"  ||
           extLower == L".orf"  || extLower == L".rw2"  || extLower == L".pef"  ||
           extLower == L".eps"  || extLower == L".ai"   || extLower == L".cdr"  ||
           extLower == L".afphoto" || extLower == L".afdesign" || extLower == L".afpub" ||
           extLower == L".emf"  || extLower == L".wmf"  || extLower == L".cgm"  ||
           extLower == L".abr"  || extLower == L".pat"  || extLower == L".dst"  ||
           extLower == L".pes"  || extLower == L".ans"  || extLower == L".sixel";
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
