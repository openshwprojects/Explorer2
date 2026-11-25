#pragma once
// WindowsWim.h — Windows Imaging Format (.wim, .esd — "MSWIM\0\0\0", XPRESS Huffman & LZX).
// Uses shared Explorer2::Lzx::Decoder (src/LzxDecoder.h) for LZX chunks (with E8 translation undo & stored-block alignment).

#include "../common/ArchiveFormatCommon.h"
#include "../../LzxDecoder.h"

namespace Explorer2::Formats {

namespace DetailDiskAndGameArc {

struct WimResEntry {
    uint64_t wimOffset = 0;
    uint64_t compSize = 0;
    uint64_t origSize = 0;
    uint8_t  flags = 0; // 0x02 = Metadata, 0x04 = Compressed
    uint8_t  sha1[20] = {};
};

// [MS-XCA] 2.2.4 XPRESS Huffman Block Decompressor (256B nibble table + 16-bit LE MSB bitstream)
inline bool DecompressXpressHuffmanChunk(
    const uint8_t* src,
    size_t srcLen,
    uint8_t* dst,
    size_t dstLen)
{
    if (dstLen == 0) return true;
    if (srcLen < 260) return false;

    uint8_t lens[512] = {};
    uint32_t blCount[16] = {};
    uint32_t nonZeroSyms = 0;
    for (size_t i = 0; i < 256; ++i) {
        uint8_t l0 = src[i] & 0x0Fu;
        uint8_t l1 = (src[i] >> 4) & 0x0Fu;
        lens[2 * i]     = l0;
        lens[2 * i + 1] = l1;
        if (l0 > 0) { ++blCount[l0]; ++nonZeroSyms; }
        if (l1 > 0) { ++blCount[l1]; ++nonZeroSyms; }
    }
    if (nonZeroSyms == 0) return false;

    uint32_t nextCode[16] = {};
    uint32_t code = 0;
    for (int bits = 1; bits <= 15; ++bits) {
        code = (code + blCount[bits - 1]) << 1;
        nextCode[bits] = code;
    }
    if (code + blCount[15] > 32768u) return false;
    if (nonZeroSyms > 1 && (code + blCount[15]) != 32768u) return false;

    std::vector<uint16_t> decTbl(32768u, 0xFFFFu);
    for (uint32_t sym = 0; sym < 512u; ++sym) {
        uint8_t len = lens[sym];
        if (len == 0) continue;
        uint32_t c = nextCode[len]++;
        uint32_t shift = 15u - len;
        uint32_t start = c << shift;
        uint32_t count = 1u << shift;
        if (start + count > 32768u) return false;
        uint16_t entry = static_cast<uint16_t>((static_cast<uint16_t>(len) << 9) | sym);
        for (uint32_t k = 0; k < count; ++k) decTbl[start + k] = entry;
    }
    if (nonZeroSyms == 1) {
        for (uint32_t k = 0; k < 32768u; ++k) {
            if (decTbl[k] == 0xFFFFu) decTbl[k] = decTbl[0];
        }
    }

    uint32_t bitBuf = (static_cast<uint32_t>(RdU16LE(src + 256)) << 16) | RdU16LE(src + 258);
    int extraBits = 16;
    size_t inPos = 260;

    auto consumeBits = [&](int n) -> bool {
        if (16 + extraBits < n) return false;
        bitBuf = (n >= 32) ? 0u : (bitBuf << n);
        extraBits -= n;
        if (extraBits < 0) {
            if (inPos + 2 <= srcLen) {
                uint32_t w = RdU16LE(src + inPos);
                inPos += 2;
                bitBuf |= (w << (-extraBits));
                extraBits += 16;
            } else if (inPos < srcLen) {
                uint32_t w = static_cast<uint32_t>(src[inPos++]) << 8;
                bitBuf |= (w << (-extraBits));
                extraBits += 16;
            }
        }
        return true;
    };

    size_t outPos = 0;
    while (outPos < dstLen) {
        uint16_t entry = decTbl[bitBuf >> 17];
        if (entry == 0xFFFFu) return false;
        int symLen   = entry >> 9;
        uint32_t sym = entry & 0x1FFu;
        if (!consumeBits(symLen)) return false;

        if (sym < 256u) {
            dst[outPos++] = static_cast<uint8_t>(sym);
        } else {
            uint32_t msym = sym - 256u;
            int offsetBits = static_cast<int>(msym >> 4);
            size_t matchLen = static_cast<size_t>(msym & 0x0Fu);

            if (matchLen == 15u) {
                if (inPos >= srcLen) return false;
                uint32_t b1 = src[inPos++];
                if (b1 < 255u) {
                    matchLen = static_cast<size_t>(b1) + 15u;
                } else {
                    if (inPos + 2 > srcLen) return false;
                    uint32_t w2 = RdU16LE(src + inPos);
                    inPos += 2;
                    if (w2 != 0) {
                        if (w2 < 15u) return false;
                        matchLen = static_cast<size_t>(w2);
                    } else {
                        if (inPos + 4 > srcLen) return false;
                        uint32_t d4 = RdU32LE(src + inPos);
                        inPos += 4;
                        if (d4 < 15u) return false;
                        matchLen = static_cast<size_t>(d4);
                    }
                }
            }
            matchLen += 3u;

            uint32_t lowOff = (offsetBits == 0) ? 0u : (bitBuf >> (32 - offsetBits));
            if (!consumeBits(offsetBits)) return false;
            size_t matchOff = (static_cast<size_t>(1) << offsetBits) | static_cast<size_t>(lowOff);

            if (matchOff == 0 || matchOff > outPos || matchLen > dstLen - outPos) return false;
            for (size_t k = 0; k < matchLen; ++k) {
                dst[outPos + k] = dst[outPos - matchOff + k];
            }
            outPos += matchLen;
        }
    }
    return true;
}

// [MS-WIM] Chunked Resource Reader (Uncompressed, XPRESS Huffman, or LZX via Explorer2::Lzx::Decoder)
inline bool DecompressWimResource(
    const uint8_t* wimData,
    size_t wimSize,
    uint32_t wimHeaderFlags,
    uint32_t wimChunkSize,
    const WimResEntry& res,
    std::vector<uint8_t>& outBuf,
    size_t maxBytes = 256 * 1024 * 1024)
{
    outBuf.clear();
    if (res.wimOffset > wimSize || res.compSize > wimSize - res.wimOffset) return false;

    if ((res.flags & 0x04u) == 0) {
        uint64_t rawLen = (res.origSize > 0 && res.origSize <= res.compSize) ? res.origSize : res.compSize;
        if (rawLen > maxBytes) return false;
        const uint8_t* p = wimData + static_cast<size_t>(res.wimOffset);
        outBuf.assign(p, p + static_cast<size_t>(rawLen));
        return true;
    }

    if (res.origSize == 0) return true;
    if (res.origSize > maxBytes || res.compSize == 0) return false;
    if ((wimHeaderFlags & 0x00080000u) != 0) return false; // LZMS solid .esd

    uint32_t chunkSize = (wimChunkSize >= 512u && wimChunkSize <= 64u * 1024u * 1024u &&
                          (wimChunkSize & (wimChunkSize - 1u)) == 0)
                       ? wimChunkSize
                       : 32768u;
    uint64_t numChunks = (res.origSize + chunkSize - 1ULL) / chunkSize;
    size_t entryBytes  = (res.origSize > 0xFFFFFFFFULL) ? 8u : 4u;
    if (numChunks == 0 || (numChunks - 1ULL) > res.compSize / entryBytes) return false;

    size_t tableBytes = static_cast<size_t>(numChunks - 1ULL) * entryBytes;
    const uint8_t* resBase  = wimData + static_cast<size_t>(res.wimOffset);
    const uint8_t* compBase = resBase + tableBytes;
    uint64_t compPayloadLen = res.compSize - tableBytes;

    outBuf.resize(static_cast<size_t>(res.origSize));
    bool isLzx    = (wimHeaderFlags & 0x00040000u) != 0;
    bool isXpress = (wimHeaderFlags & 0x00020000u) != 0 || !isLzx;

    int lzxWndBits = 15;
    while ((1u << lzxWndBits) < chunkSize && lzxWndBits < 21) ++lzxWndBits;
    const std::vector<uint64_t> emptyFrameOffs;

    for (uint64_t c = 0; c < numChunks; ++c) {
        uint64_t cStart = 0;
        if (c > 0) {
            const uint8_t* te = resBase + static_cast<size_t>(c - 1ULL) * entryBytes;
            cStart = (entryBytes == 8u) ? RdU64LE(te) : RdU32LE(te);
        }
        uint64_t cEnd = (c + 1ULL < numChunks)
            ? ((entryBytes == 8u) ? RdU64LE(resBase + static_cast<size_t>(c) * 8u)
                                  : RdU32LE(resBase + static_cast<size_t>(c) * 4u))
            : compPayloadLen;

        if (cEnd < cStart || cEnd > compPayloadLen) {
            outBuf.clear();
            return false;
        }
        size_t compLen = static_cast<size_t>(cEnd - cStart);
        size_t dstOff  = static_cast<size_t>(c * chunkSize);
        size_t dstLen  = (c + 1ULL < numChunks)
            ? static_cast<size_t>(chunkSize)
            : static_cast<size_t>(res.origSize - dstOff);

        if (compLen == dstLen) {
            std::memcpy(outBuf.data() + dstOff, compBase + static_cast<size_t>(cStart), dstLen);
        } else {
            if (compLen == 0 || compLen > dstLen) {
                outBuf.clear();
                return false;
            }
            const uint8_t* cPtr = compBase + static_cast<size_t>(cStart);
            bool ok = false;
            if (isXpress) {
                ok = DecompressXpressHuffmanChunk(cPtr, compLen, outBuf.data() + dstOff, dstLen);
            }
            if (!ok && isLzx) {
                // Pad with 2 trailing zero bytes so odd-sized LZX chunks never starve the 16-bit LE word reader
                std::vector<uint8_t> padded(compLen + 2u, 0u);
                std::memcpy(padded.data(), cPtr, compLen);
                std::vector<uint8_t> lzxChunk;
                ::Explorer2::Lzx::Decoder dec(padded.data(), padded.size(), lzxWndBits, 0u, emptyFrameOffs);
                if (dec.Decode(dstLen, lzxChunk) && lzxChunk.size() == dstLen) {
                    std::memcpy(outBuf.data() + dstOff, lzxChunk.data(), dstLen);
                    ok = true;
                }
            }
            if (!ok) {
                outBuf.clear();
                return false;
            }
        }
    }
    return true;
}

inline bool WalkOrExtractWimDirEntryTree(
    const uint8_t* wimData,
    size_t wimSize,
    uint32_t wimHeaderFlags,
    uint32_t wimChunkSize,
    const uint8_t* metaBuf,
    size_t metaLen,
    size_t dirOffset,
    const std::wstring& parentPath,
    const std::vector<WimResEntry>& lookupTable,
    int depth,
    std::set<size_t>& visitedOffsets,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm,
    std::vector<uint8_t>* outExtracted,
    bool& outExtractedOk)
{
    if (depth > 20 || dirOffset > metaLen || metaLen - dirOffset < 102) return false;
    if (outInfo && outInfo->entries.size() >= 30000) return true;
    if (!visitedOffsets.insert(dirOffset).second) return true;

    size_t pos = dirOffset;
    while (pos <= metaLen && metaLen - pos >= 8) {
        if (outInfo && outInfo->entries.size() >= 30000) break;
        uint64_t liLength = RdU64LE(metaBuf + pos);
        if (liLength == 0) break;
        if (liLength < 102 || liLength > metaLen - pos) break;

        const uint8_t* de   = metaBuf + pos;
        uint32_t attrs      = RdU32LE(de + 8);
        uint64_t subdirOff  = RdU64LE(de + 16);
        uint64_t lastWrite  = RdU64LE(de + 56);
        const uint8_t* sha1 = de + 64;
        uint16_t fnBytes    = RdU16LE(de + 100);

        std::wstring fname;
        if (fnBytes > 0 && 102u + static_cast<uint64_t>(fnBytes) <= liLength) {
            size_t nChars = fnBytes / 2;
            fname.reserve(nChars);
            for (size_t c = 0; c < nChars; ++c) {
                wchar_t wc = static_cast<wchar_t>(RdU16LE(de + 102 + c * 2));
                if (wc == 0) break;
                if (wc == L'/' || wc == L'\\') wc = L'_';
                fname.push_back(wc);
            }
        }

        bool isDir = (attrs & 0x10u) != 0;
        std::wstring curPath = fname.empty()
            ? parentPath
            : ArchivePath::Normalize(parentPath.empty() ? fname : (parentPath + L"/" + fname));

        if (!fname.empty()) {
            bool zeroHash = true;
            for (int b = 0; b < 20; ++b) {
                if (sha1[b] != 0) { zeroHash = false; break; }
            }
            const WimResEntry* matchedRes = nullptr;
            if (!isDir && !zeroHash) {
                for (const auto& res : lookupTable) {
                    if (std::memcmp(res.sha1, sha1, 20) == 0) {
                        matchedRes = &res;
                        break;
                    }
                }
            }

            if (extractTargetNorm && outExtracted) {
                if (!isDir && PathEqualsCI(curPath, *extractTargetNorm)) {
                    if (zeroHash) {
                        outExtracted->clear();
                        outExtractedOk = true;
                        return true;
                    }
                    if (!matchedRes) return false;
                    outExtractedOk = DecompressWimResource(
                        wimData, wimSize, wimHeaderFlags, wimChunkSize, *matchedRes, *outExtracted);
                    return outExtractedOk;
                }
            } else if (outInfo) {
                ArchiveItemEntry item;
                item.pathInArchive = curPath;
                item.name = ArchivePath::BaseName(curPath);
                item.isDirectory = isDir;
                FileTimeU64ToDos(lastWrite, item.dosDate, item.dosTime);
                if (!isDir && matchedRes) {
                    item.uncompressedSize  = matchedRes->origSize;
                    item.compressedSize    = matchedRes->compSize;
                    item.rawDataOffset     = matchedRes->wimOffset;
                    item.compressionMethod = ((matchedRes->flags & 0x04u) == 0) ? 0 : 1;
                }
                outInfo->entries.push_back(std::move(item));
            }
        }

        if (isDir && subdirOff > 0 && subdirOff < metaLen && subdirOff != dirOffset) {
            WalkOrExtractWimDirEntryTree(
                wimData, wimSize, wimHeaderFlags, wimChunkSize,
                metaBuf, metaLen, static_cast<size_t>(subdirOff), curPath,
                lookupTable, depth + 1, visitedOffsets,
                outInfo, extractTargetNorm, outExtracted, outExtractedOk);
            if (extractTargetNorm && outExtractedOk) return true;
        }

        uint64_t step = (liLength + 7ULL) & ~7ULL;
        if (step > metaLen - pos) break;
        pos += static_cast<size_t>(step);
    }
    return true;
}

inline bool ParseOrExtractWindowsWim(
    const uint8_t* data,
    size_t size,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm = nullptr,
    std::vector<uint8_t>* outExtracted = nullptr)
{
    if (!data || size < 208 || std::memcmp(data, "MSWIM\0\0\0", 8) != 0) return false;
    uint32_t cbSize      = RdU32LE(data + 8);
    uint32_t dwVersion   = RdU32LE(data + 12);
    uint32_t dwFlags     = RdU32LE(data + 16);
    uint32_t dwChunkSize = RdU32LE(data + 20);
    uint32_t imgCount    = RdU32LE(data + 44);
    if (cbSize < 120 || cbSize > size || dwVersion == 0) return false;

    if (outInfo) {
        outInfo->formatName = (dwFlags & 0x00080000u) ? L"Windows ESD Image (LZMS)" : L"Windows Image (WIM)";
    }

    auto readResHdr = [&](size_t off) -> WimResEntry {
        WimResEntry r;
        uint64_t sf = RdU64LE(data + off);
        r.compSize  = sf & 0x00FFFFFFFFFFFFFFULL;
        r.flags     = data[off + 7];
        r.wimOffset = RdU64LE(data + off + 8);
        r.origSize  = RdU64LE(data + off + 16);
        return r;
    };

    WimResEntry rhOffsetTbl = readResHdr(48);
    WimResEntry rhXml       = readResHdr(72);
    WimResEntry rhBootMeta  = readResHdr(96);

    struct WimXmlImage {
        uint32_t index = 1;
        std::wstring name;
    };
    std::vector<WimXmlImage> xmlImages;
    std::vector<uint8_t> xmlBytes;
    if (rhXml.compSize >= 4 && rhXml.wimOffset <= size && rhXml.compSize <= size - rhXml.wimOffset &&
        DecompressWimResource(data, size, dwFlags, dwChunkSize, rhXml, xmlBytes, 32 * 1024 * 1024))
    {
        if (extractTargetNorm && outExtracted && PathEqualsCI(*extractTargetNorm, L"[WIM_Manifest].xml")) {
            *outExtracted = std::move(xmlBytes);
            return true;
        }
        if (outInfo) {
            ArchiveItemEntry xmlEntry;
            xmlEntry.pathInArchive = L"[WIM_Manifest].xml";
            xmlEntry.name = xmlEntry.pathInArchive;
            xmlEntry.uncompressedSize = xmlBytes.size();
            xmlEntry.compressedSize = rhXml.compSize;
            xmlEntry.rawDataOffset = rhXml.wimOffset;
            xmlEntry.compressionMethod = ((rhXml.flags & 0x04u) == 0) ? 0 : 1;
            outInfo->entries.push_back(std::move(xmlEntry));
        }

        const uint8_t* xp = xmlBytes.data();
        size_t xlen = xmlBytes.size();
        std::wstring wxml;
        if (xlen >= 2 && ((xp[0] == 0xFF && xp[1] == 0xFE) || xp[1] == 0x00)) {
            size_t s = (xp[0] == 0xFF && xp[1] == 0xFE) ? 2 : 0;
            wxml.reserve((xlen - s) / 2);
            for (size_t i = s; i + 2 <= xlen; i += 2) {
                wchar_t wc = static_cast<wchar_t>(RdU16LE(xp + i));
                if (wc != 0) wxml.push_back(wc);
            }
        } else {
            wxml = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(xp), xlen));
        }

