#pragma once
// PspGimImage.h — Sony PlayStation Portable Graphic Image Map (.gim — "MIG.00.1PSP\0" / ".GIM1.00\0PSP")
// Parses hierarchical Root (0x0002) / Picture (0x0003) / Image (0x0004) / Palette (0x0005) chunks,
// PSP GE 16x8 hardware block unswizzling (pixelOrder == 1) for both direct/indexed and DXT1/DXT3/DXT5 textures.

namespace Formats {

namespace DetailPspGim {

struct GimView {
    const uint8_t* data = nullptr;
    size_t size = 0;
    bool bigEndian = false;

    uint16_t U16(size_t off) const {
        return bigEndian ? ByteIO::RdU16BE(data + off) : ByteIO::RdU16LE(data + off);
    }
    uint32_t U32(size_t off) const {
        return bigEndian ? ByteIO::RdU32BE(data + off) : ByteIO::RdU32LE(data + off);
    }
};

struct GimParsedSurf {
    bool present = false;
    uint16_t format = 0;
    uint16_t order  = 0;
    uint16_t width  = 0;
    uint16_t height = 0;
    uint16_t bpp    = 0;
    uint16_t pitchAlign  = 1;
    uint16_t heightAlign = 1;
    size_t pixelAbsOff   = 0;
    size_t pixelAvail    = 0;
};

inline bool ParseGimSurfChunk(const GimView& v, size_t chunkOff, size_t chunkEnd, GimParsedSurf& out) {
    if (chunkEnd <= chunkOff || chunkEnd - chunkOff < 0x10u + 0x30u) return false;
    const uint32_t dataRel = v.U32(chunkOff + 0x0C);
    if (dataRel < 0x10u || dataRel > chunkEnd - chunkOff - 0x30u) return false;

    const size_t base = chunkOff + dataRel;
    const size_t rem  = chunkEnd - base;

    const uint16_t hdrSize     = v.U16(base + 0x00);
    const uint16_t rawFmt      = v.U16(base + 0x04);
    const uint16_t imgFmt      = ((rawFmt & 0xFF00u) == 0x0100u) ? static_cast<uint16_t>(rawFmt & 0x00FFu) : rawFmt;
    const uint16_t pxOrder     = v.U16(base + 0x06);
    const uint16_t width       = v.U16(base + 0x08);
    const uint16_t height      = v.U16(base + 0x0A);
    const uint16_t bpp         = v.U16(base + 0x0C);
    const uint16_t pitchAlign  = std::max<uint16_t>(1u, v.U16(base + 0x0E));
    const uint16_t heightAlign = std::max<uint16_t>(1u, v.U16(base + 0x10));
    const uint32_t offsetsOff  = v.U32(base + 0x18);
    const uint32_t imagesOff   = v.U32(base + 0x1C);

    if (hdrSize < 0x30u || hdrSize > rem || width == 0u || height == 0u ||
        width > 4096u || height > 4096u || pxOrder > 1u) {
        return false;
    }

    size_t pixRel = 0;
    if (offsetsOff >= hdrSize && offsetsOff <= rem && rem - offsetsOff >= 4u) {
        pixRel = v.U32(base + offsetsOff);
    } else if (imagesOff >= hdrSize && imagesOff < rem) {
        pixRel = imagesOff;
    } else {
        return false;
    }
    if (pixRel < hdrSize || pixRel >= rem) return false;

    out.present     = true;
    out.format      = imgFmt;
    out.order       = pxOrder;
    out.width       = width;
    out.height      = height;
    out.bpp         = bpp;
    out.pitchAlign  = std::min<uint16_t>(pitchAlign, 256u);
    out.heightAlign = std::min<uint16_t>(heightAlign, 64u);
    out.pixelAbsOff = base + pixRel;
    out.pixelAvail  = rem - pixRel;
    return true;
}

inline void WalkGimChunks(
    const GimView& v,
    size_t startOff,
    size_t endOff,
    int depth,
    GimParsedSurf& ioImg,
    GimParsedSurf& ioPal)
{
    if (depth > 6) return;
    size_t pos = startOff;
    size_t guard = 0;
    while (pos + 16u <= endOff && guard++ < 256u) {
        const uint16_t blockId   = v.U16(pos + 0x00);
        const uint32_t blockSize = v.U32(pos + 0x04);
        const uint32_t nextOff   = v.U32(pos + 0x08);
        const uint32_t childOff  = v.U32(pos + 0x0C);

        const uint32_t step = (nextOff >= 16u) ? nextOff : blockSize;
        if (step < 16u || blockSize < 16u) break;
        const size_t blkEnd = std::min<size_t>(endOff, pos + std::min<uint64_t>(blockSize, endOff - pos));

        if (blockId == 0x0002u || blockId == 0x0003u) {
            if (childOff >= 16u && childOff < blkEnd - pos) {
                WalkGimChunks(v, pos + childOff, blkEnd, depth + 1, ioImg, ioPal);
            }
        } else if (blockId == 0x0004u && !ioImg.present) {
            (void)ParseGimSurfChunk(v, pos, blkEnd, ioImg);
        } else if (blockId == 0x0005u && !ioPal.present) {
            (void)ParseGimSurfChunk(v, pos, blkEnd, ioPal);
        }

        if (step > endOff - pos) break;
        pos += step;
    }
}

inline void UnpackGimDirectColor(
    uint16_t fmt,
    const uint8_t* p,
    bool bigEndian,
    uint8_t& r,
    uint8_t& g,
    uint8_t& b,
    uint8_t& a)
{
    if (fmt == 3u) {
        r = p[0]; g = p[1]; b = p[2]; a = p[3];
        return;
    }
    const uint16_t v = bigEndian ? ByteIO::RdU16BE(p) : ByteIO::RdU16LE(p);
    if (fmt == 0u) {
        ConsolePx::UnpackBgr565(v, r, g, b);
        a = 255u;
    } else if (fmt == 1u) {
        ConsolePx::UnpackAbgr1555(v, r, g, b, a);
    } else {
        ConsolePx::UnpackAbgr4444(v, r, g, b, a);
    }
}

inline bool UnswizzlePspGePlane(
    const uint8_t* src,
    size_t srcAvail,
    uint64_t rawRowBytes,
    uint64_t numRows,
    uint16_t order,
    uint16_t pitchAlign,
    uint16_t heightAlign,
    size_t& outStrideBytes,
    std::vector<uint8_t>& outLinear)
{
    const uint64_t alignP = (order == 1u) ? std::max<uint64_t>(16ULL, pitchAlign) : std::max<uint64_t>(1ULL, pitchAlign);
    const uint64_t stride = ((rawRowBytes + alignP - 1ULL) / alignP) * alignP;
    const uint64_t alignH = (order == 1u) ? std::max<uint64_t>(8ULL, heightAlign) : 1ULL;
    const uint64_t paddedH = ((numRows + alignH - 1ULL) / alignH) * alignH;

    const uint64_t needBytes = (order == 1u) ? (stride * paddedH) : (stride * (numRows - 1ULL) + rawRowBytes);
    if (stride == 0ULL || numRows == 0ULL || needBytes > srcAvail || stride * paddedH > 128ULL * 1024ULL * 1024ULL) {
        return false;
    }

    outStrideBytes = static_cast<size_t>(stride);
    outLinear.assign(static_cast<size_t>(stride * paddedH), 0u);

    if (order == 0u) {
        std::memcpy(outLinear.data(), src, static_cast<size_t>(needBytes));
        return true;
    }

    // PSP GE 16-byte x 8-row hardware block unswizzle
    const size_t blocksX = outStrideBytes / 16u;
    const size_t blocksY = static_cast<size_t>(paddedH) / 8u;
    size_t srcPos = 0;
    for (size_t by = 0; by < blocksY; ++by) {
        for (size_t bx = 0; bx < blocksX; ++bx) {
            for (size_t ry = 0; ry < 8u; ++ry) {
                const size_t dstRow = by * 8u + ry;
                std::memcpy(&outLinear[dstRow * outStrideBytes + bx * 16u], src + srcPos, 16u);
                srcPos += 16u;
            }
        }
    }
    return true;
}

inline bool ExtractLinearGimBuffer(
    const uint8_t* src,
    size_t srcAvail,
    const GimParsedSurf& s,
    size_t& outStrideBytes,
    std::vector<uint8_t>& outLinear)
{
    uint32_t expectedBpp = 0u;
    if (s.format <= 2u) expectedBpp = 16u;
    else if (s.format == 3u) expectedBpp = 32u;
    else if (s.format == 4u) expectedBpp = 4u;
    else if (s.format == 5u) expectedBpp = 8u;
    else if (s.format == 6u) expectedBpp = 16u;
    else if (s.format == 7u) expectedBpp = 32u;
    else return false;

    if (s.bpp != 0u && s.bpp != expectedBpp) return false;
    const uint64_t rawRowBytes = (static_cast<uint64_t>(s.width) * expectedBpp + 7ULL) / 8ULL;
    return UnswizzlePspGePlane(src, srcAvail, rawRowBytes, s.height, s.order, s.pitchAlign, s.heightAlign, outStrideBytes, outLinear);
}

} // namespace DetailPspGim

inline bool DecodePspGim(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 64u) return false;

