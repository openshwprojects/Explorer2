#pragma once
// FemSolidSurfaceCommon.h — Shared 3D solid & shell finite-element exterior boundary extractor
// Used by Abaqus (.inp), ANSYS (.cdb), and MSC/NX Nastran (.nas) finite-element readers.
#include "MeshBuild3D.h"

namespace Formats::FemSolidSurface {

struct FaceKey {
    uint32_t v[4] = { 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu };
    uint8_t n = 0;

    bool operator==(const FaceKey& o) const noexcept {
        return n == o.n && v[0] == o.v[0] && v[1] == o.v[1] && v[2] == o.v[2] && v[3] == o.v[3];
    }
};

struct FaceKeyHash {
    size_t operator()(const FaceKey& k) const noexcept {
        uint64_t h = 1469598103934665603ULL;
        for (uint8_t i = 0; i < k.n; ++i) {
            h ^= static_cast<uint64_t>(k.v[i]);
            h *= 1099511628211ULL;
        }
        return static_cast<size_t>(h ^ (h >> 32));
    }
};

struct FaceEntry {
    uint32_t orig[4] = {};
    uint8_t n = 0;
    uint32_t solidCount = 0;
    bool isShell = false;
};

using FaceMap = std::unordered_map<FaceKey, FaceEntry, FaceKeyHash>;

inline void RecordFace(FaceMap& faces, const uint32_t* verts, uint8_t n, bool volumetric) {
    if (n != 3 && n != 4) return;
    FaceKey key;
    key.n = n;
    for (uint8_t i = 0; i < n; ++i) key.v[i] = verts[i];
    std::sort(key.v, key.v + n);
    for (uint8_t i = 1; i < n; ++i) {
        if (key.v[i] == key.v[i - 1]) return; // degenerate face
    }

    auto [it, inserted] = faces.try_emplace(key);
    if (inserted) {
        it->second.n = n;
        for (uint8_t i = 0; i < n; ++i) it->second.orig[i] = verts[i];
    }
    if (volumetric) {
        ++it->second.solidCount;
    } else {
        it->second.isShell = true;
        if (it->second.solidCount == 0) {
            for (uint8_t i = 0; i < n; ++i) it->second.orig[i] = verts[i];
        }
    }
}

inline void EmitBoundaryFacesDirect(const FaceMap& faces, LoadedMesh3D& outMesh, uint32_t vBase = 0u) {
    for (const auto& kv : faces) {
        const FaceEntry& f = kv.second;
        if (f.solidCount == 1u || (f.solidCount == 0u && f.isShell)) {
            if (f.n == 3) {
                outMesh.tris.push_back({ vBase + f.orig[0], vBase + f.orig[1], vBase + f.orig[2], {} });
            } else if (f.n == 4) {
                outMesh.tris.push_back({ vBase + f.orig[0], vBase + f.orig[1], vBase + f.orig[2], {} });
                outMesh.tris.push_back({ vBase + f.orig[0], vBase + f.orig[2], vBase + f.orig[3], {} });
            }
        }
    }
}

class FemSurfaceBuilder {
public:
    void ReserveElements(size_t elemEstimate) {
        faces_.reserve(elemEstimate * 3u + 16u);
    }

    void AddFaceRaw(const uint32_t* verts, uint8_t n, bool volumetric) {
        RecordFace(faces_, verts, n, volumetric);
    }

    void AddTriShell(uint32_t a, uint32_t b, uint32_t c) {
        const uint32_t f[3] = { a, b, c };
        AddFaceRaw(f, 3, false);
    }

    void AddQuadShell(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
        if (c == d || a == d) {
            AddTriShell(a, b, c);
            return;
        }
        const uint32_t f[4] = { a, b, c, d };
        AddFaceRaw(f, 4, false);
    }

    void AddTet4(const uint32_t n[4]) {
        const uint32_t f0[3] = { n[0], n[2], n[1] };
        const uint32_t f1[3] = { n[0], n[1], n[3] };
        const uint32_t f2[3] = { n[1], n[2], n[3] };
        const uint32_t f3[3] = { n[2], n[0], n[3] };
        AddFaceRaw(f0, 3, true);
        AddFaceRaw(f1, 3, true);
        AddFaceRaw(f2, 3, true);
        AddFaceRaw(f3, 3, true);
    }

    void AddPyramid5(const uint32_t n[5]) {
        const uint32_t q0[4] = { n[0], n[3], n[2], n[1] };
        const uint32_t t0[3] = { n[0], n[1], n[4] };
        const uint32_t t1[3] = { n[1], n[2], n[4] };
        const uint32_t t2[3] = { n[2], n[3], n[4] };
        const uint32_t t3[3] = { n[3], n[0], n[4] };
        AddFaceRaw(q0, 4, true);
        AddFaceRaw(t0, 3, true);
        AddFaceRaw(t1, 3, true);
        AddFaceRaw(t2, 3, true);
        AddFaceRaw(t3, 3, true);
    }

