#pragma once
// Source2VtexC.h — Valve Source 2 Compiled Texture Resource Decoder (.vtex_c)

namespace Formats {

// ============================================================================
// 6. Valve Source 2 Compiled Texture (.vtex_c — ResourceHeader v12 + "DATA" + VTexExtraData)
// ============================================================================
inline bool DecodeSource2VtexC(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 68) return false;
    const uint32_t hdrFileSize = ConsoleTexDetail::RdU32LE(data + 0);
    const uint16_t hdrVersion  = ConsoleTexDetail::RdU16LE(data + 4);
    const uint32_t blockRelOff = ConsoleTexDetail::RdU32LE(data + 8);
    const uint32_t blockCount  = ConsoleTexDetail::RdU32LE(data + 12);
    (void)hdrFileSize;

    if (hdrVersion != 12u || blockCount == 0u || blockCount > 64u) return false;
    const uint64_t blockDirOff = 8ull + blockRelOff;
    if (blockDirOff > size || static_cast<uint64_t>(blockCount) * 12ull > size - blockDirOff) return false;

    size_t dataBlkOff = SIZE_MAX, dataBlkSz = 0, maxBlkEnd = static_cast<size_t>(blockDirOff + blockCount * 12u);
    for (uint32_t i = 0; i < blockCount; ++i) {
        const size_t entPos = static_cast<size_t>(blockDirOff) + static_cast<size_t>(i) * 12u;
        const int32_t rel   = ConsoleTexDetail::RdI32LE(data + entPos + 4);
        const uint32_t bsz  = ConsoleTexDetail::RdU32LE(data + entPos + 8);
        const int64_t absOff = static_cast<int64_t>(entPos + 4u) + rel;
        if (absOff < 16 || static_cast<uint64_t>(absOff) > size || bsz > size - static_cast<size_t>(absOff)) {
            return false;
        }
        maxBlkEnd = std::max(maxBlkEnd, static_cast<size_t>(absOff) + bsz);
        if (std::memcmp(data + entPos, "DATA", 4) == 0) {
            dataBlkOff = static_cast<size_t>(absOff);
            dataBlkSz  = bsz;
        }
    }
    if (dataBlkOff == SIZE_MAX || dataBlkSz < 40u) return false;

    const uint8_t* vtex = data + dataBlkOff;
    const uint16_t vtexVer  = ConsoleTexDetail::RdU16LE(vtex + 0);
    const uint16_t w        = ConsoleTexDetail::RdU16LE(vtex + 20);
    const uint16_t h        = ConsoleTexDetail::RdU16LE(vtex + 22);
    const uint16_t depth    = ConsoleTexDetail::RdU16LE(vtex + 24);
    const uint8_t  vtexFmt  = vtex[26];
    const uint8_t  numMips  = vtex[27];
    const int32_t  extraRel = ConsoleTexDetail::RdI32LE(vtex + 32);
    const uint32_t extraCnt = ConsoleTexDetail::RdU32LE(vtex + 36);
    (void)depth;

    if (vtexVer != 1u || w == 0 || h == 0 || w > 16384u || h > 16384u || numMips == 0 || numMips > 16u) return false;

    // Parse VTexExtraData array (12 bytes each: [u32 type, i32 offsetRelToField, u32 size])
    // VTEX_EXTRA_DATA_COMPRESSED_MIP_SIZE == 4:
    //   [u32 compressedFlag, i32 mipCodec (8=LZ4, 9=Zstd), u32 mipCount, i32 mipByteSizes[mipCount]]
    bool hasCompMipTable = false;
    bool mip0IsCompressed = false;
    int32_t mipCompCodec = 0;
    uint64_t mip0StoredBytes = 0;
    uint64_t totalStoredMipBytes = 0;

