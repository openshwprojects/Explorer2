#pragma once
    // 3. Stanford PLY Parser (.ply - ASCII & binary_little_endian)
    if (ext == L".ply") {
        mesh.formatName = L"Stanford PLY";
        const char* p = reinterpret_cast<const char*>(buf.data());
        const char* end = p + buf.size();
        bool isBinaryLE = false;
        size_t numVerts = 0;
        size_t numFaces = 0;
        size_t vertexFloatsOrProps = 0;
        bool inVertexElement = false;

        while (p < end) {
            const char* lineEnd = p;
            while (lineEnd < end && *lineEnd != '\n') ++lineEnd;
            std::string line(p, lineEnd);
            if (!line.empty() && line.back() == '\r') line.pop_back();

            if (line.find("format binary_little_endian") != std::string::npos) {
                isBinaryLE = true;
            } else if (line.rfind("element vertex ", 0) == 0) {
                numVerts = static_cast<size_t>(std::strtoull(line.c_str() + 15, nullptr, 10));
                inVertexElement = true;
            } else if (line.rfind("element face ", 0) == 0) {
                numFaces = static_cast<size_t>(std::strtoull(line.c_str() + 13, nullptr, 10));
                inVertexElement = false;
            } else if (line.rfind("element ", 0) == 0) {
                inVertexElement = false;
            } else if (inVertexElement && line.rfind("property ", 0) == 0) {
                ++vertexFloatsOrProps;
            } else if (line == "end_header") {
                p = (lineEnd < end) ? (lineEnd + 1) : end;
                break;
            }
            p = (lineEnd < end) ? (lineEnd + 1) : end;
        }

        // The header's counts are only a claim. A file cannot hold more vertices or faces than fit into what
        // is left of it (a text vertex takes at least 6 bytes), so anything beyond that is cut off here
        // instead of being reserved.
        {
            const size_t remaining = static_cast<size_t>(end - p);
            const size_t perVertex = isBinaryLE ? std::max<size_t>(12, vertexFloatsOrProps * 4) : 6;
            numVerts = std::min(numVerts, remaining / perVertex);
            numFaces = std::min(numFaces, remaining / 2);
        }
        if (!isBinaryLE && numVerts > 0) {
            mesh.vertices.reserve(numVerts);
            for (size_t i = 0; i < numVerts && p < end; ++i) {
                const char* lineEnd = p;
                while (lineEnd < end && *lineEnd != '\n') ++lineEnd;
                char* next = nullptr;
                float vx = std::strtof(p, &next);
                float vy = std::strtof(next, &next);
                float vz = std::strtof(next, &next);
                mesh.vertices.push_back({ vx, vy, vz });
                p = (lineEnd < end) ? (lineEnd + 1) : end;
            }
            for (size_t f = 0; f < numFaces && p < end; ++f) {
                const char* lineEnd = p;
                while (lineEnd < end && *lineEnd != '\n') ++lineEnd;
                char* next = nullptr;
                long n = std::strtol(p, &next, 10);
                if (n >= 3 && n <= 16) {
                    uint32_t idx[16] = {};
                    for (long k = 0; k < n; ++k) {
                        idx[k] = static_cast<uint32_t>(std::strtoul(next, &next, 10));
                    }
                    for (long k = 1; k + 1 < n; ++k) {
                        if (idx[0] < mesh.vertices.size() && idx[k] < mesh.vertices.size() && idx[k + 1] < mesh.vertices.size()) {
                            mesh.tris.push_back({ idx[0], idx[k], idx[k + 1], {} });
                        }
                    }
                }
                p = (lineEnd < end) ? (lineEnd + 1) : end;
            }
            FinalizeMeshBoundsAndNormals(mesh);
            return mesh;
        } else if (isBinaryLE && numVerts > 0) {
            mesh.formatName = L"Stanford PLY (Binary LE)";
            size_t strideBytes = std::max<size_t>(12, vertexFloatsOrProps * 4);
            const uint8_t* bp = reinterpret_cast<const uint8_t*>(p);
            const uint8_t* bend = buf.data() + buf.size();
            mesh.vertices.reserve(numVerts);
            for (size_t i = 0; i < numVerts && bp + 12 <= bend; ++i) {
                float v[3] = {};
                std::memcpy(v, bp, 12);
                mesh.vertices.push_back({ v[0], v[1], v[2] });
                bp += strideBytes;
            }
            for (size_t f = 0; f < numFaces && bp < bend; ++f) {
                uint8_t n = *bp++;
                if (n >= 3 && bp + static_cast<size_t>(n) * 4 <= bend) {
                    uint32_t idx[16] = {};
                    uint8_t cap = std::min<uint8_t>(n, 16);
                    std::memcpy(idx, bp, cap * 4);
                    for (uint8_t k = 1; k + 1 < cap; ++k) {
                        if (idx[0] < mesh.vertices.size() && idx[k] < mesh.vertices.size() && idx[k + 1] < mesh.vertices.size()) {
                            mesh.tris.push_back({ idx[0], idx[k], idx[k + 1], {} });
                        }
                    }
                    bp += static_cast<size_t>(n) * 4;
                } else {
                    break;
                }
            }
            FinalizeMeshBoundsAndNormals(mesh);
            return mesh;
        }
    }

