#pragma once
// PovRayMesh.h — Persistence of Vision Raytracer (.pov) 3D scene & mesh reader
// Parses // and /* */ comments, mesh { triangle / smooth_triangle }, mesh2 { vertex_vectors / face_indices },
// analytic primitives (box, sphere, cylinder, cone, polygon), #declare / object { ... } instancing,
// and object-level translate / scale / rotate / matrix transforms.

namespace Formats {

namespace DetailPovRay {

struct PovTokStream {
    std::string clean;
    size_t pos = 0;

    explicit PovTokStream(std::string_view raw) {
        clean.reserve(raw.size());
        size_t i = 0;
        while (i < raw.size()) {
            if (i + 1 < raw.size() && raw[i] == '/' && raw[i + 1] == '/') {
                i += 2;
                while (i < raw.size() && raw[i] != '\n') ++i;
            } else if (i + 1 < raw.size() && raw[i] == '/' && raw[i + 1] == '*') {
                i += 2;
                while (i + 1 < raw.size() && !(raw[i] == '*' && raw[i + 1] == '/')) ++i;
                i = std::min(raw.size(), i + 2);
                clean.push_back(' ');
            } else if (raw[i] == '"') {
                // Skip string literals (e.g. texture image names) so braces inside strings don't confuse block depth
                ++i;
                while (i < raw.size() && raw[i] != '"') {
                    if (raw[i] == '\\' && i + 1 < raw.size()) ++i;
                    ++i;
                }
                if (i < raw.size()) ++i;
                clean.push_back(' ');
            } else {
                clean.push_back(raw[i++]);
            }
        }
    }

    void SkipWsAndCommas() {
        while (pos < clean.size()) {
            char c = clean[pos];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',' || c == ';') ++pos;
            else break;
        }
    }

    bool Eof() {
        SkipWsAndCommas();
        return pos >= clean.size();
    }

    char Peek() {
        SkipWsAndCommas();
        return (pos < clean.size()) ? clean[pos] : '\0';
    }

    bool ConsumeChar(char expected) {
        SkipWsAndCommas();
        if (pos < clean.size() && clean[pos] == expected) {
            ++pos;
            return true;
        }
        return false;
    }

    std::string_view NextIdent() {
        SkipWsAndCommas();
        if (pos >= clean.size()) return {};
        const size_t start = pos;
        if (clean[pos] == '#') ++pos;
        while (pos < clean.size()) {
            unsigned char c = static_cast<unsigned char>(clean[pos]);
            if (std::isalnum(c) || c == '_') ++pos;
            else break;
        }
        return (pos > start) ? std::string_view(clean).substr(start, pos - start) : std::string_view{};
    }

    bool ParseScalar(float& outVal) {
        SkipWsAndCommas();
        if (pos >= clean.size()) return false;
        const size_t start = pos;
        if (clean[pos] == '+' || clean[pos] == '-') ++pos;
        bool sawDigit = false;
        while (pos < clean.size() && std::isdigit(static_cast<unsigned char>(clean[pos]))) { ++pos; sawDigit = true; }
        if (pos < clean.size() && clean[pos] == '.') {
            ++pos;
            while (pos < clean.size() && std::isdigit(static_cast<unsigned char>(clean[pos]))) { ++pos; sawDigit = true; }
        }
        if (!sawDigit) { pos = start; return false; }
        if (pos < clean.size() && (clean[pos] == 'e' || clean[pos] == 'E')) {
            size_t ePos = pos++;
            if (pos < clean.size() && (clean[pos] == '+' || clean[pos] == '-')) ++pos;
            bool eDig = false;
            while (pos < clean.size() && std::isdigit(static_cast<unsigned char>(clean[pos]))) { ++pos; eDig = true; }
            if (!eDig) pos = ePos;
        }
        std::string_view numSv = std::string_view(clean).substr(start, pos - start);
        if (!ReaderText::ParseFloatSv(numSv, outVal)) {
            pos = start;
            return false;
        }
        return true;
    }

