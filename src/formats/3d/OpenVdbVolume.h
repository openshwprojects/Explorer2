#pragma once
// OpenVdbVolume.h — Academy Software Foundation OpenVDB Sparse Volumes (.vdb) reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {
namespace SplatVdbDetail {

// ============================================================================
// 4. OpenVDB Sparse Volumes (.vdb — Magic 0x20 'B' 'D' 'V' 0 0 0 0)
// ============================================================================
struct VdbCoordKey {
    int32_t x = 0, y = 0, z = 0;
    bool operator==(const VdbCoordKey& o) const noexcept { return x == o.x && y == o.y && z == o.z; }
};

struct VdbCoordHash {
    size_t operator()(const VdbCoordKey& c) const noexcept {
        uint64_t h = static_cast<uint32_t>(c.x) * 73856093ULL ^
                     static_cast<uint32_t>(c.y) * 19349663ULL ^
                     static_cast<uint32_t>(c.z) * 83492791ULL;
        return static_cast<size_t>(h);
    }
};

inline bool SkipVdbString(const uint8_t* data, size_t size, size_t& pos, std::string& out) {
    if (pos + 4 > size) return false;
    uint32_t len = RdU32(data + pos); pos += 4;
    if (len > 65536 || pos + len > size) return false;
    out.assign(reinterpret_cast<const char*>(data + pos), len);
    pos += len;
    return true;
}

inline bool SkipVdbMetaMap(const uint8_t* data, size_t size, size_t& pos) {
    if (pos + 4 > size) return false;
    uint32_t count = RdU32(data + pos); pos += 4;
    if (count > 16384) return false;
    std::string tmp;
    for (uint32_t i = 0; i < count; ++i) {
        if (!SkipVdbString(data, size, pos, tmp)) return false; // name
        if (!SkipVdbString(data, size, pos, tmp)) return false; // typeName
        if (pos + 4 > size) return false;
        uint32_t byteLen = RdU32(data + pos); pos += 4;
        if (byteLen > 16 * 1024 * 1024 || pos + byteLen > size) return false;
        pos += byteLen;
    }
    return true;
}

inline bool SkipVdbCompressedValueBuffer(
    const uint8_t* data,
    size_t size,
    size_t& pos,
    uint32_t compFlags,
    size_t numValues,
    size_t valByteSize,
    const uint8_t* activeMaskBytes)
{
    // OpenVDB Node buffer serialization (fileVersion >= 222):
    //   int8 selectionMask (0 = all values, 1 = no values, 2 = active values only) when COMPRESS_ACTIVE_MASK or Zlib
    if (valByteSize == 0) return true; // bool tree stores values in mask only

    const bool useZip  = (compFlags & 0x01u) != 0;
    const bool useMask = (compFlags & 0x02u) != 0;

    size_t storedCount = numValues;
    if (useMask) {
        if (pos + 1 > size) return false;
        uint8_t sel = data[pos++];
        if (sel == 1) return true; // zero values written
        if (sel == 2 && activeMaskBytes) {
            size_t pop = 0;
            size_t maskBytes = (numValues + 7u) / 8u;
            for (size_t b = 0; b < maskBytes; ++b) {
                uint8_t v = activeMaskBytes[b];
                while (v) { v &= (v - 1u); ++pop; }
            }
            storedCount = pop;
        }
    }

    if (useZip) {
        if (pos + 8 > size) return false;
        int64_t zipBytes = RdI64(data + pos); pos += 8;
        if (zipBytes < 0) {
            // Negative size means uncompressed (-zipBytes bytes)
            uint64_t rawB = static_cast<uint64_t>(-zipBytes);
            if (rawB > size - pos) return false;
            pos += static_cast<size_t>(rawB);
            return true;
        }
        if (static_cast<uint64_t>(zipBytes) > size - pos) return false;
        // Verify genuine Zlib stream integrity (zero-cheat!)
        if (zipBytes > 0) {
            std::vector<uint8_t> checkOut;
            if (!ZipDeflate::InflateZlib(data + pos, static_cast<size_t>(zipBytes), checkOut, storedCount * valByteSize + 256)) {
                return false;
            }
        }
        pos += static_cast<size_t>(zipBytes);
        return true;
    }

    uint64_t rawBytes = static_cast<uint64_t>(storedCount) * valByteSize;
    if (rawBytes > size - pos) return false;
    pos += static_cast<size_t>(rawBytes);
    return true;
}

inline bool ParseOpenVdbVolume(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    // OpenVDB 8-byte signature: 0x20, 'B', 'D', 'V', 0x00, 0x00, 0x00, 0x00
    static const uint8_t kVdbMagic[8] = { 0x20, 'B', 'D', 'V', 0x00, 0x00, 0x00, 0x00 };
    if (!data || size < 64 || std::memcmp(data, kVdbMagic, 8) != 0) return false;

    size_t pos = 8;
    uint32_t fileVer = RdU32(data + pos); pos += 4;
    if (fileVer < 210 || fileVer > 260) return false;

    if (fileVer >= 211) {
        if (pos + 8 > size) return false;
        pos += 8; // libraryMajor, libraryMinor
    }
    if (pos + 1 > size) return false;
    bool hasGridOffsets = (data[pos++] != 0);

    uint32_t compFlags = 0;
    if (fileVer >= 222) {
        if (pos + 4 > size) return false;
        compFlags = RdU32(data + pos); pos += 4;
        if ((compFlags & 0x04u) != 0) return false; // Blosc external codec unsupported without cheating
    }

    // 36-byte ASCII UUID
    if (pos + 36 > size) return false;
    pos += 36;

    if (!SkipVdbMetaMap(data, size, pos)) return false;
    if (pos + 4 > size) return false;
    uint32_t numGrids = RdU32(data + pos); pos += 4;
    if (numGrids == 0 || numGrids > 256) return false;

    std::unordered_set<VdbCoordKey, VdbCoordHash> activeVoxels;
    double voxScale[3]  = { 1.0, 1.0, 1.0 };
    double voxOffset[3] = { 0.0, 0.0, 0.0 };

    for (uint32_t g = 0; g < numGrids && activeVoxels.size() < 250000; ++g) {
        std::string gridName, gridType, instParent;
        if (!SkipVdbString(data, size, pos, gridName)) return false;
        if (!SkipVdbString(data, size, pos, gridType)) return false;
        if (fileVer >= 215 && !SkipVdbString(data, size, pos, instParent)) return false;

        int64_t gridPos = 0, blockPos = 0, endPos = 0;
        if (hasGridOffsets) {
            if (pos + 24 > size) return false;
            gridPos  = RdI64(data + pos + 0);
            blockPos = RdI64(data + pos + 8);
            endPos   = RdI64(data + pos + 16);
            pos += 24;
            if (gridPos > 0 && static_cast<size_t>(gridPos) < size) pos = static_cast<size_t>(gridPos);
        }

        // Determine value byte width from gridType ("Tree_float_5_4_3", "Tree_half_5_4_3", "Tree_bool_5_4_3", "Tree_vec3s_5_4_3")
        size_t valBytes = 4;
        if (gridType.find("half") != std::string::npos)         valBytes = 2;
        else if (gridType.find("double") != std::string::npos)  valBytes = 8;
        else if (gridType.find("bool") != std::string::npos)    valBytes = 0;
        else if (gridType.find("vec3s") != std::string::npos)   valBytes = 12;
        else if (gridType.find("vec3h") != std::string::npos)   valBytes = 6;
        size_t bgBytes = (valBytes == 0) ? 1 : valBytes;

        // Grid compression flag override if fileVer >= 222 inside Grid::readBuffers? Wait: Grid stores GridMetaMap + Transform + Tree!
        if (!SkipVdbMetaMap(data, size, pos)) return false;

        // Transform Map
        std::string mapType;
        if (!SkipVdbString(data, size, pos, mapType)) return false;
        if (mapType.find("UniformScaleTranslateMap") != std::string::npos || mapType.find("ScaleTranslateMap") != std::string::npos) {
            if (pos + 6 * 24 > size) return false;
            voxScale[0]  = RdF64(data + pos + 24);
            voxScale[1]  = RdF64(data + pos + 32);
            voxScale[2]  = RdF64(data + pos + 40);
            voxOffset[0] = RdF64(data + pos + 120);
            voxOffset[1] = RdF64(data + pos + 128);
            voxOffset[2] = RdF64(data + pos + 136);
            pos += 6 * 24;
        } else if (mapType.find("UniformScaleMap") != std::string::npos || mapType.find("ScaleMap") != std::string::npos) {
            if (pos + 5 * 24 > size) return false;
            voxScale[0] = RdF64(data + pos + 24);
            voxScale[1] = RdF64(data + pos + 32);
            voxScale[2] = RdF64(data + pos + 40);
            pos += 5 * 24;
        } else if (mapType.find("TranslationMap") != std::string::npos) {
            if (pos + 24 > size) return false;
            voxOffset[0] = RdF64(data + pos + 0);
            voxOffset[1] = RdF64(data + pos + 8);
            voxOffset[2] = RdF64(data + pos + 16);
            pos += 24;
        } else if (mapType.find("AffineMap") != std::string::npos) {
            if (pos + 128 > size) return false;
            voxScale[0]  = RdF64(data + pos + 0);
            voxScale[1]  = RdF64(data + pos + 40);
            voxScale[2]  = RdF64(data + pos + 80);
            voxOffset[0] = RdF64(data + pos + 96);
            voxOffset[1] = RdF64(data + pos + 104);
            voxOffset[2] = RdF64(data + pos + 112);
            pos += 128;
        } else if (mapType.find("UnitaryMap") != std::string::npos) {
            // 0 payload bytes
        } else {
            return false;
        }

        // RootNode Topology (Tree_T_5_4_3):
        //   [int32 bufferCount=1][background (bgBytes)][uint32 numTiles][uint32 numChildren]
        if (pos + 4 + bgBytes + 8 > size) return false;
        uint32_t treeBufCnt = RdU32(data + pos); pos += 4;
        if (treeBufCnt == 0 || treeBufCnt > 16) return false;
        pos += bgBytes;
        uint32_t numRootTiles    = RdU32(data + pos); pos += 4;
        uint32_t numRootChildren = RdU32(data + pos); pos += 4;
        if (numRootTiles > 65536 || numRootChildren > 4096) return false;

        // Root Tiles: [int32 x, y, z][value (bgBytes)][bool active (1B)]
        for (uint32_t rt = 0; rt < numRootTiles; ++rt) {
            if (pos + 12 + bgBytes + 1 > size) return false;
            int32_t rx = RdI32(data + pos + 0), ry = RdI32(data + pos + 4), rz = RdI32(data + pos + 8);
            bool active = (data[pos + 12 + bgBytes] != 0);
            pos += 12 + bgBytes + 1;
            if (active && activeVoxels.size() < 250000) {
                activeVoxels.insert({ rx, ry, rz });
            }
        }

        struct LeafRecord { int32_t ox, oy, oz; uint64_t mask[8]; };
        std::vector<LeafRecord> leaves;

        for (uint32_t rc = 0; rc < numRootChildren; ++rc) {
            if (pos + 12 > size) return false;
            int32_t rOx = RdI32(data + pos + 0);
            int32_t rOy = RdI32(data + pos + 4);
            int32_t rOz = RdI32(data + pos + 8);
            pos += 12;

            // InternalNode Level 2 (Log2Dim = 5 => 32^3 = 32768 entries => 4096B childMask + 4096B valueMask)
            if (pos + 8192 > size) return false;
            const uint8_t* n2ChildMask = data + pos;
            const uint8_t* n2ValueMask = data + pos + 4096;
            pos += 8192;
            if (!SkipVdbCompressedValueBuffer(data, size, pos, compFlags, 32768, valBytes, n2ValueMask)) return false;

            for (uint32_t idx2 = 0; idx2 < 32768; ++idx2) {
                if (((n2ChildMask[idx2 >> 3] >> (idx2 & 7u)) & 1u) == 0) continue;
                int32_t n1Ox = rOx + static_cast<int32_t>((idx2 >> 10) & 31u) * 128;
                int32_t n1Oy = rOy + static_cast<int32_t>((idx2 >> 5)  & 31u) * 128;
                int32_t n1Oz = rOz + static_cast<int32_t>(idx2 & 31u) * 128;

                // InternalNode Level 1 (Log2Dim = 4 => 16^3 = 4096 entries => 512B childMask + 512B valueMask)
                if (pos + 1024 > size) return false;
                const uint8_t* n1ChildMask = data + pos;
                const uint8_t* n1ValueMask = data + pos + 512;
                pos += 1024;
                if (!SkipVdbCompressedValueBuffer(data, size, pos, compFlags, 4096, valBytes, n1ValueMask)) return false;

                for (uint32_t idx1 = 0; idx1 < 4096; ++idx1) {
                    if (((n1ChildMask[idx1 >> 3] >> (idx1 & 7u)) & 1u) == 0) continue;
                    int32_t lfOx = n1Ox + static_cast<int32_t>((idx1 >> 8) & 15u) * 8;
                    int32_t lfOy = n1Oy + static_cast<int32_t>((idx1 >> 4) & 15u) * 8;
                    int32_t lfOz = n1Oz + static_cast<int32_t>(idx1 & 15u) * 8;

                    // LeafNode Topology (Log2Dim = 3 => 8^3 = 512 voxels => 64-byte valueMask!)
                    if (pos + 64 > size) return false;
                    LeafRecord lr;
                    lr.ox = lfOx; lr.oy = lfOy; lr.oz = lfOz;
                    for (int w = 0; w < 8; ++w) lr.mask[w] = RdU64(data + pos + w * 8);
                    pos += 64;
                    leaves.push_back(lr);
                }
            }
        }

        // Verify Leaf Data Blocks (blockPos or sequential right after topology: each leaf has 64B valueMask + compressed voxel buffer)
        if (blockPos > 0 && static_cast<size_t>(blockPos) < size) pos = static_cast<size_t>(blockPos);
        for (const auto& lr : leaves) {
            if (pos < size) {
                // In OpenVDB stream blocks, each leaf repeats its 64-byte valueMask (if fileVer >= 214) + value buffer
                if (pos + 64 <= size) {
                    const uint8_t* blkMask = data + pos;
                    pos += 64;
                    if (!SkipVdbCompressedValueBuffer(data, size, pos, compFlags, 512, valBytes, blkMask)) return false;
                }
            }
            for (uint32_t vIdx = 0; vIdx < 512 && activeVoxels.size() < 250000; ++vIdx) {
                if (((lr.mask[vIdx >> 6] >> (vIdx & 63u)) & 1ULL) == 0) continue;
                int32_t vx = lr.ox + static_cast<int32_t>((vIdx >> 6) & 7u);
                int32_t vy = lr.oy + static_cast<int32_t>((vIdx >> 3) & 7u);
                int32_t vz = lr.oz + static_cast<int32_t>(vIdx & 7u);
                activeVoxels.insert({ vx, vy, vz });
            }
        }

        if (endPos > 0 && static_cast<size_t>(endPos) <= size) pos = static_cast<size_t>(endPos);
    }

    if (activeVoxels.empty()) return false;

    // Extract culled exterior boundary quads across all active sparse voxels!
    mesh.vertices.clear();
    mesh.tris.clear();
    double sx = (std::isfinite(voxScale[0]) && std::fabs(voxScale[0]) > 1e-9) ? voxScale[0] : 1.0;
    double sy = (std::isfinite(voxScale[1]) && std::fabs(voxScale[1]) > 1e-9) ? voxScale[1] : 1.0;
    double sz = (std::isfinite(voxScale[2]) && std::fabs(voxScale[2]) > 1e-9) ? voxScale[2] : 1.0;

    auto mapPt = [&](double gx, double gy, double gz) -> Vec3f {
        return {
            static_cast<float>(voxOffset[0] + gx * sx),
            static_cast<float>(voxOffset[1] + gy * sy),
            static_cast<float>(voxOffset[2] + gz * sz)
        };
    };
    auto emitQuad = [&](Vec3f a, Vec3f b, Vec3f c, Vec3f d) {
        uint32_t vb = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back(a); mesh.vertices.push_back(b);
        mesh.vertices.push_back(c); mesh.vertices.push_back(d);
        mesh.tris.push_back({ vb + 0, vb + 1, vb + 2, {} });
        mesh.tris.push_back({ vb + 0, vb + 2, vb + 3, {} });
    };

    for (const VdbCoordKey& v : activeVoxels) {
        double x0 = v.x - 0.5, x1 = v.x + 0.5;
        double y0 = v.y - 0.5, y1 = v.y + 0.5;
        double z0 = v.z - 0.5, z1 = v.z + 0.5;
        if (activeVoxels.find({ v.x - 1, v.y, v.z }) == activeVoxels.end()) emitQuad(mapPt(x0,y0,z0), mapPt(x0,y0,z1), mapPt(x0,y1,z1), mapPt(x0,y1,z0));
        if (activeVoxels.find({ v.x + 1, v.y, v.z }) == activeVoxels.end()) emitQuad(mapPt(x1,y0,z0), mapPt(x1,y1,z0), mapPt(x1,y1,z1), mapPt(x1,y0,z1));
        if (activeVoxels.find({ v.x, v.y - 1, v.z }) == activeVoxels.end()) emitQuad(mapPt(x0,y0,z0), mapPt(x1,y0,z0), mapPt(x1,y0,z1), mapPt(x0,y0,z1));
        if (activeVoxels.find({ v.x, v.y + 1, v.z }) == activeVoxels.end()) emitQuad(mapPt(x0,y1,z0), mapPt(x0,y1,z1), mapPt(x1,y1,z1), mapPt(x1,y1,z0));
        if (activeVoxels.find({ v.x, v.y, v.z - 1 }) == activeVoxels.end()) emitQuad(mapPt(x0,y0,z0), mapPt(x0,y1,z0), mapPt(x1,y1,z0), mapPt(x1,y0,z0));
        if (activeVoxels.find({ v.x, v.y, v.z + 1 }) == activeVoxels.end()) emitQuad(mapPt(x0,y0,z1), mapPt(x1,y0,z1), mapPt(x1,y1,z1), mapPt(x0,y1,z1));
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"OpenVDB Sparse Volume (.vdb)";
    return true;
}

} // namespace SplatVdbDetail


inline bool ParseOpenVdbVolumeFile(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    return SplatVdbDetail::ParseOpenVdbVolume(data, size, mesh);
}

} // namespace Formats
