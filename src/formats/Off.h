#pragma once
    // 4. OFF (Object File Format) Parser (.off)
    if (ext == L".off") {
        mesh.formatName = L"OFF 3D Mesh";
        const char* p = reinterpret_cast<const char*>(buf.data());
        const char* end = p + buf.size();
        if (p + 3 < end && std::memcmp(p, "OFF", 3) == 0) {
            while (p < end && *p != '\n') ++p;
            while (p < end && (*p == '\n' || *p == '\r' || *p == ' ' || *p == '\t' || *p == '#')) {
                if (*p == '#') { while (p < end && *p != '\n') ++p; }
                else ++p;
            }
            char* next = nullptr;
            size_t nv = static_cast<size_t>(std::strtoull(p, &next, 10));
            size_t nf = static_cast<size_t>(std::strtoull(next, &next, 10));
            while (next < end && *next != '\n') ++next;
            p = (next < end) ? (next + 1) : end;

            for (size_t i = 0; i < nv && p < end; ++i) {
                float vx = std::strtof(p, &next);
                float vy = std::strtof(next, &next);
                float vz = std::strtof(next, &next);
                mesh.vertices.push_back({ vx, vy, vz });
                while (next < end && *next != '\n') ++next;
                p = (next < end) ? (next + 1) : end;
            }
            for (size_t f = 0; f < nf && p < end; ++f) {
                long n = std::strtol(p, &next, 10);
                if (n >= 3 && n <= 16) {
                    uint32_t idx[16] = {};
                    for (long k = 0; k < n; ++k) idx[k] = static_cast<uint32_t>(std::strtoul(next, &next, 10));
                    for (long k = 1; k + 1 < n; ++k) {
                        if (idx[0] < mesh.vertices.size() && idx[k] < mesh.vertices.size() && idx[k + 1] < mesh.vertices.size()) {
                            mesh.tris.push_back({ idx[0], idx[k], idx[k + 1], {} });
                        }
                    }
                }
                while (next < end && *next != '\n') ++next;
                p = (next < end) ? (next + 1) : end;
            }
            FinalizeMeshBoundsAndNormals(mesh);
            return mesh;
        }
    }

