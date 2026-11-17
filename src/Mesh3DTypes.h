#pragma once
// The mesh the 3D model readers produce and Model3DViewer draws. No Windows headers: the readers also build on
// Linux (see linux/).
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

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

} // namespace Explorer2
