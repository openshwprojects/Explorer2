#pragma once
#include "../../ByteIO.h"
#include "../common/MeshBuild3D.h"
// LDrawCad.h — Zero-dependency C++20 LDraw LEGO CAD Model & Multi-Part Document (.ldr, .mpd) reader.

namespace Formats {

namespace DetailLDraw {

using ::Explorer2::ByteIO::RdU16LE;
using ::Explorer2::ByteIO::RdI16LE;
using ::Explorer2::ByteIO::RdU32LE;
using ::Explorer2::ByteIO::RdI32LE;
using ::Explorer2::ByteIO::RdU64LE;
using ::Explorer2::ByteIO::RdF32LE;
using ::Explorer2::ByteIO::RdF64LE;
using ::Explorer2::ByteIO::RdU16BE;
using ::Explorer2::ByteIO::RdI16BE;
using ::Explorer2::ByteIO::RdU32BE;
using ::Explorer2::ByteIO::RdI32BE;
using ::Explorer2::ByteIO::RdU64BE;
using ::Explorer2::ByteIO::RdF32BE;
using ::Explorer2::ByteIO::RdF64BE;

inline std::string_view TrimSv(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.remove_suffix(1);
    return s;
}

inline std::string ToLowerStr(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

inline std::string ToUpperStr(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return out;
}

inline bool NextTokenSv(std::string_view& line, std::string_view& outTok) {
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t' || line.front() == '\r')) line.remove_prefix(1);
    if (line.empty()) return false;
    size_t i = 0;
    while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '\r') ++i;
    outTok = line.substr(0, i);
    line.remove_prefix(i);
    return true;
}

inline bool ParseFloatSv(std::string_view tok, float& outVal) {
    tok = TrimSv(tok);
    if (tok.empty()) return false;
    if (tok.front() == '+') tok.remove_prefix(1);
    if (tok.empty()) return false;
    float v = 0.0f;
    auto res = std::from_chars(tok.data(), tok.data() + tok.size(), v);
    if (res.ec != std::errc{} || res.ptr != tok.data() + tok.size() || !std::isfinite(v)) return false;
    outVal = v;
    return true;
}

inline bool ParseDoubleSv(std::string_view tok, double& outVal) {
    tok = TrimSv(tok);
    if (tok.empty()) return false;
    if (tok.front() == '+') tok.remove_prefix(1);
    if (tok.empty()) return false;
    double v = 0.0;
    auto res = std::from_chars(tok.data(), tok.data() + tok.size(), v);
    if (res.ec != std::errc{} || res.ptr != tok.data() + tok.size() || !std::isfinite(v)) return false;
    outVal = v;
    return true;
}

inline bool ParseIntSv(std::string_view tok, int& outVal) {
    tok = TrimSv(tok);
    if (tok.empty()) return false;
    if (tok.front() == '+') tok.remove_prefix(1);
    if (tok.size() >= 3 && tok[0] == '0' && (tok[1] == 'x' || tok[1] == 'X')) {
        uint32_t u = 0;
        auto res = std::from_chars(tok.data() + 2, tok.data() + tok.size(), u, 16);
        if (res.ec != std::errc{} || res.ptr != tok.data() + tok.size()) return false;
        outVal = static_cast<int>(u);
        return true;
    }
    int v = 0;
    auto res = std::from_chars(tok.data(), tok.data() + tok.size(), v, 10);
    if (res.ec != std::errc{} || res.ptr != tok.data() + tok.size()) return false;
    outVal = v;
    return true;
}

inline bool IsSafeRelativeSubFile(std::string_view rel) {
    if (rel.empty() || rel.size() > 260) return false;
    if (rel.front() == '/' || rel.front() == '\\') return false;
    if (rel.find(':') != std::string_view::npos) return false;
    if (rel.find("..") != std::string_view::npos) return false;
    for (char c : rel) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 32u) return false;
    }
    return true;
}

// ============================================================================
// 1. LDraw LEGO CAD (.ldr, .mpd) Helpers
// ============================================================================
inline std::string NormalizeLDrawKey(std::string_view raw) {
    raw = TrimSv(raw);
    if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
        raw = raw.substr(1, raw.size() - 2);
    }
    std::string s = ToLowerStr(raw);
    for (char& c : s) {
        if (c == '\\') c = '/';
    }
    while (s.size() >= 2 && s[0] == '.' && s[1] == '/') s.erase(0, 2);
    return s;
}

