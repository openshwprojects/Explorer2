#pragma once
// UdfImage.h — ECMA-167 / Universal Disk Format (UDF 1.02–2.60) optical & disk image reader (.udf).

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailUdfNrg {

using ::Explorer2::ByteIO::RdU16LE;
using ::Explorer2::ByteIO::RdU32LE;
using ::Explorer2::ByteIO::RdU64LE;
using ::Explorer2::ByteIO::RdU16BE;
using ::Explorer2::ByteIO::RdU32BE;
using ::Explorer2::ByteIO::RdU64BE;

inline bool EndsWithCI(const std::wstring& s, const wchar_t* suffix) {
    const size_t sl = wcslen(suffix);
    if (s.size() < sl) return false;
    return _wcsicmp(s.c_str() + (s.size() - sl), suffix) == 0;
}

inline std::string AsciiLower(std::string_view sv) {
    std::string out(sv);
    for (char& ch : out) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    return out;
}

inline std::string_view TrimAsciiSV(std::string_view sv) {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r')) sv.remove_prefix(1);
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r')) sv.remove_suffix(1);
    return sv;
}

inline void CalendarToDosDateTime(
    int year, int month, int day,
    int hour, int minute, int second,
    uint16_t& outDate, uint16_t& outTime)
{
    outDate = 0;
    outTime = 0;
    if (year < 1980 || year > 2107 || month < 1 || month > 12 || day < 1 || day > 31) return;
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 60) return;
    if (second == 60) second = 59;
    outDate = static_cast<uint16_t>(((year - 1980) << 9) | (month << 5) | day);
    outTime = static_cast<uint16_t>((hour << 11) | (minute << 5) | (second / 2));
}

inline bool ValidateUdfTag(const uint8_t* buf, size_t len, uint16_t expectedTagId = 0) {
    if (!buf || len < 16u) return false;
    uint16_t tagId = RdU16LE(buf + 0);
    if (tagId == 0u) return false;
    if (expectedTagId != 0u && tagId != expectedTagId) return false;
    uint8_t sum = 0;
    for (int i = 0; i < 16; ++i) {
        if (i != 4) sum = static_cast<uint8_t>(sum + buf[i]);
    }
    return sum == buf[4];
}

inline std::wstring DecodeOstaCompressedUnicode(const uint8_t* src, size_t len) {
    if (!src || len == 0) return L"";
    const uint8_t compId = src[0];
    std::wstring out;
    if (compId == 8u) {
        out.reserve(len - 1u);
        for (size_t i = 1u; i < len; ++i) {
            if (src[i] == 0u) break;
            wchar_t wc = static_cast<wchar_t>(src[i]);
            if (wc == L'/' || wc == L'\\' || wc == L':') wc = L'_';
            out.push_back(wc);
        }
    } else if (compId == 16u) {
        out.reserve((len - 1u) / 2u);
        for (size_t i = 1u; i + 1u < len; i += 2u) {
            uint16_t cu = RdU16BE(src + i);
            if (cu == 0u) break;
            if (sizeof(wchar_t) >= 4 && cu >= 0xD800u && cu <= 0xDBFFu && i + 3u < len) {
                uint16_t lo = RdU16BE(src + i + 2u);
                if (lo >= 0xDC00u && lo <= 0xDFFFu) {
                    uint32_t cp = 0x10000u + (((static_cast<uint32_t>(cu) - 0xD800u) << 10u) |
                                              (static_cast<uint32_t>(lo) - 0xDC00u));
                    out.push_back(static_cast<wchar_t>(cp));
                    i += 2u;
                    continue;
                }
            }
            wchar_t wc = static_cast<wchar_t>(cu);
            if (wc == L'/' || wc == L'\\' || wc == L':') wc = L'_';
            out.push_back(wc);
        }
    }
    return out;
}

inline std::wstring DecodeOstaDstringField(const uint8_t* field, size_t fieldSize) {
    if (!field || fieldSize < 2u) return L"";
    uint8_t byteLen = field[fieldSize - 1u];
    if (byteLen == 0u || byteLen > fieldSize - 1u) return L"";
    return DecodeOstaCompressedUnicode(field, byteLen);
}

inline void UdfTimestampToDos(const uint8_t* ts12, uint16_t& outDate, uint16_t& outTime) {
    int16_t year = static_cast<int16_t>(RdU16LE(ts12 + 2));
    CalendarToDosDateTime(year, ts12[4], ts12[5], ts12[6], ts12[7], ts12[8], outDate, outTime);
}

