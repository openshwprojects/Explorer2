#pragma once
// BvhMocap.h — Biovision Hierarchy Motion Capture Skeleton (.bvh) 3D Bone Mesh Parser

namespace Formats {

inline bool ParseBvhSkeletonMesh(std::string_view sv, LoadedMesh3D& mesh) {
    if (sv.find("HIERARCHY") == std::string_view::npos && sv.find("OFFSET") == std::string_view::npos) return false;
    mesh.formatName = L"Biovision Hierarchy Motion Capture (.bvh)";

    std::vector<Vec3f> stack;
    stack.push_back({ 0.0f, 0.0f, 0.0f });

    struct BoneSeg { Vec3f a; Vec3f b; };
    std::vector<BoneSeg> bones;

    size_t pos = 0;
    while (pos < sv.size() && bones.size() < 4000) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.remove_suffix(1);
        if (ln.empty()) continue;

        if (ln.rfind("MOTION", 0) == 0) break;
        if (ln.find('{') != std::string_view::npos) {
            stack.push_back(stack.back());
        } else if (ln.find('}') != std::string_view::npos) {
            if (stack.size() > 1) stack.pop_back();
        } else if (ln.rfind("OFFSET", 0) == 0) {
            float ox = 0.0f, oy = 0.0f, oz = 0.0f;
            if (sscanf_s(std::string(ln.substr(6)).c_str(), "%f %f %f", &ox, &oy, &oz) == 3) {
                Vec3f parent = (stack.size() >= 2) ? stack[stack.size() - 2] : Vec3f{ 0, 0, 0 };
                Vec3f cur = { parent.x + ox, parent.y + oy, parent.z + oz };
                stack.back() = cur;
                if (ox * ox + oy * oy + oz * oz > 1e-5f) {
                    bones.push_back({ parent, cur });
                }
            }
        }
    }
    if (bones.empty()) return false;

    for (const auto& b : bones) {
        float dx = b.b.x - b.a.x, dy = b.b.y - b.a.y, dz = b.b.z - b.a.z;
        float len = std::sqrt(dx * dx + dy * dy + dz * dz);
        float r = std::max(0.4f, len * 0.12f);
        Vec3f mid = { (b.a.x + b.b.x) * 0.5f, (b.a.y + b.b.y) * 0.5f, (b.a.z + b.b.z) * 0.5f };
        uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        // Octahedral bone prism from parent joint (b.a) through mid-ring to child joint (b.b)
        mesh.vertices.push_back(b.a);
        mesh.vertices.push_back(b.b);
        mesh.vertices.push_back({ mid.x + r, mid.y,     mid.z     });
        mesh.vertices.push_back({ mid.x - r, mid.y,     mid.z     });
        mesh.vertices.push_back({ mid.x,     mid.y,     mid.z + r });
        mesh.vertices.push_back({ mid.x,     mid.y,     mid.z - r });
        mesh.tris.push_back({ base + 0, base + 4, base + 2, {} });
        mesh.tris.push_back({ base + 0, base + 2, base + 5, {} });
        mesh.tris.push_back({ base + 0, base + 5, base + 3, {} });
        mesh.tris.push_back({ base + 0, base + 3, base + 4, {} });
        mesh.tris.push_back({ base + 1, base + 2, base + 4, {} });
        mesh.tris.push_back({ base + 1, base + 5, base + 2, {} });
        mesh.tris.push_back({ base + 1, base + 3, base + 5, {} });
        mesh.tris.push_back({ base + 1, base + 4, base + 3, {} });
    }
    return !mesh.tris.empty();
}

} // namespace Formats
