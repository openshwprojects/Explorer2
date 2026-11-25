#pragma once
#include "../common/MeshBuild3D.h"
// Off.h — Geomview Object File Format (.off — OFF, COFF, NOFF, 4OFF, STOFF) reader.

namespace Formats {

inline bool ParseOffFile(const std::wstring& ext, const std::vector<uint8_t>& buf, uint64_t fileSize, LoadedMesh3D& mesh,
                 uint64_t generation, const std::atomic<uint64_t>* activeGen) {
    using MeshBuild3D::FinalizeMeshBoundsAndNormals;
    (void)fileSize; (void)generation; (void)activeGen;
    if (ext != L".off" || buf.size() < 8) return false;

    const size_t nulScan = std::min<size_t>(buf.size(), 4096u);
    for (size_t i = 0; i < nulScan; ++i) {
        if (buf[i] == 0) return false;
    }

    const char* p = reinterpret_cast<const char*>(buf.data());
    const char* end = p + buf.size();
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;

    // Parse optional Geomview OFF header prefix flags: [ST][C][N][4][n]OFF
    const char* kwStart = p;
    bool is4Off = false;
    while (p < end && ((*p >= 'A' && *p <= 'Z') || *p == '4')) {
        if (*p == '4') is4Off = true;
        ++p;
    }
    const size_t kwLen = static_cast<size_t>(p - kwStart);
    if (kwLen < 3 || kwLen > 8 || std::memcmp(p - 3, "OFF", 3) != 0) return false;
    for (size_t i = 0; i + 3 < kwLen; ++i) {
        char c = kwStart[i];
        if (c != 'S' && c != 'T' && c != 'C' && c != 'N' && c != '4') return false;
    }
    if (p < end && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && *p != '#') return false;

    auto skipWsAndComments = [&]() {
        while (p < end) {
            if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') { ++p; continue; }
            if (*p == '#') { while (p < end && *p != '\n') ++p; continue; }
            break;
        }
    };

    // Counts `<numVerts> <numFaces> <numEdges>` may sit on the same header line (e.g. `OFF 8 6 0`) or next line
    skipWsAndComments();
    if (p >= end) return false;

    char* next = nullptr;
    uint64_t nv64 = std::strtoull(p, &next, 10);
    if (next == p) return false;
    p = next;
    skipWsAndComments();
    uint64_t nf64 = std::strtoull(p, &next, 10);
    if (next == p) return false;
    p = next;
    // Skip numEdges + rest of counts line
    while (p < end && *p != '\n') ++p;
    if (p < end) ++p;

    if (nv64 < 3 || nv64 > 5000000ULL || nf64 == 0 || nf64 > 10000000ULL) return false;
    const size_t nv = static_cast<size_t>(nv64);
    const size_t nf = static_cast<size_t>(nf64);

    mesh.vertices.clear();
    mesh.tris.clear();
    mesh.formatName = L"OFF 3D Mesh";
    mesh.vertices.reserve(std::min<size_t>(nv, 65536u));
    mesh.tris.reserve(std::min<size_t>(nf, 65536u));

    for (size_t i = 0; i < nv; ++i) {
        skipWsAndComments();
        if (p >= end) { mesh.vertices.clear(); return false; }
        const char* lineEnd = p;
        while (lineEnd < end && *lineEnd != '\n') ++lineEnd;

        char* e1 = nullptr; float vx = std::strtof(p, &e1);
        char* e2 = nullptr; float vy = (e1 != p) ? std::strtof(e1, &e2) : 0.0f;
        char* e3 = nullptr; float vz = (e1 != p && e2 != e1) ? std::strtof(e2, &e3) : 0.0f;
        if (e1 == p || e2 == e1 || e3 == e2 || e3 > lineEnd ||
            !std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(vz))
        {
            mesh.vertices.clear();
            return false;
        }
        if (is4Off) {
            char* e4 = nullptr;
            float vw = std::strtof(e3, &e4);
            if (e4 == e3 || e4 > lineEnd || !std::isfinite(vw)) {
                mesh.vertices.clear();
                return false;
            }
            (void)vw;
        }
        mesh.vertices.push_back({ vx, vy, vz });
        p = (lineEnd < end) ? (lineEnd + 1) : end;
    }

    std::vector<uint32_t> faceIdx;
    faceIdx.reserve(16);
    for (size_t f = 0; f < nf; ++f) {
        skipWsAndComments();
        if (p >= end) { mesh.vertices.clear(); mesh.tris.clear(); return false; }
        const char* lineEnd = p;
        while (lineEnd < end && *lineEnd != '\n') ++lineEnd;

        char* en = nullptr;
        long n = std::strtol(p, &en, 10);
        if (en == p || en > lineEnd || n < 3 || n > 65536) {
            mesh.vertices.clear();
            mesh.tris.clear();
            return false;
        }
        faceIdx.clear();
        const char* cur = en;
        for (long k = 0; k < n; ++k) {
            char* ek = nullptr;
            unsigned long vi = std::strtoul(cur, &ek, 10);
            if (ek == cur || ek > lineEnd || vi >= mesh.vertices.size()) {
                mesh.vertices.clear();
                mesh.tris.clear();
                return false;
            }
            faceIdx.push_back(static_cast<uint32_t>(vi));
            cur = ek;
        }
        for (size_t k = 1; k + 1 < faceIdx.size(); ++k) {
            if (faceIdx[0] != faceIdx[k] && faceIdx[k] != faceIdx[k + 1] && faceIdx[0] != faceIdx[k + 1]) {
                mesh.tris.push_back({ faceIdx[0], faceIdx[k], faceIdx[k + 1], {} });
            }
        }
        p = (lineEnd < end) ? (lineEnd + 1) : end;
    }

    if (mesh.tris.empty()) {
        mesh.vertices.clear();
        return false;
    }
    FinalizeMeshBoundsAndNormals(mesh);
    return mesh.loadedOk;
}

} // namespace Formats
