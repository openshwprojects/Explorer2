#pragma once
// GeoTiffDem.h — GeoTIFF 3D Digital Elevation Model (.geotiff / DEM .tif) reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {
namespace BspGeoDetail {

// ============================================================================
// 2. GeoTIFF 3D Digital Elevation Model (.geotiff / single-band elevation .tif)
// ============================================================================
inline bool InspectGeoTiffHeader(
    const uint8_t* data,
    size_t size,
    bool requireGeoTags,
    uint32_t& outW,
    uint32_t& outH,
    uint16_t& outBits,
    uint16_t& outSampleFmt,
    uint16_t& outComp,
    uint16_t& outSamplesPerPx,
    double& outScaleX,
    double& outScaleY,
    double& outNoData,
    bool& outHasNoData,
    std::vector<uint64_t>& outStripOffsets,
    std::vector<uint64_t>& outStripCounts,
    bool& outBigEndian)
{
    if (!data || size < 16) return false;
    bool be = false;
    if (data[0] == 'I' && data[1] == 'I') be = false;
    else if (data[0] == 'M' && data[1] == 'M') be = true;
    else return false;
    outBigEndian = be;

    auto rd16 = [&](size_t o) -> uint16_t { return be ? RdU16BE(data + o) : RdU16LE(data + o); };
    auto rd32 = [&](size_t o) -> uint32_t { return be ? RdU32BE(data + o) : RdU32LE(data + o); };
    auto rd64 = [&](size_t o) -> uint64_t {
        return be ? ((static_cast<uint64_t>(RdU32BE(data + o)) << 32) | RdU32BE(data + o + 4))
                  : (static_cast<uint64_t>(RdU32LE(data + o)) | (static_cast<uint64_t>(RdU32LE(data + o + 4)) << 32));
    };
    auto rdF64 = [&](size_t o) -> double {
        uint64_t u = rd64(o);
        double d = 0.0;
        std::memcpy(&d, &u, 8);
        return d;
    };

    uint16_t magic = rd16(2);
    bool isBigTiff = (magic == 43);
    if (magic != 42 && magic != 43) return false;

    uint64_t ifdOff = 0;
    if (isBigTiff) {
        if (size < 16 || rd16(4) != 8 || rd16(6) != 0) return false;
        ifdOff = rd64(8);
    } else {
        ifdOff = rd32(4);
    }
    if (ifdOff < 8 || ifdOff + (isBigTiff ? 8u : 2u) > size) return false;

    uint64_t nEntries = isBigTiff ? rd64(static_cast<size_t>(ifdOff)) : rd16(static_cast<size_t>(ifdOff));
    size_t entPos = static_cast<size_t>(ifdOff) + (isBigTiff ? 8u : 2u);
    size_t entStride = isBigTiff ? 20u : 12u;
    if (nEntries == 0 || nEntries > 4096 || entPos + nEntries * entStride > size) return false;

    outW = outH = 0;
    outBits = 8;
    outSampleFmt = 1;
    outComp = 1;
    outSamplesPerPx = 1;
    outScaleX = outScaleY = 1.0;
    outNoData = -32768.0;
    outHasNoData = false;
    outStripOffsets.clear();
    outStripCounts.clear();
    bool hasGeoTag = false;

    auto typeElemSz = [](uint16_t t) -> size_t {
        switch (t) {
        case 1: case 2: case 6: case 7: return 1;
        case 3: case 8: return 2;
        case 4: case 9: case 11: return 4;
        case 5: case 10: case 12: case 16: case 17: return 8;
        default: return 0;
        }
    };

    for (uint64_t i = 0; i < nEntries; ++i) {
        size_t ep = entPos + static_cast<size_t>(i) * entStride;
        uint16_t tag  = rd16(ep + 0);
        uint16_t type = rd16(ep + 2);
        uint64_t cnt  = isBigTiff ? rd64(ep + 4) : rd32(ep + 4);
        size_t valFieldOff = ep + (isBigTiff ? 12u : 8u);
        size_t inlineCap   = isBigTiff ? 8u : 4u;
        size_t esz = typeElemSz(type);
        if (esz == 0 || cnt > 16000000) continue;
        uint64_t totalB = cnt * esz;
        size_t dataOff = valFieldOff;
        if (totalB > inlineCap) {
            uint64_t ptr = isBigTiff ? rd64(valFieldOff) : rd32(valFieldOff);
            if (ptr + totalB > size) continue;
            dataOff = static_cast<size_t>(ptr);
        }

        auto rdUIntAt = [&](size_t idx) -> uint64_t {
            size_t o = dataOff + idx * esz;
            if (type == 1) return data[o];
            if (type == 3) return rd16(o);
            if (type == 4) return rd32(o);
            if (type == 16) return rd64(o);
            return 0;
        };

        if (tag == 256 && cnt >= 1) outW = static_cast<uint32_t>(rdUIntAt(0));
        else if (tag == 257 && cnt >= 1) outH = static_cast<uint32_t>(rdUIntAt(0));
        else if (tag == 258 && cnt >= 1) outBits = static_cast<uint16_t>(rdUIntAt(0));
        else if (tag == 259 && cnt >= 1) outComp = static_cast<uint16_t>(rdUIntAt(0));
        else if (tag == 277 && cnt >= 1) outSamplesPerPx = static_cast<uint16_t>(rdUIntAt(0));
        else if (tag == 339 && cnt >= 1) outSampleFmt = static_cast<uint16_t>(rdUIntAt(0));
        else if ((tag == 273 || tag == 324) && cnt >= 1) {
            for (size_t k = 0; k < static_cast<size_t>(cnt); ++k) outStripOffsets.push_back(rdUIntAt(k));
        } else if ((tag == 279 || tag == 325) && cnt >= 1) {
            for (size_t k = 0; k < static_cast<size_t>(cnt); ++k) outStripCounts.push_back(rdUIntAt(k));
        } else if (tag == 33550 && type == 12 && cnt >= 2) { // ModelPixelScaleTag
            hasGeoTag = true;
            outScaleX = rdF64(dataOff + 0);
            outScaleY = rdF64(dataOff + 8);
        } else if (tag == 33922 || tag == 34264 || tag == 34735) {
            hasGeoTag = true;
        } else if (tag == 42113 && type == 2 && cnt >= 1) { // GDAL_NODATA ASCII string
            hasGeoTag = true;
            std::string nd(reinterpret_cast<const char*>(data + dataOff), static_cast<size_t>(cnt));
            char* endp = nullptr;
            double v = std::strtod(nd.c_str(), &endp);
            if (endp != nd.c_str() && std::isfinite(v)) { outNoData = v; outHasNoData = true; }
        }
    }

    if (outW < 2 || outH < 2 || outSamplesPerPx != 1 || outStripOffsets.empty()) return false;
    if (requireGeoTags && !hasGeoTag) return false;
    // For automatic .tif 3D disambiguation, require GeoTag AND (int16/int32/float32 OR 16/32-bit elevation)
    if (requireGeoTags && outSampleFmt == 1 && outBits < 16) return false;
    return true;
}

inline bool IsLikelyGeoTiffDemBuffer(const uint8_t* data, size_t size) {
    uint32_t w = 0, h = 0;
    uint16_t bits = 0, sfmt = 0, comp = 0, spp = 0;
    double sx = 1, sy = 1, nd = 0;
    bool hasNd = false, be = false;
    std::vector<uint64_t> offs, cnts;
    return InspectGeoTiffHeader(data, size, true, w, h, bits, sfmt, comp, spp, sx, sy, nd, hasNd, offs, cnts, be);
}

inline bool ParseGeoTiffDemMesh(const uint8_t* data, size_t size, bool requireGeoTags, LoadedMesh3D& mesh) {
    uint32_t w = 0, h = 0;
    uint16_t bits = 0, sfmt = 0, comp = 0, spp = 0;
    double scaleX = 1.0, scaleY = 1.0, noData = -32768.0;
    bool hasNoData = false, be = false;
    std::vector<uint64_t> offs, cnts;
    if (!InspectGeoTiffHeader(data, size, requireGeoTags, w, h, bits, sfmt, comp, spp, scaleX, scaleY, noData, hasNoData, offs, cnts, be)) {
        return false;
    }
    if (w > 16384 || h > 16384 || (bits != 16 && bits != 32 && bits != 64)) return false;

    size_t bytesPerSample = bits / 8u;
    uint64_t expectedBytes = static_cast<uint64_t>(w) * static_cast<uint64_t>(h) * bytesPerSample;
    if (expectedBytes > 128 * 1024 * 1024) return false;

    std::vector<uint8_t> rawPixels;
    rawPixels.reserve(static_cast<size_t>(expectedBytes));

    for (size_t s = 0; s < offs.size(); ++s) {
        uint64_t so = offs[s];
        uint64_t sc = (s < cnts.size()) ? cnts[s] : (expectedBytes - rawPixels.size());
        if (so > size || sc > size - so) return false;
        if (comp == 1) {
            rawPixels.insert(rawPixels.end(), data + so, data + so + static_cast<size_t>(sc));
        } else if (comp == 8 || comp == 32946) { // Deflate / Zlib
            std::vector<uint8_t> blk;
            if (!ZipDeflate::InflateZlib(data + so, static_cast<size_t>(sc), blk, static_cast<size_t>(expectedBytes) + 4096) &&
                !ZipDeflate::InflateRaw(data + so, static_cast<size_t>(sc), blk, static_cast<size_t>(expectedBytes) + 4096)) {
                return false;
            }
            rawPixels.insert(rawPixels.end(), blk.begin(), blk.end());
        } else {
            return false;
        }
    }
    if (rawPixels.size() < static_cast<size_t>(expectedBytes)) return false;

    auto sampleElev = [&](uint32_t x, uint32_t y, float& outE) -> bool {
        const uint8_t* p = rawPixels.data() + (static_cast<size_t>(y) * w + x) * bytesPerSample;
        double val = 0.0;
        if (bits == 16) {
            uint16_t u = be ? RdU16BE(p) : RdU16LE(p);
            val = (sfmt == 2) ? static_cast<double>(static_cast<int16_t>(u)) : static_cast<double>(u);
            if (sfmt == 2 && static_cast<int16_t>(u) == -32768) return false;
        } else if (bits == 32) {
            uint32_t u = be ? RdU32BE(p) : RdU32LE(p);
            if (sfmt == 3) { float f = 0; std::memcpy(&f, &u, 4); val = f; }
            else if (sfmt == 2) val = static_cast<int32_t>(u);
            else val = u;
        } else {
            uint64_t u = be ? ((static_cast<uint64_t>(RdU32BE(p)) << 32) | RdU32BE(p + 4))
                            : (static_cast<uint64_t>(RdU32LE(p)) | (static_cast<uint64_t>(RdU32LE(p + 4)) << 32));
            std::memcpy(&val, &u, 8);
        }
        if (!std::isfinite(val)) return false;
        if (hasNoData && std::fabs(val - noData) <= std::max(1e-5, std::fabs(noData) * 1e-6)) return false;
        outE = static_cast<float>(val);
        return true;
    };

    uint32_t step = std::max<uint32_t>(1, std::max(w, h) / 256);
    std::vector<uint32_t> xs, ys;
    for (uint32_t x = 0; x < w; x += step) xs.push_back(x);
    if (xs.back() != w - 1) xs.push_back(w - 1);
    for (uint32_t y = 0; y < h; y += step) ys.push_back(y);
    if (ys.back() != h - 1) ys.push_back(h - 1);

    float sx = (std::isfinite(scaleX) && std::fabs(scaleX) > 1e-7 && std::fabs(scaleX) < 1e5) ? static_cast<float>(std::fabs(scaleX)) : 1.0f;
    float sy = (std::isfinite(scaleY) && std::fabs(scaleY) > 1e-7 && std::fabs(scaleY) < 1e5) ? static_cast<float>(std::fabs(scaleY)) : 1.0f;

    mesh.vertices.clear();
    mesh.tris.clear();
    size_t gw = xs.size(), gh = ys.size();
    std::vector<int32_t> vMap(gw * gh, -1);

    for (size_t ry = 0; ry < gh; ++ry) {
        for (size_t cx = 0; cx < gw; ++cx) {
            float el = 0.0f;
            if (!sampleElev(xs[cx], ys[ry], el)) continue;
            vMap[ry * gw + cx] = static_cast<int32_t>(mesh.vertices.size());
            mesh.vertices.push_back({ static_cast<float>(xs[cx]) * sx, el, static_cast<float>(ys[ry]) * sy });
        }
    }

    for (size_t ry = 0; ry + 1 < gh; ++ry) {
        for (size_t cx = 0; cx + 1 < gw; ++cx) {
            int32_t i00 = vMap[ry * gw + cx];
            int32_t i10 = vMap[ry * gw + cx + 1];
            int32_t i01 = vMap[(ry + 1) * gw + cx];
            int32_t i11 = vMap[(ry + 1) * gw + cx + 1];
            if (i00 >= 0 && i01 >= 0 && i10 >= 0) mesh.tris.push_back({ static_cast<uint32_t>(i00), static_cast<uint32_t>(i01), static_cast<uint32_t>(i10), {} });
            if (i10 >= 0 && i01 >= 0 && i11 >= 0) mesh.tris.push_back({ static_cast<uint32_t>(i10), static_cast<uint32_t>(i01), static_cast<uint32_t>(i11), {} });
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"GeoTIFF 3D Digital Elevation Model (.tif)";
    return true;
}

} // namespace BspGeoDetail
} // namespace Formats
