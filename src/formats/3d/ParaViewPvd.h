#pragma once
// ParaViewPvd.h — ParaView Data Multi-Block Collection (.pvd) reader.
#include "../common/MeshFormatCommon.h"
#include "VtkXmlUnstructured.h"
#include "VtkXmlPolyData.h"
#include "VtkXmlImageData.h"
#include "VtkXmlRectilinear.h"
#include "VtkXmlStructured.h"

namespace Formats {

E2_DRACO_NOINLINE inline bool ParseParaViewPvd(
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    LoadedMesh3D& mesh,
    int pvdDepth = 0)
{
    using namespace VtkXmlDetail;
    if (!data || size < 32 || pvdDepth > 2) return false;

    std::string_view fullSv(reinterpret_cast<const char*>(data), size);
    size_t vfPos = fullSv.find("<VTKFile");
    if (vfPos == std::string_view::npos || vfPos > 4096) return false;
    size_t vfGt = fullSv.find('>', vfPos);
    if (vfGt == std::string_view::npos) return false;
    std::string_view vfTag = fullSv.substr(vfPos, vfGt - vfPos + 1);

    std::string rootType = GetXmlAttr(vfTag, "type");
    if (rootType.empty()) return false;

    // 1. ParaView Data Collection (.pvd): <VTKFile type="Collection">
    if (rootType == "Collection") {
        if (fullPath.empty()) return false;
        std::wstring baseDir = PathUtils::GetParentPath(fullPath);
        std::string_view collXml = FindXmlElementBody(fullSv, "Collection");
        if (collXml.empty()) return false;

        struct PvdEntry { double ts = 0.0; std::string file; };
        std::vector<PvdEntry> dsList;
        std::set<double> distinctTimesteps;

        size_t dp = 0;
        while ((dp = collXml.find("<DataSet", dp)) != std::string_view::npos) {
            size_t gt = collXml.find('>', dp + 8);
            if (gt == std::string_view::npos) break;
            std::string_view dTag = collXml.substr(dp, gt - dp + 1);
            std::string fAttr = GetXmlAttr(dTag, "file");
            std::string tsAttr = GetXmlAttr(dTag, "timestep");
            double ts = tsAttr.empty() ? 0.0 : std::strtod(tsAttr.c_str(), nullptr);
            if (!fAttr.empty()) {
                dsList.push_back({ ts, fAttr });
                distinctTimesteps.insert(ts);
            }
            dp = gt + 1;
        }
        if (dsList.empty()) return false;

        mesh.vertices.clear();
        mesh.tris.clear();
        double targetTs = dsList[0].ts;
        for (const auto& de : dsList) {
            if (std::fabs(de.ts - targetTs) > 1e-9) continue; // Load all multi-block parts of first timestep
            std::wstring wRel;
            for (char c : de.file) wRel.push_back((c == '/') ? L'\\' : static_cast<wchar_t>(static_cast<unsigned char>(c)));
            std::wstring subPath = baseDir + L"\\" + wRel;
            std::vector<uint8_t> subBytes;
            if (!PathUtils::ReadFileBytes(subPath, 128 * 1024 * 1024, subBytes) || subBytes.empty()) continue;
            LoadedMesh3D partMesh;
            const uint8_t* sd = subBytes.data();
            const size_t ss = subBytes.size();
            const bool subOk = ParseVtkXmlUnstructured(sd, ss, partMesh) ||
                               ParseVtkXmlPolyData(sd, ss, partMesh) ||
                               ParseVtkXmlImageData(sd, ss, partMesh) ||
                               ParseVtkXmlRectilinear(sd, ss, partMesh) ||
                               ParseVtkXmlStructured(sd, ss, partMesh) ||
                               ParseParaViewPvd(sd, ss, subPath, partMesh, pvdDepth + 1);
            if (subOk &&
                !partMesh.vertices.empty() && !partMesh.tris.empty())
            {
                uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
                mesh.vertices.insert(mesh.vertices.end(), partMesh.vertices.begin(), partMesh.vertices.end());
                for (const auto& t : partMesh.tris) {
                    mesh.tris.push_back({ base + t.i0, base + t.i1, base + t.i2, {} });
                }
            }
        }
        if (mesh.vertices.empty() || mesh.tris.empty()) return false;
        mesh.animFrameCount = distinctTimesteps.size();
        mesh.formatName = L"ParaView Data Multi-Block Collection (.pvd)";
        return true;
    }
    return false;
}

} // namespace Formats