inline std::string LDrawLeafKey(const std::string& normKey) {
    size_t sl = normKey.find_last_of('/');
    return (sl != std::string::npos) ? normKey.substr(sl + 1) : normKey;
}

inline uint32_t LookupLDrawColorRgb(int code, const std::unordered_map<int, uint32_t>& customCols) {
    if ((code & 0xFF000000) == 0x02000000 || (code >= 0x2000000 && code <= 0x2FFFFFF)) {
        return static_cast<uint32_t>(code & 0xFFFFFF);
    }
    auto it = customCols.find(code);
    if (it != customCols.end()) return it->second;
    switch (code) {
    case 0:   return 0x1B2A34u; // Black
    case 1:   return 0x1E5AA8u; // Blue
    case 2:   return 0x00852Bu; // Green
    case 3:   return 0x069D9Fu; // Dark Turquoise
    case 4:   return 0xB40000u; // Red
    case 5:   return 0xD3359Du; // Dark Pink
    case 6:   return 0x543324u; // Brown
    case 7:   return 0x8A928Du; // Light Grey
    case 8:   return 0x545955u; // Dark Grey
    case 9:   return 0x97CBD9u; // Light Blue
    case 10:  return 0x58AB41u; // Bright Green
    case 14:  return 0xFAC80Au; // Yellow
    case 15:  return 0xF4F4F4u; // White
    case 19:  return 0xDEC69Cu; // Tan
    case 25:  return 0xD67923u; // Orange
    case 27:  return 0xA5CA18u; // Lime
    case 70:  return 0x5F3109u; // Reddish Brown
    case 71:  return 0x969696u; // Light Bluish Grey
    case 72:  return 0x646464u; // Dark Bluish Grey
    default:  return 0xCCCCCCu;
    }
}

inline Vec3f TransformLDrawPt(const float m[16], float x, float y, float z) {
    return {
        m[0] * x + m[1] * y + m[2] * z + m[3],
        m[4] * x + m[5] * y + m[6] * z + m[7],
        m[8] * x + m[9] * y + m[10] * z + m[11]
    };
}

// Convert LDraw -Y up (x, y, z) to Explorer2 Y-up (x, -y, z)
inline Vec3f LDrawToViewerPt(const Vec3f& p) {
    return { p.x, -p.y, p.z };
}

