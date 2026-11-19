#pragma once
// ElectronAsar.h — Electron Chromium Pickle + JSON virtual filesystem archive reader (.asar).

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailArc100 {

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

inline bool InspectElectronAsar(
    const uint8_t* data,
    size_t size,
    ArchiveSummaryInfo& outInfo,
    const std::wstring* diskPath = nullptr)
{
    return DetailArc100::ParseElectronAsar(data, size, outInfo, diskPath);
}

inline bool ExtractElectronAsarEntry(
    const std::wstring& archivePath,
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    return DetailArc100::ParseOrExtractElectronAsar(data, size, &archivePath, nullptr, &normTarget, &outBytes);
}

} // namespace Explorer2::Formats
