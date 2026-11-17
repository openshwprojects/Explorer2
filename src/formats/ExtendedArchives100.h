#pragma once
#include "../ByteIO.h"
// ExtendedArchives100.h — Popular Archive, Disk Image, Installer & Game Container Parsers + Extractors
// Covers: .iso, .zst/.tzst, .xz/.txz, .bz2/.tbz2, .lz4, .cab, .msi/.msp, .deb, .rpm,
//         .dmg, .vhd/.vhdx, .vmdk/.vdi, .qcow2, .wim/.esd, .pak/.wad, .vpk, .bsa/.ba2, .asar

#include "../ArchiveExplorer.h"
#include "../ZipDeflate.h"
#include "../Ole2Compound.h"
#include "../PathUtils.h"
#include "../StreamCodecs.h"
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

using ::Explorer2::ByteIO::RdU16LE;
using ::Explorer2::ByteIO::RdU32LE;
using ::Explorer2::ByteIO::RdU64LE;
using ::Explorer2::ByteIO::RdU16BE;
using ::Explorer2::ByteIO::RdU32BE;
using ::Explorer2::ByteIO::RdU64BE;
inline void WrU32LE(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFFu);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    p[2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
    p[3] = static_cast<uint8_t>((v >> 24) & 0xFFu);
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
    bool allowRawSliceFallback = true;
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
            std::wstring fullRel = ArchivePath::Normalize(parentDir.empty() ? wname : (parentDir + L"/" + wname));
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
                item.name = ArchivePath::BaseName(fullRel);
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

        std::wstring wpath = ArchivePath::Normalize((attribs & 0x80u)
            ? PathUtils::Utf8ToWide(rawName)
            : PathUtils::Utf8ToWide(rawName));
        if (!wpath.empty()) {
            std::wstring full = prefix.empty() ? wpath : ArchivePath::Normalize(prefix + L"/" + wpath);
            ArchiveItemEntry item;
            item.pathInArchive = full;
            item.name = ArchivePath::BaseName(full);
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

            std::wstring norm = ArchivePath::Normalize(PathUtils::Utf8ToWide(fname));
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
                    item.name = ArchivePath::BaseName(norm);
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

            std::wstring norm = ArchivePath::Normalize(PathUtils::Utf8ToWide(fname));
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
                    item.name = ArchivePath::BaseName(norm);
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

        std::wstring norm = ArchivePath::Normalize(PathUtils::Utf8ToWide(fullUtf8));
        if (norm.empty()) continue;

        bool isDir = (i < fileModes.size()) && ((fileModes[i] & 0170000u) == 0040000u);
        uint64_t fsz = isDir ? 0 : ((i < fileSizes.size()) ? fileSizes[i] : 0);

        ArchiveItemEntry item;
        item.pathInArchive = norm;
        item.name = ArchivePath::BaseName(norm);
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
    ::Explorer2::Base64::Decode(in, out);
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

    auto decompressAppleAdc = [](const uint8_t* src, size_t srcLen, std::vector<uint8_t>& out, size_t expectedLen) -> bool {
        out.clear();
        if (!src || expectedLen > 16 * 1024 * 1024) return false;
        out.reserve(expectedLen);
        size_t p = 0;
        while (p < srcLen && out.size() < expectedLen) {
            uint8_t b = src[p++];
            if ((b & 0x80u) != 0) {
                size_t litLen = static_cast<size_t>(b & 0x7Fu) + 1u;
                if (p + litLen > srcLen || out.size() + litLen > expectedLen) return false;
                out.insert(out.end(), src + p, src + p + litLen);
                p += litLen;
            } else if ((b & 0x40u) != 0) {
                if (p + 2 > srcLen) return false;
                uint8_t b1 = src[p++];
                uint8_t b2 = src[p++];
                size_t matchLen = static_cast<size_t>(b & 0x3Fu) + 4u;
                size_t matchOff = (static_cast<size_t>(b1) << 8) + static_cast<size_t>(b2) + 1u;
                if (matchOff > out.size() || out.size() + matchLen > expectedLen) return false;
                for (size_t k = 0; k < matchLen; ++k) {
                    out.push_back(out[out.size() - matchOff]);
                }
            } else {
                if (p + 1 > srcLen) return false;
                uint8_t b1 = src[p++];
                size_t matchLen = static_cast<size_t>((b >> 2) & 0x0Fu) + 3u;
                size_t matchOff = (static_cast<size_t>(b & 0x03u) << 8) + static_cast<size_t>(b1) + 1u;
                if (matchOff > out.size() || out.size() + matchLen > expectedLen) return false;
                for (size_t k = 0; k < matchLen; ++k) {
                    out.push_back(out[out.size() - matchOff]);
                }
            }
        }
        return out.size() == expectedLen;
    };

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
                        if (et == 0x7FFFFFFEu) continue; // BT_COMMENT
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
            struct DmgCache {
                size_t runIdx = SIZE_MAX;
                std::vector<uint8_t> dec;
            };
            auto runCache = std::make_shared<DmgCache>();
            outReader.virtualSize = maxEnd;
            outReader.formatName  = L"Apple Disk Image (DMG)";
            outReader.readBytes = [=](uint64_t diskOff, uint8_t* dst, size_t len) -> bool {
                if (diskOff + len > maxEnd) return false;
                std::memset(dst, 0, len);
                for (size_t ri = 0; ri < runs->size(); ++ri) {
                    const auto& r = (*runs)[ri];
                    uint64_t rEnd = r.diskStart + r.diskLen;
                    uint64_t qEnd = diskOff + len;
                    if (rEnd <= diskOff || r.diskStart >= qEnd) continue;
                    uint64_t ovStart = std::max(diskOff, r.diskStart);
                    uint64_t ovEnd   = std::min(qEnd, rEnd);
                    size_t copyLen   = static_cast<size_t>(ovEnd - ovStart);
                    size_t dstOff    = static_cast<size_t>(ovStart - diskOff);
                    size_t runRel    = static_cast<size_t>(ovStart - r.diskStart);

                    if (r.type == 0x00000000u || r.type == 0x00000002u) {
                        // Zero-fill / Ignore run
                        std::memset(dst + dstOff, 0, copyLen);
                    } else if (r.type == 0x00000001u) {
                        if (r.compOff + runRel + copyLen > size) return false;
                        std::memcpy(dst + dstOff, data + static_cast<size_t>(r.compOff) + runRel, copyLen);
                    } else if (r.type == 0x80000004u || r.type == 0x80000005u || r.type == 0x80000006u) {
                        if (r.compOff + r.compLen > size || r.diskLen > 16 * 1024 * 1024) return false;
                        if (runCache->runIdx != ri) {
                            std::vector<uint8_t> dec;
                            bool ok = false;
                            const uint8_t* cSrc = data + static_cast<size_t>(r.compOff);
                            size_t cSz = static_cast<size_t>(r.compLen);
                            size_t uSz = static_cast<size_t>(r.diskLen);
                            if (r.type == 0x80000004u) {
                                ok = decompressAppleAdc(cSrc, cSz, dec, uSz);
                            } else if (r.type == 0x80000005u) {
                                ok = ZipDeflate::InflateZlib(cSrc, cSz, dec, uSz) ||
                                     ZipDeflate::InflateRaw(cSrc, cSz, dec, uSz);
                            } else if (r.type == 0x80000006u) {
                                ok = StreamCodecs::DecompressBzip2Stream(cSrc, cSz, dec, uSz);
                            }
                            if (!ok) return false;
                            runCache->runIdx = ri;
                            runCache->dec = std::move(dec);
                        }
                        if (runRel + copyLen > runCache->dec.size()) return false;
                        std::memcpy(dst + dstOff, runCache->dec.data() + runRel, copyLen);
                    }
                }
                return true;
            };
            outReader.allowRawSliceFallback = false;
            return true;
        }

        // Raw data fork before koly trailer
        size_t rawEnd = size - 512;
        outReader.virtualSize = rawEnd;
        outReader.formatName  = L"Apple Disk Image (DMG)";
        outReader.directRawBuffer = true;
        outReader.allowRawSliceFallback = false;
        outReader.readBytes = [=](uint64_t diskOff, uint8_t* dst, size_t len) -> bool {
            if (diskOff + len > rawEnd) return false;
            std::memcpy(dst, data + static_cast<size_t>(diskOff), len);
            return true;
        };
        return true;
    }

    bool hasRawHfs  = (size >= 2048 && data[1024] == 'H' && (data[1025] == '+' || data[1025] == 'X'));
    bool hasRawApm  = (size >= 1024 && data[512] == 'P' && data[513] == 'M');
    bool hasRawExt  = (size >= 2048 && RdU16LE(data + 1024 + 56) == 0xEF53u);
    if (hasRawHfs || hasRawApm || hasRawExt ||
        ((extLower == L".dmg" || extLower == L".hfs" || extLower == L".ext2" ||
          extLower == L".ext3" || extLower == L".ext4") && size >= 1024))
    {
        outReader.virtualSize = size;
        outReader.formatName  = (hasRawExt || extLower == L".ext2" || extLower == L".ext3" || extLower == L".ext4")
                              ? L"Linux ext2/ext3/ext4 Image"
                              : ((hasRawHfs || extLower == L".hfs") ? L"Apple HFS+ Volume" : L"Apple Disk Image (DMG)");
        outReader.directRawBuffer = true;
        outReader.allowRawSliceFallback = false;
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
        std::wstring fullRel = ArchivePath::Normalize(parentDir.empty() ? entryName : (parentDir + L"/" + entryName));

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
            item.name = ArchivePath::BaseName(fullRel);
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
        return ArchivePath::Normalize(out);
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
            item.name = ArchivePath::BaseName(fullRel);
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
// Linux ext2/ext3/ext4 Full Inode, Extent-Tree & Recursive Directory Inspector + Extractor
// ----------------------------------------------------------------------------
inline void UnixTimeU32ToDos(uint32_t unixSecs, uint16_t& outDate, uint16_t& outTime) {
    outDate = 0;
    outTime = 0;
    if (unixSecs == 0) return;
    uint64_t ticks = (static_cast<uint64_t>(unixSecs) + 11644473600ULL) * 10000000ULL;
    FileTimeU64ToDos(ticks, outDate, outTime);
}

inline bool InspectOrExtractExtFsOnReader(
    const VirtualDiskReader& reader,
    uint64_t partBase,
    uint64_t partBytes,
    const std::wstring& prefixDir,
    ArchiveSummaryInfo& outInfo,
    const std::wstring* extractTargetNorm = nullptr,
    std::vector<uint8_t>* outExtracted = nullptr)
{
    if (partBytes < 2048 || partBase > reader.virtualSize || partBytes > reader.virtualSize - partBase) return false;
    uint8_t sb[1024] = {};
    if (!reader.readBytes(partBase + 1024, sb, 1024)) return false;
    if (RdU16LE(sb + 56) != 0xEF53u) return false;

    uint32_t inodesCnt    = RdU32LE(sb + 0);
    uint32_t firstDataBlk = RdU32LE(sb + 20);
    uint32_t logBlkSz     = RdU32LE(sb + 24);
    uint32_t blocksPerGrp = RdU32LE(sb + 32);
    uint32_t inodesPerGrp = RdU32LE(sb + 40);
    uint32_t revLevel     = RdU32LE(sb + 76);
    uint16_t inodeSz      = (revLevel >= 1) ? RdU16LE(sb + 88) : 128;
    uint32_t incompat     = RdU32LE(sb + 96);
    uint16_t rawDescSz    = RdU16LE(sb + 254);

    if (logBlkSz > 6) return false;
    uint32_t blockSize = 1024u << logBlkSz;
    if (inodeSz < 128 || inodeSz > blockSize || inodesCnt < 2 || inodesPerGrp == 0 || blocksPerGrp == 0) return false;
    (void)firstDataBlk;

    bool has64Bit    = (incompat & 0x80u) != 0;
    bool hasFileType = (incompat & 0x02u) != 0;
    uint32_t descSize = (has64Bit && rawDescSz >= 64) ? rawDescSz : 32u;
    if (descSize > 1024) return false;

    uint64_t bgdtBase = partBase + ((blockSize == 1024u) ? 2048ULL : static_cast<uint64_t>(blockSize));

    auto readExtInode = [&](uint32_t inoNum, std::vector<uint8_t>& outIno) -> bool {
        if (inoNum == 0 || inoNum > inodesCnt) return false;
        uint32_t bg  = (inoNum - 1u) / inodesPerGrp;
        uint32_t idx = (inoNum - 1u) % inodesPerGrp;
        uint64_t gdOff = bgdtBase + static_cast<uint64_t>(bg) * descSize;
        size_t gdNeed = (descSize >= 64) ? 64u : 32u;
        if (gdOff + gdNeed > partBase + partBytes || gdOff + gdNeed > reader.virtualSize) return false;
        uint8_t gd[64] = {};
        if (!reader.readBytes(gdOff, gd, gdNeed)) return false;

        uint64_t inoTblBlk = RdU32LE(gd + 8);
        if (descSize >= 64) {
            inoTblBlk |= (static_cast<uint64_t>(RdU32LE(gd + 40)) << 32);
        }
        if (inoTblBlk == 0) return false;
        uint64_t inoOff = partBase + inoTblBlk * blockSize + static_cast<uint64_t>(idx) * inodeSz;
        if (inoOff + inodeSz > partBase + partBytes || inoOff + inodeSz > reader.virtualSize) return false;
        outIno.resize(inodeSz);
        return reader.readBytes(inoOff, outIno.data(), inodeSz);
    };

    auto getInodeSize = [&](const std::vector<uint8_t>& ino) -> uint64_t {
        uint16_t mode = RdU16LE(ino.data() + 0);
        uint64_t sz   = RdU32LE(ino.data() + 4);
        if (((mode & 0xF000u) == 0x8000u) && revLevel >= 1) {
            sz |= (static_cast<uint64_t>(RdU32LE(ino.data() + 108)) << 32);
        }
        return sz;
    };

    auto readInodeData = [&](const std::vector<uint8_t>& ino, uint64_t maxBytes, std::vector<uint8_t>& outData) -> bool {
        uint16_t mode   = RdU16LE(ino.data() + 0);
        uint32_t iFlags = RdU32LE(ino.data() + 32);
        uint64_t fSize  = getInodeSize(ino);
        if (fSize > maxBytes) return false;
        outData.assign(static_cast<size_t>(fSize), 0);
        if (fSize == 0) return true;

        // Fast symlink or inline data stored directly inside i_block[0..14] (60 bytes)
        if (((iFlags & 0x10000000u) != 0 || ((mode & 0xF000u) == 0xA000u && RdU32LE(ino.data() + 28) == 0)) && fSize <= 60) {
            std::memcpy(outData.data(), ino.data() + 40, static_cast<size_t>(fSize));
            return true;
        }

        // ext4 extent tree (EXT4_EXTENTS_FL = 0x00080000, eh_magic = 0xF30A)
        if ((iFlags & 0x00080000u) != 0) {
            auto walkExtentNode = [&](auto& self, const uint8_t* ehBuf, size_t bufLen, int depthGuard) -> bool {
                if (depthGuard > 8 || bufLen < 12) return false;
                if (RdU16LE(ehBuf) != 0xF30Au) return false;
                uint16_t ehEntries = RdU16LE(ehBuf + 2);
                uint16_t ehDepth   = RdU16LE(ehBuf + 6);
                if (12u + static_cast<size_t>(ehEntries) * 12u > bufLen) return false;

                if (ehDepth == 0) {
                    for (uint16_t e = 0; e < ehEntries; ++e) {
                        const uint8_t* ex = ehBuf + 12 + static_cast<size_t>(e) * 12;
                        uint32_t eeBlock  = RdU32LE(ex + 0);
                        uint16_t eeRawLen = RdU16LE(ex + 4);
                        bool uninit       = (eeRawLen > 32768u);
                        uint32_t eeLen    = uninit ? (eeRawLen - 32768u) : eeRawLen;
                        uint64_t eeStart  = (static_cast<uint64_t>(RdU16LE(ex + 6)) << 32) | RdU32LE(ex + 8);
                        if (eeLen == 0 || uninit) continue; // Sparse/uninitialized run already zero-filled
                        uint64_t logOff = static_cast<uint64_t>(eeBlock) * blockSize;
                        if (logOff >= fSize) continue;
                        uint64_t runBytes = std::min<uint64_t>(static_cast<uint64_t>(eeLen) * blockSize, fSize - logOff);
                        uint64_t physOff  = partBase + eeStart * blockSize;
                        if (eeStart == 0 || physOff + runBytes > partBase + partBytes || physOff + runBytes > reader.virtualSize) {
                            return false;
                        }
                        if (!reader.readBytes(physOff, outData.data() + static_cast<size_t>(logOff), static_cast<size_t>(runBytes))) {
                            return false;
                        }
                    }
                    return true;
                } else {
                    for (uint16_t e = 0; e < ehEntries; ++e) {
                        const uint8_t* ix = ehBuf + 12 + static_cast<size_t>(e) * 12;
                        uint64_t leafBlk  = (static_cast<uint64_t>(RdU16LE(ix + 8)) << 32) | RdU32LE(ix + 4);
                        uint64_t physOff  = partBase + leafBlk * blockSize;
                        if (leafBlk == 0 || physOff + blockSize > partBase + partBytes || physOff + blockSize > reader.virtualSize) {
                            return false;
                        }
                        std::vector<uint8_t> nodeBlk(blockSize);
                        if (!reader.readBytes(physOff, nodeBlk.data(), blockSize)) return false;
                        if (!self(self, nodeBlk.data(), blockSize, depthGuard + 1)) return false;
                    }
                    return true;
                }
            };
            return walkExtentNode(walkExtentNode, ino.data() + 40, 60, 0);
        }

        // Legacy ext2/ext3 direct + single/double/triple indirect block pointers
        uint64_t curLogOff = 0;
        uint32_t ptrsPerBlk = blockSize / 4u;
        auto readPhysBlock = [&](uint32_t blkId) -> bool {
            if (curLogOff >= fSize) return true;
            size_t toCopy = static_cast<size_t>(std::min<uint64_t>(blockSize, fSize - curLogOff));
            if (blkId != 0) {
                uint64_t physOff = partBase + static_cast<uint64_t>(blkId) * blockSize;
                if (physOff + toCopy > partBase + partBytes || physOff + toCopy > reader.virtualSize) return false;
                if (!reader.readBytes(physOff, outData.data() + static_cast<size_t>(curLogOff), toCopy)) return false;
            }
            curLogOff += toCopy;
            return true;
        };

        auto readIndirect = [&](auto& self, uint32_t indBlk, int level) -> bool {
            if (curLogOff >= fSize) return true;
            if (indBlk == 0) {
                uint64_t skipBlks = 1;
                for (int l = 0; l < level; ++l) skipBlks *= ptrsPerBlk;
                curLogOff = std::min<uint64_t>(fSize, curLogOff + skipBlks * blockSize);
                return true;
            }
            uint64_t physOff = partBase + static_cast<uint64_t>(indBlk) * blockSize;
            if (physOff + blockSize > partBase + partBytes || physOff + blockSize > reader.virtualSize) return false;
            std::vector<uint8_t> ibuf(blockSize);
            if (!reader.readBytes(physOff, ibuf.data(), blockSize)) return false;
            for (uint32_t i = 0; i < ptrsPerBlk && curLogOff < fSize; ++i) {
                uint32_t childBlk = RdU32LE(ibuf.data() + i * 4u);
                if (level == 1) {
                    if (!readPhysBlock(childBlk)) return false;
                } else {
                    if (!self(self, childBlk, level - 1)) return false;
                }
            }
            return true;
        };

        for (int b = 0; b < 12 && curLogOff < fSize; ++b) {
            if (!readPhysBlock(RdU32LE(ino.data() + 40 + b * 4))) return false;
        }
        if (curLogOff < fSize && !readIndirect(readIndirect, RdU32LE(ino.data() + 40 + 12 * 4), 1)) return false;
        if (curLogOff < fSize && !readIndirect(readIndirect, RdU32LE(ino.data() + 40 + 13 * 4), 2)) return false;
        if (curLogOff < fSize && !readIndirect(readIndirect, RdU32LE(ino.data() + 40 + 14 * 4), 3)) return false;
        return true;
    };

    auto isDirEmptyLostAndFound = [&](const std::vector<uint8_t>& dirIno) -> bool {
        std::vector<uint8_t> dBytes;
        if (!readInodeData(dirIno, 256 * 1024, dBytes)) return true;
        size_t dp = 0;
        while (dp + 8 <= dBytes.size()) {
            uint32_t dIno = RdU32LE(dBytes.data() + dp);
            uint16_t rLen = RdU16LE(dBytes.data() + dp + 4);
            uint8_t  nLen = dBytes[dp + 6];
            if (rLen < 8 || dp + rLen > dBytes.size()) break;
            if (dIno != 0 && nLen > 0 && dp + 8 + nLen <= dBytes.size()) {
                std::string_view nm(reinterpret_cast<const char*>(dBytes.data() + dp + 8), nLen);
                if (nm != "." && nm != "..") return false;
            }
            dp += rLen;
        }
        return true;
    };

    std::vector<uint8_t> rootIno;
    if (!readExtInode(2u, rootIno) || (RdU16LE(rootIno.data()) & 0xF000u) != 0x4000u) return false;

    std::set<uint32_t> visitedDirs;
    size_t addedEntries = 0;
    bool extractedFound = false;

    auto walkDir = [&](auto& self, uint32_t dirInoNum, const std::vector<uint8_t>& dirIno, const std::wstring& curPrefix, int depth) -> bool {
        if (depth > 16 || outInfo.entries.size() >= 50000) return true;
        if (!visitedDirs.insert(dirInoNum).second) return true;

        std::vector<uint8_t> dirBytes;
        if (!readInodeData(dirIno, 16 * 1024 * 1024, dirBytes)) return (depth > 0);

        size_t dp = 0;
        while (dp + 8 <= dirBytes.size() && outInfo.entries.size() < 50000) {
            uint32_t dIno = RdU32LE(dirBytes.data() + dp);
            uint16_t rLen = RdU16LE(dirBytes.data() + dp + 4);
            uint8_t  nLen = dirBytes[dp + 6];
            uint8_t  fTyp = hasFileType ? dirBytes[dp + 7] : 0u;
            if (rLen < 8 || dp + rLen > dirBytes.size()) break;

            if (dIno != 0 && nLen > 0 && dp + 8 + nLen <= dirBytes.size()) {
                std::string nm(reinterpret_cast<const char*>(dirBytes.data() + dp + 8), nLen);
                if (nm != "." && nm != ".." && nm.find('/') == std::string::npos && nm.find('\\') == std::string::npos) {
                    std::vector<uint8_t> childIno;
                    if (readExtInode(dIno, childIno)) {
                        uint16_t cMode = RdU16LE(childIno.data() + 0);
                        uint16_t typeBits = cMode & 0xF000u;
                        bool isDir = (typeBits == 0x4000u) || (typeBits == 0 && fTyp == 2u);
                        bool isReg = (typeBits == 0x8000u) || (typeBits == 0xA000u) || (typeBits == 0 && (fTyp == 1u || fTyp == 7u));

                        if (isDir && depth == 0 && nm == "lost+found" && isDirEmptyLostAndFound(childIno)) {
                            dp += rLen;
                            continue;
                        }

                        if (isDir || isReg) {
                            std::wstring wnm = PathUtils::Utf8ToWide(nm);
                            std::wstring full = ArchivePath::Normalize(curPrefix.empty() ? wnm : (curPrefix + L"/" + wnm));

                            if (extractTargetNorm && outExtracted) {
                                if (!isDir && PathEqualsCI(full, *extractTargetNorm)) {
                                    if (!readInodeData(childIno, 256ULL * 1024 * 1024, *outExtracted)) return false;
                                    extractedFound = true;
                                    return true;
                                }
                            } else {
                                ArchiveItemEntry item;
                                item.pathInArchive = full;
                                item.name = ArchivePath::BaseName(full);
                                item.isDirectory = isDir;
                                item.uncompressedSize = isDir ? 0 : getInodeSize(childIno);
                                item.compressedSize = item.uncompressedSize;
                                item.compressionMethod = 1;
                                UnixTimeU32ToDos(RdU32LE(childIno.data() + 16), item.dosDate, item.dosTime);
                                outInfo.entries.push_back(std::move(item));
                                ++addedEntries;
                            }

                            if (isDir) {
                                if (!self(self, dIno, childIno, full, depth + 1)) return false;
                                if (extractedFound) return true;
                            }
                        }
                    }
                }
            }
            dp += rLen;
        }
        return true;
    };

    if (!walkDir(walkDir, 2u, rootIno, prefixDir, 0)) return false;
    if (extractTargetNorm) return extractedFound;
    return addedEntries > 0;
}

// ----------------------------------------------------------------------------
// Apple HFS+ / HFSX Catalog B-Tree Inspector & Data-Fork Extent Extractor
// ----------------------------------------------------------------------------
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
            uint64_t physOff = volBase + static_cast<uint64_t>(ex.startBlock) * blockSize + relInEx;
            if (physOff + chunk > volBase + volBytes || physOff + chunk > reader.virtualSize) return false;
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
    // Mac epoch 1904-01-01 -> Unix epoch 1970-01-01 delta is 2082844800 seconds
    if (macSecs1904 <= 2082844800u) return;
    UnixTimeU32ToDos(macSecs1904 - 2082844800u, outDate, outTime);
}

