#pragma once
// RpgMakerRgss.h — RPG Maker Encrypted Archive v1/v2/v3 reader & extractor (.rgssad, .rgss2a, .rgss3a — "RGSSAD\0").

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailArc200 {

// ============================================================================
// #24. RPG Maker Encrypted Archive (.rgss3a / .rgss2a / .rgssad — "RGSSAD\0")
// ============================================================================
inline void DecryptRgssStream(const uint8_t* src, size_t len, uint32_t subKey, std::vector<uint8_t>& outBytes) {
    outBytes.resize(len);
    uint32_t curKey = subKey;
    for (size_t i = 0; i < len; ++i) {
        outBytes[i] = static_cast<uint8_t>(src[i] ^ ((curKey >> (8u * (i & 3u))) & 0xFFu));
        if ((i & 3u) == 3u) {
            curKey = curKey * 7u + 3u;
        }
    }
}

inline bool ParseOrExtractRpgMakerRgss(
    const uint8_t* data,
    size_t size,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetPath,
    std::vector<uint8_t>* outExtractedBytes)
{
    if (size < 12 || std::memcmp(data, "RGSSAD\0", 7) != 0) return false;
    uint8_t ver = data[7];
    if (ver != 1 && ver != 2 && ver != 3) return false;
    if (outInfo) {
        outInfo->formatName = (ver == 3) ? L"RPG Maker VX Ace Archive (.rgss3a)" : L"RPG Maker Archive (.rgssad)";
    }

    std::wstring wantNorm = extractTargetPath ? ArchivePath::Normalize(*extractTargetPath) : std::wstring{};

    if (ver == 3) {
        uint32_t masterKey = RdU32LE(data + 8) * 9u + 3u;
        size_t pos = 12;
        size_t count = 0;
        while (pos + 16 <= size && count < 30000) {
            uint32_t fOff    = RdU32LE(data + pos + 0) ^ masterKey;
            uint32_t fSize   = RdU32LE(data + pos + 4) ^ masterKey;
            uint32_t subKey  = RdU32LE(data + pos + 8) ^ masterKey;
            uint32_t nameLen = RdU32LE(data + pos + 12) ^ masterKey;
            pos += 16;
            if (fOff == 0) break;
            if (nameLen == 0 || nameLen > 1024 || pos + nameLen > size) break;
            if (static_cast<uint64_t>(fOff) + static_cast<uint64_t>(fSize) > size) break;

            std::string decName(nameLen, '\0');
            for (uint32_t i = 0; i < nameLen; ++i) {
                decName[i] = static_cast<char>(data[pos + i] ^ ((masterKey >> (8u * (i & 3u))) & 0xFFu));
            }
            pos += nameLen;

            std::wstring normP = ArchivePath::Normalize(PathUtils::Utf8ToWide(decName));
            if (normP.empty()) continue;
            ++count;

            if (extractTargetPath && outExtractedBytes) {
                if (_wcsicmp(normP.c_str(), wantNorm.c_str()) == 0) {
                    if (fSize > 256 * 1024 * 1024) return false;
                    DecryptRgssStream(data + fOff, fSize, subKey, *outExtractedBytes);
                    return true;
                }
                continue;
            }

            if (outInfo) {
                ArchiveItemEntry item;
                item.pathInArchive = std::move(normP);
                item.name = ArchivePath::BaseName(item.pathInArchive);
                item.uncompressedSize = fSize;
                item.compressedSize = fSize;
                item.rawDataOffset = fOff;
                item.compressionMethod = 99; // XOR stream cipher
                outInfo->entries.push_back(std::move(item));
            }
        }
    } else {
        uint32_t key = 0xDEADCAFEu;
        size_t pos = 8;
        size_t count = 0;
        while (pos + 4 <= size && count < 30000) {
            uint32_t nameLen = RdU32LE(data + pos) ^ key;
            key = key * 7u + 3u;
            pos += 4;
            if (nameLen == 0 || nameLen > 1024 || pos + nameLen + 4 > size) break;
            std::string decName(nameLen, '\0');
            for (uint32_t i = 0; i < nameLen; ++i) {
                decName[i] = static_cast<char>(data[pos + i] ^ (key & 0xFFu));
                key = key * 7u + 3u;
            }
            pos += nameLen;
            uint32_t fSize = RdU32LE(data + pos) ^ key;
            key = key * 7u + 3u;
            pos += 4;
            if (pos + fSize > size) break;

            uint32_t subKey = key; // Initial per-file stream key in RGSS v1/v2
            std::wstring normP = ArchivePath::Normalize(PathUtils::Utf8ToWide(decName));
            if (!normP.empty()) {
                ++count;
                if (extractTargetPath && outExtractedBytes) {
                    if (_wcsicmp(normP.c_str(), wantNorm.c_str()) == 0) {
                        if (fSize > 256 * 1024 * 1024) return false;
                        DecryptRgssStream(data + pos, fSize, subKey, *outExtractedBytes);
                        return true;
                    }
                } else if (outInfo) {
                    ArchiveItemEntry item;
                    item.pathInArchive = std::move(normP);
                    item.name = ArchivePath::BaseName(item.pathInArchive);
                    item.uncompressedSize = fSize;
                    item.compressedSize = fSize;
                    item.rawDataOffset = pos;
                    item.compressionMethod = 99;
                    outInfo->entries.push_back(std::move(item));
                }
            }
            pos += fSize;
        }
    }

    if (extractTargetPath) return false;
    if (!outInfo || outInfo->entries.empty()) return false;
    outInfo->valid = true;
    return true;
}

inline bool ParseRpgMakerRgss(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return ParseOrExtractRpgMakerRgss(data, size, &outInfo, nullptr, nullptr);
}

} // namespace DetailArc200

inline bool InspectRpgMakerRgss(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return DetailArc200::ParseRpgMakerRgss(data, size, outInfo);
}

inline bool ExtractRpgMakerRgssEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& want,
    std::vector<uint8_t>& outBytes)
{
    return DetailArc200::ParseOrExtractRpgMakerRgss(data, size, nullptr, &want, &outBytes);
}

} // namespace Explorer2::Formats
