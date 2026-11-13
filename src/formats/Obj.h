#pragma once
    // 2. Wavefront OBJ Parser (.obj)
    if (ext == L".obj") {
        mesh.formatName = L"Wavefront OBJ";
        const char* p = reinterpret_cast<const char*>(buf.data());
        const char* end = p + buf.size();
        mesh.vertices.reserve(8192);
        mesh.tris.reserve(16384);

        std::vector<uint32_t> facePoly;
        facePoly.reserve(16);

        while (p < end) {
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\r')) ++p;
            if (p >= end) break;
            if (*p == '\n') { ++p; continue; }

            const char* lineEnd = p;
            while (lineEnd < end && *lineEnd != '\n') ++lineEnd;

            if (p[0] == 'v' && (p[1] == ' ' || p[1] == '\t')) {
                const char* cur = p + 2;
                char* next = nullptr;
                float vx = std::strtof(cur, &next); cur = next;
                float vy = std::strtof(cur, &next); cur = next;
                float vz = std::strtof(cur, &next);
                mesh.vertices.push_back({ vx, vy, vz });
            } else if (p[0] == 'f' && (p[1] == ' ' || p[1] == '\t')) {
                facePoly.clear();
                const char* cur = p + 2;
                while (cur < lineEnd) {
                    while (cur < lineEnd && (*cur == ' ' || *cur == '\t' || *cur == '\r')) ++cur;
                    if (cur >= lineEnd || *cur == '#') break;
                    char* next = nullptr;
                    long idx = std::strtol(cur, &next, 10);
                    if (next == cur) break;
                    cur = next;
                    while (cur < lineEnd && *cur != ' ' && *cur != '\t' && *cur != '\r') ++cur;

                    int vIdx = -1;
                    if (idx > 0) {
                        vIdx = static_cast<int>(idx) - 1;
                    } else if (idx < 0) {
                        vIdx = static_cast<int>(mesh.vertices.size()) + static_cast<int>(idx);
                    }
                    if (vIdx >= 0 && static_cast<size_t>(vIdx) < mesh.vertices.size()) {
                        facePoly.push_back(static_cast<uint32_t>(vIdx));
                    }
                }
                for (size_t i = 1; i + 1 < facePoly.size(); ++i) {
                    mesh.tris.push_back({ facePoly[0], facePoly[i], facePoly[i + 1], {} });
                }
            }
            p = (lineEnd < end) ? (lineEnd + 1) : end;
        }
        FinalizeMeshBoundsAndNormals(mesh);
        return mesh;
    }

