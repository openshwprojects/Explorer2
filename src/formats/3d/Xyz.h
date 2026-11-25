#pragma once
#include "../common/MeshBuild3D.h"
// Xyz.h — point clouds and XYZ molecules (.xyz / .pts): "x y z [...]" or "Element x y z" lines (comma /
// semicolon separators allowed). Large scans are sampled evenly over the whole file instead of keeping only
// the first points; each point is a small octahedron sized from the cloud's extent and density.

namespace Formats {

inline bool ParseXyzPointCloud(std::string_view sv, LoadedMesh3D& mesh) {
    if (sv.size() >= 3 && static_cast<uint8_t>(sv[0]) == 0xEF &&
        static_cast<uint8_t>(sv[1]) == 0xBB && static_cast<uint8_t>(sv[2]) == 0xBF) {
        sv.remove_prefix(3);
    }
    if (sv.empty() || sv.find('\0') != std::string_view::npos) return false;

    mesh.formatName = L"3D Point Cloud / Molecular (.xyz)";
    constexpr size_t kMaxPoints = 30000;

    auto trimLine = [](std::string_view ln) -> std::string_view {
        while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t' || ln.front() == '\r')) ln.remove_prefix(1);
        while (!ln.empty() && (ln.back() == ' ' || ln.back() == '\t' || ln.back() == '\r')) ln.remove_suffix(1);
        return ln;
    };

    auto parseStandaloneCount = [&](std::string_view ln, size_t& outCount) -> bool {
        ln = trimLine(ln);
        if (ln.empty() || ln.size() > 16) return false;
        for (char c : ln) {
            if (c < '0' || c > '9') return false;
        }
        uint64_t val = 0;
        auto [ptr, ec] = std::from_chars(ln.data(), ln.data() + ln.size(), val);
        if (ec != std::errc{} || ptr != ln.data() + ln.size() || val == 0 || val > 50000000ULL) return false;
        outCount = static_cast<size_t>(val);
        return true;
    };

    // Parses either "x y z [...]" or "ElementSymbol x y z [...]" (comma / semicolon separators allowed)
    auto parseLine = [&](std::string_view ln, Vec3f& out, bool allowElementPrefix) -> bool {
        ln = trimLine(ln);
        if (ln.empty() || ln.front() == '#' || ln.front() == '/') return false;
        if (ln.size() > 1024) ln = ln.substr(0, 1024);
        std::string s(ln);
        const char* cur = s.c_str();
        if (allowElementPrefix && ((*cur >= 'A' && *cur <= 'Z') || (*cur >= 'a' && *cur <= 'z'))) {
            const char* tokStart = cur;
            while (*cur && *cur > ' ' && *cur != ',' && *cur != ';') {
                char ch = *cur;
                bool okCh = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '+' || ch == '-';
                if (!okCh) return false;
                ++cur;
            }
            if (static_cast<size_t>(cur - tokStart) > 6 || *cur == '\0') return false;
        }
        float v[3] = {};
        for (int k = 0; k < 3; ++k) {
            while (*cur == ' ' || *cur == '\t' || *cur == ',' || *cur == ';') ++cur;
            if (*cur == '\0') return false;
            char* next = nullptr;
            v[k] = std::strtof(cur, &next);
            if (next == cur || !std::isfinite(v[k])) return false;
            if (*next != '\0' && *next != ' ' && *next != '\t' && *next != ',' && *next != ';' && *next != '\r') {
                return false;
            }
            cur = next;
        }
        out = { v[0], v[1], v[2] };
        return true;
    };

    auto nextRawLine = [&](size_t& pos, std::string_view& outLn) -> bool {
        if (pos >= sv.size()) return false;
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) {
            outLn = sv.substr(pos);
            pos = sv.size();
        } else {
            outLn = sv.substr(pos, eol - pos);
            pos = eol + 1;
        }
        return true;
    };

