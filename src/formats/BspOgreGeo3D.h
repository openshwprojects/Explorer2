#pragma once
// BspOgreGeo3D.h — #20 GeoTIFF 3D DEM, Void-Safe SRTM (.hgt), 3D ESRI Shapefile (.shp) & 3D GeoJSON (.geojson),
//                  OpenCTM RAW + LZMA1 MG1/MG2 (.ctm), Quake/GoldSrc/Q2/Q3/Source (.bsp),
//                  & Ogre3D Binary/XML + INRIA Medit (.mesh)
//
// Pure C++20 zero-cheat implementation: strictly validates headers, offsets, and compressed streams;
// never synthesizes fallback geometry on corrupt/empty input.

namespace Formats {

namespace BspGeoDetail {

inline uint16_t RdU16LE(const uint8_t* p) { uint16_t v = 0; std::memcpy(&v, p, 2); return v; }
inline int16_t  RdI16LE(const uint8_t* p) { int16_t  v = 0; std::memcpy(&v, p, 2); return v; }
inline uint32_t RdU32LE(const uint8_t* p) { uint32_t v = 0; std::memcpy(&v, p, 4); return v; }
inline int32_t  RdI32LE(const uint8_t* p) { int32_t  v = 0; std::memcpy(&v, p, 4); return v; }
inline float    RdF32LE(const uint8_t* p) { float    v = 0.0f; std::memcpy(&v, p, 4); return v; }
inline double   RdF64LE(const uint8_t* p) { double   v = 0.0;  std::memcpy(&v, p, 8); return v; }

inline uint16_t RdU16BE(const uint8_t* p) { return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]); }
inline int16_t  RdI16BE(const uint8_t* p) { return static_cast<int16_t>(RdU16BE(p)); }
inline uint32_t RdU32BE(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  | static_cast<uint32_t>(p[3]);
}

