#pragma once
// EsriAsciiGrid.h — ESRI ASCII Grid (.ascgrid / ncols .dem) 3D Terrain Parser.
#include "../common/MeshFormatCommon.h"

namespace Formats {

E2_DRACO_NOINLINE inline bool ParseEsriAsciiGridMesh(std::string_view sv, LoadedMesh3D& mesh) {
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
        return false;
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
