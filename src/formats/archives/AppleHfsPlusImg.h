#pragma once
// AppleHfsPlusImg.h — Apple HFS+ / HFSX Catalog B-Tree filesystem reader & multi-extent data-fork
// extractor (.hfs, raw .dmg, raw .img, and APM/GPT partitions inside disk images).

#include "../common/VirtualDiskCommon.h"

namespace Explorer2::Formats {

namespace DetailDiskAndGameArc {

struct HfsExtentDesc {
    uint32_t startBlock = 0;
    uint32_t blockCount = 0;
};

struct HfsForkData {
    uint64_t logicalSize = 0;
    uint32_t clumpSize   = 0;
    uint32_t totalBlocks = 0;
    HfsExtentDesc extents[8] = {};
};

inline HfsForkData ParseHfsForkData(const uint8_t* p) {
    HfsForkData f;
    f.logicalSize = RdU64BE(p + 0);
    f.clumpSize   = RdU32BE(p + 8);
    f.totalBlocks = RdU32BE(p + 12);
    for (int i = 0; i < 8; ++i) {
        f.extents[i].startBlock = RdU32BE(p + 16 + i * 8);
        f.extents[i].blockCount = RdU32BE(p + 20 + i * 8);
    }
    return f;
}

inline bool ReadHfsForkBytes(
    const VirtualDiskReader& reader,
    uint64_t volBase,
    uint64_t volBytes,
    uint32_t blockSize,
    const HfsForkData& forkData,
    uint64_t logicalOff,
    uint8_t* dst,
    size_t len)
{
    if (len == 0) return true;
    if (logicalOff > forkData.logicalSize || len > forkData.logicalSize - logicalOff) return false;

    uint64_t curLogBase = 0;
    size_t written = 0;
    for (int i = 0; i < 8 && written < len; ++i) {
        const auto& ex = forkData.extents[i];
        if (ex.blockCount == 0) continue;
        uint64_t exBytes = static_cast<uint64_t>(ex.blockCount) * blockSize;
        uint64_t reqOff  = logicalOff + written;
        if (reqOff >= curLogBase && reqOff < curLogBase + exBytes) {
            uint64_t relInEx = reqOff - curLogBase;
            size_t chunk = static_cast<size_t>(std::min<uint64_t>(len - written, exBytes - relInEx));
            uint64_t blockRel = static_cast<uint64_t>(ex.startBlock) * blockSize;
            if (blockRel > volBytes || relInEx > volBytes - blockRel || chunk > volBytes - blockRel - relInEx) {
                return false;
            }
            uint64_t physOff = volBase + blockRel + relInEx;
            if (physOff > reader.virtualSize || chunk > reader.virtualSize - physOff) return false;
            if (!reader.readBytes(physOff, dst + written, chunk)) return false;
            written += chunk;
        }
        curLogBase += exBytes;
    }
    return written == len;
}

inline void HfsTimeU32ToDos(uint32_t macSecs1904, uint16_t& outDate, uint16_t& outTime) {
    outDate = 0;
    outTime = 0;
    if (macSecs1904 <= 2082844800u) return;
    UnixTimeU32ToDos(macSecs1904 - 2082844800u, outDate, outTime);
}

inline bool InspectOrExtractHfsPlusOnReader(
    const VirtualDiskReader& reader,
    uint64_t partBase,
    uint64_t partBytes,
    const std::wstring& prefixDir,
    ArchiveSummaryInfo& outInfo,
    const std::wstring* extractTargetNorm,
    std::vector<uint8_t>* outExtracted)
{
    if (partBytes < 2048 || partBase > reader.virtualSize || partBytes > reader.virtualSize - partBase) return false;

    uint64_t volBase  = partBase;
    uint64_t volBytes = partBytes;
    uint8_t vh[512]   = {};
    if (!reader.readBytes(volBase + 1024, vh, 512)) return false;

    if (vh[0] == 'B' && vh[1] == 'D' && vh[124] == 'H' && (vh[125] == '+' || vh[125] == 'X')) {
        uint32_t alBlkSiz  = RdU32BE(vh + 20);
        uint16_t alBlSt    = RdU16BE(vh + 28);
        uint16_t embStart  = RdU16BE(vh + 126);
        uint16_t embCount  = RdU16BE(vh + 128);
        uint64_t embOffset = static_cast<uint64_t>(alBlSt) * 512ULL + static_cast<uint64_t>(embStart) * alBlkSiz;
        uint64_t embLength = static_cast<uint64_t>(embCount) * alBlkSiz;
        if (alBlkSiz >= 512 && embOffset <= partBytes - 2048ULL && embLength >= 2048) {
            volBase  = partBase + embOffset;
            volBytes = std::min<uint64_t>(embLength, partBytes - embOffset);
            if (!reader.readBytes(volBase + 1024, vh, 512)) return false;
        }
    }

    if (vh[0] != 'H' || (vh[1] != '+' && vh[1] != 'X')) return false;

    uint32_t blockSize   = RdU32BE(vh + 40);
    uint32_t totalBlocks = RdU32BE(vh + 44);
    if (blockSize < 512 || blockSize > 1048576 || (blockSize & (blockSize - 1u)) != 0 || totalBlocks == 0) {
        return false;
    }

    HfsForkData catalogFork;
    uint16_t nodeSize      = 0;
    uint32_t firstLeafNode = 0;
    uint32_t totalNodes    = 0;
    auto probeCatalogFork = [&](size_t vhOff) -> bool {
        if (vhOff + 80 > sizeof(vh)) return false;
        HfsForkData cand = ParseHfsForkData(vh + vhOff);
        if (cand.logicalSize < 512) return false;
        uint8_t node0[120] = {};
        if (!ReadHfsForkBytes(reader, volBase, volBytes, blockSize, cand, 0, node0, sizeof(node0))) return false;
        if (static_cast<int8_t>(node0[8]) != 1) return false;
        uint16_t ns  = RdU16BE(node0 + 14 + 18);
        uint32_t tn  = RdU32BE(node0 + 14 + 22);
        uint32_t fln = RdU32BE(node0 + 14 + 10);
        if (ns < 512 || ns > 32768 || (ns & (ns - 1u)) != 0) return false;
        if (tn == 0 || static_cast<uint64_t>(tn) * ns > cand.logicalSize) return false;
        if (fln >= tn) return false;
        catalogFork   = cand;
        nodeSize      = ns;
        firstLeafNode = fln;
        totalNodes    = tn;
        return true;
    };

    if (!probeCatalogFork(272) && !probeCatalogFork(208)) return false;
    if (firstLeafNode == 0) return false;

    struct HfsCatFolder {
        uint32_t folderId = 0;
        uint32_t parentId = 0;
        std::wstring name;
        uint32_t modDate  = 0;
    };
    struct HfsCatFile {
        uint32_t fileId   = 0;
        uint32_t parentId = 0;
        std::wstring name;
        uint32_t modDate  = 0;
        HfsForkData dataFork;
    };

    std::unordered_map<uint32_t, HfsCatFolder> foldersMap;
    std::vector<HfsCatFolder> foldersOrder;
    std::vector<HfsCatFile>   filesList;

    std::set<uint32_t> visitedNodes;
    std::vector<uint8_t> nodeBuf(nodeSize);
    uint32_t curNode = firstLeafNode;

    while (curNode != 0 && curNode < totalNodes && visitedNodes.size() < 16384) {
        if (!visitedNodes.insert(curNode).second) return false;
        if (!ReadHfsForkBytes(reader, volBase, volBytes, blockSize, catalogFork,
                              static_cast<uint64_t>(curNode) * nodeSize, nodeBuf.data(), nodeSize))
        {
            return false;
        }
        if (static_cast<int8_t>(nodeBuf[8]) != -1) return false;

        uint16_t numRecs = RdU16BE(nodeBuf.data() + 10);
        if (14u + (static_cast<uint32_t>(numRecs) + 1u) * 2u > nodeSize) return false;

        size_t offTblFloor = nodeSize - static_cast<size_t>(numRecs + 1u) * 2u;
        for (uint16_t r = 0; r < numRecs; ++r) {
            uint16_t recOff  = RdU16BE(nodeBuf.data() + nodeSize - 2u * (r + 1u));
            uint16_t nextOff = RdU16BE(nodeBuf.data() + nodeSize - 2u * (r + 2u));
            if (recOff < 14u || nextOff < recOff || nextOff > offTblFloor + 2u || recOff + 8u > nodeSize) continue;

            uint16_t keyLen = RdU16BE(nodeBuf.data() + recOff);
            if (keyLen < 6u || static_cast<size_t>(recOff) + 2u + keyLen > nextOff) continue;

            uint32_t parentCnid = RdU32BE(nodeBuf.data() + recOff + 2);
            uint16_t uniLen     = RdU16BE(nodeBuf.data() + recOff + 6);
            if (6u + static_cast<uint32_t>(uniLen) * 2u > keyLen) continue;

            size_t valOff = static_cast<size_t>(recOff) + 2u + keyLen + (keyLen & 1u);
            if (valOff + 2u > nextOff) continue;
            int16_t recType = static_cast<int16_t>(RdU16BE(nodeBuf.data() + valOff));
            if (recType != 0x0001 && recType != 0x0002) continue;

            if (uniLen > 0 && RdU16BE(nodeBuf.data() + recOff + 8) == 0) continue;
            std::wstring wname;
            wname.reserve(uniLen);
            for (uint16_t c = 0; c < uniLen; ++c) {
                wchar_t wc = static_cast<wchar_t>(RdU16BE(nodeBuf.data() + recOff + 8 + c * 2u));
                if (wc == 0) break;
                if (wc == L'/' || wc == L'\\' || wc == L':') wc = L'_';
                wname.push_back(wc);
            }

            if (recType == 0x0001) {
                if (valOff + 20u > nextOff) continue;
                uint32_t folderId = RdU32BE(nodeBuf.data() + valOff + 8);
                uint32_t modDate  = RdU32BE(nodeBuf.data() + valOff + 16);
                HfsCatFolder fld{ folderId, parentCnid, wname, modDate };
                foldersMap[folderId] = fld;
                if (folderId != 2u && !wname.empty()) {
                    foldersOrder.push_back(std::move(fld));
                }
            } else if (recType == 0x0002) {
                if (valOff + 168u > nextOff || wname.empty()) continue;
                HfsCatFile fl;
                fl.fileId   = RdU32BE(nodeBuf.data() + valOff + 8);
                fl.parentId = parentCnid;
                fl.name     = std::move(wname);
                fl.modDate  = RdU32BE(nodeBuf.data() + valOff + 16);
                fl.dataFork = ParseHfsForkData(nodeBuf.data() + valOff + 88);
                filesList.push_back(std::move(fl));
            }
        }

        curNode = RdU32BE(nodeBuf.data() + 0);
    }

    auto buildFullPath = [&](uint32_t parentId, const std::wstring& leafName, std::wstring& outFull) -> bool {
        if (leafName.empty() || leafName == L"." || leafName == L"..") return false;
        std::vector<std::wstring> parts;
        parts.push_back(leafName);
        uint32_t cur = parentId;
        int guard = 0;
        while (cur != 2u && cur != 1u) {
            if (++guard > 32) return false;
            auto it = foldersMap.find(cur);
            if (it == foldersMap.end() || it->second.name.empty()) return false;
            parts.push_back(it->second.name);
            if (it->second.parentId == cur) return false;
            cur = it->second.parentId;
        }
        if (cur != 2u) return false;

        std::wstring joined = prefixDir;
        for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
            if (!joined.empty()) joined.push_back(L'/');
            joined += *it;
        }
        outFull = ArchivePath::Normalize(std::move(joined));
        return !outFull.empty();
    };

