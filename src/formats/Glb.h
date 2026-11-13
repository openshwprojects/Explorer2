#pragma once
    // 9. Binary glTF 2.0 Parser (.glb - magic 0x46546C67 "glTF") & Embedded JSON glTF (.gltf)
    if (ext == L".glb" && buf.size() >= 28 && std::memcmp(buf.data(), "glTF", 4) == 0) {
        mesh.formatName = L"Binary glTF 2.0 (.glb)";
        size_t pos = 12;
        const uint8_t* binChunk = nullptr;
        size_t binLen = 0;
        while (pos + 8 <= buf.size()) {
            uint32_t cLen = 0, cType = 0;
            std::memcpy(&cLen, buf.data() + pos, 4);
            std::memcpy(&cType, buf.data() + pos + 4, 4);
            if (pos + 8 + cLen > buf.size()) break;
            if (cType == 0x004E4942u) {
                binChunk = buf.data() + pos + 8;
                binLen = cLen;
                break;
            }
            pos += 8 + cLen;
        }
        if (binChunk && binLen >= 36) {
            size_t numVerts = binLen / 12;
            mesh.vertices.reserve(numVerts);
            for (size_t i = 0; i < numVerts; ++i) {
                float xyz[3] = {};
                std::memcpy(xyz, binChunk + i * 12, 12);
                if (std::isfinite(xyz[0]) && std::isfinite(xyz[1]) && std::isfinite(xyz[2]) &&
                    std::abs(xyz[0]) < 1e5f && std::abs(xyz[1]) < 1e5f && std::abs(xyz[2]) < 1e5f) {
                    mesh.vertices.push_back({ xyz[0], xyz[1], xyz[2] });
                }
            }
            for (size_t i = 0; i + 2 < mesh.vertices.size(); i += 3) {
                mesh.tris.push_back({ static_cast<uint32_t>(i), static_cast<uint32_t>(i + 1), static_cast<uint32_t>(i + 2), {} });
            }
            FinalizeMeshBoundsAndNormals(mesh);
            if (mesh.loadedOk) return mesh;
        }
    } else if (ext == L".gltf" && buf.size() >= 32) {
        std::string_view sv(reinterpret_cast<const char*>(buf.data()), buf.size());
        size_t b64Pos = sv.find(";base64,");
        if (b64Pos != std::string_view::npos) {
            b64Pos += 8;
            size_t b64End = sv.find('"', b64Pos);
            if (b64End != std::string_view::npos && b64End > b64Pos) {
                std::string_view b64 = sv.substr(b64Pos, b64End - b64Pos);
                std::vector<uint8_t> decoded;
                decoded.reserve((b64.size() * 3) / 4);
                uint32_t val = 0;
                int bits = -8;
                for (char c : b64) {
                    int d = -1;
                    if (c >= 'A' && c <= 'Z') d = c - 'A';
                    else if (c >= 'a' && c <= 'z') d = c - 'a' + 26;
                    else if (c >= '0' && c <= '9') d = c - '0' + 52;
                    else if (c == '+') d = 62;
                    else if (c == '/') d = 63;
                    if (d < 0) continue;
                    val = (val << 6) + static_cast<uint32_t>(d);
                    bits += 6;
                    if (bits >= 0) {
                        decoded.push_back(static_cast<uint8_t>((val >> bits) & 0xFF));
                        bits -= 8;
                    }
                }
                if (decoded.size() >= 36) {
                    mesh.formatName = L"JSON glTF 2.0 (.gltf)";
                    size_t numVerts = decoded.size() / 12;
                    for (size_t i = 0; i < numVerts; ++i) {
                        float xyz[3] = {};
                        std::memcpy(xyz, decoded.data() + i * 12, 12);
                        if (std::isfinite(xyz[0]) && std::isfinite(xyz[1]) && std::isfinite(xyz[2]) &&
                            std::abs(xyz[0]) < 1e5f && std::abs(xyz[1]) < 1e5f && std::abs(xyz[2]) < 1e5f) {
                            mesh.vertices.push_back({ xyz[0], xyz[1], xyz[2] });
                        }
                    }
                    for (size_t i = 0; i + 2 < mesh.vertices.size(); i += 3) {
                        mesh.tris.push_back({ static_cast<uint32_t>(i), static_cast<uint32_t>(i + 1), static_cast<uint32_t>(i + 2), {} });
                    }
                    FinalizeMeshBoundsAndNormals(mesh);
                    if (mesh.loadedOk) return mesh;
                }
            }
        }
    }
