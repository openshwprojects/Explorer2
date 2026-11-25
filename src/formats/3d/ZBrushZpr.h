#pragma once
// ZBrushZpr.h — Pixologic / Maxon ZBrush 3D Project (.zpr) reader.
#include "../common/MeshFormatCommon.h"
#include "../common/ZBrushCommon.h"

namespace Formats {

inline bool ParseZBrushZpr(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    if (!DetailZBrush::ParseZBrushFile(data, size, mesh)) return false;
    mesh.formatName = L"Maxon ZBrush 3D Project (.zpr)";
    return true;
}

} // namespace Formats
