#pragma once
// UnrealAsset3DTex.h — Unreal Engine 4 & 5 Binary Package (.uasset / .umap + companion .uexp / .ubulk)
//   - FPackageFileSummary (0x9E2A83C1) + NameMap + ExportTable + split .uexp / .ubulk stream assembly
//   - Direct 3D UStaticMesh (FStaticMeshLODResources float32 & UE5 LWC float64 positions + 16/32-bit indices)
//   - Direct 2D UTexture2D (FTexturePlatformData + FByteBulkData inline/.uexp/.ubulk: DXT1/3/5, BC4, BC5, BGRA8, RGBA8, G8, FloatRGBA)

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

inline float HalfToFloatUe(uint16_t h) {
    uint32_t sign = static_cast<uint32_t>(h >> 15) << 31;
    uint32_t exp  = (h >> 10) & 0x1Fu;
    uint32_t mant = h & 0x3FFu;
    uint32_t bits = 0;
    if (exp == 0) {
        if (mant == 0) bits = sign;
        else {
            exp = 1;
            while ((mant & 0x400u) == 0) { mant <<= 1; --exp; }
            mant &= 0x3FFu;
            bits = sign | ((exp + 127 - 15) << 23) | (mant << 13);
        }
    } else if (exp == 31) {
        bits = sign | 0x7F800000u | (mant << 13);
    } else {
        bits = sign | ((exp + 127 - 15) << 23) | (mant << 13);
    }
    float f = 0.0f;
    std::memcpy(&f, &bits, 4);
    return f;
}

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

#if defined(E2_UE_ENABLE_3D)

