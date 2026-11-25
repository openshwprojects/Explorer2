#pragma once
// GoxelGoxModel.h — Goxel Chunked 3D Voxel Model (.gox — "GOX " v2)
// Decodes `BL16` 64x64 RGBA PNG 16x16x16 voxel blocks (cross-platform PNG IHDR+IDAT+unfilter via ZipDeflate)
// and `LAYR` block placements, meshing exposed exterior voxel faces via VoxelSurfaceMeshCommon.h.

namespace Formats {

namespace DetailGoxelGox {

inline uint8_t PaethPredictor(int a, int b, int c) {
    const int p  = a + b - c;
    const int pa = std::abs(p - a);
    const int pb = std::abs(p - b);
    const int pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return static_cast<uint8_t>(a);
    if (pb <= pc)             return static_cast<uint8_t>(b);
    return static_cast<uint8_t>(c);
}

// Self-contained decoder for Goxel's 64x64 8-bit RGBA PNG `BL16` chunks (4096 voxels = 16x16x16)
inline bool DecodeGox64x64RgbaPng(const uint8_t* png, size_t len, std::array<uint8_t, 4096>& outSolidMask) {
    outSolidMask.fill(0u);
    static constexpr uint8_t kPngSig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    if (!png || len < 57u || std::memcmp(png, kPngSig, 8u) != 0) return false;

    bool sawIhdr = false;
    bool sawIend = false;
    std::vector<uint8_t> idatStream;

    size_t pos = 8u;
    while (pos + 12u <= len) {
        const uint32_t cLen = ByteIO::RdU32BE(png + pos + 0u);
        const char*    cTag = reinterpret_cast<const char*>(png + pos + 4u);
        pos += 8u;
        if (cLen > len - pos || (len - pos) - cLen < 4u) return false;

        if (std::memcmp(cTag, "IHDR", 4) == 0) {
            if (cLen != 13u) return false;
            const uint32_t w        = ByteIO::RdU32BE(png + pos + 0u);
            const uint32_t h        = ByteIO::RdU32BE(png + pos + 4u);
            const uint8_t  bitDepth = png[pos + 8u];
            const uint8_t  colType  = png[pos + 9u];
            const uint8_t  interl   = png[pos + 12u];
            if (w != 64u || h != 64u || bitDepth != 8u || colType != 6u || interl != 0u) return false;
            sawIhdr = true;
        } else if (std::memcmp(cTag, "IDAT", 4) == 0) {
            if (idatStream.size() + cLen > (1u << 20)) return false;
            idatStream.insert(idatStream.end(), png + pos, png + pos + cLen);
        } else if (std::memcmp(cTag, "IEND", 4) == 0) {
            sawIend = true;
            pos += cLen + 4u;
            break;
        }
        pos += cLen + 4u;
    }
    if (!sawIhdr || !sawIend || idatStream.empty()) return false;

    constexpr size_t kRowBytes  = 64u * 4u;
    constexpr size_t kRawNeeded = 64u * (1u + kRowBytes);
    std::vector<uint8_t> raw;
    if (!ZipDeflate::InflateZlib(idatStream.data(), idatStream.size(), raw, kRawNeeded + 64u) ||
        raw.size() < kRawNeeded) {
        return false;
    }

    std::array<uint8_t, kRowBytes> prevRow{};
    std::array<uint8_t, kRowBytes> curRow{};
    for (size_t y = 0u; y < 64u; ++y) {
        const uint8_t* src = raw.data() + y * (1u + kRowBytes);
        const uint8_t  flt = src[0];
        const uint8_t* rowIn = src + 1u;
        if (flt > 4u) return false;

        for (size_t x = 0u; x < kRowBytes; ++x) {
            const uint8_t a = (x >= 4u) ? curRow[x - 4u] : 0u;
            const uint8_t b = prevRow[x];
            const uint8_t c = (x >= 4u) ? prevRow[x - 4u] : 0u;
            uint8_t recon = rowIn[x];
            if      (flt == 1u) recon = static_cast<uint8_t>(recon + a);
            else if (flt == 2u) recon = static_cast<uint8_t>(recon + b);
            else if (flt == 3u) recon = static_cast<uint8_t>(recon + ((static_cast<int>(a) + static_cast<int>(b)) >> 1));
            else if (flt == 4u) recon = static_cast<uint8_t>(recon + PaethPredictor(a, b, c));
            curRow[x] = recon;
        }
        for (size_t px = 0u; px < 64u; ++px) {
            const uint8_t alpha = curRow[px * 4u + 3u];
            outSolidMask[y * 64u + px] = (alpha > 0u) ? 1u : 0u;
        }
        prevRow = curRow;
    }
    return true;
}

} // namespace DetailGoxelGox

inline bool ParseGoxelGoxModel(const uint8_t* data, size_t size, LoadedMesh3D& outMesh) {
    if (!data || size < 24u || size > (64u << 20)) return false;
    if (std::memcmp(data, "GOX ", 4) != 0) return false;
    const int32_t version = ByteIO::RdI32LE(data + 4u);
    if (version != 2) return false;

    std::vector<std::array<uint8_t, 4096>> blocks;
    std::unordered_set<VoxelSurfaceMesh::VoxelCoord, VoxelSurfaceMesh::VoxelCoordHash> solidVoxels;

    size_t pos = 8u;
    bool sawLayer = false;
    while (pos < size) {
        if (size - pos < 12u) return false;
        const char*    tag  = reinterpret_cast<const char*>(data + pos);
        const uint32_t cLen = ByteIO::RdU32LE(data + pos + 4u);
        pos += 8u;
        if (cLen > size - pos || (size - pos) - cLen < 4u) return false;
        const uint8_t* cPtr = data + pos;

        if (std::memcmp(tag, "BL16", 4) == 0) {
            if (blocks.size() >= 8192u) return false;
            std::array<uint8_t, 4096> mask{};
            if (!DetailGoxelGox::DecodeGox64x64RgbaPng(cPtr, cLen, mask)) return false;
            blocks.push_back(mask);
        } else if (std::memcmp(tag, "LAYR", 4) == 0) {
            if (cLen < 4u) return false;
            const uint32_t numBlkRefs = ByteIO::RdU32LE(cPtr + 0u);
            if (numBlkRefs > 65536u || 4ULL + static_cast<uint64_t>(numBlkRefs) * 20ULL > cLen) return false;
            sawLayer = true;

            for (uint32_t bi = 0u; bi < numBlkRefs; ++bi) {
                const uint8_t* bp = cPtr + 4u + static_cast<size_t>(bi) * 20u;
                const uint32_t blkIdx = ByteIO::RdU32LE(bp + 0u);
                const int32_t  bx     = ByteIO::RdI32LE(bp + 4u);
                const int32_t  by     = ByteIO::RdI32LE(bp + 8u);
                const int32_t  bz     = ByteIO::RdI32LE(bp + 12u);
                if (blkIdx >= blocks.size()) return false;
                if (std::abs(bx) > 65536 || std::abs(by) > 65536 || std::abs(bz) > 65536) return false;

                const auto& mask = blocks[blkIdx];
                for (uint32_t v = 0u; v < 4096u; ++v) {
                    if (!mask[v]) continue;
                    const int32_t lx = static_cast<int32_t>(v & 15u);
                    const int32_t ly = static_cast<int32_t>((v >> 4u) & 15u);
                    const int32_t lz = static_cast<int32_t>((v >> 8u) & 15u);
                    if (solidVoxels.size() < 500000u) {
                        solidVoxels.insert({ bx + lx, by + ly, bz + lz });
                    }
                }
            }
        }
        pos += cLen + 4u; // skip payload + 4B CRC
    }

    if (!sawLayer || solidVoxels.empty()) return false;

    outMesh = LoadedMesh3D{};
    if (!VoxelSurfaceMesh::EmitCulledSparseVoxels(solidVoxels, outMesh)) {
        outMesh = {};
        return false;
    }

    outMesh.formatName = L"Goxel Voxel Model (.gox)";
    return !outMesh.vertices.empty() && !outMesh.tris.empty();
}

} // namespace Formats
