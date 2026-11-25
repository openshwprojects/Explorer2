#pragma once
// ImagineTddd.h — Impulse TurboSilver & Imagine Amiga IFF 3D Object (.tddd / .iob, "FORM".."TDDD")
// Parses hierarchical OBJ / DESC chunks with 16.16 fixed-point POSI, AXIS, PNTS, EDGE, and edge-indexed FACEs.

namespace Formats {

namespace DetailImagineTddd {

inline bool ParseDescChunk(
    const uint8_t* descData,
    size_t descLen,
    LoadedMesh3D& mesh)
{
    float posX = 0.0f, posY = 0.0f, posZ = 0.0f;
    float axis[3][3] = {
        { 1.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f }
    };
    std::vector<Vec3f> localPts;
    std::vector<std::pair<uint16_t, uint16_t>> edges;
    struct EdgeFace { uint16_t e0, e1, e2; };
    std::vector<EdgeFace> faces;

    constexpr float kFractScale = 1.0f / 65536.0f;
    size_t p = 0;
    while (p + 8u <= descLen) {
        const uint32_t cLen = ByteIO::RdU32BE(descData + p + 4u);
        if (cLen > descLen - (p + 8u)) return false;
        const uint8_t* pay = descData + p + 8u;

        if (std::memcmp(descData + p, "POSI", 4) == 0) {
            if (cLen < 12u) return false;
            posX = static_cast<float>(ByteIO::RdI32BE(pay + 0u)) * kFractScale;
            posY = static_cast<float>(ByteIO::RdI32BE(pay + 4u)) * kFractScale;
            posZ = static_cast<float>(ByteIO::RdI32BE(pay + 8u)) * kFractScale;
        } else if (std::memcmp(descData + p, "AXIS", 4) == 0) {
            if (cLen < 36u) return false;
            for (int a = 0; a < 3; ++a) {
                for (int c = 0; c < 3; ++c) {
                    axis[a][c] = static_cast<float>(ByteIO::RdI32BE(pay + static_cast<size_t>(a * 3 + c) * 4u)) * kFractScale;
                }
            }
        } else if (std::memcmp(descData + p, "PNTS", 4) == 0) {
            if (cLen < 2u) return false;
            const uint16_t pCount = ByteIO::RdU16BE(pay);
            if (2u + static_cast<size_t>(pCount) * 12u > cLen) return false;
            localPts.resize(pCount);
            for (uint16_t i = 0; i < pCount; ++i) {
                const uint8_t* vp = pay + 2u + static_cast<size_t>(i) * 12u;
                localPts[i] = {
                    static_cast<float>(ByteIO::RdI32BE(vp + 0u)) * kFractScale,
                    static_cast<float>(ByteIO::RdI32BE(vp + 4u)) * kFractScale,
                    static_cast<float>(ByteIO::RdI32BE(vp + 8u)) * kFractScale
                };
            }
        } else if (std::memcmp(descData + p, "EDGE", 4) == 0) {
            if (cLen < 2u) return false;
            const uint16_t eCount = ByteIO::RdU16BE(pay);
            if (2u + static_cast<size_t>(eCount) * 4u > cLen) return false;
            edges.resize(eCount);
            for (uint16_t i = 0; i < eCount; ++i) {
                const uint8_t* ep = pay + 2u + static_cast<size_t>(i) * 4u;
                edges[i] = { ByteIO::RdU16BE(ep + 0u), ByteIO::RdU16BE(ep + 2u) };
            }
        } else if (std::memcmp(descData + p, "FACE", 4) == 0) {
            if (cLen < 2u) return false;
            const uint16_t tCount = ByteIO::RdU16BE(pay);
            if (2u + static_cast<size_t>(tCount) * 6u > cLen) return false;
            faces.resize(tCount);
            for (uint16_t i = 0; i < tCount; ++i) {
                const uint8_t* fp = pay + 2u + static_cast<size_t>(i) * 6u;
                faces[i] = { ByteIO::RdU16BE(fp + 0u), ByteIO::RdU16BE(fp + 2u), ByteIO::RdU16BE(fp + 4u) };
            }
        }

        const size_t padded = static_cast<size_t>(cLen) + (cLen & 1u);
        if (padded > descLen - (p + 8u)) break;
        p += 8u + padded;
    }

    if (localPts.empty()) return true; // empty null/camera/light pivot node is valid in multi-node scenes

    const uint32_t baseV = static_cast<uint32_t>(mesh.vertices.size());
    for (const auto& lp : localPts) {
        const float wx = posX + lp.x * axis[0][0] + lp.y * axis[1][0] + lp.z * axis[2][0];
        const float wy = posY + lp.x * axis[0][1] + lp.y * axis[1][1] + lp.z * axis[2][1];
        const float wz = posZ + lp.x * axis[0][2] + lp.y * axis[1][2] + lp.z * axis[2][2];
        if (!std::isfinite(wx) || !std::isfinite(wy) || !std::isfinite(wz)) return false;
        mesh.vertices.push_back(UberToolsSkel::ZUpToViewport(wx, wy, wz));
    }

    for (const auto& fc : faces) {
        if (fc.e0 >= edges.size() || fc.e1 >= edges.size() || fc.e2 >= edges.size()) return false;
        const auto [a0, b0] = edges[fc.e0];
        const auto [a1, b1] = edges[fc.e1];
        const auto [a2, b2] = edges[fc.e2];
        if (a0 >= localPts.size() || b0 >= localPts.size() ||
            a1 >= localPts.size() || b1 >= localPts.size() ||
            a2 >= localPts.size() || b2 >= localPts.size()) {
            return false;
        }

        uint16_t v0 = a0, v1 = b0, v2 = a1;
        if (b0 == a1)      { v0 = a0; v1 = b0; v2 = b1; }
        else if (b0 == b1) { v0 = a0; v1 = b0; v2 = a1; }
        else if (a0 == a1) { v0 = b0; v1 = a0; v2 = b1; }
        else if (a0 == b1) { v0 = b0; v1 = a0; v2 = a1; }
        else               return false; // disconnected edges cannot form a triangle

        if (v0 != v1 && v1 != v2 && v0 != v2) {
            mesh.tris.push_back({ baseV + v0, baseV + v1, baseV + v2, {} });
        }
    }
    return true;
}

inline bool WalkObjContainer(const uint8_t* buf, size_t len, LoadedMesh3D& mesh, int depth) {
    if (depth > 16) return false;
    size_t p = 0;
    while (p + 8u <= len) {
        const uint32_t cLen = ByteIO::RdU32BE(buf + p + 4u);
        if (cLen > len - (p + 8u)) return false;
        const uint8_t* pay = buf + p + 8u;
        if (std::memcmp(buf + p, "DESC", 4) == 0) {
            if (!ParseDescChunk(pay, cLen, mesh)) return false;
        } else if (std::memcmp(buf + p, "OBJ ", 4) == 0) {
            if (!WalkObjContainer(pay, cLen, mesh, depth + 1)) return false;
        }
        const size_t padded = static_cast<size_t>(cLen) + (cLen & 1u);
        if (padded > len - (p + 8u)) break;
        p += 8u + padded;
    }
    return true;
}

} // namespace DetailImagineTddd

inline bool ParseImagineTddd(
    const uint8_t* data,
    size_t size,
    LoadedMesh3D& mesh)
{
    if (!data || size < 20) return false;
    if (std::memcmp(data, "FORM", 4) != 0 || std::memcmp(data + 8u, "TDDD", 4) != 0) return false;

    const uint32_t formSize = ByteIO::RdU32BE(data + 4u);
    if (formSize < 12u || 8ULL + static_cast<uint64_t>(formSize) > size) return false;

    if (!DetailImagineTddd::WalkObjContainer(data + 12u, static_cast<size_t>(formSize) - 4u, mesh, 0)) {
        return false;
    }
    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"Imagine / TurboSilver 3D Object (IFF TDDD)";
    return true;
}

} // namespace Formats
