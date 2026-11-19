#pragma once
// WolfEtMdm.h — Wolfenstein: Enemy Territory Skeletal Mesh (.mdm) reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

// 3. Wolfenstein: Enemy Territory Skeletal Mesh (.mdm "MDMW" v3, optionally paired with companion .mdx "MDXW")
inline bool ParseWolfEtMdm(const uint8_t* data, size_t size, const std::wstring& fullPath, LoadedMesh3D& mesh) {
    using namespace Id3SkelDetail;
    std::vector<Id3Bone3x4> bones;
    std::vector<int32_t> parents;
    size_t animFrames = 1;
    bool haveBones = false;

    if (!fullPath.empty()) {
        std::wstring dir = PathUtils::GetParentPath(fullPath);
        std::wstring stem = PathUtils::GetStem(PathUtils::GetFileName(fullPath), false);
        std::vector<uint8_t> mdxBytes;
        if (PathUtils::ReadFileBytes(dir + L"\\" + stem + L".mdx", 32 * 1024 * 1024, mdxBytes) &&
            ParseWolfMdxBonesOnly(mdxBytes.data(), mdxBytes.size(), bones, parents, animFrames)) {
            haveBones = true;
        }
        if (!haveBones && !dir.empty()) {
            WIN32_FIND_DATAW fd = {};
            HANDLE hFind = FindFirstFileW((dir + L"\\*.mdx").c_str(), &fd);
            if (hFind != INVALID_HANDLE_VALUE) {
                do {
                    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                        if (PathUtils::ReadFileBytes(dir + L"\\" + fd.cFileName, 32 * 1024 * 1024, mdxBytes) &&
                            ParseWolfMdxBonesOnly(mdxBytes.data(), mdxBytes.size(), bones, parents, animFrames)) {
                            haveBones = true;
                            break;
                        }
                    }
                } while (FindNextFileW(hFind, &fd));
                FindClose(hFind);
            }
        }
    }

    if (!ParseWolfMdmWithBones(data, size, haveBones ? &bones : nullptr, mesh)) {
        return false;
    }
    mesh.formatName = L"Wolfenstein: ET Skeletal Mesh (.mdm)";
    mesh.animFrameCount = animFrames;
    return true;
}

} // namespace Formats
