#pragma once
#include "Types.h"
#include "Localization.h"
#include "NotepadViewer.h"
#include "HexViewer.h"
#include "Model3DViewer.h"
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
    Model3D = 3
};

enum class ResolvedContentKind : int {
    None = 0,
    DirectoryInfo = 1,
    ImagePreview = 2,
    MediaPreview = 3,
    NotepadEditor = 4,
    HexInspector = 5,
    Model3DViewport = 6
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
        return m_resolvedKind == ResolvedContentKind::Model3DViewport && m_model3D.NeedsAnimationTick();
    }
    void StepAnimation() {
        if (m_resolvedKind == ResolvedContentKind::Model3DViewport) {
            m_model3D.StepAnimation();
        }
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
    // Sets *outCloseRequested = true if [X] close button was clicked, or *outTriggerCmdId if an action button (e.g. Image Tools / Play) was clicked.
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

    enum HeaderBtnId : int {
        HBTN_NONE = 0,
        HBTN_TAB_AUTO = 1,
        HBTN_TAB_NOTEPAD = 2,
        HBTN_TAB_HEX = 3,
        HBTN_TAB_3D = 4,
        HBTN_CLOSE_PANE = 5,
        HBTN_IMG_TOOLS = 6,
        HBTN_MEDIA_PLAY_PAUSE = 7
    };

    struct HeaderHit {
        int id = 0;
        D2D1_RECT_F rect = {};
    };

    HWND m_parentHwnd = nullptr;
    NotepadViewer m_notepad;
    HexViewer m_hex;
    Model3DViewer m_model3D;

    bool m_hasEntry = false;
    FileEntry m_currentEntry = {};
    FileContentTabMode m_userTabMode = FileContentTabMode::Auto;
    ResolvedContentKind m_resolvedKind = ResolvedContentKind::None;

    D2D1_RECT_F m_paneRect = {};
    D2D1_RECT_F m_bodyRect = {};
    int m_hoverHeaderBtn = 0;
    std::vector<HeaderHit> m_headerHits;
};

} // namespace Explorer2
