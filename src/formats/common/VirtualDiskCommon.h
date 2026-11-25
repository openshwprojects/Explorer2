#pragma once
// VirtualDiskCommon.h — Abstract VirtualDiskReader, MBR / GPT / Apple Partition Map (APM) discovery,
// and unified multi-filesystem partition mounter (FAT12/16/32, exFAT, ISO 9660, NTFS, ext2/3/4, HFS+/HFSX).

#include "ArchiveFormatCommon.h"

namespace Explorer2::Formats::DetailDiskAndGameArc {

struct VirtualDiskReader {
    uint64_t virtualSize = 0;
    std::wstring formatName;
    bool directRawBuffer = false; // true when diskOff maps 1:1 to host file offset
    bool allowRawSliceFallback = true;
    std::function<bool(uint64_t diskOff, uint8_t* dst, size_t len)> readBytes;
};

struct DiscoveredPartition {
    uint64_t byteOffset = 0;
    uint64_t byteLength = 0;
    uint8_t  mbrType = 0;
    std::wstring label;
};

inline bool IsKnownVirtualDiskContainerMagic(const uint8_t* data, size_t size) noexcept {
    if (!data || size < 64) return false;
    if (size >= 72 && std::memcmp(data, "QFI\xFB", 4) == 0) return true;
    if (size >= 65536 && std::memcmp(data, "vhdxfile", 8) == 0) return true;
    if (size >= 1024 && (std::memcmp(data + size - 512, "conectix", 8) == 0 || std::memcmp(data, "conectix", 8) == 0)) return true;
    if (size >= 512 && std::memcmp(data, "KDMV", 4) == 0) return true;
    if (size >= 400 && RdU32LE(data + 64) == 0xBEDA107Fu) return true;
    if (size >= 512 && std::memcmp(data + size - 512, "koly", 4) == 0) return true;
    if (size >= 40 && RdU32LE(data) == 0xED26FF3Au) return true; // Android sparse .simg
    if (size >= 8 && std::memcmp(data, "VER2", 4) == 0) return true; // GTA SA .img
    return false;
}

inline VirtualDiskReader MakeRawMemoryDiskReader(
    const uint8_t* data,
    size_t size,
    const std::wstring& formatName,
    bool allowRawSliceFallback = false)
{
    VirtualDiskReader r;
    r.virtualSize = size;
    r.formatName = formatName;
    r.directRawBuffer = true;
    r.allowRawSliceFallback = allowRawSliceFallback;
    r.readBytes = [data, size](uint64_t diskOff, uint8_t* dst, size_t len) -> bool {
        if (diskOff > size || len > size - static_cast<size_t>(diskOff)) return false;
        if (len > 0) std::memcpy(dst, data + static_cast<size_t>(diskOff), len);
        return true;
    };
    return r;
}

// Forward declarations for single-format filesystem mounters
bool HasFatBootSignatureOnReader(const VirtualDiskReader& reader, uint64_t partBase, uint64_t partBytes);
bool InspectOrExtractFatPartitionOnReader(
    const VirtualDiskReader& reader,
    uint64_t partBase,
    uint64_t partBytes,
    const std::wstring& prefixDir,
    ArchiveSummaryInfo& outInfo,
    const std::wstring* extractTargetNorm = nullptr,
    std::vector<uint8_t>* outExtracted = nullptr);

bool HasExFatBootSignatureOnReader(const VirtualDiskReader& reader, uint64_t partBase, uint64_t partBytes);
bool InspectOrExtractExFatOnReader(
    const VirtualDiskReader& reader,
    uint64_t partBase,
    uint64_t partBytes,
    const std::wstring& prefixDir,
    ArchiveSummaryInfo& outInfo,
    const std::wstring* extractTargetNorm = nullptr,
    std::vector<uint8_t>* outExtracted = nullptr);

bool InspectOrExtractIsoOnReader(
    const VirtualDiskReader& reader,
    uint64_t partBase,
    const std::wstring& prefixDir,
    ArchiveSummaryInfo& outInfo,
    const std::wstring* extractTarget = nullptr,
    std::vector<uint8_t>* outExtracted = nullptr);

bool InspectOrExtractNtfsOnReader(
    const VirtualDiskReader& reader,
    uint64_t partBase,
    uint64_t partBytes,
    const std::wstring& prefixDir,
    ArchiveSummaryInfo& outInfo,
    const std::wstring* extractTarget = nullptr,
    std::vector<uint8_t>* outExtracted = nullptr);

bool InspectOrExtractExtFsOnReader(
    const VirtualDiskReader& reader,
    uint64_t partBase,
    uint64_t partBytes,
    const std::wstring& prefixDir,
    ArchiveSummaryInfo& outInfo,
    const std::wstring* extractTargetNorm = nullptr,
    std::vector<uint8_t>* outExtracted = nullptr);

bool InspectOrExtractHfsPlusOnReader(
    const VirtualDiskReader& reader,
    uint64_t partBase,
    uint64_t partBytes,
    const std::wstring& prefixDir,
    ArchiveSummaryInfo& outInfo,
    const std::wstring* extractTargetNorm = nullptr,
    std::vector<uint8_t>* outExtracted = nullptr);

inline void DiscoverDiskPartitions(const VirtualDiskReader& reader, std::vector<DiscoveredPartition>& outParts) {
    outParts.clear();
    if (reader.virtualSize < 512) return;

    // 1. Check if LBA 0 is already a superfloppy FAT / exFAT / NTFS / HFS+ / ext2-4 filesystem
    if (HasExFatBootSignatureOnReader(reader, 0, reader.virtualSize)) {
        outParts.push_back({ 0, reader.virtualSize, 0x07, L"" });
        return;
    }
    if (HasFatBootSignatureOnReader(reader, 0, reader.virtualSize)) {
        outParts.push_back({ 0, reader.virtualSize, 0x06, L"" });
        return;
    }
    uint8_t sec0[512] = {};
    if (!reader.readBytes(0, sec0, 512)) return;
    if (std::memcmp(sec0 + 3, "NTFS    ", 8) == 0) {
        outParts.push_back({ 0, reader.virtualSize, 0x07, L"" });
        return;
    }
    if (reader.virtualSize >= 2048) {
        uint8_t sbProbe[132] = {};
        if (reader.readBytes(1024, sbProbe, sizeof(sbProbe))) {
            bool isRawHfs = (sbProbe[0] == 'H' && (sbProbe[1] == '+' || sbProbe[1] == 'X')) ||
                            (sbProbe[0] == 'B' && sbProbe[1] == 'D' && sbProbe[124] == 'H' && (sbProbe[125] == '+' || sbProbe[125] == 'X'));
            bool isRawExt = (RdU16LE(sbProbe + 56) == 0xEF53u);
            if (isRawHfs || isRawExt) {
                outParts.push_back({ 0, reader.virtualSize, static_cast<uint8_t>(isRawHfs ? 0xAFu : 0x83u), L"" });
                return;
            }
        }
    }

    // 2. Check GPT at LBA 1 ("EFI PART")
    if (reader.virtualSize >= 1024) {
        uint8_t gpt[512] = {};
        if (reader.readBytes(512, gpt, 512) && std::memcmp(gpt, "EFI PART", 8) == 0) {
            uint64_t peLba  = RdU64LE(gpt + 72);
            uint32_t numPe  = std::min<uint32_t>(RdU32LE(gpt + 80), 128);
            uint32_t peSize = RdU32LE(gpt + 84);
            const uint64_t tblBytes = static_cast<uint64_t>(numPe) * peSize;
            if (peSize >= 128 && peSize <= 512 && peLba <= reader.virtualSize / 512ULL &&
                tblBytes <= reader.virtualSize - peLba * 512ULL)
            {
                std::vector<uint8_t> peBuf(static_cast<size_t>(tblBytes));
                if (reader.readBytes(peLba * 512ULL, peBuf.data(), peBuf.size())) {
                    for (uint32_t i = 0; i < numPe; ++i) {
                        const uint8_t* pe = peBuf.data() + i * peSize;
                        bool allZero = true;
                        for (int b = 0; b < 16; ++b) if (pe[b] != 0) { allZero = false; break; }
                        if (allZero) continue;
                        uint64_t firstLba = RdU64LE(pe + 32);
                        uint64_t lastLba  = RdU64LE(pe + 40);
                        if (firstLba == 0 || lastLba < firstLba || firstLba > reader.virtualSize / 512ULL) continue;
                        uint64_t pOff = firstLba * 512ULL;
                        if (pOff >= reader.virtualSize) continue;
                        std::wstring pname;
                        for (size_t c = 0; c < 36; ++c) {
                            wchar_t wc = static_cast<wchar_t>(RdU16LE(pe + 56 + c * 2));
                            if (wc == 0) break;
                            if (wc == L'/' || wc == L'\\') wc = L'_';
                            pname.push_back(wc);
                        }
                        uint64_t maxSecs = (reader.virtualSize - pOff) / 512ULL;
                        uint64_t spanSecs = std::min<uint64_t>(lastLba - firstLba + 1ULL, maxSecs);
                        uint64_t pLen = (spanSecs > 0) ? (spanSecs * 512ULL) : (reader.virtualSize - pOff);
                        outParts.push_back({ pOff, pLen, 0xEE, pname });
                    }
                }
            }
            if (!outParts.empty()) return;
        }
    }

    // 3. Check Apple Partition Map (APM — "PM" signature at sector 1)
    {
        uint32_t apmBlk = 512u;
        if (sec0[0] == 'E' && sec0[1] == 'R') {
            uint16_t sbBlk = RdU16BE(sec0 + 2);
            if (sbBlk >= 512u && sbBlk <= 4096u && (sbBlk & (sbBlk - 1u)) == 0) apmBlk = sbBlk;
        }
        uint8_t apm1[512] = {};
        if (reader.virtualSize >= apmBlk + 512ULL && reader.readBytes(apmBlk, apm1, 512) &&
            apm1[0] == 'P' && apm1[1] == 'M')
        {
            uint32_t mapEntries = std::min<uint32_t>(RdU32BE(apm1 + 4), 64u);
            for (uint32_t m = 1; m <= mapEntries; ++m) {
                uint64_t entOff = static_cast<uint64_t>(m) * apmBlk;
                if (entOff > reader.virtualSize || 512ULL > reader.virtualSize - entOff) break;
                uint8_t pm[512] = {};
                if (!reader.readBytes(entOff, pm, 512) || pm[0] != 'P' || pm[1] != 'M') break;
                uint32_t pBlkStart = RdU32BE(pm + 8);
                uint32_t pBlkCnt   = RdU32BE(pm + 12);
                if (pBlkCnt == 0) continue;

                std::string pName(reinterpret_cast<const char*>(pm + 16), strnlen(reinterpret_cast<const char*>(pm + 16), 32));
                std::string pType(reinterpret_cast<const char*>(pm + 48), strnlen(reinterpret_cast<const char*>(pm + 48), 32));
                if (_stricmp(pType.c_str(), "Apple_partition_map") == 0 ||
                    _stricmp(pType.c_str(), "Apple_Free") == 0 ||
                    _stricmp(pType.c_str(), "Apple_Extra") == 0 ||
                    _stricmp(pType.c_str(), "Apple_Patches") == 0 ||
                    _stricmp(pType.c_str(), "Apple_Scratch") == 0 ||
                    _stricmp(pType.c_str(), "Apple_FWDriver") == 0 ||
                    _stricmp(pType.c_str(), "Apple_Loader") == 0 ||
                    _strnicmp(pType.c_str(), "Apple_Driver", 12) == 0)
                {
                    continue;
                }

                uint64_t pOff = static_cast<uint64_t>(pBlkStart) * apmBlk;
                if (pOff >= reader.virtualSize) continue;
                uint64_t pLen = std::min<uint64_t>(static_cast<uint64_t>(pBlkCnt) * apmBlk, reader.virtualSize - pOff);
                std::wstring lbl = PathUtils::Utf8ToWide(pName);
                for (wchar_t& ch : lbl) if (ch == L'/' || ch == L'\\' || ch == L':') ch = L'_';
                outParts.push_back({ pOff, pLen, 0xAF, lbl });
            }
            if (!outParts.empty()) return;
        }
    }

    // 4. Check MBR at LBA 0 (bytes 446..509 + 0x55AA)
    if (sec0[510] == 0x55 && sec0[511] == 0xAA) {
        for (int i = 0; i < 4; ++i) {
            const uint8_t* pe = sec0 + 446 + i * 16;
            uint8_t status = pe[0];
            uint8_t ptype  = pe[4];
            uint32_t firstLba = RdU32LE(pe + 8);
            uint32_t numSecs  = RdU32LE(pe + 12);
            if ((status != 0x00 && status != 0x80) || ptype == 0 || ptype == 0xEE || firstLba == 0 || numSecs == 0) continue;
            uint64_t pOff = static_cast<uint64_t>(firstLba) * 512ULL;
            if (pOff >= reader.virtualSize) continue;
            uint64_t pLen = std::min<uint64_t>(static_cast<uint64_t>(numSecs) * 512ULL, reader.virtualSize - pOff);
            wchar_t lbl[64] = {};
            swprintf_s(lbl, L"Partition%d_Type%02X", i, ptype);
            outParts.push_back({ pOff, pLen, ptype, lbl });
        }
        if (!outParts.empty()) return;
    }

    // 5. Superfloppy fallback (ISO9660 / ext4 / HFS+ at offset 0)
    outParts.push_back({ 0, reader.virtualSize, 0, L"" });
}

inline bool sbIsHfsEmbed(const uint8_t* sig132) noexcept {
    return sig132[124] == 'H' && (sig132[125] == '+' || sig132[125] == 'X');
}

inline bool InspectOrExtractVirtualDiskReader(
    const VirtualDiskReader& reader,
    size_t size,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm,
    std::vector<uint8_t>* outExtracted)
{
    std::vector<DiscoveredPartition> parts;
    DiscoverDiskPartitions(reader, parts);
    bool multiPart = (parts.size() > 1);

    if (outInfo) {
        outInfo->formatName = reader.formatName;
    }

    for (size_t idx = 0; idx < parts.size(); ++idx) {
        const auto& p = parts[idx];
        std::wstring prefix = multiPart
            ? (p.label.empty() ? (L"Partition" + std::to_wstring(idx)) : p.label)
            : L"";

        auto matchTargetForPart = [&](std::wstring& outRelTarget) -> bool {
            if (!extractTargetNorm) return false;
            outRelTarget = *extractTargetNorm;
            if (!prefix.empty()) return true;
            if (_wcsnicmp(outRelTarget.c_str(), L"Partition", 9) == 0) {
                size_t sl = outRelTarget.find(L'/');
                if (sl != std::wstring::npos) outRelTarget = outRelTarget.substr(sl + 1);
            }
            return true;
        };

        std::wstring targetRel;
        const std::wstring* targetPtr = matchTargetForPart(targetRel) ? &targetRel : nullptr;

        // A. Try exFAT
        if (HasExFatBootSignatureOnReader(reader, p.byteOffset, p.byteLength)) {
            ArchiveSummaryInfo tmp;
            ArchiveSummaryInfo& dstInfo = outInfo ? *outInfo : tmp;
            if (InspectOrExtractExFatOnReader(reader, p.byteOffset, p.byteLength, prefix, dstInfo, targetPtr, outExtracted)) {
                if (extractTargetNorm) return true;
                continue;
            }
        }

        // B. Try FAT12 / FAT16 / FAT32
        if (HasFatBootSignatureOnReader(reader, p.byteOffset, p.byteLength)) {
            ArchiveSummaryInfo tmp;
            ArchiveSummaryInfo& dstInfo = outInfo ? *outInfo : tmp;
            if (InspectOrExtractFatPartitionOnReader(reader, p.byteOffset, p.byteLength, prefix, dstInfo, targetPtr, outExtracted)) {
                if (extractTargetNorm) return true;
                continue;
            }
        }

        // C. Try ISO-9660
        {
            ArchiveSummaryInfo tmp;
            ArchiveSummaryInfo& dstInfo = outInfo ? *outInfo : tmp;
            if (InspectOrExtractIsoOnReader(reader, p.byteOffset, prefix, dstInfo, targetPtr, outExtracted)) {
                if (extractTargetNorm) return true;
                continue;
            }
        }

        // D. Try NTFS
        {
            ArchiveSummaryInfo tmp;
            ArchiveSummaryInfo& dstInfo = outInfo ? *outInfo : tmp;
            if (InspectOrExtractNtfsOnReader(reader, p.byteOffset, p.byteLength, prefix, dstInfo, targetPtr, outExtracted)) {
                if (extractTargetNorm) return true;
                continue;
            }
        }

        // Check +1024 signatures for Linux ext2/3/4 and Apple HFS+/HFSX
        bool hasExtMagic = false;
        bool hasHfsMagic = false;
        if (p.byteLength >= 2048 && p.byteOffset <= reader.virtualSize && 1156ULL <= reader.virtualSize - p.byteOffset) {
            uint8_t sigBuf[132] = {};
            if (reader.readBytes(p.byteOffset + 1024, sigBuf, sizeof(sigBuf))) {
                hasExtMagic = (RdU16LE(sigBuf + 56) == 0xEF53u);
                hasHfsMagic = (sigBuf[0] == 'H' && (sigBuf[1] == '+' || sigBuf[1] == 'X')) ||
                              (sigBuf[0] == 'B' && sigBuf[1] == 'D' && sbIsHfsEmbed(sigBuf));
            }
        }

        // E. Try Linux ext2/ext3/ext4
        if (hasExtMagic) {
            ArchiveSummaryInfo tmp;
            ArchiveSummaryInfo& dstInfo = outInfo ? *outInfo : tmp;
            if (InspectOrExtractExtFsOnReader(reader, p.byteOffset, p.byteLength, prefix, dstInfo, targetPtr, outExtracted)) {
                if (extractTargetNorm) return true;
            }
            continue;
        }

        // F. Try Apple HFS+ / HFSX Catalog B-Tree
        if (hasHfsMagic) {
            ArchiveSummaryInfo tmp;
            ArchiveSummaryInfo& dstInfo = outInfo ? *outInfo : tmp;
            if (InspectOrExtractHfsPlusOnReader(reader, p.byteOffset, p.byteLength, prefix, dstInfo, targetPtr, outExtracted)) {
                if (extractTargetNorm) return true;
            }
            continue;
        }

        // G. Raw partition slice (only for real virtual disk containers like fresh VHD/VHDX/VMDK/VDI/QCOW2)
        if (!reader.allowRawSliceFallback) continue;

        std::wstring rawEntryName = !p.label.empty()
            ? (p.label + L"_" + std::to_wstring(p.byteLength / 512ULL) + L"sec.img")
            : (L"RawDisk_" + std::to_wstring(reader.virtualSize / 512ULL) + L"sec.img");

        if (extractTargetNorm && outExtracted) {
            if (PathEqualsCI(rawEntryName, *extractTargetNorm) && p.byteLength <= 128 * 1024 * 1024) {
                outExtracted->resize(static_cast<size_t>(p.byteLength));
                return reader.readBytes(p.byteOffset, outExtracted->data(), outExtracted->size());
            }
        } else if (outInfo) {
            ArchiveItemEntry item;
            item.pathInArchive = rawEntryName;
            item.name = ArchivePath::BaseName(rawEntryName);
            item.uncompressedSize = p.byteLength;
            item.compressedSize   = std::min<uint64_t>(p.byteLength, size);
            item.rawDataOffset    = p.byteOffset;
            item.compressionMethod = reader.directRawBuffer ? 0 : 1;
            outInfo->entries.push_back(std::move(item));
        }
    }

    if (extractTargetNorm) return false;
    if (outInfo) {
        outInfo->valid = !outInfo->entries.empty();
        return outInfo->valid;
    }
    return false;
}

// Partitioned raw disk image helper (MBR/GPT/APM raw .img / .dmg without koly)
inline bool ParseOrExtractPartitionedRawDiskImg(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm = nullptr,
    std::vector<uint8_t>* outExtracted = nullptr)
{
    if (!data || size < 1024 || IsKnownVirtualDiskContainerMagic(data, size)) return false;
    const bool isDiskExt = (extLower == L".img" || extLower == L".ima" || extLower == L".flp" || extLower == L".dmg");
    const bool hasGpt = (size >= 1024 && std::memcmp(data + 512, "EFI PART", 8) == 0);
    const bool hasApm = (size >= 1024 && ((data[512] == 'P' && data[513] == 'M') || (data[0] == 'E' && data[1] == 'R')));
    if (!isDiskExt && !hasGpt && !hasApm) return false;

    std::wstring label = (extLower == L".dmg") ? L"Apple Disk Image (DMG)" : L"Partitioned Disk Image (.img)";
    VirtualDiskReader reader = MakeRawMemoryDiskReader(data, size, label, false);
    std::vector<DiscoveredPartition> parts;
    DiscoverDiskPartitions(reader, parts);
    if (parts.empty() || (parts.size() == 1 && parts[0].byteOffset == 0)) {
        return false; // Single superfloppy volume is handled directly by its own single-format filesystem reader
    }
    return InspectOrExtractVirtualDiskReader(reader, size, outInfo, extractTargetNorm, outExtracted);
}

} // namespace Explorer2::Formats::DetailDiskAndGameArc

#include "../archives/Iso9660.h"
#include "../archives/FatFilesystemImg.h"
#include "../archives/ExFatFilesystemImg.h"
#include "../archives/NtfsFilesystemImg.h"
#include "../archives/LinuxExtFilesystem.h"
#include "../archives/AppleHfsPlusImg.h"
