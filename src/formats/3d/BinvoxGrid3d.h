#pragma once
// BinvoxGrid3d.h — Patrick Min 3D Voxel Grid (.binvox — "#binvox 1")
// Parses ASCII header (`dim`, `translate`, `scale`, `data`) followed by binary RLE `(value, count)`
// pairs in XZY order, meshing exterior exposed voxel faces via shared VoxelSurfaceMeshCommon.h.

namespace Formats {

inline bool ParseBinvoxGrid3d(const uint8_t* data, size_t size, LoadedMesh3D& outMesh) {
    if (!data || size < 24u || size > (64u << 20)) return false;

    // Locate `data\n` or `data\r\n` ending the ASCII header within the first 1024 bytes
    const size_t hdrLimit = std::min<size_t>(size, 1024u);
    size_t binStart = 0u;
    size_t pos = 0u;
    bool sawMagic = false;
    int dimX = 0, dimY = 0, dimZ = 0;
    Vec3f translate{ 0.0f, 0.0f, 0.0f };
    float scale = 1.0f;

    while (pos < hdrLimit) {
        size_t nl = pos;
        while (nl < hdrLimit && data[nl] != '\n') {
            if (data[nl] == 0u) return false;
            ++nl;
        }
        if (nl >= hdrLimit) return false;
        std::string_view line(reinterpret_cast<const char*>(data + pos), nl - pos);
        line = ReaderText::TrimSv(line);
        pos = nl + 1u;
        if (line.empty()) continue;

        if (!sawMagic) {
            if (line != "#binvox 1") return false;
            sawMagic = true;
            continue;
        }
        if (line == "data") {
            binStart = pos;
            break;
        }

        std::string_view kw;
        if (!ReaderText::NextTokenSv(line, kw)) continue;
        if (kw == "dim") {
            std::string_view t0, t1, t2;
            if (!ReaderText::NextTokenSv(line, t0) || !ReaderText::ParseIntSv(t0, dimX)) return false;
            if (ReaderText::NextTokenSv(line, t1) && ReaderText::NextTokenSv(line, t2)) {
                if (!ReaderText::ParseIntSv(t1, dimY) || !ReaderText::ParseIntSv(t2, dimZ)) return false;
            } else {
                dimY = dimZ = dimX;
            }
        } else if (kw == "translate") {
            std::string_view tx, ty, tz;
            if (!ReaderText::NextTokenSv(line, tx) ||
                !ReaderText::NextTokenSv(line, ty) ||
                !ReaderText::NextTokenSv(line, tz) ||
                !ReaderText::ParseFloatSv(tx, translate.x) ||
                !ReaderText::ParseFloatSv(ty, translate.y) ||
                !ReaderText::ParseFloatSv(tz, translate.z) ||
                !std::isfinite(translate.x) || !std::isfinite(translate.y) || !std::isfinite(translate.z)) {
                return false;
            }
        } else if (kw == "scale") {
            std::string_view ts;
            if (!ReaderText::NextTokenSv(line, ts) || !ReaderText::ParseFloatSv(ts, scale) ||
                !std::isfinite(scale) || scale <= 0.0f) {
                return false;
            }
        }
    }

    if (!sawMagic || binStart == 0u || binStart >= size) return false;
    if (dimX < 1 || dimY < 1 || dimZ < 1 || dimX > 512 || dimY > 512 || dimZ > 512) return false;
    const uint64_t totalGridVoxels = static_cast<uint64_t>(dimX) * static_cast<uint64_t>(dimY) * static_cast<uint64_t>(dimZ);
    if (totalGridVoxels > 8000000ULL) return false;
    if (((size - binStart) & 1u) != 0u) return false; // Binary RLE must be `(val, count)` pairs

    std::unordered_set<VoxelSurfaceMesh::VoxelCoord, VoxelSurfaceMesh::VoxelCoordHash> solidVoxels;
    const uint64_t yzStride = static_cast<uint64_t>(dimY) * static_cast<uint64_t>(dimZ);
    uint64_t linearIdx = 0u;

    for (size_t bp = binStart; bp + 1u < size; bp += 2u) {
        const uint8_t val = data[bp + 0u];
        const uint8_t cnt = data[bp + 1u];
        if (val > 1u || cnt == 0u) return false;
        if (linearIdx + cnt > totalGridVoxels) return false;

        if (val == 1u) {
            for (uint8_t c = 0u; c < cnt; ++c) {
                const uint64_t idx = linearIdx + c;
                // Binvox ordering: x * (dimY * dimZ) + z * dimY + y
                const int32_t vx = static_cast<int32_t>(idx / yzStride);
                const uint64_t rem = idx % yzStride;
                const int32_t vz = static_cast<int32_t>(rem / static_cast<uint64_t>(dimY));
                const int32_t vy = static_cast<int32_t>(rem % static_cast<uint64_t>(dimY));
                if (solidVoxels.size() < 500000u) {
                    solidVoxels.insert({ vx, vy, vz });
                }
            }
        }
        linearIdx += cnt;
    }

    if (linearIdx != totalGridVoxels || solidVoxels.empty()) return false;

    const float maxDim = static_cast<float>(std::max({ dimX, dimY, dimZ }));
    const float vSize  = scale / maxDim;

    outMesh = LoadedMesh3D{};
    if (!VoxelSurfaceMesh::EmitCulledSparseVoxels(solidVoxels, outMesh, translate, vSize)) {
        outMesh = {};
        return false;
    }

    wchar_t fmtBuf[112] = {};
    swprintf_s(fmtBuf, L"Binvox 3D Voxel Grid (.binvox %dx%dx%d)", dimX, dimY, dimZ);
    outMesh.formatName = fmtBuf;
    return !outMesh.vertices.empty() && !outMesh.tris.empty();
}

} // namespace Formats
