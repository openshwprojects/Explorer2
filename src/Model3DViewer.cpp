#include "Model3DViewer.h"
#include "PathUtils.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <thread>

namespace Explorer2 {

static D2D1_COLOR_F M3DColor(uint32_t rgb, float a = 1.0f) {
    return D2D1::ColorF(
        ((rgb >> 16) & 0xFF) / 255.0f,
        ((rgb >> 8) & 0xFF) / 255.0f,
        (rgb & 0xFF) / 255.0f,
        a);
}

static inline Vec3f VecSub(const Vec3f& a, const Vec3f& b) {
    return { a.x - b.x, a.y - b.y, a.z - b.z };
}

static inline Vec3f VecCross(const Vec3f& a, const Vec3f& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

static inline float VecDot(const Vec3f& a, const Vec3f& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static inline Vec3f VecNormalize(const Vec3f& v) {
    float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len > 1e-7f) {
        float inv = 1.0f / len;
        return { v.x * inv, v.y * inv, v.z * inv };
    }
    return { 0.0f, 1.0f, 0.0f };
}

Model3DViewer::~Model3DViewer() {
    Shutdown();
}

void Model3DViewer::Initialize(HWND notifyHwnd, ID2D1Factory* d2dFactory) {
    m_notifyHwnd = notifyHwnd;
    m_d2dFactory = d2dFactory;
}

void Model3DViewer::Shutdown() {
    ++m_loadGeneration;
}

bool Model3DViewer::Is3DModelExtension(const std::wstring& extLower) {
    return extLower == L".obj"  || extLower == L".stl"  || extLower == L".ply"  ||
           extLower == L".off"  || extLower == L".gltf" || extLower == L".glb"  ||
           extLower == L".3mf"  || extLower == L".fbx"  || extLower == L".dae"  ||
           extLower == L".3ds"  || extLower == L".step" || extLower == L".stp"  ||
           extLower == L".iges" || extLower == L".igs";
}

void Model3DViewer::Clear() {
    ++m_loadGeneration;
    m_requestedPath.clear();
    m_isLoading.store(false);
    std::lock_guard<std::mutex> lk(m_meshMutex);
    m_mesh = LoadedMesh3D{};
}

void Model3DViewer::ResetCamera() {
    m_yaw = 0.62f;
    m_pitch = 0.38f;
    m_zoom = 1.0f;
    m_panX = 0.0f;
    m_panY = 0.0f;
}

void Model3DViewer::StepAnimation() {
    if (!m_autoRotate) return;
    ULONGLONG now = GetTickCount64();
    float dt = (m_lastAnimTick > 0 && now > m_lastAnimTick)
        ? std::min(0.1f, static_cast<float>(now - m_lastAnimTick) * 0.001f)
        : 0.016f;
    m_lastAnimTick = now;
    m_yaw += dt * 0.85f;
    if (m_yaw > 6.2831853f) m_yaw -= 6.2831853f;
}

void Model3DViewer::LoadFile(const std::wstring& fullPath, bool forceReload) {
    if (!forceReload && m_requestedPath == fullPath && !m_requestedPath.empty()) {
        return;
    }
    m_requestedPath = fullPath;
    ResetCamera();

    uint64_t gen = ++m_loadGeneration;
    m_isLoading.store(true);
    {
        std::lock_guard<std::mutex> lk(m_meshMutex);
        m_mesh = LoadedMesh3D{};
        m_mesh.filePath = fullPath;
        m_mesh.isLoading = true;
    }

    HWND notifyWnd = m_notifyHwnd;
    std::thread([this, fullPath, gen, notifyWnd]() {
        LoadedMesh3D parsed = ParseMeshFromDisk(fullPath, gen, &m_loadGeneration);
        if (m_loadGeneration.load() != gen) return;
        {
            std::lock_guard<std::mutex> lk(m_meshMutex);
            m_mesh = std::move(parsed);
            m_mesh.isLoading = false;
        }
        m_isLoading.store(false);
        if (notifyWnd && IsWindow(notifyWnd)) {
            PostMessageW(notifyWnd, WM_APP_THUMB_READY, 0, 0);
        }
    }).detach();
}

static void FinalizeMeshBoundsAndNormals(LoadedMesh3D& mesh) {
    mesh.rawVertexCount = mesh.vertices.size();
    mesh.rawTriangleCount = mesh.tris.size();
    if (mesh.vertices.empty() || mesh.tris.empty()) return;

    Vec3f bmin = mesh.vertices[0];
    Vec3f bmax = mesh.vertices[0];
    for (const auto& v : mesh.vertices) {
        bmin.x = std::min(bmin.x, v.x); bmin.y = std::min(bmin.y, v.y); bmin.z = std::min(bmin.z, v.z);
        bmax.x = std::max(bmax.x, v.x); bmax.y = std::max(bmax.y, v.y); bmax.z = std::max(bmax.z, v.z);
    }
    mesh.bboxMin = bmin;
    mesh.bboxMax = bmax;
    mesh.bboxSize = VecSub(bmax, bmin);

    Vec3f center = {
        (bmin.x + bmax.x) * 0.5f,
        (bmin.y + bmax.y) * 0.5f,
        (bmin.z + bmax.z) * 0.5f
    };
    float maxSpan = std::max({ mesh.bboxSize.x, mesh.bboxSize.y, mesh.bboxSize.z, 1e-5f });
    float invScale = 2.0f / maxSpan;
    for (auto& v : mesh.vertices) {
        v.x = (v.x - center.x) * invScale;
        v.y = (v.y - center.y) * invScale;
        v.z = (v.z - center.z) * invScale;
    }

    // Recompute unit face normals in normalized space
    size_t vCount = mesh.vertices.size();
    for (auto& t : mesh.tris) {
        if (t.i0 < vCount && t.i1 < vCount && t.i2 < vCount) {
            Vec3f e1 = VecSub(mesh.vertices[t.i1], mesh.vertices[t.i0]);
            Vec3f e2 = VecSub(mesh.vertices[t.i2], mesh.vertices[t.i0]);
            t.normal = VecNormalize(VecCross(e1, e2));
        }
    }
    mesh.loadedOk = true;
}

LoadedMesh3D Model3DViewer::ParseMeshFromDisk(
    const std::wstring& fullPath,
    uint64_t generation,
    const std::atomic<uint64_t>* activeGen)
{
    LoadedMesh3D mesh;
    mesh.filePath = fullPath;
    std::wstring ext;
    {
        const wchar_t* dot = PathFindExtensionW(fullPath.c_str());
        if (dot && *dot) ext = PathUtils::ToLower(dot);
    }

    HANDLE hFile = CreateFileW(fullPath.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        mesh.statusMessage = L"Nie można otworzyć pliku 3D";
        return mesh;
    }

    LARGE_INTEGER liSize = {};
    GetFileSizeEx(hFile, &liSize);
    uint64_t fileSize = static_cast<uint64_t>(std::max<LONGLONG>(0, liSize.QuadPart));
    size_t maxRead = static_cast<size_t>(std::min<uint64_t>(fileSize, 64ULL * 1024ULL * 1024ULL)); // Cap at 64 MB
    std::vector<uint8_t> buf(maxRead);
    DWORD bytesRead = 0;
    if (maxRead > 0) {
        ReadFile(hFile, buf.data(), static_cast<DWORD>(maxRead), &bytesRead, nullptr);
        buf.resize(bytesRead);
    }
    CloseHandle(hFile);

    if (buf.empty()) {
        mesh.statusMessage = L"Pusty plik (0 B)";
        return mesh;
    }

    // 1. STL Parser (Binary STL vs ASCII STL)
    if (ext == L".stl") {
        bool isBinaryStl = false;
        if (buf.size() >= 84) {
            uint32_t triCount = 0;
            std::memcpy(&triCount, buf.data() + 80, 4);
            uint64_t expectedBytes = 84ULL + static_cast<uint64_t>(triCount) * 50ULL;
            if (triCount > 0 && (expectedBytes == fileSize || (buf.size() >= expectedBytes && std::memcmp(buf.data(), "solid", 5) != 0))) {
                isBinaryStl = true;
            }
        }

        if (isBinaryStl) {
            mesh.formatName = L"STL (Binary)";
            uint32_t triCount = 0;
            std::memcpy(&triCount, buf.data() + 80, 4);
            size_t availTris = std::min<size_t>(triCount, (buf.size() - 84) / 50);
            size_t stride = (availTris > 60000) ? (availTris / 60000) : 1;
            mesh.vertices.reserve((availTris / stride) * 3);
            mesh.tris.reserve(availTris / stride);

            for (size_t t = 0; t < availTris; t += stride) {
                if (activeGen && activeGen->load() != generation) return mesh;
                const uint8_t* rec = buf.data() + 84 + t * 50;
                float v[12] = {};
                std::memcpy(v, rec, 48);
                // Convert Z-up CAD convention to Y-up studio convention
                uint32_t baseIdx = static_cast<uint32_t>(mesh.vertices.size());
                mesh.vertices.push_back({ v[3],  v[5], -v[4] });
                mesh.vertices.push_back({ v[6],  v[8], -v[7] });
                mesh.vertices.push_back({ v[9], v[11], -v[10] });
                mesh.tris.push_back({ baseIdx, baseIdx + 1, baseIdx + 2, {} });
            }
            FinalizeMeshBoundsAndNormals(mesh);
            mesh.rawTriangleCount = availTris;
            mesh.rawVertexCount = availTris * 3;
            return mesh;
        } else {
            mesh.formatName = L"STL (ASCII)";
            const char* p = reinterpret_cast<const char*>(buf.data());
            const char* end = p + buf.size();
            while (p < end) {
                while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
                if (p + 6 < end && std::memcmp(p, "vertex", 6) == 0 && (p[6] == ' ' || p[6] == '\t')) {
                    p += 6;
                    char* next = nullptr;
                    float vx = std::strtof(p, &next); p = next;
                    float vy = std::strtof(p, &next); p = next;
                    float vz = std::strtof(p, &next); p = next;
                    mesh.vertices.push_back({ vx, vz, -vy });
                    if (mesh.vertices.size() % 3 == 0) {
                        uint32_t b = static_cast<uint32_t>(mesh.vertices.size() - 3);
                        mesh.tris.push_back({ b, b + 1, b + 2, {} });
                    }
                }
                while (p < end && *p != '\n') ++p;
            }
            FinalizeMeshBoundsAndNormals(mesh);
            return mesh;
        }
    }

    // 2. Wavefront OBJ Parser (.obj)
    if (ext == L".obj") {
        mesh.formatName = L"Wavefront OBJ";
        const char* p = reinterpret_cast<const char*>(buf.data());
        const char* end = p + buf.size();
        mesh.vertices.reserve(8192);
        mesh.tris.reserve(16384);

        std::vector<uint32_t> facePoly;
        facePoly.reserve(16);

        while (p < end) {
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\r')) ++p;
            if (p >= end) break;
            if (*p == '\n') { ++p; continue; }

            const char* lineEnd = p;
            while (lineEnd < end && *lineEnd != '\n') ++lineEnd;

            if (p[0] == 'v' && (p[1] == ' ' || p[1] == '\t')) {
                const char* cur = p + 2;
                char* next = nullptr;
                float vx = std::strtof(cur, &next); cur = next;
                float vy = std::strtof(cur, &next); cur = next;
                float vz = std::strtof(cur, &next);
                mesh.vertices.push_back({ vx, vy, vz });
            } else if (p[0] == 'f' && (p[1] == ' ' || p[1] == '\t')) {
                facePoly.clear();
                const char* cur = p + 2;
                while (cur < lineEnd) {
                    while (cur < lineEnd && (*cur == ' ' || *cur == '\t' || *cur == '\r')) ++cur;
                    if (cur >= lineEnd || *cur == '#') break;
                    char* next = nullptr;
                    long idx = std::strtol(cur, &next, 10);
                    if (next == cur) break;
                    cur = next;
                    while (cur < lineEnd && *cur != ' ' && *cur != '\t' && *cur != '\r') ++cur;

                    int vIdx = -1;
                    if (idx > 0) {
                        vIdx = static_cast<int>(idx) - 1;
                    } else if (idx < 0) {
                        vIdx = static_cast<int>(mesh.vertices.size()) + static_cast<int>(idx);
                    }
                    if (vIdx >= 0 && static_cast<size_t>(vIdx) < mesh.vertices.size()) {
                        facePoly.push_back(static_cast<uint32_t>(vIdx));
                    }
                }
                for (size_t i = 1; i + 1 < facePoly.size(); ++i) {
                    mesh.tris.push_back({ facePoly[0], facePoly[i], facePoly[i + 1], {} });
                }
            }
            p = (lineEnd < end) ? (lineEnd + 1) : end;
        }
        FinalizeMeshBoundsAndNormals(mesh);
        return mesh;
    }

    // 3. Stanford PLY Parser (.ply - ASCII & binary_little_endian)
    if (ext == L".ply") {
        mesh.formatName = L"Stanford PLY";
        const char* p = reinterpret_cast<const char*>(buf.data());
        const char* end = p + buf.size();
        bool isBinaryLE = false;
        size_t numVerts = 0;
        size_t numFaces = 0;
        size_t vertexFloatsOrProps = 0;
        bool inVertexElement = false;

        while (p < end) {
            const char* lineEnd = p;
            while (lineEnd < end && *lineEnd != '\n') ++lineEnd;
            std::string line(p, lineEnd);
            if (!line.empty() && line.back() == '\r') line.pop_back();

            if (line.find("format binary_little_endian") != std::string::npos) {
                isBinaryLE = true;
            } else if (line.rfind("element vertex ", 0) == 0) {
                numVerts = static_cast<size_t>(std::strtoull(line.c_str() + 15, nullptr, 10));
                inVertexElement = true;
            } else if (line.rfind("element face ", 0) == 0) {
                numFaces = static_cast<size_t>(std::strtoull(line.c_str() + 13, nullptr, 10));
                inVertexElement = false;
            } else if (line.rfind("element ", 0) == 0) {
                inVertexElement = false;
            } else if (inVertexElement && line.rfind("property ", 0) == 0) {
                ++vertexFloatsOrProps;
            } else if (line == "end_header") {
                p = (lineEnd < end) ? (lineEnd + 1) : end;
                break;
            }
            p = (lineEnd < end) ? (lineEnd + 1) : end;
        }

        if (!isBinaryLE && numVerts > 0) {
            mesh.vertices.reserve(numVerts);
            for (size_t i = 0; i < numVerts && p < end; ++i) {
                const char* lineEnd = p;
                while (lineEnd < end && *lineEnd != '\n') ++lineEnd;
                char* next = nullptr;
                float vx = std::strtof(p, &next);
                float vy = std::strtof(next, &next);
                float vz = std::strtof(next, &next);
                mesh.vertices.push_back({ vx, vy, vz });
                p = (lineEnd < end) ? (lineEnd + 1) : end;
            }
            for (size_t f = 0; f < numFaces && p < end; ++f) {
                const char* lineEnd = p;
                while (lineEnd < end && *lineEnd != '\n') ++lineEnd;
                char* next = nullptr;
                long n = std::strtol(p, &next, 10);
                if (n >= 3 && n <= 16) {
                    uint32_t idx[16] = {};
                    for (long k = 0; k < n; ++k) {
                        idx[k] = static_cast<uint32_t>(std::strtoul(next, &next, 10));
                    }
                    for (long k = 1; k + 1 < n; ++k) {
                        if (idx[0] < mesh.vertices.size() && idx[k] < mesh.vertices.size() && idx[k + 1] < mesh.vertices.size()) {
                            mesh.tris.push_back({ idx[0], idx[k], idx[k + 1], {} });
                        }
                    }
                }
                p = (lineEnd < end) ? (lineEnd + 1) : end;
            }
            FinalizeMeshBoundsAndNormals(mesh);
            return mesh;
        } else if (isBinaryLE && numVerts > 0) {
            mesh.formatName = L"Stanford PLY (Binary LE)";
            size_t strideBytes = std::max<size_t>(12, vertexFloatsOrProps * 4);
            const uint8_t* bp = reinterpret_cast<const uint8_t*>(p);
            const uint8_t* bend = buf.data() + buf.size();
            mesh.vertices.reserve(numVerts);
            for (size_t i = 0; i < numVerts && bp + 12 <= bend; ++i) {
                float v[3] = {};
                std::memcpy(v, bp, 12);
                mesh.vertices.push_back({ v[0], v[1], v[2] });
                bp += strideBytes;
            }
            for (size_t f = 0; f < numFaces && bp < bend; ++f) {
                uint8_t n = *bp++;
                if (n >= 3 && bp + static_cast<size_t>(n) * 4 <= bend) {
                    uint32_t idx[16] = {};
                    uint8_t cap = std::min<uint8_t>(n, 16);
                    std::memcpy(idx, bp, cap * 4);
                    for (uint8_t k = 1; k + 1 < cap; ++k) {
                        if (idx[0] < mesh.vertices.size() && idx[k] < mesh.vertices.size() && idx[k + 1] < mesh.vertices.size()) {
                            mesh.tris.push_back({ idx[0], idx[k], idx[k + 1], {} });
                        }
                    }
                    bp += static_cast<size_t>(n) * 4;
                } else {
                    break;
                }
            }
            FinalizeMeshBoundsAndNormals(mesh);
            return mesh;
        }
    }

    // 4. OFF (Object File Format) Parser (.off)
    if (ext == L".off") {
        mesh.formatName = L"OFF 3D Mesh";
        const char* p = reinterpret_cast<const char*>(buf.data());
        const char* end = p + buf.size();
        if (p + 3 < end && std::memcmp(p, "OFF", 3) == 0) {
            while (p < end && *p != '\n') ++p;
            while (p < end && (*p == '\n' || *p == '\r' || *p == ' ' || *p == '\t' || *p == '#')) {
                if (*p == '#') { while (p < end && *p != '\n') ++p; }
                else ++p;
            }
            char* next = nullptr;
            size_t nv = static_cast<size_t>(std::strtoull(p, &next, 10));
            size_t nf = static_cast<size_t>(std::strtoull(next, &next, 10));
            while (next < end && *next != '\n') ++next;
            p = (next < end) ? (next + 1) : end;

            for (size_t i = 0; i < nv && p < end; ++i) {
                float vx = std::strtof(p, &next);
                float vy = std::strtof(next, &next);
                float vz = std::strtof(next, &next);
                mesh.vertices.push_back({ vx, vy, vz });
                while (next < end && *next != '\n') ++next;
                p = (next < end) ? (next + 1) : end;
            }
            for (size_t f = 0; f < nf && p < end; ++f) {
                long n = std::strtol(p, &next, 10);
                if (n >= 3 && n <= 16) {
                    uint32_t idx[16] = {};
                    for (long k = 0; k < n; ++k) idx[k] = static_cast<uint32_t>(std::strtoul(next, &next, 10));
                    for (long k = 1; k + 1 < n; ++k) {
                        if (idx[0] < mesh.vertices.size() && idx[k] < mesh.vertices.size() && idx[k + 1] < mesh.vertices.size()) {
                            mesh.tris.push_back({ idx[0], idx[k], idx[k + 1], {} });
                        }
                    }
                }
                while (next < end && *next != '\n') ++next;
                p = (next < end) ? (next + 1) : end;
            }
            FinalizeMeshBoundsAndNormals(mesh);
            return mesh;
        }
    }

    // 5. Fallback for container 3D formats (.glb, .gltf, .fbx, .3mf, .blend) -> Build preview geodesic crystal mesh + metadata
    std::wstring extUp = (ext.size() > 1) ? ext.substr(1) : ext;
    for (wchar_t& ch : extUp) ch = static_cast<wchar_t>(towupper(ch));
    mesh.formatName = extUp + L" 3D Asset";
    mesh.statusMessage = L"Kontener 3D (" + mesh.formatName + L") — podgląd orientacji 3D";
    const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
    mesh.vertices = {
        {-1,  t,  0}, { 1,  t,  0}, {-1, -t,  0}, { 1, -t,  0},
        { 0, -1,  t}, { 0,  1,  t}, { 0, -1, -t}, { 0,  1, -t},
        { t,  0, -1}, { t,  0,  1}, {-t,  0, -1}, {-t,  0,  1}
    };
    mesh.tris = {
        {0,11,5,{}}, {0,5,1,{}}, {0,1,7,{}}, {0,7,10,{}}, {0,10,11,{}},
        {1,5,9,{}}, {5,11,4,{}}, {11,10,2,{}}, {10,7,6,{}}, {7,1,8,{}},
        {3,9,4,{}}, {3,4,2,{}}, {3,2,6,{}}, {3,6,8,{}}, {3,8,9,{}},
        {4,9,5,{}}, {2,4,11,{}}, {6,2,10,{}}, {8,6,7,{}}, {9,8,1,{}}
    };
    FinalizeMeshBoundsAndNormals(mesh);
    return mesh;
}

void Model3DViewer::Draw(
    ID2D1RenderTarget* rt,
    ID2D1SolidColorBrush* brush,
    IDWriteTextFormat* fmtRegular,
    IDWriteTextFormat* fmtSmall,
    IDWriteTextFormat* fmtBold,
    const D2D1_RECT_F& rect,
    Lang lang,
    D2D1_POINT_2F /*mousePt*/)
{
    const bool pl = (lang == Lang::PL);
    m_toolbarHits.clear();

    float tbH = 30.0f;
    float statusH = 26.0f;
    D2D1_RECT_F tbR = D2D1::RectF(rect.left, rect.top, rect.right, rect.top + tbH);
    brush->SetColor(M3DColor(0xF8FAFC));
    rt->FillRectangle(tbR, brush);
    brush->SetColor(M3DColor(0xE2E8F0));
    rt->DrawLine(D2D1::Point2F(tbR.left, tbR.bottom - 0.5f), D2D1::Point2F(tbR.right, tbR.bottom - 0.5f), brush, 1.0f);

    float bx = tbR.left + 6.0f;
    auto drawBtn = [&](int id, float w, const wchar_t* label, bool active = false) {
        if (bx + w > tbR.right - 4.0f) return;
        D2D1_RECT_F br = D2D1::RectF(bx, tbR.top + 4.0f, bx + w, tbR.bottom - 4.0f);
        m_toolbarHits.push_back({ id, br });
        bool hov = (m_hoverBtnId == id);
        brush->SetColor(active ? M3DColor(0xDBEAFE) : (hov ? M3DColor(0xE2E8F0) : M3DColor(0xFFFFFF)));
        rt->FillRoundedRectangle(D2D1::RoundedRect(br, 3.0f, 3.0f), brush);
        brush->SetColor(active ? M3DColor(0x3B82F6) : M3DColor(0xCBD5E1));
        rt->DrawRoundedRectangle(D2D1::RoundedRect(br, 3.0f, 3.0f), brush, 1.0f);
        brush->SetColor(active ? M3DColor(0x1D4ED8) : M3DColor(0x1E293B));
        fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        rt->DrawTextW(label, static_cast<UINT32>(wcslen(label)), fmtSmall, br, brush);
        fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        bx += w + 4.0f;
    };

    const wchar_t* modeLbl = (m_renderMode == RenderMode3D::Shaded)
        ? (pl ? L"\xD83E\xDDCA Cieniowany" : L"\xD83E\xDDCA Shaded")
        : ((m_renderMode == RenderMode3D::ShadedWireframe)
            ? (pl ? L"\xD83E\xDDCA Bryła + Siatka" : L"\xD83E\xDDCA Solid + Wire")
            : (pl ? L"\xD83D\xDD78 Siatka (Wire)" : L"\xD83D\xDD78 Wireframe"));

    drawBtn(BTN_MODE_CYCLE, 106.0f, modeLbl, true);
    drawBtn(BTN_AUTOROTATE, 88.0f,  pl ? L"\xD83D\xDD04 Auto-obrót" : L"\xD83D\xDD04 Turntable", m_autoRotate);
    drawBtn(BTN_GRID,       68.0f,  pl ? L"\x25A6 Podłoga" : L"\x25A6 Grid", m_showGrid);
    drawBtn(BTN_RESET_CAM,  68.0f,  pl ? L"\xD83C\xDFAF Resetuj" : L"\xD83C\xDFAF Reset");

    // 2. 3D Studio Viewport Canvas
    m_viewportRect = D2D1::RectF(rect.left, tbR.bottom, rect.right, std::max(tbR.bottom + 40.0f, rect.bottom - statusH));
    brush->SetColor(M3DColor(0x161B22));
    rt->FillRectangle(m_viewportRect, brush);

    rt->PushAxisAlignedClip(m_viewportRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    float vpW = m_viewportRect.right - m_viewportRect.left;
    float vpH = m_viewportRect.bottom - m_viewportRect.top;
    float cx = (m_viewportRect.left + m_viewportRect.right) * 0.5f + m_panX;
    float cy = (m_viewportRect.top + m_viewportRect.bottom) * 0.5f + m_panY;
    float scale = std::min(vpW, vpH) * 0.42f * m_zoom;

    float cosY = std::cos(m_yaw), sinY = std::sin(m_yaw);
    float cosP = std::cos(m_pitch), sinP = std::sin(m_pitch);

    auto project3D = [&](const Vec3f& p, float& outX, float& outY, float& outZ) -> bool {
        // Rotate around Y (Yaw), then around X (Pitch)
        float rx =  p.x * cosY + p.z * sinY;
        float rz = -p.x * sinY + p.z * cosY;
        float ry =  p.y * cosP - rz * sinP;
        float fz =  p.y * sinP + rz * cosP;

        float camDist = 3.6f;
        float denom = camDist - fz;
        if (denom < 0.25f) return false;
        float persp = (camDist / denom) * scale;
        outX = cx + rx * persp;
        outY = cy - ry * persp;
        outZ = fz;
        return true;
    };

    // Draw 3D Ground Grid at Y = -1.0f
    if (m_showGrid) {
        brush->SetColor(M3DColor(0x30363D, 0.75f));
        for (int g = -5; g <= 5; ++g) {
            float coord = g * 0.3f;
            float x1, y1, z1, x2, y2, z2;
            if (project3D({ coord, -1.0f, -1.5f }, x1, y1, z1) &&
                project3D({ coord, -1.0f,  1.5f }, x2, y2, z2)) {
                rt->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), brush, (g == 0) ? 1.4f : 0.8f);
            }
            if (project3D({ -1.5f, -1.0f, coord }, x1, y1, z1) &&
                project3D({  1.5f, -1.0f, coord }, x2, y2, z2)) {
                rt->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), brush, (g == 0) ? 1.4f : 0.8f);
            }
        }
    }

    size_t rawV = 0, rawT = 0;
    Vec3f bboxSz = {};
    std::wstring fmtName;
    bool isLoading = m_isLoading.load();

    {
        std::lock_guard<std::mutex> lk(m_meshMutex);
        rawV = m_mesh.rawVertexCount;
        rawT = m_mesh.rawTriangleCount;
        bboxSz = m_mesh.bboxSize;
        fmtName = m_mesh.formatName;

        if (!isLoading && m_mesh.loadedOk && !m_mesh.tris.empty() && !m_mesh.vertices.empty()) {
            const auto& verts = m_mesh.vertices;
            const auto& tris = m_mesh.tris;

            // Adaptive triangle budget so even 100k+ triangle STLs orbit smoothly at 60 FPS
            size_t maxRenderTris = (m_draggingOrbit || m_draggingPan || m_autoRotate) ? 2800 : 5500;
            size_t stride = std::max<size_t>(1, tris.size() / maxRenderTris);

            struct ProjectedTri {
                D2D1_POINT_2F p0, p1, p2;
                float depth = 0.0f;
                uint32_t fillColor = 0x58A6FF;
            };
            std::vector<ProjectedTri> drawList;
            drawList.reserve(tris.size() / stride + 1);

            const Vec3f keyLight  = VecNormalize({  0.45f,  0.75f,  0.50f });
            const Vec3f fillLight = VecNormalize({ -0.60f, -0.25f,  0.40f });

            for (size_t ti = 0; ti < tris.size(); ti += stride) {
                const auto& t = tris[ti];
                if (t.i0 >= verts.size() || t.i1 >= verts.size() || t.i2 >= verts.size()) continue;

                float x0, y0, z0, x1, y1, z1, x2, y2, z2;
                if (!project3D(verts[t.i0], x0, y0, z0)) continue;
                if (!project3D(verts[t.i1], x1, y1, z1)) continue;
                if (!project3D(verts[t.i2], x2, y2, z2)) continue;

                // Rotate normal into camera space for studio lighting
                const Vec3f& n = t.normal;
                float rnx =  n.x * cosY + n.z * sinY;
                float rnz = -n.x * sinY + n.z * cosY;
                float rny =  n.y * cosP - rnz * sinP;
                float fnz =  n.y * sinP + rnz * cosP;
                Vec3f camN = { rnx, rny, fnz };
                if (camN.z < 0.0f) {
                    camN.x = -camN.x; camN.y = -camN.y; camN.z = -camN.z;
                }

                float ndotl1 = std::max(0.0f, VecDot(camN, keyLight));
                float ndotl2 = std::max(0.0f, VecDot(camN, fillLight));
                float rim    = std::pow(1.0f - std::clamp(camN.z, 0.0f, 1.0f), 2.2f) * 0.25f;
                float shade  = std::clamp(0.24f + ndotl1 * 0.62f + ndotl2 * 0.20f + rim, 0.15f, 1.0f);

                uint32_t r = static_cast<uint32_t>(std::clamp(70.0f  * shade + rim * 90.0f,  0.0f, 255.0f));
                uint32_t g = static_cast<uint32_t>(std::clamp(152.0f * shade + rim * 100.0f, 0.0f, 255.0f));
                uint32_t b = static_cast<uint32_t>(std::clamp(235.0f * shade + rim * 110.0f, 0.0f, 255.0f));

                drawList.push_back({
                    D2D1::Point2F(x0, y0),
                    D2D1::Point2F(x1, y1),
                    D2D1::Point2F(x2, y2),
                    (z0 + z1 + z2) * (1.0f / 3.0f),
                    (r << 16) | (g << 8) | b
                });
            }

            // Painter's algorithm sort (back-to-front)
            std::sort(drawList.begin(), drawList.end(), [](const ProjectedTri& a, const ProjectedTri& b) {
                return a.depth < b.depth;
            });

            bool drawFill = (m_renderMode != RenderMode3D::Wireframe) && (m_d2dFactory != nullptr);
            bool drawWire = (m_renderMode != RenderMode3D::Shaded) || (m_d2dFactory == nullptr);

            for (const auto& pt : drawList) {
                if (drawFill) {
                    ID2D1PathGeometry* path = nullptr;
                    if (SUCCEEDED(m_d2dFactory->CreatePathGeometry(&path)) && path) {
                        ID2D1GeometrySink* sink = nullptr;
                        if (SUCCEEDED(path->Open(&sink)) && sink) {
                            sink->BeginFigure(pt.p0, D2D1_FIGURE_BEGIN_FILLED);
                            sink->AddLine(pt.p1);
                            sink->AddLine(pt.p2);
                            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                            sink->Close();
                            sink->Release();
                            brush->SetColor(M3DColor(pt.fillColor, 0.96f));
                            rt->FillGeometry(path, brush);
                        }
                        path->Release();
                    }
                }
                if (drawWire) {
                    brush->SetColor((m_renderMode == RenderMode3D::Wireframe)
                        ? M3DColor(0x58A6FF, 0.78f)
                        : M3DColor(0x0D1117, 0.42f));
                    rt->DrawLine(pt.p0, pt.p1, brush, 0.75f);
                    rt->DrawLine(pt.p1, pt.p2, brush, 0.75f);
                    rt->DrawLine(pt.p2, pt.p0, brush, 0.75f);
                }
            }
        }
    }

    if (isLoading) {
        std::wstring loadTxt = pl ? L"\x23F3 Wczytywanie modelu 3D w tle..." : L"\x23F3 Loading 3D model in background...";
        brush->SetColor(M3DColor(0xE2E8F0));
        fmtBold->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        rt->DrawTextW(loadTxt.c_str(), static_cast<UINT32>(loadTxt.size()), fmtBold, m_viewportRect, brush);
        fmtBold->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }

    // 3. Top-Right XYZ Orientation Axis Gizmo + Controls Legend
    {
        D2D1_POINT_2F gc = D2D1::Point2F(m_viewportRect.right - 36.0f, m_viewportRect.top + 36.0f);
        float axisLen = 22.0f;
        auto drawAxis = [&](Vec3f dir, uint32_t colHex, const wchar_t* lbl) {
            float rx =  dir.x * cosY + dir.z * sinY;
            float rz = -dir.x * sinY + dir.z * cosY;
            float ry =  dir.y * cosP - rz * sinP;
            D2D1_POINT_2F tip = D2D1::Point2F(gc.x + rx * axisLen, gc.y - ry * axisLen);
            brush->SetColor(M3DColor(colHex));
            rt->DrawLine(gc, tip, brush, 2.0f);
            D2D1_RECT_F lr = D2D1::RectF(tip.x - 6.0f, tip.y - 6.0f, tip.x + 8.0f, tip.y + 8.0f);
            rt->DrawTextW(lbl, 1, fmtSmall, lr, brush);
        };
        drawAxis({ 1.0f, 0.0f, 0.0f }, 0xF85149, L"X");
        drawAxis({ 0.0f, 1.0f, 0.0f }, 0x3FB950, L"Y");
        drawAxis({ 0.0f, 0.0f, 1.0f }, 0x58A6FF, L"Z");

        const wchar_t* hint = pl
            ? L"LPM = Obrót 3D   \x2022   PPM = Przesuń   \x2022   Kółko = Zoom"
            : L"LMB = Orbit 3D   \x2022   RMB = Pan   \x2022   Wheel = Zoom";
        D2D1_RECT_F hintR = D2D1::RectF(m_viewportRect.left + 10.0f, m_viewportRect.bottom - 24.0f, m_viewportRect.right - 10.0f, m_viewportRect.bottom - 4.0f);
        brush->SetColor(M3DColor(0x8B949E));
        rt->DrawTextW(hint, static_cast<UINT32>(wcslen(hint)), fmtSmall, hintR, brush);
    }

    rt->PopAxisAlignedClip();

    // 4. Bottom Telemetry Bar (Vertices, Triangles, Bounding Box)
    D2D1_RECT_F sbR = D2D1::RectF(rect.left, rect.bottom - statusH, rect.right, rect.bottom);
    brush->SetColor(M3DColor(0xF1F5F9));
    rt->FillRectangle(sbR, brush);
    brush->SetColor(M3DColor(0xCBD5E1));
    rt->DrawLine(D2D1::Point2F(sbR.left, sbR.top), D2D1::Point2F(sbR.right, sbR.top), brush, 1.0f);

    wchar_t statBuf[256] = {};
    swprintf_s(statBuf,
        pl ? L"%s   |   Wierzchołki: %zu   |   Trójkąty: %zu   |   Wymiary: %.2f \xD7 %.2f \xD7 %.2f"
           : L"%s   |   Vertices: %zu   |   Triangles: %zu   |   BBox: %.2f \xD7 %.2f \xD7 %.2f",
        fmtName.empty() ? L"3D" : fmtName.c_str(),
        rawV, rawT, bboxSz.x, bboxSz.y, bboxSz.z);

    D2D1_RECT_F stR = D2D1::RectF(sbR.left + 8.0f, sbR.top, sbR.right - 8.0f, sbR.bottom);
    brush->SetColor(M3DColor(0x334155));
    rt->DrawTextW(statBuf, static_cast<UINT32>(wcslen(statBuf)), fmtSmall, stR, brush);
}

