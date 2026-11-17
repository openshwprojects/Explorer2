#pragma once
#include "MeshBuild3D.h"
// Xyz.h — point clouds and XYZ molecules (.xyz / .pts): "x y z [...]" or "Element x y z" lines (comma /
// semicolon separators allowed). Large scans are sampled evenly over the whole file instead of keeping only
// the first points; each point is a small octahedron sized from the cloud's extent and density.

namespace Formats {

inline bool ParseXyzPointCloud(std::string_view sv, LoadedMesh3D& mesh) {
    mesh.formatName = L"3D Point Cloud / Molecular (.xyz)";
    constexpr size_t kMaxPoints = 30000;

    // A line's point, if it has one (an optional leading element symbol / label is skipped)
    auto parseLine = [](std::string_view ln, Vec3f& out) -> bool {
        while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t' || ln.front() == '\r')) ln.remove_prefix(1);
        if (ln.empty() || ln.front() == '#' || ln.front() == '/') return false;
        std::string s(ln);
        const char* cur = s.c_str();
        if ((*cur >= 'A' && *cur <= 'Z') || (*cur >= 'a' && *cur <= 'z')) {
            while (*cur && *cur > ' ') ++cur;
        }
        float v[3] = {};
        for (int k = 0; k < 3; ++k) {
            while (*cur == ',' || *cur == ';') ++cur;
            char* next = nullptr;
            v[k] = std::strtof(cur, &next);
            if (next == cur || !std::isfinite(v[k])) return false;
            cur = next;
        }
        out = { v[0], v[1], v[2] };
        return true;
    };

    // Pass 1: how many points there are; pass 2: every stride-th one
    size_t total = 0;
    Vec3f tmp{};
    for (size_t pos = 0; pos < sv.size();) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        if (parseLine(sv.substr(pos, eol - pos), tmp)) ++total;
        pos = eol + 1;
    }
    if (total == 0) return false;
    const size_t stride = (total + kMaxPoints - 1) / kMaxPoints;
    std::vector<Vec3f> pts;
    pts.reserve(std::min(total, kMaxPoints));
    size_t index = 0;
    for (size_t pos = 0; pos < sv.size() && pts.size() < kMaxPoints;) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        Vec3f p{};
        if (parseLine(sv.substr(pos, eol - pos), p)) {
            if (index % stride == 0) pts.push_back(p);
            ++index;
        }
        pos = eol + 1;
    }

    Vec3f lo = pts[0], hi = pts[0];
    for (const auto& p : pts) {
        lo = { std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z) };
        hi = { std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z) };
    }
    float span = std::max({ hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, 1e-4f });
    // Few points (molecules): visible balls; dense clouds: small markers that do not merge into a blob
    float r = (pts.size() <= 200) ? span * 0.04f : span / (std::cbrt(static_cast<float>(pts.size())) * 6.0f);
    r = std::max(r, span * 0.0015f);
    for (const auto& p : pts) MeshBuild3D::EmitOctahedron(mesh, p, r);
    return true;
}

} // namespace Formats
