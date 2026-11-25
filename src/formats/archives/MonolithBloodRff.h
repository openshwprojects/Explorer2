#pragma once
// MonolithBloodRff.h — Monolith Blood "RFF\x1A" v2.0 / v3.0 / v3.1 Resource Archive (.rff)
// Supports Blood directory XOR keystream decryption (v3.0 & v3.1) + per-lump first-256-byte XOR decryption.

#include "../common/GamePakArchiveCommon.h"

namespace Explorer2::Formats {

namespace DetailBloodRff {

struct RffLumpMeta {
    std::wstring normPath;
    uint32_t offset    = 0u;
    uint32_t sizeBytes = 0u;
    uint32_t unixTime  = 0u;
    bool     encrypted = false;
};

inline bool ParseRffDirectory(
    const uint8_t* data,
    size_t size,
    std::vector<RffLumpMeta>& outLumps,
    uint16_t& outVer)
{
    outLumps.clear();
    if (!data || size < 80u) return false;
    if (std::memcmp(data, "RFF\x1A", 4) != 0) return false;

    outVer = ByteIO::RdU16LE(data + 4u);
    if (outVer != 0x0200u && outVer != 0x0300u && outVer != 0x0301u) return false;

    const uint32_t dirOff     = ByteIO::RdU32LE(data + 8u);
    const uint32_t numEntries = ByteIO::RdU32LE(data + 12u);
    if (numEntries == 0u || numEntries > 65536u || dirOff < 32u || dirOff > size) return false;

    const uint64_t dirBytes = static_cast<uint64_t>(numEntries) * 48ULL;
    if (dirBytes > size - dirOff) return false;

    std::vector<uint8_t> dirBuf(data + dirOff, data + dirOff + static_cast<size_t>(dirBytes));
    if (outVer == 0x0300u) {
        uint32_t key = dirOff;
        for (size_t i = 0u; i < dirBuf.size(); ++i, ++key) {
            dirBuf[i] ^= static_cast<uint8_t>((key >> 1u) & 0xFFu);
        }
    } else if (outVer == 0x0301u) {
        uint32_t key = dirOff << 1u;
        for (size_t i = 0u; i < dirBuf.size(); ++i, ++key) {
            dirBuf[i] ^= static_cast<uint8_t>((key >> 1u) & 0xFFu);
        }
    }

    outLumps.reserve(numEntries);
    for (uint32_t i = 0u; i < numEntries; ++i) {
        const uint8_t* ent = dirBuf.data() + static_cast<size_t>(i) * 48u;
        const uint32_t fOff  = ByteIO::RdU32LE(ent + 0x10u);
        const uint32_t fSize = ByteIO::RdU32LE(ent + 0x14u);
        const uint32_t fTime = ByteIO::RdU32LE(ent + 0x1Cu);
        const uint8_t  flags = ent[0x20u];

        if (fOff < 32u || fOff > size || fSize > size - fOff) return false;

        std::string ext, stem;
        if (!FormatPrologue::ReadPrintableFixedAscii(ent + 0x21u, 3u, ext, false) ||
            !FormatPrologue::ReadPrintableFixedAscii(ent + 0x24u, 8u, stem, false) ||
            stem.empty()) {
            return false;
        }
        std::string fileName = stem;
        if (!ext.empty()) {
            fileName.push_back('.');
            fileName += ext;
        }
        if (!GamePakCommon::IsSafeSegmentAscii(fileName)) return false;

        RffLumpMeta lm{};
        lm.normPath  = ArchivePath::Normalize(PathUtils::Utf8ToWide(fileName));
        lm.offset    = fOff;
        lm.sizeBytes = fSize;
        lm.unixTime  = fTime;
        lm.encrypted = ((flags & 0x10u) != 0u);
        outLumps.push_back(std::move(lm));
    }
    return !outLumps.empty();
}

} // namespace DetailBloodRff

inline bool InspectMonolithBloodRff(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    std::vector<DetailBloodRff::RffLumpMeta> lumps;
    uint16_t ver = 0u;
    if (!DetailBloodRff::ParseRffDirectory(data, size, lumps, ver)) return false;

    outInfo = {};
    wchar_t fmt[96] = {};
    swprintf_s(fmt, L"Monolith Blood RFF Archive (.rff v%u.%u)", (ver >> 8u) & 0xFFu, ver & 0xFFu);
    outInfo.formatName = fmt;
    outInfo.entries.reserve(lumps.size());

    for (const auto& lm : lumps) {
        ArchiveItemEntry item{};
        item.pathInArchive     = lm.normPath;
        item.name              = ArchivePath::BaseName(lm.normPath);
        item.isDirectory       = false;
        item.uncompressedSize  = lm.sizeBytes;
        item.compressedSize    = lm.sizeBytes;
        item.rawDataOffset     = lm.offset;
        item.compressionMethod = 0u;
        GamePakCommon::UnixSecToDosDateTime(lm.unixTime, item.dosDate, item.dosTime);
        outInfo.entries.push_back(std::move(item));
    }

    outInfo.valid = true;
    return true;
}

inline bool ExtractMonolithBloodRffEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    outBytes.clear();
    std::vector<DetailBloodRff::RffLumpMeta> lumps;
    uint16_t ver = 0u;
    if (!DetailBloodRff::ParseRffDirectory(data, size, lumps, ver)) return false;

    for (const auto& lm : lumps) {
        if (DetailDiskAndGameArc::PathEqualsCI(lm.normPath, normTarget)) {
            if (lm.sizeBytes > (256u << 20)) return false;
            outBytes.assign(data + lm.offset, data + lm.offset + lm.sizeBytes);
            if (lm.encrypted) {
                const size_t cryptLen = std::min<size_t>(outBytes.size(), 256u);
                for (size_t i = 0u; i < cryptLen; ++i) {
                    outBytes[i] ^= static_cast<uint8_t>(i >> 1u);
                }
            }
            return true;
        }
    }
    return false;
}

} // namespace Explorer2::Formats
