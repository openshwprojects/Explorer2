#pragma once
// NastranBulkNas.h — MSC / NX / Simcenter Nastran Bulk Data (.nas) 3D Mesh Reader
// Supports 8-char small-field, 16-char large-field (*), and comma free-field cards, Nastran
// exponentless scientific floats ("1.25-3", "-4.5+2"), CORD2R rectangular local coordinate systems,
// GRID, CTRIA3/CTRIA6/CTRIAR, CQUAD4/CQUAD8/CQUADR, CTETRA, CHEXA, CPENTA, CPYRAM, CBAR/CBEAM/CROD,
// and explicit (+CONT / *CONT) & implicit blank continuation cards.
#include "../../ReaderTextUtils.h"
#include "../common/FemSolidSurfaceCommon.h"

namespace Formats {

namespace DetailNastranNas {

inline bool ParseNastranDouble(std::string_view sv, double& out) {
    sv = ReaderText::TrimSv(sv);
    if (sv.empty()) { out = 0.0; return true; }
    if (sv.size() >= 64) return false;
    char buf[68];
    size_t j = 0;
    for (size_t i = 0; i < sv.size() && j + 2 < sizeof(buf); ++i) {
        char c = sv[i];
        if (c == 'D' || c == 'd') c = 'E';
        if ((c == '+' || c == '-') && i > 0 &&
            sv[i - 1] != 'e' && sv[i - 1] != 'E' && sv[i - 1] != 'd' && sv[i - 1] != 'D')
        {
            buf[j++] = 'E';
        }
        buf[j++] = c;
    }
    buf[j] = '\0';
    char* endp = nullptr;
    double v = std::strtod(buf, &endp);
    if (endp == buf || *endp != '\0' || !std::isfinite(v)) return false;
    out = v;
    return true;
}

inline bool ParseNastranInt(std::string_view sv, int64_t& out) {
    sv = ReaderText::TrimSv(sv);
    if (sv.empty()) { out = 0; return true; }
    return ReaderText::ParseInt64Sv(sv, out);
}

struct Cord2R {
    int64_t cid = 0;
    int64_t rid = 0;
    double a[3] = { 0, 0, 0 }; // Origin
    double b[3] = { 0, 0, 1 }; // Point on Z axis
    double c[3] = { 1, 0, 0 }; // Point in XZ plane

    // Orthonormal basis in basic (global) system once resolved
    double origin[3] = { 0, 0, 0 };
    double ex[3] = { 1, 0, 0 };
    double ey[3] = { 0, 1, 0 };
    double ez[3] = { 0, 0, 1 };
    bool resolved = false;