    // Check if file starts with XMOL "<N>\n<comment>\n<N atom lines>" or Leica .pts "<N>\n<N point lines>"
    size_t scanPos = 0;
    std::string_view firstNonEmpty;
    while (nextRawLine(scanPos, firstNonEmpty)) {
        if (!trimLine(firstNonEmpty).empty()) break;
    }

    std::vector<Vec3f> pts;
    size_t declaredCount = 0;
    if (parseStandaloneCount(firstNonEmpty, declaredCount)) {
        // 1. Standard XMOL .xyz: immediately following line is an unconditional comment line (even if blank or numeric!)
        size_t xmolPos = scanPos;
        std::string_view commentLine;
        if (nextRawLine(xmolPos, commentLine)) {
            const size_t stride = std::max<size_t>(1, (declaredCount + kMaxPoints - 1) / kMaxPoints);
            std::vector<Vec3f> xmolPts;
            xmolPts.reserve(std::min(declaredCount, kMaxPoints));
            size_t got = 0;
            std::string_view aln;
            while (got < declaredCount && nextRawLine(xmolPos, aln)) {
                if (trimLine(aln).empty()) continue;
                Vec3f p{};
                if (!parseLine(aln, p, true)) break;
                if (got % stride == 0 && xmolPts.size() < kMaxPoints) xmolPts.push_back(p);
                ++got;
            }
            if (got == declaredCount && !xmolPts.empty()) {
                pts = std::move(xmolPts);
            }
        }

        // 2. Leica .pts point cloud: "<N>\n<x y z ...>" with NO comment line
        if (pts.empty()) {
            size_t ptsPos = scanPos;
            const size_t stride = std::max<size_t>(1, (declaredCount + kMaxPoints - 1) / kMaxPoints);
            std::vector<Vec3f> leicaPts;
            leicaPts.reserve(std::min(declaredCount, kMaxPoints));
            size_t got = 0;
            std::string_view pln;
            while (got < declaredCount && nextRawLine(ptsPos, pln)) {
                if (trimLine(pln).empty()) continue;
                Vec3f p{};
                if (!parseLine(pln, p, false)) break;
                if (got % stride == 0 && leicaPts.size() < kMaxPoints) leicaPts.push_back(p);
                ++got;
            }
            if (got == declaredCount && !leicaPts.empty()) {
                pts = std::move(leicaPts);
            }
        }

        if (pts.empty()) return false;
    } else {
        // Unheadered XYZ / PTS point cloud: require every non-comment/non-empty line to be a valid point
        size_t total = 0;
        Vec3f tmp{};
        for (size_t pos = 0; pos < sv.size();) {
            std::string_view ln;
            nextRawLine(pos, ln);
            std::string_view tr = trimLine(ln);
            if (tr.empty() || tr.front() == '#' || tr.front() == '/') continue;
            if (!parseLine(tr, tmp, true)) return false;
            ++total;
        }
        if (total == 0) return false;
        const size_t stride = std::max<size_t>(1, (total + kMaxPoints - 1) / kMaxPoints);
        pts.reserve(std::min(total, kMaxPoints));
        size_t index = 0;
        for (size_t pos = 0; pos < sv.size() && pts.size() < kMaxPoints;) {
            std::string_view ln;
            nextRawLine(pos, ln);
            Vec3f p{};
            if (parseLine(ln, p, true)) {
                if (index % stride == 0) pts.push_back(p);
                ++index;
            }
        }
    }

    if (pts.empty()) return false;
    Vec3f lo = pts[0], hi = pts[0];
    for (const auto& p : pts) {
        lo = { std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z) };
        hi = { std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z) };
    }
    float span = std::max({ hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, 1e-4f });
    float r = (pts.size() <= 200) ? span * 0.04f : span / (std::cbrt(static_cast<float>(pts.size())) * 6.0f);
    r = std::max(r, span * 0.0015f);
    for (const auto& p : pts) MeshBuild3D::EmitOctahedron(mesh, p, r);
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace Formats
