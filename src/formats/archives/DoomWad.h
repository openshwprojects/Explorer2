#pragma once
// DoomWad.h — DOOM Engine IWAD / PWAD lump archive reader & extractor (.wad — "IWAD" / "PWAD").

#include "../common/ArchiveFormatCommon.h"
#include <cctype>

namespace Explorer2::Formats {

namespace DetailDiskAndGameArc {

inline bool IsDoomMapMarkerName(std::string_view up) {
    // Classic ExMy (E1M1..E9M9, E1M10)
    if ((up.size() == 4 || up.size() == 5) && up[0] == 'E' &&
        std::isdigit(static_cast<unsigned char>(up[1])) && up[2] == 'M' &&
        std::isdigit(static_cast<unsigned char>(up[3])) &&
        (up.size() == 4 || std::isdigit(static_cast<unsigned char>(up[4]))))
    {
        return true;
    }
    // DOOM II / Hexen MAPxx (MAP01..MAP99)
    if (up.size() == 5 && up.substr(0, 3) == "MAP" &&
        std::isalnum(static_cast<unsigned char>(up[3])) &&
        std::isalnum(static_cast<unsigned char>(up[4])))
    {
        return true;
    }
    return false;
}

inline bool IsDoomMapDataLumpName(std::string_view up) {
    return up == "THINGS"   || up == "LINEDEFS" || up == "SIDEDEFS" || up == "VERTEXES" ||
           up == "SEGS"     || up == "SSECTORS" || up == "NODES"    || up == "SECTORS"  ||
           up == "REJECT"   || up == "BLOCKMAP" || up == "BEHAVIOR" || up == "SCRIPTS"  ||
           up == "LEAFS"    || up == "LIGHTS"   || up == "MACROS"   || up == "TEXTMAP"  ||
           up == "ZNODES"   || up == "DIALOGUE" || up == "ENDMAP";
}

inline bool IsDoomZeroNamespaceMarker(std::string_view up) {
    if (up.size() >= 6 && up.substr(up.size() - 6) == "_START") return true;
    if (up.size() >= 4 && up.substr(up.size() - 4) == "_END") return true;
    return false;
}

inline bool ParseDoomWad(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (!data || size < 12) return false;
    if (std::memcmp(data, "IWAD", 4) != 0 && std::memcmp(data, "PWAD", 4) != 0) return false;

    const uint32_t numLumps = RdU32LE(data + 4);
    const uint32_t dirOff   = RdU32LE(data + 8);
    if (numLumps == 0 || numLumps > 65536u || dirOff < 12u || dirOff > size ||
        static_cast<uint64_t>(numLumps) * 16ULL > size - dirOff) {
        return false;
    }

    outInfo.formatName = (data[0] == 'I') ? L"DOOM IWAD" : L"DOOM PWAD";
    std::string currentMap;
    std::unordered_map<std::wstring, uint32_t> seenPaths;

    for (uint32_t i = 0; i < numLumps && i < 65536u; ++i) {
        const uint8_t* rec = data + dirOff + static_cast<size_t>(i) * 16u;
        const uint32_t fOff = RdU32LE(rec + 0);
        const uint32_t fLen = RdU32LE(rec + 4);
        if (fLen > 0 && (fOff > size || fLen > size - fOff)) {
            outInfo = {};
            return false;
        }

        std::string rawName;
        rawName.reserve(8);
        for (size_t c = 0; c < 8; ++c) {
            const uint8_t b = rec[8 + c];
            if (b == 0) break;
            if (b < 0x20u || b >= 0x7Fu) {
                outInfo = {};
                return false;
            }
            char ch = static_cast<char>(b);
            if (ch == '/' || ch == '\\' || ch == ':' || ch == '*' || ch == '?' || ch == '"' || ch == '<' || ch == '>' || ch == '|') {
                ch = '_';
            }
            rawName.push_back(ch);
        }
        if (rawName.empty()) continue;

        std::string upName = rawName;
        for (char& ch : upName) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));

        if (IsDoomMapMarkerName(upName)) {
            currentMap = upName;
            const std::wstring mapW = PathUtils::Utf8ToWide(currentMap);
            if (fLen == 0) {
                ArchiveItemEntry dirItem;
                dirItem.pathInArchive = mapW;
                dirItem.name = mapW;
                dirItem.isDirectory = true;
                dirItem.compressionMethod = 0;
                outInfo.entries.push_back(std::move(dirItem));
                continue;
            }
            // Non-empty map header lump (e.g. FraggleScript)
            ArchiveItemEntry hdrItem;
            hdrItem.pathInArchive = mapW + L"/MAPINFO.lmp";
            hdrItem.name = L"MAPINFO.lmp";
            hdrItem.uncompressedSize = fLen;
            hdrItem.compressedSize = fLen;
            hdrItem.rawDataOffset = fOff;
            hdrItem.compressionMethod = 0;
            outInfo.entries.push_back(std::move(hdrItem));
            continue;
        }

        if (!currentMap.empty() && !IsDoomMapDataLumpName(upName)) {
            currentMap.clear();
        }
        if (fLen == 0 && currentMap.empty() && IsDoomZeroNamespaceMarker(upName)) {
            continue;
        }
        if (fLen == 0 && upName == "ENDMAP") {
            currentMap.clear();
            continue;
        }

        std::wstring leafName = PathUtils::Utf8ToWide(rawName) + L".lmp";
        std::wstring relPath  = currentMap.empty()
            ? leafName
            : (PathUtils::Utf8ToWide(currentMap) + L"/" + leafName);

        uint32_t& dupCount = seenPaths[PathUtils::ToLower(relPath)];
        ++dupCount;
        if (dupCount > 1u) {
            const std::wstring stem = PathUtils::Utf8ToWide(rawName) + L"_" + std::to_wstring(dupCount) + L".lmp";
            leafName = stem;
            relPath  = currentMap.empty() ? stem : (PathUtils::Utf8ToWide(currentMap) + L"/" + stem);
        }

        ArchiveItemEntry item;
        item.pathInArchive = std::move(relPath);
        item.name = std::move(leafName);
        item.uncompressedSize = fLen;
        item.compressedSize = fLen;
        item.rawDataOffset = (fLen > 0) ? fOff : 0u;
        item.compressionMethod = 0;
        outInfo.entries.push_back(std::move(item));
    }

    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

} // namespace DetailDiskAndGameArc

inline bool InspectDoomWad(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return DetailDiskAndGameArc::ParseDoomWad(data, size, outInfo);
}

inline bool ExtractDoomWadEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    ArchiveSummaryInfo info;
    if (!DetailDiskAndGameArc::ParseDoomWad(data, size, info)) return false;
    const std::wstring want = ArchivePath::Normalize(normTarget);
    const std::wstring wantWithExt = (want.size() >= 4 && _wcsicmp(want.c_str() + want.size() - 4, L".lmp") == 0)
        ? want
        : (want + L".lmp");

    for (const auto& e : info.entries) {
        if (e.isDirectory) continue;
        if (_wcsicmp(e.pathInArchive.c_str(), want.c_str()) == 0 ||
            _wcsicmp(e.pathInArchive.c_str(), wantWithExt.c_str()) == 0)
        {
            if (e.uncompressedSize == 0) {
                outBytes.clear();
                return true;
            }
            if (e.rawDataOffset > size || e.uncompressedSize > size - e.rawDataOffset) return false;
            const uint8_t* p = data + static_cast<size_t>(e.rawDataOffset);
            outBytes.assign(p, p + static_cast<size_t>(e.uncompressedSize));
            return true;
        }
    }
    return false;
}

} // namespace Explorer2::Formats