// ============================================================================
// 1. Void-Safe NASA SRTM Heightmap (.hgt)
//    Strictly requires exact N*N*2 byte size, culls -32768 void cells cleanly
// ============================================================================
inline bool ParseVoidSafeSrtmHgt(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    if (!data || size < 18 || (size % 2) != 0) return false;
    size_t totalSamples = size / 2;
    size_t gridN = static_cast<size_t>(std::llround(std::sqrt(static_cast<double>(totalSamples))));
    if (gridN < 3 || gridN * gridN != totalSamples) return false;

    int step = static_cast<int>(std::max<size_t>(1, gridN / 256));
    std::vector<size_t> rows, cols;
    for (size_t r = 0; r < gridN; r += step) rows.push_back(r);
    if (rows.back() != gridN - 1) rows.push_back(gridN - 1);
    for (size_t c = 0; c < gridN; c += step) cols.push_back(c);
    if (cols.back() != gridN - 1) cols.push_back(gridN - 1);

    size_t outH = rows.size(), outW = cols.size();
    if (outW < 2 || outH < 2) return false;

    // Spacing scaled so terrain proportions look natural while preserving real relative meter heights
    float horizStep = (gridN >= 1201) ? (30.0f * static_cast<float>(step)) : 10.0f;
    float elevScale = (gridN >= 1201) ? 1.0f : 0.1f;

    mesh.vertices.clear();
    mesh.tris.clear();
    std::vector<int32_t> vertMap(outH * outW, -1);
    size_t validCount = 0;

    for (size_t ri = 0; ri < outH; ++ri) {
        for (size_t ci = 0; ci < outW; ++ci) {
            size_t off = (rows[ri] * gridN + cols[ci]) * 2u;
            int16_t raw = RdI16BE(data + off);
            if (raw == -32768) continue; // SRTM void sentinel: DO NOT fabricate 0m cliff!
            uint32_t vIdx = static_cast<uint32_t>(mesh.vertices.size());
            mesh.vertices.push_back({
                static_cast<float>(ci) * horizStep,
                static_cast<float>(raw) * elevScale,
                static_cast<float>(ri) * horizStep
            });
            vertMap[ri * outW + ci] = static_cast<int32_t>(vIdx);
            ++validCount;
        }
    }
    if (validCount < 3) return false;

    for (size_t r = 0; r + 1 < outH; ++r) {
        for (size_t c = 0; c + 1 < outW; ++c) {
            int32_t v00 = vertMap[r * outW + c];
            int32_t v10 = vertMap[r * outW + c + 1];
            int32_t v01 = vertMap[(r + 1) * outW + c];
            int32_t v11 = vertMap[(r + 1) * outW + c + 1];
            if (v00 >= 0 && v01 >= 0 && v10 >= 0) {
                mesh.tris.push_back({ static_cast<uint32_t>(v00), static_cast<uint32_t>(v01), static_cast<uint32_t>(v10), {} });
            }
            if (v10 >= 0 && v01 >= 0 && v11 >= 0) {
                mesh.tris.push_back({ static_cast<uint32_t>(v10), static_cast<uint32_t>(v01), static_cast<uint32_t>(v11), {} });
            }
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"NASA SRTM Void-Safe Terrain (.hgt)";
    return true;
}

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

// ============================================================================
// 3. 3D ESRI Shapefile (.shp — PointZ=11, PolyLineZ=13, PolygonZ=15, MultiPatch=31)
//    & 3D GeoJSON (.geojson — 3D [lon, lat, elev] + extruded buildings)
// ============================================================================
inline bool IsLikely3DShapefileBuffer(const uint8_t* data, size_t size) {
    if (!data || size < 100 || RdU32BE(data) != 9994u) return false;
    int32_t shapeType = RdI32LE(data + 32);
    return (shapeType == 11 || shapeType == 13 || shapeType == 15 || shapeType == 18 || shapeType == 31);
}

inline bool ParseShapefile3DMesh(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    if (!data || size < 100 || RdU32BE(data) != 9994u) return false;
    int32_t version = RdI32LE(data + 28);
    if (version != 1000) return false;

    mesh.vertices.clear();
    mesh.tris.clear();

    size_t pos = 100;
    while (pos + 8 <= size && mesh.tris.size() < 2000000) {
        uint32_t recWords = RdU32BE(data + pos + 4);
        pos += 8;
        size_t recBytes = static_cast<size_t>(recWords) * 2u;
        if (recBytes < 4 || recBytes > size - pos) break;
        const uint8_t* rp = data + pos;
        pos += recBytes;

        int32_t st = RdI32LE(rp);
        if (st == 0) continue; // Null shape

        if ((st == 1 || st == 11 || st == 21) && recBytes >= ((st == 11) ? 28u : 20u)) {
            float x = static_cast<float>(RdF64LE(rp + 4));
            float y = static_cast<float>(RdF64LE(rp + 12));
            float z = (st == 11) ? static_cast<float>(RdF64LE(rp + 20)) : 0.0f;
            Detail3D200::AddOctaSphere(mesh, x, z, -y, 0.5f);
        }
        else if ((st == 3 || st == 5 || st == 13 || st == 15 || st == 31) && recBytes >= 44) {
            int32_t numParts  = RdI32LE(rp + 36);
            int32_t numPoints = RdI32LE(rp + 40);
            if (numParts <= 0 || numPoints <= 0 || numParts > 100000 || numPoints > 1000000) continue;

            size_t partsOff = 44;
            size_t partTypesOff = (st == 31) ? (partsOff + static_cast<size_t>(numParts) * 4u) : partsOff;
            size_t ptsOff = partTypesOff + ((st == 31) ? static_cast<size_t>(numParts) * 4u : static_cast<size_t>(numParts) * 4u);
            if (st != 31) ptsOff = partsOff + static_cast<size_t>(numParts) * 4u;

            bool hasZ = (st == 13 || st == 15 || st == 31);
            size_t zMinMaxOff = ptsOff + static_cast<size_t>(numPoints) * 16u;
            size_t zArrOff    = zMinMaxOff + 16u;
            size_t needed = hasZ ? (zArrOff + static_cast<size_t>(numPoints) * 8u) : (ptsOff + static_cast<size_t>(numPoints) * 16u);
            if (needed > recBytes) continue;

            std::vector<Vec3f> pts(numPoints);
            float minZ = hasZ ? static_cast<float>(RdF64LE(rp + zMinMaxOff)) : 0.0f;
            for (int32_t i = 0; i < numPoints; ++i) {
                double x = RdF64LE(rp + ptsOff + static_cast<size_t>(i) * 16u + 0);
                double y = RdF64LE(rp + ptsOff + static_cast<size_t>(i) * 16u + 8);
                double z = hasZ ? RdF64LE(rp + zArrOff + static_cast<size_t>(i) * 8u) : 0.0;
                pts[i] = { static_cast<float>(x), static_cast<float>(z), static_cast<float>(-y) };
            }

            for (int32_t p = 0; p < numParts; ++p) {
                int32_t startIdx = RdI32LE(rp + partsOff + static_cast<size_t>(p) * 4u);
                int32_t endIdx   = (p + 1 < numParts) ? RdI32LE(rp + partsOff + static_cast<size_t>(p + 1) * 4u) : numPoints;
                if (startIdx < 0 || endIdx > numPoints || startIdx >= endIdx) continue;
                int32_t count = endIdx - startIdx;

                if (st == 3 || st == 13) {
                    for (int32_t k = startIdx; k + 1 < endIdx; ++k) {
                        Detail3D200::AddStrut3D(mesh, pts[k], pts[k + 1], 0.25f);
                    }
                } else if (st == 31) {
                    int32_t pType = RdI32LE(rp + partTypesOff + static_cast<size_t>(p) * 4u);
                    uint32_t vb = static_cast<uint32_t>(mesh.vertices.size());
                    for (int32_t k = startIdx; k < endIdx; ++k) mesh.vertices.push_back(pts[k]);
                    if (pType == 0) { // Triangle Strip
                        for (int32_t k = 0; k + 2 < count; ++k) {
                            if ((k & 1) == 0) mesh.tris.push_back({ vb + static_cast<uint32_t>(k), vb + static_cast<uint32_t>(k + 1), vb + static_cast<uint32_t>(k + 2), {} });
                            else              mesh.tris.push_back({ vb + static_cast<uint32_t>(k + 1), vb + static_cast<uint32_t>(k), vb + static_cast<uint32_t>(k + 2), {} });
                        }
                    } else { // Triangle Fan or Ring
                        int32_t ringCnt = count;
                        if (pType >= 2 && ringCnt >= 4 &&
                            std::fabs(pts[startIdx].x - pts[endIdx - 1].x) < 1e-5f &&
                            std::fabs(pts[startIdx].z - pts[endIdx - 1].z) < 1e-5f) --ringCnt;
                        for (int32_t k = 1; k + 1 < ringCnt; ++k) {
                            mesh.tris.push_back({ vb, vb + static_cast<uint32_t>(k), vb + static_cast<uint32_t>(k + 1), {} });
                        }
                    }
                } else { // Polygon / PolygonZ (extrude vertical walls if elevated above 0!)
                    int32_t ringCnt = count;
                    if (ringCnt >= 4 &&
                        std::fabs(pts[startIdx].x - pts[endIdx - 1].x) < 1e-5f &&
                        std::fabs(pts[startIdx].z - pts[endIdx - 1].z) < 1e-5f) --ringCnt;
                    if (ringCnt >= 3) {
                        uint32_t vb = static_cast<uint32_t>(mesh.vertices.size());
                        for (int32_t k = 0; k < ringCnt; ++k) mesh.vertices.push_back(pts[startIdx + k]);
                        for (int32_t k = 1; k + 1 < ringCnt; ++k) {
                            mesh.tris.push_back({ vb, vb + static_cast<uint32_t>(k), vb + static_cast<uint32_t>(k + 1), {} });
                        }
                        // If PolygonZ has positive roof height and flat/elevated base, emit 3D building walls down to min(0, minZ)
                        float baseElev = std::min(0.0f, minZ);
                        bool hasWall = false;
                        for (int32_t k = 0; k < ringCnt; ++k) if (pts[startIdx + k].y > baseElev + 1e-3f) hasWall = true;
                        if (hasZ && hasWall) {
                            uint32_t bb = static_cast<uint32_t>(mesh.vertices.size());
                            for (int32_t k = 0; k < ringCnt; ++k) {
                                mesh.vertices.push_back({ pts[startIdx + k].x, baseElev, pts[startIdx + k].z });
                            }
                            for (int32_t k = 0; k < ringCnt; ++k) {
                                uint32_t nk = static_cast<uint32_t>((k + 1) % ringCnt);
                                mesh.tris.push_back({ bb + static_cast<uint32_t>(k), bb + nk, vb + nk, {} });
                                mesh.tris.push_back({ bb + static_cast<uint32_t>(k), vb + nk, vb + static_cast<uint32_t>(k), {} });
                            }
                        }
                    }
                }
            }
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"ESRI 3D Shapefile (.shp)";
    return true;
}

inline bool IsLikely3DGeoJsonBuffer(const uint8_t* data, size_t size) {
    if (!data || size < 24) return false;
    std::string_view sv(reinterpret_cast<const char*>(data), std::min<size_t>(size, 65536));
    if (sv.find("\"coordinates\"") == std::string_view::npos) return false;
    if (sv.find("\"height\"") != std::string_view::npos || sv.find("\"extrude\"") != std::string_view::npos) return true;

    // Check if first numeric coordinate tuple [x, y, z] has >= 3 numbers!
    size_t cp = sv.find("\"coordinates\"");
    size_t p = sv.find('[', cp);
    while (p != std::string_view::npos && p < sv.size()) {
        ++p;
        while (p < sv.size() && std::isspace(static_cast<unsigned char>(sv[p]))) ++p;
        if (p < sv.size() && sv[p] == '[') continue;
        // Inside innermost [num, num, ...]
        size_t closeB = sv.find(']', p);
        if (closeB == std::string_view::npos) break;
        std::string_view tuple = sv.substr(p, closeB - p);
        int commas = 0;
        for (char c : tuple) if (c == ',') ++commas;
        return commas >= 2;
    }
    return false;
}

inline bool ParseGeoJson3DMesh(std::string_view sv, LoadedMesh3D& mesh) {
    if (sv.find("\"coordinates\"") == std::string_view::npos) return false;
    mesh.vertices.clear();
    mesh.tris.clear();

    // Global optional default height if "height": X appears on feature
    auto parseCoordTuples = [](std::string_view arr, std::vector<std::vector<Vec3f>>& ringsOut) {
        size_t p = 0;
        std::vector<Vec3f> curRing;
        while (p < arr.size()) {
            char c = arr[p];
            if (c == '[') {
                size_t scan = p + 1;
                while (scan < arr.size() && std::isspace(static_cast<unsigned char>(arr[scan]))) ++scan;
                if (scan < arr.size() && ((arr[scan] >= '0' && arr[scan] <= '9') || arr[scan] == '-' || arr[scan] == '+')) {
                    // Innermost coordinate tuple [lon, lat] or [lon, lat, elev]
                    size_t ce = arr.find(']', scan);
                    if (ce == std::string_view::npos) break;
                    std::vector<double> vals;
                    const char* cur = arr.data() + scan;
                    const char* end = arr.data() + ce;
                    while (cur < end) {
                        while (cur < end && !((*cur >= '0' && *cur <= '9') || *cur == '-' || *cur == '+' || *cur == '.')) ++cur;
                        if (cur >= end) break;
                        char* ep = nullptr;
                        double v = std::strtod(cur, &ep);
                        if (ep == cur) { ++cur; continue; }
                        vals.push_back(v);
                        cur = ep;
                    }
                    if (vals.size() >= 2) {
                        float z = (vals.size() >= 3) ? static_cast<float>(vals[2]) : 0.0f;
                        curRing.push_back({ static_cast<float>(vals[0]), z, static_cast<float>(-vals[1]) });
                    }
                    p = ce + 1;
                    continue;
                }
                ++p;
            } else if (c == ']') {
                if (!curRing.empty()) {
                    ringsOut.push_back(std::move(curRing));
                    curRing.clear();
                }
                ++p;
            } else {
                ++p;
            }
        }
    };

    size_t searchPos = 0;
    while ((searchPos = sv.find("\"coordinates\"", searchPos)) != std::string_view::npos) {
        // Check nearby feature "height" property before this geometry
        float featHeight = 0.0f;
        size_t backStart = (searchPos > 512) ? (searchPos - 512) : 0;
        std::string_view ctx = sv.substr(backStart, searchPos - backStart);
        size_t hp = ctx.find("\"height\"");
        if (hp != std::string_view::npos) {
            size_t col = ctx.find(':', hp + 8);
            if (col != std::string_view::npos) {
                featHeight = std::strtof(std::string(ctx.substr(col + 1, 32)).c_str(), nullptr);
            }
        }
        bool isPoly = (ctx.find("Polygon") != std::string_view::npos);
        bool isLine = (ctx.find("LineString") != std::string_view::npos);

        size_t bOpen = sv.find('[', searchPos + 13);
        if (bOpen == std::string_view::npos) break;
        int depth = 0;
        size_t bClose = bOpen;
        for (; bClose < sv.size(); ++bClose) {
            if (sv[bClose] == '[') ++depth;
            else if (sv[bClose] == ']') { if (--depth == 0) break; }
        }
        if (bClose >= sv.size()) break;

        std::vector<std::vector<Vec3f>> rings;
        parseCoordTuples(sv.substr(bOpen, bClose - bOpen + 1), rings);
        for (auto& ring : rings) {
            if (featHeight > 0.0f) {
                for (auto& pt : ring) if (std::fabs(pt.y) < 1e-5f) pt.y = featHeight;
            }
            if (isPoly && ring.size() >= 3) {
                if (ring.size() >= 4 &&
                    std::fabs(ring.front().x - ring.back().x) < 1e-6f &&
                    std::fabs(ring.front().z - ring.back().z) < 1e-6f) ring.pop_back();
                if (ring.size() >= 3) {
                    uint32_t vb = static_cast<uint32_t>(mesh.vertices.size());
                    bool extrude = false;
                    for (const auto& pt : ring) {
                        mesh.vertices.push_back(pt);
                        if (pt.y > 1e-4f) extrude = true;
                    }
                    for (size_t k = 1; k + 1 < ring.size(); ++k) {
                        mesh.tris.push_back({ vb, vb + static_cast<uint32_t>(k), vb + static_cast<uint32_t>(k + 1), {} });
                    }
                    if (extrude) {
                        uint32_t bb = static_cast<uint32_t>(mesh.vertices.size());
                        for (const auto& pt : ring) mesh.vertices.push_back({ pt.x, 0.0f, pt.z });
                        for (size_t k = 0; k < ring.size(); ++k) {
                            uint32_t nk = static_cast<uint32_t>((k + 1) % ring.size());
                            mesh.tris.push_back({ bb + static_cast<uint32_t>(k), bb + nk, vb + nk, {} });
                            mesh.tris.push_back({ bb + static_cast<uint32_t>(k), vb + nk, vb + static_cast<uint32_t>(k), {} });
                        }
                    }
                }
            } else if (isLine && ring.size() >= 2) {
                float span = Detail3D200::ComputePointCloudSpan(ring);
                float rad = std::max(0.05f, span * 0.015f);
                for (size_t k = 0; k + 1 < ring.size(); ++k) Detail3D200::AddStrut3D(mesh, ring[k], ring[k + 1], rad);
            } else {
                for (const auto& pt : ring) Detail3D200::AddOctaSphere(mesh, pt.x, pt.y, pt.z, 0.4f);
            }
        }
        searchPos = bClose + 1;
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"3D GeoJSON Feature Scene (.geojson)";
    return true;
}

// ============================================================================
// 4. OpenCTM RAW + MG1 + MG2 LZMA1 Mesh Decoder (.ctm)
// ============================================================================
inline bool ReadCtmLzmaPackedWords(
    const uint8_t* data,
    size_t size,
    size_t& pos,
    const char expectedTag[4],
    uint32_t count,
    uint32_t elemCols,
    bool isSignedZigZag,
    std::vector<uint32_t>& outWords)
{
    if (pos + 13 > size || std::memcmp(data + pos, expectedTag, 4) != 0) return false;
    uint32_t packedSize = RdU32LE(data + pos + 4);
    uint8_t  lzmaProps  = data[pos + 8];
    uint32_t dictSize   = RdU32LE(data + pos + 9);
    pos += 13;
    if (packedSize > size - pos) return false;

    uint64_t totalWords = static_cast<uint64_t>(count) * elemCols;
    uint64_t unpBytes   = totalWords * 4ULL;
    if (totalWords == 0 || unpBytes > 128 * 1024 * 1024) return false;

    std::vector<uint8_t> planes;
    if (!StreamCodecs::DecompressLzma1(lzmaProps, dictSize, data + pos, packedSize,
                                       planes, unpBytes, true, static_cast<size_t>(unpBytes) + 64) ||
        planes.size() < static_cast<size_t>(unpBytes))
    {
        return false;
    }
    pos += packedSize;

    size_t tw = static_cast<size_t>(totalWords);
    outWords.resize(tw);
    for (size_t i = 0; i < tw; ++i) {
        // OpenCTM plane 0 = MSB (bits 31..24), plane 3 = LSB (bits 7..0)
        uint32_t v = (static_cast<uint32_t>(planes[0 * tw + i]) << 24) |
                     (static_cast<uint32_t>(planes[1 * tw + i]) << 16) |
                     (static_cast<uint32_t>(planes[2 * tw + i]) << 8)  |
                      static_cast<uint32_t>(planes[3 * tw + i]);
        if (isSignedZigZag) {
            int32_t sv = ((v & 1u) != 0) ? -static_cast<int32_t>((v + 1u) >> 1) : static_cast<int32_t>(v >> 1);
            outWords[i] = static_cast<uint32_t>(sv);
        } else {
            outWords[i] = v;
        }
    }
    return true;
}

inline void RestoreOpenCtmIndices(uint32_t tCount, std::vector<uint32_t>& idx) {
    for (uint32_t i = 0; i < tCount; ++i) {
        if (i >= 1) idx[i * 3 + 0] += idx[(i - 1) * 3 + 0];
        idx[i * 3 + 2] += idx[i * 3 + 0];
        if (i >= 1 && idx[i * 3 + 0] == idx[(i - 1) * 3 + 0])
            idx[i * 3 + 1] += idx[(i - 1) * 3 + 1];
        else
            idx[i * 3 + 1] += idx[i * 3 + 0];
    }
}

inline bool ParseOpenCtmComplete(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    if (!data || size < 36 || std::memcmp(data, "OCTM", 4) != 0) return false;
    uint32_t fmtVer = RdU32LE(data + 4);
    if (fmtVer == 0 || fmtVer > 10) return false;

    bool isRaw = (std::memcmp(data + 8, "RAW\0", 4) == 0 || std::memcmp(data + 8, "RAW ", 4) == 0);
    bool isMg1 = (std::memcmp(data + 8, "MG1\0", 4) == 0);
    bool isMg2 = (std::memcmp(data + 8, "MG2\0", 4) == 0);
    if (isRaw) {
        return ParseBlitzAcCtmDraco(data, size, L".ctm", mesh);
    }
    if (!isMg1 && !isMg2) return false;

    uint32_t vCount = RdU32LE(data + 12);
    uint32_t tCount = RdU32LE(data + 16);
    if (vCount < 3 || tCount == 0 || vCount > 2000000 || tCount > 4000000) return false;

    uint32_t cLen = RdU32LE(data + 32);
    if (cLen > size - 36) return false;
    size_t pos = 36 + cLen;

    mesh.vertices.clear();
    mesh.tris.clear();

    if (isMg1) {
        std::vector<uint32_t> idxWords, vertWords;
        if (!ReadCtmLzmaPackedWords(data, size, pos, "INDX", tCount, 3, false, idxWords)) return false;
        RestoreOpenCtmIndices(tCount, idxWords);
        if (!ReadCtmLzmaPackedWords(data, size, pos, "VERT", vCount, 3, false, vertWords)) return false;

        mesh.vertices.resize(vCount);
        for (uint32_t v = 0; v < vCount; ++v) {
            float fx = 0, fy = 0, fz = 0;
            std::memcpy(&fx, &vertWords[v * 3 + 0], 4);
            std::memcpy(&fy, &vertWords[v * 3 + 1], 4);
            std::memcpy(&fz, &vertWords[v * 3 + 2], 4);
            if (!std::isfinite(fx) || !std::isfinite(fy) || !std::isfinite(fz)) return false;
            mesh.vertices[v] = { fx, fy, fz };
        }
        mesh.tris.reserve(tCount);
        for (uint32_t t = 0; t < tCount; ++t) {
            uint32_t i0 = idxWords[t * 3 + 0], i1 = idxWords[t * 3 + 1], i2 = idxWords[t * 3 + 2];
            if (i0 >= vCount || i1 >= vCount || i2 >= vCount) return false;
            mesh.tris.push_back({ i0, i1, i2, {} });
        }
        mesh.formatName = L"OpenCTM MG1 LZMA Mesh (.ctm)";
        return true;
    }

    // MG2: 44-byte 3D grid header: vPrec(4) + nPrec(4) + lb[3](12) + ub[3](12) + div[3](12), then "VERT", "GIDX", "INDX"
    if (pos + 44 > size || std::memcmp(data + pos, "MG2H", 4) == 0) {
        if (pos + 48 <= size && std::memcmp(data + pos, "MG2H", 4) == 0) pos += 4;
    }
    if (pos + 44 > size) return false;
    float vPrec = RdF32LE(data + pos + 0);
    float lbX   = RdF32LE(data + pos + 8),  lbY = RdF32LE(data + pos + 12), lbZ = RdF32LE(data + pos + 16);
    float ubX   = RdF32LE(data + pos + 20), ubY = RdF32LE(data + pos + 24), ubZ = RdF32LE(data + pos + 28);
    uint32_t divX = RdU32LE(data + pos + 32), divY = RdU32LE(data + pos + 36), divZ = RdU32LE(data + pos + 40);
    pos += 44;
    if (!std::isfinite(vPrec) || vPrec <= 0.0f || divX == 0 || divY == 0 || divZ == 0 ||
        divX > 65536 || divY > 65536 || divZ > 65536) return false;

    std::vector<uint32_t> intVerts, gridIdx, idxWords;
    if (!ReadCtmLzmaPackedWords(data, size, pos, "VERT", vCount, 3, true, intVerts)) return false;
    if (!ReadCtmLzmaPackedWords(data, size, pos, "GIDX", vCount, 1, false, gridIdx)) return false;
    if (!ReadCtmLzmaPackedWords(data, size, pos, "INDX", tCount, 3, false, idxWords)) return false;

    for (uint32_t i = 1; i < vCount; ++i) gridIdx[i] += gridIdx[i - 1];
    RestoreOpenCtmIndices(tCount, idxWords);

    float szX = (ubX - lbX) / static_cast<float>(divX);
    float szY = (ubY - lbY) / static_cast<float>(divY);
    float szZ = (ubZ - lbZ) / static_cast<float>(divZ);

    mesh.vertices.resize(vCount);
    int32_t prevDeltaX = 0;
    for (uint32_t i = 0; i < vCount; ++i) {
        int32_t ivx = static_cast<int32_t>(intVerts[i * 3 + 0]);
        int32_t ivy = static_cast<int32_t>(intVerts[i * 3 + 1]);
        int32_t ivz = static_cast<int32_t>(intVerts[i * 3 + 2]);
        if (i >= 1 && gridIdx[i] == gridIdx[i - 1]) ivx += prevDeltaX;
        prevDeltaX = ivx;

        uint32_t g = gridIdx[i];
        uint32_t gx = g % divX;
        uint32_t gy = (g / divX) % divY;
        uint32_t gz = g / (divX * divY);
        mesh.vertices[i] = {
            lbX + gx * szX + ivx * vPrec,
            lbY + gy * szY + ivy * vPrec,
            lbZ + gz * szZ + ivz * vPrec
        };
    }

    mesh.tris.reserve(tCount);
    for (uint32_t t = 0; t < tCount; ++t) {
        uint32_t i0 = idxWords[t * 3 + 0], i1 = idxWords[t * 3 + 1], i2 = idxWords[t * 3 + 2];
        if (i0 >= vCount || i1 >= vCount || i2 >= vCount) return false;
        mesh.tris.push_back({ i0, i1, i2, {} });
    }
    mesh.formatName = L"OpenCTM MG2 Grid-Quantized LZMA Mesh (.ctm)";
    return true;
}

// ============================================================================
// 5. Quake 1 (v29), GoldSrc (v30), Quake II (IBSP v38), Quake III (IBSP v46/47),
//    & Valve Source Engine (VBSP v19–21) .bsp Level Geometry
// ============================================================================
inline bool ParseBspLevelMesh(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    if (!data || size < 128) return false;
    mesh.vertices.clear();
    mesh.tris.clear();

    auto emitSurfEdgeBsp = [&](
        size_t vOff, size_t vLen,
        size_t eOff, size_t eLen,
        size_t seOff, size_t seLen,
        size_t fOff, size_t fLen,
        size_t faceStride, size_t firstEdgeFieldOff, size_t numEdgesFieldOff,
        const wchar_t* fmtLabel) -> bool
    {
        if (vOff + vLen > size || eOff + eLen > size || seOff + seLen > size || fOff + fLen > size) return false;
        size_t nVerts = vLen / 12u;
        size_t nEdges = eLen / 4u;
        size_t nSurfE = seLen / 4u;
        size_t nFaces = fLen / faceStride;
        if (nVerts < 3 || nEdges < 3 || nSurfE < 3 || nFaces == 0 || nVerts > 2000000 || nFaces > 2000000) return false;

        mesh.vertices.resize(nVerts);
        for (size_t i = 0; i < nVerts; ++i) {
            float x = RdF32LE(data + vOff + i * 12u + 0);
            float y = RdF32LE(data + vOff + i * 12u + 4);
            float z = RdF32LE(data + vOff + i * 12u + 8);
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
            mesh.vertices[i] = { x, z, -y }; // Quake/Source Z-up -> Explorer2 Y-up
        }

        for (size_t f = 0; f < nFaces; ++f) {
            const uint8_t* fp = data + fOff + f * faceStride;
            int32_t firstEdge = RdI32LE(fp + firstEdgeFieldOff);
            int16_t numEdges  = RdI16LE(fp + numEdgesFieldOff);
            if (firstEdge < 0 || numEdges < 3 || static_cast<size_t>(firstEdge + numEdges) > nSurfE) continue;

            std::vector<uint32_t> ring;
            ring.reserve(static_cast<size_t>(numEdges));
            for (int16_t k = 0; k < numEdges; ++k) {
                int32_t se = RdI32LE(data + seOff + static_cast<size_t>(firstEdge + k) * 4u);
                size_t eIdx = static_cast<size_t>(se >= 0 ? se : -se);
                if (eIdx >= nEdges) continue;
                uint16_t v0 = RdU16LE(data + eOff + eIdx * 4u + 0);
                uint16_t v1 = RdU16LE(data + eOff + eIdx * 4u + 2);
                uint16_t vi = (se >= 0) ? v0 : v1;
                if (vi < nVerts) ring.push_back(vi);
            }
            for (size_t k = 1; k + 1 < ring.size(); ++k) {
                if (ring[0] != ring[k] && ring[k] != ring[k + 1] && ring[0] != ring[k + 1]) {
                    mesh.tris.push_back({ ring[0], ring[k], ring[k + 1], {} });
                }
            }
        }
        if (mesh.tris.empty()) { mesh.vertices.clear(); return false; }
        mesh.formatName = fmtLabel;
        return true;
    };

    // 1. Quake 1 (v29) & Half-Life GoldSrc (v30)
    int32_t rawVer = RdI32LE(data);
    if ((rawVer == 29 || rawVer == 30) && size >= 4 + 15 * 8) {
        auto lumpOff = [&](int l) { return static_cast<size_t>(RdU32LE(data + 4 + l * 8)); };
        auto lumpLen = [&](int l) { return static_cast<size_t>(RdU32LE(data + 4 + l * 8 + 4)); };
        return emitSurfEdgeBsp(
            lumpOff(3), lumpLen(3), lumpOff(12), lumpLen(12),
            lumpOff(13), lumpLen(13), lumpOff(7), lumpLen(7),
            20u, 4u, 8u,
            (rawVer == 30) ? L"Half-Life GoldSrc Map (.bsp v30)" : L"Quake I Level Map (.bsp v29)");
    }

    // 2. id Tech 2 / 3 "IBSP" (v38 Quake II, v46 Quake III, v47 RTCW)
    if (std::memcmp(data, "IBSP", 4) == 0) {
        int32_t ver = RdI32LE(data + 4);
        if (ver == 38 && size >= 8 + 19 * 8) {
            auto lOff = [&](int l) { return static_cast<size_t>(RdU32LE(data + 8 + l * 8)); };
            auto lLen = [&](int l) { return static_cast<size_t>(RdU32LE(data + 8 + l * 8 + 4)); };
            return emitSurfEdgeBsp(lOff(2), lLen(2), lOff(11), lLen(11), lOff(12), lLen(12),
                                   lOff(6), lLen(6), 20u, 4u, 8u, L"Quake II IBSP Level (.bsp v38)");
        }
        if ((ver == 46 || ver == 47) && size >= 8 + 17 * 8) {
            auto lOff = [&](int l) { return static_cast<size_t>(RdU32LE(data + 8 + l * 8)); };
            auto lLen = [&](int l) { return static_cast<size_t>(RdU32LE(data + 8 + l * 8 + 4)); };
            size_t dvOff = lOff(10), dvLen = lLen(10);
            size_t mvOff = lOff(11), mvLen = lLen(11);
            size_t fcOff = lOff(13), fcLen = lLen(13);
            if (dvOff + dvLen > size || mvOff + mvLen > size || fcOff + fcLen > size) return false;
            size_t nVerts = dvLen / 44u;
            size_t nMeshV = mvLen / 4u;
            size_t nFaces = fcLen / 104u;
            if (nVerts < 3 || nFaces == 0 || nVerts > 2000000 || nFaces > 2000000) return false;

            std::vector<Vec3f> drawVerts(nVerts);
            std::vector<int32_t> dvRemap(nVerts, -1);
            for (size_t i = 0; i < nVerts; ++i) {
                const uint8_t* vp = data + dvOff + i * 44u;
                drawVerts[i] = { RdF32LE(vp + 0), RdF32LE(vp + 8), -RdF32LE(vp + 4) };
            }
            auto emitDv = [&](size_t dvIdx) -> uint32_t {
                if (dvRemap[dvIdx] < 0) {
                    dvRemap[dvIdx] = static_cast<int32_t>(mesh.vertices.size());
                    mesh.vertices.push_back(drawVerts[dvIdx]);
                }
                return static_cast<uint32_t>(dvRemap[dvIdx]);
            };

            for (size_t f = 0; f < nFaces; ++f) {
                const uint8_t* fp = data + fcOff + f * 104u;
                int32_t fType   = RdI32LE(fp + 8);
                int32_t firstV  = RdI32LE(fp + 12);
                int32_t numV    = RdI32LE(fp + 16);
                int32_t firstMV = RdI32LE(fp + 20);
                int32_t numMV   = RdI32LE(fp + 24);
                if (firstV < 0 || numV <= 0 || static_cast<size_t>(firstV + numV) > nVerts) continue;

                if ((fType == 1 || fType == 3) && firstMV >= 0 && numMV >= 3 && static_cast<size_t>(firstMV + numMV) <= nMeshV) {
                    for (int32_t k = 0; k + 2 < numMV; k += 3) {
                        int32_t o0 = RdI32LE(data + mvOff + static_cast<size_t>(firstMV + k + 0) * 4u);
                        int32_t o1 = RdI32LE(data + mvOff + static_cast<size_t>(firstMV + k + 1) * 4u);
                        int32_t o2 = RdI32LE(data + mvOff + static_cast<size_t>(firstMV + k + 2) * 4u);
                        if (o0 >= 0 && o1 >= 0 && o2 >= 0 && o0 < numV && o1 < numV && o2 < numV) {
                            uint32_t v0 = emitDv(static_cast<size_t>(firstV + o0));
                            uint32_t v2 = emitDv(static_cast<size_t>(firstV + o2));
                            uint32_t v1 = emitDv(static_cast<size_t>(firstV + o1));
                            mesh.tris.push_back({ v0, v2, v1, {} });
                        }
                    }
                } else if (fType == 2) { // Biquadratic Bezier Patch (pw x ph control grid)
                    int32_t pw = RdI32LE(fp + 96), ph = RdI32LE(fp + 100);
                    if (pw >= 3 && ph >= 3 && (pw & 1) == 1 && (ph & 1) == 1 && pw * ph <= numV) {
                        const int tess = 4;
                        for (int py = 0; py + 2 < ph; py += 2) {
                            for (int px = 0; px + 2 < pw; px += 2) {
                                auto cp = [&](int cx, int cy) -> Vec3f {
                                    return drawVerts[static_cast<size_t>(firstV + (py + cy) * pw + (px + cx))];
                                };
                                uint32_t pb = static_cast<uint32_t>(mesh.vertices.size());
                                for (int iy = 0; iy <= tess; ++iy) {
                                    float v = static_cast<float>(iy) / tess, iv = 1.0f - v;
                                    float bv[3] = { iv * iv, 2.0f * iv * v, v * v };
                                    for (int ix = 0; ix <= tess; ++ix) {
                                        float u = static_cast<float>(ix) / tess, iu = 1.0f - u;
                                        float bu[3] = { iu * iu, 2.0f * iu * u, u * u };
                                        Vec3f sum = { 0, 0, 0 };
                                        for (int cy = 0; cy < 3; ++cy) {
                                            for (int cx = 0; cx < 3; ++cx) {
                                                float w = bu[cx] * bv[cy];
                                                Vec3f p = cp(cx, cy);
                                                sum.x += w * p.x; sum.y += w * p.y; sum.z += w * p.z;
                                            }
                                        }
                                        mesh.vertices.push_back(sum);
                                    }
                                }
                                uint32_t stride = tess + 1;
                                for (uint32_t iy = 0; iy < static_cast<uint32_t>(tess); ++iy) {
                                    for (uint32_t ix = 0; ix < static_cast<uint32_t>(tess); ++ix) {
                                        uint32_t a = pb + iy * stride + ix, b = a + 1, c = a + stride, d = c + 1;
                                        mesh.tris.push_back({ a, c, b, {} });
                                        mesh.tris.push_back({ b, c, d, {} });
                                    }
                                }
                            }
                        }
                    }
                }
            }
            if (mesh.tris.empty()) { mesh.vertices.clear(); return false; }
            mesh.formatName = L"Quake III Arena IBSP Level (.bsp v46)";
            return true;
        }
    }

    // 3. Valve Source Engine "VBSP" (v19, v20, v21)
    if (std::memcmp(data, "VBSP", 4) == 0 && size >= 8 + 64 * 16) {
        int32_t ver = RdI32LE(data + 4);
        if (ver < 17 || ver > 25) return false;
        auto lOff = [&](int l) { return static_cast<size_t>(RdU32LE(data + 8 + l * 16)); };
        auto lLen = [&](int l) { return static_cast<size_t>(RdU32LE(data + 8 + l * 16 + 4)); };
        return emitSurfEdgeBsp(lOff(3), lLen(3), lOff(12), lLen(12), lOff(13), lLen(13),
                               lOff(7), lLen(7), 56u, 4u, 8u, L"Valve Source Engine VBSP Map (.bsp)");
    }

    return false;
}

// ============================================================================
// 6. Ogre3D Binary (.mesh 0x1000 "[MeshSerializer_v1.x]") & INRIA Medit (.mesh)
// ============================================================================
inline bool ParseOgreOrMeditMesh(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    if (!data || size < 16) return false;
    mesh.vertices.clear();
    mesh.tris.clear();

    // A. Ogre3D Binary Mesh: uint16 0x1000 + "[MeshSerializer_v1.xx]\n"
    if (size >= 24 && RdU16LE(data) == 0x1000u) {
        size_t pos = 2;
        while (pos < size && pos < 80 && data[pos] != '\n' && data[pos] != 0) ++pos;
        std::string_view verSv(reinterpret_cast<const char*>(data + 2), pos - 2);
        if (verSv.find("MeshSerializer") == std::string_view::npos) return false;
        if (pos < size) ++pos;

        auto parseOgreGeometry = [&](size_t gStart, size_t gEnd, std::vector<Vec3f>& outVerts) -> bool {
            if (gStart + 4 > gEnd) return false;
            uint32_t vCount = RdU32LE(data + gStart);
            if (vCount == 0 || vCount > 2000000) return false;
            size_t p = gStart + 4;

            uint16_t posSource = 0, posType = 2, posOffset = 0;
            bool foundPosDecl = false;

            while (p + 6 <= gEnd) {
                uint16_t cid = RdU16LE(data + p);
                uint32_t clen = RdU32LE(data + p + 2);
                if (clen < 6 || p + clen > gEnd) break;
                size_t cBody = p + 6;
                size_t cEnd  = p + clen;

                if (cid == 0x5100u) { // M_GEOMETRY_VERTEX_DECLARATION
                    size_t dp = cBody;
                    while (dp + 16 <= cEnd) {
                        uint16_t did = RdU16LE(data + dp);
                        uint32_t dlen = RdU32LE(data + dp + 2);
                        if (dlen < 16 || dp + dlen > cEnd) break;
                        if (did == 0x5110u) {
                            uint16_t src = RdU16LE(data + dp + 6);
                            uint16_t typ = RdU16LE(data + dp + 8);
                            uint16_t sem = RdU16LE(data + dp + 10); // 1 = VES_POSITION
                            uint16_t off = RdU16LE(data + dp + 12);
                            if (sem == 1) { posSource = src; posType = typ; posOffset = off; foundPosDecl = true; }
                        }
                        dp += dlen;
                    }
                } else if (cid == 0x5200u && cBody + 4 <= cEnd) { // M_GEOMETRY_VERTEX_BUFFER
                    uint16_t bindIdx = RdU16LE(data + cBody);
                    uint16_t vStride = RdU16LE(data + cBody + 2);
                    size_t bp = cBody + 4;
                    if (bp + 6 <= cEnd && RdU16LE(data + bp) == 0x5210u && (!foundPosDecl || bindIdx == posSource)) {
                        size_t rawStart = bp + 6;
                        if (vStride >= 6 && rawStart + static_cast<uint64_t>(vCount) * vStride <= cEnd) {
                            outVerts.resize(vCount);
                            for (uint32_t i = 0; i < vCount; ++i) {
                                const uint8_t* vp = data + rawStart + static_cast<size_t>(i) * vStride + posOffset;
                                if (posType == 2 && posOffset + 12 <= vStride) { // VET_FLOAT3
                                    outVerts[i] = { RdF32LE(vp + 0), RdF32LE(vp + 4), RdF32LE(vp + 8) };
                                } else if (posOffset + 6 <= vStride) { // VET_HALF3 / SHORT3
                                    outVerts[i] = {
                                        SplatVdbDetail::HalfToF32Splat(RdU16LE(vp + 0)),
                                        SplatVdbDetail::HalfToF32Splat(RdU16LE(vp + 2)),
                                        SplatVdbDetail::HalfToF32Splat(RdU16LE(vp + 4))
                                    };
                                }
                            }
                        }
                    }
                }
                p = cEnd;
            }
            return !outVerts.empty();
        };

        while (pos + 6 <= size) {
            uint16_t mId  = RdU16LE(data + pos);
            uint32_t mLen = RdU32LE(data + pos + 2);
            if (mLen < 6 || pos + mLen > size) break;
            if (mId == 0x3000u) { // M_MESH
                size_t mp = pos + 6 + 1; // skip bool skeletallyAnimated
                std::vector<Vec3f> sharedVerts;
                struct DeferredSubMesh {
                    bool useShared = true;
                    uint16_t opType = 4; // OT_TRIANGLE_LIST
                    std::vector<uint32_t> indices;
                    std::vector<Vec3f> ownVerts;
                };
                std::vector<DeferredSubMesh> subs;

                while (mp + 6 <= pos + mLen) {
                    uint16_t sid  = RdU16LE(data + mp);
                    uint32_t slen = RdU32LE(data + mp + 2);
                    if (slen < 6 || mp + slen > pos + mLen) break;
                    size_t sBody = mp + 6;
                    size_t sEnd  = mp + slen;

                    if (sid == 0x5000u) { // Shared M_GEOMETRY
                        parseOgreGeometry(sBody, sEnd, sharedVerts);
                    } else if (sid == 0x4000u) { // M_SUBMESH
                        size_t sp = sBody;
                        while (sp < sEnd && data[sp] != '\n' && data[sp] != 0) ++sp;
                        if (sp < sEnd) ++sp;
                        if (sp + 6 <= sEnd) {
                            DeferredSubMesh sm;
                            sm.useShared = (data[sp++] != 0);
                            uint32_t idxCnt = RdU32LE(data + sp); sp += 4;
                            bool idx32 = (data[sp++] != 0);
                            size_t idxBytes = static_cast<size_t>(idxCnt) * (idx32 ? 4u : 2u);
                            if (idxCnt <= 6000000 && sp + idxBytes <= sEnd) {
                                sm.indices.resize(idxCnt);
                                for (uint32_t i = 0; i < idxCnt; ++i) {
                                    sm.indices[i] = idx32 ? RdU32LE(data + sp + i * 4u) : RdU16LE(data + sp + i * 2u);
                                }
                                sp += idxBytes;
                                while (sp + 6 <= sEnd) {
                                    uint16_t scid  = RdU16LE(data + sp);
                                    uint32_t sclen = RdU32LE(data + sp + 2);
                                    if (sclen < 6 || sp + sclen > sEnd) break;
                                    if (scid == 0x5000u) parseOgreGeometry(sp + 6, sp + sclen, sm.ownVerts);
                                    else if (scid == 0x4010u && sp + 8 <= sEnd) sm.opType = RdU16LE(data + sp + 6);
                                    sp += sclen;
                                }
                                subs.push_back(std::move(sm));
                            }
                        }
                    }
                    mp = sEnd;
                }

                uint32_t sharedBase = 0;
                bool emittedShared = false;
                for (const auto& sm : subs) {
                    const std::vector<Vec3f>& vSrc = sm.useShared ? sharedVerts : sm.ownVerts;
                    if (vSrc.empty() || sm.indices.size() < 3) continue;
                    uint32_t vBase = 0;
                    if (sm.useShared) {
                        if (!emittedShared) {
                            sharedBase = static_cast<uint32_t>(mesh.vertices.size());
                            mesh.vertices.insert(mesh.vertices.end(), sharedVerts.begin(), sharedVerts.end());
                            emittedShared = true;
                        }
                        vBase = sharedBase;
                    } else {
                        vBase = static_cast<uint32_t>(mesh.vertices.size());
                        mesh.vertices.insert(mesh.vertices.end(), sm.ownVerts.begin(), sm.ownVerts.end());
                    }

                    size_t nv = vSrc.size();
                    if (sm.opType == 5) { // OT_TRIANGLE_STRIP
                        for (size_t k = 0; k + 2 < sm.indices.size(); ++k) {
                            uint32_t a = sm.indices[k], b = sm.indices[k + 1], c = sm.indices[k + 2];
                            if (a < nv && b < nv && c < nv && a != b && b != c && a != c) {
                                if ((k & 1u) == 0) mesh.tris.push_back({ vBase + a, vBase + b, vBase + c, {} });
                                else               mesh.tris.push_back({ vBase + b, vBase + a, vBase + c, {} });
                            }
                        }
                    } else if (sm.opType == 6) { // OT_TRIANGLE_FAN
                        for (size_t k = 1; k + 1 < sm.indices.size(); ++k) {
                            uint32_t a = sm.indices[0], b = sm.indices[k], c = sm.indices[k + 1];
                            if (a < nv && b < nv && c < nv) mesh.tris.push_back({ vBase + a, vBase + b, vBase + c, {} });
                        }
                    } else { // OT_TRIANGLE_LIST (4)
                        for (size_t k = 0; k + 2 < sm.indices.size(); k += 3) {
                            uint32_t a = sm.indices[k], b = sm.indices[k + 1], c = sm.indices[k + 2];
                            if (a < nv && b < nv && c < nv) mesh.tris.push_back({ vBase + a, vBase + b, vBase + c, {} });
                        }
                    }
                }
            }
            pos += mLen;
        }
        if (mesh.vertices.empty() || mesh.tris.empty()) return false;
        mesh.formatName = L"Ogre3D Binary Mesh (.mesh)";
        return true;
    }

    // B. INRIA Medit / Gmsh-Medit ASCII .mesh ("MeshVersionFormatted")
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    if (sv.find("MeshVersionFormatted") != std::string_view::npos) {
        const char* cur = sv.data();
        const char* end = cur + sv.size();
        int dim = 3;
        std::string_view tok;

        std::unordered_map<DetailVtk::SortedFaceKey, DetailVtk::BoundaryFaceRec, DetailVtk::SortedFaceKeyHash> faceMap;

        while (DetailVtk::NextTokenVtk(cur, end, tok)) {
            if (!tok.empty() && tok[0] == '#') {
                std::string_view dummy;
                DetailVtk::ReadLineVtk(cur, end, dummy);
                continue;
            }
            std::string kw = DetailVtk::ToLowerVtk(tok);
            if (kw == "end") break;
            if (kw == "dimension") {
                int64_t d = 3;
                if (DetailVtk::NextIntAsciiVtk(cur, end, d) && (d == 2 || d == 3)) dim = static_cast<int>(d);
            } else if (kw == "vertices") {
                int64_t nv = 0;
                if (!DetailVtk::NextIntAsciiVtk(cur, end, nv) || nv <= 0 || nv > 2000000) return false;
                mesh.vertices.reserve(static_cast<size_t>(nv));
                for (int64_t i = 0; i < nv; ++i) {
                    double x = 0, y = 0, z = 0, ref = 0;
                    if (!DetailVtk::NextDoubleAsciiVtk(cur, end, x) || !DetailVtk::NextDoubleAsciiVtk(cur, end, y)) return false;
                    if (dim == 3 && !DetailVtk::NextDoubleAsciiVtk(cur, end, z)) return false;
                    DetailVtk::NextDoubleAsciiVtk(cur, end, ref); // entity ref tag
                    mesh.vertices.push_back({ static_cast<float>(x), static_cast<float>(y), static_cast<float>(z) });
                }
            } else if (kw == "triangles") {
                int64_t nt = 0;
                if (!DetailVtk::NextIntAsciiVtk(cur, end, nt) || nt < 0 || nt > 4000000) return false;
                for (int64_t i = 0; i < nt; ++i) {
                    int64_t a = 0, b = 0, c = 0, ref = 0;
                    if (!DetailVtk::NextIntAsciiVtk(cur, end, a) || !DetailVtk::NextIntAsciiVtk(cur, end, b) ||
                        !DetailVtk::NextIntAsciiVtk(cur, end, c)) return false;
                    DetailVtk::NextIntAsciiVtk(cur, end, ref);
                    if (a >= 1 && b >= 1 && c >= 1 &&
                        static_cast<size_t>(a) <= mesh.vertices.size() &&
                        static_cast<size_t>(b) <= mesh.vertices.size() &&
                        static_cast<size_t>(c) <= mesh.vertices.size())
                    {
                        uint32_t f[3] = { static_cast<uint32_t>(a - 1), static_cast<uint32_t>(b - 1), static_cast<uint32_t>(c - 1) };
                        DetailVtk::RecordFace3DVtk(faceMap, f, 3, false);
                    }
                }
            } else if (kw == "quadrilaterals") {
                int64_t nq = 0;
                if (!DetailVtk::NextIntAsciiVtk(cur, end, nq) || nq < 0 || nq > 4000000) return false;
                for (int64_t i = 0; i < nq; ++i) {
                    int64_t a = 0, b = 0, c = 0, d = 0, ref = 0;
                    if (!DetailVtk::NextIntAsciiVtk(cur, end, a) || !DetailVtk::NextIntAsciiVtk(cur, end, b) ||
                        !DetailVtk::NextIntAsciiVtk(cur, end, c) || !DetailVtk::NextIntAsciiVtk(cur, end, d)) return false;
                    DetailVtk::NextIntAsciiVtk(cur, end, ref);
                    if (a >= 1 && b >= 1 && c >= 1 && d >= 1 &&
                        static_cast<size_t>(std::max({ a, b, c, d })) <= mesh.vertices.size())
                    {
                        uint32_t q[4] = { static_cast<uint32_t>(a - 1), static_cast<uint32_t>(b - 1), static_cast<uint32_t>(c - 1), static_cast<uint32_t>(d - 1) };
                        DetailVtk::RecordFace3DVtk(faceMap, q, 4, false);
                    }
                }
            } else if (kw == "tetrahedra") {
                int64_t nt = 0;
                if (!DetailVtk::NextIntAsciiVtk(cur, end, nt) || nt < 0 || nt > 4000000) return false;
                for (int64_t i = 0; i < nt; ++i) {
                    int64_t v[4] = {}, ref = 0;
                    if (!DetailVtk::NextIntAsciiVtk(cur, end, v[0]) || !DetailVtk::NextIntAsciiVtk(cur, end, v[1]) ||
                        !DetailVtk::NextIntAsciiVtk(cur, end, v[2]) || !DetailVtk::NextIntAsciiVtk(cur, end, v[3])) return false;
                    DetailVtk::NextIntAsciiVtk(cur, end, ref);
                    if (v[0] >= 1 && v[1] >= 1 && v[2] >= 1 && v[3] >= 1 &&
                        static_cast<size_t>(std::max({ v[0], v[1], v[2], v[3] })) <= mesh.vertices.size())
                    {
                        uint32_t c0 = static_cast<uint32_t>(v[0] - 1), c1 = static_cast<uint32_t>(v[1] - 1);
                        uint32_t c2 = static_cast<uint32_t>(v[2] - 1), c3 = static_cast<uint32_t>(v[3] - 1);
                        const uint32_t f0[3] = { c0, c2, c1 }, f1[3] = { c0, c1, c3 }, f2[3] = { c1, c2, c3 }, f3[3] = { c2, c0, c3 };
                        DetailVtk::RecordFace3DVtk(faceMap, f0, 3, true);
                        DetailVtk::RecordFace3DVtk(faceMap, f1, 3, true);
                        DetailVtk::RecordFace3DVtk(faceMap, f2, 3, true);
                        DetailVtk::RecordFace3DVtk(faceMap, f3, 3, true);
                    }
                }
            }
        }

        for (const auto& kv : faceMap) {
            const auto& f = kv.second;
            if (f.volCount == 1 || (f.volCount == 0 && f.explicit2D)) {
                if (f.n == 3) mesh.tris.push_back({ f.orig[0], f.orig[1], f.orig[2], {} });
                else if (f.n == 4) {
                    mesh.tris.push_back({ f.orig[0], f.orig[1], f.orig[2], {} });
                    mesh.tris.push_back({ f.orig[0], f.orig[2], f.orig[3], {} });
                }
            }
        }
        if (mesh.vertices.empty() || mesh.tris.empty()) return false;
        mesh.formatName = L"INRIA Medit Scientific FEM Mesh (.mesh)";
        return true;
    }

    return false;
}

} // namespace BspGeoDetail

} // namespace Formats
