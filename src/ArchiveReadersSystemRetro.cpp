// ArchiveReadersSystemRetro.cpp — Modular System Filesystem, Classic Archiver & Console/Steam Container Readers
// Dispatches the 10 new archive/filesystem formats (exFAT, Linux ROMFS, Linux CramFS, ESTsoft ALZip, ZOO,
// SEA ARC, StuffIt Classic, Nintendo SARC/Yaz0 SZS, Nintendo DS NARC, Valve Steam GCF) plus standalone
// FAT12/16/32, NTFS, ext2/3/4, HFS+, and partitioned raw disk images.

#include "ArchiveReaders.h"
#include "FormatSupport.h"
#include "PathUtils.h"
#include "ZipDeflate.h"
#include "formats/common/ArchiveFormatCommon.h"
#include "formats/common/VirtualDiskCommon.h"
#include "formats/archives/FatFilesystemImg.h"
#include "formats/archives/ExFatFilesystemImg.h"
#include "formats/archives/NtfsFilesystemImg.h"
#include "formats/archives/LinuxExtFilesystem.h"
#include "formats/archives/AppleHfsPlusImg.h"
#include "formats/archives/RomFsFilesystem.h"
#include "formats/archives/CramFsFilesystem.h"
#include "formats/archives/AlzipAlzArchive.h"
#include "formats/archives/ZooArchive.h"
#include "formats/archives/SeaArcArchive.h"
#include "formats/archives/StuffItSitArchive.h"
#include "formats/archives/NintendoSarcArchive.h"
#include "formats/archives/NintendoNarcArchive.h"
#include "formats/archives/ValveGcfArchive.h"

namespace Explorer2 {
#include "formats/common/FormatLoaderDispatch.h"
} // namespace Explorer2

