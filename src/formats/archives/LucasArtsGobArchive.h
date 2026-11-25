#pragma once
// LucasArtsGobArchive.h — LucasArts Dark Forces ("GOB\x0A") & Jedi Knight ("GOB " v20) Container (.gob)
// Supports both:
//   1. Dark Forces (1995): "GOB\x0A" + masterOffset -> uint32 count + 21-byte 8.3 DOS entries
//   2. Jedi Knight / MotS (1997): "GOB " + version(20) + dirOffset(12) + count + 136-byte hierarchical entries

#include "../common/GamePakArchiveCommon.h"

namespace Explorer2::Formats {

inline bool InspectLucasArtsGobArchive(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (!data || size < 16u) return false;

    // 1. Star Wars: Dark Forces ("GOB\x0A")
    if (std::memcmp(data, "GOB\x0A", 4) == 0) {
        const uint32_t masterOff = ByteIO::RdU32LE(data + 4u);
        if (masterOff < 8u || static_cast<uint64_t>(masterOff) + 4ULL > size) return false;
        const uint32_t count = ByteIO::RdU32LE(data + masterOff);
        if (count == 0u || count > 65536u) return false;
        if (static_cast<uint64_t>(masterOff) + 4ULL + static_cast<uint64_t>(count) * 21ULL > size) {
            return false;
        }

        outInfo = {};
        outInfo.formatName = L"LucasArts Dark Forces GOB Archive (.gob)";
        outInfo.entries.reserve(count);

        for (uint32_t i = 0; i < count; ++i) {
            const uint8_t* ent = data + masterOff + 4u + static_cast<size_t>(i) * 21u;
            const uint32_t fOff = ByteIO::RdU32LE(ent + 0u);
            const uint32_t fLen = ByteIO::RdU32LE(ent + 4u);
            const char* namePtr = reinterpret_cast<const char*>(ent + 8u);
            const size_t nlen   = strnlen(namePtr, 13u);
            if (nlen == 0u || nlen == 13u || fOff < 8u) {
                outInfo = {};
                return false;
            }
            std::string_view rawName(namePtr, nlen);
            if (!GamePakCommon::IsSafeSegmentAscii(rawName) ||
                !GamePakCommon::AppendRawFileEntry(outInfo, rawName, fOff, fLen, size)) {
                outInfo = {};
                return false;
            }
        }
        outInfo.valid = !outInfo.entries.empty();
        return outInfo.valid;
    }

    // 2. Star Wars Jedi Knight: Dark Forces II ("GOB " v20)
    if (std::memcmp(data, "GOB ", 4) == 0) {
        const uint32_t version = ByteIO::RdU32LE(data + 4u);
        const uint32_t dirOff  = ByteIO::RdU32LE(data + 8u);
        if (version != 20u || dirOff < 12u || static_cast<uint64_t>(dirOff) + 4ULL > size) {
            return false;
        }
        const uint32_t count = ByteIO::RdU32LE(data + dirOff);
        if (count == 0u || count > 131072u) return false;
        const uint64_t tableEnd = static_cast<uint64_t>(dirOff) + 4ULL + static_cast<uint64_t>(count) * 136ULL;
        if (tableEnd > size) return false;

        outInfo = {};
        outInfo.formatName = L"LucasArts Jedi Knight GOB v20 Archive (.gob)";
        outInfo.entries.reserve(count);

        for (uint32_t i = 0; i < count; ++i) {
            const uint8_t* ent = data + dirOff + 4u + static_cast<size_t>(i) * 136u;
            const uint32_t fOff = ByteIO::RdU32LE(ent + 0u);
            const uint32_t fLen = ByteIO::RdU32LE(ent + 4u);
            const char* pathPtr = reinterpret_cast<const char*>(ent + 8u);
            const size_t plen   = strnlen(pathPtr, 128u);
            if (plen == 0u || plen == 128u || fOff < tableEnd) {
                outInfo = {};
                return false;
            }
            if (!GamePakCommon::AppendRawFileEntry(
                    outInfo, std::string_view(pathPtr, plen), fOff, fLen, size)) {
                outInfo = {};
                return false;
            }
        }
        outInfo.valid = !outInfo.entries.empty();
        return outInfo.valid;
    }

    return false;
}

inline bool ExtractLucasArtsGobEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    return GamePakCommon::ExtractByParsedSummary(InspectLucasArtsGobArchive, data, size, normTarget, outBytes);
}

} // namespace Explorer2::Formats
