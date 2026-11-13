#pragma once
// Gcode3D.h — 3D Printer & CNC G-Code (.gcode / .nc) Extruded 3D Toolpath Mesh Parser

namespace Formats {

inline bool ParseGcodeToolpathMesh(std::string_view sv, LoadedMesh3D& mesh) {
    mesh.formatName = L"G-Code 3D Toolpath (.gcode)";
    std::vector<Vec3f> pathPts;
    float curX = 0.0f, curY = 0.0f, curZ = 0.0f;

    size_t pos = 0;
    while (pos < sv.size() && pathPts.size() < 45000) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        size_t semi = ln.find(';');
        if (semi != std::string_view::npos) ln = ln.substr(0, semi);
        while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
        if (ln.size() < 2) continue;

        if ((ln[0] == 'G' || ln[0] == 'g') && (ln[1] == '0' || ln[1] == '1' || ln[1] == '2' || ln[1] == '3')) {
            bool moved = false;
            size_t p = 2;
            while (p < ln.size()) {
                char c = static_cast<char>(std::toupper(static_cast<unsigned char>(ln[p])));
                if (c == 'X' || c == 'Y' || c == 'Z') {
                    char* endp = nullptr;
                    float val = std::strtof(ln.data() + p + 1, &endp);
                    if (endp != ln.data() + p + 1 && std::isfinite(val)) {
                        if (c == 'X') curX = val;
                        else if (c == 'Y') curY = val;
                        else if (c == 'Z') curZ = val;
                        moved = true;
                        p = static_cast<size_t>(endp - ln.data());
                        continue;
                    }
                }
                ++p;
            }
            if (moved) {
                pathPts.push_back({ curX, curZ, -curY });
            }
        }
    }

    if (pathPts.size() < 2) return false;
    float minX = 1e9f, maxX = -1e9f;
    for (const auto& p : pathPts) { minX = std::min(minX, p.x); maxX = std::max(maxX, p.x); }
    float r = std::max(0.12f, (maxX - minX) * 0.008f);

    for (size_t i = 0; i + 1 < pathPts.size(); ++i) {
        const Vec3f& a = pathPts[i];
        const Vec3f& b = pathPts[i + 1];
        float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
        if (dx * dx + dy * dy + dz * dz < 1e-6f) continue;
        uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back({ a.x - r, a.y,     a.z });
        mesh.vertices.push_back({ a.x + r, a.y,     a.z });
        mesh.vertices.push_back({ a.x,     a.y + r, a.z });
        mesh.vertices.push_back({ b.x - r, b.y,     b.z });
        mesh.vertices.push_back({ b.x + r, b.y,     b.z });
        mesh.vertices.push_back({ b.x,     b.y + r, b.z });
        mesh.tris.push_back({ base + 0, base + 1, base + 4, {} });
        mesh.tris.push_back({ base + 0, base + 4, base + 3, {} });
        mesh.tris.push_back({ base + 1, base + 2, base + 5, {} });
        mesh.tris.push_back({ base + 1, base + 5, base + 4, {} });
    }
    return !mesh.tris.empty();
}

} // namespace Formats
