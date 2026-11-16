#pragma once
// X3d.h — Web3D Extensible 3D XML (.x3d, plain & gzipped) Hierarchical Scene-Graph Parser
// Supports <Transform> 4x4 matrix stack (translation, rotation, scale, scaleOrientation, center),
// DEF / USE instancing, IndexedFaceSet, IndexedTriangleSet, IndexedTriangleStripSet, IndexedTriangleFanSet,
// TriangleSet, TriangleStripSet, QuadSet, IndexedQuadSet, IndexedLineSet, LineSet, PointSet,
// ElevationGrid, Box, Sphere, Cylinder, Cone, Material diffuseColor & Color.

namespace Formats {

namespace DetailX3d {

inline std::string ToLowerX3d(std::string_view sv) {
    std::string s(sv);
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

struct X3dMat4 {
    float m[4][4] = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f }
    };

    static X3dMat4 Mul(const X3dMat4& a, const X3dMat4& b) {
        X3dMat4 r;
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                r.m[i][j] = a.m[i][0] * b.m[0][j] +
                            a.m[i][1] * b.m[1][j] +
                            a.m[i][2] * b.m[2][j] +
                            a.m[i][3] * b.m[3][j];
            }
        }
        return r;
    }

    static X3dMat4 Translate(float tx, float ty, float tz) {
        X3dMat4 r;
        r.m[0][3] = tx; r.m[1][3] = ty; r.m[2][3] = tz;
        return r;
    }

    static X3dMat4 Scale(float sx, float sy, float sz) {
        X3dMat4 r;
        r.m[0][0] = sx; r.m[1][1] = sy; r.m[2][2] = sz;
        return r;
    }

    static X3dMat4 RotateAxisAngle(float ax, float ay, float az, float angleRad) {
        X3dMat4 r;
        float len = std::sqrt(ax * ax + ay * ay + az * az);
        if (len < 1e-7f || std::fabs(angleRad) < 1e-8f) return r;
        float x = ax / len, y = ay / len, z = az / len;
        float c = std::cos(angleRad), s = std::sin(angleRad), t = 1.0f - c;
        r.m[0][0] = t * x * x + c;     r.m[0][1] = t * x * y - s * z; r.m[0][2] = t * x * z + s * y;
        r.m[1][0] = t * x * y + s * z; r.m[1][1] = t * y * y + c;     r.m[1][2] = t * y * z - s * x;
        r.m[2][0] = t * x * z - s * y; r.m[2][1] = t * y * z + s * x; r.m[2][2] = t * z * z + c;
        return r;
    }

    static X3dMat4 ComposeTransform(
        const float tr[3], const float rot[4], const float sc[3],
        const float scOri[4], const float ctr[3])
    {
        X3dMat4 m = Translate(tr[0] + ctr[0], tr[1] + ctr[1], tr[2] + ctr[2]);
        m = Mul(m, RotateAxisAngle(rot[0], rot[1], rot[2], rot[3]));
        const bool hasSO = (std::fabs(scOri[3]) > 1e-6f);
        if (hasSO) m = Mul(m, RotateAxisAngle(scOri[0], scOri[1], scOri[2], scOri[3]));
        m = Mul(m, Scale(sc[0], sc[1], sc[2]));
        if (hasSO) m = Mul(m, RotateAxisAngle(scOri[0], scOri[1], scOri[2], -scOri[3]));
        if (std::fabs(ctr[0]) > 1e-7f || std::fabs(ctr[1]) > 1e-7f || std::fabs(ctr[2]) > 1e-7f) {
            m = Mul(m, Translate(-ctr[0], -ctr[1], -ctr[2]));
        }
        return m;
    }

    Vec3f TransformPoint(const Vec3f& p) const {
        return {
            m[0][0] * p.x + m[0][1] * p.y + m[0][2] * p.z + m[0][3],
            m[1][0] * p.x + m[1][1] * p.y + m[1][2] * p.z + m[1][3],
            m[2][0] * p.x + m[2][1] * p.y + m[2][2] * p.z + m[2][3]
        };
    }

    float Det3x3() const {
        return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
               m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
               m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    }
};

struct X3dDomNode {
    std::string tagLower;
    std::string defName;
    std::string useName;
    std::vector<std::pair<std::string, std::string_view>> attrs; // keyLower -> raw value slice
    std::string_view textSlice;
    std::vector<int> children;

    std::string_view GetAttr(std::string_view keyLower) const {
        for (const auto& kv : attrs) {
            if (kv.first == keyLower) return kv.second;
        }
        return {};
    }
};

