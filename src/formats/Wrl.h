#pragma once
// Wrl.h — VRML 2.0 / VRML97 (.wrl) 3D Mesh Parser

namespace Formats {

inline bool ParseWrlMesh(std::string_view sv, LoadedMesh3D& mesh) {
    if (sv.find("#VRML") == std::string_view::npos && sv.find("IndexedFaceSet") == std::string_view::npos) {
        return false;
    }
    mesh.formatName = L"VRML 2.0 3D (.wrl)";

    size_t ptPos = sv.find("point");
    if (ptPos != std::string_view::npos) {
        size_t brOpen = sv.find('[', ptPos);
        size_t brClose = (brOpen != std::string_view::npos) ? sv.find(']', brOpen) : std::string_view::npos;
        if (brOpen != std::string_view::npos && brClose != std::string_view::npos) {
            const char* cur = sv.data() + brOpen + 1;
            const char* end = sv.data() + brClose;
            while (cur < end) {
                while (cur < end && (*cur == ' ' || *cur == '\t' || *cur == '\r' || *cur == '\n' || *cur == ',')) ++cur;
                if (cur >= end) break;
                char* next = nullptr;
                float x = std::strtof(cur, &next); if (next == cur) break; cur = next;
                while (cur < end && *cur == ',') ++cur;
                float y = std::strtof(cur, &next); if (next == cur) break; cur = next;
                while (cur < end && *cur == ',') ++cur;
                float z = std::strtof(cur, &next); if (next == cur) break; cur = next;
                mesh.vertices.push_back({ x, y, z });
            }
        }
    }

    size_t ciPos = sv.find("coordIndex");
    if (ciPos != std::string_view::npos && !mesh.vertices.empty()) {
        size_t brOpen = sv.find('[', ciPos);
        size_t brClose = (brOpen != std::string_view::npos) ? sv.find(']', brOpen) : std::string_view::npos;
        if (brOpen != std::string_view::npos && brClose != std::string_view::npos) {
            const char* cur = sv.data() + brOpen + 1;
            const char* end = sv.data() + brClose;
            std::vector<uint32_t> face;
            while (cur < end) {
                while (cur < end && (*cur == ' ' || *cur == '\t' || *cur == '\r' || *cur == '\n' || *cur == ',')) ++cur;
                if (cur >= end) break;
                char* next = nullptr;
                long idx = std::strtol(cur, &next, 10);
                if (next == cur) break;
                cur = next;
                if (idx < 0) {
                    for (size_t k = 1; k + 1 < face.size(); ++k) {
                        if (face[0] < mesh.vertices.size() && face[k] < mesh.vertices.size() && face[k + 1] < mesh.vertices.size()) {
                            mesh.tris.push_back({ face[0], face[k], face[k + 1], {} });
                        }
                    }
                    face.clear();
                } else {
                    face.push_back(static_cast<uint32_t>(idx));
                }
            }
            for (size_t k = 1; k + 1 < face.size(); ++k) {
                if (face[0] < mesh.vertices.size() && face[k] < mesh.vertices.size() && face[k + 1] < mesh.vertices.size()) {
                    mesh.tris.push_back({ face[0], face[k], face[k + 1], {} });
                }
            }
        }
    }

    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace Formats
