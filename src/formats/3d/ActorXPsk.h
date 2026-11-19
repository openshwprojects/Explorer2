#pragma once
// ActorXPsk.h — Unreal Engine ActorX Skeletal & Extended Meshes (.psk, .pskx) reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

inline bool ParseActorXPsk(const uint8_t* data, size_t size, const std::wstring& ext, LoadedMesh3D& mesh) {
    ActorXDetail::PskRawData psk;
    if (!ActorXDetail::ParsePskRaw(data, size, psk)) return false;
    if (!ActorXDetail::EmitPskGeometry(psk, psk.points, mesh)) return false;
    mesh.formatName = (ext == L".pskx")
        ? L"Unreal ActorX Extended Mesh (.pskx)"
        : L"Unreal ActorX Skeletal Mesh (.psk)";
    mesh.animFrameCount = 1;
    return true;
}

} // namespace Formats
