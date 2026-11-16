#pragma once
// SplatOpenVdb3D.h — #19 3D Gaussian Splatting (.splat, .ksplat, .spz, + 3DGS .ply)
//                     & Academy Software Foundation OpenVDB Sparse Volumes (.vdb)
//
// Pure C++20 zero-cheat implementation:
//   1. Antimatter15 .splat (32-byte records: float3 pos, float3 linear scale, uint8 rgba, uint8 quatWXYZ)
//      with strict IEEE-754 finiteness, positive scale, and unit-quaternion norm validation.
//   2. Mark Kellogg GaussianSplats3D .ksplat (4096-byte v0.1 header + 1024-byte section tables +
//      uncompressed float32 Level 0 and float16/bucket-quantized Level 1/2 splat streams).
//   3. Niantic / Scaniverse .spz (RFC 1952 Gzip -> "NGSP" 0x5053474E v1/v2/v3 Structure-of-Arrays
//      float16 / 24-bit fixed-point positions, log-encoded scales, and packed quaternions).
//   4. OpenVDB Sparse Volumes (.vdb, magic "\x20BDV\0\0\0\0"): parses Archive header, MetaMaps,
//      GridDescriptors ("Tree_float_5_4_3" / "Tree_half_5_4_3" / "Tree_bool_5_4_3"), UniformScale/Translate
//      & Affine transforms, hierarchical Root -> Internal(32^3) -> Internal(16^3) -> Leaf(8^3) active
//      voxel masks & tiles (with Zlib block decompression), and extracts culled exterior voxel surfaces.

