#pragma once
// ArjArchive.h — ARJ archive reader & method 0..4 extractor (.arj — 0x60 0xEA).

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailArc200 {

inline bool DecodeArjMethod4(const uint8_t* src, size_t compSize, uint64_t uncompSize, std::vector<uint8_t>& outBytes) {
    outBytes.clear();
    if (uncompSize == 0) return true;
    if (!src || compSize == 0 || uncompSize > 256ULL * 1024 * 1024) return false;

    outBytes.reserve(static_cast<size_t>(std::min<uint64_t>(uncompSize, static_cast<uint64_t>(compSize) * 16ULL + 4096ULL)));
    LhaMsbBitReader br(src, compSize);

    while (outBytes.size() < uncompSize) {
        uint32_t w = 0;
        while (w < 7u) {
            uint32_t b = br.ReadBits(1u);
            if (br.overread) { outBytes.clear(); return false; }
            if (b == 0u) break;
            ++w;
        }
        uint32_t lenVal = (w > 0u ? br.ReadBits(w) : 0u) + ((1u << w) - 1u);
        if (br.overread) { outBytes.clear(); return false; }

        if (lenVal == 0u) {
            uint8_t lit = static_cast<uint8_t>(br.ReadBits(8u));
            if (br.overread) { outBytes.clear(); return false; }
            outBytes.push_back(lit);
        } else {
            const size_t matchLen = static_cast<size_t>(lenVal) + 2u;
            uint32_t pw = 0;
            while (pw < 4u) {
                uint32_t b = br.ReadBits(1u);
                if (br.overread) { outBytes.clear(); return false; }
                if (b == 0u) break;
                ++pw;
            }
            const uint32_t ptrBits = 9u + pw;
            const uint32_t dist0 = br.ReadBits(ptrBits) + (((1u << pw) - 1u) << 9u);
            if (br.overread) { outBytes.clear(); return false; }
            const size_t dist = static_cast<size_t>(dist0) + 1u;
            for (size_t k = 0; k < matchLen && outBytes.size() < uncompSize; ++k) {
                const uint8_t ch = (outBytes.size() < dist) ? 0x20u : outBytes[outBytes.size() - dist];
                outBytes.push_back(ch);
            }
        }
    }
    return outBytes.size() == uncompSize;
}

inline bool ParseArjArchive(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    // #37. ARJ Archive — Magic 0x60 0xEA
    if (size >= 34 && data[0] == 0x60 && data[1] == 0xEA) {
        outInfo.formatName = L"ARJ Archive (.arj)";
        size_t pos = 0;
        bool isMainHdr = true;
        while (pos + 4 <= size && outInfo.entries.size() < 40000) {
            if (data[pos] != 0x60 || data[pos + 1] != 0xEA) break;
            uint16_t basicSz = RdU16LE(data + pos + 2);
            if (basicSz == 0) break; // End of ARJ archive
            if (basicSz < 30 || basicSz > 2600 || pos > size || 8u + basicSz > size - pos) break;

            const uint8_t* hdr = data + pos + 4;
            uint8_t firstHdrSz = hdr[0];
            uint8_t method     = hdr[3];
            uint8_t fileType   = hdr[4];
            uint32_t dosTime   = RdU32LE(hdr + 8);
            uint32_t compSz    = RdU32LE(hdr + 12);
            uint32_t origSz    = RdU32LE(hdr + 16);
            uint32_t crc32     = RdU32LE(hdr + 20);

            std::string fname;
            if (firstHdrSz < basicSz) {
                fname.assign(reinterpret_cast<const char*>(hdr + firstHdrSz),
                             strnlen(reinterpret_cast<const char*>(hdr + firstHdrSz), basicSz - firstHdrSz));
            }

            // Skip basic header + 4-byte header CRC + extended header chain
            size_t extPos = pos + 4 + basicSz + 4;
            bool extBad = false;
            while (extPos + 2 <= size) {
                uint16_t extSz = RdU16LE(data + extPos);
                extPos += 2;
                if (extSz == 0) break;
                if (static_cast<size_t>(extSz) + 4u > size - extPos) { extBad = true; break; }
                extPos += static_cast<size_t>(extSz) + 4u; // ext data + CRC32
            }
            if (extBad || extPos > size) break;

            if (isMainHdr || fileType == 2) {
                isMainHdr = false;
                pos = extPos;
                continue;
            }
            if (compSz > size - extPos) break;
            if (!fname.empty()) {
                ArchiveItemEntry item;
                item.pathInArchive = ArchivePath::Normalize(PathUtils::Utf8ToWide(fname));
                item.name = ArchivePath::BaseName(item.pathInArchive);
                item.isDirectory = (fileType == 3);
                item.compressedSize = compSz;
                item.uncompressedSize = origSz;
                item.crc32 = crc32;
                item.dosTime = static_cast<uint16_t>(dosTime & 0xFFFF);
                item.dosDate = static_cast<uint16_t>((dosTime >> 16) & 0xFFFF);
                item.rawDataOffset = extPos;
                item.compressionMethod = (method == 0) ? 0 : method;
                outInfo.entries.push_back(std::move(item));
            }
            pos = extPos + compSz;
        }
        outInfo.valid = !outInfo.entries.empty();
        return outInfo.valid;
    }
    return false;
}

} // namespace DetailArc200

inline bool InspectArjArchive(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return DetailArc200::ParseArjArchive(data, size, outInfo);
}

inline bool ExtractArjArchiveEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& want,
    std::vector<uint8_t>& outBytes)
{
    if (size >= 34 && data[0] == 0x60 && data[1] == 0xEA) {
        ArchiveSummaryInfo info;
        if (DetailArc200::ParseArjArchive(data, size, info)) {
            for (const auto& e : info.entries) {
                if (!e.isDirectory && _wcsicmp(e.pathInArchive.c_str(), want.c_str()) == 0) {
                    if (e.rawDataOffset > size || e.compressedSize > size - static_cast<size_t>(e.rawDataOffset)) {
                        return false;
                    }
                    if (e.uncompressedSize > 256ULL * 1024 * 1024) return false;
                    const uint8_t* p = data + static_cast<size_t>(e.rawDataOffset);
                    const size_t cLen = static_cast<size_t>(e.compressedSize);
                    bool ok = false;
                    if (e.compressionMethod == 0) {
                        if (e.uncompressedSize > size - static_cast<size_t>(e.rawDataOffset)) return false;
                        outBytes.assign(p, p + static_cast<size_t>(e.uncompressedSize));
                        ok = true;
                    } else if (e.compressionMethod >= 1 && e.compressionMethod <= 3) {
                        ok = DetailArc200::DecodeLhaArjBlockHuff(p, cLen, e.uncompressedSize, 17u, 5u, outBytes);
                    } else if (e.compressionMethod == 4) {
                        ok = DetailArc200::DecodeArjMethod4(p, cLen, e.uncompressedSize, outBytes);
                    }
                    if (!ok) {
                        outBytes.clear();
                        return false;
                    }
                    if (e.crc32 != 0u && ::Explorer2::Checksum::Crc32(outBytes.data(), outBytes.size()) != e.crc32) {
                        outBytes.clear();
                        return false;
                    }
                    return true;
                }
            }
        }
        return false;
    }
    return false;
}

} // namespace Explorer2::Formats
