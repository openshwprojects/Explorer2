#pragma once
// UnixCoffAr.h — Unix / COFF static library archive reader & extractor (.ar, .a, .lib — "!<arch>\n").

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailArc200 {

// ============================================================================
// #34. Unix / COFF Static Library Archive (.ar / .a / .lib — "!<arch>\n")
// ============================================================================
inline bool IsUnixArButNotDeb(const uint8_t* data, size_t size, const std::wstring& extLower) {
    if (!data || size < 68 || std::memcmp(data, "!<arch>\n", 8) != 0) return false;
    if (data[8 + 58] != 0x60 || data[8 + 59] != 0x0A) return false;
    if (extLower == L".deb" || extLower == L".udeb") return false;

    std::string rawFirst(reinterpret_cast<const char*>(data + 8), 16);
    while (!rawFirst.empty() && rawFirst.back() == ' ') rawFirst.pop_back();
    if (!rawFirst.empty() && rawFirst.back() == '/') rawFirst.pop_back();
    if (rawFirst == "debian-binary") return false;
    if (rawFirst.rfind("#1/", 0) == 0) {
        std::string szField(reinterpret_cast<const char*>(data + 8 + 48), 10);
        uint64_t fsz = std::strtoull(szField.c_str(), nullptr, 10);
        uint64_t nLen = std::strtoull(rawFirst.c_str() + 3, nullptr, 10);
        if (nLen <= fsz && nLen <= size - 68 && nLen <= 256) {
            std::string bsdFirst(reinterpret_cast<const char*>(data + 68),
                                 strnlen(reinterpret_cast<const char*>(data + 68), static_cast<size_t>(nLen)));
            while (!bsdFirst.empty() && bsdFirst.back() == ' ') bsdFirst.pop_back();
            if (bsdFirst == "debian-binary") return false;
        }
    }
    return true;
}

inline bool ParseUnixCoffAr(const uint8_t* data, size_t size, const std::wstring& extLower, ArchiveSummaryInfo& outInfo) {
    if (!IsUnixArButNotDeb(data, size, extLower)) return false;
    outInfo.formatName = (extLower == L".lib") ? L"COFF Static Library (.lib)" : L"Unix Static Archive (.ar/.a)";

    std::string gnuLongNames;
    size_t pos = 8;
    while (pos + 60 <= size && outInfo.entries.size() < 50000) {
        if (data[pos + 58] != 0x60 || data[pos + 59] != 0x0A) break;
        std::string rawName(reinterpret_cast<const char*>(data + pos), 16);
        while (!rawName.empty() && rawName.back() == ' ') rawName.pop_back();
        std::string sizeStr(reinterpret_cast<const char*>(data + pos + 48), 10);
        uint64_t memSz = static_cast<uint64_t>(std::strtoull(sizeStr.c_str(), nullptr, 10));
        pos += 60;
        if (memSz > size - pos) break;

        size_t payloadOff = pos;
        uint64_t payloadSz = memSz;

        if (rawName == "//") {
            gnuLongNames.assign(reinterpret_cast<const char*>(data + pos), static_cast<size_t>(memSz));
        } else if (rawName != "/" && rawName != "/SYM64/" &&
                   rawName != "__.SYMDEF" && rawName != "__.SYMDEF SORTED" &&
                   rawName != "__.SYMDEF_64" && rawName != "__.SYMDEF_64 SORTED")
        {
            std::string cleanName = rawName;
            if (rawName.size() > 1 && rawName[0] == '/' && rawName[1] >= '0' && rawName[1] <= '9') {
                size_t strOff = static_cast<size_t>(std::strtoull(rawName.c_str() + 1, nullptr, 10));
                if (strOff < gnuLongNames.size()) {
                    size_t endN = strOff;
                    while (endN < gnuLongNames.size() &&
                           gnuLongNames[endN] != '\n' &&
                           gnuLongNames[endN] != '\r' &&
                           gnuLongNames[endN] != '\0')
                    {
                        ++endN;
                    }
                    cleanName = gnuLongNames.substr(strOff, endN - strOff);
                    if (!cleanName.empty() && cleanName.back() == '/') cleanName.pop_back();
                }
            } else if (rawName.rfind("#1/", 0) == 0) { // BSD extended name inline at start of member payload
                uint64_t nLen64 = std::strtoull(rawName.c_str() + 3, nullptr, 10);
                if (nLen64 <= memSz && nLen64 <= 4096u) {
                    size_t nLen = static_cast<size_t>(nLen64);
                    cleanName.assign(reinterpret_cast<const char*>(data + pos),
                                     strnlen(reinterpret_cast<const char*>(data + pos), nLen));
                    while (!cleanName.empty() && cleanName.back() == ' ') cleanName.pop_back();
                    payloadOff += nLen;
                    payloadSz -= nLen;
                }
            } else if (!cleanName.empty() && cleanName.back() == '/') {
                cleanName.pop_back();
            }

            if (!cleanName.empty() &&
                cleanName != "/" && cleanName != "//" && cleanName != "/SYM64/" &&
                cleanName.rfind("__.SYMDEF", 0) != 0)
            {
                ArchiveItemEntry item;
                item.pathInArchive = ArchivePath::Normalize(PathUtils::Utf8ToWide(cleanName));
                if (!item.pathInArchive.empty()) {
                    item.name = ArchivePath::BaseName(item.pathInArchive);
                    item.uncompressedSize = payloadSz;
                    item.compressedSize = payloadSz;
                    item.rawDataOffset = payloadOff;
                    item.compressionMethod = 0; // Stored raw object!
                    outInfo.entries.push_back(std::move(item));
                }
            }
        }
        uint64_t step = memSz + (memSz & 1ULL);
        if (step > size - pos) break;
        pos += static_cast<size_t>(step);
    }
    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

} // namespace DetailArc200

inline bool InspectUnixCoffAr(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    ArchiveSummaryInfo& outInfo)
{
    return DetailArc200::ParseUnixCoffAr(data, size, extLower, outInfo);
}

inline bool ExtractUnixCoffArEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    const std::wstring& want,
    std::vector<uint8_t>& outBytes)
{
    if (DetailArc200::IsUnixArButNotDeb(data, size, extLower)) {
        ArchiveSummaryInfo info;
        if (DetailArc200::ParseUnixCoffAr(data, size, extLower, info)) {
            for (const auto& e : info.entries) {
                if (!e.isDirectory && _wcsicmp(e.pathInArchive.c_str(), want.c_str()) == 0) {
                    if (e.rawDataOffset > size || e.uncompressedSize > size - static_cast<size_t>(e.rawDataOffset)) {
                        return false;
                    }
                    const uint8_t* p = data + static_cast<size_t>(e.rawDataOffset);
                    outBytes.assign(p, p + static_cast<size_t>(e.uncompressedSize));
                    return true;
                }
            }
        }
        return false;
    }
    return false;
}

} // namespace Explorer2::Formats
