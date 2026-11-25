#pragma once
// NtfsFilesystemImg.h — NTFS $MFT Inspector & Resident / Non-Resident File Extractor (.ntfs, .img).
// Walks $MFT Record 0's own 0x80 $DATA runlist so fragmented MFTs and records beyond 2,048 are resolved.

#include "../common/VirtualDiskCommon.h"

namespace Explorer2::Formats {

namespace DetailDiskAndGameArc {

inline void DecodeNtfsDataRuns(
    const uint8_t* runPtr,
    size_t runMaxBytes,
    uint64_t partBase,
    uint32_t clusterBytes,
    std::vector<std::pair<uint64_t, uint64_t>>& outRuns)
{
    outRuns.clear();
    int64_t curLcn = 0;
    size_t rp = 0;
    while (rp < runMaxBytes && runPtr[rp] != 0u && outRuns.size() < 32768u) {
        const uint8_t hdr  = runPtr[rp++];
        const uint8_t lenB = hdr & 0x0Fu;
        const uint8_t offB = (hdr >> 4u) & 0x0Fu;
        if (lenB == 0u || lenB > 8u || offB > 8u || static_cast<size_t>(lenB) + offB > runMaxBytes - rp) break;

        uint64_t rLen = 0;
        for (uint8_t b = 0; b < lenB; ++b) {
            rLen |= static_cast<uint64_t>(runPtr[rp++]) << (b * 8u);
        }
        int64_t rDelta = 0;
        for (uint8_t b = 0; b < offB; ++b) {
            rDelta |= static_cast<int64_t>(static_cast<uint64_t>(runPtr[rp++]) << (b * 8u));
        }
        if (offB > 0u && (runPtr[rp - 1u] & 0x80u) != 0u) {
            for (uint8_t b = offB; b < 8u; ++b) {
                rDelta |= static_cast<int64_t>(0xFFULL << (b * 8u));
            }
        }
        if (rLen == 0u || rLen > (1ULL << 40)) break;

        if (offB == 0u) { // Sparse run
            outRuns.push_back({ UINT64_MAX, rLen * clusterBytes });
        } else {
            curLcn += rDelta;
            if (curLcn >= 0) {
                outRuns.push_back({ partBase + static_cast<uint64_t>(curLcn) * clusterBytes, rLen * clusterBytes });
            }
        }
    }
}

inline bool ReadNtfsRunlistSlice(
    const VirtualDiskReader& reader,
    const std::vector<std::pair<uint64_t, uint64_t>>& runs,
    uint64_t logicalOff,
    uint8_t* dst,
    size_t len)
{
    if (len == 0) return true;
    uint64_t curLog = 0;
    size_t done = 0;
    for (const auto& r : runs) {
        if (done >= len) break;
        const uint64_t reqOff = logicalOff + done;
        if (reqOff >= curLog && reqOff < curLog + r.second) {
            const uint64_t rel = reqOff - curLog;
            const size_t take = static_cast<size_t>(std::min<uint64_t>(len - done, r.second - rel));
            if (r.first == UINT64_MAX) {
                std::memset(dst + done, 0, take);
            } else {
                const uint64_t diskOff = r.first + rel;
                if (diskOff > reader.virtualSize || take > reader.virtualSize - diskOff) return false;
                if (!reader.readBytes(diskOff, dst + done, take)) return false;
            }
            done += take;
        }
        curLog += r.second;
    }
    return done == len;
}

inline bool ApplyNtfsUsaFixups(std::vector<uint8_t>& recBuf, uint32_t recSize) noexcept {
    if (recBuf.size() < 48 || std::memcmp(recBuf.data(), "FILE", 4) != 0) return false;
    const uint16_t usaOff   = RdU16LE(recBuf.data() + 4);
    const uint16_t usaCount = RdU16LE(recBuf.data() + 6);
    if (usaCount >= 2u && static_cast<size_t>(usaOff) + static_cast<size_t>(usaCount) * 2u <= recSize) {
        for (uint16_t s = 1; s < usaCount && static_cast<size_t>(s) * 512u <= recSize; ++s) {
            recBuf[s * 512u - 2u] = recBuf[usaOff + s * 2u];
            recBuf[s * 512u - 1u] = recBuf[usaOff + s * 2u + 1u];
        }
    }
    return true;
}

inline bool InspectOrExtractNtfsOnReader(
    const VirtualDiskReader& reader,
    uint64_t partBase,
    uint64_t partBytes,
    const std::wstring& prefixDir,
    ArchiveSummaryInfo& outInfo,
    const std::wstring* extractTarget,
    std::vector<uint8_t>* outExtracted)
{
    if (partBytes < 4096 || partBase > reader.virtualSize || 512ULL > reader.virtualSize - partBase) return false;
    uint8_t bpb[512] = {};
    if (!reader.readBytes(partBase, bpb, 512) || std::memcmp(bpb + 3, "NTFS    ", 8) != 0) return false;

    const uint16_t bps = RdU16LE(bpb + 11);
    const uint8_t  spc = bpb[13];
    const uint64_t mftLcn = RdU64LE(bpb + 48);
    const int8_t   recSpec = static_cast<int8_t>(bpb[64]);
    if (bps < 512 || bps > 4096 || (bps & (bps - 1u)) != 0 || spc == 0) return false;
    const uint32_t clusterBytes = static_cast<uint32_t>(bps) * spc;
    if (recSpec < -16 || recSpec == 0) return false;
    const uint32_t recSize = (recSpec < 0)
        ? (1u << static_cast<uint32_t>(-recSpec))
        : (static_cast<uint32_t>(recSpec) * clusterBytes);
    if (recSize < 512 || recSize > 8192) return false;

    if (mftLcn > (reader.virtualSize - partBase) / clusterBytes) return false;
    const uint64_t mftBase = partBase + mftLcn * clusterBytes;
    if (recSize > reader.virtualSize - mftBase) return false;

    struct NtfsRec {
        uint64_t parentIdx = 5;
        std::wstring name;
        uint8_t nameSpace = 255;
        uint16_t dosDate = 0;
        uint16_t dosTime = 0;
        bool isDir = false;
        bool valid = false;
        uint64_t fileSize = 0;
        bool resident = false;
        std::vector<uint8_t> residentData;
        std::vector<std::pair<uint64_t, uint64_t>> runs;
    };

    // Inspect Record 0 ($MFT) to extract its own 0x80 $DATA runlist so records past 2,048 & fragmented MFTs work!
    std::vector<std::pair<uint64_t, uint64_t>> mftRuns;
    uint64_t mftByteLen = 0;
    std::vector<uint8_t> recBuf(recSize);
    if (reader.readBytes(mftBase, recBuf.data(), recSize) && ApplyNtfsUsaFixups(recBuf, recSize)) {
        const uint16_t flags0 = RdU16LE(recBuf.data() + 22);
        const uint16_t attrOff0 = RdU16LE(recBuf.data() + 20);
        if ((flags0 & 0x01u) != 0u) {
            size_t ap = attrOff0;
            while (ap + 16u <= recSize) {
                const uint32_t attrType = RdU32LE(recBuf.data() + ap);
                if (attrType == 0xFFFFFFFFu) break;
                const uint32_t attrLen = RdU32LE(recBuf.data() + ap + 4);
                if (attrLen < 16u || attrLen > recSize - ap) break;
                if (attrType == 0x80u && recBuf[ap + 8] == 1u && recBuf[ap + 9] == 0u && attrLen >= 64u) {
                    const uint64_t dataSz = RdU64LE(recBuf.data() + ap + 48);
                    const uint64_t allocSz = RdU64LE(recBuf.data() + ap + 40);
                    const uint16_t runOff = RdU16LE(recBuf.data() + ap + 32);
                    if (runOff < attrLen) {
                        DecodeNtfsDataRuns(recBuf.data() + ap + runOff, attrLen - runOff, partBase, clusterBytes, mftRuns);
                        mftByteLen = (dataSz > 0) ? dataSz : allocSz;
                        break;
                    }
                }
                ap += attrLen;
            }
        }
    }

    const bool useMftRunlist = !mftRuns.empty() && mftByteLen >= recSize;
    const uint32_t maxRecs = useMftRunlist
        ? static_cast<uint32_t>(std::min<uint64_t>(mftByteLen / recSize, 65536ULL))
        : static_cast<uint32_t>(std::min<uint64_t>((reader.virtualSize - mftBase) / recSize, 16384ULL));

    std::unordered_map<uint64_t, NtfsRec> records;
    uint32_t consecutiveNonFile = 0;

    for (uint32_t idx = 0; idx < maxRecs; ++idx) {
        bool readOk = false;
        if (useMftRunlist) {
            readOk = ReadNtfsRunlistSlice(reader, mftRuns, static_cast<uint64_t>(idx) * recSize, recBuf.data(), recSize);
        } else {
            const uint64_t rOff = mftBase + static_cast<uint64_t>(idx) * recSize;
            readOk = (rOff <= reader.virtualSize && recSize <= reader.virtualSize - rOff) &&
                     reader.readBytes(rOff, recBuf.data(), recSize);
        }
        if (!readOk) break;

        if (!ApplyNtfsUsaFixups(recBuf, recSize)) {
            if (!useMftRunlist && idx > 24 && (records.empty() || ++consecutiveNonFile > 256)) break;
            continue;
        }
        consecutiveNonFile = 0;

        const uint16_t flags = RdU16LE(recBuf.data() + 22);
        if ((flags & 0x01u) == 0u) continue; // Not in use

        const uint16_t attrOff = RdU16LE(recBuf.data() + 20);
        NtfsRec nr;
        nr.isDir = (flags & 0x02u) != 0u;

        size_t ap = attrOff;
        while (ap + 16u <= recSize) {
            const uint32_t attrType = RdU32LE(recBuf.data() + ap);
            if (attrType == 0xFFFFFFFFu) break;
            const uint32_t attrLen = RdU32LE(recBuf.data() + ap + 4);
            if (attrLen < 16u || attrLen > recSize - ap) break;
            const uint8_t nonRes  = recBuf[ap + 8];
            const uint8_t nameLen = recBuf[ap + 9];

            if (attrType == 0x30u && nonRes == 0u && attrLen >= 24u) { // $FILE_NAME
                const uint32_t valLen = RdU32LE(recBuf.data() + ap + 16);
                const uint16_t valOff = RdU16LE(recBuf.data() + ap + 20);
                if (valOff <= attrLen && valLen <= attrLen - valOff && valLen >= 66u) {
                    const uint8_t* fn = recBuf.data() + ap + valOff;
                    const uint64_t pRef = RdU64LE(fn) & 0x0000FFFFFFFFFFFFULL;
                    const uint64_t modFt = RdU64LE(fn + 24);
                    const uint8_t fnChars = fn[64];
                    const uint8_t fnSpace = fn[65];
                    if (66u + static_cast<size_t>(fnChars) * 2u <= valLen && fnChars > 0u) {
                        if (nr.name.empty() || (nr.nameSpace == 2u && fnSpace != 2u)) {
                            nr.parentIdx = pRef;
                            nr.nameSpace = fnSpace;
                            FileTimeU64ToDos(modFt, nr.dosDate, nr.dosTime);
                            nr.name.resize(fnChars);
                            for (uint8_t c = 0; c < fnChars; ++c) {
                                nr.name[c] = static_cast<wchar_t>(RdU16LE(fn + 66u + c * 2u));
                            }
                            nr.valid = true;
                        }
                    }
                }
            } else if (attrType == 0x80u && nameLen == 0u) { // Unnamed $DATA
                if (nonRes == 0u && attrLen >= 24u) {
                    const uint32_t valLen = RdU32LE(recBuf.data() + ap + 16);
                    const uint16_t valOff = RdU16LE(recBuf.data() + ap + 20);
                    if (valOff <= attrLen && valLen <= attrLen - valOff) {
                        nr.fileSize = valLen;
                        nr.resident = true;
                        nr.residentData.assign(recBuf.data() + ap + valOff, recBuf.data() + ap + valOff + valLen);
                    }
                } else if (nonRes == 1u && attrLen >= 64u) {
                    const uint64_t startVcn = RdU64LE(recBuf.data() + ap + 16);
                    if (startVcn == 0ULL) {
                        nr.fileSize = RdU64LE(recBuf.data() + ap + 48);
                    }
                    const uint16_t runOff = RdU16LE(recBuf.data() + ap + 32);
                    if (runOff < attrLen) {
                        std::vector<std::pair<uint64_t, uint64_t>> segRuns;
                        DecodeNtfsDataRuns(recBuf.data() + ap + runOff, attrLen - runOff, partBase, clusterBytes, segRuns);
                        nr.runs.insert(nr.runs.end(), segRuns.begin(), segRuns.end());
                    }
                }
            }
            ap += attrLen;
        }
        if (nr.valid) records[idx] = std::move(nr);
    }

    auto buildFullNtfsPath = [&](uint64_t idx) -> std::wstring {
        std::vector<std::wstring> parts;
        uint64_t cur = idx;
        for (int depth = 0; depth < 24 && cur != 5; ++depth) {
            auto it = records.find(cur);
            if (it == records.end() || it->second.name.empty() || it->second.name == L".") break;
            parts.push_back(it->second.name);
            if (it->second.parentIdx == cur) break;
            cur = it->second.parentIdx;
        }
        std::wstring out = prefixDir;
        for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
            out = out.empty() ? *it : (out + L"/" + *it);
        }
        return ArchivePath::Normalize(out);
    };