    void TransformLocalToBasic(double lx, double ly, double lz, double& gx, double& gy, double& gz) const {
        gx = origin[0] + ex[0] * lx + ey[0] * ly + ez[0] * lz;
        gy = origin[1] + ex[1] * lx + ey[1] * ly + ez[1] * lz;
        gz = origin[2] + ex[2] * lx + ey[2] * ly + ez[2] * lz;
    }
};

struct LogicalCard {
    std::string headUpper;
    std::vector<std::string> fields; // f[0] = field 2, f[1] = field 3, ...
};

inline bool IsContinuationPrefix(std::string_view firstCol) {
    firstCol = ReaderText::TrimSv(firstCol);
    if (firstCol.empty()) return true;
    char c = firstCol.front();
    return c == '+' || c == '*';
}

inline void ParsePhysicalLineToFields(
    std::string_view rawLine,
    bool& outIsCont,
    std::string& outHead,
    std::vector<std::string>& outDataFields,
    std::string& outTailContTag)
{
    outDataFields.clear();
    outHead.clear();
    outTailContTag.clear();
    outIsCont = false;

    // Strip inline Nastran '$' comment
    size_t dollar = rawLine.find('$');
    if (dollar != std::string_view::npos) rawLine = rawLine.substr(0, dollar);
    while (!rawLine.empty() && (rawLine.back() == '\r' || rawLine.back() == '\n')) rawLine.remove_suffix(1);
    if (ReaderText::TrimSv(rawLine).empty()) return;

    // 1. Free-field format (comma-delimited)
    if (rawLine.find(',') != std::string_view::npos) {
        std::vector<std::string_view> parts;
        size_t p = 0;
        while (p <= rawLine.size()) {
            size_t cm = rawLine.find(',', p);
            parts.push_back(cm == std::string_view::npos ? rawLine.substr(p) : rawLine.substr(p, cm - p));
            if (cm == std::string_view::npos) break;
            p = cm + 1;
        }
        if (parts.empty()) return;
        outIsCont = IsContinuationPrefix(parts[0]);
        outHead = ReaderText::ToUpperAscii(ReaderText::TrimSv(parts[0]));
        if (!outHead.empty() && outHead.back() == '*') outHead.pop_back();
        size_t endIdx = parts.size();
        if (endIdx >= 10) {
            std::string_view last = ReaderText::TrimSv(parts.back());
            if (!last.empty() && (last.front() == '+' || last.front() == '*')) {
                outTailContTag = std::string(last);
                --endIdx;
            }
        }
        for (size_t i = 1; i < endIdx; ++i) {
            outDataFields.emplace_back(ReaderText::TrimSv(parts[i]));
        }
        return;
    }

    // 2. Fixed-field format: check 8-char field 1 for '*' (16-char large-field) vs standard 8-char small-field
    std::string_view col1 = rawLine.substr(0, std::min<size_t>(8u, rawLine.size()));
    std::string_view col1Trim = ReaderText::TrimSv(col1);
    outIsCont = IsContinuationPrefix(col1);

    const bool isLargeField = (!col1Trim.empty() &&
                               ((!outIsCont && col1Trim.back() == '*') || (outIsCont && col1Trim.front() == '*')));
    if (!outIsCont) {
        outHead = ReaderText::ToUpperAscii(col1Trim);
        if (!outHead.empty() && outHead.back() == '*') outHead.pop_back();
    } else {
        outHead = std::string(col1Trim);
    }

    if (isLargeField) {
        // Large-field line: col 0..7 (8), four 16-char data fields (8..23, 24..39, 40..55, 56..71), col 72..79 cont tag
        for (int k = 0; k < 4; ++k) {
            size_t off = 8u + static_cast<size_t>(k) * 16u;
            if (off >= rawLine.size()) {
                outDataFields.emplace_back();
            } else {
                size_t len = std::min<size_t>(16u, rawLine.size() - off);
                outDataFields.emplace_back(ReaderText::TrimSv(rawLine.substr(off, len)));
            }
        }
        if (rawLine.size() > 72u) {
            outTailContTag = std::string(ReaderText::TrimSv(rawLine.substr(72u, std::min<size_t>(8u, rawLine.size() - 72u))));
        }
    } else {
        // Small-field line: col 0..7 (8), eight 8-char data fields (8..71), col 72..79 cont tag
        for (int k = 0; k < 8; ++k) {
            size_t off = 8u + static_cast<size_t>(k) * 8u;
            if (off >= rawLine.size()) {
                outDataFields.emplace_back();
            } else {
                size_t len = std::min<size_t>(8u, rawLine.size() - off);
                outDataFields.emplace_back(ReaderText::TrimSv(rawLine.substr(off, len)));
            }
        }
        if (rawLine.size() > 72u) {
            outTailContTag = std::string(ReaderText::TrimSv(rawLine.substr(72u, std::min<size_t>(8u, rawLine.size() - 72u))));
        }
    }
}

} // namespace DetailNastranNas

inline bool ParseNastranBulkNas(
    const uint8_t* data,
    size_t size,
    const std::wstring& /*fullPath*/,
    LoadedMesh3D& mesh)
{
    using namespace DetailNastranNas;
    if (!data || size < 16) return false;
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    sv = ReaderText::StripUtf8Bom(sv);
    if (sv.find('\0') != std::string_view::npos) return false;

    mesh.vertices.clear();
    mesh.tris.clear();

    // Pass 1: Assemble logical bulk data cards (resolving small/large/free-field & continuation lines)
    std::vector<LogicalCard> cards;
    std::unordered_map<std::string, size_t> contTagToCard;

    size_t pos = 0;
    std::string_view rawLine;
    std::vector<std::string> lineFields;
    while (ReaderText::NextLineSv(sv, pos, rawLine)) {
        bool isCont = false;
        std::string head, tailTag;
        ParsePhysicalLineToFields(rawLine, isCont, head, lineFields, tailTag);
        if (head.empty() && lineFields.empty()) continue;

        if (!isCont && head == "ENDDATA") break;

        if (isCont) {
            size_t targetIdx = cards.size();
            if (!head.empty()) {
                auto it = contTagToCard.find(head);
                if (it != contTagToCard.end()) targetIdx = it->second;
                else if (head.size() > 1) {
                    // Match ignoring '*' vs '+' prefix
                    auto it2 = contTagToCard.find("+" + head.substr(1));
                    if (it2 != contTagToCard.end()) targetIdx = it2->second;
                    else {
                        auto it3 = contTagToCard.find("*" + head.substr(1));
                        if (it3 != contTagToCard.end()) targetIdx = it3->second;
                    }
                }
            }
            if (targetIdx == cards.size() && !cards.empty()) {
                targetIdx = cards.size() - 1;
            }
            if (targetIdx < cards.size()) {
                cards[targetIdx].fields.insert(cards[targetIdx].fields.end(), lineFields.begin(), lineFields.end());
                if (!tailTag.empty()) contTagToCard[tailTag] = targetIdx;
            }
        } else {
            LogicalCard c;
            c.headUpper = std::move(head);
            c.fields = std::move(lineFields);
            size_t idx = cards.size();
            cards.push_back(std::move(c));
            if (!tailTag.empty()) contTagToCard[tailTag] = idx;
        }
    }

    if (cards.empty()) return false;

    // Pass 2: Extract CORD2R coordinate systems, GRID points, and structural/solid elements
    std::unordered_map<int64_t, Cord2R> coords;
    struct RawGrid {
        int64_t id = 0;
        int64_t cp = 0;
        double x = 0, y = 0, z = 0;
    };
    std::vector<RawGrid> rawGrids;

    for (const auto& c : cards) {
        if (c.headUpper == "CORD2R" && c.fields.size() >= 11) {
            Cord2R cs;
            if (!ParseNastranInt(c.fields[0], cs.cid) || cs.cid <= 0) continue;
            ParseNastranInt(c.fields[1], cs.rid);
            if (ParseNastranDouble(c.fields[2], cs.a[0]) && ParseNastranDouble(c.fields[3], cs.a[1]) && ParseNastranDouble(c.fields[4], cs.a[2]) &&
                ParseNastranDouble(c.fields[5], cs.b[0]) && ParseNastranDouble(c.fields[6], cs.b[1]) && ParseNastranDouble(c.fields[7], cs.b[2]) &&
                ParseNastranDouble(c.fields[8], cs.c[0]) && ParseNastranDouble(c.fields[9], cs.c[1]) && ParseNastranDouble(c.fields[10], cs.c[2]))
            {
                coords[cs.cid] = cs;
            }
        } else if (c.headUpper == "GRID" && c.fields.size() >= 5) {
            RawGrid g;
            if (!ParseNastranInt(c.fields[0], g.id) || g.id <= 0) continue;
            ParseNastranInt(c.fields[1], g.cp);
            if (ParseNastranDouble(c.fields[2], g.x) &&
                ParseNastranDouble(c.fields[3], g.y) &&
                ParseNastranDouble(c.fields[4], g.z))
            {
                rawGrids.push_back(g);
            }
        }
    }

    if (rawGrids.empty()) return false;

    // Resolve CORD2R hierarchy into basic coordinate system
    auto resolveCord = [&](auto& self, int64_t cid, int depth) -> const Cord2R* {
        if (cid <= 0 || depth > 16) return nullptr;
        auto it = coords.find(cid);
        if (it == coords.end()) return nullptr;
        Cord2R& cs = it->second;
        if (cs.resolved) return &cs;

        double A[3] = { cs.a[0], cs.a[1], cs.a[2] };
        double B[3] = { cs.b[0], cs.b[1], cs.b[2] };
        double C[3] = { cs.c[0], cs.c[1], cs.c[2] };
        if (cs.rid > 0) {
            if (const Cord2R* par = self(self, cs.rid, depth + 1)) {
                par->TransformLocalToBasic(cs.a[0], cs.a[1], cs.a[2], A[0], A[1], A[2]);
                par->TransformLocalToBasic(cs.b[0], cs.b[1], cs.b[2], B[0], B[1], B[2]);
                par->TransformLocalToBasic(cs.c[0], cs.c[1], cs.c[2], C[0], C[1], C[2]);
            }
        }

        double ez[3] = { B[0] - A[0], B[1] - A[1], B[2] - A[2] };
        double lz = std::sqrt(ez[0] * ez[0] + ez[1] * ez[1] + ez[2] * ez[2]);
        if (lz > 1e-12) { ez[0] /= lz; ez[1] /= lz; ez[2] /= lz; }
        else { ez[0] = 0; ez[1] = 0; ez[2] = 1; }

        double ac[3] = { C[0] - A[0], C[1] - A[1], C[2] - A[2] };
        // ey = normalize(ez x ac)
        double ey[3] = {
            ez[1] * ac[2] - ez[2] * ac[1],
            ez[2] * ac[0] - ez[0] * ac[2],
            ez[0] * ac[1] - ez[1] * ac[0]
        };
        double ly = std::sqrt(ey[0] * ey[0] + ey[1] * ey[1] + ey[2] * ey[2]);
        if (ly > 1e-12) { ey[0] /= ly; ey[1] /= ly; ey[2] /= ly; }
        else { ey[0] = 0; ey[1] = 1; ey[2] = 0; }

        // ex = ey x ez
        double ex[3] = {
            ey[1] * ez[2] - ey[2] * ez[1],
            ey[2] * ez[0] - ey[0] * ez[2],
            ey[0] * ez[1] - ey[1] * ez[0]
        };
        for (int k = 0; k < 3; ++k) {
            cs.origin[k] = A[k];
            cs.ex[k] = ex[k];
            cs.ey[k] = ey[k];
            cs.ez[k] = ez[k];
        }
        cs.resolved = true;
        return &cs;
    };

    std::unordered_map<int64_t, uint32_t> gridMap;
    std::vector<Vec3f> nodes;
    nodes.reserve(rawGrids.size());
    for (const auto& g : rawGrids) {
        double bx = g.x, by = g.y, bz = g.z;
        if (g.cp > 0) {
            if (const Cord2R* cs = resolveCord(resolveCord, g.cp, 0)) {
                cs->TransformLocalToBasic(g.x, g.y, g.z, bx, by, bz);
            }
        }
        auto [it, inserted] = gridMap.try_emplace(g.id, static_cast<uint32_t>(nodes.size()));
        Vec3f pt = { static_cast<float>(bx), static_cast<float>(by), static_cast<float>(bz) };
        if (inserted) nodes.push_back(pt);
        else nodes[it->second] = pt;
    }

    auto readElemNodes = [&](const LogicalCard& c, size_t startField, size_t count, uint32_t* outIdx) -> bool {
        if (c.fields.size() < startField + count) return false;
        size_t found = 0;
        for (size_t k = startField; k < c.fields.size() && found < count; ++k) {
            if (c.fields[k].empty()) continue;
            int64_t gid = 0;
            if (!ParseNastranInt(c.fields[k], gid) || gid <= 0) return false;
            auto it = gridMap.find(gid);
            if (it == gridMap.end()) return false;
            outIdx[found++] = it->second;
        }
        return found == count;
    };

    FemSolidSurface::FemSurfaceBuilder builder;
    builder.ReserveElements(cards.size());

    for (const auto& c : cards) {
        const std::string& h = c.headUpper;
        uint32_t n[8] = {};
        if (h == "CTRIA3" || h == "CTRIA6" || h == "CTRIAR" || h == "CTRIAX6") {
            if (readElemNodes(c, 2, 3, n)) builder.AddTriShell(n[0], n[1], n[2]);
        } else if (h == "CQUAD4" || h == "CQUAD8" || h == "CQUADR" || h == "CQUAD") {
            if (readElemNodes(c, 2, 4, n)) builder.AddQuadShell(n[0], n[1], n[2], n[3]);
        } else if (h == "CTETRA") {
            if (readElemNodes(c, 2, 4, n)) builder.AddTet4(n);
        } else if (h == "CPYRAM") {
            if (readElemNodes(c, 2, 5, n)) builder.AddPyramid5(n);
        } else if (h == "CPENTA") {
            if (readElemNodes(c, 2, 6, n)) builder.AddWedge6(n);
        } else if (h == "CHEXA") {
            if (readElemNodes(c, 2, 8, n)) builder.AddHex8(n);
        } else if (h == "CBAR" || h == "CBEAM" || h == "CROD" || h == "CTUBE") {
            if (readElemNodes(c, 2, 2, n)) builder.AddBeamEdge(n[0], n[1]);
        }
    }

    if (!builder.EmitBoundaryToMesh(nodes, mesh)) return false;
    mesh.formatName = L"MSC / NX Nastran Bulk Data (.nas)";
    return true;
}

} // namespace Formats
