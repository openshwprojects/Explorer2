#pragma once
#include "Types.h"
#include "Localization.h"
#include "NotepadViewer.h"
#include "HexViewer.h"
#include "Model3DViewer.h"
#include "EdaViewer.h"
#include "SpreadsheetDocViewer.h"
#include "ArchiveExplorer.h"
#include "PdfViewer.h"
#include "ImageCache.h"
#include "MediaPlayer.h"
#include <d2d1.h>
#include <dwrite.h>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace Explorer2 {

enum class FileContentTabMode : int {
    Auto = 0,
    Notepad = 1,
    Hex = 2,
    Model3D = 3,
    EdaPcb = 4,
    Office = 5,
    Archive = 6,
    Pdf = 7,
    AiModel = 8
};

enum class ResolvedContentKind : int {
    None = 0,
    DirectoryInfo = 1,
    ImagePreview = 2,
    MediaPreview = 3,
    NotepadEditor = 4,
    HexInspector = 5,
    Model3DViewport = 6,
    EdaViewport = 7,
    OfficeDocument = 8,
    ArchiveSubExplorer = 9,
    PdfDocument = 10,
    AiModelInspector = 11
};

struct ImageMetadataSummary {
    bool inspected = false;
    bool hasAiMeta = false;
    bool hasExif = false;
    std::wstring generator;       // e.g. "Stable Diffusion / Fooocus", "ComfyUI"
    std::wstring positivePrompt;
    std::wstring negativePrompt;
    std::wstring seed;
    std::wstring steps;
    std::wstring sampler;
    std::wstring cfgScale;
    std::wstring modelName;
    std::wstring exifSummary;     // e.g. "Canon EOS R5 • 50mm • f/1.8 • 1/250s • ISO 100"
};

struct AiModelInfoSummary {
    bool valid = false;
    std::wstring formatName;      // "SafeTensors" or "GGUF"
    std::wstring arch;
    std::wstring modelTitle;
    std::wstring baseModel;
    std::wstring precisionSummary;
    std::wstring networkDimAlpha;
    std::wstring contextLength;
    std::wstring triggerWords;
    uint64_t tensorCount = 0;
    uint64_t metadataKeyCount = 0;
    std::vector<std::pair<std::wstring, std::wstring>> kvRows;
};

class FileContentPane {
public:
    FileContentPane() = default;
    ~FileContentPane();

    void Initialize(
        HWND parentHwnd,
        HINSTANCE hInstance,
        ID2D1Factory* d2dFactory,
        IDWriteFactory* dwriteFactory);
    void Shutdown();

    void SyncToSelection(const FileEntry* entry, bool paneVisible);
    void HideNativeControls();

    // Reading a preview that can take a while (a PDF, the table of contents of an archive, a document, a
    // board, the header of an AI model, the listing of a folder, and before any of them a file that first
    // has to come from an FTP server or out of an archive) is done by a worker thread, so that selecting
    // such a file never holds the window up. A preview read within a moment is shown at once; otherwise the
    // pane shows a note until the worker posts WM_APP_PREVIEW_READY, and the window then calls this, which
    // puts what was read into its viewer. True when the pane shows something new. A result the pane no
    // longer waits for (another file was selected meanwhile) is dropped.
    bool OnPreviewLoaded();
    // A preview is being read in the background (the pane's note shows a moving mark, so it wants repaints)
    bool IsPreviewLoading() const { return m_previewWanted != 0; }

    // Tests: the worker waits this long before each job, as if every file were slow to read
    void SetPreviewDelayForTesting(DWORD ms) { m_previewDelayForTesting = ms; }
    // Tests: turns the PDF page as the page buttons do
    void TurnPdfPageForTesting(int direction);
    struct PreviewTestState {
        ResolvedContentKind kind = ResolvedContentKind::None;
        bool loading = false;
        bool fetching = false;
        bool fetchFailed = false;
        std::wstring currentPath;       // the file or folder shown (the local copy, once fetched)
        std::wstring pdfPath;
        uint32_t pdfPages = 0;
        uint32_t pdfCurrentPage = 0;
        uint32_t pdfPicturePage = 0;
        uint32_t pdfPictureWidth = 0;
        std::wstring archivePath;
        size_t archiveEntries = 0;
        std::wstring officePath;
        bool officeValid = false;
        std::wstring edaPath;
        bool edaValid = false;
        std::wstring folderPath;
        size_t folderItems = 0;
        uint64_t jobsFinished = 0;      // by the worker, those given up included
        uint64_t jobsRead = 0;          // jobs the worker actually read (not given up before it began)
        uint64_t resultsShown = 0;      // results that went into a viewer
    };
    PreviewTestState GetPreviewStateForTesting() const;

