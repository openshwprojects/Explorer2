#pragma once
    // 10. 3MF Parser (.3mf - ZIP containing 3D/3dmodel.model XML)
    if (ext == L".3mf") {
        std::vector<uint8_t> modelXml;
        if (ZipDeflate::ExtractZipFileByName(fullPath, L"3D/3dmodel.model", modelXml) && !modelXml.empty()) {
            mesh.formatName = L"3D Manufacturing Format (.3mf)";
            std::string_view sv(reinterpret_cast<const char*>(modelXml.data()), modelXml.size());
            size_t p = 0;
            while ((p = sv.find("<vertex ", p)) != std::string_view::npos) {
                size_t gt = sv.find('>', p);
                if (gt == std::string_view::npos) break;
                std::string_view tag = sv.substr(p, gt - p);
                auto attrF = [&](const char* k) -> float {
                    size_t kp = tag.find(k);
                    if (kp == std::string_view::npos) return 0.0f;
                    kp += std::strlen(k);
                    return std::strtof(std::string(tag.substr(kp, 24)).c_str(), nullptr);
                };
                mesh.vertices.push_back({ attrF("x=\""), attrF("z=\""), -attrF("y=\"") });
                p = gt + 1;
            }
            p = 0;
            while ((p = sv.find("<triangle ", p)) != std::string_view::npos) {
                size_t gt = sv.find('>', p);
                if (gt == std::string_view::npos) break;
                std::string_view tag = sv.substr(p, gt - p);
                auto attrU = [&](const char* k) -> uint32_t {
                    size_t kp = tag.find(k);
                    if (kp == std::string_view::npos) return 0;
                    kp += std::strlen(k);
                    return static_cast<uint32_t>(std::strtoul(std::string(tag.substr(kp, 16)).c_str(), nullptr, 10));
                };
                uint32_t v1 = attrU("v1=\""), v2 = attrU("v2=\""), v3 = attrU("v3=\"");
                if (v1 < mesh.vertices.size() && v2 < mesh.vertices.size() && v3 < mesh.vertices.size()) {
                    mesh.tris.push_back({ v1, v2, v3, {} });
                }
                p = gt + 1;
            }
            FinalizeMeshBoundsAndNormals(mesh);
            if (mesh.loadedOk) return mesh;
        }
    }

