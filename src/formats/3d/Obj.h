#pragma once
#include "../common/MeshBuild3D.h"
#include "XPlaneObj8Mesh.h"
// Obj.h — Wavefront OBJ (.obj) reader (delegates X-Plane OBJ8 `.obj` headers to XPlaneObj8Mesh.h).

namespace Formats {

inline bool ParseObjFile(const std::wstring& ext, const std::vector<uint8_t>& buf, uint64_t fileSize, LoadedMesh3D& mesh,
                 uint64_t generation, const std::atomic<uint64_t>* activeGen) {
    using MeshBuild3D::FinalizeMeshBoundsAndNormals;
    (void)fileSize; (void)generation; (void)activeGen;
    if (ext != L".obj" || buf.empty()) return false;

    // 1. Delegate Laminar Research X-Plane 8+ `.obj` (`I\n800\nOBJ` / `A\n800\nOBJ`) to XPlaneObj8Mesh
    if (DetailXPlaneObj8::IsXPlaneObj8Header(buf.data(), buf.size())) {
        if (!ParseXPlaneObj8Mesh(buf.data(), buf.size(), mesh)) return false;
        FinalizeMeshBoundsAndNormals(mesh);
        return mesh.loadedOk;
    }

    // 2. Reject binary NUL garbage
    const size_t nulScan = std::min<size_t>(buf.size(), 4096u);
    for (size_t i = 0; i < nulScan; ++i) {
        if (buf[i] == 0) return false;
    }

    mesh.vertices.clear();
    mesh.tris.clear();
    mesh.formatName = L"Wavefront OBJ";

    const char* p = reinterpret_cast<const char*>(buf.data());
    const char* end = p + buf.size();
    mesh.vertices.reserve(8192);
    mesh.tris.reserve(16384);

    std::vector<uint32_t> facePoly;
    facePoly.reserve(16);
    std::vector<std::pair<uint32_t, uint32_t>> lineSegs;

    while (p < end) {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r')) ++p;
        if (p >= end) break;
        if (*p == '\n') { ++p; continue; }

        const char* lineEnd = p;
        while (lineEnd < end && *lineEnd != '\n') ++lineEnd;

        if (p + 1 < lineEnd && p[0] == 'v' && (p[1] == ' ' || p[1] == '\t')) {
            const char* cur = p + 2;
            char* e1 = nullptr; float vx = std::strtof(cur, &e1);
            char* e2 = nullptr; float vy = (e1 != cur) ? std::strtof(e1, &e2) : 0.0f;
            char* e3 = nullptr; float vz = (e1 != cur && e2 != e1) ? std::strtof(e2, &e3) : 0.0f;
            if (e1 == cur || e2 == e1 || e3 == e2 || e3 > lineEnd ||
                !std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(vz))
            {
                mesh.vertices.clear();
                mesh.tris.clear();
                return false;
            }
            mesh.vertices.push_back({ vx, vy, vz });
        } else if (p + 1 < lineEnd && (p[0] == 'f' || p[0] == 'l') && (p[1] == ' ' || p[1] == '\t')) {
            const bool isLine = (p[0] == 'l');
            facePoly.clear();
            const char* cur = p + 2;
            while (cur < lineEnd) {
                while (cur < lineEnd && (*cur == ' ' || *cur == '\t' || *cur == '\r')) ++cur;
                if (cur >= lineEnd || *cur == '#') break;
                char* next = nullptr;
                long idx = std::strtol(cur, &next, 10);
                if (next == cur || next > lineEnd) break;
                cur = next;
                while (cur < lineEnd && *cur != ' ' && *cur != '\t' && *cur != '\r') ++cur;

                int64_t vIdx = -1;
                if (idx > 0) {
                    vIdx = static_cast<int64_t>(idx) - 1;
                } else if (idx < 0) {
                    vIdx = static_cast<int64_t>(mesh.vertices.size()) + static_cast<int64_t>(idx);
                }
                if (vIdx >= 0 && static_cast<size_t>(vIdx) < mesh.vertices.size()) {
                    facePoly.push_back(static_cast<uint32_t>(vIdx));
                } else {
                    mesh.vertices.clear();
                    mesh.tris.clear();
                    return false;
                }
            }
            if (isLine) {
                for (size_t i = 0; i + 1 < facePoly.size() && lineSegs.size() < 200000; ++i) {
                    lineSegs.emplace_back(facePoly[i], facePoly[i + 1]);
                }
            } else {
                for (size_t i = 1; i + 1 < facePoly.size(); ++i) {
                    if (facePoly[0] != facePoly[i] && facePoly[i] != facePoly[i + 1] && facePoly[0] != facePoly[i + 1]) {
                        mesh.tris.push_back({ facePoly[0], facePoly[i], facePoly[i + 1], {} });
                    }
                }
            }
        }
        p = (lineEnd < end) ? (lineEnd + 1) : end;
    }

    if (!lineSegs.empty() && !mesh.vertices.empty()) {
        Vec3f lo = mesh.vertices[0], hi = lo;
        for (const auto& v : mesh.vertices) {
            lo = { std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z) };
            hi = { std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z) };
        }
        float rad = std::max({ hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, 1e-3f }) * 0.004f;
        const size_t baseCount = mesh.vertices.size();
        for (const auto& seg : lineSegs) {
            if (seg.first >= baseCount || seg.second >= baseCount) continue;
            Vec3f a = mesh.vertices[seg.first], b = mesh.vertices[seg.second];
            MeshBuild3D::EmitEdgeStrut(mesh, a, b, rad);
        }
    }

    if (mesh.tris.empty()) {
        mesh.vertices.clear();
        return false;
    }
    FinalizeMeshBoundsAndNormals(mesh);
    return mesh.loadedOk;
}

} // namespace Formats
