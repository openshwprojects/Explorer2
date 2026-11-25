#pragma once
// ArchiveFormatCommon.h — Shared stream codec declarations (TAR / CAB) and core binary/timestamp/chunk
// helpers for archive & filesystem readers.
// Filesystems live in single-format headers under src/formats/archives/* and virtual disk partition
// mounting lives in VirtualDiskCommon.h.

#include <string>
#include <string_view>
#include <vector>
#include "../../Base64.h"
#include "../../ByteIO.h"
#include "../../Crc32.h"
#include "../../JsonDom.h"
#include "../../Lz4Block.h"
#include "../../ArchiveTypes.h"
#include "../../ZipDeflate.h"
#include "../../Ole2Compound.h"
#include "../../PathUtils.h"
#include "../../StreamCodecs.h"
#include "LhaArjHuffmanCommon.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
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

namespace Explorer2::StreamCodecs {

bool ParsePosixTarFromMemory(
    const uint8_t* buf,
    size_t len,
    ArchiveSummaryInfo& outInfo,
    const std::wstring* extractTargetNorm = nullptr,
    std::vector<uint8_t>* outExtracted = nullptr);

bool ExtractCabEntryFromFile(
    const std::wstring& cabFilePath,
    const std::wstring& targetNorm,
    std::vector<uint8_t>& outBytes);

bool ExtractCabEntryFromMemory(
    const uint8_t* cabBytes,
    size_t cabSize,
    const std::wstring& targetNorm,
    std::vector<uint8_t>& outBytes);

std::wstring MakeUniqueCabScratchPath(const wchar_t* ext);

} // namespace Explorer2::StreamCodecs

namespace Explorer2::Formats {

namespace DetailDiskAndGameArc {

using ::Explorer2::ByteIO::RdU16LE;
using ::Explorer2::ByteIO::RdU32LE;
using ::Explorer2::ByteIO::RdU64LE;
using ::Explorer2::ByteIO::RdU16BE;
using ::Explorer2::ByteIO::RdU32BE;
using ::Explorer2::ByteIO::RdU64BE;

inline void WrU32LE(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFFu);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    p[2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
    p[3] = static_cast<uint8_t>((v >> 24) & 0xFFu);
}

inline bool PathEqualsCI(const std::wstring& a, const std::wstring& b) {
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

inline void FileTimeU64ToDos(uint64_t ftTicks, uint16_t& outDate, uint16_t& outTime) {
    outDate = 0;
    outTime = 0;
    if (ftTicks == 0) return;
    FILETIME utcFt = {}, locFt = {};
    utcFt.dwLowDateTime = static_cast<DWORD>(ftTicks & 0xFFFFFFFFu);
    utcFt.dwHighDateTime = static_cast<DWORD>(ftTicks >> 32);
    if (FileTimeToLocalFileTime(&utcFt, &locFt)) {
        FileTimeToDosDateTime(&locFt, &outDate, &outTime);
    }
}

inline void UnixTimeU32ToDos(uint32_t unixSecs, uint16_t& outDate, uint16_t& outTime) {
    outDate = 0;
    outTime = 0;
    if (unixSecs == 0) return;
    uint64_t ticks = (static_cast<uint64_t>(unixSecs) + 11644473600ULL) * 10000000ULL;
    FileTimeU64ToDos(ticks, outDate, outTime);
}

inline void UnixSecondsToDosDateTime(uint64_t unixSecs, uint16_t& outDate, uint16_t& outTime) {
    outDate = 0;
    outTime = 0;
    if (unixSecs == 0) return;
    uint64_t ticks = (unixSecs + 11644473600ULL) * 10000000ULL;
    FileTimeU64ToDos(ticks, outDate, outTime);
}

inline bool Base64DecodeBlock(std::string_view in, std::vector<uint8_t>& out) {
    ::Explorer2::Base64::Decode(in, out);
    return !out.empty();
}

} // namespace DetailDiskAndGameArc

namespace DetailUnixAndLegacyArc {

using ::Explorer2::ByteIO::RdU16LE;
using ::Explorer2::ByteIO::RdU32LE;
using ::Explorer2::ByteIO::RdU64LE;
using ::Explorer2::ByteIO::RdU16BE;
using ::Explorer2::ByteIO::RdU32BE;
using ::Explorer2::ByteIO::RdU64BE;

inline bool DecompressSqfsChunk(
    const uint8_t* src,
    size_t compLen,
    size_t maxOut,
    uint16_t compId,
    std::vector<uint8_t>& out)
{
    if (compLen == 0 || maxOut > 256 * 1024 * 1024) return false;
    if (compId == 1) { // Zlib / Deflate
        return ZipDeflate::InflateZlib(src, compLen, out, maxOut + 64) ||
               ZipDeflate::InflateRaw(src, compLen, out, maxOut + 64);
    }
    if (compId == 2) { // LZMA1
        if (compLen >= 13) {
            uint64_t hdrUncomp = RdU64LE(src + 5);
            bool hasSz = (hdrUncomp != UINT64_MAX);
            if (!hasSz || hdrUncomp <= maxOut) {
                if (StreamCodecs::DecompressLzma1(src[0], RdU32LE(src + 1), src + 13, compLen - 13, out, hdrUncomp, hasSz, maxOut + 64)) {
                    return true;
                }
            }
        }
        if (compLen >= 5) {
            return StreamCodecs::DecompressLzma1(src[0], RdU32LE(src + 1), src + 5, compLen - 5, out, maxOut, false, maxOut + 64);
        }
        return false;
    }
    if (compId == 4) { // XZ
        return StreamCodecs::DecompressXzStream(src, compLen, out, maxOut + 64);
    }
    if (compId == 5) { // LZ4
        if (compLen >= 4 && RdU32LE(src) == 0x184D2204u) {
            return StreamCodecs::DecompressLz4Frame(src, compLen, out, maxOut + 64);
        }
        out.clear();
        return ::Explorer2::Lz4::DecodeBlockAppend(src, compLen, out, 0, maxOut);
    }
    if (compId == 6) { // Zstd
        return StreamCodecs::DecompressZstdFrame(src, compLen, out, maxOut + 64);
    }
    return false;
}

} // namespace DetailUnixAndLegacyArc

} // namespace Explorer2::Formats
