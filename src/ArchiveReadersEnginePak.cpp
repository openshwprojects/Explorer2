// ArchiveReadersEnginePak.cpp — Game Engine Resource Archive Readers
#include "ArchiveReaders.h"
#include "FormatSupport.h"
#include "PathUtils.h"
#include "ZipDeflate.h"
#include "formats/common/ArchiveFormatCommon.h"
#include "formats/common/GamePakArchiveCommon.h"
#include "formats/archives/VolitionVpArchive.h"
#include "formats/archives/LucasArtsGobArchive.h"
#include "formats/archives/LucasArtsLabArchive.h"
#include "formats/archives/TerminalRealityPod.h"
#include "formats/archives/GothicVdfsArchive.h"
#include "formats/archives/LithTechRezArchive.h"

namespace Explorer2 {
#include "formats/common/FormatLoaderDispatch.h"
} // namespace Explorer2

namespace Explorer2::ArchiveReaders {

bool IsStrategyFpsPakArchiveExtension(const std::wstring& extLower);
bool TryInspectStrategyFpsPakArchive(
    const std::wstring& archivePath,
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    ArchiveSummaryInfo& outInfo);
bool TryExtractStrategyFpsPakArchive(
    const std::wstring& archivePath,
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes);

namespace {

bool SniffVp(const uint8_t* data, size_t size) {
    return size >= 60u && std::memcmp(data, "VPVP", 4) == 0;
}
bool SniffGob(const uint8_t* data, size_t size) {
    return size >= 29u &&
           (std::memcmp(data, "GOB\x0A", 4) == 0 ||
            (std::memcmp(data, "GOB ", 4) == 0 && ByteIO::RdU32LE(data + 4u) == 20u));
}
bool SniffLab(const uint8_t* data, size_t size) {
    return size >= 34u && std::memcmp(data, "LABN", 4) == 0;
}
bool SniffPod(const uint8_t* data, size_t size) {
    return size >= 118u && (std::memcmp(data, "POD2", 4) == 0 || std::memcmp(data, "POD3", 4) == 0);
}
bool SniffVdf(const uint8_t* data, size_t size) {
    return size >= 376u && std::memcmp(data + 0x100u, "PSVDSC_V2.00", 12) == 0;
}
bool SniffRez(const uint8_t* data, size_t size) {
    return size >= 168u && data[0] == '\r' && data[1] == '\n' &&
           std::memcmp(data + 2u, "RezMgr Version 1", 16) == 0 && data[126u] == 0x1Au;
}

constexpr LoaderDispatch::ArchiveEntry kEnginePakArchiveTable[] = {
    { L".vp",  Formats::InspectVolitionVpArchive,   Formats::ExtractVolitionVpEntry,         SniffVp  },
    { L".gob", Formats::InspectLucasArtsGobArchive, Formats::ExtractLucasArtsGobEntry,       SniffGob },
    { L".lab", Formats::InspectLucasArtsLabArchive, Formats::ExtractLucasArtsLabEntry,       SniffLab },
    { L".pod", Formats::InspectTerminalRealityPod,  Formats::ExtractTerminalRealityPodEntry, SniffPod },
    { L".vdf", Formats::InspectGothicVdfsArchive,   Formats::ExtractGothicVdfsEntry,         SniffVdf },
    { L".rez", Formats::InspectLithTechRezArchive,  Formats::ExtractLithTechRezEntry,        SniffRez },
};

} // namespace

bool IsEnginePakArchiveExtension(const std::wstring& extLower) {
    return LoaderDispatch::HasArchiveExt(kEnginePakArchiveTable, extLower) ||
           IsStrategyFpsPakArchiveExtension(extLower);
}

bool TryInspectEnginePakArchive(
    const std::wstring& archivePath,
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    ArchiveSummaryInfo& outInfo)
{
    bool matched = false;
    const bool ok = LoaderDispatch::DispatchArchiveInspect(kEnginePakArchiveTable, data, size, extLower, outInfo, matched);
    return matched ? ok : TryInspectStrategyFpsPakArchive(archivePath, data, size, extLower, outInfo);
}

bool TryExtractEnginePakArchive(
    const std::wstring& archivePath,
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    bool matched = false;
    const bool ok = LoaderDispatch::DispatchArchiveExtract(kEnginePakArchiveTable, data, size, extLower, normTarget, outBytes, matched);
    return matched ? ok : TryExtractStrategyFpsPakArchive(archivePath, data, size, extLower, normTarget, outBytes);
}

} // namespace Explorer2::ArchiveReaders
