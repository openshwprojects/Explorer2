#pragma once
#include "Types.h"
#include "Localization.h"
#include "NotepadViewer.h"
#include "HexViewer.h"
#include "Model3DViewer.h"
#include "EdaViewer.h"
#include "SpreadsheetDocViewer.h"
#include "ArchiveExplorer.h"
#include "ImageCache.h"
#include "MediaPlayer.h"
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <vector>

namespace Explorer2 {

enum class FileContentTabMode : int {
    Auto = 0,
    Notepad = 1,
    Hex = 2,
    Model3D = 3,
    EdaPcb = 4,
    Office = 5,
    Archive = 6
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
    ArchiveSubExplorer = 9
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
        HBTN_TAB_ARCHIVE = 12
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
};

} // namespace Explorer2
