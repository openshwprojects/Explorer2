#pragma once
// AlzipAlzArchive.h — ESTsoft ALZip ("ALZ\x01", "BLZ\x01") archive reader & Stored/BZip2/Deflate extractor (.alz).

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailAlzipAlz {

inline uint64_t ReadAlzSizedIntLE(const uint8_t* p, size_t byteWidth) noexcept {
    switch (byteWidth) {
    case 1: return p[0];
    case 2: return ByteIO::RdU16LE(p);
    case 4: return ByteIO::RdU32LE(p);
    case 8: return ByteIO::RdU64LE(p);
    default: return 0ULL;
    }
}

inline std::wstring DecodeAlzFileName(const uint8_t* ptr, size_t len) {
    if (!ptr || len == 0) return {};
    std::string raw(reinterpret_cast<const char*>(ptr), len);
    // Prefer UTF-8 when valid UTF-8, otherwise CP949/ACP on Windows or Latin-1 fallback
    std::wstring w = PathUtils::Utf8ToWide(raw);
    if (!w.empty()) return w;
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_ACP, 0, raw.data(), static_cast<int>(raw.size()), nullptr, 0);
    if (wlen > 0) {
        std::wstring acp(static_cast<size_t>(wlen), L'\0');
        MultiByteToWideChar(CP_ACP, 0, raw.data(), static_cast<int>(raw.size()), acp.data(), wlen);
        return acp;
    }
#endif
    std::wstring lat;
    lat.reserve(raw.size());
    for (unsigned char c : raw) lat.push_back(static_cast<wchar_t>(c));
    return lat;
}

struct AlzEntryRecord {
    std::wstring pathInArchive;
    uint64_t uncompSize = 0;
    uint64_t compSize = 0;
    uint64_t dataOffset = 0;
    uint32_t crc32 = 0;
    bool hasCrc = false;
    uint16_t dosDate = 0;
    uint16_t dosTime = 0;
    uint8_t method = 0; // 0 = Store, 1 = BZip2, 2 = Deflate
    bool isDirectory = false;
    bool isEncrypted = false;
};

inline bool DecompressAlzBzip2(const uint8_t* src, size_t cLen, size_t uLen, std::vector<uint8_t>& outBytes) {
    if (StreamCodecs::DecompressBzip2Stream(src, cLen, outBytes, uLen + 64u) && outBytes.size() == uLen) {
        return true;
    }
    // Older ESTsoft ALZip modified BZip2 streams replace "BZh9" + 6-byte block magic (0x314159265359) with "DLZ\x01"
    // or omit "BZh9"; if header begins with "DLZ", reconstruct standard BZh9 header if applicable.
    if (cLen >= 4u && std::memcmp(src, "BZh", 3) != 0 && src[0] == 0x31u && src[1] == 0x41u && src[2] == 0x59u) {
        std::vector<uint8_t> wrapped;
        wrapped.reserve(4u + cLen);
        wrapped.insert(wrapped.end(), { 'B', 'Z', 'h', '9' });
        wrapped.insert(wrapped.end(), src, src + cLen);
        if (StreamCodecs::DecompressBzip2Stream(wrapped.data(), wrapped.size(), outBytes, uLen + 64u) &&
            outBytes.size() == uLen)
        {
            return true;
        }
    }
    return false;
}

