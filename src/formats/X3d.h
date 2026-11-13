#pragma once
// X3d.h — Extensible 3D XML (.x3d) Mesh Parser

namespace Formats {

inline bool ParseX3dMesh(std::string_view sv, LoadedMesh3D& mesh) {
    if (sv.find("<X3D") == std::string_view::npos && sv.find("<IndexedFaceSet") == std::string_view::npos &&
        sv.find("<Coordinate") == std::string_view::npos) {
        return false;
    }
    mesh.formatName = L"Extensible 3D (.x3d)";

    auto extractAttrVal = [](std::string_view txt, const char* attrKey) -> std::string_view {
        size_t p = txt.find(attrKey);
        if (p == std::string_view::npos) return {};
        p += std::strlen(attrKey);
        while (p < txt.size() && (txt[p] == ' ' || txt[p] == '\t' || txt[p] == '=')) ++p;
        if (p < txt.size() && (txt[p] == '"' || txt[p] == '\'')) {
            char q = txt[p++];
            size_t endQ = txt.find(q, p);
            if (endQ != std::string_view::npos) return txt.substr(p, endQ - p);
        }
        return {};
    };

    std::string_view ptStr = extractAttrVal(sv, "point");
    if (!ptStr.empty()) {
        const char* cur = ptStr.data();
        const char* end = ptStr.data() + ptStr.size();
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

    std::string_view idxStr = extractAttrVal(sv, "coordIndex");
    if (idxStr.empty()) idxStr = extractAttrVal(sv, "index");
    if (!idxStr.empty() && !mesh.vertices.empty()) {
        const char* cur = idxStr.data();
        const char* end = idxStr.data() + idxStr.size();
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

    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace Formats
