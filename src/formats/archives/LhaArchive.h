#pragma once
// LhaArchive.h — LHArc / LZH archive reader & -lh0-/-lh4..7-/-lzs-/-lz5- extractor (.lzh, .lha).

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailArc200 {

inline bool DecodeLhaLzs(const uint8_t* src, size_t compSize, uint64_t uncompSize, std::vector<uint8_t>& outBytes) {
    outBytes.clear();
    if (uncompSize == 0) return true;
    if (!src || compSize == 0 || uncompSize > 256ULL * 1024 * 1024) return false;

    uint8_t ring[2048];
    std::memset(ring, 0x20, sizeof(ring));
    size_t r = 2048u - 17u;
    outBytes.reserve(static_cast<size_t>(std::min<uint64_t>(uncompSize, static_cast<uint64_t>(compSize) * 16ULL + 4096ULL)));
    LhaMsbBitReader br(src, compSize);

    while (outBytes.size() < uncompSize) {
        uint32_t flag = br.ReadBits(1u);
        if (br.overread) { outBytes.clear(); return false; }
        if (flag != 0u) {
            uint8_t b = static_cast<uint8_t>(br.ReadBits(8u));
            if (br.overread) { outBytes.clear(); return false; }
            ring[r] = b;
            r = (r + 1u) & 2047u;
            outBytes.push_back(b);
        } else {
            uint32_t off = br.ReadBits(11u);
            uint32_t len = br.ReadBits(4u) + 2u;
            if (br.overread) { outBytes.clear(); return false; }
            for (uint32_t k = 0; k < len && outBytes.size() < uncompSize; ++k) {
                uint8_t b = ring[(off + k) & 2047u];
                ring[r] = b;
                r = (r + 1u) & 2047u;
                outBytes.push_back(b);
            }
        }
    }
    return outBytes.size() == uncompSize;
}

inline bool DecodeLhaLz5(const uint8_t* src, size_t compSize, uint64_t uncompSize, std::vector<uint8_t>& outBytes) {
    outBytes.clear();
    if (uncompSize == 0) return true;
    if (!src || compSize == 0 || uncompSize > 256ULL * 1024 * 1024) return false;

    uint8_t ring[4096];
    size_t rp = 0;
    for (int i = 0; i < 256; ++i) {
        for (int j = 0; j < 13; ++j) ring[rp++] = static_cast<uint8_t>(i);
    }
    for (int i = 0; i < 256; ++i) ring[rp++] = static_cast<uint8_t>(i);
    for (int i = 255; i >= 0; --i) ring[rp++] = static_cast<uint8_t>(i);
    for (int i = 0; i < 128; ++i) ring[rp++] = 0u;
    for (int i = 0; i < 110; ++i) ring[rp++] = 0x20u;
    for (int i = 0; i < 18; ++i)  ring[rp++] = 0u;

    size_t r = 4096u - 18u;
    outBytes.reserve(static_cast<size_t>(std::min<uint64_t>(uncompSize, static_cast<uint64_t>(compSize) * 16ULL + 4096ULL)));
    size_t pos = 0;
    while (outBytes.size() < uncompSize) {
        if (pos >= compSize) { outBytes.clear(); return false; }
        uint8_t flags = src[pos++];
        for (int bit = 0; bit < 8 && outBytes.size() < uncompSize; ++bit, flags >>= 1) {
            if ((flags & 1u) != 0u) {
                if (pos >= compSize) { outBytes.clear(); return false; }
                uint8_t b = src[pos++];
                ring[r] = b;
                r = (r + 1u) & 4095u;
                outBytes.push_back(b);
            } else {
                if (pos + 2u > compSize) { outBytes.clear(); return false; }
                uint8_t b0 = src[pos++];
                uint8_t b1 = src[pos++];
                size_t off = static_cast<size_t>(b0) | (static_cast<size_t>(b1 & 0xF0u) << 4);
                size_t len = static_cast<size_t>(b1 & 0x0Fu) + 3u;
                for (size_t k = 0; k < len && outBytes.size() < uncompSize; ++k) {
                    uint8_t b = ring[(off + k) & 4095u];
                    ring[r] = b;
                    r = (r + 1u) & 4095u;
                    outBytes.push_back(b);
                }
            }
        }
    }
    return outBytes.size() == uncompSize;
}

inline uint16_t MapLhaMethodCode(const std::string& method) {
    if (method == "-lh0-" || method == "-lz4-" || method == "-pm0-" || method == "-lhd-") return 0;
    if (method == "-lh4-") return 4;
    if (method == "-lh5-") return 5;
    if (method == "-lh6-") return 6;
    if (method == "-lh7-") return 7;
    if (method == "-lzs-") return 16;
    if (method == "-lz5-") return 17;
    return 99;
}

inline bool ParseLhaArchive(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    // #36. LZH / LHA: "-lh0-".."-lh7-" / "-lzs-" / "-lz5-" / "-lhd-" at offset 2
    if (size >= 24 && data[2] == '-' && (data[3] == 'l' || data[3] == 'p') && data[6] == '-') {
        outInfo.formatName = L"LHArc Archive (.lzh/.lha)";
        size_t pos = 0;
        while (pos + 22 <= size && outInfo.entries.size() < 40000) {
            if (data[pos] == 0) break;
            if (data[pos + 2] != '-' || data[pos + 6] != '-') break;
            std::string method(reinterpret_cast<const char*>(data + pos + 2), 5);
            uint32_t packSz = RdU32LE(data + pos + 7);
            uint32_t origSz = RdU32LE(data + pos + 11);
            uint32_t dosDT  = RdU32LE(data + pos + 15);
            uint8_t lvl     = data[pos + 20];

            std::string fname, dirName;
            size_t dataStart = 0;
            if (lvl == 0 || lvl == 1) {
                uint8_t hdrSz = data[pos];
                uint8_t nLen  = data[pos + 21];
                if (pos > size || 2u + hdrSz > size - pos || 22u + nLen > 2u + hdrSz) break;
                fname.assign(reinterpret_cast<const char*>(data + pos + 22), nLen);
                for (char& c : fname) if (static_cast<uint8_t>(c) == 0xFFu) c = '/';
                dataStart = pos + 2u + hdrSz;

                if (lvl == 1) {
                    // In LHA Level 1, the last 2 bytes of the basic header hold the first extended header size,
                    // and packSz (skip_size) includes all extended header bytes plus the compressed payload!
                    if (2u + hdrSz >= 27u + nLen) {
                        size_t sizeFieldPos = pos + 2u + hdrSz - 2u;
                        size_t totalExtBytes = 0;
                        bool extBad = false;
                        for (int extGuard = 0; extGuard < 1024; ++extGuard) {
                            if (sizeFieldPos + 2u > size) { extBad = true; break; }
                            uint16_t nextExt = RdU16LE(data + sizeFieldPos);
                            if (nextExt == 0u) break;
                            size_t extStart = sizeFieldPos + 2u;
                            if (nextExt < 3u || extStart > size || nextExt > size - extStart) {
                                extBad = true;
                                break;
                            }
                            uint8_t etype = data[extStart];
                            size_t payloadLen = static_cast<size_t>(nextExt) - 3u;
                            if (etype == 0x01u && payloadLen > 0u) {
                                fname.assign(reinterpret_cast<const char*>(data + extStart + 1u),
                                             strnlen(reinterpret_cast<const char*>(data + extStart + 1u), payloadLen));
                            } else if (etype == 0x02u && payloadLen > 0u) {
                                dirName.assign(reinterpret_cast<const char*>(data + extStart + 1u),
                                               strnlen(reinterpret_cast<const char*>(data + extStart + 1u), payloadLen));
                                for (char& c : dirName) if (static_cast<uint8_t>(c) == 0xFFu) c = '/';
                            }
                            sizeFieldPos += nextExt;
                            totalExtBytes += nextExt;
                            if (totalExtBytes > packSz) { extBad = true; break; }
                        }
                        if (extBad) break;
                        dataStart = pos + 2u + hdrSz + totalExtBytes;
                        packSz -= static_cast<uint32_t>(totalExtBytes);
                    }
                }
            } else if (lvl == 2) {
                uint16_t totalHdr = RdU16LE(data + pos);
                if (totalHdr < 26u || pos > size || totalHdr > size - pos) break;
                dataStart = pos + totalHdr;
                size_t sizeFieldPos = pos + 24u;
                for (int extGuard = 0; extGuard < 1024 && sizeFieldPos + 2u <= dataStart; ++extGuard) {
                    uint16_t nextExt = RdU16LE(data + sizeFieldPos);
                    if (nextExt == 0u) break;
                    size_t extStart = sizeFieldPos + 2u;
                    if (nextExt < 3u || extStart > dataStart || nextExt > dataStart - extStart) break;
                    uint8_t etype = data[extStart];
                    size_t payloadLen = static_cast<size_t>(nextExt) - 3u;
                    if (etype == 0x01u && payloadLen > 0u) {
                        fname.assign(reinterpret_cast<const char*>(data + extStart + 1u),
                                     strnlen(reinterpret_cast<const char*>(data + extStart + 1u), payloadLen));
                    } else if (etype == 0x02u && payloadLen > 0u) {
                        dirName.assign(reinterpret_cast<const char*>(data + extStart + 1u),
                                       strnlen(reinterpret_cast<const char*>(data + extStart + 1u), payloadLen));
                        for (char& c : dirName) if (static_cast<uint8_t>(c) == 0xFFu) c = '/';
                    }
                    sizeFieldPos += nextExt;
                }
            } else {
                break;
            }

            if (dataStart > size || packSz > size - dataStart) break;
            if (!dirName.empty() && dirName.back() != '/' && dirName.back() != '\\') {
                dirName.push_back('/');
            }
            std::string fullP = dirName + fname;
            if (!fullP.empty()) {
                ArchiveItemEntry item;
                item.pathInArchive = ArchivePath::Normalize(PathUtils::Utf8ToWide(fullP));
                item.name = ArchivePath::BaseName(item.pathInArchive);
                item.isDirectory = (method == "-lhd-");
                item.compressedSize = packSz;
                item.uncompressedSize = origSz;
                if (lvl == 0 || lvl == 1) {
                    item.dosTime = static_cast<uint16_t>(dosDT & 0xFFFFu);
                    item.dosDate = static_cast<uint16_t>((dosDT >> 16u) & 0xFFFFu);
                }
                item.rawDataOffset = dataStart;
                item.compressionMethod = MapLhaMethodCode(method);
                outInfo.entries.push_back(std::move(item));
            }
            pos = dataStart + packSz;
        }
        outInfo.valid = !outInfo.entries.empty();
        return outInfo.valid;
    }
    return false;
}

} // namespace DetailArc200

