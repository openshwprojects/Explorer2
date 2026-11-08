#pragma once
#include "Types.h"
#include "Localization.h"
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>

namespace Explorer2 {

enum class EdaDocKind : int {
    None = 0,
    KiCadPcb = 1,
    KiCadSch = 2,
    EagleBrd = 3,
    EagleSch = 4,
    GerberLayer = 5,
    ExcellonDrill = 6
};

enum class EdaLayerId : uint8_t {
    TopCopper = 0,
    BottomCopper = 1,
    InnerCopper = 2,
    TopSilk = 3,
    BottomSilk = 4,
    EdgeCuts = 5,
    PadsThrough = 6,
    Drills = 7,
    SchWire = 8,
    SchSymbol = 9,
    SchLabel = 10
};

enum class EdaPartCategory : uint8_t {
    GenericSMD = 0,
    IC_Package = 1,
    ResistorSMD = 2,
    CapacitorSMD = 3,
    ElectrolyticCap = 4,
    Connector = 5,
    TransistorSOT = 6,
    DiodeLED = 7,
    Inductor = 8,
    Crystal = 9,
    MountingHole = 10
};

struct EdaTraceSeg {
    float x1 = 0.0f, y1 = 0.0f;
    float x2 = 0.0f, y2 = 0.0f;
    float width = 0.25f;
    EdaLayerId layer = EdaLayerId::TopCopper;
    int netId = 0;
};

struct EdaCirclePrim {
    float cx = 0.0f, cy = 0.0f;
    float radius = 1.0f;
    float width = 0.2f;
    bool filled = false;
    EdaLayerId layer = EdaLayerId::TopSilk;
};

struct EdaPadPrim {
    float x = 0.0f, y = 0.0f;
    float w = 1.2f, h = 1.2f;
    float rotDeg = 0.0f;
    float drill = 0.0f;
    uint8_t shape = 0; // 0=circle, 1=rect, 2=roundrect, 3=oval
    EdaLayerId layer = EdaLayerId::TopCopper;
    std::wstring pinName;
    std::wstring netName;
};

struct EdaViaPrim {
    float x = 0.0f, y = 0.0f;
    float diameter = 0.8f;
    float drill = 0.4f;
    int netId = 0;
};

struct EdaTextLabel {
    float x = 0.0f, y = 0.0f;
    float size = 1.2f;
    EdaLayerId layer = EdaLayerId::TopSilk;
    std::wstring text;
};

struct EdaComponent3D {
    std::wstring refDes;
    std::wstring value;
    std::wstring footprint;
    float x = 0.0f, y = 0.0f;
    float rotDeg = 0.0f;
    float widthMm = 3.0f;
    float lengthMm = 2.0f;
    float heightMm = 1.2f;
    bool bottomSide = false;
    EdaPartCategory category = EdaPartCategory::GenericSMD;
    int pinCount = 2;
};

struct ParsedEdaBoard {
    std::wstring filePath;
    std::wstring formatTitle;
    EdaDocKind kind = EdaDocKind::None;
    bool valid = false;

    std::vector<EdaTraceSeg> traces;
    std::vector<EdaCirclePrim> circles;
    std::vector<EdaPadPrim> pads;
    std::vector<EdaViaPrim> vias;
    std::vector<EdaTextLabel> labels;
    std::vector<EdaComponent3D> components;

    float minX = -25.0f, minY = -20.0f;
    float maxX = 25.0f,  maxY = 20.0f;
    size_t netCount = 0;
};

class EdaViewer {
public:
    EdaViewer() = default;
    ~EdaViewer();

    void Initialize(HWND notifyHwnd, ID2D1Factory* d2dFactory);
    void Shutdown();

    static bool IsEdaExtension(const std::wstring& extLower);
    static bool InspectEdaFile(const std::wstring& fullPath, ParsedEdaBoard& outBoard);

    void LoadFile(const std::wstring& fullPath, bool forceReload = false);
    void Clear();

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
