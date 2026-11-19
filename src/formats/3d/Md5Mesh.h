#pragma once
// Md5Mesh.h — id Tech 4 (Doom 3 / Quake 4 / Prey) Skeletal Mesh (.md5mesh) reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

inline bool ParseMd5Mesh(std::string_view sv, LoadedMesh3D& mesh) {
    std::vector<Md5Detail::Md5Joint> joints;
    std::vector<Md5Detail::Md5SubMeshRaw> subMeshes;
    if (!Md5Detail::ParseMd5MeshRaw(sv, joints, subMeshes)) return false;
    if (!Md5Detail::SkinMd5SubMeshes(joints, subMeshes, mesh)) return false;
    mesh.formatName = L"id Tech 4 MD5 Skeletal Mesh (.md5mesh)";
    mesh.animFrameCount = 1;
    return true;
}

} // namespace Formats
