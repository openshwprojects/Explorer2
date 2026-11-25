#pragma once
// MinetestSchematicMts.h — Minetest / Luanti Engine Schematic ("MTSM" v1–v4 .mts) Voxel Reader
// Parses big-endian MTSM header, sizeX/sizeY/sizeZ, Y-slice probabilities, node-name string table
// (filtering "air" & "ignore"), and zlib-compressed 16-bit BE param0 volume into a culled 6-neighbor voxel mesh.
#include "../../ByteIO.h"
#include "../../ReaderTextUtils.h"
#include "../../ZipDeflate.h"
#include "../common/VoxelMesh3D.h"

namespace Formats {

inline bool ParseMinetestSchematicMts(
    const uint8_t* data,
    size_t size,
    const std::wstring& /*fullPath*/,
    LoadedMesh3D& mesh)
{
    if (!data || size < 16) return false;
    if (std::memcmp(data, "MTSM", 4) != 0) return false;

    const uint16_t version = ByteIO::RdU16BE(data + 4);
    if (version < 1 || version > 4) return false;

    const uint16_t sx = ByteIO::RdU16BE(data + 6);
    const uint16_t sy = ByteIO::RdU16BE(data + 8);
    const uint16_t sz = ByteIO::RdU16BE(data + 10);
    if (sx == 0 || sy == 0 || sz == 0) return false;

    const uint64_t nodeCount = static_cast<uint64_t>(sx) * sy * sz;
    if (nodeCount > 16777216ULL) return false;

    size_t pos = 12u;
    if (version >= 3) {
        if (pos + sy > size) return false;
        pos += sy; // Y-slice probability table
    }

    if (pos + 2 > size) return false;
    const uint16_t nameCount = ByteIO::RdU16BE(data + pos);
    pos += 2;
    if (nameCount == 0 || nameCount > 32768) return false;

    std::vector<uint8_t> isSolidId(nameCount, 1u);
    for (uint16_t i = 0; i < nameCount; ++i) {
        if (pos + 2 > size) return false;
        uint16_t nLen = ByteIO::RdU16BE(data + pos);
        pos += 2;
        if (nLen > size - pos) return false;
        std::string low = ReaderText::ToLowerAscii(
            ReaderText::TrimSv(std::string_view(reinterpret_cast<const char*>(data + pos), nLen)));
        pos += nLen;
        if (low.empty() || low == "air" || low == "ignore" || low == "env_air" ||
            (low.size() > 4 && low.compare(low.size() - 4, 4, ":air") == 0))
        {
            isSolidId[i] = 0u;
        }
    }

    if (pos >= size) return false;
    const size_t expectedRaw = static_cast<size_t>(nodeCount) * 4u; // u16 param0[N] + u8 param1[N] + u8 param2[N]
    const size_t maxOut = std::min<size_t>(std::max<size_t>(expectedRaw + 4096u, 65536u), 128u * 1024u * 1024u);

    std::vector<uint8_t> rawNodes;
    if (!ZipDeflate::InflateZlib(data + pos, size - pos, rawNodes, maxOut)) {
        return false;
    }
    if (rawNodes.size() < static_cast<size_t>(nodeCount) * 2u) return false;

    const bool hasParam1 = (version == 4 && rawNodes.size() >= static_cast<size_t>(nodeCount) * 3u);
    const uint8_t* p1Base = hasParam1 ? (rawNodes.data() + static_cast<size_t>(nodeCount) * 2u) : nullptr;
    bool anyNonZeroProb = false;
    if (p1Base) {
        for (size_t i = 0; i < static_cast<size_t>(nodeCount); ++i) {
            if ((p1Base[i] & 0x7Fu) != 0u) { anyNonZeroProb = true; break; }
        }
    }

    std::vector<VoxelMesh3D::VoxelCoord> voxels;
    voxels.reserve(std::min<size_t>(static_cast<size_t>(nodeCount), 262144u));

    size_t idx = 0;
    for (uint16_t iz = 0; iz < sz; ++iz) {
        for (uint16_t iy = 0; iy < sy; ++iy) {
            for (uint16_t ix = 0; ix < sx; ++ix, ++idx) {
                uint16_t nid = ByteIO::RdU16BE(rawNodes.data() + idx * 2u);
                if (nid >= nameCount || !isSolidId[nid]) continue;
                if (anyNonZeroProb && (p1Base[idx] & 0x7Fu) == 0u) continue;
                voxels.push_back({ static_cast<int32_t>(ix), static_cast<int32_t>(iy), static_cast<int32_t>(iz) });
            }
        }
    }

    mesh.vertices.clear();
    mesh.tris.clear();
    if (!VoxelMesh3D::EmitCulledSparseVoxels(voxels, mesh)) return false;
    mesh.formatName = L"Minetest / Luanti Voxel Schematic (.mts)";
    return true;
}

} // namespace Formats
