#pragma once
// WolfEtMdx.h — Wolfenstein: Enemy Territory Skeletal Animation (.mdx) reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

// 4. Wolfenstein: Enemy Territory Skeletal Animation (.mdx "MDXW" v2 — skins companion .mdm or renders articulated bone rig)
inline bool ParseWolfEtMdx(const uint8_t* data, size_t size, const std::wstring& fullPath, LoadedMesh3D& mesh) {
    using namespace Id3SkelDetail;
    std::vector<Id3Bone3x4> bones;
    std::vector<int32_t> parents;
    size_t animFrames = 1;
    if (!ParseWolfMdxBonesOnly(data, size, bones, parents, animFrames)) return false;

    // 1. Deform companion .mdm in the same folder if available
    if (!fullPath.empty()) {
        std::wstring dir = PathUtils::GetParentPath(fullPath);
        std::wstring stem = PathUtils::GetStem(PathUtils::GetFileName(fullPath), false);
        std::vector<uint8_t> mdmBytes;
        if (PathUtils::ReadFileBytes(dir + L"\\" + stem + L".mdm", 64 * 1024 * 1024, mdmBytes) &&
            ParseWolfMdmWithBones(mdmBytes.data(), mdmBytes.size(), &bones, mesh))
        {
            mesh.formatName = L"Wolfenstein: ET Skeletal Anim (.mdx + .mdm)";
            mesh.animFrameCount = animFrames;
            return true;
        }
    }

    // 2. Standalone .mdx: build articulated 3D octahedral skeleton from Frame 0 bones
    for (size_t b = 0; b < bones.size(); ++b) {
        int32_t p = parents[b];
        Vec3f curPos = Id3ZUpToViewport(bones[b].m[0][3], bones[b].m[1][3], bones[b].m[2][3]);
        if (p >= 0 && static_cast<size_t>(p) < bones.size() && static_cast<size_t>(p) != b) {
            Vec3f parPos = Id3ZUpToViewport(bones[static_cast<size_t>(p)].m[0][3], bones[static_cast<size_t>(p)].m[1][3], bones[static_cast<size_t>(p)].m[2][3]);
            AppendOctahedralBonePrism(parPos, curPos, mesh);
        } else {
            // Root stub along bone forward axis
            Vec3f tip = Id3ZUpToViewport(
                bones[b].m[0][3] + bones[b].m[0][0] * 4.0f,
                bones[b].m[1][3] + bones[b].m[1][0] * 4.0f,
                bones[b].m[2][3] + bones[b].m[2][0] * 4.0f);
            AppendOctahedralBonePrism(curPos, tip, mesh);
        }
    }
    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"Wolfenstein: ET Skeletal Rig (.mdx)";
    mesh.animFrameCount = animFrames;
    return true;
}

} // namespace Formats
