#pragma once
// RedHatRpm.h — Red Hat Package Manager archive reader & CPIO payload extractor (.rpm).

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailArc100 {

// ============================================================================
// 27. Red Hat Package Manager (.rpm — Lead + Signature + Metadata Tags + CPIO)
// ============================================================================
inline bool DecompressRpmPayloadToCpio(
    const uint8_t* payload,
    size_t payloadLen,
    const std::string& compressorHint,
    std::vector<uint8_t>& outCpio,
    const uint8_t*& outRawPtr,
    size_t& outRawLen)
{
    outCpio.clear();
    outRawPtr = nullptr;
    outRawLen = 0;
    if (!payload || payloadLen < 6) return false;

    if (payloadLen >= 6 && (std::memcmp(payload, "070701", 6) == 0 ||
                            std::memcmp(payload, "070702", 6) == 0 ||
                            std::memcmp(payload, "070707", 6) == 0))
    {
        outRawPtr = payload;
        outRawLen = payloadLen;
        return true;
    }

    bool ok = false;
    if (payloadLen >= 18 && payload[0] == 0x1F && payload[1] == 0x8B) {
        ok = ZipDeflate::InflateGzip(payload, payloadLen, outCpio, 128 * 1024 * 1024);
    } else if (payloadLen >= 12 && std::memcmp(payload, "\xFD" "7zXZ\x00", 6) == 0) {
        ok = StreamCodecs::DecompressXzStream(payload, payloadLen, outCpio, 128 * 1024 * 1024);
    } else if (payloadLen >= 4 && RdU32LE(payload) == 0xFD2FB528u) {
        ok = StreamCodecs::DecompressZstdFrame(payload, payloadLen, outCpio, 128 * 1024 * 1024);
    } else if (payloadLen >= 4 && payload[0] == 'B' && payload[1] == 'Z' && payload[2] == 'h') {
        ok = StreamCodecs::DecompressBzip2Stream(payload, payloadLen, outCpio, 128 * 1024 * 1024);
    } else if (payloadLen >= 4 && RdU32LE(payload) == 0x184D2204u) {
        ok = StreamCodecs::DecompressLz4Frame(payload, payloadLen, outCpio, 128 * 1024 * 1024);
    } else if (compressorHint == "lzma" && payloadLen >= 14) {
        uint8_t props = payload[0];
        uint32_t dict = RdU32LE(payload + 1);
        uint64_t unp = RdU64LE(payload + 5);
        bool known = (unp != 0xFFFFFFFFFFFFFFFFULL && unp <= 128 * 1024 * 1024);
        ok = StreamCodecs::DecompressLzma1(props, dict, payload + 13, payloadLen - 13, outCpio, known ? unp : 0, known);
    }
    if (!ok || outCpio.size() < 6) return false;
    outRawPtr = outCpio.data();
    outRawLen = outCpio.size();
    return true;
}

inline bool WalkCpioBuffer(
    const uint8_t* cpio,
    size_t cpioLen,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm,
    std::vector<uint8_t>* outExtracted)
{
    if (!cpio || cpioLen < 76) return false;
    bool isNewc = (cpioLen >= 110 && (std::memcmp(cpio, "070701", 6) == 0 || std::memcmp(cpio, "070702", 6) == 0));
    bool isOdc  = (!isNewc && std::memcmp(cpio, "070707", 6) == 0);
    if (!isNewc && !isOdc) return false;

    size_t pos = 0;
    size_t count = 0;
    if (isNewc) {
        while (pos + 110 <= cpioLen && count < 50000) {
            if (std::memcmp(cpio + pos, "070701", 6) != 0 && std::memcmp(cpio + pos, "070702", 6) != 0) break;
            auto hex8 = [&](size_t off) -> uint32_t {
                char b[9] = {};
                std::memcpy(b, cpio + pos + off, 8);
                return static_cast<uint32_t>(std::strtoul(b, nullptr, 16));
            };
            uint32_t mode     = hex8(14);
            uint32_t fSize    = hex8(54);
            uint32_t nameSize = hex8(94);
            uint32_t check    = hex8(102);
            if (nameSize == 0 || nameSize > 4096 || pos + 110 + nameSize > cpioLen) break;

            std::string fname(reinterpret_cast<const char*>(cpio + pos + 110),
                              strnlen(reinterpret_cast<const char*>(cpio + pos + 110), nameSize));
            size_t dataStart = (pos + 110 + nameSize + 3u) & ~size_t(3u);
            if (fname == "TRAILER!!!") break;
            if (dataStart + fSize > cpioLen) break;

            std::wstring norm = ArchivePath::Normalize(PathUtils::Utf8ToWide(fname));
            bool isDir = ((mode & 0170000u) == 0040000u);
            if (!norm.empty() && norm != L".") {
                if (extractTargetNorm && outExtracted) {
                    if (!isDir && PathEqualsCI(norm, *extractTargetNorm)) {
                        outExtracted->assign(cpio + dataStart, cpio + dataStart + fSize);
                        return true;
                    }
                } else if (outInfo) {
                    ArchiveItemEntry item;
                    item.pathInArchive = norm;
                    item.name = ArchivePath::BaseName(norm);
                    item.isDirectory = isDir;
                    item.uncompressedSize = isDir ? 0 : fSize;
                    item.compressedSize   = item.uncompressedSize;
                    item.crc32 = check;
                    item.compressionMethod = 1;
                    outInfo->entries.push_back(std::move(item));
                }
                ++count;
            }
            pos = (dataStart + fSize + 3u) & ~size_t(3u);
        }
    } else {
        while (pos + 76 <= cpioLen && count < 50000) {
            if (std::memcmp(cpio + pos, "070707", 6) != 0) break;
            auto oct = [&](size_t off, size_t len) -> uint64_t {
                char b[16] = {};
                std::memcpy(b, cpio + pos + off, len);
                return static_cast<uint64_t>(std::strtoull(b, nullptr, 8));
            };
            uint64_t mode     = oct(18, 6);
            uint64_t nameSize = oct(59, 6);
            uint64_t fSize    = oct(65, 11);
            if (nameSize == 0 || nameSize > 4096 || pos + 76 + nameSize > cpioLen) break;
            std::string fname(reinterpret_cast<const char*>(cpio + pos + 76),
                              strnlen(reinterpret_cast<const char*>(cpio + pos + 76), static_cast<size_t>(nameSize)));
            if (fname == "TRAILER!!!") break;
            size_t dataStart = pos + 76 + static_cast<size_t>(nameSize);
            if (dataStart + fSize > cpioLen) break;

            std::wstring norm = ArchivePath::Normalize(PathUtils::Utf8ToWide(fname));
            bool isDir = ((mode & 0170000u) == 0040000u);
            if (!norm.empty() && norm != L".") {
                if (extractTargetNorm && outExtracted) {
                    if (!isDir && PathEqualsCI(norm, *extractTargetNorm)) {
                        outExtracted->assign(cpio + dataStart, cpio + dataStart + static_cast<size_t>(fSize));
                        return true;
                    }
                } else if (outInfo) {
                    ArchiveItemEntry item;
                    item.pathInArchive = norm;
                    item.name = ArchivePath::BaseName(norm);
                    item.isDirectory = isDir;
                    item.uncompressedSize = isDir ? 0 : fSize;
                    item.compressedSize   = item.uncompressedSize;
                    item.compressionMethod = 1;
                    outInfo->entries.push_back(std::move(item));
                }
                ++count;
            }
            pos = dataStart + static_cast<size_t>(fSize);
        }
    }
    return extractTargetNorm ? false : (count > 0);
}

inline bool ParseOrExtractRedHatRpm(
    const uint8_t* data,
    size_t size,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm,
    std::vector<uint8_t>* outExtracted)
{
    if (size < 112 || data[0] != 0xED || data[1] != 0xAB || data[2] != 0xEE || data[3] != 0xDB) return false;

    size_t pos = 96; // Skip 96-byte RPM Lead
    // 1. Skip Signature Header (0x8EADE801), padded to 8-byte boundary
    if (pos + 16 > size || data[pos] != 0x8E || data[pos + 1] != 0xAD || data[pos + 2] != 0xE8 || data[pos + 3] != 0x01) {
        return false;
    }
    uint32_t sigIdxCnt  = RdU32BE(data + pos + 8);
    uint32_t sigDataLen = RdU32BE(data + pos + 12);
    if (sigIdxCnt > 65536 || sigDataLen > size) return false;
    size_t sigBytes = 16 + static_cast<size_t>(sigIdxCnt) * 16 + sigDataLen;
    size_t alignedSig = (sigBytes + 7u) & ~size_t(7u);
    if (pos + alignedSig > size) return false;

    // Some older RPMs only have 1 header if pos + alignedSig doesn't start with 0x8EADE801
    size_t metaPos = pos + alignedSig;
    if (metaPos + 16 > size || data[metaPos] != 0x8E || data[metaPos + 1] != 0xAD ||
        data[metaPos + 2] != 0xE8 || data[metaPos + 3] != 0x01)
    {
        // Check unaligned sigBytes just in case, else treat first header as Metadata Header
        if (pos + sigBytes + 16 <= size && data[pos + sigBytes] == 0x8E &&
            data[pos + sigBytes + 1] == 0xAD && data[pos + sigBytes + 2] == 0xE8 && data[pos + sigBytes + 3] == 0x01)
        {
            metaPos = pos + sigBytes;
        } else {
            metaPos = pos;
        }
    }

    uint32_t hdrIdxCnt  = RdU32BE(data + metaPos + 8);
    uint32_t hdrDataLen = RdU32BE(data + metaPos + 12);
    if (hdrIdxCnt > 131072 || hdrDataLen > size) return false;
    size_t idxBase   = metaPos + 16;
    size_t storeBase = idxBase + static_cast<size_t>(hdrIdxCnt) * 16;
    if (storeBase > size || hdrDataLen > size - storeBase) return false;
    size_t storeEnd  = storeBase + hdrDataLen;
    size_t payloadPos = storeEnd;

    std::vector<std::string> baseNames, dirNames, oldFileNames;
    std::vector<uint32_t> dirIndexes;
    std::vector<uint64_t> fileSizes;
    std::vector<uint64_t> fallbackSizes1009;
    std::vector<uint16_t> fileModes;
    std::string compressor;

    auto readStringArray = [&](uint32_t off, uint32_t cnt, std::vector<std::string>& outArr) {
        if (off >= hdrDataLen || cnt > 100000) return;
        size_t p = storeBase + off;
        outArr.reserve(std::min<uint32_t>(cnt, 4096));
        for (uint32_t k = 0; k < cnt && p < storeEnd; ++k) {
            size_t e = p;
            while (e < storeEnd && data[e] != 0) ++e;
            outArr.emplace_back(reinterpret_cast<const char*>(data + p), e - p);
            p = (e < storeEnd) ? (e + 1) : storeEnd;
        }
    };

    for (uint32_t i = 0; i < hdrIdxCnt; ++i) {
        const uint8_t* ie = data + idxBase + static_cast<size_t>(i) * 16;
        uint32_t tag   = RdU32BE(ie + 0);
        uint32_t type  = RdU32BE(ie + 4);
        uint32_t off   = RdU32BE(ie + 8);
        uint32_t count = RdU32BE(ie + 12);
        if (off >= hdrDataLen) continue;

        if (tag == 1117 && type == 8) { // RPMTAG_BASENAMES
            readStringArray(off, count, baseNames);
        } else if (tag == 1118 && type == 8) { // RPMTAG_DIRNAMES
            readStringArray(off, count, dirNames);
        } else if (tag == 1027 && type == 8) { // RPMTAG_OLDFILENAMES
            readStringArray(off, count, oldFileNames);
        } else if (tag == 1116 && type == 4 && count <= 100000) { // RPMTAG_DIRINDEXES
            if (off + static_cast<uint64_t>(count) * 4 <= hdrDataLen) {
                dirIndexes.resize(count);
                for (uint32_t k = 0; k < count; ++k) dirIndexes[k] = RdU32BE(data + storeBase + off + k * 4);
            }
        } else if (tag == 1028 && type == 4 && fileSizes.empty() && count <= 100000) { // RPMTAG_FILESIZES (INT32)
            if (off + static_cast<uint64_t>(count) * 4 <= hdrDataLen) {
                fileSizes.resize(count);
                for (uint32_t k = 0; k < count; ++k) fileSizes[k] = RdU32BE(data + storeBase + off + k * 4);
            }
        } else if (tag == 1009 && type == 4 && fallbackSizes1009.empty() && count <= 100000) { // RPMTAG_SIZE / synthetic fallback
            if (off + static_cast<uint64_t>(count) * 4 <= hdrDataLen) {
                fallbackSizes1009.resize(count);
                for (uint32_t k = 0; k < count; ++k) fallbackSizes1009[k] = RdU32BE(data + storeBase + off + k * 4);
            }
        } else if (tag == 5008 && type == 5 && count <= 100000) { // RPMTAG_LONGFILESIZES (INT64)
            if (off + static_cast<uint64_t>(count) * 8 <= hdrDataLen) {
                fileSizes.resize(count);
                for (uint32_t k = 0; k < count; ++k) fileSizes[k] = RdU64BE(data + storeBase + off + k * 8);
            }
        } else if (tag == 1030 && type == 3 && count <= 100000) { // RPMTAG_FILEMODES (INT16)
            if (off + static_cast<uint64_t>(count) * 2 <= hdrDataLen) {
                fileModes.resize(count);
                for (uint32_t k = 0; k < count; ++k) fileModes[k] = RdU16BE(data + storeBase + off + k * 2);
            }
        } else if (tag == 1125 && type == 6) { // RPMTAG_PAYLOADCOMPRESSOR
            size_t p = storeBase + off, e = p;
            while (e < storeEnd && data[e] != 0) ++e;
            compressor.assign(reinterpret_cast<const char*>(data + p), e - p);
        }
    }

    // Extraction mode: decompress trailing CPIO payload and extract matching path
    if (extractTargetNorm && outExtracted) {
        if (payloadPos >= size) return false;
        std::vector<uint8_t> cpioStorage;
        const uint8_t* cpioPtr = nullptr;
        size_t cpioLen = 0;
        if (!DecompressRpmPayloadToCpio(data + payloadPos, size - payloadPos, compressor, cpioStorage, cpioPtr, cpioLen)) {
            return false;
        }
        return WalkCpioBuffer(cpioPtr, cpioLen, nullptr, extractTargetNorm, outExtracted);
    }

    if (!outInfo) return false;
    outInfo->formatName = L"Red Hat Package (RPM)";
    if (!compressor.empty()) {
        outInfo->formatName += L" [" + PathUtils::Utf8ToWide(compressor) + L"]";
    }

    size_t nFiles = !baseNames.empty() ? baseNames.size() : oldFileNames.size();
    if (fileSizes.empty() && fallbackSizes1009.size() == nFiles) {
        fileSizes = std::move(fallbackSizes1009);
    }

    uint64_t payloadBytes = (size > payloadPos) ? (size - payloadPos) : 0;
    uint64_t sumUnp = 0;
    for (uint64_t fs : fileSizes) sumUnp += fs;
    for (size_t i = 0; i < nFiles; ++i) {
        std::string fullUtf8;
        if (!baseNames.empty()) {
            uint32_t dIdx = (i < dirIndexes.size()) ? dirIndexes[i] : 0;
            if (dIdx < dirNames.size()) fullUtf8 = dirNames[dIdx] + baseNames[i];
            else fullUtf8 = baseNames[i];
        } else {
            fullUtf8 = oldFileNames[i];
        }

        std::wstring norm = ArchivePath::Normalize(PathUtils::Utf8ToWide(fullUtf8));
        if (norm.empty()) continue;

        bool isDir = (i < fileModes.size()) && ((fileModes[i] & 0170000u) == 0040000u);
        uint64_t fsz = isDir ? 0 : ((i < fileSizes.size()) ? fileSizes[i] : 0);

        ArchiveItemEntry item;
        item.pathInArchive = norm;
        item.name = ArchivePath::BaseName(norm);
        item.isDirectory = isDir;
        item.uncompressedSize = fsz;
        item.compressedSize = (!isDir && sumUnp > 0) ? ((payloadBytes * fsz) / sumUnp) : 0;
        item.compressionMethod = 1;
        outInfo->entries.push_back(std::move(item));
    }

    // Fallback: if Metadata Header omitted file tags, inspect the trailing CPIO stream directly!
    if (outInfo->entries.empty() && payloadPos < size) {
        std::vector<uint8_t> cpioStorage;
        const uint8_t* cpioPtr = nullptr;
        size_t cpioLen = 0;
        if (DecompressRpmPayloadToCpio(data + payloadPos, size - payloadPos, compressor, cpioStorage, cpioPtr, cpioLen)) {
            WalkCpioBuffer(cpioPtr, cpioLen, outInfo, nullptr, nullptr);
        }
    }

    outInfo->valid = !outInfo->entries.empty();
    return outInfo->valid;
}

inline bool ParseRedHatRpm(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return ParseOrExtractRedHatRpm(data, size, &outInfo, nullptr, nullptr);
}

} // namespace DetailArc100

inline bool InspectRedHatRpm(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return DetailArc100::ParseRedHatRpm(data, size, outInfo);
}

inline bool ExtractRedHatRpmEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    return DetailArc100::ParseOrExtractRedHatRpm(data, size, nullptr, &normTarget, &outBytes);
}

} // namespace Explorer2::Formats
