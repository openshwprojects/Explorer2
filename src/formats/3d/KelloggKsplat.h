#pragma once
// KelloggKsplat.h — Mark Kellogg GaussianSplats3D (.ksplat) reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {
namespace SplatVdbDetail {

// ============================================================================
// 2. Mark Kellogg GaussianSplats3D .ksplat (4096B Header + 1024B Sections)
// ============================================================================
inline bool ParseKelloggKsplat(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    if (!data || size < 5120) return false;
    uint8_t maj = data[0];
    uint8_t min = data[1];
    if (maj != 0 || (min != 1 && min != 2)) return false;

    uint32_t maxSections = RdU32(data + 4);
    uint32_t totalSplats = RdU32(data + 16);
    uint16_t compLevel   = RdU16(data + 20);
    if (maxSections == 0 || maxSections > 4096 || totalSplats == 0 || totalSplats > 10000000 || compLevel > 2) {
        return false;
    }

    size_t dataPos = 4096ULL + static_cast<size_t>(maxSections) * 1024ULL;
    if (dataPos >= size) return false;

    static const size_t kBaseBytesPerSplat[3] = { 44, 24, 24 };
    static const uint32_t kShComponents[4]    = { 0, 9, 24, 45 };

    std::vector<GaussianPrim> splats;
    splats.reserve(std::min<size_t>(totalSplats, 100000));

    size_t secCursor = dataPos;
    uint32_t parsedSplats = 0;

    for (uint32_t s = 0; s < maxSections && parsedSplats < totalSplats && secCursor < size; ++s) {
        const uint8_t* shdr = data + 4096 + static_cast<size_t>(s) * 1024;
        uint32_t secCount       = RdU32(shdr + 0);
        uint32_t bucketCap      = RdU32(shdr + 8);
        uint32_t bucketCount    = RdU32(shdr + 12);
        float    bucketBlockSz  = RdF32(shdr + 16);
        uint16_t bucketMetaSz   = RdU16(shdr + 20);
        uint32_t storageSize    = RdU32(shdr + 24);
        uint32_t compScaleRange = RdU32(shdr + 28);
        uint32_t fullBuckets    = RdU32(shdr + 36);
        uint32_t partBuckets    = RdU32(shdr + 40);
        uint16_t shDegree       = RdU16(shdr + 44);
        if (shDegree > 3) shDegree = 0;
        if (secCount == 0) continue;

        size_t shBytes = 0;
        if (compLevel == 0)      shBytes = static_cast<size_t>(kShComponents[shDegree]) * 4u;
        else if (compLevel == 1) shBytes = static_cast<size_t>(kShComponents[shDegree]) * 2u;
        else                     shBytes = static_cast<size_t>(kShComponents[shDegree]) * 1u;
        size_t bytesPerSplat = kBaseBytesPerSplat[compLevel] + shBytes;

        if (compLevel == 0) {
            if (secCursor + static_cast<uint64_t>(secCount) * bytesPerSplat > size) return false;
            for (uint32_t i = 0; i < secCount; ++i) {
                const uint8_t* sp = data + secCursor + static_cast<size_t>(i) * bytesPerSplat;
                GaussianPrim g;
                g.pos   = { RdF32(sp + 0),  RdF32(sp + 4),  RdF32(sp + 8) };
                g.scale = { RdF32(sp + 12), RdF32(sp + 16), RdF32(sp + 20) };
                g.qw    = RdF32(sp + 24);
                g.qx    = RdF32(sp + 28);
                g.qy    = RdF32(sp + 32);
                g.qz    = RdF32(sp + 36);
                g.alpha = sp[43];
                if (std::isfinite(g.pos.x) && std::isfinite(g.pos.y) && std::isfinite(g.pos.z) &&
                    std::isfinite(g.scale.x) && g.scale.x > 0.0f && g.scale.y > 0.0f && g.scale.z > 0.0f) {
                    splats.push_back(g);
                }
            }
            secCursor += static_cast<size_t>(secCount) * bytesPerSplat;
        } else {
            // Level 1 / 2: optional partial-bucket counts (partBuckets * 4) + bucket centers (bucketCount * 12) + splats
            size_t partMetaBytes = static_cast<size_t>(partBuckets) * 4u;
            size_t bucketsBytes  = static_cast<size_t>(bucketCount) * 12u;
            if (bucketMetaSz > 0 && bucketCount > 0) bucketsBytes = static_cast<size_t>(bucketCount) * bucketMetaSz;
            if (secCursor + partMetaBytes + bucketsBytes + static_cast<uint64_t>(secCount) * bytesPerSplat > size) return false;

            const uint8_t* partCountsPtr = data + secCursor;
            const uint8_t* bucketCenters = data + secCursor + partMetaBytes;
            const uint8_t* splatBase     = bucketCenters + bucketsBytes;

            float scaleFactor = (compScaleRange > 0) ? (bucketBlockSz * 0.5f / static_cast<float>(compScaleRange)) : (1.0f / 32767.0f);
            if (!std::isfinite(scaleFactor) || scaleFactor <= 0.0f) scaleFactor = 1.0f / 32767.0f;
            if (bucketCap == 0) bucketCap = 256;

            uint32_t curBucket = 0;
            uint32_t inBucketIdx = 0;
            uint32_t curBucketLimit = (fullBuckets > 0) ? bucketCap : ((partBuckets > 0) ? RdU32(partCountsPtr) : secCount);

            for (uint32_t i = 0; i < secCount; ++i) {
                while (curBucket < bucketCount && inBucketIdx >= curBucketLimit) {
                    ++curBucket;
                    inBucketIdx = 0;
                    if (curBucket < fullBuckets) curBucketLimit = bucketCap;
                    else if (curBucket - fullBuckets < partBuckets) curBucketLimit = RdU32(partCountsPtr + (curBucket - fullBuckets) * 4u);
                    else curBucketLimit = secCount;
                }
                Vec3f bCenter = { 0, 0, 0 };
                if (curBucket < bucketCount) {
                    const uint8_t* bc = bucketCenters + static_cast<size_t>(curBucket) * 12u;
                    bCenter = { RdF32(bc + 0), RdF32(bc + 4), RdF32(bc + 8) };
                }
                ++inBucketIdx;

                const uint8_t* sp = splatBase + static_cast<size_t>(i) * bytesPerSplat;
                float dx = (static_cast<float>(RdU16(sp + 0)) - static_cast<float>(compScaleRange)) * scaleFactor;
                float dy = (static_cast<float>(RdU16(sp + 2)) - static_cast<float>(compScaleRange)) * scaleFactor;
                float dz = (static_cast<float>(RdU16(sp + 4)) - static_cast<float>(compScaleRange)) * scaleFactor;

                GaussianPrim g;
                g.pos   = { bCenter.x + dx, bCenter.y + dy, bCenter.z + dz };
                g.scale = { HalfToF32Splat(RdU16(sp + 6)), HalfToF32Splat(RdU16(sp + 8)), HalfToF32Splat(RdU16(sp + 10)) };
                g.qw    = HalfToF32Splat(RdU16(sp + 12));
                g.qx    = HalfToF32Splat(RdU16(sp + 14));
                g.qy    = HalfToF32Splat(RdU16(sp + 16));
                g.qz    = HalfToF32Splat(RdU16(sp + 18));
                g.alpha = sp[23];
                if (std::isfinite(g.pos.x) && std::isfinite(g.scale.x) && g.scale.x > 0.0f && g.scale.y > 0.0f && g.scale.z > 0.0f) {
                    splats.push_back(g);
                }
            }
            secCursor += (storageSize > 0) ? storageSize : (partMetaBytes + bucketsBytes + static_cast<size_t>(secCount) * bytesPerSplat);
        }
        parsedSplats += secCount;
    }

    mesh.vertices.clear();
    mesh.tris.clear();
    EmitGaussianEllipsoids(splats, mesh);
    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"Kellogg 3D Gaussian Splats (.ksplat)";
    return true;
}

} // namespace SplatVdbDetail

inline bool ParseKelloggKsplatFile(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    return SplatVdbDetail::ParseKelloggKsplat(data, size, mesh);
}

} // namespace Formats
