#pragma once
// AcclaimAmc.h — Acclaim Motion Capture Data (.amc, CMU Motion Capture Standard)
// Discovers companion .asf skeleton (same stem or CMU subject prefix e.g. 07_01.amc -> 07.asf)
// and poses Frame 1 via AcclaimSkelCommon.h.

namespace Formats {

namespace DetailAcclaimAmc {

inline std::wstring FindCompanionAsfForAmc(const std::wstring& amcPath) {
    if (amcPath.size() < 5u) return {};
    const std::wstring sameStem = amcPath.substr(0, amcPath.size() - 4u) + L".asf";
    if (GetFileAttributesW(sameStem.c_str()) != INVALID_FILE_ATTRIBUTES) return sameStem;

    const size_t slash = amcPath.find_last_of(L"\\/");
    const std::wstring dir  = (slash == std::wstring::npos) ? L"" : amcPath.substr(0, slash + 1u);
    const std::wstring base = (slash == std::wstring::npos) ? amcPath : amcPath.substr(slash + 1u);
    const size_t us = base.find(L'_');
    if (us != std::wstring::npos && us > 0u) {
        const std::wstring subjAsf = dir + base.substr(0, us) + L".asf";
        if (GetFileAttributesW(subjAsf.c_str()) != INVALID_FILE_ATTRIBUTES) return subjAsf;
    }
    return {};
}

} // namespace DetailAcclaimAmc

inline bool ParseAcclaimAmc(
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    LoadedMesh3D& mesh)
{
    if (!data || size < 16u || size > (64u << 20)) return false;

    const std::wstring asfPath = DetailAcclaimAmc::FindCompanionAsfForAmc(fullPath);
    if (asfPath.empty()) return false;

    std::vector<uint8_t> asfBytes;
    if (!PathUtils::ReadFileBytes(asfPath, 32ULL << 20, asfBytes) || asfBytes.empty()) {
        return false;
    }

    AcclaimSkelCommon::AsfSkeleton skel;
    if (!AcclaimSkelCommon::ParseAsfText(
            std::string_view(reinterpret_cast<const char*>(asfBytes.data()), asfBytes.size()), skel)) {
        return false;
    }

    size_t animFrames = 1u;
    if (!AcclaimSkelCommon::ApplyAmcMotionText(
            std::string_view(reinterpret_cast<const char*>(data), size), skel, animFrames)) {
        return false;
    }

    return AcclaimSkelCommon::EmitAcclaimSkeletonMesh(
        skel, animFrames, L"Acclaim Motion Capture (AMC+ASF)", mesh);
}

} // namespace Formats