struct UdfLongAd {
    uint32_t extLenAndType = 0;
    uint32_t lba = 0;
    uint16_t partRef = 0;

    uint32_t ByteLength() const { return extLenAndType & 0x3FFFFFFFu; }
    uint8_t  ExtentType() const { return static_cast<uint8_t>((extLenAndType >> 30u) & 0x03u); }
};

inline UdfLongAd ReadUdfLongAd(const uint8_t* p) {
    UdfLongAd ad;
    ad.extLenAndType = RdU32LE(p + 0);
    ad.lba           = RdU32LE(p + 4);
    ad.partRef       = RdU16LE(p + 8);
    return ad;
}

struct UdfResolvedRun {
    bool isSparseZero = false;
    uint64_t diskByteOff = 0;
    uint64_t byteLen = 0;
};

struct UdfVolumeContext {
    const DetailArc100::VirtualDiskReader* reader = nullptr;
    uint64_t partBase = 0;
    uint32_t sectorSize = 2048;
    uint32_t logBlkSize = 2048;
    uint16_t udfRevBcd = 0x0102;
    std::wstring volLabel;

    struct PhysPartition {
        uint32_t startSector = 0;
        uint32_t lengthSectors = 0;
    };
    std::unordered_map<uint16_t, PhysPartition> physParts; // keyed by PartitionNumber

    struct PartRefMapping {
        bool isMetadataPart = false;
        uint16_t physPartNum = 0;
        uint32_t metaFileLba = 0;
        uint32_t metaMirrorLba = 0;
        std::vector<UdfResolvedRun> metaFileRuns;
        uint64_t metaFileSize = 0;
    };
    std::vector<PartRefMapping> partRefs;

    bool ResolvePhysBlockToDisk(uint16_t physPartNum, uint32_t lba, uint64_t& outDiskOff) const {
        auto it = physParts.find(physPartNum);
        if (it == physParts.end()) return false;
        uint64_t partDiskStart = partBase + static_cast<uint64_t>(it->second.startSector) * sectorSize;
        outDiskOff = partDiskStart + static_cast<uint64_t>(lba) * logBlkSize;
        return outDiskOff < reader->virtualSize;
    }

    bool ResolveLogicalBlockToDisk(uint16_t partRef, uint32_t lba, uint64_t& outDiskOff) const {
        if (partRef >= partRefs.size()) {
            // Direct fallback if single physical partition matches partRef
            return ResolvePhysBlockToDisk(partRef, lba, outDiskOff);
        }
        const auto& pm = partRefs[partRef];
        if (!pm.isMetadataPart) {
            return ResolvePhysBlockToDisk(pm.physPartNum, lba, outDiskOff);
        }
        // Translate logical byte offset B * logBlkSize through Metadata File's resolved runs
        const uint64_t targetMetaOff = static_cast<uint64_t>(lba) * logBlkSize;
        uint64_t curMetaOff = 0;
        for (const auto& r : pm.metaFileRuns) {
            if (targetMetaOff >= curMetaOff && targetMetaOff < curMetaOff + r.byteLen) {
                if (r.isSparseZero) return false;
                outDiskOff = r.diskByteOff + (targetMetaOff - curMetaOff);
                return outDiskOff < reader->virtualSize;
            }
            curMetaOff += r.byteLen;
        }
        return false;
    }

    bool ReadLogicalBytes(uint16_t partRef, uint32_t lba, uint64_t byteLen, std::vector<uint8_t>& outBuf) const {
        outBuf.clear();
        if (byteLen == 0) return true;
        if (byteLen > 128ULL * 1024 * 1024) return false;

        if (partRef < partRefs.size() && partRefs[partRef].isMetadataPart) {
            // Block-by-block read in case Metadata File extents are non-contiguous
            outBuf.resize(static_cast<size_t>(byteLen), 0);
            uint64_t done = 0;
            uint32_t curLba = lba;
            while (done < byteLen) {
                size_t chunk = static_cast<size_t>(std::min<uint64_t>(logBlkSize, byteLen - done));
                uint64_t dOff = 0;
                if (!ResolveLogicalBlockToDisk(partRef, curLba, dOff)) return false;
                if (dOff > reader->virtualSize || chunk > reader->virtualSize - dOff) return false;
                if (!reader->readBytes(dOff, outBuf.data() + static_cast<size_t>(done), chunk)) return false;
                done += chunk;
                ++curLba;
            }
            return true;
        }

        uint64_t dOff = 0;
        if (!ResolveLogicalBlockToDisk(partRef, lba, dOff)) return false;
        if (dOff > reader->virtualSize || byteLen > reader->virtualSize - dOff) return false;
        outBuf.resize(static_cast<size_t>(byteLen));
        return reader->readBytes(dOff, outBuf.data(), outBuf.size());
    }
};

