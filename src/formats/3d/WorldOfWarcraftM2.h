#pragma once
// WorldOfWarcraftM2.h — World of Warcraft Model (.m2 + .skin) 3D reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

// World of Warcraft Model (.m2 — "MD20" / chunked "MD21" + external .skin or embedded Vanilla/TBC View)
inline bool ParseWowM2(const uint8_t* data, size_t size, const std::wstring& fullPath, LoadedMesh3D& mesh) {
    if (!data || size < 80) return false;
    const uint8_t* md20 = nullptr;
    size_t md20Size = 0;

    if (std::memcmp(data, "MD20", 4) == 0) {
        md20 = data;
        md20Size = size;
    } else if (std::memcmp(data, "MD21", 4) == 0) {
        size_t pos = 0;
        while (pos + 8 <= size) {
            char tag[4];
            uint32_t cSz = 0;
            std::memcpy(tag, data + pos, 4);
            std::memcpy(&cSz, data + pos + 4, 4);
            pos += 8;
            if (cSz > size - pos) return false;
            if (std::memcmp(tag, "MD21", 4) == 0 && cSz >= 80 && std::memcmp(data + pos, "MD20", 4) == 0) {
                md20 = data + pos;
                md20Size = cSz;
                break;
            }
            pos += cSz;
        }
    }
    if (!md20 || md20Size < 80) return false;

    uint32_t version = 0;
    std::memcpy(&version, md20 + 4, 4);
    if (version < 256 || version > 300) return false;

    uint32_t nSeqs = 0;
    std::memcpy(&nSeqs, md20 + 0x1C, 4);

    // Vertex M2Array offset: 0x44 in Vanilla (256..260), 0x3C in TBC..Modern (261+)
    const size_t vertArrOff = (version <= 260) ? 0x44 : 0x3C;
    const size_t viewFieldOff = vertArrOff + 8;
    if (viewFieldOff + 8 > md20Size) return false;

    uint32_t nVerts = 0, ofsVerts = 0;
    std::memcpy(&nVerts, md20 + vertArrOff, 4);
    std::memcpy(&ofsVerts, md20 + vertArrOff + 4, 4);
    if (nVerts == 0 || nVerts > 4000000) return false;
    if (static_cast<uint64_t>(ofsVerts) + static_cast<uint64_t>(nVerts) * 48ULL > md20Size) return false;

    mesh.vertices.resize(nVerts);
    for (uint32_t i = 0; i < nVerts; ++i) {
        const uint8_t* vp = md20 + ofsVerts + static_cast<size_t>(i) * 48;
        float x = 0.0f, y = 0.0f, z = 0.0f;
        std::memcpy(&x, vp + 0, 4);
        std::memcpy(&y, vp + 4, 4);
        std::memcpy(&z, vp + 8, 4);
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
        mesh.vertices[i] = BlizzDetail::BlizzZUpToViewport(x, y, z);
    }

    auto decodeSkinTables = [&](const uint8_t* base, size_t totalSz, size_t hdrArraysOff) -> bool {
        if (hdrArraysOff + 16 > totalSz) return false;
        uint32_t nLookup = 0, ofsLookup = 0, nTrisIdx = 0, ofsTrisIdx = 0;
        std::memcpy(&nLookup, base + hdrArraysOff + 0, 4);
        std::memcpy(&ofsLookup, base + hdrArraysOff + 4, 4);
        std::memcpy(&nTrisIdx, base + hdrArraysOff + 8, 4);
        std::memcpy(&ofsTrisIdx, base + hdrArraysOff + 12, 4);
        if (nLookup == 0 || nTrisIdx < 3 || (nTrisIdx % 3) != 0) return false;
        if (static_cast<uint64_t>(ofsLookup) + static_cast<uint64_t>(nLookup) * 2ULL > totalSz) return false;
        if (static_cast<uint64_t>(ofsTrisIdx) + static_cast<uint64_t>(nTrisIdx) * 2ULL > totalSz) return false;

        for (uint32_t t = 0; t + 2 < nTrisIdx; t += 3) {
            uint16_t la = 0, lb = 0, lc = 0;
            std::memcpy(&la, base + ofsTrisIdx + static_cast<size_t>(t + 0) * 2, 2);
            std::memcpy(&lb, base + ofsTrisIdx + static_cast<size_t>(t + 1) * 2, 2);
            std::memcpy(&lc, base + ofsTrisIdx + static_cast<size_t>(t + 2) * 2, 2);
            if (la >= nLookup || lb >= nLookup || lc >= nLookup) return false;
            uint16_t ga = 0, gb = 0, gc = 0;
            std::memcpy(&ga, base + ofsLookup + static_cast<size_t>(la) * 2, 2);
            std::memcpy(&gb, base + ofsLookup + static_cast<size_t>(lb) * 2, 2);
            std::memcpy(&gc, base + ofsLookup + static_cast<size_t>(lc) * 2, 2);
            if (ga >= nVerts || gb >= nVerts || gc >= nVerts) return false;
            if (ga == gb || gb == gc || gc == ga) continue;
            mesh.tris.push_back({ ga, gc, gb, {} });
        }
        return !mesh.tris.empty();
    };

    bool gotSkin = false;
    // 1. Try companion <stem>00.skin / <stem>.skin file on disk (WotLK v264+ standard)
    if (!fullPath.empty()) {
        std::wstring dir = PathUtils::GetParentPath(fullPath);
        std::wstring stem = PathUtils::GetStem(PathUtils::GetFileName(fullPath), false);
        const std::wstring candidates[3] = {
            dir + L"\\" + stem + L"00.skin",
            dir + L"\\" + stem + L"01.skin",
            dir + L"\\" + stem + L".skin"
        };
        for (const auto& sp : candidates) {
            std::vector<uint8_t> skinBytes;
            if (PathUtils::ReadFileBytes(sp, 32 * 1024 * 1024, skinBytes) &&
                skinBytes.size() >= 24 && std::memcmp(skinBytes.data(), "SKIN", 4) == 0)
            {
                if (decodeSkinTables(skinBytes.data(), skinBytes.size(), 4)) {
                    gotSkin = true;
                    break;
                }
            }
        }
    }

    // 2. Try embedded Views M2Array (Vanilla / TBC v256..263)
    if (!gotSkin && version < 264) {
        uint32_t nViews = 0, ofsViews = 0;
        std::memcpy(&nViews, md20 + viewFieldOff, 4);
        std::memcpy(&ofsViews, md20 + viewFieldOff + 4, 4);
        if (nViews > 0 && static_cast<uint64_t>(ofsViews) + 32ULL <= md20Size) {
            size_t subOff = ofsViews;
            if (ofsViews + 20 <= md20Size && std::memcmp(md20 + ofsViews, "SKIN", 4) == 0) subOff += 4;
            gotSkin = decodeSkinTables(md20, md20Size, subOff);
        }
    }

    if (!gotSkin || mesh.tris.empty()) return false;
    mesh.formatName = (std::memcmp(data, "MD21", 4) == 0)
        ? L"World of Warcraft Model MD21 (.m2)"
        : L"World of Warcraft Model MD20 (.m2)";
    mesh.animFrameCount = std::max<size_t>(1, nSeqs);
    return true;
}

} // namespace Formats
