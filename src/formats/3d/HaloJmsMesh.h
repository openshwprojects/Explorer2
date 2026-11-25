#pragma once
// HaloJmsMesh.h — Bungie Halo CE (8197–8200) & Halo 2 / 3 (8205–8210) Jointed Model Skeleton (.jms) Reader
// Parses node hierarchy (first-child/next-sibling in 8197–8200 and parent-index in 8205–8210) with
// quaternion (i, j, k, w) + translation rest-pose propagation, materials, markers, regions,
// weighted vertex blocks, and triangle index blocks.
#include "../../ReaderTextUtils.h"
#include "../common/MeshBuild3D.h"

namespace Formats {

namespace DetailHaloJms {

class JmsScanner {
public:
    explicit JmsScanner(std::string_view s) : sv_(ReaderText::StripUtf8Bom(s)) {}

    bool NextNonEmptyLine(std::string_view& out) {
        std::string_view ln;
        while (ReaderText::NextLineSv(sv_, pos_, ln)) {
            size_t semi = ln.find(';');
            if (semi != std::string_view::npos) ln = ln.substr(0, semi);
            ln = ReaderText::TrimSv(ln);
            if (!ln.empty()) {
                tokLine_ = {};
                out = ln;
                return true;
            }
        }
        return false;
    }

    bool NextToken(std::string_view& out) {
        while (true) {
            if (ReaderText::NextTokenSv(tokLine_, out)) return true;
            std::string_view ln;
            if (!ReaderText::NextLineSv(sv_, pos_, ln)) return false;
            size_t semi = ln.find(';');
            if (semi != std::string_view::npos) ln = ln.substr(0, semi);
            tokLine_ = ReaderText::TrimSv(ln);
        }
    }

    bool NextInt(int& out) {
        std::string_view t;
        return NextToken(t) && ReaderText::ParseIntSv(t, out);
    }

    bool NextFloat(float& out) {
        std::string_view t;
        return NextToken(t) && ReaderText::ParseFloatSv(t, out);
    }

    // Consume remainder of current token line before NextNonEmptyLine if tokens were read partially
    void FlushCurrentLine() { tokLine_ = {}; }

    size_t SavePos() const { return pos_; }
    std::string_view SaveTokLine() const { return tokLine_; }
    void Restore(size_t p, std::string_view tl) { pos_ = p; tokLine_ = tl; }

private:
    std::string_view sv_;
    size_t pos_ = 0;
    std::string_view tokLine_;
};

struct JmsNode {
    std::string name;
    int firstChild = -1;
    int nextSibling = -1;
    int parent = -1;
    float qx = 0, qy = 0, qz = 0, qw = 1;
    float tx = 0, ty = 0, tz = 0;
    Vec3f worldPos = { 0, 0, 0 };
};

inline void ComputeJmsNodeWorldPositions(std::vector<JmsNode>& nodes) {
    struct Mat3x4 {
        float m[3][4] = { {1,0,0,0}, {0,1,0,0}, {0,0,1,0} };
    };
    auto makeMat = [](const JmsNode& nd) -> Mat3x4 {
        Mat3x4 r{};
        float x = nd.qx, y = nd.qy, z = nd.qz, w = nd.qw;
        float n = std::sqrt(x * x + y * y + z * z + w * w);
        if (n > 1e-6f) { x /= n; y /= n; z /= n; w /= n; }
        else { x = y = z = 0.0f; w = 1.0f; }
        float xx = x * x, yy = y * y, zz = z * z;
        float xy = x * y, xz = x * z, yz = y * z;
        float wx = w * x, wy = w * y, wz = w * z;
        r.m[0][0] = 1.0f - 2.0f * (yy + zz); r.m[0][1] = 2.0f * (xy - wz); r.m[0][2] = 2.0f * (xz + wy); r.m[0][3] = nd.tx;
        r.m[1][0] = 2.0f * (xy + wz); r.m[1][1] = 1.0f - 2.0f * (xx + zz); r.m[1][2] = 2.0f * (yz - wx); r.m[1][3] = nd.ty;
        r.m[2][0] = 2.0f * (xz - wy); r.m[2][1] = 2.0f * (yz + wx); r.m[2][2] = 1.0f - 2.0f * (xx + yy); r.m[2][3] = nd.tz;
        return r;
    };
    auto mulMat = [](const Mat3x4& a, const Mat3x4& b) -> Mat3x4 {
        Mat3x4 r{};
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j)
                r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
            r.m[i][3] = a.m[i][0] * b.m[0][3] + a.m[i][1] * b.m[1][3] + a.m[i][2] * b.m[2][3] + a.m[i][3];
        }
        return r;
    };

