#pragma once
// ExtendedArchives100.h — Popular Archive, Disk Image, Installer & Game Container Parsers + Extractors
// Covers: .iso, .zst/.tzst, .xz/.txz, .bz2/.tbz2, .lz4, .cab, .msi/.msp, .deb, .rpm,
//         .dmg, .vhd/.vhdx, .vmdk/.vdi, .qcow2, .wim/.esd, .pak/.wad, .vpk, .bsa/.ba2, .asar

#include "../ArchiveExplorer.h"
#include "../ZipDeflate.h"
#include "../Ole2Compound.h"
#include "../PathUtils.h"
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <set>
#include <unordered_map>
#include <functional>
#include <algorithm>
#include <memory>

namespace Explorer2::StreamCodecs {

bool DecompressLzma1(
    uint8_t propsByte,
    uint32_t dictSize,
    const uint8_t* src,
    size_t srcLen,
    std::vector<uint8_t>& out,
    uint64_t uncompressedLen,
    bool hasKnownSize = true,
    size_t maxOut = 128 * 1024 * 1024);

bool DecompressLzma2(
    uint8_t dictPropByte,
    const uint8_t* src,
    size_t srcLen,
    std::vector<uint8_t>& out,
    size_t maxOut = 128 * 1024 * 1024,
    size_t* outBytesConsumed = nullptr,
    uint64_t expectedOut = 0);

void ApplyBcjX86Decode(uint8_t* data, size_t size, uint32_t ip = 0);
void ApplyXzDeltaDecode(uint8_t* data, size_t size, size_t dist);

bool DecompressXzStream(const uint8_t* data, size_t size, std::vector<uint8_t>& out, size_t maxOut = 128 * 1024 * 1024);
bool DecompressBzip2Stream(const uint8_t* data, size_t size, std::vector<uint8_t>& out, size_t maxOut = 128 * 1024 * 1024);
bool DecompressLz4Frame(const uint8_t* data, size_t size, std::vector<uint8_t>& out, size_t maxOut = 128 * 1024 * 1024);
bool DecompressZstdFrame(const uint8_t* data, size_t size, std::vector<uint8_t>& out, size_t maxOut = 128 * 1024 * 1024);

bool ParsePosixTarFromMemory(
    const uint8_t* buf,
    size_t len,
    ArchiveSummaryInfo& outInfo,
    const std::wstring* extractTargetNorm = nullptr,
    std::vector<uint8_t>* outExtracted = nullptr);

bool ExtractCabEntryFromFile(
    const std::wstring& cabFilePath,
    const std::wstring& targetNorm,
    std::vector<uint8_t>& outBytes);

bool ExtractCabEntryFromMemory(
    const uint8_t* cabBytes,
    size_t cabSize,
    const std::wstring& targetNorm,
    std::vector<uint8_t>& outBytes);

} // namespace Explorer2::StreamCodecs

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
inline uint16_t RdU16BE(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
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
inline void WrU32LE(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFFu);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    p[2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
    p[3] = static_cast<uint8_t>((v >> 24) & 0xFFu);
}

inline std::wstring NormPath(std::wstring p) {
    for (wchar_t& ch : p) {
        if (ch == L'\\') ch = L'/';
    }
    while (!p.empty() && p.front() == L'/') p.erase(p.begin());
    while (!p.empty() && p.back() == L'/') p.pop_back();
    while (p.rfind(L"./", 0) == 0) p.erase(0, 2);
    return p;
}

inline std::wstring BaseName(const std::wstring& p) {
    size_t s = p.find_last_of(L'/');
    return (s == std::wstring::npos) ? p : p.substr(s + 1);
}

inline bool PathEqualsCI(const std::wstring& a, const std::wstring& b) {
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

inline void FileTimeU64ToDos(uint64_t ftTicks, uint16_t& outDate, uint16_t& outTime) {
    outDate = 0;
    outTime = 0;
    if (ftTicks == 0) return;
    FILETIME utcFt = {}, locFt = {};
    utcFt.dwLowDateTime = static_cast<DWORD>(ftTicks & 0xFFFFFFFFu);
    utcFt.dwHighDateTime = static_cast<DWORD>(ftTicks >> 32);
    if (FileTimeToLocalFileTime(&utcFt, &locFt)) {
        FileTimeToDosDateTime(&locFt, &outDate, &outTime);
    }
}

// ============================================================================
// Abstract Virtual Disk / Raw Reader Interface
// ============================================================================
struct VirtualDiskReader {
    uint64_t virtualSize = 0;
    std::wstring formatName;
    bool directRawBuffer = false; // true when diskOff maps 1:1 to host file offset
    std::function<bool(uint64_t diskOff, uint8_t* dst, size_t len)> readBytes;
};

// ============================================================================
// 19. ISO 9660 Optical Disc Image (.iso) — PVD + Joliet UTF-16BE + Rock Ridge NM
// ============================================================================
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

    uint32_t clampedBytes = std::min<uint32_t>(extentBytes, 4 * 1024 * 1024);
    std::vector<uint8_t> dirBuf(clampedBytes);
    uint64_t startOff = partBase + static_cast<uint64_t>(extentLba) * 2048ULL;
    if (startOff + clampedBytes > reader.virtualSize) {
        if (startOff >= reader.virtualSize) return;
        clampedBytes = static_cast<uint32_t>(reader.virtualSize - startOff);
        dirBuf.resize(clampedBytes);
    }
    if (!reader.readBytes(startOff, dirBuf.data(), clampedBytes)) return;

    size_t pos = 0;
    while (pos < clampedBytes && outInfo.entries.size() < 25000) {
        uint8_t recLen = dirBuf[pos];
        if (recLen == 0) {
            pos = ((pos / 2048) + 1) * 2048;
            continue;
        }
        if (recLen < 34 || pos + recLen > clampedBytes) break;

        const uint8_t* rec = dirBuf.data() + pos;
        uint32_t childLba = RdU32LE(rec + 2);
        uint32_t dataLen  = RdU32LE(rec + 10);
        uint8_t  flags    = rec[25];
        std::wstring wname = DecodeIsoEntryName(rec, recLen, isJoliet);
        if (!wname.empty()) {
            std::wstring fullRel = NormPath(parentDir.empty() ? wname : (parentDir + L"/" + wname));
            bool isDir = (flags & 0x02u) != 0;

            if (extractTarget && outExtracted && outFoundExtract) {
                if (!isDir && PathEqualsCI(fullRel, *extractTarget)) {
                    if (dataLen <= 128 * 1024 * 1024) {
                        outExtracted->resize(dataLen);
                        if (dataLen == 0 || reader.readBytes(partBase + static_cast<uint64_t>(childLba) * 2048ULL, outExtracted->data(), dataLen)) {
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
                item.name = BaseName(fullRel);
                item.isDirectory = isDir;
                item.uncompressedSize = isDir ? 0 : dataLen;
                item.compressedSize   = isDir ? 0 : dataLen;
                item.rawDataOffset    = partBase + static_cast<uint64_t>(childLba) * 2048ULL;
                item.compressionMethod = reader.directRawBuffer ? 0 : 1;
                outInfo.entries.push_back(std::move(item));
            }

            if (isDir && childLba != extentLba) {
                WalkIsoDirectoryReader(reader, partBase, childLba, dataLen, isJoliet, fullRel,
                                       depth + 1, visitedExtents, outInfo, extractTarget, outExtracted, outFoundExtract);
                if (outFoundExtract && *outFoundExtract) return;
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
    const std::wstring* extractTarget = nullptr,
    std::vector<uint8_t>* outExtracted = nullptr)
{
    if (partBase + 0x8800ULL > reader.virtualSize) return false;
    uint8_t vd[2048] = {};
    if (!reader.readBytes(partBase + 0x8000ULL, vd, 2048)) return false;
    if (vd[0] != 1 || std::memcmp(vd + 1, "CD001", 5) != 0) return false;

    uint32_t rootLba = RdU32LE(vd + 156 + 2);
    uint32_t rootLen = RdU32LE(vd + 156 + 10);
    bool useJoliet = false;

    // Scan subsequent volume descriptors for Joliet UTF-16BE SVD (type 2)
    for (uint32_t sec = 17; sec < 32; ++sec) {
        uint64_t off = partBase + static_cast<uint64_t>(sec) * 2048ULL;
        if (off + 2048 > reader.virtualSize) break;
        uint8_t svd[2048] = {};
        if (!reader.readBytes(off, svd, 2048) || std::memcmp(svd + 1, "CD001", 5) != 0) break;
        if (svd[0] == 255) break; // Volume Descriptor Set Terminator
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

inline bool ParseIso9660(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo,
                         const std::wstring* extractTarget = nullptr, std::vector<uint8_t>* outExtracted = nullptr)
{
    if (size < 0x8800 || data[0x8000] != 1 || std::memcmp(data + 0x8001, "CD001", 5) != 0) return false;
    VirtualDiskReader raw;
    raw.virtualSize = size;
    raw.formatName = L"ISO 9660";
    raw.directRawBuffer = true;
    raw.readBytes = [data, size](uint64_t off, uint8_t* dst, size_t len) -> bool {
        if (off + len > size) return false;
        std::memcpy(dst, data + static_cast<size_t>(off), len);
        return true;
    };

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

// ============================================================================
// 24. Microsoft Cabinet Archive (.cab — "MSCF")
// ============================================================================
inline bool ParseCabinetMscf(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo, const std::wstring& prefix = L"") {
    if (size < 36 || std::memcmp(data, "MSCF\0\0\0\0", 8) != 0) return false;
    if (prefix.empty()) outInfo.formatName = L"MS Cabinet (CAB)";
    uint32_t cbCabinet = RdU32LE(data + 8);
    uint32_t coffFiles = RdU32LE(data + 16);
    uint16_t cFiles    = RdU16LE(data + 28);
    if (coffFiles >= size || cFiles == 0) return false;

    uint64_t totalUnp = 0;
    size_t scanPos = coffFiles;
    for (uint16_t i = 0; i < cFiles && scanPos + 16 < size; ++i) {
        totalUnp += RdU32LE(data + scanPos);
        scanPos += 16;
        while (scanPos < size && data[scanPos] != 0) ++scanPos;
        if (scanPos >= size) break;
        ++scanPos;
    }
    uint64_t payloadPool = (cbCabinet > coffFiles && cbCabinet <= size) ? (cbCabinet - coffFiles) : (size - coffFiles);

    size_t pos = coffFiles;
    size_t added = 0;
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

        std::wstring wpath = NormPath((attribs & 0x80u)
            ? PathUtils::Utf8ToWide(rawName)
            : PathUtils::Utf8ToWide(rawName));
        if (!wpath.empty()) {
            std::wstring full = prefix.empty() ? wpath : NormPath(prefix + L"/" + wpath);
            ArchiveItemEntry item;
            item.pathInArchive = full;
            item.name = BaseName(full);
            item.uncompressedSize = cbFile;
            item.compressedSize = (totalUnp > 0) ? ((payloadPool * cbFile) / totalUnp) : cbFile;
            item.dosDate = date;
            item.dosTime = time;
            item.isDirectory = (attribs & 0x10u) != 0;
            item.compressionMethod = 1; // Extracted via SetupIterateCabinetW
            outInfo.entries.push_back(std::move(item));
            ++added;
        }
    }
    if (prefix.empty()) outInfo.valid = (added > 0);
    return added > 0;
}

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
        std::wstring path = NormPath(de.fullPath);
        if (path.empty()) continue;

        ArchiveItemEntry item;
        item.pathInArchive = path;
        item.name = BaseName(path);
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
        std::wstring path = NormPath(de.fullPath);
        if (PathEqualsCI(path, targetNorm)) {
            return Ole2::ExtractStream(cf, de, outBytes);
        }
    }

    // 2. Inner file inside an embedded MSCF Cabinet stream ("<CabStream>/<innerPath>")
    for (const auto& de : cf.entries) {
        if (de.objectType != 2 || de.size < 36) continue;
        std::wstring cabPath = NormPath(de.fullPath);
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

// ============================================================================
// 26. Debian Software Package (.deb — Unix "!<arch>\n" container + data.tar.*)
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
            item.compressionMethod = 0;
            outInfo.entries.push_back(std::move(item));
        }
        pos += static_cast<size_t>(memberSz + (memberSz & 1ULL));
    }
    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

// ============================================================================
// 27. Red Hat Package Manager (.rpm — Lead + Signature + Metadata Tags + CPIO)
// ============================================================================
inline bool DecompressRpmPayloadToCpio(
    const uint8_t* payload,
    size_t payloadLen,
    const std::string& compressorHint,
    std::vector<uint8_t>& outCpio,
    const uint8_t*& outRawPtr,
    size_t& outRawLen)
{
    outCpio.clear();
    outRawPtr = nullptr;
    outRawLen = 0;
    if (!payload || payloadLen < 6) return false;

    if (payloadLen >= 6 && (std::memcmp(payload, "070701", 6) == 0 ||
                            std::memcmp(payload, "070702", 6) == 0 ||
                            std::memcmp(payload, "070707", 6) == 0))
    {
        outRawPtr = payload;
        outRawLen = payloadLen;
        return true;
    }

    bool ok = false;
    if (payloadLen >= 18 && payload[0] == 0x1F && payload[1] == 0x8B) {
        ok = ZipDeflate::InflateGzip(payload, payloadLen, outCpio, 128 * 1024 * 1024);
    } else if (payloadLen >= 12 && std::memcmp(payload, "\xFD" "7zXZ\x00", 6) == 0) {
        ok = StreamCodecs::DecompressXzStream(payload, payloadLen, outCpio, 128 * 1024 * 1024);
    } else if (payloadLen >= 4 && RdU32LE(payload) == 0xFD2FB528u) {
        ok = StreamCodecs::DecompressZstdFrame(payload, payloadLen, outCpio, 128 * 1024 * 1024);
    } else if (payloadLen >= 4 && payload[0] == 'B' && payload[1] == 'Z' && payload[2] == 'h') {
        ok = StreamCodecs::DecompressBzip2Stream(payload, payloadLen, outCpio, 128 * 1024 * 1024);
    } else if (payloadLen >= 4 && RdU32LE(payload) == 0x184D2204u) {
        ok = StreamCodecs::DecompressLz4Frame(payload, payloadLen, outCpio, 128 * 1024 * 1024);
    } else if (compressorHint == "lzma" && payloadLen >= 14) {
        uint8_t props = payload[0];
        uint32_t dict = RdU32LE(payload + 1);
        uint64_t unp = RdU64LE(payload + 5);
        bool known = (unp != 0xFFFFFFFFFFFFFFFFULL && unp <= 128 * 1024 * 1024);
        ok = StreamCodecs::DecompressLzma1(props, dict, payload + 13, payloadLen - 13, outCpio, known ? unp : 0, known);
    }
    if (!ok || outCpio.size() < 6) return false;
    outRawPtr = outCpio.data();
    outRawLen = outCpio.size();
    return true;
}

inline bool WalkCpioBuffer(
    const uint8_t* cpio,
    size_t cpioLen,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm,
    std::vector<uint8_t>* outExtracted)
{
    if (!cpio || cpioLen < 76) return false;
    bool isNewc = (cpioLen >= 110 && (std::memcmp(cpio, "070701", 6) == 0 || std::memcmp(cpio, "070702", 6) == 0));
    bool isOdc  = (!isNewc && std::memcmp(cpio, "070707", 6) == 0);
    if (!isNewc && !isOdc) return false;

    size_t pos = 0;
    size_t count = 0;
    if (isNewc) {
        while (pos + 110 <= cpioLen && count < 50000) {
            if (std::memcmp(cpio + pos, "070701", 6) != 0 && std::memcmp(cpio + pos, "070702", 6) != 0) break;
            auto hex8 = [&](size_t off) -> uint32_t {
                char b[9] = {};
                std::memcpy(b, cpio + pos + off, 8);
                return static_cast<uint32_t>(std::strtoul(b, nullptr, 16));
            };
            uint32_t mode     = hex8(14);
            uint32_t fSize    = hex8(54);
            uint32_t nameSize = hex8(94);
            uint32_t check    = hex8(102);
            if (nameSize == 0 || nameSize > 4096 || pos + 110 + nameSize > cpioLen) break;

            std::string fname(reinterpret_cast<const char*>(cpio + pos + 110),
                              strnlen(reinterpret_cast<const char*>(cpio + pos + 110), nameSize));
            size_t dataStart = (pos + 110 + nameSize + 3u) & ~size_t(3u);
            if (fname == "TRAILER!!!") break;
            if (dataStart + fSize > cpioLen) break;

            std::wstring norm = NormPath(PathUtils::Utf8ToWide(fname));
            bool isDir = ((mode & 0170000u) == 0040000u);
            if (!norm.empty() && norm != L".") {
                if (extractTargetNorm && outExtracted) {
                    if (!isDir && PathEqualsCI(norm, *extractTargetNorm)) {
                        outExtracted->assign(cpio + dataStart, cpio + dataStart + fSize);
                        return true;
                    }
                } else if (outInfo) {
                    ArchiveItemEntry item;
                    item.pathInArchive = norm;
                    item.name = BaseName(norm);
                    item.isDirectory = isDir;
                    item.uncompressedSize = isDir ? 0 : fSize;
                    item.compressedSize   = item.uncompressedSize;
                    item.crc32 = check;
                    item.compressionMethod = 1;
                    outInfo->entries.push_back(std::move(item));
                }
                ++count;
            }
            pos = (dataStart + fSize + 3u) & ~size_t(3u);
        }
    } else {
        while (pos + 76 <= cpioLen && count < 50000) {
            if (std::memcmp(cpio + pos, "070707", 6) != 0) break;
            auto oct = [&](size_t off, size_t len) -> uint64_t {
                char b[16] = {};
                std::memcpy(b, cpio + pos + off, len);
                return static_cast<uint64_t>(std::strtoull(b, nullptr, 8));
            };
            uint64_t mode     = oct(18, 6);
            uint64_t nameSize = oct(59, 6);
            uint64_t fSize    = oct(65, 11);
            if (nameSize == 0 || nameSize > 4096 || pos + 76 + nameSize > cpioLen) break;
            std::string fname(reinterpret_cast<const char*>(cpio + pos + 76),
                              strnlen(reinterpret_cast<const char*>(cpio + pos + 76), static_cast<size_t>(nameSize)));
            if (fname == "TRAILER!!!") break;
            size_t dataStart = pos + 76 + static_cast<size_t>(nameSize);
            if (dataStart + fSize > cpioLen) break;

            std::wstring norm = NormPath(PathUtils::Utf8ToWide(fname));
            bool isDir = ((mode & 0170000u) == 0040000u);
            if (!norm.empty() && norm != L".") {
                if (extractTargetNorm && outExtracted) {
                    if (!isDir && PathEqualsCI(norm, *extractTargetNorm)) {
                        outExtracted->assign(cpio + dataStart, cpio + dataStart + static_cast<size_t>(fSize));
                        return true;
                    }
                } else if (outInfo) {
                    ArchiveItemEntry item;
                    item.pathInArchive = norm;
                    item.name = BaseName(norm);
                    item.isDirectory = isDir;
                    item.uncompressedSize = isDir ? 0 : fSize;
                    item.compressedSize   = item.uncompressedSize;
                    item.compressionMethod = 1;
                    outInfo->entries.push_back(std::move(item));
                }
                ++count;
            }
            pos = dataStart + static_cast<size_t>(fSize);
        }
    }
    return extractTargetNorm ? false : (count > 0);
}

inline bool ParseOrExtractRedHatRpm(
    const uint8_t* data,
    size_t size,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm,
    std::vector<uint8_t>* outExtracted)
{
    if (size < 112 || data[0] != 0xED || data[1] != 0xAB || data[2] != 0xEE || data[3] != 0xDB) return false;

    size_t pos = 96; // Skip 96-byte RPM Lead
    // 1. Skip Signature Header (0x8EADE801), padded to 8-byte boundary
    if (pos + 16 > size || data[pos] != 0x8E || data[pos + 1] != 0xAD || data[pos + 2] != 0xE8 || data[pos + 3] != 0x01) {
        return false;
    }
    uint32_t sigIdxCnt  = RdU32BE(data + pos + 8);
    uint32_t sigDataLen = RdU32BE(data + pos + 12);
    if (sigIdxCnt > 65536 || sigDataLen > size) return false;
    size_t sigBytes = 16 + static_cast<size_t>(sigIdxCnt) * 16 + sigDataLen;
    size_t alignedSig = (sigBytes + 7u) & ~size_t(7u);
    if (pos + alignedSig > size) return false;

    // Some older RPMs only have 1 header if pos + alignedSig doesn't start with 0x8EADE801
    size_t metaPos = pos + alignedSig;
    if (metaPos + 16 > size || data[metaPos] != 0x8E || data[metaPos + 1] != 0xAD ||
        data[metaPos + 2] != 0xE8 || data[metaPos + 3] != 0x01)
    {
        // Check unaligned sigBytes just in case, else treat first header as Metadata Header
        if (pos + sigBytes + 16 <= size && data[pos + sigBytes] == 0x8E &&
            data[pos + sigBytes + 1] == 0xAD && data[pos + sigBytes + 2] == 0xE8 && data[pos + sigBytes + 3] == 0x01)
        {
            metaPos = pos + sigBytes;
        } else {
            metaPos = pos;
        }
    }

    uint32_t hdrIdxCnt  = RdU32BE(data + metaPos + 8);
    uint32_t hdrDataLen = RdU32BE(data + metaPos + 12);
    if (hdrIdxCnt > 131072 || hdrDataLen > size) return false;
    size_t idxBase   = metaPos + 16;
    size_t storeBase = idxBase + static_cast<size_t>(hdrIdxCnt) * 16;
    if (storeBase > size || hdrDataLen > size - storeBase) return false;
    size_t storeEnd  = storeBase + hdrDataLen;
    size_t payloadPos = storeEnd;

    std::vector<std::string> baseNames, dirNames, oldFileNames;
    std::vector<uint32_t> dirIndexes;
    std::vector<uint64_t> fileSizes;
    std::vector<uint64_t> fallbackSizes1009;
    std::vector<uint16_t> fileModes;
    std::string compressor;

    auto readStringArray = [&](uint32_t off, uint32_t cnt, std::vector<std::string>& outArr) {
        if (off >= hdrDataLen || cnt > 100000) return;
        size_t p = storeBase + off;
        outArr.reserve(std::min<uint32_t>(cnt, 4096));
        for (uint32_t k = 0; k < cnt && p < storeEnd; ++k) {
            size_t e = p;
            while (e < storeEnd && data[e] != 0) ++e;
            outArr.emplace_back(reinterpret_cast<const char*>(data + p), e - p);
            p = (e < storeEnd) ? (e + 1) : storeEnd;
        }
    };

    for (uint32_t i = 0; i < hdrIdxCnt; ++i) {
        const uint8_t* ie = data + idxBase + static_cast<size_t>(i) * 16;
        uint32_t tag   = RdU32BE(ie + 0);
        uint32_t type  = RdU32BE(ie + 4);
        uint32_t off   = RdU32BE(ie + 8);
        uint32_t count = RdU32BE(ie + 12);
        if (off >= hdrDataLen) continue;

        if (tag == 1117 && type == 8) { // RPMTAG_BASENAMES
            readStringArray(off, count, baseNames);
        } else if (tag == 1118 && type == 8) { // RPMTAG_DIRNAMES
            readStringArray(off, count, dirNames);
        } else if (tag == 1027 && type == 8) { // RPMTAG_OLDFILENAMES
            readStringArray(off, count, oldFileNames);
        } else if (tag == 1116 && type == 4 && count <= 100000) { // RPMTAG_DIRINDEXES
            if (off + static_cast<uint64_t>(count) * 4 <= hdrDataLen) {
                dirIndexes.resize(count);
                for (uint32_t k = 0; k < count; ++k) dirIndexes[k] = RdU32BE(data + storeBase + off + k * 4);
            }
        } else if (tag == 1028 && type == 4 && fileSizes.empty() && count <= 100000) { // RPMTAG_FILESIZES (INT32)
            if (off + static_cast<uint64_t>(count) * 4 <= hdrDataLen) {
                fileSizes.resize(count);
                for (uint32_t k = 0; k < count; ++k) fileSizes[k] = RdU32BE(data + storeBase + off + k * 4);
            }
        } else if (tag == 1009 && type == 4 && fallbackSizes1009.empty() && count <= 100000) { // RPMTAG_SIZE / synthetic fallback
            if (off + static_cast<uint64_t>(count) * 4 <= hdrDataLen) {
                fallbackSizes1009.resize(count);
                for (uint32_t k = 0; k < count; ++k) fallbackSizes1009[k] = RdU32BE(data + storeBase + off + k * 4);
            }
        } else if (tag == 5008 && type == 5 && count <= 100000) { // RPMTAG_LONGFILESIZES (INT64)
            if (off + static_cast<uint64_t>(count) * 8 <= hdrDataLen) {
                fileSizes.resize(count);
                for (uint32_t k = 0; k < count; ++k) fileSizes[k] = RdU64BE(data + storeBase + off + k * 8);
            }
        } else if (tag == 1030 && type == 3 && count <= 100000) { // RPMTAG_FILEMODES (INT16)
            if (off + static_cast<uint64_t>(count) * 2 <= hdrDataLen) {
                fileModes.resize(count);
                for (uint32_t k = 0; k < count; ++k) fileModes[k] = RdU16BE(data + storeBase + off + k * 2);
            }
        } else if (tag == 1125 && type == 6) { // RPMTAG_PAYLOADCOMPRESSOR
            size_t p = storeBase + off, e = p;
            while (e < storeEnd && data[e] != 0) ++e;
            compressor.assign(reinterpret_cast<const char*>(data + p), e - p);
        }
    }

    // Extraction mode: decompress trailing CPIO payload and extract matching path
    if (extractTargetNorm && outExtracted) {
        if (payloadPos >= size) return false;
        std::vector<uint8_t> cpioStorage;
        const uint8_t* cpioPtr = nullptr;
        size_t cpioLen = 0;
        if (!DecompressRpmPayloadToCpio(data + payloadPos, size - payloadPos, compressor, cpioStorage, cpioPtr, cpioLen)) {
            return false;
        }
        return WalkCpioBuffer(cpioPtr, cpioLen, nullptr, extractTargetNorm, outExtracted);
    }

    if (!outInfo) return false;
    outInfo->formatName = L"Red Hat Package (RPM)";
    if (!compressor.empty()) {
        outInfo->formatName += L" [" + PathUtils::Utf8ToWide(compressor) + L"]";
    }

    size_t nFiles = !baseNames.empty() ? baseNames.size() : oldFileNames.size();
    if (fileSizes.empty() && fallbackSizes1009.size() == nFiles) {
        fileSizes = std::move(fallbackSizes1009);
    }

    uint64_t payloadBytes = (size > payloadPos) ? (size - payloadPos) : 0;
    uint64_t sumUnp = 0;
    for (uint64_t fs : fileSizes) sumUnp += fs;
    for (size_t i = 0; i < nFiles; ++i) {
        std::string fullUtf8;
        if (!baseNames.empty()) {
            uint32_t dIdx = (i < dirIndexes.size()) ? dirIndexes[i] : 0;
            if (dIdx < dirNames.size()) fullUtf8 = dirNames[dIdx] + baseNames[i];
            else fullUtf8 = baseNames[i];
        } else {
            fullUtf8 = oldFileNames[i];
        }

        std::wstring norm = NormPath(PathUtils::Utf8ToWide(fullUtf8));
        if (norm.empty()) continue;

        bool isDir = (i < fileModes.size()) && ((fileModes[i] & 0170000u) == 0040000u);
        uint64_t fsz = isDir ? 0 : ((i < fileSizes.size()) ? fileSizes[i] : 0);

        ArchiveItemEntry item;
        item.pathInArchive = norm;
        item.name = BaseName(norm);
        item.isDirectory = isDir;
        item.uncompressedSize = fsz;
        item.compressedSize = (!isDir && sumUnp > 0) ? ((payloadBytes * fsz) / sumUnp) : 0;
        item.compressionMethod = 1;
        outInfo->entries.push_back(std::move(item));
    }

    // Fallback: if Metadata Header omitted file tags, inspect the trailing CPIO stream directly!
    if (outInfo->entries.empty() && payloadPos < size) {
        std::vector<uint8_t> cpioStorage;
        const uint8_t* cpioPtr = nullptr;
        size_t cpioLen = 0;
        if (DecompressRpmPayloadToCpio(data + payloadPos, size - payloadPos, compressor, cpioStorage, cpioPtr, cpioLen)) {
            WalkCpioBuffer(cpioPtr, cpioLen, outInfo, nullptr, nullptr);
        }
    }

    outInfo->valid = !outInfo->entries.empty();
    return outInfo->valid;
}

inline bool ParseRedHatRpm(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return ParseOrExtractRedHatRpm(data, size, &outInfo, nullptr, nullptr);
}

// ============================================================================
// 28–31. Virtual Disk Images (.vhd, .vhdx, .vmdk, .vdi, .qcow2, .dmg)
//        + MBR/GPT Partition Discovery + FAT12/16/32, ISO9660, NTFS & ext4
// ============================================================================

inline bool Base64DecodeBlock(std::string_view in, std::vector<uint8_t>& out) {
    out.clear();
    out.reserve(in.size() * 3 / 4);
    uint32_t val = 0;
    int bits = -8;
    for (char ch : in) {
        int d = -1;
        if (ch >= 'A' && ch <= 'Z') d = ch - 'A';
        else if (ch >= 'a' && ch <= 'z') d = ch - 'a' + 26;
        else if (ch >= '0' && ch <= '9') d = ch - '0' + 52;
        else if (ch == '+') d = 62;
        else if (ch == '/') d = 63;
        else if (ch == '=') break;
        else continue;
        val = (val << 6) | static_cast<uint32_t>(d);
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<uint8_t>((val >> bits) & 0xFFu));
            bits -= 8;
        }
    }
    return !out.empty();
}

inline bool BuildVirtualDiskReader(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    VirtualDiskReader& outReader)
{
    outReader = {};
    if (!data || size < 512) return false;

    // 1. QEMU QCOW2 ("QFI\xFB" v2/v3)
    if (size >= 72 && std::memcmp(data, "QFI\xFB", 4) == 0) {
        uint32_t ver         = RdU32BE(data + 4);
        uint32_t clusterBits = RdU32BE(data + 20);
        uint64_t virtSize    = RdU64BE(data + 24);
        uint32_t l1Size      = RdU32BE(data + 36);
        uint64_t l1Offset    = RdU64BE(data + 40);
        if ((ver != 2 && ver != 3) || clusterBits < 9 || clusterBits > 21 || virtSize == 0) return false;
        if (l1Offset + static_cast<uint64_t>(l1Size) * 8ULL > size) return false;

        uint64_t clusterSize = 1ULL << clusterBits;
        uint64_t l2Entries   = clusterSize / 8ULL;

        outReader.virtualSize = virtSize;
        outReader.formatName  = L"QEMU QCOW2 Disk";
        outReader.readBytes = [=](uint64_t diskOff, uint8_t* dst, size_t len) -> bool {
            if (diskOff + len > virtSize) return false;
            size_t done = 0;
            while (done < len) {
                uint64_t cur = diskOff + done;
                uint64_t clusIdx = cur / clusterSize;
                uint64_t offInClus = cur % clusterSize;
                size_t chunk = static_cast<size_t>(std::min<uint64_t>(len - done, clusterSize - offInClus));

                uint64_t l1Idx = clusIdx / l2Entries;
                uint64_t l2Idx = clusIdx % l2Entries;
                bool filled = false;
                if (l1Idx < l1Size && l1Offset + (l1Idx + 1) * 8 <= size) {
                    uint64_t l1Ent = RdU64BE(data + static_cast<size_t>(l1Offset + l1Idx * 8));
                    uint64_t l2TblOff = l1Ent & 0x00FFFFFFFFFFFE00ULL;
                    if (l2TblOff > 0 && l2TblOff + (l2Idx + 1) * 8 <= size) {
                        uint64_t l2Ent = RdU64BE(data + static_cast<size_t>(l2TblOff + l2Idx * 8));
                        bool compressed = (l2Ent & (1ULL << 62)) != 0;
                        bool zeroFlag = (ver >= 3) && ((l2Ent & 1ULL) != 0);
                        if (!compressed) {
                            uint64_t hostOff = l2Ent & 0x00FFFFFFFFFFFE00ULL;
                            if (!zeroFlag && hostOff > 0 && hostOff + offInClus + chunk <= size) {
                                std::memcpy(dst + done, data + static_cast<size_t>(hostOff + offInClus), chunk);
                                filled = true;
                            }
                        } else {
                            uint32_t csizeShift = 62 - (clusterBits - 8);
                            uint64_t hostOff = l2Ent & ((1ULL << csizeShift) - 1ULL);
                            uint64_t nbSectors = ((l2Ent >> csizeShift) & ((1ULL << (clusterBits - 8)) - 1ULL)) + 1ULL;
                            uint64_t compBytes = nbSectors * 512ULL - (hostOff & 511ULL);
                            if (hostOff + compBytes <= size) {
                                std::vector<uint8_t> dec;
                                if (ZipDeflate::InflateRaw(data + static_cast<size_t>(hostOff), static_cast<size_t>(compBytes), dec, static_cast<size_t>(clusterSize)) &&
                                    offInClus + chunk <= dec.size())
                                {
                                    std::memcpy(dst + done, dec.data() + static_cast<size_t>(offInClus), chunk);
                                    filled = true;
                                }
                            }
                        }
                    }
                }
                if (!filled) std::memset(dst + done, 0, chunk);
                done += chunk;
            }
            return true;
        };
        return true;
    }

    // 2. Hyper-V VHDX ("vhdxfile")
    if (size >= 65536 && std::memcmp(data, "vhdxfile", 8) == 0) {
        static const uint8_t kBatGuid[16]  = { 0x66,0x77,0xC2,0x2D,0x23,0xF6,0x00,0x42,0x9D,0x64,0x11,0x5E,0x9B,0xFD,0x4A,0x08 };
        static const uint8_t kMetaGuid[16] = { 0x06,0xA2,0x7C,0x8B,0x90,0x47,0x9A,0x4B,0xB8,0xFE,0x57,0x5F,0x05,0x0F,0x88,0x6E };
        static const uint8_t kFileParamGuid[16] = { 0x37,0x67,0xA1,0xCA,0x36,0xFA,0x43,0x4D,0xB3,0xB6,0x33,0xF0,0xAA,0x44,0xE7,0x6B };
        static const uint8_t kVirtSizeGuid[16]  = { 0x24,0x42,0xA5,0x2F,0x1B,0xCD,0x76,0x48,0xB2,0x11,0x5D,0xBE,0xD8,0x3B,0xF4,0xB8 };
        static const uint8_t kLogSecGuid[16]    = { 0x1D,0xBF,0x41,0x81,0x6F,0xA9,0x09,0x47,0xBA,0x47,0xF2,0x33,0xA8,0xFA,0xAB,0x5F };

        const size_t regCandidates[3] = { 0x30000, 0x40000, 0x10000 };
        size_t regPos = SIZE_MAX;
        for (size_t cand : regCandidates) {
            if (cand + 16 <= size && std::memcmp(data + cand, "regi", 4) == 0) {
                regPos = cand;
                break;
            }
        }
        if (regPos == SIZE_MAX) return false;

        uint32_t regCount = std::min<uint32_t>(RdU32LE(data + regPos + 8), 2047);
        uint64_t batOff = 0, batLen = 0, metaOff = 0, metaLen = 0;
        for (uint32_t i = 0; i < regCount && regPos + 16 + (i + 1) * 32 <= size; ++i) {
            const uint8_t* re = data + regPos + 16 + i * 32;
            if (std::memcmp(re, kBatGuid, 16) == 0) {
                batOff = RdU64LE(re + 16);
                batLen = RdU32LE(re + 24);
            } else if (std::memcmp(re, kMetaGuid, 16) == 0) {
                metaOff = RdU64LE(re + 16);
                metaLen = RdU32LE(re + 24);
            }
        }
        if (metaOff == 0 || metaOff + 32 > size || std::memcmp(data + metaOff, "metadata", 8) != 0) return false;

        uint16_t metaCount = std::min<uint16_t>(RdU16LE(data + metaOff + 10), 2047);
        uint32_t blockSize = 0, logicalSector = 512;
        uint64_t virtSize = 0;
        for (uint16_t i = 0; i < metaCount && metaOff + 32 + (i + 1) * 32 <= size; ++i) {
            const uint8_t* me = data + metaOff + 32 + i * 32;
            uint32_t itemOff = RdU32LE(me + 16);
            uint32_t itemLen = RdU32LE(me + 20);
            if (itemOff + itemLen > metaLen || metaOff + itemOff + itemLen > size) continue;
            const uint8_t* val = data + static_cast<size_t>(metaOff + itemOff);
            if (std::memcmp(me, kFileParamGuid, 16) == 0 && itemLen >= 4) {
                blockSize = RdU32LE(val);
            } else if (std::memcmp(me, kVirtSizeGuid, 16) == 0 && itemLen >= 8) {
                virtSize = RdU64LE(val);
            } else if (std::memcmp(me, kLogSecGuid, 16) == 0 && itemLen >= 4) {
                uint32_t ls = RdU32LE(val);
                if (ls >= 512 && ls <= 4096) logicalSector = ls;
            }
        }
        if (blockSize < 65536 || (blockSize & (blockSize - 1)) != 0 || virtSize == 0 || batOff == 0 || batOff >= size) {
            return false;
        }
        uint64_t chunkRatio = ((1ULL << 23) * logicalSector) / blockSize;

        outReader.virtualSize = virtSize;
        outReader.formatName  = L"Hyper-V VHDX Disk";
        outReader.readBytes = [=](uint64_t diskOff, uint8_t* dst, size_t len) -> bool {
            if (diskOff + len > virtSize) return false;
            size_t done = 0;
            while (done < len) {
                uint64_t cur = diskOff + done;
                uint64_t pBlk = cur / blockSize;
                uint64_t offInBlk = cur % blockSize;
                size_t chunk = static_cast<size_t>(std::min<uint64_t>(len - done, blockSize - offInBlk));

                uint64_t batIdx = pBlk + (chunkRatio > 0 ? (pBlk / chunkRatio) : 0);
                bool filled = false;
                if ((batIdx + 1) * 8 <= batLen && batOff + (batIdx + 1) * 8 <= size) {
                    uint64_t entry = RdU64LE(data + static_cast<size_t>(batOff + batIdx * 8));
                    if ((entry & 0x7ULL) == 6ULL) { // PAYLOAD_BLOCK_FULLY_PRESENT
                        uint64_t physBase = entry & 0xFFFFFFFFFFF00000ULL;
                        if (physBase > 0 && physBase + offInBlk + chunk <= size) {
                            std::memcpy(dst + done, data + static_cast<size_t>(physBase + offInBlk), chunk);
                            filled = true;
                        }
                    }
                }
                if (!filled) std::memset(dst + done, 0, chunk);
                done += chunk;
            }
            return true;
        };
        return true;
    }

    // 3. Hyper-V / VirtualPC VHD ("conectix" footer at size - 512 or 0)
    if (size >= 1024 && (std::memcmp(data + size - 512, "conectix", 8) == 0 || std::memcmp(data, "conectix", 8) == 0)) {
        const uint8_t* ftr = (std::memcmp(data + size - 512, "conectix", 8) == 0) ? (data + size - 512) : data;
        uint64_t dataOffset = RdU64BE(ftr + 16);
        uint64_t virtSize   = RdU64BE(ftr + 48);
        uint32_t diskType   = RdU32BE(ftr + 60);
        if (virtSize == 0) return false;

        if (diskType == 2) { // Fixed VHD (raw disk sectors followed by 512-byte footer)
            size_t payloadEnd = (ftr == data + size - 512) ? (size - 512) : size;
            outReader.virtualSize = virtSize;
            outReader.formatName  = L"Hyper-V Fixed VHD Disk";
            outReader.directRawBuffer = (ftr == data + size - 512);
            outReader.readBytes = [=](uint64_t diskOff, uint8_t* dst, size_t len) -> bool {
                if (diskOff + len > virtSize) return false;
                if (diskOff >= payloadEnd) {
                    std::memset(dst, 0, len);
                    return true;
                }
                size_t avail = static_cast<size_t>(std::min<uint64_t>(len, payloadEnd - diskOff));
                std::memcpy(dst, data + static_cast<size_t>(diskOff), avail);
                if (avail < len) std::memset(dst + avail, 0, len - avail);
                return true;
            };
            return true;
        } else if (diskType == 3 && dataOffset + 1024 <= size && std::memcmp(data + dataOffset, "cxsparse", 8) == 0) {
            const uint8_t* dyn = data + static_cast<size_t>(dataOffset);
            uint64_t batOff    = RdU64BE(dyn + 16);
            uint32_t maxBat    = RdU32BE(dyn + 28);
            uint32_t blockSize = RdU32BE(dyn + 32);
            if (blockSize < 512 || (blockSize & (blockSize - 1)) != 0 || batOff >= size) return false;
            uint64_t bitmapBytes = (((blockSize / 512u + 7u) / 8u) + 511u) & ~511u;

            outReader.virtualSize = virtSize;
            outReader.formatName  = L"Hyper-V Dynamic VHD Disk";
            outReader.readBytes = [=](uint64_t diskOff, uint8_t* dst, size_t len) -> bool {
                if (diskOff + len > virtSize) return false;
                size_t done = 0;
                while (done < len) {
                    uint64_t cur = diskOff + done;
                    uint64_t blkIdx = cur / blockSize;
                    uint64_t offInBlk = cur % blockSize;
                    size_t chunk = static_cast<size_t>(std::min<uint64_t>(len - done, blockSize - offInBlk));
                    bool filled = false;
                    if (blkIdx < maxBat && batOff + (blkIdx + 1) * 4 <= size) {
                        uint32_t secOff = RdU32BE(data + static_cast<size_t>(batOff + blkIdx * 4));
                        if (secOff != 0xFFFFFFFFu) {
                            uint64_t phys = static_cast<uint64_t>(secOff) * 512ULL + bitmapBytes + offInBlk;
                            if (phys + chunk <= size) {
                                std::memcpy(dst + done, data + static_cast<size_t>(phys), chunk);
                                filled = true;
                            }
                        }
                    }
                    if (!filled) std::memset(dst + done, 0, chunk);
                    done += chunk;
                }
                return true;
            };
            return true;
        }
        return false;
    }

    // 4. VMware Sparse VMDK ("KDMV")
    if (size >= 512 && std::memcmp(data, "KDMV", 4) == 0) {
        uint32_t flags        = RdU32LE(data + 8);
        uint64_t capacitySec  = RdU64LE(data + 12);
        uint64_t grainSec     = RdU64LE(data + 20);
        uint32_t numGtesPerGt = RdU32LE(data + 44);
        uint64_t rgdSec       = RdU64LE(data + 48);
        uint64_t gdSec        = RdU64LE(data + 56);
        if (gdSec == 0xFFFFFFFFFFFFFFFFULL) gdSec = rgdSec;
        if (capacitySec == 0 || grainSec == 0 || numGtesPerGt == 0 || gdSec * 512ULL >= size) return false;

        uint64_t virtSize   = capacitySec * 512ULL;
        uint64_t grainBytes = grainSec * 512ULL;
        uint64_t gdByteOff  = gdSec * 512ULL;
        bool compressedGrains = (flags & 0x10000u) != 0;

        outReader.virtualSize = virtSize;
        outReader.formatName  = L"VMware VMDK Disk";
        outReader.readBytes = [=](uint64_t diskOff, uint8_t* dst, size_t len) -> bool {
            if (diskOff + len > virtSize) return false;
            size_t done = 0;
            while (done < len) {
                uint64_t cur = diskOff + done;
                uint64_t grainIdx = cur / grainBytes;
                uint64_t offInGrain = cur % grainBytes;
                size_t chunk = static_cast<size_t>(std::min<uint64_t>(len - done, grainBytes - offInGrain));
                uint64_t gtIdx  = grainIdx / numGtesPerGt;
                uint64_t gteIdx = grainIdx % numGtesPerGt;
                bool filled = false;
                if (gdByteOff + (gtIdx + 1) * 4 <= size) {
                    uint32_t gtSec = RdU32LE(data + static_cast<size_t>(gdByteOff + gtIdx * 4));
                    uint64_t gtByteOff = static_cast<uint64_t>(gtSec) * 512ULL;
                    if (gtSec > 0 && gtByteOff + (gteIdx + 1) * 4 <= size) {
                        uint32_t gSec = RdU32LE(data + static_cast<size_t>(gtByteOff + gteIdx * 4));
                        uint64_t gByteOff = static_cast<uint64_t>(gSec) * 512ULL;
                        if (gSec > 0 && gByteOff < size) {
                            if (!compressedGrains) {
                                if (gByteOff + offInGrain + chunk <= size) {
                                    std::memcpy(dst + done, data + static_cast<size_t>(gByteOff + offInGrain), chunk);
                                    filled = true;
                                }
                            } else if (gByteOff + 12 <= size) {
                                uint32_t cmpSz = RdU32LE(data + static_cast<size_t>(gByteOff + 8));
                                if (gByteOff + 12 + cmpSz <= size) {
                                    std::vector<uint8_t> dec;
                                    if (ZipDeflate::InflateZlib(data + static_cast<size_t>(gByteOff + 12), cmpSz, dec, static_cast<size_t>(grainBytes)) &&
                                        offInGrain + chunk <= dec.size())
                                    {
                                        std::memcpy(dst + done, dec.data() + static_cast<size_t>(offInGrain), chunk);
                                        filled = true;
                                    }
                                }
                            }
                        }
                    }
                }
                if (!filled) std::memset(dst + done, 0, chunk);
                done += chunk;
            }
            return true;
        };
        return true;
    }

    // 5. VirtualBox VDI (0xBEDA107F at offset 64)
    if (size >= 400 && RdU32LE(data + 64) == 0xBEDA107Fu) {
        uint32_t offsetBlocks = RdU32LE(data + 340);
        uint32_t offsetData   = RdU32LE(data + 344);
        uint64_t virtSize     = RdU64LE(data + 368);
        uint32_t blockSize    = RdU32LE(data + 376);
        uint32_t blocksInHdd  = RdU32LE(data + 384);
        if (virtSize == 0 || blockSize < 4096 || offsetBlocks >= size || offsetData > size) return false;

        outReader.virtualSize = virtSize;
        outReader.formatName  = L"VirtualBox VDI Disk";
        outReader.readBytes = [=](uint64_t diskOff, uint8_t* dst, size_t len) -> bool {
            if (diskOff + len > virtSize) return false;
            size_t done = 0;
            while (done < len) {
                uint64_t cur = diskOff + done;
                uint64_t blkIdx = cur / blockSize;
                uint64_t offInBlk = cur % blockSize;
                size_t chunk = static_cast<size_t>(std::min<uint64_t>(len - done, blockSize - offInBlk));
                bool filled = false;
                if (blkIdx < blocksInHdd && offsetBlocks + (blkIdx + 1) * 4 <= size) {
                    uint32_t physBlk = RdU32LE(data + static_cast<size_t>(offsetBlocks + blkIdx * 4));
                    if (physBlk < 0xFFFFFFFEu) {
                        uint64_t phys = static_cast<uint64_t>(offsetData) + static_cast<uint64_t>(physBlk) * blockSize + offInBlk;
                        if (phys + chunk <= size) {
                            std::memcpy(dst + done, data + static_cast<size_t>(phys), chunk);
                            filled = true;
                        }
                    }
                }
                if (!filled) std::memset(dst + done, 0, chunk);
                done += chunk;
            }
            return true;
        };
        return true;
    }

    // 6. Apple Disk Image (.dmg — UDIF "koly" trailer or raw disk sectors)
    if (size >= 512 && std::memcmp(data + size - 512, "koly", 4) == 0) {
        const uint8_t* koly = data + size - 512;
        uint64_t dataForkOff = RdU64BE(koly + 24);
        uint64_t xmlOff      = RdU64BE(koly + 216);
        uint64_t xmlLen      = RdU64BE(koly + 224);
        uint64_t secCount    = RdU64BE(koly + 492);
        uint64_t virtSize    = (secCount > 0) ? (secCount * 512ULL) : (size - 512);

        struct DmgRun {
            uint32_t type;
            uint64_t diskStart;
            uint64_t diskLen;
            uint64_t compOff;
            uint64_t compLen;
        };
        auto runs = std::make_shared<std::vector<DmgRun>>();

        if (xmlOff + xmlLen <= size - 512 && xmlLen > 16 && xmlLen <= 16 * 1024 * 1024) {
            std::string_view xml(reinterpret_cast<const char*>(data + static_cast<size_t>(xmlOff)), static_cast<size_t>(xmlLen));
            size_t searchPos = 0;
            uint64_t mishPartBaseSec = 0;
            while (searchPos < xml.size() && runs->size() < 65536) {
                size_t d1 = xml.find("<data>", searchPos);
                if (d1 == std::string_view::npos) break;
                d1 += 6;
                size_t d2 = xml.find("</data>", d1);
                if (d2 == std::string_view::npos) break;
                searchPos = d2 + 7;

                std::vector<uint8_t> mish;
                if (Base64DecodeBlock(xml.substr(d1, d2 - d1), mish) &&
                    mish.size() >= 244 && std::memcmp(mish.data(), "mish", 4) == 0)
                {
                    uint64_t firstSec = RdU64BE(mish.data() + 8);
                    uint64_t partSecs = RdU64BE(mish.data() + 16);
                    uint64_t mishDataStart = RdU64BE(mish.data() + 24);
                    uint32_t nBlocks  = RdU32BE(mish.data() + 200);
                    uint64_t baseSec  = (firstSec > 0 || runs->empty()) ? firstSec : mishPartBaseSec;
                    for (uint32_t b = 0; b < nBlocks && 204u + (b + 1) * 40u <= mish.size(); ++b) {
                        const uint8_t* ce = mish.data() + 204 + b * 40;
                        uint32_t et = RdU32BE(ce + 0);
                        if (et == 0xFFFFFFFFu) break;
                        uint64_t sNum = RdU64BE(ce + 8);
                        uint64_t sCnt = RdU64BE(ce + 16);
                        uint64_t cOff = RdU64BE(ce + 24);
                        uint64_t cLen = RdU64BE(ce + 32);
                        if (sCnt == 0) continue;
                        runs->push_back({ et, (baseSec + sNum) * 512ULL, sCnt * 512ULL, dataForkOff + mishDataStart + cOff, cLen });
                    }
                    mishPartBaseSec = baseSec + partSecs;
                }
            }
        }

        if (!runs->empty()) {
            uint64_t maxEnd = virtSize;
            for (const auto& r : *runs) maxEnd = std::max(maxEnd, r.diskStart + r.diskLen);
            outReader.virtualSize = maxEnd;
            outReader.formatName  = L"Apple Disk Image (DMG)";
            outReader.readBytes = [=](uint64_t diskOff, uint8_t* dst, size_t len) -> bool {
                if (diskOff + len > maxEnd) return false;
                std::memset(dst, 0, len);
                for (const auto& r : *runs) {
                    uint64_t rEnd = r.diskStart + r.diskLen;
                    uint64_t qEnd = diskOff + len;
                    if (rEnd <= diskOff || r.diskStart >= qEnd) continue;
                    uint64_t ovStart = std::max(diskOff, r.diskStart);
                    uint64_t ovEnd   = std::min(qEnd, rEnd);
                    size_t copyLen   = static_cast<size_t>(ovEnd - ovStart);
                    size_t dstOff    = static_cast<size_t>(ovStart - diskOff);
                    size_t runRel    = static_cast<size_t>(ovStart - r.diskStart);

                    if (r.type == 0x00000001u && r.compOff + runRel + copyLen <= size) {
                        std::memcpy(dst + dstOff, data + static_cast<size_t>(r.compOff) + runRel, copyLen);
                    } else if (r.type == 0x80000005u && r.compOff + r.compLen <= size && r.diskLen <= 16 * 1024 * 1024) {
                        std::vector<uint8_t> dec;
                        if (ZipDeflate::InflateZlib(data + static_cast<size_t>(r.compOff), static_cast<size_t>(r.compLen), dec, static_cast<size_t>(r.diskLen)) &&
                            runRel + copyLen <= dec.size())
                        {
                            std::memcpy(dst + dstOff, dec.data() + runRel, copyLen);
                        }
                    } else if (r.type == 0x80000006u && r.compOff + r.compLen <= size && r.diskLen <= 16 * 1024 * 1024) {
                        std::vector<uint8_t> dec;
                        if (StreamCodecs::DecompressBzip2Stream(data + static_cast<size_t>(r.compOff), static_cast<size_t>(r.compLen), dec, static_cast<size_t>(r.diskLen)) &&
                            runRel + copyLen <= dec.size())
                        {
                            std::memcpy(dst + dstOff, dec.data() + runRel, copyLen);
                        }
                    }
                }
                return true;
            };
            return true;
        }

        // Raw data fork before koly trailer
        size_t rawEnd = size - 512;
        outReader.virtualSize = rawEnd;
        outReader.formatName  = L"Apple Disk Image (DMG)";
        outReader.directRawBuffer = true;
        outReader.readBytes = [=](uint64_t diskOff, uint8_t* dst, size_t len) -> bool {
            if (diskOff + len > rawEnd) return false;
            std::memcpy(dst, data + static_cast<size_t>(diskOff), len);
            return true;
        };
        return true;
    }

    if (extLower == L".dmg" && size >= 1024) {
        outReader.virtualSize = size;
        outReader.formatName  = L"Apple Disk Image (DMG)";
        outReader.directRawBuffer = true;
        outReader.readBytes = [=](uint64_t diskOff, uint8_t* dst, size_t len) -> bool {
            if (diskOff + len > size) return false;
            std::memcpy(dst, data + static_cast<size_t>(diskOff), len);
            return true;
        };
        return true;
    }

    return false;
}

// ----------------------------------------------------------------------------
// FAT12 / FAT16 / FAT32 Filesystem Inspector & Cluster-Chain Extractor
// ----------------------------------------------------------------------------
struct FatVolumeParams {
    uint64_t partBase = 0;
    uint64_t partBytes = 0;
    uint32_t bytesPerSec = 512;
    uint32_t secPerClus = 1;
    uint32_t rsvdSecCnt = 1;
    uint32_t numFats = 2;
    uint32_t rootEntCnt = 0;
    uint32_t fatSz = 0;
    uint32_t firstDataSector = 0;
    uint32_t countOfClusters = 0;
    uint32_t rootCluster = 2;
    int fatBits = 16; // 12, 16, or 32
    std::vector<uint8_t> fatTable;

    uint32_t BytesPerCluster() const { return bytesPerSec * secPerClus; }

    uint64_t ClusterOffset(uint32_t clus) const {
        return partBase + (static_cast<uint64_t>(firstDataSector) + static_cast<uint64_t>(clus - 2) * secPerClus) * bytesPerSec;
    }

    uint32_t NextCluster(uint32_t clus) const {
        if (clus < 2) return 0x0FFFFFFFu;
        if (fatBits == 12) {
            size_t off = clus + (clus / 2);
            if (off + 2 > fatTable.size()) return 0x0FFFu;
            uint16_t v = RdU16LE(fatTable.data() + off);
            v = (clus & 1u) ? (v >> 4) : (v & 0x0FFFu);
            return (v >= 0x0FF8u || v < 2u) ? 0x0FFFFFFFu : v;
        } else if (fatBits == 16) {
            size_t off = static_cast<size_t>(clus) * 2;
            if (off + 2 > fatTable.size()) return 0x0FFFFFFFu;
            uint16_t v = RdU16LE(fatTable.data() + off);
            return (v >= 0xFFF8u || v < 2u) ? 0x0FFFFFFFu : v;
        } else {
            size_t off = static_cast<size_t>(clus) * 4;
            if (off + 4 > fatTable.size()) return 0x0FFFFFFFu;
            uint32_t v = RdU32LE(fatTable.data() + off) & 0x0FFFFFFFu;
            return (v >= 0x0FFFFFF8u || v < 2u) ? 0x0FFFFFFFu : v;
        }
    }
};

inline bool ProbeFatVolume(const VirtualDiskReader& reader, uint64_t partBase, uint64_t partBytes, FatVolumeParams& out) {
    if (partBytes < 2048 || partBase + 512 > reader.virtualSize) return false;
    uint8_t bpb[512] = {};
    if (!reader.readBytes(partBase, bpb, 512)) return false;
    if (bpb[510] != 0x55 || bpb[511] != 0xAA) return false;
    if (std::memcmp(bpb + 3, "NTFS    ", 8) == 0 || std::memcmp(bpb + 3, "EXFAT   ", 8) == 0) return false;
    if (!((bpb[0] == 0xEB && bpb[2] == 0x90) || bpb[0] == 0xE9 || bpb[0] == 0xEB)) return false;

    uint16_t bps = RdU16LE(bpb + 11);
    uint8_t  spc = bpb[13];
    uint16_t rsvd = RdU16LE(bpb + 14);
    uint8_t  nfats = bpb[16];
    uint16_t rootCnt = RdU16LE(bpb + 17);
    if ((bps != 512 && bps != 1024 && bps != 2048 && bps != 4096) ||
        spc == 0 || (spc & (spc - 1)) != 0 || rsvd == 0 || nfats == 0 || nfats > 4)
    {
        return false;
    }

    uint32_t totSec = RdU16LE(bpb + 19) != 0 ? RdU16LE(bpb + 19) : RdU32LE(bpb + 32);
    uint32_t fatSz  = RdU16LE(bpb + 22) != 0 ? RdU16LE(bpb + 22) : RdU32LE(bpb + 36);
    if (totSec == 0 || fatSz == 0) return false;

    uint32_t rootDirSectors = ((static_cast<uint32_t>(rootCnt) * 32u) + (bps - 1u)) / bps;
    uint32_t firstDataSec = static_cast<uint32_t>(rsvd) + static_cast<uint32_t>(nfats) * fatSz + rootDirSectors;
    if (firstDataSec >= totSec) return false;
    if (partBase + static_cast<uint64_t>(firstDataSec) * bps > reader.virtualSize) return false;

    uint32_t dataSec = totSec - firstDataSec;
    uint32_t countClusters = dataSec / spc;
    if (countClusters == 0) return false;

    out = {};
    out.partBase = partBase;
    out.partBytes = partBytes;
    out.bytesPerSec = bps;
    out.secPerClus = spc;
    out.rsvdSecCnt = rsvd;
    out.numFats = nfats;
    out.rootEntCnt = rootCnt;
    out.fatSz = fatSz;
    out.firstDataSector = firstDataSec;
    out.countOfClusters = countClusters;

    if (rootCnt == 0 || std::memcmp(bpb + 82, "FAT32", 5) == 0) {
        out.fatBits = 32;
        out.rootCluster = RdU32LE(bpb + 44);
        if (out.rootCluster < 2) out.rootCluster = 2;
    } else if (std::memcmp(bpb + 54, "FAT16", 5) == 0) {
        out.fatBits = 16;
    } else if (std::memcmp(bpb + 54, "FAT12", 5) == 0) {
        out.fatBits = 12;
    } else {
        out.fatBits = (countClusters < 4085) ? 12 : ((countClusters < 65525) ? 16 : 32);
    }

    uint64_t fatByteOff = partBase + static_cast<uint64_t>(rsvd) * bps;
    size_t fatByteLen = static_cast<size_t>(std::min<uint64_t>(static_cast<uint64_t>(fatSz) * bps, 16ULL * 1024 * 1024));
    if (fatByteOff + fatByteLen > reader.virtualSize) return false;
    out.fatTable.resize(fatByteLen);
    return reader.readBytes(fatByteOff, out.fatTable.data(), fatByteLen);
}

inline bool ReadFatClusterChain(
    const VirtualDiskReader& reader,
    const FatVolumeParams& vol,
    uint32_t startCluster,
    uint64_t wantedBytes,
    size_t maxBytes,
    std::vector<uint8_t>& outBytes)
{
    outBytes.clear();
    if (wantedBytes == 0) return true;
    if (startCluster < 2 || wantedBytes > maxBytes) return false;

    size_t target = static_cast<size_t>(wantedBytes);
    outBytes.reserve(std::min<size_t>(target, 1024 * 1024));
    uint32_t bpc = vol.BytesPerCluster();
    std::vector<uint8_t> clusBuf(bpc);

    uint32_t clus = startCluster;
    size_t steps = 0;
    size_t maxSteps = static_cast<size_t>(vol.countOfClusters) + 4;
    while (clus >= 2 && clus < 0x0FFFFFF8u && clus < vol.countOfClusters + 2 &&
           outBytes.size() < target && steps++ < maxSteps)
    {
        uint64_t off = vol.ClusterOffset(clus);
        size_t take = std::min<size_t>(bpc, target - outBytes.size());
        if (off + take > reader.virtualSize) return false;
        if (!reader.readBytes(off, clusBuf.data(), take)) return false;
        outBytes.insert(outBytes.end(), clusBuf.data(), clusBuf.data() + take);
        clus = vol.NextCluster(clus);
    }
    return outBytes.size() == target;
}

inline void WalkFatDirectoryBuffer(
    const VirtualDiskReader& reader,
    const FatVolumeParams& vol,
    const uint8_t* dirData,
    size_t dirLen,
    const std::wstring& parentDir,
    int depth,
    std::set<uint32_t>& visitedDirs,
    ArchiveSummaryInfo& outInfo,
    const std::wstring* extractTarget,
    std::vector<uint8_t>* outExtracted,
    bool* outFound)
{
    if (depth > 16 || outInfo.entries.size() >= 25000) return;
    if (outFound && *outFound) return;

    std::wstring lfnParts[21];
    int lfnMaxOrd = 0;
    auto resetLfn = [&]() {
        for (int k = 0; k < 21; ++k) lfnParts[k].clear();
        lfnMaxOrd = 0;
    };

    for (size_t pos = 0; pos + 32 <= dirLen; pos += 32) {
        const uint8_t* ent = dirData + pos;
        uint8_t firstByte = ent[0];
        if (firstByte == 0x00) break; // End of directory
        if (firstByte == 0xE5) { resetLfn(); continue; } // Deleted entry

        uint8_t attr = ent[11];
        if (attr == 0x0Fu) { // VFAT Long File Name entry
            uint8_t ord = firstByte & 0x3Fu;
            if ((firstByte & 0x40u) != 0) resetLfn();
            if (ord >= 1 && ord <= 20) {
                static const int kOffs[13] = { 1,3,5,7,9, 14,16,18,20,22,24, 28,30 };
                std::wstring seg;
                for (int c = 0; c < 13; ++c) {
                    uint16_t u = RdU16LE(ent + kOffs[c]);
                    if (u == 0x0000 || u == 0xFFFF) break;
                    seg.push_back(static_cast<wchar_t>(u));
                }
                lfnParts[ord] = std::move(seg);
                if (static_cast<int>(ord) > lfnMaxOrd) lfnMaxOrd = static_cast<int>(ord);
            }
            continue;
        }

        if ((attr & 0x08u) != 0) { // Volume ID
            resetLfn();
            continue;
        }

        std::wstring entryName;
        if (lfnMaxOrd > 0) {
            for (int k = 1; k <= lfnMaxOrd; ++k) entryName += lfnParts[k];
        }
        resetLfn();

        if (entryName.empty()) {
            std::string base(reinterpret_cast<const char*>(ent), 8);
            if (!base.empty() && static_cast<uint8_t>(base[0]) == 0x05) base[0] = static_cast<char>(0xE5);
            while (!base.empty() && base.back() == ' ') base.pop_back();
            std::string ext(reinterpret_cast<const char*>(ent + 8), 3);
            while (!ext.empty() && ext.back() == ' ') ext.pop_back();
            if ((ent[12] & 0x08u) != 0) base = PathUtils::ToLowerAscii(base);
            if ((ent[12] & 0x10u) != 0) ext  = PathUtils::ToLowerAscii(ext);
            std::string full83 = ext.empty() ? base : (base + "." + ext);
            entryName = PathUtils::Utf8ToWide(full83);
        }

        if (entryName.empty() || entryName == L"." || entryName == L"..") continue;

        bool isDir = (attr & 0x10u) != 0;
        uint32_t firstClus = (vol.fatBits == 32 ? (static_cast<uint32_t>(RdU16LE(ent + 20)) << 16) : 0u) | RdU16LE(ent + 26);
        uint32_t fSize     = RdU32LE(ent + 28);
        uint16_t wrtTime   = RdU16LE(ent + 22);
        uint16_t wrtDate   = RdU16LE(ent + 24);
        std::wstring fullRel = NormPath(parentDir.empty() ? entryName : (parentDir + L"/" + entryName));

        if (extractTarget && outExtracted && outFound) {
            if (!isDir && PathEqualsCI(fullRel, *extractTarget)) {
                if (ReadFatClusterChain(reader, vol, firstClus, fSize, 128 * 1024 * 1024, *outExtracted)) {
                    *outFound = true;
                }
                return;
            }
        } else {
            ArchiveItemEntry item;
            item.pathInArchive = fullRel;
            item.name = BaseName(fullRel);
            item.isDirectory = isDir;
            item.uncompressedSize = isDir ? 0 : fSize;
            item.compressedSize   = isDir ? 0 : fSize;
            item.dosTime = wrtTime;
            item.dosDate = wrtDate;
            item.compressionMethod = 1;
            outInfo.entries.push_back(std::move(item));
        }

        if (isDir && firstClus >= 2 && visitedDirs.insert(firstClus).second) {
            std::vector<uint8_t> subDirBuf;
            uint32_t bpc = vol.BytesPerCluster();
            uint32_t c = firstClus;
            size_t steps = 0;
            while (c >= 2 && c < 0x0FFFFFF8u && c < vol.countOfClusters + 2 &&
                   subDirBuf.size() < 2 * 1024 * 1024 && steps++ < 4096)
            {
                size_t oldSz = subDirBuf.size();
                subDirBuf.resize(oldSz + bpc);
                if (!reader.readBytes(vol.ClusterOffset(c), subDirBuf.data() + oldSz, bpc)) {
                    subDirBuf.resize(oldSz);
                    break;
                }
                c = vol.NextCluster(c);
            }
            if (!subDirBuf.empty()) {
                WalkFatDirectoryBuffer(reader, vol, subDirBuf.data(), subDirBuf.size(), fullRel,
                                       depth + 1, visitedDirs, outInfo, extractTarget, outExtracted, outFound);
                if (outFound && *outFound) return;
            }
        }
    }
}

inline bool InspectOrExtractFatOnReader(
    const VirtualDiskReader& reader,
    const FatVolumeParams& vol,
    const std::wstring& prefixDir,
    ArchiveSummaryInfo& outInfo,
    const std::wstring* extractTarget = nullptr,
    std::vector<uint8_t>* outExtracted = nullptr)
{
    std::vector<uint8_t> rootBuf;
    std::set<uint32_t> visitedDirs;
    if (vol.fatBits == 32) {
        visitedDirs.insert(vol.rootCluster);
        uint32_t bpc = vol.BytesPerCluster();
        uint32_t c = vol.rootCluster;
        size_t steps = 0;
        while (c >= 2 && c < 0x0FFFFFF8u && c < vol.countOfClusters + 2 &&
               rootBuf.size() < 2 * 1024 * 1024 && steps++ < 4096)
        {
            size_t oldSz = rootBuf.size();
            rootBuf.resize(oldSz + bpc);
            if (!reader.readBytes(vol.ClusterOffset(c), rootBuf.data() + oldSz, bpc)) {
                rootBuf.resize(oldSz);
                break;
            }
            c = vol.NextCluster(c);
        }
    } else {
        size_t rootBytes = static_cast<size_t>(vol.rootEntCnt) * 32u;
        uint64_t rootOff = vol.partBase + (static_cast<uint64_t>(vol.rsvdSecCnt) + static_cast<uint64_t>(vol.numFats) * vol.fatSz) * vol.bytesPerSec;
        if (rootOff + rootBytes > reader.virtualSize) return false;
        rootBuf.resize(rootBytes);
        if (!reader.readBytes(rootOff, rootBuf.data(), rootBytes)) return false;
    }

    bool found = false;
    WalkFatDirectoryBuffer(reader, vol, rootBuf.data(), rootBuf.size(), prefixDir, 0,
                           visitedDirs, outInfo, extractTarget, outExtracted, &found);
    return extractTarget ? found : true;
}

// ----------------------------------------------------------------------------
// NTFS $MFT Inspector & Resident / Non-Resident File Extractor
// ----------------------------------------------------------------------------
inline bool InspectOrExtractNtfsOnReader(
    const VirtualDiskReader& reader,
    uint64_t partBase,
    uint64_t partBytes,
    const std::wstring& prefixDir,
    ArchiveSummaryInfo& outInfo,
    const std::wstring* extractTarget = nullptr,
    std::vector<uint8_t>* outExtracted = nullptr)
{
    if (partBytes < 4096 || partBase + 512 > reader.virtualSize) return false;
    uint8_t bpb[512] = {};
    if (!reader.readBytes(partBase, bpb, 512) || std::memcmp(bpb + 3, "NTFS    ", 8) != 0) return false;

    uint16_t bps = RdU16LE(bpb + 11);
    uint8_t  spc = bpb[13];
    uint64_t mftLcn = RdU64LE(bpb + 48);
    int8_t   recSpec = static_cast<int8_t>(bpb[64]);
    if (bps < 512 || spc == 0) return false;
    uint32_t clusterBytes = static_cast<uint32_t>(bps) * spc;
    uint32_t recSize = (recSpec < 0) ? (1u << (-recSpec)) : (static_cast<uint32_t>(recSpec) * clusterBytes);
    if (recSize < 512 || recSize > 8192) return false;

    uint64_t mftBase = partBase + mftLcn * clusterBytes;
    if (mftBase + recSize > reader.virtualSize) return false;

    struct NtfsRec {
        uint64_t parentIdx = 5;
        std::wstring name;
        uint8_t nameSpace = 255;
        bool isDir = false;
        bool valid = false;
        uint64_t fileSize = 0;
        bool resident = false;
        std::vector<uint8_t> residentData;
        std::vector<std::pair<uint64_t, uint64_t>> runs; // (lcnByteOff, byteLen)
    };

    const uint32_t maxRecs = static_cast<uint32_t>(std::min<uint64_t>((reader.virtualSize - mftBase) / recSize, 2048));
    std::unordered_map<uint64_t, NtfsRec> records;
    std::vector<uint8_t> recBuf(recSize);

    for (uint32_t idx = 0; idx < maxRecs; ++idx) {
        if (!reader.readBytes(mftBase + static_cast<uint64_t>(idx) * recSize, recBuf.data(), recSize)) break;
        if (std::memcmp(recBuf.data(), "FILE", 4) != 0) {
            if (idx > 24 && records.empty()) break;
            continue;
        }
        // Apply Update Sequence Array (USA) fixups
        uint16_t usaOff   = RdU16LE(recBuf.data() + 4);
        uint16_t usaCount = RdU16LE(recBuf.data() + 6);
        if (usaCount >= 2u && static_cast<size_t>(usaOff) + static_cast<size_t>(usaCount) * 2u <= recSize) {
            for (uint16_t s = 1; s < usaCount && static_cast<size_t>(s) * 512u <= recSize; ++s) {
                recBuf[s * 512 - 2] = recBuf[usaOff + s * 2];
                recBuf[s * 512 - 1] = recBuf[usaOff + s * 2 + 1];
            }
        }

        uint16_t flags = RdU16LE(recBuf.data() + 22);
        if ((flags & 0x01u) == 0) continue; // Not in use

        uint16_t attrOff = RdU16LE(recBuf.data() + 20);
        NtfsRec nr;
        nr.isDir = (flags & 0x02u) != 0;

        size_t ap = attrOff;
        while (ap + 16 <= recSize) {
            uint32_t attrType = RdU32LE(recBuf.data() + ap);
            if (attrType == 0xFFFFFFFFu) break;
            uint32_t attrLen = RdU32LE(recBuf.data() + ap + 4);
            if (attrLen < 16 || ap + attrLen > recSize) break;
            uint8_t nonRes  = recBuf[ap + 8];
            uint8_t nameLen = recBuf[ap + 9];

            if (attrType == 0x30u && nonRes == 0 && attrLen >= 24) { // $FILE_NAME
                uint32_t valLen = RdU32LE(recBuf.data() + ap + 16);
                uint16_t valOff = RdU16LE(recBuf.data() + ap + 20);
                if (valOff + valLen <= attrLen && valLen >= 66) {
                    const uint8_t* fn = recBuf.data() + ap + valOff;
                    uint64_t pRef = RdU64LE(fn) & 0x0000FFFFFFFFFFFFULL;
                    uint8_t fnChars = fn[64];
                    uint8_t fnSpace = fn[65];
                    if (66u + static_cast<size_t>(fnChars) * 2 <= valLen && fnChars > 0) {
                        if (nr.name.empty() || (nr.nameSpace == 2 && fnSpace != 2)) {
                            nr.parentIdx = pRef;
                            nr.nameSpace = fnSpace;
                            nr.name.resize(fnChars);
                            for (uint8_t c = 0; c < fnChars; ++c) {
                                nr.name[c] = static_cast<wchar_t>(RdU16LE(fn + 66 + c * 2));
                            }
                            nr.valid = true;
                        }
                    }
                }
            } else if (attrType == 0x80u && nameLen == 0) { // Unnamed $DATA
                if (nonRes == 0 && attrLen >= 24) {
                    uint32_t valLen = RdU32LE(recBuf.data() + ap + 16);
                    uint16_t valOff = RdU16LE(recBuf.data() + ap + 20);
                    if (valOff + valLen <= attrLen) {
                        nr.fileSize = valLen;
                        nr.resident = true;
                        nr.residentData.assign(recBuf.data() + ap + valOff, recBuf.data() + ap + valOff + valLen);
                    }
                } else if (nonRes == 1 && attrLen >= 64) {
                    nr.fileSize = RdU64LE(recBuf.data() + ap + 48);
                    uint16_t runOff = RdU16LE(recBuf.data() + ap + 32);
                    int64_t curLcn = 0;
                    size_t rp = ap + runOff;
                    while (rp < ap + attrLen && recBuf[rp] != 0) {
                        uint8_t hdr = recBuf[rp++];
                        uint8_t lenB = hdr & 0x0Fu;
                        uint8_t offB = (hdr >> 4) & 0x0Fu;
                        if (lenB == 0 || lenB > 8 || offB > 8 || rp + lenB + offB > ap + attrLen) break;
                        uint64_t rLen = 0;
                        for (uint8_t b = 0; b < lenB; ++b) rLen |= static_cast<uint64_t>(recBuf[rp++]) << (b * 8);
                        int64_t rDelta = 0;
                        for (uint8_t b = 0; b < offB; ++b) rDelta |= static_cast<int64_t>(recBuf[rp++]) << (b * 8);
                        if (offB > 0 && (recBuf[rp - 1] & 0x80u) != 0) {
                            for (uint8_t b = offB; b < 8; ++b) rDelta |= (-1LL << (b * 8));
                        }
                        if (offB == 0) { // Sparse run
                            nr.runs.push_back({ UINT64_MAX, rLen * clusterBytes });
                        } else {
                            curLcn += rDelta;
                            if (curLcn > 0) nr.runs.push_back({ partBase + static_cast<uint64_t>(curLcn) * clusterBytes, rLen * clusterBytes });
                        }
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
        for (int depth = 0; depth < 16 && cur != 5; ++depth) {
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
        return NormPath(out);
    };

    // Prefer user files first; if disk only has NTFS system files ($MFT, etc.), include them so volume isn't blank
    bool hasUserFiles = false;
    for (const auto& kv : records) {
        if (kv.first >= 24 && !kv.second.name.empty() && kv.second.name[0] != L'$') {
            hasUserFiles = true;
            break;
        }
    }

    for (const auto& kv : records) {
        if (kv.first == 5) continue; // Root "."
        if (hasUserFiles && (!kv.second.name.empty() && kv.second.name[0] == L'$')) continue;
        std::wstring fullRel = buildFullNtfsPath(kv.first);
        if (fullRel.empty()) continue;

        if (extractTarget && outExtracted) {
            if (!kv.second.isDir && PathEqualsCI(fullRel, *extractTarget)) {
                if (kv.second.resident) {
                    *outExtracted = kv.second.residentData;
                    return true;
                }
                if (kv.second.fileSize <= 128 * 1024 * 1024) {
                    outExtracted->clear();
                    outExtracted->reserve(static_cast<size_t>(kv.second.fileSize));
                    for (const auto& run : kv.second.runs) {
                        if (outExtracted->size() >= kv.second.fileSize) break;
                        size_t take = static_cast<size_t>(std::min<uint64_t>(run.second, kv.second.fileSize - outExtracted->size()));
                        size_t prev = outExtracted->size();
                        outExtracted->resize(prev + take, 0);
                        if (run.first != UINT64_MAX) {
                            if (!reader.readBytes(run.first, outExtracted->data() + prev, take)) return false;
                        }
                    }
                    return outExtracted->size() == kv.second.fileSize;
                }
                return false;
            }
        } else {
            ArchiveItemEntry item;
            item.pathInArchive = fullRel;
            item.name = BaseName(fullRel);
            item.isDirectory = kv.second.isDir;
            item.uncompressedSize = kv.second.isDir ? 0 : kv.second.fileSize;
            item.compressedSize   = item.uncompressedSize;
            item.compressionMethod = 1;
            outInfo.entries.push_back(std::move(item));
        }
    }
    return extractTarget ? false : !outInfo.entries.empty();
}

// ----------------------------------------------------------------------------
// Linux ext2/ext3/ext4 Superblock & Root Directory Inspector
// ----------------------------------------------------------------------------
inline bool InspectExtFsOnReader(
    const VirtualDiskReader& reader,
    uint64_t partBase,
    uint64_t partBytes,
    const std::wstring& prefixDir,
    ArchiveSummaryInfo& outInfo)
{
    if (partBytes < 2048 || partBase + 2048 > reader.virtualSize) return false;
    uint8_t sb[1024] = {};
    if (!reader.readBytes(partBase + 1024, sb, 1024)) return false;
    if (RdU16LE(sb + 56) != 0xEF53u) return false;

    uint32_t inodesCnt   = RdU32LE(sb + 0);
    uint32_t blocksCnt   = RdU32LE(sb + 4);
    uint32_t logBlkSz    = RdU32LE(sb + 24);
    uint16_t inodeSz     = (RdU32LE(sb + 76) >= 1) ? RdU16LE(sb + 88) : 128;
    if (logBlkSz > 6 || inodeSz < 128) return false;
    uint32_t blockSize   = 1024u << logBlkSz;
    std::string volName(reinterpret_cast<const char*>(sb + 120), strnlen(reinterpret_cast<const char*>(sb + 120), 16));

    // Read Block Group Descriptor 0 to locate Inode Table and Root Inode (#2)
    uint64_t bgdOff = partBase + ((blockSize == 1024) ? 2048ULL : static_cast<uint64_t>(blockSize));
    uint8_t bgd[64] = {};
    size_t added = 0;
    if (bgdOff + 64 <= reader.virtualSize && reader.readBytes(bgdOff, bgd, 64)) {
        uint32_t inodeTblBlk = RdU32LE(bgd + 8);
        uint64_t rootInodeOff = partBase + static_cast<uint64_t>(inodeTblBlk) * blockSize + static_cast<uint64_t>(inodeSz); // Inode 2 is index 1
        std::vector<uint8_t> ino(inodeSz);
        if (inodeTblBlk > 0 && rootInodeOff + inodeSz <= reader.virtualSize && reader.readBytes(rootInodeOff, ino.data(), inodeSz)) {
            uint32_t iFlags = RdU32LE(ino.data() + 32);
            uint32_t dirBlk = 0;
            if ((iFlags & 0x80000u) != 0 && RdU16LE(ino.data() + 40) == 0xF30Au && RdU16LE(ino.data() + 42) >= 1) {
                dirBlk = RdU32LE(ino.data() + 40 + 12 + 8); // ext4_extent ee_block_lo
            } else {
                dirBlk = RdU32LE(ino.data() + 40); // Direct block 0
            }
            if (dirBlk > 0 && partBase + static_cast<uint64_t>(dirBlk + 1) * blockSize <= reader.virtualSize) {
                std::vector<uint8_t> dirData(blockSize);
                if (reader.readBytes(partBase + static_cast<uint64_t>(dirBlk) * blockSize, dirData.data(), blockSize)) {
                    size_t dp = 0;
                    while (dp + 8 <= blockSize) {
                        uint32_t dIno = RdU32LE(dirData.data() + dp);
                        uint16_t rLen = RdU16LE(dirData.data() + dp + 4);
                        uint8_t  nLen = dirData[dp + 6];
                        uint8_t  fTyp = dirData[dp + 7];
                        if (rLen < 8 || dp + rLen > blockSize) break;
                        if (dIno != 0 && nLen > 0 && dp + 8 + nLen <= blockSize) {
                            std::string nm(reinterpret_cast<const char*>(dirData.data() + dp + 8), nLen);
                            if (nm != "." && nm != "..") {
                                std::wstring wnm = PathUtils::Utf8ToWide(nm);
                                std::wstring full = NormPath(prefixDir.empty() ? wnm : (prefixDir + L"/" + wnm));
                                ArchiveItemEntry item;
                                item.pathInArchive = full;
                                item.name = BaseName(full);
                                item.isDirectory = (fTyp == 2);
                                item.uncompressedSize = item.isDirectory ? 0 : blockSize;
                                item.compressedSize = item.uncompressedSize;
                                item.compressionMethod = 1;
                                outInfo.entries.push_back(std::move(item));
                                ++added;
                            }
                        }
                        dp += rLen;
                    }
                }
            }
        }
    }

    if (added == 0) {
        std::wstring label = volName.empty() ? L"ext4_fs" : PathUtils::Utf8ToWide(volName);
        std::wstring p = NormPath((prefixDir.empty() ? L"" : (prefixDir + L"/")) +
                                  label + L"_" + std::to_wstring(inodesCnt) + L"inodes.ext4");
        ArchiveItemEntry item;
        item.pathInArchive = p;
        item.name = BaseName(p);
        item.uncompressedSize = static_cast<uint64_t>(blocksCnt) * blockSize;
        item.compressedSize = item.uncompressedSize;
        item.compressionMethod = 1;
        outInfo.entries.push_back(std::move(item));
    }
    return true;
}

struct DiscoveredPartition {
    uint64_t byteOffset = 0;
    uint64_t byteLength = 0;
    uint8_t  mbrType = 0;
    std::wstring label;
};

inline void DiscoverDiskPartitions(const VirtualDiskReader& reader, std::vector<DiscoveredPartition>& outParts) {
    outParts.clear();
    if (reader.virtualSize < 512) return;

    // 1. Check if LBA 0 is already a superfloppy FAT / NTFS / ISO / ext4 filesystem
    FatVolumeParams lba0Fat;
    if (ProbeFatVolume(reader, 0, reader.virtualSize, lba0Fat)) {
        outParts.push_back({ 0, reader.virtualSize, 0x06, L"" });
        return;
    }
    uint8_t sec0[512] = {};
    if (!reader.readBytes(0, sec0, 512)) return;
    if (std::memcmp(sec0 + 3, "NTFS    ", 8) == 0) {
        outParts.push_back({ 0, reader.virtualSize, 0x07, L"" });
        return;
    }

    // 2. Check GPT at LBA 1 ("EFI PART")
    if (reader.virtualSize >= 1024) {
        uint8_t gpt[512] = {};
        if (reader.readBytes(512, gpt, 512) && std::memcmp(gpt, "EFI PART", 8) == 0) {
            uint64_t peLba  = RdU64LE(gpt + 72);
            uint32_t numPe  = std::min<uint32_t>(RdU32LE(gpt + 80), 128);
            uint32_t peSize = RdU32LE(gpt + 84);
            if (peSize >= 128 && peSize <= 512 && peLba * 512ULL + static_cast<uint64_t>(numPe) * peSize <= reader.virtualSize) {
                std::vector<uint8_t> peBuf(static_cast<size_t>(numPe) * peSize);
                if (reader.readBytes(peLba * 512ULL, peBuf.data(), peBuf.size())) {
                    for (uint32_t i = 0; i < numPe; ++i) {
                        const uint8_t* pe = peBuf.data() + i * peSize;
                        bool allZero = true;
                        for (int b = 0; b < 16; ++b) if (pe[b] != 0) { allZero = false; break; }
                        if (allZero) continue;
                        uint64_t firstLba = RdU64LE(pe + 32);
                        uint64_t lastLba  = RdU64LE(pe + 40);
                        if (firstLba == 0 || lastLba < firstLba || firstLba * 512ULL >= reader.virtualSize) continue;
                        std::wstring pname;
                        for (size_t c = 0; c < 36; ++c) {
                            wchar_t wc = static_cast<wchar_t>(RdU16LE(pe + 56 + c * 2));
                            if (wc == 0) break;
                            if (wc == L'/' || wc == L'\\') wc = L'_';
                            pname.push_back(wc);
                        }
                        uint64_t pOff = firstLba * 512ULL;
                        uint64_t pLen = std::min<uint64_t>((lastLba - firstLba + 1ULL) * 512ULL, reader.virtualSize - pOff);
                        outParts.push_back({ pOff, pLen, 0xEE, pname });
                    }
                }
            }
            if (!outParts.empty()) return;
        }
    }

    // 3. Check MBR at LBA 0 (bytes 446..509 + 0x55AA)
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

    // 4. Superfloppy fallback (ISO9660 / ext4 / HFS+ at offset 0)
    outParts.push_back({ 0, reader.virtualSize, 0, L"" });
}

inline bool InspectOrExtractVirtualDisk(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm,
    std::vector<uint8_t>* outExtracted)
{
    VirtualDiskReader reader;
    if (!BuildVirtualDiskReader(data, size, extLower, reader)) return false;

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
            // Also strip optional "Partition0.../" prefix if caller passed one
            if (_wcsnicmp(outRelTarget.c_str(), L"Partition", 9) == 0) {
                size_t sl = outRelTarget.find(L'/');
                if (sl != std::wstring::npos) outRelTarget = outRelTarget.substr(sl + 1);
            }
            return true;
        };

        std::wstring targetRel;
        const std::wstring* targetPtr = matchTargetForPart(targetRel) ? &targetRel : nullptr;

        // A. Try FAT12 / FAT16 / FAT32
        FatVolumeParams fatVol;
        if (ProbeFatVolume(reader, p.byteOffset, p.byteLength, fatVol)) {
            ArchiveSummaryInfo tmp;
            ArchiveSummaryInfo& dstInfo = outInfo ? *outInfo : tmp;
            if (InspectOrExtractFatOnReader(reader, fatVol, prefix, dstInfo, targetPtr, outExtracted)) {
                if (extractTargetNorm) return true;
                continue;
            }
        }

        // B. Try ISO-9660
        {
            ArchiveSummaryInfo tmp;
            ArchiveSummaryInfo& dstInfo = outInfo ? *outInfo : tmp;
            if (InspectOrExtractIsoOnReader(reader, p.byteOffset, prefix, dstInfo, targetPtr, outExtracted)) {
                if (extractTargetNorm) return true;
                continue;
            }
        }

        // C. Try NTFS
        {
            ArchiveSummaryInfo tmp;
            ArchiveSummaryInfo& dstInfo = outInfo ? *outInfo : tmp;
            if (InspectOrExtractNtfsOnReader(reader, p.byteOffset, p.byteLength, prefix, dstInfo, targetPtr, outExtracted)) {
                if (extractTargetNorm) return true;
                continue;
            }
        }

        // D. Try Linux ext2/3/4
        if (!extractTargetNorm && outInfo && InspectExtFsOnReader(reader, p.byteOffset, p.byteLength, prefix, *outInfo)) {
            continue;
        }

        // E. Raw partition slice (or HFS+/APFS volume)
        std::wstring rawEntryName;
        uint8_t vh[64] = {};
        if (p.byteLength >= 2048 && reader.readBytes(p.byteOffset + 1024, vh, 64) &&
            ( (vh[0] == 'H' && (vh[1] == '+' || vh[1] == 'X')) ))
        {
            uint32_t fileCnt = RdU32BE(vh + 16);
            rawEntryName = (prefix.empty() ? L"HFSPlus_Volume_" : (prefix + L"_HFSPlus_")) + std::to_wstring(fileCnt) + L"files.hfs";
        } else if (p.byteLength >= 64 && reader.readBytes(p.byteOffset + 32, vh, 8) && std::memcmp(vh, "NXSB", 4) == 0) {
            rawEntryName = (prefix.empty() ? L"APFS_Container" : (prefix + L"_APFS")) + L".apfs";
        } else if (!p.label.empty()) {
            rawEntryName = p.label + L"_" + std::to_wstring(p.byteLength / 512ULL) + L"sec.img";
        } else {
            // Unpartitioned virtual disk with real virtual geometry header (e.g. fresh unformatted VHD/QCOW2/VDI)
            rawEntryName = L"RawDisk_" + std::to_wstring(reader.virtualSize / 512ULL) + L"sec.img";
        }

        if (extractTargetNorm && outExtracted) {
            if (PathEqualsCI(rawEntryName, *extractTargetNorm) && p.byteLength <= 128 * 1024 * 1024) {
                outExtracted->resize(static_cast<size_t>(p.byteLength));
                return reader.readBytes(p.byteOffset, outExtracted->data(), outExtracted->size());
            }
        } else if (outInfo) {
            ArchiveItemEntry item;
            item.pathInArchive = rawEntryName;
            item.name = BaseName(rawEntryName);
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

inline bool ParseVirtualDiskImage(const uint8_t* data, size_t size, const std::wstring& extLower, ArchiveSummaryInfo& outInfo) {
    return InspectOrExtractVirtualDisk(data, size, extLower, &outInfo, nullptr, nullptr);
}

// ============================================================================
// 32. Windows Imaging Format (.wim / .esd — "MSWIM\0\0\0")
//     XML Manifest + RESHDR Lookup Table + Recursive DIRENTRY Tree
// ============================================================================
struct WimResEntry {
    uint64_t wimOffset = 0;
    uint64_t compSize = 0;
    uint64_t origSize = 0;
    uint8_t  flags = 0; // 0x02 = Metadata, 0x04 = Compressed
    uint8_t  sha1[20] = {};
};

inline void WalkWimDirEntryTree(
    const uint8_t* metaBuf,
    size_t metaLen,
    size_t dirOffset,
    const std::wstring& parentPath,
    const std::vector<WimResEntry>& lookupTable,
    int depth,
    std::set<size_t>& visitedOffsets,
    ArchiveSummaryInfo& outInfo)
{
    if (depth > 20 || dirOffset + 102 > metaLen || outInfo.entries.size() >= 30000) return;
    if (!visitedOffsets.insert(dirOffset).second) return;

    size_t pos = dirOffset;
    while (pos + 8 <= metaLen && outInfo.entries.size() < 30000) {
        uint64_t liLength = RdU64LE(metaBuf + pos);
        if (liLength == 0) break; // End of directory list
        if (liLength < 102 || pos + liLength > metaLen) break;

        const uint8_t* de = metaBuf + pos;
        uint32_t attrs     = RdU32LE(de + 8);
        uint64_t subdirOff = RdU64LE(de + 16);
        uint64_t lastWrite = RdU64LE(de + 56);
        const uint8_t* sha1 = de + 64;
        uint16_t fnBytes   = RdU16LE(de + 100);

        std::wstring fname;
        if (fnBytes > 0 && 102u + fnBytes <= liLength) {
            size_t nChars = fnBytes / 2;
            fname.reserve(nChars);
            for (size_t c = 0; c < nChars; ++c) {
                wchar_t wc = static_cast<wchar_t>(RdU16LE(de + 102 + c * 2));
                if (wc == 0) break;
                fname.push_back(wc);
            }
        }

        bool isDir = (attrs & 0x10u) != 0;
        std::wstring curPath = fname.empty() ? parentPath : NormPath(parentPath.empty() ? fname : (parentPath + L"/" + fname));

        if (!fname.empty()) {
            ArchiveItemEntry item;
            item.pathInArchive = curPath;
            item.name = BaseName(curPath);
            item.isDirectory = isDir;
            FileTimeU64ToDos(lastWrite, item.dosDate, item.dosTime);

            if (!isDir) {
                bool zeroHash = true;
                for (int b = 0; b < 20; ++b) if (sha1[b] != 0) { zeroHash = false; break; }
                if (!zeroHash) {
                    for (const auto& res : lookupTable) {
                        if (std::memcmp(res.sha1, sha1, 20) == 0) {
                            item.uncompressedSize = res.origSize;
                            item.compressedSize   = res.compSize;
                            item.rawDataOffset    = res.wimOffset;
                            item.compressionMethod = ((res.flags & 0x04u) == 0) ? 0 : 1;
                            break;
                        }
                    }
                }
            }
            outInfo.entries.push_back(std::move(item));
        }

        if (isDir && subdirOff > 0 && subdirOff < metaLen && subdirOff != dirOffset) {
            WalkWimDirEntryTree(metaBuf, metaLen, static_cast<size_t>(subdirOff), curPath,
                                lookupTable, depth + 1, visitedOffsets, outInfo);
        }

        pos += static_cast<size_t>((liLength + 7ULL) & ~7ULL);
    }
}

inline bool ParseWindowsWim(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (size < 208 || std::memcmp(data, "MSWIM\0\0\0", 8) != 0) return false;
    uint32_t dwFlags  = RdU32LE(data + 16);
    uint32_t imgCount = RdU32LE(data + 44);
    outInfo.formatName = (dwFlags & 0x00080000u) ? L"Windows ESD Image (LZMS)" : L"Windows Image (WIM)";

    auto readResHdr = [&](size_t off) -> WimResEntry {
        WimResEntry r;
        uint64_t sf = RdU64LE(data + off);
        r.compSize  = sf & 0x00FFFFFFFFFFFFFFULL;
        r.flags     = data[off + 7];
        r.wimOffset = RdU64LE(data + off + 8);
        r.origSize  = RdU64LE(data + off + 16);
        return r;
    };

    WimResEntry rhOffsetTbl = readResHdr(48);
    WimResEntry rhXml       = readResHdr(72);

    // 1. Parse UTF-16LE / UTF-8 XML Manifest
    struct WimXmlImage {
        uint32_t index = 1;
        std::wstring name;
        uint64_t totalBytes = 0;
        uint64_t fileCount = 0;
    };
    std::vector<WimXmlImage> xmlImages;

    if (rhXml.wimOffset + rhXml.compSize <= size && rhXml.compSize >= 4) {
        ArchiveItemEntry xmlEntry;
        xmlEntry.pathInArchive = L"[WIM_Manifest].xml";
        xmlEntry.name = xmlEntry.pathInArchive;
        xmlEntry.uncompressedSize = rhXml.compSize;
        xmlEntry.compressedSize = rhXml.compSize;
        xmlEntry.rawDataOffset = rhXml.wimOffset;
        xmlEntry.compressionMethod = 0;
        outInfo.entries.push_back(std::move(xmlEntry));

        const uint8_t* xp = data + static_cast<size_t>(rhXml.wimOffset);
        size_t xlen = static_cast<size_t>(rhXml.compSize);
        std::wstring wxml;
        if (xlen >= 2 && ((xp[0] == 0xFF && xp[1] == 0xFE) || xp[1] == 0x00)) {
            size_t s = (xp[0] == 0xFF && xp[1] == 0xFE) ? 2 : 0;
            wxml.reserve((xlen - s) / 2);
            for (size_t i = s; i + 2 <= xlen; i += 2) {
                wchar_t wc = static_cast<wchar_t>(RdU16LE(xp + i));
                if (wc != 0) wxml.push_back(wc);
            }
        } else {
            wxml = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(xp), xlen));
        }

        size_t pos = 0;
        uint32_t seqIdx = 1;
        while (pos < wxml.size()) {
            size_t imStart = wxml.find(L"<IMAGE", pos);
            if (imStart == std::wstring::npos) break;
            size_t imEnd = wxml.find(L"</IMAGE>", imStart);
            if (imEnd == std::wstring::npos) imEnd = wxml.size();
            std::wstring_view blk(wxml.data() + imStart, imEnd - imStart);

            auto getTag = [&](std::wstring_view openT, std::wstring_view closeT) -> std::wstring {
                size_t a = blk.find(openT);
                if (a == std::wstring::npos) return L"";
                a += openT.size();
                size_t b = blk.find(closeT, a);
                return (b == std::wstring::npos) ? L"" : std::wstring(blk.substr(a, b - a));
            };

            WimXmlImage wi;
            wi.index = seqIdx++;
            size_t idxAttr = blk.find(L"INDEX=\"");
            if (idxAttr != std::wstring::npos && idxAttr < 64) {
                wi.index = static_cast<uint32_t>(_wtoi(std::wstring(blk.substr(idxAttr + 7, 8)).c_str()));
                if (wi.index == 0) wi.index = seqIdx - 1;
            }
            wi.name = getTag(L"<NAME>", L"</NAME>");
            std::wstring tb = getTag(L"<TOTALBYTES>", L"</TOTALBYTES>");
            std::wstring fc = getTag(L"<FILECOUNT>", L"</FILECOUNT>");
            if (!tb.empty()) wi.totalBytes = static_cast<uint64_t>(_wtoi64(tb.c_str()));
            if (!fc.empty()) wi.fileCount  = static_cast<uint64_t>(_wtoi64(fc.c_str()));
            xmlImages.push_back(std::move(wi));

            pos = (imEnd < wxml.size()) ? (imEnd + 8) : wxml.size();
        }
    }

    // 2. Parse Lookup Table (50-byte RESHDR_TABLE_ENTRY records) & Uncompressed Metadata Resources
    std::vector<WimResEntry> lookupTable;
    std::vector<WimResEntry> metadataResources;
    if ((rhOffsetTbl.flags & 0x04u) == 0 && rhOffsetTbl.compSize >= 50 &&
        rhOffsetTbl.wimOffset + rhOffsetTbl.compSize <= size)
    {
        size_t cnt = static_cast<size_t>(rhOffsetTbl.compSize / 50ULL);
        size_t tp = static_cast<size_t>(rhOffsetTbl.wimOffset);
        for (size_t i = 0; i < cnt && i < 200000; ++i, tp += 50) {
            WimResEntry e = readResHdr(tp);
            std::memcpy(e.sha1, data + tp + 30, 20);
            if ((e.flags & 0x02u) != 0) metadataResources.push_back(e);
            else lookupTable.push_back(e);
        }
    }

    size_t walkedImages = 0;
    for (size_t m = 0; m < metadataResources.size(); ++m) {
        const auto& mr = metadataResources[m];
        if ((mr.flags & 0x04u) != 0 || mr.compSize < 110 || mr.wimOffset + mr.compSize > size) continue;
        const uint8_t* mb = data + static_cast<size_t>(mr.wimOffset);
        size_t mlen = static_cast<size_t>(mr.compSize);
        uint32_t secLen = RdU32LE(mb);
        size_t rootDirOff = (secLen == 0) ? 8 : static_cast<size_t>((static_cast<uint64_t>(secLen) + 7ULL) & ~7ULL);
        if (rootDirOff + 102 > mlen) continue;

        std::wstring imgPrefix;
        if (m < xmlImages.size() && !xmlImages[m].name.empty()) {
            if (metadataResources.size() > 1 || imgCount > 1) {
                imgPrefix = L"Image" + std::to_wstring(xmlImages[m].index) + L"_" + xmlImages[m].name;
            } else {
                imgPrefix = xmlImages[m].name;
            }
        } else if (metadataResources.size() > 1 || imgCount > 1) {
            imgPrefix = L"Image_" + std::to_wstring(m + 1);
        }
        for (wchar_t& ch : imgPrefix) if (ch == L'/' || ch == L'\\') ch = L'_';

        std::set<size_t> visited;
        size_t before = outInfo.entries.size();
        WalkWimDirEntryTree(mb, mlen, rootDirOff, imgPrefix, lookupTable, 0, visited, outInfo);
        if (outInfo.entries.size() > before) ++walkedImages;
    }

    // If metadata resources are compressed (LZX/LZMS), emit rich XML image entries with exact names & sizes
    if (walkedImages == 0) {
        if (!xmlImages.empty()) {
            for (const auto& wi : xmlImages) {
                std::wstring label = L"Image_" + std::to_wstring(wi.index);
                if (!wi.name.empty()) label += L"_" + wi.name;
                for (wchar_t& ch : label) if (ch == L'/' || ch == L'\\') ch = L'_';
                ArchiveItemEntry imgEntry;
                imgEntry.pathInArchive = label + L" (" + std::to_wstring(wi.fileCount) + L" files)";
                imgEntry.name = imgEntry.pathInArchive;
                imgEntry.isDirectory = (wi.totalBytes == 0);
                imgEntry.uncompressedSize = wi.totalBytes;
                imgEntry.compressedSize = size / std::max<size_t>(1, xmlImages.size());
                imgEntry.compressionMethod = 1;
                outInfo.entries.push_back(std::move(imgEntry));
            }
        } else {
            for (uint32_t i = 1; i <= std::clamp(imgCount, 1u, 32u); ++i) {
                ArchiveItemEntry imgDir;
                imgDir.pathInArchive = L"Image_" + std::to_wstring(i);
                imgDir.name = imgDir.pathInArchive;
                imgDir.isDirectory = true;
                outInfo.entries.push_back(std::move(imgDir));
            }
        }
    }

    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
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
// 34. Valve Pak (.vpk 0x55AA1234)
// 35. Bethesda Archive (.bsa "BSA\0" v103/104/105 & .ba2 "BTDX" GNRL/DX10)
// ============================================================================
inline bool ParseValveVpk(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (size < 12 || RdU32LE(data) != 0x55AA1234u) return false;
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
                if (archiveIdx == 0x7FFFu) {
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

inline bool ParseOrExtractBethesdaBsa(
    const uint8_t* data,
    size_t size,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm,
    std::vector<uint8_t>* outExtracted)
{
    if (size < 36 || std::memcmp(data, "BSA\0", 4) != 0) return false;
    uint32_t version      = RdU32LE(data + 4);
    uint32_t folderOffset = RdU32LE(data + 8);
    uint32_t archiveFlags = RdU32LE(data + 12);
    uint32_t folderCount  = RdU32LE(data + 16);
    uint32_t fileCount    = RdU32LE(data + 20);
    uint32_t totalFolderNameLen = RdU32LE(data + 24);
    uint32_t totalFileNameLen   = RdU32LE(data + 28);
    (void)totalFolderNameLen;

    if ((version != 103 && version != 104 && version != 105) ||
        folderOffset < 36 || folderOffset >= size ||
        folderCount == 0 || folderCount > 200000 ||
        fileCount == 0 || fileCount > 500000)
    {
        return false;
    }

    size_t folderRecSize = (version == 105) ? 24 : 16;
    if (folderOffset + static_cast<uint64_t>(folderCount) * folderRecSize > size) return false;
    size_t folderTableEnd = folderOffset + static_cast<size_t>(folderCount) * folderRecSize;

    struct BsaFolderHdr {
        uint64_t hash;
        uint32_t count;
        uint64_t rawOffset;
    };
    std::vector<BsaFolderHdr> folders(folderCount);
    for (uint32_t f = 0; f < folderCount; ++f) {
        const uint8_t* fp = data + folderOffset + static_cast<size_t>(f) * folderRecSize;
        folders[f].hash  = RdU64LE(fp + 0);
        folders[f].count = RdU32LE(fp + 8);
        folders[f].rawOffset = (version == 105) ? RdU64LE(fp + 16) : RdU32LE(fp + 12);
    }

    bool hasDirNames  = (archiveFlags & 0x001u) != 0;
    bool hasFileNames = (archiveFlags & 0x002u) != 0;
    bool defaultComp  = (archiveFlags & 0x004u) != 0;
    bool embedNames   = (version == 104 || version == 105) && ((archiveFlags & 0x100u) != 0);

    struct BsaFileRec {
        std::wstring dirPath;
        uint64_t fileHash;
        uint32_t rawSize;
        uint32_t dataOffset;
        bool compressed;
    };
    std::vector<BsaFileRec> files;
    files.reserve(std::min<uint32_t>(fileCount, 16384));

    size_t maxBlockEnd = folderTableEnd;
    for (uint32_t f = 0; f < folderCount; ++f) {
        uint64_t rawOff = folders[f].rawOffset;
        uint64_t realOff = (hasFileNames && rawOff >= folderTableEnd + totalFileNameLen)
            ? (rawOff - totalFileNameLen)
            : rawOff;
        if (realOff >= size) return false;
        size_t bp = static_cast<size_t>(realOff);

        std::wstring dirPath;
        if (hasDirNames) {
            if (bp >= size) return false;
            uint8_t bzLen = data[bp++];
            if (bp + bzLen > size) return false;
            if (bzLen > 0) {
                std::string ds(reinterpret_cast<const char*>(data + bp), strnlen(reinterpret_cast<const char*>(data + bp), bzLen));
                dirPath = NormPath(PathUtils::Utf8ToWide(ds));
            }
            bp += bzLen;
        } else {
            wchar_t hb[32] = {};
            swprintf_s(hb, L"dir_%016llx", static_cast<unsigned long long>(folders[f].hash));
            dirPath = hb;
        }

        uint32_t cnt = folders[f].count;
        if (cnt > fileCount || bp + static_cast<uint64_t>(cnt) * 16 > size) return false;
        for (uint32_t k = 0; k < cnt && files.size() < fileCount; ++k) {
            const uint8_t* fr = data + bp + static_cast<size_t>(k) * 16;
            uint64_t fHash = RdU64LE(fr + 0);
            uint32_t szFlg = RdU32LE(fr + 8);
            uint32_t dOff  = RdU32LE(fr + 12);
            bool flipComp  = (szFlg & 0x40000000u) != 0;
            bool isComp    = defaultComp ^ flipComp;
            uint32_t rSz   = szFlg & 0x3FFFFFFFu;
            files.push_back({ dirPath, fHash, rSz, dOff, isComp });
        }
        bp += static_cast<size_t>(cnt) * 16;
        if (bp > maxBlockEnd) maxBlockEnd = bp;
    }

    // Read fileCount null-terminated filenames right after all folder record blocks
    std::vector<std::wstring> fileNames;
    if (hasFileNames && maxBlockEnd < size) {
        fileNames.reserve(files.size());
        size_t np = maxBlockEnd;
        size_t npEnd = std::min<size_t>(size, maxBlockEnd + totalFileNameLen + 64);
        for (size_t i = 0; i < files.size() && np < npEnd; ++i) {
            size_t e = np;
            while (e < npEnd && data[e] != 0) ++e;
            std::string fn(reinterpret_cast<const char*>(data + np), e - np);
            fileNames.push_back(NormPath(PathUtils::Utf8ToWide(fn)));
            np = (e < npEnd) ? (e + 1) : npEnd;
        }
    }

    if (outInfo) {
        outInfo->formatName = L"Bethesda BSA v" + std::to_wstring(version);
    }

    for (size_t i = 0; i < files.size(); ++i) {
        const auto& fr = files[i];
        std::wstring leaf;
        if (i < fileNames.size() && !fileNames[i].empty()) {
            leaf = fileNames[i];
        } else {
            wchar_t hb[32] = {};
            swprintf_s(hb, L"file_%016llx.bin", static_cast<unsigned long long>(fr.fileHash));
            leaf = hb;
        }
        std::wstring full = NormPath(fr.dirPath.empty() ? leaf : (fr.dirPath + L"/" + leaf));
        if (full.empty()) continue;

        // Inspect exact payload slice (accounting for optional bString embedded name and compressed 4B size prefix)
        size_t p = fr.dataOffset;
        size_t pEnd = p + fr.rawSize;
        uint64_t unpSz = fr.rawSize;
        bool validSlice = (fr.dataOffset <= size && fr.rawSize <= size - fr.dataOffset);
        if (validSlice) {
            if (embedNames && p < pEnd) {
                uint8_t bLen = data[p++];
                p = std::min<size_t>(pEnd, p + bLen);
            }
            if (fr.compressed) {
                if (p + 4 <= pEnd) {
                    unpSz = RdU32LE(data + p);
                    p += 4;
                } else {
                    validSlice = false;
                }
            } else {
                unpSz = pEnd - p;
            }
        }

        if (extractTargetNorm && outExtracted) {
            if (PathEqualsCI(full, *extractTargetNorm)) {
                if (!validSlice) return false;
                if (!fr.compressed) {
                    outExtracted->assign(data + p, data + pEnd);
                    return true;
                }
                size_t cmpLen = pEnd - p;
                if (cmpLen >= 4 && RdU32LE(data + p) == 0x184D2204u) {
                    return StreamCodecs::DecompressLz4Frame(data + p, cmpLen, *outExtracted, 128 * 1024 * 1024);
                }
                if (ZipDeflate::InflateZlib(data + p, cmpLen, *outExtracted, 128 * 1024 * 1024)) return true;
                return ZipDeflate::InflateRaw(data + p, cmpLen, *outExtracted, 128 * 1024 * 1024);
            }
        } else if (outInfo) {
            ArchiveItemEntry item;
            item.pathInArchive = full;
            item.name = BaseName(full);
            item.uncompressedSize = unpSz;
            item.compressedSize   = fr.rawSize;
            item.rawDataOffset    = p;
            item.compressionMethod = (!fr.compressed && validSlice) ? 0 : 8;
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

inline void BuildBa2Dx10DdsHeader(
    uint16_t width,
    uint16_t height,
    uint8_t numMips,
    uint8_t dxgiFormat,
    uint16_t tileMode,
    uint8_t outHdr[148])
{
    std::memset(outHdr, 0, 148);
    std::memcpy(outHdr, "DDS ", 4);
    uint32_t mips = std::max<uint32_t>(1, numMips);
    uint32_t ddsFlags = 0x1u | 0x2u | 0x4u | 0x1000u | (mips > 1 ? 0x20000u : 0u);
    WrU32LE(outHdr + 4, 124);       // DDS_HEADER.dwSize
    WrU32LE(outHdr + 8, ddsFlags);  // DDS_HEADER.dwFlags
    WrU32LE(outHdr + 12, height);   // dwHeight
    WrU32LE(outHdr + 16, width);    // dwWidth
    WrU32LE(outHdr + 28, mips);     // dwMipMapCount
    WrU32LE(outHdr + 76, 32);       // ddspf.dwSize
    WrU32LE(outHdr + 80, 0x4u);     // DDPF_FOURCC
    std::memcpy(outHdr + 84, "DX10", 4);
    uint32_t caps1 = 0x1000u | (mips > 1 ? 0x400008u : 0u);
    WrU32LE(outHdr + 108, caps1);
    bool isCubemap = (tileMode & 0x0100u) != 0 || (tileMode & 0x80u) != 0;
    if (isCubemap) WrU32LE(outHdr + 112, 0xFE00u);

    // DDS_HEADER_DXT10 (20 bytes @ offset 128)
    WrU32LE(outHdr + 128, dxgiFormat);
    WrU32LE(outHdr + 132, 3); // D3D10_RESOURCE_DIMENSION_TEXTURE2D
    WrU32LE(outHdr + 136, isCubemap ? 0x4u : 0u);
    WrU32LE(outHdr + 140, 1); // arraySize = 1
    WrU32LE(outHdr + 144, 0);
}

inline bool ParseOrExtractBethesdaBa2(
    const uint8_t* data,
    size_t size,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm,
    std::vector<uint8_t>* outExtracted)
{
    if (size < 24 || std::memcmp(data, "BTDX", 4) != 0) return false;
    uint32_t version = RdU32LE(data + 4);
    char typeTag[5]  = {};
    std::memcpy(typeTag, data + 8, 4);
    uint32_t numFiles = RdU32LE(data + 12);
    uint64_t nameTblOff = RdU64LE(data + 16);

    bool isGnrl = (std::memcmp(typeTag, "GNRL", 4) == 0);
    bool isDx10 = (std::memcmp(typeTag, "DX10", 4) == 0 || std::memcmp(typeTag, "GNMF", 4) == 0);
    if ((!isGnrl && !isDx10) || numFiles == 0 || numFiles > 500000) return false;

    size_t hdrSize = (version == 2) ? 32 : ((version == 3) ? 36 : 24);
    if (hdrSize > size) return false;

    // 1. Read optional string name table at nameTableOffset
    std::vector<std::wstring> names;
    if (nameTblOff > 0 && nameTblOff < size) {
        names.reserve(std::min<uint32_t>(numFiles, 16384));
        size_t np = static_cast<size_t>(nameTblOff);
        for (uint32_t i = 0; i < numFiles && np + 2 <= size; ++i) {
            uint16_t nLen = RdU16LE(data + np);
            np += 2;
            if (np + nLen > size) break;
            std::string raw(reinterpret_cast<const char*>(data + np), nLen);
            names.push_back(NormPath(PathUtils::Utf8ToWide(raw)));
            np += nLen;
        }
    }

    if (outInfo) {
        outInfo->formatName = isGnrl ? L"Bethesda BA2 (GNRL)" : L"Bethesda BA2 Textures (DX10)";
    }

    auto makeFallbackBa2Name = [&](uint32_t idx, uint32_t dirHash, uint32_t nameHash, const uint8_t* ext4) -> std::wstring {
        if (idx < names.size() && !names[idx].empty()) return names[idx];
        std::string ext(reinterpret_cast<const char*>(ext4), strnlen(reinterpret_cast<const char*>(ext4), 4));
        if (ext.empty()) ext = isDx10 ? "dds" : "bin";
        wchar_t buf[96] = {};
        swprintf_s(buf, L"%08x/%08x.%S", dirHash, nameHash, ext.c_str());
        return buf;
    };

    if (isGnrl) {
        if (hdrSize + static_cast<uint64_t>(numFiles) * 36 > size) return false;
        for (uint32_t i = 0; i < numFiles; ++i) {
            const uint8_t* rec = data + hdrSize + static_cast<size_t>(i) * 36;
            uint32_t nameHash = RdU32LE(rec + 0);
            uint32_t dirHash  = RdU32LE(rec + 8);
            uint64_t offset   = RdU64LE(rec + 16);
            uint32_t packSz   = RdU32LE(rec + 24);
            uint32_t unpSz    = RdU32LE(rec + 28);
            std::wstring full = makeFallbackBa2Name(i, dirHash, nameHash, rec + 4);

            if (extractTargetNorm && outExtracted) {
                if (PathEqualsCI(full, *extractTargetNorm)) {
                    if (packSz == 0) {
                        if (offset + unpSz > size || unpSz > 128 * 1024 * 1024) return false;
                        outExtracted->assign(data + static_cast<size_t>(offset), data + static_cast<size_t>(offset + unpSz));
                        return true;
                    } else {
                        if (offset + packSz > size) return false;
                        const uint8_t* src = data + static_cast<size_t>(offset);
                        if (packSz >= 4 && RdU32LE(src) == 0x184D2204u) {
                            return StreamCodecs::DecompressLz4Frame(src, packSz, *outExtracted, 128 * 1024 * 1024);
                        }
                        if (ZipDeflate::InflateZlib(src, packSz, *outExtracted, 128 * 1024 * 1024)) return true;
                        return ZipDeflate::InflateRaw(src, packSz, *outExtracted, 128 * 1024 * 1024);
                    }
                }
            } else if (outInfo) {
                ArchiveItemEntry item;
                item.pathInArchive = full;
                item.name = BaseName(full);
                item.uncompressedSize = unpSz;
                item.compressedSize   = (packSz > 0) ? packSz : unpSz;
                item.rawDataOffset    = offset;
                item.compressionMethod = (packSz == 0 && offset + unpSz <= size) ? 0 : 8;
                outInfo->entries.push_back(std::move(item));
            }
        }
    } else {
        // DX10 Texture records + variable chunk arrays
        size_t pos = hdrSize;
        for (uint32_t i = 0; i < numFiles; ++i) {
            if (pos + 24 > size) break;
            const uint8_t* th = data + pos;
            uint32_t nameHash   = RdU32LE(th + 0);
            uint32_t dirHash    = RdU32LE(th + 8);
            uint8_t  numChunks  = th[13];
            uint16_t chunkHdrSz = RdU16LE(th + 14);
            uint16_t height     = RdU16LE(th + 16);
            uint16_t width      = RdU16LE(th + 18);
            uint8_t  numMips    = th[20];
            uint8_t  dxgiFmt    = th[21];
            uint16_t tileMode   = RdU16LE(th + 22);
            if (chunkHdrSz < 24) chunkHdrSz = 24;
            pos += 24;
            if (pos + static_cast<size_t>(numChunks) * chunkHdrSz > size) break;

            uint64_t sumUnp = 148;
            uint64_t sumPack = 0;
            size_t chunksStart = pos;
            for (uint8_t c = 0; c < numChunks; ++c) {
                const uint8_t* cr = data + chunksStart + static_cast<size_t>(c) * chunkHdrSz;
                uint32_t pSz = RdU32LE(cr + 8);
                uint32_t uSz = RdU32LE(cr + 12);
                sumUnp  += uSz;
                sumPack += (pSz > 0) ? pSz : uSz;
            }
            pos += static_cast<size_t>(numChunks) * chunkHdrSz;

            std::wstring full = makeFallbackBa2Name(i, dirHash, nameHash, th + 4);
            if (extractTargetNorm && outExtracted) {
                if (PathEqualsCI(full, *extractTargetNorm)) {
                    if (sumUnp > 128 * 1024 * 1024) return false;
                    uint8_t ddsHdr[148] = {};
                    BuildBa2Dx10DdsHeader(width, height, numMips, dxgiFmt, tileMode, ddsHdr);
                    outExtracted->assign(ddsHdr, ddsHdr + 148);
                    outExtracted->reserve(static_cast<size_t>(sumUnp));
                    for (uint8_t c = 0; c < numChunks; ++c) {
                        const uint8_t* cr = data + chunksStart + static_cast<size_t>(c) * chunkHdrSz;
                        uint64_t cOff = RdU64LE(cr + 0);
                        uint32_t pSz  = RdU32LE(cr + 8);
                        uint32_t uSz  = RdU32LE(cr + 12);
                        if (pSz == 0) {
                            if (cOff + uSz > size) return false;
                            outExtracted->insert(outExtracted->end(), data + static_cast<size_t>(cOff), data + static_cast<size_t>(cOff + uSz));
                        } else {
                            if (cOff + pSz > size) return false;
                            std::vector<uint8_t> chunkDec;
                            const uint8_t* cSrc = data + static_cast<size_t>(cOff);
                            bool ok = ZipDeflate::InflateZlib(cSrc, pSz, chunkDec, 64 * 1024 * 1024) ||
                                      ZipDeflate::InflateRaw(cSrc, pSz, chunkDec, 64 * 1024 * 1024);
                            if (!ok) return false;
                            outExtracted->insert(outExtracted->end(), chunkDec.begin(), chunkDec.end());
                        }
                    }
                    return true;
                }
            } else if (outInfo) {
                ArchiveItemEntry item;
                item.pathInArchive = full;
                item.name = BaseName(full);
                item.uncompressedSize = sumUnp;
                item.compressedSize   = sumPack;
                item.compressionMethod = 8; // Prepends 148-byte DDS+DX10 header on extraction
                outInfo->entries.push_back(std::move(item));
            }
        }
    }

    if (extractTargetNorm) return false;
    if (outInfo) {
        outInfo->valid = !outInfo->entries.empty();
        return outInfo->valid;
    }
    return false;
}

inline bool ParseVpkOrBsa(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (ParseValveVpk(data, size, outInfo)) return true;
    if (ParseOrExtractBethesdaBsa(data, size, &outInfo, nullptr, nullptr)) return true;
    if (ParseOrExtractBethesdaBa2(data, size, &outInfo, nullptr, nullptr)) return true;
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

    size_t pos = 0;
    while (pos < json.size() && outInfo.entries.size() < 10000) {
        size_t szKey = json.find("\"size\":", pos);
        if (szKey == std::string_view::npos) break;
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
                    item.compressionMethod = 0;
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
    (void)archivePath;
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
    return false;
}

inline bool ExtractExtendedArchive100Entry(
    const std::wstring& archivePath,
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    // 1. Native Windows Cabinet (.cab) via SetupIterateCabinetW
    if (size >= 36 && std::memcmp(data, "MSCF\0\0\0\0", 8) == 0) {
        return StreamCodecs::ExtractCabEntryFromFile(archivePath, normTarget, outBytes);
    }
    // 2. Bethesda .bsa / .ba2
    if (size >= 36 && std::memcmp(data, "BSA\0", 4) == 0) {
        return DetailArc100::ParseOrExtractBethesdaBsa(data, size, nullptr, &normTarget, &outBytes);
    }
    if (size >= 24 && std::memcmp(data, "BTDX", 4) == 0) {
        return DetailArc100::ParseOrExtractBethesdaBa2(data, size, nullptr, &normTarget, &outBytes);
    }
    // 3. RedHat .rpm (Metadata + compressed CPIO extraction)
    if (size >= 112 && data[0] == 0xED && data[1] == 0xAB && data[2] == 0xEE && data[3] == 0xDB) {
        return DetailArc100::ParseOrExtractRedHatRpm(data, size, nullptr, &normTarget, &outBytes);
    }
    // 4. Windows Installer .msi / .msp (OLE2 streams + embedded MSCF CAB files)
    if (extLower == L".msi" || extLower == L".msp") {
        if (DetailArc100::ExtractOle2ArchiveEntry(data, size, extLower, normTarget, outBytes)) return true;
    }
    // 5. ISO-9660 Optical Disc
    if (size >= 0x8800 && data[0x8000] == 1 && std::memcmp(data + 0x8001, "CD001", 5) == 0) {
        ArchiveSummaryInfo dummy;
        if (DetailArc100::ParseIso9660(data, size, dummy, &normTarget, &outBytes)) return true;
    }
    // 6. Virtual Disk Images (.vhd, .vhdx, .vmdk, .vdi, .qcow2, .dmg)
    if (DetailArc100::InspectOrExtractVirtualDisk(data, size, extLower, nullptr, &normTarget, &outBytes)) {
        return true;
    }
    return false;
}

} // namespace Explorer2::Formats
