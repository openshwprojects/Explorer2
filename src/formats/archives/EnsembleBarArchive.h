#pragma once
// EnsembleBarArchive.h — Ensemble Studios Age of Mythology / Age of Empires III "ESPN" BAR Archive (.bar)
// Parses `0x124`-byte `ESPN` header (`0x44332211` endianness tag + `footerOffset`), UTF-16LE directory records,
// and both uncompressed and compressed (`l33t` zlib/deflate, `alz4`/LZ4, Zstd) BAR entries.

#include "../common/GamePakArchiveCommon.h"
#include "../../Lz4Block.h"
#include "../../StreamCodecs.h"

namespace Explorer2::Formats {

inline bool InspectEnsembleBarArchive(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (!data || size < 0x140u) return false;
    if (std::memcmp(data, "ESPN", 4) != 0) return false;

    const uint32_t version     = ByteIO::RdU32LE(data + 0x04u);
    const uint32_t endianCheck = ByteIO::RdU32LE(data + 0x08u);
    if (version < 1u || version > 6u || endianCheck != 0x44332211u) return false;

    const uint32_t totalFiles  = ByteIO::RdU32LE(data + 0x114u);
    const uint32_t footerSize  = ByteIO::RdU32LE(data + 0x11Cu);
    const uint32_t footerOff   = ByteIO::RdU32LE(data + 0x120u);

    if (totalFiles == 0u || totalFiles > 65536u) return false;
    if (footerOff < 0x124u || footerOff >= size || footerSize < 12u || footerSize > size - footerOff) {
        return false;
    }

    const uint8_t* ftr = data + footerOff;
    const size_t   ftrAvail = size - footerOff;
    const uint32_t rootNameChars = ByteIO::RdU32LE(ftr + 0u);
    if (rootNameChars > 260u || 8ULL + static_cast<uint64_t>(rootNameChars) * 2ULL > ftrAvail) return false;

    std::string rootPrefix;
    if (rootNameChars > 0u) {
        rootPrefix = PathUtils::WideToUtf8(ReaderText::DecodeUtf16Span(ftr + 4u, rootNameChars, false));
        while (!rootPrefix.empty() && (rootPrefix.back() == '\0' || rootPrefix.back() == '\\' || rootPrefix.back() == '/')) {
            rootPrefix.pop_back();
        }
    }

    const size_t cntPos = 4u + static_cast<size_t>(rootNameChars) * 2u;
    if (ftrAvail - cntPos < 4u) return false;
    const uint32_t dirCount = ByteIO::RdU32LE(ftr + cntPos);
    if (dirCount == 0u || dirCount != totalFiles) return false;

    const size_t ptrTablePos = cntPos + 4u;
    if (static_cast<uint64_t>(dirCount) * 4ULL > ftrAvail - ptrTablePos) return false;

    outInfo = {};
    wchar_t fmtBuf[96] = {};
    swprintf_s(fmtBuf, L"Ensemble Studios ESPN BAR Archive (.bar v%u)", version);
    outInfo.formatName = fmtBuf;
    outInfo.entries.reserve(dirCount);

    for (uint32_t i = 0u; i < dirCount; ++i) {
        const uint32_t recRel = ByteIO::RdU32LE(ftr + ptrTablePos + static_cast<size_t>(i) * 4u);
        if (recRel >= ftrAvail || ftrAvail - recRel < 24u) {
            outInfo = {};
            return false;
        }
        const uint8_t* rec = ftr + recRel;
        const uint32_t fOff      = ByteIO::RdU32LE(rec + 0u);
        uint32_t       uncompSz  = ByteIO::RdU32LE(rec + 4u);
        const uint32_t compSz    = ByteIO::RdU32LE(rec + 8u);
        const uint32_t nameChars = ByteIO::RdU32LE(rec + 20u);

        if (nameChars == 0u || nameChars > 240u) {
            outInfo = {};
            return false;
        }
        if (24ULL + static_cast<uint64_t>(nameChars) * 2ULL > ftrAvail - recRel) {
            outInfo = {};
            return false;
        }

        const uint32_t storedSz = (compSz > 0u) ? compSz : uncompSz;
        if (storedSz > 0u && (fOff < 0x124u || fOff > size || storedSz > size - fOff)) {
            outInfo = {};
            return false;
        }

        std::string itemName = PathUtils::WideToUtf8(ReaderText::DecodeUtf16Span(rec + 24u, nameChars, false));
        while (!itemName.empty() && itemName.back() == '\0') itemName.pop_back();
        if (itemName.empty()) { outInfo = {}; return false; }

        std::string fullRel = rootPrefix.empty() ? itemName : (rootPrefix + "/" + itemName);
        std::string cleanRel;
        if (!GamePakCommon::SanitizeRelativeArchivePath(fullRel, cleanRel)) { outInfo = {}; return false; }
        std::wstring normRel = ArchivePath::Normalize(PathUtils::Utf8ToWide(cleanRel));
        if (normRel.empty()) { outInfo = {}; return false; }

        bool hasWrapperMagic = false;
        if (storedSz >= 8u && (std::memcmp(data + fOff, "l33t", 4) == 0 || std::memcmp(data + fOff, "alz4", 4) == 0)) {
            hasWrapperMagic = true;
            const uint32_t innerUncomp = ByteIO::RdU32LE(data + fOff + 4u);
            if (innerUncomp > 0u && uncompSz == storedSz) {
                uncompSz = innerUncomp;
            }
        }
        const bool isComp = (compSz > 0u && compSz != uncompSz) || hasWrapperMagic;

        ArchiveItemEntry e;
        e.pathInArchive     = std::move(normRel);
        e.name              = ArchivePath::BaseName(e.pathInArchive);
        e.uncompressedSize  = uncompSz;
        e.compressedSize    = storedSz;
        e.rawDataOffset     = (storedSz > 0u) ? fOff : 0u;
        e.compressionMethod = isComp ? 8 : 0;
        e.isDirectory       = false;
        outInfo.entries.push_back(std::move(e));
    }

    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

inline bool ExtractEnsembleBarEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    ArchiveSummaryInfo info;
    if (!InspectEnsembleBarArchive(data, size, info)) return false;
    const std::wstring want = ArchivePath::Normalize(normTarget);

    for (const auto& e : info.entries) {
        if (e.isDirectory || _wcsicmp(e.pathInArchive.c_str(), want.c_str()) != 0) continue;
        if (e.compressedSize == 0 && e.uncompressedSize == 0) {
            outBytes.clear();
            return true;
        }
        if (e.rawDataOffset > size || e.compressedSize > size - e.rawDataOffset ||
            e.uncompressedSize > 256ULL * 1024 * 1024) {
            return false;
        }

        const uint8_t* src = data + static_cast<size_t>(e.rawDataOffset);
        const size_t cLen  = static_cast<size_t>(e.compressedSize);
        const size_t uLen  = static_cast<size_t>(e.uncompressedSize);

        // 1. Ensemble "l33t" wrapper: "l33t" + uint32 uncompLenLE + zlib/deflate stream
        if (cLen >= 8u && std::memcmp(src, "l33t", 4) == 0) {
            const uint32_t hdrUncomp = ByteIO::RdU32LE(src + 4u);
            const size_t targetSz = (hdrUncomp > 0u && hdrUncomp <= 256u * 1024u * 1024u) ? hdrUncomp : uLen;
            if (ZipDeflate::InflateZlib(src + 8u, cLen - 8u, outBytes, targetSz + 64u) ||
                ZipDeflate::InflateRaw(src + 8u, cLen - 8u, outBytes, targetSz + 64u))
            {
                return true;
            }
            return false;
        }

        // 2. AoE3:DE "alz4" wrapper: "alz4" + uint32 uncompLenLE (+ optional uint32 compLenLE) + LZ4 block/frame
        if (cLen >= 8u && std::memcmp(src, "alz4", 4) == 0) {
            const uint32_t hdrUncomp = ByteIO::RdU32LE(src + 4u);
            const size_t targetSz = (hdrUncomp > 0u && hdrUncomp <= 256u * 1024u * 1024u) ? hdrUncomp : uLen;
            if (cLen >= 12u) {
                const uint32_t innerComp = ByteIO::RdU32LE(src + 8u);
                if (innerComp > 0u && innerComp <= cLen - 12u) {
                    if (Lz4::DecodeBlockExact(src + 12u, innerComp, targetSz, outBytes) ||
                        StreamCodecs::DecompressLz4Frame(src + 12u, innerComp, outBytes, targetSz + 64u))
                    {
                        return true;
                    }
                }
            }
            if (Lz4::DecodeBlockExact(src + 8u, cLen - 8u, targetSz, outBytes) ||
                StreamCodecs::DecompressLz4Frame(src + 8u, cLen - 8u, outBytes, targetSz + 64u))
            {
                return true;
            }
            return false;
        }

        // 3. Stored uncompressed entry
        if (e.compressionMethod == 0 && cLen == uLen) {
            outBytes.assign(src, src + uLen);
            return true;
        }

        // 4. Direct zlib / raw deflate / LZ4 frame / Zstd / LZ4 raw block
        if ((ZipDeflate::InflateZlib(src, cLen, outBytes, uLen + 64u) && outBytes.size() == uLen) ||
            (ZipDeflate::InflateRaw(src, cLen, outBytes, uLen + 64u) && outBytes.size() == uLen) ||
            (StreamCodecs::DecompressLz4Frame(src, cLen, outBytes, uLen + 64u) && outBytes.size() == uLen) ||
            (StreamCodecs::DecompressZstdFrame(src, cLen, outBytes, uLen + 64u) && outBytes.size() == uLen) ||
            Lz4::DecodeBlockExact(src, cLen, uLen, outBytes))
        {
            return true;
        }

        return false;
    }
    return false;
}

} // namespace Explorer2::Formats
