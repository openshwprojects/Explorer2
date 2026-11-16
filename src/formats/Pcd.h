#pragma once
// Pcd.h — Point Cloud Library 3D Format (.pcd v0.6 / v0.7) Parser
// Supports DATA ascii, DATA binary, and DATA binary_compressed (LibLZF + PCL SoA layout),
// arbitrary FIELDS / SIZE (1,2,4,8) / TYPE (F,I,U) / COUNT strides, RGB/RGBA & intensity.

namespace Formats {

namespace DetailPcd {

inline std::string_view TrimPcdWs(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

inline void SplitPcdTokens(std::string_view line, std::vector<std::string_view>& out) {
    out.clear();
    size_t i = 0;
    const size_t n = line.size();
    while (i < n) {
        while (i < n && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) ++i;
        if (i >= n || line[i] == '#') break;
        size_t start = i;
        while (i < n && line[i] != ' ' && line[i] != '\t' && line[i] != '\r' && line[i] != '#') ++i;
        out.push_back(line.substr(start, i - start));
    }
}

inline bool EqualsIgnoreCasePcd(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        unsigned char ca = static_cast<unsigned char>(a[i]);
        unsigned char cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb)) return false;
    }
    return true;
}

inline double ReadPcdScalarLE(const uint8_t* p, char type, uint32_t sz) {
    switch (type) {
    case 'F':
        if (sz == 4) { float v = 0.0f; std::memcpy(&v, p, 4); return static_cast<double>(v); }
        if (sz == 8) { double v = 0.0; std::memcpy(&v, p, 8); return v; }
        break;
    case 'I':
        if (sz == 1) { int8_t v = 0; std::memcpy(&v, p, 1); return static_cast<double>(v); }
        if (sz == 2) { int16_t v = 0; std::memcpy(&v, p, 2); return static_cast<double>(v); }
        if (sz == 4) { int32_t v = 0; std::memcpy(&v, p, 4); return static_cast<double>(v); }
        if (sz == 8) { int64_t v = 0; std::memcpy(&v, p, 8); return static_cast<double>(v); }
        break;
    case 'U':
        if (sz == 1) { return static_cast<double>(p[0]); }
        if (sz == 2) { uint16_t v = 0; std::memcpy(&v, p, 2); return static_cast<double>(v); }
        if (sz == 4) { uint32_t v = 0; std::memcpy(&v, p, 4); return static_cast<double>(v); }
        if (sz == 8) { uint64_t v = 0; std::memcpy(&v, p, 8); return static_cast<double>(v); }
        break;
    default:
        break;
    }
    return std::numeric_limits<double>::quiet_NaN();
}

inline uint32_t ReadPcdPackedRgbLE(const uint8_t* p, char type, uint32_t sz) {
    if (sz == 4 && (type == 'F' || type == 'U' || type == 'I')) {
        uint32_t u = 0;
        std::memcpy(&u, p, 4);
        return u & 0x00FFFFFFu;
    }
    return 0x00DCE6F2u;
}

// Official PCL LibLZF decompressor (lzf_decompress).
inline bool DecompressPcdLzf(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& dst, size_t uncompressedSize) {
    if (!src || srcLen == 0 || uncompressedSize == 0) return false;
    dst.assign(uncompressedSize, 0);
    const uint8_t* ip = src;
    const uint8_t* const iend = src + srcLen;
    size_t op = 0;

    while (ip < iend) {
        uint32_t ctrl = *ip++;
        if (ctrl < 32u) {
            uint32_t len = ctrl + 1u;
            if (static_cast<size_t>(iend - ip) < len || op + len > uncompressedSize) {
                return false;
            }
            std::memcpy(dst.data() + op, ip, len);
            ip += len;
            op += len;
        } else {
            uint32_t len = ctrl >> 5;
            if (len == 7u) {
                if (ip >= iend) return false;
                len += *ip++;
            }
            len += 2u;
            if (ip >= iend) return false;
            size_t refOff = (static_cast<size_t>(ctrl & 0x1Fu) << 8) + static_cast<size_t>(*ip++) + 1u;
            if (refOff > op || op + len > uncompressedSize) {
                return false;
            }
            size_t refPos = op - refOff;
            uint8_t* outPtr = dst.data();
            for (uint32_t k = 0; k < len; ++k) {
                outPtr[op + k] = outPtr[refPos + k];
            }
            op += len;
        }
    }
    return (ip == iend) && (op == uncompressedSize);
}