        size_t pos = 0;
        uint32_t seqIdx = 1;
        while (pos < wxml.size()) {
            size_t imStart = wxml.find(L"<IMAGE", pos);
            if (imStart == std::wstring::npos) break;
            size_t imEnd = wxml.find(L"</IMAGE>", imStart);
            if (imEnd == std::wstring::npos) imEnd = wxml.size();
            std::wstring_view blk(wxml.data() + imStart, imEnd - imStart);

            auto getTag = [&](std::wstring_view openT, std::wstring_view closeT) -> std::wstring {
                size_t a = blk.find(openT);
                if (a == std::wstring::npos) return L"";
                a += openT.size();
                size_t b = blk.find(closeT, a);
                return (b == std::wstring::npos) ? L"" : std::wstring(blk.substr(a, b - a));
            };

            WimXmlImage wi;
            wi.index = seqIdx++;
            size_t idxAttr = blk.find(L"INDEX=\"");
            if (idxAttr != std::wstring::npos && idxAttr < 64) {
                wi.index = static_cast<uint32_t>(_wtoi(std::wstring(blk.substr(idxAttr + 7, 8)).c_str()));
                if (wi.index == 0) wi.index = seqIdx - 1;
            }
            wi.name = getTag(L"<NAME>", L"</NAME>");
            xmlImages.push_back(std::move(wi));
            pos = (imEnd < wxml.size()) ? (imEnd + 8) : wxml.size();
        }
    }