inline bool EnumerateAlzEntries(const uint8_t* data, size_t size, std::vector<AlzEntryRecord>& outEntries) {
    outEntries.clear();
    if (!data || size < 16u || std::memcmp(data, "ALZ\x01", 4) != 0) return false;

    size_t pos = 8u; // 8-byte ALZ archive header: "ALZ\x01" + u16 version + u16 id
    while (size - pos >= 4u && outEntries.size() < 65536u) {
        const uint32_t sig = ByteIO::RdU32LE(data + pos);
        if (sig == 0x015A4C43u || sig == 0x025A4C43u || sig == 0x035A4C43u || sig == 0x015A4C45u) {
            // "CLZ\x01..03" Central Directory or "ELZ\x01" End of Archive
            break;
        }
        if (sig == 0x015A4C44u) {
            // Archive comment chunk ("DLZ\x01") if present before end
            break;
        }
        if (sig != 0x015A4C42u) { // Must be "BLZ\x01" Local File Header
            return false;
        }

        if (size - pos < 13u) return false;
        const uint16_t nameLen    = ByteIO::RdU16LE(data + pos + 4u);
        const uint8_t  fileAttr   = data[pos + 6u];
        const uint32_t timeDate   = ByteIO::RdU32LE(data + pos + 7u);
        const uint8_t  descriptor = data[pos + 11u];
        // data[pos + 12u] = 1-byte reserved/unknown field in _SLocalFileHeaderHead
        size_t cur = pos + 13u;

        const uint8_t sizeMask = static_cast<uint8_t>(descriptor & 0xF0u);
        size_t sizeFieldWidth = 0;
        if (sizeMask == 0x00u) sizeFieldWidth = 0u;
        else if (sizeMask == 0x10u) sizeFieldWidth = 1u;
        else if (sizeMask == 0x20u) sizeFieldWidth = 2u;
        else if (sizeMask == 0x40u) sizeFieldWidth = 4u;
        else if (sizeMask == 0x80u) sizeFieldWidth = 8u;
        else return false;

        uint8_t compMethod = 0u;
        uint32_t fileCrc   = 0u;
        uint64_t compSz    = 0ULL;
        uint64_t uncompSz  = 0ULL;
        bool hasCrc = false;

        if (sizeFieldWidth > 0u) {
            const size_t varNeed = 6u + sizeFieldWidth * 2u;
            if (size - cur < varNeed) return false;
            compMethod = data[cur + 0u];
            // data[cur + 1u] = 1-byte unknown/reserved
            fileCrc    = ByteIO::RdU32LE(data + cur + 2u);
            compSz     = ReadAlzSizedIntLE(data + cur + 6u, sizeFieldWidth);
            uncompSz   = ReadAlzSizedIntLE(data + cur + 6u + sizeFieldWidth, sizeFieldWidth);
            hasCrc     = true;
            cur += varNeed;
        }

        if (nameLen == 0u || nameLen > size - cur) return false;
        const std::wstring rawName = DecodeAlzFileName(data + cur, nameLen);
        cur += nameLen;

        const bool isEncrypted = (descriptor & 0x01u) != 0u;
        if (isEncrypted) {
            if (size - cur < 12u) return false;
            cur += 12u; // 12-byte ALZip encryption check block
        }

        if (compSz > size - cur) return false;
        const uint64_t payloadOff = cur;
        pos = cur + static_cast<size_t>(compSz);

        const bool trailingSlash = !rawName.empty() && (rawName.back() == L'/' || rawName.back() == L'\\');
        const bool isDir = trailingSlash || ((fileAttr & 0x10u) != 0u);
        const std::wstring normPath = ArchivePath::Normalize(rawName);
        if (normPath.empty()) continue;

        AlzEntryRecord rec;
        rec.pathInArchive = normPath;
        rec.uncompSize    = uncompSz;
        rec.compSize      = compSz;
        rec.dataOffset    = payloadOff;
        rec.crc32         = fileCrc;
        rec.hasCrc        = hasCrc && !isDir;
        rec.dosTime       = static_cast<uint16_t>(timeDate & 0xFFFFu);
        rec.dosDate       = static_cast<uint16_t>((timeDate >> 16u) & 0xFFFFu);
        rec.method        = compMethod;
        rec.isDirectory   = isDir;
        rec.isEncrypted   = isEncrypted;
        outEntries.push_back(std::move(rec));
    }

    return !outEntries.empty();
}

} // namespace DetailAlzipAlz

inline bool InspectAlzipAlzArchive(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    std::vector<DetailAlzipAlz::AlzEntryRecord> entries;
    if (!DetailAlzipAlz::EnumerateAlzEntries(data, size, entries)) return false;

    outInfo.formatName = L"ESTsoft ALZip Archive (.alz)";
    outInfo.entries.reserve(entries.size());
    for (const auto& r : entries) {
        ArchiveItemEntry item;
        item.pathInArchive     = r.pathInArchive;
        item.name              = ArchivePath::BaseName(r.pathInArchive);
        item.uncompressedSize  = r.uncompSize;
        item.compressedSize    = r.compSize;
        item.crc32             = r.crc32;
        item.dosDate           = r.dosDate;
        item.dosTime           = r.dosTime;
        item.isDirectory       = r.isDirectory;
        item.rawDataOffset     = 0ULL; // Always route through ExtractAlzipAlzEntry to enforce CRC-32 verification
        item.compressionMethod = r.isEncrypted ? 99u : (r.method == 1u ? 12u : (r.method == 2u ? 8u : r.method));
        outInfo.entries.push_back(std::move(item));
    }
    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

inline bool ExtractAlzipAlzEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    std::vector<DetailAlzipAlz::AlzEntryRecord> entries;
    if (!DetailAlzipAlz::EnumerateAlzEntries(data, size, entries)) return false;

    const std::wstring want = ArchivePath::Normalize(normTarget);
    for (const auto& r : entries) {
        if (r.isDirectory || _wcsicmp(r.pathInArchive.c_str(), want.c_str()) != 0) continue;
        if (r.isEncrypted || r.uncompSize > 256ULL * 1024 * 1024) return false;
        if (r.uncompSize == 0ULL && r.compSize == 0ULL) {
            outBytes.clear();
            return true;
        }
        if (r.dataOffset > size || r.compSize > size - r.dataOffset) return false;

        const uint8_t* src = data + static_cast<size_t>(r.dataOffset);
        const size_t cLen  = static_cast<size_t>(r.compSize);
        const size_t uLen  = static_cast<size_t>(r.uncompSize);
        bool ok = false;

        if (r.method == 0u) { // Stored
            if (cLen < uLen) return false;
            outBytes.assign(src, src + uLen);
            ok = true;
        } else if (r.method == 1u) { // BZip2
            ok = DetailAlzipAlz::DecompressAlzBzip2(src, cLen, uLen, outBytes);
        } else if (r.method == 2u) { // Deflate
            ok = (ZipDeflate::InflateRaw(src, cLen, outBytes, uLen + 64u) && outBytes.size() == uLen) ||
                 (ZipDeflate::InflateZlib(src, cLen, outBytes, uLen + 64u) && outBytes.size() == uLen);
        } else {
            return false;
        }

        if (!ok || outBytes.size() != uLen) {
            outBytes.clear();
            return false;
        }
        if (r.hasCrc && Checksum::Crc32(outBytes.data(), outBytes.size()) != r.crc32) {
            outBytes.clear();
            return false;
        }
        return true;
    }
    return false;
}

} // namespace Explorer2::Formats