inline void ParseX3dFloats(std::string_view sv, std::vector<float>& out, size_t maxCount = 50000000u) {
    out.clear();
    const char* cur = sv.data();
    const char* const end = cur + sv.size();
    while (cur < end && out.size() < maxCount) {
        while (cur < end && (*cur == ' ' || *cur == '\t' || *cur == '\r' || *cur == '\n' || *cur == ',')) ++cur;
        if (cur >= end) break;
        const char* s = cur;
        while (cur < end && *cur != ' ' && *cur != '\t' && *cur != '\r' && *cur != '\n' && *cur != ',') ++cur;
        size_t len = static_cast<size_t>(cur - s);
        if (len == 0 || len >= 64) continue;
        char buf[64];
        std::memcpy(buf, s, len);
        buf[len] = '\0';
        char* next = nullptr;
        double v = std::strtod(buf, &next);
        if (next != buf && std::isfinite(v)) {
            out.push_back(static_cast<float>(v));
        }
    }
}

inline void ParseX3dInts(std::string_view sv, std::vector<int64_t>& out, size_t maxCount = 50000000u) {
    out.clear();
    const char* cur = sv.data();
    const char* const end = cur + sv.size();
    while (cur < end && out.size() < maxCount) {
        while (cur < end && (*cur == ' ' || *cur == '\t' || *cur == '\r' || *cur == '\n' || *cur == ',')) ++cur;
        if (cur >= end) break;
        const char* s = cur;
        while (cur < end && *cur != ' ' && *cur != '\t' && *cur != '\r' && *cur != '\n' && *cur != ',') ++cur;
        int64_t val = 0;
        auto [ptr, ec] = std::from_chars(s, cur, val);
        if (ec == std::errc{} && ptr == cur) {
            out.push_back(val);
        }
    }
}

inline float ReadX3dFloatAttr(const X3dDomNode& nd, std::string_view key, float defV) {
    std::string_view sv = nd.GetAttr(key);
    if (sv.empty()) return defV;
    std::vector<float> tmp;
    ParseX3dFloats(sv, tmp, 1);
    return tmp.empty() ? defV : tmp[0];
}

inline bool ReadX3dBoolAttr(const X3dDomNode& nd, std::string_view key, bool defV) {
    std::string_view sv = nd.GetAttr(key);
    if (sv.empty()) return defV;
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t')) sv.remove_prefix(1);
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t')) sv.remove_suffix(1);
    std::string low = ToLowerX3d(sv);
    if (low == "false" || low == "0") return false;
    if (low == "true" || low == "1") return true;
    return defV;
}

inline void ReadX3dVecAttr(const X3dDomNode& nd, std::string_view key, size_t n, float* dst) {
    std::string_view sv = nd.GetAttr(key);
    if (sv.empty()) return;
    std::vector<float> tmp;
    ParseX3dFloats(sv, tmp, n);
    if (tmp.size() >= n) {
        for (size_t i = 0; i < n; ++i) dst[i] = tmp[i];
    }
}

struct X3dDom {
    std::vector<X3dDomNode> nodes;
    std::unordered_map<std::string, int> defMap;
    int x3dRoot = -1;

