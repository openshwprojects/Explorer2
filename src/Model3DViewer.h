#pragma once
#include "Types.h"
#include "Localization.h"
#include "Mesh3DTypes.h"
#include "Mesh3DReaders.h"
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <memory>
#include <cstdint>

namespace Explorer2 {

enum class RenderMode3D : int {
    Shaded = 0,
    ShadedWireframe = 1,
    Wireframe = 2
};

class Model3DViewer {
public:
    Model3DViewer() = default;
    ~Model3DViewer();

    void Initialize(HWND notifyHwnd, ID2D1Factory* d2dFactory);
    void Shutdown();

    // The readers themselves live in Mesh3DReaders.cpp
    static bool Is3DModelExtension(const std::wstring& extLower) { return MeshReaders::Is3DModelExtension(extLower); }
    static bool IsLikely3DDxfFile(const std::wstring& fullPath) { return MeshReaders::IsLikely3DDxfFile(fullPath); }
    static bool IsLikely3DGeoOrDemFile(const std::wstring& fullPath, const std::wstring& extLower) {
        return MeshReaders::IsLikely3DGeoOrDemFile(fullPath, extLower);
    }
    static bool InspectMeshFile(const std::wstring& fullPath, LoadedMesh3D& outMesh) {
        return MeshReaders::InspectMeshFile(fullPath, outMesh);
    }

    void LoadFile(const std::wstring& fullPath, bool forceReload = false);
    void Clear();
    const std::wstring& GetCurrentPath() const { return m_requestedPath; }
    bool NeedsAnimationTick() const { return m_autoRotate || m_load->loading.load(); }
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

    bool OnMouseDown(float x, float y, bool rightButton, Lang lang, std::wstring* outStatusToast = nullptr);
    bool OnMouseMove(float x, float y, WPARAM keyFlags);
    bool OnMouseUp();
    bool OnMouseWheel(short delta, float x, float y);

    // Draws a mesh into a picture with a depth buffer: every triangle is drawn, and where triangles overlap
    // the nearest one is seen. The picture is BGRA, premultiplied; pixels the model does not cover stay
    // transparent. The camera is the view's: turned by yaw, then pitch, seen from 3.6 units away.
    struct RasterView {
        int width = 0;              // picture size, pixels
        int height = 0;
        float centerX = 0.0f;       // where the model's centre lands, pixels
        float centerY = 0.0f;
        float scale = 1.0f;         // pixels per model unit
        float yaw = 0.0f;
        float pitch = 0.0f;
        RenderMode3D mode = RenderMode3D::Shaded;
        size_t maxTriangles = 0;    // 0: all of them
    };
    struct RasterScratch {
        std::vector<uint32_t> pixels;
        std::vector<float> depth;   // 1 / distance from the camera, 0 where nothing is
        std::vector<float> sx, sy, iw;
    };
    static void RasterizeMesh(const LoadedMesh3D& mesh, const RasterView& view, RasterScratch& scratch);

private:
    void ResetCamera();

    enum ButtonId : int {
        BTN_NONE = 0,
        BTN_MODE_CYCLE = 1,
        BTN_AUTOROTATE = 2,
        BTN_GRID = 3,
        BTN_RESET_CAM = 4
    };

    struct ToolbarHit {
        int id = 0;
        D2D1_RECT_F rect = {};
    };

    HWND m_notifyHwnd = nullptr;
    ID2D1Factory* m_d2dFactory = nullptr;

    std::wstring m_requestedPath;

    // What the loader thread and the viewer share. The thread holds a reference of its own, so a load that
    // is still running when the viewer goes away works on memory that is still there; its result is dropped.
    struct LoadState {
        std::atomic<uint64_t> generation{ 0 };   // counts the requests; a load that is not the newest gives up
        std::atomic<bool> loading{ false };
        std::mutex mutex;                        // guards mesh
        LoadedMesh3D mesh;
    };
    std::shared_ptr<LoadState> m_load = std::make_shared<LoadState>();

    // Camera & Viewport settings
    float m_yaw = 0.62f;
    float m_pitch = 0.38f;
    float m_zoom = 1.0f;
    float m_panX = 0.0f;
    float m_panY = 0.0f;
    RenderMode3D m_renderMode = RenderMode3D::Shaded;
    bool m_autoRotate = false;
    bool m_showGrid = true;
    ULONGLONG m_lastAnimTick = 0;

    bool m_draggingOrbit = false;
    bool m_draggingPan = false;
    D2D1_POINT_2F m_dragLastPt = {};
    D2D1_RECT_F m_viewportRect = {};

    int m_hoverBtnId = 0;
    std::vector<ToolbarHit> m_toolbarHits;

    // The picture of the model (RasterizeMesh) and what it was drawn for: a repaint of the window that
    // changes nothing here shows the same picture again without drawing it anew
    RasterScratch m_raster;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> m_rasterBitmap;
    ID2D1RenderTarget* m_rasterTarget = nullptr;
    UINT32 m_rasterW = 0;
    UINT32 m_rasterH = 0;
    struct RasterKey {
        uint64_t generation = 0;
        const void* meshData = nullptr;
        size_t triCount = 0;
        RasterView view;
    } m_rasterKey;
};

} // namespace Explorer2
