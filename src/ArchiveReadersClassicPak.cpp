// ArchiveReadersClassicPak.cpp — Modular Game Archive & Embedded Filesystem Container Readers
#include "ArchiveReaders.h"
#include "FormatSupport.h"
#include "PathUtils.h"
#include "ZipDeflate.h"
#include "formats/common/ArchiveFormatCommon.h"
#include "formats/archives/RenPyRpaArchive.h"
#include "formats/archives/DescentHogArchive.h"
#include "formats/archives/VolitionVppArchive.h"
#include "formats/archives/WestwoodMixArchive.h"

namespace Explorer2 {
#include "formats/common/FormatLoaderDispatch.h"
} // namespace Explorer2

namespace Explorer2::ArchiveReaders {

bool IsEnginePakArchiveExtension(const std::wstring& extLower);
bool TryInspectEnginePakArchive(
    const std::wstring& archivePath,
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    ArchiveSummaryInfo& outInfo);
bool TryExtractEnginePakArchive(
    const std::wstring& archivePath,
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes);

namespace {

bool SniffRpa(const uint8_t* data, size_t size) {
    return size >= 26u && (std::memcmp(data, "RPA-3.0 ", 8) == 0 || std::memcmp(data, "RPA-2.0 ", 8) == 0);
}
bool SniffHog(const uint8_t* data, size_t size) {
    return (size >= 68u && std::memcmp(data, "HOG2", 4) == 0) ||
           (size >= 20u && std::memcmp(data, "DHF", 3) == 0);
}
bool SniffVpp(const uint8_t* data, size_t size) {
    return size >= 4096u && ByteIO::RdU32LE(data) == 0x51890ACEu;
}

constexpr LoaderDispatch::ArchiveEntry kClassicPakArchiveTable[] = {
    { L".rpa", Formats::InspectRenPyRpaArchive,    Formats::ExtractRenPyRpaEntry,    SniffRpa },
    { L".hog", Formats::InspectDescentHogArchive,  Formats::ExtractDescentHogEntry,  SniffHog },
    { L".vpp", Formats::InspectVolitionVppArchive, Formats::ExtractVolitionVppEntry, SniffVpp },
    { L".mix", Formats::InspectWestwoodMixArchive, Formats::ExtractWestwoodMixEntry           },
};

} // namespace

bool IsGamePakArchiveExtension(const std::wstring& extLower) {
    return LoaderDispatch::HasArchiveExt(kClassicPakArchiveTable, extLower) ||
           IsEnginePakArchiveExtension(extLower);
}

bool TryInspectGamePakArchive(
    const std::wstring& archivePath,
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    ArchiveSummaryInfo& outInfo)
{
    bool matched = false;
    const bool ok = LoaderDispatch::DispatchArchiveInspect(kClassicPakArchiveTable, data, size, extLower, outInfo, matched);
    return matched ? ok : TryInspectEnginePakArchive(archivePath, data, size, extLower, outInfo);
}

bool TryExtractGamePakArchive(
    const std::wstring& archivePath,
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    if (normTarget.empty()) return false;
    bool matched = false;
    const bool ok = LoaderDispatch::DispatchArchiveExtract(kClassicPakArchiveTable, data, size, extLower, normTarget, outBytes, matched);
    return matched ? ok : TryExtractEnginePakArchive(archivePath, data, size, extLower, normTarget, outBytes);
}

} // namespace Explorer2::ArchiveReaders
