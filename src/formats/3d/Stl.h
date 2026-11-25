#pragma once
#include "../common/MeshBuild3D.h"
// Stl.h — STL, binary and ASCII (.stl) reader for the 3D viewer.

namespace Formats {

inline bool ParseStlFile(const std::wstring& ext, const std::vector<uint8_t>& buf, uint64_t fileSize, LoadedMesh3D& mesh,
                 uint64_t generation, const std::atomic<uint64_t>* activeGen) {
    using MeshBuild3D::FinalizeMeshBoundsAndNormals;
    if (ext != L".stl" || buf.size() < 15) return false;

    bool isBinaryStl = false;
    if (buf.size() >= 84) {
        uint32_t triCount = 0;
        std::memcpy(&triCount, buf.data() + 80, 4);
        uint64_t expectedBytes = 84ULL + static_cast<uint64_t>(triCount) * 50ULL;
        if (triCount > 0 && (expectedBytes == fileSize || (buf.size() >= expectedBytes && std::memcmp(buf.data(), "solid", 5) != 0))) {
            isBinaryStl = true;
        }
    }

    mesh.vertices.clear();
    mesh.tris.clear();

    if (isBinaryStl) {
        mesh.formatName = L"STL (Binary)";
        uint32_t triCount = 0;
        std::memcpy(&triCount, buf.data() + 80, 4);
        constexpr size_t kMaxBinaryStlTris = 5000000u;
        size_t availTris = std::min<size_t>({ static_cast<size_t>(triCount), (buf.size() - 84) / 50, kMaxBinaryStlTris });
        if (availTris == 0) return false;

        mesh.vertices.reserve(availTris * 3);
        mesh.tris.reserve(availTris);

        for (size_t t = 0; t < availTris; ++t) {
            if ((t & 4095u) == 0u && activeGen && activeGen->load() != generation) return true;
            const uint8_t* rec = buf.data() + 84 + t * 50;
            float v[12] = {};
            std::memcpy(v, rec, 48);
            bool finiteTri = true;
            for (int k = 3; k < 12; ++k) {
                if (!std::isfinite(v[k])) { finiteTri = false; break; }
            }
            if (!finiteTri) continue;
            // Convert Z-up CAD convention to right-handed Y-up studio convention
            uint32_t baseIdx = static_cast<uint32_t>(mesh.vertices.size());
            mesh.vertices.push_back({ v[3],  v[5], -v[4] });
            mesh.vertices.push_back({ v[6],  v[8], -v[7] });
            mesh.vertices.push_back({ v[9], v[11], -v[10] });
            mesh.tris.push_back({ baseIdx, baseIdx + 1, baseIdx + 2, {} });
        }
        if (mesh.tris.empty()) {
            mesh.vertices.clear();
            return false;
        }
        FinalizeMeshBoundsAndNormals(mesh);
        return mesh.loadedOk;
    }

    // ASCII STL: reject binary NULs and require "solid" keyword
    const size_t nulScan = std::min<size_t>(buf.size(), 4096u);
    for (size_t i = 0; i < nulScan; ++i) {
        if (buf[i] == 0) return false;
    }

    const char* p = reinterpret_cast<const char*>(buf.data());
    const char* end = p + buf.size();
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
    if (end - p < 5 || std::memcmp(p, "solid", 5) != 0) return false;

    mesh.formatName = L"STL (ASCII)";
    Vec3f facetVerts[3] = {};
    int facetVertCount = 0;

    while (p < end) {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
        if (p >= end) break;

        if (end - p >= 6 && std::memcmp(p, "vertex", 6) == 0 && (p[6] == ' ' || p[6] == '\t')) {
            const char* cur = p + 6;
            char* e1 = nullptr; float vx = std::strtof(cur, &e1);
            if (e1 == cur || !std::isfinite(vx)) { mesh.vertices.clear(); mesh.tris.clear(); return false; }
            char* e2 = nullptr; float vy = std::strtof(e1, &e2);
            if (e2 == e1 || !std::isfinite(vy)) { mesh.vertices.clear(); mesh.tris.clear(); return false; }
            char* e3 = nullptr; float vz = std::strtof(e2, &e3);
            if (e3 == e2 || !std::isfinite(vz)) { mesh.vertices.clear(); mesh.tris.clear(); return false; }

            if (facetVertCount < 3) {
                facetVerts[facetVertCount++] = { vx, vz, -vy };
                if (facetVertCount == 3) {
                    uint32_t b = static_cast<uint32_t>(mesh.vertices.size());
                    mesh.vertices.push_back(facetVerts[0]);
                    mesh.vertices.push_back(facetVerts[1]);
                    mesh.vertices.push_back(facetVerts[2]);
                    mesh.tris.push_back({ b, b + 1, b + 2, {} });
                    facetVertCount = 0;
                }
            }
        } else if (end - p >= 8 && std::memcmp(p, "endfacet", 8) == 0) {
            facetVertCount = 0;
        }
        while (p < end && *p != '\n') ++p;
    }

    if (mesh.tris.empty()) {
        mesh.vertices.clear();
        return false;
    }
    FinalizeMeshBoundsAndNormals(mesh);
    return mesh.loadedOk;
}

} // namespace Formats
