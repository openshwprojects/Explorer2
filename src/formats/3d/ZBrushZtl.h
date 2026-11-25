#pragma once
// ZBrushZtl.h — Pixologic / Maxon ZBrush 3D SubTool (.ztl) reader.
#include "../common/MeshFormatCommon.h"
#include "../common/ZBrushCommon.h"

namespace Formats {

inline bool ParseZBrushZtl(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    if (!DetailZBrush::ParseZBrushFile(data, size, mesh)) return false;
    mesh.formatName = L"Maxon ZBrush 3D SubTool (.ztl)";
    return true;
}

} // namespace Formats
