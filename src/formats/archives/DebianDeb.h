#pragma once
// DebianDeb.h — Debian binary package reader & extractor (.deb, .udeb).

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailArc100 {

// ============================================================================
// 26. Debian Software Package (.deb — Unix "!<arch>\n" container + data.tar.*)
// ============================================================================
inline std::string DecodeArMemberName(
    const uint8_t* hdr60,
    const uint8_t* memberData,
    uint64_t memberSz,
    const std::string& gnuLongNames,
    size_t& ioPayloadAdvance)
{
    ioPayloadAdvance = 0;
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
            ioPayloadAdvance = nLen;
            return resolved;
        }
    }
    if (!rawName.empty() && rawName.back() == '/') rawName.pop_back();
    return rawName;
}

inline bool IsDebianDebArchive(const uint8_t* data, size_t size, const std::wstring& extLower) {
    if (!data || size < 68 || std::memcmp(data, "!<arch>\n", 8) != 0) return false;
    if (data[8 + 58] != 0x60 || data[8 + 59] != 0x0A) return false;
    if (extLower == L".deb" || extLower == L".udeb") return true;

    std::string sizeField(reinterpret_cast<const char*>(data + 8 + 48), 10);
    uint64_t firstSz = std::strtoull(sizeField.c_str(), nullptr, 10);
    if (firstSz > size - 68) return false;
    size_t adv = 0;
    std::string emptyTbl;
    std::string firstName = DecodeArMemberName(data + 8, data + 68, firstSz, emptyTbl, adv);
    return firstName == "debian-binary";
}

inline bool DecompressDebTarMember(
    const uint8_t* payload,
    size_t payloadLen,
    const std::string& nameLower,
    std::vector<uint8_t>& outTarBuf,
    const uint8_t*& outTarPtr,
    size_t& outTarLen,
    bool& outIsUncompressed)
{
    outTarBuf.clear();
    outTarPtr = nullptr;
    outTarLen = 0;
    outIsUncompressed = false;
    if (!payload || payloadLen == 0) return false;

    constexpr size_t kMaxDebTarBytes = 128 * 1024 * 1024;
    auto endsWith = [&](const char* suf) -> bool {
        size_t sl = std::strlen(suf);
        return nameLower.size() >= sl && nameLower.compare(nameLower.size() - sl, sl, suf) == 0;
    };

    if (nameLower == "data.tar" ||
        (payloadLen >= 512 && std::memcmp(payload + 257, "ustar", 5) == 0))
    {
        outTarPtr = payload;
        outTarLen = payloadLen;
        outIsUncompressed = true;
        return true;
    }
    if (endsWith(".gz") || endsWith(".tgz") ||
        (payloadLen >= 18 && payload[0] == 0x1F && payload[1] == 0x8B))
    {
        if (!ZipDeflate::InflateGzip(payload, payloadLen, outTarBuf, kMaxDebTarBytes)) return false;
        outTarPtr = outTarBuf.data();
        outTarLen = outTarBuf.size();
        return outTarLen >= 512;
    }
    if (endsWith(".xz") ||
        (payloadLen >= 6 && std::memcmp(payload, "\xFD" "7zXZ\0", 6) == 0))
    {
        if (!StreamCodecs::DecompressXzStream(payload, payloadLen, outTarBuf, kMaxDebTarBytes)) return false;
        outTarPtr = outTarBuf.data();
        outTarLen = outTarBuf.size();
        return outTarLen >= 512;
    }
    if (endsWith(".zst") ||
        (payloadLen >= 4 && RdU32LE(payload) == 0xFD2FB528u))
    {
        if (!StreamCodecs::DecompressZstdFrame(payload, payloadLen, outTarBuf, kMaxDebTarBytes)) return false;
        outTarPtr = outTarBuf.data();
        outTarLen = outTarBuf.size();
        return outTarLen >= 512;
    }
    if (endsWith(".bz2") ||
        (payloadLen >= 4 && std::memcmp(payload, "BZh", 3) == 0))
    {
        if (!StreamCodecs::DecompressBzip2Stream(payload, payloadLen, outTarBuf, kMaxDebTarBytes)) return false;
        outTarPtr = outTarBuf.data();
        outTarLen = outTarBuf.size();
        return outTarLen >= 512;
    }
    if (endsWith(".lz4") ||
        (payloadLen >= 4 && RdU32LE(payload) == 0x184D2204u))
    {
        if (!StreamCodecs::DecompressLz4Frame(payload, payloadLen, outTarBuf, kMaxDebTarBytes)) return false;
        outTarPtr = outTarBuf.data();
        outTarLen = outTarBuf.size();
        return outTarLen >= 512;
    }
    if (endsWith(".lzma") && payloadLen >= 14) {
        uint64_t unpSz = RdU64LE(payload + 5);
        bool known = (unpSz != UINT64_MAX);
        if (!StreamCodecs::DecompressLzma1(payload[0], RdU32LE(payload + 1), payload + 13, payloadLen - 13,
                                           outTarBuf, unpSz, known, kMaxDebTarBytes))
        {
            return false;
        }
        outTarPtr = outTarBuf.data();
        outTarLen = outTarBuf.size();
        return outTarLen >= 512;
    }
    if (endsWith(".br")) {
        if (!StreamCodecs::DecompressBrotliStream(payload, payloadLen, outTarBuf, kMaxDebTarBytes, false)) return false;
        outTarPtr = outTarBuf.data();
        outTarLen = outTarBuf.size();
        return outTarLen >= 512;
    }
    return false;
}