    DetailPspGim::GimView view{ data, size, false };
    if (std::memcmp(data, "MIG.00.1PSP\0", 12) == 0) {
        view.bigEndian = false;
    } else if (std::memcmp(data, ".GIM1.00\0PSP", 12) == 0) {
        view.bigEndian = true;
    } else {
        return false;
    }

    DetailPspGim::GimParsedSurf img{}, pal{};
    DetailPspGim::WalkGimChunks(view, 16u, size, 0, img, pal);
    if (!img.present) return false;
    if (static_cast<uint64_t>(img.width) * img.height > 64ULL * 1024ULL * 1024ULL) return false;

    // Official SCE_GIM DXT formats: 0x08 = DXT1, 0x09 = DXT3, 0x0A = DXT5 (plus legacy 0x07 when no palette chunk exists)
    const bool isDxt1 = (img.format == 8u) || (img.format == 7u && !pal.present);
    const bool isDxt3 = (img.format == 9u);
    const bool isDxt5 = (img.format == 10u);
    const bool isDxt  = (isDxt1 || isDxt3 || isDxt5);
    const bool isIndexed = (!isDxt && img.format >= 4u && img.format <= 7u);
    if (!isDxt && img.format > 7u) return false;

    std::array<std::array<uint8_t, 4>, 256> lut{};
    if (isIndexed) {
        if (!pal.present || pal.format > 3u) return false;
        size_t palStride = 0;
        std::vector<uint8_t> palBuf;
        if (!DetailPspGim::ExtractLinearGimBuffer(
                data + pal.pixelAbsOff, pal.pixelAvail, pal, palStride, palBuf)) {
            return false;
        }
        const size_t bytesPerCol = (pal.format == 3u) ? 4u : 2u;
        const size_t numCols = std::min<size_t>(256u, std::min<size_t>(pal.width, palBuf.size() / bytesPerCol));
        if (numCols == 0u) return false;
        for (size_t i = 0; i < numCols; ++i) {
            DetailPspGim::UnpackGimDirectColor(
                pal.format, &palBuf[i * bytesPerCol], view.bigEndian,
                lut[i][0], lut[i][1], lut[i][2], lut[i][3]);
        }
    }

