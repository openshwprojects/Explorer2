#pragma once
// SpringRtsS3o.h — Spring RTS Engine 3D Unit Model (.s3o — "Spring unit\0")
// Parses hierarchical `S3OPiece` tree with world-space parent offset accumulation,
// supporting `triangles` (0), `triangle strips` with `0xFFFFFFFF` restart cuts (1), and `quads` (2).

namespace Formats {

namespace DetailSpringS3o {

inline bool ParsePieceRecursive(
    const uint8_t* data,
    size_t size,
    uint32_t pieceOff,
    Vec3f parentWorldOffset,
    int depth,
    std::unordered_set<uint32_t>& visitedPieces,
    LoadedMesh3D& outMesh)
{
    if (depth > 64 || pieceOff == 0u || pieceOff >= size || size - pieceOff < 52u) return false;
    if (!visitedPieces.insert(pieceOff).second) return false; // Cycle guard!

    const uint8_t* p = data + pieceOff;
    const uint32_t numChilds    = ByteIO::RdU32LE(p + 0x04u);
    const uint32_t childsOff    = ByteIO::RdU32LE(p + 0x08u);
    const uint32_t numVertices  = ByteIO::RdU32LE(p + 0x0Cu);
    const uint32_t verticesOff  = ByteIO::RdU32LE(p + 0x10u);
    const uint32_t primType     = ByteIO::RdU32LE(p + 0x18u);
    const uint32_t vtxTableSize = ByteIO::RdU32LE(p + 0x1Cu);
    const uint32_t vtxTableOff  = ByteIO::RdU32LE(p + 0x20u);
    const float    ox           = ByteIO::RdF32LE(p + 0x28u);
    const float    oy           = ByteIO::RdF32LE(p + 0x2Cu);
    const float    oz           = ByteIO::RdF32LE(p + 0x30u);

    if (!std::isfinite(ox) || !std::isfinite(oy) || !std::isfinite(oz)) return false;
    if (primType > 2u || numChilds > 2048u || numVertices > 1000000u || vtxTableSize > 3000000u) return false;

    const Vec3f worldOff{ parentWorldOffset.x + ox, parentWorldOffset.y + oy, parentWorldOffset.z + oz };

    if (numVertices > 0u) {
        const uint64_t vBytes = static_cast<uint64_t>(numVertices) * 32ULL;
        if (verticesOff >= size || vBytes > size - verticesOff) return false;
        if (outMesh.vertices.size() + numVertices > 2000000u) return false;

        const uint32_t baseV = static_cast<uint32_t>(outMesh.vertices.size());
        for (uint32_t i = 0u; i < numVertices; ++i) {
            const uint8_t* vp = data + verticesOff + static_cast<size_t>(i) * 32u;
            const float vx = ByteIO::RdF32LE(vp + 0u);
            const float vy = ByteIO::RdF32LE(vp + 4u);
            const float vz = ByteIO::RdF32LE(vp + 8u);
            if (!std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(vz)) return false;
            outMesh.vertices.push_back({ worldOff.x + vx, worldOff.y + vy, worldOff.z + vz });
        }

        if (vtxTableSize > 0u) {
            const uint64_t iBytes = static_cast<uint64_t>(vtxTableSize) * 4ULL;
            if (vtxTableOff >= size || iBytes > size - vtxTableOff) return false;
            const uint8_t* ip = data + vtxTableOff;

            auto addTri = [&](uint32_t a, uint32_t b, uint32_t c) {
                if (a != b && b != c && a != c && outMesh.tris.size() < 2000000u) {
                    outMesh.tris.push_back({ baseV + a, baseV + b, baseV + c, {} });
                }
            };

            if (primType == 0u) { // Triangles
                for (uint32_t i = 0u; i + 2u < vtxTableSize; i += 3u) {
                    const uint32_t i0 = ByteIO::RdU32LE(ip + static_cast<size_t>(i + 0u) * 4u);
                    const uint32_t i1 = ByteIO::RdU32LE(ip + static_cast<size_t>(i + 1u) * 4u);
                    const uint32_t i2 = ByteIO::RdU32LE(ip + static_cast<size_t>(i + 2u) * 4u);
                    if (i0 >= numVertices || i1 >= numVertices || i2 >= numVertices) return false;
                    addTri(i0, i1, i2);
                }
            } else if (primType == 1u) { // Triangle strips with 0xFFFFFFFF restart index
                std::vector<uint32_t> strip;
                auto flushStrip = [&]() {
                    for (size_t k = 2u; k < strip.size(); ++k) {
                        if ((k & 1u) == 0u) addTri(strip[k - 2u], strip[k - 1u], strip[k]);
                        else                addTri(strip[k - 1u], strip[k - 2u], strip[k]);
                    }
                    strip.clear();
                };
                for (uint32_t i = 0u; i < vtxTableSize; ++i) {
                    const uint32_t idx = ByteIO::RdU32LE(ip + static_cast<size_t>(i) * 4u);
                    if (idx == 0xFFFFFFFFu) {
                        flushStrip();
                    } else {
                        if (idx >= numVertices) return false;
                        strip.push_back(idx);
                    }
                }
                flushStrip();
            } else if (primType == 2u) { // Quads
                for (uint32_t i = 0u; i + 3u < vtxTableSize; i += 4u) {
                    const uint32_t i0 = ByteIO::RdU32LE(ip + static_cast<size_t>(i + 0u) * 4u);
                    const uint32_t i1 = ByteIO::RdU32LE(ip + static_cast<size_t>(i + 1u) * 4u);
                    const uint32_t i2 = ByteIO::RdU32LE(ip + static_cast<size_t>(i + 2u) * 4u);
                    const uint32_t i3 = ByteIO::RdU32LE(ip + static_cast<size_t>(i + 3u) * 4u);
                    if (i0 >= numVertices || i1 >= numVertices || i2 >= numVertices || i3 >= numVertices) return false;
                    addTri(i0, i1, i2);
                    addTri(i0, i2, i3);
                }
            }
        }
    }

    if (numChilds > 0u) {
        const uint64_t cBytes = static_cast<uint64_t>(numChilds) * 4ULL;
        if (childsOff >= size || cBytes > size - childsOff) return false;
        for (uint32_t c = 0u; c < numChilds; ++c) {
            const uint32_t childPieceOff = ByteIO::RdU32LE(data + childsOff + static_cast<size_t>(c) * 4u);
            if (!ParsePieceRecursive(data, size, childPieceOff, worldOff, depth + 1, visitedPieces, outMesh)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace DetailSpringS3o

inline bool ParseSpringRtsS3o(const uint8_t* data, size_t size, LoadedMesh3D& outMesh) {
    if (!data || size < 104u || size > (128u << 20)) return false;
    if (std::memcmp(data, "Spring unit\0", 12) != 0) return false;

    const uint32_t version = ByteIO::RdU32LE(data + 0x0Cu);
    const float    radius  = ByteIO::RdF32LE(data + 0x10u);
    const float    height  = ByteIO::RdF32LE(data + 0x14u);
    const uint32_t rootOff = ByteIO::RdU32LE(data + 0x24u);
    if (version != 0u || !std::isfinite(radius) || !std::isfinite(height)) return false;
    if (rootOff < 52u || rootOff >= size) return false;

    outMesh = LoadedMesh3D{};
    std::unordered_set<uint32_t> visited;
    if (!DetailSpringS3o::ParsePieceRecursive(data, size, rootOff, { 0.0f, 0.0f, 0.0f }, 0, visited, outMesh) ||
        outMesh.vertices.empty() || outMesh.tris.empty()) {
        outMesh = {};
        return false;
    }

    outMesh.formatName = L"Spring RTS Unit Model (.s3o)";
    return !outMesh.vertices.empty() && !outMesh.tris.empty();
}

} // namespace Formats
