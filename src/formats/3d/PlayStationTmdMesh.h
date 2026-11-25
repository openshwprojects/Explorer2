#pragma once
// PlayStationTmdMesh.h — Sony PlayStation 1 3D Model (.tmd, id == 0x00000041) Reader
// Parses Psy-Q TMD header (relative & fixed-address flags), 28-byte object descriptors,
// 8-byte 16-bit signed SVECTOR vertices & normals, and all GTE 3D polygon & line primitive packets
// (flat / Gouraud, textured / untextured, lit / unlit, triangles & Z-order PS1 quads).
#include "../../ByteIO.h"
#include "../common/MeshBuild3D.h"

namespace Formats {

inline bool ParsePlayStationTmdMesh(
    const uint8_t* data,
    size_t size,
    const std::wstring& /*fullPath*/,
    LoadedMesh3D& mesh)
{
    if (!data || size < 40) return false;
    const uint32_t id    = ByteIO::RdU32LE(data + 0);
    const uint32_t flags = ByteIO::RdU32LE(data + 4);
    const uint32_t nobj  = ByteIO::RdU32LE(data + 8);

    if (id != 0x00000041u) return false;
    if ((flags & ~1u) != 0u) return false;
    if (nobj == 0 || nobj > 4096) return false;
    if (12ULL + static_cast<uint64_t>(nobj) * 28ULL > size) return false;

    mesh.vertices.clear();
    mesh.tris.clear();

    // Determine pointer base for FIXP == 1 (either 0-based file offset or PSX RAM address)
    uint32_t ramBase = 0u;
    if (flags & 1u) {
        uint32_t minPtr = 0xFFFFFFFFu;
        for (uint32_t o = 0; o < nobj; ++o) {
            const uint8_t* op = data + 12u + static_cast<size_t>(o) * 28u;
            uint32_t vt = ByteIO::RdU32LE(op + 0);
            uint32_t pt = ByteIO::RdU32LE(op + 16);
            if (vt > 0) minPtr = std::min(minPtr, vt);
            if (pt > 0) minPtr = std::min(minPtr, pt);
        }
        if (minPtr >= size && minPtr >= 12u + nobj * 28u) {
            ramBase = minPtr - static_cast<uint32_t>(12u + nobj * 28u);
        }
    }

    auto resolveOffset = [&](uint32_t rawPtr, size_t& outOff) -> bool {
        uint64_t off = (flags & 1u)
            ? (rawPtr >= ramBase ? static_cast<uint64_t>(rawPtr - ramBase) : static_cast<uint64_t>(size + 1u))
            : (12ULL + static_cast<uint64_t>(rawPtr));
        if (off >= size) return false;
        outOff = static_cast<size_t>(off);
        return true;
    };

    std::vector<std::pair<Vec3f, Vec3f>> fallbackLines;

    for (uint32_t o = 0; o < nobj; ++o) {
        const uint8_t* op = data + 12u + static_cast<size_t>(o) * 28u;
        const uint32_t vertTop = ByteIO::RdU32LE(op + 0);
        const uint32_t nVert   = ByteIO::RdU32LE(op + 4);
        const uint32_t primTop = ByteIO::RdU32LE(op + 16);
        const uint32_t nPrim   = ByteIO::RdU32LE(op + 20);
        const int32_t  scale   = ByteIO::RdI32LE(op + 24);

        if (nVert == 0 || nPrim == 0 || nVert > 262144 || nPrim > 524288) continue;
        size_t vOff = 0, pOff = 0;
        if (!resolveOffset(vertTop, vOff) || !resolveOffset(primTop, pOff)) continue;
        if (static_cast<uint64_t>(nVert) * 8ULL > size - vOff) continue;

        float sc = 1.0f / 256.0f;
        if (scale >= -16 && scale <= 16) {
            sc *= std::ldexp(1.0f, scale);
        }

        const uint32_t baseV = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.reserve(mesh.vertices.size() + nVert);
        for (uint32_t v = 0; v < nVert; ++v) {
            const uint8_t* vp = data + vOff + static_cast<size_t>(v) * 8u;
            int16_t vx = ByteIO::RdI16LE(vp + 0);
            int16_t vy = ByteIO::RdI16LE(vp + 2);
            int16_t vz = ByteIO::RdI16LE(vp + 4);
            // Sony PS1 GTE coordinate system has +X right, +Y down, +Z forward -> flip Y to +Y up!
            mesh.vertices.push_back({ static_cast<float>(vx) * sc, -static_cast<float>(vy) * sc, static_cast<float>(vz) * sc });
        }

        size_t cur = pOff;
        for (uint32_t pr = 0; pr < nPrim && cur + 4u <= size; ++pr) {
            const uint8_t ilen = data[cur + 1];
            const uint8_t flag = data[cur + 2];
            const uint8_t mode = data[cur + 3];
            const size_t bodyBytes = static_cast<size_t>(ilen) * 4u;
            if (bodyBytes > size - (cur + 4u)) break;
            const uint8_t* body = data + cur + 4u;
            cur += 4u + bodyBytes;

            auto rdW = [&](size_t wordIdx) -> uint16_t {
                return ByteIO::RdU16LE(body + wordIdx * 2u);
            };
            const size_t nWords = bodyBytes / 2u;

            const uint8_t primCode = static_cast<uint8_t>((mode >> 5) & 0x07u);
            if (primCode == 1u) { // 3D Polygon (0x20 .. 0x3F)
                const bool isGouraud  = ((mode & 0x10u) != 0u);
                const bool isQuad     = ((mode & 0x08u) != 0u);
                const bool isTextured = ((mode & 0x04u) != 0u);
                const bool isUnlit    = ((flag & 0x01u) != 0u);

                uint16_t vi[4] = {};
                bool okPkt = false;

                if (!isUnlit) {
                    // Lit polygon (has normal index / indices)
                    if (!isTextured && !isGouraud) {
                        // Flat untextured lit: R,G,B,mode2 (2w) + norm0 (1w) + v0,v1,v2[,v3]
                        size_t needW = isQuad ? 7u : 6u;
                        if (nWords >= needW) {
                            vi[0] = rdW(3); vi[1] = rdW(4); vi[2] = rdW(5);
                            if (isQuad) vi[3] = rdW(6);
                            okPkt = true;
                        }
                    } else if (!isTextured && isGouraud) {
                        // Gouraud untextured lit: R,G,B,mode2 (2w) + (norm_k, vert_k) pairs
                        size_t needW = isQuad ? 10u : 8u;
                        if (nWords >= needW) {
                            vi[0] = rdW(3); vi[1] = rdW(5); vi[2] = rdW(7);
                            if (isQuad) vi[3] = rdW(9);
                            okPkt = true;
                        }
                    } else if (isTextured && !isGouraud) {
                        // Flat textured lit: 3*2w UVs (6w) or 4*2w UVs (8w) + norm0 (1w) + v0..v2/v3
                        size_t baseW = isQuad ? 9u : 7u;
                        size_t needW = baseW + (isQuad ? 4u : 3u);
                        if (nWords >= needW) {
                            vi[0] = rdW(baseW + 0); vi[1] = rdW(baseW + 1); vi[2] = rdW(baseW + 2);
                            if (isQuad) vi[3] = rdW(baseW + 3);
                            okPkt = true;
                        }
                    } else {
                        // Gouraud textured lit: UVs (6w or 8w) + (norm_k, vert_k) pairs
                        size_t baseW = isQuad ? 8u : 6u;
                        size_t needW = baseW + (isQuad ? 8u : 6u);
                        if (nWords >= needW) {
                            vi[0] = rdW(baseW + 1); vi[1] = rdW(baseW + 3); vi[2] = rdW(baseW + 5);
                            if (isQuad) vi[3] = rdW(baseW + 7);
                            okPkt = true;
                        }
                    }
                } else {
                    // Unlit polygon (LGT == 1: no normal indices; vertices occupy the trailing words of the packet!)
                    if (isQuad && nWords >= 6u) {
                        vi[0] = rdW(nWords - 4u);
                        vi[1] = rdW(nWords - 3u);
                        vi[2] = rdW(nWords - 2u);
                        vi[3] = rdW(nWords - 1u);
                        okPkt = true;
                    } else if (!isQuad && nWords >= 6u) {
                        // 3 vertices + 1 padding uint16 at end of 32-bit aligned packet
                        vi[0] = rdW(nWords - 4u);
                        vi[1] = rdW(nWords - 3u);
                        vi[2] = rdW(nWords - 2u);
                        okPkt = true;
                    }
                }

                if (okPkt) {
                    const size_t vc = isQuad ? 4u : 3u;
                    bool inRange = true;
                    for (size_t k = 0; k < vc; ++k) {
                        if (vi[k] >= nVert) { inRange = false; break; }
                    }
                    if (inRange) {
                        if (vi[0] != vi[1] && vi[1] != vi[2] && vi[0] != vi[2]) {
                            mesh.tris.push_back({ baseV + vi[0], baseV + vi[2], baseV + vi[1], {} });
                        }
                        if (isQuad && vi[1] != vi[3] && vi[3] != vi[2] && vi[1] != vi[2]) {
                            // PS1 Z-shaped quad vertex order (v0, v1, v2, v3) -> second tri connects (v1, v3, v2)
                            mesh.tris.push_back({ baseV + vi[1], baseV + vi[2], baseV + vi[3], {} });
                        }
                    }
                }
            } else if (primCode == 2u && nWords >= 4u) { // 3D Line packet
                uint16_t a = rdW(nWords - 2u);
                uint16_t b = rdW(nWords - 1u);
                if (a < nVert && b < nVert && a != b) {
                    fallbackLines.emplace_back(mesh.vertices[baseV + a], mesh.vertices[baseV + b]);
                }
            }
        }
    }

    if (mesh.tris.empty() && !fallbackLines.empty()) {
        mesh.vertices.clear();
        Vec3f lo = fallbackLines[0].first, hi = fallbackLines[0].first;
        for (const auto& s : fallbackLines) {
            for (const Vec3f& p : { s.first, s.second }) {
                lo = { std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z) };
                hi = { std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z) };
            }
        }
        float span = std::max({ hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, 1e-3f });
        for (const auto& s : fallbackLines) {
            MeshBuild3D::EmitEdgeStrut(mesh, s.first, s.second, span * 0.008f);
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"Sony PlayStation 1 TMD Model (.tmd)";
    return true;
}

} // namespace Formats
