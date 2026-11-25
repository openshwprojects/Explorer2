#pragma once
// Rar4Archive.h — RAR 4.x ('Rar!\x1A\x07\x00') block header & LHD_UNICODE packed UTF-16 table-of-contents reader.
#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailRar4 {

// Official RAR 3.x/4.x packed Unicode filename decoder (LHD_UNICODE = 0x0200, UnRAR encname.cpp)
inline std::wstring DecodeRar4UnicodeName(const uint8_t* nameBuf, size_t nameLen) {
    if (!nameBuf || nameLen == 0) return {};
    size_t zeroPos = 0;
    while (zeroPos < nameLen && nameBuf[zeroPos] != 0u) ++zeroPos;

    // No NUL separator -> standard UTF-8 / OEM narrow string
    if (zeroPos >= nameLen || zeroPos + 1 >= nameLen) {
        return PathUtils::Utf8ToWide(std::string(reinterpret_cast<const char*>(nameBuf), zeroPos));
    }

    size_t encPos = zeroPos + 1u;
    const uint8_t highByte = nameBuf[encPos++];
    uint8_t flagBits = 0;
    int flags = 0;
    size_t namePos = 0;

    std::vector<uint8_t> u16Le;
    u16Le.reserve(nameLen * 2u);
    auto pushU16 = [&](uint16_t ch) {
        u16Le.push_back(static_cast<uint8_t>(ch & 0xFFu));
        u16Le.push_back(static_cast<uint8_t>((ch >> 8u) & 0xFFu));
    };

    while (encPos < nameLen && u16Le.size() < 8192u) {
        if (flags == 0) {
            flagBits = nameBuf[encPos++];
            flags = 8;
        }
        switch ((flagBits >> 6u) & 3u) {
        case 0: {
            if (encPos >= nameLen) { flags = 0; encPos = nameLen; break; }
            const uint16_t ch = nameBuf[encPos++];
            if (ch == 0u) { encPos = nameLen; break; }
            pushU16(ch);
            ++namePos;
            break;
        }
        case 1: {
            if (encPos >= nameLen) { flags = 0; encPos = nameLen; break; }
            const uint16_t ch = static_cast<uint16_t>(nameBuf[encPos++]) | (static_cast<uint16_t>(highByte) << 8u);
            if (ch == 0u) { encPos = nameLen; break; }
            pushU16(ch);
            ++namePos;
            break;
        }
        case 2: {
            if (nameLen - encPos < 2u) { flags = 0; encPos = nameLen; break; }
            const uint16_t ch = static_cast<uint16_t>(nameBuf[encPos]) |
                                (static_cast<uint16_t>(nameBuf[encPos + 1u]) << 8u);
            encPos += 2u;
            if (ch == 0u) { encPos = nameLen; break; }
            pushU16(ch);
            ++namePos;
            break;
        }
        case 3: {
            if (encPos >= nameLen) { flags = 0; encPos = nameLen; break; }
            const uint8_t length = nameBuf[encPos++];
            if ((length & 0x80u) != 0u) {
                if (encPos >= nameLen) { flags = 0; encPos = nameLen; break; }
                const uint8_t correction = nameBuf[encPos++];
                size_t count = static_cast<size_t>(length & 0x7Fu) + 2u;
                while (count-- > 0u && namePos < zeroPos && u16Le.size() < 8192u) {
                    const uint8_t low = static_cast<uint8_t>(nameBuf[namePos++] + correction);
                    pushU16(static_cast<uint16_t>(low) | (static_cast<uint16_t>(highByte) << 8u));
                }
            } else {
                size_t count = static_cast<size_t>(length) + 2u;
                while (count-- > 0u && namePos < zeroPos && u16Le.size() < 8192u) {
                    pushU16(static_cast<uint16_t>(nameBuf[namePos++]));
                }
            }
            break;
        }
        }
        flagBits = static_cast<uint8_t>(flagBits << 2u);
        flags -= 2;
    }

    if (u16Le.empty()) {
        return PathUtils::Utf8ToWide(std::string(reinterpret_cast<const char*>(nameBuf), zeroPos));
    }
    return ReaderText::DecodeUtf16Span(u16Le.data(), u16Le.size() / 2u, false);
}

} // namespace DetailRar4

