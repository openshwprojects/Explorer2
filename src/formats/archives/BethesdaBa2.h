#pragma once
// BethesdaBa2.h — Bethesda Archive 2 GNRL & DX10 texture reader & extractor (.ba2 — "BTDX").

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailDiskAndGameArc {

inline void BuildBa2Dx10DdsHeader(
    uint16_t width,
    uint16_t height,
    uint8_t numMips,
    uint8_t dxgiFormat,
    uint16_t tileMode,
    uint8_t outHdr[148])
{
    std::memset(outHdr, 0, 148);
    std::memcpy(outHdr, "DDS ", 4);
    uint32_t mips = std::max<uint32_t>(1, numMips);
    uint32_t ddsFlags = 0x1u | 0x2u | 0x4u | 0x1000u | (mips > 1 ? 0x20000u : 0u);
    WrU32LE(outHdr + 4, 124);       // DDS_HEADER.dwSize
    WrU32LE(outHdr + 8, ddsFlags);  // DDS_HEADER.dwFlags
    WrU32LE(outHdr + 12, height);   // dwHeight
    WrU32LE(outHdr + 16, width);    // dwWidth
    WrU32LE(outHdr + 28, mips);     // dwMipMapCount
    WrU32LE(outHdr + 76, 32);       // ddspf.dwSize
    WrU32LE(outHdr + 80, 0x4u);     // DDPF_FOURCC
    std::memcpy(outHdr + 84, "DX10", 4);
    uint32_t caps1 = 0x1000u | (mips > 1 ? 0x400008u : 0u);
    WrU32LE(outHdr + 108, caps1);
    bool isCubemap = (tileMode & 0x0100u) != 0 || (tileMode & 0x80u) != 0;
    if (isCubemap) WrU32LE(outHdr + 112, 0xFE00u);

    // DDS_HEADER_DXT10 (20 bytes @ offset 128)
    WrU32LE(outHdr + 128, dxgiFormat);
    WrU32LE(outHdr + 132, 3); // D3D10_RESOURCE_DIMENSION_TEXTURE2D
    WrU32LE(outHdr + 136, isCubemap ? 0x4u : 0u);
    WrU32LE(outHdr + 140, 1); // arraySize = 1
    WrU32LE(outHdr + 144, 0);
}

inline bool ParseOrExtractBethesdaBa2(
    const uint8_t* data,
    size_t size,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm,
    std::vector<uint8_t>* outExtracted)
{
    if (size < 24 || std::memcmp(data, "BTDX", 4) != 0) return false;
    uint32_t version = RdU32LE(data + 4);
    char typeTag[5]  = {};
    std::memcpy(typeTag, data + 8, 4);
    uint32_t numFiles = RdU32LE(data + 12);
    uint64_t nameTblOff = RdU64LE(data + 16);

    bool isGnrl = (std::memcmp(typeTag, "GNRL", 4) == 0);
    bool isDx10 = (std::memcmp(typeTag, "DX10", 4) == 0 || std::memcmp(typeTag, "GNMF", 4) == 0);
    if ((!isGnrl && !isDx10) || numFiles == 0 || numFiles > 500000) return false;

    size_t hdrSize = (version == 2) ? 32 : ((version == 3) ? 36 : 24);
    if (hdrSize > size) return false;

    // 1. Read optional string name table at nameTableOffset
    std::vector<std::wstring> names;
    if (nameTblOff > 0 && nameTblOff < size) {
        names.reserve(std::min<uint32_t>(numFiles, 16384));
        size_t np = static_cast<size_t>(nameTblOff);
        for (uint32_t i = 0; i < numFiles && np + 2 <= size; ++i) {
            uint16_t nLen = RdU16LE(data + np);
            np += 2;
            if (np + nLen > size) break;
            std::string raw(reinterpret_cast<const char*>(data + np), nLen);
            names.push_back(ArchivePath::Normalize(PathUtils::Utf8ToWide(raw)));
            np += nLen;
        }
    }

    if (outInfo) {
        outInfo->formatName = isGnrl ? L"Bethesda BA2 (GNRL)" : L"Bethesda BA2 Textures (DX10)";
    }

    auto makeFallbackBa2Name = [&](uint32_t idx, uint32_t dirHash, uint32_t nameHash, const uint8_t* ext4) -> std::wstring {
        if (idx < names.size() && !names[idx].empty()) return names[idx];
        std::string ext(reinterpret_cast<const char*>(ext4), strnlen(reinterpret_cast<const char*>(ext4), 4));
        if (ext.empty()) ext = isDx10 ? "dds" : "bin";
        wchar_t buf[96] = {};
        swprintf_s(buf, L"%08x/%08x.%S", dirHash, nameHash, ext.c_str());
        return buf;
    };

    if (isGnrl) {
        if (hdrSize + static_cast<uint64_t>(numFiles) * 36 > size) return false;
        for (uint32_t i = 0; i < numFiles; ++i) {
            const uint8_t* rec = data + hdrSize + static_cast<size_t>(i) * 36;
            uint32_t nameHash = RdU32LE(rec + 0);
            uint32_t dirHash  = RdU32LE(rec + 8);
            uint64_t offset   = RdU64LE(rec + 16);
            uint32_t packSz   = RdU32LE(rec + 24);
            uint32_t unpSz    = RdU32LE(rec + 28);
            std::wstring full = makeFallbackBa2Name(i, dirHash, nameHash, rec + 4);

            if (extractTargetNorm && outExtracted) {
                if (PathEqualsCI(full, *extractTargetNorm)) {
                    if (packSz == 0) {
                        if (offset + unpSz > size || unpSz > 128 * 1024 * 1024) return false;
                        outExtracted->assign(data + static_cast<size_t>(offset), data + static_cast<size_t>(offset + unpSz));
                        return true;
                    } else {
                        if (offset + packSz > size) return false;
                        const uint8_t* src = data + static_cast<size_t>(offset);
                        if (packSz >= 4 && RdU32LE(src) == 0x184D2204u) {
                            return StreamCodecs::DecompressLz4Frame(src, packSz, *outExtracted, 128 * 1024 * 1024);
                        }
                        if (ZipDeflate::InflateZlib(src, packSz, *outExtracted, 128 * 1024 * 1024)) return true;
                        return ZipDeflate::InflateRaw(src, packSz, *outExtracted, 128 * 1024 * 1024);
                    }
                }
            } else if (outInfo) {
                ArchiveItemEntry item;
                item.pathInArchive = full;
                item.name = ArchivePath::BaseName(full);
                item.uncompressedSize = unpSz;
                item.compressedSize   = (packSz > 0) ? packSz : unpSz;
                item.rawDataOffset    = offset;
                item.compressionMethod = (packSz == 0 && offset + unpSz <= size) ? 0 : 8;
                outInfo->entries.push_back(std::move(item));
            }
        }
    } else {
        // DX10 Texture records + variable chunk arrays
        size_t pos = hdrSize;
        for (uint32_t i = 0; i < numFiles; ++i) {
            if (pos + 24 > size) break;
            const uint8_t* th = data + pos;
            uint32_t nameHash   = RdU32LE(th + 0);
            uint32_t dirHash    = RdU32LE(th + 8);
            uint8_t  numChunks  = th[13];
            uint16_t chunkHdrSz = RdU16LE(th + 14);
            uint16_t height     = RdU16LE(th + 16);
            uint16_t width      = RdU16LE(th + 18);
            uint8_t  numMips    = th[20];
            uint8_t  dxgiFmt    = th[21];
            uint16_t tileMode   = RdU16LE(th + 22);
            if (chunkHdrSz < 24) chunkHdrSz = 24;
            pos += 24;
            if (pos + static_cast<size_t>(numChunks) * chunkHdrSz > size) break;

            uint64_t sumUnp = 148;
            uint64_t sumPack = 0;
            size_t chunksStart = pos;
            for (uint8_t c = 0; c < numChunks; ++c) {
                const uint8_t* cr = data + chunksStart + static_cast<size_t>(c) * chunkHdrSz;
                uint32_t pSz = RdU32LE(cr + 8);
                uint32_t uSz = RdU32LE(cr + 12);
                sumUnp  += uSz;
                sumPack += (pSz > 0) ? pSz : uSz;
            }
            pos += static_cast<size_t>(numChunks) * chunkHdrSz;

            std::wstring full = makeFallbackBa2Name(i, dirHash, nameHash, th + 4);
            if (extractTargetNorm && outExtracted) {
                if (PathEqualsCI(full, *extractTargetNorm)) {
                    if (sumUnp > 128 * 1024 * 1024) return false;
                    uint8_t ddsHdr[148] = {};
                    BuildBa2Dx10DdsHeader(width, height, numMips, dxgiFmt, tileMode, ddsHdr);
                    outExtracted->assign(ddsHdr, ddsHdr + 148);
                    outExtracted->reserve(static_cast<size_t>(sumUnp));
                    for (uint8_t c = 0; c < numChunks; ++c) {
                        const uint8_t* cr = data + chunksStart + static_cast<size_t>(c) * chunkHdrSz;
                        uint64_t cOff = RdU64LE(cr + 0);
                        uint32_t pSz  = RdU32LE(cr + 8);
                        uint32_t uSz  = RdU32LE(cr + 12);
                        if (pSz == 0) {
                            if (cOff + uSz > size) return false;
                            outExtracted->insert(outExtracted->end(), data + static_cast<size_t>(cOff), data + static_cast<size_t>(cOff + uSz));
                        } else {
                            if (cOff + pSz > size) return false;
                            std::vector<uint8_t> chunkDec;
                            const uint8_t* cSrc = data + static_cast<size_t>(cOff);
                            bool ok = ZipDeflate::InflateZlib(cSrc, pSz, chunkDec, 64 * 1024 * 1024) ||
                                      ZipDeflate::InflateRaw(cSrc, pSz, chunkDec, 64 * 1024 * 1024);
                            if (!ok) return false;
                            outExtracted->insert(outExtracted->end(), chunkDec.begin(), chunkDec.end());
                        }
                    }
                    return true;
                }
            } else if (outInfo) {
                ArchiveItemEntry item;
                item.pathInArchive = full;
                item.name = ArchivePath::BaseName(full);
                item.uncompressedSize = sumUnp;
                item.compressedSize   = sumPack;
                item.compressionMethod = 8; // Prepends 148-byte DDS+DX10 header on extraction
                outInfo->entries.push_back(std::move(item));
            }
        }
    }

    if (extractTargetNorm) return false;
    if (outInfo) {
        outInfo->valid = !outInfo->entries.empty();
        return outInfo->valid;
    }
    return false;
}

} // namespace DetailDiskAndGameArc

inline bool InspectBethesdaBa2(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return DetailDiskAndGameArc::ParseOrExtractBethesdaBa2(data, size, &outInfo, nullptr, nullptr);
}

inline bool ExtractBethesdaBa2Entry(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    return DetailDiskAndGameArc::ParseOrExtractBethesdaBa2(data, size, nullptr, &normTarget, &outBytes);
}

} // namespace Explorer2::Formats
