#pragma once
// UnrealVertexMesh3d.h — Unreal Engine 1 / UT99 / Deus Ex Vertex Mesh (.3d) reader
// Supports paired Data (*_d.3d: 48-byte JSDataHeader + 16-byte JSMeshTri) and Animation
// (*_a.3d: 4-byte JSAnivHeader + 32-bit 11-11-10 or Deus Ex 64-bit 16-16-16 vertex frames),
// as well as standalone *_a.3d animated vertex files.

namespace Formats {

namespace DetailUnreal3d {

inline bool IsValidData3d(const uint8_t* d, size_t sz, uint16_t& outNumPolys, uint16_t& outNumVerts) {
    if (!d || sz < 64 || ((sz - 48u) % 16u) != 0) return false;
    const uint16_t numPolys = ByteIO::RdU16LE(d + 0);
    const uint16_t numVerts = ByteIO::RdU16LE(d + 2);
    if (numPolys < 1 || numVerts < 3) return false;
    if (48u + static_cast<size_t>(numPolys) * 16u != sz) return false;

    for (uint16_t p = 0; p < numPolys; ++p) {
        const uint8_t* tp = d + 48u + static_cast<size_t>(p) * 16u;
        const uint16_t i0 = ByteIO::RdU16LE(tp + 0);
        const uint16_t i1 = ByteIO::RdU16LE(tp + 2);
        const uint16_t i2 = ByteIO::RdU16LE(tp + 4);
        if (i0 >= numVerts || i1 >= numVerts || i2 >= numVerts) return false;
    }
    outNumPolys = numPolys;
    outNumVerts = numVerts;
    return true;
}

inline bool DecodeAnivFrame0(
    const uint8_t* aData,
    size_t aSize,
    uint16_t expectedVerts, // 0 if standalone _a.3d
    std::vector<Vec3f>& outVerts,
    size_t& outNumFrames)
{
    if (!aData || aSize < 16) return false;
    const uint16_t numFrames = ByteIO::RdU16LE(aData + 0);
    const uint16_t frameSize = ByteIO::RdU16LE(aData + 2);
    if (numFrames < 1 || frameSize < 12) return false;
    if (4u + static_cast<size_t>(numFrames) * static_cast<size_t>(frameSize) != aSize) return false;

    bool isDeusEx64 = false;
    size_t numVerts = 0;
    if (expectedVerts > 0) {
        if (frameSize == static_cast<uint32_t>(expectedVerts) * 4u) {
            numVerts = expectedVerts;
            isDeusEx64 = false;
        } else if (frameSize == static_cast<uint32_t>(expectedVerts) * 8u) {
            numVerts = expectedVerts;
            isDeusEx64 = true;
        } else {
            return false;
        }
    } else {
        if ((frameSize % 4u) != 0) return false;
        numVerts = frameSize / 4u;
        if (numVerts < 3) return false;
    }

    outVerts.clear();
    outVerts.reserve(numVerts);
    const uint8_t* fp = aData + 4u;
    for (size_t i = 0; i < numVerts; ++i) {
        if (!isDeusEx64) {
            const uint32_t packed = ByteIO::RdU32LE(fp + i * 4u);
            const int32_t ix = static_cast<int32_t>(packed << 21) >> 21;
            const int32_t iy = static_cast<int32_t>(packed << 10) >> 21;
            const int32_t iz = static_cast<int32_t>(packed) >> 22;
            // Unreal Z-up -> viewer Y-up
            outVerts.push_back({ static_cast<float>(ix) * 0.125f, static_cast<float>(iz) * 0.25f, -static_cast<float>(iy) * 0.125f });
        } else {
            const int16_t ix = ByteIO::RdI16LE(fp + i * 8u + 0u);
            const int16_t iy = ByteIO::RdI16LE(fp + i * 8u + 2u);
            const int16_t iz = ByteIO::RdI16LE(fp + i * 8u + 4u);
            outVerts.push_back({ static_cast<float>(ix) / 256.0f, static_cast<float>(iz) / 256.0f, -static_cast<float>(iy) / 256.0f });
        }
    }
    outNumFrames = numFrames;
    return true;
}

inline std::wstring CompanionUnrealPath(const std::wstring& fullPath, wchar_t fromTag, wchar_t toTag) {
    if (fullPath.size() < 5) return {};
    const size_t pos = fullPath.size() - 5; // "_d.3d" or "_a.3d"
    if (fullPath[pos] != L'_') return {};
    const wchar_t c = static_cast<wchar_t>(towlower(fullPath[pos + 1]));
    if (c != fromTag) return {};
    std::wstring res = fullPath;
    res[pos + 1] = iswupper(fullPath[pos + 1]) ? static_cast<wchar_t>(towupper(toTag)) : toTag;
    return res;
}

inline void EmitDataPolys(const uint8_t* dData, uint16_t numPolys, std::vector<MeshTriangle>& outTris) {
    outTris.reserve(numPolys);
    for (uint16_t p = 0; p < numPolys; ++p) {
        const uint8_t* tp = dData + 48u + static_cast<size_t>(p) * 16u;
        const uint32_t i0 = ByteIO::RdU16LE(tp + 0);
        const uint32_t i1 = ByteIO::RdU16LE(tp + 2);
        const uint32_t i2 = ByteIO::RdU16LE(tp + 4);
        if (i0 == i1 || i1 == i2 || i0 == i2) continue;
        outTris.push_back({ i0, i2, i1, {} });
    }
}

} // namespace DetailUnreal3d

inline bool ParseUnrealVertexMesh3d(
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    LoadedMesh3D& mesh)
{
    if (!data || size < 16) return false;

    // Case 1: Data file (*_d.3d) -> requires companion *_a.3d for 3D vertex positions
    uint16_t numPolys = 0, numVerts = 0;
    if (DetailUnreal3d::IsValidData3d(data, size, numPolys, numVerts)) {
        const std::wstring anivPath = DetailUnreal3d::CompanionUnrealPath(fullPath, L'd', L'a');
        if (anivPath.empty()) return false;
        std::vector<uint8_t> anivBytes;
        if (!PathUtils::ReadFileBytes(anivPath, 64ULL * 1024 * 1024, anivBytes)) return false;

        size_t numFrames = 1;
        if (!DetailUnreal3d::DecodeAnivFrame0(anivBytes.data(), anivBytes.size(), numVerts, mesh.vertices, numFrames)) {
            return false;
        }
        DetailUnreal3d::EmitDataPolys(data, numPolys, mesh.tris);
        if (mesh.tris.empty()) return false;
        mesh.animFrameCount = numFrames;
        mesh.formatName = L"Unreal Engine 1 Mesh (.3d)";
        return true;
    }

    // Case 2: Animation vertex file (*_a.3d) -> pair with companion *_d.3d if present, or standalone 11-11-10 mesh
    const std::wstring dataPath = DetailUnreal3d::CompanionUnrealPath(fullPath, L'a', L'd');
    if (!dataPath.empty()) {
        std::vector<uint8_t> dBytes;
        uint16_t dPolys = 0, dVerts = 0;
        if (PathUtils::ReadFileBytes(dataPath, 64ULL * 1024 * 1024, dBytes) &&
            DetailUnreal3d::IsValidData3d(dBytes.data(), dBytes.size(), dPolys, dVerts))
        {
            size_t numFrames = 1;
            if (DetailUnreal3d::DecodeAnivFrame0(data, size, dVerts, mesh.vertices, numFrames)) {
                DetailUnreal3d::EmitDataPolys(dBytes.data(), dPolys, mesh.tris);
                if (!mesh.tris.empty()) {
                    mesh.animFrameCount = numFrames;
                    mesh.formatName = L"Unreal Engine 1 Mesh (.3d)";
                    return true;
                }
            }
        }
    }

    // Standalone vertex-only *_a.3d (no companion *_d.3d): emit clean point-cloud markers (never random sequential soup!)
    size_t numFrames = 1;
    std::vector<Vec3f> verts;
    if (!DetailUnreal3d::DecodeAnivFrame0(data, size, 0, verts, numFrames)) return false;

    Vec3f bmin = verts[0], bmax = verts[0];
    for (const auto& v : verts) {
        bmin.x = std::min(bmin.x, v.x); bmin.y = std::min(bmin.y, v.y); bmin.z = std::min(bmin.z, v.z);
        bmax.x = std::max(bmax.x, v.x); bmax.y = std::max(bmax.y, v.y); bmax.z = std::max(bmax.z, v.z);
    }
    const float span = std::max({ bmax.x - bmin.x, bmax.y - bmin.y, bmax.z - bmin.z });
    if (span <= 0.25f) return false;

    const float markerRad = std::max(1e-3f, span * 0.02f);
    const size_t stride = std::max<size_t>(1u, verts.size() / 25000u);
    for (size_t i = 0; i < verts.size(); i += stride) {
        MeshBuild3D::EmitOctahedron(mesh, verts[i], markerRad);
    }
    if (mesh.tris.empty()) return false;
    mesh.animFrameCount = numFrames;
    mesh.formatName = L"Unreal Engine 1 Animated Mesh (_a.3d)";
    return true;
}

} // namespace Formats
