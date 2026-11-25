#pragma once
// GzipSingleArchive.h — RFC 1952 single- and multi-member GZIP (.gz) metadata & member inspector.
#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

inline bool ParseSingleGzip(const std::wstring& archivePath, const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (!data || size < 18 || data[0] != 0x1Fu || data[1] != 0x8Bu || data[2] != 8u) return false;
    const uint8_t flags = data[3];
    if ((flags & 0xE0u) != 0u) return false; // Reserved RFC 1952 flag bits must be zero

    const uint32_t mtime = ByteIO::RdU32LE(data + 4);
    size_t pos = 10;

    if ((flags & 0x04u) != 0u) { // FEXTRA
        if (size - pos < 2u) return false;
        const uint16_t xlen = ByteIO::RdU16LE(data + pos);
        pos += 2u;
        if (xlen > size - pos) return false;
        pos += xlen;
    }

    std::wstring innerName;
    if ((flags & 0x08u) != 0u) { // FNAME (ISO-8859-1 / UTF-8 NUL-terminated)
        const size_t start = pos;
        while (pos < size && data[pos] != 0u) ++pos;
        if (pos >= size) return false;
        if (pos > start) {
            innerName = PathUtils::Utf8ToWide(std::string(reinterpret_cast<const char*>(data + start), pos - start));
        }
        ++pos;
    }

    if ((flags & 0x10u) != 0u) { // FCOMMENT
        while (pos < size && data[pos] != 0u) ++pos;
        if (pos >= size) return false;
        ++pos;
    }

    if ((flags & 0x02u) != 0u) { // FHCRC
        if (size - pos < 2u) return false;
        pos += 2u;
    }

    // At least 8 bytes must remain for CRC32 + ISIZE trailer
    if (size - pos < 8u) return false;

    if (innerName.empty()) {
        std::wstring fn = PathUtils::GetFileName(archivePath);
        if (fn.size() > 3 && PathUtils::ToLower(fn.substr(fn.size() - 3)) == L".gz") {
            innerName = fn.substr(0, fn.size() - 3);
        } else {
            innerName = fn + L".extracted";
        }
    }

    uint64_t uncompSize = ByteIO::RdU32LE(data + size - 4);
    uint32_t crc        = ByteIO::RdU32LE(data + size - 8);

    // For in-memory GZIP streams up to 8 MB, test-decompress via InflateGzip (which supports
    // multi-member concatenated GZIP streams) so concatenated .gz files report exact total size & CRC32.
    if (size <= 8u * 1024u * 1024u) {
        std::vector<uint8_t> dec;
        if (ZipDeflate::InflateGzip(data, size, dec, 64u * 1024u * 1024u)) {
            uncompSize = dec.size();
            crc = Checksum::Crc32(dec.data(), dec.size());
        } else {
            return false;
        }
    }

    outInfo.formatName = L"GZIP";
    ArchiveItemEntry item;
    item.pathInArchive = ArchivePath::BaseName(ArchivePath::Normalize(innerName));
    if (item.pathInArchive.empty()) item.pathInArchive = L"payload.bin";
    item.name = item.pathInArchive;
    item.uncompressedSize = uncompSize;
    item.compressedSize = size;
    item.crc32 = crc;
    item.isDirectory = false;
    item.compressionMethod = 8;
    if (mtime > 0u) {
        DetailDiskAndGameArc::UnixSecondsToDosDateTime(mtime, item.dosDate, item.dosTime);
    }
    outInfo.entries.push_back(std::move(item));
    outInfo.valid = true;
    return true;
}

} // namespace Explorer2::Formats
