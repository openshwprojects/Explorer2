#pragma once
#include "Types.h"
#include "Localization.h"
#include "EdaTypes.h"
#include "EdaReaders.h"
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>

namespace Explorer2 {

class EdaViewer {
public:
    EdaViewer() = default;
    ~EdaViewer();

    void Initialize(HWND notifyHwnd, ID2D1Factory* d2dFactory);
    void Shutdown();

    // The readers themselves live in EdaReaders.cpp
    static bool IsEdaExtension(const std::wstring& extLower) { return EdaReaders::IsEdaExtension(extLower); }
    static bool IsLikelySpiceRawFile(const std::wstring& fullPath) { return EdaReaders::IsLikelySpiceRawFile(fullPath); }
    static bool IsLikelyAsicDefFile(const std::wstring& fullPath) { return EdaReaders::IsLikelyAsicDefFile(fullPath); }
    static bool IsLikelyEasyEdaJsonFile(const std::wstring& fullPath) { return EdaReaders::IsLikelyEasyEdaJsonFile(fullPath); }
    static bool IsLikelyLibertyOrEdaSdfFile(const std::wstring& fullPath, const std::wstring& extLower) { return EdaReaders::IsLikelyLibertyOrEdaSdfFile(fullPath, extLower); }
    static bool IsLikelyOdbPlusPlusTgz(const std::wstring& fullPath) { return EdaReaders::IsLikelyOdbPlusPlusTgz(fullPath); }
    static bool InspectEdaFile(const std::wstring& fullPath, ParsedEdaBoard& outBoard) { return EdaReaders::InspectEdaFile(fullPath, outBoard); }

    void LoadFile(const std::wstring& fullPath, bool forceReload = false);
    void Clear();

    // A board read on another thread (InspectEdaFile) goes into the view, which then shows what LoadFile
    // would have shown.
    void ShowLoadedBoard(const std::wstring& fullPath, ParsedEdaBoard&& board);
    bool IsShowing(const std::wstring& fullPath) const { return !m_currentPath.empty() && m_currentPath == fullPath; }
    const std::wstring& LoadedPath() const { return m_currentPath; }
    bool HasValidBoard() const { return m_board.valid; }

    bool NeedsAnimationTick() const { return m_view3D && m_autoRotate3D; }
    void StepAnimation();

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
    bool OnMouseMove(float x, float y, WPARAM keyFlags);
    bool OnMouseUp();
    bool OnMouseWheel(short delta, float x, float y);

private:
    void Fit2DView();
    void Reset3DCamera();

    void Draw2DCanvas(
        ID2D1RenderTarget* rt,
        ID2D1SolidColorBrush* brush,
        IDWriteTextFormat* fmtSmall,
        IDWriteTextFormat* fmtBold,
        const D2D1_RECT_F& canvasR,
        const ParsedEdaBoard& board,
        Lang lang);

    void Draw3DPcbViewport(
        ID2D1RenderTarget* rt,
        ID2D1SolidColorBrush* brush,
        IDWriteTextFormat* fmtSmall,
        IDWriteTextFormat* fmtBold,
        const D2D1_RECT_F& canvasR,
        const ParsedEdaBoard& board,
        Lang lang);

    enum ToolbarBtnId : int {
        BTN_NONE = 0,
        BTN_TOGGLE_2D_3D = 1,
        BTN_LAYER_TOPCU = 2,
        BTN_LAYER_BOTCU = 3,
        BTN_LAYER_SILK = 4,
        BTN_LAYER_PADS = 5,
        BTN_SOLDER_MASK_COLOR = 6,
        BTN_TOGGLE_3D_PARTS = 7,
        BTN_AUTO_ROTATE_3D = 8,
        BTN_TOGGLE_BOM = 9,
        BTN_FIT_RESET = 10
    };

    struct HitRect {
        int id = 0;
        D2D1_RECT_F rect = {};
    };

    HWND m_notifyHwnd = nullptr;
    ID2D1Factory* m_d2dFactory = nullptr;

    std::wstring m_currentPath;
    ParsedEdaBoard m_board;

    // View mode: 2D CAD vs 3D PCB + Parts
    bool m_view3D = false;
    bool m_showTopCu = true;
    bool m_showBotCu = true;
    bool m_showSilk = true;
    bool m_showPads = true;
    bool m_show3DParts = true;
    bool m_autoRotate3D = false;
    bool m_showBomPanel = false;
    int m_solderMaskPreset = 0; // 0=Green, 1=Blue, 2=Red, 3=Black, 4=Purple, 5=White

    // 2D camera state
    float m_zoom2D = 1.0f;
    float m_pan2DX = 0.0f;
    float m_pan2DY = 0.0f;

    // 3D camera state
    float m_yaw3D = 0.65f;
    float m_pitch3D = 0.62f;
    float m_zoom3D = 1.0f;
    float m_pan3DX = 0.0f;
    float m_pan3DY = 0.0f;
    ULONGLONG m_lastAnimTick = 0;

    // Interaction state
    bool m_draggingLeft = false;
    bool m_draggingRight = false;
    float m_lastMouseX = 0.0f;
    float m_lastMouseY = 0.0f;
    int m_selectedCompIdx = -1;
    int m_hoverBtnId = 0;
    float m_bomScrollY = 0.0f;

    D2D1_RECT_F m_canvasRect = {};
    D2D1_RECT_F m_bomRect = {};
    std::vector<HitRect> m_toolbarHits;
};

} // namespace Explorer2
