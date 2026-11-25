#pragma once
// TerminalRealityPod.h — Terminal Reality POD1 / "POD2" / "POD3" Archive (.pod)
// Supports:
//   - "POD3" (288B header + back-allocated 20B directory + string pool — BloodRayne / Blair Witch / 4x4 Evo)
//   - "POD2" (96B header + 20B directory + string pool — Nocturne / Monster Truck Madness 2 / Fly!)
//   - POD1  (84B count+comment header + 40B fixed-path directory — Terminal Velocity / Fury3 / Hellbender)

#include "../common/GamePakArchiveCommon.h"

namespace Explorer2::Formats {

inline bool InspectTerminalRealityPod(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    ArchiveSummaryInfo& outInfo)
{
    if (!data || size < 96u) return false;

    // 1. "POD3" (288-byte header)
    if (size >= 290u && std::memcmp(data, "POD3", 4) == 0) {
        const uint32_t fileCount   = ByteIO::RdU32LE(data + 0x058);
        const uint32_t dirOffset   = ByteIO::RdU32LE(data + 0x108);
        const uint32_t strPoolSize = ByteIO::RdU32LE(data + 0x110);
        if (fileCount == 0u || fileCount > 131072u || dirOffset < 288u || strPoolSize < 2u) {
            return false;
        }
        const uint64_t strPoolOff = static_cast<uint64_t>(dirOffset) + static_cast<uint64_t>(fileCount) * 20ULL;
        const uint64_t strPoolEnd = strPoolOff + strPoolSize;
        if (strPoolOff > size || strPoolEnd > size) return false;

        const char* strPool = reinterpret_cast<const char*>(data + static_cast<size_t>(strPoolOff));
        if (strPool[strPoolSize - 1u] != '\0') return false;

        outInfo = {};
        outInfo.formatName = L"Terminal Reality POD3 Archive (.pod)";
        outInfo.entries.reserve(fileCount);

        for (uint32_t i = 0; i < fileCount; ++i) {
            const uint8_t* ent      = data + dirOffset + static_cast<size_t>(i) * 20u;
            const uint32_t pathOff  = ByteIO::RdU32LE(ent + 0x00);
            const uint32_t fileSize = ByteIO::RdU32LE(ent + 0x04);
            const uint32_t fileOff  = ByteIO::RdU32LE(ent + 0x08);
            const uint32_t unixTs   = ByteIO::RdU32LE(ent + 0x0C);
            if (pathOff >= strPoolSize || fileOff < 288u) {
                outInfo = {};
                return false;
            }
            const size_t plen = strnlen(strPool + pathOff, strPoolSize - pathOff);
            if (plen == 0u || pathOff + plen >= strPoolSize ||
                !GamePakCommon::AppendRawFileEntry(
                    outInfo, std::string_view(strPool + pathOff, plen), fileOff, fileSize, size, unixTs)) {
                outInfo = {};
                return false;
            }
        }
        outInfo.valid = !outInfo.entries.empty();
        return outInfo.valid;
    }

    // 2. "POD2" (96-byte header)
    if (size >= 118u && std::memcmp(data, "POD2", 4) == 0) {
        const uint32_t fileCount = ByteIO::RdU32LE(data + 0x58);
        if (fileCount == 0u || fileCount > 131072u) return false;
        const uint64_t strPoolOff = 96ULL + static_cast<uint64_t>(fileCount) * 20ULL;
        if (strPoolOff + 2ULL > size) return false;

        // String pool ends where the earliest payload starts (or EOF)
        uint64_t minDataOff = size;
        for (uint32_t i = 0; i < fileCount; ++i) {
            const uint8_t* ent = data + 96u + static_cast<size_t>(i) * 20u;
            const uint32_t fOff = ByteIO::RdU32LE(ent + 0x08);
            if (fOff < strPoolOff + 2ULL || fOff > size) return false;
            minDataOff = std::min<uint64_t>(minDataOff, fOff);
        }
        const size_t strPoolSize = static_cast<size_t>(minDataOff - strPoolOff);
        const char* strPool = reinterpret_cast<const char*>(data + static_cast<size_t>(strPoolOff));

        outInfo = {};
        outInfo.formatName = L"Terminal Reality POD2 Archive (.pod)";
        outInfo.entries.reserve(fileCount);

        for (uint32_t i = 0; i < fileCount; ++i) {
            const uint8_t* ent      = data + 96u + static_cast<size_t>(i) * 20u;
            const uint32_t pathOff  = ByteIO::RdU32LE(ent + 0x00);
            const uint32_t fileSize = ByteIO::RdU32LE(ent + 0x04);
            const uint32_t fileOff  = ByteIO::RdU32LE(ent + 0x08);
            const uint32_t unixTs   = ByteIO::RdU32LE(ent + 0x0C);
            if (pathOff >= strPoolSize) {
                outInfo = {};
                return false;
            }
            const size_t plen = strnlen(strPool + pathOff, strPoolSize - pathOff);
            if (plen == 0u || pathOff + plen >= strPoolSize ||
                !GamePakCommon::AppendRawFileEntry(
                    outInfo, std::string_view(strPool + pathOff, plen), fileOff, fileSize, size, unixTs)) {
                outInfo = {};
                return false;
            }
        }
        outInfo.valid = !outInfo.entries.empty();
        return outInfo.valid;
    }

    // 3. POD1 (Terminal Velocity / Fury3 — extension-gated since no magic FourCC)
    if (extLower == L".pod" && size >= 124u) {
        const uint32_t fileCount = ByteIO::RdU32LE(data + 0x00);
        if (fileCount == 0u || fileCount > 16384u) return false;
        const uint64_t dirEnd = 84ULL + static_cast<uint64_t>(fileCount) * 40ULL;
        if (dirEnd > size) return false;

        // Validate 80-byte ASCII comment field
        for (size_t i = 0; i < 80u && data[4u + i] != 0u; ++i) {
            const uint8_t ch = data[4u + i];
            if (ch < 0x20u || ch > 0x7Eu) return false;
        }

        outInfo = {};
        outInfo.formatName = L"Terminal Reality POD1 Archive (.pod)";
        outInfo.entries.reserve(fileCount);

        for (uint32_t i = 0; i < fileCount; ++i) {
            const uint8_t* ent      = data + 84u + static_cast<size_t>(i) * 40u;
            const char*    pathPtr  = reinterpret_cast<const char*>(ent + 0x00);
            const uint32_t fileSize = ByteIO::RdU32LE(ent + 32u);
            const uint32_t fileOff  = ByteIO::RdU32LE(ent + 36u);
            const size_t   plen     = strnlen(pathPtr, 32u);
            if (plen == 0u || plen == 32u || fileOff < dirEnd) {
                outInfo = {};
                return false;
            }
            if (!GamePakCommon::AppendRawFileEntry(
                    outInfo, std::string_view(pathPtr, plen), fileOff, fileSize, size)) {
                outInfo = {};
                return false;
            }
        }
        outInfo.valid = !outInfo.entries.empty();
        return outInfo.valid;
    }

    return false;
}

inline bool ExtractTerminalRealityPodEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    auto parseFn = [&](const uint8_t* d, size_t s, ArchiveSummaryInfo& info) {
        return InspectTerminalRealityPod(d, s, extLower, info);
    };
    return GamePakCommon::ExtractByParsedSummary(parseFn, data, size, normTarget, outBytes);
}

} // namespace Explorer2::Formats