    // Parses <x, y, z> or scalar broadcast s -> <s, s, s> if allowScalar==true
    bool ParseVec3(Vec3f& outV, bool allowScalar = false) {
        SkipWsAndCommas();
        if (pos < clean.size() && clean[pos] == '<') {
            ++pos;
            float x = 0.0f, y = 0.0f, z = 0.0f;
            if (!ParseScalar(x) || !ParseScalar(y)) return false;
            if (!ParseScalar(z)) z = 0.0f;
            ConsumeChar('>');
            outV = { x, y, z };
            return true;
        }
        if (allowScalar) {
            float s = 0.0f;
            if (ParseScalar(s)) {
                outV = { s, s, s };
                return true;
            }
        }
        return false;
    }

    void SkipBalancedBraces() {
        if (!ConsumeChar('{')) return;
        int depth = 1;
        while (pos < clean.size() && depth > 0) {
            char c = clean[pos++];
            if (c == '{') ++depth;
            else if (c == '}') --depth;
        }
    }
};

struct PovSubMesh {
    std::vector<Vec3f> verts;
    std::vector<MeshTriangle> tris;
};

inline void ApplyMat4ToSubMesh(PovSubMesh& sm, const float m[16]) {
    for (auto& v : sm.verts) {
        const float x = v.x, y = v.y, z = v.z;
        v.x = m[0] * x + m[1] * y + m[2]  * z + m[3];
        v.y = m[4] * x + m[5] * y + m[6]  * z + m[7];
        v.z = m[8] * x + m[9] * y + m[10] * z + m[11];
    }
}

inline void AppendXform(float cur[16], const float step[16]) {
    // POV-Ray object transforms apply in appearance order: p' = step * (cur * p)
    float next[16];
    MeshBuild3D::MatMul4RowMajor(step, cur, next);
    std::memcpy(cur, next, sizeof(next));
}

inline void ParseObjectModifier(std::string_view kw, PovTokStream& ts, float xf[16]) {
    if (kw == "translate") {
        Vec3f t{};
        if (ts.ParseVec3(t, true)) {
            float step[16] = { 1,0,0,t.x,  0,1,0,t.y,  0,0,1,t.z,  0,0,0,1 };
            AppendXform(xf, step);
        }
    } else if (kw == "scale") {
        Vec3f s{};
        if (ts.ParseVec3(s, true)) {
            float step[16] = { s.x,0,0,0,  0,s.y,0,0,  0,0,s.z,0,  0,0,0,1 };
            AppendXform(xf, step);
        }
    } else if (kw == "rotate") {
        Vec3f deg{};
        if (ts.ParseVec3(deg, false)) {
            constexpr float kD2R = 3.14159265358979323846f / 180.0f;
            const float cx = std::cos(deg.x * kD2R), sx = std::sin(deg.x * kD2R);
            const float cy = std::cos(deg.y * kD2R), sy = std::sin(deg.y * kD2R);
            const float cz = std::cos(deg.z * kD2R), sz = std::sin(deg.z * kD2R);
            const float rx[16] = { 1,0,0,0,  0,cx,-sx,0,  0,sx,cx,0,  0,0,0,1 };
            const float ry[16] = { cy,0,sy,0,  0,1,0,0,  -sy,0,cy,0,  0,0,0,1 };
            const float rz[16] = { cz,-sz,0,0,  sz,cz,0,0,  0,0,1,0,  0,0,0,1 };
            AppendXform(xf, rx);
            AppendXform(xf, ry);
            AppendXform(xf, rz);
        }
    } else if (kw == "matrix") {
        if (ts.ConsumeChar('<')) {
            float v[12] = {};
            bool ok = true;
            for (int i = 0; i < 12; ++i) {
                if (!ts.ParseScalar(v[i])) { ok = false; break; }
            }
            ts.ConsumeChar('>');
            if (ok) {
                // POV-Ray row-vector matrix <v0..v11>: x' = v0*x + v3*y + v6*z + v9
                const float step[16] = {
                    v[0], v[3], v[6], v[9],
                    v[1], v[4], v[7], v[10],
                    v[2], v[5], v[8], v[11],
                    0,    0,    0,    1
                };
                AppendXform(xf, step);
            }
        }
    } else if (ts.Peek() == '{') {
        ts.SkipBalancedBraces();
    }
}

inline bool ParsePovMeshBlock(PovTokStream& ts, PovSubMesh& outSm) {
    if (!ts.ConsumeChar('{')) return false;
    float xf[16] = { 1,0,0,0,  0,1,0,0,  0,0,1,0,  0,0,0,1 };

    while (!ts.Eof()) {
        if (ts.ConsumeChar('}')) break;
        std::string_view kw = ts.NextIdent();
        if (kw.empty()) { ++ts.pos; continue; }

        if (kw == "triangle" || kw == "smooth_triangle") {
            const bool smooth = (kw == "smooth_triangle");
            if (!ts.ConsumeChar('{')) continue;
            Vec3f p0{}, p1{}, p2{}, dummy{};
            bool ok = false;
            if (!smooth) {
                ok = ts.ParseVec3(p0) && ts.ParseVec3(p1) && ts.ParseVec3(p2);
            } else {
                ok = ts.ParseVec3(p0) && ts.ParseVec3(dummy) &&
                     ts.ParseVec3(p1) && ts.ParseVec3(dummy) &&
                     ts.ParseVec3(p2) && ts.ParseVec3(dummy);
            }
            // Skip any trailing texture { ... } inside triangle
            int depth = 1;
            while (ts.pos < ts.clean.size() && depth > 0) {
                char c = ts.clean[ts.pos++];
                if (c == '{') ++depth;
                else if (c == '}') --depth;
            }
            if (ok && outSm.tris.size() < 2000000) {
                uint32_t b = static_cast<uint32_t>(outSm.verts.size());
                outSm.verts.push_back(p0);
                outSm.verts.push_back(p1);
                outSm.verts.push_back(p2);
                outSm.tris.push_back({ b, b + 1, b + 2, {} });
            }
        } else {
            ParseObjectModifier(kw, ts, xf);
        }
    }

    if (outSm.tris.empty()) return false;
    ApplyMat4ToSubMesh(outSm, xf);
    return true;
}

inline bool ParsePovMesh2Block(PovTokStream& ts, PovSubMesh& outSm) {
    if (!ts.ConsumeChar('{')) return false;
    float xf[16] = { 1,0,0,0,  0,1,0,0,  0,0,1,0,  0,0,0,1 };

    while (!ts.Eof()) {
        if (ts.ConsumeChar('}')) break;
        std::string_view kw = ts.NextIdent();
        if (kw.empty()) { ++ts.pos; continue; }

        if (kw == "vertex_vectors") {
            if (!ts.ConsumeChar('{')) continue;
            float nFloat = 0.0f;
            if (!ts.ParseScalar(nFloat) || !std::isfinite(nFloat) || nFloat < 3.0f || nFloat > 2000000.0f) return false;
            const int count = static_cast<int>(std::lround(nFloat));
            outSm.verts.reserve(static_cast<size_t>(count));
            for (int i = 0; i < count; ++i) {
                Vec3f v{};
                if (!ts.ParseVec3(v)) return false;
                outSm.verts.push_back(v);
            }
            ts.ConsumeChar('}');
        } else if (kw == "face_indices") {
            if (!ts.ConsumeChar('{')) continue;
            float mFloat = 0.0f;
            if (!ts.ParseScalar(mFloat) || !std::isfinite(mFloat) || mFloat < 1.0f || mFloat > 4000000.0f) return false;
            const int fCount = static_cast<int>(std::lround(mFloat));
            outSm.tris.reserve(outSm.tris.size() + static_cast<size_t>(std::min(fCount, 65536)));
            int readFaces = 0;
            while (!ts.Eof()) {
                if (ts.ConsumeChar('}')) break;
                if (ts.Peek() == '<') {
                    Vec3f idxV{};
                    if (!ts.ParseVec3(idxV)) return false;
                    if (!std::isfinite(idxV.x) || !std::isfinite(idxV.y) || !std::isfinite(idxV.z) ||
                        idxV.x < -0.5f || idxV.y < -0.5f || idxV.z < -0.5f ||
                        idxV.x > 4000000.0f || idxV.y > 4000000.0f || idxV.z > 4000000.0f) {
                        return false;
                    }
                    const int i0 = static_cast<int>(std::lround(idxV.x));
                    const int i1 = static_cast<int>(std::lround(idxV.y));
                    const int i2 = static_cast<int>(std::lround(idxV.z));
                    if (i0 < 0 || i1 < 0 || i2 < 0 ||
                        static_cast<size_t>(i0) >= outSm.verts.size() ||
                        static_cast<size_t>(i1) >= outSm.verts.size() ||
                        static_cast<size_t>(i2) >= outSm.verts.size()) {
                        return false;
                    }
                    if (i0 != i1 && i1 != i2 && i0 != i2) {
                        outSm.tris.push_back({ static_cast<uint32_t>(i0), static_cast<uint32_t>(i1), static_cast<uint32_t>(i2), {} });
                    }
                    ++readFaces;
                    (void)readFaces;
                } else {
                    // Optional per-face texture index integers after <i0,i1,i2>
                    float dummyTex = 0.0f;
                    if (!ts.ParseScalar(dummyTex)) ++ts.pos;
                }
            }
        } else if (kw == "normal_vectors" || kw == "uv_vectors" || kw == "texture_list" ||
                   kw == "normal_indices" || kw == "uv_indices") {
            ts.SkipBalancedBraces();
        } else {
            ParseObjectModifier(kw, ts, xf);
        }
    }

    if (outSm.verts.empty() || outSm.tris.empty()) return false;
    ApplyMat4ToSubMesh(outSm, xf);
    return true;
}

inline bool ParsePovAnalyticPrim(std::string_view kw, PovTokStream& ts, PovSubMesh& outSm) {
    if (!ts.ConsumeChar('{')) return false;
    float xf[16] = { 1,0,0,0,  0,1,0,0,  0,0,1,0,  0,0,0,1 };

    if (kw == "box") {
        Vec3f a{}, b{};
        if (!ts.ParseVec3(a) || !ts.ParseVec3(b)) return false;
        const Vec3f c[8] = {
            {a.x,a.y,a.z}, {b.x,a.y,a.z}, {b.x,b.y,a.z}, {a.x,b.y,a.z},
            {a.x,a.y,b.z}, {b.x,a.y,b.z}, {b.x,b.y,b.z}, {a.x,b.y,b.z}
        };
        for (const auto& p : c) outSm.verts.push_back(p);
        const uint32_t q[6][4] = {
            {0,3,2,1}, {4,5,6,7}, {0,1,5,4}, {2,3,7,6}, {0,4,7,3}, {1,2,6,5}
        };
        for (const auto& f : q) {
            outSm.tris.push_back({ f[0], f[1], f[2], {} });
            outSm.tris.push_back({ f[0], f[2], f[3], {} });
        }
    } else if (kw == "sphere") {
        Vec3f ctr{};
        float rad = 0.0f;
        if (!ts.ParseVec3(ctr) || !ts.ParseScalar(rad) || rad <= 1e-6f) return false;
        constexpr int kRings = 10, kSegs = 16;
        outSm.verts.push_back({ ctr.x, ctr.y + rad, ctr.z });
        for (int r = 1; r < kRings; ++r) {
            const float phi = 3.1415926535f * static_cast<float>(r) / kRings;
            for (int s = 0; s < kSegs; ++s) {
                const float th = 6.283185307f * static_cast<float>(s) / kSegs;
                outSm.verts.push_back({
                    ctr.x + rad * std::sin(phi) * std::cos(th),
                    ctr.y + rad * std::cos(phi),
                    ctr.z + rad * std::sin(phi) * std::sin(th)
                });
            }
        }
        const uint32_t south = static_cast<uint32_t>(outSm.verts.size());
        outSm.verts.push_back({ ctr.x, ctr.y - rad, ctr.z });
        auto rIdx = [](int r, int s) -> uint32_t { return static_cast<uint32_t>(1 + (r - 1) * kSegs + (s % kSegs)); };
        for (int s = 0; s < kSegs; ++s) outSm.tris.push_back({ 0, rIdx(1, s), rIdx(1, s + 1), {} });
        for (int r = 1; r < kRings - 1; ++r) {
            for (int s = 0; s < kSegs; ++s) {
                outSm.tris.push_back({ rIdx(r, s), rIdx(r + 1, s), rIdx(r + 1, s + 1), {} });
                outSm.tris.push_back({ rIdx(r, s), rIdx(r + 1, s + 1), rIdx(r, s + 1), {} });
            }
        }
        for (int s = 0; s < kSegs; ++s) outSm.tris.push_back({ rIdx(kRings - 1, s), south, rIdx(kRings - 1, s + 1), {} });
    } else if (kw == "cylinder" || kw == "cone") {
        Vec3f p0{}, p1{};
        float r0 = 0.0f, r1 = 0.0f;
        if (kw == "cylinder") {
            if (!ts.ParseVec3(p0) || !ts.ParseVec3(p1) || !ts.ParseScalar(r0) || r0 <= 1e-6f) return false;
            r1 = r0;
        } else {
            if (!ts.ParseVec3(p0) || !ts.ParseScalar(r0) || !ts.ParseVec3(p1) || !ts.ParseScalar(r1)) return false;
            if (r0 < 0.0f || r1 < 0.0f || (r0 <= 1e-6f && r1 <= 1e-6f)) return false;
        }
        Vec3f axis = { p1.x - p0.x, p1.y - p0.y, p1.z - p0.z };
        const float len = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
        if (len <= 1e-6f) return false;
        axis = { axis.x / len, axis.y / len, axis.z / len };
        Vec3f ref = (std::fabs(axis.y) < 0.9f) ? Vec3f{ 0, 1, 0 } : Vec3f{ 1, 0, 0 };
        Vec3f u = { axis.y * ref.z - axis.z * ref.y, axis.z * ref.x - axis.x * ref.z, axis.x * ref.y - axis.y * ref.x };
        const float ul = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z);
        u = { u.x / ul, u.y / ul, u.z / ul };
        Vec3f v = { axis.y * u.z - axis.z * u.y, axis.z * u.x - axis.x * u.z, axis.x * u.y - axis.y * u.x };

        constexpr int kSegs = 16;
        for (int k = 0; k < 2; ++k) {
            const Vec3f& pc = (k == 0) ? p0 : p1;
            const float rk = (k == 0) ? r0 : r1;
            for (int s = 0; s < kSegs; ++s) {
                const float th = 6.283185307f * static_cast<float>(s) / kSegs;
                const float cs = std::cos(th), sn = std::sin(th);
                outSm.verts.push_back({
                    pc.x + rk * (cs * u.x + sn * v.x),
                    pc.y + rk * (cs * u.y + sn * v.y),
                    pc.z + rk * (cs * u.z + sn * v.z)
                });
            }
        }
        const uint32_t c0 = static_cast<uint32_t>(outSm.verts.size());
        outSm.verts.push_back(p0);
        const uint32_t c1 = static_cast<uint32_t>(outSm.verts.size());
        outSm.verts.push_back(p1);
        for (int s = 0; s < kSegs; ++s) {
            const uint32_t sn = static_cast<uint32_t>((s + 1) % kSegs);
            outSm.tris.push_back({ static_cast<uint32_t>(s), static_cast<uint32_t>(kSegs + s), static_cast<uint32_t>(kSegs + sn), {} });
            outSm.tris.push_back({ static_cast<uint32_t>(s), static_cast<uint32_t>(kSegs + sn), sn, {} });
            if (r0 > 1e-6f) outSm.tris.push_back({ c0, sn, static_cast<uint32_t>(s), {} });
            if (r1 > 1e-6f) outSm.tris.push_back({ c1, static_cast<uint32_t>(kSegs + s), static_cast<uint32_t>(kSegs + sn), {} });
        }
    } else {
        return false;
    }

