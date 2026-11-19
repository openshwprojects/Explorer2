#pragma once
// BohemiaPbo.h — Bohemia Interactive Packed Bank reader & LZSS extractor (.pbo).

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailArc200 {

// ============================================================================
// #29. Bohemia Interactive Packed Bank (.pbo — Arma / DayZ)
// ============================================================================
inline bool DecompressBohemiaPboLzss(
    const uint8_t* src,
    size_t compSize,
    uint64_t uncompSize,
    std::vector<uint8_t>& outBytes)
{
    outBytes.clear();
    if (uncompSize == 0) return true;
    if (!src || compSize < 4 || uncompSize > 256ULL * 1024 * 1024) return false;

    const size_t streamLen = compSize - 4u;
    const uint32_t expectedChecksum = RdU32LE(src + streamLen);
    outBytes.reserve(static_cast<size_t>(std::min<uint64_t>(uncompSize, static_cast<uint64_t>(compSize) * 16ULL + 4096ULL)));

    size_t pos = 0;
    while (outBytes.size() < uncompSize) {
        if (pos >= streamLen) {
            outBytes.clear();
            return false;
        }
        uint8_t flags = src[pos++];
        for (int bit = 0; bit < 8 && outBytes.size() < uncompSize; ++bit, flags >>= 1) {
            if ((flags & 1u) != 0) {
                if (pos >= streamLen) {
                    outBytes.clear();
                    return false;
                }
                outBytes.push_back(src[pos++]);
            } else {
                if (pos + 2u > streamLen) {
                    outBytes.clear();
                    return false;
                }
                const uint8_t b0 = src[pos++];
                const uint8_t b1 = src[pos++];
                const size_t ptr = static_cast<size_t>(b0) | (static_cast<size_t>(b1 & 0xF0u) << 4);
                if (ptr == 0u) {
                    outBytes.clear();
                    return false;
                }
                const size_t mLen = static_cast<size_t>(b1 & 0x0Fu) + 3u;
                for (size_t k = 0; k < mLen && outBytes.size() < uncompSize; ++k) {
                    const uint8_t ch = (outBytes.size() < ptr) ? 0x20u : outBytes[outBytes.size() - ptr];
                    outBytes.push_back(ch);
                }
            }
        }
    }

    uint32_t checksum = 0;
    for (uint8_t b : outBytes) {
        checksum += static_cast<uint32_t>(b);
    }
    if (checksum != expectedChecksum) {
        outBytes.clear();
        return false;
    }
    return true;
}

inline bool ParseBohemiaPbo(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (size < 22) return false;
    outInfo.formatName = L"Bohemia Interactive PBO (.pbo)";

    struct RawPboRec { std::wstring path; uint32_t method; uint32_t origSz; uint32_t dataSz; };
    std::vector<RawPboRec> recs;
    std::wstring prefixDir;

    size_t pos = 0;
    bool sawHeaderOrEntry = false;
    while (pos < size && recs.size() < 50000) {
        size_t ns = pos;
        while (pos < size && data[pos] != 0) ++pos;
        if (pos + 21 > size) break;
        std::string fname(reinterpret_cast<const char*>(data + ns), pos - ns);
        ++pos; // skip NUL

        uint32_t mime   = RdU32LE(data + pos + 0);
        uint32_t origSz = RdU32LE(data + pos + 4);
        uint32_t ts     = RdU32LE(data + pos + 12);
        uint32_t dataSz = RdU32LE(data + pos + 16);
        (void)ts;
        pos += 20;

        if (fname.empty() && mime == 0x56657273u) { // "sreV" version/prefix properties block
            sawHeaderOrEntry = true;
            while (pos < size) {
                size_t ks = pos;
                while (pos < size && data[pos] != 0) ++pos;
                std::string k(reinterpret_cast<const char*>(data + ks), pos - ks);
                if (pos < size) ++pos;
                if (k.empty()) break;
                size_t vs = pos;
                while (pos < size && data[pos] != 0) ++pos;
                std::string v(reinterpret_cast<const char*>(data + vs), pos - vs);
                if (pos < size) ++pos;
                if (_stricmp(k.c_str(), "prefix") == 0) prefixDir = ArchivePath::Normalize(PathUtils::Utf8ToWide(v));
            }
            continue;
        }
        if (fname.empty() && mime == 0 && dataSz == 0) {
            break; // End of PBO header table! Data stream immediately follows!
        }
        if (fname.empty()) return false;
        sawHeaderOrEntry = true;
        recs.push_back({ ArchivePath::Normalize(PathUtils::Utf8ToWide(fname)), mime, origSz, dataSz });
    }

    if (!sawHeaderOrEntry || recs.empty()) return false;
    uint64_t curDataOff = pos;
    for (const auto& r : recs) {
        if (curDataOff > size || r.dataSz > size - static_cast<size_t>(curDataOff)) return false;
        const bool isComp = (r.method == 0x43707273u || r.method == 0x73727043u);
        ArchiveItemEntry item;
        item.pathInArchive = prefixDir.empty() ? r.path : (prefixDir + L"/" + r.path);
        item.name = ArchivePath::BaseName(item.pathInArchive);
        item.compressedSize = r.dataSz;
        item.uncompressedSize = isComp ? r.origSz : ((r.origSz > 0) ? r.origSz : r.dataSz);
        item.rawDataOffset = curDataOff;
        item.compressionMethod = isComp ? 99 : 0; // "srpC" = compressed, 0 = stored!
        outInfo.entries.push_back(std::move(item));
        curDataOff += r.dataSz;
    }
    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

} // namespace DetailArc200

inline bool InspectBohemiaPbo(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    ArchiveSummaryInfo& outInfo)
{
    return (extLower == L".pbo") && DetailArc200::ParseBohemiaPbo(data, size, outInfo);
}

inline bool ExtractBohemiaPboEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    const std::wstring& want,
    std::vector<uint8_t>& outBytes)
{
    if (extLower == L".pbo") {
        ArchiveSummaryInfo info;
        if (DetailArc200::ParseBohemiaPbo(data, size, info)) {
            for (const auto& e : info.entries) {
                if (!e.isDirectory && _wcsicmp(e.pathInArchive.c_str(), want.c_str()) == 0) {
                    if (e.rawDataOffset > size || e.compressedSize > size - static_cast<size_t>(e.rawDataOffset)) {
                        return false;
                    }
                    const uint8_t* p = data + static_cast<size_t>(e.rawDataOffset);
                    if (e.compressionMethod == 0) {
                        if (e.uncompressedSize > size - static_cast<size_t>(e.rawDataOffset)) return false;
                        outBytes.assign(p, p + static_cast<size_t>(e.uncompressedSize));
                        return true;
                    }
                    return DetailArc200::DecompressBohemiaPboLzss(
                        p, static_cast<size_t>(e.compressedSize), e.uncompressedSize, outBytes);
                }
            }
        }
        return false;
    }
    return false;
}

} // namespace Explorer2::Formats
