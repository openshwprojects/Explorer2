#pragma once
// WorldOfWarcraftWmo.h — World of Warcraft World Map Object (.wmo) 3D reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

namespace BlizzDetail {

inline bool ParseWmoGroupBytes(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    if (!data || size < 20 || std::memcmp(data, "REVM", 4) != 0) return false;
    size_t pos = 0;
    size_t trisBefore = mesh.tris.size();

    while (pos + 8 <= size) {
        char tag[4];
        uint32_t cSize = 0;
        std::memcpy(tag, data + pos, 4);
        std::memcpy(&cSize, data + pos + 4, 4);
        pos += 8;
        if (cSize > size - pos) break;

        if (std::memcmp(tag, "PGOM", 4) == 0 && cSize >= 68) {
            // MOGP group chunk: 68-byte group header followed by nested sub-chunks (MOVI="IVOM", MOVT="TVOM", ...)
            size_t subPos = pos + 68;
            const size_t subEnd = pos + cSize;
            std::vector<uint16_t> movi;
            std::vector<Vec3f> movt;

            while (subPos + 8 <= subEnd) {
                char stag[4];
                uint32_t sSize = 0;
                std::memcpy(stag, data + subPos, 4);
                std::memcpy(&sSize, data + subPos + 4, 4);
                subPos += 8;
                if (sSize > subEnd - subPos) break;

                if (std::memcmp(stag, "IVOM", 4) == 0 && sSize >= 6 && (sSize % 6) == 0) {
                    size_t cnt = sSize / 2;
                    movi.resize(cnt);
                    std::memcpy(movi.data(), data + subPos, sSize);
                } else if (std::memcmp(stag, "TVOM", 4) == 0 && sSize >= 36 && (sSize % 12) == 0) {
                    size_t cnt = sSize / 12;
                    movt.resize(cnt);
                    for (size_t i = 0; i < cnt; ++i) {
                        float vx = 0.0f, vy = 0.0f, vz = 0.0f;
                        std::memcpy(&vx, data + subPos + i * 12 + 0, 4);
                        std::memcpy(&vy, data + subPos + i * 12 + 4, 4);
                        std::memcpy(&vz, data + subPos + i * 12 + 8, 4);
                        if (!std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(vz)) return false;
                        movt[i] = BlizzZUpToViewport(vx, vy, vz);
                    }
                }
                subPos += sSize;
            }

            if (!movt.empty() && movi.size() >= 3) {
                uint32_t baseV = static_cast<uint32_t>(mesh.vertices.size());
                for (const auto& v : movt) mesh.vertices.push_back(v);
                for (size_t t = 0; t + 2 < movi.size(); t += 3) {
                    uint16_t a = movi[t], b = movi[t + 1], c = movi[t + 2];
                    if (a >= movt.size() || b >= movt.size() || c >= movt.size()) return false;
                    if (a == b || b == c || c == a) continue;
                    mesh.tris.push_back({ baseV + a, baseV + c, baseV + b, {} });
                }
            }
        }
        pos += cSize;
    }

    return mesh.tris.size() > trisBefore;
}

} // namespace BlizzDetail


// World of Warcraft World Map Object (.wmo — "REVM" Group or Root + sibling _000.wmo groups)
inline bool ParseWowWmo(const uint8_t* data, size_t size, const std::wstring& fullPath, LoadedMesh3D& mesh) {
    if (!data || size < 20 || std::memcmp(data, "REVM", 4) != 0) return false;

    // 1. Direct WMO Group file ("PGOM" / MOGP present)
    if (BlizzDetail::ParseWmoGroupBytes(data, size, mesh)) {
        mesh.formatName = L"World of Warcraft Map Object (.wmo)";
        return true;
    }

    // 2. WMO Root file ("DHOM" / MOHD present) -> load sibling <stem>_000.wmo, _001.wmo, ...
    size_t pos = 0;
    uint32_t nGroups = 0;
    bool isRoot = false;
    while (pos + 8 <= size) {
        char tag[4];
        uint32_t cSize = 0;
        std::memcpy(tag, data + pos, 4);
        std::memcpy(&cSize, data + pos + 4, 4);
        pos += 8;
        if (cSize > size - pos) break;
        if (std::memcmp(tag, "DHOM", 4) == 0 && cSize >= 8) {
            std::memcpy(&nGroups, data + pos + 4, 4);
            isRoot = true;
            break;
        }
        pos += cSize;
    }

    if (isRoot && nGroups > 0 && !fullPath.empty()) {
        std::wstring dir = PathUtils::GetParentPath(fullPath);
        std::wstring stem = PathUtils::GetStem(PathUtils::GetFileName(fullPath), false);
        uint32_t maxProbe = std::min<uint32_t>(nGroups, 64);
        for (uint32_t g = 0; g < maxProbe; ++g) {
            wchar_t suffix[16];
            swprintf_s(suffix, L"_%03u.wmo", g);
            std::vector<uint8_t> gBytes;
            if (PathUtils::ReadFileBytes(dir + L"\\" + stem + suffix, 64 * 1024 * 1024, gBytes) && !gBytes.empty()) {
                BlizzDetail::ParseWmoGroupBytes(gBytes.data(), gBytes.size(), mesh);
            }
        }
        if (!mesh.vertices.empty() && !mesh.tris.empty()) {
            mesh.formatName = L"World of Warcraft Root Map Object (.wmo)";
            return true;
        }
    }
    return false;
}

} // namespace Formats
