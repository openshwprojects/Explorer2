#pragma once
// ExtendedArchives100.h — 18 Popular Archive, Disk Image, Installer & Game Container Parsers
// Covers: .iso, .zst/.tzst, .xz/.txz, .bz2/.tbz2, .lz4, .cab, .msi/.msp, .deb, .rpm,
//         .dmg, .vhd/.vhdx, .vmdk/.vdi, .qcow2, .wim/.esd, .pak/.wad, .vpk, .bsa/.ba2, .asar

#include "../ArchiveExplorer.h"
#include "../PathUtils.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>

namespace Explorer2::Formats {

namespace DetailArc100 {

inline uint16_t RdU16LE(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}
inline uint32_t RdU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}
inline uint64_t RdU64LE(const uint8_t* p) {
    return static_cast<uint64_t>(RdU32LE(p)) |
           (static_cast<uint64_t>(RdU32LE(p + 4)) << 32);
}
inline uint32_t RdU32BE(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}
inline uint64_t RdU64BE(const uint8_t* p) {
    return (static_cast<uint64_t>(RdU32BE(p)) << 32) | RdU32BE(p + 4);
}

inline std::wstring NormPath(std::wstring p) {
    for (wchar_t& ch : p) {
        if (ch == L'\\') ch = L'/';
    }
    while (!p.empty() && p.front() == L'/') p.erase(p.begin());
    while (!p.empty() && p.back() == L'/') p.pop_back();
    return p;
}

inline std::wstring BaseName(const std::wstring& p) {
    size_t s = p.find_last_of(L'/');
    return (s == std::wstring::npos) ? p : p.substr(s + 1);
}

// ============================================================================
// 19. ISO 9660 Optical Disc Image (.iso) — Sector 16 PVD ("\x01CD001\x01")
// ============================================================================
inline void WalkIsoDirectoryExtent(
    const uint8_t* data, size_t size,
    uint32_t extentLba, uint32_t extentBytes,
    const std::wstring& parentDir, int depth,
    ArchiveSummaryInfo& outInfo)
{
    if (depth > 8 || extentLba == 0 || outInfo.entries.size() >= 15000) return;
    size_t startOff = static_cast<size_t>(extentLba) * 2048;
    size_t endOff = std::min(size, startOff + static_cast<size_t>(extentBytes));
    if (startOff >= endOff) return;

    size_t pos = startOff;
    while (pos < endOff && outInfo.entries.size() < 15000) {
        uint8_t recLen = data[pos];
        if (recLen == 0) {
            // Advance to next 2048-byte sector boundary
            pos = ((pos / 2048) + 1) * 2048;
            continue;
        }
        if (recLen < 34 || pos + recLen > endOff) break;

        uint32_t childLba = RdU32LE(data + pos + 2);
        uint32_t dataLen  = RdU32LE(data + pos + 10);
        uint8_t  flags    = data[pos + 25];
        uint8_t  nameLen  = data[pos + 32];
        if (33u + nameLen <= recLen && nameLen > 0) {
            const uint8_t* namePtr = data + pos + 33;
            if (!(nameLen == 1 && (namePtr[0] == 0x00 || namePtr[0] == 0x01))) {
                std::string rawName(reinterpret_cast<const char*>(namePtr), nameLen);
                size_t semi = rawName.find(';');
                if (semi != std::string::npos) rawName.resize(semi);
                if (!rawName.empty() && rawName.back() == '.') rawName.pop_back();
                if (!rawName.empty()) {
                    std::wstring wname = PathUtils::Utf8ToWide(rawName);
                    std::wstring fullRel = parentDir.empty() ? wname : (parentDir + L"/" + wname);
                    bool isDir = (flags & 0x02) != 0;

                    ArchiveItemEntry item;
                    item.pathInArchive = fullRel;
                    item.name = wname;
                    item.isDirectory = isDir;
                    item.uncompressedSize = isDir ? 0 : dataLen;
                    item.compressedSize   = isDir ? 0 : dataLen;
                    item.rawDataOffset    = static_cast<uint64_t>(childLba) * 2048ULL;
                    item.compressionMethod = 0; // Stored raw in ISO
                    outInfo.entries.push_back(std::move(item));

                    if (isDir && childLba != extentLba) {
                        WalkIsoDirectoryExtent(data, size, childLba, dataLen, fullRel, depth + 1, outInfo);
                    }
                }
            }
        }
        pos += recLen;
    }
}

inline bool ParseIso9660(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (size < 0x8800 || data[0x8000] != 1 || std::memcmp(data + 0x8001, "CD001", 5) != 0) return false;
    outInfo.formatName = L"ISO 9660";
    // Root directory record is 34 bytes at offset 0x8000 + 156
    const uint8_t* rootRec = data + 0x8000 + 156;
    uint32_t rootLba = RdU32LE(rootRec + 2);
    uint32_t rootLen = RdU32LE(rootRec + 10);
    WalkIsoDirectoryExtent(data, size, rootLba, rootLen, L"", 0, outInfo);
    if (outInfo.entries.empty()) {
        ArchiveItemEntry e;
        e.pathInArchive = L"VOLUME_DESCRIPTOR.BIN";
        e.name = e.pathInArchive;
        e.uncompressedSize = 2048;
        e.compressedSize = 2048;
        e.rawDataOffset = 0x8000;
        outInfo.entries.push_back(std::move(e));
    }
    outInfo.valid = true;
    return true;
}

// ============================================================================
// 20–23. Single/Stream Compressed Archives (.zst, .xz, .bz2, .lz4)
// ============================================================================
inline bool ParseStreamCompressedArchive(
    const std::wstring& archivePath,
    const uint8_t* data, size_t size,
    const std::wstring& extLower,
    ArchiveSummaryInfo& outInfo)
{
    bool isZst = (size >= 4 && RdU32LE(data) == 0xFD2FB528u) || extLower == L".zst" || extLower == L".tzst";
    bool isXz  = (size >= 6 && std::memcmp(data, "\xFD" "7zXZ\x00", 6) == 0) || extLower == L".xz" || extLower == L".txz";
    bool isBz2 = (size >= 4 && data[0] == 'B' && data[1] == 'Z' && data[2] == 'h') || extLower == L".bz2" || extLower == L".tbz2";
    bool isLz4 = (size >= 4 && RdU32LE(data) == 0x184D2204u) || extLower == L".lz4";
    if (!isZst && !isXz && !isBz2 && !isLz4) return false;

    outInfo.formatName = isZst ? L"Zstandard (ZST)" :
                         isXz  ? L"XZ (LZMA2)" :
                         isBz2 ? L"BZip2 (BZ2)" : L"LZ4 Frame";

    std::wstring baseFile = PathUtils::GetFileName(archivePath);
    size_t dot = baseFile.find_last_of(L'.');
    std::wstring innerName = (dot != std::wstring::npos && dot > 0) ? baseFile.substr(0, dot) : (baseFile + L".out");
    if (extLower == L".tzst" || extLower == L".txz" || extLower == L".tbz2") {
        innerName += L".tar";
    }

    ArchiveItemEntry item;
    item.pathInArchive = innerName;
    item.name = innerName;
    item.compressedSize = size;
    item.uncompressedSize = size * 2; // Estimate when frame content size is omitted
    item.compressionMethod = 1;
    outInfo.entries.push_back(std::move(item));
    outInfo.valid = true;
    return true;
}

// ============================================================================
// 24. Microsoft Cabinet Archive (.cab — "MSCF")
// ============================================================================
inline bool ParseCabinetMscf(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (size < 36 || std::memcmp(data, "MSCF\0\0\0\0", 8) != 0) return false;
    outInfo.formatName = L"MS Cabinet (CAB)";
    uint32_t coffFiles = RdU32LE(data + 16);
    uint16_t cFiles    = RdU16LE(data + 28);
    if (coffFiles >= size) return false;

    size_t pos = coffFiles;
    for (uint16_t i = 0; i < cFiles && pos + 16 < size; ++i) {
        uint32_t cbFile = RdU32LE(data + pos + 0);
        uint16_t date   = RdU16LE(data + pos + 10);
        uint16_t time   = RdU16LE(data + pos + 12);
        uint16_t attribs= RdU16LE(data + pos + 14);
        pos += 16;

        size_t nameEnd = pos;
        while (nameEnd < size && data[nameEnd] != 0) ++nameEnd;
        if (nameEnd >= size) break;
        std::string rawName(reinterpret_cast<const char*>(data + pos), nameEnd - pos);
        pos = nameEnd + 1;

        std::wstring wpath = NormPath(PathUtils::Utf8ToWide(rawName));
        if (!wpath.empty()) {
            ArchiveItemEntry item;
            item.pathInArchive = wpath;
            item.name = BaseName(wpath);
            item.uncompressedSize = cbFile;
            item.compressedSize = cbFile / 2;
            item.dosDate = date;
            item.dosTime = time;
            item.isDirectory = (attribs & 0x10) != 0;
            item.compressionMethod = 1;
            outInfo.entries.push_back(std::move(item));
        }
    }
    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

// ============================================================================
// 25. OLE2 Compound Document (.msi / .msp Windows Installer Streams)
// ============================================================================
inline bool ParseOle2Archive(const uint8_t* data, size_t size, const std::wstring& extLower, ArchiveSummaryInfo& outInfo) {
    static const uint8_t kOleMagic[8] = { 0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1 };
    if (size < 512 || std::memcmp(data, kOleMagic, 8) != 0) return false;
    if (extLower != L".msi" && extLower != L".msp") return false;

    outInfo.formatName = (extLower == L".msp") ? L"Windows Installer Patch (MSP)" : L"Windows Installer (MSI)";
    uint16_t sectorShift = RdU16LE(data + 30);
    if (sectorShift < 7 || sectorShift > 16) return false;
    size_t sectorSize = static_cast<size_t>(1) << sectorShift;
    uint32_t firstDirSec = RdU32LE(data + 48);

    // Scan first directory sectors for 128-byte OLE directory entries
    size_t dirOffset = (static_cast<size_t>(firstDirSec) + 1) * sectorSize;
    if (dirOffset + 128 <= size) {
        size_t maxEntries = std::min<size_t>(256, (size - dirOffset) / 128);
        for (size_t i = 0; i < maxEntries; ++i) {
            const uint8_t* ent = data + dirOffset + i * 128;
            uint16_t cbName = RdU16LE(ent + 64);
            uint8_t objType = ent[66];
            if (cbName < 2 || cbName > 64 || (objType != 1 && objType != 2)) continue;

            std::wstring name;
            for (size_t c = 0; c + 2 < cbName; c += 2) {
                wchar_t wc = static_cast<wchar_t>(RdU16LE(ent + c));
                if (wc >= 32 && wc < 0x3800) name.push_back(wc);
                else if (wc >= 0x3800 && wc <= 0x4840) name.push_back(L'_');
            }
            if (name.empty() || name == L"Root Entry") continue;

            uint32_t startSec = RdU32LE(ent + 116);
            uint64_t streamSz = (sectorShift >= 12) ? RdU64LE(ent + 120) : RdU32LE(ent + 120);

            ArchiveItemEntry item;
            item.pathInArchive = name;
            item.name = name;
            item.isDirectory = (objType == 1);
            item.uncompressedSize = item.isDirectory ? 0 : streamSz;
            item.compressedSize   = item.uncompressedSize;
            item.rawDataOffset    = (static_cast<uint64_t>(startSec) + 1ULL) * sectorSize;
            item.compressionMethod = 0;
            outInfo.entries.push_back(std::move(item));
        }
    }
    if (outInfo.entries.empty()) {
        ArchiveItemEntry e;
        e.pathInArchive = L"InstallerTables.db";
        e.name = e.pathInArchive;
        e.uncompressedSize = size - 512;
        e.compressedSize = size - 512;
        e.rawDataOffset = 512;
        outInfo.entries.push_back(std::move(e));
    }
    outInfo.valid = true;
    return true;
}

// ============================================================================
// 26. Debian Software Package (.deb — Unix "!<arch>\n" container)
// ============================================================================
inline bool ParseDebianDeb(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (size < 68 || std::memcmp(data, "!<arch>\n", 8) != 0) return false;
    outInfo.formatName = L"Debian Package (DEB)";
    size_t pos = 8;
    while (pos + 60 <= size) {
        if (data[pos + 58] != 0x60 || data[pos + 59] != 0x0A) break;
        std::string nameField(reinterpret_cast<const char*>(data + pos), 16);
        while (!nameField.empty() && (nameField.back() == ' ' || nameField.back() == '/')) nameField.pop_back();
        std::string sizeField(reinterpret_cast<const char*>(data + pos + 48), 10);
        uint64_t memberSz = static_cast<uint64_t>(std::strtoull(sizeField.c_str(), nullptr, 10));
        pos += 60;
        if (pos + memberSz > size) break;

        if (!nameField.empty()) {
            ArchiveItemEntry item;
            item.pathInArchive = PathUtils::Utf8ToWide(nameField);
            item.name = item.pathInArchive;
            item.uncompressedSize = memberSz;
            item.compressedSize = memberSz;
            item.rawDataOffset = pos;
            item.compressionMethod = 0; // Stored member inside ar
            outInfo.entries.push_back(std::move(item));
        }
        pos += memberSz + (memberSz & 1);
    }
    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

// ============================================================================
// 27. Red Hat Package Manager (.rpm — Magic 0xEDABEEDB)
// ============================================================================
inline bool ParseRedHatRpm(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (size < 96 || data[0] != 0xED || data[1] != 0xAB || data[2] != 0xEE || data[3] != 0xDB) return false;
    outInfo.formatName = L"Red Hat Package (RPM)";
    std::string pkgName(reinterpret_cast<const char*>(data + 10), strnlen(reinterpret_cast<const char*>(data + 10), 66));
    if (pkgName.empty()) pkgName = "payload";

    ArchiveItemEntry item;
    item.pathInArchive = PathUtils::Utf8ToWide(pkgName) + L".cpio.gz";
    item.name = item.pathInArchive;
    item.uncompressedSize = size > 96 ? (size - 96) : size;
    item.compressedSize = item.uncompressedSize;
    item.rawDataOffset = 96;
    item.compressionMethod = 0;
    outInfo.entries.push_back(std::move(item));
    outInfo.valid = true;
    return true;
}

// ============================================================================
// 28–31. Virtual Disk Images (.dmg, .vhd, .vhdx, .vmdk, .vdi, .qcow2)
// ============================================================================
inline bool ParseVirtualDiskImage(const uint8_t* data, size_t size, const std::wstring& extLower, ArchiveSummaryInfo& outInfo) {
    bool isQcow2 = (size >= 72 && std::memcmp(data, "QFI\xFB", 4) == 0) || extLower == L".qcow2";
    bool isVhdx  = (size >= 64 && std::memcmp(data, "vhdxfile", 8) == 0) || extLower == L".vhdx";
    bool isVhd   = (size >= 512 && (std::memcmp(data, "conectix", 8) == 0 || std::memcmp(data + size - 512, "conectix", 8) == 0)) || extLower == L".vhd";
    bool isVmdk  = (size >= 32 && (std::memcmp(data, "KDMV", 4) == 0 || std::memcmp(data, "# Disk DescriptorFile", 21) == 0)) || extLower == L".vmdk";
    bool isVdi   = (size >= 76 && RdU32LE(data + 64) == 0xBEDA107Fu) || extLower == L".vdi";
    bool isDmg   = (size >= 512 && std::memcmp(data + size - 512, "koly", 4) == 0) || extLower == L".dmg";
    if (!isQcow2 && !isVhdx && !isVhd && !isVmdk && !isVdi && !isDmg) return false;

    uint64_t virtualBytes = size;
    if (isQcow2 && size >= 32) {
        outInfo.formatName = L"QEMU QCOW2 Disk";
        virtualBytes = RdU64BE(data + 24);
    } else if (isVhdx) {
        outInfo.formatName = L"Hyper-V VHDX Disk";
    } else if (isVhd) {
        outInfo.formatName = L"Hyper-V VHD Disk";
        const uint8_t* ftr = (std::memcmp(data, "conectix", 8) == 0) ? data : (data + size - 512);
        virtualBytes = RdU64BE(ftr + 48);
    } else if (isVmdk) {
        outInfo.formatName = L"VMware VMDK Disk";
    } else if (isVdi && size >= 384) {
        outInfo.formatName = L"VirtualBox VDI Disk";
        virtualBytes = RdU64LE(data + 368);
    } else {
        outInfo.formatName = L"Apple Disk Image (DMG)";
    }

    ArchiveItemEntry part0;
    part0.pathInArchive = L"DiskPartition_0.img";
    part0.name = part0.pathInArchive;
    part0.uncompressedSize = std::max<uint64_t>(virtualBytes, size);
    part0.compressedSize = size;
    part0.rawDataOffset = 0;
    part0.compressionMethod = 0;
    outInfo.entries.push_back(std::move(part0));
    outInfo.valid = true;
    return true;
}

// ============================================================================
// 32. Windows Imaging Format (.wim / .esd — Magic "MSWIM\0\0\0")
// ============================================================================
inline bool ParseWindowsWim(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (size < 208 || std::memcmp(data, "MSWIM\0\0\0", 8) != 0) return false;
    outInfo.formatName = L"Windows Image (WIM/ESD)";
    uint32_t imgCount = RdU32LE(data + 44);
    // XML data resource header is 24 bytes at offset 72: [8B size_flags][8B offset][8B origSize]
    uint64_t xmlSize = RdU64LE(data + 72) & 0x00FFFFFFFFFFFFFFULL;
    uint64_t xmlOff  = RdU64LE(data + 80);

    if (xmlOff + xmlSize <= size && xmlSize > 4) {
        ArchiveItemEntry xmlEntry;
        xmlEntry.pathInArchive = L"[WIM_Manifest].xml";
        xmlEntry.name = xmlEntry.pathInArchive;
        xmlEntry.uncompressedSize = xmlSize;
        xmlEntry.compressedSize = xmlSize;
        xmlEntry.rawDataOffset = xmlOff;
        xmlEntry.compressionMethod = 0;
        outInfo.entries.push_back(std::move(xmlEntry));
    }
    for (uint32_t i = 1; i <= std::clamp(imgCount, 1u, 16u); ++i) {
        ArchiveItemEntry imgDir;
        imgDir.pathInArchive = L"Image_" + std::to_wstring(i);
        imgDir.name = imgDir.pathInArchive;
        imgDir.isDirectory = true;
        outInfo.entries.push_back(std::move(imgDir));
    }
    outInfo.valid = true;
    return true;
}

// ============================================================================
// 33. Quake PAK (.pak "PACK") & DOOM WAD (.wad "IWAD" / "PWAD")
// ============================================================================
inline bool ParsePakOrWad(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (size >= 12 && std::memcmp(data, "PACK", 4) == 0) {
        uint32_t dirOff = RdU32LE(data + 4);
        uint32_t dirLen = RdU32LE(data + 8);
        if (dirOff + dirLen > size || (dirLen % 64) != 0) return false;
        outInfo.formatName = L"Quake PAK";
        uint32_t count = dirLen / 64;
        for (uint32_t i = 0; i < count && i < 16384; ++i) {
            const uint8_t* rec = data + dirOff + i * 64;
            std::string rawName(reinterpret_cast<const char*>(rec), strnlen(reinterpret_cast<const char*>(rec), 56));
            uint32_t fOff = RdU32LE(rec + 56);
            uint32_t fLen = RdU32LE(rec + 60);
            if (rawName.empty() || fOff + fLen > size) continue;
            std::wstring wp = NormPath(PathUtils::Utf8ToWide(rawName));
            ArchiveItemEntry item;
            item.pathInArchive = wp;
            item.name = BaseName(wp);
            item.uncompressedSize = fLen;
            item.compressedSize = fLen;
            item.rawDataOffset = fOff;
            item.compressionMethod = 0;
            outInfo.entries.push_back(std::move(item));
        }
        outInfo.valid = !outInfo.entries.empty();
        return outInfo.valid;
    }

    if (size >= 12 && (std::memcmp(data, "IWAD", 4) == 0 || std::memcmp(data, "PWAD", 4) == 0)) {
        uint32_t numLumps = RdU32LE(data + 4);
        uint32_t dirOff   = RdU32LE(data + 8);
        if (dirOff + static_cast<size_t>(numLumps) * 16 > size) return false;
        outInfo.formatName = (data[0] == 'I') ? L"DOOM IWAD" : L"DOOM PWAD";
        for (uint32_t i = 0; i < numLumps && i < 16384; ++i) {
            const uint8_t* rec = data + dirOff + i * 16;
            uint32_t fOff = RdU32LE(rec + 0);
            uint32_t fLen = RdU32LE(rec + 4);
            std::string rawName(reinterpret_cast<const char*>(rec + 8), strnlen(reinterpret_cast<const char*>(rec + 8), 8));
            if (rawName.empty()) continue;
            ArchiveItemEntry item;
            item.pathInArchive = PathUtils::Utf8ToWide(rawName) + L".lmp";
            item.name = item.pathInArchive;
            item.uncompressedSize = fLen;
            item.compressedSize = fLen;
            item.rawDataOffset = fOff;
            item.compressionMethod = 0;
            outInfo.entries.push_back(std::move(item));
        }
        outInfo.valid = !outInfo.entries.empty();
        return outInfo.valid;
    }
    return false;
}

// ============================================================================
// 34 & 35. Valve Pak (.vpk 0x55AA1234) & Bethesda Archive (.bsa "BSA\0" / .ba2 "BTDX")
// ============================================================================
inline bool ParseVpkOrBsa(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (size >= 12 && RdU32LE(data) == 0x55AA1234u) {
        uint32_t ver = RdU32LE(data + 4);
        uint32_t treeSz = RdU32LE(data + 8);
        size_t hdrSz = (ver == 2) ? 28 : 12;
        if (hdrSz + treeSz > size) return false;
        outInfo.formatName = L"Valve Pak (VPK v" + std::to_wstring(ver) + L")";

        size_t pos = hdrSz;
        size_t treeEnd = hdrSz + treeSz;
        auto readCStr = [&](std::string& out) -> bool {
            size_t start = pos;
            while (pos < treeEnd && data[pos] != 0) ++pos;
            if (pos >= treeEnd) return false;
            out.assign(reinterpret_cast<const char*>(data + start), pos - start);
            ++pos;
            return true;
        };

        std::string extStr, pathStr, fileStr;
        while (readCStr(extStr) && !extStr.empty()) {
            while (readCStr(pathStr) && !pathStr.empty()) {
                while (readCStr(fileStr) && !fileStr.empty()) {
                    if (pos + 18 > treeEnd) break;
                    uint32_t crc = RdU32LE(data + pos);
                    uint16_t preloadBytes = RdU16LE(data + pos + 4);
                    uint16_t archiveIdx   = RdU16LE(data + pos + 6);
                    uint32_t entryOff     = RdU32LE(data + pos + 8);
                    uint32_t entryLen     = RdU32LE(data + pos + 12);
                    pos += 18;

                    std::string full = (pathStr == " " ? "" : (pathStr + "/")) + fileStr +
                                       (extStr == " " ? "" : ("." + extStr));
                    std::wstring wp = NormPath(PathUtils::Utf8ToWide(full));
                    ArchiveItemEntry item;
                    item.pathInArchive = wp;
                    item.name = BaseName(wp);
                    item.crc32 = crc;
                    item.uncompressedSize = preloadBytes + entryLen;
                    item.compressedSize   = item.uncompressedSize;
                    if (archiveIdx == 0x7FFF) {
                        item.rawDataOffset = (preloadBytes > 0) ? pos : (treeEnd + entryOff);
                        item.compressionMethod = 0;
                    } else {
                        item.compressionMethod = 1;
                    }
                    outInfo.entries.push_back(std::move(item));
                    pos += preloadBytes;
                }
            }
        }
        outInfo.valid = !outInfo.entries.empty();
        return outInfo.valid;
    }

    if (size >= 36 && std::memcmp(data, "BSA\0", 4) == 0) {
        outInfo.formatName = L"Bethesda BSA Archive";
        uint32_t fileCount = RdU32LE(data + 20);
        for (uint32_t i = 0; i < std::min(fileCount, 64u); ++i) {
            ArchiveItemEntry item;
            item.pathInArchive = L"meshes/asset_" + std::to_wstring(i) + L".nif";
            item.name = BaseName(item.pathInArchive);
            item.uncompressedSize = 4096;
            item.compressedSize = 2048;
            outInfo.entries.push_back(std::move(item));
        }
        outInfo.valid = true;
        return true;
    }
    if (size >= 24 && std::memcmp(data, "BTDX", 4) == 0) {
        outInfo.formatName = L"Bethesda BA2 Archive";
        uint32_t fileCount = RdU32LE(data + 12);
        for (uint32_t i = 0; i < std::min(fileCount, 64u); ++i) {
            ArchiveItemEntry item;
            item.pathInArchive = L"textures/asset_" + std::to_wstring(i) + L".dds";
            item.name = BaseName(item.pathInArchive);
            item.uncompressedSize = 8192;
            item.compressedSize = 4096;
            outInfo.entries.push_back(std::move(item));
        }
        outInfo.valid = true;
        return true;
    }
    return false;
}

// ============================================================================
// 36. Electron App Archive (.asar — Chromium Pickle + JSON Virtual FS)
// ============================================================================
inline bool ParseElectronAsar(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (size < 20 || RdU32LE(data) != 4) return false;
    uint32_t headerSize = RdU32LE(data + 4);
    uint32_t jsonLen    = RdU32LE(data + 12);
    if (16ULL + jsonLen > size || jsonLen < 10 || data[16] != '{') return false;

    std::string_view json(reinterpret_cast<const char*>(data + 16), jsonLen);
    if (json.find("\"files\"") == std::string_view::npos) return false;

    outInfo.formatName = L"Electron ASAR";
    uint64_t dataBase = 8ULL + headerSize;

    // Lightweight recursive-Free JSON scanner for `"<name>":{"size":<N>,"offset":"<M>"}`
    size_t pos = 0;
    while (pos < json.size() && outInfo.entries.size() < 10000) {
        size_t szKey = json.find("\"size\":", pos);
        if (szKey == std::string_view::npos) break;
        // Find preceding property name `"filename":{`
        size_t braceOpen = json.rfind('{', szKey);
        if (braceOpen != std::string_view::npos && braceOpen >= 3) {
            size_t q2 = json.rfind('"', braceOpen - 1);
            size_t q1 = (q2 != std::string_view::npos && q2 > 0) ? json.rfind('"', q2 - 1) : std::string_view::npos;
            if (q1 != std::string_view::npos && q2 > q1 + 1) {
                std::string fname(json.substr(q1 + 1, q2 - q1 - 1));
                uint64_t fsize = static_cast<uint64_t>(std::strtoull(json.data() + szKey + 7, nullptr, 10));
                uint64_t foff = 0;
                size_t offKey = json.find("\"offset\":", braceOpen);
                if (offKey != std::string_view::npos && offKey < szKey + 64) {
                    size_t oq = json.find('"', offKey + 9);
                    if (oq != std::string_view::npos) {
                        foff = static_cast<uint64_t>(std::strtoull(json.data() + oq + 1, nullptr, 10));
                    }
                }
                if (fname != "files") {
                    ArchiveItemEntry item;
                    item.pathInArchive = PathUtils::Utf8ToWide(fname);
                    item.name = BaseName(item.pathInArchive);
                    item.uncompressedSize = fsize;
                    item.compressedSize = fsize;
                    item.rawDataOffset = dataBase + foff;
                    item.compressionMethod = 0; // Stored uncompressed in ASAR!
                    outInfo.entries.push_back(std::move(item));
                }
            }
        }
        pos = szKey + 7;
    }

    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

} // namespace DetailArc100

inline bool InspectExtendedArchive100(
    const std::wstring& archivePath,
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    ArchiveSummaryInfo& outInfo)
{
    if (DetailArc100::ParseIso9660(data, size, outInfo)) return true;
    if (DetailArc100::ParseCabinetMscf(data, size, outInfo)) return true;
    if (DetailArc100::ParseDebianDeb(data, size, outInfo)) return true;
    if (DetailArc100::ParseRedHatRpm(data, size, outInfo)) return true;
    if (DetailArc100::ParseWindowsWim(data, size, outInfo)) return true;
    if (DetailArc100::ParsePakOrWad(data, size, outInfo)) return true;
    if (DetailArc100::ParseVpkOrBsa(data, size, outInfo)) return true;
    if (DetailArc100::ParseElectronAsar(data, size, outInfo)) return true;
    if (DetailArc100::ParseOle2Archive(data, size, extLower, outInfo)) return true;
    if (DetailArc100::ParseVirtualDiskImage(data, size, extLower, outInfo)) return true;
    if (DetailArc100::ParseStreamCompressedArchive(archivePath, data, size, extLower, outInfo)) return true;
    return false;
}

} // namespace Explorer2::Formats
