#pragma once
// MaxonCinema4d.h — Maxon Cinema 4D (.c4d — MC4D / C4DC / C4D6) 3D scene reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

// ============================================================================
// 3. Maxon Cinema 4D (.c4d — MC4D / C4DC / C4D6 v5-R25+ Raw & Zlib HyperFile Chunks)
// ============================================================================
inline bool ParseCinema4dEnhanced(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    using namespace ModoZbMaxC4dDetail;
    if (!data || size < 16) return false;

    size_t bodyOff = 0;
    if (std::memcmp(data, "FORM", 4) == 0 && size >= 12 &&
        (std::memcmp(data + 8, "MC4D", 4) == 0 || std::memcmp(data + 8, "C4DC", 4) == 0)) {
        bodyOff = 12;
    } else if (std::memcmp(data, "C4DC", 4) == 0 || std::memcmp(data, "C4D6", 4) == 0 || std::memcmp(data, "MC4D", 4) == 0) {
        bodyOff = 8;
    } else if (size >= 12 && (std::memcmp(data + 4, "C4DC", 4) == 0 || std::memcmp(data + 4, "MC4D", 4) == 0 || std::memcmp(data + 4, "C4D6", 4) == 0)) {
        bodyOff = 12;
    } else if (size >= 16 && (std::memcmp(data + 8, "C4DC", 4) == 0 || std::memcmp(data + 8, "MC4D", 4) == 0)) {
        bodyOff = 16;
    } else {
        return false;
    }

    // Check if the C4DC body is zlib-compressed (standard in Cinema 4D R6-R25+)
    std::vector<uint8_t> inflated;
    const uint8_t* buf = data + bodyOff;
    size_t bufLen = size - bodyOff;
    if (TryFindAndInflateZlib(buf, bufLen, 24, inflated)) {
        buf = inflated.data();
        bufLen = inflated.size();
    }

    mesh.vertices.clear();
    mesh.tris.clear();

    auto walkC4d = [&](auto& self, size_t s, size_t e, int depth) -> void {
        if (depth > 24 || s + 8 > e) return;
        uint32_t vBase = static_cast<uint32_t>(mesh.vertices.size());
        uint32_t vCount = 0;
        size_t p = s;

        while (p + 8 <= e) {
            const uint8_t* tag = buf + p;
            uint32_t rawLen = RdU32BE(buf + p + 4);
            bool isCont = std::memcmp(tag, "OBJ ", 4) == 0 || std::memcmp(tag, "OPOL", 4) == 0 ||
                          std::memcmp(tag, "ROOT", 4) == 0 || std::memcmp(tag, "SCEN", 4) == 0 ||
                          std::memcmp(tag, "DOKU", 4) == 0;
            uint32_t cLen = rawLen;
            p += 8;
            if (cLen > e - p) break;

            if (isCont) {
                self(self, p, p + cLen, depth + 1);
            }
            // Float32 vertex chunk: POLP / PNTS / Poin
            else if ((std::memcmp(tag, "POLP", 4) == 0 || std::memcmp(tag, "PNTS", 4) == 0 || std::memcmp(tag, "Poin", 4) == 0) &&
                     cLen >= 36 && (cLen % 12u) == 0u)
            {
                uint32_t nv = cLen / 12u;
                if (nv <= 2000000u) {
                    vBase = static_cast<uint32_t>(mesh.vertices.size());
                    vCount = 0;
                    for (uint32_t i = 0; i < nv; ++i) {
                        float x = RdF32BE(buf + p + static_cast<size_t>(i) * 12u + 0);
                        float y = RdF32BE(buf + p + static_cast<size_t>(i) * 12u + 4);
                        float z = RdF32BE(buf + p + static_cast<size_t>(i) * 12u + 8);
                        if (!IsValidCoord3(x, y, z)) { vCount = 0; break; }
                        mesh.vertices.push_back({ x, y, z });
                        ++vCount;
                    }
                }
            }
            // Float64 (R16+ Vector64) vertex chunk: PNT8 / Poi8
            else if ((std::memcmp(tag, "PNT8", 4) == 0 || std::memcmp(tag, "Poi8", 4) == 0) &&
                     cLen >= 72 && (cLen % 24u) == 0u)
            {
                uint32_t nv = cLen / 24u;
                if (nv <= 2000000u) {
                    vBase = static_cast<uint32_t>(mesh.vertices.size());
                    vCount = 0;
                    for (uint32_t i = 0; i < nv; ++i) {
                        float x = static_cast<float>(RdF64BE(buf + p + static_cast<size_t>(i) * 24u + 0));
                        float y = static_cast<float>(RdF64BE(buf + p + static_cast<size_t>(i) * 24u + 8));
                        float z = static_cast<float>(RdF64BE(buf + p + static_cast<size_t>(i) * 24u + 16));
                        if (!IsValidCoord3(x, y, z)) { vCount = 0; break; }
                        mesh.vertices.push_back({ x, y, z });
                        ++vCount;
                    }
                }
            }
            // Polygon chunk (CPolygon: 4 * uint32BE a,b,c,d; c==d is triangle, c!=d is quad): POLY / POLS / Polg
            else if ((std::memcmp(tag, "POLY", 4) == 0 || std::memcmp(tag, "POLS", 4) == 0 || std::memcmp(tag, "Polg", 4) == 0) &&
                     cLen >= 16 && (cLen % 16u) == 0u && vCount >= 3)
            {
                uint32_t nf = cLen / 16u;
                for (uint32_t i = 0; i < nf && i < 4000000u; ++i) {
                    uint32_t a = RdU32BE(buf + p + static_cast<size_t>(i) * 16u + 0);
                    uint32_t b = RdU32BE(buf + p + static_cast<size_t>(i) * 16u + 4);
                    uint32_t c = RdU32BE(buf + p + static_cast<size_t>(i) * 16u + 8);
                    uint32_t d = RdU32BE(buf + p + static_cast<size_t>(i) * 16u + 12);
                    if (a < vCount && b < vCount && c < vCount && d < vCount) {
                        if (a != b && b != c && a != c) {
                            mesh.tris.push_back({ vBase + a, vBase + b, vBase + c, {} });
                        }
                        if (c != d && a != c && c != d && a != d) {
                            mesh.tris.push_back({ vBase + a, vBase + c, vBase + d, {} });
                        }
                    }
                }
            }

            p += static_cast<size_t>(cLen) + (cLen & 1u);
        }
    };

    walkC4d(walkC4d, 0, bufLen, 0);
    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"Maxon Cinema 4D Scene (.c4d)";
    return true;
}

} // namespace Formats
