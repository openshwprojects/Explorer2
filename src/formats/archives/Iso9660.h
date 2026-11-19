#pragma once
// Iso9660.h — ISO 9660 / Joliet / Rock Ridge optical disc image reader (.iso).

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailArc100 {

inline bool ParseIso9660(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo,
                         const std::wstring* extractTarget = nullptr, std::vector<uint8_t>* outExtracted = nullptr)
{
    if (size < 0x8800 || data[0x8000] != 1 || std::memcmp(data + 0x8001, "CD001", 5) != 0) return false;
    VirtualDiskReader raw;
    raw.virtualSize = size;
    raw.formatName = L"ISO 9660";
    raw.directRawBuffer = true;
    raw.readBytes = [data, size](uint64_t off, uint8_t* dst, size_t len) -> bool {
        if (off + len > size) return false;
        std::memcpy(dst, data + static_cast<size_t>(off), len);
        return true;
    };

    if (extractTarget && outExtracted) {
        return InspectOrExtractIsoOnReader(raw, 0, L"", outInfo, extractTarget, outExtracted);
    }

    outInfo.formatName = L"ISO 9660";
    if (!InspectOrExtractIsoOnReader(raw, 0, L"", outInfo, nullptr, nullptr)) return false;
    if (outInfo.entries.empty()) {
        std::string volId(reinterpret_cast<const char*>(data + 0x8000 + 40), 32);
        while (!volId.empty() && volId.back() == ' ') volId.pop_back();
        std::wstring vName = volId.empty() ? L"EMPTY_ISO_VOLUME" : PathUtils::Utf8ToWide(volId);
        ArchiveItemEntry e;
        e.pathInArchive = vName;
        e.name = vName;
        e.isDirectory = true;
        outInfo.entries.push_back(std::move(e));
    }
    outInfo.valid = true;
    return true;
}

} // namespace DetailArc100

inline bool InspectIso9660(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return DetailArc100::ParseIso9660(data, size, outInfo);
}

inline bool ExtractIso9660Entry(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    if (size >= 0x8800 && data[0x8000] == 1 && std::memcmp(data + 0x8001, "CD001", 5) == 0) {
        ArchiveSummaryInfo dummy;
        if (DetailArc100::ParseIso9660(data, size, dummy, &normTarget, &outBytes)) return true;
    }
    return false;
}

} // namespace Explorer2::Formats
