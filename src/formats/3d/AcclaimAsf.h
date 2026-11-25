#pragma once
// AcclaimAsf.h — Acclaim Skeleton File (.asf, CMU Motion Capture Standard)
// Parses :units, :root, :bonedata, and :hierarchy via AcclaimSkelCommon.h, and applies optional
// same-stem companion .amc motion frame if present alongside the .asf file.

namespace Formats {

inline bool ParseAcclaimAsf(
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    LoadedMesh3D& mesh)
{
    if (!data || size < 24u || size > (64u << 20)) return false;

    AcclaimSkelCommon::AsfSkeleton skel;
    size_t animFrames = 1u;
    if (!AcclaimSkelCommon::ParseAsfText(
            std::string_view(reinterpret_cast<const char*>(data), size), skel)) {
        return false;
    }

    if (fullPath.size() >= 5u) {
        const std::wstring amcPath = fullPath.substr(0, fullPath.size() - 4u) + L".amc";
        std::vector<uint8_t> amcBytes;
        if (PathUtils::ReadFileBytes(amcPath, 64ULL << 20, amcBytes) && !amcBytes.empty()) {
            AcclaimSkelCommon::ApplyAmcMotionText(
                std::string_view(reinterpret_cast<const char*>(amcBytes.data()), amcBytes.size()),
                skel, animFrames);
        }
    }

    return AcclaimSkelCommon::EmitAcclaimSkeletonMesh(
        skel, animFrames, L"Acclaim Skeleton (ASF)", mesh);
}

} // namespace Formats
