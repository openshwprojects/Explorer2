#pragma once
// Blitz3DB3d.h — Blitz3D (.b3d — BB3D chunked binary) 3D mesh reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

inline bool ParseBlitz3DB3d(const uint8_t* data, size_t size, const std::wstring& ext, LoadedMesh3D& mesh) {
    // #46. Blitz3D — "BB3D" chunked binary
    if (ext == L".b3d" || (size >= 12 && std::memcmp(data, "BB3D", 4) == 0)) {
        if (size < 12 || std::memcmp(data, "BB3D", 4) != 0) return false;
        mesh.formatName = L"Blitz3D Mesh (.b3d)";
        uint32_t curVBase = 0;
        auto walkChunks = [&](auto& self, size_t start, size_t end) -> void {
            size_t p = start;
            while (p + 8 <= end) {
                char tag[5] = {};
                std::memcpy(tag, data + p, 4);
                uint32_t cLen = DetailMeshSciCad::RdU32LE(data + p + 4);
                p += 8;
                if (p + cLen > end) break;
                size_t cEnd = p + cLen;

                if (std::strcmp(tag, "MESH") == 0 && cLen >= 4) {
                    curVBase = static_cast<uint32_t>(mesh.vertices.size());
                    self(self, p + 4, cEnd);
                } else if (std::strcmp(tag, "NODE") == 0) {
                    size_t np = p;
                    while (np < cEnd && data[np] != 0) ++np;
                    if (np + 1 + 40 <= cEnd) self(self, np + 41, cEnd);
                } else if (std::strcmp(tag, "VRTS") == 0 && cLen >= 12) {
                    curVBase = static_cast<uint32_t>(mesh.vertices.size());
                    uint32_t vFlags = DetailMeshSciCad::RdU32LE(data + p + 0);
                    uint32_t tcSets = DetailMeshSciCad::RdU32LE(data + p + 4);
                    uint32_t tcSize = DetailMeshSciCad::RdU32LE(data + p + 8);
                    size_t stride = 12 + ((vFlags & 1u) ? 12 : 0) + ((vFlags & 2u) ? 16 : 0) + static_cast<size_t>(tcSets) * tcSize * 4;
                    if (stride >= 12 && stride <= 256) {
                        for (size_t vp = p + 12; vp + stride <= cEnd; vp += stride) {
                            mesh.vertices.push_back({
                                DetailMeshSciCad::RdF32LE(data + vp + 0),
                                DetailMeshSciCad::RdF32LE(data + vp + 4),
                                DetailMeshSciCad::RdF32LE(data + vp + 8)
                            });
                        }
                    }
                } else if (std::strcmp(tag, "TRIS") == 0 && cLen >= 16) {
                    for (size_t tp = p + 4; tp + 12 <= cEnd; tp += 12) {
                        uint32_t i0 = curVBase + DetailMeshSciCad::RdU32LE(data + tp + 0);
                        uint32_t i1 = curVBase + DetailMeshSciCad::RdU32LE(data + tp + 4);
                        uint32_t i2 = curVBase + DetailMeshSciCad::RdU32LE(data + tp + 8);
                        if (i0 < mesh.vertices.size() && i1 < mesh.vertices.size() && i2 < mesh.vertices.size()) {
                            mesh.tris.push_back({ i0, i1, i2, {} });
                        }
                    }
                }
                p = cEnd;
            }
        };
        walkChunks(walkChunks, 12, size);
        return !mesh.vertices.empty() && !mesh.tris.empty();
    }
    return false;
}

} // namespace Formats
