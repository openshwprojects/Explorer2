#pragma once
#include "../common/MeshBuild3D.h"
// Wrl.h — VRML 1.0 & VRML 2.0 / VRML97 (.wrl, plain & gzipped) Scene-Graph 3D Parser
// Supports #VRML V2.0 utf8, #VRML V1.0 ascii, and #Inventor V2.x, gzip inflation (0x1F 0x8B),
// DEF / USE instancing, 4x4 Transform & VRML 1.0 Separator matrix stacks, IndexedFaceSet,
// IndexedLineSet, PointSet, ElevationGrid, Box/Cube, Sphere, Cylinder, Cone, Material & Color.

namespace Formats {

namespace DetailWrl {

struct WrlMat4 {
    float m[4][4] = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f }
    };

    static WrlMat4 Identity() { return WrlMat4{}; }

    static WrlMat4 Mul(const WrlMat4& a, const WrlMat4& b) {
        WrlMat4 r;
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

    static WrlMat4 Translate(float tx, float ty, float tz) {
        WrlMat4 r;
        r.m[0][3] = tx; r.m[1][3] = ty; r.m[2][3] = tz;
        return r;
    }

    static WrlMat4 Scale(float sx, float sy, float sz) {
        WrlMat4 r;
        r.m[0][0] = sx; r.m[1][1] = sy; r.m[2][2] = sz;
        return r;
    }

    static WrlMat4 RotateAxisAngle(float ax, float ay, float az, float angleRad) {
        WrlMat4 r;
        float len = std::sqrt(ax * ax + ay * ay + az * az);
        if (len < 1e-7f || std::fabs(angleRad) < 1e-8f) return r;
        float x = ax / len, y = ay / len, z = az / len;
        float c = std::cos(angleRad);
        float s = std::sin(angleRad);
        float t = 1.0f - c;
        r.m[0][0] = t * x * x + c;     r.m[0][1] = t * x * y - s * z; r.m[0][2] = t * x * z + s * y;
        r.m[1][0] = t * x * y + s * z; r.m[1][1] = t * y * y + c;     r.m[1][2] = t * y * z - s * x;
        r.m[2][0] = t * x * z - s * y; r.m[2][1] = t * y * z + s * x; r.m[2][2] = t * z * z + c;
        return r;
    }

    static WrlMat4 ComposeTransform(
        const float tr[3], const float rot[4], const float sc[3],
        const float scOri[4], const float ctr[3])
    {
        // ISO/IEC 14772 VRML97 Transform: T * C * R * SR * S * (-SR) * (-C)
        WrlMat4 m = Translate(tr[0] + ctr[0], tr[1] + ctr[1], tr[2] + ctr[2]);
        m = Mul(m, RotateAxisAngle(rot[0], rot[1], rot[2], rot[3]));
        const bool hasScaleOri = (std::fabs(scOri[3]) > 1e-6f);
        if (hasScaleOri) m = Mul(m, RotateAxisAngle(scOri[0], scOri[1], scOri[2], scOri[3]));
        m = Mul(m, Scale(sc[0], sc[1], sc[2]));
        if (hasScaleOri) m = Mul(m, RotateAxisAngle(scOri[0], scOri[1], scOri[2], -scOri[3]));
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

enum class WrlTokKind : uint8_t {
    End = 0,
    LBrace,
    RBrace,
    LBracket,
    RBracket,
    String,
    Word
};

struct WrlTok {
    WrlTokKind kind = WrlTokKind::End;
    std::string_view text;
};

struct WrlLexer {
    const char* cur = nullptr;
    const char* end = nullptr;
    bool hasPeek = false;
    WrlTok peeked;

    WrlLexer(std::string_view sv) : cur(sv.data()), end(sv.data() + sv.size()) {}

    WrlTok ReadRaw() {
        while (cur < end) {
            char c = *cur;
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',') {
                ++cur;
                continue;
            }
            if (c == '#') {
                while (cur < end && *cur != '\n') ++cur;
                continue;
            }
            break;
        }
        if (cur >= end) return { WrlTokKind::End, {} };

        char c = *cur;
        if (c == '{') { ++cur; return { WrlTokKind::LBrace, std::string_view(cur - 1, 1) }; }
        if (c == '}') { ++cur; return { WrlTokKind::RBrace, std::string_view(cur - 1, 1) }; }
        if (c == '[') { ++cur; return { WrlTokKind::LBracket, std::string_view(cur - 1, 1) }; }
        if (c == ']') { ++cur; return { WrlTokKind::RBracket, std::string_view(cur - 1, 1) }; }
        if (c == '"') {
            ++cur;
            const char* s = cur;
            while (cur < end && *cur != '"') {
                if (*cur == '\\' && cur + 1 < end) cur += 2;
                else ++cur;
            }
            std::string_view sv(s, static_cast<size_t>(cur - s));
            if (cur < end && *cur == '"') ++cur;
            return { WrlTokKind::String, sv };
        }

        const char* s = cur;
        while (cur < end) {
            char ch = *cur;
            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == ',' ||
                ch == '{' || ch == '}' || ch == '[' || ch == ']' || ch == '"' || ch == '#') {
                break;
            }
            ++cur;
        }
        return { WrlTokKind::Word, std::string_view(s, static_cast<size_t>(cur - s)) };
    }

    WrlTok Next() {
        if (hasPeek) {
            hasPeek = false;
            return peeked;
        }
        return ReadRaw();
    }

    WrlTok Peek() {
        if (!hasPeek) {
            peeked = ReadRaw();
            hasPeek = true;
        }
        return peeked;
    }
};

inline bool TryParseWrlDouble(std::string_view sv, double& out) {
    if (sv.empty() || sv.size() >= 64) return false;
    char c0 = sv.front();
    if (!((c0 >= '0' && c0 <= '9') || c0 == '-' || c0 == '+' || c0 == '.')) return false;
    char buf[64];
    std::memcpy(buf, sv.data(), sv.size());
    buf[sv.size()] = '\0';
    char* next = nullptr;
    out = std::strtod(buf, &next);
    return next == buf + sv.size() && std::isfinite(out);
}

struct WrlField {
    std::string name;
    std::vector<double> numbers;
    std::vector<std::string> words;
    std::vector<int> childNodes;
};

struct WrlNode {
    std::string typeName;
    std::string defName;
    std::string useName; // non-empty => USE reference
    std::vector<WrlField> fields;
    std::vector<int> directChildren; // VRML 1.0 child nodes directly inside Separator/Group { ... }

