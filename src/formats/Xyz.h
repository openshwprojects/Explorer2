#pragma once
// Xyz.h — 3D Point Cloud / Molecular Coordinates (.xyz / .pts) Parser

namespace Formats {

inline bool ParseXyzPointCloud(std::string_view sv, LoadedMesh3D& mesh) {
    mesh.formatName = L"3D Point Cloud / Molecular (.xyz)";

    std::vector<Vec3f> pts;
    size_t pos = 0;
    while (pos < sv.size() && pts.size() < 20000) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t' || ln.front() == '\r')) ln.remove_prefix(1);
        if (ln.empty() || ln.front() == '#') continue;

        // Support optional element symbol prefix (e.g., "C 1.20 0.50 -0.30" or "1.20 0.50 -0.30")
        std::string s(ln);
        const char* cur = s.c_str();
        if ((*cur >= 'A' && *cur <= 'Z') || (*cur >= 'a' && *cur <= 'z')) {
            while (*cur && *cur > ' ') ++cur;
        }
        char* next = nullptr;
        float x = std::strtof(cur, &next);
        if (next == cur) continue;
        cur = next;
        while (*cur == ',' || *cur == ';') ++cur;
        float y = std::strtof(cur, &next);
        if (next == cur) continue;
        cur = next;
        while (*cur == ',' || *cur == ';') ++cur;
        float z = std::strtof(cur, &next);
        if (next == cur) continue;
        if (std::isfinite(x) && std::isfinite(y) && std::isfinite(z)) {
            pts.push_back({ x, y, z });
        }
    }
    if (pts.empty()) return false;

    float minX = 1e9f, maxX = -1e9f;
    for (const auto& p : pts) { minX = std::min(minX, p.x); maxX = std::max(maxX, p.x); }
    float r = std::max(0.05f, (maxX - minX) * 0.04f);

    for (const auto& p : pts) {
        uint32_t b = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back({ p.x,     p.y + r, p.z     });
        mesh.vertices.push_back({ p.x + r, p.y,     p.z     });
        mesh.vertices.push_back({ p.x,     p.y,     p.z + r });
        mesh.vertices.push_back({ p.x - r, p.y,     p.z     });
        mesh.vertices.push_back({ p.x,     p.y,     p.z - r });
        mesh.vertices.push_back({ p.x,     p.y - r, p.z     });
        mesh.tris.push_back({ b+0, b+1, b+2, {} });
        mesh.tris.push_back({ b+0, b+2, b+3, {} });
        mesh.tris.push_back({ b+0, b+3, b+4, {} });
        mesh.tris.push_back({ b+0, b+4, b+1, {} });
        mesh.tris.push_back({ b+5, b+2, b+1, {} });
        mesh.tris.push_back({ b+5, b+3, b+2, {} });
        mesh.tris.push_back({ b+5, b+4, b+3, {} });
        mesh.tris.push_back({ b+5, b+1, b+4, {} });
    }
    return true;
}

} // namespace Formats
