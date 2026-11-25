#pragma once
// KhorosViffImage.h — Khoros Visualization Image File Format (.viff, .xv — 0xAB 0x01 magic) Decoder

namespace Formats {

inline bool DecodeKhorosViffImage(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    // Khoros VIFF header is a fixed 1024-byte block starting with XV_FILE_MAGIC_NUM (0xAB) + XV_FILE_TYPE_XVIFF (0x01)
    if (!data || size < 1024u || data[0] != 0xABu || data[1] != 0x01u) return false;

    const uint8_t machineDep = data[4];
    // 0x02 (VFF_DEP_DECORDER) & 0x08 (VFF_DEP_NSORDER / x86) are little-endian; 0x04 (IEEE/Sun/SGI) & 0x01 are big-endian
    const bool be = (machineDep != 0x02u && machineDep != 0x08u);

    auto rd16 = [&](const uint8_t* p) -> uint16_t { return ByteIO::RdU16(p, be); };
    auto rd32 = [&](const uint8_t* p) -> uint32_t { return ByteIO::RdU32(p, be); };
    auto rd64 = [&](const uint8_t* p) -> uint64_t {
        return be ? ByteIO::RdU64BE(p) : ByteIO::RdU64LE(p);
    };
    auto rdF32 = [&](const uint8_t* p) -> float {
        uint32_t u = rd32(p);
        float f = 0.0f;
        std::memcpy(&f, &u, 4);
        return f;
    };
    auto rdF64 = [&](const uint8_t* p) -> double {
        uint64_t u = rd64(p);
        double d = 0.0;
        std::memcpy(&d, &u, 8);
        return d;
    };

    const uint32_t w               = rd32(data + 520); // row_size (pixels per row)
    const uint32_t h               = rd32(data + 524); // col_size (number of rows)
    const uint32_t locType         = rd32(data + 548); // 1 = IMPLICIT, 2 = EXPLICIT
    const uint32_t locDim          = rd32(data + 552);
    const uint32_t numImages       = std::max<uint32_t>(1u, rd32(data + 556));
    const uint32_t numBands        = rd32(data + 560);
    const uint32_t dataStorageType = rd32(data + 564); // 0=BIT, 1=BYTE, 2=SHORT, 4=INT, 5=FLOAT, 9=DOUBLE
    const uint32_t encodeScheme    = rd32(data + 568); // 0=NONE, 1=RLE
    const uint32_t mapScheme       = rd32(data + 572); // 0=NONE, 1=EACHBAND, 2=CYCLE, 3=SHARED, 4=GROUP
    const uint32_t mapStorageType  = rd32(data + 576); // 0=NONE, 1=BYTE, 2=SHORT, 4=INT, 5=FLOAT, 7=DOUBLE
    const uint32_t mapRowSize      = rd32(data + 580); // LUT channels (e.g. 3 for RGB)
    const uint32_t mapColSize      = rd32(data + 584); // LUT entries per channel (e.g. 256)
    (void)numImages;

    if (w == 0 || h == 0 || w > 8192u || h > 8192u || numBands == 0 || numBands > 16u) return false;
    if (encodeScheme != 0u) return false; // Standard Khoros VIFF stores uncompressed arrays

    auto storageBytes = [](uint32_t st, bool isMap) -> size_t {
        switch (st) {
            case 1u: return 1u; // VFF_TYP_1_BYTE / VFF_MS_1_BYTE
            case 2u: return 2u; // VFF_TYP_2_BYTE / VFF_MS_2_BYTE
            case 4u: return 4u; // VFF_TYP_4_BYTE / VFF_MS_4_BYTE
            case 5u: return 4u; // VFF_TYP_FLOAT  / VFF_MS_FLOAT
            case 7u: return isMap ? 8u : 0u; // VFF_MS_DOUBLE
            case 9u: return isMap ? 0u : 8u; // VFF_TYP_DOUBLE
            default: return 0u;
        }
    };

    size_t pos = 1024u;

    // 1. Optional Colormap LUT section (immediately follows 1024-byte header)
    const bool hasMap = (mapScheme != 0u && mapStorageType != 0u && mapRowSize > 0u && mapColSize > 0u);
    std::vector<uint8_t> lut8;
    if (hasMap) {
        if (mapRowSize > 16u || mapColSize > 65536u) return false;
        const size_t mapElemBytes = storageBytes(mapStorageType, true);
        if (mapElemBytes == 0u) return false;
        const size_t numMapTables = (mapScheme == 1u) ? numBands : 1u;
        const uint64_t totalMapElems = static_cast<uint64_t>(numMapTables) * mapRowSize * mapColSize;
        const uint64_t totalMapBytes = totalMapElems * mapElemBytes;
        if (totalMapBytes > size - pos) return false;

        lut8.resize(static_cast<size_t>(mapRowSize) * mapColSize);
        const uint8_t* mp = data + pos;
        for (size_t i = 0; i < static_cast<size_t>(mapRowSize) * mapColSize; ++i) {
            const uint8_t* ep = mp + i * mapElemBytes;
            double v = 0.0;
            if (mapStorageType == 1u)      v = ep[0];
            else if (mapStorageType == 2u) v = (rd16(ep) <= 255u) ? static_cast<double>(rd16(ep)) : (rd16(ep) * 255.0 / 65535.0);
            else if (mapStorageType == 4u) v = std::clamp<double>(static_cast<int32_t>(rd32(ep)), 0.0, 255.0);
            else if (mapStorageType == 5u) { float f = rdF32(ep); v = (f <= 1.0f) ? (f * 255.0) : f; }
            else if (mapStorageType == 7u) { double d = rdF64(ep); v = (d <= 1.0) ? (d * 255.0) : d; }
            lut8[i] = static_cast<uint8_t>(std::clamp(v + 0.5, 0.0, 255.0));
        }
        pos += static_cast<size_t>(totalMapBytes);
    }

    // 2. Skip explicit coordinate location array if present (VFF_LOC_EXPLICIT == 2)
    if (locType == 2u && locDim > 0u) {
        const uint64_t locBytes = static_cast<uint64_t>(locDim) * w * h * 4ull;
        if (locBytes > size - pos) return false;
        pos += static_cast<size_t>(locBytes);
    }

    const size_t planePixels = static_cast<size_t>(w) * h;
    if (planePixels > 64ull * 1024ull * 1024ull) return false;

    // 3. Decode 1-bit packed bitmap (VFF_TYP_BIT == 0)
    if (dataStorageType == 0u) {
        const size_t rowBytes = (w + 7u) / 8u;
        const uint64_t bandBytes = static_cast<uint64_t>(rowBytes) * h;
        if (bandBytes * numBands > size - pos) return false;

        outInfo = {};
        outInfo.width = w;
        outInfo.height = h;
        outInfo.origWidth = w;
        outInfo.origHeight = h;
        outInfo.channels = 1u;
        outInfo.bitsPerChannel = 1u;
        outInfo.formatName = L"Khoros VIFF Image (1-bit .viff)";
        outInfo.bgraPixels.resize(planePixels * 4u);

        const uint8_t* bp = data + pos;
        for (uint32_t y = 0; y < h; ++y) {
            const uint8_t* row = bp + static_cast<size_t>(y) * rowBytes;
            for (uint32_t x = 0; x < w; ++x) {
                // VIFF 1-bit packs LSB-first on little-endian DEC/x86, MSB-first on big-endian IEEE
                const uint8_t bit = be ? ((row[x >> 3] >> (7u - (x & 7u))) & 1u)
                                       : ((row[x >> 3] >> (x & 7u)) & 1u);
                uint8_t r = bit ? 255u : 0u, g = r, b = r;
                if (hasMap && bit < mapColSize) {
                    r = lut8[0u * mapColSize + bit];
                    g = (mapRowSize >= 3u) ? lut8[1u * mapColSize + bit] : r;
                    b = (mapRowSize >= 3u) ? lut8[2u * mapColSize + bit] : r;
                }
                WritePremulBGRA(&outInfo.bgraPixels[(static_cast<size_t>(y) * w + x) * 4u], r, g, b, 255u);
            }
        }
        outInfo.valid = true;
        return true;
    }

    const size_t elemBytes = storageBytes(dataStorageType, false);
    if (elemBytes == 0u) return false;
    const uint32_t usedBands = std::min<uint32_t>(numBands, 4u);
    const uint64_t neededBytes = static_cast<uint64_t>(usedBands) * planePixels * elemBytes;
    if (neededBytes > size - pos) return false;

    const uint8_t* imgBase = data + pos;

    // 4. Colormap-mapped single-band (or multi-band) integer image
    if (hasMap && (dataStorageType == 1u || dataStorageType == 2u || dataStorageType == 4u)) {
        outInfo = {};
        outInfo.width = w;
        outInfo.height = h;
        outInfo.origWidth = w;
        outInfo.origHeight = h;
        outInfo.channels = (mapRowSize >= 3u) ? 3u : 1u;
        outInfo.bitsPerChannel = 8u;
        outInfo.formatName = L"Khoros VIFF Colormapped Image (.viff)";
        outInfo.bgraPixels.resize(planePixels * 4u);

        for (size_t i = 0; i < planePixels; ++i) {
            const uint8_t* ep = imgBase + i * elemBytes;
            uint32_t idx = (elemBytes == 1u) ? ep[0] : (elemBytes == 2u ? rd16(ep) : rd32(ep));
            idx = std::min<uint32_t>(idx, mapColSize - 1u);
            const uint8_t r = lut8[0u * mapColSize + idx];
            const uint8_t g = (mapRowSize >= 3u) ? lut8[1u * mapColSize + idx] : r;
            const uint8_t b = (mapRowSize >= 3u) ? lut8[2u * mapColSize + idx] : r;
            const uint8_t a = (mapRowSize >= 4u) ? lut8[3u * mapColSize + idx] : 255u;
            WritePremulBGRA(&outInfo.bgraPixels[i * 4u], r, g, b, a);
        }
        outInfo.valid = true;
        return true;
    }

    // 5. Direct / Scientific multi-band raster (u8, i16/u16, i32, float, double)
    outInfo = {};
    outInfo.width = w;
    outInfo.height = h;
    outInfo.origWidth = w;
    outInfo.origHeight = h;
    outInfo.channels = (usedBands >= 4u) ? 4u : ((usedBands >= 3u) ? 3u : 1u);
    outInfo.bitsPerChannel = static_cast<uint32_t>(elemBytes * 8u);
    outInfo.formatName = L"Khoros VIFF Raster (.viff)";
    outInfo.bgraPixels.resize(planePixels * 4u);

    if (dataStorageType == 1u) { // Direct 8-bit bands
        for (size_t i = 0; i < planePixels; ++i) {
            const uint8_t r = imgBase[i];
            const uint8_t g = (usedBands >= 3u) ? imgBase[planePixels + i] : r;
            const uint8_t b = (usedBands >= 3u) ? imgBase[planePixels * 2u + i] : r;
            const uint8_t a = (usedBands >= 4u) ? imgBase[planePixels * 3u + i] : 255u;
            WritePremulBGRA(&outInfo.bgraPixels[i * 4u], r, g, b, a);
        }
    } else {
        auto readScalar = [&](size_t band, size_t idx) -> double {
            const uint8_t* ep = imgBase + (band * planePixels + idx) * elemBytes;
            if (dataStorageType == 2u) return static_cast<double>(static_cast<int16_t>(rd16(ep)));
            if (dataStorageType == 4u) return static_cast<double>(static_cast<int32_t>(rd32(ep)));
            if (dataStorageType == 5u) { float f = rdF32(ep); return std::isfinite(f) ? static_cast<double>(f) : 0.0; }
            double d = rdF64(ep);
            return std::isfinite(d) ? d : 0.0;
        };

        const uint32_t colorBands = (usedBands >= 3u) ? 3u : 1u;
        double minV = 1e300, maxV = -1e300;
        for (uint32_t b = 0; b < colorBands; ++b) {
            for (size_t i = 0; i < planePixels; ++i) {
                double v = readScalar(b, i);
                if (v < minV) minV = v;
                if (v > maxV) maxV = v;
            }
        }
        // Preserve canonical [0..1] or [0..255] or [0..65535] dynamic range if non-negative and within standard bounds,
        // otherwise min-max normalize scientific sensor arrays cleanly!
        double lo = minV, span = (maxV > minV) ? (maxV - minV) : 1.0;
        if (minV >= 0.0 && maxV <= 1.0 && (dataStorageType == 5u || dataStorageType == 9u)) {
            lo = 0.0; span = 1.0;
        } else if (minV >= 0.0 && maxV <= 255.0 && maxV > 1.0) {
            lo = 0.0; span = 255.0;
        }

        for (size_t i = 0; i < planePixels; ++i) {
            auto norm8 = [&](uint32_t band) -> uint8_t {
                double t = (readScalar(band, i) - lo) / span;
                return static_cast<uint8_t>(std::clamp(t * 255.0 + 0.5, 0.0, 255.0));
            };
            const uint8_t r = norm8(0u);
            const uint8_t g = (colorBands >= 3u) ? norm8(1u) : r;
            const uint8_t b = (colorBands >= 3u) ? norm8(2u) : r;
            WritePremulBGRA(&outInfo.bgraPixels[i * 4u], r, g, b, 255u);
        }
    }

    outInfo.valid = true;
    return true;
}

} // namespace Formats
