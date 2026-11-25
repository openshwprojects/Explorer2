#pragma once
// WindowsInstallerMsi.h — Windows Installer & Patch OLE2 + embedded CAB reader (.msi, .msp).

#include "../common/ArchiveFormatCommon.h"
#include "CabinetMscf.h"

namespace Explorer2::Formats {

namespace DetailDiskAndGameArc {

// ============================================================================
// 25. Windows Installer (.msi / .msp) via Ole2Compound + Embedded MSCF CABs
// ============================================================================
inline bool ParseOle2Archive(const uint8_t* data, size_t size, const std::wstring& extLower, ArchiveSummaryInfo& outInfo) {
    static const uint8_t kOleMagic[8] = { 0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1 };
    if (size < 512 || std::memcmp(data, kOleMagic, 8) != 0) return false;
    if (extLower != L".msi" && extLower != L".msp") return false;

    Ole2::CompoundFile cf;
    if (!Ole2::ParseCompoundFile(data, size, cf, /*demangleMsi=*/true) || !cf.valid) {
        return false;
    }

    outInfo.formatName = (extLower == L".msp") ? L"Windows Installer Patch (MSP)" : L"Windows Installer (MSI)";
    for (const auto& de : cf.entries) {
        if (de.objectType != 1 && de.objectType != 2) continue;
        std::wstring path = ArchivePath::Normalize(de.fullPath);
        if (path.empty()) continue;

        ArchiveItemEntry item;
        item.pathInArchive = path;
        item.name = ArchivePath::BaseName(path);
        item.isDirectory = (de.objectType == 1);
        item.uncompressedSize = item.isDirectory ? 0 : de.size;
        item.compressedSize   = item.uncompressedSize;
        item.compressionMethod = 2; // Extracted via Ole2::ExtractStream

        // Check if this stream is an embedded Cabinet ("MSCF\0\0\0\0")
        bool isEmbeddedCab = false;
        std::vector<uint8_t> streamBytes;
        if (de.objectType == 2 && de.size >= 36 && de.size <= 128 * 1024 * 1024) {
            if (Ole2::ExtractStream(cf, de, streamBytes) &&
                streamBytes.size() >= 36 &&
                std::memcmp(streamBytes.data(), "MSCF\0\0\0\0", 8) == 0)
            {
                isEmbeddedCab = true;
            }
        }

        outInfo.entries.push_back(std::move(item));
        if (isEmbeddedCab) {
            ParseCabinetMscf(streamBytes.data(), streamBytes.size(), outInfo, path);
        }
    }

    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

inline bool ExtractOle2ArchiveEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    const std::wstring& targetNorm,
    std::vector<uint8_t>& outBytes)
{
    static const uint8_t kOleMagic[8] = { 0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1 };
    if (size < 512 || std::memcmp(data, kOleMagic, 8) != 0) return false;
    if (extLower != L".msi" && extLower != L".msp") return false;

    Ole2::CompoundFile cf;
    if (!Ole2::ParseCompoundFile(data, size, cf, /*demangleMsi=*/true) || !cf.valid) return false;

    // 1. Direct MSI/MSP stream match
    for (const auto& de : cf.entries) {
        if (de.objectType != 2) continue;
        std::wstring path = ArchivePath::Normalize(de.fullPath);
        if (PathEqualsCI(path, targetNorm)) {
            return Ole2::ExtractStream(cf, de, outBytes);
        }
    }

    // 2. Inner file inside an embedded MSCF Cabinet stream ("<CabStream>/<innerPath>")
    for (const auto& de : cf.entries) {
        if (de.objectType != 2 || de.size < 36) continue;
        std::wstring cabPath = ArchivePath::Normalize(de.fullPath);
        if (cabPath.empty() || targetNorm.size() <= cabPath.size() + 1) continue;
        if (_wcsnicmp(targetNorm.c_str(), cabPath.c_str(), cabPath.size()) == 0 &&
            targetNorm[cabPath.size()] == L'/')
        {
            std::vector<uint8_t> cabStream;
            if (Ole2::ExtractStream(cf, de, cabStream) &&
                cabStream.size() >= 36 &&
                std::memcmp(cabStream.data(), "MSCF\0\0\0\0", 8) == 0)
            {
                std::wstring innerCabTarget = targetNorm.substr(cabPath.size() + 1);
                return StreamCodecs::ExtractCabEntryFromMemory(cabStream.data(), cabStream.size(), innerCabTarget, outBytes);
            }
        }
    }
    return false;
}

} // namespace DetailDiskAndGameArc

inline bool InspectWindowsInstallerMsi(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    ArchiveSummaryInfo& outInfo)
{
    return DetailDiskAndGameArc::ParseOle2Archive(data, size, extLower, outInfo);
}

inline bool ExtractWindowsInstallerMsiEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    return DetailDiskAndGameArc::ExtractOle2ArchiveEntry(data, size, extLower, normTarget, outBytes);
}

} // namespace Explorer2::Formats
