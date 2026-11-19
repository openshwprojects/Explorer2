#pragma once
// SpaceClaimScdoc.h — ANSYS SpaceClaim / Discovery 3D CAD Document (.scdoc) reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

// 2. ANSYS SpaceClaim / Discovery Document (.scdoc)
inline bool ParseSpaceClaimScdoc(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    using namespace CadPkgDetail;
    if (!data || size < 30 || data[0] != 'P' || data[1] != 'K') return false;
    std::vector<ZipDeflate::ArchiveItemMeta> entries;
    if (!ZipDeflate::ListZipEntriesFromMemory(data, size, entries) || entries.empty()) return false;

    bool isSpaceClaimPkg = false;
    for (const auto& ze : entries) {
        std::wstring lp = PathUtils::ToLower(ze.pathInArchive);
        if (lp.find(L"spaceclaim") != std::wstring::npos || lp == L"[content_types].xml") {
            isSpaceClaimPkg = true;
            break;
        }
    }
    if (!isSpaceClaimPkg) return false;

    mesh.vertices.clear();
    mesh.tris.clear();
    for (const auto& ze : entries) {
        if (ze.isDirectory) continue;
        std::wstring ext = PathUtils::ToLower(PathUtils::GetExtension(ze.name));
        if (ext != L".sab" && ext != L".sat" && ext != L".x_t" && ext != L".x_b" &&
            ext != L".brep" && ext != L".facets" && ext != L".bin")
        {
            continue;
        }
        std::vector<uint8_t> partBytes;
        if (!ZipDeflate::ExtractZipEntryFromMemory(data, size, ze, partBytes, 64 * 1024 * 1024) || partBytes.empty()) continue;
        TryDecodeCadPayloadBuffer(partBytes.data(), partBytes.size(), mesh);
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"ANSYS SpaceClaim 3D CAD Document (.scdoc)";
    return true;
}

} // namespace Formats
