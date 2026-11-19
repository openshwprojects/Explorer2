#pragma once
// MmdPmd.h — MikuMikuDance PMD 1.0 Polygon Model (.pmd) reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

inline bool ParseMmdPmd(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    // Header: "Pmd" (3) + float version (4) + name[20] + comment[256] = 283 bytes + uint32 numVerts (4) = 287 bytes
    if (!data || size < 291) return false;
    if (std::memcmp(data, "Pmd", 3) != 0) return false;
    float ver = 0.0f;
    std::memcpy(&ver, data + 3, 4);
    if (!std::isfinite(ver) || ver < 0.9f || ver > 1.1f) return false;

    size_t pos = 283;
    uint32_t numVerts = 0;
    std::memcpy(&numVerts, data + pos, 4);
    pos += 4;
    if (numVerts == 0 || numVerts > 1000000) return false;
    uint64_t vertBytes = static_cast<uint64_t>(numVerts) * 38ULL;
    if (vertBytes + 4ULL > size - pos) return false;

    mesh.vertices.resize(numVerts);
    for (uint32_t i = 0; i < numVerts; ++i) {
        const uint8_t* vp = data + pos + static_cast<size_t>(i) * 38;
        float vx = 0.0f, vy = 0.0f, vz = 0.0f;
        std::memcpy(&vx, vp + 0, 4);
        std::memcpy(&vy, vp + 4, 4);
        std::memcpy(&vz, vp + 8, 4);
        if (!std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(vz)) return false;
        mesh.vertices[i] = MmdDetail::MmdToViewport(vx, vy, vz);
    }
    pos += static_cast<size_t>(vertBytes);

    uint32_t numIndices = 0;
    std::memcpy(&numIndices, data + pos, 4);
    pos += 4;
    if (numIndices < 3 || (numIndices % 3) != 0 || numIndices > 12000000) return false;
    if (static_cast<uint64_t>(numIndices) * 2ULL > size - pos) return false;

    const uint32_t nTris = numIndices / 3;
    mesh.tris.reserve(nTris);
    for (uint32_t t = 0; t < nTris; ++t) {
        uint16_t a = 0, b = 0, c = 0;
        std::memcpy(&a, data + pos + static_cast<size_t>(t) * 6 + 0, 2);
        std::memcpy(&b, data + pos + static_cast<size_t>(t) * 6 + 2, 2);
        std::memcpy(&c, data + pos + static_cast<size_t>(t) * 6 + 4, 2);
        if (a >= numVerts || b >= numVerts || c >= numVerts) return false;
        if (a == b || b == c || c == a) continue;
        mesh.tris.push_back({ a, c, b, {} });
    }

    if (mesh.tris.empty()) return false;
    mesh.formatName = L"MikuMikuDance PMD 1.0 Model (.pmd)";
    return true;
}

} // namespace Formats