inline bool ParseRar4Headers(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (!data || size < 14 || std::memcmp(data, "Rar!\x1A\x07\x00", 7) != 0) return false;
    outInfo.formatName = L"RAR4";

    size_t pos = 7;
    while (size - pos >= 7u) {
        const uint8_t  headType  = data[pos + 2];
        const uint16_t headFlags = ByteIO::RdU16LE(data + pos + 3);
        const uint16_t headSize  = ByteIO::RdU16LE(data + pos + 5);
        if (headSize < 7u || headSize > size - pos) break;

        // Reject -hp header-encrypted RAR4 archives (MAIN_HEAD 0x73 with MHD_PASSWORD = 0x0080)
        if (headType == 0x73u && (headFlags & 0x0080u) != 0u) {
            outInfo = {};
            return false;
        }

        uint64_t addSize = 0;
        if ((headFlags & 0x8000u) != 0u || headType == 0x74u) {
            if (headSize >= 11u) {
                addSize = ByteIO::RdU32LE(data + pos + 7);
            } else {
                break;
            }
        }

        if (headType == 0x74u) {
            if (headSize < 32u) break;
            uint32_t packLow = ByteIO::RdU32LE(data + pos + 7);
            uint32_t unpLow  = ByteIO::RdU32LE(data + pos + 11);
            uint32_t fileCrc = ByteIO::RdU32LE(data + pos + 16);
            uint32_t ftime   = ByteIO::RdU32LE(data + pos + 20);
            uint8_t  method  = data[pos + 25];
            uint16_t nameLen = ByteIO::RdU16LE(data + pos + 26);
            uint32_t attr    = ByteIO::RdU32LE(data + pos + 28);

            size_t nameOff = pos + 32u;
            const size_t hdrEnd = pos + headSize;
            uint64_t packHigh = 0, unpHigh = 0;
            if ((headFlags & 0x0100u) != 0u) {
                if (hdrEnd - nameOff < 8u) break;
                packHigh = ByteIO::RdU32LE(data + nameOff);
                unpHigh  = ByteIO::RdU32LE(data + nameOff + 4);
                nameOff += 8u;
            }
            const uint64_t packSize = (packHigh << 32u) | packLow;
            const uint64_t unpSize  = (unpHigh << 32u) | unpLow;
            addSize = packSize;

            if (addSize > size - hdrEnd) break;

            if (nameLen > 0u && nameLen <= hdrEnd - nameOff) {
                std::wstring decodedName;
                if ((headFlags & 0x0200u) != 0u) {
                    decodedName = DetailRar4::DecodeRar4UnicodeName(data + nameOff, nameLen);
                } else {
                    size_t asciiLen = 0;
                    while (asciiLen < nameLen && data[nameOff + asciiLen] != 0u) ++asciiLen;
                    decodedName = PathUtils::Utf8ToWide(std::string(reinterpret_cast<const char*>(data + nameOff), asciiLen));
                }

                std::wstring normPath = ArchivePath::Normalize(decodedName);
                if (!normPath.empty()) {
                    ArchiveItemEntry item;
                    item.pathInArchive     = normPath;
                    item.name              = ArchivePath::BaseName(normPath);
                    item.uncompressedSize  = unpSize;
                    item.compressedSize    = packSize;
                    item.crc32             = fileCrc;
                    item.dosTime           = static_cast<uint16_t>(ftime & 0xFFFFu);
                    item.dosDate           = static_cast<uint16_t>((ftime >> 16u) & 0xFFFFu);
                    item.isDirectory       = ((headFlags & 0x00E0u) == 0x00E0u) || ((attr & 0x10u) != 0u);
                    item.rawDataOffset     = hdrEnd;
                    item.compressionMethod = method; // 0x30 = Store
                    outInfo.entries.push_back(std::move(item));
                }
            }
        } else if (headType == 0x7Bu) { // ENDARC_HEAD
            break;
        }

        if (addSize > size - pos - headSize) break;
        pos += headSize + static_cast<size_t>(addSize);
    }

    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

} // namespace Explorer2::Formats