    bool HasNotepadFocus() const { return m_notepad.HasFocus(); }
    HWND GetNotepadEditHwnd() const { return m_notepad.GetEditHwnd(); }
    void OnNotepadEditChanged() { m_notepad.OnEditChanged(); }
    bool NeedsAnimationTick() const {
        return (m_resolvedKind == ResolvedContentKind::Model3DViewport && m_model3D.NeedsAnimationTick()) ||
               (m_resolvedKind == ResolvedContentKind::EdaViewport && m_eda.NeedsAnimationTick());
    }
    void StepAnimation() {
        if (m_resolvedKind == ResolvedContentKind::Model3DViewport) {
            m_model3D.StepAnimation();
        } else if (m_resolvedKind == ResolvedContentKind::EdaViewport) {
            m_eda.StepAnimation();
        }
    }

    std::wstring TakePendingNavigation() {
        std::wstring out = std::move(m_pendingNavigatePath);
        m_pendingNavigatePath.clear();
        return out;
    }

    // An "Extract" button of the archive view was clicked (see ArchiveExplorer::ExtractRequest)
    bool TakePendingExtract(ArchiveExplorer::ExtractRequest& out) { return m_archive.TakePendingExtract(out); }

    void Draw(
        ID2D1RenderTarget* rt,
        ID2D1SolidColorBrush* brush,
        IDWriteTextFormat* fmtRegular,
        IDWriteTextFormat* fmtSmall,
        IDWriteTextFormat* fmtBold,
        IDWriteTextFormat* fmtCenterSmall,
        ImageCache& imageCache,
        MediaPlayer& mediaPlayer,
        const D2D1_RECT_F& rect,
        bool draggingSplitter,
        Lang lang,
        D2D1_POINT_2F mousePt,
        D2D1_RECT_F& outMediaSeekBarRect);

    // Returns true if the click was handled inside the FileContentPane.
    bool OnMouseDown(
        float x,
        float y,
        bool rightButton,
        Lang lang,
        bool* outCloseRequested,
        int* outTriggerCmdId,
        std::wstring* outStatusToast);

    bool OnMouseMove(float x, float y, WPARAM keyFlags);
    bool OnMouseUp();
    bool OnMouseWheel(short delta, float x, float y);

private:
    ResolvedContentKind ResolveKindForCurrentEntry() const;
    void ActivateResolvedViewer();
    void LoadDirectoryPreview(const std::wstring& folderPath, bool forceReload = false);

    // Background reading (see OnPreviewLoaded)
    enum class PreviewJobKind : int { None = 0, Fetch, Pdf, PdfPage, Archive, Office, Eda, AiModel, Folder };
    struct PreviewJob;
    struct PreviewResult;
    struct PreviewWorker;       // shared by the pane and its worker thread
    static void PreviewWorkerLoop(std::shared_ptr<PreviewWorker> worker);
    static void RunPreviewJob(const PreviewJob& job, PreviewResult& out, const std::function<bool()>& keepGoing);
    static void ListFolderForPreview(const std::wstring& folderPath, PreviewResult& out, const std::function<bool()>& keepGoing);
    bool EnsurePreviewWorker();
    void StartPreviewJob(PreviewJob&& job);
    void RequestPreview(PreviewJobKind kind, const std::wstring& path, bool alreadyShown);
    void RequestPdfPage(uint32_t page);
    void ForwardPdfPageRequest();
    void StopWaitingForPreview();
    void StopPreviewWorker();
    bool IsWaitingFor(PreviewJobKind kind, const std::wstring& path) const;
    void ShowPreviewResult(PreviewResult& result);
    // Shows m_currentEntry: a file on an FTP server or inside an archive is fetched first (StartFetch)
    void ShowCurrentEntry();
    void StartFetch(const std::wstring& source, const std::wstring& innerPath);
    // The body shows the "loading" note instead of a viewer (a page being drawn keeps the PDF view)
    bool BodyShowsWaitNote() const {
        return m_fetching || (m_previewWanted != 0 && m_previewWantedKind != PreviewJobKind::PdfPage);
    }
    void DrawWaitNote(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, IDWriteTextFormat* fmtCenterSmall, bool pl);

