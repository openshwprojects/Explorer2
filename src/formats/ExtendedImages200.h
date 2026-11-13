#pragma once
// ExtendedImages200.h — 20 Camera RAW, Vector/Print, Creative Design, Embroidery & Retro Art Decoders (#1–#20)
// Covers:
//   Camera RAW (#1–#9): .cr2, .cr3, .nef, .arw, .dng, .raf, .orf, .rw2, .pef
//   Print/Vector/Design (#10–#17): .eps, .ai, .cdr, .afphoto/.afdesign/.afpub, .emf/.wmf, .cgm, .abr, .pat
//   Embroidery & Terminal Art (#18–#20): .dst, .pes, .ans/.sixel

namespace Formats {

namespace DetailImg200 {

inline uint16_t Rd16(const uint8_t* p, bool be) {
    return be ? DetailImg100::RdU16BE(p) : DetailImg100::RdU16LE(p);
}

inline uint32_t Rd32(const uint8_t* p, bool be) {
    return be ? DetailImg100::RdU32BE(p) : DetailImg100::RdU32LE(p);
}

// Anti-aliased / thick Bresenham line helper on BGRA canvas
inline void DrawLineBGRA(
    std::vector<uint8_t>& bgra, uint32_t w, uint32_t h,
    int x0, int y0, int x1, int y1,
    uint8_t r, uint8_t g, uint8_t b, int thickness = 1)
{
    int dx = std::abs(x1 - x0), sx = (x0 < x1) ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;
    int rad = std::max(0, (thickness - 1) / 2);
    int steps = 0;
    const int maxSteps = static_cast<int>(w + h) * 4 + 64;
    while (steps++ < maxSteps) {
        for (int oy = -rad; oy <= rad; ++oy) {
            int py = y0 + oy;
            if (py < 0 || py >= static_cast<int>(h)) continue;
            for (int ox = -rad; ox <= rad; ++ox) {
                int px = x0 + ox;
                if (px < 0 || px >= static_cast<int>(w)) continue;
                uint8_t* dst = &bgra[(static_cast<size_t>(py) * w + static_cast<size_t>(px)) * 4];
                dst[0] = b; dst[1] = g; dst[2] = r; dst[3] = 255;
            }
        }
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

inline void FillRectBGRA(
    std::vector<uint8_t>& bgra, uint32_t w, uint32_t h,
    int x0, int y0, int x1, int y1,
    uint8_t r, uint8_t g, uint8_t b)
{
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    x0 = std::clamp(x0, 0, static_cast<int>(w));
    x1 = std::clamp(x1, 0, static_cast<int>(w));
    y0 = std::clamp(y0, 0, static_cast<int>(h));
    y1 = std::clamp(y1, 0, static_cast<int>(h));
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            uint8_t* dst = &bgra[(static_cast<size_t>(y) * w + static_cast<size_t>(x)) * 4];
            dst[0] = b; dst[1] = g; dst[2] = r; dst[3] = 255;
        }
    }
}

// Scans a memory buffer for the largest valid embedded JPEG (FF D8 FF ... FF D9) or PNG (89 50 4E 47 ... IEND)
inline bool FindAndDecodeBestEmbeddedRaster(
    const uint8_t* data, size_t size,
    const std::wstring& fmtLabel,
    ExtendedImageInfo& outInfo,
    size_t startOffset = 0)
{
    size_t bestJpgOff = 0, bestJpgLen = 0;
    size_t bestPngOff = 0, bestPngLen = 0;

    // 1. Scan for PNG signatures (used by Affinity .afphoto, CorelDRAW, modern previews)
    for (size_t i = startOffset; i + 24 < size; ++i) {
        if (data[i] == 0x89 && data[i + 1] == 'P' && data[i + 2] == 'N' && data[i + 3] == 'G' &&
            data[i + 4] == 0x0D && data[i + 5] == 0x0A && data[i + 6] == 0x1A && data[i + 7] == 0x0A)
        {
            // Walk PNG chunks to find exact IEND end
            size_t p = i + 8;
            bool validChunks = false;
            while (p + 12 <= size) {
                uint32_t cLen = DetailImg100::RdU32BE(data + p);
                if (cLen > size - p - 12) break;
                bool isIend = (std::memcmp(data + p + 4, "IEND", 4) == 0);
                p += 12 + static_cast<size_t>(cLen);
                if (isIend) { validChunks = true; break; }
            }
            if (validChunks && (p - i) > bestPngLen) {
                bestPngOff = i;
                bestPngLen = p - i;
            }
            if (p > i + 8) i = p - 1;
        }
    }
    if (bestPngLen >= 32) {
        if (DetailImg100::DecodeEmbeddedWicBuffer(data + bestPngOff, bestPngLen, fmtLabel, outInfo)) {
            return true;
        }
    }

    // 2. Scan for JPEG streams (FF D8 FF E0/E1/DB/C0/C4/FE ... FF D9)
    for (size_t i = startOffset; i + 16 < size; ++i) {
        if (data[i] == 0xFF && data[i + 1] == 0xD8 && data[i + 2] == 0xFF &&
            (data[i + 3] == 0xE0 || data[i + 3] == 0xE1 || data[i + 3] == 0xDB ||
             data[i + 3] == 0xC0 || data[i + 3] == 0xC4 || data[i + 3] == 0xEE || data[i + 3] == 0xFE))
        {
            // Find matching EOI (FF D9) by walking JPEG markers up to SOS, then scanning for unescaped FF D9
            size_t p = i + 2;
            size_t endJpg = 0;
            while (p + 4 <= size) {
                if (data[p] != 0xFF) { ++p; continue; }
                while (p < size && data[p] == 0xFF) ++p;
                if (p >= size) break;
                uint8_t marker = data[p++];
                if (marker == 0xD9) { endJpg = p; break; }
                if (marker == 0x00 || (marker >= 0xD0 && marker <= 0xD8)) continue;
                if (p + 2 > size) break;
                uint16_t segLen = DetailImg100::RdU16BE(data + p);
                if (segLen < 2 || p + segLen > size) break;
                p += segLen;
                if (marker == 0xDA) {
                    // Inside entropy-coded scan: scan forward for FF D9 (skipping FF 00 and RST0..RST7)
                    while (p + 1 < size) {
                        if (data[p] == 0xFF) {
                            uint8_t nx = data[p + 1];
                            if (nx == 0xD9) { endJpg = p + 2; break; }
                            if (nx != 0x00 && !(nx >= 0xD0 && nx <= 0xD7) && nx != 0xFF) {
                                // Another marker (e.g. multi-scan progressive)
                                break;
                            }
                            p += 2;
                        } else {
                            ++p;
                        }
                    }
                    if (endJpg != 0) break;
                }
            }
            if (endJpg > i + 32 && (endJpg - i) > bestJpgLen) {
                ExtendedImageInfo candidate;
                if (DetailImg100::DecodeEmbeddedWicBuffer(data + i, endJpg - i, fmtLabel, candidate) && candidate.valid) {
                    if (static_cast<uint64_t>(candidate.width) * candidate.height >=
                        static_cast<uint64_t>(outInfo.width) * outInfo.height)
                    {
                        outInfo = std::move(candidate);
                        bestJpgOff = i;
                        bestJpgLen = endJpg - i;
                    }
                }
                i = endJpg - 1;
            }
        }
    }
    (void)bestJpgOff;
    return outInfo.valid;
}

// Parses TIFF-based RAW IFDs (Canon CR2, Nikon NEF, Sony ARW, Adobe DNG, Olympus ORF, Panasonic RW2, Pentax PEF)
inline bool ParseTiffRawIfds(
    const uint8_t* data, size_t size,
    const std::wstring& fmtLabel,
    ExtendedImageInfo& outInfo)
{
    if (size < 16) return false;
    bool be = false;
    if (data[0] == 'I' && data[1] == 'I') be = false;
    else if (data[0] == 'M' && data[1] == 'M') be = true;
    else return false;

    uint16_t magic = Rd16(data + 2, be);
    // Standard TIFF=42, Olympus ORF=0x4F52 ('RO') / 0x5352 ('RS'), Panasonic RW2=0x0055
    if (magic != 42 && magic != 0x4F52 && magic != 0x5352 && magic != 0x0055) return false;

    uint32_t firstIfd = Rd32(data + 4, be);
    std::vector<uint32_t> ifdQueue;
    if (firstIfd >= 8 && firstIfd + 2 <= size) ifdQueue.push_back(firstIfd);

    uint32_t bestRawW = 0, bestRawH = 0;
    uint32_t bestJpgOff = 0, bestJpgLen = 0;

    // Also track uncompressed RGB / Grayscale / Bayer CFA strip in case DNG/RAW has uncompressed preview
    uint32_t uncompW = 0, uncompH = 0, uncompOff = 0, uncompBytes = 0;
    uint16_t uncompBps = 8, uncompSpp = 1, uncompPhoto = 1;

    size_t visited = 0;
    while (!ifdQueue.empty() && visited < 32) {
        uint32_t ifdOff = ifdQueue.back();
        ifdQueue.pop_back();
        ++visited;
        if (ifdOff + 2 > size) continue;

        uint16_t numEntries = Rd16(data + ifdOff, be);
        if (numEntries > 512 || ifdOff + 2 + static_cast<size_t>(numEntries) * 12 > size) continue;

        uint32_t curW = 0, curH = 0, stripOff = 0, stripBytes = 0, jpgOff = 0, jpgLen = 0;
        uint16_t comp = 1, bps = 8, spp = 1, photo = 2;

        for (uint16_t e = 0; e < numEntries; ++e) {
            const uint8_t* ent = data + ifdOff + 2 + static_cast<size_t>(e) * 12;
            uint16_t tag   = Rd16(ent + 0, be);
            uint16_t type  = Rd16(ent + 2, be);
            uint32_t count = Rd32(ent + 4, be);
            uint32_t valOff = Rd32(ent + 8, be);

            auto scalarU32 = [&]() -> uint32_t {
                if (type == 3) return Rd16(ent + 8, be); // SHORT
                if (type == 4) return valOff;            // LONG
                if (type == 1) return ent[8];            // BYTE
                return valOff;
            };

            if (tag == 0x0100) curW = scalarU32();                 // ImageWidth
            else if (tag == 0x0101) curH = scalarU32();            // ImageLength
            else if (tag == 0x0102) {                              // BitsPerSample
                if (count == 1) bps = static_cast<uint16_t>(scalarU32());
                else if (valOff + 2 <= size) bps = Rd16(data + valOff, be);
            }
            else if (tag == 0x0103) comp = static_cast<uint16_t>(scalarU32()); // Compression
            else if (tag == 0x0106) photo = static_cast<uint16_t>(scalarU32()); // PhotometricInterpretation
            else if (tag == 0x0111) {                              // StripOffsets
                stripOff = (count == 1) ? scalarU32() : ((valOff + 4 <= size) ? Rd32(data + valOff, be) : 0);
            }
            else if (tag == 0x0115) spp = static_cast<uint16_t>(scalarU32()); // SamplesPerPixel
            else if (tag == 0x0117) {                              // StripByteCounts
                stripBytes = (count == 1) ? scalarU32() : ((valOff + 4 <= size) ? Rd32(data + valOff, be) : 0);
            }
            else if (tag == 0x0201) jpgOff = scalarU32();          // JPEGInterchangeFormat
            else if (tag == 0x0202) jpgLen = scalarU32();          // JPEGInterchangeFormatLength
            else if (tag == 0x002E && valOff + count <= size && count > 32) {
                // Panasonic RW2 JpgFromRaw inline/offset tag (0x002E)!
                if (count > bestJpgLen) { bestJpgOff = valOff; bestJpgLen = count; }
            }
            else if (tag == 0x014A) {                              // SubIFDs
                if (count == 1 && valOff < size) {
                    ifdQueue.push_back(valOff);
                } else if (count > 1 && count <= 16 && valOff + count * 4 <= size) {
                    for (uint32_t k = 0; k < count; ++k) {
                        uint32_t sub = Rd32(data + valOff + k * 4, be);
                        if (sub < size) ifdQueue.push_back(sub);
                    }
                }
            }
            else if (tag == 0x8769 && valOff < size) {             // ExifIFD
                ifdQueue.push_back(valOff);
            }
        }

        if (curW > bestRawW && curH > bestRawH && curW <= 32768 && curH <= 32768) {
            bestRawW = curW;
            bestRawH = curH;
        }
        // Check embedded JPEG via 0x0201/0x0202 or JPEG-compressed strip (comp == 6 or 7)
        if (jpgOff > 0 && jpgLen > 32 && static_cast<size_t>(jpgOff) + jpgLen <= size && jpgLen > bestJpgLen) {
            bestJpgOff = jpgOff;
            bestJpgLen = jpgLen;
        }
        if ((comp == 6 || comp == 7) && stripOff > 0 && stripBytes > 32 &&
            static_cast<size_t>(stripOff) + stripBytes <= size && stripBytes > bestJpgLen)
        {
            if (data[stripOff] == 0xFF && data[stripOff + 1] == 0xD8) {
                bestJpgOff = stripOff;
                bestJpgLen = stripBytes;
            }
        }
        // Uncompressed RGB / Grayscale / CFA strip (comp == 1)
        if (comp == 1 && curW > 0 && curH > 0 && curW <= 8192 && curH <= 8192 &&
            stripOff > 0 && stripBytes > 0 && static_cast<size_t>(stripOff) + stripBytes <= size)
        {
            if (static_cast<uint64_t>(curW) * curH >= static_cast<uint64_t>(uncompW) * uncompH) {
                uncompW = curW; uncompH = curH;
                uncompOff = stripOff; uncompBytes = stripBytes;
                uncompBps = bps; uncompSpp = spp; uncompPhoto = photo;
            }
        }

        size_t nextPtrPos = ifdOff + 2 + static_cast<size_t>(numEntries) * 12;
        if (nextPtrPos + 4 <= size) {
            uint32_t nextIfd = Rd32(data + nextPtrPos, be);
            if (nextIfd >= 8 && nextIfd < size) ifdQueue.push_back(nextIfd);
        }
    }

    if (bestJpgLen > 32 && DetailImg100::DecodeEmbeddedWicBuffer(data + bestJpgOff, bestJpgLen, fmtLabel, outInfo)) {
        if (bestRawW > outInfo.origWidth) { outInfo.origWidth = bestRawW; outInfo.origHeight = bestRawH; }
        return true;
    }

    // Decode uncompressed RGB / Grayscale / Bayer RGGB CFA strip directly!
    if (uncompW > 0 && uncompH > 0 && uncompOff < size) {
        size_t bytesPerSample = (uncompBps > 8) ? 2 : 1;
        size_t needed = static_cast<size_t>(uncompW) * uncompH * std::max<uint16_t>(1, uncompSpp) * bytesPerSample;
        if (static_cast<size_t>(uncompOff) + needed <= size) {
            outInfo.width = uncompW;
            outInfo.height = uncompH;
            outInfo.origWidth = (bestRawW > 0) ? bestRawW : uncompW;
            outInfo.origHeight = (bestRawH > 0) ? bestRawH : uncompH;
            outInfo.channels = 4;
            outInfo.bitsPerChannel = uncompBps;
            outInfo.formatName = fmtLabel;
            outInfo.bgraPixels.resize(static_cast<size_t>(uncompW) * uncompH * 4);

            const uint8_t* src = data + uncompOff;
            auto readSample8 = [&](size_t idx) -> uint8_t {
                if (bytesPerSample == 1) return src[idx];
                uint16_t v16 = Rd16(src + idx * 2, be);
                int shift = std::max(0, static_cast<int>(uncompBps) - 8);
                return static_cast<uint8_t>(std::min<uint32_t>(255u, v16 >> shift));
            };

            if (uncompSpp >= 3) {
                for (size_t i = 0; i < static_cast<size_t>(uncompW) * uncompH; ++i) {
                    uint8_t r = readSample8(i * uncompSpp + 0);
                    uint8_t g = readSample8(i * uncompSpp + 1);
                    uint8_t b = readSample8(i * uncompSpp + 2);
                    outInfo.bgraPixels[i * 4 + 0] = b;
                    outInfo.bgraPixels[i * 4 + 1] = g;
                    outInfo.bgraPixels[i * 4 + 2] = r;
                    outInfo.bgraPixels[i * 4 + 3] = 255;
                }
            } else if (uncompPhoto == 32803 && uncompW >= 2 && uncompH >= 2) {
                // Photometric 32803 = CFA (Color Filter Array) RGGB Bilinear Demosaic!
                for (uint32_t y = 0; y < uncompH; ++y) {
                    uint32_t y0 = (y > 0) ? (y - 1) : y;
                    uint32_t y1 = (y + 1 < uncompH) ? (y + 1) : y;
                    for (uint32_t x = 0; x < uncompW; ++x) {
                        uint32_t x0 = (x > 0) ? (x - 1) : x;
                        uint32_t x1 = (x + 1 < uncompW) ? (x + 1) : x;
                        auto at = [&](uint32_t px, uint32_t py) -> int {
                            return readSample8(static_cast<size_t>(py) * uncompW + px);
                        };
                        int r = 0, g = 0, b = 0;
                        bool evenY = ((y & 1) == 0), evenX = ((x & 1) == 0);
                        if (evenY && evenX) { // R site
                            r = at(x, y);
                            g = (at(x0, y) + at(x1, y) + at(x, y0) + at(x, y1)) / 4;
                            b = (at(x0, y0) + at(x1, y0) + at(x0, y1) + at(x1, y1)) / 4;
                        } else if (evenY && !evenX) { // Gr site
                            r = (at(x0, y) + at(x1, y)) / 2;
                            g = at(x, y);
                            b = (at(x, y0) + at(x, y1)) / 2;
                        } else if (!evenY && evenX) { // Gb site
                            r = (at(x, y0) + at(x, y1)) / 2;
                            g = at(x, y);
                            b = (at(x0, y) + at(x1, y)) / 2;
                        } else { // B site
                            r = (at(x0, y0) + at(x1, y0) + at(x0, y1) + at(x1, y1)) / 4;
                            g = (at(x0, y) + at(x1, y) + at(x, y0) + at(x, y1)) / 4;
                            b = at(x, y);
                        }
                        size_t di = (static_cast<size_t>(y) * uncompW + x) * 4;
                        outInfo.bgraPixels[di + 0] = static_cast<uint8_t>(b);
                        outInfo.bgraPixels[di + 1] = static_cast<uint8_t>(g);
                        outInfo.bgraPixels[di + 2] = static_cast<uint8_t>(r);
                        outInfo.bgraPixels[di + 3] = 255;
                    }
                }
            } else {
                // Grayscale
                for (size_t i = 0; i < static_cast<size_t>(uncompW) * uncompH; ++i) {
                    uint8_t v = readSample8(i);
                    outInfo.bgraPixels[i * 4 + 0] = v;
                    outInfo.bgraPixels[i * 4 + 1] = v;
                    outInfo.bgraPixels[i * 4 + 2] = v;
                    outInfo.bgraPixels[i * 4 + 3] = 255;
                }
            }
            outInfo.valid = true;
            return true;
        }
    }

    // Fallback scan for any embedded JPEG stream (e.g. Olympus MakerNote 0x2020 preview)
    if (FindAndDecodeBestEmbeddedRaster(data, size, fmtLabel, outInfo, 8)) {
        if (bestRawW > outInfo.origWidth) { outInfo.origWidth = bestRawW; outInfo.origHeight = bestRawH; }
        return true;
    }

    // Try system WIC decoder if Windows Raw Image Extension is installed
    if (DetailImg100::DecodeEmbeddedWicBuffer(data, size, fmtLabel, outInfo)) {
        return true;
    }

    // Valid TIFF/RAW header with proprietary lossless compression (e.g. Canon LJ92 / Sony ARW2) without preview:
    // render clean darkroom sensor telemetry card showing exact RAW sensor resolution!
    if (bestRawW > 0 && bestRawH > 0) {
        uint32_t cw = 320, ch = 200;
        outInfo.width = cw; outInfo.height = ch;
        outInfo.origWidth = bestRawW; outInfo.origHeight = bestRawH;
        outInfo.channels = 4; outInfo.bitsPerChannel = 14;
        outInfo.formatName = fmtLabel;
        outInfo.bgraPixels.resize(cw * ch * 4);
        FillRectBGRA(outInfo.bgraPixels, cw, ch, 0, 0, cw, ch, 18, 24, 38);
        // Bayer RGGB mosaic accent strip
        for (int by = 0; by < 8; ++by) {
            for (int bx = 0; bx < 16; ++bx) {
                bool ey = ((by & 1) == 0), ex = ((bx & 1) == 0);
                uint8_t r = (ey && ex) ? 220 : 30;
                uint8_t g = (ey != ex) ? 195 : 35;
                uint8_t b = (!ey && !ex) ? 230 : 35;
                FillRectBGRA(outInfo.bgraPixels, cw, ch, 20 + bx * 10, 68 + by * 10, 29 + bx * 10, 77 + by * 10, r, g, b);
            }
        }
        DetailImg100::DrawBitmapText5x7(outInfo.bgraPixels, cw, ch, 20, 22, "CAMERA RAW SENSOR", 2, 248, 250, 252);
        char dimBuf[64] = {};
        snprintf(dimBuf, sizeof(dimBuf), "%u X %u PX", bestRawW, bestRawH);
        DetailImg100::DrawBitmapText5x7(outInfo.bgraPixels, cw, ch, 20, 164, dimBuf, 2, 56, 189, 248);
        outInfo.valid = true;
        return true;
    }
    return false;
}

} // namespace DetailImg200

// ============================================================================
// #1–#9. Camera RAW Family (.cr2, .cr3, .nef, .arw, .dng, .raf, .orf, .rw2, .pef)
// ============================================================================
inline bool DecodeCameraRaw200(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    // #2. Canon CR3 — ISOBMFF ('ftypcrx ')
    if (extLower == L".cr3" || (size >= 16 && std::memcmp(data + 4, "ftypcrx ", 8) == 0)) {
        if (size < 16 || std::memcmp(data + 4, "ftyp", 4) != 0) return false;
        std::wstring label = L"Canon CR3 RAW (.cr3)";
        // Walk top-level & nested ISOBMFF boxes for PRVW / THMB / mdat JPEG stream
        size_t pos = 0;
        while (pos + 8 <= size) {
            uint64_t boxLen = DetailImg100::RdU32BE(data + pos);
            size_t hdrLen = 8;
            if (boxLen == 1) {
                if (pos + 16 > size) break;
                boxLen = (static_cast<uint64_t>(DetailImg100::RdU32BE(data + pos + 8)) << 32) |
                         DetailImg100::RdU32BE(data + pos + 12);
                hdrLen = 16;
            } else if (boxLen == 0) {
                boxLen = size - pos;
            }
            if (boxLen < hdrLen || pos + boxLen > size) break;

            if (std::memcmp(data + pos + 4, "PRVW", 4) == 0 ||
                std::memcmp(data + pos + 4, "THMB", 4) == 0 ||
                std::memcmp(data + pos + 4, "uuid", 4) == 0 ||
                std::memcmp(data + pos + 4, "mdat", 4) == 0)
            {
                if (DetailImg200::FindAndDecodeBestEmbeddedRaster(
                        data + pos + hdrLen, static_cast<size_t>(boxLen - hdrLen), label, outInfo))
                {
                    return true;
                }
            }
            pos += static_cast<size_t>(boxLen);
        }
        if (DetailImg200::FindAndDecodeBestEmbeddedRaster(data, size, label, outInfo)) return true;
        return false;
    }

    // #6. Fujifilm RAF — "FUJIFILMCCD-RAW "
    if (extLower == L".raf" || (size >= 16 && std::memcmp(data, "FUJIFILMCCD-RAW", 15) == 0)) {
        if (size < 92 || std::memcmp(data, "FUJIFILMCCD-RAW", 15) != 0) return false;
        std::wstring label = L"Fujifilm RAF RAW (.raf)";
        uint32_t jpgOff = DetailImg100::RdU32BE(data + 0x54);
        uint32_t jpgLen = DetailImg100::RdU32BE(data + 0x58);
        if (jpgOff > 0 && jpgLen > 32 && static_cast<size_t>(jpgOff) + jpgLen <= size) {
            if (DetailImg100::DecodeEmbeddedWicBuffer(data + jpgOff, jpgLen, label, outInfo)) {
                return true;
            }
        }
        return DetailImg200::FindAndDecodeBestEmbeddedRaster(data, size, label, outInfo, 64);
    }

    // #1, #3, #4, #5, #7, #8, #9: TIFF-structured RAWs
    std::wstring label = L"Camera RAW Image";
    if (extLower == L".cr2")      label = L"Canon CR2 RAW (.cr2)";
    else if (extLower == L".nef") label = L"Nikon NEF RAW (.nef)";
    else if (extLower == L".arw") label = L"Sony Alpha ARW RAW (.arw)";
    else if (extLower == L".dng") label = L"Adobe Digital Negative (.dng)";
    else if (extLower == L".orf") label = L"Olympus ORF RAW (.orf)";
    else if (extLower == L".rw2") label = L"Panasonic Lumix RW2 RAW (.rw2)";
    else if (extLower == L".pef") label = L"Pentax PEF RAW (.pef)";
    else return false;

    return DetailImg200::ParseTiffRawIfds(data, size, label, outInfo);
}

// ============================================================================
// #10 & #11. Encapsulated PostScript (.eps) & Adobe Illustrator (.ai)
// ============================================================================
inline bool DecodeEpsOrAi(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    std::wstring label = (extLower == L".ai") ? L"Adobe Illustrator Artwork (.ai)" : L"Encapsulated PostScript (.eps)";

    // 1. DOS Binary EPS Header (C5 D0 D3 C6): [psOff:4][psLen:4][wmfOff:4][wmfLen:4][tiffOff:4][tiffLen:4]
    if (size >= 30 && data[0] == 0xC5 && data[1] == 0xD0 && data[2] == 0xD3 && data[3] == 0xC6) {
        uint32_t tiffOff = DetailImg100::RdU32LE(data + 20);
        uint32_t tiffLen = DetailImg100::RdU32LE(data + 24);
        if (tiffOff > 0 && tiffLen > 16 && static_cast<size_t>(tiffOff) + tiffLen <= size) {
            if (DetailImg100::DecodeEmbeddedWicBuffer(data + tiffOff, tiffLen, label, outInfo)) return true;
            if (DetailImg200::ParseTiffRawIfds(data + tiffOff, tiffLen, label, outInfo)) return true;
        }
    }

    // 2. Modern .ai is often a PDF-1.x container with embedded JPEG/PNG thumbnail XObject
    if (size >= 8 && std::memcmp(data, "%PDF-", 5) == 0) {
        if (DetailImg200::FindAndDecodeBestEmbeddedRaster(data, size, label, outInfo)) {
            return true;
        }
    }

    std::string_view sv(reinterpret_cast<const char*>(data), std::min<size_t>(size, 4 * 1024 * 1024));
    if (sv.find("%!PS") == std::string_view::npos &&
        sv.find("%%BoundingBox") == std::string_view::npos &&
        sv.find("%PDF-") == std::string_view::npos)
    {
        return false;
    }

    // Parse %%BoundingBox: llx lly urx ury
    float bx0 = 0.0f, by0 = 0.0f, bx1 = 400.0f, by1 = 300.0f;
    size_t bbPos = sv.find("%%BoundingBox:");
    if (bbPos != std::string_view::npos) {
        size_t eol = sv.find_first_of("\r\n", bbPos);
        std::string bbLine(sv.substr(bbPos + 14, (eol == std::string_view::npos ? sv.size() : eol) - (bbPos + 14)));
        float a = 0, b = 0, c = 0, d = 0;
        if (sscanf_s(bbLine.c_str(), "%f %f %f %f", &a, &b, &c, &d) == 4 && c > a && d > b) {
            bx0 = a; by0 = b; bx1 = c; by1 = d;
        }
    }

    float docW = std::max(1.0f, bx1 - bx0);
    float docH = std::max(1.0f, by1 - by0);
    uint32_t imgW = 512;
    uint32_t imgH = std::clamp(static_cast<uint32_t>(std::round(512.0f * (docH / docW))), 128u, 768u);
    if (docH > docW) {
        imgH = 512;
        imgW = std::clamp(static_cast<uint32_t>(std::round(512.0f * (docW / docH))), 128u, 768u);
    }

    outInfo.width = imgW;
    outInfo.height = imgH;
    outInfo.origWidth = static_cast<uint32_t>(std::round(docW));
    outInfo.origHeight = static_cast<uint32_t>(std::round(docH));
    outInfo.channels = 4;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = label;
    outInfo.bgraPixels.assign(static_cast<size_t>(imgW) * imgH * 4, 255); // Crisp white artboard

    auto mapX = [&](float x) -> int {
        return static_cast<int>(std::round(((x - bx0) / docW) * (imgW - 1)));
    };
    auto mapY = [&](float y) -> int {
        // PostScript Y-axis points UP
        return static_cast<int>(std::round((1.0f - ((y - by0) / docH)) * (imgH - 1)));
    };

    // Simple PostScript / AI operand stack & vector interpreter
    std::vector<float> stack;
    float curX = bx0, curY = by0, startX = bx0, startY = by0;
    uint8_t colR = 25, colG = 35, colB = 50;
    int lineThick = 2;
    int drawnPrims = 0;

    size_t i = 0;
    while (i < sv.size()) {
        char ch = sv[i];
        if (ch == '%') {
            while (i < sv.size() && sv[i] != '\n') ++i;
            continue;
        }
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') { ++i; continue; }
        if (ch == '(' || ch == '<' || ch == '[' || ch == ']') {
            if (ch == '(') {
                int depth = 1; ++i;
                while (i < sv.size() && depth > 0) {
                    if (sv[i] == '\\') i += 2;
                    else { if (sv[i] == '(') ++depth; else if (sv[i] == ')') --depth; ++i; }
                }
            } else ++i;
            continue;
        }

        size_t tokStart = i;
        while (i < sv.size() && sv[i] != ' ' && sv[i] != '\t' && sv[i] != '\r' && sv[i] != '\n' &&
               sv[i] != '%' && sv[i] != '(' && sv[i] != '/' && sv[i] != '[' && sv[i] != ']')
        {
            ++i;
        }
        if (i == tokStart) { ++i; continue; }
        std::string_view tok = sv.substr(tokStart, i - tokStart);

        char numBuf[48] = {};
        if (tok.size() < 44 && ((tok[0] >= '0' && tok[0] <= '9') || tok[0] == '-' || tok[0] == '+' || tok[0] == '.')) {
            std::memcpy(numBuf, tok.data(), tok.size());
            char* endPtr = nullptr;
            float v = std::strtof(numBuf, &endPtr);
            if (endPtr && endPtr != numBuf) {
                if (stack.size() > 64) stack.erase(stack.begin(), stack.begin() + 32);
                stack.push_back(v);
                continue;
            }
        }

        auto pop = [&]() -> float {
            if (stack.empty()) return 0.0f;
            float v = stack.back();
            stack.pop_back();
            return v;
        };

        if ((tok == "moveto" || tok == "m") && stack.size() >= 2) {
            curY = pop(); curX = pop();
            startX = curX; startY = curY;
        } else if ((tok == "rmoveto" || tok == "rm") && stack.size() >= 2) {
            float dy = pop(), dx = pop();
            curX += dx; curY += dy;
            startX = curX; startY = curY;
        } else if ((tok == "lineto" || tok == "l" || tok == "L") && stack.size() >= 2) {
            float ny = pop(), nx = pop();
            DetailImg200::DrawLineBGRA(outInfo.bgraPixels, imgW, imgH, mapX(curX), mapY(curY), mapX(nx), mapY(ny), colR, colG, colB, lineThick);
            curX = nx; curY = ny;
            ++drawnPrims;
        } else if (tok == "rlineto" && stack.size() >= 2) {
            float dy = pop(), dx = pop();
            float nx = curX + dx, ny = curY + dy;
            DetailImg200::DrawLineBGRA(outInfo.bgraPixels, imgW, imgH, mapX(curX), mapY(curY), mapX(nx), mapY(ny), colR, colG, colB, lineThick);
            curX = nx; curY = ny;
            ++drawnPrims;
        } else if ((tok == "curveto" || tok == "c" || tok == "C") && stack.size() >= 6) {
            float y3 = pop(), x3 = pop(), y2 = pop(), x2 = pop(), y1 = pop(), x1 = pop();
            float px = curX, py = curY;
            for (int s = 1; s <= 12; ++s) {
                float t = s / 12.0f, it = 1.0f - t;
                float bx = it*it*it*curX + 3*it*it*t*x1 + 3*it*t*t*x2 + t*t*t*x3;
                float by = it*it*it*curY + 3*it*it*t*y1 + 3*it*t*t*y2 + t*t*t*y3;
                DetailImg200::DrawLineBGRA(outInfo.bgraPixels, imgW, imgH, mapX(px), mapY(py), mapX(bx), mapY(by), colR, colG, colB, lineThick);
                px = bx; py = by;
            }
            curX = x3; curY = y3;
            ++drawnPrims;
        } else if (tok == "closepath" || tok == "h") {
            DetailImg200::DrawLineBGRA(outInfo.bgraPixels, imgW, imgH, mapX(curX), mapY(curY), mapX(startX), mapY(startY), colR, colG, colB, lineThick);
            curX = startX; curY = startY;
        } else if ((tok == "rectfill" || tok == "re") && stack.size() >= 4) {
            float rh = pop(), rw = pop(), ry = pop(), rx = pop();
            DetailImg200::FillRectBGRA(outInfo.bgraPixels, imgW, imgH, mapX(rx), mapY(ry + rh), mapX(rx + rw), mapY(ry), colR, colG, colB);
            ++drawnPrims;
        } else if (tok == "arc" && stack.size() >= 5) {
            float a2 = pop(), a1 = pop(), rad = pop(), cy = pop(), cx = pop();
            int px = 0, py = 0;
            for (int s = 0; s <= 24; ++s) {
                float ang = (a1 + (a2 - a1) * (s / 24.0f)) * 3.14159265f / 180.0f;
                int nx = mapX(cx + rad * std::cos(ang));
                int ny = mapY(cy + rad * std::sin(ang));
                if (s > 0) DetailImg200::DrawLineBGRA(outInfo.bgraPixels, imgW, imgH, px, py, nx, ny, colR, colG, colB, lineThick);
                px = nx; py = ny;
            }
            ++drawnPrims;
        } else if ((tok == "setrgbcolor" || tok == "rg" || tok == "RG" || tok == "Xa") && stack.size() >= 3) {
            float b = pop(), g = pop(), r = pop();
            colR = static_cast<uint8_t>(std::clamp(r * 255.0f, 0.0f, 255.0f));
            colG = static_cast<uint8_t>(std::clamp(g * 255.0f, 0.0f, 255.0f));
            colB = static_cast<uint8_t>(std::clamp(b * 255.0f, 0.0f, 255.0f));
        } else if ((tok == "setgray" || tok == "g" || tok == "G") && !stack.empty()) {
            uint8_t v = static_cast<uint8_t>(std::clamp(pop() * 255.0f, 0.0f, 255.0f));
            colR = colG = colB = v;
        } else if ((tok == "setlinewidth" || tok == "w") && !stack.empty()) {
            float lw = pop();
            lineThick = std::clamp(static_cast<int>(std::round((lw / docW) * imgW)), 1, 8);
        } else if (tok == "stroke" || tok == "fill" || tok == "newpath" || tok == "S" || tok == "f" || tok == "B") {
            stack.clear();
        }
    }

    if (drawnPrims == 0) {
        DetailImg200::DrawLineBGRA(outInfo.bgraPixels, imgW, imgH, 24, 24, imgW - 24, 24, 59, 130, 246, 2);
        DetailImg200::DrawLineBGRA(outInfo.bgraPixels, imgW, imgH, imgW - 24, 24, imgW - 24, imgH - 24, 59, 130, 246, 2);
        DetailImg200::DrawLineBGRA(outInfo.bgraPixels, imgW, imgH, imgW - 24, imgH - 24, 24, imgH - 24, 59, 130, 246, 2);
        DetailImg200::DrawLineBGRA(outInfo.bgraPixels, imgW, imgH, 24, imgH - 24, 24, 24, 59, 130, 246, 2);
        DetailImg100::DrawBitmapText5x7(outInfo.bgraPixels, imgW, imgH, 40, 48, (extLower == L".ai") ? "ADOBE ILLUSTRATOR" : "POSTSCRIPT EPS", 2, 30, 41, 59);
    }

    outInfo.valid = true;
    return true;
}

// ============================================================================
// #12 & #13. CorelDRAW (.cdr) & Affinity Suite (.afphoto / .afdesign / .afpub)
// ============================================================================
inline bool DecodeCorelOrAffinity(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    if (extLower == L".cdr") {
        std::wstring label = L"CorelDRAW Artwork (.cdr)";
        // Modern CorelDRAW X4+ is a ZIP archive containing metadata/thumbnails/thumbnail.png or preview.png
        if (size >= 4 && data[0] == 'P' && data[1] == 'K') {
            std::vector<uint8_t> thumb;
            if (ZipDeflate::ExtractZipEntryFromMemory(data, size, "thumbnail.png", thumb) ||
                ZipDeflate::ExtractZipEntryFromMemory(data, size, "preview.png", thumb) ||
                ZipDeflate::ExtractZipEntryFromMemory(data, size, "thumbnail.bmp", thumb))
            {
                if (DetailImg100::DecodeEmbeddedWicBuffer(thumb.data(), thumb.size(), label, outInfo)) {
                    return true;
                }
            }
        }
        // Classic CorelDRAW v3–X3 is RIFF....CDR* with a 'DISP' chunk containing CF_DIB (BITMAPINFOHEADER + pixels)
        if (size >= 16 && std::memcmp(data, "RIFF", 4) == 0 && std::memcmp(data + 8, "CDR", 3) == 0) {
            if (DetailImg200::FindAndDecodeBestEmbeddedRaster(data, size, label, outInfo, 12)) return true;
            // Search for 'DISP' fourCC -> construct transient 14-byte BMP file header around DIB and decode via WIC!
            for (size_t i = 12; i + 48 <= size; ++i) {
                if (std::memcmp(data + i, "DISP", 4) == 0) {
                    uint32_t chunkLen = DetailImg100::RdU32LE(data + i + 4);
                    if (chunkLen >= 44 && i + 8 + chunkLen <= size) {
                        // Skip 4-byte CF_DIB clipboard tag inside DISP chunk
                        const uint8_t* dib = data + i + 12;
                        uint32_t dibLen = chunkLen - 4;
                        uint32_t biSize = DetailImg100::RdU32LE(dib);
                        uint16_t biBitCount = DetailImg100::RdU16LE(dib + 14);
                        uint32_t biClrUsed = DetailImg100::RdU32LE(dib + 32);
                        if (biSize == 40) {
                            uint32_t palEntries = (biBitCount <= 8) ? (biClrUsed ? biClrUsed : (1u << biBitCount)) : 0;
                            uint32_t bfOffBits = 14 + biSize + palEntries * 4;
                            std::vector<uint8_t> bmp(14 + dibLen, 0);
                            bmp[0] = 'B'; bmp[1] = 'M';
                            uint32_t fileSz = static_cast<uint32_t>(bmp.size());
                            std::memcpy(bmp.data() + 2, &fileSz, 4);
                            std::memcpy(bmp.data() + 10, &bfOffBits, 4);
                            std::memcpy(bmp.data() + 14, dib, dibLen);
                            if (DetailImg100::DecodeEmbeddedWicBuffer(bmp.data(), bmp.size(), label, outInfo)) return true;
                        }
                    }
                }
            }
            // Valid RIFF CDR without embedded DISP -> vector card
            outInfo.width = 280; outInfo.height = 180;
            outInfo.channels = 4; outInfo.bitsPerChannel = 8;
            outInfo.formatName = label;
            outInfo.bgraPixels.resize(280 * 180 * 4);
            DetailImg200::FillRectBGRA(outInfo.bgraPixels, 280, 180, 0, 0, 280, 180, 20, 83, 45);
            DetailImg100::DrawBitmapText5x7(outInfo.bgraPixels, 280, 180, 24, 76, "CORELDRAW RIFF", 2, 255, 255, 255);
            outInfo.valid = true;
            return true;
        }
        return false;
    }

    // #13. Affinity Photo / Designer / Publisher (.afphoto, .afdesign, .afpub — magic 00 FF 4B 41)
    if (extLower == L".afphoto" || extLower == L".afdesign" || extLower == L".afpub" ||
        (size >= 8 && data[0] == 0x00 && data[1] == 0xFF && data[2] == 0x4B && data[3] == 0x41))
    {
        std::wstring label = (extLower == L".afdesign") ? L"Affinity Designer (.afdesign)" :
                             (extLower == L".afpub")    ? L"Affinity Publisher (.afpub)" :
                                                          L"Affinity Photo (.afphoto)";
        if (DetailImg200::FindAndDecodeBestEmbeddedRaster(data, size, label, outInfo, 4)) {
            return true;
        }
        if (size >= 8 && data[0] == 0x00 && data[1] == 0xFF && data[2] == 0x4B && data[3] == 0x41) {
            outInfo.width = 280; outInfo.height = 180;
            outInfo.channels = 4; outInfo.bitsPerChannel = 8;
            outInfo.formatName = label;
            outInfo.bgraPixels.resize(280 * 180 * 4);
            DetailImg200::FillRectBGRA(outInfo.bgraPixels, 280, 180, 0, 0, 280, 180, 49, 16, 66);
            DetailImg100::DrawBitmapText5x7(outInfo.bgraPixels, 280, 180, 24, 76, "AFFINITY DOCUMENT", 2, 232, 121, 249);
            outInfo.valid = true;
            return true;
        }
    }
    return false;
}

// ============================================================================
// #14. Windows Enhanced Metafile (.emf) & Windows / Aldus Placeable Metafile (.wmf)
// ============================================================================
inline bool DecodeEmfOrWmf(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    if (size < 22) return false;
    HENHMETAFILE hEmf = nullptr;
    std::wstring label = L"Windows Enhanced Metafile (.emf)";

    // Check EMF signature: RecordType == 1 (EMR_HEADER) and dSignature == 0x464D4520 (" EMF") at offset 40
    if (size >= 88 && DetailImg100::RdU32LE(data) == 1 && DetailImg100::RdU32LE(data + 40) == 0x464D4520u) {
        hEmf = SetEnhMetaFileBits(static_cast<UINT>(size), data);
    } else if (extLower == L".wmf" || DetailImg100::RdU32LE(data) == 0x9AC6CDD7u ||
               (data[0] == 0x01 && data[1] == 0x00 && data[2] == 0x09 && data[3] == 0x00))
    {
        label = L"Windows Metafile (.wmf)";
        size_t wmfOff = 0;
        if (DetailImg100::RdU32LE(data) == 0x9AC6CDD7u) {
            if (size <= 22) return false;
            wmfOff = 22; // Skip 22-byte Aldus Placeable header
        }
        hEmf = SetWinMetaFileBits(static_cast<UINT>(size - wmfOff), data + wmfOff, nullptr, nullptr);
    } else if (extLower == L".emf") {
        hEmf = SetEnhMetaFileBits(static_cast<UINT>(size), data);
    }

    if (!hEmf) return false;

    ENHMETAHEADER emh = {};
    emh.nSize = sizeof(emh);
    UINT gotHdr = GetEnhMetaFileHeader(hEmf, sizeof(emh), &emh);
    int srcW = 512, srcH = 384;
    if (gotHdr > 0) {
        int bw = emh.rclBounds.right - emh.rclBounds.left;
        int bh = emh.rclBounds.bottom - emh.rclBounds.top;
        if (bw > 8 && bh > 8) { srcW = bw; srcH = bh; }
        else {
            int fw = (emh.rclFrame.right - emh.rclFrame.left) / 26;
            int fh = (emh.rclFrame.bottom - emh.rclFrame.top) / 26;
            if (fw > 8 && fh > 8) { srcW = fw; srcH = fh; }
        }
    }

    float aspect = static_cast<float>(srcW) / std::max(1, srcH);
    uint32_t outW = 512, outH = 512;
    if (aspect >= 1.0f) outH = std::clamp(static_cast<uint32_t>(512.0f / aspect), 64u, 512u);
    else                outW = std::clamp(static_cast<uint32_t>(512.0f * aspect), 64u, 512u);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = static_cast<LONG>(outW);
    bmi.bmiHeader.biHeight = -static_cast<LONG>(outH); // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC hdcMem = CreateCompatibleDC(nullptr);
    void* dibBits = nullptr;
    HBITMAP hBmp = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &dibBits, nullptr, 0);
    bool rendered = false;
    if (hdcMem && hBmp && dibBits) {
        HGDIOBJ oldObj = SelectObject(hdcMem, hBmp);
        RECT rc = { 0, 0, static_cast<LONG>(outW), static_cast<LONG>(outH) };
        HBRUSH hWhite = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
        FillRect(hdcMem, &rc, hWhite);
        PlayEnhMetaFile(hdcMem, hEmf, &rc);
        GdiFlush();

        outInfo.width = outW;
        outInfo.height = outH;
        outInfo.origWidth = static_cast<uint32_t>(std::max(1, srcW));
        outInfo.origHeight = static_cast<uint32_t>(std::max(1, srcH));
        outInfo.channels = 4;
        outInfo.bitsPerChannel = 8;
        outInfo.formatName = label;
        outInfo.bgraPixels.resize(static_cast<size_t>(outW) * outH * 4);
        std::memcpy(outInfo.bgraPixels.data(), dibBits, outInfo.bgraPixels.size());
        // GDI writes 0 in alpha byte; force opaque alpha = 255
        for (size_t p = 3; p < outInfo.bgraPixels.size(); p += 4) {
            outInfo.bgraPixels[p] = 255;
        }
        outInfo.valid = true;
        rendered = true;
        SelectObject(hdcMem, oldObj);
    }
    if (hBmp) DeleteObject(hBmp);
    if (hdcMem) DeleteDC(hdcMem);
    DeleteEnhMetaFile(hEmf);
    return rendered;
}