    bool hasUserFiles = false;
    for (const auto& kv : records) {
        if (kv.first >= 24 && !kv.second.name.empty() && kv.second.name[0] != L'$') {
            hasUserFiles = true;
            break;
        }
    }

    std::vector<uint64_t> sortedIds;
    sortedIds.reserve(records.size());
    for (const auto& kv : records) sortedIds.push_back(kv.first);
    std::sort(sortedIds.begin(), sortedIds.end());

    for (uint64_t id : sortedIds) {
        if (id == 5) continue;
        const auto& rec = records[id];
        if (hasUserFiles && (!rec.name.empty() && rec.name[0] == L'$')) continue;
        std::wstring fullRel = buildFullNtfsPath(id);
        if (fullRel.empty()) continue;

        if (extractTarget && outExtracted) {
            if (!rec.isDir && PathEqualsCI(fullRel, *extractTarget)) {
                if (rec.resident) {
                    *outExtracted = rec.residentData;
                    return true;
                }
                if (rec.fileSize <= 128 * 1024 * 1024) {
                    outExtracted->resize(static_cast<size_t>(rec.fileSize), 0);
                    if (rec.fileSize == 0) return true;
                    if (!ReadNtfsRunlistSlice(reader, rec.runs, 0, outExtracted->data(), outExtracted->size())) {
                        outExtracted->clear();
                        return false;
                    }
                    return true;
                }
                return false;
            }
        } else {
            ArchiveItemEntry item;
            item.pathInArchive    = fullRel;
            item.name             = ArchivePath::BaseName(fullRel);
            item.isDirectory      = rec.isDir;
            item.uncompressedSize = rec.isDir ? 0 : rec.fileSize;
            item.compressedSize   = item.uncompressedSize;
            item.dosDate          = rec.dosDate;
            item.dosTime          = rec.dosTime;
            item.compressionMethod = 1;
            outInfo.entries.push_back(std::move(item));
        }
    }
    return extractTarget ? false : !outInfo.entries.empty();
}

} // namespace DetailDiskAndGameArc

