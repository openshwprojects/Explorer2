#pragma once
// VroidVrm.h — VRoid / Metaverse Humanoid Avatar (.vrm, GLB 2.0 container) reader.
#include "../common/MeshFormatCommon.h"
#include "Glb.h"

namespace Formats {

inline bool ParseVroidVrm(const uint8_t* data, size_t size, const std::wstring& ext, LoadedMesh3D& mesh) {
    // #58. VRoid / Metaverse 3D Avatar (.vrm — GLB 2.0 binary container)
    if (ext == L".vrm") {
        bool ok = Formats::ParseGltfOrGlbMesh(data, size, mesh.filePath, ext, mesh);
        mesh.formatName = L"VRoid / glTF Humanoid Avatar (.vrm)";
        return ok;
    }
    return false;
}

} // namespace Formats
