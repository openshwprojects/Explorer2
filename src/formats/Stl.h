#pragma once
    // 1. STL Parser (Binary STL vs ASCII STL)
    if (ext == L".stl") {
        bool isBinaryStl = false;
        if (buf.size() >= 84) {
            uint32_t triCount = 0;
            std::memcpy(&triCount, buf.data() + 80, 4);
            uint64_t expectedBytes = 84ULL + static_cast<uint64_t>(triCount) * 50ULL;
            if (triCount > 0 && (expectedBytes == fileSize || (buf.size() >= expectedBytes && std::memcmp(buf.data(), "solid", 5) != 0))) {
                isBinaryStl = true;
            }
        }

        if (isBinaryStl) {
            mesh.formatName = L"STL (Binary)";
            uint32_t triCount = 0;
            std::memcpy(&triCount, buf.data() + 80, 4);
            size_t availTris = std::min<size_t>(triCount, (buf.size() - 84) / 50);
            size_t stride = (availTris > 60000) ? (availTris / 60000) : 1;
            mesh.vertices.reserve((availTris / stride) * 3);
            mesh.tris.reserve(availTris / stride);

            for (size_t t = 0; t < availTris; t += stride) {
                if (activeGen && activeGen->load() != generation) return mesh;
                const uint8_t* rec = buf.data() + 84 + t * 50;
                float v[12] = {};
                std::memcpy(v, rec, 48);
                // Convert Z-up CAD convention to Y-up studio convention
                uint32_t baseIdx = static_cast<uint32_t>(mesh.vertices.size());
                mesh.vertices.push_back({ v[3],  v[5], -v[4] });
                mesh.vertices.push_back({ v[6],  v[8], -v[7] });
                mesh.vertices.push_back({ v[9], v[11], -v[10] });
                mesh.tris.push_back({ baseIdx, baseIdx + 1, baseIdx + 2, {} });
            }
            FinalizeMeshBoundsAndNormals(mesh);
            mesh.rawTriangleCount = availTris;
            mesh.rawVertexCount = availTris * 3;
            return mesh;
        } else {
            mesh.formatName = L"STL (ASCII)";
            const char* p = reinterpret_cast<const char*>(buf.data());
            const char* end = p + buf.size();
            while (p < end) {
                while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
                if (p + 6 < end && std::memcmp(p, "vertex", 6) == 0 && (p[6] == ' ' || p[6] == '\t')) {
                    p += 6;
                    char* next = nullptr;
                    float vx = std::strtof(p, &next); p = next;
                    float vy = std::strtof(p, &next); p = next;
                    float vz = std::strtof(p, &next); p = next;
                    mesh.vertices.push_back({ vx, vz, -vy });
                    if (mesh.vertices.size() % 3 == 0) {
                        uint32_t b = static_cast<uint32_t>(mesh.vertices.size() - 3);
                        mesh.tris.push_back({ b, b + 1, b + 2, {} });
                    }
                }
                while (p < end && *p != '\n') ++p;
            }
            FinalizeMeshBoundsAndNormals(mesh);
            return mesh;
        }
    }

