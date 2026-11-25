#pragma once
// VolitionVpArchive.h — Volition FreeSpace 1 & 2 "VPVP" v2 Package (.vp)
// Parses 16-byte VPVP v2 header + 44-byte directory table with hierarchical push/pop ("..") folders
// and byte-exact uncompressed lump extraction.

#include "../common/GamePakArchiveCommon.h"

namespace Explorer2::Formats {

inline bool InspectVolitionVpArchive(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (!data || size < 60u) return false;
    if (std::memcmp(data, "VPVP", 4) != 0) return false;

    const int32_t version    = ByteIO::RdI32LE(data + 0x04);
    const int32_t dirOffset  = ByteIO::RdI32LE(data + 0x08);
    const int32_t dirEntries = ByteIO::RdI32LE(data + 0x0C);
    if (version != 2 || dirOffset < 16 || dirEntries <= 0 || dirEntries > 131072) {
        return false;
    }

    const uint64_t dirEnd = static_cast<uint64_t>(dirOffset) + static_cast<uint64_t>(dirEntries) * 44ULL;
    if (dirEnd > size) return false;

    outInfo = {};
    outInfo.formatName = L"Volition FreeSpace VP Archive (.vp)";

    std::vector<std::string> dirStack;
    for (int32_t i = 0; i < dirEntries; ++i) {
        const uint8_t* ent      = data + static_cast<size_t>(dirOffset) + static_cast<size_t>(i) * 44u;
        const int32_t  fOff     = ByteIO::RdI32LE(ent + 0x00);
        const int32_t  fSize    = ByteIO::RdI32LE(ent + 0x04);
        const char*    nameRaw  = reinterpret_cast<const char*>(ent + 0x08);
        const uint32_t writeTs  = ByteIO::RdU32LE(ent + 0x28);

        const size_t nlen = strnlen(nameRaw, 32u);
        if (nlen == 0u || nlen == 32u) return false; // Must be NUL-terminated within 32 bytes
        std::string_view nameSv(nameRaw, nlen);

        if (nameSv == "..") {
            if (dirStack.empty()) return false; // Directory stack underflow!
            dirStack.pop_back();
            continue;
        }

        // Volition cfile.cpp convention: (!size && !write_time) denotes entering a subdirectory
        if (fSize == 0 && writeTs == 0u) {
            if (!GamePakCommon::IsSafeSegmentAscii(nameSv) || dirStack.size() >= 32u) return false;
            dirStack.emplace_back(nameSv);
            continue;
        }

        if (fOff < 16 || fSize < 0) return false;
        if (!GamePakCommon::IsSafeSegmentAscii(nameSv)) return false;

        std::string fullRel;
        for (const auto& d : dirStack) {
            fullRel += d;
            fullRel.push_back('/');
        }
        fullRel.append(nameSv.data(), nameSv.size());

        if (!GamePakCommon::AppendRawFileEntry(
                outInfo, fullRel, static_cast<uint64_t>(fOff), static_cast<uint64_t>(fSize), size, writeTs)) {
            outInfo = {};
            return false;
        }
    }

    if (outInfo.entries.empty()) {
        outInfo = {};
        return false;
    }
    outInfo.valid = true;
    return true;
}

inline bool ExtractVolitionVpEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    return GamePakCommon::ExtractByParsedSummary(InspectVolitionVpArchive, data, size, normTarget, outBytes);
}

} // namespace Explorer2::Formats
