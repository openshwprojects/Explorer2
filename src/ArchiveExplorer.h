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

    // Unpacks a whole archive into a folder. Nothing that is already there is replaced: an entry whose name
    // is taken is written as "name (2).ext" (`outKeptExisting` counts those). An entry that would land outside
    // the folder ("..", an absolute path) is refused (`outRefused`).
    static bool ExtractEntireArchiveToFolder(
        const std::wstring& archivePath,
        const std::wstring& destFolderPath,
        size_t* outKeptExisting = nullptr,
        size_t* outRefused = nullptr);

    // Right-Side File Content Pane UI methods
    void Initialize(HWND parentHwnd);
    void Shutdown();

    void LoadFile(const std::wstring& fullPath, bool forceReload = false);
    void Clear();

    // The table of contents of an archive read on another thread (EnumerateArchiveFolder of its root) goes
    // into the view, which then shows what LoadFile would have shown.
    void ShowLoadedArchive(const std::wstring& fullPath, ArchiveSummaryInfo&& summary, std::vector<ArchiveItemEntry>&& rootItems);
    bool IsShowing(const std::wstring& fullPath) const { return !m_archivePath.empty() && m_archivePath == fullPath; }
    const std::wstring& LoadedPath() const { return m_archivePath; }
    size_t EntryCount() const { return m_summary.entries.size(); }

    // A file inside the archive was double-clicked: the preview pane shows it. It is not unpacked here, as
    // that can take seconds; the pane has it unpacked on its worker thread.
    struct PreviewEntryRequest {
        std::wstring archivePath;
        std::wstring innerPath;     // path inside the archive, with forward slashes
        std::wstring name;
        uint64_t size = 0;
    };
    bool TakePendingPreviewEntry(PreviewEntryRequest& out) {
        if (!m_hasPendingPreviewEntry) return false;
        out = std::move(m_pendingPreviewEntry);
        m_pendingPreviewEntry = {};
        m_hasPendingPreviewEntry = false;
        return true;
    }

    // If the user clicked "[Otwórz w eksploratorze ➔]" to browse the archive in the main pane
    std::wstring TakePendingNavigateArchivePath() {
        std::wstring out = std::move(m_pendingNavigateArchivePath);
        m_pendingNavigateArchivePath.clear();
        return out;
    }

    // Runs an external archive tool (7z.exe, UnRAR.exe, tar.exe) without a window and waits for it.
    // timeoutMs = INFINITE waits for as long as the tool takes. A tool that is still running when its time
    // is up is ended, and the caller is told so: it must never be left working unseen on files the caller
    // is about to read, hand to another tool or report on.
    enum class ToolRun { Succeeded, Failed, TimedOut };
    static ToolRun RunToolAndWait(const std::wstring& exePath, const std::wstring& args, DWORD timeoutMs);

    // If the user clicked one of the "Extract" buttons: what to unpack and where to. Unpacking can take
    // minutes, so the pane only records the wish; its owner runs ExtractEntireArchiveToFolder on a worker
    // thread and reports the outcome.
    struct ExtractRequest {
        std::wstring archivePath;
        std::wstring targetFolder;
        bool toSubfolder = false;
    };
    bool TakePendingExtract(ExtractRequest& out) {
        if (!m_hasPendingExtract) return false;
        out = std::move(m_pendingExtract);
        m_pendingExtract = {};
        m_hasPendingExtract = false;
        return true;
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

    PreviewEntryRequest m_pendingPreviewEntry;
    bool m_hasPendingPreviewEntry = false;
    std::wstring m_pendingNavigateArchivePath;
    ExtractRequest m_pendingExtract;
    bool m_hasPendingExtract = false;
};

} // namespace Explorer2
