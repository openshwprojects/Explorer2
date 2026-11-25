#pragma once
// LithTechRezArchive.h — Monolith LithTech "RezMgr Version 1" Resource Archive (.rez)
// Used across Shogo, Blood II, No One Lives Forever 1 & 2, Aliens vs. Predator 2, and TRON 2.0.
// Parses 127B CRLF+0x1A banner + binary header + recursive Directory/Resource blocks with
// StrToTyp/TypToStr big-endian FourCC extension reconstruction and cycle-safe block tracking.

#include "../common/GamePakArchiveCommon.h"

namespace Explorer2::Formats {

namespace DetailLithTechRez {

inline bool ReadCStringBounded(const uint8_t* buf, size_t endPos, size_t& ioPos, std::string& outStr) {
    outStr.clear();
    while (ioPos < endPos) {
        const uint8_t ch = buf[ioPos++];
        if (ch == 0u) return true;
        if (ch < 0x20u || ch > 0x7Eu || outStr.size() >= 256u) return false;
        outStr.push_back(static_cast<char>(ch));
    }
    return false; // Unterminated string inside directory block!
}

inline std::string UnpackRezTypeExt(uint32_t typVal) {
    std::string ext;
    for (int shift = 24; shift >= 0; shift -= 8) {
        const uint8_t c = static_cast<uint8_t>((typVal >> shift) & 0xFFu);
        if (c == 0u || c == ' ') continue;
        if (c < 0x21u || c > 0x7Eu || c == '/' || c == '\\' || c == '.') return {};
        ext.push_back(static_cast<char>(c));
    }
    return ext;
}

inline bool WalkRezDirBlock(
    const uint8_t* data,
    size_t archiveSize,
    uint32_t dirPos,
    uint32_t dirSize,
    int depth,
    const std::string& prefix,
    std::set<uint64_t>& visitedDirs,
    ArchiveSummaryInfo& outInfo)
{
    if (depth > 32) return false;
    if (dirSize == 0u) return true;
    if (dirPos < 163u || static_cast<uint64_t>(dirPos) + dirSize > archiveSize) return false;

    const uint64_t dirKey = (static_cast<uint64_t>(dirPos) << 32) | dirSize;
    if (!visitedDirs.insert(dirKey).second) return false; // Directory cycle bomb!

    size_t p = dirPos;
    const size_t blockEnd = static_cast<size_t>(dirPos) + dirSize;

    while (p < blockEnd) {
        if (blockEnd - p < 4u) return false;
        const uint32_t entryType = ByteIO::RdU32LE(data + p);
        p += 4u;

        if (entryType == 1u) {
            // Subdirectory entry: DirPos(4) + DirSize(4) + DirTime(4) + DirName(zstr)
            if (blockEnd - p < 12u) return false;
            const uint32_t subPos  = ByteIO::RdU32LE(data + p + 0u);
            const uint32_t subSize = ByteIO::RdU32LE(data + p + 4u);
            p += 12u;

            std::string dirName;
            if (!ReadCStringBounded(data, blockEnd, p, dirName) ||
                !GamePakCommon::IsSafeSegmentAscii(dirName)) {
                return false;
            }
            if (subSize > 0u) {
                if (!WalkRezDirBlock(
                        data, archiveSize, subPos, subSize, depth + 1,
                        prefix + dirName + "/", visitedDirs, outInfo)) {
                    return false;
                }
            }
        } else if (entryType == 0u) {
            // Resource file entry: Pos(4) + Size(4) + Time(4) + ID(4) + Type(4) + NumKeys(4) + Name(zstr) + Comment(zstr) + Keys[NumKeys]
            if (blockEnd - p < 24u) return false;
            const uint32_t fPos    = ByteIO::RdU32LE(data + p + 0u);
            const uint32_t fSize   = ByteIO::RdU32LE(data + p + 4u);
            const uint32_t fTime   = ByteIO::RdU32LE(data + p + 8u);
            const uint32_t fType   = ByteIO::RdU32LE(data + p + 16u);
            const uint32_t numKeys = ByteIO::RdU32LE(data + p + 20u);
            p += 24u;

            std::string rezName, comment;
            if (!ReadCStringBounded(data, blockEnd, p, rezName) ||
                !ReadCStringBounded(data, blockEnd, p, comment)) {
                return false;
            }
            if (numKeys > 4096u || static_cast<uint64_t>(numKeys) * 4ULL > blockEnd - p) {
                return false;
            }
            p += static_cast<size_t>(numKeys) * 4u;

            if (rezName.empty() || (fSize > 0u && fPos < 163u)) return false;
            const std::string ext = UnpackRezTypeExt(fType);
            std::string fileSeg = rezName;
            if (!ext.empty()) {
                fileSeg.push_back('.');
                fileSeg += ext;
            }
            if (!GamePakCommon::IsSafeSegmentAscii(fileSeg)) return false;
            if (outInfo.entries.size() >= 262144u) return false;

            if (!GamePakCommon::AppendRawFileEntry(
                    outInfo, prefix + fileSeg, fPos, fSize, archiveSize, fTime)) {
                return false;
            }
        } else {
            return false; // Unknown RezMgr entry type
        }
    }

    return true;
}

} // namespace DetailLithTechRez

inline bool InspectLithTechRezArchive(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (!data || size < 168u) return false;
    if (data[0] != '\r' || data[1] != '\n') return false;
    if (std::memcmp(data + 2u, "RezMgr Version 1", 16) != 0) return false;
    if (data[126u] != 0x1Au) return false;

    const uint32_t fmtVer      = ByteIO::RdU32LE(data + 127u);
    const uint32_t rootDirPos  = ByteIO::RdU32LE(data + 131u);
    const uint32_t rootDirSize = ByteIO::RdU32LE(data + 135u);
    if (fmtVer != 1u || rootDirSize == 0u || rootDirPos < 163u) return false;
    if (static_cast<uint64_t>(rootDirPos) + rootDirSize > size) return false;

    outInfo = {};
    outInfo.formatName = L"Monolith LithTech REZ Archive (.rez)";

    std::set<uint64_t> visitedDirs;
    if (!DetailLithTechRez::WalkRezDirBlock(
            data, size, rootDirPos, rootDirSize, 0, "", visitedDirs, outInfo) ||
        outInfo.entries.empty()) {
        outInfo = {};
        return false;
    }

    outInfo.valid = true;
    return true;
}

inline bool ExtractLithTechRezEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    return GamePakCommon::ExtractByParsedSummary(InspectLithTechRezArchive, data, size, normTarget, outBytes);
}

} // namespace Explorer2::Formats