inline bool ParseOrExtractDebianDeb(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm = nullptr,
    std::vector<uint8_t>* outExtracted = nullptr)
{
    if (!IsDebianDebArchive(data, size, extLower)) return false;

    struct OuterMember {
        std::string name;
        std::string nameLower;
        size_t offset = 0;
        size_t length = 0;
    };
    std::vector<OuterMember> outerMembers;
    std::string gnuLongNames;

    size_t pos = 8;
    while (pos + 60 <= size && outerMembers.size() < 4096) {
        if (data[pos + 58] != 0x60 || data[pos + 59] != 0x0A) break;
        std::string sizeField(reinterpret_cast<const char*>(data + pos + 48), 10);
        uint64_t memberSz = std::strtoull(sizeField.c_str(), nullptr, 10);
        const uint8_t* hdr = data + pos;
        pos += 60;
        if (memberSz > size - pos) return false;

        std::string rawTag(reinterpret_cast<const char*>(hdr), 16);
        while (!rawTag.empty() && rawTag.back() == ' ') rawTag.pop_back();
        if (rawTag == "//") {
            gnuLongNames.assign(reinterpret_cast<const char*>(data + pos), static_cast<size_t>(memberSz));
        } else {
            size_t adv = 0;
            std::string cleanName = DecodeArMemberName(hdr, data + pos, memberSz, gnuLongNames, adv);
            if (!cleanName.empty() && cleanName != "/" && cleanName != "//" && cleanName != "/SYM64/" &&
                cleanName.rfind("__.SYMDEF", 0) != 0)
            {
                std::string low = cleanName;
                for (char& ch : low) {
                    if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
                }
                outerMembers.push_back({
                    std::move(cleanName),
                    std::move(low),
                    pos + adv,
                    static_cast<size_t>(memberSz) - adv
                });
            }
        }
        uint64_t step = memberSz + (memberSz & 1ULL);
        if (step > size - pos) {
            pos = size;
            break;
        }
        pos += static_cast<size_t>(step);
    }

    if (outerMembers.empty()) return false;

    const OuterMember* dataTarMember = nullptr;
    for (const auto& m : outerMembers) {
        if (m.nameLower == "data.tar" || m.nameLower.rfind("data.tar.", 0) == 0) {
            dataTarMember = &m;
            break;
        }
    }

    if (dataTarMember) {
        std::vector<uint8_t> tarStorage;
        const uint8_t* tarPtr = nullptr;
        size_t tarLen = 0;
        bool isPlainTar = false;
        if (!DecompressDebTarMember(data + dataTarMember->offset, dataTarMember->length,
                                    dataTarMember->nameLower, tarStorage, tarPtr, tarLen, isPlainTar))
        {
            return false;
        }

        if (extractTargetNorm && outExtracted) {
            ArchiveSummaryInfo dummy;
            std::wstring want = ArchivePath::Normalize(*extractTargetNorm);
            return StreamCodecs::ParsePosixTarFromMemory(tarPtr, tarLen, dummy, &want, outExtracted);
        }

        if (!outInfo) return false;
        ArchiveSummaryInfo tarInfo;
        if (!StreamCodecs::ParsePosixTarFromMemory(tarPtr, tarLen, tarInfo) || tarInfo.entries.empty()) {
            return false;
        }
        outInfo->formatName = L"Debian Package (DEB)";
        uint64_t totalUnp = 0;
        for (const auto& e : tarInfo.entries) {
            if (!e.isDirectory) totalUnp += e.uncompressedSize;
        }
        for (auto& e : tarInfo.entries) {
            if (isPlainTar) {
                e.rawDataOffset += dataTarMember->offset;
                e.compressionMethod = 0;
            } else {
                e.compressionMethod = 8;
                if (!e.isDirectory) {
                    e.compressedSize = (totalUnp > 0)
                        ? ((static_cast<uint64_t>(dataTarMember->length) * e.uncompressedSize) / totalUnp)
                        : dataTarMember->length;
                }
            }
        }
        outInfo->entries = std::move(tarInfo.entries);
        outInfo->valid = !outInfo->entries.empty();
        return outInfo->valid;
    }

    // Fallback for uncommon .deb files that have no data.tar* member
    if (extractTargetNorm && outExtracted) {
        std::wstring want = ArchivePath::Normalize(*extractTargetNorm);
        for (const auto& m : outerMembers) {
            std::wstring mp = ArchivePath::Normalize(PathUtils::Utf8ToWide(m.name));
            if (_wcsicmp(mp.c_str(), want.c_str()) == 0) {
                outExtracted->assign(data + m.offset, data + m.offset + m.length);
                return true;
            }
        }
        return false;
    }

    if (!outInfo) return false;
    outInfo->formatName = L"Debian Package (DEB)";
    for (const auto& m : outerMembers) {
        std::wstring mp = ArchivePath::Normalize(PathUtils::Utf8ToWide(m.name));
        if (mp.empty()) continue;
        ArchiveItemEntry item;
        item.pathInArchive = mp;
        item.name = ArchivePath::BaseName(mp);
        item.uncompressedSize = m.length;
        item.compressedSize = m.length;
        item.rawDataOffset = m.offset;
        item.compressionMethod = 0;
        outInfo->entries.push_back(std::move(item));
    }
    outInfo->valid = !outInfo->entries.empty();
    return outInfo->valid;
}

inline bool ParseDebianDeb(const uint8_t* data, size_t size, const std::wstring& extLower, ArchiveSummaryInfo& outInfo) {
    return ParseOrExtractDebianDeb(data, size, extLower, &outInfo, nullptr, nullptr);
}

} // namespace DetailArc100

inline bool InspectDebianDeb(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    ArchiveSummaryInfo& outInfo)
{
    return DetailArc100::ParseDebianDeb(data, size, extLower, outInfo);
}

inline bool ExtractDebianDebEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    return DetailArc100::ParseOrExtractDebianDeb(data, size, extLower, nullptr, &normTarget, &outBytes);
}

} // namespace Explorer2::Formats
