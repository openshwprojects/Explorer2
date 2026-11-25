#pragma once
// KenSilvermanKvx.h — Ken Silverman Build Engine / Slab6 Voxel Model (.kvx) reader
// Validates exact MIP0 numbytes, xsiz/ysiz/zsiz bounds, and the structural xoffset[0] table
// invariant, reconstructs surface + hollow-interior column spans, and emits culled 3D faces.

namespace Formats {

inline bool ParseKenSilvermanKvx(
    const uint8_t* data,
    size_t size,
    LoadedMesh3D& mesh)
{
    // Minimum header: 4 (numbytes) + 24 (dims + pivots) + 8 (xoffset[2]) + 4 (xyoffset[1][2]) = 40 bytes
    if (!data || size < 40) return false;

    const uint32_t numBytes = ByteIO::RdU32LE(data + 0);
    if (numBytes < 36 || 4ULL + static_cast<uint64_t>(numBytes) > size) return false;

    const uint8_t* vox = data + 4u; // MIP 0 block of length numBytes
    const int32_t xsiz = ByteIO::RdI32LE(vox + 0);
    const int32_t ysiz = ByteIO::RdI32LE(vox + 4);
    const int32_t zsiz = ByteIO::RdI32LE(vox + 8);
    if (xsiz < 1 || xsiz > 512 || ysiz < 1 || ysiz > 512 || zsiz < 1 || zsiz > 512) return false;

    const size_t xOffBytes = static_cast<size_t>(xsiz + 1) * 4u;
    const size_t xyOffBytes = static_cast<size_t>(xsiz) * static_cast<size_t>(ysiz + 1) * 2u;
    const size_t expectedXOff0 = xOffBytes + xyOffBytes;
    if (24u + expectedXOff0 > numBytes) return false;

    const uint8_t* xOffTable = vox + 24u;
    const uint8_t* xyOffTable = xOffTable + xOffBytes;

    // Exact Slab6 / Build Engine KVX invariant: xoffset[0] == (xsiz+1)*4 + xsiz*(ysiz+1)*2
    const int32_t xOff0 = ByteIO::RdI32LE(xOffTable + 0);
    if (xOff0 < 0 || static_cast<size_t>(xOff0) != expectedXOff0) return false;

    const int32_t xOffEnd = ByteIO::RdI32LE(xOffTable + static_cast<size_t>(xsiz) * 4u);
    if (xOffEnd < xOff0 || 24ULL + static_cast<uint64_t>(xOffEnd) > numBytes) return false;

    std::vector<VoxelMesh3D::VoxelCoord> voxels;
    constexpr size_t kMaxVoxels = 1200000u;

    for (int32_t x = 0; x < xsiz; ++x) {
        const int32_t xo = ByteIO::RdI32LE(xOffTable + static_cast<size_t>(x) * 4u);
        const int32_t xoNext = ByteIO::RdI32LE(xOffTable + static_cast<size_t>(x + 1) * 4u);
        if (xo < xOff0 || xoNext < xo || 24ULL + static_cast<uint64_t>(xoNext) > numBytes) return false;

        const uint8_t* xyRow = xyOffTable + static_cast<size_t>(x) * static_cast<size_t>(ysiz + 1) * 2u;
        for (int32_t y = 0; y < ysiz; ++y) {
            const uint16_t y0 = ByteIO::RdU16LE(xyRow + static_cast<size_t>(y) * 2u);
            const uint16_t y1 = ByteIO::RdU16LE(xyRow + static_cast<size_t>(y + 1) * 2u);
            if (y1 < y0) return false;

            const size_t colStart = 24u + static_cast<size_t>(xo) + static_cast<size_t>(y0);
            const size_t colEnd   = 24u + static_cast<size_t>(xo) + static_cast<size_t>(y1);
            if (colEnd > numBytes) return false;

            size_t p = colStart;
            int32_t prevSlabBottomZ = -1;
            bool prevContinuedDown = false;

            while (p < colEnd) {
                if (p + 3u > colEnd) return false;
                const uint8_t ztop = vox[p + 0];
                const uint8_t zleng = vox[p + 1];
                const uint8_t vis = vox[p + 2];
                p += 3u;
                if (p + static_cast<size_t>(zleng) > colEnd) return false;
                if (static_cast<int32_t>(ztop) + static_cast<int32_t>(zleng) > zsiz) return false;
                p += zleng;

                if (zleng == 0) continue;

                // If previous slab omitted its bottom face (bit 5 clear) and this slab omits its top face
                // (bit 4 clear), fill the solid interior column span between them so face culling is exact.
                if (prevContinuedDown && (vis & 0x10u) == 0 && prevSlabBottomZ >= 0 && prevSlabBottomZ < static_cast<int32_t>(ztop)) {
                    for (int32_t z = prevSlabBottomZ; z < static_cast<int32_t>(ztop); ++z) {
                        if (voxels.size() >= kMaxVoxels) return false;
                        // Build Engine +Z points downward: flip Z -> viewer +Y up
                        voxels.push_back({ x, (zsiz - 1) - z, -y });
                    }
                }

                for (uint8_t k = 0; k < zleng; ++k) {
                    if (voxels.size() >= kMaxVoxels) return false;
                    const int32_t z = static_cast<int32_t>(ztop) + static_cast<int32_t>(k);
                    voxels.push_back({ x, (zsiz - 1) - z, -y });
                }

                prevSlabBottomZ = static_cast<int32_t>(ztop) + static_cast<int32_t>(zleng);
                prevContinuedDown = ((vis & 0x20u) == 0);
            }
        }
    }

    if (!VoxelMesh3D::EmitExposedVoxelFaces(voxels, mesh)) return false;
    mesh.formatName = L"Build Engine Voxel (KVX)";
    return true;
}

} // namespace Formats
