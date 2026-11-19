#pragma once
// WindowsCursorCur.h — Windows Static Cursor Hotspot & PNG/DIB+AND Mask Decoder (.cur)

namespace Formats {

namespace DetailArtFont {

// ----------------------------------------------------------------------------
// 7. Windows Static Cursor (.cur) — Header 00 00 02 00 + Hotspot + PNG/DIB+AND
// ----------------------------------------------------------------------------
inline bool DecodeCurCursor(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 22) return false;
    uint16_t idReserved = DetailImg100::RdU16LE(data + 0);
    uint16_t idType     = DetailImg100::RdU16LE(data + 2);
    uint16_t idCount    = DetailImg100::RdU16LE(data + 4);
    if (idReserved != 0u || idType != 2u || idCount == 0u || idCount > 256u) return false;
    if (6u + static_cast<size_t>(idCount) * 16u > size) return false;

    size_t bestIdx = SIZE_MAX;
    uint64_t bestScore = 0;

    for (uint16_t i = 0; i < idCount; ++i) {
        const uint8_t* ent = data + 6u + static_cast<size_t>(i) * 16u;
        uint32_t w = (ent[0] == 0u) ? 256u : ent[0];
        uint32_t h = (ent[1] == 0u) ? 256u : ent[1];
        uint32_t bytesInRes = DetailImg100::RdU32LE(ent + 8);
        uint32_t imgOff     = DetailImg100::RdU32LE(ent + 12);
        if (bytesInRes < 16u || imgOff < 6u + static_cast<size_t>(idCount) * 16u ||
            imgOff > size || bytesInRes > size - imgOff) {
            continue;
        }
        uint32_t bpp = 32u;
        if (bytesInRes >= 40u && std::memcmp(data + imgOff, "\x89PNG\r\n\x1A\n", 8) != 0) {
            bpp = DetailImg100::RdU16LE(data + imgOff + 14);
        }
        uint64_t score = static_cast<uint64_t>(w) * h * 64ull + bpp;
        if (score > bestScore || bestIdx == SIZE_MAX) {
            bestScore = score;
            bestIdx = i;
        }
    }
    if (bestIdx == SIZE_MAX) return false;

    const uint8_t* ent = data + 6u + bestIdx * 16u;
    uint16_t hotX = DetailImg100::RdU16LE(ent + 4);
    uint16_t hotY = DetailImg100::RdU16LE(ent + 6);
    uint32_t bytesInRes = DetailImg100::RdU32LE(ent + 8);
    uint32_t imgOff     = DetailImg100::RdU32LE(ent + 12);
    const uint8_t* img  = data + imgOff;

    std::wstring fmtLabel = L"Windows Cursor (.cur, Hotspot " + std::to_wstring(hotX) + L"," + std::to_wstring(hotY) + L")";

    // Case A: Embedded PNG cursor frame
    if (bytesInRes >= 24u && std::memcmp(img, "\x89PNG\r\n\x1A\n", 8) == 0) {
        if (DetailImg100::DecodeEmbeddedWicBuffer(img, bytesInRes, fmtLabel, outInfo)) {
            outInfo.origWidth = outInfo.width;
            outInfo.origHeight = outInfo.height;
            return true;
        }
        return false;
    }

    // Case B: BITMAPINFOHEADER DIB (XOR color bitmap + 1-bit AND mask)
    if (bytesInRes < 40u) return false;
    uint32_t biSize   = DetailImg100::RdU32LE(img + 0);
    int32_t  biWidth  = static_cast<int32_t>(DetailImg100::RdU32LE(img + 4));
    int32_t  biHeight = static_cast<int32_t>(DetailImg100::RdU32LE(img + 8));
    uint16_t biPlanes = DetailImg100::RdU16LE(img + 12);
    uint16_t biBits   = DetailImg100::RdU16LE(img + 14);
    uint32_t biComp   = DetailImg100::RdU32LE(img + 16);
    uint32_t biClrUsed= DetailImg100::RdU32LE(img + 32);

    if (biSize < 40u || biSize > bytesInRes || biPlanes != 1u || biComp != 0u ||
        biWidth <= 0 || biWidth > 1024 || biHeight == 0) {
        return false;
    }
    if (biBits != 1u && biBits != 4u && biBits != 8u && biBits != 24u && biBits != 32u) return false;

