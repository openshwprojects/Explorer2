#pragma once
// StepCad.h — ISO 10303-21 STEP (.step / .stp / .p21) 3D CAD B-Rep, Curve & AP242 Tessellated Mesh Parser

namespace Formats {

namespace DetailStep {

struct StepEntity {
    std::string type;
    std::string_view params;
    // Additional sub-entities for complex instances: #id = (BOUNDED_CURVE() B_SPLINE_CURVE(...) ...);
    std::vector<std::pair<std::string, std::string_view>> subEntities;
};

inline std::string_view TrimSv(std::string_view s) {
    size_t b = 0;
    while (b < s.size() && static_cast<unsigned char>(s[b]) <= 32) ++b;
    size_t e = s.size();
    while (e > b && static_cast<unsigned char>(s[e - 1]) <= 32) --e;
    return s.substr(b, e - b);
}

inline std::string UpperAscii(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c >= 'a' && c <= 'z') out.push_back(static_cast<char>(c - 'a' + 'A'));
        else if (static_cast<unsigned char>(c) > 32) out.push_back(c);
    }
    return out;
}

// Splits top-level comma-separated arguments inside outer parentheses (or already-unwrapped span)
inline void SplitStepArgs(std::string_view s, std::vector<std::string_view>& out) {
    out.clear();
    s = TrimSv(s);
    if (s.size() >= 2 && s.front() == '(' && s.back() == ')') {
        // Verify outer parens actually match each other
        int depth = 0;
        bool inStr = false;
        bool matchesEnd = true;
        for (size_t i = 0; i < s.size(); ++i) {
            char c = s[i];
            if (inStr) {
                if (c == '\'') {
                    if (i + 1 < s.size() && s[i + 1] == '\'') ++i;
                    else inStr = false;
                }
                continue;
            }
            if (c == '\'') inStr = true;
            else if (c == '(') ++depth;
            else if (c == ')') {
                --depth;
                if (depth == 0 && i + 1 < s.size()) { matchesEnd = false; break; }
            }
        }
        if (matchesEnd && depth == 0) {
            s = s.substr(1, s.size() - 2);
        }
    }

    int depth = 0;
    bool inStr = false;
    size_t start = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (inStr) {
            if (c == '\'') {
                if (i + 1 < s.size() && s[i + 1] == '\'') ++i;
                else inStr = false;
            }
            continue;
        }
        if (c == '\'') inStr = true;
        else if (c == '(') ++depth;
        else if (c == ')') { if (depth > 0) --depth; }
        else if (c == ',' && depth == 0) {
            out.push_back(TrimSv(s.substr(start, i - start)));
            start = i + 1;
        }
    }
    if (start <= s.size()) {
        std::string_view tail = TrimSv(s.substr(start));
        if (!tail.empty() || !out.empty()) out.push_back(tail);
    }
}

inline int ParseRefId(std::string_view s) {
    s = TrimSv(s);
    size_t hp = s.find('#');
    if (hp == std::string_view::npos) return -1;
    ++hp;
    int val = 0;
    bool any = false;
    while (hp < s.size() && s[hp] >= '0' && s[hp] <= '9') {
        val = val * 10 + (s[hp] - '0');
        any = true;
        ++hp;
    }
    return any ? val : -1;
}

inline void ParseRefList(std::string_view s, std::vector<int>& out) {
    out.clear();
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '\'') {
            ++i;
            while (i < s.size()) {
                if (s[i] == '\'') {
                    if (i + 1 < s.size() && s[i + 1] == '\'') { i += 2; continue; }
                    ++i; break;
                }
                ++i;
            }
            continue;
        }
        if (s[i] == '#') {
            ++i;
            int val = 0;
            bool any = false;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
                val = val * 10 + (s[i] - '0');
                any = true;
                ++i;
            }
            if (any) out.push_back(val);
            continue;
        }
        ++i;
    }
}

inline double ParseStepDouble(std::string_view s, double defVal = 0.0) {
    s = TrimSv(s);
    if (s.empty() || s == "$" || s == "*") return defVal;
    // Handle typed parameter wrappers like LENGTH_MEASURE(12.5)
    size_t lp = s.find('(');
    if (lp != std::string_view::npos && s.back() == ')') {
        s = TrimSv(s.substr(lp + 1, s.size() - lp - 2));
    }
    char buf[64] = {};
    size_t n = std::min<size_t>(s.size(), 63);
    std::memcpy(buf, s.data(), n);
    char* ep = nullptr;
    double v = std::strtod(buf, &ep);
    return (ep != buf && std::isfinite(v)) ? v : defVal;
}

inline void ParseFloatTuple(std::string_view s, std::vector<double>& out) {
    out.clear();
    s = TrimSv(s);
    if (s.empty() || s == "$" || s == "*") return;
    std::vector<std::string_view> parts;
    SplitStepArgs(s, parts);
    out.reserve(parts.size());
    for (auto p : parts) {
        p = TrimSv(p);
        if (p.empty() || p == "$" || p == "*") continue;
        out.push_back(ParseStepDouble(p, 0.0));
    }
}

inline bool ParseStepBool(std::string_view s, bool defVal = true) {
    s = TrimSv(s);
    if (s.find(".F.") != std::string_view::npos || s.find(".f.") != std::string_view::npos) return false;
    if (s.find(".T.") != std::string_view::npos || s.find(".t.") != std::string_view::npos) return true;
    return defVal;
}