    enum HeaderBtnId : int {
        HBTN_NONE = 0,
        HBTN_TAB_AUTO = 1,
        HBTN_TAB_NOTEPAD = 2,
        HBTN_TAB_HEX = 3,
        HBTN_TAB_3D = 4,
        HBTN_CLOSE_PANE = 5,
        HBTN_IMG_TOOLS = 6,
        HBTN_MEDIA_PLAY_PAUSE = 7,
        HBTN_DIR_ENTER = 8,
        HBTN_DIR_BACK_TO_FOLDER = 9,
        HBTN_TAB_EDA = 10,
        HBTN_TAB_OFFICE = 11,
        HBTN_TAB_ARCHIVE = 12,
        HBTN_TAB_PDF = 13,
        HBTN_TAB_AIMODEL = 14,
        HBTN_CALC_HASH = 15,
        HBTN_COPY_PROMPT = 16,
        HBTN_COPY_NEG_PROMPT = 17,
        HBTN_COPY_SEED = 18,
        HBTN_COPY_MODEL_META = 19,
        HBTN_COPY_TRIGGERS = 20
    };

    struct HeaderHit {
        int id = 0;
        D2D1_RECT_F rect = {};
    };

    struct DirPreviewEntry {
        std::wstring name;
        std::wstring fullPath;
        std::wstring ext;
        std::wstring dateStr;
        std::wstring sizeStr;
        uint64_t size = 0;
        bool isDirectory = false;
        bool isImage = false;
    };

    HWND m_parentHwnd = nullptr;
    NotepadViewer m_notepad;
    HexViewer m_hex;
    Model3DViewer m_model3D;
    EdaViewer m_eda;
    SpreadsheetDocViewer m_office;
    ArchiveExplorer m_archive;
    PdfViewer m_pdf;

    bool m_hasEntry = false;
    FileEntry m_currentEntry = {};
    FileEntry m_rootFolderEntry = {};
    bool m_inspectingChildOfFolder = false;
    FileContentTabMode m_userTabMode = FileContentTabMode::Auto;
    ResolvedContentKind m_resolvedKind = ResolvedContentKind::None;

    // Folder Content Sub-Explorer state
    std::wstring m_dirPreviewPath;
    std::vector<DirPreviewEntry> m_dirPreviewItems;
    size_t m_dirFolderCount = 0;
    size_t m_dirFileCount = 0;
    uint64_t m_dirTotalFileBytes = 0;
    float m_dirScrollY = 0.0f;
    int m_dirSelectedIdx = -1;
    int m_dirHoverIdx = -1;
    ULONGLONG m_dirLastClickTick = 0;
    int m_dirLastClickIdx = -1;
    D2D1_RECT_F m_dirListRect = {};
    D2D1_RECT_F m_dirScrollTrackRect = {};
    D2D1_RECT_F m_dirScrollThumbRect = {};
    bool m_dirDraggingScroll = false;
    float m_dirScrollDragOffset = 0.0f;
    std::wstring m_pendingNavigatePath;

    D2D1_RECT_F m_paneRect = {};
    D2D1_RECT_F m_bodyRect = {};
    int m_hoverHeaderBtn = 0;
    std::vector<HeaderHit> m_headerHits;

    // Image AI / EXIF metadata, AI Model (.safetensors/.gguf) inspector, and File Hash state
    ImageMetadataSummary m_imgMeta = {};
    std::wstring m_imgMetaPath;
    AiModelInfoSummary m_aiModelInfo = {};
    std::wstring m_aiModelPath;
    float m_aiModelScrollY = 0.0f;
    std::wstring m_hashFilePath;
    std::wstring m_hashSummary; // "CRC32: ... • xxHash64: ... • SHA-256: ..."
    std::wstring m_sha256Hex;

    // Background reading (see OnPreviewLoaded)
    std::shared_ptr<PreviewWorker> m_worker;
    std::thread m_workerThread;
    uint64_t m_previewGeneration = 0;       // last number given to a job
    uint64_t m_previewWanted = 0;           // the job whose result the pane waits for, 0 = none
    PreviewJobKind m_previewWantedKind = PreviewJobKind::None;
    std::wstring m_previewWantedPath;
    ULONGLONG m_previewStartTick = 0;
    bool m_fetching = false;                // the selected file is being downloaded or unpacked
    bool m_fetchFailed = false;             // and that did not work
    DWORD m_previewDelayForTesting = 0;
    uint64_t m_resultsShown = 0;
};

} // namespace Explorer2
