#pragma once
// IgesCad.h — IGES (.iges / .igs) Initial Graphics Exchange Specification 3D CAD Parser

namespace Formats {

inline bool ParseIgesCadMesh(std::string_view sv, LoadedMesh3D& mesh) {
    mesh.formatName = L"IGES 3D CAD (.iges)";
    std::vector<Vec3f> pts;

    size_t pos = 0;
    while (pos < sv.size() && pts.size() < 40000) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.remove_suffix(1);
        if (ln.empty()) continue;

        // IGES Parameter Data section lines have 'P' at col 73 (index 72) or contain 110,/116,/106,/126,/128,
        bool isParam = (ln.size() >= 65 && (ln[64] == 'P' || (ln.size() >= 73 && ln[72] == 'P'))) ||
                       ln.rfind("110,", 0) == 0 || ln.rfind("116,", 0) == 0 ||
                       ln.rfind("106,", 0) == 0 || ln.rfind("126,", 0) == 0 || ln.rfind("128,", 0) == 0;
        if (!isParam) continue;

        std::string dataPart(ln.substr(0, std::min<size_t>(ln.size(), 64)));
        for (char& c : dataPart) {
            if (c == ',' || c == ';') c = ' ';
            else if (c == 'D' || c == 'd') c = 'E';
        }
        const char* cur = dataPart.c_str();
        char* next = nullptr;
        int entType = static_cast<int>(std::strtol(cur, &next, 10));
        if (next == cur) continue;
        cur = next;

        if (entType == 110) {
            // Line entity: 110, X1, Y1, Z1, X2, Y2, Z2
            float v[6] = {};
            int cnt = 0;
            while (cnt < 6) {
                v[cnt] = std::strtof(cur, &next);
                if (next == cur) break;
                cur = next;
                ++cnt;
            }
            if (cnt >= 6) {
                pts.push_back({ v[0], v[2], -v[1] });
                pts.push_back({ v[3], v[5], -v[4] });
            }
        } else if (entType == 116 || entType == 106 || entType == 126 || entType == 128) {
            float v[3] = {};
            int cnt = 0;
            while (cnt < 3) {
                v[cnt] = std::strtof(cur, &next);
                if (next == cur) break;
                cur = next;
                ++cnt;
            }
            if (cnt >= 3) {
                pts.push_back({ v[0], v[2], -v[1] });
            }
        }
    }

    if (pts.size() < 2) return false;
    float minX = 1e9f, maxX = -1e9f;
    for (const auto& p : pts) { minX = std::min(minX, p.x); maxX = std::max(maxX, p.x); }
    float r = std::max(0.05f, (maxX - minX) * 0.02f);

    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        const Vec3f& a = pts[i];
        const Vec3f& b = pts[i + 1];
        uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back({ a.x, a.y - r, a.z });
        mesh.vertices.push_back({ a.x, a.y + r, a.z });
        mesh.vertices.push_back({ b.x, b.y + r, b.z });
        mesh.vertices.push_back({ b.x, b.y - r, b.z });
        mesh.tris.push_back({ base + 0, base + 1, base + 2, {} });
        mesh.tris.push_back({ base + 0, base + 2, base + 3, {} });
    }
    return !mesh.tris.empty();
}

} // namespace Formats
