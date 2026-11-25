#pragma once
// WestwoodMixArchive.h — Westwood Studios Command & Conquer / Red Alert / Tiberian Sun MIX (.mix) Reader
// Supports both classic Tiberian Dawn 6-byte headers `[u16 count][u32 bodySize]` and Red Alert / TS / RA2
// 10-byte flags headers `[u16 0][u16 flags][u16 count][u32 bodySize]` (including optional 20-byte SHA-1
// checksum trailer; encrypted Blowfish headers `flags & 0x0002` are cleanly declined without misparsing).
// Resolves SubBlock 32-bit hashes via BOTH Westwood TD/RA1 rolling-rotate hash and TS/RA2 XCC CRC32 against
// embedded `"local mix database.dat"` XCC name tables + built-in C&C asset dictionary + `MIX_<CRC8>.bin`.

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailWestwoodMix {

// Westwood Tiberian Dawn / Red Alert 1 rolling 32-bit hash on uppercase 4-byte groups
inline uint32_t HashWestwoodTd(std::string_view name) {
    uint32_t id = 0;
    size_t i = 0;
    const size_t n = name.size();
    while (i < n) {
        uint32_t group = 0;
        for (size_t b = 0; b < 4u && i < n; ++b, ++i) {
            const uint8_t up = static_cast<uint8_t>(std::toupper(static_cast<unsigned char>(name[i])));
            group |= static_cast<uint32_t>(up) << (b * 8u);
        }
        id = ((id << 1u) | (id >> 31u)) + group;
    }
    return id;
}

// Westwood Tiberian Sun / Red Alert 2 padded uppercase IEEE CRC-32 (Olaf van der Spek XCC spec)
inline uint32_t HashWestwoodTs(std::string_view name) {
    std::string up;
    up.reserve(name.size() + 4u);
    for (char c : name) {
        up.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    const size_t l = up.size();
    const size_t a = l >> 2u;
    if ((l & 3u) != 0) {
        up.push_back(static_cast<char>(l - (a << 2u)));
        size_t pad = 3u - (l & 3u);
        while (pad-- > 0) {
            up.push_back(up[a << 2u]);
        }
    }
    return Checksum::Crc32(reinterpret_cast<const uint8_t*>(up.data()), up.size());
}

inline void RegisterCandidateName(std::unordered_map<uint32_t, std::string>& lut, std::string_view nm) {
    if (!ReaderText::IsSafeRelativeSubFile(nm)) return;
    lut.emplace(HashWestwoodTd(nm), std::string(nm));
    lut.emplace(HashWestwoodTs(nm), std::string(nm));
}

// Parses Olaf van der Spek's embedded `"local mix database.dat"` XCC filename table:
//   [0..31] "XCC by Olaf van der Spek\x1a\x04\x17\x27\x10\x19\x80\x00"
//   [32..51] uint32LE size, type, version, game, count
//   [52..]   `count` NUL-terminated ASCII filenames
inline void ParseLocalMixDatabase(
    const uint8_t* dbBytes,
    size_t dbSize,
    std::unordered_map<uint32_t, std::string>& lut)
{
    if (!dbBytes || dbSize < 8u) return;
    size_t pos = 0;
    uint32_t maxCount = 65536u;
    if (dbSize >= 52u && std::memcmp(dbBytes, "XCC by Olaf van der Spek", 24) == 0) {
        maxCount = std::min<uint32_t>(ByteIO::RdU32LE(dbBytes + 48u), 65536u);
        pos = 52u;
    } else if (dbSize >= 8u && std::memcmp(dbBytes, "LMD\0", 4) == 0) {
        maxCount = std::min<uint32_t>(ByteIO::RdU32LE(dbBytes + 4u), 65536u);
        pos = 8u;
    } else {
        return;
    }

    uint32_t parsed = 0;
    while (pos < dbSize && parsed < maxCount) {
        const size_t st = pos;
        while (pos < dbSize && dbBytes[pos] != 0) ++pos;
        if (pos >= dbSize) break;
        std::string_view nm(reinterpret_cast<const char*>(dbBytes + st), pos - st);
        ++pos;
        if (!nm.empty()) {
            RegisterCandidateName(lut, nm);
            ++parsed;
        }
    }
}

inline bool ParseMixArchive(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    ArchiveSummaryInfo& outInfo)
{
    if (!data || size < 18u) return false;
    if (extLower != L".mix") return false;

    bool hasFlags = false;
    uint16_t flags = 0;
    uint16_t count = 0;
    uint32_t bodySize = 0;
    size_t indexOff = 0;

    const uint16_t firstWord = ByteIO::RdU16LE(data + 0);
    if (firstWord == 0) {
        if (size < 22u) return false;
        hasFlags = true;
        flags = ByteIO::RdU16LE(data + 2);
        // Reject unknown flags or Blowfish-encrypted header (`0x0002`)
        if ((flags & ~0x0003u) != 0 || (flags & 0x0002u) != 0) return false;
        count = ByteIO::RdU16LE(data + 4);
        bodySize = ByteIO::RdU32LE(data + 6);
        indexOff = 10u;
    } else {
        count = firstWord;
        bodySize = ByteIO::RdU32LE(data + 2);
        indexOff = 6u;
    }

    if (count == 0 || count > 16384u || bodySize == 0) return false;
    const uint64_t bodyBase = indexOff + static_cast<uint64_t>(count) * 12ULL;
    const uint64_t shaTrailer = (hasFlags && (flags & 0x0001u)) ? 20ULL : 0ULL;
    if (bodyBase > size || static_cast<uint64_t>(bodySize) + shaTrailer > size - bodyBase) return false;
    // Anti-false-positive: real MIX file size matches header + index + body (+ optional 20B SHA-1) closely
    if (size - (bodyBase + static_cast<uint64_t>(bodySize) + shaTrailer) > 16ULL) return false;

    struct RawMixRec {
        uint32_t id = 0;
        uint32_t relOff = 0;
        uint32_t len = 0;
    };
    std::vector<RawMixRec> recs(count);
    std::unordered_set<uint32_t> seenIds;
    seenIds.reserve(count);

    for (uint16_t i = 0; i < count; ++i) {
        const uint8_t* p = data + indexOff + static_cast<size_t>(i) * 12u;
        recs[i].id     = ByteIO::RdU32LE(p + 0);
        recs[i].relOff = ByteIO::RdU32LE(p + 4);
        recs[i].len    = ByteIO::RdU32LE(p + 8);
        if (static_cast<uint64_t>(recs[i].relOff) + static_cast<uint64_t>(recs[i].len) > bodySize) {
            return false;
        }
        // MIX SubBlock IDs in a valid archive are unique
        if (!seenIds.insert(recs[i].id).second) return false;
    }

    // Build hash -> filename lookup table (built-in common C&C names + embedded local mix database.dat)
    std::unordered_map<uint32_t, std::string> nameLut;
    static const char* const kKnownWestwoodNames[] = {
        "local mix database.dat",
        "rules.ini", "art.ini", "sound.ini", "ai.ini", "eva.ini", "theme.ini",
        "temperat.pal", "snow.pal", "interior.pal", "desert.pal", "unittem.pal", "isosnow.pal",
        "conquer.eng", "general.eng", "mission.ini", "allies.ini", "soviet.ini", "tutorial.ini"
    };
    for (const char* kn : kKnownWestwoodNames) RegisterCandidateName(nameLut, kn);

    const uint32_t lmdTd = HashWestwoodTd("local mix database.dat");
    const uint32_t lmdTs = HashWestwoodTs("local mix database.dat");
    for (const auto& r : recs) {
        const uint8_t* pay = data + static_cast<size_t>(bodyBase) + r.relOff;
        if (r.id == lmdTd || r.id == lmdTs ||
            (r.len >= 52u && std::memcmp(pay, "XCC by Olaf van der Spek", 24) == 0))
        {
            ParseLocalMixDatabase(pay, r.len, nameLut);
        }
    }

    outInfo.formatName = hasFlags
        ? L"Westwood C&C MIX Archive (RA/TS)"
        : L"Westwood C&C MIX Archive (Classic)";
    outInfo.entries.reserve(count);

    for (const auto& r : recs) {
        std::wstring wpath;
        auto nit = nameLut.find(r.id);
        if (nit != nameLut.end()) {
            wpath = ArchivePath::Normalize(PathUtils::Utf8ToWide(nit->second));
        } else {
            wchar_t hexName[32] = {};
            swprintf_s(hexName, L"MIX_%08X.bin", r.id);
            wpath = hexName;
        }

        ArchiveItemEntry item;
        item.pathInArchive = std::move(wpath);
        item.name = ArchivePath::BaseName(item.pathInArchive);
        item.uncompressedSize = r.len;
        item.compressedSize = r.len;
        item.rawDataOffset = bodyBase + r.relOff;
        item.crc32 = r.id;
        item.compressionMethod = 0;
        outInfo.entries.push_back(std::move(item));
    }

    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

} // namespace DetailWestwoodMix

inline bool InspectWestwoodMixArchive(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    ArchiveSummaryInfo& outInfo)
{
    return DetailWestwoodMix::ParseMixArchive(data, size, extLower, outInfo);
}

inline bool ExtractWestwoodMixEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    outBytes.clear();
    ArchiveSummaryInfo info;
    if (!DetailWestwoodMix::ParseMixArchive(data, size, extLower, info)) return false;
    for (const auto& e : info.entries) {
        if (!e.isDirectory && DetailDiskAndGameArc::PathEqualsCI(e.pathInArchive, normTarget)) {
            if (e.rawDataOffset > size || e.uncompressedSize > size - e.rawDataOffset) return false;
            outBytes.assign(
                data + static_cast<size_t>(e.rawDataOffset),
                data + static_cast<size_t>(e.rawDataOffset + e.uncompressedSize));
            return true;
        }
    }
    return false;
}

} // namespace Explorer2::Formats
