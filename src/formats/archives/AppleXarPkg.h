#pragma once
// AppleXarPkg.h — macOS eXtensible Archive & installer package reader & extractor (.xar, .pkg — "xar!").

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailArc200 {

// ============================================================================
// #35. macOS eXtensible Archive (.xar / .pkg — "xar!")
// ============================================================================
inline bool ParseXarPkg(const uint8_t* data, size_t size, const std::wstring& extLower, ArchiveSummaryInfo& outInfo) {
    if (size < 28 || std::memcmp(data, "xar!", 4) != 0) return false;
    uint16_t hdrSize   = RdU16BE(data + 4);
    uint64_t tocCompSz = RdU64BE(data + 8);
    uint64_t tocUncSz  = RdU64BE(data + 16);
    if (hdrSize < 28 || hdrSize + tocCompSz > size || tocUncSz > 64 * 1024 * 1024) return false;

    outInfo.formatName = (extLower == L".pkg") ? L"macOS Installer Package (.pkg / XAR)" : L"eXtensible Archive (.xar)";

    std::vector<uint8_t> xmlBytes;
    if (tocCompSz == tocUncSz && tocCompSz > 8 && data[hdrSize] == '<') {
        xmlBytes.assign(data + hdrSize, data + hdrSize + static_cast<size_t>(tocCompSz));
    } else if (!ZipDeflate::InflateZlib(data + hdrSize, static_cast<size_t>(tocCompSz), xmlBytes, static_cast<size_t>(tocUncSz) + 64)) {
        return false;
    }

    uint64_t heapBase = hdrSize + tocCompSz;
    std::string_view sv(reinterpret_cast<const char*>(xmlBytes.data()), xmlBytes.size());
    std::vector<std::wstring> dirStack;

    size_t pos = 0;
    while (pos < sv.size() && outInfo.entries.size() < 20000) {
        size_t fOpen = sv.find("<file", pos);
        size_t fClose = sv.find("</file>", pos);
        if (fOpen == std::string_view::npos && fClose == std::string_view::npos) break;
        if (fClose != std::string_view::npos && (fOpen == std::string_view::npos || fClose < fOpen)) {
            if (!dirStack.empty()) dirStack.pop_back();
            pos = fClose + 7;
            continue;
        }
        size_t nextBoundary = std::min(
            sv.find("<file", fOpen + 5) == std::string_view::npos ? sv.size() : sv.find("<file", fOpen + 5),
            sv.find("</file>", fOpen + 5) == std::string_view::npos ? sv.size() : sv.find("</file>", fOpen + 5));
        std::string_view blk = sv.substr(fOpen, nextBoundary - fOpen);

        auto extractTag = [&](const char* openT, const char* closeT) -> std::string_view {
            size_t a = blk.find(openT);
            if (a == std::string_view::npos) return {};
            a += std::strlen(openT);
            size_t b = blk.find(closeT, a);
            return (b == std::string_view::npos) ? std::string_view{} : blk.substr(a, b - a);
        };

        std::string_view nm = extractTag("<name>", "</name>");
        std::string_view tp = extractTag("<type>", "</type>");
        std::string_view sz = extractTag("<size>", "</size>");
        std::string_view ln = extractTag("<length>", "</length>");
        std::string_view of = extractTag("<offset>", "</offset>");

        std::wstring wName = PathUtils::Utf8ToWide(nm);
        bool isDir = (tp == "directory");
        std::wstring fullRel = dirStack.empty() ? wName : (dirStack.back() + L"/" + wName);
        if (!wName.empty()) {
            ArchiveItemEntry item;
            item.pathInArchive = ArchivePath::Normalize(fullRel);
            item.name = wName;
            item.isDirectory = isDir;
            item.uncompressedSize = sz.empty() ? 0 : std::strtoull(std::string(sz).c_str(), nullptr, 10);
            item.compressedSize   = ln.empty() ? item.uncompressedSize : std::strtoull(std::string(ln).c_str(), nullptr, 10);
            uint64_t relOff       = of.empty() ? 0 : std::strtoull(std::string(of).c_str(), nullptr, 10);
            item.rawDataOffset    = heapBase + relOff;
            item.compressionMethod = (blk.find("octet-stream") != std::string_view::npos && item.compressedSize == item.uncompressedSize) ? 0 : 8;
            outInfo.entries.push_back(std::move(item));
        }
        dirStack.push_back(fullRel);
        pos = fOpen + 5;
    }
    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

} // namespace DetailArc200

inline bool InspectAppleXarPkg(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    ArchiveSummaryInfo& outInfo)
{
    return DetailArc200::ParseXarPkg(data, size, extLower, outInfo);
}

inline bool ExtractAppleXarPkgEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    const std::wstring& want,
    std::vector<uint8_t>& outBytes)
{
    if (size >= 28 && std::memcmp(data, "xar!", 4) == 0) {
        ArchiveSummaryInfo info;
        if (DetailArc200::ParseXarPkg(data, size, extLower, info)) {
            for (const auto& e : info.entries) {
                if (!e.isDirectory && _wcsicmp(e.pathInArchive.c_str(), want.c_str()) == 0) {
                    if (e.rawDataOffset + e.compressedSize > size || e.uncompressedSize > 256 * 1024 * 1024) return false;
                    const uint8_t* p = data + static_cast<size_t>(e.rawDataOffset);
                    size_t cLen = static_cast<size_t>(e.compressedSize);
                    size_t uLen = static_cast<size_t>(e.uncompressedSize);
                    if (e.compressionMethod == 0) {
                        if (e.rawDataOffset + uLen > size) return false;
                        outBytes.assign(p, p + uLen);
                        return true;
                    }
                    if (cLen >= 3 && std::memcmp(p, "BZh", 3) == 0) {
                        return StreamCodecs::DecompressBzip2Stream(p, cLen, outBytes, uLen + 64);
                    }
                    if (cLen >= 6 && std::memcmp(p, "\xFD" "7zXZ\0", 6) == 0) {
                        return StreamCodecs::DecompressXzStream(p, cLen, outBytes, uLen + 64);
                    }
                    if (ZipDeflate::InflateZlib(p, cLen, outBytes, uLen + 64) ||
                        ZipDeflate::InflateRaw(p, cLen, outBytes, uLen + 64))
                    {
                        return true;
                    }
                    if (cLen == uLen) {
                        outBytes.assign(p, p + uLen);
                        return true;
                    }
                    return false;
                }
            }
        }
        return false;
    }
    return false;
}

} // namespace Explorer2::Formats
