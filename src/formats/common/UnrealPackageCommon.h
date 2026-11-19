#pragma once
// UnrealPackageCommon.h — Shared Unreal Engine 4/5 package summary & stream loader.
#include "../../ByteIO.h"

namespace Formats {

enum class UnrealPackageAssetKind {
    Unknown = 0,
    StaticMesh3D = 1,
    Texture2D = 2,
    GenericPackage = 3
};

namespace UnrealPkgDetail {

constexpr uint32_t kUnrealPackageTagLE = 0x9E2A83C1u;

inline uint16_t UeU16(const uint8_t* p) { uint16_t v = 0; std::memcpy(&v, p, 2); return v; }
inline uint32_t UeU32(const uint8_t* p) { uint32_t v = 0; std::memcpy(&v, p, 4); return v; }
inline int32_t  UeI32(const uint8_t* p) { int32_t  v = 0; std::memcpy(&v, p, 4); return v; }
inline int64_t  UeI64(const uint8_t* p) { int64_t  v = 0; std::memcpy(&v, p, 8); return v; }
inline float    UeF32(const uint8_t* p) { float    v = 0.0f; std::memcpy(&v, p, 4); return v; }
inline double   UeF64(const uint8_t* p) { double   v = 0.0; std::memcpy(&v, p, 8); return v; }

using ::Explorer2::ByteIO::HalfToFloat;

inline bool ReadUeFString(const uint8_t* data, size_t size, size_t& pos, std::string& out) {
    out.clear();
    if (pos + 4 > size) return false;
    int32_t saveNum = UeI32(data + pos);
    pos += 4;
    if (saveNum == 0) return true;
    if (saveNum > 0) {
        if (saveNum > 65536 || pos + static_cast<size_t>(saveNum) > size) return false;
        out.assign(reinterpret_cast<const char*>(data + pos), static_cast<size_t>(saveNum));
        pos += static_cast<size_t>(saveNum);
        while (!out.empty() && out.back() == '\0') out.pop_back();
        return true;
    }
    // Negative length: UTF-16LE character count
    int64_t wCount = -static_cast<int64_t>(saveNum);
    if (wCount <= 0 || wCount > 65536 || pos + static_cast<size_t>(wCount) * 2 > size) return false;
    std::wstring ws;
    ws.reserve(static_cast<size_t>(wCount));
    for (int64_t i = 0; i < wCount; ++i) {
        uint16_t wc = UeU16(data + pos + static_cast<size_t>(i) * 2);
        if (wc != 0) ws.push_back(static_cast<wchar_t>(wc));
    }
    pos += static_cast<size_t>(wCount) * 2;
    for (wchar_t c : ws) out.push_back((c < 0x80) ? static_cast<char>(c) : '?');
    return true;
}

struct ParsedUnrealSummary {
    int32_t legacyVer = 0;
    int32_t verUe4 = 0;
    int32_t verUe5 = 0;
    uint32_t pkgFlags = 0;
    int32_t totalHeaderSize = 0;
    int32_t nameCount = 0;
    int32_t nameOffset = 0;
    int32_t exportCount = 0;
    int32_t exportOffset = 0;
    int32_t importCount = 0;
    int32_t importOffset = 0;
    std::vector<std::string> names;
};

inline bool ParseUnrealPackageSummary(const uint8_t* data, size_t size, ParsedUnrealSummary& out) {
    out = {};
    if (!data || size < 64 || UeU32(data) != kUnrealPackageTagLE) return false;

    size_t pos = 4;
    out.legacyVer = UeI32(data + pos); pos += 4;
    if (out.legacyVer >= 0 || out.legacyVer < -12) return false; // Reject UE1/UE2/UE3 or garbage
    if (out.legacyVer != -4) {
        if (pos + 4 > size) return false;
        pos += 4; // LegacyUE3Version
    }
    if (pos + 8 > size) return false;
    out.verUe4 = UeI32(data + pos); pos += 4;
    if (out.legacyVer <= -8) {
        if (pos + 4 > size) return false;
        out.verUe5 = UeI32(data + pos); pos += 4;
    }
    if (pos + 4 > size) return false;
    pos += 4; // FileVersionLicenseeUE

    if (out.legacyVer <= -2) {
        if (pos + 4 > size) return false;
        int32_t customCnt = UeI32(data + pos); pos += 4;
        if (customCnt < 0 || customCnt > 4096) return false;
        if (out.legacyVer <= -6) {
            if (pos + static_cast<size_t>(customCnt) * 20 > size) return false;
            pos += static_cast<size_t>(customCnt) * 20;
        } else if (out.legacyVer == -5) {
            for (int32_t i = 0; i < customCnt; ++i) {
                if (pos + 20 > size) return false;
                pos += 20;
                std::string fn;
                if (!ReadUeFString(data, size, pos, fn)) return false;
            }
        } else {
            if (pos + static_cast<size_t>(customCnt) * 8 > size) return false;
            pos += static_cast<size_t>(customCnt) * 8;
        }
    }

    if (pos + 4 > size) return false;
    out.totalHeaderSize = UeI32(data + pos); pos += 4;
    std::string folderName;
    if (!ReadUeFString(data, size, pos, folderName)) return false;
    if (pos + 12 > size) return false;
    out.pkgFlags   = UeU32(data + pos); pos += 4;
    out.nameCount  = UeI32(data + pos); pos += 4;
    out.nameOffset = UeI32(data + pos); pos += 4;

    if (out.nameCount <= 0 || out.nameCount > 500000) return false;
    if (out.nameOffset < static_cast<int32_t>(pos) || static_cast<size_t>(out.nameOffset) >= size) return false;

    if (out.verUe5 >= 1008) { // ADD_SOFTOBJECTPATH_LIST
        if (pos + 8 > size) return false;
        pos += 8;
    }
    if ((out.pkgFlags & 0x80000000u) == 0 && out.verUe4 >= 516) {
        std::string locId;
        if (!ReadUeFString(data, size, pos, locId)) return false;
    }
    if (out.verUe4 >= 459) {
        if (pos + 8 > size) return false;
        pos += 8; // GatherableTextDataCount + Offset
    }
    if (pos + 16 > size) return false;
    out.exportCount  = UeI32(data + pos); pos += 4;
    out.exportOffset = UeI32(data + pos); pos += 4;
    out.importCount  = UeI32(data + pos); pos += 4;
    out.importOffset = UeI32(data + pos); pos += 4;

    if (out.exportCount < 0 || out.exportCount > 500000 || out.importCount < 0 || out.importCount > 500000) {
        return false;
    }

    // Parse NameMap (auto-detecting 4-byte hash trailer used in UE4.12+ vs 0-byte trailer in UE4.0-4.11 / unversioned)
    auto tryReadNames = [&](bool with4ByteHashes) -> bool {
        out.names.clear();
        out.names.reserve(static_cast<size_t>(out.nameCount));
        size_t nPos = static_cast<size_t>(out.nameOffset);
        for (int32_t i = 0; i < out.nameCount; ++i) {
            std::string s;
            if (!ReadUeFString(data, size, nPos, s)) return false;
            if (with4ByteHashes) {
                if (nPos + 4 > size) return false;
                nPos += 4;
            }
            out.names.push_back(std::move(s));
        }
        return true;
    };

    bool preferHashes = (out.verUe4 >= 504 || out.verUe4 == 0);
    if (!tryReadNames(preferHashes)) {
        if (!tryReadNames(!preferHashes)) return false;
    }
    return !out.names.empty();
}

inline void LoadCombinedUnrealStreams(
    const uint8_t* uassetData,
    size_t uassetSize,
    const ParsedUnrealSummary& sum,
    const std::wstring& fullPath,
    std::vector<uint8_t>& outCombined,
    std::vector<uint8_t>& outUbulk)
{
    outCombined.clear();
    outUbulk.clear();
    size_t hdrCopy = uassetSize;
    std::vector<uint8_t> uexpBytes;
    if (!fullPath.empty()) {
        std::wstring dir = PathUtils::GetParentPath(fullPath);
        std::wstring stem = PathUtils::GetStem(PathUtils::GetFileName(fullPath), false);
        PathUtils::ReadFileBytes(dir + L"\\" + stem + L".uexp", 256 * 1024 * 1024, uexpBytes);
        PathUtils::ReadFileBytes(dir + L"\\" + stem + L".ubulk", 256 * 1024 * 1024, outUbulk);
    }

    if (!uexpBytes.empty()) {
        // Cooked split asset: SerialOffset in FObjectExport is relative to TotalHeaderSize
        if (sum.totalHeaderSize > 0 && static_cast<size_t>(sum.totalHeaderSize) <= uassetSize) {
            hdrCopy = static_cast<size_t>(sum.totalHeaderSize);
        }
        outCombined.reserve(hdrCopy + uexpBytes.size());
        outCombined.insert(outCombined.end(), uassetData, uassetData + hdrCopy);
        outCombined.insert(outCombined.end(), uexpBytes.begin(), uexpBytes.end());
    } else {
        outCombined.assign(uassetData, uassetData + uassetSize);
    }
}

inline bool EqualsAsciiCI(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

inline bool ContainsNameCI(const ParsedUnrealSummary& sum, std::string_view target) {
    for (const auto& n : sum.names) {
        if (EqualsAsciiCI(n, target)) return true;
    }
    return false;
}

} // namespace UnrealPkgDetail

// Fast package classifier used by FileContentPane to route .uasset / .umap to 3D, Image, or Metadata Inspector
inline UnrealPackageAssetKind ClassifyUnrealPackageMemory(const uint8_t* data, size_t size) {
    using namespace UnrealPkgDetail;
    ParsedUnrealSummary sum;
    if (!ParseUnrealPackageSummary(data, size, sum)) return UnrealPackageAssetKind::Unknown;

    bool hasTex = ContainsNameCI(sum, "Texture2D") ||
                  ContainsNameCI(sum, "PF_DXT1") || ContainsNameCI(sum, "PF_DXT3") ||
                  ContainsNameCI(sum, "PF_DXT5") || ContainsNameCI(sum, "PF_BC4") ||
                  ContainsNameCI(sum, "PF_BC5")  || ContainsNameCI(sum, "PF_B8G8R8A8") ||
                  ContainsNameCI(sum, "PF_R8G8B8A8") || ContainsNameCI(sum, "PF_FloatRGBA");
    bool hasMesh = ContainsNameCI(sum, "StaticMesh") || ContainsNameCI(sum, "StaticMeshRenderData");
    if (hasMesh && !hasTex) return UnrealPackageAssetKind::StaticMesh3D;
    if (hasTex && !hasMesh) return UnrealPackageAssetKind::Texture2D;
    if (hasMesh) return UnrealPackageAssetKind::StaticMesh3D;
    return UnrealPackageAssetKind::GenericPackage;
}

} // namespace Formats