    while (!ts.Eof()) {
        if (ts.ConsumeChar('}')) break;
        std::string_view mkw = ts.NextIdent();
        if (mkw.empty()) { ++ts.pos; continue; }
        ParseObjectModifier(mkw, ts, xf);
    }
    ApplyMat4ToSubMesh(outSm, xf);
    return !outSm.tris.empty();
}

inline void MergeSubMesh(LoadedMesh3D& dst, const PovSubMesh& src) {
    const uint32_t base = static_cast<uint32_t>(dst.vertices.size());
    for (const auto& v : src.verts) {
        // POV-Ray is left-handed Y-up (+Z into screen); negate Z for right-handed Y-up viewer
        dst.vertices.push_back({ v.x, v.y, -v.z });
    }
    for (const auto& t : src.tris) {
        dst.tris.push_back({ base + t.i0, base + t.i2, base + t.i1, {} });
    }
}

} // namespace DetailPovRay

inline bool ParsePovRayMesh(
    const uint8_t* data,
    size_t size,
    LoadedMesh3D& mesh)
{
    if (!data || size < 16 || size > 64u * 1024u * 1024u) return false;
    std::string_view sv = ReaderText::StripUtf8Bom(std::string_view(reinterpret_cast<const char*>(data), size));
    if (sv.find('\0') != std::string_view::npos) return false;

    DetailPovRay::PovTokStream ts(sv);
    std::unordered_map<std::string, DetailPovRay::PovSubMesh> declaredMeshes;

    auto parseAnyObject = [&](std::string_view kw, DetailPovRay::PovSubMesh& outSm) -> bool {
        if (kw == "mesh") return DetailPovRay::ParsePovMeshBlock(ts, outSm);
        if (kw == "mesh2") return DetailPovRay::ParsePovMesh2Block(ts, outSm);
        if (kw == "box" || kw == "sphere" || kw == "cylinder" || kw == "cone") {
            return DetailPovRay::ParsePovAnalyticPrim(kw, ts, outSm);
        }
        return false;
    };

    while (!ts.Eof()) {
        std::string_view kw = ts.NextIdent();
        if (kw.empty()) { ++ts.pos; continue; }

        if (kw == "#declare" || kw == "#local") {
            std::string_view varName = ts.NextIdent();
            if (!varName.empty() && ts.ConsumeChar('=')) {
                std::string_view rhsKw = ts.NextIdent();
                DetailPovRay::PovSubMesh sm;
                if (parseAnyObject(rhsKw, sm)) {
                    declaredMeshes[std::string(varName)] = std::move(sm);
                }
            }
        } else if (kw == "object") {
            if (!ts.ConsumeChar('{')) continue;
            std::string_view refName = ts.NextIdent();
            DetailPovRay::PovSubMesh inst;
            bool haveInst = false;
            auto it = declaredMeshes.find(std::string(refName));
            if (it != declaredMeshes.end()) {
                inst = it->second;
                haveInst = true;
            } else if (parseAnyObject(refName, inst)) {
                haveInst = true;
            }
            float xf[16] = { 1,0,0,0,  0,1,0,0,  0,0,1,0,  0,0,0,1 };
            while (!ts.Eof()) {
                if (ts.ConsumeChar('}')) break;
                std::string_view mkw = ts.NextIdent();
                if (mkw.empty()) { ++ts.pos; continue; }
                DetailPovRay::ParseObjectModifier(mkw, ts, xf);
            }
            if (haveInst) {
                DetailPovRay::ApplyMat4ToSubMesh(inst, xf);
                DetailPovRay::MergeSubMesh(mesh, inst);
            }
        } else {
            DetailPovRay::PovSubMesh sm;
            if (parseAnyObject(kw, sm)) {
                DetailPovRay::MergeSubMesh(mesh, sm);
            } else if (kw == "camera" || kw == "light_source" || kw == "global_settings" ||
                       kw == "sky_sphere" || kw == "background" || kw == "fog") {
                ts.SkipBalancedBraces();
            }
        }
    }

    // If a library .pov file only #declared a mesh without instantiating it at scene level, show declared meshes
    if (mesh.tris.empty() && !declaredMeshes.empty()) {
        for (const auto& kv : declaredMeshes) {
            DetailPovRay::MergeSubMesh(mesh, kv.second);
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"POV-Ray Scene Mesh (.pov)";
    return true;
}

} // namespace Formats