// ============================================================================
// #15. Computer Graphics Metafile (.cgm — ISO/IEC 8632 Clear-Text & Binary)
// ============================================================================
inline bool DecodeCgmVector(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (size < 8) return false;
    std::string_view sv(reinterpret_cast<const char*>(data), std::min<size_t>(size, 2 * 1024 * 1024));
    bool isBinaryCgm = ((DetailImg100::RdU16BE(data) & 0xFFE0u) == 0x0020u); // Class 0, Element 1 = Begin Metafile
    bool isTextCgm   = (sv.find("BEGMF") != std::string_view::npos || sv.find("begmf") != std::string_view::npos);
    if (!isBinaryCgm && !isTextCgm) return false;

    const uint32_t W = 480, H = 360;
    outInfo.width = W; outInfo.height = H;
    outInfo.origWidth = W; outInfo.origHeight = H;
    outInfo.channels = 4; outInfo.bitsPerChannel = 8;
    outInfo.formatName = L"ISO 8632 Computer Graphics Metafile (.cgm)";
    outInfo.bgraPixels.resize(W * H * 4);
    DetailImg200::FillRectBGRA(outInfo.bgraPixels, W, H, 0, 0, W, H, 248, 250, 252);

    float vx0 = 0.0f, vy0 = 0.0f, vx1 = 1000.0f, vy1 = 1000.0f;
    auto mapX = [&](float x) -> int { return static_cast<int>(24 + ((x - vx0) / std::max(1.0f, vx1 - vx0)) * (W - 48)); };
    auto mapY = [&](float y) -> int { return static_cast<int>(H - 24 - ((y - vy0) / std::max(1.0f, vy1 - vy0)) * (H - 48)); };

    uint8_t colR = 15, colG = 118, colB = 110;
    int prims = 0;

    if (isTextCgm) {
        // Parse semicolon-delimited Clear-Text CGM statements: VDCEXT, LINE, POLYGON, RECT, CIRCLE
        size_t pos = 0;
        while (pos < sv.size()) {
            size_t semi = sv.find(';', pos);
            if (semi == std::string_view::npos) semi = sv.size();
            std::string stmt(sv.substr(pos, semi - pos));
            pos = semi + 1;
            for (char& c : stmt) {
                if (c == '(' || c == ')' || c == ',' || c == '\r' || c == '\n') c = ' ';
                else c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
            }
            std::vector<std::string> toks;
            size_t tp = 0;
            while (tp < stmt.size()) {
                while (tp < stmt.size() && stmt[tp] == ' ') ++tp;
                size_t te = stmt.find(' ', tp);
                if (te == std::string::npos) te = stmt.size();
                if (te > tp) toks.push_back(stmt.substr(tp, te - tp));
                tp = te;
            }
            if (toks.empty()) continue;
            if (toks[0] == "VDCEXT" && toks.size() >= 5) {
                vx0 = std::strtof(toks[1].c_str(), nullptr);
                vy0 = std::strtof(toks[2].c_str(), nullptr);
                vx1 = std::strtof(toks[3].c_str(), nullptr);
                vy1 = std::strtof(toks[4].c_str(), nullptr);
            } else if ((toks[0] == "LINE" || toks[0] == "POLYGON") && toks.size() >= 5) {
                for (size_t k = 1; k + 3 < toks.size(); k += 2) {
                    float x1 = std::strtof(toks[k].c_str(), nullptr), y1 = std::strtof(toks[k + 1].c_str(), nullptr);
                    float x2 = std::strtof(toks[k + 2].c_str(), nullptr), y2 = std::strtof(toks[k + 3].c_str(), nullptr);
                    DetailImg200::DrawLineBGRA(outInfo.bgraPixels, W, H, mapX(x1), mapY(y1), mapX(x2), mapY(y2), colR, colG, colB, 2);
                    ++prims;
                }
                if (toks[0] == "POLYGON" && toks.size() >= 7) {
                    float xFirst = std::strtof(toks[1].c_str(), nullptr), yFirst = std::strtof(toks[2].c_str(), nullptr);
                    float xLast  = std::strtof(toks[toks.size() - 2].c_str(), nullptr), yLast = std::strtof(toks[toks.size() - 1].c_str(), nullptr);
                    DetailImg200::DrawLineBGRA(outInfo.bgraPixels, W, H, mapX(xLast), mapY(yLast), mapX(xFirst), mapY(yFirst), colR, colG, colB, 2);
                }
            } else if (toks[0] == "RECT" && toks.size() >= 5) {
                float x1 = std::strtof(toks[1].c_str(), nullptr), y1 = std::strtof(toks[2].c_str(), nullptr);
                float x2 = std::strtof(toks[3].c_str(), nullptr), y2 = std::strtof(toks[4].c_str(), nullptr);
                DetailImg200::FillRectBGRA(outInfo.bgraPixels, W, H, mapX(x1), mapY(y2), mapX(x2), mapY(y1), 186, 230, 253);
                DetailImg200::DrawLineBGRA(outInfo.bgraPixels, W, H, mapX(x1), mapY(y1), mapX(x2), mapY(y1), colR, colG, colB, 2);
                DetailImg200::DrawLineBGRA(outInfo.bgraPixels, W, H, mapX(x2), mapY(y1), mapX(x2), mapY(y2), colR, colG, colB, 2);
                DetailImg200::DrawLineBGRA(outInfo.bgraPixels, W, H, mapX(x2), mapY(y2), mapX(x1), mapY(y2), colR, colG, colB, 2);
                DetailImg200::DrawLineBGRA(outInfo.bgraPixels, W, H, mapX(x1), mapY(y2), mapX(x1), mapY(y1), colR, colG, colB, 2);
                ++prims;
            } else if (toks[0] == "CIRCLE" && toks.size() >= 4) {
                float cx = std::strtof(toks[1].c_str(), nullptr), cy = std::strtof(toks[2].c_str(), nullptr);
                float rad = std::strtof(toks[3].c_str(), nullptr);
                int px = 0, py = 0;
                for (int s = 0; s <= 32; ++s) {
                    float a = s * 6.2831853f / 32.0f;
                    int nx = mapX(cx + rad * std::cos(a)), ny = mapY(cy + rad * std::sin(a));
                    if (s > 0) DetailImg200::DrawLineBGRA(outInfo.bgraPixels, W, H, px, py, nx, ny, colR, colG, colB, 2);
                    px = nx; py = ny;
                }
                ++prims;
            }
        }
    } else {
        // Binary ISO 8632 CGM command walker (16-bit big-endian command header: [class:4][id:7][len:5])
        vx0 = -32768.0f; vy0 = -32768.0f; vx1 = 32767.0f; vy1 = 32767.0f;
        size_t p = 0;
        while (p + 2 <= size) {
            uint16_t cmd = DetailImg100::RdU16BE(data + p);
            p += 2;
            int cls = (cmd >> 12) & 0x0F;
            int eid = (cmd >> 5) & 0x7F;
            uint32_t plen = cmd & 0x1F;
            if (plen == 31) {
                if (p + 2 > size) break;
                plen = DetailImg100::RdU16BE(data + p) & 0x7FFFu;
                p += 2;
            }
            if (p + plen > size) break;
            const uint8_t* payload = data + p;
            if (cls == 2 && eid == 6 && plen >= 8) { // VDC Extent (16-bit signed ints)
                vx0 = static_cast<int16_t>(DetailImg100::RdU16BE(payload + 0));
                vy0 = static_cast<int16_t>(DetailImg100::RdU16BE(payload + 2));
                vx1 = static_cast<int16_t>(DetailImg100::RdU16BE(payload + 4));
                vy1 = static_cast<int16_t>(DetailImg100::RdU16BE(payload + 6));
            } else if (cls == 4 && (eid == 1 || eid == 7) && plen >= 8) { // Polyline / Polygon
                for (uint32_t k = 0; k + 7 < plen; k += 4) {
                    float x1 = static_cast<int16_t>(DetailImg100::RdU16BE(payload + k + 0));
                    float y1 = static_cast<int16_t>(DetailImg100::RdU16BE(payload + k + 2));
                    float x2 = static_cast<int16_t>(DetailImg100::RdU16BE(payload + k + 4));
                    float y2 = static_cast<int16_t>(DetailImg100::RdU16BE(payload + k + 6));
                    DetailImg200::DrawLineBGRA(outInfo.bgraPixels, W, H, mapX(x1), mapY(y1), mapX(x2), mapY(y2), colR, colG, colB, 2);
                    ++prims;
                }
            }
            p += plen + (plen & 1u); // word-aligned
            if (cls == 0 && eid == 2) break; // End Metafile
        }
    }

    if (prims == 0) {
        DetailImg100::DrawBitmapText5x7(outInfo.bgraPixels, W, H, 24, 24, "ISO 8632 CGM VECTOR", 2, 15, 23, 42);
    }
    outInfo.valid = true;
    return true;
}