struct UdfParsedIcb {
    uint8_t  fileType = 0;
    uint8_t  allocType = 0;
    uint64_t infoLength = 0;
    uint16_t dosDate = 0;
    uint16_t dosTime = 0;
    bool     isInline = false;
    std::vector<uint8_t> inlineBytes;
    std::vector<UdfResolvedRun> runs;
};

inline bool ParseUdfIcbAt(
    const UdfVolumeContext& ctx,
    uint16_t icbPartRef,
    uint32_t icbLba,
    bool icbIsPhysicalPartNum,
    UdfParsedIcb& outIcb)
{
    outIcb = {};
    uint64_t icbDiskOff = 0;
    if (icbIsPhysicalPartNum) {
        if (!ctx.ResolvePhysBlockToDisk(icbPartRef, icbLba, icbDiskOff)) return false;
    } else {
        if (!ctx.ResolveLogicalBlockToDisk(icbPartRef, icbLba, icbDiskOff)) return false;
    }
    if (icbDiskOff + ctx.logBlkSize > ctx.reader->virtualSize) return false;

    std::vector<uint8_t> fe(ctx.logBlkSize);
    if (!ctx.reader->readBytes(icbDiskOff, fe.data(), fe.size())) return false;
    if (!ValidateUdfTag(fe.data(), fe.size())) return false;

    const uint16_t tagId = RdU16LE(fe.data() + 0);
    if (tagId != 261u && tagId != 266u) return false;

    outIcb.fileType   = fe[27];
    uint16_t icbFlags = RdU16LE(fe.data() + 34);
    outIcb.allocType  = static_cast<uint8_t>(icbFlags & 0x07u);
    outIcb.infoLength = RdU64LE(fe.data() + 56);
    if (outIcb.infoLength > 256ULL * 1024 * 1024) return false;

    size_t allocOff = 0;
    uint32_t lenEA = 0;
    uint32_t lenAD = 0;
    if (tagId == 261u) {
        if (fe.size() < 176u) return false;
        UdfTimestampToDos(fe.data() + 84, outIcb.dosDate, outIcb.dosTime);
        lenEA    = RdU32LE(fe.data() + 168);
        lenAD    = RdU32LE(fe.data() + 172);
        allocOff = 176u + static_cast<size_t>(lenEA);
    } else {
        if (fe.size() < 216u) return false;
        UdfTimestampToDos(fe.data() + 92, outIcb.dosDate, outIcb.dosTime);
        lenEA    = RdU32LE(fe.data() + 208);
        lenAD    = RdU32LE(fe.data() + 212);
        allocOff = 216u + static_cast<size_t>(lenEA);
    }
    if (lenEA > fe.size() || allocOff > fe.size() || lenAD > fe.size() - allocOff) return false;

    if (outIcb.allocType == 3u) { // Inline ICB
        outIcb.isInline = true;
        size_t take = std::min<size_t>(static_cast<size_t>(outIcb.infoLength), lenAD);
        if (allocOff + take > fe.size()) return false;
        outIcb.inlineBytes.assign(fe.data() + allocOff, fe.data() + allocOff + take);
        if (outIcb.inlineBytes.size() < outIcb.infoLength) {
            outIcb.inlineBytes.resize(static_cast<size_t>(outIcb.infoLength), 0);
        }
        return true;
    }

    if (outIcb.allocType > 2u) return false;
    const size_t adStep = (outIcb.allocType == 0u) ? 8u : ((outIcb.allocType == 1u) ? 16u : 20u);

    auto appendAdsFromBuffer = [&](auto& self, const uint8_t* adBuf, size_t adBytes, uint16_t curPartRef, int aedDepth) -> bool {
        if (aedDepth > 32) return false;
        size_t p = 0;
        while (p + adStep <= adBytes && outIcb.runs.size() < 65536u) {
            uint32_t rawLen = RdU32LE(adBuf + p);
            uint32_t extLen = rawLen & 0x3FFFFFFFu;
            uint8_t  extTp  = static_cast<uint8_t>((rawLen >> 30u) & 0x03u);
            if (extLen == 0u) break;

            uint32_t extLba = 0;
            uint16_t extPartRef = curPartRef;
            if (outIcb.allocType == 0u) {
                extLba = RdU32LE(adBuf + p + 4);
            } else if (outIcb.allocType == 1u) {
                extLba     = RdU32LE(adBuf + p + 4);
                extPartRef = RdU16LE(adBuf + p + 8);
            } else {
                extLba     = RdU32LE(adBuf + p + 12);
                extPartRef = RdU16LE(adBuf + p + 16);
            }
            p += adStep;

            if (extTp == 3u) {
                // Allocation Extent Descriptor (tagId == 258) continuation block
                uint64_t aedOff = 0;
                bool okRes = icbIsPhysicalPartNum
                    ? ctx.ResolvePhysBlockToDisk(extPartRef, extLba, aedOff)
                    : ctx.ResolveLogicalBlockToDisk(extPartRef, extLba, aedOff);
                if (!okRes || aedOff + ctx.logBlkSize > ctx.reader->virtualSize) return false;
                std::vector<uint8_t> aed(ctx.logBlkSize);
                if (!ctx.reader->readBytes(aedOff, aed.data(), aed.size())) return false;
                if (!ValidateUdfTag(aed.data(), aed.size(), 258u) || aed.size() < 24u) return false;
                uint32_t nextLenAD = RdU32LE(aed.data() + 20);
                if (nextLenAD > aed.size() - 24u) return false;
                if (!self(self, aed.data() + 24, nextLenAD, extPartRef, aedDepth + 1)) return false;
                break;
            }

            if (extTp == 1u || extTp == 2u) {
                // Unrecorded / sparse extent -> zero-filled
                outIcb.runs.push_back({ true, 0, extLen });
            } else {
                if (!icbIsPhysicalPartNum && extPartRef < ctx.partRefs.size() && ctx.partRefs[extPartRef].isMetadataPart) {
                    // Split across logical blocks in Metadata Partition
                    uint32_t rem = extLen;
                    uint32_t curB = extLba;
                    while (rem > 0u) {
                        uint32_t chunk = std::min<uint32_t>(rem, ctx.logBlkSize);
                        uint64_t dOff = 0;
                        if (!ctx.ResolveLogicalBlockToDisk(extPartRef, curB, dOff)) return false;
                        outIcb.runs.push_back({ false, dOff, chunk });
                        rem -= chunk;
                        ++curB;
                    }
                } else {
                    uint64_t dOff = 0;
                    bool okRes = icbIsPhysicalPartNum
                        ? ctx.ResolvePhysBlockToDisk(extPartRef, extLba, dOff)
                        : ctx.ResolveLogicalBlockToDisk(extPartRef, extLba, dOff);
                    if (!okRes) return false;
                    outIcb.runs.push_back({ false, dOff, extLen });
                }
            }
        }
        return true;
    };

    return appendAdsFromBuffer(appendAdsFromBuffer, fe.data() + allocOff, lenAD, icbPartRef, 0);
}