struct PcdSamplePoint {
    Vec3f pos;
    uint32_t rgb = 0x00DCE6F2u;
    float intensity = 0.0f;
};

} // namespace DetailPcd

inline bool ParsePcdPointCloud(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    if (!data || size < 16) return false;

    std::string_view sv(reinterpret_cast<const char*>(data), size);
    std::vector<std::string> fieldNames;
    std::vector<uint32_t> fieldSizes;
    std::vector<char> fieldTypes;
    std::vector<uint32_t> fieldCounts;
    uint64_t width = 0;
    uint64_t height = 1;
    uint64_t pointsDecl = 0;
    bool hasWidth = false;
    bool hasPoints = false;
    bool foundDataLine = false;
    std::string dataMode;
    size_t payloadOffset = 0;

    std::vector<std::string_view> toks;
    size_t pos = 0;
    while (pos < size) {
        size_t eol = sv.find('\n', pos);
        size_t lineEnd = (eol == std::string_view::npos) ? size : eol;
        std::string_view line = DetailPcd::TrimPcdWs(sv.substr(pos, lineEnd - pos));
        pos = (eol == std::string_view::npos) ? size : (eol + 1);

        if (line.empty() || line.front() == '#') continue;
        DetailPcd::SplitPcdTokens(line, toks);
        if (toks.empty()) continue;

        std::string_view key = toks[0];
        if (DetailPcd::EqualsIgnoreCasePcd(key, "FIELDS") || DetailPcd::EqualsIgnoreCasePcd(key, "COLUMNS")) {
            fieldNames.clear();
            for (size_t i = 1; i < toks.size(); ++i) {
                std::string fn(toks[i]);
                for (char& c : fn) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                fieldNames.push_back(std::move(fn));
            }
        } else if (DetailPcd::EqualsIgnoreCasePcd(key, "SIZE")) {
            fieldSizes.clear();
            for (size_t i = 1; i < toks.size(); ++i) {
                uint32_t v = 0;
                auto [ptr, ec] = std::from_chars(toks[i].data(), toks[i].data() + toks[i].size(), v);
                if (ec != std::errc{} || ptr != toks[i].data() + toks[i].size() ||
                    (v != 1u && v != 2u && v != 4u && v != 8u)) {
                    return false;
                }
                fieldSizes.push_back(v);
            }
        } else if (DetailPcd::EqualsIgnoreCasePcd(key, "TYPE")) {
            fieldTypes.clear();
            for (size_t i = 1; i < toks.size(); ++i) {
                if (toks[i].size() != 1) return false;
                char tc = static_cast<char>(std::toupper(static_cast<unsigned char>(toks[i][0])));
                if (tc != 'F' && tc != 'I' && tc != 'U') return false;
                fieldTypes.push_back(tc);
            }
        } else if (DetailPcd::EqualsIgnoreCasePcd(key, "COUNT")) {
            fieldCounts.clear();
            for (size_t i = 1; i < toks.size(); ++i) {
                uint32_t v = 0;
                auto [ptr, ec] = std::from_chars(toks[i].data(), toks[i].data() + toks[i].size(), v);
                if (ec != std::errc{} || ptr != toks[i].data() + toks[i].size() || v == 0u || v > 65536u) {
                    return false;
                }
                fieldCounts.push_back(v);
            }
        } else if (DetailPcd::EqualsIgnoreCasePcd(key, "WIDTH") && toks.size() >= 2) {
            auto [ptr, ec] = std::from_chars(toks[1].data(), toks[1].data() + toks[1].size(), width);
            if (ec != std::errc{}) return false;
            hasWidth = true;
        } else if (DetailPcd::EqualsIgnoreCasePcd(key, "HEIGHT") && toks.size() >= 2) {
            auto [ptr, ec] = std::from_chars(toks[1].data(), toks[1].data() + toks[1].size(), height);
            if (ec != std::errc{}) return false;
        } else if (DetailPcd::EqualsIgnoreCasePcd(key, "POINTS") && toks.size() >= 2) {
            auto [ptr, ec] = std::from_chars(toks[1].data(), toks[1].data() + toks[1].size(), pointsDecl);
            if (ec != std::errc{}) return false;
            hasPoints = true;
        } else if (DetailPcd::EqualsIgnoreCasePcd(key, "DATA") && toks.size() >= 2) {
            if (eol == std::string_view::npos) return false;
            dataMode.assign(toks[1]);
            for (char& c : dataMode) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            foundDataLine = true;
            payloadOffset = pos;
            break;
        }
    }

    if (!foundDataLine || fieldNames.empty()) return false;
    const size_t numFields = fieldNames.size();
    if (fieldSizes.empty()) fieldSizes.assign(numFields, 4u);
    if (fieldTypes.empty()) fieldTypes.assign(numFields, 'F');
    if (fieldCounts.empty()) fieldCounts.assign(numFields, 1u);
    if (fieldSizes.size() != numFields || fieldTypes.size() != numFields || fieldCounts.size() != numFields) {
        return false;
    }

    uint64_t numPts64 = hasPoints ? pointsDecl : (hasWidth ? (width * std::max<uint64_t>(1, height)) : 0);
    if (numPts64 == 0 && hasWidth && width > 0) {
        numPts64 = width * std::max<uint64_t>(1, height);
    }
    if (numPts64 == 0 || numPts64 > 100000000ULL) return false;
    const size_t numPts = static_cast<size_t>(numPts64);

    int idxX = -1, idxY = -1, idxZ = -1, idxRgb = -1, idxIntensity = -1;
    std::vector<size_t> fieldByteSizes(numFields, 0);
    std::vector<size_t> aosOffsets(numFields, 0);
    std::vector<size_t> scalarOffsets(numFields, 0);
    size_t pointStep = 0;
    size_t totalScalars = 0;

    for (size_t f = 0; f < numFields; ++f) {
        size_t fb = static_cast<size_t>(fieldSizes[f]) * static_cast<size_t>(fieldCounts[f]);
        if (fb == 0 || pointStep + fb > 65536u) return false;
        fieldByteSizes[f] = fb;
        aosOffsets[f] = pointStep;
        scalarOffsets[f] = totalScalars;
        pointStep += fb;
        totalScalars += fieldCounts[f];

        if (fieldNames[f] == "x" && idxX < 0) idxX = static_cast<int>(f);
        else if (fieldNames[f] == "y" && idxY < 0) idxY = static_cast<int>(f);
        else if (fieldNames[f] == "z" && idxZ < 0) idxZ = static_cast<int>(f);
        else if ((fieldNames[f] == "rgb" || fieldNames[f] == "rgba") && idxRgb < 0) idxRgb = static_cast<int>(f);
        else if (fieldNames[f] == "intensity" && idxIntensity < 0) idxIntensity = static_cast<int>(f);
    }
    if (idxX < 0 || idxY < 0 || idxZ < 0 || pointStep == 0) return false;

    mesh.formatName = L"Point Cloud Library (.pcd)";

    constexpr size_t kMaxRenderPts = 25000;
    const size_t ptStride = std::max<size_t>(1, numPts / kMaxRenderPts);
    std::vector<DetailPcd::PcdSamplePoint> pts;
    pts.reserve(std::min<size_t>(numPts, kMaxRenderPts));
    bool sawColorOrIntensity = false;

    const uint8_t* bp = data + payloadOffset;
    const uint8_t* const bend = data + size;

    // Distinguish DATA binary_compressed BEFORE DATA binary!
    if (dataMode.find("binary_compressed") != std::string::npos) {
        if (static_cast<size_t>(bend - bp) < 8) return false;
        uint32_t compressedSize = 0, uncompressedSize = 0;
        std::memcpy(&compressedSize, bp, 4);
        std::memcpy(&uncompressedSize, bp + 4, 4);
        bp += 8;

        if (compressedSize == 0 || uncompressedSize == 0 ||
            static_cast<size_t>(bend - bp) < static_cast<size_t>(compressedSize) ||
            uncompressedSize > 256u * 1024u * 1024u) {
            return false;
        }
        const uint64_t expectedBytes = static_cast<uint64_t>(numPts) * static_cast<uint64_t>(pointStep);
        if (expectedBytes != static_cast<uint64_t>(uncompressedSize)) {
            return false;
        }

        std::vector<uint8_t> decomp;
        if (!DetailPcd::DecompressPcdLzf(bp, compressedSize, decomp, uncompressedSize)) {
            return false;
        }

        // Official PCL PCDWriter::writeBinaryCompressed layout: Structure-of-Arrays (SoA) column-major per field!
        std::vector<size_t> soaBase(numFields, 0);
        size_t accumSoa = 0;
        for (size_t f = 0; f < numFields; ++f) {
            soaBase[f] = accumSoa;
            accumSoa += numPts * fieldByteSizes[f];
        }

        const uint8_t* raw = decomp.data();
        for (size_t i = 0; i < numPts && pts.size() < kMaxRenderPts; i += ptStride) {
            double x = DetailPcd::ReadPcdScalarLE(raw + soaBase[idxX] + i * fieldByteSizes[idxX], fieldTypes[idxX], fieldSizes[idxX]);
            double y = DetailPcd::ReadPcdScalarLE(raw + soaBase[idxY] + i * fieldByteSizes[idxY], fieldTypes[idxY], fieldSizes[idxY]);
            double z = DetailPcd::ReadPcdScalarLE(raw + soaBase[idxZ] + i * fieldByteSizes[idxZ], fieldTypes[idxZ], fieldSizes[idxZ]);
            if (std::isfinite(x) && std::isfinite(y) && std::isfinite(z)) {
                DetailPcd::PcdSamplePoint sp;
                sp.pos = { static_cast<float>(x), static_cast<float>(z), static_cast<float>(-y) };
                if (idxRgb >= 0) {
                    sp.rgb = DetailPcd::ReadPcdPackedRgbLE(
                        raw + soaBase[idxRgb] + i * fieldByteSizes[idxRgb], fieldTypes[idxRgb], fieldSizes[idxRgb]);
                    sawColorOrIntensity = true;
                }
                if (idxIntensity >= 0) {
                    double inten = DetailPcd::ReadPcdScalarLE(
                        raw + soaBase[idxIntensity] + i * fieldByteSizes[idxIntensity], fieldTypes[idxIntensity], fieldSizes[idxIntensity]);
                    if (std::isfinite(inten)) {
                        sp.intensity = static_cast<float>(inten);
                        sawColorOrIntensity = true;
                    }
                }
                pts.push_back(sp);
            }
        }

        // Fallback if a non-standard exporter wrote AoS inside binary_compressed and SoA produced 0 finite points
        if (pts.empty() && numPts > 1) {
            for (size_t i = 0; i < numPts && pts.size() < kMaxRenderPts; i += ptStride) {
                const uint8_t* rec = raw + i * pointStep;
                double x = DetailPcd::ReadPcdScalarLE(rec + aosOffsets[idxX], fieldTypes[idxX], fieldSizes[idxX]);
                double y = DetailPcd::ReadPcdScalarLE(rec + aosOffsets[idxY], fieldTypes[idxY], fieldSizes[idxY]);
                double z = DetailPcd::ReadPcdScalarLE(rec + aosOffsets[idxZ], fieldTypes[idxZ], fieldSizes[idxZ]);
                if (std::isfinite(x) && std::isfinite(y) && std::isfinite(z)) {
                    DetailPcd::PcdSamplePoint sp;
                    sp.pos = { static_cast<float>(x), static_cast<float>(z), static_cast<float>(-y) };
                    if (idxRgb >= 0) {
                        sp.rgb = DetailPcd::ReadPcdPackedRgbLE(rec + aosOffsets[idxRgb], fieldTypes[idxRgb], fieldSizes[idxRgb]);
                        sawColorOrIntensity = true;
                    }
                    pts.push_back(sp);
                }
            }
        }
    } else if (dataMode.find("binary") != std::string::npos) {
        const size_t availBytes = static_cast<size_t>(bend - bp);
        if (availBytes < pointStep || static_cast<uint64_t>(availBytes) < static_cast<uint64_t>(numPts) * pointStep) {
            return false;
        }
        for (size_t i = 0; i < numPts && pts.size() < kMaxRenderPts; i += ptStride) {
            const uint8_t* rec = bp + i * pointStep;
            double x = DetailPcd::ReadPcdScalarLE(rec + aosOffsets[idxX], fieldTypes[idxX], fieldSizes[idxX]);
            double y = DetailPcd::ReadPcdScalarLE(rec + aosOffsets[idxY], fieldTypes[idxY], fieldSizes[idxY]);
            double z = DetailPcd::ReadPcdScalarLE(rec + aosOffsets[idxZ], fieldTypes[idxZ], fieldSizes[idxZ]);
            if (std::isfinite(x) && std::isfinite(y) && std::isfinite(z)) {
                DetailPcd::PcdSamplePoint sp;
                sp.pos = { static_cast<float>(x), static_cast<float>(z), static_cast<float>(-y) };
                if (idxRgb >= 0) {
                    sp.rgb = DetailPcd::ReadPcdPackedRgbLE(rec + aosOffsets[idxRgb], fieldTypes[idxRgb], fieldSizes[idxRgb]);
                    sawColorOrIntensity = true;
                }
                if (idxIntensity >= 0) {
                    double inten = DetailPcd::ReadPcdScalarLE(
                        rec + aosOffsets[idxIntensity], fieldTypes[idxIntensity], fieldSizes[idxIntensity]);
                    if (std::isfinite(inten)) {
                        sp.intensity = static_cast<float>(inten);
                        sawColorOrIntensity = true;
                    }
                }
                pts.push_back(sp);
            }
        }
    } else if (dataMode.find("ascii") != std::string::npos) {
        const char* cur = reinterpret_cast<const char*>(bp);
        const char* const cend = reinterpret_cast<const char*>(bend);
        std::vector<double> rowVals;
        rowVals.reserve(totalScalars);

        size_t ptIdx = 0;
        while (cur < cend && ptIdx < numPts) {
            const char* lineEnd = cur;
            while (lineEnd < cend && *lineEnd != '\n') ++lineEnd;

            const char* p = cur;
            while (p < lineEnd && (*p == ' ' || *p == '\t' || *p == '\r')) ++p;
            if (p < lineEnd && *p != '#') {
                rowVals.clear();
                while (p < lineEnd && rowVals.size() < totalScalars) {
                    while (p < lineEnd && (*p == ' ' || *p == '\t' || *p == '\r')) ++p;
                    if (p >= lineEnd) break;
                    const char* tokStart = p;
                    while (p < lineEnd && *p != ' ' && *p != '\t' && *p != '\r') ++p;
                    std::string tmp(tokStart, p);
                    char* next = nullptr;
                    double v = std::strtod(tmp.c_str(), &next);
                    if (next == tmp.c_str()) {
                        v = std::numeric_limits<double>::quiet_NaN();
                    }
                    rowVals.push_back(v);
                }
                const size_t minNeeded = std::max({ scalarOffsets[idxX], scalarOffsets[idxY], scalarOffsets[idxZ] }) + 1;
                if (rowVals.size() >= minNeeded) {
                    if ((ptIdx % ptStride) == 0 && pts.size() < kMaxRenderPts) {
                        double x = rowVals[scalarOffsets[idxX]];
                        double y = rowVals[scalarOffsets[idxY]];
                        double z = rowVals[scalarOffsets[idxZ]];
                        if (std::isfinite(x) && std::isfinite(y) && std::isfinite(z)) {
                            DetailPcd::PcdSamplePoint sp;
                            sp.pos = { static_cast<float>(x), static_cast<float>(z), static_cast<float>(-y) };
                            if (idxRgb >= 0 && scalarOffsets[idxRgb] < rowVals.size() && std::isfinite(rowVals[scalarOffsets[idxRgb]])) {
                                if (fieldTypes[idxRgb] == 'F') {
                                    float rf = static_cast<float>(rowVals[scalarOffsets[idxRgb]]);
                                    uint32_t u = 0;
                                    std::memcpy(&u, &rf, 4);
                                    sp.rgb = u & 0x00FFFFFFu;
                                } else {
                                    sp.rgb = static_cast<uint32_t>(rowVals[scalarOffsets[idxRgb]]) & 0x00FFFFFFu;
                                }
                                sawColorOrIntensity = true;
                            }
                            if (idxIntensity >= 0 && scalarOffsets[idxIntensity] < rowVals.size() &&
                                std::isfinite(rowVals[scalarOffsets[idxIntensity]])) {
                                sp.intensity = static_cast<float>(rowVals[scalarOffsets[idxIntensity]]);
                                sawColorOrIntensity = true;
                            }
                            pts.push_back(sp);
                        }
                    }
                    ++ptIdx;
                }
            }
            cur = (lineEnd < cend) ? (lineEnd + 1) : cend;
        }
    } else {
        return false;
    }

    if (pts.empty()) return false;
    if (sawColorOrIntensity) {
        mesh.formatName = L"Point Cloud Library (.pcd, RGB/Intensity)";
    }

    Vec3f bmin = pts[0].pos, bmax = pts[0].pos;
    for (const auto& sp : pts) {
        bmin.x = std::min(bmin.x, sp.pos.x); bmin.y = std::min(bmin.y, sp.pos.y); bmin.z = std::min(bmin.z, sp.pos.z);
        bmax.x = std::max(bmax.x, sp.pos.x); bmax.y = std::max(bmax.y, sp.pos.y); bmax.z = std::max(bmax.z, sp.pos.z);
    }
    float span = std::max({ bmax.x - bmin.x, bmax.y - bmin.y, bmax.z - bmin.z });
    float r = std::max(0.015f, (span > 1e-5f) ? (span * 0.018f) : 0.02f);

    mesh.vertices.reserve(pts.size() * 4);
    mesh.tris.reserve(pts.size() * 4);
    for (const auto& sp : pts) {
        const Vec3f& p = sp.pos;
        uint32_t b = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back({ p.x,     p.y + r, p.z     });
        mesh.vertices.push_back({ p.x + r, p.y - r, p.z + r });
        mesh.vertices.push_back({ p.x - r, p.y - r, p.z + r });
        mesh.vertices.push_back({ p.x,     p.y - r, p.z - r });
        mesh.tris.push_back({ b + 0, b + 1, b + 2, {} });
        mesh.tris.push_back({ b + 0, b + 2, b + 3, {} });
        mesh.tris.push_back({ b + 0, b + 3, b + 1, {} });
        mesh.tris.push_back({ b + 1, b + 3, b + 2, {} });
    }
    return true;
}

} // namespace Formats
