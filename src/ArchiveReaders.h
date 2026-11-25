#pragma once
// The archive readers behind the archive pane (ArchiveExplorer): table of contents and unpacking of ZIP, RAR, 7z,
// TAR, CAB, ISO and the other containers. They also build on Linux (see linux/), where the parts that need Windows
// (CAB via SetupAPI, 7-Zip / UnRAR / tar.exe) report failure.
#include "ArchiveTypes.h"
#include "PlatformCompat.h"   // DWORD; <windows.h> on Windows
#include <cstdint>
#include <string>
#include <vector>

namespace Explorer2::ArchiveReaders {

bool IsArchiveExtension(const std::wstring& extLower);

// Checks if a filesystem path points to an archive or a virtual subfolder inside an archive
// e.g. "C:\Data\bundle.zip\src\main.cpp" -> outArchiveFile = "C:\Data\bundle.zip", outInnerPath = "src/main.cpp"
bool SplitVirtualArchivePath(const std::wstring& fullPath, std::wstring& outArchiveFile, std::wstring& outInnerPath);
bool IsVirtualArchivePath(const std::wstring& fullPath);

// Reads the complete table of contents of an archive (.zip, .rar, .7z, .tar, .gz, .tgz, .jar, .nupkg, .apk)
bool InspectArchiveFile(const std::wstring& archivePath, ArchiveSummaryInfo& outInfo);

// Lists immediate children at `innerDir` (e.g. "" for root or "src/core" for a subfolder)
bool EnumerateArchiveFolder(const std::wstring& archivePath, const std::wstring& innerDir,
                            std::vector<ArchiveItemEntry>& outImmediateChildren, ArchiveSummaryInfo* outFullSummary = nullptr);
// The same from a table of contents read before, without opening the archive again
void FilterArchiveSummaryFolder(const ArchiveSummaryInfo& summary, const std::wstring& innerDir,
                                std::vector<ArchiveItemEntry>& outImmediateChildren);

// Extracts a single file from `archivePath` into memory or a temporary file on disk
bool ExtractSingleEntryToMemory(const std::wstring& archivePath, const std::wstring& innerPath, std::vector<uint8_t>& outBytes);
bool ExtractSingleEntryToTempFile(const std::wstring& archivePath, const std::wstring& innerPath, std::wstring& outTempFilePath);

// Unpacks a whole archive into a folder. Nothing that is already there is replaced: an entry whose name is taken
// is written as "name (2).ext" (`outKeptExisting` counts those). An entry that would land outside the folder
// ("..", an absolute path) is refused (`outRefused`).
bool ExtractEntireArchiveToFolder(const std::wstring& archivePath, const std::wstring& destFolderPath,
                                  size_t* outKeptExisting = nullptr, size_t* outRefused = nullptr);

// Runs an external archive tool (7z.exe, UnRAR.exe, tar.exe) without a window and waits for it. timeoutMs =
// INFINITE waits for as long as the tool takes. A tool that is still running when its time is up is ended, and
// the caller is told so: it must never be left working unseen on files the caller is about to read, hand to
// another tool or report on.
using ToolRun = ArchiveToolRun;
ToolRun RunToolAndWait(const std::wstring& exePath, const std::wstring& args, DWORD timeoutMs);

bool IsGamePakArchiveExtension(const std::wstring& extLower);
bool TryInspectGamePakArchive(
    const std::wstring& archivePath,
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    ArchiveSummaryInfo& outInfo);
bool TryExtractGamePakArchive(
    const std::wstring& archivePath,
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes);

bool IsSystemRetroArchiveExtension(const std::wstring& extLower);
bool TryInspectSystemRetroArchive(
    const std::wstring& archivePath,
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    ArchiveSummaryInfo& outInfo);
bool TryExtractSystemRetroArchive(
    const std::wstring& archivePath,
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes);

} // namespace Explorer2::ArchiveReaders
