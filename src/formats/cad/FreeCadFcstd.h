#pragma once
// FreeCadFcstd.h — FreeCAD Parametric CAD Project (.fcstd) 3D reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

// 1. FreeCAD Document (.fcstd)
inline bool ParseFreeCadFcstd(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    using namespace CadPkgDetail;
    if (!data || size < 30 || data[0] != 'P' || data[1] != 'K') return false;
    std::vector<ZipDeflate::ArchiveItemMeta> entries;
    if (!ZipDeflate::ListZipEntriesFromMemory(data, size, entries) || entries.empty()) return false;

    // Parse Document.xml to map shape/mesh filenames (e.g. "PartShape.brp") -> Object Placement
    std::unordered_map<std::wstring, Placement3D> filePlacements;
    bool hasDocXml = false;
    for (const auto& ze : entries) {
        if (!ze.isDirectory && PathUtils::ToLower(ze.name) == L"document.xml") {
            std::vector<uint8_t> xmlBytes;
            if (ZipDeflate::ExtractZipEntryFromMemory(data, size, ze, xmlBytes, 16 * 1024 * 1024) && !xmlBytes.empty()) {
                hasDocXml = true;
                std::string_view docSv(reinterpret_cast<const char*>(xmlBytes.data()), xmlBytes.size());
                size_t objPos = 0;
                while ((objPos = docSv.find("<Object ", objPos)) != std::string_view::npos) {
                    size_t objEnd = docSv.find("</Object>", objPos);
                    if (objEnd == std::string_view::npos) break;
                    std::string_view objBlk = docSv.substr(objPos, objEnd - objPos);
                    objPos = objEnd + 9;

                    Placement3D pl;
                    size_t plPos = objBlk.find("<PropertyPlacement");
                    if (plPos != std::string_view::npos) {
                        size_t plEnd = objBlk.find('>', plPos);
                        if (plEnd != std::string_view::npos) {
                            std::string_view plTag = objBlk.substr(plPos, plEnd - plPos + 1);
                            pl.px = ParseAttrFloat(plTag, "Px", 0.0f);
                            pl.py = ParseAttrFloat(plTag, "Py", 0.0f);
                            pl.pz = ParseAttrFloat(plTag, "Pz", 0.0f);
                            pl.qx = ParseAttrFloat(plTag, "Q0", 0.0f);
                            pl.qy = ParseAttrFloat(plTag, "Q1", 0.0f);
                            pl.qz = ParseAttrFloat(plTag, "Q2", 0.0f);
                            pl.qw = ParseAttrFloat(plTag, "Q3", 1.0f);
                        }
                    }

                    for (std::string_view refTagName : { std::string_view("<Part "), std::string_view("<Mesh ") }) {
                        size_t fPos = objBlk.find(refTagName);
                        if (fPos != std::string_view::npos) {
                            size_t fEnd = objBlk.find('>', fPos);
                            if (fEnd != std::string_view::npos) {
                                std::string_view fileAttr = XmlAttrValue(objBlk.substr(fPos, fEnd - fPos + 1), "file");
                                if (!fileAttr.empty()) {
                                    std::wstring wName;
                                    for (char ch : fileAttr) wName.push_back(static_cast<wchar_t>(std::tolower(static_cast<unsigned char>(ch))));
                                    filePlacements[wName] = pl;
                                }
                            }
                        }
                    }
                }
            }
            break;
        }
    }
    if (!hasDocXml) return false;

    mesh.vertices.clear();
    mesh.tris.clear();
    for (const auto& ze : entries) {
        if (ze.isDirectory) continue;
        std::wstring lowerName = PathUtils::ToLower(ze.name);
        std::wstring ext = PathUtils::ToLower(PathUtils::GetExtension(lowerName));
        if (ext != L".brp" && ext != L".brep" && ext != L".bms") continue;

        std::vector<uint8_t> partBytes;
        if (!ZipDeflate::ExtractZipEntryFromMemory(data, size, ze, partBytes, 64 * 1024 * 1024) || partBytes.empty()) continue;

        LoadedMesh3D sub;
        bool okSub = false;
        if (ext == L".bms") {
            okSub = ParseFreeCadBinaryMeshStream(partBytes.data(), partBytes.size(), sub);
        } else {
            okSub = ParseCadKernels200(partBytes.data(), partBytes.size(), L".brep", sub);
        }
        if (okSub && !sub.vertices.empty() && !sub.tris.empty()) {
            Placement3D pl;
            auto itPl = filePlacements.find(lowerName);
            if (itPl != filePlacements.end()) pl = itPl->second;
            AppendSubMeshPlaced(sub, pl, mesh);
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"FreeCAD Parametric CAD Project (.fcstd)";
    return true;
}

} // namespace Formats
