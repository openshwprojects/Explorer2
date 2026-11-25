#pragma once
// DebianDeb.h — Debian binary package reader & extractor (.deb, .udeb).
// Reuses UnixCoffAr.h's EnumerateUnixArMembers for "!<arch>\n" outer member enumeration.

#include "../common/ArchiveFormatCommon.h"
#include "UnixCoffAr.h"

namespace Explorer2::Formats {

namespace DetailDiskAndGameArc {

inline bool IsDebianDebArchive(const uint8_t* data, size_t size, const std::wstring& extLower) {
    if (!data || size < 68 || std::memcmp(data, "!<arch>\n", 8) != 0) return false;
    if (data[8 + 58] != 0x60 || data[8 + 59] != 0x0A) return false;
    if (extLower == L".deb" || extLower == L".udeb") return true;
    return DetailUnixAndLegacyArc::HasDebianBinaryFirstMember(data, size);
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

    std::vector<DetailUnixAndLegacyArc::ParsedUnixArMember> outerMembers;
    if (!DetailUnixAndLegacyArc::EnumerateUnixArMembers(data, size, outerMembers, 4096)) return false;

    const DetailUnixAndLegacyArc::ParsedUnixArMember* dataTarMember = nullptr;
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
        if (!DecompressDebTarMember(data + dataTarMember->payloadOffset, dataTarMember->payloadSize,
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
                e.rawDataOffset += dataTarMember->payloadOffset;
                e.compressionMethod = 0;
            } else {
                e.compressionMethod = 8;
                if (!e.isDirectory) {
                    e.compressedSize = (totalUnp > 0)
                        ? ((static_cast<uint64_t>(dataTarMember->payloadSize) * e.uncompressedSize) / totalUnp)
                        : dataTarMember->payloadSize;
                }
            }
        }
        outInfo->entries = std::move(tarInfo.entries);
        outInfo->valid = !outInfo->entries.empty();
        return outInfo->valid;
    }

    if (extractTargetNorm && outExtracted) {
        std::wstring want = ArchivePath::Normalize(*extractTargetNorm);
        for (const auto& m : outerMembers) {
            std::wstring mp = ArchivePath::Normalize(PathUtils::Utf8ToWide(m.name));
            if (_wcsicmp(mp.c_str(), want.c_str()) == 0) {
                outExtracted->assign(data + m.payloadOffset, data + m.payloadOffset + m.payloadSize);
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
        item.uncompressedSize = m.payloadSize;
        item.compressedSize = m.payloadSize;
        item.rawDataOffset = m.payloadOffset;
        item.compressionMethod = 0;
        outInfo->entries.push_back(std::move(item));
    }
    outInfo->valid = !outInfo->entries.empty();
    return outInfo->valid;
}

inline bool ParseDebianDeb(const uint8_t* data, size_t size, const std::wstring& extLower, ArchiveSummaryInfo& outInfo) {
    return ParseOrExtractDebianDeb(data, size, extLower, &outInfo, nullptr, nullptr);
}

} // namespace DetailDiskAndGameArc

inline bool InspectDebianDeb(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    ArchiveSummaryInfo& outInfo)
{
    return DetailDiskAndGameArc::ParseDebianDeb(data, size, extLower, outInfo);
}

inline bool ExtractDebianDebEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    return DetailDiskAndGameArc::ParseOrExtractDebianDeb(data, size, extLower, nullptr, &normTarget, &outBytes);
}

} // namespace Explorer2::Formats
