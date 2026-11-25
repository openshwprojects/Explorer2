#pragma once
// ValveSourcePhy.h — Valve Source Engine VPhysics Collision & Ragdoll Mesh (.phy) reader
// Parses phyheader_t (16B), solid compact surfaces ("VPHY" + "IVPS"), and Ipion IVP_Compact_Ledge
// convex triangle soups + 16-byte aligned vertex pools (converting VPhysics meters to Source units).

namespace Formats {

inline bool ParseValveSourcePhy(
    const uint8_t* data,
    size_t size,
    LoadedMesh3D& mesh)
{
    if (!data || size < 96) return false;

    const int32_t hdrSize    = ByteIO::RdI32LE(data + 0u);
    const int32_t solidCount = ByteIO::RdI32LE(data + 8u);
    if (hdrSize != 16 || solidCount < 1 || solidCount > 1024) return false;

    size_t sPos = 16u;
    bool foundIvps = false;
    constexpr float kMetersToInches = 39.3700787f;

    for (int32_t s = 0; s < solidCount; ++s) {
        if (sPos + 4u > size) return false;
        const int32_t solidSize = ByteIO::RdI32LE(data + sPos);
        if (solidSize < 64 || sPos + 4u + static_cast<size_t>(solidSize) > size) return false;

        const size_t payStart = sPos + 4u;
        const size_t sEnd     = payStart + static_cast<size_t>(solidSize);

        // Locate "IVPS" magic at end of legacysurfaceheader_t (standard offset payStart + 72 with VPHY, or +44 legacy)
        size_t ivpsPos = std::numeric_limits<size_t>::max();
        const size_t scanLim = std::min(sEnd - 4u, payStart + 112u);
        for (size_t probe = payStart; probe <= scanLim; probe += 4u) {
            if (std::memcmp(data + probe, "IVPS", 4) == 0) {
                ivpsPos = probe;
                break;
            }
        }
        if (ivpsPos == std::numeric_limits<size_t>::max()) return false;
        foundIvps = true;

        // Walk convex leaf IVP_Compact_Ledge blocks between ivpsPos + 4 and the shared vertex pool
        size_t ledgePos = ivpsPos + 4u;
        size_t minPointPoolPos = sEnd;
        int ledgeGuard = 0;

        while (ledgePos + 16u <= minPointPoolPos && ledgeGuard < 4096) {
            ++ledgeGuard;
            const int32_t cPointOffset = ByteIO::RdI32LE(data + ledgePos + 0u);
            const uint32_t bfWord      = ByteIO::RdU32LE(data + ledgePos + 8u);
            const int16_t nTris        = ByteIO::RdI16LE(data + ledgePos + 12u);
            const uint32_t sizeDiv16   = (bfWord >> 8) & 0xFFFFFFu;

            if (cPointOffset < 16 || nTris < 1 || nTris > 65535) break;
            const size_t ledgeBytes = 16u + static_cast<size_t>(nTris) * 16u;
            if (sizeDiv16 != 0 && sizeDiv16 * 16u != ledgeBytes) break;
            if (ledgePos + ledgeBytes > sEnd) return false;

            const size_t vPoolPos = ledgePos + static_cast<size_t>(cPointOffset);
            if (vPoolPos < ledgePos + ledgeBytes || vPoolPos + 16u > sEnd) return false;
            minPointPoolPos = std::min(minPointPoolPos, vPoolPos);
            const size_t maxPoolVerts = (sEnd - vPoolPos) / 16u;
            if (maxPoolVerts < 3u) return false;

            // Remap only referenced vertices of this convex hull
            std::unordered_map<uint16_t, uint32_t> localMap;
            localMap.reserve(static_cast<size_t>(nTris) * 2u);

            auto getOrEmitVert = [&](uint16_t pidx, uint32_t& outIdx) -> bool {
                auto it = localMap.find(pidx);
                if (it != localMap.end()) {
                    outIdx = it->second;
                    return true;
                }
                if (static_cast<size_t>(pidx) >= maxPoolVerts) return false;
                const uint8_t* vp = data + vPoolPos + static_cast<size_t>(pidx) * 16u;
                const float vx = ByteIO::RdF32LE(vp + 0u);
                const float vy = ByteIO::RdF32LE(vp + 4u);
                const float vz = ByteIO::RdF32LE(vp + 8u);
                if (!std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(vz) ||
                    std::fabs(vx) > 1e5f || std::fabs(vy) > 1e5f || std::fabs(vz) > 1e5f) {
                    return false;
                }
                outIdx = static_cast<uint32_t>(mesh.vertices.size());
                // Ipion VPhysics space: Y points down, X right, Z forward -> Y-up inches: (x, -y, z) * 39.37
                mesh.vertices.push_back({ vx * kMetersToInches, -vy * kMetersToInches, vz * kMetersToInches });
                localMap.emplace(pidx, outIdx);
                return true;
            };

            for (int16_t t = 0; t < nTris; ++t) {
                const uint8_t* tp = data + ledgePos + 16u + static_cast<size_t>(t) * 16u;
                const uint32_t triFlags = ByteIO::RdU32LE(tp + 0u);
                if ((triFlags & 0x80000000u) != 0u) continue; // skip virtual internal portal triangles
                const uint16_t p0 = static_cast<uint16_t>(ByteIO::RdU32LE(tp + 4u)  & 0xFFFFu);
                const uint16_t p1 = static_cast<uint16_t>(ByteIO::RdU32LE(tp + 8u)  & 0xFFFFu);
                const uint16_t p2 = static_cast<uint16_t>(ByteIO::RdU32LE(tp + 12u) & 0xFFFFu);
                if (p0 == p1 || p1 == p2 || p0 == p2) continue;
                uint32_t i0 = 0, i1 = 0, i2 = 0;
                if (!getOrEmitVert(p0, i0) || !getOrEmitVert(p1, i1) || !getOrEmitVert(p2, i2)) {
                    return false;
                }
                mesh.tris.push_back({ i0, i2, i1, {} });
            }

            ledgePos += ledgeBytes;
        }

        sPos = sEnd;
    }

    if (!foundIvps || mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"Valve Source VPhysics Collision (PHY)";
    return true;
}

} // namespace Formats
