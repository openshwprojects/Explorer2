#pragma once
// Ac3dMesh.h — AC3D (.ac) 3D model reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

inline bool ParseAc3dMesh(const uint8_t* data, size_t size, const std::wstring& ext, LoadedMesh3D& mesh) {
    // #47. AC3D — "AC3Db" / "AC3Dc"
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    if (ext == L".ac" || (size >= 5 && sv.substr(0, 4) == "AC3D")) {
        if (size < 5 || sv.substr(0, 4) != "AC3D") return false;
        mesh.formatName = L"AC3D Model (.ac)";
        size_t pos = 0;
        float locX = 0.0f, locY = 0.0f, locZ = 0.0f;
        uint32_t objVBase = 0;
        while (pos < sv.size()) {
            size_t eol = sv.find('\n', pos);
            if (eol == std::string_view::npos) eol = sv.size();
            std::string ln(sv.substr(pos, eol - pos));
            pos = eol + 1;

            if (ln.rfind("OBJECT ", 0) == 0) {
                locX = locY = locZ = 0.0f;
            } else if (ln.rfind("loc ", 0) == 0) {
                sscanf_s(ln.c_str() + 4, "%f %f %f", &locX, &locY, &locZ);
            } else if (ln.rfind("numvert ", 0) == 0) {
                int nv = std::atoi(ln.c_str() + 8);
                objVBase = static_cast<uint32_t>(mesh.vertices.size());
                for (int i = 0; i < nv && pos < sv.size() && i < 500000; ++i) {
                    size_t ve = sv.find('\n', pos);
                    if (ve == std::string_view::npos) ve = sv.size();
                    std::string vln(sv.substr(pos, ve - pos));
                    pos = ve + 1;
                    float x = 0, y = 0, z = 0;
                    if (sscanf_s(vln.c_str(), "%f %f %f", &x, &y, &z) == 3) {
                        mesh.vertices.push_back({ x + locX, y + locY, z + locZ });
                    }
                }
            } else if (ln.rfind("refs ", 0) == 0) {
                int nr = std::atoi(ln.c_str() + 5);
                std::vector<uint32_t> poly;
                for (int i = 0; i < nr && pos < sv.size() && i < 64; ++i) {
                    size_t re = sv.find('\n', pos);
                    if (re == std::string_view::npos) re = sv.size();
                    std::string rln(sv.substr(pos, re - pos));
                    pos = re + 1;
                    int vidx = 0;
                    if (sscanf_s(rln.c_str(), "%d", &vidx) == 1 && vidx >= 0) {
                        poly.push_back(objVBase + static_cast<uint32_t>(vidx));
                    }
                }
                for (size_t k = 1; k + 1 < poly.size(); ++k) {
                    if (poly[0] < mesh.vertices.size() && poly[k] < mesh.vertices.size() && poly[k + 1] < mesh.vertices.size()) {
                        mesh.tris.push_back({ poly[0], poly[k], poly[k + 1], {} });
                    }
                }
            }
        }
        return !mesh.vertices.empty() && !mesh.tris.empty();
    }
    return false;
}

} // namespace Formats
