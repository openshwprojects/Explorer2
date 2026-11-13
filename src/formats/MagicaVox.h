#pragma once
// MagicaVox.h — MagicaVoxel 3D Voxel Model (.vox) Chunk & Cube Mesh Parser

namespace Formats {

inline bool ParseMagicaVoxMesh(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    if (size < 24 || std::memcmp(data, "VOX ", 4) != 0) return false;
    mesh.formatName = L"MagicaVoxel 3D Voxel Model (.vox)";

    auto rdU32 = [&](size_t off) -> uint32_t {
        uint32_t v = 0; std::memcpy(&v, data + off, 4); return v;
    };

    auto scanChunks = [&](auto& self, size_t start, size_t end) -> void {
        size_t pos = start;
        while (pos + 12 <= end) {
            char id[5] = {};
            std::memcpy(id, data + pos, 4);
            uint32_t contentBytes = rdU32(pos + 4);
            uint32_t childBytes   = rdU32(pos + 8);
            size_t contentStart   = pos + 12;
            size_t childStart     = contentStart + contentBytes;
            size_t nextChunk      = childStart + childBytes;
            if (nextChunk > end) break;

            if (std::memcmp(id, "XYZI", 4) == 0 && contentBytes >= 4) {
                uint32_t numVoxels = rdU32(contentStart);
                size_t vpos = contentStart + 4;
                for (uint32_t i = 0; i < numVoxels && vpos + 4 <= childStart && mesh.tris.size() < 48000; ++i, vpos += 4) {
                    float vx = static_cast<float>(data[vpos + 0]);
                    float vy = static_cast<float>(data[vpos + 2]); // Z-up in MagicaVoxel -> Y-up
                    float vz = -static_cast<float>(data[vpos + 1]);
                    const float h = 0.48f;
                    uint32_t b = static_cast<uint32_t>(mesh.vertices.size());
                    mesh.vertices.push_back({ vx - h, vy - h, vz - h });
                    mesh.vertices.push_back({ vx + h, vy - h, vz - h });
                    mesh.vertices.push_back({ vx + h, vy + h, vz - h });
                    mesh.vertices.push_back({ vx - h, vy + h, vz - h });
                    mesh.vertices.push_back({ vx - h, vy - h, vz + h });
                    mesh.vertices.push_back({ vx + h, vy - h, vz + h });
                    mesh.vertices.push_back({ vx + h, vy + h, vz + h });
                    mesh.vertices.push_back({ vx - h, vy + h, vz + h });
                    const uint32_t q[6][4] = {
                        {0,1,2,3}, {5,4,7,6}, {4,0,3,7},
                        {1,5,6,2}, {3,2,6,7}, {4,5,1,0}
                    };
                    for (int f = 0; f < 6; ++f) {
                        mesh.tris.push_back({ b + q[f][0], b + q[f][1], b + q[f][2], {} });
                        mesh.tris.push_back({ b + q[f][0], b + q[f][2], b + q[f][3], {} });
                    }
                }
            }
            if (childBytes > 0) {
                self(self, childStart, nextChunk);
            }
            pos = nextChunk;
        }
    };

    scanChunks(scanChunks, 8, size);
    return !mesh.tris.empty();
}

} // namespace Formats