inline bool InspectLhaArchive(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return DetailArc200::ParseLhaArchive(data, size, outInfo);
}

inline bool ExtractLhaArchiveEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& want,
    std::vector<uint8_t>& outBytes)
{
    if (size >= 24 && data[2] == '-' && (data[3] == 'l' || data[3] == 'p') && data[6] == '-') {
        ArchiveSummaryInfo info;
        if (DetailArc200::ParseLhaArchive(data, size, info)) {
            for (const auto& e : info.entries) {
                if (!e.isDirectory && _wcsicmp(e.pathInArchive.c_str(), want.c_str()) == 0) {
                    if (e.rawDataOffset > size || e.compressedSize > size - static_cast<size_t>(e.rawDataOffset)) {
                        return false;
                    }
                    if (e.uncompressedSize > 256ULL * 1024 * 1024) return false;
                    const uint8_t* p = data + static_cast<size_t>(e.rawDataOffset);
                    const size_t cLen = static_cast<size_t>(e.compressedSize);
                    if (e.compressionMethod == 0) {
                        if (e.uncompressedSize > size - static_cast<size_t>(e.rawDataOffset)) return false;
                        outBytes.assign(p, p + static_cast<size_t>(e.uncompressedSize));
                        return true;
                    }
                    if (e.compressionMethod == 4 || e.compressionMethod == 5) {
                        return DetailArc200::DecodeLhaArjBlockHuff(p, cLen, e.uncompressedSize, 14u, 4u, outBytes);
                    }
                    if (e.compressionMethod == 6) {
                        return DetailArc200::DecodeLhaArjBlockHuff(p, cLen, e.uncompressedSize, 16u, 5u, outBytes);
                    }
                    if (e.compressionMethod == 7) {
                        return DetailArc200::DecodeLhaArjBlockHuff(p, cLen, e.uncompressedSize, 17u, 5u, outBytes);
                    }
                    if (e.compressionMethod == 16) {
                        return DetailArc200::DecodeLhaLzs(p, cLen, e.uncompressedSize, outBytes);
                    }
                    if (e.compressionMethod == 17) {
                        return DetailArc200::DecodeLhaLz5(p, cLen, e.uncompressedSize, outBytes);
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