    bool Parse(std::string_view sv) {
        nodes.clear();
        defMap.clear();
        x3dRoot = -1;

        std::vector<int> stack;
        std::vector<size_t> contentStartStack;

        size_t pos = 0;
        const size_t n = sv.size();
        while (pos < n) {
            size_t lt = sv.find('<', pos);
            if (lt == std::string_view::npos || lt + 1 >= n) break;

            // Skip XML comments, CDATA, PI, DOCTYPE
            if (lt + 4 <= n && sv.substr(lt, 4) == "<!--") {
                size_t ec = sv.find("-->", lt + 4);
                pos = (ec == std::string_view::npos) ? n : (ec + 3);
                continue;
            }
            if (lt + 9 <= n && sv.substr(lt, 9) == "<![CDATA[") {
                size_t ec = sv.find("]]>", lt + 9);
                pos = (ec == std::string_view::npos) ? n : (ec + 3);
                continue;
            }
            if (sv[lt + 1] == '?' || sv[lt + 1] == '!') {
                size_t gt = sv.find('>', lt + 2);
                pos = (gt == std::string_view::npos) ? n : (gt + 1);
                continue;
            }

            size_t gt = lt + 1;
            char q = 0;
            while (gt < n) {
                char c = sv[gt];
                if (q) { if (c == q) q = 0; }
                else if (c == '"' || c == '\'') q = c;
                else if (c == '>') break;
                ++gt;
            }
            if (gt >= n) break;
            pos = gt + 1;

            std::string_view inner = sv.substr(lt + 1, gt - lt - 1);
            while (!inner.empty() && (inner.front() == ' ' || inner.front() == '\t' || inner.front() == '\r' || inner.front() == '\n')) inner.remove_prefix(1);
            while (!inner.empty() && (inner.back() == ' ' || inner.back() == '\t' || inner.back() == '\r' || inner.back() == '\n')) inner.remove_suffix(1);
            if (inner.empty()) continue;

            bool isClosing = false;
            bool isSelfClosing = false;
            if (inner.front() == '/') {
                isClosing = true;
                inner.remove_prefix(1);
            } else if (inner.back() == '/') {
                isSelfClosing = true;
                inner.remove_suffix(1);
            }

            size_t ne = 0;
            while (ne < inner.size() && inner[ne] != ' ' && inner[ne] != '\t' && inner[ne] != '\r' && inner[ne] != '\n') ++ne;
            std::string_view qName = inner.substr(0, ne);
            size_t col = qName.rfind(':');
            std::string tagLow = ToLowerX3d((col == std::string_view::npos) ? qName : qName.substr(col + 1));

            if (isClosing) {
                for (int si = static_cast<int>(stack.size()) - 1; si >= 0; --si) {
                    int openIdx = stack[si];
                    if (nodes[openIdx].tagLower == tagLow) {
                        if (nodes[openIdx].children.empty() && lt >= contentStartStack[si]) {
                            nodes[openIdx].textSlice = sv.substr(contentStartStack[si], lt - contentStartStack[si]);
                        }
                        stack.resize(static_cast<size_t>(si));
                        contentStartStack.resize(static_cast<size_t>(si));
                        break;
                    }
                }
                continue;
            }

            X3dDomNode nd;
            nd.tagLower = std::move(tagLow);

            std::string_view attrSv = (ne < inner.size()) ? inner.substr(ne) : std::string_view{};
            size_t ap = 0;
            while (ap < attrSv.size()) {
                while (ap < attrSv.size() && (attrSv[ap] == ' ' || attrSv[ap] == '\t' || attrSv[ap] == '\r' || attrSv[ap] == '\n')) ++ap;
                if (ap >= attrSv.size()) break;
                size_t ks = ap;
                while (ap < attrSv.size() && attrSv[ap] != '=' && attrSv[ap] != ' ' && attrSv[ap] != '\t' && attrSv[ap] != '\r' && attrSv[ap] != '\n') ++ap;
                std::string kLow = ToLowerX3d(attrSv.substr(ks, ap - ks));
                while (ap < attrSv.size() && (attrSv[ap] == ' ' || attrSv[ap] == '\t' || attrSv[ap] == '\r' || attrSv[ap] == '\n')) ++ap;
                if (ap >= attrSv.size() || attrSv[ap] != '=') continue;
                ++ap;
                while (ap < attrSv.size() && (attrSv[ap] == ' ' || attrSv[ap] == '\t' || attrSv[ap] == '\r' || attrSv[ap] == '\n')) ++ap;
                if (ap >= attrSv.size() || (attrSv[ap] != '"' && attrSv[ap] != '\'')) continue;
                char aq = attrSv[ap++];
                size_t vs = ap;
                size_t ve = attrSv.find(aq, vs);
                if (ve == std::string_view::npos) break;
                ap = ve + 1;
                std::string_view valSlice = attrSv.substr(vs, ve - vs);
                if (kLow == "def") nd.defName.assign(valSlice);
                else if (kLow == "use") nd.useName.assign(valSlice);
                nd.attrs.emplace_back(std::move(kLow), valSlice);
            }

            int idx = static_cast<int>(nodes.size());
            if (!nd.defName.empty()) {
                defMap[nd.defName] = idx;
            }
            if (nd.tagLower == "x3d" && x3dRoot < 0) {
                x3dRoot = idx;
            }
            if (!stack.empty()) {
                nodes[stack.back()].children.push_back(idx);
            }
            nodes.push_back(std::move(nd));

            if (!isSelfClosing) {
                stack.push_back(idx);
                contentStartStack.push_back(pos);
            }
        }
        return x3dRoot >= 0;
    }

    const X3dDomNode* Resolve(int idx) const {
        for (int hop = 0; hop < 16 && idx >= 0 && static_cast<size_t>(idx) < nodes.size(); ++hop) {
            const X3dDomNode& nd = nodes[idx];
            if (nd.useName.empty()) return &nd;
            auto it = defMap.find(nd.useName);
            if (it == defMap.end()) return nullptr;
            idx = it->second;
        }
        return nullptr;
    }

