#pragma once
// EsriDem.h — ESRI ASCII Grid Digital Elevation Model (.dem / .ascgrid) 3D Terrain Mesh Parser

namespace Formats {

inline bool ParseEsriDemMesh(std::string_view sv, LoadedMesh3D& mesh) {
    if (sv.find("ncols") == std::string_view::npos && sv.find("NCOLS") == std::string_view::npos) return false;
    mesh.formatName = L"ESRI ASCII Digital Elevation Model (.dem)";

    int ncols = 0, nrows = 0;
    float cellsize = 1.0f, nodata = -9999.0f;
    std::vector<float> heights;

    size_t pos = 0;
    while (pos < sv.size()) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.remove_suffix(1);
        if (ln.empty()) continue;

        char key[32] = {};
        float val = 0.0f;
        if (( (ln[0] >= 'A' && ln[0] <= 'Z') || (ln[0] >= 'a' && ln[0] <= 'z') ) &&
            sscanf_s(std::string(ln).c_str(), "%31s %f", key, (unsigned)_countof(key), &val) == 2)
        {
            for (char& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (std::strcmp(key, "ncols") == 0) ncols = static_cast<int>(val);
            else if (std::strcmp(key, "nrows") == 0) nrows = static_cast<int>(val);
            else if (std::strcmp(key, "cellsize") == 0) cellsize = std::max(0.01f, val);
            else if (std::strcmp(key, "nodata_value") == 0) nodata = val;
        } else {
            const char* cur = ln.data();
            const char* end = ln.data() + ln.size();
            while (cur < end && heights.size() < 65536) {
                char* next = nullptr;
                float h = std::strtof(cur, &next);
                if (next == cur) break;
                heights.push_back((std::fabs(h - nodata) < 0.01f) ? 0.0f : h);
                cur = next;
            }
        }
    }

    if (ncols < 2 || nrows < 2 || heights.size() < static_cast<size_t>(ncols * nrows)) return false;
    ncols = std::min(ncols, 256);
    nrows = std::min(nrows, 256);

    for (int r = 0; r < nrows; ++r) {
        for (int c = 0; c < ncols; ++c) {
            float h = heights[static_cast<size_t>(r * ncols + c)];
            mesh.vertices.push_back({ c * cellsize, h, r * cellsize });
        }
    }
    for (int r = 0; r + 1 < nrows; ++r) {
        for (int c = 0; c + 1 < ncols; ++c) {
            uint32_t i00 = static_cast<uint32_t>(r * ncols + c);
            uint32_t i10 = static_cast<uint32_t>(r * ncols + c + 1);
            uint32_t i01 = static_cast<uint32_t>((r + 1) * ncols + c);
            uint32_t i11 = static_cast<uint32_t>((r + 1) * ncols + c + 1);
            mesh.tris.push_back({ i00, i01, i10, {} });
            mesh.tris.push_back({ i10, i01, i11, {} });
        }
    }
    return !mesh.tris.empty();
}

} // namespace Formats
