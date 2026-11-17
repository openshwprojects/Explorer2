#pragma once
// MagicaVox.h — MagicaVoxel (.vox): SIZE / XYZI model chunks placed through the scene graph (nTRN translations
// over nGRP / nSHP nodes; rotations are not applied). Only voxel faces that touch empty space become
// triangles, so whole models fit the triangle budget instead of the first few thousand voxels.

namespace Formats {

inline bool ParseMagicaVoxMesh(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    if (size < 20 || std::memcmp(data, "VOX ", 4) != 0) return false;
    mesh.formatName = L"MagicaVoxel 3D Voxel Model (.vox)";

    auto rdU32 = [&](size_t off) -> uint32_t { uint32_t v = 0; std::memcpy(&v, data + off, 4); return v; };
    auto rdI32 = [&](size_t off) -> int32_t { int32_t v = 0; std::memcpy(&v, data + off, 4); return v; };

    struct Model { uint32_t sx = 0, sy = 0, sz = 0; const uint8_t* xyzi = nullptr; uint32_t count = 0; };
    struct Node { int kind = 0; std::vector<int> children; int tx = 0, ty = 0, tz = 0; std::vector<int> models; };
    std::vector<Model> models;
    std::map<int, Node> nodes; // 1 = transform, 2 = group, 3 = shape

    // Readers for the scene-graph chunk contents (bounds checked against `end`)
    auto readString = [&](size_t& p, size_t end, std::string& out) {
        if (p + 4 > end) return false;
        uint32_t n = rdU32(p);
        p += 4;
        if (n > end - p) return false;
        out.assign(reinterpret_cast<const char*>(data + p), n);
        p += n;
        return true;
    };
    auto readDict = [&](size_t& p, size_t end, std::map<std::string, std::string>* outDict) {
        if (p + 4 > end) return false;
        uint32_t n = rdU32(p);
        p += 4;
        if (n > 4096) return false;
        for (uint32_t i = 0; i < n; ++i) {
            std::string k, v;
            if (!readString(p, end, k) || !readString(p, end, v)) return false;
            if (outDict) (*outDict)[k] = v;
        }
        return true;
    };

    // 1. Walk the RIFF-like chunk list inside MAIN
    size_t pos = 8;
    if (pos + 12 > size || std::memcmp(data + pos, "MAIN", 4) != 0) return false;
    size_t mainEnd = std::min<size_t>(size, pos + 12 + rdU32(pos + 4) + static_cast<size_t>(rdU32(pos + 8)));
    pos += 12 + rdU32(pos + 4);
    uint32_t pendingX = 0, pendingY = 0, pendingZ = 0;
    while (pos + 12 <= mainEnd) {
        const uint8_t* id = data + pos;
        size_t content = rdU32(pos + 4), children = rdU32(pos + 8);
        size_t cs = pos + 12;
        if (content > mainEnd - cs || children > mainEnd - cs - content) break;
        size_t ce = cs + content;
        if (std::memcmp(id, "SIZE", 4) == 0 && content >= 12) {
            pendingX = rdU32(cs); pendingY = rdU32(cs + 4); pendingZ = rdU32(cs + 8);
        } else if (std::memcmp(id, "XYZI", 4) == 0 && content >= 4) {
            Model m;
            m.sx = pendingX; m.sy = pendingY; m.sz = pendingZ;
            m.count = std::min<uint32_t>(rdU32(cs), static_cast<uint32_t>((content - 4) / 4));
            m.xyzi = data + cs + 4;
            if (models.size() < 4096) models.push_back(m);
        } else if (std::memcmp(id, "nTRN", 4) == 0 || std::memcmp(id, "nGRP", 4) == 0 || std::memcmp(id, "nSHP", 4) == 0) {
            size_t p = cs;
            if (p + 4 > ce) { pos = ce + children; continue; }
            int nodeId = rdI32(p);
            p += 4;
            Node node;
            if (readDict(p, ce, nullptr)) {
                if (id[1] == 'T') { // nTRN: child, reserved, layer, frame count, frames
                    node.kind = 1;
                    if (p + 16 <= ce) {
                        node.children.push_back(rdI32(p));
                        uint32_t frames = rdU32(p + 12);
                        p += 16;
                        std::map<std::string, std::string> frame;
                        if (frames > 0 && readDict(p, ce, &frame)) {
                            auto it = frame.find("_t");
                            if (it != frame.end()) sscanf_s(it->second.c_str(), "%d %d %d", &node.tx, &node.ty, &node.tz);
                        }
                    }
                } else if (id[1] == 'G') { // nGRP: child ids
                    node.kind = 2;
                    if (p + 4 <= ce) {
                        uint32_t n = rdU32(p);
                        p += 4;
                        for (uint32_t i = 0; i < n && p + 4 <= ce && i < 65536; ++i, p += 4) node.children.push_back(rdI32(p));
                    }
                } else { // nSHP: model ids
                    node.kind = 3;
                    if (p + 4 <= ce) {
                        uint32_t n = rdU32(p);
                        p += 4;
                        for (uint32_t i = 0; i < n && p + 4 <= ce && i < 4096; ++i) {
                            node.models.push_back(rdI32(p));
                            p += 4;
                            if (!readDict(p, ce, nullptr)) break;
                        }
                    }
                }
                nodes[nodeId] = std::move(node);
            }
        }
        pos = ce + children;
    }
    if (models.empty()) return false;

    // 2. Model placements: scene graph from node 0 (translations accumulate), else each model at the origin
    struct Placement { int model; int tx, ty, tz; };
    std::vector<Placement> placements;
    if (!nodes.empty() && nodes.count(0)) {
        auto walk = [&](auto& self, int id, int tx, int ty, int tz, int depth) -> void {
            if (depth > 64 || placements.size() > 65536) return;
            auto it = nodes.find(id);
            if (it == nodes.end()) return;
            const Node& n = it->second;
            if (n.kind == 1) { tx += n.tx; ty += n.ty; tz += n.tz; }
            for (int m : n.models) {
                if (m >= 0 && static_cast<size_t>(m) < models.size()) placements.push_back({ m, tx, ty, tz });
            }
            for (int c : n.children) self(self, c, tx, ty, tz, depth + 1);
        };
        walk(walk, 0, 0, 0, 0, 0);
    }
    if (placements.empty()) {
        for (size_t m = 0; m < models.size(); ++m) placements.push_back({ static_cast<int>(m), 0, 0, 0 });
    }

    // 3. Exposed faces only (a neighbour voxel inside the same model hides a face)
    constexpr size_t kMaxTris = 400000;
    const int dirs[6][3] = { { 0, 0, -1 }, { 0, 0, 1 }, { -1, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 } };
    // Corner indices of the unit cube faces matching dirs (cube corners: bit0 x, bit1 y, bit2 z)
    const int faceCorners[6][4] = { { 0, 2, 3, 1 }, { 4, 5, 7, 6 }, { 0, 4, 6, 2 }, { 1, 3, 7, 5 }, { 2, 6, 7, 3 }, { 0, 1, 5, 4 } };
    for (const auto& pl : placements) {
        const Model& m = models[static_cast<size_t>(pl.model)];
        if (m.count == 0) continue;
        uint32_t sx = std::max<uint32_t>(m.sx, 1), sy = std::max<uint32_t>(m.sy, 1), sz = std::max<uint32_t>(m.sz, 1);
        if (sx > 2048 || sy > 2048 || sz > 2048) continue;
        std::vector<uint8_t> occ(static_cast<size_t>(sx) * sy * sz / 8 + 1, 0);
        auto bit = [&](int x, int y, int z) -> bool {
            if (x < 0 || y < 0 || z < 0 || x >= static_cast<int>(sx) || y >= static_cast<int>(sy) || z >= static_cast<int>(sz)) return false;
            size_t i = (static_cast<size_t>(z) * sy + static_cast<size_t>(y)) * sx + static_cast<size_t>(x);
            return (occ[i >> 3] >> (i & 7)) & 1;
        };
        for (uint32_t v = 0; v < m.count; ++v) {
            const uint8_t* q = m.xyzi + 4 * v;
            if (q[0] >= sx || q[1] >= sy || q[2] >= sz) continue;
            size_t i = (static_cast<size_t>(q[2]) * sy + q[1]) * sx + q[0];
            occ[i >> 3] |= static_cast<uint8_t>(1u << (i & 7));
        }
        // MagicaVoxel models are centred on their translation
        float ox = static_cast<float>(pl.tx) - static_cast<float>(sx / 2);
        float oy = static_cast<float>(pl.ty) - static_cast<float>(sy / 2);
        float oz = static_cast<float>(pl.tz) - static_cast<float>(sz / 2);
        for (uint32_t v = 0; v < m.count && mesh.tris.size() + 12 <= kMaxTris; ++v) {
            const uint8_t* q = m.xyzi + 4 * v;
            int x = q[0], y = q[1], z = q[2];
            if (x >= static_cast<int>(sx) || y >= static_cast<int>(sy) || z >= static_cast<int>(sz)) continue;
            float wx = ox + x, wy = oy + y, wz = oz + z; // MagicaVoxel is Z-up
            for (int f = 0; f < 6; ++f) {
                if (bit(x + dirs[f][0], y + dirs[f][1], z + dirs[f][2])) continue;
                uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
                for (int k = 0; k < 4; ++k) {
                    int c = faceCorners[f][k];
                    float cx = wx + (c & 1), cy = wy + ((c >> 1) & 1), cz = wz + ((c >> 2) & 1);
                    mesh.vertices.push_back({ cx, cz, -cy }); // Z-up -> Y-up
                }
                mesh.tris.push_back({ base, base + 1, base + 2, {} });
                mesh.tris.push_back({ base, base + 2, base + 3, {} });
            }
        }
    }
    return !mesh.tris.empty();
}

} // namespace Formats