inline Vec3f CrossVec3(const Vec3f& a, const Vec3f& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

inline float DotVec3(const Vec3f& a, const Vec3f& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3f SubVec3(const Vec3f& a, const Vec3f& b) {
    return { a.x - b.x, a.y - b.y, a.z - b.z };
}

inline void EmitLDrawTri(LoadedMesh3D& mesh, const Vec3f& p0, const Vec3f& p1, const Vec3f& p2, bool flipWinding) {
    if (mesh.tris.size() >= 2000000u) return;
    Vec3f v0 = LDrawToViewerPt(p0);
    Vec3f v1 = LDrawToViewerPt(p1);
    Vec3f v2 = LDrawToViewerPt(p2);
    // LDrawToViewerPt negates Y (Jacobian det = -1), so standard CCW LDraw winding swaps (1, 2) to face outward in Y-up!
    uint32_t b = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(v0);
    mesh.vertices.push_back(v1);
    mesh.vertices.push_back(v2);
    if (flipWinding) {
        mesh.tris.push_back({ b + 0u, b + 1u, b + 2u, {} });
    } else {
        mesh.tris.push_back({ b + 0u, b + 2u, b + 1u, {} });
    }
}

// Untwist bowtie quads using 3D cross products before splitting into 2 triangles
inline void EmitLDrawQuad(LoadedMesh3D& mesh, Vec3f q0, Vec3f q1, Vec3f q2, Vec3f q3, bool flipWinding) {
    auto isConvexOrder = [&](const Vec3f& a, const Vec3f& b, const Vec3f& c, const Vec3f& d) -> bool {
        Vec3f n1 = CrossVec3(SubVec3(b, a), SubVec3(c, a));
        Vec3f n2 = CrossVec3(SubVec3(c, a), SubVec3(d, a));
        Vec3f n3 = CrossVec3(SubVec3(c, b), SubVec3(d, b));
        return DotVec3(n1, n2) >= -1e-6f && DotVec3(n1, n3) >= -1e-6f;
    };

    if (!isConvexOrder(q0, q1, q2, q3)) {
        if (isConvexOrder(q0, q1, q3, q2)) {
            std::swap(q2, q3);
        } else if (isConvexOrder(q0, q2, q1, q3)) {
            std::swap(q1, q2);
        }
    }
    EmitLDrawTri(mesh, q0, q1, q2, flipWinding);
    EmitLDrawTri(mesh, q0, q2, q3, flipWinding);
}

// Built-in procedural LDraw standard geometric primitives (LDraw.org Primitive Reference)
inline bool EmitLDrawBuiltinPrimitive(
    const std::string& leafName,
    const float m[16],
    bool flipWinding,
    LoadedMesh3D& mesh)
{
    auto xf = [&](float x, float y, float z) { return TransformLDrawPt(m, x, y, z); };
    constexpr float kPi = 3.14159265358979323846f;

    if (leafName == "rect.dat") {
        EmitLDrawQuad(mesh, xf(-1, 0, -1), xf(-1, 0, 1), xf(1, 0, 1), xf(1, 0, -1), flipWinding);
        return true;
    }
    if (leafName == "box5.dat" || leafName == "box.dat" || leafName == "box4.dat") {
        // 4 vertical walls around [-1, 1]^3
        EmitLDrawQuad(mesh, xf(-1, -1,  1), xf(-1,  1,  1), xf( 1,  1,  1), xf( 1, -1,  1), flipWinding);
        EmitLDrawQuad(mesh, xf( 1, -1, -1), xf( 1,  1, -1), xf(-1,  1, -1), xf(-1, -1, -1), flipWinding);
        EmitLDrawQuad(mesh, xf(-1, -1, -1), xf(-1,  1, -1), xf(-1,  1,  1), xf(-1, -1,  1), flipWinding);
        EmitLDrawQuad(mesh, xf( 1, -1,  1), xf( 1,  1,  1), xf( 1,  1, -1), xf( 1, -1, -1), flipWinding);
        if (leafName != "box4.dat") {
            // Top face at y = -1 (-Y is up in LDraw)
            EmitLDrawQuad(mesh, xf(-1, -1, -1), xf(-1, -1,  1), xf( 1, -1,  1), xf( 1, -1, -1), flipWinding);
        }
        if (leafName == "box.dat") {
            EmitLDrawQuad(mesh, xf(-1,  1,  1), xf(-1,  1, -1), xf( 1,  1, -1), xf( 1,  1,  1), flipWinding);
        }
        return true;
    }
    if (leafName == "stud.dat" || leafName == "stud2.dat" || leafName == "stud2a.dat" ||
        leafName == "stud3.dat" || leafName == "stud4.dat")
    {
        // Standard LEGO stud: outer radius 6 LDU, height from y = 0 up to y = -4 LDU
        const int segs = 16;
        const float rOut = 6.0f;
        const float rIn  = 4.0f;
        const bool hollow = (leafName == "stud2.dat" || leafName == "stud2a.dat" || leafName == "stud4.dat");
        for (int i = 0; i < segs; ++i) {
            float a0 = (2.0f * kPi * static_cast<float>(i)) / static_cast<float>(segs);
            float a1 = (2.0f * kPi * static_cast<float>(i + 1)) / static_cast<float>(segs);
            float c0 = std::cos(a0), s0 = std::sin(a0);
            float c1 = std::cos(a1), s1 = std::sin(a1);
            // Outer cylinder wall (y = -4 to 0)
            EmitLDrawQuad(mesh,
                          xf(rOut * c0, -4.0f, rOut * s0),
                          xf(rOut * c0,  0.0f, rOut * s0),
                          xf(rOut * c1,  0.0f, rOut * s1),
                          xf(rOut * c1, -4.0f, rOut * s1),
                          flipWinding);
            if (!hollow) {
                EmitLDrawTri(mesh,
                             xf(0.0f, -4.0f, 0.0f),
                             xf(rOut * c0, -4.0f, rOut * s0),
                             xf(rOut * c1, -4.0f, rOut * s1),
                             flipWinding);
            } else {
                // Top ring + inner wall
                EmitLDrawQuad(mesh,
                              xf(rIn  * c0, -4.0f, rIn  * s0),
                              xf(rOut * c0, -4.0f, rOut * s0),
                              xf(rOut * c1, -4.0f, rOut * s1),
                              xf(rIn  * c1, -4.0f, rIn  * s1),
                              flipWinding);
                EmitLDrawQuad(mesh,
                              xf(rIn * c1, -4.0f, rIn * s1),
                              xf(rIn * c1,  0.0f, rIn * s1),
                              xf(rIn * c0,  0.0f, rIn * s0),
                              xf(rIn * c0, -4.0f, rIn * s0),
                              flipWinding);
            }
        }
        return true;
    }

    // Parse optional fractional prefix <num>-<den><stem>.dat (e.g. 4-4cyli.dat, 1-4disc.dat, 4-4ring2.dat)
    std::string stem = leafName;
    if (stem.size() > 4 && stem.substr(stem.size() - 4) == ".dat") {
        stem.resize(stem.size() - 4);
    } else {
        return false;
    }

    float frac = 1.0f;
    size_t dash = stem.find('-');
    if (dash != std::string::npos && dash >= 1 && dash <= 2) {
        size_t pEnd = dash + 1;
        while (pEnd < stem.size() && std::isdigit(static_cast<unsigned char>(stem[pEnd]))) ++pEnd;
        int num = 0, den = 0;
        if (pEnd > dash + 1 &&
            ParseIntSv(std::string_view(stem).substr(0, dash), num) &&
            ParseIntSv(std::string_view(stem).substr(dash + 1, pEnd - (dash + 1)), den) &&
            num > 0 && den > 0 && num <= den)
        {
            frac = static_cast<float>(num) / static_cast<float>(den);
            stem.erase(0, pEnd);
        }
    }

    int totalSegs = std::max(4, static_cast<int>(std::lround(16.0f * frac)));
    float totalAng = 2.0f * kPi * frac;

    if (stem == "cyli" || stem == "cylo" || stem == "cyli2" || stem == "cylc" || stem == "cylc2") {
        bool capped = (stem == "cylc" || stem == "cylc2");
        bool inside = (stem == "cyli" || stem == "cyli2");
        for (int i = 0; i < totalSegs; ++i) {
            float a0 = totalAng * static_cast<float>(i) / static_cast<float>(totalSegs);
            float a1 = totalAng * static_cast<float>(i + 1) / static_cast<float>(totalSegs);
            float c0 = std::cos(a0), s0 = std::sin(a0);
            float c1 = std::cos(a1), s1 = std::sin(a1);
            EmitLDrawQuad(mesh, xf(c0, 0.0f, s0), xf(c0, 1.0f, s0), xf(c1, 1.0f, s1), xf(c1, 0.0f, s1),
                          flipWinding ^ inside);
            if (capped) {
                EmitLDrawTri(mesh, xf(0.0f, 0.0f, 0.0f), xf(c0, 0.0f, s0), xf(c1, 0.0f, s1), flipWinding);
            }
        }
        return true;
    }
    if (stem == "disc") {
        for (int i = 0; i < totalSegs; ++i) {
            float a0 = totalAng * static_cast<float>(i) / static_cast<float>(totalSegs);
            float a1 = totalAng * static_cast<float>(i + 1) / static_cast<float>(totalSegs);
            EmitLDrawTri(mesh, xf(0.0f, 0.0f, 0.0f),
                         xf(std::cos(a0), 0.0f, std::sin(a0)),
                         xf(std::cos(a1), 0.0f, std::sin(a1)), flipWinding);
        }
        return true;
    }
    if (stem == "ndis") {
        for (int i = 0; i < totalSegs; ++i) {
            float a0 = totalAng * static_cast<float>(i) / static_cast<float>(totalSegs);
            float a1 = totalAng * static_cast<float>(i + 1) / static_cast<float>(totalSegs);
            float c0 = std::cos(a0), s0 = std::sin(a0);
            float c1 = std::cos(a1), s1 = std::sin(a1);
            float am = 0.5f * (a0 + a1);
            float sx = (std::cos(am) >= 0.0f) ? 1.0f : -1.0f;
            float sz = (std::sin(am) >= 0.0f) ? 1.0f : -1.0f;
            EmitLDrawTri(mesh, xf(sx, 0.0f, sz), xf(c1, 0.0f, s1), xf(c0, 0.0f, s0), flipWinding);
        }
        return true;
    }
    if (stem.size() >= 5 && stem.substr(0, 4) == "ring") {
        int rInner = 0;
        if (ParseIntSv(std::string_view(stem).substr(4), rInner) && rInner >= 1 && rInner <= 32) {
            float ri = static_cast<float>(rInner);
            float ro = ri + 1.0f;
            for (int i = 0; i < totalSegs; ++i) {
                float a0 = totalAng * static_cast<float>(i) / static_cast<float>(totalSegs);
                float a1 = totalAng * static_cast<float>(i + 1) / static_cast<float>(totalSegs);
                float c0 = std::cos(a0), s0 = std::sin(a0);
                float c1 = std::cos(a1), s1 = std::sin(a1);
                EmitLDrawQuad(mesh, xf(ri * c0, 0.0f, ri * s0), xf(ro * c0, 0.0f, ro * s0),
                              xf(ro * c1, 0.0f, ro * s1), xf(ri * c1, 0.0f, ri * s1), flipWinding);
            }
            return true;
        }
    }
    return false;
}

struct LDrawContext {
    std::unordered_map<std::string, std::string> subModels; // normalized key -> text
    std::unordered_map<std::string, std::string> leafMap;   // leaf key -> normalized key
    std::unordered_map<std::string, std::string> diskCache; // normalized key -> disk text
    std::unordered_map<int, uint32_t> customColors;
    std::wstring baseDir;
    bool sawLDrawMeta = false;
    std::vector<std::pair<Vec3f, Vec3f>> edgeSegments;
};

inline void SplitLDrawMpd(std::string_view sv, LDrawContext& ctx, std::string& outRootKey) {
    outRootKey.clear();
    std::string curKey;
    std::string curBody;
    bool inDataBlock = false;

    size_t pos = 0;
    while (pos < sv.size()) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view rawLine = sv.substr(pos, eol - pos);
        pos = eol + 1u;

        std::string_view ln = TrimSv(rawLine);
        if (!ln.empty() && ln.front() == '0') {
            std::string_view rest = TrimSv(ln.substr(1));
            std::string_view kw;
            std::string_view tail = rest;
            if (NextTokenSv(tail, kw)) {
                std::string kwUp = ToUpperStr(kw);
                if (kwUp == "FILE") {
                    if (!curKey.empty() && !inDataBlock) {
                        ctx.subModels[curKey] = std::move(curBody);
                    }
                    curBody.clear();
                    inDataBlock = false;
                    curKey = NormalizeLDrawKey(tail);
                    if (!curKey.empty()) {
                        ctx.sawLDrawMeta = true;
                        std::string leaf = LDrawLeafKey(curKey);
                        if (!ctx.leafMap.count(leaf)) ctx.leafMap[leaf] = curKey;
                        if (outRootKey.empty()) outRootKey = curKey;
                    }
                    continue;
                }
                if (kwUp == "NOFILE") {
                    if (!curKey.empty() && !inDataBlock) {
                        ctx.subModels[curKey] = std::move(curBody);
                    }
                    curKey.clear();
                    curBody.clear();
                    inDataBlock = false;
                    ctx.sawLDrawMeta = true;
                    continue;
                }
                if (kwUp == "!DATA") {
                    if (!curKey.empty() && !inDataBlock) {
                        ctx.subModels[curKey] = std::move(curBody);
                    }
                    curKey.clear();
                    curBody.clear();
                    inDataBlock = true;
                    continue;
                }
                if (kwUp == "!COLOUR") {
                    ctx.sawLDrawMeta = true;
                    int codeId = -1;
                    uint32_t hexRgb = 0;
                    bool hasCode = false, hasVal = false;
                    std::string_view ct = tail;
                    std::string_view tok;
                    while (NextTokenSv(ct, tok)) {
                        std::string tu = ToUpperStr(tok);
                        if (tu == "CODE") {
                            std::string_view vTok;
                            if (NextTokenSv(ct, vTok) && ParseIntSv(vTok, codeId)) hasCode = true;
                        } else if (tu == "VALUE") {
                            std::string_view vTok;
                            if (NextTokenSv(ct, vTok)) {
                                if (!vTok.empty() && vTok.front() == '#') vTok.remove_prefix(1);
                                else if (vTok.size() > 2 && vTok[0] == '0' && (vTok[1] == 'x' || vTok[1] == 'X')) vTok.remove_prefix(2);
                                if (vTok.size() == 6) {
                                    auto rc = std::from_chars(vTok.data(), vTok.data() + 6, hexRgb, 16);
                                    if (rc.ec == std::errc{}) hasVal = true;
                                }
                            }
                        }
                    }
                    if (hasCode && hasVal) ctx.customColors[codeId] = hexRgb;
                } else if (kwUp == "!LDRAW_ORG" || kwUp == "BFC" || kwUp == "STEP" ||
                           kwUp == "ROTSTEP" || kwUp == "AUTHOR:" || kwUp == "NAME:")
                {
                    ctx.sawLDrawMeta = true;
                }
            }
        }

        if (inDataBlock) continue;
        if (!curKey.empty()) {
            curBody.append(rawLine.data(), rawLine.size());
            curBody.push_back('\n');
        } else if (outRootKey.empty()) {
            curBody.append(rawLine.data(), rawLine.size());
            curBody.push_back('\n');
        }
    }

    if (!curKey.empty() && !inDataBlock) {
        ctx.subModels[curKey] = std::move(curBody);
    } else if (outRootKey.empty() && !curBody.empty()) {
        outRootKey = "__root__.ldr";
        ctx.subModels[outRootKey] = std::move(curBody);
    }
}

inline const std::string* ResolveLDrawSubModel(LDrawContext& ctx, const std::string& normRef) {
    auto it = ctx.subModels.find(normRef);
    if (it != ctx.subModels.end()) return &it->second;

    std::string leaf = LDrawLeafKey(normRef);
    auto itLeaf = ctx.leafMap.find(leaf);
    if (itLeaf != ctx.leafMap.end()) {
        auto it2 = ctx.subModels.find(itLeaf->second);
        if (it2 != ctx.subModels.end()) return &it2->second;
    }

    auto itDisk = ctx.diskCache.find(normRef);
    if (itDisk != ctx.diskCache.end()) {
        return itDisk->second.empty() ? nullptr : &itDisk->second;
    }

    if (!ctx.baseDir.empty() && IsSafeRelativeSubFile(normRef)) {
        std::vector<uint8_t> bytes;
        std::wstring relW = PathUtils::Utf8ToWide(normRef);
#ifdef _WIN32
        for (wchar_t& ch : relW) if (ch == L'/') ch = L'\\';
        const wchar_t sep = L'\\';
#else
        const wchar_t sep = L'/';
#endif
        std::wstring p1 = ctx.baseDir + sep + relW;
        if (!PathUtils::ReadFileBytes(p1, 16 * 1024 * 1024, bytes) && leaf != normRef && IsSafeRelativeSubFile(leaf)) {
            std::wstring p2 = ctx.baseDir + sep + PathUtils::Utf8ToWide(leaf);
            PathUtils::ReadFileBytes(p2, 16 * 1024 * 1024, bytes);
        }
        if (!bytes.empty()) {
            ctx.diskCache[normRef] = std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            return &ctx.diskCache[normRef];
        }
    }
    ctx.diskCache[normRef] = std::string{};
    return nullptr;
}

inline void InstantiateLDrawModel(
    LDrawContext& ctx,
    std::string_view modelText,
    const float parentMat[16],
    int parentColor,
    bool parentInvert,
    int depth,
    LoadedMesh3D& mesh)
{
    if (depth > 16 || mesh.tris.size() >= 2000000u) return;

    bool ccwWinding = true;
    bool invertNext = false;

    size_t pos = 0;
    while (pos < modelText.size() && mesh.tris.size() < 2000000u) {
        size_t eol = modelText.find('\n', pos);
        if (eol == std::string_view::npos) eol = modelText.size();
        std::string_view ln = TrimSv(modelText.substr(pos, eol - pos));
        pos = eol + 1u;
        if (ln.empty()) continue;

        std::string_view rem = ln;
        std::string_view cmdTok;
        if (!NextTokenSv(rem, cmdTok) || cmdTok.size() != 1u) continue;
        char cmd = cmdTok[0];

        if (cmd == '0') {
            std::string_view mTok;
            if (NextTokenSv(rem, mTok) && ToUpperStr(mTok) == "BFC") {
                ctx.sawLDrawMeta = true;
                std::string_view bfcTok;
                while (NextTokenSv(rem, bfcTok)) {
                    std::string bu = ToUpperStr(bfcTok);
                    if (bu == "INVERTNEXT") invertNext = true;
                    else if (bu == "CCW")   ccwWinding = true;
                    else if (bu == "CW")    ccwWinding = false;
                }
            }
            continue;
        }

        if (cmd == '1') {
            std::string_view colTok;
            int col = 16;
            if (!NextTokenSv(rem, colTok) || !ParseIntSv(colTok, col)) { invertNext = false; continue; }
            float v[12] = {};
            bool ok12 = true;
            for (int k = 0; k < 12; ++k) {
                std::string_view ft;
                if (!NextTokenSv(rem, ft) || !ParseFloatSv(ft, v[k])) { ok12 = false; break; }
            }
            std::string subFile = NormalizeLDrawKey(rem);
            if (!ok12 || subFile.empty()) { invertNext = false; continue; }

            // Local 4x4 LDraw matrix: translation (v[0..2]), 3x3 rotation/scale (v[3..11])
            float localMat[16] = {
                v[3], v[4], v[5],  v[0],
                v[6], v[7], v[8],  v[1],
                v[9], v[10], v[11], v[2],
                0.0f, 0.0f, 0.0f,  1.0f
            };
            float childMat[16] = {};
            MeshBuild3D::MatMul4RowMajor(parentMat, localMat, childMat);

            float det = v[3] * (v[7] * v[11] - v[8] * v[10]) -
                        v[4] * (v[6] * v[11] - v[8] * v[9]) +
                        v[5] * (v[6] * v[10] - v[7] * v[9]);
            bool detNeg = (det < -1e-7f);
            bool childInvert = parentInvert ^ detNeg ^ invertNext;
            int childColor = (col == 16 || col == 24) ? parentColor : col;
            (void)LookupLDrawColorRgb(childColor, ctx.customColors);
            invertNext = false;

            if (const std::string* subBody = ResolveLDrawSubModel(ctx, subFile)) {
                InstantiateLDrawModel(ctx, *subBody, childMat, childColor, childInvert, depth + 1, mesh);
            } else {
                EmitLDrawBuiltinPrimitive(LDrawLeafKey(subFile), childMat, childInvert, mesh);
            }
        } else if (cmd == '2') {
            std::string_view colTok;
            int col = 24;
            if (!NextTokenSv(rem, colTok) || !ParseIntSv(colTok, col)) continue;
            float c[6] = {};
            bool ok6 = true;
            for (int k = 0; k < 6; ++k) {
                std::string_view ft;
                if (!NextTokenSv(rem, ft) || !ParseFloatSv(ft, c[k])) { ok6 = false; break; }
            }
            if (ok6 && ctx.edgeSegments.size() < 200000u) {
                ctx.edgeSegments.push_back({
                    LDrawToViewerPt(TransformLDrawPt(parentMat, c[0], c[1], c[2])),
                    LDrawToViewerPt(TransformLDrawPt(parentMat, c[3], c[4], c[5]))
                });
            }
        } else if (cmd == '3') {
            std::string_view colTok;
            int col = 16;
            if (!NextTokenSv(rem, colTok) || !ParseIntSv(colTok, col)) { invertNext = false; continue; }
            float c[9] = {};
            bool ok9 = true;
            for (int k = 0; k < 9; ++k) {
                std::string_view ft;
                if (!NextTokenSv(rem, ft) || !ParseFloatSv(ft, c[k])) { ok9 = false; break; }
            }
            bool effFlip = (!ccwWinding) ^ parentInvert ^ invertNext;
            invertNext = false;
            if (!ok9) continue;
            int triCol = (col == 16 || col == 24) ? parentColor : col;
            (void)LookupLDrawColorRgb(triCol, ctx.customColors);
            EmitLDrawTri(mesh,
                         TransformLDrawPt(parentMat, c[0], c[1], c[2]),
                         TransformLDrawPt(parentMat, c[3], c[4], c[5]),
                         TransformLDrawPt(parentMat, c[6], c[7], c[8]),
                         effFlip);
        } else if (cmd == '4') {
            std::string_view colTok;
            int col = 16;
            if (!NextTokenSv(rem, colTok) || !ParseIntSv(colTok, col)) { invertNext = false; continue; }
            float c[12] = {};
            bool ok12 = true;
            for (int k = 0; k < 12; ++k) {
                std::string_view ft;
                if (!NextTokenSv(rem, ft) || !ParseFloatSv(ft, c[k])) { ok12 = false; break; }
            }
            bool effFlip = (!ccwWinding) ^ parentInvert ^ invertNext;
            invertNext = false;
            if (!ok12) continue;
            int quadCol = (col == 16 || col == 24) ? parentColor : col;
            (void)LookupLDrawColorRgb(quadCol, ctx.customColors);
            EmitLDrawQuad(mesh,
                          TransformLDrawPt(parentMat, c[0], c[1], c[2]),
                          TransformLDrawPt(parentMat, c[3], c[4], c[5]),
                          TransformLDrawPt(parentMat, c[6], c[7], c[8]),
                          TransformLDrawPt(parentMat, c[9], c[10], c[11]),
                          effFlip);
        }
    }
}

} // namespace DetailLDraw

