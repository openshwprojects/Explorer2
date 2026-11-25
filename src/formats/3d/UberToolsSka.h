#pragma once
// UberToolsSka.h — Ritual Entertainment UberTools Skeletal Animation (.ska, "SKAN" v3/v4) reader
// Parses bone hierarchy & Frame 0 quantized/float pose; skins companion <stem>.skb when present,
// or emits articulated 3D skeleton struts + joint octahedra standalone.

namespace Formats {

inline bool ParseUberToolsSka(
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    LoadedMesh3D& mesh)
{
    std::vector<int32_t> parents;
    std::vector<UberToolsSkel::BoneMat3x4> worldBones;
    int32_t numFrames = 0;
    if (!UberToolsSkel::ExtractUberToolsSkaFrame0(data, size, parents, worldBones, &numFrames)) {
        return false;
    }

    const int32_t version = ByteIO::RdI32LE(data + 4);
    mesh.animFrameCount = static_cast<size_t>(numFrames);

    // If a companion <stem>.skb mesh sits beside this .ska animation, skin it in Frame 0's pose!
    if (!fullPath.empty()) {
        const std::wstring dir  = PathUtils::GetParentPath(fullPath);
        const std::wstring stem = PathUtils::GetStem(fullPath, false);
        if (!dir.empty() && !stem.empty()) {
            const std::wstring skbPath = dir + L"\\" + stem + L".skb";
            std::vector<uint8_t> skbBuf;
            if (PathUtils::ReadFileBytes(skbPath, 64u * 1024u * 1024u, skbBuf) && skbBuf.size() >= 92) {
                LoadedMesh3D skinned;
                if (ParseUberToolsSkbWithBones(skbBuf.data(), skbBuf.size(), &worldBones, skinned) &&
                    !skinned.vertices.empty() && !skinned.tris.empty())
                {
                    mesh.vertices = std::move(skinned.vertices);
                    mesh.tris     = std::move(skinned.tris);
                    mesh.formatName = L"UberTools Skeletal Animation + Mesh (SKA+SKB)";
                    return true;
                }
            }
        }
    }

    if (!UberToolsSkel::EmitSkeletonPoseMesh(parents, worldBones, mesh)) {
        return false;
    }
    mesh.formatName = (version == 4)
        ? L"UberTools Skeletal Animation (SKA v4)"
        : L"UberTools Skeletal Animation (SKA v3)";
    return true;
}

} // namespace Formats