    std::vector<Vec3f> ExtractCoordinates(const X3dDomNode& geom) const {
        for (int cIdx : geom.children) {
            const X3dDomNode* cn = Resolve(cIdx);
            if (!cn) continue;
            if (cn->tagLower == "coordinate" || cn->tagLower == "coordinatedouble") {
                std::string_view ptSv = cn->GetAttr("point");
                if (ptSv.empty()) ptSv = cn->textSlice;
                std::vector<float> coords;
                ParseX3dFloats(ptSv, coords);
                std::vector<Vec3f> pts;
                pts.reserve(coords.size() / 3);
                for (size_t i = 0; i + 2 < coords.size(); i += 3) {
                    pts.push_back({ coords[i], coords[i + 1], coords[i + 2] });
                }
                return pts;
            }
        }
        return {};
    }
};

inline void EmitX3dLocalTris(
    LoadedMesh3D& outMesh, const X3dMat4& mat,
    const std::vector<Vec3f>& localV,
    const std::vector<std::array<uint32_t, 3>>& localT,
    bool extraFlip = false)
{
    if (localV.empty() || localT.empty()) return;
    const bool flip = (mat.Det3x3() < 0.0f) ^ extraFlip;
    const uint32_t base = static_cast<uint32_t>(outMesh.vertices.size());
    outMesh.vertices.reserve(outMesh.vertices.size() + localV.size());
    for (const auto& v : localV) {
        outMesh.vertices.push_back(mat.TransformPoint(v));
    }
    outMesh.tris.reserve(outMesh.tris.size() + localT.size());
    for (const auto& t : localT) {
        if (!flip) outMesh.tris.push_back({ base + t[0], base + t[1], base + t[2], {} });
        else       outMesh.tris.push_back({ base + t[0], base + t[2], base + t[1], {} });
    }
}

} // namespace DetailX3d

