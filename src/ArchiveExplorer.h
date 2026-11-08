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
#include "Types.h"
#include "Localization.h"
#include <d2d1.h>
#include <dwrite.h>
#include <cstdint>
#include <string>
#include <vector>

namespace Explorer2 {

struct ArchiveItemEntry {
    std::wstring pathInArchive; // Forward-slash normalized relative path, no leading/trailing slash
    std::wstring name;          // Base name
    uint64_t uncompressedSize = 0;
    uint64_t compressedSize = 0;
    uint32_t crc32 = 0;
    uint16_t dosTime = 0;
    uint16_t dosDate = 0;
    bool isDirectory = false;
    // Internal offsets for fast direct extraction
    uint64_t rawDataOffset = 0;
    uint16_t compressionMethod = 0;
};

struct ArchiveSummaryInfo {
    std::wstring formatName; // e.g. L"ZIP", L"RAR4", L"RAR5", L"7-Zip", L"TAR.GZ"
    size_t totalFiles = 0;
    size_t totalFolders = 0;
    uint64_t totalUncompressedBytes = 0;
    uint64_t totalCompressedBytes = 0;
    std::vector<ArchiveItemEntry> entries;
    bool valid = false;
};

class ArchiveExplorer {
public:
    ArchiveExplorer() = default;
    ~ArchiveExplorer() = default;

    static bool IsArchiveExtension(const std::wstring& extLower);

    // Checks if a filesystem path points to an archive or a virtual subfolder inside an archive
    // e.g. "C:\Data\bundle.zip\src\main.cpp" -> outArchiveFile = "C:\Data\bundle.zip", outInnerPath = "src/main.cpp"
    static bool SplitVirtualArchivePath(
        const std::wstring& fullPath,
        std::wstring& outArchiveFile,
        std::wstring& outInnerPath);

    static bool IsVirtualArchivePath(const std::wstring& fullPath);

    // Reads the complete table of contents of an archive (.zip, .rar, .7z, .tar, .gz, .tgz, .jar, .nupkg, .apk)
    static bool InspectArchiveFile(const std::wstring& archivePath, ArchiveSummaryInfo& outInfo);

    // Lists immediate children at `innerDir` (e.g. "" for root or "src/core" for a subfolder)
    static bool EnumerateArchiveFolder(
        const std::wstring& archivePath,
        const std::wstring& innerDir,
        std::vector<ArchiveItemEntry>& outImmediateChildren,
        ArchiveSummaryInfo* outFullSummary = nullptr);

    // Extracts a single file from `archivePath` into memory or a temporary file on disk
    static bool ExtractSingleEntryToMemory(
        const std::wstring& archivePath,
        const std::wstring& innerPath,
        std::vector<uint8_t>& outBytes);

    static bool ExtractSingleEntryToTempFile(
        const std::wstring& archivePath,
        const std::wstring& innerPath,
        std::wstring& outTempFilePath);

    static bool ExtractEntireArchiveToFolder(
        const std::wstring& archivePath,
        const std::wstring& destFolderPath);

    // Right-Side File Content Pane UI methods
    void Initialize(HWND parentHwnd);
    void Shutdown();

    void LoadFile(const std::wstring& fullPath, bool forceReload = false);
    void Clear();

    // If the user double-clicked a file inside the archive to preview it in FileContentPane
    std::wstring TakePendingPreviewFilePath() {
        std::wstring out = std::move(m_pendingPreviewTempFile);
        m_pendingPreviewTempFile.clear();
        return out;
    }

    // If the user clicked "[Otwórz w eksploratorze ➔]" to browse the archive in the main pane
    std::wstring TakePendingNavigateArchivePath() {
        std::wstring out = std::move(m_pendingNavigateArchivePath);
        m_pendingNavigateArchivePath.clear();
        return out;
    }

    void Draw(
        ID2D1RenderTarget* rt,
        ID2D1SolidColorBrush* brush,
        IDWriteTextFormat* fmtRegular,
        IDWriteTextFormat* fmtSmall,
        IDWriteTextFormat* fmtBold,
        const D2D1_RECT_F& rect,
        Lang lang,
        D2D1_POINT_2F mousePt);

    bool OnMouseDown(float x, float y, bool rightButton, Lang lang, std::wstring* outStatusToast);
    bool OnMouseMove(float x, float y);
    bool OnMouseUp();
    bool OnMouseWheel(short delta, float x, float y);

private:
    void RebuildCurrentSubfolderView();

    enum BtnId : int {
        BTN_NONE = 0,
        BTN_UP_DIR = 1,
        BTN_EXTRACT_HERE = 2,
        BTN_EXTRACT_SUBFOLDER = 3,
        BTN_OPEN_AS_FOLDER = 4
    };

    struct HitRect {
        int id = 0;
        D2D1_RECT_F rect = {};
    };

    HWND m_parentHwnd = nullptr;
    std::wstring m_archivePath;
    std::wstring m_currentInnerDir; // "" for root, or "folder/sub"
    ArchiveSummaryInfo m_summary;
    std::vector<ArchiveItemEntry> m_visibleItems;

    float m_scrollY = 0.0f;
    int m_selectedIdx = -1;
    int m_hoverIdx = -1;
    int m_hoverBtnId = 0;
    ULONGLONG m_lastClickTick = 0;
    int m_lastClickIdx = -1;

    D2D1_RECT_F m_listRect = {};
    D2D1_RECT_F m_scrollTrackRect = {};
    D2D1_RECT_F m_scrollThumbRect = {};
    bool m_draggingScroll = false;
    float m_scrollDragOffset = 0.0f;
    std::vector<HitRect> m_toolbarHits;

    std::wstring m_pendingPreviewTempFile;
    std::wstring m_pendingNavigateArchivePath;
};

} // namespace Explorer2
