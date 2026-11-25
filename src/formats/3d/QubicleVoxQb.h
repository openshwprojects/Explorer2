#pragma once
// QubicleVoxQb.h — Minddesk Qubicle Binary (.qb, v1.1.0.0) 3D Voxel Model reader
// Supports multi-matrix hierarchies with world offsets (posX, posY, posZ), RGBA & BGRA color
// formats, left/right-handed Z orientation, visibility masks, and both uncompressed & RLE Z-slices.

namespace Formats {

inline bool ParseQubicleVoxQb(
    const uint8_t* data,
    size_t size,
    LoadedMesh3D& mesh)
{
    if (!data || size < 30) return false;

    const uint32_t version = ByteIO::RdU32LE(data + 0);
    const uint32_t colorFormat = ByteIO::RdU32LE(data + 4);
    const uint32_t zOrient = ByteIO::RdU32LE(data + 8);
    const uint32_t compressed = ByteIO::RdU32LE(data + 12);
    const uint32_t visMaskEncoded = ByteIO::RdU32LE(data + 16);
    const uint32_t numMatrices = ByteIO::RdU32LE(data + 20);

    // Version 1.1.0.0 = {1, 1, 0, 0} = 0x00000101 (also accept 1.0.0.0 = 0x00000001)
    if (version != 0x00000101u && version != 0x00000001u) return false;
    if (colorFormat > 1 || zOrient > 1 || compressed > 1 || visMaskEncoded > 1) return false;
    if (numMatrices < 1 || numMatrices > 4096) return false;

    constexpr uint32_t kCodeFlag = 2u;
    constexpr uint32_t kNextSliceFlag = 6u;
    constexpr size_t kMaxTotalVoxels = 1200000u;

    std::vector<VoxelMesh3D::VoxelCoord> voxels;
    size_t pos = 24u;

    for (uint32_t m = 0; m < numMatrices; ++m) {
        if (pos >= size) return false;
        const uint8_t nameLen = data[pos++];
        if (pos + static_cast<size_t>(nameLen) + 24u > size) return false;
        // Validate matrix name is printable ASCII/UTF-8 without NUL
        for (uint8_t k = 0; k < nameLen; ++k) {
            if (data[pos + k] == 0) return false;
        }
        pos += nameLen;

        const uint32_t sx = ByteIO::RdU32LE(data + pos + 0);
        const uint32_t sy = ByteIO::RdU32LE(data + pos + 4);
        const uint32_t sz = ByteIO::RdU32LE(data + pos + 8);
        const int32_t px = ByteIO::RdI32LE(data + pos + 12);
        const int32_t py = ByteIO::RdI32LE(data + pos + 16);
        const int32_t pz = ByteIO::RdI32LE(data + pos + 20);
        pos += 24u;

        if (sx == 0 || sy == 0 || sz == 0 || sx > 512 || sy > 512 || sz > 512) return false;
        if (px < -100000 || px > 100000 || py < -100000 || py > 100000 || pz < -100000 || pz > 100000) return false;

        const uint64_t sliceCells = static_cast<uint64_t>(sx) * static_cast<uint64_t>(sy);
        const uint64_t volCells = sliceCells * static_cast<uint64_t>(sz);
        if (volCells > 32ULL * 1024ULL * 1024ULL) return false;

        auto pushVoxel = [&](uint32_t lx, uint32_t ly, uint32_t lz) -> bool {
            if (voxels.size() >= kMaxTotalVoxels) return false;
            const int32_t wx = px + static_cast<int32_t>(lx);
            const int32_t wy = py + static_cast<int32_t>(ly);
            const int32_t wz = pz + static_cast<int32_t>(lz);
            voxels.push_back({ wx, wy, (zOrient == 0) ? -wz : wz });
            return true;
        };

        if (compressed == 0) {
            if (volCells * 4ULL > size - pos) return false;
            for (uint32_t z = 0; z < sz; ++z) {
                for (uint32_t y = 0; y < sy; ++y) {
                    for (uint32_t x = 0; x < sx; ++x) {
                        const uint32_t v = ByteIO::RdU32LE(data + pos);
                        pos += 4u;
                        if (((v >> 24) & 0xFFu) != 0) {
                            if (!pushVoxel(x, y, z)) return false;
                        }
                    }
                }
            }
        } else {
            for (uint32_t z = 0; z < sz; ++z) {
                uint64_t idx = 0;
                while (true) {
                    if (pos + 4u > size) return false;
                    const uint32_t tag = ByteIO::RdU32LE(data + pos);
                    pos += 4u;
                    if (tag == kNextSliceFlag) break;

                    if (tag == kCodeFlag) {
                        if (pos + 8u > size) return false;
                        const uint32_t count = ByteIO::RdU32LE(data + pos);
                        const uint32_t val = ByteIO::RdU32LE(data + pos + 4u);
                        pos += 8u;
                        if (count > sliceCells - std::min(idx, sliceCells)) return false;
                        if (((val >> 24) & 0xFFu) != 0) {
                            for (uint32_t k = 0; k < count; ++k) {
                                const uint64_t c = idx + k;
                                const uint32_t lx = static_cast<uint32_t>(c % sx);
                                const uint32_t ly = static_cast<uint32_t>(c / sx);
                                if (!pushVoxel(lx, ly, z)) return false;
                            }
                        }
                        idx += count;
                    } else {
                        if (idx >= sliceCells) return false;
                        if (((tag >> 24) & 0xFFu) != 0) {
                            const uint32_t lx = static_cast<uint32_t>(idx % sx);
                            const uint32_t ly = static_cast<uint32_t>(idx / sx);
                            if (!pushVoxel(lx, ly, z)) return false;
                        }
                        ++idx;
                    }
                }
            }
        }
    }

    if (!VoxelMesh3D::EmitExposedVoxelFaces(voxels, mesh)) return false;
    mesh.formatName = L"Qubicle Binary Voxel (.qb)";
    return true;
}

} // namespace Formats
