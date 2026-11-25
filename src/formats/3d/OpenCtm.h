#pragma once
// OpenCtm.h — OpenCTM RAW + MG1 + MG2 LZMA1 3D Mesh (.ctm) reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {
namespace BspGeoDetail {

inline bool ReadCtmLzmaPackedWords(
    const uint8_t* data,
    size_t size,
    size_t& pos,
    const char expectedTag[4],
    uint32_t count,
    uint32_t elemCols,
    bool isSignedZigZag,
    std::vector<uint32_t>& outWords)
{
    if (pos + 13 > size || std::memcmp(data + pos, expectedTag, 4) != 0) return false;
    uint32_t packedSize = RdU32LE(data + pos + 4);
    uint8_t  lzmaProps  = data[pos + 8];
    uint32_t dictSize   = RdU32LE(data + pos + 9);
    pos += 13;
    if (packedSize > size - pos) return false;

    uint64_t totalWords = static_cast<uint64_t>(count) * elemCols;
    uint64_t unpBytes   = totalWords * 4ULL;
    if (totalWords == 0 || unpBytes > 128 * 1024 * 1024) return false;

    std::vector<uint8_t> planes;
    if (!StreamCodecs::DecompressLzma1(lzmaProps, dictSize, data + pos, packedSize,
                                       planes, unpBytes, true, static_cast<size_t>(unpBytes) + 64) ||
        planes.size() < static_cast<size_t>(unpBytes))
    {
        return false;
    }
    pos += packedSize;

    size_t tw = static_cast<size_t>(totalWords);
    outWords.resize(tw);
    for (size_t i = 0; i < tw; ++i) {
        // OpenCTM plane 0 = MSB (bits 31..24), plane 3 = LSB (bits 7..0)
        uint32_t v = (static_cast<uint32_t>(planes[0 * tw + i]) << 24) |
                     (static_cast<uint32_t>(planes[1 * tw + i]) << 16) |
                     (static_cast<uint32_t>(planes[2 * tw + i]) << 8)  |
                      static_cast<uint32_t>(planes[3 * tw + i]);
        if (isSignedZigZag) {
            int32_t sv = ((v & 1u) != 0) ? -static_cast<int32_t>((v + 1u) >> 1) : static_cast<int32_t>(v >> 1);
            outWords[i] = static_cast<uint32_t>(sv);
        } else {
            outWords[i] = v;
        }
    }
    return true;
}

inline void RestoreOpenCtmIndices(uint32_t tCount, std::vector<uint32_t>& idx) {
    for (uint32_t i = 0; i < tCount; ++i) {
        if (i >= 1) idx[i * 3 + 0] += idx[(i - 1) * 3 + 0];
        idx[i * 3 + 2] += idx[i * 3 + 0];
        if (i >= 1 && idx[i * 3 + 0] == idx[(i - 1) * 3 + 0])
            idx[i * 3 + 1] += idx[(i - 1) * 3 + 1];
        else
            idx[i * 3 + 1] += idx[i * 3 + 0];
    }
}

inline bool ParseOpenCtmMesh(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    if (!data || size < 32 || std::memcmp(data, "OCTM", 4) != 0) return false;
    uint32_t fmtVer = RdU32LE(data + 4);
    if (fmtVer == 0 || fmtVer > 10) return false;

    bool isRaw = (std::memcmp(data + 8, "RAW\0", 4) == 0 || std::memcmp(data + 8, "RAW ", 4) == 0);
    bool isMg1 = (std::memcmp(data + 8, "MG1\0", 4) == 0);
    bool isMg2 = (std::memcmp(data + 8, "MG2\0", 4) == 0);
    if (!isRaw && !isMg1 && !isMg2) return false;

    uint32_t vCount = RdU32LE(data + 12);
    uint32_t tCount = RdU32LE(data + 16);
    if (vCount < 3 || tCount == 0 || vCount > 2000000 || tCount > 4000000) return false;

    mesh.vertices.clear();
    mesh.tris.clear();

    if (isRaw) {
        size_t p = 0;
        if (size >= 36) {
            uint32_t cLen36 = RdU32LE(data + 32);
            if (cLen36 <= size - 36 && 36 + cLen36 + 4 <= size && std::memcmp(data + 36 + cLen36, "INDX", 4) == 0) {
                p = 36 + cLen36;
            }
        }
        if (p == 0) {
            uint32_t cLen32 = RdU32LE(data + 28);
            if (cLen32 <= size - 32 && 32 + cLen32 + 4 <= size && std::memcmp(data + 32 + cLen32, "INDX", 4) == 0) {
                p = 32 + cLen32;
            } else {
                return false;
            }
        }

        if (p + 4 + static_cast<size_t>(tCount) * 12 + 4 + static_cast<size_t>(vCount) * 12 > size) return false;
        p += 4; // "INDX"
        mesh.tris.reserve(tCount);
        for (uint32_t t = 0; t < tCount; ++t) {
            uint32_t i0 = RdU32LE(data + p + static_cast<size_t>(t) * 12 + 0);
            uint32_t i1 = RdU32LE(data + p + static_cast<size_t>(t) * 12 + 4);
            uint32_t i2 = RdU32LE(data + p + static_cast<size_t>(t) * 12 + 8);
            if (i0 >= vCount || i1 >= vCount || i2 >= vCount) { mesh.tris.clear(); return false; }
            mesh.tris.push_back({ i0, i1, i2, {} });
        }
        p += static_cast<size_t>(tCount) * 12;
        if (std::memcmp(data + p, "VERT", 4) != 0) {
            mesh.tris.clear();
            return false;
        }
        p += 4; // "VERT"
        mesh.vertices.reserve(vCount);
        for (uint32_t v = 0; v < vCount; ++v) {
            float fx = RdF32LE(data + p + static_cast<size_t>(v) * 12 + 0);
            float fy = RdF32LE(data + p + static_cast<size_t>(v) * 12 + 4);
            float fz = RdF32LE(data + p + static_cast<size_t>(v) * 12 + 8);
            if (!std::isfinite(fx) || !std::isfinite(fy) || !std::isfinite(fz)) {
                mesh.vertices.clear();
                mesh.tris.clear();
                return false;
            }
            mesh.vertices.push_back({ fx, fy, fz });
        }
        mesh.formatName = L"OpenCTM 3D Mesh (.ctm)";
        return !mesh.vertices.empty() && !mesh.tris.empty();
    }

    if (size < 36) return false;
    uint32_t cLen = RdU32LE(data + 32);
    if (cLen > size - 36) return false;
    size_t pos = 36 + cLen;

    if (isMg1) {
        std::vector<uint32_t> idxWords, vertWords;
        if (!ReadCtmLzmaPackedWords(data, size, pos, "INDX", tCount, 3, false, idxWords)) return false;
        RestoreOpenCtmIndices(tCount, idxWords);
        if (!ReadCtmLzmaPackedWords(data, size, pos, "VERT", vCount, 3, false, vertWords)) return false;

        mesh.vertices.resize(vCount);
        for (uint32_t v = 0; v < vCount; ++v) {
            float fx = 0, fy = 0, fz = 0;
            std::memcpy(&fx, &vertWords[v * 3 + 0], 4);
            std::memcpy(&fy, &vertWords[v * 3 + 1], 4);
            std::memcpy(&fz, &vertWords[v * 3 + 2], 4);
            if (!std::isfinite(fx) || !std::isfinite(fy) || !std::isfinite(fz)) return false;
            mesh.vertices[v] = { fx, fy, fz };
        }
        mesh.tris.reserve(tCount);
        for (uint32_t t = 0; t < tCount; ++t) {
            uint32_t i0 = idxWords[t * 3 + 0], i1 = idxWords[t * 3 + 1], i2 = idxWords[t * 3 + 2];
            if (i0 >= vCount || i1 >= vCount || i2 >= vCount) return false;
            mesh.tris.push_back({ i0, i1, i2, {} });
        }
        mesh.formatName = L"OpenCTM MG1 LZMA Mesh (.ctm)";
        return true;
    }

    // MG2: 44-byte 3D grid header: vPrec(4) + nPrec(4) + lb[3](12) + ub[3](12) + div[3](12), then "VERT", "GIDX", "INDX"
    if (pos + 48 <= size && std::memcmp(data + pos, "MG2H", 4) == 0) pos += 4;
    if (pos + 44 > size) return false;
    float vPrec = RdF32LE(data + pos + 0);
    float lbX   = RdF32LE(data + pos + 8),  lbY = RdF32LE(data + pos + 12), lbZ = RdF32LE(data + pos + 16);
    float ubX   = RdF32LE(data + pos + 20), ubY = RdF32LE(data + pos + 24), ubZ = RdF32LE(data + pos + 28);
    uint32_t divX = RdU32LE(data + pos + 32), divY = RdU32LE(data + pos + 36), divZ = RdU32LE(data + pos + 40);
    pos += 44;
    if (!std::isfinite(vPrec) || vPrec <= 0.0f || divX == 0 || divY == 0 || divZ == 0 ||
        divX > 65536 || divY > 65536 || divZ > 65536) return false;

    std::vector<uint32_t> intVerts, gridIdx, idxWords;
    if (!ReadCtmLzmaPackedWords(data, size, pos, "VERT", vCount, 3, true, intVerts)) return false;
    if (!ReadCtmLzmaPackedWords(data, size, pos, "GIDX", vCount, 1, false, gridIdx)) return false;
    if (!ReadCtmLzmaPackedWords(data, size, pos, "INDX", tCount, 3, false, idxWords)) return false;

    for (uint32_t i = 1; i < vCount; ++i) gridIdx[i] += gridIdx[i - 1];
    RestoreOpenCtmIndices(tCount, idxWords);

    float szX = (ubX - lbX) / static_cast<float>(divX);
    float szY = (ubY - lbY) / static_cast<float>(divY);
    float szZ = (ubZ - lbZ) / static_cast<float>(divZ);

    mesh.vertices.resize(vCount);
    int32_t prevDeltaX = 0;
    for (uint32_t i = 0; i < vCount; ++i) {
        int32_t ivx = static_cast<int32_t>(intVerts[i * 3 + 0]);
        int32_t ivy = static_cast<int32_t>(intVerts[i * 3 + 1]);
        int32_t ivz = static_cast<int32_t>(intVerts[i * 3 + 2]);
        if (i >= 1 && gridIdx[i] == gridIdx[i - 1]) ivx += prevDeltaX;
        prevDeltaX = ivx;

        uint32_t g = gridIdx[i];
        uint32_t gx = g % divX;
        uint32_t gy = (g / divX) % divY;
        uint32_t gz = g / (divX * divY);
        mesh.vertices[i] = {
            lbX + gx * szX + ivx * vPrec,
            lbY + gy * szY + ivy * vPrec,
            lbZ + gz * szZ + ivz * vPrec
        };
    }

    mesh.tris.reserve(tCount);
    for (uint32_t t = 0; t < tCount; ++t) {
        uint32_t i0 = idxWords[t * 3 + 0], i1 = idxWords[t * 3 + 1], i2 = idxWords[t * 3 + 2];
        if (i0 >= vCount || i1 >= vCount || i2 >= vCount) return false;
        mesh.tris.push_back({ i0, i1, i2, {} });
    }
    mesh.formatName = L"OpenCTM MG2 Grid-Quantized LZMA Mesh (.ctm)";
    return true;
}

} // namespace BspGeoDetail
} // namespace Formats
