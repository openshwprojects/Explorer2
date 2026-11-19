#pragma once
// UnityPackage.h — Unity Asset Package TAR/TAR.GZ GUID-mapped archive reader (.unitypackage).

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailArc200 {

// ============================================================================
// #21. Unity Asset Package (.unitypackage — TAR/TAR.GZ with <GUID>/pathname -> <GUID>/asset)
// ============================================================================
inline bool ParseUnityPackageFromMemory(
    const uint8_t* data, size_t size,
    ArchiveSummaryInfo& outInfo,
    const std::wstring* extractTargetPath = nullptr,
    std::vector<uint8_t>* outExtractedBytes = nullptr)
{
    std::vector<uint8_t> tarStorage;
    const uint8_t* tarBuf = data;
    size_t tarLen = size;
    if (size >= 18 && data[0] == 0x1F && data[1] == 0x8B) {
        if (!ZipDeflate::InflateGzip(data, size, tarStorage, 256 * 1024 * 1024) || tarStorage.size() < 512) {
            return false;
        }
        tarBuf = tarStorage.data();
        tarLen = tarStorage.size();
    }
    if (tarLen < 512) return false;

    struct GuidSlot {
        std::wstring realPath;
        size_t assetOff = 0;
        uint64_t assetSize = 0;
        bool hasAsset = false;
        bool isDir = false;
    };
    std::unordered_map<std::string, GuidSlot> guidMap;

    size_t pos = 0;
    while (pos + 512 <= tarLen) {
        if (tarBuf[pos] == 0) break;
        std::string name(reinterpret_cast<const char*>(tarBuf + pos), strnlen(reinterpret_cast<const char*>(tarBuf + pos), 100));
        uint64_t fsize = std::strtoull(std::string(reinterpret_cast<const char*>(tarBuf + pos + 124), 12).c_str(), nullptr, 8);
        char typeFlag = static_cast<char>(tarBuf[pos + 156]);
        pos += 512;
        if (pos + fsize > tarLen) break;

        // Normalize leading "./"
        if (name.rfind("./", 0) == 0) name.erase(0, 2);
        size_t slash = name.find('/');
        if (slash != std::string::npos && typeFlag != '5') {
            std::string guid = name.substr(0, slash);
            std::string leaf = name.substr(slash + 1);
            if (leaf == "pathname" && fsize > 0 && fsize < 2048) {
                std::string rawP(reinterpret_cast<const char*>(tarBuf + pos), static_cast<size_t>(fsize));
                size_t nl = rawP.find_first_of("\r\n\0", 0, 3);
                if (nl != std::string::npos) rawP.resize(nl);
                guidMap[guid].realPath = ArchivePath::Normalize(PathUtils::Utf8ToWide(rawP));
            } else if (leaf == "asset") {
                auto& slot = guidMap[guid];
                slot.assetOff = pos;
                slot.assetSize = fsize;
                slot.hasAsset = true;
            }
        }
        pos += static_cast<size_t>((fsize + 511ULL) & ~511ULL);
    }

    if (guidMap.empty()) return false;

    if (extractTargetPath && outExtractedBytes) {
        std::wstring want = ArchivePath::Normalize(*extractTargetPath);
        for (const auto& kv : guidMap) {
            if (kv.second.hasAsset && _wcsicmp(kv.second.realPath.c_str(), want.c_str()) == 0) {
                outExtractedBytes->assign(
                    tarBuf + kv.second.assetOff,
                    tarBuf + kv.second.assetOff + static_cast<size_t>(kv.second.assetSize));
                return true;
            }
        }
        return false;
    }

    outInfo.formatName = L"Unity Asset Package (.unitypackage)";
    for (auto& kv : guidMap) {
        if (kv.second.realPath.empty()) continue;
        ArchiveItemEntry item;
        item.pathInArchive = kv.second.realPath;
        item.name = ArchivePath::BaseName(item.pathInArchive);
        item.isDirectory = !kv.second.hasAsset;
        item.uncompressedSize = kv.second.assetSize;
        item.compressedSize = kv.second.assetSize;
        if (tarBuf == data && kv.second.hasAsset) {
            item.rawDataOffset = kv.second.assetOff;
            item.compressionMethod = 0;
        } else {
            item.compressionMethod = 8;
        }
        outInfo.entries.push_back(std::move(item));
    }
    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

} // namespace DetailArc200

inline bool InspectUnityPackage(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    ArchiveSummaryInfo& outInfo)
{
    return (extLower == L".unitypackage") && DetailArc200::ParseUnityPackageFromMemory(data, size, outInfo);
}

inline bool ExtractUnityPackageEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    const std::wstring& want,
    std::vector<uint8_t>& outBytes)
{
    if (extLower == L".unitypackage") {
        ArchiveSummaryInfo dummy;
        if (DetailArc200::ParseUnityPackageFromMemory(data, size, dummy, &want, &outBytes)) {
            return true;
        }
    }
    return false;
}

} // namespace Explorer2::Formats