    if (extractTargetNorm && outExtracted) {
        for (const auto& fl : filesList) {
            std::wstring full;
            if (!buildFullPath(fl.parentId, fl.name, full)) continue;
            if (PathEqualsCI(full, *extractTargetNorm)) {
                if (fl.dataFork.logicalSize > 256ULL * 1024 * 1024) return false;
                outExtracted->resize(static_cast<size_t>(fl.dataFork.logicalSize));
                if (!ReadHfsForkBytes(reader, volBase, volBytes, blockSize, fl.dataFork, 0,
                                      outExtracted->data(), outExtracted->size()))
                {
                    outExtracted->clear();
                    return false;
                }
                return true;
            }
        }
        return false;
    }

    size_t added = 0;
    for (const auto& fld : foldersOrder) {
        if (outInfo.entries.size() >= 50000) break;
        std::wstring full;
        if (!buildFullPath(fld.parentId, fld.name, full)) continue;
        ArchiveItemEntry item;
        item.pathInArchive = full;
        item.name = ArchivePath::BaseName(full);
        item.isDirectory = true;
        HfsTimeU32ToDos(fld.modDate, item.dosDate, item.dosTime);
        outInfo.entries.push_back(std::move(item));
        ++added;
    }

    for (const auto& fl : filesList) {
        if (outInfo.entries.size() >= 50000) break;
        std::wstring full;
        if (!buildFullPath(fl.parentId, fl.name, full)) continue;
        ArchiveItemEntry item;
        item.pathInArchive = full;
        item.name = ArchivePath::BaseName(full);
        item.isDirectory = false;
        item.uncompressedSize = fl.dataFork.logicalSize;
        uint64_t allocBytes = static_cast<uint64_t>(fl.dataFork.totalBlocks) * blockSize;
        item.compressedSize = (allocBytes > 0) ? std::min<uint64_t>(allocBytes, fl.dataFork.logicalSize ? allocBytes : 0) : fl.dataFork.logicalSize;
        item.compressionMethod = 1;
        HfsTimeU32ToDos(fl.modDate, item.dosDate, item.dosTime);
        outInfo.entries.push_back(std::move(item));
        ++added;
    }

