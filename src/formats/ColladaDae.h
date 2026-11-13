#pragma once
    // 11. Collada .dae XML Mesh Parser
    if (ext == L".dae") {
        mesh.formatName = L"COLLADA 3D (.dae)";
        std::string_view sv(reinterpret_cast<const char*>(buf.data()), buf.size());
        size_t faPos = sv.find("<float_array");
        if (faPos != std::string_view::npos) {
            size_t gt = sv.find('>', faPos);
            size_t endFa = sv.find("</float_array>", gt);
            if (gt != std::string_view::npos && endFa != std::string_view::npos) {
                const char* cur = sv.data() + gt + 1;
                const char* end = sv.data() + endFa;
                while (cur < end) {
                    char* next = nullptr;
                    float x = std::strtof(cur, &next);
                    if (next == cur) break;
                    cur = next;
                    float y = std::strtof(cur, &next); cur = next;
                    float z = std::strtof(cur, &next); cur = next;
                    mesh.vertices.push_back({ x, z, -y });
                }
                for (size_t i = 0; i + 2 < mesh.vertices.size(); i += 3) {
                    mesh.tris.push_back({ static_cast<uint32_t>(i), static_cast<uint32_t>(i + 1), static_cast<uint32_t>(i + 2), {} });
                }
                FinalizeMeshBoundsAndNormals(mesh);
                if (mesh.loadedOk) return mesh;
            }
        }
    }