inline bool ReadUdfIcbContent(
    const UdfVolumeContext& ctx,
    const UdfParsedIcb& icb,
    std::vector<uint8_t>& outBytes)
{
    outBytes.clear();
    if (icb.infoLength == 0) return true;
    if (icb.infoLength > 128ULL * 1024 * 1024) return false;
    if (icb.isInline) {
        outBytes = icb.inlineBytes;
        return outBytes.size() == icb.infoLength;
    }

    outBytes.reserve(static_cast<size_t>(icb.infoLength));
    for (const auto& r : icb.runs) {
        if (outBytes.size() >= icb.infoLength) break;
        size_t take = static_cast<size_t>(std::min<uint64_t>(r.byteLen, icb.infoLength - outBytes.size()));
        size_t prev = outBytes.size();
        outBytes.resize(prev + take, 0);
        if (!r.isSparseZero) {
            if (r.diskByteOff > ctx.reader->virtualSize || take > ctx.reader->virtualSize - r.diskByteOff) {
                outBytes.clear();
                return false;
            }
            if (!ctx.reader->readBytes(r.diskByteOff, outBytes.data() + prev, take)) {
                outBytes.clear();
                return false;
            }
        }
    }
    if (outBytes.size() < icb.infoLength) {
        outBytes.resize(static_cast<size_t>(icb.infoLength), 0);
    }
    return true;
}

