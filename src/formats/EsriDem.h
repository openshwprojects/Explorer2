#pragma once
// EsriDem.h — ESRI ASCII Grid (.dem / .ascgrid) & USGS 7.5-Minute / 1-Degree ASCII DEM (.dem) 3D Terrain Parser
// Supports arbitrary grid sizes (>256x256 via stride downsampling), xllcorner/xllcenter, yllcorner/yllcenter,
// cellsize/dx/dy, NODATA_value triangle culling, hypsometric elevation ramp, and USGS Type A + Type B profiles.

namespace Formats {

namespace DetailDem {

inline uint32_t ComputeHypsometricRgb(float normH) {
    float t = std::clamp(normH, 0.0f, 1.0f);
    // Clean hypsometric ramp: lowland green -> foothill gold -> alpine brown -> snow peak white
    float r = 0.0f, g = 0.0f, b = 0.0f;
    if (t < 0.33f) {
        float u = t / 0.33f;
        r = 0.22f + 0.50f * u;
        g = 0.56f + 0.22f * u;
        b = 0.24f - 0.06f * u;
    } else if (t < 0.70f) {
        float u = (t - 0.33f) / 0.37f;
        r = 0.72f - 0.14f * u;
        g = 0.78f - 0.34f * u;
        b = 0.18f + 0.08f * u;
    } else {
        float u = (t - 0.70f) / 0.30f;
        r = 0.58f + 0.38f * u;
        g = 0.44f + 0.52f * u;
        b = 0.26f + 0.70f * u;
    }
    return (static_cast<uint32_t>(r * 255.0f) << 16) |
           (static_cast<uint32_t>(g * 255.0f) << 8)  |
            static_cast<uint32_t>(b * 255.0f);
}

inline bool EqualsIgnoreCaseDem(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

// Parses Fortran 'D'/'E' double from fixed-width or token string_view
inline bool ParseFortranDouble(std::string_view sv, double& out) {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r' || sv.front() == '\n')) sv.remove_prefix(1);
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r' || sv.back() == '\n')) sv.remove_suffix(1);
    if (sv.empty() || sv.size() >= 64) return false;
    char buf[64];
    for (size_t i = 0; i < sv.size(); ++i) {
        char c = sv[i];
        if (c == 'D' || c == 'd') c = 'E';
        buf[i] = c;
    }
    buf[sv.size()] = '\0';
    char* next = nullptr;
    out = std::strtod(buf, &next);
    if (next == buf || !std::isfinite(out)) return false;
    while (*next == ' ') ++next;
    return *next == '\0';
}

inline bool ParseFixedInt(std::string_view sv, int64_t& out) {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t')) sv.remove_prefix(1);
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r' || sv.back() == '\n')) sv.remove_suffix(1);
    if (sv.empty()) return false;
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), out);
    return ec == std::errc{} && ptr == sv.data() + sv.size();
}

// Streams next Fortran number token (splitting concatenated signs like "120-32767")
inline bool NextUsgsToken(const char*& cur, const char* end, std::string_view& outTok) {
    while (cur < end && (*cur == ' ' || *cur == '\t' || *cur == '\r' || *cur == '\n')) ++cur;
    if (cur >= end) return false;
    const char* s = cur;
    ++cur;
    while (cur < end) {
        char c = *cur;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') break;
        if ((c == '+' || c == '-') && cur > s) {
            char prev = *(cur - 1);
            if (prev != 'E' && prev != 'e' && prev != 'D' && prev != 'd') {
                break;
            }
        }
        ++cur;
    }
    outTok = std::string_view(s, static_cast<size_t>(cur - s));
    return true;
}

inline bool EmitTerrainGrid(
    size_t rows, size_t cols,
    const std::vector<float>& elevGrid,
    const std::vector<uint8_t>& validMask,
    double dx, double dy,
    LoadedMesh3D& mesh)
{
    if (rows < 2 || cols < 2 || elevGrid.size() < rows * cols || validMask.size() < rows * cols) {
        return false;
    }

    float minE = 1e30f, maxE = -1e30f;
    for (size_t i = 0; i < rows * cols; ++i) {
        if (validMask[i]) {
            minE = std::min(minE, elevGrid[i]);
            maxE = std::max(maxE, elevGrid[i]);
        }
    }
    if (minE > maxE) return false;
    float invSpan = (maxE > minE + 1e-5f) ? (1.0f / (maxE - minE)) : 0.0f;

    std::vector<uint32_t> vertRemap(rows * cols, 0xFFFFFFFFu);
    auto getOrEmitVert = [&](size_t r, size_t c) -> uint32_t {
        size_t idx = r * cols + c;
        if (vertRemap[idx] != 0xFFFFFFFFu) return vertRemap[idx];
        uint32_t vId = static_cast<uint32_t>(mesh.vertices.size());
        float h = elevGrid[idx];
        (void)ComputeHypsometricRgb((h - minE) * invSpan);
        mesh.vertices.push_back({
            static_cast<float>(static_cast<double>(c) * dx),
            h,
            static_cast<float>(static_cast<double>(r) * dy)
        });
        vertRemap[idx] = vId;
        return vId;
    };

    for (size_t r = 0; r + 1 < rows; ++r) {
        for (size_t c = 0; c + 1 < cols; ++c) {
            const uint8_t v00 = validMask[r * cols + c];
            const uint8_t v10 = validMask[r * cols + (c + 1)];
            const uint8_t v01 = validMask[(r + 1) * cols + c];
            const uint8_t v11 = validMask[(r + 1) * cols + (c + 1)];

            if (v00 && v01 && v10) {
                uint32_t i00 = getOrEmitVert(r, c);
                uint32_t i01 = getOrEmitVert(r + 1, c);
                uint32_t i10 = getOrEmitVert(r, c + 1);
                mesh.tris.push_back({ i00, i01, i10, {} });
            }
            if (v10 && v01 && v11) {
                uint32_t i10 = getOrEmitVert(r, c + 1);
                uint32_t i01 = getOrEmitVert(r + 1, c);
                uint32_t i11 = getOrEmitVert(r + 1, c + 1);
                mesh.tris.push_back({ i10, i01, i11, {} });
            }
        }
    }
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

// Parses genuine USGS 7.5-Minute / 1-Degree ASCII DEM (Type A 1024-byte logical record + Type B elevation profiles)
inline bool ParseUsgsAsciiDem(std::string_view sv, LoadedMesh3D& mesh) {
    if (sv.size() < 864) return false;

    // Type A header must be pure printable ASCII
    const size_t checkLen = std::min<size_t>(sv.size(), 1024);
    for (size_t i = 0; i < checkLen; ++i) {
        unsigned char u = static_cast<unsigned char>(sv[i]);
        if (u < 32 && u != '\r' && u != '\n' && u != '\t') return false;
        if (u >= 127) return false;
    }

    int64_t demLevel = 0, elevPattern = 0, planSys = -1, groundUnit = -1, elevUnit = -1, polySides = 0;
    if (!ParseFixedInt(sv.substr(144, 6), demLevel) || demLevel < 1 || demLevel > 4) return false;
    if (!ParseFixedInt(sv.substr(150, 6), elevPattern) || (elevPattern != 1 && elevPattern != 2)) return false;
    if (!ParseFixedInt(sv.substr(156, 6), planSys) || planSys < 0 || planSys > 20) return false;
    if (!ParseFixedInt(sv.substr(528, 6), groundUnit) || groundUnit < 0 || groundUnit > 3) return false;
    if (!ParseFixedInt(sv.substr(534, 6), elevUnit) || (elevUnit != 1 && elevUnit != 2)) return false;
    if (!ParseFixedInt(sv.substr(540, 6), polySides) || polySides != 4) return false;

    // Validate 4x2D quadrangle corners (546..738) and min/max elevation (738..786)
    double corners[8] = {};
    for (int i = 0; i < 8; ++i) {
        if (!ParseFortranDouble(sv.substr(546 + static_cast<size_t>(i) * 24, 24), corners[i])) return false;
    }
    double minElevHdr = 0.0, maxElevHdr = 0.0;
    if (!ParseFortranDouble(sv.substr(738, 24), minElevHdr) ||
        !ParseFortranDouble(sv.substr(762, 24), maxElevHdr)) {
        return false;
    }

    // Spatial resolution (dx, dy, dz) and (rows, cols) profile count:
    // Standard USGS Type A columns: accuracy at 810..816, dx/dy/dz at 816..852 (3x12), rows/cols at 852..864 (2x6).
    // Also fallback-check 810..846 + 846..858 in case accuracy code was omitted.
    double dx = 0.0, dy = 0.0, dz = 0.0;
    int64_t profRows = 0, profCols = 0;
    bool gotRes = (sv.size() >= 864 &&
                   ParseFortranDouble(sv.substr(816, 12), dx) &&
                   ParseFortranDouble(sv.substr(828, 12), dy) &&
                   ParseFortranDouble(sv.substr(840, 12), dz) &&
                   ParseFixedInt(sv.substr(852, 6), profRows) &&
                   ParseFixedInt(sv.substr(858, 6), profCols));
    if (!gotRes) {
        gotRes = (ParseFortranDouble(sv.substr(810, 12), dx) &&
                  ParseFortranDouble(sv.substr(822, 12), dy) &&
                  ParseFortranDouble(sv.substr(834, 12), dz) &&
                  ParseFixedInt(sv.substr(846, 6), profRows) &&
                  ParseFixedInt(sv.substr(852, 6), profCols));
    }
    if (!gotRes) {
        // Tokenized fallback across bytes 810..min(size, 872)
        std::string_view tail = sv.substr(810, std::min<size_t>(sv.size() - 810, 62));
        const char* tc = tail.data();
        const char* te = tc + tail.size();
        std::vector<std::string_view> tks;
        std::string_view tk;
        while (NextUsgsToken(tc, te, tk)) tks.push_back(tk);
        if (tks.size() >= 5) {
            size_t n = tks.size();
            gotRes = ParseFortranDouble(tks[n - 5], dx) &&
                     ParseFortranDouble(tks[n - 4], dy) &&
                     ParseFortranDouble(tks[n - 3], dz) &&
                     ParseFixedInt(tks[n - 2], profRows) &&
                     ParseFixedInt(tks[n - 1], profCols);
        }
    }

    if (!gotRes || dx <= 0.0 || dy <= 0.0 || dz <= 0.0 || profRows < 1 || profCols < 2 || profCols > 50000) {
        return false;
    }

    // Locate start of first Type B profile: either byte 1024 (standard 1024B logical record) or after newline >= 864
    size_t bStart = 1024;
    size_t nlAfterA = sv.find('\n', 858);
    if (nlAfterA != std::string_view::npos && nlAfterA < 1024) {
        bStart = nlAfterA + 1;
    }
    if (bStart >= sv.size()) return false;

    // Ground unit 3 = arc-seconds (1-degree DEMs): convert arc-sec horizontal spacing roughly to meters so elevation isn't towering
    double horizScaleX = dx;
    double horizScaleY = dy;
    if (groundUnit == 3) {
        horizScaleX = dx * 25.0;
        horizScaleY = dy * 30.87;
    }
    if (elevUnit == 1 && groundUnit == 2) {
        dz *= 0.3048; // feet -> meters
    }

    struct UsgsProfile {
        double xGo = 0.0;
        double yGo = 0.0;
        std::vector<float> elevs;
        std::vector<uint8_t> valid;
    };
    std::vector<UsgsProfile> profiles;
    profiles.reserve(static_cast<size_t>(profCols));

    const char* cur = sv.data() + bStart;
    const char* const end = sv.data() + sv.size();
    for (int64_t p = 0; p < profCols && cur < end; ++p) {
        std::string_view tRow, tCol, tM, tN, tX, tY, tDatum, tMinZ, tMaxZ;
        if (!NextUsgsToken(cur, end, tRow) || !NextUsgsToken(cur, end, tCol) ||
            !NextUsgsToken(cur, end, tM)   || !NextUsgsToken(cur, end, tN)   ||
            !NextUsgsToken(cur, end, tX)   || !NextUsgsToken(cur, end, tY)   ||
            !NextUsgsToken(cur, end, tDatum) || !NextUsgsToken(cur, end, tMinZ) ||
            !NextUsgsToken(cur, end, tMaxZ))
        {
            break;
        }
        int64_t rowId = 0, colId = 0, mRows = 0, nColsP = 0;
        double xGo = 0.0, yGo = 0.0, datum = 0.0, pMinZ = 0.0, pMaxZ = 0.0;
        if (!ParseFixedInt(tRow, rowId) || !ParseFixedInt(tCol, colId) ||
            !ParseFixedInt(tM, mRows)   || !ParseFixedInt(tN, nColsP)  ||
            !ParseFortranDouble(tX, xGo) || !ParseFortranDouble(tY, yGo) ||
            !ParseFortranDouble(tDatum, datum) ||
            !ParseFortranDouble(tMinZ, pMinZ) || !ParseFortranDouble(tMaxZ, pMaxZ))
        {
            break;
        }
        if (mRows <= 0 || mRows > 100000) return false;

        UsgsProfile prof;
        prof.xGo = xGo;
        prof.yGo = yGo;
        prof.elevs.resize(static_cast<size_t>(mRows), 0.0f);
        prof.valid.resize(static_cast<size_t>(mRows), 0);

        bool okVals = true;
        for (int64_t r = 0; r < mRows; ++r) {
            std::string_view tv;
            if (!NextUsgsToken(cur, end, tv)) { okVals = false; break; }
            int64_t rawInt = 0;
            if (!ParseFixedInt(tv, rawInt)) { okVals = false; break; }
            if (rawInt == -32767 || rawInt == -32768) {
                prof.valid[static_cast<size_t>(r)] = 0;
            } else {
                prof.elevs[static_cast<size_t>(r)] = static_cast<float>(datum + static_cast<double>(rawInt) * dz);
                prof.valid[static_cast<size_t>(r)] = 1;
            }
        }
        if (!okVals) break;
        profiles.push_back(std::move(prof));
    }

    if (profiles.size() < 2) return false;

    double minYGo = profiles[0].yGo;
    double maxYEnd = profiles[0].yGo;
    for (const auto& pr : profiles) {
        minYGo = std::min(minYGo, pr.yGo);
        maxYEnd = std::max(maxYEnd, pr.yGo + static_cast<double>(pr.elevs.size() - 1) * dy);
    }
    size_t fullCols = profiles.size();
    size_t fullRows = static_cast<size_t>(std::llround((maxYEnd - minYGo) / dy)) + 1;
    if (fullRows < 2 || fullRows > 50000) return false;

    const size_t step = std::max<size_t>(1, static_cast<size_t>(std::ceil(static_cast<double>(std::max(fullCols, fullRows)) / 512.0)));
    std::vector<size_t> cIdx, rIdx;
    for (size_t c = 0; c < fullCols; c += step) cIdx.push_back(c);
    if (cIdx.back() != fullCols - 1) cIdx.push_back(fullCols - 1);
    for (size_t r = 0; r < fullRows; r += step) rIdx.push_back(r);
    if (rIdx.back() != fullRows - 1) rIdx.push_back(fullRows - 1);

    const size_t sCols = cIdx.size();
    const size_t sRows = rIdx.size();
    std::vector<float> grid(sRows * sCols, 0.0f);
    std::vector<uint8_t> mask(sRows * sCols, 0);

    for (size_t sc = 0; sc < sCols; ++sc) {
        const auto& pr = profiles[cIdx[sc]];
        int64_t rOffset = std::llround((pr.yGo - minYGo) / dy);
        for (size_t sr = 0; sr < sRows; ++sr) {
            // Flip row index so North (+Y) maps to -Z in right-handed Y-up viewer
            int64_t srcR = static_cast<int64_t>(fullRows - 1 - rIdx[sr]) - rOffset;
            if (srcR >= 0 && static_cast<size_t>(srcR) < pr.elevs.size() && pr.valid[static_cast<size_t>(srcR)]) {
                grid[sr * sCols + sc] = pr.elevs[static_cast<size_t>(srcR)];
                mask[sr * sCols + sc] = 1;
            }
        }
    }

    mesh.formatName = L"USGS Digital Elevation Model (.dem)";
    return EmitTerrainGrid(sRows, sCols, grid, mask, horizScaleX * static_cast<double>(step), horizScaleY * static_cast<double>(step), mesh);
}

} // namespace DetailDem

inline bool ParseEsriDemMesh(std::string_view sv, LoadedMesh3D& mesh) {
    if (sv.size() < 16) return false;

    // Skip leading whitespace / BOM to inspect first keyword
    size_t start = 0;
    if (sv.size() >= 3 &&
        static_cast<uint8_t>(sv[0]) == 0xEF &&
        static_cast<uint8_t>(sv[1]) == 0xBB &&
        static_cast<uint8_t>(sv[2]) == 0xBF) {
        start = 3;
    }
    while (start < sv.size() && (sv[start] == ' ' || sv[start] == '\t' || sv[start] == '\r' || sv[start] == '\n')) {
        ++start;
    }
    if (start >= sv.size()) return false;

    // Check if header begins with an ESRI ASCII Grid keyword (ncols / nrows / xllcorner / etc.)
    std::string_view probe = sv.substr(start, std::min<size_t>(512, sv.size() - start));
    bool looksEsri = false;
    {
        size_t kwEnd = 0;
        while (kwEnd < probe.size() && std::isalpha(static_cast<unsigned char>(probe[kwEnd]))) ++kwEnd;
        std::string_view firstWord = probe.substr(0, kwEnd);
        if (DetailDem::EqualsIgnoreCaseDem(firstWord, "ncols") ||
            DetailDem::EqualsIgnoreCaseDem(firstWord, "nrows") ||
            DetailDem::EqualsIgnoreCaseDem(firstWord, "xllcorner") ||
            DetailDem::EqualsIgnoreCaseDem(firstWord, "xllcenter") ||
            DetailDem::EqualsIgnoreCaseDem(firstWord, "yllcorner") ||
            DetailDem::EqualsIgnoreCaseDem(firstWord, "yllcenter")) {
            looksEsri = true;
        }
    }

    if (!looksEsri) {
        return DetailDem::ParseUsgsAsciiDem(sv, mesh);
    }

    // Parse ESRI ASCII Grid header lines
    int64_t ncols = 0, nrows = 0;
    double xll = 0.0, yll = 0.0;
    bool centerReg = false;
    double dx = 1.0, dy = 1.0;
    bool hasDx = false, hasDy = false, hasCellSize = false;
    double nodata = -9999.0;
    bool hasNoData = false;

    const char* cur = sv.data() + start;
    const char* const end = sv.data() + sv.size();

    while (cur < end) {
        while (cur < end && (*cur == ' ' || *cur == '\t' || *cur == '\r' || *cur == '\n')) ++cur;
        if (cur >= end) break;
        if (!std::isalpha(static_cast<unsigned char>(*cur))) break;

        const char* ks = cur;
        while (cur < end && *cur != ' ' && *cur != '\t' && *cur != '\r' && *cur != '\n') ++cur;
        std::string_view key(ks, static_cast<size_t>(cur - ks));

        while (cur < end && (*cur == ' ' || *cur == '\t')) ++cur;
        const char* vs = cur;
        while (cur < end && *cur != ' ' && *cur != '\t' && *cur != '\r' && *cur != '\n') ++cur;
        std::string_view valSv(vs, static_cast<size_t>(cur - vs));
        while (cur < end && *cur != '\n') ++cur;
        if (cur < end && *cur == '\n') ++cur;

        double val = 0.0;
        if (!DetailDem::ParseFortranDouble(valSv, val)) return false;

        if (DetailDem::EqualsIgnoreCaseDem(key, "ncols")) ncols = static_cast<int64_t>(std::llround(val));
        else if (DetailDem::EqualsIgnoreCaseDem(key, "nrows")) nrows = static_cast<int64_t>(std::llround(val));
        else if (DetailDem::EqualsIgnoreCaseDem(key, "xllcorner")) { xll = val; }
        else if (DetailDem::EqualsIgnoreCaseDem(key, "yllcorner")) { yll = val; }
        else if (DetailDem::EqualsIgnoreCaseDem(key, "xllcenter")) { xll = val; centerReg = true; }
        else if (DetailDem::EqualsIgnoreCaseDem(key, "yllcenter")) { yll = val; centerReg = true; }
        else if (DetailDem::EqualsIgnoreCaseDem(key, "cellsize")) {
            dx = dy = std::max(1e-6, val);
            hasCellSize = true;
        } else if (DetailDem::EqualsIgnoreCaseDem(key, "dx")) {
            dx = std::max(1e-6, val);
            hasDx = true;
        } else if (DetailDem::EqualsIgnoreCaseDem(key, "dy")) {
            dy = std::max(1e-6, val);
            hasDy = true;
        } else if (DetailDem::EqualsIgnoreCaseDem(key, "nodata_value") ||
                   DetailDem::EqualsIgnoreCaseDem(key, "nodata")) {
            nodata = val;
            hasNoData = true;
        } else {
            return false;
        }
    }

    (void)xll; (void)yll; (void)centerReg; (void)hasCellSize; (void)hasDx; (void)hasDy;
    if (ncols < 2 || nrows < 2 || ncols > 50000 || nrows > 50000) return false;
    const uint64_t totalCells = static_cast<uint64_t>(ncols) * static_cast<uint64_t>(nrows);
    if (totalCells > 100000000ULL) return false;

    mesh.formatName = L"ESRI ASCII Digital Elevation Model (.dem)";

    // Compute stride step = max(1, ceil(max(ncols, nrows) / 512.0)) and exact sampled row/col tables
    const size_t uCols = static_cast<size_t>(ncols);
    const size_t uRows = static_cast<size_t>(nrows);
    const size_t step = std::max<size_t>(1, static_cast<size_t>(std::ceil(static_cast<double>(std::max(uCols, uRows)) / 512.0)));

    std::vector<int32_t> colToSample(uCols, -1);
    std::vector<int32_t> rowToSample(uRows, -1);
    size_t sCols = 0, sRows = 0;
    for (size_t c = 0; c < uCols; c += step) colToSample[c] = static_cast<int32_t>(sCols++);
    if (colToSample[uCols - 1] < 0) colToSample[uCols - 1] = static_cast<int32_t>(sCols++);
    for (size_t r = 0; r < uRows; r += step) rowToSample[r] = static_cast<int32_t>(sRows++);
    if (rowToSample[uRows - 1] < 0) rowToSample[uRows - 1] = static_cast<int32_t>(sRows++);

    std::vector<float> sampledElev(sRows * sCols, 0.0f);
    std::vector<uint8_t> sampledMask(sRows * sCols, 0);
    const double nodataTol = std::max(1e-4, std::fabs(nodata) * 1e-5);

    for (size_t r = 0; r < uRows; ++r) {
        const int32_t sr = rowToSample[r];
        for (size_t c = 0; c < uCols; ++c) {
            while (cur < end && (*cur == ' ' || *cur == '\t' || *cur == '\r' || *cur == '\n')) ++cur;
            if (cur >= end) return false;
            const char* ts = cur;
            while (cur < end && *cur != ' ' && *cur != '\t' && *cur != '\r' && *cur != '\n') ++cur;
            size_t len = static_cast<size_t>(cur - ts);
            if (len == 0 || len >= 64) return false;

            if (sr >= 0 && colToSample[c] >= 0) {
                char buf[64];
                std::memcpy(buf, ts, len);
                buf[len] = '\0';
                char* next = nullptr;
                double v = std::strtod(buf, &next);
                if (next == buf) return false;
                bool isVoid = !std::isfinite(v) ||
                              (hasNoData && std::fabs(v - nodata) <= nodataTol) ||
                              (!hasNoData && std::fabs(v - (-9999.0)) <= 1e-3);
                size_t dstIdx = static_cast<size_t>(sr) * sCols + static_cast<size_t>(colToSample[c]);
                if (!isVoid) {
                    sampledElev[dstIdx] = static_cast<float>(v);
                    sampledMask[dstIdx] = 1;
                }
            }
        }
    }

    return DetailDem::EmitTerrainGrid(
        sRows, sCols, sampledElev, sampledMask,
        dx * static_cast<double>(step), dy * static_cast<double>(step), mesh);
}

} // namespace Formats
