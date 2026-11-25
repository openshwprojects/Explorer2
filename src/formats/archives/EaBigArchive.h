#pragma once
// EaBigArchive.h — EA Games BIGF / BIG4 archive reader & RefPack 0x10FB extractor (.big).

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailUnixAndLegacyArc {

// ============================================================================
// #27. EA Games BIGF / BIG4 Archive (.big — Raw + EA RefPack 0x10FB Decompression)
// ============================================================================
inline bool PeekEaRefPackHeader(
    const uint8_t* src,
    size_t srcLen,
    uint64_t& outUncompressedSize,
    size_t& outHeaderBytes)
{
    if (srcLen < 6) return false;
    uint16_t sig = RdU16BE(src);
    if ((sig & 0xFEFFu) != 0x10FBu) return false; // 0x10FB / 0x11FB / 0x90FB / 0x91FB

    size_t szBytes    = ((sig & 0x8000u) != 0) ? 4u : 3u;
    bool hasCompField = ((sig & 0x0100u) != 0);
    size_t hdrLen     = 2u + (hasCompField ? szBytes : 0u) + szBytes;
    if (srcLen < hdrLen + 1u) return false;

    auto rdBeN = [](const uint8_t* p, size_t n) -> uint64_t {
        uint64_t v = 0;
        for (size_t i = 0; i < n; ++i) v = (v << 8) | p[i];
        return v;
    };

    uint64_t uncompSz = rdBeN(src + 2u + (hasCompField ? szBytes : 0u), szBytes);
    if (uncompSz > 512ULL * 1024 * 1024) return false;
    outUncompressedSize = uncompSz;
    outHeaderBytes      = hdrLen;
    return true;
}

inline bool DecompressEaRefPack(
    const uint8_t* src,
    size_t srcLen,
    std::vector<uint8_t>& out,
    size_t maxOut = 256 * 1024 * 1024)
{
    out.clear();
    uint64_t uncompSz = 0;
    size_t pos = 0;
    if (!PeekEaRefPackHeader(src, srcLen, uncompSz, pos) || uncompSz > maxOut) {
        return false;
    }

    out.reserve(static_cast<size_t>(uncompSz));

    auto copyLits = [&](size_t litLen) -> bool {
        if (litLen > srcLen - pos || litLen > uncompSz - out.size()) return false;
        out.insert(out.end(), src + pos, src + pos + litLen);
        pos += litLen;
        return true;
    };

    auto copyMatch = [&](size_t dist, size_t copyLen) -> bool {
        if (dist == 0 || dist > out.size() || copyLen > uncompSz - out.size()) return false;
        for (size_t i = 0; i < copyLen; ++i) {
            out.push_back(out[out.size() - dist]);
        }
        return true;
    };

    bool sawStop = false;
    while (pos < srcLen) {
        uint8_t b0 = src[pos++];
        if ((b0 & 0x80u) == 0) {
            // 2-byte command (0x00..0x7F): short LZ77 back-reference
            if (pos >= srcLen) return false;
            uint8_t b1     = src[pos++];
            size_t procLen = b0 & 0x03u;
            size_t refLen  = ((b0 & 0x1Cu) >> 2) + 3u;
            size_t dist    = ((static_cast<size_t>(b0 & 0x60u) << 3) | b1) + 1u;
            if (!copyLits(procLen) || !copyMatch(dist, refLen)) return false;
        } else if ((b0 & 0xC0u) == 0x80u) {
            // 3-byte command (0x80..0xBF): medium LZ77 back-reference
            if (pos + 2 > srcLen) return false;
            uint8_t b1     = src[pos++];
            uint8_t b2     = src[pos++];
            size_t procLen = (b1 >> 6) & 0x03u;
            size_t refLen  = (b0 & 0x3Fu) + 4u;
            size_t dist    = ((static_cast<size_t>(b1 & 0x3Fu) << 8) | b2) + 1u;
            if (!copyLits(procLen) || !copyMatch(dist, refLen)) return false;
        } else if ((b0 & 0xE0u) == 0xC0u) {
            // 4-byte command (0xC0..0xDF): long LZ77 back-reference
            if (pos + 3 > srcLen) return false;
            uint8_t b1     = src[pos++];
            uint8_t b2     = src[pos++];
            uint8_t b3     = src[pos++];
            size_t procLen = b0 & 0x03u;
            size_t refLen  = ((static_cast<size_t>(b0 & 0x0Cu) << 6) | b3) + 5u;
            size_t dist    = ((static_cast<size_t>(b0 & 0x10u) << 12) |
                              (static_cast<size_t>(b1) << 8) |
                              static_cast<size_t>(b2)) + 1u;
            if (!copyLits(procLen) || !copyMatch(dist, refLen)) return false;
        } else if (b0 <= 0xFBu) {
            // 1-byte literal run (0xE0..0xFB): 4..112 literal bytes
            size_t procLen = (static_cast<size_t>(b0 & 0x1Fu) + 1u) * 4u;
            if (!copyLits(procLen)) return false;
        } else {
            // 1-byte stop command (0xFC..0xFF): 0..3 trailing literal bytes + EOF
            size_t procLen = b0 & 0x03u;
            if (!copyLits(procLen)) return false;
            sawStop = true;
            break;
        }
    }

    if (!sawStop || out.size() != uncompSz) {
        out.clear();
        return false;
    }
    return true;
}

inline bool ParseOrExtractEaBigArchive(
    const uint8_t* data,
    size_t size,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm = nullptr,
    std::vector<uint8_t>* outExtracted = nullptr)
{
    if (size < 16 || (std::memcmp(data, "BIGF", 4) != 0 && std::memcmp(data, "BIG4", 4) != 0)) return false;
    uint32_t numFiles = RdU32BE(data + 8);
    uint32_t hdrEnd   = RdU32BE(data + 12);
    if (numFiles == 0 || numFiles > 60000) return false;

    if (outInfo) {
        outInfo->formatName = L"EA Games BIG Archive (.big)";
    }

    size_t pos   = 16;
    size_t limit = std::min<size_t>(size, hdrEnd > 16 ? hdrEnd : size);

    for (uint32_t i = 0; i < numFiles && pos + 9 <= limit; ++i) {
        uint32_t fOff  = RdU32BE(data + pos + 0);
        uint32_t fSize = RdU32BE(data + pos + 4);
        pos += 8;

        size_t ns = pos;
        while (pos < limit && data[pos] != 0) ++pos;
        if (pos >= limit) return false;
        std::string fname(reinterpret_cast<const char*>(data + ns), pos - ns);
        ++pos;

        if (fname.empty()) continue;
        if (fOff > size || fSize > size - fOff) {
            if (outInfo) { outInfo->entries.clear(); outInfo->valid = false; }
            return false;
        }

        std::wstring fullPath = ArchivePath::Normalize(PathUtils::Utf8ToWide(fname));
        if (fullPath.empty()) continue;

        const uint8_t* payload = data + fOff;
        uint64_t refUncompSz   = 0;
        size_t refHdrLen       = 0;
        bool isRefPack         = PeekEaRefPackHeader(payload, fSize, refUncompSz, refHdrLen);

        if (extractTargetNorm && outExtracted) {
            if (_wcsicmp(fullPath.c_str(), extractTargetNorm->c_str()) == 0) {
                if (isRefPack) {
                    return DecompressEaRefPack(payload, fSize, *outExtracted);
                }
                if (fSize > 256u * 1024u * 1024u) return false;
                outExtracted->assign(payload, payload + fSize);
                return true;
            }
        } else if (outInfo) {
            ArchiveItemEntry item;
            item.pathInArchive     = fullPath;
            item.name              = ArchivePath::BaseName(fullPath);
            item.uncompressedSize  = isRefPack ? refUncompSz : fSize;
            item.compressedSize    = fSize;
            item.rawDataOffset     = fOff;
            item.compressionMethod = isRefPack ? 1 : 0;
            outInfo->entries.push_back(std::move(item));
        }
    }

    if (extractTargetNorm) return false;
    if (outInfo) {
        outInfo->valid = !outInfo->entries.empty();
        return outInfo->valid;
    }
    return false;
}

inline bool ParseEaBigArchive(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return ParseOrExtractEaBigArchive(data, size, &outInfo, nullptr, nullptr);
}

} // namespace DetailUnixAndLegacyArc

inline bool InspectEaBigArchive(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return DetailUnixAndLegacyArc::ParseEaBigArchive(data, size, outInfo);
}

inline bool ExtractEaBigArchiveEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& want,
    std::vector<uint8_t>& outBytes)
{
    return DetailUnixAndLegacyArc::ParseOrExtractEaBigArchive(data, size, nullptr, &want, &outBytes);
}

} // namespace Explorer2::Formats
