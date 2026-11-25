#pragma once
// BohemiaMlodP3d.h — Bohemia Interactive OFP / ArmA / DayZ Unbinarized "MLOD" 3D Model (.p3d) reader
// Parses "MLOD" v257 container and LOD 0 geometry ("P3DM" v28.256 & legacy "SP3X" blocks).

namespace Formats {

inline bool ParseBohemiaMlodP3d(
    const uint8_t* data,
    size_t size,
    LoadedMesh3D& mesh)
{
    if (!data || size < 40) return false;
    if (std::memcmp(data, "MLOD", 4) != 0) return false;

    const uint32_t version = ByteIO::RdU32LE(data + 4u);
    const uint32_t numLods = ByteIO::RdU32LE(data + 8u);
    if (version != 257u || numLods < 1u || numLods > 64u) return false;

    const size_t lodPos = 12u;
    if (std::memcmp(data + lodPos, "P3DM", 4) == 0) {
        if (lodPos + 28u > size) return false;
        const uint32_t majorVer   = ByteIO::RdU32LE(data + lodPos + 4u);
        const uint32_t minorVer   = ByteIO::RdU32LE(data + lodPos + 8u);
        const uint32_t numPoints  = ByteIO::RdU32LE(data + lodPos + 12u);
        const uint32_t numNormals = ByteIO::RdU32LE(data + lodPos + 16u);
        const uint32_t numFaces   = ByteIO::RdU32LE(data + lodPos + 20u);
        if (majorVer != 28u || minorVer != 256u) return false;
        if (numPoints < 3u || numPoints > 1000000u || numNormals > 2000000u || numFaces < 1u || numFaces > 2000000u) {
            return false;
        }

        size_t p = lodPos + 28u;
        const uint64_t ptsBytes = static_cast<uint64_t>(numPoints) * 16ULL;
        const uint64_t nrmBytes = static_cast<uint64_t>(numNormals) * 12ULL;
        if (p + ptsBytes + nrmBytes > size) return false;

        mesh.vertices.resize(numPoints);
        for (uint32_t i = 0; i < numPoints; ++i) {
            const uint8_t* vp = data + p + static_cast<size_t>(i) * 16u;
            const float x = ByteIO::RdF32LE(vp + 0u);
            const float y = ByteIO::RdF32LE(vp + 4u);
            const float z = ByteIO::RdF32LE(vp + 8u);
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
            mesh.vertices[i] = { x, y, z };
        }
        p += static_cast<size_t>(ptsBytes + nrmBytes);

        for (uint32_t f = 0; f < numFaces; ++f) {
            // faceType (4) + 4 * VertexTable (64) + faceFlags (4) + tex\0 + mat\0 >= 74 bytes
            if (p + 74u > size) return false;
            const uint32_t fVerts = ByteIO::RdU32LE(data + p);
            if (fVerts != 3u && fVerts != 4u) return false;

            uint32_t idx[4] = {};
            for (uint32_t k = 0; k < fVerts; ++k) {
                const int32_t pi = ByteIO::RdI32LE(data + p + 4u + static_cast<size_t>(k) * 16u);
                if (pi < 0 || static_cast<uint32_t>(pi) >= numPoints) return false;
                idx[k] = static_cast<uint32_t>(pi);
            }
            p += 72u; // 4 + 64 + 4

            // Skip null-terminated textureName and materialName
            for (int s = 0; s < 2; ++s) {
                const size_t s0 = p;
                while (p < size && data[p] != 0) {
                    if (p - s0 > 512u) return false;
                    ++p;
                }
                if (p >= size) return false;
                ++p;
            }

            if (idx[0] != idx[1] && idx[1] != idx[2] && idx[0] != idx[2]) {
                mesh.tris.push_back({ idx[0], idx[1], idx[2], {} });
            }
            if (fVerts == 4u && idx[0] != idx[2] && idx[2] != idx[3] && idx[0] != idx[3]) {
                mesh.tris.push_back({ idx[0], idx[2], idx[3], {} });
            }
        }
    } else if (std::memcmp(data + lodPos, "SP3X", 4) == 0) {
        if (lodPos + 24u > size) return false;
        const uint32_t majorVer   = ByteIO::RdU32LE(data + lodPos + 4u);
        const uint32_t numPoints  = ByteIO::RdU32LE(data + lodPos + 8u);
        const uint32_t numNormals = ByteIO::RdU32LE(data + lodPos + 12u);
        const uint32_t numFaces   = ByteIO::RdU32LE(data + lodPos + 16u);
        if ((majorVer != 28u && majorVer != 29u) ||
            numPoints < 3u || numPoints > 500000u || numNormals > 1000000u || numFaces < 1u || numFaces > 1000000u) {
            return false;
        }
        size_t p = lodPos + 24u;
        const uint64_t need = static_cast<uint64_t>(numPoints) * 16ULL +
                              static_cast<uint64_t>(numNormals) * 12ULL +
                              static_cast<uint64_t>(numFaces) * 104ULL;
        if (p + need > size) return false;

        mesh.vertices.resize(numPoints);
        for (uint32_t i = 0; i < numPoints; ++i) {
            const uint8_t* vp = data + p + static_cast<size_t>(i) * 16u;
            const float x = ByteIO::RdF32LE(vp + 0u);
            const float y = ByteIO::RdF32LE(vp + 4u);
            const float z = ByteIO::RdF32LE(vp + 8u);
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
            mesh.vertices[i] = { x, y, z };
        }
        p += static_cast<size_t>(numPoints) * 16u + static_cast<size_t>(numNormals) * 12u;

        for (uint32_t f = 0; f < numFaces; ++f) {
            const uint8_t* fp = data + p + static_cast<size_t>(f) * 104u;
            const uint32_t fVerts = ByteIO::RdU32LE(fp + 32u);
            if (fVerts != 3u && fVerts != 4u) return false;
            uint32_t idx[4] = {};
            for (uint32_t k = 0; k < fVerts; ++k) {
                const int32_t pi = ByteIO::RdI32LE(fp + 36u + static_cast<size_t>(k) * 16u);
                if (pi < 0 || static_cast<uint32_t>(pi) >= numPoints) return false;
                idx[k] = static_cast<uint32_t>(pi);
            }
            if (idx[0] != idx[1] && idx[1] != idx[2] && idx[0] != idx[2]) {
                mesh.tris.push_back({ idx[0], idx[1], idx[2], {} });
            }
            if (fVerts == 4u && idx[0] != idx[2] && idx[2] != idx[3] && idx[0] != idx[3]) {
                mesh.tris.push_back({ idx[0], idx[2], idx[3], {} });
            }
        }
    } else {
        return false;
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"Bohemia Interactive MLOD Model (P3D)";
    return true;
}

} // namespace Formats