bool Model3DViewer::OnMouseDown(float x, float y, bool rightButton, Lang /*lang*/, std::wstring* /*outStatusToast*/) {
    if (!rightButton) {
        for (const auto& hit : m_toolbarHits) {
            if (PathUtils::PtInRectF(x, y, hit.rect)) {
                switch (hit.id) {
                case BTN_MODE_CYCLE:
                    m_renderMode = static_cast<RenderMode3D>((static_cast<int>(m_renderMode) + 1) % 3);
                    break;
                case BTN_AUTOROTATE:
                    m_autoRotate = !m_autoRotate;
                    m_lastAnimTick = GetTickCount64();
                    break;
                case BTN_GRID:
                    m_showGrid = !m_showGrid;
                    break;
                case BTN_RESET_CAM:
                    ResetCamera();
                    break;
                }
                return true;
            }
        }
    }

    if (PathUtils::PtInRectF(x, y, m_viewportRect)) {
        m_dragLastPt = D2D1::Point2F(x, y);
        if (rightButton || (GetKeyState(VK_SHIFT) & 0x8000)) {
            m_draggingPan = true;
        } else {
            m_draggingOrbit = true;
        }
        return true;
    }
    return false;
}

bool Model3DViewer::OnMouseMove(float x, float y, WPARAM /*keyFlags*/) {
    if (m_draggingOrbit) {
        float dx = x - m_dragLastPt.x;
        float dy = y - m_dragLastPt.y;
        m_dragLastPt = D2D1::Point2F(x, y);
        m_yaw   += dx * 0.011f;
        m_pitch  = std::clamp(m_pitch + dy * 0.011f, -1.52f, 1.52f);
        return true;
    }
    if (m_draggingPan) {
        float dx = x - m_dragLastPt.x;
        float dy = y - m_dragLastPt.y;
        m_dragLastPt = D2D1::Point2F(x, y);
        m_panX += dx;
        m_panY += dy;
        return true;
    }

    int newHov = 0;
    for (const auto& hit : m_toolbarHits) {
        if (PathUtils::PtInRectF(x, y, hit.rect)) {
            newHov = hit.id;
            break;
        }
    }
    if (newHov != m_hoverBtnId) {
        m_hoverBtnId = newHov;
        return true;
    }
    return false;
}

bool Model3DViewer::OnMouseUp() {
    if (m_draggingOrbit || m_draggingPan) {
        m_draggingOrbit = false;
        m_draggingPan = false;
        return true;
    }
    return false;
}

bool Model3DViewer::OnMouseWheel(short delta, float x, float y) {
    if (!PathUtils::PtInRectF(x, y, m_viewportRect)) return false;
    float factor = (delta > 0) ? 1.14f : (1.0f / 1.14f);
    m_zoom = std::clamp(m_zoom * factor, 0.20f, 8.0f);
    return true;
}

} // namespace Explorer2
