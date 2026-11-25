// ArchiveReadersStrategyFpsPak.cpp — Strategy & FPS Game Engine Resource Archive Readers
#include "ArchiveReaders.h"
#include "FormatSupport.h"
#include "PathUtils.h"
#include "ZipDeflate.h"
#include "formats/common/ArchiveFormatCommon.h"
#include "formats/common/GamePakArchiveCommon.h"

#include <unordered_set>

namespace Explorer2 {
#include "formats/common/FormatPrologueCommon.h"
#include "formats/common/FormatLoaderDispatch.h"
} // namespace Explorer2

#include "formats/archives/MonolithBloodRff.h"
#include "formats/archives/RitualSinSpakArchive.h"
#include "formats/archives/EnsembleBarArchive.h"
#include "formats/archives/EnsembleDrsArchive.h"
#include "formats/archives/Heroes3LodArchive.h"
#include "formats/archives/ActivisionZfsArchive.h"
#include "formats/archives/LucasArtsBunArchive.h"

namespace Explorer2::ArchiveReaders {

namespace {

bool SniffRff(const uint8_t* data, size_t size) {
    return size >= 80u && std::memcmp(data, "RFF\x1A", 4) == 0;
}
bool SniffSpak(const uint8_t* data, size_t size) {
    return size >= 140u && std::memcmp(data, "SPAK", 4) == 0;
}
bool SniffBar(const uint8_t* data, size_t size) {
    return size >= 0x140u && std::memcmp(data, "ESPN", 4) == 0 && ByteIO::RdU32LE(data + 8u) == 0x44332211u;
}
bool SniffDrs(const uint8_t* data, size_t size) {
    return size >= 76u && std::memcmp(data, "Copyright (c) 1997 Ensemble Studios.", 36) == 0;
}
bool SniffLod(const uint8_t* data, size_t size) {
    return size >= 124u && std::memcmp(data, "LOD\0", 4) == 0;
}
bool SniffZfs(const uint8_t* data, size_t size) {
    return size >= 68u && std::memcmp(data, "ZFSF", 4) == 0;
}
bool SniffBun(const uint8_t* data, size_t size) {
    return size >= 44u && std::memcmp(data, "LB83", 4) == 0;
}

constexpr LoaderDispatch::ArchiveEntry kStrategyFpsPakArchiveTable[] = {
    { L".rff", Formats::InspectMonolithBloodRff,     Formats::ExtractMonolithBloodRffEntry, SniffRff  },
    { L".sin", Formats::InspectRitualSinSpakArchive, Formats::ExtractRitualSinSpakEntry,    SniffSpak },
    { L".bar", Formats::InspectEnsembleBarArchive,   Formats::ExtractEnsembleBarEntry,      SniffBar  },
    { L".drs", Formats::InspectEnsembleDrsArchive,   Formats::ExtractEnsembleDrsEntry,      SniffDrs  },
    { L".lod", Formats::InspectHeroes3LodArchive,    Formats::ExtractHeroes3LodEntry,       SniffLod  },
    { L".zfs", Formats::InspectActivisionZfsArchive, Formats::ExtractActivisionZfsEntry,    SniffZfs  },
    { L".bun", Formats::InspectLucasArtsBunArchive,  Formats::ExtractLucasArtsBunEntry,     SniffBun  },
};

} // namespace

bool IsStrategyFpsPakArchiveExtension(const std::wstring& extLower) {
    return LoaderDispatch::HasArchiveExt(kStrategyFpsPakArchiveTable, extLower) ||
           IsSystemRetroArchiveExtension(extLower);
}

bool TryInspectStrategyFpsPakArchive(
    const std::wstring& archivePath,
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    ArchiveSummaryInfo& outInfo)
{
    bool matched = false;
    const bool ok = LoaderDispatch::DispatchArchiveInspect(kStrategyFpsPakArchiveTable, data, size, extLower, outInfo, matched);
    return matched ? ok : TryInspectSystemRetroArchive(archivePath, data, size, extLower, outInfo);
}

bool TryExtractStrategyFpsPakArchive(
    const std::wstring& archivePath,
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    bool matched = false;
    const bool ok = LoaderDispatch::DispatchArchiveExtract(kStrategyFpsPakArchiveTable, data, size, extLower, normTarget, outBytes, matched);
    return matched ? ok : TryExtractSystemRetroArchive(archivePath, data, size, extLower, normTarget, outBytes);
}

} // namespace Explorer2::ArchiveReaders
