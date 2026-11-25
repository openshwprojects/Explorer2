#pragma once
// Iso9660.h — ISO 9660 / Joliet UTF-16BE / Rock Ridge POSIX NM optical disc image reader & extractor (.iso).

#include "../common/VirtualDiskCommon.h"

namespace Explorer2::Formats {

namespace DetailDiskAndGameArc {

inline std::wstring DecodeIsoEntryName(const uint8_t* rec, uint8_t recLen, bool isJoliet) {
    uint8_t nameLen = rec[32];
    if (33u + nameLen > recLen || nameLen == 0) return L"";
    const uint8_t* namePtr = rec + 33;
    if (nameLen == 1 && (namePtr[0] == 0x00 || namePtr[0] == 0x01)) return L"";

    // Check Rock Ridge System Use Area for POSIX "NM" alternate name
    size_t suaOff = 33u + nameLen + ((nameLen & 1u) == 0 ? 1u : 0u);
    std::string rrName;
    while (suaOff + 4 <= recLen) {
        uint8_t suLen = rec[suaOff + 2];
        if (suLen < 4 || suaOff + suLen > recLen) break;
        if (rec[suaOff] == 'N' && rec[suaOff + 1] == 'M' && suLen > 5) {
            uint8_t nmFlags = rec[suaOff + 4];
            if ((nmFlags & 0x06u) == 0) {
                rrName.append(reinterpret_cast<const char*>(rec + suaOff + 5), suLen - 5);
            }
        }
        suaOff += suLen;
    }
    if (!rrName.empty()) {
        return PathUtils::Utf8ToWide(rrName);
    }

    std::wstring wname;
    if (isJoliet && (nameLen & 1u) == 0) {
        for (uint8_t i = 0; i + 1 < nameLen; i += 2) {
            wchar_t wc = static_cast<wchar_t>(RdU16BE(namePtr + i));
            if (wc == 0) break;
            wname.push_back(wc);
        }
    } else {
        std::string rawName(reinterpret_cast<const char*>(namePtr), nameLen);
        wname = PathUtils::Utf8ToWide(rawName);
    }

    size_t semi = wname.find(L';');
    if (semi != std::wstring::npos) wname.resize(semi);
    if (!wname.empty() && wname.back() == L'.') wname.pop_back();
    return wname;
}

inline void WalkIsoDirectoryReader(
    const VirtualDiskReader& reader,
    uint64_t partBase,
    uint32_t extentLba,
    uint32_t extentBytes,
    bool isJoliet,
    const std::wstring& parentDir,
    int depth,
    std::set<uint32_t>& visitedExtents,
    ArchiveSummaryInfo& outInfo,
    const std::wstring* extractTarget = nullptr,
    std::vector<uint8_t>* outExtracted = nullptr,
    bool* outFoundExtract = nullptr)
{
    if (depth > 12 || extentLba == 0 || extentBytes == 0 || outInfo.entries.size() >= 25000) return;
    if (outFoundExtract && *outFoundExtract) return;
    if (!visitedExtents.insert(extentLba).second) return;

    const uint64_t relOff = static_cast<uint64_t>(extentLba) * 2048ULL;
    if (partBase > reader.virtualSize || relOff >= reader.virtualSize - partBase) return;
    const uint64_t startOff = partBase + relOff;

    uint32_t clampedBytes = std::min<uint32_t>(extentBytes, 4 * 1024 * 1024);
    if (clampedBytes > reader.virtualSize - startOff) {
        clampedBytes = static_cast<uint32_t>(reader.virtualSize - startOff);
    }
    if (clampedBytes < 34u) return;

    std::vector<uint8_t> dirBuf(clampedBytes);
    if (!reader.readBytes(startOff, dirBuf.data(), clampedBytes)) return;

    size_t pos = 0;
    while (pos < clampedBytes && outInfo.entries.size() < 25000) {
        uint8_t recLen = dirBuf[pos];
        if (recLen == 0) {
            pos = ((pos / 2048) + 1) * 2048;
            continue;
        }
        if (recLen < 34 || recLen > clampedBytes - pos) break;

        const uint8_t* rec = dirBuf.data() + pos;
        uint32_t childLba = RdU32LE(rec + 2);
        uint32_t dataLen  = RdU32LE(rec + 10);
        uint8_t  flags    = rec[25];
        std::wstring wname = DecodeIsoEntryName(rec, recLen, isJoliet);
        if (!wname.empty()) {
            std::wstring fullRel = ArchivePath::Normalize(parentDir.empty() ? wname : (parentDir + L"/" + wname));
            bool isDir = (flags & 0x02u) != 0;
            const uint64_t childRel = static_cast<uint64_t>(childLba) * 2048ULL;
            const bool validExtent = (childRel <= reader.virtualSize - partBase) &&
                                     (isDir || dataLen <= reader.virtualSize - partBase - childRel);

            if (validExtent) {
                const uint64_t childDiskOff = partBase + childRel;
                if (extractTarget && outExtracted && outFoundExtract) {
                    if (!isDir && PathEqualsCI(fullRel, *extractTarget)) {
                        if (dataLen <= 128 * 1024 * 1024) {
                            outExtracted->resize(dataLen);
                            if (dataLen == 0 || reader.readBytes(childDiskOff, outExtracted->data(), dataLen)) {
                                *outFoundExtract = true;
                                return;
                            }
                            outExtracted->clear();
                        }
                        return;
                    }
                } else {
                    ArchiveItemEntry item;
                    item.pathInArchive = fullRel;
                    item.name = ArchivePath::BaseName(fullRel);
                    item.isDirectory = isDir;
                    item.uncompressedSize = isDir ? 0 : dataLen;
                    item.compressedSize   = isDir ? 0 : dataLen;
                    item.rawDataOffset    = childDiskOff;
                    item.compressionMethod = reader.directRawBuffer ? 0 : 1;
                    outInfo.entries.push_back(std::move(item));
                }

                if (isDir && childLba != extentLba) {
                    WalkIsoDirectoryReader(reader, partBase, childLba, dataLen, isJoliet, fullRel,
                                           depth + 1, visitedExtents, outInfo, extractTarget, outExtracted, outFoundExtract);
                    if (outFoundExtract && *outFoundExtract) return;
                }
            }
        }
        pos += recLen;
    }
}

inline bool InspectOrExtractIsoOnReader(
    const VirtualDiskReader& reader,
    uint64_t partBase,
    const std::wstring& prefixDir,
    ArchiveSummaryInfo& outInfo,
    const std::wstring* extractTarget,
    std::vector<uint8_t>* outExtracted)
{
    if (partBase > reader.virtualSize || 0x8800ULL > reader.virtualSize - partBase) return false;
    uint8_t vd[2048] = {};
    if (!reader.readBytes(partBase + 0x8000ULL, vd, 2048)) return false;
    if (vd[0] != 1 || std::memcmp(vd + 1, "CD001", 5) != 0) return false;

    uint32_t rootLba = RdU32LE(vd + 156 + 2);
    uint32_t rootLen = RdU32LE(vd + 156 + 10);
    bool useJoliet = false;

    // Scan subsequent volume descriptors for Joliet UTF-16BE SVD (type 2)
    for (uint32_t sec = 17; sec < 32; ++sec) {
        const uint64_t rel = static_cast<uint64_t>(sec) * 2048ULL;
        if (rel > reader.virtualSize - partBase || 2048ULL > reader.virtualSize - partBase - rel) break;
        uint8_t svd[2048] = {};
        if (!reader.readBytes(partBase + rel, svd, 2048) || std::memcmp(svd + 1, "CD001", 5) != 0) break;
        if (svd[0] == 255) break;
        if (svd[0] == 2 && svd[88] == 0x25 && svd[89] == 0x2F &&
            (svd[90] == 0x40 || svd[90] == 0x43 || svd[90] == 0x45))
        {
            uint32_t jLba = RdU32LE(svd + 156 + 2);
            uint32_t jLen = RdU32LE(svd + 156 + 10);
            if (jLba > 0 && jLen > 0) {
                rootLba = jLba;
                rootLen = jLen;
                useJoliet = true;
                break;
            }
        }
    }

    std::set<uint32_t> visited;
    bool foundExtract = false;
    WalkIsoDirectoryReader(reader, partBase, rootLba, rootLen, useJoliet, prefixDir, 0,
                           visited, outInfo, extractTarget, outExtracted, &foundExtract);
    if (extractTarget) return foundExtract;
    return true;
}

inline bool ParseIso9660(
    const uint8_t* data,
    size_t size,
    ArchiveSummaryInfo& outInfo,
    const std::wstring* extractTarget = nullptr,
    std::vector<uint8_t>* outExtracted = nullptr)
{
    if (!data || size < 0x8800 || data[0x8000] != 1 || std::memcmp(data + 0x8001, "CD001", 5) != 0) return false;
    VirtualDiskReader raw = MakeRawMemoryDiskReader(data, size, L"ISO 9660", false);

    if (extractTarget && outExtracted) {
        return InspectOrExtractIsoOnReader(raw, 0, L"", outInfo, extractTarget, outExtracted);
    }

    outInfo.formatName = L"ISO 9660";
    if (!InspectOrExtractIsoOnReader(raw, 0, L"", outInfo, nullptr, nullptr)) return false;
    if (outInfo.entries.empty()) {
        std::string volId(reinterpret_cast<const char*>(data + 0x8000 + 40), 32);
        while (!volId.empty() && volId.back() == ' ') volId.pop_back();
        std::wstring vName = volId.empty() ? L"EMPTY_ISO_VOLUME" : PathUtils::Utf8ToWide(volId);
        ArchiveItemEntry e;
        e.pathInArchive = vName;
        e.name = vName;
        e.isDirectory = true;
        outInfo.entries.push_back(std::move(e));
    }
    outInfo.valid = true;
    return true;
}

} // namespace DetailDiskAndGameArc

inline bool InspectIso9660(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return DetailDiskAndGameArc::ParseIso9660(data, size, outInfo);
}

inline bool ExtractIso9660Entry(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    if (data && size >= 0x8800 && data[0x8000] == 1 && std::memcmp(data + 0x8001, "CD001", 5) == 0) {
        ArchiveSummaryInfo dummy;
        if (DetailDiskAndGameArc::ParseIso9660(data, size, dummy, &normTarget, &outBytes)) return true;
    }
    return false;
}

} // namespace Explorer2::Formats
