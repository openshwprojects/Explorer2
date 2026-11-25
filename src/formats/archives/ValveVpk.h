#pragma once
// ValveVpk.h — Valve Source Engine Pak v1/v2 archive reader & multi-chunk extractor (.vpk).

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailDiskAndGameArc {

inline std::wstring ResolveVpkChunkPath(const std::wstring& dirVpkPath, uint16_t archiveIdx) {
    if (dirVpkPath.empty()) return L"";
    size_t slashPos = dirVpkPath.find_last_of(L"\\/");
    size_t nameStart = (slashPos == std::wstring::npos) ? 0 : (slashPos + 1);
    std::wstring dirPrefix = dirVpkPath.substr(0, nameStart);
    std::wstring fileName  = dirVpkPath.substr(nameStart);

    wchar_t chunkSuffix[24] = {};
    swprintf_s(chunkSuffix, L"_%03u.vpk", static_cast<unsigned>(archiveIdx));

    if (fileName.size() >= 8 &&
        _wcsicmp(fileName.c_str() + fileName.size() - 8, L"_dir.vpk") == 0)
    {
        return dirPrefix + fileName.substr(0, fileName.size() - 8) + chunkSuffix;
    }
    if (fileName.size() >= 4 &&
        _wcsicmp(fileName.c_str() + fileName.size() - 4, L".vpk") == 0)
    {
        return dirPrefix + fileName.substr(0, fileName.size() - 4) + chunkSuffix;
    }
    return L"";
}

inline bool ReadExactDiskSlice(const std::wstring& filePath, uint64_t offset, uint32_t length, uint8_t* dst) {
    if (length == 0) return true;
    HANDLE h = CreateFileW(
        filePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER fsz = {};
    if (!GetFileSizeEx(h, &fsz) || fsz.QuadPart < 0 ||
        offset > static_cast<uint64_t>(fsz.QuadPart) ||
        length > static_cast<uint64_t>(fsz.QuadPart) - offset)
    {
        CloseHandle(h);
        return false;
    }

    LARGE_INTEGER seekPos = {};
    seekPos.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(h, seekPos, nullptr, FILE_BEGIN)) {
        CloseHandle(h);
        return false;
    }

    uint32_t remaining = length;
    size_t written = 0;
    while (remaining > 0) {
        DWORD toRead = static_cast<DWORD>(std::min<uint32_t>(remaining, 16u * 1024u * 1024u));
        DWORD got = 0;
        if (!ReadFile(h, dst + written, toRead, &got, nullptr) || got != toRead) {
            CloseHandle(h);
            return false;
        }
        written   += got;
        remaining -= got;
    }

    CloseHandle(h);
    return true;
}

inline bool ParseOrExtractValveVpk(
    const uint8_t* data,
    size_t size,
    const std::wstring* vpkDiskPath,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm = nullptr,
    std::vector<uint8_t>* outExtracted = nullptr)
{
    if (size < 12 || RdU32LE(data) != 0x55AA1234u) return false;
    uint32_t ver    = RdU32LE(data + 4);
    uint32_t treeSz = RdU32LE(data + 8);
    if (ver != 1u && ver != 2u) return false;
    size_t hdrSz = (ver == 2u) ? 28u : 12u;
    if (treeSz == 0 || size < hdrSz || treeSz > size - hdrSz) return false;

    if (outInfo) {
        outInfo->formatName = L"Valve Pak (VPK v" + std::to_wstring(ver) + L")";
    }

    size_t pos     = hdrSz;
    size_t treeEnd = hdrSz + treeSz;
    auto readCStr = [&](std::string& out) -> bool {
        size_t start = pos;
        while (pos < treeEnd && data[pos] != 0) ++pos;
        if (pos >= treeEnd) return false;
        out.assign(reinterpret_cast<const char*>(data + start), pos - start);
        ++pos;
        return true;
    };

    std::string extStr, pathStr, fileStr;
    while (pos < treeEnd) {
        if (!readCStr(extStr)) return false;
        if (extStr.empty()) break;

        while (pos < treeEnd) {
            if (!readCStr(pathStr)) return false;
            if (pathStr.empty()) break;

            while (pos < treeEnd) {
                if (!readCStr(fileStr)) return false;
                if (fileStr.empty()) break;

                if (pos + 18 > treeEnd) return false;
                uint32_t crc          = RdU32LE(data + pos + 0);
                uint16_t preloadBytes = RdU16LE(data + pos + 4);
                uint16_t archiveIdx   = RdU16LE(data + pos + 6);
                uint32_t entryOff     = RdU32LE(data + pos + 8);
                uint32_t entryLen     = RdU32LE(data + pos + 12);
                uint16_t terminator   = RdU16LE(data + pos + 16);
                if (terminator != 0xFFFFu) return false;
                pos += 18;

                if (preloadBytes > treeEnd - pos) return false;
                const uint8_t* preloadPtr = data + pos;
                pos += preloadBytes;

                // Strictly validate in-archive payload bounds when archiveIdx == 0x7FFF
                if (archiveIdx == 0x7FFFu && entryLen > 0) {
                    if (entryOff > size - treeEnd || entryLen > (size - treeEnd) - entryOff) {
                        if (outInfo) outInfo->entries.clear();
                        return false;
                    }
                }

                std::string full = (pathStr == " " ? "" : (pathStr + "/")) + fileStr +
                                   (extStr == " " ? "" : ("." + extStr));
                std::wstring wp = ArchivePath::Normalize(PathUtils::Utf8ToWide(full));
                if (wp.empty()) continue;

                uint64_t totalBytes = static_cast<uint64_t>(preloadBytes) + static_cast<uint64_t>(entryLen);

                if (extractTargetNorm && outExtracted) {
                    if (PathEqualsCI(wp, *extractTargetNorm)) {
                        if (totalBytes > 256ULL * 1024 * 1024) return false;
                        outExtracted->resize(static_cast<size_t>(totalBytes));
                        if (preloadBytes > 0) {
                            std::memcpy(outExtracted->data(), preloadPtr, preloadBytes);
                        }
                        if (entryLen > 0) {
                            if (archiveIdx == 0x7FFFu) {
                                std::memcpy(outExtracted->data() + preloadBytes,
                                            data + treeEnd + static_cast<size_t>(entryOff),
                                            entryLen);
                            } else {
                                if (!vpkDiskPath || vpkDiskPath->empty()) {
                                    outExtracted->clear();
                                    return false;
                                }
                                std::wstring chunkPath = ResolveVpkChunkPath(*vpkDiskPath, archiveIdx);
                                if (chunkPath.empty() ||
                                    !ReadExactDiskSlice(chunkPath, entryOff, entryLen, outExtracted->data() + preloadBytes))
                                {
                                    outExtracted->clear();
                                    return false;
                                }
                            }
                        }
                        if (::Explorer2::Checksum::Crc32(outExtracted->data(), outExtracted->size()) != crc) {
                            outExtracted->clear();
                            return false;
                        }
                        return true;
                    }
                } else if (outInfo) {
                    ArchiveItemEntry item;
                    item.pathInArchive    = wp;
                    item.name             = ArchivePath::BaseName(wp);
                    item.crc32            = crc;
                    item.uncompressedSize = totalBytes;
                    item.compressedSize   = totalBytes;
                    item.rawDataOffset    = (archiveIdx == 0x7FFFu)
                        ? ((preloadBytes > 0) ? (pos - preloadBytes) : (treeEnd + entryOff))
                        : entryOff;
                    item.compressionMethod = 1; // Always extract via ParseOrExtractValveVpk (preload + _NNN.vpk + CRC32)
                    outInfo->entries.push_back(std::move(item));
                }
            }
        }
    }

    if (extractTargetNorm) return false;
    if (outInfo) {
        outInfo->valid = !outInfo->entries.empty();
        return outInfo->valid;
    }
    return false;
}

inline bool ParseValveVpk(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo, const std::wstring* vpkDiskPath = nullptr) {
    return ParseOrExtractValveVpk(data, size, vpkDiskPath, &outInfo, nullptr, nullptr);
}

} // namespace DetailDiskAndGameArc

inline bool InspectValveVpk(
    const uint8_t* data,
    size_t size,
    ArchiveSummaryInfo& outInfo,
    const std::wstring* vpkDiskPath = nullptr)
{
    return DetailDiskAndGameArc::ParseValveVpk(data, size, outInfo, vpkDiskPath);
}

inline bool ExtractValveVpkEntry(
    const std::wstring& archivePath,
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    return DetailDiskAndGameArc::ParseOrExtractValveVpk(data, size, &archivePath, nullptr, &normTarget, &outBytes);
}

} // namespace Explorer2::Formats
