#pragma once
#include "../../ReaderTextUtils.h"
// BrotliArchive.h — Zero-dependency C++20 RFC 7932 Brotli Stream & Tarball (.br, .tbr, .tar.br) archive reader.

#include "../../ByteIO.h"
#include "../../ArchiveTypes.h"
#include "../../ZipDeflate.h"
#include "../../Crc32.h"
#include "../../StreamCodecs.h"
#include "../../PathUtils.h"
#include "../common/ArchiveFormatCommon.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>


namespace Explorer2::Formats {

namespace DetailBrotliArc {

using ::Explorer2::ByteIO::RdU16LE;
using ::Explorer2::ByteIO::RdU32LE;
using ::Explorer2::ByteIO::RdU64LE;
using ::Explorer2::ByteIO::RdU16BE;
using ::Explorer2::ByteIO::RdU32BE;
using ::Explorer2::ByteIO::RdU64BE;

inline bool EndsWithCI(const std::wstring& s, const wchar_t* suffix) {
    return ::Explorer2::ReaderText::EndsWithWideCI(s, suffix);
}
inline std::string AsciiLower(std::string_view sv) { return ::Explorer2::ReaderText::ToLowerAscii(sv); }
inline std::string_view TrimAsciiSV(std::string_view sv) { return ::Explorer2::ReaderText::TrimCrTabSpace(sv); }
using ::Explorer2::ReaderText::CalendarToDosDateTime;

struct BrotliDecompCache {
    std::wstring normKey;
    size_t fileSize = 0;
    uint32_t headCrc = 0;
    std::vector<uint8_t> decompressed;
};

inline bool DecompressBrotliCached(
    const std::wstring& archivePath,
    const uint8_t* data,
    size_t size,
    std::vector<uint8_t>& outDec)
{
    outDec.clear();
    if (!data || size == 0) return false;

    static std::mutex s_brMutex;
    static BrotliDecompCache s_brCache;

    const std::wstring normKey = PathUtils::NormalizePathKey(archivePath);
    const uint32_t headCrc = ::Explorer2::Checksum::Crc32(data, std::min<size_t>(size, 4096u));
    {
        std::lock_guard<std::mutex> lock(s_brMutex);
        if (!normKey.empty() &&
            s_brCache.normKey == normKey &&
            s_brCache.fileSize == size &&
            s_brCache.headCrc == headCrc &&
            !s_brCache.decompressed.empty())
        {
            outDec = s_brCache.decompressed;
            return true;
        }
    }

    constexpr size_t kMaxBrotliOut = 128u * 1024u * 1024u;
    if (!StreamCodecs::DecompressBrotliStream(data, size, outDec, kMaxBrotliOut, /*requireExactEnd=*/true)) {
        outDec.clear();
        return false;
    }

    if (!normKey.empty() && outDec.size() <= 64u * 1024u * 1024u) {
        std::lock_guard<std::mutex> lock(s_brMutex);
        s_brCache.normKey = normKey;
        s_brCache.fileSize = size;
        s_brCache.headCrc = headCrc;
        s_brCache.decompressed = outDec;
    }
    return true;
}

inline std::wstring DeriveBrotliSingleName(const std::wstring& archivePath) {
    std::wstring fn = PathUtils::GetFileName(archivePath);
    if (EndsWithCI(fn, L".tbr") && fn.size() > 4) {
        return fn.substr(0, fn.size() - 4) + L".tar";
    }
    if (EndsWithCI(fn, L".br") && fn.size() > 3) {
        return fn.substr(0, fn.size() - 3);
    }
    return fn.empty() ? L"stream.bin" : (fn + L".extracted");
}

} // namespace DetailBrotliArc

inline bool ParseOrExtractBrotli(
    const std::wstring& archivePath,
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm,
    std::vector<uint8_t>* outExtracted)
{
    if (extLower != L".br" && extLower != L".tbr") return false;

    std::vector<uint8_t> dec;
    if (!DetailBrotliArc::DecompressBrotliCached(archivePath, data, size, dec)) return false;

    ArchiveSummaryInfo tarInfo;
    const bool isTar = (dec.size() >= 512u &&
                        StreamCodecs::ParsePosixTarFromMemory(dec.data(), dec.size(), tarInfo) &&
                        !tarInfo.entries.empty());

    if (extractTargetNorm && outExtracted) {
        std::wstring want = ArchivePath::Normalize(*extractTargetNorm);
        if (isTar) {
            ArchiveSummaryInfo dummy;
            return StreamCodecs::ParsePosixTarFromMemory(dec.data(), dec.size(), dummy, &want, outExtracted);
        }
        std::wstring singleName = ArchivePath::BaseName(ArchivePath::Normalize(DetailBrotliArc::DeriveBrotliSingleName(archivePath)));
        if (_wcsicmp(want.c_str(), singleName.c_str()) == 0) {
            *outExtracted = std::move(dec);
            return true;
        }
        return false;
    }

    if (!outInfo) return false;
    if (isTar) {
        *outInfo = std::move(tarInfo);
        outInfo->formatName = L"TAR.BR";
        uint64_t totalUnp = 0;
        for (const auto& e : outInfo->entries) {
            if (!e.isDirectory) totalUnp += e.uncompressedSize;
        }
        for (auto& e : outInfo->entries) {
            if (!e.isDirectory) {
                e.compressedSize = (totalUnp > 0)
                    ? ((static_cast<uint64_t>(size) * e.uncompressedSize) / totalUnp)
                    : size;
                e.compressionMethod = 8;
            }
        }
        outInfo->valid = true;
        return true;
    }

    outInfo->formatName = L"Brotli Stream (.br)";
    ArchiveItemEntry item;
    item.pathInArchive = ArchivePath::BaseName(ArchivePath::Normalize(DetailBrotliArc::DeriveBrotliSingleName(archivePath)));
    if (item.pathInArchive.empty()) item.pathInArchive = L"decompressed.bin";
    item.name = item.pathInArchive;
    item.uncompressedSize = dec.size();
    item.compressedSize = size;
    item.isDirectory = false;
    item.compressionMethod = 8;
    outInfo->entries.push_back(std::move(item));
    outInfo->valid = true;
    return true;
}

} // namespace Explorer2::Formats
