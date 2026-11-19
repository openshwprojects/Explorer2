#pragma once
// Catia3dxml.h — Dassault CATIA / SOLIDWORKS 3DXML (.3dxml) reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

inline bool ParseCatia3dxml(const uint8_t* data, size_t size, const std::wstring& ext, LoadedMesh3D& mesh) {
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    // #50. CATIA / SOLIDWORKS 3DXML (ZIP or raw XML containing <Positions> & <Faces>)
    if (ext == L".3dxml") {
        std::vector<uint8_t> xmlBuf;
        if (size >= 4 && data[0] == 'P' && data[1] == 'K') {
            std::vector<ZipDeflate::ArchiveItemMeta> zEntries;
            if (ZipDeflate::ListZipEntriesFromMemory(data, size, zEntries)) {
                for (const auto& ze : zEntries) {
                    if (!ze.isDirectory && (ze.name.find(L".3drep") != std::wstring::npos ||
                                            ze.name.find(L".xml") != std::wstring::npos ||
                                            ze.name.find(L".3dxml") != std::wstring::npos))
                    {
                        std::vector<uint8_t> part;
                        if (ZipDeflate::ExtractZipEntryFromMemory(data, size, ze, part) && !part.empty()) {
                            std::string_view psv(reinterpret_cast<const char*>(part.data()), part.size());
                            if (psv.find("<Positions") != std::string_view::npos) {
                                xmlBuf = std::move(part);
                                break;
                            }
                            if (xmlBuf.empty()) xmlBuf = std::move(part);
                        }
                    }
                }
            }
        }
        if (!xmlBuf.empty()) sv = std::string_view(reinterpret_cast<const char*>(xmlBuf.data()), xmlBuf.size());
        if (sv.find("3DXML") == std::string_view::npos && sv.find("<Positions") == std::string_view::npos) return false;

        mesh.formatName = L"Dassault CATIA / SOLIDWORKS 3DXML (.3dxml)";
        size_t posTag = sv.find("<Positions");
        while (posTag != std::string_view::npos) {
            uint32_t vBase = static_cast<uint32_t>(mesh.vertices.size());
            size_t gt = sv.find('>', posTag);
            size_t closeP = sv.find("</Positions>", gt == std::string_view::npos ? posTag : gt);
            if (gt == std::string_view::npos || closeP == std::string_view::npos) break;
            std::string posStr(sv.substr(gt + 1, closeP - gt - 1));
            for (char& c : posStr) if (c == ',') c = ' ';
            const char* p = posStr.c_str();
            while (*p) {
                char* e1 = nullptr; float x = std::strtof(p, &e1); if (e1 == p) break; p = e1;
                char* e2 = nullptr; float y = std::strtof(p, &e2); if (e2 == p) break; p = e2;
                char* e3 = nullptr; float z = std::strtof(p, &e3); if (e3 == p) break; p = e3;
                mesh.vertices.push_back({ x, y, z });
            }

            size_t nextRep = sv.find("<Positions", closeP);
            std::string_view faceScope = sv.substr(closeP, (nextRep == std::string_view::npos ? sv.size() : nextRep) - closeP);
            size_t trAttr = faceScope.find("triangles=\"");
            if (trAttr != std::string_view::npos) {
                trAttr += 11;
                size_t qEnd = faceScope.find('"', trAttr);
                if (qEnd != std::string_view::npos) {
                    std::string idxStr(faceScope.substr(trAttr, qEnd - trAttr));
                    for (char& c : idxStr) if (c == ',') c = ' ';
                    const char* ip = idxStr.c_str();
                    while (*ip) {
                        char* a1 = nullptr; long i0 = std::strtol(ip, &a1, 10); if (a1 == ip) break; ip = a1;
                        char* a2 = nullptr; long i1 = std::strtol(ip, &a2, 10); if (a2 == ip) break; ip = a2;
                        char* a3 = nullptr; long i2 = std::strtol(ip, &a3, 10); if (a3 == ip) break; ip = a3;
                        if (i0 >= 0 && i1 >= 0 && i2 >= 0 &&
                            vBase + static_cast<uint32_t>(i0) < mesh.vertices.size() &&
                            vBase + static_cast<uint32_t>(i1) < mesh.vertices.size() &&
                            vBase + static_cast<uint32_t>(i2) < mesh.vertices.size())
                        {
                            mesh.tris.push_back({
                                vBase + static_cast<uint32_t>(i0),
                                vBase + static_cast<uint32_t>(i1),
                                vBase + static_cast<uint32_t>(i2), {}
                            });
                        }
                    }
                }
            }
            posTag = nextRep;
        }
        return !mesh.vertices.empty() && !mesh.tris.empty();
    }
    return false;
}

} // namespace Formats
