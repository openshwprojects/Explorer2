#pragma once
// NianticSpz.h — Niantic / Scaniverse Compressed 3D Gaussian Splats (.spz) reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {
namespace SplatVdbDetail {

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

} // namespace SplatVdbDetail

inline bool ParseNianticSpzFile(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    return SplatVdbDetail::ParseNianticSpz(data, size, mesh);
}

} // namespace Formats
