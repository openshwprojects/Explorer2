#pragma once
// VoxlapKv6.h — Ken Silverman Voxlap / Ace of Spades Voxel Model (.kv6, "Kvxl") reader
// Validates "Kvxl" magic, dimensions, finite float pivots, 8-byte surface blocks, and exact
// xlen/ylen column counts summing to blklen, reconstructing hollow column spans & exposed faces.

namespace Formats {

inline bool ParseVoxlapKv6(
    const uint8_t* data,
    size_t size,
    LoadedMesh3D& mesh)
{
    if (!data || size < 40) return false;
    if (std::memcmp(data, "Kvxl", 4) != 0) return false;

    const uint32_t xsiz = ByteIO::RdU32LE(data + 4);
    const uint32_t ysiz = ByteIO::RdU32LE(data + 8);
    const uint32_t zsiz = ByteIO::RdU32LE(data + 12);
    const float xpiv = ByteIO::RdF32LE(data + 16);
    const float ypiv = ByteIO::RdF32LE(data + 20);
    const float zpiv = ByteIO::RdF32LE(data + 24);
    const uint32_t blklen = ByteIO::RdU32LE(data + 28);

    if (xsiz < 1 || xsiz > 1024 || ysiz < 1 || ysiz > 1024 || zsiz < 1 || zsiz > 1024) return false;
    if (!std::isfinite(xpiv) || !std::isfinite(ypiv) || !std::isfinite(zpiv)) return false;
    if (blklen < 1 || blklen > 1200000u) return false;

    const uint64_t blkBytes = static_cast<uint64_t>(blklen) * 8ULL;
    const uint64_t xlenBytes = static_cast<uint64_t>(xsiz) * 4ULL;
    const uint64_t ylenBytes = static_cast<uint64_t>(xsiz) * static_cast<uint64_t>(ysiz) * 2ULL;
    if (32ULL + blkBytes + xlenBytes + ylenBytes > size) return false;

    const uint8_t* blkPtr  = data + 32u;
    const uint8_t* xlenPtr = blkPtr + static_cast<size_t>(blkBytes);
    const uint8_t* ylenPtr = xlenPtr + static_cast<size_t>(xlenBytes);

    // Verify xlen sum matches blklen
    uint64_t xSum = 0;
    for (uint32_t x = 0; x < xsiz; ++x) {
        const int32_t xl = ByteIO::RdI32LE(xlenPtr + static_cast<size_t>(x) * 4u);
        if (xl < 0) return false;
        xSum += static_cast<uint64_t>(xl);
    }
    if (xSum != blklen) return false;

    std::vector<VoxelMesh3D::VoxelCoord> voxels;
    voxels.reserve(blklen);
    constexpr size_t kMaxTotalVoxels = 1500000u;

    size_t curBlk = 0;
    size_t yIdx = 0;
    for (uint32_t x = 0; x < xsiz; ++x) {
        for (uint32_t y = 0; y < ysiz; ++y, ++yIdx) {
            const uint16_t yl = ByteIO::RdU16LE(ylenPtr + yIdx * 2u);
            if (curBlk + yl > blklen) return false;

            int32_t prevZ = -1;
            bool prevOpenDown = false;
            for (uint16_t k = 0; k < yl; ++k, ++curBlk) {
                const uint8_t* bp = blkPtr + curBlk * 8u;
                const uint16_t zpos = ByteIO::RdU16LE(bp + 4);
                const uint8_t vis = bp[6];
                if (zpos >= zsiz) return false;

                if (prevOpenDown && (vis & 0x10u) == 0 && prevZ >= 0 && prevZ + 1 < static_cast<int32_t>(zpos)) {
                    for (int32_t z = prevZ + 1; z < static_cast<int32_t>(zpos); ++z) {
                        if (voxels.size() >= kMaxTotalVoxels) return false;
                        voxels.push_back({
                            static_cast<int32_t>(x),
                            static_cast<int32_t>(zsiz - 1u) - z,
                            -static_cast<int32_t>(y)
                        });
                    }
                }

                if (voxels.size() >= kMaxTotalVoxels) return false;
                voxels.push_back({
                    static_cast<int32_t>(x),
                    static_cast<int32_t>(zsiz - 1u - zpos),
                    -static_cast<int32_t>(y)
                });
                prevZ = static_cast<int32_t>(zpos);
                prevOpenDown = ((vis & 0x20u) == 0);
            }
        }
    }
    if (curBlk != blklen) return false;

    if (!VoxelMesh3D::EmitExposedVoxelFaces(voxels, mesh)) return false;
    mesh.formatName = L"Voxlap Voxel Model (KV6)";
    return true;
}

} // namespace Formats
