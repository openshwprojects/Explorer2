#pragma once
#include "Types.h"
#include "Localization.h"
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <cstdint>

namespace Explorer2 {

struct Vec3f {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct MeshTriangle {
    uint32_t i0 = 0;
    uint32_t i1 = 0;
    uint32_t i2 = 0;
    Vec3f normal = { 0.0f, 1.0f, 0.0f };
};

struct LoadedMesh3D {
    std::wstring filePath;
    std::wstring formatName;
    std::wstring statusMessage;
    bool loadedOk = false;
    bool isLoading = false;

    std::vector<Vec3f> vertices;       // Normalized into [-1, 1] unit sphere
    std::vector<MeshTriangle> tris;
    size_t rawVertexCount = 0;
    size_t rawTriangleCount = 0;
    size_t animFrameCount = 1;
    Vec3f bboxMin = {};
    Vec3f bboxMax = {};
    Vec3f bboxSize = {};
};

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

    static bool Is3DModelExtension(const std::wstring& extLower);
    static bool InspectMeshFile(const std::wstring& fullPath, LoadedMesh3D& outMesh);

    void LoadFile(const std::wstring& fullPath, bool forceReload = false);
    void Clear();
    const std::wstring& GetCurrentPath() const { return m_requestedPath; }
    bool NeedsAnimationTick() const { return m_autoRotate || m_isLoading.load(); }
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

private:
    void ResetCamera();
    static LoadedMesh3D ParseMeshFromDisk(const std::wstring& fullPath, uint64_t generation, const std::atomic<uint64_t>* activeGen);

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
    std::atomic<uint64_t> m_loadGeneration{ 0 };
    std::atomic<bool> m_isLoading{ false };
    mutable std::mutex m_meshMutex;
    LoadedMesh3D m_mesh;

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
};

} // namespace Explorer2
