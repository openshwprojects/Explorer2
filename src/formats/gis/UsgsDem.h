#pragma once
// UsgsDem.h — USGS 7.5-Minute / 1-Degree ASCII DEM (.dem) 3D Terrain Parser.
#include "../common/MeshFormatCommon.h"

namespace Formats {

// Parses genuine USGS 7.5-Minute / 1-Degree ASCII DEM (Type A 1024-byte logical record + Type B elevation profiles)
inline bool ParseUsgsDemMesh(std::string_view sv, LoadedMesh3D& mesh) {
    using namespace DetailDem;
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

} // namespace Formats