    if (extraCnt > 0u && extraCnt <= 32u && extraRel != 0) {
        const int64_t extraDirAbs = static_cast<int64_t>(dataBlkOff + 32u) + extraRel;
        if (extraDirAbs >= static_cast<int64_t>(dataBlkOff) &&
            static_cast<uint64_t>(extraDirAbs) <= size &&
            static_cast<uint64_t>(extraCnt) * 12ull <= size - static_cast<size_t>(extraDirAbs))
        {
            for (uint32_t i = 0; i < extraCnt; ++i) {
                const size_t ep = static_cast<size_t>(extraDirAbs) + static_cast<size_t>(i) * 12u;
                const uint32_t eType = ConsoleTexDetail::RdU32LE(data + ep + 0);
                const int32_t  eRel  = ConsoleTexDetail::RdI32LE(data + ep + 4);
                const uint32_t eSize = ConsoleTexDetail::RdU32LE(data + ep + 8);
                const int64_t  eAbs  = static_cast<int64_t>(ep + 4u) + eRel;
                if (eAbs < 0 || static_cast<uint64_t>(eAbs) > size || eSize > size - static_cast<size_t>(eAbs)) continue;
                maxBlkEnd = std::max(maxBlkEnd, static_cast<size_t>(eAbs) + eSize);

                if (eType == 4u && eSize >= 16u) { // VTEX_EXTRA_DATA_COMPRESSED_MIP_SIZE
                    const uint8_t* cm = data + static_cast<size_t>(eAbs);
                    const uint32_t compFlag = ConsoleTexDetail::RdU32LE(cm + 0);
                    const int32_t  mCodec   = ConsoleTexDetail::RdI32LE(cm + 4);
                    const uint32_t mCount   = ConsoleTexDetail::RdU32LE(cm + 8);
                    if (mCount >= 1u && mCount <= 16u && 12ull + static_cast<uint64_t>(mCount) * 4ull <= eSize) {
                        const int32_t sz0 = ConsoleTexDetail::RdI32LE(cm + 12);
                        if (sz0 > 0) {
                            hasCompMipTable = true;
                            mip0IsCompressed = (compFlag != 0u);
                            mipCompCodec = mCodec;
                            mip0StoredBytes = static_cast<uint64_t>(sz0);
                            totalStoredMipBytes = 0;
                            for (uint32_t m = 0; m < mCount; ++m) {
                                int32_t sm = ConsoleTexDetail::RdI32LE(cm + 12 + m * 4u);
                                if (sm > 0) totalStoredMipBytes += static_cast<uint64_t>(sm);
                            }
                        }
                    }
                }
            }
        }
    }

    size_t streamOff = maxBlkEnd;
    if (streamOff >= size) {
        if (dataBlkOff + 40u < size) streamOff = dataBlkOff + 40u;
        else return false;
    }

    // Embedded PNG formats (16 = PNG_RGBA8888, 18 = PNG_DXT5, and 10 = JPEG_RGBA8888, 15 = JPEG_DXT5)
    if (vtexFmt == 16u || vtexFmt == 18u || vtexFmt == 10u || vtexFmt == 15u) {
        size_t imgPos = streamOff;
        size_t imgLen = size - streamOff;
        if (hasCompMipTable && mip0StoredBytes > 0 && mip0StoredBytes <= size - streamOff) {
            imgPos = size - static_cast<size_t>(mip0StoredBytes);
            imgLen = static_cast<size_t>(mip0StoredBytes);
        }
        std::vector<uint8_t> decompPng;
        const uint8_t* payPtr = data + imgPos;
        size_t payLen = imgLen;
        if (mip0IsCompressed && payLen >= 4u && std::memcmp(payPtr, "\x89PNG\r\n\x1A\n", 8) != 0) {
            if (payPtr[0] == 0x28u && payPtr[1] == 0xB5u && payPtr[2] == 0x2Fu && payPtr[3] == 0xFDu) {
                if (::Explorer2::StreamCodecs::DecompressZstdFrame(payPtr, payLen, decompPng, 64 * 1024 * 1024)) {
                    payPtr = decompPng.data();
                    payLen = decompPng.size();
                }
            } else if (::Explorer2::Lz4::DecodeBlockAppend(payPtr, payLen, decompPng, 0, 64 * 1024 * 1024, false) && !decompPng.empty()) {
                payPtr = decompPng.data();
                payLen = decompPng.size();
            }
        }
        for (size_t p = 0; p + 24u <= payLen; ++p) {
            if ((payPtr[p] == 0x89u && std::memcmp(payPtr + p, "\x89PNG\r\n\x1A\n", 8) == 0) ||
                (payPtr[p] == 0xFFu && payPtr[p + 1] == 0xD8u && payPtr[p + 2] == 0xFFu))
            {
                if (DetailPngPure::DecodePngToImageInfo(payPtr + p, payLen - p, L"Valve Source 2 Texture (.vtex_c)", outInfo) ||
                    DetailLayeredAndHdrImg::DecodeEmbeddedWicBuffer(payPtr + p, payLen - p, L"Valve Source 2 Texture (.vtex_c)", outInfo))
                {
                    outInfo.origWidth = outInfo.width;
                    outInfo.origHeight = outInfo.height;
                    return true;
                }
            }
        }
        return false;
    }

    ConsoleTexDetail::GpuSurfaceCodec codec = ConsoleTexDetail::GpuSurfaceCodec::None;
    switch (vtexFmt) {
        case 1:  codec = ConsoleTexDetail::GpuSurfaceCodec::BC1; break;
        case 2:  codec = ConsoleTexDetail::GpuSurfaceCodec::BC3; break;
        case 3:  codec = ConsoleTexDetail::GpuSurfaceCodec::R8; break;
        case 4:  codec = ConsoleTexDetail::GpuSurfaceCodec::RGBA8; break;
        case 5:  codec = ConsoleTexDetail::GpuSurfaceCodec::RG8; break;
        case 11: codec = ConsoleTexDetail::GpuSurfaceCodec::RGBA16F; break;
        case 20: codec = ConsoleTexDetail::GpuSurfaceCodec::BC7; break;
        case 21: codec = ConsoleTexDetail::GpuSurfaceCodec::BC6H_U; break;
        case 22: codec = ConsoleTexDetail::GpuSurfaceCodec::BC4_U; break;
        case 23: codec = ConsoleTexDetail::GpuSurfaceCodec::BC5_U; break;
        case 27: codec = ConsoleTexDetail::GpuSurfaceCodec::ETC2_RGB; break;
        case 28: codec = ConsoleTexDetail::GpuSurfaceCodec::ETC2_RGBA; break;
        case 29: codec = ConsoleTexDetail::GpuSurfaceCodec::BGRA8; break;
        default: return false;
    }