    std::vector<WimResEntry> lookupTable;
    std::vector<WimResEntry> metadataResources;
    std::vector<uint8_t> tblBytes;
    if (rhOffsetTbl.compSize >= 50 &&
        DecompressWimResource(data, size, dwFlags, dwChunkSize, rhOffsetTbl, tblBytes, 128 * 1024 * 1024))
    {
        size_t cnt = tblBytes.size() / 50u;
        for (size_t i = 0; i < cnt && i < 200000; ++i) {
            const uint8_t* tp = tblBytes.data() + i * 50u;
            WimResEntry e;
            uint64_t sf = RdU64LE(tp);
            e.compSize  = sf & 0x00FFFFFFFFFFFFFFULL;
            e.flags     = tp[7];
            e.wimOffset = RdU64LE(tp + 8);
            e.origSize  = RdU64LE(tp + 16);
            std::memcpy(e.sha1, tp + 30, 20);
            if ((e.flags & 0x02u) != 0) metadataResources.push_back(e);
            else lookupTable.push_back(e);
        }
    }
    if (metadataResources.empty() && rhBootMeta.compSize >= 32 && rhBootMeta.wimOffset < size) {
        metadataResources.push_back(rhBootMeta);
    }

    size_t walkedImages = 0;
    for (size_t m = 0; m < metadataResources.size(); ++m) {
        const auto& mr = metadataResources[m];
        std::vector<uint8_t> metaBytes;
        if (!DecompressWimResource(data, size, dwFlags, dwChunkSize, mr, metaBytes, 128 * 1024 * 1024)) continue;
        if (metaBytes.size() < 110) continue;

        uint32_t secLen = RdU32LE(metaBytes.data());
        size_t rootDirOff = (secLen == 0) ? 8u : static_cast<size_t>((static_cast<uint64_t>(secLen) + 7ULL) & ~7ULL);
        if (rootDirOff > metaBytes.size() || metaBytes.size() - rootDirOff < 102) continue;

        std::wstring imgPrefix;
        if (m < xmlImages.size() && !xmlImages[m].name.empty()) {
            if (metadataResources.size() > 1 || imgCount > 1) {
                imgPrefix = L"Image" + std::to_wstring(xmlImages[m].index) + L"_" + xmlImages[m].name;
            } else {
                imgPrefix = xmlImages[m].name;
            }
        } else if (metadataResources.size() > 1 || imgCount > 1) {
            imgPrefix = L"Image_" + std::to_wstring(m + 1);
        }
        for (wchar_t& ch : imgPrefix) if (ch == L'/' || ch == L'\\') ch = L'_';

        std::set<size_t> visited;
        bool extractedOk = false;
        size_t before = outInfo ? outInfo->entries.size() : 0;
        WalkOrExtractWimDirEntryTree(
            data, size, dwFlags, dwChunkSize,
            metaBytes.data(), metaBytes.size(), rootDirOff, imgPrefix,
            lookupTable, 0, visited, outInfo, extractTargetNorm, outExtracted, extractedOk);
        if (extractTargetNorm && extractedOk) return true;
        if (outInfo && outInfo->entries.size() > before) ++walkedImages;
    }

    if (extractTargetNorm) return false;
    if (!outInfo) return false;

    if (walkedImages == 0) {
        outInfo->entries.clear();
        outInfo->valid = false;
        return false;
    }

    outInfo->valid = !outInfo->entries.empty();
    return outInfo->valid;
}

inline bool ParseWindowsWim(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return ParseOrExtractWindowsWim(data, size, &outInfo, nullptr, nullptr);
}

inline bool ExtractWimEntryFromMemory(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    return ParseOrExtractWindowsWim(data, size, nullptr, &normTarget, &outBytes);
}

} // namespace DetailDiskAndGameArc

inline bool InspectWindowsWim(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return DetailDiskAndGameArc::ParseWindowsWim(data, size, outInfo);
}

inline bool ExtractWindowsWimEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    return DetailDiskAndGameArc::ExtractWimEntryFromMemory(data, size, normTarget, outBytes);
}

} // namespace Explorer2::Formats