    void AddWedge6(const uint32_t n[6]) {
        const uint32_t t0[3] = { n[0], n[2], n[1] };
        const uint32_t t1[3] = { n[3], n[4], n[5] };
        const uint32_t q0[4] = { n[0], n[1], n[4], n[3] };
        const uint32_t q1[4] = { n[1], n[2], n[5], n[4] };
        const uint32_t q2[4] = { n[2], n[0], n[3], n[5] };
        AddFaceRaw(t0, 3, true);
        AddFaceRaw(t1, 3, true);
        AddFaceRaw(q0, 4, true);
        AddFaceRaw(q1, 4, true);
        AddFaceRaw(q2, 4, true);
    }

    void AddHex8(const uint32_t n[8]) {
        const uint32_t q0[4] = { n[0], n[3], n[2], n[1] };
        const uint32_t q1[4] = { n[4], n[5], n[6], n[7] };
        const uint32_t q2[4] = { n[0], n[1], n[5], n[4] };
        const uint32_t q3[4] = { n[1], n[2], n[6], n[5] };
        const uint32_t q4[4] = { n[2], n[3], n[7], n[6] };
        const uint32_t q5[4] = { n[3], n[0], n[4], n[7] };
        AddFaceRaw(q0, 4, true);
        AddFaceRaw(q1, 4, true);
        AddFaceRaw(q2, 4, true);
        AddFaceRaw(q3, 4, true);
        AddFaceRaw(q4, 4, true);
        AddFaceRaw(q5, 4, true);
    }

    // Handles ANSYS SOLID185 / SOLID45 / SOLID186 degenerate 8-node brick conventions
    void AddCollapsedHex8(const uint32_t n[8]) {
        if (n[2] == n[3] && n[4] == n[5] && n[5] == n[6] && n[6] == n[7]) {
            const uint32_t tet[4] = { n[0], n[1], n[2], n[4] };
            AddTet4(tet);
        } else if (n[4] == n[5] && n[5] == n[6] && n[6] == n[7]) {
            const uint32_t pyr[5] = { n[0], n[1], n[2], n[3], n[4] };
            AddPyramid5(pyr);
        } else if (n[2] == n[3] && n[6] == n[7]) {
            const uint32_t wdg[6] = { n[0], n[1], n[2], n[4], n[5], n[6] };
            AddWedge6(wdg);
        } else {
            AddHex8(n);
        }
    }

    void AddBeamEdge(uint32_t a, uint32_t b) {
        if (a != b) beams_.emplace_back(a, b);
    }

    bool Empty() const noexcept { return faces_.empty() && beams_.empty(); }

    bool EmitBoundaryToMesh(const std::vector<Vec3f>& nodes, LoadedMesh3D& outMesh) const {
        if (nodes.empty()) return false;
        std::vector<uint32_t> remap(nodes.size(), 0xFFFFFFFFu);
        auto mapNode = [&](uint32_t idx) -> uint32_t {
            if (remap[idx] == 0xFFFFFFFFu) {
                remap[idx] = static_cast<uint32_t>(outMesh.vertices.size());
                outMesh.vertices.push_back(nodes[idx]);
            }
            return remap[idx];
        };

        const size_t trisBefore = outMesh.tris.size();
        for (const auto& kv : faces_) {
            const FaceEntry& f = kv.second;
            if (f.solidCount == 1u || (f.solidCount == 0u && f.isShell)) {
                bool valid = true;
                for (uint8_t i = 0; i < f.n; ++i) {
                    if (f.orig[i] >= nodes.size()) { valid = false; break; }
                }
                if (!valid) continue;
                if (f.n == 3) {
                    uint32_t i0 = mapNode(f.orig[0]);
                    uint32_t i1 = mapNode(f.orig[1]);
                    uint32_t i2 = mapNode(f.orig[2]);
                    outMesh.tris.push_back({ i0, i1, i2, {} });
                } else if (f.n == 4) {
                    uint32_t i0 = mapNode(f.orig[0]);
                    uint32_t i1 = mapNode(f.orig[1]);
                    uint32_t i2 = mapNode(f.orig[2]);
                    uint32_t i3 = mapNode(f.orig[3]);
                    outMesh.tris.push_back({ i0, i1, i2, {} });
                    outMesh.tris.push_back({ i0, i2, i3, {} });
                }
            }
        }

        if (outMesh.tris.size() == trisBefore && !beams_.empty()) {
            Vec3f lo = nodes[0], hi = nodes[0];
            for (const auto& p : nodes) {
                lo = { std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z) };
                hi = { std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z) };
            }
            float span = std::max({ hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, 1e-3f });
            float rad = span * 0.008f;
            for (const auto& bm : beams_) {
                if (bm.first < nodes.size() && bm.second < nodes.size()) {
                    MeshBuild3D::EmitEdgeStrut(outMesh, nodes[bm.first], nodes[bm.second], rad);
                }
            }
        }
        return outMesh.tris.size() > trisBefore;
    }

private:
    std::unordered_map<FaceKey, FaceEntry, FaceKeyHash> faces_;
    std::vector<std::pair<uint32_t, uint32_t>> beams_;
};

} // namespace Formats::FemSolidSurface