// ============================================================================
// #16 & #17. Photoshop Brush Set (.abr) & GIMP / Photoshop Pattern (.pat)
// ============================================================================
inline bool DecodeAbrOrPat(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    // #17. GIMP Pattern (.pat — magic "GPAT" at offset 20) or Photoshop Pattern ("8BPT")
    if (extLower == L".pat" || (size >= 24 && std::memcmp(data + 20, "GPAT", 4) == 0)) {
        if (size >= 24 && std::memcmp(data + 20, "GPAT", 4) == 0) {
            uint32_t hdrSize = DetailImg100::RdU32BE(data + 0);
            uint32_t pw      = DetailImg100::RdU32BE(data + 8);
            uint32_t ph      = DetailImg100::RdU32BE(data + 12);
            uint32_t bpp     = DetailImg100::RdU32BE(data + 16);
            if (pw == 0 || ph == 0 || pw > 4096 || ph > 4096 || bpp < 1 || bpp > 4) return false;
            if (hdrSize < 24 || hdrSize + static_cast<size_t>(pw) * ph * bpp > size) return false;

            // Tile 2x2 so seamless pattern tiling is immediately visible!
            uint32_t tileX = (pw <= 256) ? 2 : 1;
            uint32_t tileY = (ph <= 256) ? 2 : 1;
            uint32_t outW = pw * tileX, outH = ph * tileY;
            outInfo.width = outW; outInfo.height = outH;
            outInfo.origWidth = pw; outInfo.origHeight = ph;
            outInfo.channels = 4; outInfo.bitsPerChannel = 8;
            outInfo.formatName = L"GIMP Seamless Pattern (.pat)";
            outInfo.bgraPixels.resize(static_cast<size_t>(outW) * outH * 4);

            const uint8_t* src = data + hdrSize;
            for (uint32_t y = 0; y < outH; ++y) {
                uint32_t sy = y % ph;
                for (uint32_t x = 0; x < outW; ++x) {
                    uint32_t sx = x % pw;
                    const uint8_t* p = src + (static_cast<size_t>(sy) * pw + sx) * bpp;
                    uint8_t r = 0, g = 0, b = 0, a = 255;
                    if (bpp == 1) { r = g = b = p[0]; }
                    else if (bpp == 2) { r = g = b = p[0]; a = p[1]; }
                    else if (bpp == 3) { r = p[0]; g = p[1]; b = p[2]; }
                    else { r = p[0]; g = p[1]; b = p[2]; a = p[3]; }
                    uint8_t* dst = &outInfo.bgraPixels[(static_cast<size_t>(y) * outW + x) * 4];
                    dst[0] = static_cast<uint8_t>((b * a) / 255);
                    dst[1] = static_cast<uint8_t>((g * a) / 255);
                    dst[2] = static_cast<uint8_t>((r * a) / 255);
                    dst[3] = a;
                }
            }
            outInfo.valid = true;
            return true;
        }
        if (size >= 8 && std::memcmp(data, "8BPT", 4) == 0) {
            outInfo.width = 256; outInfo.height = 256;
            outInfo.channels = 4; outInfo.bitsPerChannel = 8;
            outInfo.formatName = L"Adobe Photoshop Pattern (.pat)";
            outInfo.bgraPixels.resize(256 * 256 * 4);
            for (uint32_t y = 0; y < 256; ++y) {
                for (uint32_t x = 0; x < 256; ++x) {
                    bool chk = (((x / 32) + (y / 32)) & 1) != 0;
                    uint8_t c = chk ? 56 : 38;
                    uint8_t* dst = &outInfo.bgraPixels[(y * 256 + x) * 4];
                    dst[0] = c + 20; dst[1] = c + 10; dst[2] = c; dst[3] = 255;
                }
            }
            DetailImg100::DrawBitmapText5x7(outInfo.bgraPixels, 256, 256, 20, 118, "PHOTOSHOP 8BPT", 2, 248, 250, 252);
            outInfo.valid = true;
            return true;
        }
        return false;
    }

    // #16. Adobe Photoshop Brush Set (.abr)
    if (extLower != L".abr" || size < 12) return false;
    uint16_t ver = DetailImg100::RdU16BE(data + 0);
    if (ver < 1 || ver > 12) return false;

    const uint32_t W = 384, H = 256;
    outInfo.width = W; outInfo.height = H;
    outInfo.origWidth = W; outInfo.origHeight = H;
    outInfo.channels = 4; outInfo.bitsPerChannel = 8;
    outInfo.formatName = L"Adobe Photoshop Brush Set (.abr)";
    outInfo.bgraPixels.resize(W * H * 4);
    DetailImg200::FillRectBGRA(outInfo.bgraPixels, W, H, 0, 0, W, H, 24, 24, 27);

    struct BrushTip { uint32_t w = 0, h = 0; std::vector<uint8_t> alpha; };
    std::vector<BrushTip> tips;

    if (ver == 1 || ver == 2) {
        uint16_t count = DetailImg100::RdU16BE(data + 2);
        size_t pos = 4;
        for (uint16_t b = 0; b < count && pos + 6 <= size && tips.size() < 6; ++b) {
            uint16_t btype = DetailImg100::RdU16BE(data + pos);
            uint32_t bsize = DetailImg100::RdU32BE(data + pos + 2);
            pos += 6;
            if (pos + bsize > size) break;
            if (btype == 2) { // Sampled brush
                size_t hdrSkip = (ver == 2) ? (4 + 2) : (4 + 2);
                const uint8_t* bp = data + pos + hdrSkip;
                const uint8_t* bend = data + pos + bsize;
                if (ver == 2 && bp + 4 <= bend) {
                    uint32_t nameLen = DetailImg100::RdU32BE(bp);
                    bp += 4 + static_cast<size_t>(nameLen) * 2;
                }
                if (bp + 1 + 16 + 2 + 1 <= bend) {
                    bp += 1 + 8; // antialias + short bounds[4]
                    int32_t top    = static_cast<int32_t>(DetailImg100::RdU32BE(bp + 0));
                    int32_t left   = static_cast<int32_t>(DetailImg100::RdU32BE(bp + 4));
                    int32_t bottom = static_cast<int32_t>(DetailImg100::RdU32BE(bp + 8));
                    int32_t right  = static_cast<int32_t>(DetailImg100::RdU32BE(bp + 12));
                    uint16_t depth = DetailImg100::RdU16BE(bp + 16);
                    uint8_t comp   = bp[18];
                    bp += 19;
                    int32_t bw = right - left, bh = bottom - top;
                    if (bw > 0 && bh > 0 && bw <= 1024 && bh <= 1024 && depth == 8 && comp == 0 &&
                        bp + static_cast<size_t>(bw) * bh <= bend)
                    {
                        BrushTip tip;
                        tip.w = static_cast<uint32_t>(bw);
                        tip.h = static_cast<uint32_t>(bh);
                        tip.alpha.assign(bp, bp + static_cast<size_t>(bw) * bh);
                        tips.push_back(std::move(tip));
                    }
                }
            }
            pos += bsize;
        }
    } else {
        // v6–v10: scan for "8BIMsamp" section
        for (size_t i = 4; i + 12 <= size; ++i) {
            if (std::memcmp(data + i, "8BIMsamp", 8) == 0) {
                uint32_t secLen = DetailImg100::RdU32BE(data + i + 8);
                size_t p = i + 12;
                size_t secEnd = std::min(size, p + secLen);
                uint16_t subVer = DetailImg100::RdU16BE(data + 2);
                size_t hdrSkip = (subVer == 1) ? 47 : 301;
                while (p + 4 <= secEnd && tips.size() < 6) {
                    uint32_t bLen = DetailImg100::RdU32BE(data + p);
                    p += 4;
                    if (bLen == 0 || p + bLen > secEnd) break;
                    if (bLen > hdrSkip + 19) {
                        const uint8_t* bp = data + p + hdrSkip;
                        int32_t top    = static_cast<int32_t>(DetailImg100::RdU32BE(bp + 0));
                        int32_t left   = static_cast<int32_t>(DetailImg100::RdU32BE(bp + 4));
                        int32_t bottom = static_cast<int32_t>(DetailImg100::RdU32BE(bp + 8));
                        int32_t right  = static_cast<int32_t>(DetailImg100::RdU32BE(bp + 12));
                        uint16_t depth = DetailImg100::RdU16BE(bp + 16);
                        uint8_t comp   = bp[18];
                        const uint8_t* pix = bp + 19;
                        int32_t bw = right - left, bh = bottom - top;
                        if (bw > 0 && bh > 0 && bw <= 1024 && bh <= 1024 && depth == 8) {
                            BrushTip tip;
                            tip.w = static_cast<uint32_t>(bw);
                            tip.h = static_cast<uint32_t>(bh);
                            tip.alpha.resize(static_cast<size_t>(bw) * bh, 0);
                            if (comp == 0 && pix + tip.alpha.size() <= data + p + bLen) {
                                std::memcpy(tip.alpha.data(), pix, tip.alpha.size());
                                tips.push_back(std::move(tip));
                            } else if (comp == 1 && pix + static_cast<size_t>(bh) * 2 <= data + p + bLen) {
                                // PackBits RLE decode
                                const uint8_t* rle = pix + static_cast<size_t>(bh) * 2;
                                const uint8_t* rend = data + p + bLen;
                                size_t dstIdx = 0;
                                while (rle < rend && dstIdx < tip.alpha.size()) {
                                    int8_t n = static_cast<int8_t>(*rle++);
                                    if (n >= 0) {
                                        size_t cnt = static_cast<size_t>(n) + 1;
                                        for (size_t k = 0; k < cnt && rle < rend && dstIdx < tip.alpha.size(); ++k) {
                                            tip.alpha[dstIdx++] = *rle++;
                                        }
                                    } else if (n != -128 && rle < rend) {
                                        size_t cnt = static_cast<size_t>(1 - n);
                                        uint8_t val = *rle++;
                                        for (size_t k = 0; k < cnt && dstIdx < tip.alpha.size(); ++k) {
                                            tip.alpha[dstIdx++] = val;
                                        }
                                    }
                                }
                                tips.push_back(std::move(tip));
                            }
                        }
                    }
                    p += (bLen + 3u) & ~3u;
                }
                break;
            }
        }
    }

    if (!tips.empty()) {
        for (size_t t = 0; t < tips.size() && t < 6; ++t) {
            int cellX = static_cast<int>(t % 3) * 124 + 14;
            int cellY = static_cast<int>(t / 3) * 116 + 14;
            DetailImg200::FillRectBGRA(outInfo.bgraPixels, W, H, cellX, cellY, cellX + 104, cellY + 98, 39, 39, 42);
            const auto& tip = tips[t];
            for (int dy = 0; dy < 88; ++dy) {
                uint32_t sy = (static_cast<uint32_t>(dy) * tip.h) / 88;
                for (int dx = 0; dx < 88; ++dx) {
                    uint32_t sx = (static_cast<uint32_t>(dx) * tip.w) / 88;
                    uint8_t a = tip.alpha[static_cast<size_t>(sy) * tip.w + sx];
                    uint8_t* dst = &outInfo.bgraPixels[(static_cast<size_t>(cellY + 5 + dy) * W + (cellX + 8 + dx)) * 4];
                    dst[0] = static_cast<uint8_t>(39 + (212 * a) / 255);
                    dst[1] = static_cast<uint8_t>(39 + (212 * a) / 255);
                    dst[2] = static_cast<uint8_t>(42 + (210 * a) / 255);
                    dst[3] = 255;
                }
            }
        }
    } else {
        // Soft procedural radial brush dabs for computed/parametric ABR brushes
        for (int b = 0; b < 3; ++b) {
            int cx = 80 + b * 112, cy = 128, rad = 36 - b * 6;
            for (int y = -rad; y <= rad; ++y) {
                for (int x = -rad; x <= rad; ++x) {
                    float d = std::sqrt(static_cast<float>(x * x + y * y)) / rad;
                    if (d <= 1.0f) {
                        float fall = (1.0f - d) * (1.0f - d);
                        uint8_t v = static_cast<uint8_t>(30 + 215 * fall);
                        uint8_t* dst = &outInfo.bgraPixels[(static_cast<size_t>(cy + y) * W + (cx + x)) * 4];
                        dst[0] = v; dst[1] = v; dst[2] = v; dst[3] = 255;
                    }
                }
            }
        }
        DetailImg100::DrawBitmapText5x7(outInfo.bgraPixels, W, H, 18, 18, "PHOTOSHOP BRUSH SET", 2, 161, 161, 170);
    }
    outInfo.valid = true;
    return true;
}

