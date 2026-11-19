#pragma once
// Warcraft3Mdx.h — Warcraft III Binary Model (.mdx — MDLX) 3D reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

// Warcraft III Binary Model (.mdx — magic "MDLX")
inline bool ParseWarcraftMdx(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    if (!data || size < 16 || std::memcmp(data, "MDLX", 4) != 0) return false;
    size_t pos = 4;
    size_t seqCount = 1;

    while (pos + 8 <= size) {
        char tag[4];
        uint32_t chunkSize = 0;
        std::memcpy(tag, data + pos, 4);
        std::memcpy(&chunkSize, data + pos + 4, 4);
        pos += 8;
        if (chunkSize > size - pos) return false;

        if (std::memcmp(tag, "SEQS", 4) == 0 && chunkSize >= 132) {
            seqCount = std::max<size_t>(1, chunkSize / 132);
        } else if (std::memcmp(tag, "GEOS", 4) == 0 && chunkSize >= 20) {
            size_t gPos = pos;
            const size_t gEnd = pos + chunkSize;
            while (gPos + 4 <= gEnd) {
                uint32_t incSize = 0;
                std::memcpy(&incSize, data + gPos, 4);
                if (incSize < 20 || incSize > gEnd - gPos) return false;
                const size_t nextGeoset = gPos + incSize;
                size_t sPos = gPos + 4;

                std::vector<Vec3f> vrtx;
                std::vector<uint16_t> pvtx;

                // Walk ordered Geoset sub-arrays: VRTX -> NRMS -> PTYP -> PCNT -> PVTX
                while (sPos + 8 <= nextGeoset) {
                    char subTag[4];
                    uint32_t cnt = 0;
                    std::memcpy(subTag, data + sPos, 4);
                    std::memcpy(&cnt, data + sPos + 4, 4);
                    if (std::memcmp(subTag, "VRTX", 4) == 0) {
                        sPos += 8;
                        if (cnt == 0 || cnt > 2000000 || static_cast<uint64_t>(cnt) * 12ULL > nextGeoset - sPos) return false;
                        vrtx.resize(cnt);
                        for (uint32_t i = 0; i < cnt; ++i) {
                            float x = 0.0f, y = 0.0f, z = 0.0f;
                            std::memcpy(&x, data + sPos + static_cast<size_t>(i) * 12 + 0, 4);
                            std::memcpy(&y, data + sPos + static_cast<size_t>(i) * 12 + 4, 4);
                            std::memcpy(&z, data + sPos + static_cast<size_t>(i) * 12 + 8, 4);
                            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
                            vrtx[i] = BlizzDetail::BlizzZUpToViewport(x, y, z);
                        }
                        sPos += static_cast<size_t>(cnt) * 12;
                    } else if (std::memcmp(subTag, "NRMS", 4) == 0) {
                        sPos += 8;
                        if (static_cast<uint64_t>(cnt) * 12ULL > nextGeoset - sPos) return false;
                        sPos += static_cast<size_t>(cnt) * 12;
                    } else if (std::memcmp(subTag, "PTYP", 4) == 0 || std::memcmp(subTag, "PCNT", 4) == 0) {
                        sPos += 8;
                        if (static_cast<uint64_t>(cnt) * 4ULL > nextGeoset - sPos) return false;
                        sPos += static_cast<size_t>(cnt) * 4;
                    } else if (std::memcmp(subTag, "PVTX", 4) == 0) {
                        sPos += 8;
                        if (cnt < 3 || (cnt % 3) != 0 || static_cast<uint64_t>(cnt) * 2ULL > nextGeoset - sPos) return false;
                        pvtx.resize(cnt);
                        std::memcpy(pvtx.data(), data + sPos, static_cast<size_t>(cnt) * 2);
                        break; // Remaining Geoset fields (GNDX/MTGC/MATS/UVBS) skipped via nextGeoset
                    } else {
                        break;
                    }
                }

                if (!vrtx.empty() && pvtx.size() >= 3) {
                    uint32_t baseV = static_cast<uint32_t>(mesh.vertices.size());
                    for (const auto& v : vrtx) mesh.vertices.push_back(v);
                    for (size_t t = 0; t + 2 < pvtx.size(); t += 3) {
                        uint16_t a = pvtx[t], b = pvtx[t + 1], c = pvtx[t + 2];
                        if (a >= vrtx.size() || b >= vrtx.size() || c >= vrtx.size()) return false;
                        if (a == b || b == c || c == a) continue;
                        mesh.tris.push_back({ baseV + a, baseV + c, baseV + b, {} });
                    }
                }

                gPos = nextGeoset;
            }
        }
        pos += chunkSize;
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"Warcraft III Binary Model (.mdx)";
    mesh.animFrameCount = seqCount;
    return true;
}

} // namespace Formats
