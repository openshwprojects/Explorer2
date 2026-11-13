#pragma once
// Pcd.h — Point Cloud Library 3D Format (.pcd) Parser

namespace Formats {

inline bool ParsePcdPointCloud(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    size_t dataPos = sv.find("\nDATA ");
    if (dataPos == std::string_view::npos) return false;
    mesh.formatName = L"Point Cloud Library (.pcd)";

    size_t eol = sv.find('\n', dataPos + 1);
    if (eol == std::string_view::npos) return false;
    std::string_view modeLine = sv.substr(dataPos + 6, eol - (dataPos + 6));
    bool isBinary = (modeLine.find("binary") != std::string_view::npos);

    size_t numPts = 0;
    size_t ptsPos = sv.find("POINTS ");
    if (ptsPos != std::string_view::npos && ptsPos < dataPos) {
        numPts = static_cast<size_t>(std::strtoul(std::string(sv.substr(ptsPos + 7, 32)).c_str(), nullptr, 10));
    }

    std::vector<Vec3f> pts;
    const char* cur = sv.data() + eol + 1;
    const char* end = sv.data() + sv.size();
    if (isBinary) {
        for (size_t i = 0; (numPts == 0 || i < numPts) && (cur + 12) <= end && pts.size() < 20000; ++i) {
            float xyz[3] = {};
            std::memcpy(xyz, cur, 12);
            if (std::isfinite(xyz[0]) && std::isfinite(xyz[1]) && std::isfinite(xyz[2])) {
                pts.push_back({ xyz[0], xyz[2], -xyz[1] });
            }
            cur += 12;
        }
    } else {
        while (cur < end && (numPts == 0 || pts.size() < numPts) && pts.size() < 20000) {
            size_t lineLen = 0;
            while (cur + lineLen < end && cur[lineLen] != '\n') ++lineLen;
            char* next = nullptr;
            float x = std::strtof(cur, &next);
            if (next != cur) {
                float y = std::strtof(next, &next);
                float z = std::strtof(next, &next);
                if (std::isfinite(x) && std::isfinite(y) && std::isfinite(z)) {
                    pts.push_back({ x, z, -y });
                }
            }
            cur += lineLen + 1;
        }
    }
    if (pts.empty()) return false;

    float minX = 1e9f, maxX = -1e9f;
    for (const auto& p : pts) { minX = std::min(minX, p.x); maxX = std::max(maxX, p.x); }
    float r = std::max(0.02f, (maxX - minX) * 0.025f);

    for (const auto& p : pts) {
        uint32_t b = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back({ p.x,     p.y + r, p.z     });
        mesh.vertices.push_back({ p.x + r, p.y - r, p.z + r });
        mesh.vertices.push_back({ p.x - r, p.y - r, p.z + r });
        mesh.vertices.push_back({ p.x,     p.y - r, p.z - r });
        mesh.tris.push_back({ b + 0, b + 1, b + 2, {} });
        mesh.tris.push_back({ b + 0, b + 2, b + 3, {} });
        mesh.tris.push_back({ b + 0, b + 3, b + 1, {} });
        mesh.tris.push_back({ b + 1, b + 3, b + 2, {} });
    }
    return true;
}

} // namespace Formats
