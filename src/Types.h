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
constexpr UINT WM_APP_DELETE_DONE         = WM_APP + 103;
constexpr UINT WM_APP_DIR_CHANGED         = WM_APP + 104;
constexpr UINT WM_APP_FOLDER_SIZE_UPDATED = WM_APP + 105;
constexpr UINT WM_APP_IMAGE_ROTATED       = WM_APP + 106;
constexpr UINT WM_APP_SEARCH_UPDATED      = WM_APP + 107;

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

enum class RibbonTab : int {
    File = 0,
    Home = 1,
    Share = 2,
    View = 3,
    Search = 4,
    Hardware = 5
};

enum class HardwareSubTab : int {
    All = 0,
    Cpu = 1,
    Gpu = 2,
    Ram = 3,
    Motherboard = 4,
    Disks = 5,
    Network = 6,
    System = 7
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
    bool hasChildren = true;
    bool isExpanded = false;
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
    uint64_t size = 0;
    bool isDirectory = false;
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
           extLower == L".jfif" || extLower == L".wdp"  || extLower == L".dds";
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
