#pragma once
// NeroNrg.h — Nero Burning ROM NER5 / NERO optical disc image reader (.nrg).

#include "../common/ArchiveFormatCommon.h"
#include "UdfImage.h"

namespace Explorer2::Formats {

namespace DetailUdfNrg {

inline bool BuildNeroNrgReader(
    const uint8_t* data,
    size_t size,
    DetailArc100::VirtualDiskReader& outReader)
{
    if (!data || size < 64u) return false;

    uint64_t chunkOff = 0;
    size_t trailerEnd = 0;
    if (size >= 12u && std::memcmp(data + size - 12u, "NER5", 4) == 0) {
        chunkOff   = RdU64BE(data + size - 8u);
        trailerEnd = size - 12u;
    } else if (size >= 8u && std::memcmp(data + size - 8u, "NERO", 4) == 0) {
        chunkOff   = RdU32BE(data + size - 4u);
        trailerEnd = size - 8u;
    } else {
        return false;
    }

    if (chunkOff >= trailerEnd || trailerEnd - static_cast<size_t>(chunkOff) > 16u * 1024u * 1024u) {
        return false;
    }

    bool foundTrack = false;
    bool sawEnd = false;
    uint64_t trackDataOff = 0;
    uint64_t trackEndOff  = trailerEnd;
    uint32_t rawSecSize   = 2048u;
    uint8_t  nrgMode      = 0u;
    uint32_t startLba     = 0u;

    size_t pos = static_cast<size_t>(chunkOff);
    while (pos + 8u <= trailerEnd) {
        const uint8_t* ch = data + pos;
        uint32_t cLen = RdU32BE(ch + 4);
        pos += 8u;
        if (std::memcmp(ch, "END!", 4) == 0) {
            sawEnd = true;
            break;
        }
        if (cLen > trailerEnd - pos) return false;
        const uint8_t* body = data + pos;

        if (!foundTrack && std::memcmp(ch, "DAOX", 4) == 0 && cLen >= 22u + 42u) {
            uint16_t ss  = RdU16BE(body + 22u + 12u);
            uint8_t  md  = body[22u + 14u];
            uint64_t idx1 = RdU64BE(body + 22u + 26u);
            uint64_t endO = RdU64BE(body + 22u + 34u);
            if (idx1 < trailerEnd && endO > idx1 && endO <= trailerEnd &&
                (ss == 2048u || ss == 2336u || ss == 2352u || ss == 2448u))
            {
                trackDataOff = idx1;
                trackEndOff  = endO;
                rawSecSize   = ss;
                nrgMode      = md;
                foundTrack   = true;
            }
        } else if (!foundTrack && std::memcmp(ch, "DAOO", 4) == 0 && cLen >= 22u + 30u) {
            uint16_t ss  = RdU16BE(body + 22u + 12u);
            uint8_t  md  = body[22u + 14u];
            uint32_t idx1 = RdU32BE(body + 22u + 22u);
            uint32_t endO = RdU32BE(body + 22u + 26u);
            if (idx1 < trailerEnd && endO > idx1 && endO <= trailerEnd &&
                (ss == 2048u || ss == 2336u || ss == 2352u || ss == 2448u))
            {
                trackDataOff = idx1;
                trackEndOff  = endO;
                rawSecSize   = ss;
                nrgMode      = md;
                foundTrack   = true;
            }
        } else if (!foundTrack && std::memcmp(ch, "ETN2", 4) == 0 && cLen >= 32u) {
            uint64_t tOff = RdU64BE(body + 0u);
            uint64_t tLen = RdU64BE(body + 8u);
            uint32_t tMod = RdU32BE(body + 16u);
            uint32_t tLba = RdU32BE(body + 20u);
            if (tOff < trailerEnd && tLen > 0u && tLen <= trailerEnd - static_cast<size_t>(tOff)) {
                trackDataOff = tOff;
                trackEndOff  = tOff + tLen;
                nrgMode      = static_cast<uint8_t>(tMod & 0xFFu);
                rawSecSize   = (tMod == 3u) ? 2336u : ((tMod == 6u || tMod == 7u) ? 2352u : ((tMod >= 15u) ? 2448u : 2048u));
                startLba     = tLba;
                foundTrack   = true;
            }
        } else if (!foundTrack && std::memcmp(ch, "ETNF", 4) == 0 && cLen >= 20u) {
            uint32_t tOff = RdU32BE(body + 0u);
            uint32_t tLen = RdU32BE(body + 4u);
            uint32_t tMod = RdU32BE(body + 8u);
            uint32_t tLba = RdU32BE(body + 12u);
            if (tOff < trailerEnd && tLen > 0u && tLen <= trailerEnd - tOff) {
                trackDataOff = tOff;
                trackEndOff  = static_cast<uint64_t>(tOff) + tLen;
                nrgMode      = static_cast<uint8_t>(tMod & 0xFFu);
                rawSecSize   = (tMod == 3u) ? 2336u : ((tMod == 6u || tMod == 7u) ? 2352u : ((tMod >= 15u) ? 2448u : 2048u));
                startLba     = tLba;
                foundTrack   = true;
            }
        }

        pos += cLen;
    }

    if (!foundTrack) {
        if (!sawEnd || chunkOff < 34816ULL) return false;
        trackDataOff = 0;
        trackEndOff  = chunkOff;
        rawSecSize   = 2048u;
    }

    const uint64_t trackSectors = (trackEndOff - trackDataOff) / rawSecSize;
    if (trackSectors < 17ULL) return false;
    const uint64_t totalLogicalSectors = static_cast<uint64_t>(startLba) + trackSectors;
    const uint64_t virtBytes = totalLogicalSectors * 2048ULL;

    outReader.virtualSize = virtBytes;
    outReader.formatName  = L"Nero Optical Image (NRG)";
    outReader.directRawBuffer = false;
    outReader.allowRawSliceFallback = false;
    outReader.readBytes = [=](uint64_t diskOff, uint8_t* dst, size_t len) -> bool {
        if (len == 0u) return diskOff <= virtBytes;
        if (diskOff > virtBytes || len > virtBytes - diskOff) return false;

        uint64_t cur = diskOff;
        size_t rem = len;
        while (rem > 0u) {
            uint64_t lba    = cur / 2048ULL;
            size_t inSecOff = static_cast<size_t>(cur % 2048ULL);
            size_t take     = std::min<size_t>(rem, 2048u - inSecOff);

            if (lba < startLba) {
                std::memset(dst, 0, take);
            } else {
                uint64_t rawOff = trackDataOff + (lba - startLba) * rawSecSize;
                if (rawOff > size || rawSecSize > size - static_cast<size_t>(rawOff)) return false;
                const uint8_t* secPtr = data + static_cast<size_t>(rawOff);
                size_t userHdrSkip = 0u;
                if (rawSecSize == 2336u) {
                    userHdrSkip = 8u;
                } else if (rawSecSize == 2352u || rawSecSize == 2448u) {
                    static const uint8_t kCdSync[12] = {
                        0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00
                    };
                    if (std::memcmp(secPtr, kCdSync, 12) == 0) {
                        userHdrSkip = (secPtr[15] == 2u) ? 24u : 16u;
                    } else {
                        userHdrSkip = (nrgMode == 3u || nrgMode == 6u) ? 24u : 16u;
                    }
                }
                if (userHdrSkip + inSecOff + take > rawSecSize) return false;
                std::memcpy(dst, secPtr + userHdrSkip + inSecOff, take);
            }

            dst += take;
            cur += take;
            rem -= take;
        }
        return true;
    };
    return true;
}

} // namespace DetailUdfNrg

inline bool ParseOrExtractNeroNrg(
    const uint8_t* data,
    size_t size,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm,
    std::vector<uint8_t>* outExtracted)
{
    // 1. Nero Burning ROM Optical Image (.nrg — NER5 / NERO trailer)
    DetailArc100::VirtualDiskReader nrgReader;
    if (DetailUdfNrg::BuildNeroNrgReader(data, size, nrgReader)) {
        if (DetailUdfNrg::ParseOrExtractUdfOnReader(nrgReader, 0, L"", outInfo, extractTargetNorm, outExtracted)) {
            if (outInfo) outInfo->formatName = L"Nero Optical Image (.nrg / " + outInfo->formatName + L")";
            return true;
        }
        ArchiveSummaryInfo isoInfo;
        ArchiveSummaryInfo& targetInfo = outInfo ? *outInfo : isoInfo;
        if (DetailArc100::InspectOrExtractIsoOnReader(nrgReader, 0, L"", targetInfo, extractTargetNorm, outExtracted)) {
            if (outInfo) {
                outInfo->formatName = L"Nero Optical Image (.nrg / ISO 9660)";
                outInfo->valid = true;
            }
            return true;
        }
        return false;
    }
    return false;
}

} // namespace Explorer2::Formats
