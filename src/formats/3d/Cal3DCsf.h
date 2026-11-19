#pragma once
// Cal3DCsf.h — Cal3D Binary & XML Skeleton (.csf, .xsf) reader.
#include "../common/MeshFormatCommon.h"
#include "Cal3DCmf.h"

namespace Formats {

// 3. Cal3D Binary & XML Skeleton (.csf — "CSF\0" / MAGIC="XSF")
inline bool ParseCal3DCsf(const uint8_t* data, size_t size, const std::wstring& fullPath, LoadedMesh3D& mesh) {
    using namespace CalCryDetail;
    std::vector<CalBone> bones;
    if (!ParseCal3DBinarySkeleton(data, size, bones)) {
        // Check XML skeleton variant (<HEADER MAGIC="XSF"/> ... <BONE ...>)
        std::string_view sv(reinterpret_cast<const char*>(data), size);
        if (sv.find("XSF") == std::string_view::npos && sv.find("<SKELETON") == std::string_view::npos) {
            return false;
        }
        size_t pos = 0;
        while (pos < sv.size()) {
            size_t bOpen = sv.find("<BONE", pos);
            if (bOpen == std::string_view::npos) break;
            size_t bClose = sv.find("</BONE>", bOpen);
            if (bClose == std::string_view::npos) return false;
            std::string_view bBlock = sv.substr(bOpen, bClose - bOpen);
            pos = bClose + 7;

            CalBone b;
            size_t tOpen = bBlock.find("<TRANSLATION>"), tClose = bBlock.find("</TRANSLATION>");
            if (tOpen != std::string_view::npos && tClose != std::string_view::npos && tClose > tOpen + 13) {
                std::vector<float> v;
                ParseXmlFloatsSimple(bBlock.substr(tOpen + 13, tClose - (tOpen + 13)), v);
                if (v.size() >= 3) b.localPos = { v[0], v[1], v[2] };
            }
            size_t rOpen = bBlock.find("<ROTATION>"), rClose = bBlock.find("</ROTATION>");
            if (rOpen != std::string_view::npos && rClose != std::string_view::npos && rClose > rOpen + 10) {
                std::vector<float> v;
                ParseXmlFloatsSimple(bBlock.substr(rOpen + 10, rClose - (rOpen + 10)), v);
                if (v.size() >= 4) b.localRot = { v[0], v[1], v[2], v[3] };
            }
            size_t pOpen = bBlock.find("<PARENTID>"), pClose = bBlock.find("</PARENTID>");
            if (pOpen != std::string_view::npos && pClose != std::string_view::npos && pClose > pOpen + 10) {
                std::vector<float> v;
                ParseXmlFloatsSimple(bBlock.substr(pOpen + 10, pClose - (pOpen + 10)), v);
                if (!v.empty()) b.parentId = static_cast<int32_t>(v[0]);
            }
            bones.push_back(std::move(b));
        }
        if (bones.empty()) return false;
        EvaluateCalSkeletonWorld(bones);
    }

    // Check for same-stem companion .cmf or .xmf mesh first
    if (!fullPath.empty()) {
        std::wstring dir = PathUtils::GetParentPath(fullPath);
        std::wstring stem = PathUtils::GetStem(PathUtils::GetFileName(fullPath), false);
        std::vector<uint8_t> cmfBytes;
        if (PathUtils::ReadFileBytes(dir + L"\\" + stem + L".cmf", 64 * 1024 * 1024, cmfBytes) && !cmfBytes.empty()) {
            if (ParseCal3DCmf(cmfBytes.data(), cmfBytes.size(), fullPath, mesh)) {
                mesh.formatName = L"Cal3D Skeleton + Mesh (.csf + .cmf)";
                return true;
            }
        }
    }

    // Build articulated 3D octahedral bone armature directly from real bone hierarchy
    mesh.vertices.clear();
    mesh.tris.clear();
    for (size_t i = 0; i < bones.size(); ++i) {
        int32_t p = bones[i].parentId;
        if (p >= 0 && static_cast<size_t>(p) < bones.size()) {
            AppendOctahedralBonePrism(bones[static_cast<size_t>(p)].worldPos, bones[i].worldPos, mesh.vertices, mesh.tris);
        }
    }
    if (mesh.tris.empty() && bones.size() == 1) {
        // Single root bone with non-zero length vector along local +Y
        Vec3f tip = bones[0].worldRot.Rotate({ 0.0f, 1.0f, 0.0f });
        tip.x += bones[0].worldPos.x; tip.y += bones[0].worldPos.y; tip.z += bones[0].worldPos.z;
        AppendOctahedralBonePrism(bones[0].worldPos, tip, mesh.vertices, mesh.tris);
    }
    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"Cal3D Skeletal Hierarchy (.csf)";
    return true;
}

} // namespace Formats
