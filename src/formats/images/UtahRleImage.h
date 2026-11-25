#pragma once
// UtahRleImage.h — Utah Raster Toolkit RLE (.rle — 0xCC52 LE magic) Bytecode Interpreter

namespace Formats {

inline bool DecodeUtahRleImage(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    // 0xCC52 little-endian magic -> [0x52, 0xCC]
    if (!data || size < 16u || ByteIO::RdU16LE(data) != 0xCC52u) return false;

    const uint16_t xpos      = ByteIO::RdU16LE(data + 2);
    const uint16_t ypos      = ByteIO::RdU16LE(data + 4);
    const uint16_t w         = ByteIO::RdU16LE(data + 6);
    const uint16_t h         = ByteIO::RdU16LE(data + 8);
    const uint8_t  flags     = data[10];
    const uint8_t  ncolors   = data[11];
    const uint8_t  pixelbits = data[12];
    const uint8_t  ncmap     = data[13];
    const uint8_t  cmaplen   = data[14];
    (void)xpos; (void)ypos;

    if (w == 0 || h == 0 || w > 8192u || h > 8192u) return false;
    if (pixelbits != 8u || ncolors > 4u || ncmap > 4u || cmaplen > 12u) return false;
    if (ncolors == 0u && ncmap == 0u) return false;

    const bool noBackground = (flags & 0x02u) != 0u;
    const bool hasAlpha     = (flags & 0x04u) != 0u;
    const bool hasComment   = (flags & 0x08u) != 0u;

    size_t pos = 15u;
    uint8_t bgCol[4] = { 0, 0, 0, 255 };
    if (!noBackground && ncolors > 0u) {
        if (pos + ncolors > size) return false;
        for (uint8_t c = 0; c < ncolors; ++c) bgCol[c] = data[pos + c];
        pos += ncolors;
        if ((ncolors & 1u) == 0u) { // Pad so total after byte 14 is odd -> pos becomes even!
            if (pos >= size) return false;
            ++pos;
        }
    } else {
        if (pos >= size) return false;
        ++pos; // Filler byte at offset 15 -> pos = 16
    }

    // Optional 16-bit LE colormap: ncmap channels of (1 << cmaplen) uint16_t entries (high byte is 8-bit color)
    const size_t mapLen = (ncmap > 0u) ? (size_t{1} << cmaplen) : 0u;
    std::vector<uint8_t> cmap;
    if (ncmap > 0u) {
        const size_t cmapBytes = static_cast<size_t>(ncmap) * mapLen * 2u;
        if (cmapBytes > size - pos) return false;
        cmap.resize(static_cast<size_t>(ncmap) * mapLen);
        for (size_t i = 0; i < static_cast<size_t>(ncmap) * mapLen; ++i) {
            cmap[i] = static_cast<uint8_t>(ByteIO::RdU16LE(data + pos + i * 2u) >> 8u);
        }
        pos += cmapBytes;
    }

    // Optional even-padded comment block
    if (hasComment) {
        if (pos + 2u > size) return false;
        const uint16_t cmtLen = ByteIO::RdU16LE(data + pos);
        pos += 2u;
        const size_t paddedCmt = (static_cast<size_t>(cmtLen) + 1u) & ~size_t(1u);
        if (paddedCmt > size - pos) return false;
        pos += paddedCmt;
    }

    const size_t totalPixels = static_cast<size_t>(w) * h;
    if (totalPixels > 64ull * 1024ull * 1024ull) return false;

    const uint8_t numChans = std::max<uint8_t>(1u, ncolors);
    std::vector<uint8_t> planes(static_cast<size_t>(numChans) * totalPixels, 0u);
    std::vector<uint8_t> alphaPlane(totalPixels, noBackground ? 0u : 255u);
    if (!noBackground) {
        for (uint8_t c = 0; c < numChans; ++c) {
            std::fill(planes.begin() + static_cast<ptrdiff_t>(c * totalPixels),
                      planes.begin() + static_cast<ptrdiff_t>((c + 1u) * totalPixels),
                      bgCol[c]);
        }
    }

    int32_t curX = 0;
    int32_t curY = 0;
    int32_t curChan = 0; // 0..ncolors-1, or -1 (255) for alpha

    auto getTargetRow = [&](int32_t y) -> uint8_t* {
        if (y < 0 || y >= static_cast<int32_t>(h)) return nullptr;
        // Utah RLE coordinate origin is bottom-left!
        const size_t dstY = static_cast<size_t>(h - 1 - y);
        if (curChan == -1 || curChan == 255) {
            return hasAlpha ? &alphaPlane[dstY * w] : nullptr;
        }
        if (curChan >= 0 && curChan < static_cast<int32_t>(numChans)) {
            return &planes[static_cast<size_t>(curChan) * totalPixels + dstY * w];
        }
        return nullptr;
    };

    // Execute Utah RLE opcode stream:
    //   1 = SkipLines, 2 = SetColor, 3 = SkipPixels, 5 = ByteData, 6 = RunData, 7 = EOF (0x40 bit = long 16-bit datum)
    while (pos + 2u <= size) {
        const uint8_t rawOp = data[pos];
        const uint8_t arg8  = data[pos + 1u];
        pos += 2u;

        const bool longDatum = (rawOp & 0x40u) != 0u;
        const uint8_t op = rawOp & 0x3Fu;

        auto readDatum = [&]() -> bool {
            return !longDatum || (pos + 2u <= size);
        };
        auto fetchDatum = [&](bool signedVal) -> int32_t {
            if (!longDatum) return static_cast<int32_t>(arg8);
            const uint16_t u16 = ByteIO::RdU16LE(data + pos);
            pos += 2u;
            return signedVal ? static_cast<int32_t>(static_cast<int16_t>(u16)) : static_cast<int32_t>(u16);
        };

        if (op == 7u) { // EOFOp
            break;
        } else if (op == 1u) { // SkipLinesOp
            if (!readDatum()) return false;
            curY += fetchDatum(true);
            curX = 0;
        } else if (op == 2u) { // SetColorOp
            curChan = (arg8 == 255u) ? -1 : static_cast<int32_t>(arg8);
            curX = 0;
        } else if (op == 3u) { // SkipPixelsOp
            if (!readDatum()) return false;
            curX += fetchDatum(true);
        } else if (op == 5u) { // ByteDataOp (nc - 1)
            if (!readDatum()) return false;
            const int32_t count = fetchDatum(false) + 1;
            if (count <= 0 || static_cast<size_t>(count) > size - pos) return false;
            if (uint8_t* dstRow = getTargetRow(curY)) {
                for (int32_t i = 0; i < count; ++i) {
                    const int32_t px = curX + i;
                    if (px >= 0 && px < static_cast<int32_t>(w)) {
                        dstRow[px] = data[pos + static_cast<size_t>(i)];
                        if (!hasAlpha && noBackground && curY >= 0 && curY < static_cast<int32_t>(h)) {
                            alphaPlane[static_cast<size_t>(h - 1 - curY) * w + static_cast<size_t>(px)] = 255u;
                        }
                    }
                }
            }
            curX += count;
            pos += (static_cast<size_t>(count) + 1u) & ~size_t(1u);
        } else if (op == 6u) { // RunDataOp (nc - 1), followed by [val, pad]
            if (!readDatum()) return false;
            const int32_t count = fetchDatum(false) + 1;
            if (count <= 0 || pos + 2u > size) return false;
            const uint8_t val = data[pos];
            pos += 2u;
            if (uint8_t* dstRow = getTargetRow(curY)) {
                for (int32_t i = 0; i < count; ++i) {
                    const int32_t px = curX + i;
                    if (px >= 0 && px < static_cast<int32_t>(w)) {
                        dstRow[px] = val;
                        if (!hasAlpha && noBackground && curY >= 0 && curY < static_cast<int32_t>(h)) {
                            alphaPlane[static_cast<size_t>(h - 1 - curY) * w + static_cast<size_t>(px)] = 255u;
                        }
                    }
                }
            }
            curX += count;
        } else {
            return false; // Unknown opcode -> reject corrupt data
        }
    }

    auto mapChan = [&](uint8_t chIdx, uint8_t v) -> uint8_t {
        if (ncmap == 0u || mapLen == 0u) return v;
        const size_t lutCh = std::min<size_t>(chIdx, ncmap - 1u);
        return cmap[lutCh * mapLen + (v % mapLen)];
    };

    outInfo = {};
    outInfo.width = w;
    outInfo.height = h;
    outInfo.origWidth = w;
    outInfo.origHeight = h;
    outInfo.channels = (numChans >= 3u || ncmap >= 3u) ? (hasAlpha ? 4u : 3u) : (hasAlpha ? 2u : 1u);
    outInfo.bitsPerChannel = 8u;
    outInfo.formatName = L"Utah Raster Toolkit RLE (.rle)";
    outInfo.bgraPixels.resize(totalPixels * 4u);

    for (size_t i = 0; i < totalPixels; ++i) {
        uint8_t r = 0, g = 0, b = 0;
        if (numChans == 1u && ncmap >= 3u) {
            const uint8_t idx = planes[i];
            r = mapChan(0, idx);
            g = mapChan(1, idx);
            b = mapChan(2, idx);
        } else if (numChans >= 3u) {
            r = mapChan(0, planes[i]);
            g = mapChan(1, planes[totalPixels + i]);
            b = mapChan(2, planes[totalPixels * 2u + i]);
        } else {
            r = g = b = mapChan(0, planes[i]);
        }
        const uint8_t a = alphaPlane[i];
        WritePremulBGRA(&outInfo.bgraPixels[i * 4u], r, g, b, a);
    }

    outInfo.valid = true;
    return true;
}

} // namespace Formats
