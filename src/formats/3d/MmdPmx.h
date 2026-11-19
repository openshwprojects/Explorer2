#pragma once
// MmdPmx.h — MikuMikuDance PMX 2.0/2.1 Polygon Model (.pmx) reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

inline bool ParseMmdPmx(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    if (!data || size < 32) return false;
    if (std::memcmp(data, "PMX ", 4) != 0) return false;

    float ver = 0.0f;
    std::memcpy(&ver, data + 4, 4);
    if (!std::isfinite(ver) || ver < 1.99f || ver > 2.2f) return false;

    uint8_t globalsCount = data[8];
    if (globalsCount < 8 || 9 + static_cast<size_t>(globalsCount) > size) return false;
    const uint8_t* globals = data + 9;
    const uint8_t addVec4Count = globals[1];
    const uint8_t vertIdxSize  = globals[2];
    const uint8_t boneIdxSize  = globals[5];
    if (addVec4Count > 8) return false;
    if ((vertIdxSize != 1 && vertIdxSize != 2 && vertIdxSize != 4) ||
        (boneIdxSize != 1 && boneIdxSize != 2 && boneIdxSize != 4)) {
        return false;
    }

    size_t pos = 9 + static_cast<size_t>(globalsCount);
    // Skip 4 text fields: ModelNameJP, ModelNameEN, CommentJP, CommentEN
    for (int t = 0; t < 4; ++t) {
        if (pos + 4 > size) return false;
        int32_t byteLen = 0;
        std::memcpy(&byteLen, data + pos, 4);
        pos += 4;
        if (byteLen < 0 || static_cast<size_t>(byteLen) > size - pos) return false;
        pos += static_cast<size_t>(byteLen);
    }

    if (pos + 4 > size) return false;
    int32_t numVerts = 0;
    std::memcpy(&numVerts, data + pos, 4);
    pos += 4;
    if (numVerts <= 0 || numVerts > 8000000) return false;

    mesh.vertices.resize(static_cast<size_t>(numVerts));
    const size_t addVecBytes = static_cast<size_t>(addVec4Count) * 16;

    for (int32_t i = 0; i < numVerts; ++i) {
        // pos(12) + normal(12) + uv(8) + addVec4(16*N) + deformType(1)
        if (pos + 32 + addVecBytes + 1 > size) return false;
        float vx = 0.0f, vy = 0.0f, vz = 0.0f;
        std::memcpy(&vx, data + pos + 0, 4);
        std::memcpy(&vy, data + pos + 4, 4);
        std::memcpy(&vz, data + pos + 8, 4);
        if (!std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(vz)) return false;
        mesh.vertices[static_cast<size_t>(i)] = MmdDetail::MmdToViewport(vx, vy, vz);
        pos += 32 + addVecBytes;

        uint8_t deformType = data[pos++];
        size_t weightBytes = 0;
        switch (deformType) {
        case 0: // BDEF1
            weightBytes = static_cast<size_t>(boneIdxSize);
            break;
        case 1: // BDEF2
            weightBytes = static_cast<size_t>(boneIdxSize) * 2 + 4;
            break;
        case 2: // BDEF4
        case 4: // QDEF (PMX 2.1)
            weightBytes = static_cast<size_t>(boneIdxSize) * 4 + 16;
            break;
        case 3: // SDEF
            weightBytes = static_cast<size_t>(boneIdxSize) * 2 + 40;
            break;
        default:
            return false;
        }
        // Plus 4-byte edgeScale at end of vertex
        if (pos + weightBytes + 4 > size) return false;
        pos += weightBytes + 4;
    }

    if (pos + 4 > size) return false;
    int32_t numIndices = 0;
    std::memcpy(&numIndices, data + pos, 4);
    pos += 4;
    if (numIndices < 3 || (numIndices % 3) != 0 || numIndices > 24000000) return false;
    if (static_cast<uint64_t>(numIndices) * static_cast<uint64_t>(vertIdxSize) > size - pos) return false;

    const int32_t nTris = numIndices / 3;
    mesh.tris.reserve(static_cast<size_t>(nTris));
    for (int32_t t = 0; t < nTris; ++t) {
        int32_t a = 0, b = 0, c = 0;
        if (!MmdDetail::ReadPmxVarIndex(data, size, pos, vertIdxSize, true, a) ||
            !MmdDetail::ReadPmxVarIndex(data, size, pos, vertIdxSize, true, b) ||
            !MmdDetail::ReadPmxVarIndex(data, size, pos, vertIdxSize, true, c)) {
            return false;
        }
        if (a < 0 || a >= numVerts || b < 0 || b >= numVerts || c < 0 || c >= numVerts) return false;
        if (a == b || b == c || c == a) continue;
        // Flip (a, c, b) for Z-mirrored right-handed CCW winding
        mesh.tris.push_back({ static_cast<uint32_t>(a), static_cast<uint32_t>(c), static_cast<uint32_t>(b), {} });
    }

    if (mesh.tris.empty()) return false;
    mesh.formatName = (ver >= 2.05f)
        ? L"MikuMikuDance PMX 2.1 Model (.pmx)"
        : L"MikuMikuDance PMX 2.0 Model (.pmx)";
    return true;
}

} // namespace Formats