    std::vector<Mat3x4> world(nodes.size());
    std::vector<uint8_t> done(nodes.size(), 0u);
    auto eval = [&](auto& self, size_t idx, int depth) -> const Mat3x4& {
        if (done[idx] || depth > 64) return world[idx];
        done[idx] = 1u;
        Mat3x4 loc = makeMat(nodes[idx]);
        int p = nodes[idx].parent;
        if (p >= 0 && static_cast<size_t>(p) < nodes.size() && static_cast<size_t>(p) != idx) {
            world[idx] = mulMat(self(self, static_cast<size_t>(p), depth + 1), loc);
        } else {
            world[idx] = loc;
        }
        nodes[idx].worldPos = { world[idx].m[0][3], world[idx].m[2][3], -world[idx].m[1][3] };
        return world[idx];
    };
    for (size_t i = 0; i < nodes.size(); ++i) eval(eval, i, 0);
}

} // namespace DetailHaloJms

inline bool ParseHaloJmsMesh(
    const uint8_t* data,
    size_t size,
    const std::wstring& /*fullPath*/,
    LoadedMesh3D& mesh)
{
    using namespace DetailHaloJms;
    if (!data || size < 16) return false;
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    if (sv.find('\0') != std::string_view::npos) return false;

    JmsScanner sc(sv);
    int version = 0;
    if (!sc.NextInt(version) || version < 8197 || version > 8213) return false;
    sc.FlushCurrentLine();

    mesh.vertices.clear();
    mesh.tris.clear();

    const bool isHalo2 = (version >= 8205);

    // Halo CE (8197–8204) has nodeListChecksum before nodeCount; Halo 2 (8205+) may omit or include checksum
    int nodeCount = 0;
    if (!isHalo2) {
        int checksum = 0;
        if (!sc.NextInt(checksum) || !sc.NextInt(nodeCount)) return false;
    } else {
        int firstInt = 0;
        if (!sc.NextInt(firstInt)) return false;
        sc.FlushCurrentLine();
        size_t sp = sc.SavePos();
        std::string_view stl = sc.SaveTokLine();
        std::string_view peekLine;
        if (!sc.NextNonEmptyLine(peekLine)) return false;
        int secondInt = 0;
        if (ReaderText::ParseIntSv(peekLine, secondInt) && secondInt >= 0 && secondInt <= 4096 &&
            (firstInt < 0 || firstInt > 4096 || secondInt > 0))
        {
            // firstInt was nodeListChecksum, secondInt is nodeCount!
            nodeCount = secondInt;
        } else {
            sc.Restore(sp, stl);
            nodeCount = firstInt;
        }
    }
    sc.FlushCurrentLine();
    if (nodeCount < 0 || nodeCount > 4096) return false;

    std::vector<JmsNode> nodes(static_cast<size_t>(nodeCount));
    for (int i = 0; i < nodeCount; ++i) {
        std::string_view nameLn;
        if (!sc.NextNonEmptyLine(nameLn)) return false;
        nodes[static_cast<size_t>(i)].name = std::string(nameLn);
        if (!isHalo2) {
            if (!sc.NextInt(nodes[static_cast<size_t>(i)].firstChild) ||
                !sc.NextInt(nodes[static_cast<size_t>(i)].nextSibling))
            {
                return false;
            }
        } else {
            if (!sc.NextInt(nodes[static_cast<size_t>(i)].parent)) return false;
        }
        if (!sc.NextFloat(nodes[static_cast<size_t>(i)].qx) ||
            !sc.NextFloat(nodes[static_cast<size_t>(i)].qy) ||
            !sc.NextFloat(nodes[static_cast<size_t>(i)].qz) ||
            !sc.NextFloat(nodes[static_cast<size_t>(i)].qw) ||
            !sc.NextFloat(nodes[static_cast<size_t>(i)].tx) ||
            !sc.NextFloat(nodes[static_cast<size_t>(i)].ty) ||
            !sc.NextFloat(nodes[static_cast<size_t>(i)].tz))
        {
            return false;
        }
        sc.FlushCurrentLine();
    }

    if (!isHalo2) {
        for (int i = 0; i < nodeCount; ++i) {
            int child = nodes[static_cast<size_t>(i)].firstChild;
            int guard = 0;
            while (child >= 0 && child < nodeCount && guard++ < nodeCount) {
                nodes[static_cast<size_t>(child)].parent = i;
                child = nodes[static_cast<size_t>(child)].nextSibling;
            }
        }
    }
    ComputeJmsNodeWorldPositions(nodes);

    // Materials table
    int matCount = 0;
    if (!sc.NextInt(matCount) || matCount < 0 || matCount > 4096) return false;
    sc.FlushCurrentLine();
    for (int m = 0; m < matCount; ++m) {
        std::string_view l1, l2;
        if (!sc.NextNonEmptyLine(l1) || !sc.NextNonEmptyLine(l2)) return false;
    }

    // Markers table
    int markerCount = 0;
    if (!sc.NextInt(markerCount) || markerCount < 0 || markerCount > 16384) return false;
    sc.FlushCurrentLine();
    for (int m = 0; m < markerCount; ++m) {
        std::string_view mName;
        if (!sc.NextNonEmptyLine(mName)) return false;
        int dummyI = 0;
        if (!isHalo2 && version >= 8198) {
            if (!sc.NextInt(dummyI)) return false; // regionIdx
        }
        if (!sc.NextInt(dummyI)) return false; // parentNodeIdx
        float dummyF = 0;
        for (int k = 0; k < 7; ++k) {
            if (!sc.NextFloat(dummyF)) return false; // rot(4) + pos(3)
        }
        if (version >= 8198) {
            if (!sc.NextFloat(dummyF)) return false; // radius
        }
        sc.FlushCurrentLine();
    }

    if (!isHalo2) {
        // Halo CE (8197–8200): Regions -> Vertices -> Triangles
        int regionCount = 0;
        if (!sc.NextInt(regionCount) || regionCount < 0 || regionCount > 4096) return false;
        sc.FlushCurrentLine();
        for (int r = 0; r < regionCount; ++r) {
            std::string_view rName;
            if (!sc.NextNonEmptyLine(rName)) return false;
        }

        int vertCount = 0;
        if (!sc.NextInt(vertCount) || vertCount < 0 || vertCount > 2000000) return false;
        mesh.vertices.reserve(static_cast<size_t>(vertCount));
        for (int v = 0; v < vertCount; ++v) {
            int node0 = 0, node1 = -1;
            float px = 0, py = 0, pz = 0, nx = 0, ny = 0, nz = 0, w1 = 0, u = 0, tv = 0, tw = 0;
            if (!sc.NextInt(node0) ||
                !sc.NextFloat(px) || !sc.NextFloat(py) || !sc.NextFloat(pz) ||
                !sc.NextFloat(nx) || !sc.NextFloat(ny) || !sc.NextFloat(nz) ||
                !sc.NextInt(node1) || !sc.NextFloat(w1) ||
                !sc.NextFloat(u) || !sc.NextFloat(tv))
            {
                return false;
            }
            if (version >= 8200) {
                if (!sc.NextFloat(tw)) return false; // w / flags in 8200
            }
            (void)node0; (void)node1; (void)w1; (void)nx; (void)ny; (void)nz; (void)u; (void)tv; (void)tw;
            mesh.vertices.push_back({ px, pz, -py }); // Halo Z-up -> Explorer2 Y-up
        }

        int triCount = 0;
        if (vertCount > 0 && sc.NextInt(triCount) && triCount > 0 && triCount <= 4000000) {
            mesh.tris.reserve(static_cast<size_t>(triCount));
            for (int t = 0; t < triCount; ++t) {
                int regIdx = 0, matIdx = 0, i0 = 0, i1 = 0, i2 = 0;
                if (version >= 8198 && !sc.NextInt(regIdx)) return false;
                if (!sc.NextInt(matIdx) || !sc.NextInt(i0) || !sc.NextInt(i1) || !sc.NextInt(i2)) return false;
                (void)regIdx; (void)matIdx;
                if (i0 < 0 || i0 >= vertCount || i1 < 0 || i1 >= vertCount || i2 < 0 || i2 >= vertCount) {
                    return false;
                }
                if (i0 != i1 && i1 != i2 && i0 != i2) {
                    mesh.tris.push_back({
                        static_cast<uint32_t>(i0),
                        static_cast<uint32_t>(i2),
                        static_cast<uint32_t>(i1),
                        {}
                    });
                }
            }
        }
    } else {
        // Halo 2 (8205–8210): Instance Xrefs -> Instance Markers -> Vertices -> Triangles
        int xrefCount = 0;
        if (!sc.NextInt(xrefCount) || xrefCount < 0 || xrefCount > 4096) return false;
        sc.FlushCurrentLine();
        for (int x = 0; x < xrefCount; ++x) {
            std::string_view p1, p2;
            if (!sc.NextNonEmptyLine(p1) || !sc.NextNonEmptyLine(p2)) return false;
        }
        int instMarkers = 0;
        if (!sc.NextInt(instMarkers) || instMarkers < 0 || instMarkers > 16384) return false;
        sc.FlushCurrentLine();
        for (int im = 0; im < instMarkers; ++im) {
            std::string_view iname;
            int idx0 = 0, idx1 = 0;
            float f = 0;
            if (!sc.NextNonEmptyLine(iname) || !sc.NextInt(idx0) || !sc.NextInt(idx1)) return false;
            for (int k = 0; k < 7; ++k) if (!sc.NextFloat(f)) return false;
            sc.FlushCurrentLine();
        }

        int vertCount = 0;
        if (!sc.NextInt(vertCount) || vertCount < 0 || vertCount > 2000000) return false;
        mesh.vertices.reserve(static_cast<size_t>(vertCount));
        for (int v = 0; v < vertCount; ++v) {
            float px = 0, py = 0, pz = 0, nx = 0, ny = 0, nz = 0;
            if (!sc.NextFloat(px) || !sc.NextFloat(py) || !sc.NextFloat(pz) ||
                !sc.NextFloat(nx) || !sc.NextFloat(ny) || !sc.NextFloat(nz))
            {
                return false;
            }
            int infCount = 0;
            if (!sc.NextInt(infCount) || infCount < 0 || infCount > 16) return false;
            for (int k = 0; k < infCount; ++k) {
                int bIdx = 0; float bw = 0;
                if (!sc.NextInt(bIdx) || !sc.NextFloat(bw)) return false;
            }
            int uvCount = 0;
            if (!sc.NextInt(uvCount) || uvCount < 0 || uvCount > 16) return false;
            for (int k = 0; k < uvCount; ++k) {
                float u = 0, tv = 0;
                if (!sc.NextFloat(u) || !sc.NextFloat(tv)) return false;
            }
            mesh.vertices.push_back({ px, pz, -py });
        }

        int triCount = 0;
        if (vertCount > 0 && sc.NextInt(triCount) && triCount > 0 && triCount <= 4000000) {
            mesh.tris.reserve(static_cast<size_t>(triCount));
            for (int t = 0; t < triCount; ++t) {
                int matIdx = 0, i0 = 0, i1 = 0, i2 = 0;
                if (!sc.NextInt(matIdx) || !sc.NextInt(i0) || !sc.NextInt(i1) || !sc.NextInt(i2)) return false;
                if (i0 < 0 || i0 >= vertCount || i1 < 0 || i1 >= vertCount || i2 < 0 || i2 >= vertCount) {
                    return false;
                }
                if (i0 != i1 && i1 != i2 && i0 != i2) {
                    mesh.tris.push_back({
                        static_cast<uint32_t>(i0),
                        static_cast<uint32_t>(i2),
                        static_cast<uint32_t>(i1),
                        {}
                    });
                }
            }
        }
    }

    // Fallback to skeleton bones if this .jms is a skeleton/rig-only export (0 triangles, >= 2 nodes)
    if (mesh.tris.empty() && nodes.size() >= 2) {
        mesh.vertices.clear();
        for (size_t i = 0; i < nodes.size(); ++i) {
            int p = nodes[i].parent;
            if (p >= 0 && static_cast<size_t>(p) < nodes.size()) {
                MeshBuild3D::AppendOctahedralBonePrism(mesh, nodes[static_cast<size_t>(p)].worldPos, nodes[i].worldPos, 0.08f);
            }
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = isHalo2
        ? L"Halo 2 Jointed Model Skeleton (.jms)"
        : L"Halo CE Jointed Model Skeleton (.jms)";
    return true;
}

} // namespace Formats
