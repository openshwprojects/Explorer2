#pragma once
// LightWaveLwo.h — LightWave 3D Object (.lwo — IFF LWO2 / LWOB) reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

// ============================================================================
// 68. LightWave 3D Object (.lwo — IFF "FORM....LWO2" / "LWOB")
// ============================================================================
// wantLayer: -1 = every layer (a .lwo file opened by itself), else the 0-based LAYR number to keep (a scene's
// "LoadObjectLayer 1 ..." is LAYR 0). outLayersSeen counts the LAYR chunks (LWOB / layer-less LWO2 = 1 layer).
inline bool ParseLightWaveLwoLayer(const uint8_t* data, size_t size, LoadedMesh3D& mesh, int wantLayer, int* outLayersSeen = nullptr) {
    if (outLayersSeen) *outLayersSeen = 0;
    if (size < 20 || std::memcmp(data, "FORM", 4) != 0) return false;
    bool isLwo2 = (std::memcmp(data + 8, "LWO2", 4) == 0);
    bool isLwob = (std::memcmp(data + 8, "LWOB", 4) == 0);
    if (!isLwo2 && !isLwob) return false;

    mesh.formatName = isLwo2 ? L"LightWave 3D Object (.lwo LWO2)" : L"LightWave 3D Object (.lwo LWOB)";
    size_t pos = 12;
    uint32_t layerVBase = 0;
    int curLayer = 0;
    int layersSeen = 0;
    while (pos + 8 <= size) {
        char tag[5] = {};
        std::memcpy(tag, data + pos, 4);
        uint32_t clen = Detail3D100::RdU32BE(data + pos + 4);
        pos += 8;
        if (pos + clen > size) break;
        const bool keepLayer = (wantLayer < 0 || curLayer == wantLayer);

        if (std::memcmp(tag, "LAYR", 4) == 0 && clen >= 2) {
            curLayer = Detail3D100::RdU16BE(data + pos);
            ++layersSeen;
        } else if (std::memcmp(tag, "PNTS", 4) == 0 && keepLayer) {
            layerVBase = static_cast<uint32_t>(mesh.vertices.size());
            uint32_t numPts = clen / 12;
            for (uint32_t i = 0; i < numPts && mesh.vertices.size() < 200000; ++i) {
                float x = Detail3D100::RdF32BE(data + pos + i * 12 + 0);
                float y = Detail3D100::RdF32BE(data + pos + i * 12 + 4);
                float z = Detail3D100::RdF32BE(data + pos + i * 12 + 8);
                mesh.vertices.push_back({ x, y, z });
            }
        } else if (std::memcmp(tag, "POLS", 4) == 0 && keepLayer) {
            size_t p = pos;
            size_t pend = pos + clen;
            if (isLwo2 && p + 4 <= pend) {
                p += 4; // 4-byte polygon type ("FACE", "PTCH", "SUBD")
            }
            while (p + 2 <= pend && mesh.tris.size() < 400000) {
                uint16_t numVerts = Detail3D100::RdU16BE(data + p) & 0x03FFu;
                p += 2;
                std::vector<uint32_t> poly;
                for (uint16_t v = 0; v < numVerts && p + 2 <= pend; ++v) {
                    uint32_t idx = 0;
                    if (isLwo2 && data[p] == 0xFF) {
                        if (p + 4 > pend) break;
                        idx = Detail3D100::RdU32BE(data + p) & 0x00FFFFFFu;
                        p += 4;
                    } else {
                        idx = Detail3D100::RdU16BE(data + p);
                        p += 2;
                    }
                    uint32_t absIdx = layerVBase + idx;
                    if (absIdx < mesh.vertices.size()) poly.push_back(absIdx);
                }
                if (!isLwo2 && p + 2 <= pend) p += 2; // LWOB surface index
                for (size_t i = 1; i + 1 < poly.size(); ++i) {
                    mesh.tris.push_back({ poly[0], poly[i], poly[i + 1], {} });
                }
            }
        }
        pos += clen + (clen & 1u);
    }
    if (outLayersSeen) *outLayersSeen = std::max(layersSeen, 1);
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

inline bool ParseLightWaveLwo(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    return ParseLightWaveLwoLayer(data, size, mesh, -1);
}

} // namespace Formats