inline bool InspectNtfsFilesystemImg(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (!data || size < 4096 || std::memcmp(data + 3, "NTFS    ", 8) != 0) return false;
    if (DetailDiskAndGameArc::IsKnownVirtualDiskContainerMagic(data, size)) return false;
    DetailDiskAndGameArc::VirtualDiskReader raw = DetailDiskAndGameArc::MakeRawMemoryDiskReader(data, size, L"NTFS Filesystem Image (.ntfs)", false);
    outInfo = {};
    outInfo.formatName = L"NTFS Filesystem Image (.ntfs)";
    if (!DetailDiskAndGameArc::InspectOrExtractNtfsOnReader(raw, 0, size, L"", outInfo, nullptr, nullptr)) {
        outInfo = {};
        return false;
    }
    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

inline bool ExtractNtfsFilesystemImgEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    outBytes.clear();
    if (!data || size < 4096 || normTarget.empty() || std::memcmp(data + 3, "NTFS    ", 8) != 0) return false;
    if (DetailDiskAndGameArc::IsKnownVirtualDiskContainerMagic(data, size)) return false;
    DetailDiskAndGameArc::VirtualDiskReader raw = DetailDiskAndGameArc::MakeRawMemoryDiskReader(data, size, L"NTFS Filesystem Image (.ntfs)", false);
    ArchiveSummaryInfo dummy;
    return DetailDiskAndGameArc::InspectOrExtractNtfsOnReader(raw, 0, size, L"", dummy, &normTarget, &outBytes);
}

} // namespace Explorer2::Formats
