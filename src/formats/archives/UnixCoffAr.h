#pragma once
// UnixCoffAr.h — Unix / BSD / GNU / COFF static library archive reader & extractor (.ar, .a, .lib — "!<arch>\n").
// Provides the single shared "!<arch>\n" member enumerator reused by DebianDeb.h.

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailUnixAndLegacyArc {

struct ParsedUnixArMember {
    std::string name;
    std::string nameLower;
    size_t payloadOffset = 0;
    size_t payloadSize = 0;
    uint32_t mtime = 0;
};

inline std::string DecodeUnixArMemberName(
    const uint8_t* hdr60,
    const uint8_t* memberData,
    uint64_t memberSz,
    const std::string& gnuLongNames,
    size_t& outPayloadAdvance)
{
    outPayloadAdvance = 0;
    std::string rawName(reinterpret_cast<const char*>(hdr60), 16);
    while (!rawName.empty() && rawName.back() == ' ') rawName.pop_back();
    if (rawName.empty() || rawName == "/" || rawName == "//" || rawName == "/SYM64/") {
        return rawName;
    }
    if (rawName.size() > 1 && rawName[0] == '/' && rawName[1] >= '0' && rawName[1] <= '9') {
        size_t strOff = static_cast<size_t>(std::strtoull(rawName.c_str() + 1, nullptr, 10));
        if (strOff < gnuLongNames.size()) {
            size_t endPos = strOff;
            while (endPos < gnuLongNames.size() &&
                   gnuLongNames[endPos] != '\n' &&
                   gnuLongNames[endPos] != '\r' &&
                   gnuLongNames[endPos] != '\0')
            {
                ++endPos;
            }
            std::string resolved = gnuLongNames.substr(strOff, endPos - strOff);
            if (!resolved.empty() && resolved.back() == '/') resolved.pop_back();
            return resolved;
        }
    }
    if (rawName.rfind("#1/", 0) == 0) {
        uint64_t nLen64 = std::strtoull(rawName.c_str() + 3, nullptr, 10);
        if (nLen64 <= memberSz && nLen64 <= 4096u) {
            size_t nLen = static_cast<size_t>(nLen64);
            size_t actualLen = strnlen(reinterpret_cast<const char*>(memberData), nLen);
            std::string resolved(reinterpret_cast<const char*>(memberData), actualLen);
            while (!resolved.empty() && resolved.back() == ' ') resolved.pop_back();
            outPayloadAdvance = nLen;
            return resolved;
        }
    }
    if (!rawName.empty() && rawName.back() == '/') rawName.pop_back();
    return rawName;
}

inline bool EnumerateUnixArMembers(
    const uint8_t* data,
    size_t size,
    std::vector<ParsedUnixArMember>& outMembers,
    size_t maxMembers = 50000)
{
    outMembers.clear();
    if (!data || size < 68 || std::memcmp(data, "!<arch>\n", 8) != 0) return false;
    if (data[8 + 58] != 0x60 || data[8 + 59] != 0x0A) return false;

    std::string gnuLongNames;
    size_t pos = 8;
    while (pos <= size && size - pos >= 60u && outMembers.size() < maxMembers) {
        if (data[pos + 58] != 0x60 || data[pos + 59] != 0x0A) break;
        const uint8_t* hdr = data + pos;
        std::string dateField(reinterpret_cast<const char*>(hdr + 16), 12);
        std::string sizeField(reinterpret_cast<const char*>(hdr + 48), 10);
        const uint32_t mtime = static_cast<uint32_t>(std::strtoull(dateField.c_str(), nullptr, 10));
        const uint64_t memberSz = std::strtoull(sizeField.c_str(), nullptr, 10);
        pos += 60u;
        if (memberSz > size - pos) break;

        std::string rawTag(reinterpret_cast<const char*>(hdr), 16);
        while (!rawTag.empty() && rawTag.back() == ' ') rawTag.pop_back();
        if (rawTag == "//") {
            gnuLongNames.assign(reinterpret_cast<const char*>(data + pos), static_cast<size_t>(memberSz));
        } else if (rawTag != "/" && rawTag != "/SYM64/" &&
                   rawTag != "__.SYMDEF" && rawTag != "__.SYMDEF SORTED" &&
                   rawTag != "__.SYMDEF_64" && rawTag != "__.SYMDEF_64 SORTED")
        {
            size_t adv = 0;
            std::string cleanName = DecodeUnixArMemberName(hdr, data + pos, memberSz, gnuLongNames, adv);
            if (!cleanName.empty() &&
                cleanName != "/" && cleanName != "//" && cleanName != "/SYM64/" &&
                cleanName.rfind("__.SYMDEF", 0) != 0)
            {
                std::string low = PathUtils::ToLowerAscii(cleanName);
                outMembers.push_back({
                    std::move(cleanName),
                    std::move(low),
                    pos + adv,
                    static_cast<size_t>(memberSz) - adv,
                    mtime
                });
            }
        }

        const uint64_t step = memberSz + (memberSz & 1ULL);
        if (step > size - pos) break;
        pos += static_cast<size_t>(step);
    }
    return !outMembers.empty();
}

inline bool HasDebianBinaryFirstMember(const uint8_t* data, size_t size) {
    if (!data || size < 68 || std::memcmp(data, "!<arch>\n", 8) != 0) return false;
    if (data[8 + 58] != 0x60 || data[8 + 59] != 0x0A) return false;
    std::string sizeField(reinterpret_cast<const char*>(data + 8 + 48), 10);
    uint64_t firstSz = std::strtoull(sizeField.c_str(), nullptr, 10);
    if (firstSz > size - 68) return false;
    size_t adv = 0;
    std::string emptyTbl;
    return DecodeUnixArMemberName(data + 8, data + 68, firstSz, emptyTbl, adv) == "debian-binary";
}

inline bool IsUnixArButNotDeb(const uint8_t* data, size_t size, const std::wstring& extLower) {
    if (!data || size < 68 || std::memcmp(data, "!<arch>\n", 8) != 0) return false;
    if (data[8 + 58] != 0x60 || data[8 + 59] != 0x0A) return false;
    if (extLower == L".deb" || extLower == L".udeb") return false;
    return !HasDebianBinaryFirstMember(data, size);
}

inline bool ParseUnixCoffAr(const uint8_t* data, size_t size, const std::wstring& extLower, ArchiveSummaryInfo& outInfo) {
    if (!IsUnixArButNotDeb(data, size, extLower)) return false;
    std::vector<ParsedUnixArMember> members;
    if (!EnumerateUnixArMembers(data, size, members)) return false;

    outInfo.formatName = (extLower == L".lib") ? L"COFF Static Library (.lib)" : L"Unix Static Archive (.ar/.a)";
    outInfo.entries.reserve(members.size());
    for (const auto& m : members) {
        ArchiveItemEntry item;
        item.pathInArchive = ArchivePath::Normalize(PathUtils::Utf8ToWide(m.name));
        if (item.pathInArchive.empty()) continue;
        item.name = ArchivePath::BaseName(item.pathInArchive);
        item.uncompressedSize = m.payloadSize;
        item.compressedSize = m.payloadSize;
        item.rawDataOffset = m.payloadOffset;
        item.compressionMethod = 0;
        DetailDiskAndGameArc::UnixTimeU32ToDos(m.mtime, item.dosDate, item.dosTime);
        outInfo.entries.push_back(std::move(item));
    }
    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

} // namespace DetailUnixAndLegacyArc

inline bool InspectUnixCoffAr(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    ArchiveSummaryInfo& outInfo)
{
    return DetailUnixAndLegacyArc::ParseUnixCoffAr(data, size, extLower, outInfo);
}

inline bool ExtractUnixCoffArEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    const std::wstring& want,
    std::vector<uint8_t>& outBytes)
{
    if (DetailUnixAndLegacyArc::IsUnixArButNotDeb(data, size, extLower)) {
        ArchiveSummaryInfo info;
        if (DetailUnixAndLegacyArc::ParseUnixCoffAr(data, size, extLower, info)) {
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
    }
    return false;
}

} // namespace Explorer2::Formats