    outInfo = {};
    outInfo.width = img.width;
    outInfo.height = img.height;
    outInfo.origWidth = img.width;
    outInfo.origHeight = img.height;
    outInfo.channels = 4u;
    outInfo.bitsPerChannel = 8u;
    outInfo.bgraPixels.assign(static_cast<size_t>(img.width) * img.height * 4u, 0u);

    const wchar_t* fmtLabel = L"RGBA8888";
    if (isDxt) {
        fmtLabel = isDxt1 ? L"DXT1" : (isDxt3 ? L"DXT3" : L"DXT5");
        const size_t blockBytes = isDxt1 ? 8u : 16u;
        const uint32_t bxCnt = (img.width  + 3u) / 4u;
        const uint32_t byCnt = (img.height + 3u) / 4u;
        const uint64_t rawBlockRowBytes = static_cast<uint64_t>(bxCnt) * blockBytes;

        size_t blockRowStride = static_cast<size_t>(rawBlockRowBytes);
        std::vector<uint8_t> unswizzledBlocks;
        const uint8_t* blockPlane = data + img.pixelAbsOff;

        if (img.order == 1u) {
            // PSP GE swizzles DXT textures in 16-byte x 8-row tiles across the compressed block stream.
            // Note: PSP GE treats each 4-pixel-tall DXT row as 2 or 4 "scanlines" depending on GimConv pitch:
            // check both (rawBlockRowBytes x byCnt) and ((rawBlockRowBytes / 4) x img.height) layouts cleanly!
            const uint64_t pixelEquivRowBytes = rawBlockRowBytes / 4u;
            if (pixelEquivRowBytes >= 16u && (rawBlockRowBytes % 4u) == 0u &&
                pixelEquivRowBytes * ((static_cast<uint64_t>(img.height) + 7u) & ~7ull) <= img.pixelAvail &&
                rawBlockRowBytes * ((static_cast<uint64_t>(byCnt) + 7u) & ~7ull) > img.pixelAvail)
            {
                size_t subStride = 0;
                if (!DetailPspGim::UnswizzlePspGePlane(
                        blockPlane, img.pixelAvail, pixelEquivRowBytes, byCnt * 4u,
                        1u, img.pitchAlign, img.heightAlign, subStride, unswizzledBlocks))
                {
                    return false;
                }
                blockRowStride = subStride * 4u;
            } else {
                if (!DetailPspGim::UnswizzlePspGePlane(
                        blockPlane, img.pixelAvail, rawBlockRowBytes, byCnt,
                        1u, 16u, 8u, blockRowStride, unswizzledBlocks))
                {
                    return false;
                }
            }
            blockPlane = unswizzledBlocks.data();
        } else {
            if (rawBlockRowBytes * byCnt > img.pixelAvail) return false;
        }

        uint8_t blockRgba[64]{};
        for (uint32_t by = 0; by < byCnt; ++by) {
            const uint8_t* rowPtr = blockPlane + static_cast<size_t>(by) * blockRowStride;
            for (uint32_t bx = 0; bx < bxCnt; ++bx) {
                const uint8_t* sp = rowPtr + static_cast<size_t>(bx) * blockBytes;
                if (isDxt1)      GpuTex::DecodeBC1Block(sp, blockRgba);
                else if (isDxt3) GpuTex::DecodeBC2Block(sp, blockRgba);
                else             GpuTex::DecodeBC3Block(sp, blockRgba);
                GpuTex::BlitBlock4x4RgbaToPremulBgra(blockRgba, bx, by, img.width, img.height, outInfo.bgraPixels);
            }
        }
    } else {
        size_t stride = 0;
        std::vector<uint8_t> lin;
        if (!DetailPspGim::ExtractLinearGimBuffer(
                data + img.pixelAbsOff, img.pixelAvail, img, stride, lin)) {
            return false;
        }
        switch (img.format) {
        case 0u: fmtLabel = L"RGBA5650"; break;
        case 1u: fmtLabel = L"RGBA5551"; break;
        case 2u: fmtLabel = L"RGBA4444"; break;
        case 3u: fmtLabel = L"RGBA8888"; break;
        case 4u: fmtLabel = L"INDEX4";   break;
        case 5u: fmtLabel = L"INDEX8";   break;
        case 6u: fmtLabel = L"INDEX16";  break;
        case 7u: fmtLabel = L"INDEX32";  break;
        default: break;
        }

        for (uint32_t y = 0; y < img.height; ++y) {
            const uint8_t* row = &lin[static_cast<size_t>(y) * stride];
            uint8_t* dstRow = &outInfo.bgraPixels[static_cast<size_t>(y) * img.width * 4u];
            for (uint32_t x = 0; x < img.width; ++x) {
                uint8_t r = 0, g = 0, b = 0, a = 255;
                if (img.format <= 2u) {
                    DetailPspGim::UnpackGimDirectColor(img.format, row + x * 2u, view.bigEndian, r, g, b, a);
                } else if (img.format == 3u) {
                    DetailPspGim::UnpackGimDirectColor(3u, row + x * 4u, view.bigEndian, r, g, b, a);
                } else if (img.format == 4u) {
                    const uint8_t byte = row[x >> 1];
                    const uint8_t nib  = ((x & 1u) == 0u) ? (byte & 0x0Fu) : ((byte >> 4u) & 0x0Fu);
                    r = lut[nib][0]; g = lut[nib][1]; b = lut[nib][2]; a = lut[nib][3];
                } else if (img.format == 5u) {
                    const uint8_t idx = row[x];
                    r = lut[idx][0]; g = lut[idx][1]; b = lut[idx][2]; a = lut[idx][3];
                } else if (img.format == 6u) {
                    const uint8_t idx = static_cast<uint8_t>((view.bigEndian ? ByteIO::RdU16BE(row + x * 2u) : ByteIO::RdU16LE(row + x * 2u)) & 0xFFu);
                    r = lut[idx][0]; g = lut[idx][1]; b = lut[idx][2]; a = lut[idx][3];
                } else {
                    const uint8_t idx = static_cast<uint8_t>((view.bigEndian ? ByteIO::RdU32BE(row + x * 4u) : ByteIO::RdU32LE(row + x * 4u)) & 0xFFu);
                    r = lut[idx][0]; g = lut[idx][1]; b = lut[idx][2]; a = lut[idx][3];
                }
                WritePremulBGRA(dstRow + x * 4u, r, g, b, a);
            }
        }
    }

    outInfo.formatName = std::wstring(L"Sony PSP GIM (") + fmtLabel +
                         (img.order == 1u ? L", GE-Swizzled)" : L")");
    outInfo.valid = true;
    return true;
}

} // namespace Formats
