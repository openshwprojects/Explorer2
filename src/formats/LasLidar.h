#pragma once
// LasLidar.h — ASPRS LAS Airborne LiDAR Point Cloud (.las) 3D Parser

namespace Formats {

inline bool ParseLasLidarMesh(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    if (size < 227 || std::memcmp(data, "LASF", 4) != 0) return false;
    mesh.formatName = L"ASPRS LAS LiDAR Point Cloud (.las)";

    auto rdU16 = [&](size_t off) -> uint16_t {
        uint16_t v = 0; std::memcpy(&v, data + off, 2); return v;
    };
    auto rdU32 = [&](size_t off) -> uint32_t {
        uint32_t v = 0; std::memcpy(&v, data + off, 4); return v;
    };
    auto rdI32 = [&](size_t off) -> int32_t {
        int32_t v = 0; std::memcpy(&v, data + off, 4); return v;
    };
    auto rdF64 = [&](size_t off) -> double {
        double v = 0.0; std::memcpy(&v, data + off, 8); return v;
    };

    uint32_t offsetToPoints = rdU32(96);
    uint16_t recLen = rdU16(105);
    uint32_t numPoints = rdU32(107);
    double scaleX = rdF64(131), scaleY = rdF64(139), scaleZ = rdF64(147);
    double offX   = rdF64(155), offY   = rdF64(163), offZ   = rdF64(171);

    if (scaleX == 0.0) scaleX = 0.01;
    if (scaleY == 0.0) scaleY = 0.01;
    if (scaleZ == 0.0) scaleZ = 0.01;
    if (recLen < 12 || offsetToPoints >= size) return false;

    std::vector<Vec3f> pts;
    size_t pos = offsetToPoints;
    for (uint32_t i = 0; i < numPoints && pos + recLen <= size && pts.size() < 20000; ++i, pos += recLen) {
        float x = static_cast<float>(rdI32(pos + 0) * scaleX + offX);
        float y = static_cast<float>(rdI32(pos + 4) * scaleY + offY);
        float z = static_cast<float>(rdI32(pos + 8) * scaleZ + offZ);
        if (std::isfinite(x) && std::isfinite(y) && std::isfinite(z)) {
            pts.push_back({ x, z, -y });
        }
    }
    if (pts.empty()) return false;

    float minX = 1e9f, maxX = -1e9f;
    for (const auto& p : pts) { minX = std::min(minX, p.x); maxX = std::max(maxX, p.x); }
    float r = std::max(0.05f, (maxX - minX) * 0.02f);

    for (const auto& p : pts) {
        uint32_t b = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back({ p.x,     p.y + r, p.z     });
        mesh.vertices.push_back({ p.x + r, p.y - r, p.z + r });
        mesh.vertices.push_back({ p.x - r, p.y - r, p.z + r });
        mesh.vertices.push_back({ p.x,     p.y - r, p.z - r });
        mesh.tris.push_back({ b + 0, b + 1, b + 2, {} });
        mesh.tris.push_back({ b + 0, b + 2, b + 3, {} });
        mesh.tris.push_back({ b + 0, b + 3, b + 1, {} });
    }
    return !mesh.tris.empty();
}

} // namespace Formats