    uint32_t blkW = 1, blkH = 1, bpb = 4;
    ConsoleTexDetail::GetCodecBlockLayout(codec, blkW, blkH, bpb);
    const uint64_t mip0RawBytes = static_cast<uint64_t>((w + blkW - 1u) / blkW) * ((h + blkH - 1u) / blkH) * bpb;
    if (mip0RawBytes == 0 || mip0RawBytes > 256ull * 1024ull * 1024ull) return false;

    const uint8_t* mip0SrcPtr = nullptr;
    std::vector<uint8_t> decompressedMip0;

    if (hasCompMipTable && mip0StoredBytes > 0) {
        if (mip0StoredBytes > size - streamOff) {
            if (mip0StoredBytes > size - (dataBlkOff + 40u)) return false;
            streamOff = dataBlkOff + 40u;
        }
        // Source 2 stores mips smallest-to-largest at the tail of the streaming data section: mip 0 is last!
        size_t mip0Pos = size - static_cast<size_t>(mip0StoredBytes);
        if (totalStoredMipBytes <= size - streamOff && streamOff + static_cast<size_t>(totalStoredMipBytes) <= size) {
            // If file has no trailing padding, streamOff + totalStoredMipBytes == size; otherwise anchor to streamOff + (total - mip0)
            mip0Pos = streamOff + static_cast<size_t>(totalStoredMipBytes - mip0StoredBytes);
            if (size - static_cast<size_t>(mip0StoredBytes) >= streamOff && streamOff + totalStoredMipBytes < size) {
                // Prefer tail alignment if header blocks precede streaming mips with alignment pad
                mip0Pos = size - static_cast<size_t>(mip0StoredBytes);
            }
        }
        const uint8_t* rawSlice = data + mip0Pos;
        const size_t rawSliceLen = static_cast<size_t>(mip0StoredBytes);

        const bool shouldDecompress = mip0IsCompressed && (mip0StoredBytes != mip0RawBytes ||
            (rawSliceLen >= 4u && rawSlice[0] == 0x28u && rawSlice[1] == 0xB5u && rawSlice[2] == 0x2Fu && rawSlice[3] == 0xFDu));
        if (shouldDecompress) {
            const bool isZstd = (mipCompCodec == 9) ||
                                (rawSliceLen >= 4u && rawSlice[0] == 0x28u && rawSlice[1] == 0xB5u && rawSlice[2] == 0x2Fu && rawSlice[3] == 0xFDu);
            if (isZstd) {
                if (!::Explorer2::StreamCodecs::DecompressZstdFrame(rawSlice, rawSliceLen, decompressedMip0, static_cast<size_t>(mip0RawBytes) + 4096u) ||
                    decompressedMip0.size() < mip0RawBytes)
                {
                    return false;
                }
            } else {
                if (!::Explorer2::Lz4::DecodeBlockExact(rawSlice, rawSliceLen, static_cast<size_t>(mip0RawBytes), decompressedMip0)) {
                    if (!::Explorer2::StreamCodecs::DecompressLz4Frame(rawSlice, rawSliceLen, decompressedMip0, static_cast<size_t>(mip0RawBytes) + 4096u) ||
                        decompressedMip0.size() < mip0RawBytes)
                    {
                        return false;
                    }
                }
            }
            mip0SrcPtr = decompressedMip0.data();
        } else {
            if (rawSliceLen < mip0RawBytes) return false;
            mip0SrcPtr = rawSlice;
        }
    } else {
        if (mip0RawBytes > size - streamOff) return false;
        size_t mip0Pos = size - static_cast<size_t>(mip0RawBytes);
        if (mip0Pos < streamOff) mip0Pos = streamOff;
        mip0SrcPtr = data + mip0Pos;
    }

    if (!ConsoleTexDetail::DecodeLinearGpuSurface(mip0SrcPtr, static_cast<size_t>(mip0RawBytes), w, h, codec, outInfo.bgraPixels)) {
        return false;
    }

    outInfo.width = w;
    outInfo.height = h;
    outInfo.origWidth = w;
    outInfo.origHeight = h;
    outInfo.channels = 4u;
    outInfo.bitsPerChannel = (codec == ConsoleTexDetail::GpuSurfaceCodec::RGBA16F ||
                              codec == ConsoleTexDetail::GpuSurfaceCodec::BC6H_U) ? 16u : 8u;
    outInfo.formatName = L"Valve Source 2 Compiled Texture (.vtex_c)";
    outInfo.valid = true;
    return true;
}

} // namespace Formats