namespace Explorer2::ArchiveReaders {

namespace {

bool SniffExFat(const uint8_t* data, size_t size) {
    return size >= 6144u && std::memcmp(data + 3u, "EXFAT   ", 8) == 0 &&
           data[510u] == 0x55u && data[511u] == 0xAAu &&
           !Formats::DetailDiskAndGameArc::IsKnownVirtualDiskContainerMagic(data, size);
}

bool SniffNtfs(const uint8_t* data, size_t size) {
    return size >= 4096u && std::memcmp(data + 3u, "NTFS    ", 8) == 0 &&
           !Formats::DetailDiskAndGameArc::IsKnownVirtualDiskContainerMagic(data, size);
}

bool SniffRomFs(const uint8_t* data, size_t size) {
    return size >= 32u && std::memcmp(data, "-rom1fs-", 8) == 0;
}

bool SniffCramFs(const uint8_t* data, size_t size) {
    for (size_t cand : { size_t(0), size_t(512) }) {
        if (size >= cand + 76u && std::memcmp(data + cand + 16u, "Compressed ROMFS", 16) == 0) {
            const uint32_t mLe = ByteIO::RdU32LE(data + cand);
            const uint32_t mBe = ByteIO::RdU32BE(data + cand);
            if (mLe == 0x28CD3D45u || mBe == 0x28CD3D45u) return true;
        }
    }
    return false;
}

bool SniffAlz(const uint8_t* data, size_t size) {
    return size >= 21u && std::memcmp(data, "ALZ\x01", 4) == 0 && std::memcmp(data + 8u, "BLZ\x01", 4) == 0;
}

bool SniffZoo(const uint8_t* data, size_t size) {
    if (size < 42u || ByteIO::RdU32LE(data + 20u) != 0xFDC4A7DCu) return false;
    const uint32_t s = ByteIO::RdU32LE(data + 24u);
    const uint32_t m = ByteIO::RdU32LE(data + 28u);
    return static_cast<uint32_t>(s + m) == 0u && s >= 34u && s < size;
}

bool SniffSeaArc(const uint8_t* data, size_t size) {
    if (size < 27u || data[0] != 0x1Au) return false;
    const uint8_t method = data[1u];
    if (method < 1u || method > 10u) return false;
    std::string dummyName;
    if (!Formats::DetailSeaArc::ValidateSeaArcNameField(data + 2u, dummyName)) return false;
    const size_t hdrLen = (method == 1u) ? 25u : 29u;
    if (size < hdrLen) return false;
    const uint32_t compSz = ByteIO::RdU32LE(data + 15u);
    if (compSz > size - hdrLen) return false;
    const size_t nextOff = hdrLen + compSz;
    return nextOff == size || data[nextOff] == 0x1Au;
}

bool SniffSit(const uint8_t* data, size_t size) {
    if (size >= 134u && Formats::DetailStuffItSit::IsSitTopSignature(data) &&
        std::memcmp(data + 10u, "rLau", 4) == 0)
    {
        return true;
    }
    if (size >= 262u && data[0] == 0u && data[1] > 0u && data[1] <= 63u &&
        Formats::DetailStuffItSit::IsSitTopSignature(data + 128u) &&
        std::memcmp(data + 138u, "rLau", 4) == 0)
    {
        return true;
    }
    return false;
}

bool SniffRawSarc(const uint8_t* data, size_t size) {
    return size >= 40u && std::memcmp(data, "SARC", 4) == 0 &&
           ((data[6u] == 0xFFu && data[7u] == 0xFEu) || (data[6u] == 0xFEu && data[7u] == 0xFFu));
}

bool SniffYaz0(const uint8_t* data, size_t size) {
    return size >= 32u && std::memcmp(data, "Yaz0", 4) == 0;
}

bool SniffNarc(const uint8_t* data, size_t size) {
    if (size < 44u || std::memcmp(data, "NARC", 4) != 0) return false;
    const uint16_t hdrSz = ByteIO::RdU16LE(data + 12u);
    if (hdrSz < 16u || size - hdrSz < 12u) return false;
    return std::memcmp(data + hdrSz, "BTAF", 4) == 0 || std::memcmp(data + hdrSz, "FATB", 4) == 0;
}

bool SniffGcf(const uint8_t* data, size_t size) {
    if (size < 160u) return false;
    const uint32_t d0  = ByteIO::RdU32LE(data + 0u);
    const uint32_t maj = ByteIO::RdU32LE(data + 4u);
    const uint32_t minV = ByteIO::RdU32LE(data + 8u);
    if (d0 != 1u || maj != 1u || (minV != 6u && minV != 5u)) return false;
    return ByteIO::RdU32LE(data + 36u) == ByteIO::RdU32LE(data + 44u);
}

constexpr LoaderDispatch::ArchiveEntry kSystemRetroArchiveTable[] = {
    { L".exfat",  Formats::InspectExFatFilesystemImg,  Formats::ExtractExFatFilesystemImgEntry,  SniffExFat   },
    { L".ntfs",   Formats::InspectNtfsFilesystemImg,   Formats::ExtractNtfsFilesystemImgEntry,   SniffNtfs    },
    { L".romfs",  Formats::InspectRomFsFilesystem,     Formats::ExtractRomFsFilesystemEntry,     SniffRomFs   },
    { L".cramfs", Formats::InspectCramFsFilesystem,    Formats::ExtractCramFsFilesystemEntry,    SniffCramFs  },
    { L".alz",    Formats::InspectAlzipAlzArchive,     Formats::ExtractAlzipAlzEntry,            SniffAlz     },
    { L".zoo",    Formats::InspectZooArchive,          Formats::ExtractZooArchiveEntry,          SniffZoo     },
    { L".arc",    Formats::InspectSeaArcArchive,       Formats::ExtractSeaArcEntry,              SniffSeaArc  },
    { L".sit",    Formats::InspectStuffItSitArchive,   Formats::ExtractStuffItSitEntry,          SniffSit     },
    { L".sarc",   Formats::InspectNintendoSarcArchive, Formats::ExtractNintendoSarcEntry,        SniffRawSarc },
    { L".szs",    Formats::InspectNintendoSarcArchive, Formats::ExtractNintendoSarcEntry,        SniffYaz0    },
    { L".narc",   Formats::InspectNintendoNarcArchive, Formats::ExtractNintendoNarcEntry,        SniffNarc    },
    { L".gcf",    Formats::InspectValveGcfArchive,     Formats::ExtractValveGcfEntry,            SniffGcf     },
    { L".ima",    Formats::InspectFatFilesystemImg,    Formats::ExtractFatFilesystemImgEntry                  },
    { L".flp",    Formats::InspectFatFilesystemImg,    Formats::ExtractFatFilesystemImgEntry                  },
};

} // namespace

bool IsSystemRetroArchiveExtension(const std::wstring& extLower) {
    return LoaderDispatch::HasArchiveExt(kSystemRetroArchiveTable, extLower);
}

bool TryInspectSystemRetroArchive(
    const std::wstring& archivePath,
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    ArchiveSummaryInfo& outInfo)
{
    (void)archivePath;
    bool matched = false;
    if (LoaderDispatch::DispatchArchiveInspect(kSystemRetroArchiveTable, data, size, extLower, outInfo, matched)) {
        return true;
    }
    if (matched && extLower != L".ima" && extLower != L".flp" && extLower != L".szs") {
        return false;
    }

    // Multi-filesystem & partitioned raw disk fallback (.img, .ima, .flp, .ext2/.ext3/.ext4, .hfs, raw .dmg)
    return Formats::InspectExFatFilesystemImg(data, size, outInfo) ||
           Formats::InspectFatFilesystemImg(data, size, outInfo) ||
           Formats::InspectNtfsFilesystemImg(data, size, outInfo) ||
           Formats::InspectLinuxExtFilesystem(data, size, outInfo) ||
           Formats::InspectAppleHfsPlusImg(data, size, extLower, outInfo) ||
           Formats::DetailDiskAndGameArc::ParseOrExtractPartitionedRawDiskImg(data, size, extLower, &outInfo, nullptr, nullptr);
}

bool TryExtractSystemRetroArchive(
    const std::wstring& archivePath,
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    (void)archivePath;
    if (normTarget.empty()) return false;
    bool matched = false;
    if (LoaderDispatch::DispatchArchiveExtract(kSystemRetroArchiveTable, data, size, extLower, normTarget, outBytes, matched)) {
        return true;
    }
    if (matched && extLower != L".ima" && extLower != L".flp" && extLower != L".szs") {
        return false;
    }

    // Multi-filesystem & partitioned raw disk fallback (.img, .ima, .flp, .ext2/.ext3/.ext4, .hfs, raw .dmg)
    return Formats::ExtractExFatFilesystemImgEntry(data, size, normTarget, outBytes) ||
           Formats::ExtractFatFilesystemImgEntry(data, size, normTarget, outBytes) ||
           Formats::ExtractNtfsFilesystemImgEntry(data, size, normTarget, outBytes) ||
           Formats::ExtractLinuxExtFilesystemEntry(data, size, normTarget, outBytes) ||
           Formats::ExtractAppleHfsPlusImgEntry(data, size, extLower, normTarget, outBytes) ||
           Formats::DetailDiskAndGameArc::ParseOrExtractPartitionedRawDiskImg(data, size, extLower, nullptr, &normTarget, &outBytes);
}

} // namespace Explorer2::ArchiveReaders