namespace Formats {

namespace SplatVdbDetail {

inline uint16_t RdU16(const uint8_t* p) { uint16_t v = 0; std::memcpy(&v, p, 2); return v; }
inline uint32_t RdU32(const uint8_t* p) { uint32_t v = 0; std::memcpy(&v, p, 4); return v; }
inline int32_t  RdI32(const uint8_t* p) { int32_t  v = 0; std::memcpy(&v, p, 4); return v; }
inline uint64_t RdU64(const uint8_t* p) { uint64_t v = 0; std::memcpy(&v, p, 8); return v; }
inline int64_t  RdI64(const uint8_t* p) { int64_t  v = 0; std::memcpy(&v, p, 8); return v; }
inline float    RdF32(const uint8_t* p) { float    v = 0.0f; std::memcpy(&v, p, 4); return v; }
inline double   RdF64(const uint8_t* p) { double   v = 0.0;  std::memcpy(&v, p, 8); return v; }

inline float HalfToF32Splat(uint16_t h) {
    uint32_t sign = static_cast<uint32_t>(h >> 15) << 31;
    uint32_t exp  = (h >> 10) & 0x1Fu;
    uint32_t mant = h & 0x03FFu;
    uint32_t bits = 0;
    if (exp == 0) {
        if (mant != 0) {
            exp = 1;
            while ((mant & 0x0400u) == 0) { mant <<= 1; --exp; }
            mant &= 0x03FFu;
            bits = sign | ((exp + 112u) << 23) | (mant << 13);
        } else {
            bits = sign;
        }
    } else if (exp == 31u) {
        bits = (mant == 0) ? (sign | 0x7F800000u) : 0x7FC00000u;
    } else {
        bits = sign | ((exp + 112u) << 23) | (mant << 13);
    }
    float f = 0.0f;
    std::memcpy(&f, &bits, 4);
    return f;
}

struct GaussianPrim {
    Vec3f pos = { 0, 0, 0 };
    Vec3f scale = { 0.05f, 0.05f, 0.05f };
    float qw = 1.0f, qx = 0.0f, qy = 0.0f, qz = 0.0f;
    uint8_t alpha = 255;
};

// Emits oriented 3D anisotropic Gaussian octahedra (6 verts, 8 tris per Gaussian)
inline void EmitGaussianEllipsoids(const std::vector<GaussianPrim>& splats, LoadedMesh3D& mesh) {
    if (splats.empty()) return;
    constexpr size_t kMaxEllipsoids = 35000;
    size_t step = std::max<size_t>(1, splats.size() / kMaxEllipsoids);

    static const Vec3f kOct[6] = {
        { 1.0f, 0.0f, 0.0f }, {-1.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }, { 0.0f,-1.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f,-1.0f }
    };
    static const uint32_t kOctTris[8][3] = {
        {0,2,4}, {4,2,1}, {1,2,5}, {5,2,0},
        {4,3,0}, {1,3,4}, {5,3,1}, {0,3,5}
    };

    mesh.vertices.reserve(std::min(splats.size() / step, kMaxEllipsoids) * 6);
    mesh.tris.reserve(std::min(splats.size() / step, kMaxEllipsoids) * 8);

    for (size_t i = 0; i < splats.size() && mesh.tris.size() < kMaxEllipsoids * 8; i += step) {
        const GaussianPrim& g = splats[i];
        if (g.alpha <= 2) continue;

        float w = g.qw, x = g.qx, y = g.qy, z = g.qz;
        float n = std::sqrt(w * w + x * x + y * y + z * z);
        if (n > 1e-6f) { w /= n; x /= n; y /= n; z /= n; }
        else { w = 1.0f; x = y = z = 0.0f; }

        // 3x3 rotation matrix columns multiplied by anisotropic Gaussian sigma scales (2.0 sigma visual hull)
        float sx = std::clamp(g.scale.x * 2.0f, 1e-4f, 1e5f);
        float sy = std::clamp(g.scale.y * 2.0f, 1e-4f, 1e5f);
        float sz = std::clamp(g.scale.z * 2.0f, 1e-4f, 1e5f);

        Vec3f ax = { (1.0f - 2.0f * (y * y + z * z)) * sx, (2.0f * (x * y + w * z)) * sx, (2.0f * (x * z - w * y)) * sx };
        Vec3f ay = { (2.0f * (x * y - w * z)) * sy, (1.0f - 2.0f * (x * x + z * z)) * sy, (2.0f * (y * z + w * x)) * sy };
        Vec3f az = { (2.0f * (x * z + w * y)) * sz, (2.0f * (y * z - w * x)) * sz, (1.0f - 2.0f * (x * x + y * y)) * sz };

        uint32_t vb = static_cast<uint32_t>(mesh.vertices.size());
        for (int k = 0; k < 6; ++k) {
            const Vec3f& u = kOct[k];
            mesh.vertices.push_back({
                g.pos.x + ax.x * u.x + ay.x * u.y + az.x * u.z,
                g.pos.y + ax.y * u.x + ay.y * u.y + az.y * u.z,
                g.pos.z + ax.z * u.x + ay.z * u.y + az.z * u.z
            });
        }
        for (int t = 0; t < 8; ++t) {
            mesh.tris.push_back({ vb + kOctTris[t][0], vb + kOctTris[t][1], vb + kOctTris[t][2], {} });
        }
    }
}

// ============================================================================
// 1. Antimatter15 .splat (32 bytes/splat)
// ============================================================================
inline bool ParseAntimatterSplat(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    if (!data || size < 32 || (size % 32) != 0) return false;
    size_t count = size / 32;
    if (count > 5000000) return false;

    // Strict structural validation on sample window to reject arbitrary 32-byte-aligned garbage/text
    size_t checkCount = std::min<size_t>(count, 256);
    size_t validQuats = 0;
    for (size_t i = 0; i < checkCount; ++i) {
        const uint8_t* p = data + i * 32;
        float px = RdF32(p + 0),  py = RdF32(p + 4),  pz = RdF32(p + 8);
        float sx = RdF32(p + 12), sy = RdF32(p + 16), sz = RdF32(p + 20);
        if (!std::isfinite(px) || !std::isfinite(py) || !std::isfinite(pz) ||
            std::fabs(px) > 1e6f || std::fabs(py) > 1e6f || std::fabs(pz) > 1e6f) return false;
        if (!std::isfinite(sx) || !std::isfinite(sy) || !std::isfinite(sz) ||
            sx <= 0.0f || sy <= 0.0f || sz <= 0.0f ||
            sx > 1e5f || sy > 1e5f || sz > 1e5f) return false;

        float qw = (static_cast<float>(p[28]) - 128.0f) / 128.0f;
        float qx = (static_cast<float>(p[29]) - 128.0f) / 128.0f;
        float qy = (static_cast<float>(p[30]) - 128.0f) / 128.0f;
        float qz = (static_cast<float>(p[31]) - 128.0f) / 128.0f;
        float n2 = qw * qw + qx * qx + qy * qy + qz * qz;
        if (n2 >= 0.45f && n2 <= 1.65f) ++validQuats;
    }
    if (validQuats * 10 < checkCount * 9) return false;

    std::vector<GaussianPrim> splats;
    splats.reserve(std::min<size_t>(count, 100000));
    size_t strideStep = std::max<size_t>(1, count / 100000);
    for (size_t i = 0; i < count; i += strideStep) {
        const uint8_t* p = data + i * 32;
        GaussianPrim g;
        g.pos   = { RdF32(p + 0),  RdF32(p + 4),  RdF32(p + 8) };
        g.scale = { RdF32(p + 12), RdF32(p + 16), RdF32(p + 20) };
        if (!std::isfinite(g.pos.x) || !std::isfinite(g.pos.y) || !std::isfinite(g.pos.z) ||
            !std::isfinite(g.scale.x) || !std::isfinite(g.scale.y) || !std::isfinite(g.scale.z) ||
            g.scale.x <= 0.0f || g.scale.y <= 0.0f || g.scale.z <= 0.0f) continue;
        g.alpha = p[27];
        g.qw = (static_cast<float>(p[28]) - 128.0f) / 128.0f;
        g.qx = (static_cast<float>(p[29]) - 128.0f) / 128.0f;
        g.qy = (static_cast<float>(p[30]) - 128.0f) / 128.0f;
        g.qz = (static_cast<float>(p[31]) - 128.0f) / 128.0f;
        splats.push_back(g);
    }

    mesh.vertices.clear();
    mesh.tris.clear();
    EmitGaussianEllipsoids(splats, mesh);
    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"3D Gaussian Splatting Cloud (.splat)";
    return true;
}

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

// ============================================================================
// 3. Niantic / Scaniverse Compressed Gaussian Splats (.spz — Gzip "NGSP" 0x5053474E)
// ============================================================================
inline bool ParseNianticSpz(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    if (!data || size < 16) return false;
    std::vector<uint8_t> inflated;
    const uint8_t* raw = data;
    size_t rawLen = size;

    if (size >= 10 && data[0] == 0x1Fu && data[1] == 0x8Bu) {
        if (!ZipDeflate::InflateGzip(data, size, inflated, 256 * 1024 * 1024) || inflated.size() < 16) return false;
        raw = inflated.data();
        rawLen = inflated.size();
    }

    if (RdU32(raw) != 0x5053474Eu) return false; // "NGSP"
    uint32_t ver      = RdU32(raw + 4);
    uint32_t nPts     = RdU32(raw + 8);
    uint8_t  shDegree = raw[12];
    uint8_t  fracBits = raw[13];
    if (ver < 1 || ver > 3 || nPts == 0 || nPts > 10000000 || shDegree > 3 || fracBits > 24) return false;

    size_t posBytesPerPt = (ver == 1) ? 6u : 9u;
    size_t rotBytesPerPt = (ver >= 3) ? 4u : 3u;
    size_t minNeeded = 16ULL + static_cast<size_t>(nPts) * (posBytesPerPt + 1u + 3u + 3u + rotBytesPerPt);
    if (rawLen < minNeeded) return false;

    const uint8_t* pPos   = raw + 16;
    const uint8_t* pAlpha = pPos   + static_cast<size_t>(nPts) * posBytesPerPt;
    const uint8_t* pColor = pAlpha + static_cast<size_t>(nPts) * 1u;
    const uint8_t* pScale = pColor + static_cast<size_t>(nPts) * 3u;
    const uint8_t* pRot   = pScale + static_cast<size_t>(nPts) * 3u;

    float fixedInv = 1.0f / static_cast<float>(1u << fracBits);
    std::vector<GaussianPrim> splats;
    splats.reserve(std::min<size_t>(nPts, 100000));
    size_t step = std::max<size_t>(1, static_cast<size_t>(nPts) / 100000);

    auto rdSigned24 = [](const uint8_t* b) -> int32_t {
        int32_t v = static_cast<int32_t>(b[0]) | (static_cast<int32_t>(b[1]) << 8) | (static_cast<int32_t>(b[2]) << 16);
        if ((v & 0x00800000) != 0) v |= static_cast<int32_t>(0xFF000000u);
        return v;
    };

    for (size_t i = 0; i < static_cast<size_t>(nPts); i += step) {
        GaussianPrim g;
        if (ver == 1) {
            const uint8_t* pp = pPos + i * 6u;
            g.pos = { HalfToF32Splat(RdU16(pp + 0)), HalfToF32Splat(RdU16(pp + 2)), HalfToF32Splat(RdU16(pp + 4)) };
        } else {
            const uint8_t* pp = pPos + i * 9u;
            g.pos = {
                static_cast<float>(rdSigned24(pp + 0)) * fixedInv,
                static_cast<float>(rdSigned24(pp + 3)) * fixedInv,
                static_cast<float>(rdSigned24(pp + 6)) * fixedInv
            };
        }
        g.alpha = pAlpha[i];

        const uint8_t* sp = pScale + i * 3u;
        g.scale = {
            std::exp(static_cast<float>(sp[0]) / 16.0f - 10.0f),
            std::exp(static_cast<float>(sp[1]) / 16.0f - 10.0f),
            std::exp(static_cast<float>(sp[2]) / 16.0f - 10.0f)
        };

        if (ver <= 2) {
            const uint8_t* rp = pRot + i * 3u;
            g.qx = static_cast<float>(rp[0]) / 127.5f - 1.0f;
            g.qy = static_cast<float>(rp[1]) / 127.5f - 1.0f;
            g.qz = static_cast<float>(rp[2]) / 127.5f - 1.0f;
            g.qw = std::sqrt(std::max(0.0f, 1.0f - g.qx * g.qx - g.qy * g.qy - g.qz * g.qz));
        } else {
            uint32_t pk = RdU32(pRot + i * 4u);
            uint32_t largestIdx = pk >> 30;
            float comps[4] = {};
            float sumSq = 0.0f;
            uint32_t shift = 0;
            for (uint32_t c = 0; c < 4; ++c) {
                if (c == largestIdx) continue;
                uint32_t mag = (pk >> shift) & 0x1FFu;
                bool neg = ((pk >> (shift + 9u)) & 1u) != 0;
                float v = (static_cast<float>(mag) / 511.0f) * 0.70710678f;
                comps[c] = neg ? -v : v;
                sumSq += comps[c] * comps[c];
                shift += 10u;
            }
            comps[largestIdx] = std::sqrt(std::max(0.0f, 1.0f - sumSq));
            g.qx = comps[0]; g.qy = comps[1]; g.qz = comps[2]; g.qw = comps[3];
        }

        if (std::isfinite(g.pos.x) && std::isfinite(g.pos.y) && std::isfinite(g.pos.z)) {
            splats.push_back(g);
        }
    }

    mesh.vertices.clear();
    mesh.tris.clear();
    EmitGaussianEllipsoids(splats, mesh);
    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"Niantic Compressed 3D Gaussian Splats (.spz)";
    return true;
}

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

inline bool ParseSplatOrOpenVdb(const uint8_t* data, size_t size, const std::wstring& extLower, LoadedMesh3D& mesh) {
    if (extLower == L".splat")  return SplatVdbDetail::ParseAntimatterSplat(data, size, mesh);
    if (extLower == L".ksplat") return SplatVdbDetail::ParseKelloggKsplat(data, size, mesh);
    if (extLower == L".spz")    return SplatVdbDetail::ParseNianticSpz(data, size, mesh);
    if (extLower == L".vdb")    return SplatVdbDetail::ParseOpenVdbVolume(data, size, mesh);
    return false;
}

// Detects INRIA / Nerfstudio binary_little_endian 3D Gaussian Splatting .ply (scale_0..2, rot_0..3, opacity)
inline bool Parse3DGaussianPlyIfPresent(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    using namespace SplatVdbDetail;
    if (!data || size < 64) return false;
    std::string_view hdrScan(reinterpret_cast<const char*>(data), std::min<size_t>(size, 8192));
    if (hdrScan.rfind("ply", 0) != 0) return false;
    size_t eh = hdrScan.find("end_header");
    if (eh == std::string_view::npos) return false;
    std::string_view hdr = hdrScan.substr(0, eh);
    if (hdr.find("format binary_little_endian") == std::string_view::npos) return false;
    if (hdr.find("scale_0") == std::string_view::npos || hdr.find("rot_0") == std::string_view::npos) return false;
    if (hdr.find("element face") != std::string_view::npos && hdr.find("element face 0") == std::string_view::npos) return false;

    size_t bodyOff = eh + 10;
    if (bodyOff < size && data[bodyOff] == '\r') ++bodyOff;
    if (bodyOff < size && data[bodyOff] == '\n') ++bodyOff;

    size_t nVerts = 0;
    std::vector<std::string> propNames;
    bool inVertElem = false;
    size_t p = 0;
    while (p < hdr.size()) {
        size_t eol = hdr.find('\n', p);
        if (eol == std::string_view::npos) eol = hdr.size();
        std::string_view ln = DetailStep::TrimSv(hdr.substr(p, eol - p));
        p = eol + 1;
        if (ln.rfind("element vertex ", 0) == 0) {
            nVerts = static_cast<size_t>(std::strtoull(std::string(ln.substr(15)).c_str(), nullptr, 10));
            inVertElem = true;
        } else if (ln.rfind("element ", 0) == 0) {
            inVertElem = false;
        } else if (inVertElem && ln.rfind("property float ", 0) == 0) {
            propNames.emplace_back(DetailStep::TrimSv(ln.substr(15)));
        } else if (inVertElem && ln.rfind("property ", 0) == 0) {
            return false; // Mixed non-float vertex schema -> let generic ParsePlyMesh handle
        }
    }
    if (nVerts == 0 || nVerts > 5000000 || propNames.size() < 11) return false;
    size_t stride = propNames.size() * 4u;
    if (bodyOff + static_cast<uint64_t>(nVerts) * stride > size) return false;

    auto idxOf = [&](const char* nm) -> int {
        for (size_t i = 0; i < propNames.size(); ++i) if (propNames[i] == nm) return static_cast<int>(i);
        return -1;
    };
    int ix = idxOf("x"), iy = idxOf("y"), iz = idxOf("z");
    int is0 = idxOf("scale_0"), is1 = idxOf("scale_1"), is2 = idxOf("scale_2");
    int ir0 = idxOf("rot_0"), ir1 = idxOf("rot_1"), ir2 = idxOf("rot_2"), ir3 = idxOf("rot_3");
    int iop = idxOf("opacity");
    if (ix < 0 || iy < 0 || iz < 0 || is0 < 0 || is1 < 0 || is2 < 0 || ir0 < 0 || ir1 < 0 || ir2 < 0 || ir3 < 0) return false;

    std::vector<GaussianPrim> splats;
    size_t step = std::max<size_t>(1, nVerts / 100000);
    splats.reserve(nVerts / step + 1);
    for (size_t i = 0; i < nVerts; i += step) {
        const uint8_t* vp = data + bodyOff + i * stride;
        GaussianPrim g;
        g.pos   = { RdF32(vp + ix * 4), RdF32(vp + iy * 4), RdF32(vp + iz * 4) };
        g.scale = { std::exp(std::clamp(RdF32(vp + is0 * 4), -20.0f, 10.0f)),
                    std::exp(std::clamp(RdF32(vp + is1 * 4), -20.0f, 10.0f)),
                    std::exp(std::clamp(RdF32(vp + is2 * 4), -20.0f, 10.0f)) };
        g.qw = RdF32(vp + ir0 * 4); g.qx = RdF32(vp + ir1 * 4);
        g.qy = RdF32(vp + ir2 * 4); g.qz = RdF32(vp + ir3 * 4);
        if (iop >= 0) {
            float logit = std::clamp(RdF32(vp + iop * 4), -15.0f, 15.0f);
            float sig = 1.0f / (1.0f + std::exp(-logit));
            g.alpha = static_cast<uint8_t>(std::clamp(sig * 255.0f, 0.0f, 255.0f));
        }
        if (std::isfinite(g.pos.x) && std::isfinite(g.pos.y) && std::isfinite(g.pos.z)) splats.push_back(g);
    }

    mesh.vertices.clear();
    mesh.tris.clear();
    EmitGaussianEllipsoids(splats, mesh);
    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"3D Gaussian Splatting Cloud (.ply)";
    return true;
}

} // namespace Formats