    const WrlField* FindField(std::string_view fname) const {
        for (const auto& f : fields) {
            if (f.name == fname) return &f;
        }
        return nullptr;
    }
};

struct WrlAst {
    std::vector<WrlNode> nodes;
    std::unordered_map<std::string, int> defMap;
    std::vector<int> rootNodes;

    void SkipBalanced(WrlLexer& lex, WrlTokKind openK, WrlTokKind closeK) {
        int depth = 1;
        while (depth > 0) {
            WrlTok t = lex.Next();
            if (t.kind == WrlTokKind::End) break;
            if (t.kind == openK) ++depth;
            else if (t.kind == closeK) --depth;
        }
    }

    // Parses a single node ("DEF id Type { ... }", "USE id", or "Type { ... }"); returns node index or -1
    int ParseNode(WrlLexer& lex, std::string pendingDef = {}) {
        WrlTok t = lex.Next();
        if (t.kind != WrlTokKind::Word) return -1;

        if (t.text == "DEF") {
            WrlTok nameTok = lex.Next();
            if (nameTok.kind != WrlTokKind::Word) return -1;
            return ParseNode(lex, std::string(nameTok.text));
        }
        if (t.text == "USE") {
            WrlTok targetTok = lex.Next();
            if (targetTok.kind != WrlTokKind::Word) return -1;
            int idx = static_cast<int>(nodes.size());
            WrlNode un;
            un.useName.assign(targetTok.text);
            nodes.push_back(std::move(un));
            return idx;
        }
        if (t.text == "PROTO" || t.text == "EXTERNPROTO") {
            // Skip PROTO Name [ ... ] { ... } or EXTERNPROTO Name [ ... ] [ ... ]
            (void)lex.Next(); // proto name
            if (lex.Peek().kind == WrlTokKind::LBracket) {
                lex.Next();
                SkipBalanced(lex, WrlTokKind::LBracket, WrlTokKind::RBracket);
            }
            if (t.text == "PROTO") {
                if (lex.Peek().kind == WrlTokKind::LBrace) {
                    lex.Next();
                    SkipBalanced(lex, WrlTokKind::LBrace, WrlTokKind::RBrace);
                }
            } else {
                if (lex.Peek().kind == WrlTokKind::LBracket) {
                    lex.Next();
                    SkipBalanced(lex, WrlTokKind::LBracket, WrlTokKind::RBracket);
                } else if (lex.Peek().kind == WrlTokKind::String) {
                    lex.Next();
                }
            }
            return -2; // skipped declaration
        }
        if (t.text == "ROUTE") {
            // ROUTE a.f TO b.g
            (void)lex.Next(); (void)lex.Next(); (void)lex.Next();
            return -2;
        }
        if (t.text == "NULL") {
            return -2;
        }

        // Must be <NodeType> { ... }
        if (lex.Peek().kind != WrlTokKind::LBrace) {
            return -1;
        }
        lex.Next(); // consume '{'

        int nodeIdx = static_cast<int>(nodes.size());
        WrlNode nd;
        nd.typeName.assign(t.text);
        nd.defName = std::move(pendingDef);
        nodes.push_back(std::move(nd));
        if (!nodes[nodeIdx].defName.empty()) {
            defMap[nodes[nodeIdx].defName] = nodeIdx;
        }

        if (nodes[nodeIdx].typeName == "Script") {
            SkipBalanced(lex, WrlTokKind::LBrace, WrlTokKind::RBrace);
            return nodeIdx;
        }

        while (true) {
            WrlTok pk = lex.Peek();
            if (pk.kind == WrlTokKind::End) break;
            if (pk.kind == WrlTokKind::RBrace) {
                lex.Next();
                break;
            }
            if (pk.kind != WrlTokKind::Word) {
                // Unexpected stray bracket/string inside node body; skip safely
                if (pk.kind == WrlTokKind::LBrace) { lex.Next(); SkipBalanced(lex, WrlTokKind::LBrace, WrlTokKind::RBrace); }
                else if (pk.kind == WrlTokKind::LBracket) { lex.Next(); SkipBalanced(lex, WrlTokKind::LBracket, WrlTokKind::RBracket); }
                else lex.Next();
                continue;
            }

            if (pk.text == "DEF" || pk.text == "USE" || pk.text == "ROUTE" || pk.text == "PROTO" || pk.text == "EXTERNPROTO") {
                int cIdx = ParseNode(lex);
                if (cIdx >= 0) nodes[nodeIdx].directChildren.push_back(cIdx);
                continue;
            }

            WrlTok firstWord = lex.Next();
            // VRML 1.0 direct child node: <NodeType> { ... }
            if (lex.Peek().kind == WrlTokKind::LBrace) {
                lex.Next(); // consume '{'
                int childIdx = ParseInlineNodeBody(lex, std::string(firstWord.text));
                if (childIdx >= 0) nodes[nodeIdx].directChildren.push_back(childIdx);
                continue;
            }

            // Otherwise firstWord is a field name!
            WrlField fld;
            fld.name.assign(firstWord.text);
            ParseFieldValue(lex, fld);
            nodes[nodeIdx].fields.push_back(std::move(fld));
        }

        return nodeIdx;
    }