// ============================================================================
// #18 & #19. Tajima (.dst) & Brother (.pes) Industrial Embroidery Stitch Renderers
// ============================================================================
inline bool DecodeEmbroideryDstOrPes(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    struct StitchPt { float x, y; int colorIdx; bool jump; };
    std::vector<StitchPt> pts;
    std::wstring label;

    static const uint8_t kThreadColors[12][3] = {
        {239, 68, 68},   {59, 130, 246},  {34, 197, 94},  {250, 204, 21},
        {168, 85, 247},  {249, 115, 22},  {20, 184, 166}, {236, 72, 153},
        {248, 250, 252}, {132, 204, 22},  {56, 189, 248}, {251, 191, 36}
    };

    if (extLower == L".dst" || (size >= 515 && std::memcmp(data, "LA:", 3) == 0)) {
        if (size < 515) return false;
        label = L"Tajima Embroidery Stitch Design (.dst)";
        float cx = 0.0f, cy = 0.0f;
        int col = 0;
        for (size_t p = 512; p + 3 <= size; p += 3) {
            uint8_t b0 = data[p + 0], b1 = data[p + 1], b2 = data[p + 2];
            if (b0 == 0x00 && b1 == 0x00 && b2 == 0xF3) break; // End of stitch stream
            int dx = 0, dy = 0;
            if (b0 & 0x01) dx += 1;  if (b0 & 0x02) dx -= 1;
            if (b0 & 0x04) dx += 9;  if (b0 & 0x08) dx -= 9;
            if (b0 & 0x80) dy += 1;  if (b0 & 0x40) dy -= 1;
            if (b0 & 0x20) dy += 9;  if (b0 & 0x10) dy -= 9;

            if (b1 & 0x01) dx += 3;  if (b1 & 0x02) dx -= 3;
            if (b1 & 0x04) dx += 27; if (b1 & 0x08) dx -= 27;
            if (b1 & 0x80) dy += 3;  if (b1 & 0x40) dy -= 3;
            if (b1 & 0x20) dy += 27; if (b1 & 0x10) dy -= 27;

            if (b2 & 0x04) dx += 81; if (b2 & 0x08) dx -= 81;
            if (b2 & 0x20) dy += 81; if (b2 & 0x10) dy -= 81;

            bool colorChange = ((b2 & 0xC0) == 0xC0);
            bool jump = ((b2 & 0x80) != 0) && !colorChange;
            cx += dx * 0.1f; // 0.1 mm units
            cy -= dy * 0.1f;
            if (colorChange) {
                col = (col + 1) % 12;
                pts.push_back({ cx, cy, col, true });
            } else {
                pts.push_back({ cx, cy, col, jump });
            }
        }
    } else if (extLower == L".pes" || (size >= 64 && std::memcmp(data, "#PES", 4) == 0)) {
        if (size < 64 || std::memcmp(data, "#PES", 4) != 0) return false;
        label = L"Brother Embroidery Stitch Design (.pes)";
        uint32_t pecOff = DetailImg100::RdU32LE(data + 8);
        if (pecOff == 0 || pecOff + 532 > size) return false;

        uint8_t numColors = data[pecOff + 48];
        std::vector<int> pecPalette;
        for (int i = 0; i <= numColors && pecOff + 49 + i < size; ++i) {
            pecPalette.push_back(data[pecOff + 49 + i] % 12);
        }
        if (pecPalette.empty()) pecPalette.push_back(0);

        size_t sp = pecOff + 532; // PEC stitch data starts right after 512-byte header + 20-byte graphic header
        float cx = 0.0f, cy = 0.0f;
        int colIdx = 0;
        while (sp < size && pts.size() < 200000) {
            uint8_t v1 = data[sp++];
            if (v1 == 0xFF && (sp >= size || data[sp] == 0x00)) break; // End of PEC
            if (v1 == 0xFE && sp + 1 < size && data[sp] == 0xB0) {
                sp += 2; // Color change
                colIdx = (colIdx + 1) % static_cast<int>(pecPalette.size());
                pts.push_back({ cx, cy, pecPalette[colIdx], true });
                continue;
            }
            bool jump = false;
            int dx = 0, dy = 0;
            if ((v1 & 0x80) == 0) {
                dx = (v1 >= 0x40) ? (static_cast<int>(v1) - 0x80) : static_cast<int>(v1);
            } else {
                if (sp >= size) break;
                uint8_t b2 = data[sp++];
                if (v1 & 0x20) jump = true;
                int d12 = ((v1 & 0x0F) << 8) | b2;
                if (d12 & 0x800) d12 -= 0x1000;
                dx = d12;
            }
            if (sp >= size) break;
            uint8_t v2 = data[sp++];
            if ((v2 & 0x80) == 0) {
                dy = (v2 >= 0x40) ? (static_cast<int>(v2) - 0x80) : static_cast<int>(v2);
            } else {
                if (sp >= size) break;
                uint8_t b2 = data[sp++];
                if (v2 & 0x20) jump = true;
                int d12 = ((v2 & 0x0F) << 8) | b2;
                if (d12 & 0x800) d12 -= 0x1000;
                dy = d12;
            }
            cx += dx * 0.1f;
            cy += dy * 0.1f;
            pts.push_back({ cx, cy, pecPalette[colIdx], jump });
        }
    } else {
        return false;
    }

    const uint32_t W = 440, H = 440;
    outInfo.width = W; outInfo.height = H;
    outInfo.origWidth = W; outInfo.origHeight = H;
    outInfo.channels = 4; outInfo.bitsPerChannel = 8;
    outInfo.formatName = label;
    outInfo.bgraPixels.resize(W * H * 4);

    // Woven dark linen hoop background
    for (uint32_t y = 0; y < H; ++y) {
        for (uint32_t x = 0; x < W; ++x) {
            uint8_t weave = (((x + y) & 3) == 0) ? 34 : 26;
            uint8_t* dst = &outInfo.bgraPixels[(y * W + x) * 4];
            dst[0] = weave + 8; dst[1] = weave + 2; dst[2] = weave; dst[3] = 255;
        }
    }

    if (!pts.empty()) {
        float minX = pts[0].x, maxX = pts[0].x, minY = pts[0].y, maxY = pts[0].y;
        for (const auto& p : pts) {
            minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
        }
        float span = std::max({ maxX - minX, maxY - minY, 1.0f });
        float scale = (W - 64.0f) / span;
        float midX = (minX + maxX) * 0.5f, midY = (minY + maxY) * 0.5f;

        for (size_t i = 1; i < pts.size(); ++i) {
            if (pts[i].jump) continue;
            int x0 = static_cast<int>(W * 0.5f + (pts[i - 1].x - midX) * scale);
            int y0 = static_cast<int>(H * 0.5f + (pts[i - 1].y - midY) * scale);
            int x1 = static_cast<int>(W * 0.5f + (pts[i].x - midX) * scale);
            int y1 = static_cast<int>(H * 0.5f + (pts[i].y - midY) * scale);
            int ci = pts[i].colorIdx % 12;
            DetailImg200::DrawLineBGRA(
                outInfo.bgraPixels, W, H, x0, y0, x1, y1,
                kThreadColors[ci][0], kThreadColors[ci][1], kThreadColors[ci][2], 2);
        }
    }
    outInfo.valid = true;
    return true;
}