inline Vec3f StepAdd(Vec3f a, Vec3f b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline Vec3f StepSub(Vec3f a, Vec3f b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline Vec3f StepScale(Vec3f a, float s) { return { a.x * s, a.y * s, a.z * s }; }
inline float StepDot(Vec3f a, Vec3f b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3f StepCross(Vec3f a, Vec3f b) {
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
inline float StepLen(Vec3f a) { return std::sqrt(StepDot(a, a)); }
inline Vec3f StepNorm(Vec3f a, Vec3f fallback = { 0.0f, 0.0f, 1.0f }) {
    float l = StepLen(a);
    return (l > 1e-7f) ? Vec3f{ a.x / l, a.y / l, a.z / l } : fallback;
}

struct StepAxis2 {
    Vec3f origin = { 0.0f, 0.0f, 0.0f };
    Vec3f xAxis  = { 1.0f, 0.0f, 0.0f };
    Vec3f yAxis  = { 0.0f, 1.0f, 0.0f };
    Vec3f zAxis  = { 0.0f, 0.0f, 1.0f };
};

// Bridges inner hole loops into an outer 2D polygon via visible keyhole edges, then ear-clips
inline void EarClip2DWithHoles(
    const std::vector<std::pair<float, float>>& outer2D,
    const std::vector<uint32_t>& outer3DIdx,
    const std::vector<std::vector<std::pair<float, float>>>& holes2D,
    const std::vector<std::vector<uint32_t>>& holes3DIdx,
    bool reverseWinding,
    std::vector<MeshTriangle>& outTris)
{
    if (outer2D.size() < 3 || outer2D.size() != outer3DIdx.size()) return;

    auto signedArea2D = [](const std::vector<std::pair<float, float>>& poly) -> float {
        double a = 0.0;
        size_t n = poly.size();
        for (size_t i = 0; i < n; ++i) {
            size_t j = (i + 1) % n;
            a += static_cast<double>(poly[i].first) * poly[j].second -
                 static_cast<double>(poly[j].first) * poly[i].second;
        }
        return static_cast<float>(0.5 * a);
    };

    std::vector<std::pair<float, float>> pts = outer2D;
    std::vector<uint32_t> idx = outer3DIdx;
    bool flipped2D = false;
    if (signedArea2D(pts) < 0.0f) {
        std::reverse(pts.begin(), pts.end());
        std::reverse(idx.begin(), idx.end());
        flipped2D = true;
    }

    // Splice holes sorted by rightmost X coordinate
    if (!holes2D.empty() && holes2D.size() == holes3DIdx.size()) {
        struct HoleInfo {
            size_t hIdx;
            size_t rightMostVert;
            float maxX;
        };
        std::vector<HoleInfo> order;
        for (size_t h = 0; h < holes2D.size(); ++h) {
            if (holes2D[h].size() < 3 || holes2D[h].size() != holes3DIdx[h].size()) continue;
            size_t rm = 0;
            for (size_t v = 1; v < holes2D[h].size(); ++v) {
                if (holes2D[h][v].first > holes2D[h][rm].first) rm = v;
            }
            order.push_back({ h, rm, holes2D[h][rm].first });
        }
        std::sort(order.begin(), order.end(), [](const HoleInfo& a, const HoleInfo& b) {
            return a.maxX > b.maxX;
        });

        for (const auto& hi : order) {
            std::vector<std::pair<float, float>> hp = holes2D[hi.hIdx];
            std::vector<uint32_t> hIdx = holes3DIdx[hi.hIdx];
            // Hole must be CW (negative signed area) when outer is CCW
            if (signedArea2D(hp) > 0.0f) {
                std::reverse(hp.begin(), hp.end());
                std::reverse(hIdx.begin(), hIdx.end());
            }
            size_t rm = 0;
            for (size_t v = 1; v < hp.size(); ++v) {
                if (hp[v].first > hp[rm].first) rm = v;
            }
            // Find nearest outer vertex to the right (or closest distance fallback)
            size_t bestOuter = 0;
            float bestDist = 1e30f;
            for (size_t o = 0; o < pts.size(); ++o) {
                float dx = pts[o].first - hp[rm].first;
                float dy = pts[o].second - hp[rm].second;
                float d = dx * dx + dy * dy + ((dx >= -1e-4f) ? 0.0f : 1e6f);
                if (d < bestDist) { bestDist = d; bestOuter = o; }
            }

            std::vector<std::pair<float, float>> mergedP;
            std::vector<uint32_t> mergedI;
            mergedP.reserve(pts.size() + hp.size() + 2);
            mergedI.reserve(idx.size() + hIdx.size() + 2);
            for (size_t o = 0; o <= bestOuter; ++o) {
                mergedP.push_back(pts[o]);
                mergedI.push_back(idx[o]);
            }
            for (size_t k = 0; k <= hp.size(); ++k) {
                size_t hv = (rm + k) % hp.size();
                mergedP.push_back(hp[hv]);
                mergedI.push_back(hIdx[hv]);
            }
            for (size_t o = bestOuter; o < pts.size(); ++o) {
                mergedP.push_back(pts[o]);
                mergedI.push_back(idx[o]);
            }
            pts = std::move(mergedP);
            idx = std::move(mergedI);
        }
    }

    auto emitTri = [&](uint32_t a, uint32_t b, uint32_t c) {
        if (a == b || b == c || a == c) return;
        bool rev = reverseWinding ^ flipped2D;
        if (rev) outTris.push_back({ a, c, b, {} });
        else     outTris.push_back({ a, b, c, {} });
    };

    if (pts.size() > 512) {
        for (size_t i = 1; i + 1 < idx.size(); ++i) emitTri(idx[0], idx[i], idx[i + 1]);
        return;
    }

    std::vector<size_t> ring(pts.size());
    std::iota(ring.begin(), ring.end(), 0);

    auto cross2D = [&](size_t ia, size_t ib, size_t ic) -> float {
        float ax = pts[ib].first - pts[ia].first, ay = pts[ib].second - pts[ia].second;
        float bx = pts[ic].first - pts[ia].first, by = pts[ic].second - pts[ia].second;
        return ax * by - ay * bx;
    };
    auto ptInTri = [&](size_t ip, size_t ia, size_t ib, size_t ic) -> bool {
        if (idx[ip] == idx[ia] || idx[ip] == idx[ib] || idx[ip] == idx[ic]) return false;
        float c1 = cross2D(ia, ib, ip);
        float c2 = cross2D(ib, ic, ip);
        float c3 = cross2D(ic, ia, ip);
        return (c1 >= -1e-6f && c2 >= -1e-6f && c3 >= -1e-6f);
    };

    int guard = static_cast<int>(ring.size() * ring.size()) + 16;
    while (ring.size() > 3 && guard-- > 0) {
        bool clipped = false;
        size_t m = ring.size();
        for (size_t i = 0; i < m; ++i) {
            size_t prev = ring[(i + m - 1) % m];
            size_t curr = ring[i];
            size_t next = ring[(i + 1) % m];
            if (cross2D(prev, curr, next) <= 1e-7f) continue;
            bool anyInside = false;
            for (size_t j = 0; j < m; ++j) {
                size_t test = ring[j];
                if (test == prev || test == curr || test == next) continue;
                if (ptInTri(test, prev, curr, next)) { anyInside = true; break; }
            }
            if (!anyInside) {
                emitTri(idx[prev], idx[curr], idx[next]);
                ring.erase(ring.begin() + static_cast<ptrdiff_t>(i));
                clipped = true;
                break;
            }
        }
        if (!clipped) {
            // Sliver / self-touching keyhole fallback: relax convexity tolerance
            size_t bestI = 0;
            float bestC = -1e30f;
            for (size_t i = 0; i < m; ++i) {
                float c = cross2D(ring[(i + m - 1) % m], ring[i], ring[(i + 1) % m]);
                if (c > bestC) { bestC = c; bestI = i; }
            }
            emitTri(idx[ring[(bestI + m - 1) % m]], idx[ring[bestI]], idx[ring[(bestI + 1) % m]]);
            ring.erase(ring.begin() + static_cast<ptrdiff_t>(bestI));
        }
    }
    if (ring.size() == 3) {
        emitTri(idx[ring[0]], idx[ring[1]], idx[ring[2]]);
    }
}

inline void Triangulate3DRings(
    const std::vector<Vec3f>& outer3D,
    const std::vector<std::vector<Vec3f>>& holes3D,
    bool reverseWinding,
    LoadedMesh3D& mesh)
{
    if (outer3D.size() < 3) return;

    // Newell normal of outer loop
    Vec3f nrm = { 0.0f, 0.0f, 0.0f };
    for (size_t i = 0; i < outer3D.size(); ++i) {
        const Vec3f& cur = outer3D[i];
        const Vec3f& nxt = outer3D[(i + 1) % outer3D.size()];
        nrm.x += (cur.y - nxt.y) * (cur.z + nxt.z);
        nrm.y += (cur.z - nxt.z) * (cur.x + nxt.x);
        nrm.z += (cur.x - nxt.x) * (cur.y + nxt.y);
    }
    nrm = StepNorm(nrm, { 0.0f, 0.0f, 1.0f });
    Vec3f ref = (std::fabs(nrm.z) < 0.9f) ? Vec3f{ 0.0f, 0.0f, 1.0f } : Vec3f{ 1.0f, 0.0f, 0.0f };
    Vec3f uAx = StepNorm(StepCross(ref, nrm), { 1.0f, 0.0f, 0.0f });
    Vec3f vAx = StepCross(nrm, uAx);

    auto emitVert = [&](Vec3f p) -> uint32_t {
        uint32_t id = static_cast<uint32_t>(mesh.vertices.size());
        // STEP Z-up -> Explorer2 Y-up
        mesh.vertices.push_back({ p.x, p.z, -p.y });
        return id;
    };

    std::vector<std::pair<float, float>> outer2D;
    std::vector<uint32_t> outerIdx;
    outer2D.reserve(outer3D.size());
    outerIdx.reserve(outer3D.size());
    Vec3f org = outer3D[0];
    for (const auto& p : outer3D) {
        Vec3f d = StepSub(p, org);
        outer2D.push_back({ StepDot(d, uAx), StepDot(d, vAx) });
        outerIdx.push_back(emitVert(p));
    }

    std::vector<std::vector<std::pair<float, float>>> holes2D;
    std::vector<std::vector<uint32_t>> holesIdx;
    for (const auto& h : holes3D) {
        if (h.size() < 3) continue;
        std::vector<std::pair<float, float>> h2;
        std::vector<uint32_t> hi;
        h2.reserve(h.size());
        hi.reserve(h.size());
        for (const auto& p : h) {
            Vec3f d = StepSub(p, org);
            h2.push_back({ StepDot(d, uAx), StepDot(d, vAx) });
            hi.push_back(emitVert(p));
        }
        holes2D.push_back(std::move(h2));
        holesIdx.push_back(std::move(hi));
    }

    EarClip2DWithHoles(outer2D, outerIdx, holes2D, holesIdx, reverseWinding, mesh.tris);
}

inline void AddWireStrut(LoadedMesh3D& mesh, Vec3f aStep, Vec3f bStep, float radius) {
    Vec3f p0 = { aStep.x, aStep.z, -aStep.y };
    Vec3f p1 = { bStep.x, bStep.z, -bStep.y };
    Vec3f d = StepSub(p1, p0);
    float len = StepLen(d);
    if (len < 1e-6f) return;
    Vec3f ref = (std::fabs(d.y) < 0.9f * len) ? Vec3f{ 0.0f, 1.0f, 0.0f } : Vec3f{ 1.0f, 0.0f, 0.0f };
    Vec3f n1 = StepScale(StepNorm(StepCross(d, ref)), radius);
    Vec3f n2 = StepScale(StepNorm(StepCross(d, n1)), radius);
    uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(StepAdd(p0, n1));
    mesh.vertices.push_back(StepAdd(p0, n2));
    mesh.vertices.push_back(StepSub(p0, n1));
    mesh.vertices.push_back(StepSub(p0, n2));
    mesh.vertices.push_back(StepAdd(p1, n1));
    mesh.vertices.push_back(StepAdd(p1, n2));
    mesh.vertices.push_back(StepSub(p1, n1));
    mesh.vertices.push_back(StepSub(p1, n2));
    for (uint32_t i = 0; i < 4; ++i) {
        uint32_t ni = (i + 1) & 3u;
        mesh.tris.push_back({ base + i, base + ni, base + 4 + ni, {} });
        mesh.tris.push_back({ base + i, base + 4 + ni, base + 4 + i, {} });
    }
}

} // namespace DetailStep

inline bool ParseStepCadMesh(std::string_view sv, LoadedMesh3D& mesh) {
    using namespace DetailStep;
    if (sv.find("ISO-10303-21") == std::string_view::npos &&
        sv.find("CARTESIAN_POINT") == std::string_view::npos &&
        sv.find("COORDINATES_LIST") == std::string_view::npos)
    {
        return false;
    }
    mesh.formatName = L"ISO 10303-21 STEP CAD (.step)";

    // 1. Scan DATA; ... ENDSEC; statements into #id -> StepEntity map (multi-line safe, comment & string safe)
    std::unordered_map<int, StepEntity> entities;
    entities.reserve(1024);

    size_t pos = 0;
    size_t dataSec = sv.find("DATA;");
    if (dataSec != std::string_view::npos) pos = dataSec + 5;

    while (pos < sv.size() && entities.size() < 500000) {
        // Skip whitespace and /* ... */ comments
        while (pos < sv.size()) {
            unsigned char ch = static_cast<unsigned char>(sv[pos]);
            if (ch <= 32) { ++pos; continue; }
            if (ch == '/' && pos + 1 < sv.size() && sv[pos + 1] == '*') {
                size_t ce = sv.find("*/", pos + 2);
                pos = (ce == std::string_view::npos) ? sv.size() : (ce + 2);
                continue;
            }
            break;
        }
        if (pos >= sv.size()) break;

        if (sv.compare(pos, 7, "ENDSEC;") == 0) {
            size_t nextData = sv.find("DATA;", pos + 7);
            if (nextData == std::string_view::npos) break;
            pos = nextData + 5;
            continue;
        }

        // Find terminating ';' outside strings and comments
        size_t stmtStart = pos;
        bool inStr = false;
        while (pos < sv.size()) {
            char c = sv[pos];
            if (inStr) {
                if (c == '\'') {
                    if (pos + 1 < sv.size() && sv[pos + 1] == '\'') pos += 2;
                    else { inStr = false; ++pos; }
                } else {
                    ++pos;
                }
                continue;
            }
            if (c == '\'') { inStr = true; ++pos; continue; }
            if (c == '/' && pos + 1 < sv.size() && sv[pos + 1] == '*') {
                size_t ce = sv.find("*/", pos + 2);
                pos = (ce == std::string_view::npos) ? sv.size() : (ce + 2);
                continue;
            }
            if (c == ';') break;
            ++pos;
        }
        std::string_view stmt = TrimSv(sv.substr(stmtStart, pos - stmtStart));
        if (pos < sv.size() && sv[pos] == ';') ++pos;

        if (stmt.size() < 5 || stmt[0] != '#') continue;
        size_t eq = stmt.find('=');
        if (eq == std::string_view::npos) continue;
        int id = ParseRefId(stmt.substr(0, eq));
        if (id <= 0) continue;

        std::string_view rhs = TrimSv(stmt.substr(eq + 1));
        if (rhs.empty()) continue;

        StepEntity ent;
        if (rhs[0] == '(' && rhs.back() == ')') {
            // Complex entity instance: (BOUNDED_CURVE() B_SPLINE_CURVE(3,(#1,#2,#3,#4),.UNSPECIFIED.,.F.,.F.) ...)
            std::string_view inner = TrimSv(rhs.substr(1, rhs.size() - 2));
            size_t ip = 0;
            while (ip < inner.size()) {
                while (ip < inner.size() && static_cast<unsigned char>(inner[ip]) <= 32) ++ip;
                size_t kwStart = ip;
                while (ip < inner.size() && inner[ip] != '(' && static_cast<unsigned char>(inner[ip]) > 32) ++ip;
                std::string subName = UpperAscii(inner.substr(kwStart, ip - kwStart));
                while (ip < inner.size() && inner[ip] != '(') ++ip;
                if (ip >= inner.size()) break;
                size_t pStart = ip;
                int depth = 0;
                bool q = false;
                while (ip < inner.size()) {
                    char ch = inner[ip];
                    if (q) {
                        if (ch == '\'') {
                            if (ip + 1 < inner.size() && inner[ip + 1] == '\'') ++ip;
                            else q = false;
                        }
                    } else {
                        if (ch == '\'') q = true;
                        else if (ch == '(') ++depth;
                        else if (ch == ')') {
                            --depth;
                            if (depth == 0) { ++ip; break; }
                        }
                    }
                    ++ip;
                }
                std::string_view pSpan = inner.substr(pStart, ip - pStart);
                if (!subName.empty()) {
                    ent.subEntities.emplace_back(subName, pSpan);
                    if (ent.type.empty() ||
                        subName.find("B_SPLINE_CURVE_WITH_KNOTS") != std::string::npos ||
                        subName == "B_SPLINE_CURVE" ||
                        subName.find("SURFACE") != std::string::npos)
                    {
                        ent.type = subName;
                        ent.params = pSpan;
                    }
                }
            }
        } else {
            size_t lp = rhs.find('(');
            if (lp == std::string_view::npos) continue;
            ent.type = UpperAscii(rhs.substr(0, lp));
            ent.params = TrimSv(rhs.substr(lp));
        }
        if (!ent.type.empty()) {
            entities[id] = std::move(ent);
        }
    }

    if (entities.empty()) return false;

    // 2. Cached geometric primitives (CARTESIAN_POINT, VERTEX_POINT, DIRECTION, AXIS2_PLACEMENT_3D)
    std::unordered_map<int, Vec3f> pointCache;
    std::unordered_map<int, Vec3f> dirCache;
    std::unordered_map<int, StepAxis2> axisCache;

    std::function<bool(int, Vec3f&, int)> resolvePoint = [&](int id, Vec3f& out, int depth) -> bool {
        if (id <= 0 || depth > 8) return false;
        auto cit = pointCache.find(id);
        if (cit != pointCache.end()) { out = cit->second; return true; }
        auto it = entities.find(id);
        if (it == entities.end()) return false;
        const auto& e = it->second;
        std::vector<std::string_view> args;
        SplitStepArgs(e.params, args);
        if (e.type == "CARTESIAN_POINT" && args.size() >= 2) {
            std::vector<double> xyz;
            ParseFloatTuple(args[1], xyz);
            if (xyz.size() >= 2) {
                out = {
                    static_cast<float>(xyz[0]),
                    static_cast<float>(xyz[1]),
                    static_cast<float>(xyz.size() >= 3 ? xyz[2] : 0.0)
                };
                pointCache[id] = out;
                return true;
            }
        } else if (e.type == "VERTEX_POINT" && args.size() >= 2) {
            if (resolvePoint(ParseRefId(args[1]), out, depth + 1)) {
                pointCache[id] = out;
                return true;
            }
        }
        return false;
    };

    std::function<bool(int, Vec3f&, int)> resolveDir = [&](int id, Vec3f& out, int depth) -> bool {
        if (id <= 0 || depth > 8) return false;
        auto cit = dirCache.find(id);
        if (cit != dirCache.end()) { out = cit->second; return true; }
        auto it = entities.find(id);
        if (it == entities.end()) return false;
        const auto& e = it->second;
        std::vector<std::string_view> args;
        SplitStepArgs(e.params, args);
        if (e.type == "DIRECTION" && args.size() >= 2) {
            std::vector<double> xyz;
            ParseFloatTuple(args[1], xyz);
            if (xyz.size() >= 2) {
                out = StepNorm({
                    static_cast<float>(xyz[0]),
                    static_cast<float>(xyz[1]),
                    static_cast<float>(xyz.size() >= 3 ? xyz[2] : 0.0)
                });
                dirCache[id] = out;
                return true;
            }
        } else if (e.type == "VECTOR" && args.size() >= 3) {
            Vec3f d;
            if (resolveDir(ParseRefId(args[1]), d, depth + 1)) {
                float mag = static_cast<float>(ParseStepDouble(args[2], 1.0));
                out = StepScale(d, mag);
                dirCache[id] = out;
                return true;
            }
        }
        return false;
    };

    auto resolveAxis2 = [&](int id) -> StepAxis2 {
        auto cit = axisCache.find(id);
        if (cit != axisCache.end()) return cit->second;
        StepAxis2 ax;
        auto it = entities.find(id);
        if (it != entities.end() && (it->second.type == "AXIS2_PLACEMENT_3D" || it->second.type == "AXIS2_PLACEMENT_2D")) {
            std::vector<std::string_view> args;
            SplitStepArgs(it->second.params, args);
            if (args.size() >= 2) resolvePoint(ParseRefId(args[1]), ax.origin, 0);
            if (it->second.type == "AXIS2_PLACEMENT_3D") {
                bool hasZ = (args.size() >= 3 && resolveDir(ParseRefId(args[2]), ax.zAxis, 0));
                if (hasZ) ax.zAxis = StepNorm(ax.zAxis, { 0.0f, 0.0f, 1.0f });
                Vec3f refDir = (std::fabs(ax.zAxis.z) < 0.9f) ? Vec3f{ 0.0f, 0.0f, 1.0f } : Vec3f{ 1.0f, 0.0f, 0.0f };
                if (args.size() >= 4) {
                    Vec3f rd;
                    if (resolveDir(ParseRefId(args[3]), rd, 0)) refDir = rd;
                }
                // Gram-Schmidt orthogonalization: xAxis = normalize(refDir - dot(refDir, zAxis) * zAxis)
                Vec3f proj = StepSub(refDir, StepScale(ax.zAxis, StepDot(refDir, ax.zAxis)));
                if (StepLen(proj) < 1e-6f) {
                    Vec3f alt = (std::fabs(ax.zAxis.x) < 0.9f) ? Vec3f{ 1.0f, 0.0f, 0.0f } : Vec3f{ 0.0f, 1.0f, 0.0f };
                    proj = StepSub(alt, StepScale(ax.zAxis, StepDot(alt, ax.zAxis)));
                }
                ax.xAxis = StepNorm(proj, { 1.0f, 0.0f, 0.0f });
                ax.yAxis = StepNorm(StepCross(ax.zAxis, ax.xAxis), { 0.0f, 1.0f, 0.0f });
            } else if (args.size() >= 3) {
                Vec3f rd;
                if (resolveDir(ParseRefId(args[2]), rd, 0)) {
                    ax.xAxis = StepNorm({ rd.x, rd.y, 0.0f }, { 1.0f, 0.0f, 0.0f });
                    ax.yAxis = { -ax.xAxis.y, ax.xAxis.x, 0.0f };
                }
            }
        }
        axisCache[id] = ax;
        return ax;
    };

    // 3. STEP AP242 Tessellated Solids / Shells / Faces (COORDINATES_LIST + TRIANGULATED_FACE / COMPLEX_TRIANGULATED_FACE)
    std::unordered_map<int, std::vector<Vec3f>> coordLists;
    for (const auto& [id, ent] : entities) {
        if (ent.type == "COORDINATES_LIST") {
            std::vector<std::string_view> args;
            SplitStepArgs(ent.params, args);
            if (args.size() >= 3) {
                std::vector<std::string_view> tuples;
                SplitStepArgs(args[2], tuples);
                std::vector<Vec3f> pts;
                pts.reserve(tuples.size());
                for (auto t : tuples) {
                    std::vector<double> xyz;
                    ParseFloatTuple(t, xyz);
                    if (xyz.size() >= 2) {
                        pts.push_back({
                            static_cast<float>(xyz[0]),
                            static_cast<float>(xyz[1]),
                            static_cast<float>(xyz.size() >= 3 ? xyz[2] : 0.0)
                        });
                    }
                }
                if (!pts.empty()) coordLists[id] = std::move(pts);
            }
        }
    }

    for (const auto& [id, ent] : entities) {
        if (ent.type == "TRIANGULATED_FACE" || ent.type == "COMPLEX_TRIANGULATED_FACE") {
            std::vector<std::string_view> args;
            SplitStepArgs(ent.params, args);
            if (args.size() < 6) continue;
            int clId = ParseRefId(args[1]);
            auto clIt = coordLists.find(clId);
            if (clIt == coordLists.end() || clIt->second.empty()) continue;
            const auto& cpts = clIt->second;

            uint32_t vBase = static_cast<uint32_t>(mesh.vertices.size());
            for (const auto& p : cpts) {
                mesh.vertices.push_back({ p.x, p.z, -p.y });
            }

            std::vector<double> pnIndexD;
            ParseFloatTuple(args[4], pnIndexD);
            auto mapPn = [&](int oneBasedIdx) -> int {
                if (oneBasedIdx < 1) return -1;
                int raw = oneBasedIdx - 1;
                if (!pnIndexD.empty()) {
                    if (raw < 0 || static_cast<size_t>(raw) >= pnIndexD.size()) return -1;
                    raw = static_cast<int>(pnIndexD[raw]) - 1;
                }
                return (raw >= 0 && static_cast<size_t>(raw) < cpts.size()) ? raw : -1;
            };

            if (ent.type == "TRIANGULATED_FACE") {
                std::vector<std::string_view> trisSv;
                SplitStepArgs(args[5], trisSv);
                for (auto ts : trisSv) {
                    std::vector<double> ti;
                    ParseFloatTuple(ts, ti);
                    if (ti.size() >= 3) {
                        int i0 = mapPn(static_cast<int>(ti[0]));
                        int i1 = mapPn(static_cast<int>(ti[1]));
                        int i2 = mapPn(static_cast<int>(ti[2]));
                        if (i0 >= 0 && i1 >= 0 && i2 >= 0) {
                            mesh.tris.push_back({ vBase + static_cast<uint32_t>(i0), vBase + static_cast<uint32_t>(i1), vBase + static_cast<uint32_t>(i2), {} });
                        }
                    }
                }
            } else {
                // COMPLEX_TRIANGULATED_FACE(name, #coords, pnmax, normals, pnindex, triangle_strips, triangle_fans)
                std::vector<std::string_view> stripsSv;
                SplitStepArgs(args[5], stripsSv);
                for (auto ss : stripsSv) {
                    std::vector<double> si;
                    ParseFloatTuple(ss, si);
                    for (size_t k = 0; k + 2 < si.size(); ++k) {
                        int i0 = mapPn(static_cast<int>(si[k]));
                        int i1 = mapPn(static_cast<int>(si[k + 1]));
                        int i2 = mapPn(static_cast<int>(si[k + 2]));
                        if (i0 >= 0 && i1 >= 0 && i2 >= 0) {
                            if (k & 1u) mesh.tris.push_back({ vBase + static_cast<uint32_t>(i1), vBase + static_cast<uint32_t>(i0), vBase + static_cast<uint32_t>(i2), {} });
                            else        mesh.tris.push_back({ vBase + static_cast<uint32_t>(i0), vBase + static_cast<uint32_t>(i1), vBase + static_cast<uint32_t>(i2), {} });
                        }
                    }
                }
                if (args.size() >= 7) {
                    std::vector<std::string_view> fansSv;
                    SplitStepArgs(args[6], fansSv);
                    for (auto fs : fansSv) {
                        std::vector<double> fi;
                        ParseFloatTuple(fs, fi);
                        if (fi.size() >= 3) {
                            int c0 = mapPn(static_cast<int>(fi[0]));
                            for (size_t k = 1; k + 1 < fi.size(); ++k) {
                                int i1 = mapPn(static_cast<int>(fi[k]));
                                int i2 = mapPn(static_cast<int>(fi[k + 1]));
                                if (c0 >= 0 && i1 >= 0 && i2 >= 0) {
                                    mesh.tris.push_back({ vBase + static_cast<uint32_t>(c0), vBase + static_cast<uint32_t>(i1), vBase + static_cast<uint32_t>(i2), {} });
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // 4. B-Rep Curve Sampler: EDGE_CURVE -> LINE, CIRCLE, ELLIPSE, B_SPLINE_CURVE_WITH_KNOTS / BOUNDED_CURVE
    std::unordered_map<int, std::vector<Vec3f>> edgeSampleCache;

    auto evalBSpline = [&](int degree, const std::vector<Vec3f>& ctrl, const std::vector<double>& knotMults, const std::vector<double>& knotVals, int nSamples) -> std::vector<Vec3f> {
        std::vector<Vec3f> out;
        if (ctrl.size() < 2) return ctrl;
        int n = static_cast<int>(ctrl.size()) - 1;
        int p = std::clamp(degree, 1, n);

        // Build expanded knot vector U of size n + p + 2
        std::vector<double> U;
        if (!knotMults.empty() && knotMults.size() == knotVals.size()) {
            for (size_t k = 0; k < knotMults.size(); ++k) {
                int m = std::clamp(static_cast<int>(knotMults[k]), 1, p + 1);
                for (int r = 0; r < m; ++r) U.push_back(knotVals[k]);
            }
        }
        if (static_cast<int>(U.size()) != n + p + 2) {
            U.clear();
            for (int k = 0; k <= p; ++k) U.push_back(0.0);
            int internalCnt = n - p;
            for (int k = 1; k <= internalCnt; ++k) U.push_back(static_cast<double>(k) / static_cast<double>(internalCnt + 1));
            for (int k = 0; k <= p; ++k) U.push_back(1.0);
        }

        double tMin = U[p];
        double tMax = U[n + 1];
        if (tMax <= tMin + 1e-12) return { ctrl.front(), ctrl.back() };

        out.reserve(nSamples + 1);
        out.push_back(ctrl.front());
        for (int s = 1; s < nSamples; ++s) {
            double u = tMin + (tMax - tMin) * (static_cast<double>(s) / static_cast<double>(nSamples));
            // Find knot span k such that U[k] <= u < U[k+1]
            int k = p;
            for (int j = p; j <= n; ++j) {
                if (u >= U[j] && u < U[j + 1]) { k = j; break; }
            }
            std::vector<Vec3f> d(p + 1);
            for (int j = 0; j <= p; ++j) d[j] = ctrl[std::clamp(k - p + j, 0, n)];
            for (int r = 1; r <= p; ++r) {
                for (int j = p; j >= r; --j) {
                    double denom = U[k + 1 + j - r] - U[k - p + j];
                    float alpha = (std::fabs(denom) > 1e-12) ? static_cast<float>((u - U[k - p + j]) / denom) : 0.0f;
                    d[j] = {
                        (1.0f - alpha) * d[j - 1].x + alpha * d[j].x,
                        (1.0f - alpha) * d[j - 1].y + alpha * d[j].y,
                        (1.0f - alpha) * d[j - 1].z + alpha * d[j].z
                    };
                }
            }
            out.push_back(d[p]);
        }
        out.push_back(ctrl.back());
        return out;
    };

    auto sampleEdgeCurve = [&](int edgeId) -> std::vector<Vec3f> {
        auto cit = edgeSampleCache.find(edgeId);
        if (cit != edgeSampleCache.end()) return cit->second;

        std::vector<Vec3f> res;
        auto it = entities.find(edgeId);
        if (it == entities.end()) return res;
        const auto& ee = it->second;
        if (ee.type != "EDGE_CURVE" && ee.type != "EDGE") return res;

        std::vector<std::string_view> eArgs;
        SplitStepArgs(ee.params, eArgs);
        if (eArgs.size() < 3) return res;

        int vStartId = ParseRefId(eArgs[1]);
        int vEndId   = ParseRefId(eArgs[2]);
        Vec3f pStart = {}, pEnd = {};
        bool okS = resolvePoint(vStartId, pStart, 0);
        bool okE = resolvePoint(vEndId, pEnd, 0);
        if (!okS && !okE) return res;
        if (!okS) pStart = pEnd;
        if (!okE) pEnd = pStart;

        if (ee.type == "EDGE" || eArgs.size() < 4) {
            res = { pStart, pEnd };
            edgeSampleCache[edgeId] = res;
            return res;
        }

        int curveId = ParseRefId(eArgs[3]);
        bool sameSense = (eArgs.size() >= 5) ? ParseStepBool(eArgs[4], true) : true;

        auto cIt = entities.find(curveId);
        if (cIt != entities.end()) {
            const auto& ce = cIt->second;
            std::vector<std::string_view> cArgs;
            SplitStepArgs(ce.params, cArgs);

            if ((ce.type == "CIRCLE" && cArgs.size() >= 3) || (ce.type == "ELLIPSE" && cArgs.size() >= 4)) {
                StepAxis2 ax = resolveAxis2(ParseRefId(cArgs[1]));
                float r1 = static_cast<float>(ParseStepDouble(cArgs[2], 0.0));
                float r2 = (ce.type == "ELLIPSE") ? static_cast<float>(ParseStepDouble(cArgs[3], r1)) : r1;
                if (r1 > 1e-6f && r2 > 1e-6f) {
                    Vec3f d0 = StepSub(pStart, ax.origin);
                    Vec3f d1 = StepSub(pEnd, ax.origin);
                    float z0 = StepDot(d0, ax.zAxis);
                    float z1 = StepDot(d1, ax.zAxis);
                    double a0 = std::atan2(StepDot(d0, ax.yAxis) / r2, StepDot(d0, ax.xAxis) / r1);
                    double a1 = std::atan2(StepDot(d1, ax.yAxis) / r2, StepDot(d1, ax.xAxis) / r1);

                    constexpr double kTwoPi = 6.283185307179586;
                    bool closedCircle = (vStartId == vEndId) || (StepLen(StepSub(pStart, pEnd)) < 1e-4f * std::max(r1, r2));
                    double delta = 0.0;
                    if (closedCircle) {
                        delta = sameSense ? kTwoPi : -kTwoPi;
                    } else if (sameSense) {
                        delta = a1 - a0;
                        while (delta <= 1e-5) delta += kTwoPi;
                    } else {
                        delta = a1 - a0;
                        while (delta >= -1e-5) delta -= kTwoPi;
                    }

                    const int segs = (std::fabs(delta) > 3.14) ? 16 : 12;
                    res.reserve(segs + 1);
                    res.push_back(pStart);
                    for (int k = 1; k < segs; ++k) {
                        double t = static_cast<double>(k) / static_cast<double>(segs);
                        double ang = a0 + delta * t;
                        float ca = static_cast<float>(std::cos(ang)) * r1;
                        float sa = static_cast<float>(std::sin(ang)) * r2;
                        float zk = z0 + (z1 - z0) * static_cast<float>(t);
                        Vec3f pt = StepAdd(ax.origin, StepAdd(StepScale(ax.xAxis, ca), StepAdd(StepScale(ax.yAxis, sa), StepScale(ax.zAxis, zk))));
                        res.push_back(pt);
                    }
                    res.push_back(closedCircle ? pStart : pEnd);
                }
            } else if (ce.type.find("B_SPLINE_CURVE") != std::string::npos || !ce.subEntities.empty()) {
                int deg = 3;
                std::vector<Vec3f> ctrlPts;
                std::vector<double> knotMults, knotVals;

                auto extractSplineSub = [&](const std::string& sType, std::string_view sParams) {
                    std::vector<std::string_view> sa;
                    SplitStepArgs(sParams, sa);
                    if (sType == "B_SPLINE_CURVE") {
                        // Complex entity form has no leading name: (degree, (#cp...), form, closed, self_intersect)
                        size_t degIdx = (sa.size() >= 3 && sa[1].find('#') != std::string_view::npos) ? 0 : 1;
                        if (degIdx + 1 < sa.size()) {
                            deg = static_cast<int>(ParseStepDouble(sa[degIdx], 3.0));
                            std::vector<int> cpRefs;
                            ParseRefList(sa[degIdx + 1], cpRefs);
                            for (int r : cpRefs) {
                                Vec3f cp;
                                if (resolvePoint(r, cp, 0)) ctrlPts.push_back(cp);
                            }
                        }
                    } else if (sType == "B_SPLINE_CURVE_WITH_KNOTS") {
                        if (sa.size() >= 8) {
                            // Standalone: (name, deg, (#cp...), form, closed, self_int, (mults), (knots), spec)
                            deg = static_cast<int>(ParseStepDouble(sa[1], 3.0));
                            std::vector<int> cpRefs;
                            ParseRefList(sa[2], cpRefs);
                            for (int r : cpRefs) {
                                Vec3f cp;
                                if (resolvePoint(r, cp, 0)) ctrlPts.push_back(cp);
                            }
                            ParseFloatTuple(sa[6], knotMults);
                            ParseFloatTuple(sa[7], knotVals);
                        } else if (sa.size() >= 2) {
                            // Complex sub-entity: ((mults), (knots), spec)
                            ParseFloatTuple(sa[0], knotMults);
                            ParseFloatTuple(sa[1], knotVals);
                        }
                    }
                };

                if (!ce.subEntities.empty()) {
                    for (const auto& sub : ce.subEntities) extractSplineSub(sub.first, sub.second);
                } else {
                    extractSplineSub(ce.type, ce.params);
                }

                if (ctrlPts.size() >= 2) {
                    if (!sameSense) std::reverse(ctrlPts.begin(), ctrlPts.end());
                    res = evalBSpline(deg, ctrlPts, knotMults, knotVals, 14);
                    res.front() = pStart;
                    res.back()  = pEnd;
                }
            } else if (ce.type == "POLYLINE" && cArgs.size() >= 2) {
                std::vector<int> pRefs;
                ParseRefList(cArgs[1], pRefs);
                for (int r : pRefs) {
                    Vec3f cp;
                    if (resolvePoint(r, cp, 0)) res.push_back(cp);
                }
                if (!sameSense) std::reverse(res.begin(), res.end());
            }
        }

        if (res.size() < 2) {
            res = { pStart, pEnd };
        }
        edgeSampleCache[edgeId] = res;
        return res;
    };

    // 5. Build closed 3D polygon ring from POLY_LOOP or EDGE_LOOP
    std::set<int> consumedLoops;

    auto buildLoopRing = [&](int loopId, bool boundOri) -> std::vector<Vec3f> {
        std::vector<Vec3f> ring;
        auto it = entities.find(loopId);
        if (it == entities.end()) return ring;
        consumedLoops.insert(loopId);
        const auto& le = it->second;
        std::vector<std::string_view> lArgs;
        SplitStepArgs(le.params, lArgs);
        if (lArgs.size() < 2) return ring;

        std::vector<int> refs;
        ParseRefList(lArgs[1], refs);

        if (le.type == "POLY_LOOP" || le.type == "VERTEX_LOOP") {
            for (int r : refs) {
                Vec3f p;
                if (resolvePoint(r, p, 0)) ring.push_back(p);
            }
        } else if (le.type == "EDGE_LOOP") {
            for (int oeId : refs) {
                auto oIt = entities.find(oeId);
                if (oIt == entities.end()) continue;
                int edgeCurveId = oeId;
                bool oeOri = true;
                if (oIt->second.type == "ORIENTED_EDGE") {
                    std::vector<std::string_view> oArgs;
                    SplitStepArgs(oIt->second.params, oArgs);
                    if (oArgs.size() >= 4) edgeCurveId = ParseRefId(oArgs[3]);
                    if (oArgs.size() >= 5) oeOri = ParseStepBool(oArgs[4], true);
                }
                std::vector<Vec3f> seg = sampleEdgeCurve(edgeCurveId);
                if (seg.empty()) continue;
                if (!oeOri) std::reverse(seg.begin(), seg.end());

                for (const auto& pt : seg) {
                    if (ring.empty() || StepLen(StepSub(pt, ring.back())) > 1e-5f) {
                        ring.push_back(pt);
                    }
                }
            }
            if (ring.size() >= 2 && StepLen(StepSub(ring.front(), ring.back())) <= 1e-4f) {
                ring.pop_back();
            }
        }

        if (!boundOri && ring.size() >= 3) {
            std::reverse(ring.begin(), ring.end());
        }
        return ring;
    };

    // 6. Triangulate all ADVANCED_FACE & FACE_SURFACE B-Rep faces
    for (const auto& [id, ent] : entities) {
        if (ent.type != "ADVANCED_FACE" && ent.type != "FACE_SURFACE" && ent.type != "FACE") continue;
        std::vector<std::string_view> fArgs;
        SplitStepArgs(ent.params, fArgs);
        if (fArgs.size() < 2) continue;

        std::vector<int> boundIds;
        ParseRefList(fArgs[1], boundIds);
        int surfId = (fArgs.size() >= 3) ? ParseRefId(fArgs[2]) : -1;
        bool faceSameSense = (fArgs.size() >= 4) ? ParseStepBool(fArgs[3], true) : true;

        std::vector<Vec3f> outerRing;
        std::vector<std::vector<Vec3f>> innerRings;

        for (int bId : boundIds) {
            auto bIt = entities.find(bId);
            if (bIt == entities.end()) continue;
            std::vector<std::string_view> bArgs;
            SplitStepArgs(bIt->second.params, bArgs);
            if (bArgs.size() < 2) continue;
            int loopId = ParseRefId(bArgs[1]);
            bool bOri = (bArgs.size() >= 3) ? ParseStepBool(bArgs[2], true) : true;
            std::vector<Vec3f> ring = buildLoopRing(loopId, bOri);
            if (ring.size() < 3) continue;

            if (bIt->second.type == "FACE_OUTER_BOUND" && outerRing.empty()) {
                outerRing = std::move(ring);
            } else if (outerRing.empty()) {
                outerRing = std::move(ring);
            } else {
                innerRings.push_back(std::move(ring));
            }
        }

        if (outerRing.size() < 3) continue;

        // Cylindrical / Conical surface parametric (theta, h) unwrap so curved barrels never fold in planar projection!
        bool handledCurvedSurf = false;
        auto sIt = entities.find(surfId);
        if (sIt != entities.end() && (sIt->second.type == "CYLINDRICAL_SURFACE" || sIt->second.type == "CONICAL_SURFACE")) {
            std::vector<std::string_view> sArgs;
            SplitStepArgs(sIt->second.params, sArgs);
            if (sArgs.size() >= 3) {
                StepAxis2 ax = resolveAxis2(ParseRefId(sArgs[1]));
                float rad = static_cast<float>(ParseStepDouble(sArgs[2], 1.0));
                if (rad > 1e-6f) {
                    // Case A: Two disjoint circular cap bounds (bottom & top rings without seam edges)
                    if (innerRings.size() == 1 && outerRing.size() >= 6 && innerRings[0].size() >= 6) {
                        const auto& rA = outerRing;
                        const auto& rB = innerRings[0];
                        float zA = StepDot(StepSub(rA[0], ax.origin), ax.zAxis);
                        float zB = StepDot(StepSub(rB[0], ax.origin), ax.zAxis);
                        if (std::fabs(zA - zB) > 1e-4f) {
                            const int segs = 16;
                            float zMin = std::min(zA, zB), zMax = std::max(zA, zB);
                            float coneSlope = 0.0f;
                            if (sIt->second.type == "CONICAL_SURFACE" && sArgs.size() >= 4) {
                                coneSlope = static_cast<float>(std::tan(ParseStepDouble(sArgs[3], 0.0)));
                            }
                            uint32_t vBase = static_cast<uint32_t>(mesh.vertices.size());
                            for (int i = 0; i <= segs; ++i) {
                                float ang = static_cast<float>(i) * 6.2831853f / static_cast<float>(segs);
                                float ca = std::cos(ang), sa = std::sin(ang);
                                float rLo = std::max(0.0f, rad + zMin * coneSlope);
                                float rHi = std::max(0.0f, rad + zMax * coneSlope);
                                Vec3f pLo = StepAdd(ax.origin, StepAdd(StepScale(ax.xAxis, ca * rLo), StepAdd(StepScale(ax.yAxis, sa * rLo), StepScale(ax.zAxis, zMin))));
                                Vec3f pHi = StepAdd(ax.origin, StepAdd(StepScale(ax.xAxis, ca * rHi), StepAdd(StepScale(ax.yAxis, sa * rHi), StepScale(ax.zAxis, zMax))));
                                mesh.vertices.push_back({ pLo.x, pLo.z, -pLo.y });
                                mesh.vertices.push_back({ pHi.x, pHi.z, -pHi.y });
                            }
                            for (int i = 0; i < segs; ++i) {
                                uint32_t b0 = vBase + static_cast<uint32_t>(i * 2);
                                uint32_t t0 = b0 + 1;
                                uint32_t b1 = b0 + 2;
                                uint32_t t1 = b0 + 3;
                                if (faceSameSense) {
                                    mesh.tris.push_back({ b0, b1, t1, {} });
                                    mesh.tris.push_back({ b0, t1, t0, {} });
                                } else {
                                    mesh.tris.push_back({ b0, t1, b1, {} });
                                    mesh.tris.push_back({ b0, t0, t1, {} });
                                }
                            }
                            handledCurvedSurf = true;
                        }
                    }
                    // Case B: Single seam-unrolled ring (unwrap continuous azimuth theta + axial height h)
                    if (!handledCurvedSurf && innerRings.empty()) {
                        std::vector<std::pair<float, float>> uv;
                        std::vector<uint32_t> idx3D;
                        uv.reserve(outerRing.size());
                        idx3D.reserve(outerRing.size());
                        double prevAng = 0.0;
                        for (size_t i = 0; i < outerRing.size(); ++i) {
                            Vec3f d = StepSub(outerRing[i], ax.origin);
                            float h = StepDot(d, ax.zAxis);
                            double ang = std::atan2(StepDot(d, ax.yAxis), StepDot(d, ax.xAxis));
                            if (i > 0) {
                                while (ang - prevAng >  3.141592653589793) ang -= 6.283185307179586;
                                while (ang - prevAng < -3.141592653589793) ang += 6.283185307179586;
                            }
                            prevAng = ang;
                            uv.push_back({ static_cast<float>(ang * rad), h });
                            uint32_t vid = static_cast<uint32_t>(mesh.vertices.size());
                            mesh.vertices.push_back({ outerRing[i].x, outerRing[i].z, -outerRing[i].y });
                            idx3D.push_back(vid);
                        }
                        EarClip2DWithHoles(uv, idx3D, {}, {}, !faceSameSense, mesh.tris);
                        handledCurvedSurf = true;
                    }
                }
            }
        }

        if (!handledCurvedSurf) {
            Triangulate3DRings(outerRing, innerRings, !faceSameSense, mesh);
        }
    }

    // 7. Standalone POLY_LOOP / EDGE_LOOP entities not referenced by any FACE_BOUND (e.g. testdata/sample.step)
    for (const auto& [id, ent] : entities) {
        if ((ent.type == "POLY_LOOP" || ent.type == "EDGE_LOOP") && consumedLoops.find(id) == consumedLoops.end()) {
            std::vector<Vec3f> ring = buildLoopRing(id, true);
            if (ring.size() >= 3) {
                Triangulate3DRings(ring, {}, false, mesh);
            }
        }
    }

    // 8. Standalone wireframe curves (GEOMETRIC_CURVE_SET / POLYLINE / standalone EDGE_CURVE) if no surface faces exist
    if (mesh.tris.empty()) {
        std::vector<std::pair<Vec3f, Vec3f>> segments;
        for (const auto& [id, ent] : entities) {
            if (ent.type == "POLYLINE") {
                std::vector<std::string_view> args;
                SplitStepArgs(ent.params, args);
                if (args.size() >= 2) {
                    std::vector<int> refs;
                    ParseRefList(args[1], refs);
                    std::vector<Vec3f> pts;
                    for (int r : refs) {
                        Vec3f p;
                        if (resolvePoint(r, p, 0)) pts.push_back(p);
                    }
                    for (size_t i = 0; i + 1 < pts.size(); ++i) segments.push_back({ pts[i], pts[i + 1] });
                }
            } else if (ent.type == "EDGE_CURVE") {
                std::vector<Vec3f> pts = sampleEdgeCurve(id);
                for (size_t i = 0; i + 1 < pts.size(); ++i) segments.push_back({ pts[i], pts[i + 1] });
            } else if (ent.type == "CIRCLE") {
                std::vector<std::string_view> args;
                SplitStepArgs(ent.params, args);
                if (args.size() >= 3) {
                    StepAxis2 ax = resolveAxis2(ParseRefId(args[1]));
                    float r = static_cast<float>(ParseStepDouble(args[2], 0.0));
                    if (r > 1e-6f) {
                        Vec3f prev = {};
                        for (int k = 0; k <= 16; ++k) {
                            float a = static_cast<float>(k) * 6.2831853f / 16.0f;
                            Vec3f pt = StepAdd(ax.origin, StepAdd(StepScale(ax.xAxis, std::cos(a) * r), StepScale(ax.yAxis, std::sin(a) * r)));
                            if (k > 0) segments.push_back({ prev, pt });
                            prev = pt;
                        }
                    }
                }
            }
        }
        if (!segments.empty()) {
            Vec3f mn = segments[0].first, mx = segments[0].first;
            for (const auto& sg : segments) {
                for (const Vec3f& p : { sg.first, sg.second }) {
                    mn.x = std::min(mn.x, p.x); mn.y = std::min(mn.y, p.y); mn.z = std::min(mn.z, p.z);
                    mx.x = std::max(mx.x, p.x); mx.y = std::max(mx.y, p.y); mx.z = std::max(mx.z, p.z);
                }
            }
            float span = std::max({ mx.x - mn.x, mx.y - mn.y, mx.z - mn.z, 1.0f });
            float strutR = span * 0.012f;
            for (const auto& sg : segments) {
                AddWireStrut(mesh, sg.first, sg.second, strutR);
            }
        }
    }

    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace Formats