inline bool ParseUnrealStaticMeshPackage(
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    LoadedMesh3D& mesh)
{
    using namespace UnrealPkgDetail;
    ParsedUnrealSummary sum;
    if (!ParseUnrealPackageSummary(data, size, sum)) return false;
    if (!ContainsNameCI(sum, "StaticMesh") && !ContainsNameCI(sum, "World") && !ContainsNameCI(sum, "Level")) {
        return false;
    }

    std::vector<uint8_t> combined, ubulk;
    LoadCombinedUnrealStreams(data, size, sum, fullPath, combined, ubulk);
    const size_t bodyStart = (sum.totalHeaderSize > 0 && static_cast<size_t>(sum.totalHeaderSize) < combined.size())
        ? static_cast<size_t>(sum.totalHeaderSize)
        : 64u;
    if (bodyStart + 28 > combined.size()) return false;

    // Scan serialized export payload for UE4/UE5 FStaticMeshLODResources:
    //   PositionVertexBuffer: uint32 Stride (12 = float3, 24 = UE5 LWC double3), uint32 NumVertices,
    //                         int32 BulkElemSize (= Stride), int32 BulkElemCount (= NumVertices), [NumVertices * Stride bytes]
    //   Followed within the same LOD resource by FRawStaticIndexBuffer:
    //     uint32 b32Bit (0 or 1), int32 BulkElemSize (1, 2, or 4), int32 BulkElemCount, [index bytes]
    const uint8_t* buf = combined.data();
    const size_t totalSz = combined.size();
    size_t scan = bodyStart;

    while (scan + 28 <= totalSz) {
        uint32_t stride   = UeU32(buf + scan + 0);
        uint32_t numVerts = UeU32(buf + scan + 4);
        if ((stride != 12u && stride != 24u) || numVerts < 3u || numVerts > 1500000u) {
            scan += 1;
            continue;
        }
        int32_t bulkElemSz  = UeI32(buf + scan + 8);
        int32_t bulkElemCnt = UeI32(buf + scan + 12);
        uint64_t posBytes = static_cast<uint64_t>(numVerts) * stride;
        if (static_cast<uint32_t>(bulkElemSz) != stride ||
            static_cast<uint32_t>(bulkElemCnt) != numVerts ||
            scan + 16ULL + posBytes + 12ULL > totalSz)
        {
            scan += 1;
            continue;
        }

        // Validate all vertex coordinates are finite and non-degenerate
        const uint8_t* vBase = buf + scan + 16;
        bool vertsValid = true;
        float minX = 1e30f, maxX = -1e30f, minY = 1e30f, maxY = -1e30f, minZ = 1e30f, maxZ = -1e30f;
        std::vector<Vec3f> candVerts;
        candVerts.reserve(numVerts);
        for (uint32_t v = 0; v < numVerts; ++v) {
            float vx = 0.0f, vy = 0.0f, vz = 0.0f;
            if (stride == 12u) {
                const uint8_t* vp = vBase + static_cast<size_t>(v) * 12;
                vx = UeF32(vp + 0); vy = UeF32(vp + 4); vz = UeF32(vp + 8);
            } else {
                const uint8_t* vp = vBase + static_cast<size_t>(v) * 24;
                vx = static_cast<float>(UeF64(vp + 0));
                vy = static_cast<float>(UeF64(vp + 8));
                vz = static_cast<float>(UeF64(vp + 16));
            }
            if (!std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(vz) ||
                std::fabs(vx) > 1e8f || std::fabs(vy) > 1e8f || std::fabs(vz) > 1e8f) {
                vertsValid = false;
                break;
            }
            minX = std::min(minX, vx); maxX = std::max(maxX, vx);
            minY = std::min(minY, vy); maxY = std::max(maxY, vy);
            minZ = std::min(minZ, vz); maxZ = std::max(maxZ, vz);
            // Unreal Z-up centimeters -> Y-up viewport
            candVerts.push_back({ vx * 0.01f, vz * 0.01f, -vy * 0.01f });
        }
        if (!vertsValid || ((maxX - minX) + (maxY - minY) + (maxZ - minZ) < 1e-5f)) {
            scan += 1;
            continue;
        }

        // Search forward after PositionVertexBuffer (skipping StaticMeshVertexBuffer & ColorVertexBuffer) for FRawStaticIndexBuffer
        size_t afterPos = scan + 16 + static_cast<size_t>(posBytes);
        size_t maxIdxSearch = std::min<size_t>(totalSz, afterPos + static_cast<size_t>(numVerts) * 64u + 512u);
        bool matchedIndexBuf = false;
        size_t idxEndPos = afterPos;

        for (size_t ip = afterPos; ip + 12 <= maxIdxSearch; ip += 2) {
            uint32_t is32Bit = UeU32(buf + ip + 0);
            if (is32Bit > 1u) continue;
            int32_t iElemSz  = UeI32(buf + ip + 4);
            int32_t iElemCnt = UeI32(buf + ip + 8);
            if (iElemCnt <= 0) continue;

            uint64_t idxByteLen = 0;
            uint32_t idxCount = 0;
            if (is32Bit == 0) {
                if (iElemSz == 2) { idxCount = static_cast<uint32_t>(iElemCnt); idxByteLen = static_cast<uint64_t>(idxCount) * 2ULL; }
                else if (iElemSz == 1 && (iElemCnt % 2) == 0) { idxCount = static_cast<uint32_t>(iElemCnt / 2); idxByteLen = static_cast<uint64_t>(iElemCnt); }
                else continue;
            } else {
                if (iElemSz == 4) { idxCount = static_cast<uint32_t>(iElemCnt); idxByteLen = static_cast<uint64_t>(idxCount) * 4ULL; }
                else if (iElemSz == 1 && (iElemCnt % 4) == 0) { idxCount = static_cast<uint32_t>(iElemCnt / 4); idxByteLen = static_cast<uint64_t>(iElemCnt); }
                else continue;
            }

            if (idxCount < 3u || (idxCount % 3u) != 0 || idxCount > 4000000u || ip + 12ULL + idxByteLen > totalSz) {
                continue;
            }

            const uint8_t* iBase = buf + ip + 12;
            bool allInRange = true;
            std::vector<std::array<uint32_t, 3>> candTris;
            candTris.reserve(idxCount / 3u);
            for (uint32_t t = 0; t + 2 < idxCount; t += 3) {
                uint32_t i0 = is32Bit ? UeU32(iBase + (t + 0) * 4) : UeU16(iBase + (t + 0) * 2);
                uint32_t i1 = is32Bit ? UeU32(iBase + (t + 1) * 4) : UeU16(iBase + (t + 1) * 2);
                uint32_t i2 = is32Bit ? UeU32(iBase + (t + 2) * 4) : UeU16(iBase + (t + 2) * 2);
                if (i0 >= numVerts || i1 >= numVerts || i2 >= numVerts) {
                    allInRange = false;
                    break;
                }
                if (i0 != i1 && i1 != i2 && i2 != i0) {
                    candTris.push_back({ i0, i1, i2 });
                }
            }
            if (allInRange && !candTris.empty()) {
                uint32_t baseV = static_cast<uint32_t>(mesh.vertices.size());
                mesh.vertices.insert(mesh.vertices.end(), candVerts.begin(), candVerts.end());
                for (const auto& ct : candTris) {
                    mesh.tris.push_back({ baseV + ct[0], baseV + ct[1], baseV + ct[2], {} });
                }
                matchedIndexBuf = true;
                idxEndPos = ip + 12 + static_cast<size_t>(idxByteLen);
                break;
            }
        }

        if (matchedIndexBuf) {
            scan = idxEndPos;
            // For single StaticMesh .uasset, take LOD0 only (don't stack lower LODs on top of LOD0!)
            if (ContainsNameCI(sum, "StaticMesh") && !ContainsNameCI(sum, "World")) {
                break;
            }
        } else {
            scan += 1;
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = (sum.verUe5 > 0)
        ? L"Unreal Engine 5 UStaticMesh (.uasset)"
        : L"Unreal Engine 4 UStaticMesh (.uasset)";
    return true;
}

#endif // E2_UE_ENABLE_3D

#if defined(E2_UE_ENABLE_TEX)

inline bool DecodeUnrealTexture2D(
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    ExtendedImageInfo& outInfo)
{
    using namespace UnrealPkgDetail;
    ParsedUnrealSummary sum;
    if (!ParseUnrealPackageSummary(data, size, sum)) return false;

    enum class UePixFmt { Unknown, DXT1, DXT3, DXT5, BC4, BC5, BGRA8, RGBA8, G8, FloatRGBA };
    UePixFmt fmt = UePixFmt::Unknown;
    std::wstring fmtLabel;
    for (const auto& nm : sum.names) {
        if (EqualsAsciiCI(nm, "PF_DXT1"))           { fmt = UePixFmt::DXT1;      fmtLabel = L"PF_DXT1"; break; }
        if (EqualsAsciiCI(nm, "PF_DXT3"))           { fmt = UePixFmt::DXT3;      fmtLabel = L"PF_DXT3"; break; }
        if (EqualsAsciiCI(nm, "PF_DXT5"))           { fmt = UePixFmt::DXT5;      fmtLabel = L"PF_DXT5"; break; }
        if (EqualsAsciiCI(nm, "PF_BC4"))            { fmt = UePixFmt::BC4;       fmtLabel = L"PF_BC4"; break; }
        if (EqualsAsciiCI(nm, "PF_BC5"))            { fmt = UePixFmt::BC5;       fmtLabel = L"PF_BC5"; break; }
        if (EqualsAsciiCI(nm, "PF_B8G8R8A8"))       { fmt = UePixFmt::BGRA8;     fmtLabel = L"PF_B8G8R8A8"; break; }
        if (EqualsAsciiCI(nm, "PF_R8G8B8A8"))       { fmt = UePixFmt::RGBA8;     fmtLabel = L"PF_R8G8B8A8"; break; }
        if (EqualsAsciiCI(nm, "PF_G8"))             { fmt = UePixFmt::G8;        fmtLabel = L"PF_G8"; break; }
        if (EqualsAsciiCI(nm, "PF_FloatRGBA"))      { fmt = UePixFmt::FloatRGBA; fmtLabel = L"PF_FloatRGBA"; break; }
    }
    if (fmt == UePixFmt::Unknown) return false;

    std::vector<uint8_t> combined, ubulk;
    LoadCombinedUnrealStreams(data, size, sum, fullPath, combined, ubulk);
    const size_t bodyStart = (sum.totalHeaderSize > 0 && static_cast<size_t>(sum.totalHeaderSize) < combined.size())
        ? static_cast<size_t>(sum.totalHeaderSize)
        : 64u;
    if (bodyStart + 28 > combined.size()) return false;

    auto computeExpectedMipBytes = [&](uint32_t w, uint32_t h) -> uint64_t {
        uint64_t bx = (w + 3u) / 4u, by = (h + 3u) / 4u;
        switch (fmt) {
        case UePixFmt::DXT1:
        case UePixFmt::BC4:       return bx * by * 8ULL;
        case UePixFmt::DXT3:
        case UePixFmt::DXT5:
        case UePixFmt::BC5:       return bx * by * 16ULL;
        case UePixFmt::BGRA8:
        case UePixFmt::RGBA8:     return static_cast<uint64_t>(w) * h * 4ULL;
        case UePixFmt::G8:        return static_cast<uint64_t>(w) * h;
        case UePixFmt::FloatRGBA: return static_cast<uint64_t>(w) * h * 8ULL;
        default:                  return 0;
        }
    };

    // Scan FTexturePlatformData -> Mip[0] FByteBulkData:
    //   FByteBulkData (20B): uint32 BulkDataFlags, int32 ElementCount, int32 SizeOnDisk, int64 OffsetInFile
    //   followed (for inline) or preceded by int32 SizeX, int32 SizeY (and int32 SizeZ=1 in UE4.20+)!
    const uint8_t* buf = combined.data();
    const size_t totalSz = combined.size();
    uint32_t imgW = 0, imgH = 0;
    std::vector<uint8_t> mipBytes;

    for (size_t pos = bodyStart; pos + 28 <= totalSz; pos += 4) {
        uint32_t bFlags  = UeU32(buf + pos + 0);
        int32_t elemCnt  = UeI32(buf + pos + 4);
        int32_t diskSz   = UeI32(buf + pos + 8);
        int64_t bulkOff  = UeI64(buf + pos + 12);
        if (elemCnt <= 0 || diskSz <= 0 || elemCnt > 128 * 1024 * 1024) continue;
        if ((bFlags & 0xFFFF0000u) != 0) continue; // Plausible EBulkDataFlags check

        const bool inSeparateUbulk = (bFlags & 0x0100u) != 0; // BULKDATA_PayloadInSeperateFile
        const bool isZlib          = (bFlags & 0x0002u) != 0; // BULKDATA_SerializeCompressedZLIB
        if (!isZlib && diskSz != elemCnt) continue;

        // Look right after inline payload (where UE4/UE5 FTexture2DMipMap stores int32 SizeX, int32 SizeY, [int32 SizeZ])
        // OR right before FByteBulkData (where FTexturePlatformData stores int32 SizeX, int32 SizeY)!
        size_t afterInline = inSeparateUbulk ? (pos + 20) : (pos + 20 + static_cast<size_t>(diskSz));
        if (afterInline > totalSz) continue;

        auto checkDimsAt = [&](size_t dPos, uint32_t& outW, uint32_t& outH) -> bool {
            if (dPos + 8 > totalSz) return false;
            int32_t sx = UeI32(buf + dPos + 0);
            int32_t sy = UeI32(buf + dPos + 4);
            if (sx <= 0 || sy <= 0 || sx > 16384 || sy > 16384) return false;
            return computeExpectedMipBytes(static_cast<uint32_t>(sx), static_cast<uint32_t>(sy)) == static_cast<uint64_t>(elemCnt);
        };

        uint32_t candW = 0, candH = 0;
        if (!checkDimsAt(afterInline, candW, candH)) {
            bool foundBefore = false;
            size_t lookBack = (pos >= bodyStart + 64) ? (pos - 64) : bodyStart;
            for (size_t bp = lookBack; bp + 8 <= pos; bp += 4) {
                if (checkDimsAt(bp, candW, candH)) { foundBefore = true; break; }
            }
            if (!foundBefore) continue;
        } else {
            candW = static_cast<uint32_t>(UeI32(buf + afterInline + 0));
            candH = static_cast<uint32_t>(UeI32(buf + afterInline + 4));
        }
        if (candW == 0 || candH == 0) {
            // Populate candW/candH if found via lookBack
            size_t lookBack = (pos >= bodyStart + 64) ? (pos - 64) : bodyStart;
            for (size_t bp = lookBack; bp + 8 <= pos; bp += 4) {
                int32_t sx = UeI32(buf + bp + 0), sy = UeI32(buf + bp + 4);
                if (sx > 0 && sy > 0 && sx <= 16384 && sy <= 16384 &&
                    computeExpectedMipBytes(static_cast<uint32_t>(sx), static_cast<uint32_t>(sy)) == static_cast<uint64_t>(elemCnt)) {
                    candW = static_cast<uint32_t>(sx);
                    candH = static_cast<uint32_t>(sy);
                    break;
                }
            }
        }
        if (candW == 0 || candH == 0) continue;

        const uint8_t* rawSrc = nullptr;
        if (inSeparateUbulk) {
            if (ubulk.empty()) continue;
            uint64_t uOff = (bulkOff >= 0) ? static_cast<uint64_t>(bulkOff) : 0ULL;
            if (uOff + static_cast<uint64_t>(diskSz) > ubulk.size()) {
                // In UE4.16+, BulkDataOffsetInFile can be biased by negative BulkDataStartOffset -> try 0 when single mip fits
                if (static_cast<size_t>(diskSz) <= ubulk.size()) uOff = 0;
                else continue;
            }
            rawSrc = ubulk.data() + static_cast<size_t>(uOff);
        } else {
            if (pos + 20 + static_cast<size_t>(diskSz) > totalSz) continue;
            rawSrc = buf + pos + 20;
        }

        if (isZlib) {
            if (!ZipDeflate::InflateZlib(rawSrc, static_cast<size_t>(diskSz), mipBytes, static_cast<size_t>(elemCnt) + 64) ||
                mipBytes.size() != static_cast<size_t>(elemCnt)) {
                mipBytes.clear();
                continue;
            }
        } else {
            mipBytes.assign(rawSrc, rawSrc + static_cast<size_t>(elemCnt));
        }
        imgW = candW;
        imgH = candH;
        break;
    }

    if (imgW == 0 || imgH == 0 || mipBytes.empty()) return false;

    outInfo.width = imgW;
    outInfo.height = imgH;
    outInfo.bgraPixels.assign(static_cast<size_t>(imgW) * imgH * 4, 0);

    auto decodeBc4BlockToChannel = [](const uint8_t* blk, uint8_t out16[16]) {
        uint8_t r0 = blk[0], r1 = blk[1];
        uint64_t bits = 0;
        for (int i = 0; i < 6; ++i) bits |= (static_cast<uint64_t>(blk[2 + i]) << (i * 8));
        uint8_t lut[8] = { r0, r1, 0, 0, 0, 0, 0, 255 };
        if (r0 > r1) {
            for (int k = 1; k <= 6; ++k) lut[k + 1] = static_cast<uint8_t>(((7 - k) * r0 + k * r1 + 3) / 7);
        } else {
            for (int k = 1; k <= 4; ++k) lut[k + 1] = static_cast<uint8_t>(((5 - k) * r0 + k * r1 + 2) / 5);
        }
        for (int i = 0; i < 16; ++i) out16[i] = lut[(bits >> (i * 3)) & 7u];
    };

    const uint32_t blocksX = (imgW + 3u) / 4u;
    const uint32_t blocksY = (imgH + 3u) / 4u;
    uint8_t blockRgba[64] = {};

    if (fmt == UePixFmt::DXT1 || fmt == UePixFmt::DXT3 || fmt == UePixFmt::DXT5) {
        size_t bSz = (fmt == UePixFmt::DXT1) ? 8u : 16u;
        size_t off = 0;
        for (uint32_t by = 0; by < blocksY; ++by) {
            for (uint32_t bx = 0; bx < blocksX; ++bx) {
                if (fmt == UePixFmt::DXT1) GpuTex::DecodeBC1Block(mipBytes.data() + off, blockRgba, true);
                else if (fmt == UePixFmt::DXT3) GpuTex::DecodeBC2Block(mipBytes.data() + off, blockRgba);
                else GpuTex::DecodeBC3Block(mipBytes.data() + off, blockRgba);
                GpuTex::BlitBlock4x4RgbaToPremulBgra(blockRgba, bx, by, imgW, imgH, outInfo.bgraPixels);
                off += bSz;
            }
        }
    } else if (fmt == UePixFmt::BC4 || fmt == UePixFmt::BC5) {
        size_t bSz = (fmt == UePixFmt::BC4) ? 8u : 16u;
        size_t off = 0;
        uint8_t chR[16] = {}, chG[16] = {};
        for (uint32_t by = 0; by < blocksY; ++by) {
            for (uint32_t bx = 0; bx < blocksX; ++bx) {
                decodeBc4BlockToChannel(mipBytes.data() + off, chR);
                if (fmt == UePixFmt::BC5) decodeBc4BlockToChannel(mipBytes.data() + off + 8, chG);
                for (int i = 0; i < 16; ++i) {
                    if (fmt == UePixFmt::BC4) {
                        blockRgba[i * 4 + 0] = chR[i];
                        blockRgba[i * 4 + 1] = chR[i];
                        blockRgba[i * 4 + 2] = chR[i];
                    } else {
                        // Reconstruct normal map Z from X,Y in BC5!
                        float nx = (static_cast<float>(chR[i]) / 127.5f) - 1.0f;
                        float ny = (static_cast<float>(chG[i]) / 127.5f) - 1.0f;
                        float nz = std::sqrt(std::max(0.0f, 1.0f - nx * nx - ny * ny));
                        blockRgba[i * 4 + 0] = chR[i];
                        blockRgba[i * 4 + 1] = chG[i];
                        blockRgba[i * 4 + 2] = static_cast<uint8_t>(std::clamp((nz * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f));
                    }
                    blockRgba[i * 4 + 3] = 255;
                }
                GpuTex::BlitBlock4x4RgbaToPremulBgra(blockRgba, bx, by, imgW, imgH, outInfo.bgraPixels);
                off += bSz;
            }
        }
    } else if (fmt == UePixFmt::BGRA8 || fmt == UePixFmt::RGBA8) {
        const size_t nPix = static_cast<size_t>(imgW) * imgH;
        for (size_t i = 0; i < nPix; ++i) {
            uint8_t r = (fmt == UePixFmt::BGRA8) ? mipBytes[i * 4 + 2] : mipBytes[i * 4 + 0];
            uint8_t g = mipBytes[i * 4 + 1];
            uint8_t b = (fmt == UePixFmt::BGRA8) ? mipBytes[i * 4 + 0] : mipBytes[i * 4 + 2];
            uint8_t a = mipBytes[i * 4 + 3];
            WritePremulBGRA(&outInfo.bgraPixels[i * 4], r, g, b, a);
        }
    } else if (fmt == UePixFmt::G8) {
        const size_t nPix = static_cast<size_t>(imgW) * imgH;
        for (size_t i = 0; i < nPix; ++i) {
            uint8_t l = mipBytes[i];
            WritePremulBGRA(&outInfo.bgraPixels[i * 4], l, l, l, 255);
        }
    } else if (fmt == UePixFmt::FloatRGBA) {
        const size_t nPix = static_cast<size_t>(imgW) * imgH;
        for (size_t i = 0; i < nPix; ++i) {
            float rf = HalfToFloatUe(UeU16(mipBytes.data() + i * 8 + 0));
            float gf = HalfToFloatUe(UeU16(mipBytes.data() + i * 8 + 2));
            float bf = HalfToFloatUe(UeU16(mipBytes.data() + i * 8 + 4));
            float af = HalfToFloatUe(UeU16(mipBytes.data() + i * 8 + 6));
            auto toneMap = [](float c) -> uint8_t {
                if (!std::isfinite(c) || c <= 0.0f) return 0;
                float mapped = c / (1.0f + c);
                float srgb = std::pow(mapped, 1.0f / 2.2f);
                return static_cast<uint8_t>(std::clamp(srgb * 255.0f + 0.5f, 0.0f, 255.0f));
            };
            uint8_t a8 = std::isfinite(af) ? static_cast<uint8_t>(std::clamp(af * 255.0f + 0.5f, 0.0f, 255.0f)) : 255;
            WritePremulBGRA(&outInfo.bgraPixels[i * 4], toneMap(rf), toneMap(gf), toneMap(bf), a8);
        }
    }

    outInfo.bitsPerChannel = (fmt == UePixFmt::FloatRGBA) ? 16 : 8;
    outInfo.channels = 4;
    outInfo.formatName = L"Unreal UTexture2D (" + fmtLabel + L")";
    outInfo.valid = true;
    return true;
}

#endif // E2_UE_ENABLE_TEX

} // namespace Formats