// ============================================================================
// #20. ANSI Escape CP437 BBS Art (.ans) & DEC VT340 Sixel Graphics (.sixel)
// ============================================================================
inline bool DecodeAnsiOrSixelArt(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    // Check DEC Sixel Graphics (ESC P ... q or DCS 0x90 ... q)
    std::string_view sv(reinterpret_cast<const char*>(data), std::min<size_t>(size, 4 * 1024 * 1024));
    bool isSixel = (extLower == L".sixel") ||
                   (sv.find("\x1BP") != std::string_view::npos && sv.find('q') != std::string_view::npos);
    if (isSixel) {
        size_t qPos = sv.find('q');
        if (qPos == std::string_view::npos) return false;
        const uint32_t maxW = 640, maxH = 480;
        std::vector<uint8_t> canvas(maxW * maxH * 4, 0);
        DetailImg200::FillRectBGRA(canvas, maxW, maxH, 0, 0, maxW, maxH, 15, 23, 42);

        uint8_t pal[256][3] = {};
        pal[0][0] = 0; pal[0][1] = 0; pal[0][2] = 0;
        pal[1][0] = 59; pal[1][1] = 130; pal[1][2] = 246;
        pal[2][0] = 239; pal[2][1] = 68; pal[2][2] = 68;
        pal[3][0] = 34; pal[3][1] = 197; pal[3][2] = 94;
        for (int i = 4; i < 256; ++i) {
            pal[i][0] = static_cast<uint8_t>((i * 73) & 0xFF);
            pal[i][1] = static_cast<uint8_t>((i * 151) & 0xFF);
            pal[i][2] = static_cast<uint8_t>((i * 211) & 0xFF);
        }

        uint32_t curX = 0, bandY = 0, usedW = 16, usedH = 16;
        int curCol = 1;
        size_t i = qPos + 1;
        while (i < sv.size()) {
            char c = sv[i];
            if (c == '\x1B' || static_cast<uint8_t>(c) == 0x9C) break; // ST terminator
            if (c == '$') { curX = 0; ++i; continue; }
            if (c == '-') { curX = 0; bandY += 6; ++i; continue; }
            if (c == '#') {
                ++i;
                int reg = 0;
                while (i < sv.size() && sv[i] >= '0' && sv[i] <= '9') reg = reg * 10 + (sv[i++] - '0');
                curCol = reg & 0xFF;
                if (i < sv.size() && sv[i] == ';') {
                    // #reg;2;r;g;b (0..100 percentages)
                    int vals[4] = {};
                    for (int k = 0; k < 4 && i < sv.size() && sv[i] == ';'; ++k) {
                        ++i;
                        while (i < sv.size() && sv[i] >= '0' && sv[i] <= '9') vals[k] = vals[k] * 10 + (sv[i++] - '0');
                    }
                    if (vals[0] == 2) {
                        pal[curCol][0] = static_cast<uint8_t>(std::clamp(vals[1] * 255 / 100, 0, 255));
                        pal[curCol][1] = static_cast<uint8_t>(std::clamp(vals[2] * 255 / 100, 0, 255));
                        pal[curCol][2] = static_cast<uint8_t>(std::clamp(vals[3] * 255 / 100, 0, 255));
                    }
                }
                continue;
            }
            uint32_t repeat = 1;
            if (c == '!') {
                ++i;
                repeat = 0;
                while (i < sv.size() && sv[i] >= '0' && sv[i] <= '9') repeat = repeat * 10 + (sv[i++] - '0');
                if (repeat == 0) repeat = 1;
                if (i >= sv.size()) break;
                c = sv[i];
            }
            if (c >= '?' && c <= '~') {
                uint8_t bits = static_cast<uint8_t>(c - '?');
                for (uint32_t r = 0; r < repeat; ++r) {
                    if (curX < maxW) {
                        for (uint32_t b = 0; b < 6; ++b) {
                            if ((bits & (1u << b)) && (bandY + b) < maxH) {
                                uint8_t* dst = &canvas[((bandY + b) * maxW + curX) * 4];
                                dst[0] = pal[curCol][2];
                                dst[1] = pal[curCol][1];
                                dst[2] = pal[curCol][0];
                                dst[3] = 255;
                                usedW = std::max(usedW, curX + 1);
                                usedH = std::max(usedH, bandY + b + 1);
                            }
                        }
                    }
                    ++curX;
                }
            }
            ++i;
        }

        outInfo.width = std::min(maxW, usedW + 8);
        outInfo.height = std::min(maxH, usedH + 8);
        outInfo.origWidth = outInfo.width;
        outInfo.origHeight = outInfo.height;
        outInfo.channels = 4; outInfo.bitsPerChannel = 8;
        outInfo.formatName = L"DEC VT340 Sixel Graphics (.sixel)";
        outInfo.bgraPixels.resize(static_cast<size_t>(outInfo.width) * outInfo.height * 4);
        for (uint32_t y = 0; y < outInfo.height; ++y) {
            std::memcpy(&outInfo.bgraPixels[static_cast<size_t>(y) * outInfo.width * 4],
                        &canvas[static_cast<size_t>(y) * maxW * 4],
                        static_cast<size_t>(outInfo.width) * 4);
        }
        outInfo.valid = true;
        return true;
    }

    if (extLower != L".ans" || size == 0) return false;

    // Strip optional SAUCE record at EOF ("SAUCE00" 128 bytes from end, preceded by 0x1A EOF)
    size_t contentEnd = size;
    if (size >= 128 && std::memcmp(data + size - 128, "SAUCE00", 7) == 0) {
        contentEnd = size - 128;
    }

    static const uint8_t kCga16[16][3] = {
        {0,0,0},       {170,0,0},     {0,170,0},     {170,85,0},
        {0,0,170},     {170,0,170},   {0,170,170},   {170,170,170},
        {85,85,85},    {255,85,85},   {85,255,85},   {255,255,85},
        {85,85,255},   {255,85,255},  {85,255,255},  {255,255,255}
    };

    const int cols = 80, maxRows = 60;
    struct Cell { uint8_t ch = ' ', fg = 7, bg = 0; };
    std::vector<Cell> grid(cols * maxRows);
    int cx = 0, cy = 0, maxUsedRow = 1;
    int fg = 7, bg = 0;
    bool bold = false;

    size_t p = 0;
    while (p < contentEnd && data[p] != 0x1A) {
        uint8_t b = data[p++];
        if (b == 0x1B && p < contentEnd && data[p] == '[') {
            ++p;
            std::vector<int> params;
            int cur = 0; bool hasNum = false;
            while (p < contentEnd) {
                uint8_t c = data[p];
                if (c >= '0' && c <= '9') { cur = cur * 10 + (c - '0'); hasNum = true; ++p; }
                else if (c == ';') { params.push_back(hasNum ? cur : 0); cur = 0; hasNum = false; ++p; }
                else break;
            }
            if (hasNum || !params.empty()) params.push_back(hasNum ? cur : 0);
            if (p >= contentEnd) break;
            uint8_t cmd = data[p++];
            if (cmd == 'm') {
                if (params.empty()) params.push_back(0);
                for (int v : params) {
                    if (v == 0) { fg = 7; bg = 0; bold = false; }
                    else if (v == 1) bold = true;
                    else if (v == 22) bold = false;
                    else if (v >= 30 && v <= 37) fg = v - 30;
                    else if (v >= 40 && v <= 47) bg = v - 40;
                    else if (v >= 90 && v <= 97) { fg = v - 90; bold = true; }
                }
            } else if (cmd == 'H' || cmd == 'f') {
                cy = std::clamp((params.size() >= 1 ? std::max(1, params[0]) : 1) - 1, 0, maxRows - 1);
                cx = std::clamp((params.size() >= 2 ? std::max(1, params[1]) : 1) - 1, 0, cols - 1);
            } else if (cmd == 'C') {
                cx = std::clamp(cx + (params.empty() ? 1 : std::max(1, params[0])), 0, cols - 1);
            } else if (cmd == 'D') {
                cx = std::clamp(cx - (params.empty() ? 1 : std::max(1, params[0])), 0, cols - 1);
            } else if (cmd == 'A') {
                cy = std::clamp(cy - (params.empty() ? 1 : std::max(1, params[0])), 0, maxRows - 1);
            } else if (cmd == 'B') {
                cy = std::clamp(cy + (params.empty() ? 1 : std::max(1, params[0])), 0, maxRows - 1);
            }
            continue;
        }
        if (b == '\r') { cx = 0; continue; }
        if (b == '\n') { cy = std::min(maxRows - 1, cy + 1); maxUsedRow = std::max(maxUsedRow, cy + 1); continue; }
        if (cy < maxRows && cx < cols) {
            uint8_t effFg = static_cast<uint8_t>((fg & 7) + (bold ? 8 : 0));
            grid[cy * cols + cx] = { b, effFg, static_cast<uint8_t>(bg & 7) };
            maxUsedRow = std::max(maxUsedRow, cy + 1);
            if (++cx >= cols) { cx = 0; cy = std::min(maxRows - 1, cy + 1); }
        }
    }

    int rows = std::clamp(maxUsedRow, 8, maxRows);
    uint32_t imgW = static_cast<uint32_t>(cols * 8);
    uint32_t imgH = static_cast<uint32_t>(rows * 16);
    outInfo.width = imgW; outInfo.height = imgH;
    outInfo.origWidth = imgW; outInfo.origHeight = imgH;
    outInfo.channels = 4; outInfo.bitsPerChannel = 8;
    outInfo.formatName = L"ANSI Escape CP437 Art (.ans)";
    outInfo.bgraPixels.resize(static_cast<size_t>(imgW) * imgH * 4);

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const Cell& cell = grid[r * cols + c];
            const uint8_t* fc = kCga16[cell.fg & 15];
            const uint8_t* bc = kCga16[cell.bg & 15];
            int px0 = c * 8, py0 = r * 16;
            DetailImg200::FillRectBGRA(outInfo.bgraPixels, imgW, imgH, px0, py0, px0 + 8, py0 + 16, bc[0], bc[1], bc[2]);

            uint8_t ch = cell.ch;
            if (ch == 0xDB) { // Full block █
                DetailImg200::FillRectBGRA(outInfo.bgraPixels, imgW, imgH, px0, py0, px0 + 8, py0 + 16, fc[0], fc[1], fc[2]);
            } else if (ch == 0xDC) { // Lower half block ▄
                DetailImg200::FillRectBGRA(outInfo.bgraPixels, imgW, imgH, px0, py0 + 8, px0 + 8, py0 + 16, fc[0], fc[1], fc[2]);
            } else if (ch == 0xDF) { // Upper half block ▀
                DetailImg200::FillRectBGRA(outInfo.bgraPixels, imgW, imgH, px0, py0, px0 + 8, py0 + 8, fc[0], fc[1], fc[2]);
            } else if (ch == 0xDD) { // Left half ▌
                DetailImg200::FillRectBGRA(outInfo.bgraPixels, imgW, imgH, px0, py0, px0 + 4, py0 + 16, fc[0], fc[1], fc[2]);
            } else if (ch == 0xDE) { // Right half ▐
                DetailImg200::FillRectBGRA(outInfo.bgraPixels, imgW, imgH, px0 + 4, py0, px0 + 8, py0 + 16, fc[0], fc[1], fc[2]);
            } else if (ch == 0xB0 || ch == 0xB1 || ch == 0xB2) { // Shades ░▒▓
                int mod = (ch == 0xB0) ? 4 : ((ch == 0xB1) ? 2 : 1);
                for (int y = 0; y < 16; ++y) {
                    for (int x = 0; x < 8; ++x) {
                        bool on = (mod == 1) ? (((x + y) & 3) != 0) : (((x + y) % mod) == 0);
                        if (on) {
                            uint8_t* dst = &outInfo.bgraPixels[(static_cast<size_t>(py0 + y) * imgW + (px0 + x)) * 4];
                            dst[0] = fc[2]; dst[1] = fc[1]; dst[2] = fc[0]; dst[3] = 255;
                        }
                    }
                }
            } else if (ch >= 33 && ch <= 126) {
                char s[2] = { static_cast<char>(ch), '\0' };
                DetailImg100::DrawBitmapText5x7(outInfo.bgraPixels, imgW, imgH, px0 + 1, py0 + 2, s, 1, fc[0], fc[1], fc[2]);
            }
        }
    }
    outInfo.valid = true;
    return true;
}

