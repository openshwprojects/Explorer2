#pragma once
// GaussianSplat.h — 3D Gaussian Splatting (.splat + 3DGS .ply) reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {
namespace SplatVdbDetail {

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

} // namespace SplatVdbDetail

inline bool ParseGaussianSplat(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    return SplatVdbDetail::ParseAntimatterSplat(data, size, mesh);
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