    const bool topDown = (biHeight < 0);
    const uint32_t absH = static_cast<uint32_t>(std::abs(biHeight));
    // ICO/CUR DIB biHeight stores XOR height + AND height (2 * cursorH)!
    const uint32_t w = static_cast<uint32_t>(biWidth);
    const uint32_t h = absH / 2u;
    if (h == 0u || h > 1024u) return false;

    uint32_t palCount = (biBits <= 8u) ? ((biClrUsed > 0u && biClrUsed <= (1u << biBits)) ? biClrUsed : (1u << biBits)) : 0u;
    size_t palBytes = static_cast<size_t>(palCount) * 4u;
    if (biSize + palBytes > bytesInRes) return false;
    const uint8_t* palPtr = img + biSize;

    size_t xorStride = ((static_cast<size_t>(w) * biBits + 31u) / 32u) * 4u;
    size_t andStride = ((static_cast<size_t>(w) + 31u) / 32u) * 4u;
    size_t xorOffset = biSize + palBytes;
    size_t xorBytes  = xorStride * h;
    if (xorOffset + xorBytes > bytesInRes) return false;
    const uint8_t* xorPtr = img + xorOffset;
    const uint8_t* andPtr = (xorOffset + xorBytes + andStride * h <= bytesInRes) ? (xorPtr + xorBytes) : nullptr;

    // Check if 32-bit cursor has non-zero per-pixel alpha channel
    bool anyAlpha32 = false;
    if (biBits == 32u) {
        for (uint32_t y = 0; y < h && !anyAlpha32; ++y) {
            const uint8_t* row = xorPtr + static_cast<size_t>(y) * xorStride;
            for (uint32_t x = 0; x < w; ++x) {
                if (row[x * 4u + 3u] != 0u) { anyAlpha32 = true; break; }
            }
        }
    }

    outInfo.width = w;
    outInfo.height = h;
    outInfo.origWidth = w;
    outInfo.origHeight = h;
    outInfo.channels = 4u;
    outInfo.bitsPerChannel = 8u;
    outInfo.formatName = fmtLabel;
    outInfo.bgraPixels.resize(static_cast<size_t>(w) * h * 4u);

    for (uint32_t y = 0; y < h; ++y) {
        uint32_t srcRow = topDown ? y : (h - 1u - y);
        const uint8_t* xRow = xorPtr + static_cast<size_t>(srcRow) * xorStride;
        const uint8_t* aRow = andPtr ? (andPtr + static_cast<size_t>(srcRow) * andStride) : nullptr;

        for (uint32_t x = 0; x < w; ++x) {
            uint8_t b = 0, g = 0, r = 0, a = 255u;
            bool andBit = aRow ? (((aRow[x >> 3] >> (7u - (x & 7u))) & 1u) != 0u) : false;

            if (biBits == 32u) {
                b = xRow[x * 4u + 0u];
                g = xRow[x * 4u + 1u];
                r = xRow[x * 4u + 2u];
                a = anyAlpha32 ? xRow[x * 4u + 3u] : (andBit ? 0u : 255u);
            } else if (biBits == 24u) {
                b = xRow[x * 3u + 0u];
                g = xRow[x * 3u + 1u];
                r = xRow[x * 3u + 2u];
                a = andBit ? ((b | g | r) ? 192u : 0u) : 255u;
            } else {
                uint32_t idx = 0;
                if (biBits == 8u) idx = xRow[x];
                else if (biBits == 4u) idx = ((x & 1u) == 0u) ? (xRow[x >> 1] >> 4) : (xRow[x >> 1] & 0x0Fu);
                else idx = (xRow[x >> 3] >> (7u - (x & 7u))) & 1u;

                if (idx < palCount) {
                    b = palPtr[idx * 4u + 0u];
                    g = palPtr[idx * 4u + 1u];
                    r = palPtr[idx * 4u + 2u];
                }
                if (andBit) {
                    // AND=1, XOR=0 -> transparent; AND=1, XOR!=0 -> inverted screen cursor pixel!
                    if (b == 0u && g == 0u && r == 0u && idx == 0u) {
                        a = 0u;
                    } else {
                        a = 220u;
                    }
                }
            }
            WritePremulBGRA(&outInfo.bgraPixels[(static_cast<size_t>(y) * w + x) * 4u], r, g, b, a);
        }
    }

    outInfo.valid = true;
    return true;
}


} // namespace DetailArtFont

inline bool DecodeCur(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    return DetailArtFont::DecodeCurCursor(data, size, outInfo);
}

} // namespace Formats