inline bool ParseOrExtractUdfOnReader(
    const DetailArc100::VirtualDiskReader& reader,
    uint64_t partBase,
    const std::wstring& prefixDir,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm = nullptr,
    std::vector<uint8_t>* outExtracted = nullptr)
{
    if (!reader.readBytes || partBase >= reader.virtualSize) return false;
    const uint64_t availBytes = reader.virtualSize - partBase;
    if (availBytes < 65536ULL) return false;

    // Check ECMA-167 Volume Recognition Sequence at byte 32768 (16 * 2048)
    bool sawBea = false, sawNsr = false, sawTea = false;
    uint16_t nsrVer = 0;
    for (uint32_t i = 0; i < 32u; ++i) {
        uint64_t vrsOff = partBase + 32768ULL + static_cast<uint64_t>(i) * 2048ULL;
        if (vrsOff + 7ULL > reader.virtualSize) break;
        uint8_t vrs[7] = {};
        if (!reader.readBytes(vrsOff, vrs, sizeof(vrs))) break;
        if (std::memcmp(vrs + 1, "BEA01", 5) == 0) sawBea = true;
        else if (std::memcmp(vrs + 1, "NSR02", 5) == 0) { sawNsr = true; nsrVer = 0x0150; }
        else if (std::memcmp(vrs + 1, "NSR03", 5) == 0) { sawNsr = true; nsrVer = 0x0200; }
        else if (std::memcmp(vrs + 1, "TEA01", 5) == 0) { sawTea = true; if (sawNsr) break; }
    }
    (void)sawBea;
    (void)sawTea;

    // Locate Anchor Volume Descriptor Pointer (tagId == 2) across 2048B or 512B sector sizes
    static constexpr uint32_t kCandidateSecSizes[2] = { 2048u, 512u };
    uint32_t chosenSecSize = 0;
    uint32_t mvdsLen = 0, mvdsLoc = 0;
    uint32_t rvdsLen = 0, rvdsLoc = 0;

    for (uint32_t secSz : kCandidateSecSizes) {
        uint64_t totalSectors = availBytes / secSz;
        if (totalSectors < 32ULL) continue;
        uint64_t probes[4] = {
            256ULL,
            (totalSectors > 256ULL) ? (totalSectors - 256ULL) : 0ULL,
            totalSectors - 1ULL,
            512ULL
        };
        uint8_t avdp[512] = {};
        for (uint64_t pSec : probes) {
            if (pSec == 0ULL || pSec >= totalSectors) continue;
            uint64_t off = partBase + pSec * secSz;
            if (off + 512ULL > reader.virtualSize) continue;
            if (!reader.readBytes(off, avdp, 512)) continue;
            if (ValidateUdfTag(avdp, 512, 2u) && RdU32LE(avdp + 12) == static_cast<uint32_t>(pSec)) {
                chosenSecSize = secSz;
                mvdsLen = RdU32LE(avdp + 16);
                mvdsLoc = RdU32LE(avdp + 20);
                rvdsLen = RdU32LE(avdp + 24);
                rvdsLoc = RdU32LE(avdp + 28);
                break;
            }
        }
        if (chosenSecSize != 0u) break;
    }

    if (chosenSecSize == 0u) return false;
    if (!sawNsr && chosenSecSize == 2048u) {
        // Accept AVDP-only UDF images (e.g. mkudffs hard-disk/partition images without optical VRS),
        // while still requiring valid MVDS + FSD + Root ICB below!
    }

    UdfVolumeContext ctx;
    ctx.reader     = &reader;
    ctx.partBase   = partBase;
    ctx.sectorSize = chosenSecSize;
    ctx.logBlkSize = chosenSecSize;
    ctx.udfRevBcd  = (nsrVer != 0) ? nsrVer : 0x0102;

    UdfLongAd fsdAd{};
    bool hasLvd = false;

    auto parseVdsSequence = [&](uint32_t seqLoc, uint32_t seqByteLen) -> bool {
        if (seqLoc == 0u || seqByteLen < chosenSecSize) return false;
        uint32_t countSecs = std::min<uint32_t>(seqByteLen / chosenSecSize, 256u);
        std::vector<uint8_t> secBuf(std::max<uint32_t>(chosenSecSize, 2048u));

        for (uint32_t s = 0; s < countSecs; ++s) {
            uint64_t dOff = partBase + static_cast<uint64_t>(seqLoc + s) * chosenSecSize;
            if (dOff + chosenSecSize > reader.virtualSize) break;
            if (!reader.readBytes(dOff, secBuf.data(), chosenSecSize)) break;
            if (!ValidateUdfTag(secBuf.data(), chosenSecSize)) continue;

            uint16_t tid = RdU16LE(secBuf.data() + 0);
            if (tid == 8u) break; // Terminating Descriptor

            if (tid == 5u && chosenSecSize >= 196u) { // Partition Descriptor
                uint16_t pNum   = RdU16LE(secBuf.data() + 22);
                uint32_t pStart = RdU32LE(secBuf.data() + 188);
                uint32_t pLen   = RdU32LE(secBuf.data() + 192);
                ctx.physParts[pNum] = { pStart, pLen };
            } else if (tid == 6u) { // Logical Volume Descriptor
                std::vector<uint8_t> lvd = secBuf;
                if (chosenSecSize < 512u) {
                    lvd.resize(512u, 0);
                    if (dOff + 512u <= reader.virtualSize) reader.readBytes(dOff, lvd.data(), 512u);
                }
                uint32_t lbs = RdU32LE(lvd.data() + 212);
                if (lbs < 512u || lbs > 65536u || (lbs & (lbs - 1u)) != 0u) continue;
                ctx.logBlkSize = lbs;
                ctx.volLabel   = DecodeOstaDstringField(lvd.data() + 84, 128);
                uint16_t domRev = RdU16LE(lvd.data() + 240);
                if (domRev >= 0x0100u && domRev <= 0x0300u) ctx.udfRevBcd = domRev;

                fsdAd = ReadUdfLongAd(lvd.data() + 248);
                uint32_t mapTblLen = RdU32LE(lvd.data() + 264);
                uint32_t numMaps   = RdU32LE(lvd.data() + 268);
                if (mapTblLen > 16384u || numMaps > 64u) continue;

                if (440u + mapTblLen > lvd.size()) {
                    size_t need = 440u + mapTblLen;
                    if (dOff + need > reader.virtualSize) continue;
                    lvd.resize(need);
                    if (!reader.readBytes(dOff, lvd.data(), need)) continue;
                }

                ctx.partRefs.clear();
                size_t mpOff = 440u;
                for (uint32_t m = 0; m < numMaps && mpOff + 2u <= 440u + mapTblLen; ++m) {
                    uint8_t pmType = lvd[mpOff + 0];
                    uint8_t pmLen  = lvd[mpOff + 1];
                    if (pmLen < 6u || mpOff + pmLen > 440u + mapTblLen) break;

                    UdfVolumeContext::PartRefMapping mapping;
                    if (pmType == 1u && pmLen >= 6u) {
                        mapping.physPartNum = RdU16LE(lvd.data() + mpOff + 4);
                    } else if (pmType == 2u && pmLen >= 64u) {
                        std::string_view ident(reinterpret_cast<const char*>(lvd.data() + mpOff + 5),
                                               strnlen(reinterpret_cast<const char*>(lvd.data() + mpOff + 5), 23));
                        mapping.physPartNum = RdU16LE(lvd.data() + mpOff + 38);
                        if (ident.find("*UDF Metadata Partition") != std::string_view::npos) {
                            mapping.isMetadataPart = true;
                            mapping.metaFileLba    = RdU32LE(lvd.data() + mpOff + 40);
                            mapping.metaMirrorLba  = RdU32LE(lvd.data() + mpOff + 44);
                        }
                    }
                    ctx.partRefs.push_back(std::move(mapping));
                    mpOff += pmLen;
                }
                hasLvd = true;
            }
        }
        return hasLvd && !ctx.physParts.empty();
    };

    if (!parseVdsSequence(mvdsLoc, mvdsLen)) {
        if (!parseVdsSequence(rvdsLoc, rvdsLen)) return false;
    }

    // Resolve any UDF 2.50/2.60 Metadata Partitions via their Metadata File (or Mirror) ICB
    for (auto& pm : ctx.partRefs) {
        if (!pm.isMetadataPart) continue;
        UdfParsedIcb metaIcb;
        bool okMeta = (pm.metaFileLba != 0xFFFFFFFFu &&
                       ParseUdfIcbAt(ctx, pm.physPartNum, pm.metaFileLba, /*icbIsPhysicalPartNum=*/true, metaIcb));
        if (!okMeta && pm.metaMirrorLba != 0xFFFFFFFFu) {
            okMeta = ParseUdfIcbAt(ctx, pm.physPartNum, pm.metaMirrorLba, /*icbIsPhysicalPartNum=*/true, metaIcb);
        }
        if (!okMeta || metaIcb.runs.empty()) return false;
        pm.metaFileRuns = std::move(metaIcb.runs);
        pm.metaFileSize = metaIcb.infoLength;
    }

    // Read File Set Descriptor (tagId == 256)
    std::vector<uint8_t> fsd;
    if (!ctx.ReadLogicalBytes(fsdAd.partRef, fsdAd.lba, std::max<uint32_t>(ctx.logBlkSize, 512u), fsd)) return false;
    if (fsd.size() < 416u || !ValidateUdfTag(fsd.data(), fsd.size(), 256u)) return false;
    UdfLongAd rootIcbAd = ReadUdfLongAd(fsd.data() + 400);

    std::wstring wantNorm = extractTargetNorm ? ArchivePath::Normalize(*extractTargetNorm) : std::wstring{};
    std::unordered_set<uint64_t> visitedDirs;
    size_t totalListed = 0;
    bool extractedOk = false;

    auto walkUdfDir = [&](auto& self, uint16_t dirPartRef, uint32_t dirLba, const std::wstring& curPrefix, int depth) -> bool {
        if (depth > 32 || totalListed >= 50000u) return true;
        uint64_t dirKey = (static_cast<uint64_t>(dirPartRef) << 32u) | dirLba;
        if (!visitedDirs.insert(dirKey).second) return true;

        UdfParsedIcb dirIcb;
        if (!ParseUdfIcbAt(ctx, dirPartRef, dirLba, false, dirIcb)) return false;
        if (dirIcb.fileType != 4u && dirIcb.fileType != 248u) return false;

        std::vector<uint8_t> dirData;
        if (!ReadUdfIcbContent(ctx, dirIcb, dirData)) return false;

        size_t pos = 0;
        while (pos + 38u <= dirData.size() && totalListed < 50000u) {
            const uint8_t* fid = dirData.data() + pos;
            if (!ValidateUdfTag(fid, dirData.size() - pos, 257u)) {
                // Advance to next logical block boundary if block-padded
                size_t nextBlk = ((pos / ctx.logBlkSize) + 1u) * ctx.logBlkSize;
                if (nextBlk <= pos || nextBlk >= dirData.size()) break;
                pos = nextBlk;
                continue;
            }

            uint8_t  fileChars = fid[18];
            uint8_t  lFi       = fid[19];
            UdfLongAd childAd  = ReadUdfLongAd(fid + 20);
            uint16_t lIu       = RdU16LE(fid + 36);
            size_t   fidLen    = (38u + static_cast<size_t>(lIu) + static_cast<size_t>(lFi) + 3u) & ~size_t(3u);
            if (fidLen < 40u || pos + 38u + lIu + lFi > dirData.size()) break;
            pos += std::min<size_t>(fidLen, dirData.size() - pos);

            // Skip deleted (0x04) and parent ".." (0x08) descriptors
            if ((fileChars & 0x04u) != 0u || (fileChars & 0x08u) != 0u || lFi == 0u) continue;

            std::wstring wName = DecodeOstaCompressedUnicode(fid + 38u + lIu, lFi);
            if (wName.empty() || wName == L"." || wName == L"..") continue;

            std::wstring fullRel = ArchivePath::Normalize(curPrefix.empty() ? wName : (curPrefix + L"/" + wName));
            if (fullRel.empty()) continue;

            const bool isDir = (fileChars & 0x02u) != 0u;
            if (isDir) {
                if (outInfo && !extractTargetNorm) {
                    UdfParsedIcb subIcb;
                    ArchiveItemEntry item;
                    item.pathInArchive = fullRel;
                    item.name = ArchivePath::BaseName(fullRel);
                    item.isDirectory = true;
                    item.compressionMethod = 0;
                    if (ParseUdfIcbAt(ctx, childAd.partRef, childAd.lba, false, subIcb)) {
                        item.dosDate = subIcb.dosDate;
                        item.dosTime = subIcb.dosTime;
                    }
                    outInfo->entries.push_back(std::move(item));
                    ++totalListed;
                }
                self(self, childAd.partRef, childAd.lba, fullRel, depth + 1);
                if (extractedOk) return true;
                continue;
            }

            UdfParsedIcb fileIcb;
            if (!ParseUdfIcbAt(ctx, childAd.partRef, childAd.lba, false, fileIcb)) continue;

            if (extractTargetNorm && outExtracted) {
                if (_wcsicmp(fullRel.c_str(), wantNorm.c_str()) == 0) {
                    extractedOk = ReadUdfIcbContent(ctx, fileIcb, *outExtracted);
                    return extractedOk;
                }
            } else if (outInfo) {
                ArchiveItemEntry item;
                item.pathInArchive = fullRel;
                item.name = ArchivePath::BaseName(fullRel);
                item.isDirectory = false;
                item.uncompressedSize = fileIcb.infoLength;
                item.compressedSize   = fileIcb.infoLength;
                item.dosDate = fileIcb.dosDate;
                item.dosTime = fileIcb.dosTime;
                if (reader.directRawBuffer && !fileIcb.isInline &&
                    fileIcb.runs.size() == 1u && !fileIcb.runs[0].isSparseZero &&
                    fileIcb.runs[0].byteLen >= fileIcb.infoLength)
                {
                    item.rawDataOffset = fileIcb.runs[0].diskByteOff;
                    item.compressionMethod = 0;
                } else {
                    item.compressionMethod = 1; // Inline ICB, multi-extent, sparse, or NRG-wrapped
                }
                outInfo->entries.push_back(std::move(item));
                ++totalListed;
            }
        }
        return true;
    };

    const std::wstring normPrefix = ArchivePath::Normalize(prefixDir);
    if (!walkUdfDir(walkUdfDir, rootIcbAd.partRef, rootIcbAd.lba, normPrefix, 0)) {
        if (extractTargetNorm) return false;
    }
    if (extractTargetNorm) return extractedOk;
    if (!outInfo) return false;

    wchar_t revStr[32] = {};
    swprintf_s(revStr, L"%u.%02X", (ctx.udfRevBcd >> 8u) & 0xFFu, ctx.udfRevBcd & 0xFFu);
    outInfo->formatName = L"UDF " + std::wstring(revStr) + L" Optical Image";
    if (!ctx.volLabel.empty()) {
        outInfo->formatName += L" (" + ctx.volLabel + L")";
    }
    outInfo->valid = true;
    return true;
}

} // namespace DetailUdfNrg