inline bool ParseX3dMesh(const uint8_t* data, size_t size, LoadedMesh3D& outMesh) {
    if (!data || size < 12) return false;

    std::vector<uint8_t> inflated;
    const uint8_t* rawPtr = data;
    size_t rawLen = size;
    if (size >= 18 && data[0] == 0x1F && data[1] == 0x8B) {
        if (!ZipDeflate::InflateGzip(data, size, inflated, 128 * 1024 * 1024) || inflated.size() < 12) {
            return false;
        }
        rawPtr = inflated.data();
        rawLen = inflated.size();
    }

    std::string_view sv(reinterpret_cast<const char*>(rawPtr), rawLen);
    DetailX3d::X3dDom dom;
    if (!dom.Parse(sv)) {
        return false; // Strictly requires <X3D> root element!
    }

    outMesh.formatName = L"Extensible 3D (.x3d)";

    std::vector<std::pair<Vec3f, Vec3f>> worldLines;
    std::vector<Vec3f> worldPoints;

    auto emitGeometryNode = [&](const DetailX3d::X3dDomNode& gn, const DetailX3d::X3dMat4& mat) {
        const std::string& tp = gn.tagLower;

        if (tp == "indexedfaceset") {
            std::vector<Vec3f> pts = dom.ExtractCoordinates(gn);
            if (pts.empty()) return;
            std::vector<int64_t> idx;
            DetailX3d::ParseX3dInts(gn.GetAttr("coordindex"), idx);
            if (idx.empty()) return;
            const bool ccw = DetailX3d::ReadX3dBoolAttr(gn, "ccw", true);

            std::vector<std::array<uint32_t, 3>> lt;
            std::vector<uint32_t> face;
            auto flush = [&]() {
                if (face.size() >= 3) {
                    for (size_t k = 1; k + 1 < face.size(); ++k) {
                        lt.push_back({ face[0], face[k], face[k + 1] });
                    }
                }
                face.clear();
            };
            for (int64_t v : idx) {
                if (v < 0) flush();
                else if (static_cast<size_t>(v) < pts.size()) face.push_back(static_cast<uint32_t>(v));
            }
            flush();
            DetailX3d::EmitX3dLocalTris(outMesh, mat, pts, lt, !ccw);
        } else if (tp == "indexedtriangleset") {
            std::vector<Vec3f> pts = dom.ExtractCoordinates(gn);
            if (pts.empty()) return;
            std::vector<int64_t> idx;
            DetailX3d::ParseX3dInts(gn.GetAttr("index"), idx);
            const bool ccw = DetailX3d::ReadX3dBoolAttr(gn, "ccw", true);
            std::vector<std::array<uint32_t, 3>> lt;
            for (size_t i = 0; i + 2 < idx.size(); i += 3) {
                int64_t a = idx[i], b = idx[i + 1], c = idx[i + 2];
                if (a >= 0 && b >= 0 && c >= 0 &&
                    static_cast<size_t>(a) < pts.size() &&
                    static_cast<size_t>(b) < pts.size() &&
                    static_cast<size_t>(c) < pts.size()) {
                    lt.push_back({ static_cast<uint32_t>(a), static_cast<uint32_t>(b), static_cast<uint32_t>(c) });
                }
            }
            DetailX3d::EmitX3dLocalTris(outMesh, mat, pts, lt, !ccw);
        } else if (tp == "indexedtrianglestripset") {
            std::vector<Vec3f> pts = dom.ExtractCoordinates(gn);
            if (pts.empty()) return;
            std::vector<int64_t> idx;
            DetailX3d::ParseX3dInts(gn.GetAttr("index"), idx);
            const bool ccw = DetailX3d::ReadX3dBoolAttr(gn, "ccw", true);
            std::vector<std::array<uint32_t, 3>> lt;
            size_t sLen = 0;
            uint32_t s0 = 0, s1 = 0;
            for (int64_t v : idx) {
                if (v < 0 || static_cast<size_t>(v) >= pts.size()) {
                    sLen = 0;
                    continue;
                }
                uint32_t s2 = static_cast<uint32_t>(v);
                if (sLen >= 2 && s0 != s1 && s1 != s2 && s2 != s0) {
                    if (((sLen - 2) & 1u) == 0u) lt.push_back({ s0, s1, s2 });
                    else                         lt.push_back({ s1, s0, s2 });
                }
                s0 = s1; s1 = s2; ++sLen;
            }
            DetailX3d::EmitX3dLocalTris(outMesh, mat, pts, lt, !ccw);
        } else if (tp == "indexedtrianglefanset") {
            std::vector<Vec3f> pts = dom.ExtractCoordinates(gn);
            if (pts.empty()) return;
            std::vector<int64_t> idx;
            DetailX3d::ParseX3dInts(gn.GetAttr("index"), idx);
            const bool ccw = DetailX3d::ReadX3dBoolAttr(gn, "ccw", true);
            std::vector<std::array<uint32_t, 3>> lt;
            std::vector<uint32_t> fan;
            auto flushFan = [&]() {
                for (size_t k = 1; k + 1 < fan.size(); ++k) lt.push_back({ fan[0], fan[k], fan[k + 1] });
                fan.clear();
            };
            for (int64_t v : idx) {
                if (v < 0) flushFan();
                else if (static_cast<size_t>(v) < pts.size()) fan.push_back(static_cast<uint32_t>(v));
            }
            flushFan();
            DetailX3d::EmitX3dLocalTris(outMesh, mat, pts, lt, !ccw);
        } else if (tp == "indexedquadset") {
            std::vector<Vec3f> pts = dom.ExtractCoordinates(gn);
            if (pts.empty()) return;
            std::vector<int64_t> idx;
            DetailX3d::ParseX3dInts(gn.GetAttr("index"), idx);
            const bool ccw = DetailX3d::ReadX3dBoolAttr(gn, "ccw", true);
            std::vector<std::array<uint32_t, 3>> lt;
            for (size_t i = 0; i + 3 < idx.size(); i += 4) {
                int64_t a = idx[i], b = idx[i + 1], c = idx[i + 2], d = idx[i + 3];
                if (a >= 0 && b >= 0 && c >= 0 && d >= 0 &&
                    static_cast<size_t>(a) < pts.size() && static_cast<size_t>(b) < pts.size() &&
                    static_cast<size_t>(c) < pts.size() && static_cast<size_t>(d) < pts.size()) {
                    lt.push_back({ static_cast<uint32_t>(a), static_cast<uint32_t>(b), static_cast<uint32_t>(c) });
                    lt.push_back({ static_cast<uint32_t>(a), static_cast<uint32_t>(c), static_cast<uint32_t>(d) });
                }
            }
            DetailX3d::EmitX3dLocalTris(outMesh, mat, pts, lt, !ccw);
        } else if (tp == "triangleset") {
            std::vector<Vec3f> pts = dom.ExtractCoordinates(gn);
            const bool ccw = DetailX3d::ReadX3dBoolAttr(gn, "ccw", true);
            std::vector<std::array<uint32_t, 3>> lt;
            for (uint32_t i = 0; i + 2 < static_cast<uint32_t>(pts.size()); i += 3) {
                lt.push_back({ i, i + 1u, i + 2u });
            }
            DetailX3d::EmitX3dLocalTris(outMesh, mat, pts, lt, !ccw);
        } else if (tp == "trianglestripset") {
            std::vector<Vec3f> pts = dom.ExtractCoordinates(gn);
            const bool ccw = DetailX3d::ReadX3dBoolAttr(gn, "ccw", true);
            std::vector<int64_t> counts;
            DetailX3d::ParseX3dInts(gn.GetAttr("stripcount"), counts);
            if (counts.empty() && pts.size() >= 3) counts.push_back(static_cast<int64_t>(pts.size()));
            std::vector<std::array<uint32_t, 3>> lt;
            size_t vOff = 0;
            for (int64_t cnt : counts) {
                if (cnt < 3 || vOff + static_cast<size_t>(cnt) > pts.size()) break;
                for (size_t k = 0; k + 2 < static_cast<size_t>(cnt); ++k) {
                    uint32_t a = static_cast<uint32_t>(vOff + k);
                    if ((k & 1u) == 0u) lt.push_back({ a, a + 1u, a + 2u });
                    else                lt.push_back({ a + 1u, a, a + 2u });
                }
                vOff += static_cast<size_t>(cnt);
            }
            DetailX3d::EmitX3dLocalTris(outMesh, mat, pts, lt, !ccw);
        } else if (tp == "quadset") {
            std::vector<Vec3f> pts = dom.ExtractCoordinates(gn);
            const bool ccw = DetailX3d::ReadX3dBoolAttr(gn, "ccw", true);
            std::vector<std::array<uint32_t, 3>> lt;
            for (uint32_t i = 0; i + 3 < static_cast<uint32_t>(pts.size()); i += 4) {
                lt.push_back({ i, i + 1u, i + 2u });
                lt.push_back({ i, i + 2u, i + 3u });
            }
            DetailX3d::EmitX3dLocalTris(outMesh, mat, pts, lt, !ccw);
        } else if (tp == "indexedlineset") {
            std::vector<Vec3f> pts = dom.ExtractCoordinates(gn);
            std::vector<int64_t> idx;
            DetailX3d::ParseX3dInts(gn.GetAttr("coordindex"), idx);
            std::vector<uint32_t> poly;
            auto flushL = [&]() {
                for (size_t k = 0; k + 1 < poly.size(); ++k) {
                    worldLines.emplace_back(mat.TransformPoint(pts[poly[k]]), mat.TransformPoint(pts[poly[k + 1]]));
                }
                poly.clear();
            };
            for (int64_t v : idx) {
                if (v < 0) flushL();
                else if (static_cast<size_t>(v) < pts.size()) poly.push_back(static_cast<uint32_t>(v));
            }
            flushL();
        } else if (tp == "lineset") {
            std::vector<Vec3f> pts = dom.ExtractCoordinates(gn);
            std::vector<int64_t> counts;
            DetailX3d::ParseX3dInts(gn.GetAttr("vertexcount"), counts);
            size_t vOff = 0;
            for (int64_t cnt : counts) {
                if (cnt < 2 || vOff + static_cast<size_t>(cnt) > pts.size()) break;
                for (size_t k = 0; k + 1 < static_cast<size_t>(cnt); ++k) {
                    worldLines.emplace_back(mat.TransformPoint(pts[vOff + k]), mat.TransformPoint(pts[vOff + k + 1]));
                }
                vOff += static_cast<size_t>(cnt);
            }
        } else if (tp == "pointset") {
            std::vector<Vec3f> pts = dom.ExtractCoordinates(gn);
            for (const auto& p : pts) worldPoints.push_back(mat.TransformPoint(p));
        } else if (tp == "elevationgrid") {
            int64_t xDim = static_cast<int64_t>(DetailX3d::ReadX3dFloatAttr(gn, "xdimension", 0.0f));
            int64_t zDim = static_cast<int64_t>(DetailX3d::ReadX3dFloatAttr(gn, "zdimension", 0.0f));
            float xSp = DetailX3d::ReadX3dFloatAttr(gn, "xspacing", 1.0f);
            float zSp = DetailX3d::ReadX3dFloatAttr(gn, "zspacing", 1.0f);
            bool ccw = DetailX3d::ReadX3dBoolAttr(gn, "ccw", true);
            std::string_view hSv = gn.GetAttr("height");
            if (hSv.empty()) hSv = gn.textSlice;
            std::vector<float> hts;
            DetailX3d::ParseX3dFloats(hSv, hts);
            if (xDim >= 2 && zDim >= 2 && xDim <= 4096 && zDim <= 4096 &&
                hts.size() >= static_cast<size_t>(xDim * zDim))
            {
                size_t need = static_cast<size_t>(xDim * zDim);
                std::vector<Vec3f> lv(need);
                for (int64_t j = 0; j < zDim; ++j) {
                    for (int64_t i = 0; i < xDim; ++i) {
                        size_t k = static_cast<size_t>(j * xDim + i);
                        lv[k] = { static_cast<float>(i) * xSp, hts[k], static_cast<float>(j) * zSp };
                    }
                }
                std::vector<std::array<uint32_t, 3>> lt;
                for (int64_t j = 0; j + 1 < zDim; ++j) {
                    for (int64_t i = 0; i + 1 < xDim; ++i) {
                        uint32_t i00 = static_cast<uint32_t>(j * xDim + i);
                        uint32_t i10 = i00 + 1u;
                        uint32_t i01 = i00 + static_cast<uint32_t>(xDim);
                        uint32_t i11 = i01 + 1u;
                        lt.push_back({ i00, i01, i11 });
                        lt.push_back({ i00, i11, i10 });
                    }
                }
                DetailX3d::EmitX3dLocalTris(outMesh, mat, lv, lt, !ccw);
            }
        } else if (tp == "box") {
            float sz[3] = { 2.0f, 2.0f, 2.0f };
            DetailX3d::ReadX3dVecAttr(gn, "size", 3, sz);
            if (sz[0] > 0.0f && sz[1] > 0.0f && sz[2] > 0.0f) {
                std::vector<Vec3f> lv; std::vector<std::array<uint32_t, 3>> lt;
                DetailWrl::BuildWrlBox(sz[0], sz[1], sz[2], lv, lt);
                DetailX3d::EmitX3dLocalTris(outMesh, mat, lv, lt);
            }
        } else if (tp == "sphere") {
            float rad = DetailX3d::ReadX3dFloatAttr(gn, "radius", 1.0f);
            if (rad > 0.0f) {
                std::vector<Vec3f> lv; std::vector<std::array<uint32_t, 3>> lt;
                DetailWrl::BuildWrlSphere(rad, lv, lt);
                DetailX3d::EmitX3dLocalTris(outMesh, mat, lv, lt);
            }
        } else if (tp == "cylinder") {
            float rad = DetailX3d::ReadX3dFloatAttr(gn, "radius", 1.0f);
            float hgt = DetailX3d::ReadX3dFloatAttr(gn, "height", 2.0f);
            bool side = DetailX3d::ReadX3dBoolAttr(gn, "side", true);
            bool top  = DetailX3d::ReadX3dBoolAttr(gn, "top", true);
            bool bot  = DetailX3d::ReadX3dBoolAttr(gn, "bottom", true);
            if (rad > 0.0f && hgt > 0.0f) {
                std::vector<Vec3f> lv; std::vector<std::array<uint32_t, 3>> lt;
                DetailWrl::BuildWrlCylinder(rad, hgt, side, top, bot, lv, lt);
                DetailX3d::EmitX3dLocalTris(outMesh, mat, lv, lt);
            }
        } else if (tp == "cone") {
            float rad = DetailX3d::ReadX3dFloatAttr(gn, "bottomradius", 1.0f);
            float hgt = DetailX3d::ReadX3dFloatAttr(gn, "height", 2.0f);
            bool side = DetailX3d::ReadX3dBoolAttr(gn, "side", true);
            bool bot  = DetailX3d::ReadX3dBoolAttr(gn, "bottom", true);
            if (rad > 0.0f && hgt > 0.0f) {
                std::vector<Vec3f> lv; std::vector<std::array<uint32_t, 3>> lt;
                DetailWrl::BuildWrlCone(rad, hgt, side, bot, lv, lt);
                DetailX3d::EmitX3dLocalTris(outMesh, mat, lv, lt);
            }
        }
    };

    std::function<void(int, const DetailX3d::X3dMat4&, int)> walkScene = [&](int rawIdx, const DetailX3d::X3dMat4& curMat, int depth) {
        if (depth > 64) return;
        const DetailX3d::X3dDomNode* nd = dom.Resolve(rawIdx);
        if (!nd) return;
        const std::string& tp = nd->tagLower;

        if (tp == "transform") {
            float tr[3]    = { 0.0f, 0.0f, 0.0f };
            float rot[4]   = { 0.0f, 0.0f, 1.0f, 0.0f };
            float sc[3]    = { 1.0f, 1.0f, 1.0f };
            float scOri[4] = { 0.0f, 0.0f, 1.0f, 0.0f };
            float ctr[3]   = { 0.0f, 0.0f, 0.0f };
            DetailX3d::ReadX3dVecAttr(*nd, "translation", 3, tr);
            DetailX3d::ReadX3dVecAttr(*nd, "rotation", 4, rot);
            DetailX3d::ReadX3dVecAttr(*nd, "scale", 3, sc);
            DetailX3d::ReadX3dVecAttr(*nd, "scaleorientation", 4, scOri);
            DetailX3d::ReadX3dVecAttr(*nd, "center", 3, ctr);
            DetailX3d::X3dMat4 nextMat = DetailX3d::X3dMat4::Mul(
                curMat, DetailX3d::X3dMat4::ComposeTransform(tr, rot, sc, scOri, ctr));
            for (int cIdx : nd->children) walkScene(cIdx, nextMat, depth + 1);
            return;
        }

        if (tp == "switch") {
            int which = static_cast<int>(DetailX3d::ReadX3dFloatAttr(*nd, "whichchoice", -1.0f));
            if (which >= 0 && static_cast<size_t>(which) < nd->children.size()) {
                walkScene(nd->children[which], curMat, depth + 1);
            }
            return;
        }

        if (tp == "lod") {
            if (!nd->children.empty()) {
                walkScene(nd->children[0], curMat, depth + 1);
            }
            return;
        }

        if (tp == "shape") {
            for (int cIdx : nd->children) {
                const DetailX3d::X3dDomNode* cn = dom.Resolve(cIdx);
                if (!cn) continue;
                if (cn->tagLower == "appearance") {
                    for (int aIdx : cn->children) {
                        const DetailX3d::X3dDomNode* an = dom.Resolve(aIdx);
                        if (an && an->tagLower == "material") {
                            float diff[3] = { 0.8f, 0.8f, 0.8f };
                            DetailX3d::ReadX3dVecAttr(*an, "diffusecolor", 3, diff);
                            (void)diff;
                        }
                    }
                } else {
                    emitGeometryNode(*cn, curMat);
                }
            }
            return;
        }

        // Direct geometry node (e.g. USE of a DEF'd geometry or bare geometry inside Scene)
        emitGeometryNode(*nd, curMat);

        // Traverse children of <X3D>, <Scene>, <Group>, <StaticGroup>, <Collision>, <Anchor>, <Billboard>, CAD*
        if (tp == "x3d" || tp == "scene" || tp == "group" || tp == "staticgroup" ||
            tp == "collision" || tp == "anchor" || tp == "billboard" ||
            tp == "cadassembly" || tp == "cadpart" || tp == "cadlayer" || tp == "cadface") {
            for (int cIdx : nd->children) {
                walkScene(cIdx, curMat, depth + 1);
            }
        }
    };

    const DetailX3d::X3dMat4 identityMat;
    walkScene(dom.x3dRoot, identityMat, 0);

    if (!worldLines.empty()) {
        Vec3f bmin = worldLines[0].first, bmax = bmin;
        for (const auto& seg : worldLines) {
            for (const Vec3f& p : { seg.first, seg.second }) {
                bmin.x = std::min(bmin.x, p.x); bmin.y = std::min(bmin.y, p.y); bmin.z = std::min(bmin.z, p.z);
                bmax.x = std::max(bmax.x, p.x); bmax.y = std::max(bmax.y, p.y); bmax.z = std::max(bmax.z, p.z);
            }
        }
        float span = std::max({ bmax.x - bmin.x, bmax.y - bmin.y, bmax.z - bmin.z, 1e-4f });
        float rad = span * 0.008f;
        for (const auto& seg : worldLines) {
            DetailWrl::EmitWrlStrut(outMesh, seg.first, seg.second, rad);
        }
    }

    if (!worldPoints.empty()) {
        constexpr size_t kMaxPts = 25000;
        const size_t step = std::max<size_t>(1, worldPoints.size() / kMaxPts);
        Vec3f bmin = worldPoints[0], bmax = bmin;
        for (const auto& p : worldPoints) {
            bmin.x = std::min(bmin.x, p.x); bmin.y = std::min(bmin.y, p.y); bmin.z = std::min(bmin.z, p.z);
            bmax.x = std::max(bmax.x, p.x); bmax.y = std::max(bmax.y, p.y); bmax.z = std::max(bmax.z, p.z);
        }
        float span = std::max({ bmax.x - bmin.x, bmax.y - bmin.y, bmax.z - bmin.z });
        float r = std::max(0.015f, (span > 1e-5f) ? (span * 0.018f) : 0.02f);
        for (size_t i = 0; i < worldPoints.size() && i / step < kMaxPts; i += step) {
            const Vec3f& p = worldPoints[i];
            uint32_t b = static_cast<uint32_t>(outMesh.vertices.size());
            outMesh.vertices.push_back({ p.x,     p.y + r, p.z     });
            outMesh.vertices.push_back({ p.x + r, p.y - r, p.z + r });
            outMesh.vertices.push_back({ p.x - r, p.y - r, p.z + r });
            outMesh.vertices.push_back({ p.x,     p.y - r, p.z - r });
            outMesh.tris.push_back({ b + 0, b + 1, b + 2, {} });
            outMesh.tris.push_back({ b + 0, b + 2, b + 3, {} });
            outMesh.tris.push_back({ b + 0, b + 3, b + 1, {} });
            outMesh.tris.push_back({ b + 1, b + 3, b + 2, {} });
        }
    }

    return !outMesh.vertices.empty() && !outMesh.tris.empty();
}

inline bool ParseX3dMesh(std::string_view sv, LoadedMesh3D& outMesh) {
    return ParseX3dMesh(reinterpret_cast<const uint8_t*>(sv.data()), sv.size(), outMesh);
}

} // namespace Formats
