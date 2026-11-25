#pragma once
// Warzone2100Pie.h — Warzone 2100 3D Unit & Structure Mesh (.pie — PIE 2 / PIE 3 / PIE 4)
// Parses multi-LEVEL `POINTS`, `POLYGONS` (with hex flags, vertex count, optional texAnim), and `CONNECTORS`.

namespace Formats {

inline bool ParseWarzone2100Pie(const uint8_t* data, size_t size, LoadedMesh3D& outMesh) {
    std::string_view sv;
    if (!FormatPrologue::PrepareUtf8TextNoNul(data, size, sv, 18u, (32u << 20))) return false;

    size_t linePos = 0u;
    std::string_view rawLine;
    int pieVersion = 0;
    while (ReaderText::NextLineSv(sv, linePos, rawLine)) {
        std::string_view tr = ReaderText::TrimSv(rawLine);
        if (tr.empty() || tr.front() == '#') continue;
        std::string_view kw, vTok;
        if (!ReaderText::NextTokenSv(tr, kw) || !ReaderText::AsciiEqCI(kw, "PIE") ||
            !ReaderText::NextTokenSv(tr, vTok) || !ReaderText::ParseIntSv(vTok, pieVersion) ||
            (pieVersion != 2 && pieVersion != 3 && pieVersion != 4)) {
            return false;
        }
        break;
    }
    if (pieVersion == 0) return false;

    outMesh = LoadedMesh3D{};
    size_t levelVertBase = 0u;
    int expectPoints = 0;
    int expectPolys  = 0;
    int expectConns  = 0;

    while (ReaderText::NextLineSv(sv, linePos, rawLine)) {
        size_t hash = rawLine.find('#');
        if (hash != std::string_view::npos) rawLine = rawLine.substr(0u, hash);
        std::string_view line = ReaderText::TrimSv(rawLine);
        if (line.empty()) continue;

        if (expectPoints > 0) {
            std::string_view tx, ty, tz;
            float x = 0.0f, y = 0.0f, z = 0.0f;
            if (!ReaderText::NextTokenSv(line, tx) ||
                !ReaderText::NextTokenSv(line, ty) ||
                !ReaderText::NextTokenSv(line, tz) ||
                !ReaderText::ParseFloatSv(tx, x) ||
                !ReaderText::ParseFloatSv(ty, y) ||
                !ReaderText::ParseFloatSv(tz, z) ||
                !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
                outMesh = {};
                return false;
            }
            outMesh.vertices.push_back({ x, y, z });
            --expectPoints;
            continue;
        }

        if (expectPolys > 0) {
            std::string_view flagsTok, countTok;
            int nVerts = 0;
            if (!ReaderText::NextTokenSv(line, flagsTok) ||
                !ReaderText::NextTokenSv(line, countTok) ||
                !ReaderText::ParseIntSv(countTok, nVerts) ||
                nVerts < 3 || nVerts > 16) {
                outMesh = {};
                return false;
            }
            const size_t localPts = outMesh.vertices.size() - levelVertBase;
            std::vector<uint32_t> idx(static_cast<size_t>(nVerts));
            for (int k = 0; k < nVerts; ++k) {
                std::string_view itok;
                int vi = -1;
                if (!ReaderText::NextTokenSv(line, itok) || !ReaderText::ParseIntSv(itok, vi) ||
                    vi < 0 || static_cast<size_t>(vi) >= localPts) {
                    outMesh = {};
                    return false;
                }
                idx[static_cast<size_t>(k)] = static_cast<uint32_t>(levelVertBase + static_cast<size_t>(vi));
            }
            for (int k = 1; k + 1 < nVerts; ++k) {
                uint32_t a = idx[0], b = idx[static_cast<size_t>(k)], c = idx[static_cast<size_t>(k + 1)];
                if (a != b && b != c && a != c) {
                    outMesh.tris.push_back({ a, b, c, {} });
                }
            }
            --expectPolys;
            continue;
        }

        if (expectConns > 0) {
            std::string_view tx, ty, tz;
            float x = 0, y = 0, z = 0;
            if (!ReaderText::NextTokenSv(line, tx) ||
                !ReaderText::NextTokenSv(line, ty) ||
                !ReaderText::NextTokenSv(line, tz) ||
                !ReaderText::ParseFloatSv(tx, x) ||
                !ReaderText::ParseFloatSv(ty, y) ||
                !ReaderText::ParseFloatSv(tz, z)) {
                outMesh = {};
                return false;
            }
            --expectConns;
            continue;
        }

        std::string_view kw;
        if (!ReaderText::NextTokenSv(line, kw)) continue;
        if (ReaderText::AsciiEqCI(kw, "LEVEL")) {
            levelVertBase = outMesh.vertices.size();
        } else if (ReaderText::AsciiEqCI(kw, "POINTS")) {
            std::string_view cntTok;
            if (!ReaderText::NextTokenSv(line, cntTok) || !ReaderText::ParseIntSv(cntTok, expectPoints) ||
                expectPoints <= 0 || expectPoints > 500000) {
                outMesh = {};
                return false;
            }
        } else if (ReaderText::AsciiEqCI(kw, "POLYGONS")) {
            std::string_view cntTok;
            if (!ReaderText::NextTokenSv(line, cntTok) || !ReaderText::ParseIntSv(cntTok, expectPolys) ||
                expectPolys <= 0 || expectPolys > 1000000) {
                outMesh = {};
                return false;
            }
        } else if (ReaderText::AsciiEqCI(kw, "CONNECTORS")) {
            std::string_view cntTok;
            if (!ReaderText::NextTokenSv(line, cntTok) || !ReaderText::ParseIntSv(cntTok, expectConns) ||
                expectConns < 0 || expectConns > 4096) {
                outMesh = {};
                return false;
            }
        }
    }

    if (expectPoints != 0 || expectPolys != 0 || expectConns != 0) {
        outMesh = {};
        return false; // Truncated POINTS / POLYGONS / CONNECTORS table!
    }
    if (outMesh.vertices.empty() || outMesh.tris.empty()) {
        outMesh = {};
        return false;
    }

    wchar_t fmtBuf[96] = {};
    swprintf_s(fmtBuf, L"Warzone 2100 Model (.pie v%d)", pieVersion);
    outMesh.formatName = fmtBuf;
    return !outMesh.vertices.empty() && !outMesh.tris.empty();
}

} // namespace Formats