inline bool InspectOrExtractHfsPlusOnReader(
    const VirtualDiskReader& reader,
    uint64_t partBase,
    uint64_t partBytes,
    const std::wstring& prefixDir,
    ArchiveSummaryInfo& outInfo,
    const std::wstring* extractTargetNorm = nullptr,
    std::vector<uint8_t>* outExtracted = nullptr)
{
    if (partBytes < 2048 || partBase > reader.virtualSize || partBytes > reader.virtualSize - partBase) return false;

    uint64_t volBase  = partBase;
    uint64_t volBytes = partBytes;
    uint8_t vh[512]   = {};
    if (!reader.readBytes(volBase + 1024, vh, 512)) return false;

    // Optional HFS Standard ("BD") wrapper embedding an HFS+ ("H+"/"HX") volume
    if (vh[0] == 'B' && vh[1] == 'D' && vh[124] == 'H' && (vh[125] == '+' || vh[125] == 'X')) {
        uint32_t alBlkSiz  = RdU32BE(vh + 20);
        uint16_t alBlSt    = RdU16BE(vh + 28);
        uint16_t embStart  = RdU16BE(vh + 126);
        uint16_t embCount  = RdU16BE(vh + 128);
        uint64_t embOffset = static_cast<uint64_t>(alBlSt) * 512ULL + static_cast<uint64_t>(embStart) * alBlkSiz;
        uint64_t embLength = static_cast<uint64_t>(embCount) * alBlkSiz;
        if (alBlkSiz >= 512 && embOffset + 2048 <= partBytes && embLength >= 2048) {
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

    // Probe Catalog File ForkData at Apple TN1150 offset +272 (and fallback +208)
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
        // BTNodeDescriptor: kind == 1 (kBTHeaderNode) at +8
        if (static_cast<int8_t>(node0[8]) != 1) return false;
        // BTHeaderRec starts at node0 + 14
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
        if (!visitedNodes.insert(curNode).second) return false; // Cycle in fLink
        if (!ReadHfsForkBytes(reader, volBase, volBytes, blockSize, catalogFork,
                              static_cast<uint64_t>(curNode) * nodeSize, nodeBuf.data(), nodeSize))
        {
            return false;
        }
        if (static_cast<int8_t>(nodeBuf[8]) != -1) return false; // Must be kBTLeafNode (-1 / 0xFF)

        uint16_t numRecs = RdU16BE(nodeBuf.data() + 10);
        if (numRecs == 0 && nodeBuf[9] == 0) {
            // Tolerance if height byte was omitted in non-standard writer (numRecords at +8..9 would conflict with kind, so keep strict +10)
        }
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
            if (recType != 0x0001 && recType != 0x0002) continue; // Skip thread records 0x0003 / 0x0004

            // Decode HFSUniStr255 UTF-16BE nodeName
            if (uniLen > 0 && RdU16BE(nodeBuf.data() + recOff + 8) == 0) continue; // Skip \0\0\0\0HFS+ Private Data
            std::wstring wname;
            wname.reserve(uniLen);
            for (uint16_t c = 0; c < uniLen; ++c) {
                wchar_t wc = static_cast<wchar_t>(RdU16BE(nodeBuf.data() + recOff + 8 + c * 2u));
                if (wc == 0) break;
                if (wc == L'/' || wc == L'\\' || wc == L':') wc = L'_';
                wname.push_back(wc);
            }

            if (recType == 0x0001) {
                // kHFSPlusFolderRecord (88 bytes; folderID at +8, contentModDate at +16)
                if (valOff + 20u > nextOff) continue;
                uint32_t folderId = RdU32BE(nodeBuf.data() + valOff + 8);
                uint32_t modDate  = RdU32BE(nodeBuf.data() + valOff + 16);
                HfsCatFolder fld{ folderId, parentCnid, wname, modDate };
                foldersMap[folderId] = fld;
                if (folderId != 2u && !wname.empty()) {
                    foldersOrder.push_back(std::move(fld));
                }
            } else if (recType == 0x0002) {
                // kHFSPlusFileRecord (248 bytes; fileID at +8, contentModDate at +16, dataFork at +88..167)
                if (valOff + 168u > nextOff || wname.empty()) continue;
                // Skip HFS+ hard-link private inode metadata files under private dir
                HfsCatFile fl;
                fl.fileId   = RdU32BE(nodeBuf.data() + valOff + 8);
                fl.parentId = parentCnid;
                fl.name     = std::move(wname);
                fl.modDate  = RdU32BE(nodeBuf.data() + valOff + 16);
                fl.dataFork = ParseHfsForkData(nodeBuf.data() + valOff + 88);
                filesList.push_back(std::move(fl));
            }
        }

        curNode = RdU32BE(nodeBuf.data() + 0); // fLink
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
        if (cur != 2u) return false; // Must descend from kHFSRootFolderID == 2

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

struct DiscoveredPartition {
    uint64_t byteOffset = 0;
    uint64_t byteLength = 0;
    uint8_t  mbrType = 0;
    std::wstring label;
};

inline void DiscoverDiskPartitions(const VirtualDiskReader& reader, std::vector<DiscoveredPartition>& outParts) {
    outParts.clear();
    if (reader.virtualSize < 512) return;

    // 1. Check if LBA 0 is already a superfloppy FAT / NTFS / HFS+ / ext2-4 filesystem
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
    if (reader.virtualSize >= 2048) {
        uint8_t sbProbe[128] = {};
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
                if (entOff + 512ULL > reader.virtualSize) break;
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

        // Check +1024 signatures for Linux ext2/3/4 and Apple HFS+/HFSX
        bool hasExtMagic = false;
        bool hasHfsMagic = false;
        if (p.byteLength >= 2048 && p.byteOffset + 1152 <= reader.virtualSize) {
            uint8_t sigBuf[128] = {};
            if (reader.readBytes(p.byteOffset + 1024, sigBuf, sizeof(sigBuf))) {
                hasExtMagic = (RdU16LE(sigBuf + 56) == 0xEF53u);
                hasHfsMagic = (sigBuf[0] == 'H' && (sigBuf[1] == '+' || sigBuf[1] == 'X')) ||
                              (sigBuf[0] == 'B' && sigBuf[1] == 'D' && sigBuf[124] == 'H' && (sigBuf[125] == '+' || sigBuf[125] == 'X'));
            }
        }

        // D. Try Linux ext2/ext3/ext4
        if (hasExtMagic) {
            ArchiveSummaryInfo tmp;
            ArchiveSummaryInfo& dstInfo = outInfo ? *outInfo : tmp;
            if (InspectOrExtractExtFsOnReader(reader, p.byteOffset, p.byteLength, prefix, dstInfo, targetPtr, outExtracted)) {
                if (extractTargetNorm) return true;
            }
            continue; // Never emit fake fallback items for corrupt ext partitions
        }

        // E. Try Apple HFS+ / HFSX Catalog B-Tree
        if (hasHfsMagic) {
            ArchiveSummaryInfo tmp;
            ArchiveSummaryInfo& dstInfo = outInfo ? *outInfo : tmp;
            if (InspectOrExtractHfsPlusOnReader(reader, p.byteOffset, p.byteLength, prefix, dstInfo, targetPtr, outExtracted)) {
                if (extractTargetNorm) return true;
            }
            continue; // Never emit fake HFSPlus_Volume_ fallback items
        }

        // F. Raw partition slice (only for real virtual disk containers like fresh VHD/VHDX/VMDK/VDI/QCOW2)
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

inline bool ParseVirtualDiskImage(const uint8_t* data, size_t size, const std::wstring& extLower, ArchiveSummaryInfo& outInfo) {
    return InspectOrExtractVirtualDisk(data, size, extLower, &outInfo, nullptr, nullptr);
}

// ============================================================================
// 32. Windows Imaging Format (.wim / .esd — "MSWIM\0\0\0")
//     XPRESS Huffman + LZX Chunked Resources, XML Manifest, RESHDR Lookup & DIRENTRY Tree
// ============================================================================
struct WimResEntry {
    uint64_t wimOffset = 0;
    uint64_t compSize = 0;
    uint64_t origSize = 0;
    uint8_t  flags = 0; // 0x02 = Metadata, 0x04 = Compressed
    uint8_t  sha1[20] = {};
};

// [MS-XCA] 2.2.4 XPRESS Huffman Block Decompressor (256B nibble table + 16-bit LE MSB bitstream)
inline bool DecompressXpressHuffmanChunk(
    const uint8_t* src,
    size_t srcLen,
    uint8_t* dst,
    size_t dstLen)
{
    if (dstLen == 0) return true;
    if (srcLen < 260) return false;

    uint8_t lens[512] = {};
    uint32_t blCount[16] = {};
    uint32_t nonZeroSyms = 0;
    for (size_t i = 0; i < 256; ++i) {
        uint8_t l0 = src[i] & 0x0Fu;
        uint8_t l1 = (src[i] >> 4) & 0x0Fu;
        lens[2 * i]     = l0;
        lens[2 * i + 1] = l1;
        if (l0 > 0) { ++blCount[l0]; ++nonZeroSyms; }
        if (l1 > 0) { ++blCount[l1]; ++nonZeroSyms; }
    }
    if (nonZeroSyms == 0) return false;

    uint32_t nextCode[16] = {};
    uint32_t code = 0;
    for (int bits = 1; bits <= 15; ++bits) {
        code = (code + blCount[bits - 1]) << 1;
        nextCode[bits] = code;
    }
    if (code + blCount[15] > 32768u) return false;
    // Allow single-symbol degenerate tree (nonZeroSyms == 1 && blCount[1] == 1), otherwise require full Kraft tree
    if (nonZeroSyms > 1 && (code + blCount[15]) != 32768u) return false;

    std::vector<uint16_t> decTbl(32768u, 0xFFFFu);
    for (uint32_t sym = 0; sym < 512u; ++sym) {
        uint8_t len = lens[sym];
        if (len == 0) continue;
        uint32_t c = nextCode[len]++;
        uint32_t shift = 15u - len;
        uint32_t start = c << shift;
        uint32_t count = 1u << shift;
        if (start + count > 32768u) return false;
        uint16_t entry = static_cast<uint16_t>((static_cast<uint16_t>(len) << 9) | sym);
        for (uint32_t k = 0; k < count; ++k) decTbl[start + k] = entry;
    }
    if (nonZeroSyms == 1) {
        for (uint32_t k = 0; k < 32768u; ++k) {
            if (decTbl[k] == 0xFFFFu) decTbl[k] = decTbl[0];
        }
    }

    uint32_t bitBuf = (static_cast<uint32_t>(RdU16LE(src + 256)) << 16) | RdU16LE(src + 258);
    int extraBits = 16; // validBits = 16 + extraBits; refill next U16LE when extraBits < 0
    size_t inPos = 260;

    auto consumeBits = [&](int n) -> bool {
        if (16 + extraBits < n) return false;
        bitBuf = (n >= 32) ? 0u : (bitBuf << n);
        extraBits -= n;
        if (extraBits < 0) {
            if (inPos + 2 <= srcLen) {
                uint32_t w = RdU16LE(src + inPos);
                inPos += 2;
                bitBuf |= (w << (-extraBits));
                extraBits += 16;
            } else if (inPos < srcLen) {
                uint32_t w = static_cast<uint32_t>(src[inPos++]) << 8;
                bitBuf |= (w << (-extraBits));
                extraBits += 16;
            }
        }
        return true;
    };

    size_t outPos = 0;
    while (outPos < dstLen) {
        uint16_t entry = decTbl[bitBuf >> 17];
        if (entry == 0xFFFFu) return false;
        int symLen   = entry >> 9;
        uint32_t sym = entry & 0x1FFu;
        if (!consumeBits(symLen)) return false;

        if (sym < 256u) {
            dst[outPos++] = static_cast<uint8_t>(sym);
        } else {
            uint32_t msym = sym - 256u;
            int offsetBits = static_cast<int>(msym >> 4);
            size_t matchLen = static_cast<size_t>(msym & 0x0Fu);

            if (matchLen == 15u) {
                if (inPos >= srcLen) return false;
                uint32_t b1 = src[inPos++];
                if (b1 < 255u) {
                    matchLen = static_cast<size_t>(b1) + 15u;
                } else {
                    if (inPos + 2 > srcLen) return false;
                    uint32_t w2 = RdU16LE(src + inPos);
                    inPos += 2;
                    if (w2 != 0) {
                        if (w2 < 15u) return false;
                        matchLen = static_cast<size_t>(w2);
                    } else {
                        if (inPos + 4 > srcLen) return false;
                        uint32_t d4 = RdU32LE(src + inPos);
                        inPos += 4;
                        if (d4 < 15u) return false;
                        matchLen = static_cast<size_t>(d4);
                    }
                }
            }
            matchLen += 3u;

            uint32_t lowOff = (offsetBits == 0) ? 0u : (bitBuf >> (32 - offsetBits));
            if (!consumeBits(offsetBits)) return false;
            size_t matchOff = (static_cast<size_t>(1) << offsetBits) | static_cast<size_t>(lowOff);

            if (matchOff == 0 || matchOff > outPos || matchLen > dstLen - outPos) return false;
            for (size_t k = 0; k < matchLen; ++k) {
                dst[outPos + k] = dst[outPos - matchOff + k];
            }
            outPos += matchLen;
        }
    }
    return true;
}

// [MS-PATCH] / WIM LZX Chunk Decompressor (window reset per WIM chunk, 16-bit LE MSB bitstream)
inline bool DecompressWimLzxChunk(
    const uint8_t* src,
    size_t srcLen,
    uint8_t* dst,
    size_t dstLen,
    uint32_t windowSize)
{
    if (dstLen == 0) return true;
    if (srcLen < 2) return false;

    // Determine LZX position slot count from windowSize (32KB -> 30 slots, 64KB -> 32, ..., 2MB -> 50)
    int wndBits = 15;
    while ((1u << wndBits) < windowSize && wndBits < 21) ++wndBits;
    static const uint32_t kPosBase[51] = {
        0,1,2,3,4,6,8,12,16,24,32,48,64,96,128,192,256,384,512,768,1024,1536,2048,3072,
        4096,6144,8192,12288,16384,24576,32768,49152,65536,98304,131072,196608,262144,
        393216,524288,655360,786432,917504,1048576,1179648,1310720,1441792,1572864,1703936,
        1835008,1966080,2097152
    };
    static const uint8_t kExtraBits[51] = {
        0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13,14,14,
        15,15,16,16,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17
    };
    uint32_t numPosSlots = (wndBits == 20) ? 42u : ((wndBits == 21) ? 50u : static_cast<uint32_t>(wndBits * 2));
    uint32_t mainTreeSyms = 256u + numPosSlots * 8u;

    uint64_t bitBuf = 0;
    int validBits = 0;
    size_t inPos = 0;
    auto ensureBits = [&](int need) -> bool {
        while (validBits < need) {
            uint16_t w = 0;
            if (inPos + 2 <= srcLen) {
                w = RdU16LE(src + inPos);
                inPos += 2;
            } else if (inPos < srcLen) {
                w = static_cast<uint16_t>(src[inPos++]);
            } else {
                if (validBits >= need) break;
                return false;
            }
            bitBuf |= (static_cast<uint64_t>(w) << (48 - validBits));
            validBits += 16;
        }
        return true;
    };
    auto peekBits = [&](int n) -> uint32_t {
        return (n == 0) ? 0u : static_cast<uint32_t>(bitBuf >> (64 - n));
    };
    auto dropBits = [&](int n) {
        bitBuf <<= n;
        validBits -= n;
    };
    auto readBits = [&](int n, uint32_t& outV) -> bool {
        if (n == 0) { outV = 0; return true; }
        if (!ensureBits(n)) return false;
        outV = peekBits(n);
        dropBits(n);
        return true;
    };

    auto buildHuff = [](const uint8_t* lens, uint32_t nSyms, int maxB, std::vector<uint16_t>& tbl) -> bool {
        uint32_t tblSz = 1u << maxB;
        tbl.assign(tblSz, 0xFFFFu);
        uint32_t cnt[17] = {};
        uint32_t nz = 0;
        for (uint32_t i = 0; i < nSyms; ++i) {
            if (lens[i] > maxB) return false;
            if (lens[i] > 0) { ++cnt[lens[i]]; ++nz; }
        }
        if (nz == 0) return true;
        uint32_t nextC[17] = {};
        uint32_t c = 0;
        for (int b = 1; b <= maxB; ++b) {
            c = (c + cnt[b - 1]) << 1;
            nextC[b] = c;
        }
        if (c + cnt[maxB] > tblSz) return false;
        for (uint32_t s = 0; s < nSyms; ++s) {
            uint8_t l = lens[s];
            if (l == 0) continue;
            uint32_t code = nextC[l]++;
            uint32_t sh = static_cast<uint32_t>(maxB - l);
            uint32_t st = code << sh;
            uint32_t n  = 1u << sh;
            if (st + n > tblSz) return false;
            uint16_t v = static_cast<uint16_t>((static_cast<uint16_t>(l) << 10) | s);
            for (uint32_t k = 0; k < n; ++k) tbl[st + k] = v;
        }
        if (nz == 1) {
            for (uint32_t k = 0; k < tblSz; ++k) if (tbl[k] == 0xFFFFu) tbl[k] = tbl[0];
        }
        return true;
    };

    auto decodeSym = [&](const std::vector<uint16_t>& tbl, int maxB, uint32_t& outSym) -> bool {
        if (validBits < maxB) ensureBits(maxB);
        if (validBits <= 0) return false;
        uint16_t e = tbl[peekBits(maxB)];
        if (e == 0xFFFFu) return false;
        int l = e >> 10;
        if (validBits < l) return false;
        dropBits(l);
        outSym = e & 0x3FFu;
        return true;
    };

    auto readLzxTreeLengths = [&](uint8_t* lens, uint32_t startSym, uint32_t count) -> bool {
        uint8_t preLens[20] = {};
        for (int i = 0; i < 20; ++i) {
            uint32_t v = 0;
            if (!readBits(4, v)) return false;
            preLens[i] = static_cast<uint8_t>(v);
        }
        std::vector<uint16_t> preTbl;
        if (!buildHuff(preLens, 20, 16, preTbl)) return false;

        uint32_t i = startSym;
        uint32_t endSym = startSym + count;
        while (i < endSym) {
            uint32_t z = 0;
            if (!decodeSym(preTbl, 16, z)) return false;
            if (z == 17u) {
                uint32_t run = 0;
                if (!readBits(4, run)) return false;
                run += 4u;
                for (uint32_t k = 0; k < run && i < endSym; ++k) lens[i++] = 0;
            } else if (z == 18u) {
                uint32_t run = 0;
                if (!readBits(5, run)) return false;
                run += 20u;
                for (uint32_t k = 0; k < run && i < endSym; ++k) lens[i++] = 0;
            } else if (z == 19u) {
                uint32_t run = 0, sym2 = 0;
                if (!readBits(1, run) || !decodeSym(preTbl, 16, sym2) || sym2 > 16u) return false;
                run += 4u;
                uint8_t val = static_cast<uint8_t>((17 + static_cast<int>(lens[i]) - static_cast<int>(sym2)) % 17);
                for (uint32_t k = 0; k < run && i < endSym; ++k) lens[i++] = val;
            } else {
                lens[i] = static_cast<uint8_t>((17 + static_cast<int>(lens[i]) - static_cast<int>(z)) % 17);
                ++i;
            }
        }
        return true;
    };

    // WIM LZX chunk header: 1-bit E8 translation flag (if 1, followed by 32-bit filesize)
    uint32_t e8Flag = 0, e8FileSize = 0;
    if (!readBits(1, e8Flag)) return false;
    if (e8Flag != 0) {
        uint32_t hi = 0, lo = 0;
        if (!readBits(16, hi) || !readBits(16, lo)) return false;
        e8FileSize = (hi << 16) | lo;
    }
    (void)e8FileSize;

    std::vector<uint8_t> mainLens(mainTreeSyms, 0), lenLens(249, 0);
    uint8_t alignLens[8] = {};
    std::vector<uint16_t> mainTbl, lenTbl, alignTbl;
    uint32_t R0 = 1, R1 = 1, R2 = 1;
    size_t outPos = 0;

    while (outPos < dstLen) {
        uint32_t blkType = 0, szBit = 0, blkLen = 0;
        if (!readBits(3, blkType) || blkType == 0 || blkType > 3) return false;
        if (!readBits(1, szBit)) return false;
        if (szBit != 0) {
            blkLen = 32768u; // WIM LZX compact 1-bit size indicator when chunkSize <= 32KB
        } else {
            //Wait: check standard 24-bit vs WIM 16-bit block size: in WIM LZX (chunkSize=32768), if bit==1 blockSize=32768, else 16-bit blockSize!
            if (windowSize <= 32768u) {
                if (!readBits(16, blkLen)) return false;
            } else {
                uint32_t bHi = 0, bLo = 0;
                if (!readBits(8, bHi) || !readBits(16, bLo)) return false;
                blkLen = (bHi << 16) | bLo;
            }
        }
        if (blkLen == 0) break;
        size_t blkEnd = std::min<size_t>(dstLen, outPos + blkLen);

        if (blkType == 3u) {
            // Uncompressed LZX block: align bitstream to 16-bit boundary, read R0, R1, R2 (3 x u32 LE)
            int rem = validBits & 15;
            if (rem > 0) dropBits(rem);
            // Put back buffered 16-bit words or read from bitBuf/src
            auto readRawU32 = [&](uint32_t& v) -> bool {
                uint8_t b[4] = {};
                for (int k = 0; k < 4; ++k) {
                    if (validBits >= 8) {
                        // Note: 16-bit words were loaded LE into MSB, so low byte of original word is bits 7..0 of the 16-bit word!
                    }
                    (void)b;
                }
                // Rewind inPos by buffered whole 16-bit words
                return false;
            };
            (void)readRawU32;
            size_t bufferedWords = static_cast<size_t>(validBits / 16);
            if (bufferedWords * 2 <= inPos) {
                inPos -= bufferedWords * 2;
                bitBuf = 0;
                validBits = 0;
            }
            if (inPos + 12 > srcLen) return false;
            R0 = RdU32LE(src + inPos + 0);
            R1 = RdU32LE(src + inPos + 4);
            R2 = RdU32LE(src + inPos + 8);
            inPos += 12;
            size_t rawCopy = blkEnd - outPos;
            if (inPos + rawCopy > srcLen) return false;
            std::memcpy(dst + outPos, src + inPos, rawCopy);
            inPos += rawCopy;
            if ((rawCopy & 1u) != 0 && inPos < srcLen) ++inPos;
            outPos = blkEnd;
            continue;
        }

        if (blkType == 2u) {
            for (int i = 0; i < 8; ++i) {
                uint32_t v = 0;
                if (!readBits(3, v)) return false;
                alignLens[i] = static_cast<uint8_t>(v);
            }
            if (!buildHuff(alignLens, 8, 8, alignTbl)) return false;
        }

        if (!readLzxTreeLengths(mainLens.data(), 0, 256) ||
            !readLzxTreeLengths(mainLens.data(), 256, mainTreeSyms - 256) ||
            !readLzxTreeLengths(lenLens.data(), 0, 249))
        {
            return false;
        }
        if (!buildHuff(mainLens.data(), mainTreeSyms, 16, mainTbl) ||
            !buildHuff(lenLens.data(), 249, 16, lenTbl))
        {
            return false;
        }

        while (outPos < blkEnd) {
            uint32_t mainSym = 0;
            if (!decodeSym(mainTbl, 16, mainSym)) return false;
            if (mainSym < 256u) {
                dst[outPos++] = static_cast<uint8_t>(mainSym);
            } else {
                uint32_t m = mainSym - 256u;
                uint32_t posSlot = m >> 3;
                uint32_t lenHdr  = m & 7u;
                size_t matchLen  = 0;
                if (lenHdr == 7u) {
                    uint32_t lSym = 0;
                    if (!decodeSym(lenTbl, 16, lSym)) return false;
                    matchLen = static_cast<size_t>(lSym) + 9u;
                } else {
                    matchLen = static_cast<size_t>(lenHdr) + 2u;
                }

                uint32_t matchOff = 0;
                if (posSlot == 0) {
                    matchOff = R0;
                } else if (posSlot == 1) {
                    matchOff = R1; R1 = R0; R0 = matchOff;
                } else if (posSlot == 2) {
                    matchOff = R2; R2 = R0; R0 = matchOff;
                } else {
                    if (posSlot >= 51u) return false;
                    int eb = kExtraBits[posSlot];
                    uint32_t formattedOff = kPosBase[posSlot];
                    if (blkType == 2u && eb >= 3) {
                        uint32_t vHi = 0, al = 0;
                        if (eb > 3 && !readBits(eb - 3, vHi)) return false;
                        if (!decodeSym(alignTbl, 8, al)) return false;
                        formattedOff += (vHi << 3) + al;
                    } else if (eb > 0) {
                        uint32_t ex = 0;
                        if (eb > 16) {
                            uint32_t eHi = 0, eLo = 0;
                            if (!readBits(eb - 16, eHi) || !readBits(16, eLo)) return false;
                            ex = (eHi << 16) | eLo;
                        } else {
                            if (!readBits(eb, ex)) return false;
                        }
                        formattedOff += ex;
                    }
                    if (formattedOff < 2u) return false;
                    matchOff = formattedOff - 2u;
                    R2 = R1; R1 = R0; R0 = matchOff;
                }

                if (matchOff == 0 || matchOff > outPos || matchLen > dstLen - outPos) return false;
                for (size_t k = 0; k < matchLen; ++k) {
                    dst[outPos + k] = dst[outPos - matchOff + k];
                }
                outPos += matchLen;
            }
        }
    }
    return outPos == dstLen;
}

// [MS-WIM] Chunked Resource Reader (Uncompressed, XPRESS Huffman, or LZX)
inline bool DecompressWimResource(
    const uint8_t* wimData,
    size_t wimSize,
    uint32_t wimHeaderFlags,
    uint32_t wimChunkSize,
    const WimResEntry& res,
    std::vector<uint8_t>& outBuf,
    size_t maxBytes = 256 * 1024 * 1024)
{
    outBuf.clear();
    if (res.wimOffset > wimSize || res.compSize > wimSize - res.wimOffset) return false;

    if ((res.flags & 0x04u) == 0) {
        uint64_t rawLen = (res.origSize > 0 && res.origSize <= res.compSize) ? res.origSize : res.compSize;
        if (rawLen > maxBytes) return false;
        const uint8_t* p = wimData + static_cast<size_t>(res.wimOffset);
        outBuf.assign(p, p + static_cast<size_t>(rawLen));
        return true;
    }

    if (res.origSize == 0) return true;
    if (res.origSize > maxBytes || res.compSize == 0) return false;
    if ((wimHeaderFlags & 0x00080000u) != 0) return false; // LZMS solid .esd requires solid block table

    uint32_t chunkSize = (wimChunkSize >= 512u && wimChunkSize <= 64u * 1024u * 1024u &&
                          (wimChunkSize & (wimChunkSize - 1u)) == 0)
                       ? wimChunkSize
                       : 32768u;
    uint64_t numChunks = (res.origSize + chunkSize - 1ULL) / chunkSize;
    size_t entryBytes  = (res.origSize > 0xFFFFFFFFULL) ? 8u : 4u;
    if (numChunks == 0 || (numChunks - 1ULL) > res.compSize / entryBytes) return false;

    size_t tableBytes = static_cast<size_t>(numChunks - 1ULL) * entryBytes;
    const uint8_t* resBase  = wimData + static_cast<size_t>(res.wimOffset);
    const uint8_t* compBase = resBase + tableBytes;
    uint64_t compPayloadLen = res.compSize - tableBytes;

    outBuf.resize(static_cast<size_t>(res.origSize));
    bool isLzx    = (wimHeaderFlags & 0x00040000u) != 0;
    bool isXpress = (wimHeaderFlags & 0x00020000u) != 0 || !isLzx;

    for (uint64_t c = 0; c < numChunks; ++c) {
        uint64_t cStart = 0;
        if (c > 0) {
            const uint8_t* te = resBase + static_cast<size_t>(c - 1ULL) * entryBytes;
            cStart = (entryBytes == 8u) ? RdU64LE(te) : RdU32LE(te);
        }
        uint64_t cEnd = (c + 1ULL < numChunks)
            ? ((entryBytes == 8u) ? RdU64LE(resBase + static_cast<size_t>(c) * 8u)
                                  : RdU32LE(resBase + static_cast<size_t>(c) * 4u))
            : compPayloadLen;

        if (cEnd < cStart || cEnd > compPayloadLen) {
            outBuf.clear();
            return false;
        }
        size_t compLen = static_cast<size_t>(cEnd - cStart);
        size_t dstOff  = static_cast<size_t>(c * chunkSize);
        size_t dstLen  = (c + 1ULL < numChunks)
            ? static_cast<size_t>(chunkSize)
            : static_cast<size_t>(res.origSize - dstOff);

        if (compLen == dstLen) {
            // Per [MS-WIM], chunks whose compressed size equals target uncompressed size are stored raw!
            std::memcpy(outBuf.data() + dstOff, compBase + static_cast<size_t>(cStart), dstLen);
        } else {
            if (compLen == 0 || compLen > dstLen) {
                outBuf.clear();
                return false;
            }
            const uint8_t* cPtr = compBase + static_cast<size_t>(cStart);
            bool ok = false;
            if (isXpress) {
                ok = DecompressXpressHuffmanChunk(cPtr, compLen, outBuf.data() + dstOff, dstLen);
            }
            if (!ok && isLzx) {
                ok = DecompressWimLzxChunk(cPtr, compLen, outBuf.data() + dstOff, dstLen, chunkSize);
            }
            if (!ok) {
                outBuf.clear();
                return false;
            }
        }
    }
    return true;
}

inline bool WalkOrExtractWimDirEntryTree(
    const uint8_t* wimData,
    size_t wimSize,
    uint32_t wimHeaderFlags,
    uint32_t wimChunkSize,
    const uint8_t* metaBuf,
    size_t metaLen,
    size_t dirOffset,
    const std::wstring& parentPath,
    const std::vector<WimResEntry>& lookupTable,
    int depth,
    std::set<size_t>& visitedOffsets,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm,
    std::vector<uint8_t>* outExtracted,
    bool& outExtractedOk)
{
    if (depth > 20 || dirOffset + 102 > metaLen) return false;
    if (outInfo && outInfo->entries.size() >= 30000) return true;
    if (!visitedOffsets.insert(dirOffset).second) return true;

    size_t pos = dirOffset;
    while (pos + 8 <= metaLen) {
        if (outInfo && outInfo->entries.size() >= 30000) break;
        uint64_t liLength = RdU64LE(metaBuf + pos);
        if (liLength == 0) break; // End of directory list
        if (liLength < 102 || liLength > metaLen - pos) break;

        const uint8_t* de   = metaBuf + pos;
        uint32_t attrs      = RdU32LE(de + 8);
        uint64_t subdirOff  = RdU64LE(de + 16);
        uint64_t lastWrite  = RdU64LE(de + 56);
        const uint8_t* sha1 = de + 64;
        uint16_t fnBytes    = RdU16LE(de + 100);

        std::wstring fname;
        if (fnBytes > 0 && 102u + static_cast<uint64_t>(fnBytes) <= liLength) {
            size_t nChars = fnBytes / 2;
            fname.reserve(nChars);
            for (size_t c = 0; c < nChars; ++c) {
                wchar_t wc = static_cast<wchar_t>(RdU16LE(de + 102 + c * 2));
                if (wc == 0) break;
                if (wc == L'/' || wc == L'\\') wc = L'_';
                fname.push_back(wc);
            }
        }

        bool isDir = (attrs & 0x10u) != 0;
        std::wstring curPath = fname.empty()
            ? parentPath
            : ArchivePath::Normalize(parentPath.empty() ? fname : (parentPath + L"/" + fname));

        if (!fname.empty()) {
            bool zeroHash = true;
            for (int b = 0; b < 20; ++b) {
                if (sha1[b] != 0) { zeroHash = false; break; }
            }
            const WimResEntry* matchedRes = nullptr;
            if (!isDir && !zeroHash) {
                for (const auto& res : lookupTable) {
                    if (std::memcmp(res.sha1, sha1, 20) == 0) {
                        matchedRes = &res;
                        break;
                    }
                }
            }

            if (extractTargetNorm && outExtracted) {
                if (!isDir && PathEqualsCI(curPath, *extractTargetNorm)) {
                    if (zeroHash) {
                        outExtracted->clear();
                        outExtractedOk = true;
                        return true;
                    }
                    if (!matchedRes) return false;
                    outExtractedOk = DecompressWimResource(
                        wimData, wimSize, wimHeaderFlags, wimChunkSize, *matchedRes, *outExtracted);
                    return outExtractedOk;
                }
            } else if (outInfo) {
                ArchiveItemEntry item;
                item.pathInArchive = curPath;
                item.name = ArchivePath::BaseName(curPath);
                item.isDirectory = isDir;
                FileTimeU64ToDos(lastWrite, item.dosDate, item.dosTime);
                if (!isDir && matchedRes) {
                    item.uncompressedSize  = matchedRes->origSize;
                    item.compressedSize    = matchedRes->compSize;
                    item.rawDataOffset     = matchedRes->wimOffset;
                    item.compressionMethod = ((matchedRes->flags & 0x04u) == 0) ? 0 : 1;
                }
                outInfo->entries.push_back(std::move(item));
            }
        }

        if (isDir && subdirOff > 0 && subdirOff < metaLen && subdirOff != dirOffset) {
            WalkOrExtractWimDirEntryTree(
                wimData, wimSize, wimHeaderFlags, wimChunkSize,
                metaBuf, metaLen, static_cast<size_t>(subdirOff), curPath,
                lookupTable, depth + 1, visitedOffsets,
                outInfo, extractTargetNorm, outExtracted, outExtractedOk);
            if (extractTargetNorm && outExtractedOk) return true;
        }

        pos += static_cast<size_t>((liLength + 7ULL) & ~7ULL);
    }
    return true;
}

inline bool ParseOrExtractWindowsWim(
    const uint8_t* data,
    size_t size,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm = nullptr,
    std::vector<uint8_t>* outExtracted = nullptr)
{
    if (size < 208 || std::memcmp(data, "MSWIM\0\0\0", 8) != 0) return false;
    uint32_t cbSize      = RdU32LE(data + 8);
    uint32_t dwVersion   = RdU32LE(data + 12);
    uint32_t dwFlags     = RdU32LE(data + 16);
    uint32_t dwChunkSize = RdU32LE(data + 20);
    uint32_t imgCount    = RdU32LE(data + 44);
    if (cbSize < 120 || cbSize > size || dwVersion == 0) return false;

    if (outInfo) {
        outInfo->formatName = (dwFlags & 0x00080000u) ? L"Windows ESD Image (LZMS)" : L"Windows Image (WIM)";
    }

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
    WimResEntry rhBootMeta  = readResHdr(96);

    // 1. Parse UTF-16LE / UTF-8 XML Manifest (uncompressed or compressed)
    struct WimXmlImage {
        uint32_t index = 1;
        std::wstring name;
    };
    std::vector<WimXmlImage> xmlImages;
    std::vector<uint8_t> xmlBytes;
    if (rhXml.compSize >= 4 && rhXml.wimOffset <= size && rhXml.compSize <= size - rhXml.wimOffset &&
        DecompressWimResource(data, size, dwFlags, dwChunkSize, rhXml, xmlBytes, 32 * 1024 * 1024))
    {
        if (extractTargetNorm && outExtracted && PathEqualsCI(*extractTargetNorm, L"[WIM_Manifest].xml")) {
            *outExtracted = std::move(xmlBytes);
            return true;
        }
        if (outInfo) {
            ArchiveItemEntry xmlEntry;
            xmlEntry.pathInArchive = L"[WIM_Manifest].xml";
            xmlEntry.name = xmlEntry.pathInArchive;
            xmlEntry.uncompressedSize = xmlBytes.size();
            xmlEntry.compressedSize = rhXml.compSize;
            xmlEntry.rawDataOffset = rhXml.wimOffset;
            xmlEntry.compressionMethod = ((rhXml.flags & 0x04u) == 0) ? 0 : 1;
            outInfo->entries.push_back(std::move(xmlEntry));
        }

        const uint8_t* xp = xmlBytes.data();
        size_t xlen = xmlBytes.size();
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
            xmlImages.push_back(std::move(wi));
            pos = (imEnd < wxml.size()) ? (imEnd + 8) : wxml.size();
        }
    }

    // 2. Parse Lookup Table (50-byte RESHDR_DISK_LONG records)
    std::vector<WimResEntry> lookupTable;
    std::vector<WimResEntry> metadataResources;
    std::vector<uint8_t> tblBytes;
    if (rhOffsetTbl.compSize >= 50 &&
        DecompressWimResource(data, size, dwFlags, dwChunkSize, rhOffsetTbl, tblBytes, 128 * 1024 * 1024))
    {
        size_t cnt = tblBytes.size() / 50u;
        for (size_t i = 0; i < cnt && i < 200000; ++i) {
            const uint8_t* tp = tblBytes.data() + i * 50u;
            WimResEntry e;
            uint64_t sf = RdU64LE(tp);
            e.compSize  = sf & 0x00FFFFFFFFFFFFFFULL;
            e.flags     = tp[7];
            e.wimOffset = RdU64LE(tp + 8);
            e.origSize  = RdU64LE(tp + 16);
            std::memcpy(e.sha1, tp + 30, 20);
            if ((e.flags & 0x02u) != 0) metadataResources.push_back(e);
            else lookupTable.push_back(e);
        }
    }
    if (metadataResources.empty() && rhBootMeta.compSize >= 32 && rhBootMeta.wimOffset < size) {
        metadataResources.push_back(rhBootMeta);
    }

    // 3. Decompress and walk each Metadata Resource (SECURITYBLOCK_DISK -> root DIRENTRY tree)
    size_t walkedImages = 0;
    for (size_t m = 0; m < metadataResources.size(); ++m) {
        const auto& mr = metadataResources[m];
        std::vector<uint8_t> metaBytes;
        if (!DecompressWimResource(data, size, dwFlags, dwChunkSize, mr, metaBytes, 128 * 1024 * 1024)) continue;
        if (metaBytes.size() < 110) continue;

        uint32_t secLen = RdU32LE(metaBytes.data());
        size_t rootDirOff = (secLen == 0) ? 8u : static_cast<size_t>((static_cast<uint64_t>(secLen) + 7ULL) & ~7ULL);
        if (rootDirOff + 102 > metaBytes.size()) continue;

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
        bool extractedOk = false;
        size_t before = outInfo ? outInfo->entries.size() : 0;
        WalkOrExtractWimDirEntryTree(
            data, size, dwFlags, dwChunkSize,
            metaBytes.data(), metaBytes.size(), rootDirOff, imgPrefix,
            lookupTable, 0, visited, outInfo, extractTargetNorm, outExtracted, extractedOk);
        if (extractTargetNorm && extractedOk) return true;
        if (outInfo && outInfo->entries.size() > before) ++walkedImages;
    }

    if (extractTargetNorm) return false;
    if (!outInfo) return false;

    // Never emit fake Image_1 placeholder entries when metadata fails to decode!
    if (walkedImages == 0) {
        outInfo->entries.clear();
        outInfo->valid = false;
        return false;
    }

    outInfo->valid = !outInfo->entries.empty();
    return outInfo->valid;
}

inline bool ParseWindowsWim(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return ParseOrExtractWindowsWim(data, size, &outInfo, nullptr, nullptr);
}

inline bool ExtractWimEntryFromMemory(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    return ParseOrExtractWindowsWim(data, size, nullptr, &normTarget, &outBytes);
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
            std::wstring wp = ArchivePath::Normalize(PathUtils::Utf8ToWide(rawName));
            ArchiveItemEntry item;
            item.pathInArchive = wp;
            item.name = ArchivePath::BaseName(wp);
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
inline std::wstring ResolveVpkChunkPath(const std::wstring& dirVpkPath, uint16_t archiveIdx) {
    if (dirVpkPath.empty()) return L"";
    size_t slashPos = dirVpkPath.find_last_of(L"\\/");
    size_t nameStart = (slashPos == std::wstring::npos) ? 0 : (slashPos + 1);
    std::wstring dirPrefix = dirVpkPath.substr(0, nameStart);
    std::wstring fileName  = dirVpkPath.substr(nameStart);

    wchar_t chunkSuffix[24] = {};
    swprintf_s(chunkSuffix, L"_%03u.vpk", static_cast<unsigned>(archiveIdx));

    if (fileName.size() >= 8 &&
        _wcsicmp(fileName.c_str() + fileName.size() - 8, L"_dir.vpk") == 0)
    {
        return dirPrefix + fileName.substr(0, fileName.size() - 8) + chunkSuffix;
    }
    if (fileName.size() >= 4 &&
        _wcsicmp(fileName.c_str() + fileName.size() - 4, L".vpk") == 0)
    {
        return dirPrefix + fileName.substr(0, fileName.size() - 4) + chunkSuffix;
    }
    return L"";
}

inline bool ReadExactDiskSlice(const std::wstring& filePath, uint64_t offset, uint32_t length, uint8_t* dst) {
    if (length == 0) return true;
    HANDLE h = CreateFileW(
        filePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER fsz = {};
    if (!GetFileSizeEx(h, &fsz) || fsz.QuadPart < 0 ||
        offset > static_cast<uint64_t>(fsz.QuadPart) ||
        length > static_cast<uint64_t>(fsz.QuadPart) - offset)
    {
        CloseHandle(h);
        return false;
    }

    LARGE_INTEGER seekPos = {};
    seekPos.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(h, seekPos, nullptr, FILE_BEGIN)) {
        CloseHandle(h);
        return false;
    }

    uint32_t remaining = length;
    size_t written = 0;
    while (remaining > 0) {
        DWORD toRead = static_cast<DWORD>(std::min<uint32_t>(remaining, 16u * 1024u * 1024u));
        DWORD got = 0;
        if (!ReadFile(h, dst + written, toRead, &got, nullptr) || got != toRead) {
            CloseHandle(h);
            return false;
        }
        written   += got;
        remaining -= got;
    }

    CloseHandle(h);
    return true;
}

inline bool ParseOrExtractValveVpk(
    const uint8_t* data,
    size_t size,
    const std::wstring* vpkDiskPath,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm = nullptr,
    std::vector<uint8_t>* outExtracted = nullptr)
{
    if (size < 12 || RdU32LE(data) != 0x55AA1234u) return false;
    uint32_t ver    = RdU32LE(data + 4);
    uint32_t treeSz = RdU32LE(data + 8);
    if (ver != 1u && ver != 2u) return false;
    size_t hdrSz = (ver == 2u) ? 28u : 12u;
    if (treeSz == 0 || size < hdrSz || treeSz > size - hdrSz) return false;

    if (outInfo) {
        outInfo->formatName = L"Valve Pak (VPK v" + std::to_wstring(ver) + L")";
    }

    size_t pos     = hdrSz;
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
    while (pos < treeEnd) {
        if (!readCStr(extStr)) return false;
        if (extStr.empty()) break;

        while (pos < treeEnd) {
            if (!readCStr(pathStr)) return false;
            if (pathStr.empty()) break;

            while (pos < treeEnd) {
                if (!readCStr(fileStr)) return false;
                if (fileStr.empty()) break;

                if (pos + 18 > treeEnd) return false;
                uint32_t crc          = RdU32LE(data + pos + 0);
                uint16_t preloadBytes = RdU16LE(data + pos + 4);
                uint16_t archiveIdx   = RdU16LE(data + pos + 6);
                uint32_t entryOff     = RdU32LE(data + pos + 8);
                uint32_t entryLen     = RdU32LE(data + pos + 12);
                uint16_t terminator   = RdU16LE(data + pos + 16);
                if (terminator != 0xFFFFu) return false;
                pos += 18;

                if (preloadBytes > treeEnd - pos) return false;
                const uint8_t* preloadPtr = data + pos;
                pos += preloadBytes;

                // Strictly validate in-archive payload bounds when archiveIdx == 0x7FFF
                if (archiveIdx == 0x7FFFu && entryLen > 0) {
                    if (entryOff > size - treeEnd || entryLen > (size - treeEnd) - entryOff) {
                        if (outInfo) outInfo->entries.clear();
                        return false;
                    }
                }

                std::string full = (pathStr == " " ? "" : (pathStr + "/")) + fileStr +
                                   (extStr == " " ? "" : ("." + extStr));
                std::wstring wp = ArchivePath::Normalize(PathUtils::Utf8ToWide(full));
                if (wp.empty()) continue;

                uint64_t totalBytes = static_cast<uint64_t>(preloadBytes) + static_cast<uint64_t>(entryLen);

                if (extractTargetNorm && outExtracted) {
                    if (PathEqualsCI(wp, *extractTargetNorm)) {
                        if (totalBytes > 256ULL * 1024 * 1024) return false;
                        outExtracted->resize(static_cast<size_t>(totalBytes));
                        if (preloadBytes > 0) {
                            std::memcpy(outExtracted->data(), preloadPtr, preloadBytes);
                        }
                        if (entryLen > 0) {
                            if (archiveIdx == 0x7FFFu) {
                                std::memcpy(outExtracted->data() + preloadBytes,
                                            data + treeEnd + static_cast<size_t>(entryOff),
                                            entryLen);
                            } else {
                                if (!vpkDiskPath || vpkDiskPath->empty()) {
                                    outExtracted->clear();
                                    return false;
                                }
                                std::wstring chunkPath = ResolveVpkChunkPath(*vpkDiskPath, archiveIdx);
                                if (chunkPath.empty() ||
                                    !ReadExactDiskSlice(chunkPath, entryOff, entryLen, outExtracted->data() + preloadBytes))
                                {
                                    outExtracted->clear();
                                    return false;
                                }
                            }
                        }
                        if (::Explorer2::Checksum::Crc32(outExtracted->data(), outExtracted->size()) != crc) {
                            outExtracted->clear();
                            return false;
                        }
                        return true;
                    }
                } else if (outInfo) {
                    ArchiveItemEntry item;
                    item.pathInArchive    = wp;
                    item.name             = ArchivePath::BaseName(wp);
                    item.crc32            = crc;
                    item.uncompressedSize = totalBytes;
                    item.compressedSize   = totalBytes;
                    item.rawDataOffset    = (archiveIdx == 0x7FFFu)
                        ? ((preloadBytes > 0) ? (pos - preloadBytes) : (treeEnd + entryOff))
                        : entryOff;
                    item.compressionMethod = 1; // Always extract via ParseOrExtractValveVpk (preload + _NNN.vpk + CRC32)
                    outInfo->entries.push_back(std::move(item));
                }
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

inline bool ParseValveVpk(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo, const std::wstring* vpkDiskPath = nullptr) {
    return ParseOrExtractValveVpk(data, size, vpkDiskPath, &outInfo, nullptr, nullptr);
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
                dirPath = ArchivePath::Normalize(PathUtils::Utf8ToWide(ds));
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
            fileNames.push_back(ArchivePath::Normalize(PathUtils::Utf8ToWide(fn)));
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
        std::wstring full = ArchivePath::Normalize(fr.dirPath.empty() ? leaf : (fr.dirPath + L"/" + leaf));
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
            item.name = ArchivePath::BaseName(full);
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
            names.push_back(ArchivePath::Normalize(PathUtils::Utf8ToWide(raw)));
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
                item.name = ArchivePath::BaseName(full);
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
                item.name = ArchivePath::BaseName(full);
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

inline bool ParseVpkOrBsa(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo, const std::wstring* diskPath = nullptr) {
    if (ParseValveVpk(data, size, outInfo, diskPath)) return true;
    if (ParseOrExtractBethesdaBsa(data, size, &outInfo, nullptr, nullptr)) return true;
    if (ParseOrExtractBethesdaBa2(data, size, &outInfo, nullptr, nullptr)) return true;
    return false;
}

// ============================================================================
// 36. Electron App Archive (.asar — Chromium Pickle + Recursive JSON Virtual FS)
// ============================================================================
using AsarJsonNode = ::Explorer2::Json::Value; // ASAR header JSON uses the shared reader


inline bool ParseStrictDecimalU64(std::string_view sv, uint64_t& outVal) {
    if (sv.empty()) return false;
    uint64_t v = 0;
    for (char c : sv) {
        if (c < '0' || c > '9') return false;
        uint32_t d = static_cast<uint32_t>(c - '0');
        if (v > (UINT64_MAX - d) / 10ULL) return false;
        v = v * 10ULL + d;
    }
    outVal = v;
    return true;
}

inline bool ParseOrExtractElectronAsar(
    const uint8_t* data,
    size_t size,
    const std::wstring* asarDiskPath,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm = nullptr,
    std::vector<uint8_t>* outExtracted = nullptr)
{
    if (size < 20 || RdU32LE(data + 0) != 4u) return false;
    uint32_t headerSize = RdU32LE(data + 4);
    uint32_t pickleSz   = RdU32LE(data + 8);
    uint32_t jsonLen    = RdU32LE(data + 12);

    uint64_t payloadBase = 8ULL + static_cast<uint64_t>(headerSize);
    if (headerSize < 8u || payloadBase > size) return false;
    if (pickleSz < 4u + jsonLen || jsonLen < 10u || 16ULL + static_cast<uint64_t>(jsonLen) > payloadBase) return false;
    if (jsonLen > 64u * 1024u * 1024u) return false;

    std::string_view jsonSv(reinterpret_cast<const char*>(data + 16), jsonLen);
    while (!jsonSv.empty() && jsonSv.back() == '\0') jsonSv.remove_suffix(1); // pickle padding
    AsarJsonNode root;
    ::Explorer2::Json::Limits asarLimits;
    asarLimits.maxNodes = 8000000; // big Electron apps list hundreds of thousands of files
    if (!::Explorer2::Json::ParseDocument(jsonSv, root, asarLimits) || !root.IsObject()) return false;

    const AsarJsonNode* rootFiles = root.Find("files");
    if (!rootFiles || !rootFiles->IsObject()) return false;

    if (outInfo) {
        outInfo->formatName = L"Electron ASAR";
    }

    bool extractedOk = false;
    size_t entryCount = 0;

    auto walkFiles = [&](auto& self, const AsarJsonNode& filesObj, const std::wstring& prefixPath, int depth) -> bool {
        if (depth > 48 || !filesObj.IsObject()) return false;
        for (const auto& kv : filesObj.objVal) {
            if (entryCount >= 50000) break;
            const std::string& keyUtf8 = kv.first;
            const AsarJsonNode& node   = kv.second;
            if (keyUtf8.empty() || keyUtf8 == ".") continue;
            if (keyUtf8 == ".." || keyUtf8.find('/') != std::string::npos || keyUtf8.find('\\') != std::string::npos) {
                return false;
            }
            if (!node.IsObject()) return false;

            std::wstring wname = PathUtils::Utf8ToWide(keyUtf8);
            std::wstring fullPath = ArchivePath::Normalize(prefixPath.empty() ? wname : (prefixPath + L"/" + wname));
            if (fullPath.empty()) continue;

            const AsarJsonNode* subFiles = node.Find("files");
            if (subFiles) {
                if (!subFiles->IsObject()) return false;
                if (outInfo) {
                    ArchiveItemEntry dirItem;
                    dirItem.pathInArchive = fullPath;
                    dirItem.name          = ArchivePath::BaseName(fullPath);
                    dirItem.isDirectory   = true;
                    outInfo->entries.push_back(std::move(dirItem));
                }
                ++entryCount;
                if (!self(self, *subFiles, fullPath, depth + 1)) return false;
                continue;
            }

            const AsarJsonNode* linkNode = node.Find("link");
            if (linkNode && linkNode->IsString() && !node.Find("size")) {
                continue; // Symbolic link metadata entry
            }

            const AsarJsonNode* szNode = node.Find("size");
            uint64_t fsize = 0;
            if (!szNode || !szNode->AsUint64(fsize)) return false;

            const AsarJsonNode* unpackedNode = node.Find("unpacked");
            bool isUnpacked = (unpackedNode && unpackedNode->IsBool() && unpackedNode->boolVal);

            uint64_t foff = 0;
            if (!isUnpacked) {
                const AsarJsonNode* offNode = node.Find("offset");
                if (!offNode) return false;
                if (offNode->IsString()) {
                    if (!ParseStrictDecimalU64(offNode->strVal, foff)) return false;
                } else if (offNode->AsUint64(foff)) {
                } else {
                    return false;
                }
                // Strict payload bounds validation: reject corrupt ASAR offsets/sizes!
                if (foff > size - payloadBase || fsize > (size - payloadBase) - foff) {
                    return false;
                }
            }

            if (extractTargetNorm && outExtracted && PathEqualsCI(fullPath, *extractTargetNorm)) {
                if (fsize > 256ULL * 1024 * 1024) return false;
                if (!isUnpacked) {
                    size_t absStart = static_cast<size_t>(payloadBase + foff);
                    outExtracted->assign(data + absStart, data + absStart + static_cast<size_t>(fsize));
                    extractedOk = true;
                } else if (asarDiskPath && !asarDiskPath->empty()) {
                    std::wstring winRel = fullPath;
                    for (wchar_t& ch : winRel) if (ch == L'/') ch = L'\\';
                    std::wstring sidecar = *asarDiskPath + L".unpacked\\" + winRel;
                    extractedOk = PathUtils::ReadFileBytes(sidecar, 256 * 1024 * 1024, *outExtracted) &&
                                  outExtracted->size() == fsize;
                }
            }

            if (outInfo) {
                ArchiveItemEntry item;
                item.pathInArchive     = fullPath;
                item.name              = ArchivePath::BaseName(fullPath);
                item.uncompressedSize  = fsize;
                item.compressedSize    = fsize;
                item.rawDataOffset     = isUnpacked ? 0 : (payloadBase + foff);
                item.compressionMethod = isUnpacked ? 1 : 0;
                outInfo->entries.push_back(std::move(item));
            }
            ++entryCount;
        }
        return true;
    };

    if (!walkFiles(walkFiles, *rootFiles, L"", 0)) {
        if (outInfo) {
            outInfo->entries.clear();
            outInfo->valid = false;
        }
        if (outExtracted) outExtracted->clear();
        return false;
    }

    if (extractTargetNorm) return extractedOk;
    if (outInfo) {
        outInfo->valid = !outInfo->entries.empty();
        return outInfo->valid;
    }
    return false;
}

inline bool ParseElectronAsar(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo, const std::wstring* diskPath = nullptr) {
    return ParseOrExtractElectronAsar(data, size, diskPath, &outInfo, nullptr, nullptr);
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
    if (DetailArc100::ParseVpkOrBsa(data, size, outInfo, &archivePath)) return true;
    if (DetailArc100::ParseElectronAsar(data, size, outInfo, &archivePath)) return true;
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
    // 2. Windows Imaging (.wim / .esd — uncompressed, XPRESS Huffman, or LZX)
    if (size >= 208 && std::memcmp(data, "MSWIM\0\0\0", 8) == 0) {
        return DetailArc100::ExtractWimEntryFromMemory(data, size, normTarget, outBytes);
    }
    // 3. Valve Pak (.vpk v1/v2 — inline 0x7FFF + multi-chunk _NNN.vpk + CRC32 verification)
    if (size >= 12 && DetailArc100::RdU32LE(data) == 0x55AA1234u) {
        return DetailArc100::ParseOrExtractValveVpk(data, size, &archivePath, nullptr, &normTarget, &outBytes);
    }
    // 4. Electron App Archive (.asar — recursive JSON hierarchy + .asar.unpacked sidecar)
    if (extLower == L".asar" || (size >= 20 && DetailArc100::RdU32LE(data) == 4u && data[16] == '{')) {
        if (DetailArc100::ParseOrExtractElectronAsar(data, size, &archivePath, nullptr, &normTarget, &outBytes)) {
            return true;
        }
        if (extLower == L".asar") return false;
    }
    // 5. Bethesda .bsa / .ba2
    if (size >= 36 && std::memcmp(data, "BSA\0", 4) == 0) {
        return DetailArc100::ParseOrExtractBethesdaBsa(data, size, nullptr, &normTarget, &outBytes);
    }
    if (size >= 24 && std::memcmp(data, "BTDX", 4) == 0) {
        return DetailArc100::ParseOrExtractBethesdaBa2(data, size, nullptr, &normTarget, &outBytes);
    }
    // 6. RedHat .rpm (Metadata + compressed CPIO extraction)
    if (size >= 112 && data[0] == 0xED && data[1] == 0xAB && data[2] == 0xEE && data[3] == 0xDB) {
        return DetailArc100::ParseOrExtractRedHatRpm(data, size, nullptr, &normTarget, &outBytes);
    }
    // 7. Windows Installer .msi / .msp (OLE2 streams + embedded MSCF CAB files)
    if (extLower == L".msi" || extLower == L".msp") {
        if (DetailArc100::ExtractOle2ArchiveEntry(data, size, extLower, normTarget, outBytes)) return true;
    }
    // 8. ISO-9660 Optical Disc
    if (size >= 0x8800 && data[0x8000] == 1 && std::memcmp(data + 0x8001, "CD001", 5) == 0) {
        ArchiveSummaryInfo dummy;
        if (DetailArc100::ParseIso9660(data, size, dummy, &normTarget, &outBytes)) return true;
    }
    // 9. Virtual Disk Images (.vhd, .vhdx, .vmdk, .vdi, .qcow2, .dmg, .hfs, .ext2, .ext3, .ext4)
    if (DetailArc100::InspectOrExtractVirtualDisk(data, size, extLower, nullptr, &normTarget, &outBytes)) {
        return true;
    }
    return false;
}

} // namespace Explorer2::Formats
