#pragma once
// KiriKiriXp3.h — KiriKiri Visual Novel XP3 archive reader & zlib segment extractor (.xp3).

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailArc200 {

// ============================================================================
// #25. KiriKiri Visual Novel Archive (.xp3 — "XP3\r\n \n\x1A\x8B\x67\x01")
// ============================================================================
inline bool ParseOrExtractKiriKiriXp3(
    const uint8_t* data,
    size_t size,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetPath,
    std::vector<uint8_t>* outExtractedBytes)
{
    static const uint8_t kXp3Sig[11] = { 'X','P','3','\r','\n',' ','\n',0x1A,0x8B,0x67,0x01 };
    if (size < 28 || std::memcmp(data, kXp3Sig, 11) != 0) return false;
    if (outInfo) outInfo->formatName = L"KiriKiri XP3 Archive (.xp3)";

    uint64_t idxOff = RdU64LE(data + 11);
    if (idxOff + 9 <= size && data[idxOff] == 0x80) {
        // v2 extended index header: [0x80:1][hdrSize:8][minorVer:4][realIdxOff:8]
        if (idxOff + 21 <= size) idxOff = RdU64LE(data + idxOff + 13);
    }
    if (idxOff + 9 > size) return false;

    uint8_t idxFlags = data[idxOff];
    std::vector<uint8_t> idxBuf;
    const uint8_t* idxPtr = nullptr;
    size_t idxLen = 0;

    if ((idxFlags & 0x07) == 1) {
        if (idxOff + 17 > size) return false;
        uint64_t compSz   = RdU64LE(data + idxOff + 1);
        uint64_t uncompSz = RdU64LE(data + idxOff + 9);
        if (idxOff + 17 + compSz > size || uncompSz > 64 * 1024 * 1024) return false;
        if (!ZipDeflate::InflateZlib(data + idxOff + 17, static_cast<size_t>(compSz), idxBuf, static_cast<size_t>(uncompSz) + 64)) {
            return false;
        }
        idxPtr = idxBuf.data();
        idxLen = idxBuf.size();
    } else {
        uint64_t rawSz = RdU64LE(data + idxOff + 1);
        if (idxOff + 9 + rawSz > size) return false;
        idxPtr = data + idxOff + 9;
        idxLen = static_cast<size_t>(rawSz);
    }

    struct Xp3Seg {
        uint32_t flags = 0;
        uint64_t start = 0;
        uint64_t origSz = 0;
        uint64_t compSz = 0;
    };

    std::wstring wantNorm = extractTargetPath ? ArchivePath::Normalize(*extractTargetPath) : std::wstring{};
    size_t p = 0;
    size_t count = 0;
    while (p + 12 <= idxLen && count < 40000) {
        uint64_t chunkSz = RdU64LE(idxPtr + p + 4);
        if (chunkSz > idxLen - p - 12) break;
        if (std::memcmp(idxPtr + p, "File", 4) == 0) {
            size_t sp = p + 12, send = p + 12 + static_cast<size_t>(chunkSz);
            std::wstring fname;
            uint64_t origSz = 0, compSz = 0, rawStart = 0;
            bool segCompressed = false;
            std::vector<Xp3Seg> segs;
            while (sp + 12 <= send) {
                uint64_t subSz = RdU64LE(idxPtr + sp + 4);
                if (subSz > send - sp - 12) break;
                const uint8_t* sub = idxPtr + sp + 12;
                if (std::memcmp(idxPtr + sp, "info", 4) == 0 && subSz >= 22) {
                    origSz = RdU64LE(sub + 4);
                    compSz = RdU64LE(sub + 12);
                    uint16_t nChars = RdU16LE(sub + 20);
                    if (22 + static_cast<size_t>(nChars) * 2 <= subSz) {
                        fname.resize(nChars);
                        for (uint16_t c = 0; c < nChars; ++c) {
                            fname[c] = static_cast<wchar_t>(RdU16LE(sub + 22 + c * 2));
                        }
                    }
                } else if (std::memcmp(idxPtr + sp, "segm", 4) == 0 && subSz >= 28) {
                    size_t numSegs = static_cast<size_t>(subSz / 28);
                    for (size_t s = 0; s < numSegs; ++s) {
                        const uint8_t* se = sub + s * 28;
                        Xp3Seg seg;
                        seg.flags  = RdU32LE(se + 0);
                        seg.start  = RdU64LE(se + 4);
                        seg.origSz = RdU64LE(se + 12);
                        seg.compSz = RdU64LE(se + 20);
                        if (s == 0) {
                            rawStart = seg.start;
                            if (origSz == 0) origSz = seg.origSz;
                            if (compSz == 0) compSz = seg.compSz;
                        }
                        if ((seg.flags & 0x07u) != 0 || segs.size() > 0) {
                            if ((seg.flags & 0x07u) != 0) segCompressed = true;
                        }
                        segs.push_back(seg);
                    }
                    if (segs.size() > 1) segCompressed = true;
                }
                sp += 12 + static_cast<size_t>(subSz);
            }
            std::wstring normP = ArchivePath::Normalize(fname);
            if (!normP.empty()) {
                ++count;
                if (extractTargetPath && outExtractedBytes) {
                    if (_wcsicmp(normP.c_str(), wantNorm.c_str()) == 0) {
                        if (segs.empty() || origSz > 256 * 1024 * 1024) return false;
                        outExtractedBytes->clear();
                        outExtractedBytes->reserve(static_cast<size_t>(origSz));
                        std::vector<uint8_t> segOut;
                        for (const auto& seg : segs) {
                            if ((seg.flags & 0x07u) != 0) {
                                if (seg.start + seg.compSz > size || seg.origSz > 256 * 1024 * 1024) return false;
                                if (!ZipDeflate::InflateZlib(data + seg.start, static_cast<size_t>(seg.compSz), segOut, static_cast<size_t>(seg.origSz) + 64) &&
                                    !ZipDeflate::InflateRaw(data + seg.start, static_cast<size_t>(seg.compSz), segOut, static_cast<size_t>(seg.origSz) + 64))
                                {
                                    return false;
                                }
                                outExtractedBytes->insert(outExtractedBytes->end(), segOut.begin(), segOut.end());
                            } else {
                                if (seg.start + seg.origSz > size) return false;
                                outExtractedBytes->insert(outExtractedBytes->end(), data + seg.start, data + seg.start + static_cast<size_t>(seg.origSz));
                            }
                        }
                        return true;
                    }
                } else if (outInfo) {
                    ArchiveItemEntry item;
                    item.pathInArchive = std::move(normP);
                    item.name = ArchivePath::BaseName(item.pathInArchive);
                    item.uncompressedSize = origSz;
                    item.compressedSize = compSz;
                    item.rawDataOffset = rawStart;
                    item.compressionMethod = segCompressed ? 8 : 0;
                    outInfo->entries.push_back(std::move(item));
                }
            }
        }
        p += 12 + static_cast<size_t>(chunkSz);
    }

    if (extractTargetPath) return false;
    if (!outInfo || outInfo->entries.empty()) return false;
    outInfo->valid = true;
    return true;
}

inline bool ParseKiriKiriXp3(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return ParseOrExtractKiriKiriXp3(data, size, &outInfo, nullptr, nullptr);
}

} // namespace DetailArc200

inline bool InspectKiriKiriXp3(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return DetailArc200::ParseKiriKiriXp3(data, size, outInfo);
}

inline bool ExtractKiriKiriXp3Entry(
    const uint8_t* data,
    size_t size,
    const std::wstring& want,
    std::vector<uint8_t>& outBytes)
{
    return DetailArc200::ParseOrExtractKiriKiriXp3(data, size, nullptr, &want, &outBytes);
}

} // namespace Explorer2::Formats
