#pragma once
// CabinetMscf.h — Microsoft Cabinet archive reader & extractor (.cab — "MSCF").

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailDiskAndGameArc {

inline std::wstring DecodeCabAnsiCp1252(std::string_view raw) {
    if (raw.empty()) return {};
#ifdef _WIN32
    int wlen = MultiByteToWideChar(1252u, 0, raw.data(), static_cast<int>(raw.size()), nullptr, 0);
    if (wlen <= 0) {
        wlen = MultiByteToWideChar(CP_ACP, 0, raw.data(), static_cast<int>(raw.size()), nullptr, 0);
        if (wlen > 0) {
            std::wstring w(static_cast<size_t>(wlen), L'\0');
            MultiByteToWideChar(CP_ACP, 0, raw.data(), static_cast<int>(raw.size()), w.data(), wlen);
            return w;
        }
    } else {
        std::wstring w(static_cast<size_t>(wlen), L'\0');
        MultiByteToWideChar(1252u, 0, raw.data(), static_cast<int>(raw.size()), w.data(), wlen);
        return w;
    }
#endif
    static constexpr uint16_t kCp1252High[32] = {
        0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
        0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
        0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
        0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
    };
    std::wstring out;
    out.reserve(raw.size());
    for (unsigned char b : raw) {
        if (b >= 0x80u && b <= 0x9Fu) {
            out.push_back(static_cast<wchar_t>(kCp1252High[b - 0x80u]));
        } else {
            out.push_back(static_cast<wchar_t>(b));
        }
    }
    return out;
}

inline bool ParseCabinetMscf(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo, const std::wstring& prefix = L"") {
    if (!data || size < 36 || std::memcmp(data, "MSCF\0\0\0\0", 8) != 0) return false;
    if (prefix.empty()) outInfo.formatName = L"MS Cabinet (CAB)";
    uint32_t cbCabinet = RdU32LE(data + 8);
    uint32_t coffFiles = RdU32LE(data + 16);
    uint16_t cFiles    = RdU16LE(data + 28);
    if (coffFiles >= size || cFiles == 0) return false;

    uint64_t totalUnp = 0;
    size_t scanPos = coffFiles;
    for (uint16_t i = 0; i < cFiles && size - scanPos > 16u; ++i) {
        totalUnp += RdU32LE(data + scanPos);
        scanPos += 16;
        while (scanPos < size && data[scanPos] != 0) ++scanPos;
        if (scanPos >= size) break;
        ++scanPos;
    }
    uint64_t payloadPool = (cbCabinet > coffFiles && cbCabinet <= size) ? (cbCabinet - coffFiles) : (size - coffFiles);

    size_t pos = coffFiles;
    size_t added = 0;
    for (uint16_t i = 0; i < cFiles && size - pos > 16u; ++i) {
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

        // _A_NAME_IS_UTF (0x80): UTF-8 when set; ANSI / CP1252 when clear
        std::wstring decodedName = (attribs & 0x80u)
            ? PathUtils::Utf8ToWide(rawName)
            : DecodeCabAnsiCp1252(rawName);
        std::wstring wpath = ArchivePath::Normalize(decodedName);
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
            item.compressionMethod = 1;
            outInfo.entries.push_back(std::move(item));
            ++added;
        }
    }
    if (prefix.empty()) outInfo.valid = (added > 0);
    return added > 0;
}

} // namespace DetailDiskAndGameArc

inline bool InspectCabinetMscf(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return DetailDiskAndGameArc::ParseCabinetMscf(data, size, outInfo);
}

inline bool ExtractCabinetMscfEntry(
    const std::wstring& archivePath,
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    if (data && size >= 36 && std::memcmp(data, "MSCF\0\0\0\0", 8) == 0) {
        if (StreamCodecs::ExtractCabEntryFromMemory(data, size, normTarget, outBytes)) {
            return true;
        }
        if (!archivePath.empty()) {
            return StreamCodecs::ExtractCabEntryFromFile(archivePath, normTarget, outBytes);
        }
    }
    return false;
}

} // namespace Explorer2::Formats