inline bool ParseLDrawMesh(
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    const std::wstring& extLower,
    LoadedMesh3D& mesh)
{
    using namespace DetailLDraw;
    if (!data || size < 6u) return false;
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    if (sv.size() >= 3u && static_cast<uint8_t>(sv[0]) == 0xEFu &&
        static_cast<uint8_t>(sv[1]) == 0xBBu && static_cast<uint8_t>(sv[2]) == 0xBFu)
    {
        sv.remove_prefix(3u);
    }

    mesh.vertices.clear();
    mesh.tris.clear();

    LDrawContext ctx;
    if (!fullPath.empty()) {
        ctx.baseDir = PathUtils::GetParentPath(fullPath);
    }

    std::string rootKey;
    SplitLDrawMpd(sv, ctx, rootKey);
    if (rootKey.empty()) return false;
    auto itRoot = ctx.subModels.find(rootKey);
    if (itRoot == ctx.subModels.end()) return false;

    const float ident[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    InstantiateLDrawModel(ctx, itRoot->second, ident, 16, false, 0, mesh);

    // Wireframe-only LDraw edge fallback ONLY if genuine LDraw header meta-commands were present
    if (mesh.tris.empty() && ctx.sawLDrawMeta && ctx.edgeSegments.size() >= 2u) {
        Vec3f mn = ctx.edgeSegments[0].first, mx = mn;
        for (const auto& seg : ctx.edgeSegments) {
            for (const Vec3f& p : { seg.first, seg.second }) {
                mn.x = std::min(mn.x, p.x); mn.y = std::min(mn.y, p.y); mn.z = std::min(mn.z, p.z);
                mx.x = std::max(mx.x, p.x); mx.y = std::max(mx.y, p.y); mx.z = std::max(mx.z, p.z);
            }
        }
        float rad = std::max({ mx.x - mn.x, mx.y - mn.y, mx.z - mn.z, 1.0f }) * 0.012f;
        for (const auto& seg : ctx.edgeSegments) {
            MeshBuild3D::EmitEdgeStrut(mesh, seg.first, seg.second, rad);
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = (extLower == L".mpd" || ctx.subModels.size() > 1u)
        ? L"LDraw Multi-Part LEGO CAD Assembly (.mpd)"
        : L"LDraw LEGO CAD Model (.ldr)";
    return true;
}

} // namespace Formats
