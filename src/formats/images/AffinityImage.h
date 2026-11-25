#pragma once
// AffinityImage.h — Serif Affinity Persona Container (#Inf Trailer + #FAT/#FT2/#FT3/#F4T Piece Table) Thmb Decoder (.afphoto, .afdesign, .afpub)

namespace Formats {

namespace DetailAffinity {

inline uint32_t Rd32LE(const uint8_t* p) noexcept {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

inline uint64_t Rd64LE(const uint8_t* p) noexcept {
    return static_cast<uint64_t>(Rd32LE(p)) | (static_cast<uint64_t>(Rd32LE(p + 4)) << 32);
}

inline bool IsFatMagic(const uint8_t* p) noexcept {
    return std::memcmp(p, "#FAT", 4) == 0 || std::memcmp(p, "TAF#", 4) == 0 ||
           std::memcmp(p, "#FT2", 4) == 0 || std::memcmp(p, "2TF#", 4) == 0 ||
           std::memcmp(p, "#FT3", 4) == 0 || std::memcmp(p, "3TF#", 4) == 0 ||
           std::memcmp(p, "#F4T", 4) == 0 || std::memcmp(p, "T4F#", 4) == 0;
}

inline bool IsThmbTag(const uint8_t* p) noexcept {
    return std::memcmp(p, "Thmb", 4) == 0 || std::memcmp(p, "bmhT", 4) == 0;
}

// Decode a decompressed Serif Persona "Thmb" object payload (contains either direct PNG or Persona serialized PNG field)
inline bool DecodePersonaThmbObject(const uint8_t* obj, size_t objLen, const std::wstring& label, ExtendedImageInfo& outInfo) {
    if (!obj || objLen < 24u) return false;

    // 1. Direct PNG or WIC image at offset 0
    if (DetailPngPure::DecodePngToImageInfo(obj, objLen, label, outInfo)) return true;

    // 2. Persona serialized Thmb struct: small object header (< 256 bytes) preceding PNG signature (89 50 4E 47)
    static constexpr uint8_t kPngSig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    static constexpr uint8_t kIend[8]   = { 'I', 'E', 'N', 'D', 0xAE, 0x42, 0x60, 0x82 };
    const size_t hdrLimit = std::min<size_t>(objLen, 512u);
    for (size_t i = 0; i + 24u <= hdrLimit; ++i) {
        if (std::memcmp(obj + i, kPngSig, 8) == 0) {
            size_t pngLen = objLen - i;
            if (i >= 4u) {
                uint32_t declLen = Rd32LE(obj + i - 4u);
                if (declLen >= 24u && declLen <= objLen - i) pngLen = declLen;
            }
            for (size_t j = i + 16u; j + 8u <= objLen; ++j) {
                if (std::memcmp(obj + j, kIend, 8) == 0) {
                    pngLen = (j + 8u) - i;
                    break;
                }
            }
            if (DetailPngPure::DecodePngToImageInfo(obj + i, pngLen, label, outInfo) ||
                DetailLayeredAndHdrImg::DecodeEmbeddedWicBuffer(obj + i, pngLen, label, outInfo))
            {
                return true;
            }
        }
    }

    return DetailLayeredAndHdrImg::DecodeEmbeddedWicBuffer(obj, objLen, label, outInfo);
}

// Decompress a Serif Persona piece stream (#Fil header or raw Zstd/Zlib/uncompressed slice)
inline bool DecodePersonaPieceSlice(
    const uint8_t* data,
    size_t fileSize,
    uint64_t pieceOff,
    uint64_t pieceLen,
    uint8_t compHint,
    const std::wstring& label,
    ExtendedImageInfo& outInfo)
{
    if (pieceOff >= fileSize || pieceLen < 8u) return false;
    const size_t maxAvail = fileSize - static_cast<size_t>(pieceOff);
    size_t sliceLen = static_cast<size_t>(std::min<uint64_t>(pieceLen, maxAvail));
    const uint8_t* ptr = data + static_cast<size_t>(pieceOff);

    // Serif #Fil piece stream wrapper: ["#Fil" / "liF#" (4B)][compressionMode (1B)][payload...]
    if (sliceLen > 12u && (std::memcmp(ptr, "#Fil", 4) == 0 || std::memcmp(ptr, "liF#", 4) == 0)) {
        compHint = ptr[4];
        ptr += 5u;
        sliceLen -= 5u;
    }

    constexpr size_t kMaxThmbUncomp = 32u * 1024u * 1024u;
    std::vector<uint8_t> decomp;

    // Zstandard frame (Affinity v2+ uses Zstd 28 B5 2F FD; compHint & 3 == 2 or 0x20)
    if (sliceLen >= 4u && ptr[0] == 0x28u && ptr[1] == 0xB5u && ptr[2] == 0x2Fu && ptr[3] == 0xFDu) {
        if (::Explorer2::StreamCodecs::DecompressZstdFrame(ptr, sliceLen, decomp, kMaxThmbUncomp)) {
            return DecodePersonaThmbObject(decomp.data(), decomp.size(), label, outInfo);
        }
        return false;
    }

    // Zlib stream (Affinity v1 uses Zlib; compHint & 3 == 1)
    if (sliceLen >= 6u && (ptr[0] & 0x0Fu) == 8u && (ptr[0] >> 4) <= 7u &&
        ((static_cast<uint32_t>(ptr[0]) << 8) | ptr[1]) % 31u == 0u)
    {
        if (ZipDeflate::InflateZlib(ptr, sliceLen, decomp, kMaxThmbUncomp)) {
            return DecodePersonaThmbObject(decomp.data(), decomp.size(), label, outInfo);
        }
    }

    // Explicit Zstd or Zlib after short piece subheader (e.g. 4-byte or 8-byte uncompressed size prefix)
    for (size_t skip : { size_t(4), size_t(8), size_t(12), size_t(16) }) {
        if (skip + 8u >= sliceLen) break;
        const uint8_t* sp = ptr + skip;
        const size_t sl = sliceLen - skip;
        if (sp[0] == 0x28u && sp[1] == 0xB5u && sp[2] == 0x2Fu && sp[3] == 0xFDu) {
            if (::Explorer2::StreamCodecs::DecompressZstdFrame(sp, sl, decomp, kMaxThmbUncomp) &&
                DecodePersonaThmbObject(decomp.data(), decomp.size(), label, outInfo))
            {
                return true;
            }
        } else if ((sp[0] & 0x0Fu) == 8u && (sp[0] >> 4) <= 7u && ((static_cast<uint32_t>(sp[0]) << 8) | sp[1]) % 31u == 0u) {
            if (ZipDeflate::InflateZlib(sp, sl, decomp, kMaxThmbUncomp) &&
                DecodePersonaThmbObject(decomp.data(), decomp.size(), label, outInfo))
            {
                return true;
            }
        }
    }

    // Uncompressed piece payload (compHint == 0)
    if ((compHint & 0x03u) == 0u) {
        return DecodePersonaThmbObject(ptr, sliceLen, label, outInfo);
    }
    return false;
}

inline bool TryParseFatAndDecodeThmb(
    const uint8_t* fatBuf,
    size_t fatLen,
    const uint8_t* fileData,
    size_t fileSize,
    const std::wstring& label,
    ExtendedImageInfo& outInfo)
{
    if (!fatBuf || fatLen < 24u) return false;

    // Serif FAT table versions (#FAT, #FT2, #FT3, #F4T) store fixed-size piece descriptors containing:
    //   uint64_t dataOffset, uint64_t uncompressedSize, uint64_t compressedSize, uint32_t crc, ... char tag[4] ("Thmb"/"bmhT")
    // Locate the "Thmb" / "bmhT" tag inside the FAT table and inspect the descriptor fields immediately preceding/following it.
    for (size_t p = 4u; p + 4u <= fatLen; ++p) {
        if (!IsThmbTag(fatBuf + p)) continue;

        // Standard #FAT / #FT2 / #FT3 / #F4T layout: [u64 offset][u64 realSz][u64 compSz]...[tag:4][flags:1]
        // Try candidate offsets of `dataOffset` relative to `tag` in all Serif FAT struct revisions:
        static constexpr int kRelOffsets[] = { -32, -28, -36, -24, -40, 4, 8, 12 };
        for (int rel : kRelOffsets) {
            const ptrdiff_t offPos = static_cast<ptrdiff_t>(p) + rel;
            if (offPos < 4 || static_cast<size_t>(offPos) + 16u > fatLen) continue;
            const uint64_t pieceOff = Rd64LE(fatBuf + offPos);
            if (pieceOff < 16u || pieceOff >= fileSize) continue;

            uint64_t pieceSz = fileSize - pieceOff;
            if (static_cast<size_t>(offPos) + 24u <= fatLen) {
                const uint64_t szA = Rd64LE(fatBuf + offPos + 8u);
                const uint64_t szB = Rd64LE(fatBuf + offPos + 16u);
                // Compressed size is <= fileSize - pieceOff and > 0
                if (szB >= 8u && szB <= fileSize - pieceOff) pieceSz = szB + 16u;
                else if (szA >= 8u && szA <= fileSize - pieceOff) pieceSz = szA + 16u;
            }
            const uint8_t compFlag = (p + 4u < fatLen) ? fatBuf[p + 4u] : 0u;
            if (DecodePersonaPieceSlice(fileData, fileSize, pieceOff, pieceSz, compFlag, label, outInfo)) {
                return true;
            }
        }

        // 32-bit FAT entry variant: [u32 offset][u32 compSz][u32 realSz][tag:4] or [tag:4][u32 offset][u32 compSz]
        if (p >= 12u) {
            const uint32_t off32 = Rd32LE(fatBuf + p - 12u);
            const uint32_t sz32  = Rd32LE(fatBuf + p - 8u);
            if (off32 >= 16u && off32 < fileSize && sz32 >= 8u && DecodePersonaPieceSlice(fileData, fileSize, off32, sz32 + 16u, 0u, label, outInfo)) {
                return true;
            }
        }
        if (p + 12u <= fatLen) {
            const uint32_t off32 = Rd32LE(fatBuf + p + 4u);
            const uint32_t sz32  = Rd32LE(fatBuf + p + 8u);
            if (off32 >= 16u && off32 < fileSize && sz32 >= 8u && DecodePersonaPieceSlice(fileData, fileSize, off32, sz32 + 16u, 0u, label, outInfo)) {
                return true;
            }
        }
    }
    return false;
}

} // namespace DetailAffinity

inline bool DecodeAffinityImage(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    using namespace DetailAffinity;
    if (!data || size < 24u) return false;
    const bool hasAffinityMagic = (data[0] == 0x00u && data[1] == 0xFFu && data[2] == 0x4Bu && data[3] == 0x41u);
    const bool hasZipMagic      = (data[0] == 'P' && data[1] == 'K' && (data[2] == 3u || data[2] == 5u));
    if (extLower != L".afphoto" && extLower != L".afdesign" && extLower != L".afpub" && !hasAffinityMagic) {
        return false;
    }
    if (!hasAffinityMagic && !hasZipMagic) return false;

    const std::wstring label = (extLower == L".afdesign") ? L"Affinity Designer (.afdesign)" :
                               (extLower == L".afpub")    ? L"Affinity Publisher (.afpub)" :
                                                            L"Affinity Photo (.afphoto)";

    if (hasZipMagic) {
        return DetailVectorAndRawImg::TryDecodeBestZipThumbnail(data, size, label, outInfo);
    }

    // 1. Parse Serif Persona revision trailer ("#Inf" / "fnI#") in the final 512 bytes of the container:
    //    [#Inf:4][fatOffset:u64LE][fatLength:u64LE]...
    const size_t trailerStart = (size > 512u) ? (size - 512u) : 8u;
    for (size_t t = size - 20u; t >= trailerStart; --t) {
        if (std::memcmp(data + t, "#Inf", 4) == 0 || std::memcmp(data + t, "fnI#", 4) == 0) {
            const uint64_t fatOff = Rd64LE(data + t + 4u);
            const uint64_t fatLen = Rd64LE(data + t + 12u);
            if (fatOff >= 16u && fatOff < size && fatLen >= 16u && fatLen <= size - fatOff) {
                const uint8_t* fp = data + static_cast<size_t>(fatOff);
                const size_t fl = static_cast<size_t>(fatLen);
                // FAT stream itself may be compressed (#FT3 / #F4T with Zlib or Zstd after 8/12/16B header) or uncompressed
                if (TryParseFatAndDecodeThmb(fp, fl, data, size, label, outInfo)) return true;
                for (size_t hdrSz : { size_t(0), size_t(4), size_t(8), size_t(12), size_t(16), size_t(20) }) {
                    if (hdrSz + 8u >= fl) break;
                    std::vector<uint8_t> fatDec;
                    const uint8_t* cp = fp + hdrSz;
                    const size_t cl = fl - hdrSz;
                    if (cp[0] == 0x28u && cp[1] == 0xB5u && cp[2] == 0x2Fu && cp[3] == 0xFDu) {
                        if (::Explorer2::StreamCodecs::DecompressZstdFrame(cp, cl, fatDec, 16 * 1024 * 1024) &&
                            TryParseFatAndDecodeThmb(fatDec.data(), fatDec.size(), data, size, label, outInfo))
                        {
                            return true;
                        }
                    } else if ((cp[0] & 0x0Fu) == 8u && (cp[0] >> 4) <= 7u && ((static_cast<uint32_t>(cp[0]) << 8) | cp[1]) % 31u == 0u) {
                        if (ZipDeflate::InflateZlib(cp, cl, fatDec, 16 * 1024 * 1024) &&
                            TryParseFatAndDecodeThmb(fatDec.data(), fatDec.size(), data, size, label, outInfo))
                        {
                            return true;
                        }
                    }
                }
            }
        }
        if (t == trailerStart) break;
    }

    // 2. Check header pointer offsets (header[16..23] or header[24..31] in older Persona revisions) for #FAT/#FT2/#FT3/#F4T
    for (size_t hp : { size_t(8), size_t(12), size_t(16), size_t(20), size_t(24), size_t(32) }) {
        if (hp + 8u > size) break;
        const uint64_t candFat = Rd64LE(data + hp);
        if (candFat >= 16u && candFat + 24u <= size && IsFatMagic(data + static_cast<size_t>(candFat))) {
            const size_t rem = size - static_cast<size_t>(candFat);
            if (TryParseFatAndDecodeThmb(data + static_cast<size_t>(candFat), rem, data, size, label, outInfo)) {
                return true;
            }
        }
    }

    // 3. Single-revision compact Persona header where "#FAT" or "#Fil"/"Thmb" piece starts right after the 16..128B file header
    const size_t headScanLim = std::min<size_t>(size, 256u);
    for (size_t p = 8u; p + 12u <= headScanLim; ++p) {
        if (IsFatMagic(data + p)) {
            if (TryParseFatAndDecodeThmb(data + p, size - p, data, size, label, outInfo)) return true;
        }
        if (IsThmbTag(data + p)) {
            if (DecodePersonaPieceSlice(data, size, p + 4u, size - (p + 4u), 0u, label, outInfo)) return true;
        }
        if (std::memcmp(data + p, "#Fil", 4) == 0 || std::memcmp(data + p, "liF#", 4) == 0) {
            if (DecodePersonaPieceSlice(data, size, p, size - p, data[p + 4u], label, outInfo)) return true;
        }
    }

    return false;
}

} // namespace Formats