inline bool DecodeExtendedImages200(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    if (extLower == L".cr2" || extLower == L".cr3" || extLower == L".nef" ||
        extLower == L".arw" || extLower == L".dng" || extLower == L".raf" ||
        extLower == L".orf" || extLower == L".rw2" || extLower == L".pef")
    {
        return DecodeCameraRaw200(data, size, extLower, outInfo);
    }
    if (extLower == L".eps" || extLower == L".ai") {
        return DecodeEpsOrAi(data, size, extLower, outInfo);
    }
    if (extLower == L".cdr" || extLower == L".afphoto" || extLower == L".afdesign" || extLower == L".afpub") {
        return DecodeCorelOrAffinity(data, size, extLower, outInfo);
    }
    if (extLower == L".emf" || extLower == L".wmf") {
        return DecodeEmfOrWmf(data, size, extLower, outInfo);
    }
    if (extLower == L".cgm") {
        return DecodeCgmVector(data, size, outInfo);
    }
    if (extLower == L".abr" || extLower == L".pat") {
        return DecodeAbrOrPat(data, size, extLower, outInfo);
    }
    if (extLower == L".dst" || extLower == L".pes") {
        return DecodeEmbroideryDstOrPes(data, size, extLower, outInfo);
    }
    if (extLower == L".ans" || extLower == L".sixel") {
        return DecodeAnsiOrSixelArt(data, size, extLower, outInfo);
    }
    return false;
}

} // namespace Formats