    int ParseInlineNodeBody(WrlLexer& lex, std::string typeName) {
        int nodeIdx = static_cast<int>(nodes.size());
        WrlNode nd;
        nd.typeName = std::move(typeName);
        nodes.push_back(std::move(nd));

        if (nodes[nodeIdx].typeName == "Script") {
            SkipBalanced(lex, WrlTokKind::LBrace, WrlTokKind::RBrace);
            return nodeIdx;
        }

        while (true) {
            WrlTok pk = lex.Peek();
            if (pk.kind == WrlTokKind::End) break;
            if (pk.kind == WrlTokKind::RBrace) {
                lex.Next();
                break;
            }
            if (pk.kind != WrlTokKind::Word) {
                if (pk.kind == WrlTokKind::LBrace) { lex.Next(); SkipBalanced(lex, WrlTokKind::LBrace, WrlTokKind::RBrace); }
                else if (pk.kind == WrlTokKind::LBracket) { lex.Next(); SkipBalanced(lex, WrlTokKind::LBracket, WrlTokKind::RBracket); }
                else lex.Next();
                continue;
            }
            if (pk.text == "DEF" || pk.text == "USE" || pk.text == "ROUTE" || pk.text == "PROTO" || pk.text == "EXTERNPROTO") {
                int cIdx = ParseNode(lex);
                if (cIdx >= 0) nodes[nodeIdx].directChildren.push_back(cIdx);
                continue;
            }
            WrlTok fw = lex.Next();
            if (lex.Peek().kind == WrlTokKind::LBrace) {
                lex.Next();
                int childIdx = ParseInlineNodeBody(lex, std::string(fw.text));
                if (childIdx >= 0) nodes[nodeIdx].directChildren.push_back(childIdx);
                continue;
            }
            WrlField fld;
            fld.name.assign(fw.text);
            ParseFieldValue(lex, fld);
            nodes[nodeIdx].fields.push_back(std::move(fld));
        }
        return nodeIdx;
    }

    void ParseFieldValue(WrlLexer& lex, WrlField& fld) {
        WrlTok pk = lex.Peek();
        if (pk.kind == WrlTokKind::Word && pk.text == "IS") {
            lex.Next();
            (void)lex.Next();
            return;
        }
        if (pk.kind == WrlTokKind::LBracket) {
            lex.Next(); // consume '['
            while (true) {
                WrlTok bp = lex.Peek();
                if (bp.kind == WrlTokKind::End) break;
                if (bp.kind == WrlTokKind::RBracket) {
                    lex.Next();
                    break;
                }
                if (bp.kind == WrlTokKind::String) {
                    fld.words.emplace_back(lex.Next().text);
                    continue;
                }
                if (bp.kind == WrlTokKind::Word) {
                    double num = 0.0;
                    if (TryParseWrlDouble(bp.text, num)) {
                        lex.Next();
                        fld.numbers.push_back(num);
                        continue;
                    }
                    if (bp.text == "DEF" || bp.text == "USE" || bp.text == "NULL") {
                        int cIdx = ParseNode(lex);
                        if (cIdx >= 0) fld.childNodes.push_back(cIdx);
                        continue;
                    }
                    WrlTok w = lex.Next();
                    if (lex.Peek().kind == WrlTokKind::LBrace) {
                        lex.Next();
                        int cIdx = ParseInlineNodeBody(lex, std::string(w.text));
                        if (cIdx >= 0) fld.childNodes.push_back(cIdx);
                    } else {
                        fld.words.emplace_back(w.text);
                    }
                    continue;
                }
                lex.Next();
            }
            return;
        }

        // Single-value field: SFNode (DEF/USE/NodeType{...}/NULL), SFString, SFBool/Enum, or 1..16 numbers (SFFloat/SFVec3f/SFRotation/SFMatrix)
        if (pk.kind == WrlTokKind::String) {
            fld.words.emplace_back(lex.Next().text);
            return;
        }
        if (pk.kind == WrlTokKind::Word) {
            if (pk.text == "DEF" || pk.text == "USE") {
                int cIdx = ParseNode(lex);
                if (cIdx >= 0) fld.childNodes.push_back(cIdx);
                return;
            }
            if (pk.text == "NULL") {
                lex.Next();
                return;
            }
            double num = 0.0;
            if (TryParseWrlDouble(pk.text, num)) {
                while (lex.Peek().kind == WrlTokKind::Word && TryParseWrlDouble(lex.Peek().text, num)) {
                    lex.Next();
                    fld.numbers.push_back(num);
                }
                return;
            }
            WrlTok w = lex.Next();
            if (lex.Peek().kind == WrlTokKind::LBrace) {
                lex.Next();
                int cIdx = ParseInlineNodeBody(lex, std::string(w.text));
                if (cIdx >= 0) fld.childNodes.push_back(cIdx);
            } else {
                fld.words.emplace_back(w.text);
                // VRML 1.0 BitMask e.g. "(SIDES | BOTTOM)" or "SIDES"
                while (lex.Peek().kind == WrlTokKind::Word && lex.Peek().text == "|") {
                    lex.Next();
                    if (lex.Peek().kind == WrlTokKind::Word) fld.words.emplace_back(lex.Next().text);
                }
            }
        }
    }

