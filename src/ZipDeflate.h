#pragma once
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <windows.h>

namespace Explorer2::ZipDeflate {

struct ArchiveItemMeta {
    std::wstring pathInArchive;   // Normalized forward-slash or backslash path inside archive
    std::wstring name;            // Leaf name
    uint64_t uncompressedSize = 0;
    uint64_t compressedSize = 0;
    uint64_t localHeaderOffset = 0;
    uint32_t crc32 = 0;
    uint16_t compressionMethod = 0; // 0 = Store, 8 = Deflate
    uint16_t dosTime = 0;
    uint16_t dosDate = 0;
    bool isDirectory = false;
    bool isEncrypted = false;
};

// Decompresses raw RFC 1951 DEFLATE stream
bool InflateRaw(
    const uint8_t* src,
    size_t srcLen,
    std::vector<uint8_t>& outData,
    size_t maxOutputBytes = 128 * 1024 * 1024);

// Decompresses RFC 1950 Zlib stream (2-byte header + raw deflate)
bool InflateZlib(
    const uint8_t* src,
    size_t srcLen,
    std::vector<uint8_t>& outData,
    size_t maxOutputBytes = 128 * 1024 * 1024);

// Decompresses RFC 1952 Gzip stream (10+ byte header + raw deflate)
bool InflateGzip(
    const uint8_t* src,
    size_t srcLen,
    std::vector<uint8_t>& outData,
    size_t maxOutputBytes = 128 * 1024 * 1024);

// Reads ZIP Central Directory from an in-memory buffer or directly from disk file
bool ListZipEntriesFromFile(
    const std::wstring& zipFilePath,
    std::vector<ArchiveItemMeta>& outEntries,
    std::wstring* outFormatSummary = nullptr);

bool ListZipEntriesFromMemory(
    const uint8_t* data,
    size_t dataLen,
    std::vector<ArchiveItemMeta>& outEntries);

// Extracts a specific entry by its ArchiveItemMeta or by case-insensitive inner path (e.g. L"xl/sharedStrings.xml")
bool ExtractZipEntryFromFile(
    const std::wstring& zipFilePath,
    const ArchiveItemMeta& entry,
    std::vector<uint8_t>& outBytes,
    size_t maxOutputBytes = 64 * 1024 * 1024);

bool ExtractZipFileByName(
    const std::wstring& zipFilePath,
    const std::wstring& innerPathRelative,
    std::vector<uint8_t>& outBytes,
    size_t maxOutputBytes = 64 * 1024 * 1024);

// Lists TAR (POSIX ustar / GNU tar) or .tar.gz / .tgz entries
bool ListTarEntriesFromFile(
    const std::wstring& tarFilePath,
    std::vector<ArchiveItemMeta>& outEntries,
    std::wstring* outFormatSummary = nullptr);

bool ExtractTarEntryFromFile(
    const std::wstring& tarFilePath,
    const ArchiveItemMeta& entry,
    std::vector<uint8_t>& outBytes,
    size_t maxOutputBytes = 64 * 1024 * 1024);

// Converts MS-DOS date/time to FILETIME
FILETIME DosDateTimeToFileTime(uint16_t dosDate, uint16_t dosTime);

} // namespace Explorer2::ZipDeflate