    return added > 0;
}

inline bool HasRawHfsPlusSignature(const uint8_t* data, size_t size) noexcept {
    if (!data || size < 2048) return false;
    const uint8_t* vh = data + 1024;
    if (vh[0] == 'H' && (vh[1] == '+' || vh[1] == 'X')) return true;
    if (vh[0] == 'B' && vh[1] == 'D' && vh[124] == 'H' && (vh[125] == '+' || vh[125] == 'X')) return true;
    return false;
}

} // namespace DetailDiskAndGameArc

inline bool InspectAppleHfsPlusImg(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    ArchiveSummaryInfo& outInfo)
{
    if (!data || size < 2048 || DetailDiskAndGameArc::IsKnownVirtualDiskContainerMagic(data, size)) return false;
    if (!DetailDiskAndGameArc::HasRawHfsPlusSignature(data, size)) return false;
    const std::wstring fmt = (extLower == L".dmg") ? L"Apple Disk Image (DMG)" : L"Apple HFS+ Volume";
    DetailDiskAndGameArc::VirtualDiskReader raw = DetailDiskAndGameArc::MakeRawMemoryDiskReader(data, size, fmt, false);
    outInfo = {};
    outInfo.formatName = fmt;
    if (!DetailDiskAndGameArc::InspectOrExtractHfsPlusOnReader(raw, 0, size, L"", outInfo, nullptr, nullptr)) {
        outInfo = {};
        return false;
    }
    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

inline bool ExtractAppleHfsPlusImgEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    outBytes.clear();
    if (!data || size < 2048 || normTarget.empty() || DetailDiskAndGameArc::IsKnownVirtualDiskContainerMagic(data, size)) return false;
    if (!DetailDiskAndGameArc::HasRawHfsPlusSignature(data, size)) return false;
    const std::wstring fmt = (extLower == L".dmg") ? L"Apple Disk Image (DMG)" : L"Apple HFS+ Volume";
    DetailDiskAndGameArc::VirtualDiskReader raw = DetailDiskAndGameArc::MakeRawMemoryDiskReader(data, size, fmt, false);
    ArchiveSummaryInfo dummy;
    return DetailDiskAndGameArc::InspectOrExtractHfsPlusOnReader(raw, 0, size, L"", dummy, &normTarget, &outBytes);
}

} // namespace Explorer2::Formats