    const WrlNode* ResolveNode(int idx) const {
        for (int hop = 0; hop < 16 && idx >= 0 && static_cast<size_t>(idx) < nodes.size(); ++hop) {
            const WrlNode& n = nodes[idx];
            if (n.useName.empty()) return &n;
            auto it = defMap.find(n.useName);
            if (it == defMap.end()) return nullptr;
            idx = it->second;
        }
        return nullptr;
    }
};

inline void EmitWrlLocalTris(
    LoadedMesh3D& outMesh, const WrlMat4& mat,
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

inline void BuildWrlBox(float sx, float sy, float sz, std::vector<Vec3f>& v, std::vector<std::array<uint32_t, 3>>& t) {
    const float hx = sx * 0.5f, hy = sy * 0.5f, hz = sz * 0.5f;
    v = {
        { -hx, -hy, -hz }, {  hx, -hy, -hz }, {  hx,  hy, -hz }, { -hx,  hy, -hz },
        { -hx, -hy,  hz }, {  hx, -hy,  hz }, {  hx,  hy,  hz }, { -hx,  hy,  hz }
    };
    t = {
        {{0, 2, 1}}, {{0, 3, 2}},
        {{4, 5, 6}}, {{4, 6, 7}},
        {{0, 1, 5}}, {{0, 5, 4}},
        {{1, 2, 6}}, {{1, 6, 5}},
        {{2, 3, 7}}, {{2, 7, 6}},
        {{3, 0, 4}}, {{3, 4, 7}}
    };
}

inline void BuildWrlSphere(float rad, std::vector<Vec3f>& v, std::vector<std::array<uint32_t, 3>>& t) {
    constexpr int kLat = 10, kLon = 16;
    v.push_back({ 0.0f, -rad, 0.0f });
    for (int il = 1; il < kLat; ++il) {
        float phi = -1.570796327f + static_cast<float>(il) * (3.141592654f / static_cast<float>(kLat));
        float cy = rad * std::sin(phi), cr = rad * std::cos(phi);
        for (int io = 0; io < kLon; ++io) {
            float th = static_cast<float>(io) * (6.283185307f / static_cast<float>(kLon));
            v.push_back({ cr * std::cos(th), cy, cr * std::sin(th) });
        }
    }
    const uint32_t topIdx = static_cast<uint32_t>(v.size());
    v.push_back({ 0.0f, rad, 0.0f });

    for (int io = 0; io < kLon; ++io) {
        uint32_t c0 = 1u + static_cast<uint32_t>(io);
        uint32_t c1 = 1u + static_cast<uint32_t>((io + 1) % kLon);
        t.push_back({ 0u, c0, c1 });
    }
    for (int il = 0; il + 2 < kLat; ++il) {
        uint32_t rA = 1u + static_cast<uint32_t>(il * kLon);
        uint32_t rB = rA + static_cast<uint32_t>(kLon);
        for (int io = 0; io < kLon; ++io) {
            uint32_t no = static_cast<uint32_t>((io + 1) % kLon);
            t.push_back({ rA + static_cast<uint32_t>(io), rB + no, rA + no });
            t.push_back({ rA + static_cast<uint32_t>(io), rB + static_cast<uint32_t>(io), rB + no });
        }
    }
    uint32_t topR = 1u + static_cast<uint32_t>((kLat - 2) * kLon);
    for (int io = 0; io < kLon; ++io) {
        uint32_t c0 = topR + static_cast<uint32_t>(io);
        uint32_t c1 = topR + static_cast<uint32_t>((io + 1) % kLon);
        t.push_back({ topIdx, c1, c0 });
    }
}

inline void BuildWrlCylinder(float rad, float height, bool hasSide, bool hasTop, bool hasBot,
                             std::vector<Vec3f>& v, std::vector<std::array<uint32_t, 3>>& t)
{
    constexpr int kSegs = 18;
    const float hy = height * 0.5f;
    for (int i = 0; i < kSegs; ++i) {
        float ang = static_cast<float>(i) * (6.283185307f / static_cast<float>(kSegs));
        float cx = rad * std::cos(ang), cz = rad * std::sin(ang);
        v.push_back({ cx, -hy, cz });
        v.push_back({ cx,  hy, cz });
    }
    const uint32_t botC = static_cast<uint32_t>(v.size());
    v.push_back({ 0.0f, -hy, 0.0f });
    const uint32_t topC = static_cast<uint32_t>(v.size());
    v.push_back({ 0.0f,  hy, 0.0f });

    for (int i = 0; i < kSegs; ++i) {
        int ni = (i + 1) % kSegs;
        uint32_t b0 = static_cast<uint32_t>(i * 2), t0 = b0 + 1;
        uint32_t b1 = static_cast<uint32_t>(ni * 2), t1 = b1 + 1;
        if (hasSide) {
            t.push_back({ b0, t1, b1 });
            t.push_back({ b0, t0, t1 });
        }
        if (hasBot) t.push_back({ botC, b0, b1 });
        if (hasTop) t.push_back({ topC, t1, t0 });
    }
}

inline void BuildWrlCone(float botRad, float height, bool hasSide, bool hasBot,
                         std::vector<Vec3f>& v, std::vector<std::array<uint32_t, 3>>& t)
{
    constexpr int kSegs = 18;
    const float hy = height * 0.5f;
    for (int i = 0; i < kSegs; ++i) {
        float ang = static_cast<float>(i) * (6.283185307f / static_cast<float>(kSegs));
        v.push_back({ botRad * std::cos(ang), -hy, botRad * std::sin(ang) });
    }
    const uint32_t apex = static_cast<uint32_t>(v.size());
    v.push_back({ 0.0f, hy, 0.0f });
    const uint32_t botC = static_cast<uint32_t>(v.size());
    v.push_back({ 0.0f, -hy, 0.0f });

    for (int i = 0; i < kSegs; ++i) {
        uint32_t b0 = static_cast<uint32_t>(i);
        uint32_t b1 = static_cast<uint32_t>((i + 1) % kSegs);
        if (hasSide) t.push_back({ b0, apex, b1 });
        if (hasBot)  t.push_back({ botC, b0, b1 });
    }
}

inline std::vector<Vec3f> ExtractPointsFromCoordNode(const WrlAst& ast, const WrlNode* geomNode, const std::vector<Vec3f>& vrml1InheritedCoords) {
    if (const WrlField* cf = geomNode->FindField("coord")) {
        for (int cIdx : cf->childNodes) {
            if (const WrlNode* cn = ast.ResolveNode(cIdx)) {
                if (const WrlField* pf = cn->FindField("point")) {
                    std::vector<Vec3f> pts;
                    pts.reserve(pf->numbers.size() / 3);
                    for (size_t i = 0; i + 2 < pf->numbers.size(); i += 3) {
                        pts.push_back({
                            static_cast<float>(pf->numbers[i]),
                            static_cast<float>(pf->numbers[i + 1]),
                            static_cast<float>(pf->numbers[i + 2])
                        });
                    }
                    return pts;
                }
            }
        }
    }
    return vrml1InheritedCoords;
}

struct WrlState {
    WrlMat4 mat;
    std::vector<Vec3f> coords3; // VRML 1.0 stateful Coordinate3
    float diffuse[3] = { 0.8f, 0.8f, 0.8f };
};

} // namespace DetailWrl

inline bool ParseWrlMesh(const uint8_t* data, size_t size, LoadedMesh3D& outMesh) {
    if (!data || size < 10) return false;

    std::vector<uint8_t> inflated;
    const uint8_t* rawPtr = data;
    size_t rawLen = size;
    bool wasGzip = false;
    if (size >= 18 && data[0] == 0x1F && data[1] == 0x8B) {
        if (!ZipDeflate::InflateGzip(data, size, inflated, 128 * 1024 * 1024) || inflated.size() < 10) {
            return false;
        }
        rawPtr = inflated.data();
        rawLen = inflated.size();
        wasGzip = true;
    }

    std::string_view sv(reinterpret_cast<const char*>(rawPtr), rawLen);
    size_t bomSkip = 0;
    if (sv.size() >= 3 &&
        static_cast<uint8_t>(sv[0]) == 0xEF &&
        static_cast<uint8_t>(sv[1]) == 0xBB &&
        static_cast<uint8_t>(sv[2]) == 0xBF) {
        bomSkip = 3;
    }
    while (bomSkip < sv.size() && (sv[bomSkip] == ' ' || sv[bomSkip] == '\t' || sv[bomSkip] == '\r' || sv[bomSkip] == '\n')) {
        ++bomSkip;
    }
    std::string_view head = sv.substr(bomSkip, std::min<size_t>(256, sv.size() - bomSkip));
    const bool isVrml2 = (head.rfind("#VRML V2.", 0) == 0);
    const bool isVrml1 = (head.rfind("#VRML V1.", 0) == 0 || head.rfind("#Inventor V", 0) == 0);
    if (!isVrml2 && !isVrml1) {
        return false; // Reject files missing #VRML / #Inventor header!
    }

    outMesh.formatName = isVrml1
        ? (wasGzip ? L"VRML 1.0 3D (.wrl.gz)" : L"VRML 1.0 3D (.wrl)")
        : (wasGzip ? L"VRML97 2.0 3D (.wrl.gz)" : L"VRML 2.0 3D (.wrl)");

    DetailWrl::WrlLexer lex(sv.substr(bomSkip));
    DetailWrl::WrlAst ast;
    while (lex.Peek().kind != DetailWrl::WrlTokKind::End) {
        int nIdx = ast.ParseNode(lex);
        if (nIdx >= 0) {
            ast.rootNodes.push_back(nIdx);
        } else if (nIdx == -1) {
            // Skip unrecognized top-level token safely
            lex.Next();
        }
    }

    if (ast.rootNodes.empty()) return false;

    std::vector<std::pair<Vec3f, Vec3f>> worldLineSegs;
    std::vector<Vec3f> worldPoints;

    auto readVec3Field = [](const DetailWrl::WrlNode& nd, std::string_view fn, float dx, float dy, float dz, float out[3]) {
        out[0] = dx; out[1] = dy; out[2] = dz;
        if (const auto* f = nd.FindField(fn); f && f->numbers.size() >= 3) {
            out[0] = static_cast<float>(f->numbers[0]);
            out[1] = static_cast<float>(f->numbers[1]);
            out[2] = static_cast<float>(f->numbers[2]);
        }
    };
    auto readRotField = [](const DetailWrl::WrlNode& nd, std::string_view fn, float out[4]) {
        out[0] = 0.0f; out[1] = 0.0f; out[2] = 1.0f; out[3] = 0.0f;
        if (const auto* f = nd.FindField(fn); f && f->numbers.size() >= 4) {
            out[0] = static_cast<float>(f->numbers[0]);
            out[1] = static_cast<float>(f->numbers[1]);
            out[2] = static_cast<float>(f->numbers[2]);
            out[3] = static_cast<float>(f->numbers[3]);
        }
    };
    auto readFloatField = [](const DetailWrl::WrlNode& nd, std::string_view fn, float defV) -> float {
        if (const auto* f = nd.FindField(fn); f && !f->numbers.empty()) {
            return static_cast<float>(f->numbers[0]);
        }
        return defV;
    };
    auto readBoolField = [](const DetailWrl::WrlNode& nd, std::string_view fn, bool defV) -> bool {
        if (const auto* f = nd.FindField(fn)) {
            if (!f->words.empty()) {
                if (f->words[0] == "FALSE" || f->words[0] == "false") return false;
                if (f->words[0] == "TRUE" || f->words[0] == "true") return true;
            }
            if (!f->numbers.empty()) return f->numbers[0] != 0.0;
        }
        return defV;
    };

    std::function<void(int, DetailWrl::WrlState&, int)> evalNode = [&](int rawIdx, DetailWrl::WrlState& state, int depth) {
        if (depth > 64) return;
        const DetailWrl::WrlNode* ndPtr = ast.ResolveNode(rawIdx);
        if (!ndPtr) return;
        const DetailWrl::WrlNode& nd = *ndPtr;
        const std::string& tp = nd.typeName;

        // 1. Grouping & Transform Nodes (VRML97 & VRML 1.0)
        if (tp == "Transform" && (!nd.FindField("scaleFactor") && (isVrml2 || nd.FindField("children")))) {
            // VRML97 Transform { translation, rotation, scale, scaleOrientation, center, children [...] }
            float tr[3], rot[4], sc[3], scOri[4], ctr[3];
            readVec3Field(nd, "translation", 0.0f, 0.0f, 0.0f, tr);
            readRotField(nd, "rotation", rot);
            readVec3Field(nd, "scale", 1.0f, 1.0f, 1.0f, sc);
            readRotField(nd, "scaleOrientation", scOri);
            readVec3Field(nd, "center", 0.0f, 0.0f, 0.0f, ctr);

            DetailWrl::WrlState childState = state;
            childState.mat = DetailWrl::WrlMat4::Mul(state.mat, DetailWrl::WrlMat4::ComposeTransform(tr, rot, sc, scOri, ctr));
            if (const auto* cf = nd.FindField("children")) {
                for (int cIdx : cf->childNodes) evalNode(cIdx, childState, depth + 1);
            }
            for (int cIdx : nd.directChildren) evalNode(cIdx, childState, depth + 1);
            return;
        }
        if (tp == "Separator" || tp == "WWWAnchor" || tp == "LOD") {
            DetailWrl::WrlState subState = state;
            if (tp == "LOD") {
                if (const auto* lf = nd.FindField("level"); lf && !lf->childNodes.empty()) {
                    evalNode(lf->childNodes[0], subState, depth + 1);
                    return;
                }
                if (!nd.directChildren.empty()) {
                    evalNode(nd.directChildren[0], subState, depth + 1);
                    return;
                }
            }
            if (const auto* cf = nd.FindField("children")) {
                for (int cIdx : cf->childNodes) evalNode(cIdx, subState, depth + 1);
            }
            for (int cIdx : nd.directChildren) evalNode(cIdx, subState, depth + 1);
            return;
        }
        if (tp == "TransformSeparator") {
            DetailWrl::WrlMat4 savedMat = state.mat;
            for (int cIdx : nd.directChildren) evalNode(cIdx, state, depth + 1);
            state.mat = savedMat;
            return;
        }
        if (tp == "Group" || tp == "Collision" || tp == "Anchor" || tp == "Billboard") {
            if (const auto* cf = nd.FindField("children")) {
                for (int cIdx : cf->childNodes) evalNode(cIdx, state, depth + 1);
            }
            for (int cIdx : nd.directChildren) evalNode(cIdx, state, depth + 1);
            return;
        }
        if (tp == "Switch") {
            int which = static_cast<int>(readFloatField(nd, isVrml1 ? "whichChild" : "whichChoice", -1.0f));
            const auto* cf = nd.FindField("choice");
            if (!cf) cf = nd.FindField("children");
            if (cf && !cf->childNodes.empty()) {
                if (which >= 0 && static_cast<size_t>(which) < cf->childNodes.size()) {
                    evalNode(cf->childNodes[which], state, depth + 1);
                } else if (which == -3) { // SO_SWITCH_ALL in Inventor/VRML 1.0
                    for (int cIdx : cf->childNodes) evalNode(cIdx, state, depth + 1);
                }
            } else if (!nd.directChildren.empty()) {
                if (which >= 0 && static_cast<size_t>(which) < nd.directChildren.size()) {
                    evalNode(nd.directChildren[which], state, depth + 1);
                } else if (which == -3) {
                    for (int cIdx : nd.directChildren) evalNode(cIdx, state, depth + 1);
                }
            }
            return;
        }

        // 2. VRML 1.0 Stateful Property & Transform Modifiers
        if (tp == "Translation") {
            float tr[3];
            readVec3Field(nd, "translation", 0.0f, 0.0f, 0.0f, tr);
            state.mat = DetailWrl::WrlMat4::Mul(state.mat, DetailWrl::WrlMat4::Translate(tr[0], tr[1], tr[2]));
            return;
        }
        if (tp == "Rotation") {
            float rot[4];
            readRotField(nd, "rotation", rot);
            state.mat = DetailWrl::WrlMat4::Mul(state.mat, DetailWrl::WrlMat4::RotateAxisAngle(rot[0], rot[1], rot[2], rot[3]));
            return;
        }
        if (tp == "Scale") {
            float sc[3];
            readVec3Field(nd, "scaleFactor", 1.0f, 1.0f, 1.0f, sc);
            state.mat = DetailWrl::WrlMat4::Mul(state.mat, DetailWrl::WrlMat4::Scale(sc[0], sc[1], sc[2]));
            return;
        }
        if (tp == "Transform") {
            // VRML 1.0 Transform { translation, rotation, scaleFactor, scaleOrientation, center }
            float tr[3], rot[4], sc[3], scOri[4], ctr[3];
            readVec3Field(nd, "translation", 0.0f, 0.0f, 0.0f, tr);
            readRotField(nd, "rotation", rot);
            readVec3Field(nd, "scaleFactor", 1.0f, 1.0f, 1.0f, sc);
            readRotField(nd, "scaleOrientation", scOri);
            readVec3Field(nd, "center", 0.0f, 0.0f, 0.0f, ctr);
            state.mat = DetailWrl::WrlMat4::Mul(state.mat, DetailWrl::WrlMat4::ComposeTransform(tr, rot, sc, scOri, ctr));
            return;
        }
        if (tp == "MatrixTransform") {
            if (const auto* mf = nd.FindField("matrix"); mf && mf->numbers.size() >= 16) {
                DetailWrl::WrlMat4 mm;
                // VRML 1.0 MatrixTransform stores row-vector matrix (translation at [12..14]); transpose to column-vector!
                for (int r = 0; r < 4; ++r) {
                    for (int c = 0; c < 4; ++c) {
                        mm.m[r][c] = static_cast<float>(mf->numbers[static_cast<size_t>(c * 4 + r)]);
                    }
                }
                state.mat = DetailWrl::WrlMat4::Mul(state.mat, mm);
            }
            return;
        }
        if (tp == "Coordinate3") {
            if (const auto* pf = nd.FindField("point")) {
                state.coords3.clear();
                state.coords3.reserve(pf->numbers.size() / 3);
                for (size_t i = 0; i + 2 < pf->numbers.size(); i += 3) {
                    state.coords3.push_back({
                        static_cast<float>(pf->numbers[i]),
                        static_cast<float>(pf->numbers[i + 1]),
                        static_cast<float>(pf->numbers[i + 2])
                    });
                }
            }
            return;
        }
        if (tp == "Material") {
            readVec3Field(nd, "diffuseColor", state.diffuse[0], state.diffuse[1], state.diffuse[2], state.diffuse);
            return;
        }

        // 3. VRML97 Shape { appearance ... geometry ... }
        if (tp == "Shape") {
            DetailWrl::WrlState shapeState = state;
            if (const auto* af = nd.FindField("appearance")) {
                for (int aIdx : af->childNodes) {
                    if (const auto* an = ast.ResolveNode(aIdx)) {
                        if (const auto* mf = an->FindField("material")) {
                            for (int mIdx : mf->childNodes) {
                                if (const auto* mn = ast.ResolveNode(mIdx)) {
                                    readVec3Field(*mn, "diffuseColor", shapeState.diffuse[0], shapeState.diffuse[1], shapeState.diffuse[2], shapeState.diffuse);
                                }
                            }
                        }
                    }
                }
            }
            if (const auto* gf = nd.FindField("geometry")) {
                for (int gIdx : gf->childNodes) {
                    evalNode(gIdx, shapeState, depth + 1);
                }
            }
            return;
        }

        // 4. Geometry Nodes
        if (tp == "IndexedFaceSet") {
            std::vector<Vec3f> pts = DetailWrl::ExtractPointsFromCoordNode(ast, &nd, state.coords3);
            if (pts.empty()) return;
            const auto* cif = nd.FindField("coordIndex");
            if (!cif || cif->numbers.empty()) return;
            const bool ccw = readBoolField(nd, "ccw", true);

            // Optional Color node inspection
            if (const auto* colF = nd.FindField("color")) {
                for (int colIdx : colF->childNodes) {
                    (void)ast.ResolveNode(colIdx);
                }
            }

            std::vector<std::array<uint32_t, 3>> localTris;
            std::vector<uint32_t> face;
            auto flushFace = [&]() {
                if (face.size() >= 3) {
                    for (size_t k = 1; k + 1 < face.size(); ++k) {
                        localTris.push_back({ face[0], face[k], face[k + 1] });
                    }
                }
                face.clear();
            };

            for (double dIdx : cif->numbers) {
                int64_t idx = static_cast<int64_t>(std::llround(dIdx));
                if (idx < 0) {
                    flushFace();
                } else if (static_cast<size_t>(idx) < pts.size()) {
                    face.push_back(static_cast<uint32_t>(idx));
                }
            }
            flushFace();
            DetailWrl::EmitWrlLocalTris(outMesh, state.mat, pts, localTris, !ccw);
            return;
        }

        if (tp == "IndexedLineSet") {
            std::vector<Vec3f> pts = DetailWrl::ExtractPointsFromCoordNode(ast, &nd, state.coords3);
            const auto* cif = nd.FindField("coordIndex");
            if (pts.empty() || !cif || cif->numbers.empty()) return;

            std::vector<uint32_t> poly;
            auto flushLine = [&]() {
                for (size_t k = 0; k + 1 < poly.size(); ++k) {
                    worldLineSegs.emplace_back(
                        state.mat.TransformPoint(pts[poly[k]]),
                        state.mat.TransformPoint(pts[poly[k + 1]]));
                }
                poly.clear();
            };
            for (double dIdx : cif->numbers) {
                int64_t idx = static_cast<int64_t>(std::llround(dIdx));
                if (idx < 0) flushLine();
                else if (static_cast<size_t>(idx) < pts.size()) poly.push_back(static_cast<uint32_t>(idx));
            }
            flushLine();
            return;
        }

        if (tp == "PointSet") {
            std::vector<Vec3f> pts = DetailWrl::ExtractPointsFromCoordNode(ast, &nd, state.coords3);
            if (pts.empty()) return;
            int64_t startIdx = static_cast<int64_t>(readFloatField(nd, "startIndex", 0.0f));
            int64_t numP = static_cast<int64_t>(readFloatField(nd, "numPoints", -1.0f));
            size_t b = (startIdx > 0 && static_cast<size_t>(startIdx) < pts.size()) ? static_cast<size_t>(startIdx) : 0;
            size_t e = (numP >= 0) ? std::min<size_t>(pts.size(), b + static_cast<size_t>(numP)) : pts.size();
            for (size_t i = b; i < e; ++i) {
                worldPoints.push_back(state.mat.TransformPoint(pts[i]));
            }
            return;
        }

        if (tp == "ElevationGrid") {
            int64_t xDim = static_cast<int64_t>(readFloatField(nd, "xDimension", 0.0f));
            int64_t zDim = static_cast<int64_t>(readFloatField(nd, "zDimension", 0.0f));
            float xSp = readFloatField(nd, "xSpacing", 1.0f);
            float zSp = readFloatField(nd, "zSpacing", 1.0f);
            bool ccw = readBoolField(nd, "ccw", true);
            const auto* hf = nd.FindField("height");
            if (xDim < 2 || zDim < 2 || xDim > 4096 || zDim > 4096 || !hf) return;
            size_t need = static_cast<size_t>(xDim) * static_cast<size_t>(zDim);
            if (hf->numbers.size() < need) return;

            std::vector<Vec3f> lv(need);
            for (int64_t j = 0; j < zDim; ++j) {
                for (int64_t i = 0; i < xDim; ++i) {
                    size_t k = static_cast<size_t>(j * xDim + i);
                    lv[k] = { static_cast<float>(i) * xSp, static_cast<float>(hf->numbers[k]), static_cast<float>(j) * zSp };
                }
            }
            std::vector<std::array<uint32_t, 3>> lt;
            lt.reserve(static_cast<size_t>((xDim - 1) * (zDim - 1) * 2));
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
            DetailWrl::EmitWrlLocalTris(outMesh, state.mat, lv, lt, !ccw);
            return;
        }

        if (tp == "Box" || tp == "Cube") {
            float sz[3] = { 2.0f, 2.0f, 2.0f };
            if (tp == "Box") {
                readVec3Field(nd, "size", 2.0f, 2.0f, 2.0f, sz);
            } else {
                sz[0] = readFloatField(nd, "width", 2.0f);
                sz[1] = readFloatField(nd, "height", 2.0f);
                sz[2] = readFloatField(nd, "depth", 2.0f);
            }
            if (sz[0] > 0.0f && sz[1] > 0.0f && sz[2] > 0.0f) {
                std::vector<Vec3f> lv; std::vector<std::array<uint32_t, 3>> lt;
                DetailWrl::BuildWrlBox(sz[0], sz[1], sz[2], lv, lt);
                DetailWrl::EmitWrlLocalTris(outMesh, state.mat, lv, lt);
            }
            return;
        }

        if (tp == "Sphere") {
            float rad = readFloatField(nd, "radius", 1.0f);
            if (rad > 0.0f) {
                std::vector<Vec3f> lv; std::vector<std::array<uint32_t, 3>> lt;
                DetailWrl::BuildWrlSphere(rad, lv, lt);
                DetailWrl::EmitWrlLocalTris(outMesh, state.mat, lv, lt);
            }
            return;
        }

        if (tp == "Cylinder") {
            float rad = readFloatField(nd, "radius", 1.0f);
            float hgt = readFloatField(nd, "height", 2.0f);
            bool side = readBoolField(nd, "side", true);
            bool top  = readBoolField(nd, "top", true);
            bool bot  = readBoolField(nd, "bottom", true);
            if (rad > 0.0f && hgt > 0.0f) {
                std::vector<Vec3f> lv; std::vector<std::array<uint32_t, 3>> lt;
                DetailWrl::BuildWrlCylinder(rad, hgt, side, top, bot, lv, lt);
                DetailWrl::EmitWrlLocalTris(outMesh, state.mat, lv, lt);
            }
            return;
        }

        if (tp == "Cone") {
            float rad = readFloatField(nd, "bottomRadius", 1.0f);
            float hgt = readFloatField(nd, "height", 2.0f);
            bool side = readBoolField(nd, "side", true);
            bool bot  = readBoolField(nd, "bottom", true);
            if (rad > 0.0f && hgt > 0.0f) {
                std::vector<Vec3f> lv; std::vector<std::array<uint32_t, 3>> lt;
                DetailWrl::BuildWrlCone(rad, hgt, side, bot, lv, lt);
                DetailWrl::EmitWrlLocalTris(outMesh, state.mat, lv, lt);
            }
            return;
        }
    };

    DetailWrl::WrlState rootState;
    for (int rIdx : ast.rootNodes) {
        evalNode(rIdx, rootState, 0);
    }

    if (!worldLineSegs.empty()) {
        Vec3f bmin = worldLineSegs[0].first, bmax = bmin;
        for (const auto& seg : worldLineSegs) {
            for (const Vec3f& p : { seg.first, seg.second }) {
                bmin.x = std::min(bmin.x, p.x); bmin.y = std::min(bmin.y, p.y); bmin.z = std::min(bmin.z, p.z);
                bmax.x = std::max(bmax.x, p.x); bmax.y = std::max(bmax.y, p.y); bmax.z = std::max(bmax.z, p.z);
            }
        }
        float span = std::max({ bmax.x - bmin.x, bmax.y - bmin.y, bmax.z - bmin.z, 1e-4f });
        float rad = span * 0.008f;
        for (const auto& seg : worldLineSegs) {
            MeshBuild3D::EmitEdgeStrut(outMesh, seg.first, seg.second, rad);
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

inline bool ParseWrlMesh(std::string_view sv, LoadedMesh3D& outMesh) {
    return ParseWrlMesh(reinterpret_cast<const uint8_t*>(sv.data()), sv.size(), outMesh);
}

} // namespace Formats
