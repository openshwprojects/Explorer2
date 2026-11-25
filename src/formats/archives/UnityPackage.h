#pragma once
// UnityPackage.h — Unity Asset Package TAR/TAR.GZ GUID-mapped archive reader (.unitypackage).
// Uses unified StreamCodecs::ParsePosixTarFromMemory to enumerate <GUID>/pathname and <GUID>/asset entries.

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailUnixAndLegacyArc {

inline bool ParseUnityPackageFromMemory(
    const uint8_t* data,
    size_t size,
    ArchiveSummaryInfo& outInfo,
    const std::wstring* extractTargetPath = nullptr,
    std::vector<uint8_t>* outExtractedBytes = nullptr)
{
    if (!data || size < 18) return false;
    std::vector<uint8_t> tarStorage;
    const uint8_t* tarBuf = data;
    size_t tarLen = size;
    if (data[0] == 0x1Fu && data[1] == 0x8Bu && data[2] == 8u) {
        if (!ZipDeflate::InflateGzip(data, size, tarStorage, 256 * 1024 * 1024) || tarStorage.size() < 512) {
            return false;
        }
        tarBuf = tarStorage.data();
        tarLen = tarStorage.size();
    }
    if (tarLen < 512) return false;

    ArchiveSummaryInfo rawTar;
    if (!StreamCodecs::ParsePosixTarFromMemory(tarBuf, tarLen, rawTar) || rawTar.entries.empty()) {
        return false;
    }

    struct GuidSlot {
        std::wstring realPath;
        size_t assetOff = 0;
        uint64_t assetSize = 0;
        bool hasAsset = false;
    };
    std::unordered_map<std::wstring, GuidSlot> guidMap;
    std::vector<std::wstring> guidOrder;

    for (const auto& te : rawTar.entries) {
        if (te.isDirectory) continue;
        if (te.rawDataOffset > tarLen || te.uncompressedSize > tarLen - static_cast<size_t>(te.rawDataOffset)) {
            continue;
        }
        const size_t slash = te.pathInArchive.find(L'/');
        if (slash == std::wstring::npos) continue;
        const std::wstring guid = PathUtils::ToLower(te.pathInArchive.substr(0, slash));
        const std::wstring leaf = te.pathInArchive.substr(slash + 1u);

        if (leaf == L"pathname" && te.uncompressedSize > 0 && te.uncompressedSize < 2048) {
            if (guidMap.find(guid) == guidMap.end()) guidOrder.push_back(guid);
            std::string rawP(
                reinterpret_cast<const char*>(tarBuf + static_cast<size_t>(te.rawDataOffset)),
                static_cast<size_t>(te.uncompressedSize));
            size_t nl = rawP.find_first_of("\r\n\0", 0, 3);
            if (nl != std::string::npos) rawP.resize(nl);
            guidMap[guid].realPath = ArchivePath::Normalize(PathUtils::Utf8ToWide(rawP));
        } else if (leaf == L"asset") {
            if (guidMap.find(guid) == guidMap.end()) guidOrder.push_back(guid);
            auto& slot = guidMap[guid];
            slot.assetOff = static_cast<size_t>(te.rawDataOffset);
            slot.assetSize = te.uncompressedSize;
            slot.hasAsset = true;
        }
    }

    if (guidMap.empty()) return false;

    if (extractTargetPath && outExtractedBytes) {
        std::wstring want = ArchivePath::Normalize(*extractTargetPath);
        for (const auto& guid : guidOrder) {
            const auto& slot = guidMap[guid];
            if (slot.hasAsset && _wcsicmp(slot.realPath.c_str(), want.c_str()) == 0) {
                if (slot.assetOff > tarLen || slot.assetSize > tarLen - slot.assetOff) return false;
                outExtractedBytes->assign(
                    tarBuf + slot.assetOff,
                    tarBuf + slot.assetOff + static_cast<size_t>(slot.assetSize));
                return true;
            }
        }
        return false;
    }

    outInfo.formatName = L"Unity Asset Package (.unitypackage)";
    for (const auto& guid : guidOrder) {
        const auto& slot = guidMap[guid];
        if (slot.realPath.empty()) continue;
        ArchiveItemEntry item;
        item.pathInArchive = slot.realPath;
        item.name = ArchivePath::BaseName(item.pathInArchive);
        item.isDirectory = !slot.hasAsset;
        item.uncompressedSize = slot.assetSize;
        item.compressedSize = slot.assetSize;
        if (tarBuf == data && slot.hasAsset) {
            item.rawDataOffset = slot.assetOff;
            item.compressionMethod = 0;
        } else {
            item.compressionMethod = 8;
        }
        outInfo.entries.push_back(std::move(item));
    }
    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

} // namespace DetailUnixAndLegacyArc

inline bool InspectUnityPackage(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    ArchiveSummaryInfo& outInfo)
{
    return (extLower == L".unitypackage") && DetailUnixAndLegacyArc::ParseUnityPackageFromMemory(data, size, outInfo);
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
        if (DetailUnixAndLegacyArc::ParseUnityPackageFromMemory(data, size, dummy, &want, &outBytes)) {
            return true;
        }
    }
    return false;
}

} // namespace Explorer2::Formats
