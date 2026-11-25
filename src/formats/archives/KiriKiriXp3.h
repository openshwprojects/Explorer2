#pragma once
// KiriKiriXp3.h — KiriKiri Visual Novel XP3 archive reader & zlib segment extractor (.xp3).

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailUnixAndLegacyArc {

inline bool ParseOrExtractKiriKiriXp3(
    const uint8_t* data,
    size_t size,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetPath,
    std::vector<uint8_t>* outExtractedBytes)
{
    static const uint8_t kXp3Sig[11] = { 'X','P','3','\r','\n',' ','\n',0x1A,0x8B,0x67,0x01 };
    if (!data || size < 28 || std::memcmp(data, kXp3Sig, 11) != 0) return false;
    if (outInfo) outInfo->formatName = L"KiriKiri XP3 Archive (.xp3)";

    uint64_t idxOff = RdU64LE(data + 11);
    if (idxOff > size || size - idxOff < 9u) return false;
    if (data[static_cast<size_t>(idxOff)] == 0x80u) {
        // v2 extended index header: [0x80:1][hdrSize:8][minorVer:4][realIdxOff:8]
        if (size - idxOff < 21u) return false;
        idxOff = RdU64LE(data + static_cast<size_t>(idxOff) + 13u);
        if (idxOff > size || size - idxOff < 9u) return false;
    }

    const size_t idxPos = static_cast<size_t>(idxOff);
    uint8_t idxFlags = data[idxPos];
    std::vector<uint8_t> idxBuf;
    const uint8_t* idxPtr = nullptr;
    size_t idxLen = 0;

    if ((idxFlags & 0x07u) == 1u) {
        if (size - idxPos < 17u) return false;
        uint64_t compSz   = RdU64LE(data + idxPos + 1u);
        uint64_t uncompSz = RdU64LE(data + idxPos + 9u);
        if (compSz > size - idxPos - 17u || uncompSz > 64ULL * 1024 * 1024) return false;
        if (!ZipDeflate::InflateZlib(data + idxPos + 17u, static_cast<size_t>(compSz), idxBuf, static_cast<size_t>(uncompSz) + 64u)) {
            return false;
        }
        idxPtr = idxBuf.data();
        idxLen = idxBuf.size();
    } else {
        uint64_t rawSz = RdU64LE(data + idxPos + 1u);
        if (rawSz > size - idxPos - 9u) return false;
        idxPtr = data + idxPos + 9u;
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
    while (idxLen - p >= 12u && count < 40000u) {
        uint64_t chunkSz = RdU64LE(idxPtr + p + 4u);
        if (chunkSz > idxLen - p - 12u) break;
        if (std::memcmp(idxPtr + p, "File", 4) == 0) {
            size_t sp = p + 12u, send = p + 12u + static_cast<size_t>(chunkSz);
            std::wstring fname;
            uint64_t origSz = 0, compSz = 0, rawStart = 0;
            bool segCompressed = false;
            std::vector<Xp3Seg> segs;
            while (send - sp >= 12u) {
                uint64_t subSz = RdU64LE(idxPtr + sp + 4u);
                if (subSz > send - sp - 12u) break;
                const uint8_t* sub = idxPtr + sp + 12u;
                if (std::memcmp(idxPtr + sp, "info", 4) == 0 && subSz >= 22u) {
                    origSz = RdU64LE(sub + 4u);
                    compSz = RdU64LE(sub + 12u);
                    uint16_t nChars = RdU16LE(sub + 20u);
                    if (static_cast<uint64_t>(nChars) * 2ULL <= subSz - 22u) {
                        fname.resize(nChars);
                        for (uint16_t c = 0; c < nChars; ++c) {
                            fname[c] = static_cast<wchar_t>(RdU16LE(sub + 22u + static_cast<size_t>(c) * 2u));
                        }
                    }
                } else if (std::memcmp(idxPtr + sp, "segm", 4) == 0 && subSz >= 28u) {
                    size_t numSegs = static_cast<size_t>(subSz / 28u);
                    for (size_t s = 0; s < numSegs; ++s) {
                        const uint8_t* se = sub + s * 28u;
                        Xp3Seg seg;
                        seg.flags  = RdU32LE(se + 0u);
                        seg.start  = RdU64LE(se + 4u);
                        seg.origSz = RdU64LE(se + 12u);
                        seg.compSz = RdU64LE(se + 20u);
                        const uint64_t needBytes = ((seg.flags & 0x07u) != 0u) ? seg.compSz : seg.origSz;
                        if (seg.start > size || needBytes > size - seg.start) return false;
                        if (s == 0) {
                            rawStart = seg.start;
                            if (origSz == 0) origSz = seg.origSz;
                            if (compSz == 0) compSz = seg.compSz;
                        }
                        if ((seg.flags & 0x07u) != 0u) segCompressed = true;
                        segs.push_back(seg);
                    }
                    if (segs.size() > 1u) segCompressed = true;
                }
                sp += 12u + static_cast<size_t>(subSz);
            }
            std::wstring normP = ArchivePath::Normalize(fname);
            if (!normP.empty() && !segs.empty()) {
                ++count;
                if (extractTargetPath && outExtractedBytes) {
                    if (_wcsicmp(normP.c_str(), wantNorm.c_str()) == 0) {
                        if (origSz > 256ULL * 1024 * 1024) return false;
                        outExtractedBytes->clear();
                        outExtractedBytes->reserve(static_cast<size_t>(origSz));
                        std::vector<uint8_t> segOut;
                        for (const auto& seg : segs) {
                            if ((seg.flags & 0x07u) != 0u) {
                                if (seg.start > size || seg.compSz > size - seg.start || seg.origSz > 256ULL * 1024 * 1024) {
                                    return false;
                                }
                                const uint8_t* segPtr = data + static_cast<size_t>(seg.start);
                                if (!ZipDeflate::InflateZlib(segPtr, static_cast<size_t>(seg.compSz), segOut, static_cast<size_t>(seg.origSz) + 64u) &&
                                    !ZipDeflate::InflateRaw(segPtr, static_cast<size_t>(seg.compSz), segOut, static_cast<size_t>(seg.origSz) + 64u))
                                {
                                    return false;
                                }
                                outExtractedBytes->insert(outExtractedBytes->end(), segOut.begin(), segOut.end());
                            } else {
                                if (seg.start > size || seg.origSz > size - seg.start) return false;
                                const uint8_t* segPtr = data + static_cast<size_t>(seg.start);
                                outExtractedBytes->insert(outExtractedBytes->end(), segPtr, segPtr + static_cast<size_t>(seg.origSz));
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
        p += 12u + static_cast<size_t>(chunkSz);
    }

    if (extractTargetPath) return false;
    if (!outInfo || outInfo->entries.empty()) return false;
    outInfo->valid = true;
    return true;
}

inline bool ParseKiriKiriXp3(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return ParseOrExtractKiriKiriXp3(data, size, &outInfo, nullptr, nullptr);
}

} // namespace DetailUnixAndLegacyArc

inline bool InspectKiriKiriXp3(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return DetailUnixAndLegacyArc::ParseKiriKiriXp3(data, size, outInfo);
}

inline bool ExtractKiriKiriXp3Entry(
    const uint8_t* data,
    size_t size,
    const std::wstring& want,
    std::vector<uint8_t>& outBytes)
{
    return DetailUnixAndLegacyArc::ParseOrExtractKiriKiriXp3(data, size, nullptr, &want, &outBytes);
}

} // namespace Explorer2::Formats
