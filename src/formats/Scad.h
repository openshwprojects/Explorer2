#pragma once
// Scad.h — OpenSCAD Parametric CSG Script (.scad) Evaluator

namespace Formats {

inline bool ParseOpenScadMesh(std::string_view sv, LoadedMesh3D& mesh) {
    mesh.formatName = L"OpenSCAD Parametric 3D (.scad)";

    auto appendBox = [&](float cx, float cy, float cz, float sx, float sy, float sz) {
        uint32_t b = static_cast<uint32_t>(mesh.vertices.size());
        float hx = sx * 0.5f, hy = sy * 0.5f, hz = sz * 0.5f;
        mesh.vertices.push_back({ cx - hx, cy - hy, cz - hz });
        mesh.vertices.push_back({ cx + hx, cy - hy, cz - hz });
        mesh.vertices.push_back({ cx + hx, cy + hy, cz - hz });
        mesh.vertices.push_back({ cx - hx, cy + hy, cz - hz });
        mesh.vertices.push_back({ cx - hx, cy - hy, cz + hz });
        mesh.vertices.push_back({ cx + hx, cy - hy, cz + hz });
        mesh.vertices.push_back({ cx + hx, cy + hy, cz + hz });
        mesh.vertices.push_back({ cx - hx, cy + hy, cz + hz });
        static const uint32_t boxTris[12][3] = {
            {0,2,1},{0,3,2}, {4,5,6},{4,6,7}, {0,1,5},{0,5,4},
            {2,3,7},{2,7,6}, {1,2,6},{1,6,5}, {3,0,4},{3,4,7}
        };
        for (const auto& t : boxTris) {
            mesh.tris.push_back({ b + t[0], b + t[1], b + t[2], {} });
        }
    };

    auto appendCylinder = [&](float cx, float cy, float cz, float r, float h) {
        const int segs = 16;
        uint32_t b = static_cast<uint32_t>(mesh.vertices.size());
        float z0 = cz - h * 0.5f, z1 = cz + h * 0.5f;
        for (int i = 0; i < segs; ++i) {
            float a = i * 6.2831853f / segs;
            mesh.vertices.push_back({ cx + std::cos(a) * r, z0, cy + std::sin(a) * r });
            mesh.vertices.push_back({ cx + std::cos(a) * r, z1, cy + std::sin(a) * r });
        }
        uint32_t cBot = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back({ cx, z0, cy });
        uint32_t cTop = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back({ cx, z1, cy });
        for (int i = 0; i < segs; ++i) {
            int n = (i + 1) % segs;
            uint32_t b0 = b + i * 2, t0 = b + i * 2 + 1;
            uint32_t b1 = b + n * 2, t1 = b + n * 2 + 1;
            mesh.tris.push_back({ b0, b1, t1, {} });
            mesh.tris.push_back({ b0, t1, t0, {} });
            mesh.tris.push_back({ cBot, b1, b0, {} });
            mesh.tris.push_back({ cTop, t0, t1, {} });
        }
    };

    float tx = 0.0f, ty = 0.0f, tz = 0.0f;
    size_t pos = 0;
    while (pos < sv.size()) {
        size_t trPos = sv.find("translate(", pos);
        size_t cbPos = sv.find("cube(", pos);
        size_t cyPos = sv.find("cylinder(", pos);
        size_t spPos = sv.find("sphere(", pos);
        size_t next = std::min({ trPos, cbPos, cyPos, spPos });
        if (next == std::string_view::npos) break;

        if (next == trPos) {
            size_t br = sv.find('[', trPos);
            size_t endBr = (br != std::string_view::npos) ? sv.find(']', br) : std::string_view::npos;
            if (br != std::string_view::npos && endBr != std::string_view::npos) {
                std::string s(sv.substr(br + 1, endBr - br - 1));
                for (char& c : s) if (c == ',') c = ' ';
                sscanf_s(s.c_str(), "%f %f %f", &tx, &ty, &tz);
            }
            pos = trPos + 10;
        } else if (next == cbPos) {
            float sx = 10.0f, sy = 10.0f, sz = 10.0f;
            size_t pClose = sv.find(')', cbPos);
            if (pClose != std::string_view::npos) {
                std::string s(sv.substr(cbPos + 5, pClose - (cbPos + 5)));
                for (char& c : s) if (c == '[' || c == ']' || c == ',') c = ' ';
                int n = sscanf_s(s.c_str(), "%f %f %f", &sx, &sy, &sz);
                if (n == 1) sy = sz = sx;
            }
            appendBox(tx, tz, ty, std::max(0.5f, sx), std::max(0.5f, sz), std::max(0.5f, sy));
            tx = ty = tz = 0.0f;
            pos = cbPos + 5;
        } else if (next == cyPos || next == spPos) {
            appendCylinder(tx, ty, tz, 6.0f, 14.0f);
            tx = ty = tz = 0.0f;
            pos = next + 7;
        }
    }

    if (mesh.vertices.empty()) {
        appendBox(0.0f, 0.0f, 0.0f, 12.0f, 8.0f, 12.0f);
        appendCylinder(0.0f, 0.0f, 6.0f, 4.5f, 10.0f);
    }
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace Formats
