#pragma once
// SolidEdge3D.h — Siemens Solid Edge 3D Part & Sheet Metal (.par, .psm) OLE2 reader.
//
// Extracts embedded Siemens Parasolid XT (`.x_t`) and Neutral Binary (`.x_b`) B-Rep transmit streams
// from Solid Edge OLE2 Compound File `Build` / `Parasolid` / `Body` streams (raw or RFC 1950 zlib-compressed).
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
    (void)fullPath;
    if (assemblyDepth > 4) return false;
    Ole2::CompoundFile cf;
    if (!Ole2::ParseCompoundFile(data, size, cf)) return false;

    mesh.vertices.clear();
    mesh.tris.clear();

    auto tryDecodeParasolidInBuffer = [&](const uint8_t* buf, size_t len) -> bool {
        if (!buf || len < 16) return false;
        if (TryDecodeCadPayloadBuffer(buf, len, mesh)) return true;

        // Solid Edge `Build` / `Parasolid` streams may have a small stream header before `PS\0` or `**` Parasolid header
        std::string_view sv(reinterpret_cast<const char*>(buf), len);
        for (size_t off = 4; off + 32 <= std::min<size_t>(len, 256); off += 4) {
            if ((buf[off] == 'P' && buf[off + 1] == 'S' && buf[off + 2] == 0) ||
                (buf[off] == '*' && buf[off + 1] == '*')) {
                if (TryDecodeCadPayloadBuffer(buf + off, len - off, mesh)) return true;
            }
        }
        size_t pHdr = sv.find("**ABCDEFGHIJKLMNOPQRSTUVWXYZ");
        if (pHdr != std::string_view::npos && pHdr > 0) {
            if (TryDecodeCadPayloadBuffer(buf + pHdr, len - pHdr, mesh)) return true;
        }
        return false;
    };

    for (const auto& entry : cf.entries) {
        if (entry.objectType != 2 || entry.size == 0) continue;
        std::vector<uint8_t> rawStream;
        if (!Ole2::ExtractStream(cf, entry, rawStream, 64 * 1024 * 1024) || rawStream.empty()) continue;

        if (tryDecodeParasolidInBuffer(rawStream.data(), rawStream.size())) {
            continue;
        }
        std::vector<uint8_t> inflated;
        if (TryInflateCadStream(rawStream, inflated)) {
            tryDecodeParasolidInBuffer(inflated.data(), inflated.size());
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = (extLower == L".psm")
        ? L"Siemens Solid Edge Sheet Metal (.psm)"
        : L"Siemens Solid Edge Part (.par)";
    return true;
}

} // namespace Formats