inline bool ParseOrExtractUdfImage(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm,
    std::vector<uint8_t>* outExtracted)
{
    if (extLower == L".nrg") return false;

    // 2. Standalone UDF Image (.udf, or pure UDF optical image without ISO 9660 PVD)
    const bool hasIsoPvd = (size >= 0x8800u && data[0x8000u] == 1u && std::memcmp(data + 0x8001u, "CD001", 5) == 0);
    if (extLower == L".iso" && hasIsoPvd) return false;
    bool likelyUdf = (extLower == L".udf");
    if (!likelyUdf && size >= 65536u) {
        // Quick signature check at 32 KB VRS ("NSR02" / "NSR03") or sector 256 AVDP (tagId == 2)
        for (size_t v = 32768u; v + 2048u <= std::min<size_t>(size, 98304u); v += 2048u) {
            if (std::memcmp(data + v + 1u, "NSR02", 5) == 0 || std::memcmp(data + v + 1u, "NSR03", 5) == 0) {
                likelyUdf = true;
                break;
            }
        }
        if (!likelyUdf && size >= 257u * 2048u && DetailUdfNrg::ValidateUdfTag(data + 256u * 2048u, 512u, 2u) &&
            DetailUdfNrg::RdU32LE(data + 256u * 2048u + 12u) == 256u)
        {
            likelyUdf = true;
        }
    }
    if (!likelyUdf) return false;

    DetailArc100::VirtualDiskReader rawUdf;
    rawUdf.virtualSize = size;
    rawUdf.formatName  = L"UDF Optical Image";
    rawUdf.directRawBuffer = true;
    rawUdf.readBytes = [data, size](uint64_t off, uint8_t* dst, size_t len) -> bool {
        if (off > size || len > size - static_cast<size_t>(off)) return false;
        std::memcpy(dst, data + static_cast<size_t>(off), len);
        return true;
    };
    return DetailUdfNrg::ParseOrExtractUdfOnReader(rawUdf, 0, L"", outInfo, extractTargetNorm, outExtracted);
}

} // namespace Explorer2::Formats
