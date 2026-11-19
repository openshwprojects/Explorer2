#pragma once
// SolidEdge3D.h — Siemens Solid Edge 3D Part & Sheet Metal (.par, .psm) OLE2 reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

inline bool ParseSolidEdge3D(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    const std::wstring& fullPath,
    LoadedMesh3D& mesh,
    int assemblyDepth = 0)
{
    using namespace CadPkgDetail;
    if (assemblyDepth > 4) return false;
    Ole2::CompoundFile cf;
    if (!Ole2::ParseCompoundFile(data, size, cf)) return false;

    mesh.vertices.clear();
    mesh.tris.clear();

    for (const auto& entry : cf.entries) {
        if (entry.objectType != 2 || entry.size == 0) continue;
        std::vector<uint8_t> rawStream;
        if (!Ole2::ExtractStream(cf, entry, rawStream, 64 * 1024 * 1024) || rawStream.empty()) continue;

        // Try direct uncompressed stream decode first, then zlib-compressed RSe / Solid Edge segment
        if (TryDecodeCadPayloadBuffer(rawStream.data(), rawStream.size(), mesh)) {
            continue;
        }
        std::vector<uint8_t> inflated;
        if (TryInflateCadStream(rawStream, inflated)) {
            TryDecodeCadPayloadBuffer(inflated.data(), inflated.size(), mesh);
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = (extLower == L".psm")
        ? L"Siemens Solid Edge Sheet Metal (.psm)"
        : L"Siemens Solid Edge Part (.par)";
    return true;
}

} // namespace Formats
