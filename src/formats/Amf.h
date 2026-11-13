#pragma once
// Amf.h — Additive Manufacturing File Format (.amf) 3D Mesh Parser

namespace Formats {

inline bool ParseAmfMesh(std::string_view sv, LoadedMesh3D& mesh) {
    if (sv.find("<amf") == std::string_view::npos) return false;
    mesh.formatName = L"Additive Manufacturing (.amf)";

    auto readInnerFloat = [](std::string_view blk, const char* openTag, const char* closeTag) -> float {
        size_t p1 = blk.find(openTag);
        if (p1 == std::string_view::npos) return 0.0f;
        p1 += std::strlen(openTag);
        size_t p2 = blk.find(closeTag, p1);
        if (p2 == std::string_view::npos) return 0.0f;
        return std::strtof(std::string(blk.substr(p1, p2 - p1)).c_str(), nullptr);
    };
    auto readInnerUInt = [](std::string_view blk, const char* openTag, const char* closeTag) -> uint32_t {
        size_t p1 = blk.find(openTag);
        if (p1 == std::string_view::npos) return 0;
        p1 += std::strlen(openTag);
        size_t p2 = blk.find(closeTag, p1);
        if (p2 == std::string_view::npos) return 0;
        return static_cast<uint32_t>(std::strtoul(std::string(blk.substr(p1, p2 - p1)).c_str(), nullptr, 10));
    };

    size_t pos = 0;
    while ((pos = sv.find("<coordinates", pos)) != std::string_view::npos) {
        size_t endC = sv.find("</coordinates>", pos);
        if (endC == std::string_view::npos) break;
        std::string_view cBlk = sv.substr(pos, endC - pos);
        float x = readInnerFloat(cBlk, "<x>", "</x>");
        float y = readInnerFloat(cBlk, "<y>", "</y>");
        float z = readInnerFloat(cBlk, "<z>", "</z>");
        mesh.vertices.push_back({ x, z, -y });
        pos = endC + 14;
    }

    pos = 0;
    while ((pos = sv.find("<triangle", pos)) != std::string_view::npos) {
        size_t endT = sv.find("</triangle>", pos);
        if (endT == std::string_view::npos) break;
        std::string_view tBlk = sv.substr(pos, endT - pos);
        uint32_t v1 = readInnerUInt(tBlk, "<v1>", "</v1>");
        uint32_t v2 = readInnerUInt(tBlk, "<v2>", "</v2>");
        uint32_t v3 = readInnerUInt(tBlk, "<v3>", "</v3>");
        if (v1 < mesh.vertices.size() && v2 < mesh.vertices.size() && v3 < mesh.vertices.size()) {
            mesh.tris.push_back({ v1, v2, v3, {} });
        }
        pos = endT + 11;
    }

    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace Formats
