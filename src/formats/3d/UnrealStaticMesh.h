#pragma once
// UnrealStaticMesh.h — Unreal Engine 4/5 StaticMesh 3D asset reader (.uasset).
#include "../common/UnrealPackageCommon.h"

namespace Formats {
inline bool ParseUnrealStaticMeshPackage(
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    LoadedMesh3D& mesh)
{
    using namespace UnrealPkgDetail;
    ParsedUnrealSummary sum;
    if (!ParseUnrealPackageSummary(data, size, sum)) return false;
    if (!ContainsNameCI(sum, "StaticMesh") && !ContainsNameCI(sum, "World") && !ContainsNameCI(sum, "Level")) {
        return false;
    }

    std::vector<uint8_t> combined, ubulk;
    LoadCombinedUnrealStreams(data, size, sum, fullPath, combined, ubulk);
    const size_t bodyStart = (sum.totalHeaderSize > 0 && static_cast<size_t>(sum.totalHeaderSize) < combined.size())
        ? static_cast<size_t>(sum.totalHeaderSize)
        : 64u;
    if (bodyStart + 28 > combined.size()) return false;

    // Scan serialized export payload for UE4/UE5 FStaticMeshLODResources:
    //   PositionVertexBuffer: uint32 Stride (12 = float3, 24 = UE5 LWC double3), uint32 NumVertices,
    //                         int32 BulkElemSize (= Stride), int32 BulkElemCount (= NumVertices), [NumVertices * Stride bytes]
    //   Followed within the same LOD resource by FRawStaticIndexBuffer:
    //     uint32 b32Bit (0 or 1), int32 BulkElemSize (1, 2, or 4), int32 BulkElemCount, [index bytes]
    const uint8_t* buf = combined.data();
    const size_t totalSz = combined.size();
    size_t scan = bodyStart;

    while (scan + 28 <= totalSz) {
        uint32_t stride   = UeU32(buf + scan + 0);
        uint32_t numVerts = UeU32(buf + scan + 4);
        if ((stride != 12u && stride != 24u) || numVerts < 3u || numVerts > 1500000u) {
            scan += 1;
            continue;
        }
        int32_t bulkElemSz  = UeI32(buf + scan + 8);
        int32_t bulkElemCnt = UeI32(buf + scan + 12);
        uint64_t posBytes = static_cast<uint64_t>(numVerts) * stride;
        if (static_cast<uint32_t>(bulkElemSz) != stride ||
            static_cast<uint32_t>(bulkElemCnt) != numVerts ||
            scan + 16ULL + posBytes + 12ULL > totalSz)
        {
            scan += 1;
            continue;
        }

        // Validate all vertex coordinates are finite and non-degenerate
        const uint8_t* vBase = buf + scan + 16;
        bool vertsValid = true;
        float minX = 1e30f, maxX = -1e30f, minY = 1e30f, maxY = -1e30f, minZ = 1e30f, maxZ = -1e30f;
        std::vector<Vec3f> candVerts;
        candVerts.reserve(numVerts);
        for (uint32_t v = 0; v < numVerts; ++v) {
            float vx = 0.0f, vy = 0.0f, vz = 0.0f;
            if (stride == 12u) {
                const uint8_t* vp = vBase + static_cast<size_t>(v) * 12;
                vx = UeF32(vp + 0); vy = UeF32(vp + 4); vz = UeF32(vp + 8);
            } else {
                const uint8_t* vp = vBase + static_cast<size_t>(v) * 24;
                vx = static_cast<float>(UeF64(vp + 0));
                vy = static_cast<float>(UeF64(vp + 8));
                vz = static_cast<float>(UeF64(vp + 16));
            }
            if (!std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(vz) ||
                std::fabs(vx) > 1e8f || std::fabs(vy) > 1e8f || std::fabs(vz) > 1e8f) {
                vertsValid = false;
                break;
            }
            minX = std::min(minX, vx); maxX = std::max(maxX, vx);
            minY = std::min(minY, vy); maxY = std::max(maxY, vy);
            minZ = std::min(minZ, vz); maxZ = std::max(maxZ, vz);
            // Unreal Z-up centimeters -> Y-up viewport
            candVerts.push_back({ vx * 0.01f, vz * 0.01f, -vy * 0.01f });
        }
        if (!vertsValid || ((maxX - minX) + (maxY - minY) + (maxZ - minZ) < 1e-5f)) {
            scan += 1;
            continue;
        }

        // Search forward after PositionVertexBuffer (skipping StaticMeshVertexBuffer & ColorVertexBuffer) for FRawStaticIndexBuffer
        size_t afterPos = scan + 16 + static_cast<size_t>(posBytes);
        size_t maxIdxSearch = std::min<size_t>(totalSz, afterPos + static_cast<size_t>(numVerts) * 64u + 512u);
        bool matchedIndexBuf = false;
        size_t idxEndPos = afterPos;

        for (size_t ip = afterPos; ip + 12 <= maxIdxSearch; ip += 2) {
            uint32_t is32Bit = UeU32(buf + ip + 0);
            if (is32Bit > 1u) continue;
            int32_t iElemSz  = UeI32(buf + ip + 4);
            int32_t iElemCnt = UeI32(buf + ip + 8);
            if (iElemCnt <= 0) continue;

            uint64_t idxByteLen = 0;
            uint32_t idxCount = 0;
            if (is32Bit == 0) {
                if (iElemSz == 2) { idxCount = static_cast<uint32_t>(iElemCnt); idxByteLen = static_cast<uint64_t>(idxCount) * 2ULL; }
                else if (iElemSz == 1 && (iElemCnt % 2) == 0) { idxCount = static_cast<uint32_t>(iElemCnt / 2); idxByteLen = static_cast<uint64_t>(iElemCnt); }
                else continue;
            } else {
                if (iElemSz == 4) { idxCount = static_cast<uint32_t>(iElemCnt); idxByteLen = static_cast<uint64_t>(idxCount) * 4ULL; }
                else if (iElemSz == 1 && (iElemCnt % 4) == 0) { idxCount = static_cast<uint32_t>(iElemCnt / 4); idxByteLen = static_cast<uint64_t>(iElemCnt); }
                else continue;
            }

            if (idxCount < 3u || (idxCount % 3u) != 0 || idxCount > 4000000u || ip + 12ULL + idxByteLen > totalSz) {
                continue;
            }

            const uint8_t* iBase = buf + ip + 12;
            bool allInRange = true;
            std::vector<std::array<uint32_t, 3>> candTris;
            candTris.reserve(idxCount / 3u);
            for (uint32_t t = 0; t + 2 < idxCount; t += 3) {
                uint32_t i0 = is32Bit ? UeU32(iBase + (t + 0) * 4) : UeU16(iBase + (t + 0) * 2);
                uint32_t i1 = is32Bit ? UeU32(iBase + (t + 1) * 4) : UeU16(iBase + (t + 1) * 2);
                uint32_t i2 = is32Bit ? UeU32(iBase + (t + 2) * 4) : UeU16(iBase + (t + 2) * 2);
                if (i0 >= numVerts || i1 >= numVerts || i2 >= numVerts) {
                    allInRange = false;
                    break;
                }
                if (i0 != i1 && i1 != i2 && i2 != i0) {
                    candTris.push_back({ i0, i1, i2 });
                }
            }
            if (allInRange && !candTris.empty()) {
                uint32_t baseV = static_cast<uint32_t>(mesh.vertices.size());
                mesh.vertices.insert(mesh.vertices.end(), candVerts.begin(), candVerts.end());
                for (const auto& ct : candTris) {
                    mesh.tris.push_back({ baseV + ct[0], baseV + ct[1], baseV + ct[2], {} });
                }
                matchedIndexBuf = true;
                idxEndPos = ip + 12 + static_cast<size_t>(idxByteLen);
                break;
            }
        }

        if (matchedIndexBuf) {
            scan = idxEndPos;
            // For single StaticMesh .uasset, take LOD0 only (don't stack lower LODs on top of LOD0!)
            if (ContainsNameCI(sum, "StaticMesh") && !ContainsNameCI(sum, "World")) {
                break;
            }
        } else {
            scan += 1;
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = (sum.verUe5 > 0)
        ? L"Unreal Engine 5 UStaticMesh (.uasset)"
        : L"Unreal Engine 4 UStaticMesh (.uasset)";
    return true;
}

} // namespace Formats
