#pragma once
// GodotMeshScene.h — Godot Engine 3.x & 4.x 3D Scenes & Mesh Resources
//   Text formats:   .tscn ([gd_scene]), .tres ([gd_resource]), text .mesh
//   Binary formats: .scn, .res, binary .mesh ("RSRC" uncompressed & "RSCC" compressed)

namespace Formats {

namespace GodotDetail {

inline uint16_t GdU16(const uint8_t* p, bool be) {
    return be ? static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1])
              : static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

inline uint32_t GdU32(const uint8_t* p, bool be) {
    if (be) {
        return (static_cast<uint32_t>(p[0]) << 24) |
               (static_cast<uint32_t>(p[1]) << 16) |
               (static_cast<uint32_t>(p[2]) << 8)  |
                static_cast<uint32_t>(p[3]);
    }
    uint32_t v = 0;
    std::memcpy(&v, p, 4);
    return v;
}

inline uint64_t GdU64(const uint8_t* p, bool be) {
    if (be) return (static_cast<uint64_t>(GdU32(p, true)) << 32) | GdU32(p + 4, true);
    return static_cast<uint64_t>(GdU32(p, false)) | (static_cast<uint64_t>(GdU32(p + 4, false)) << 32);
}

inline float GdF32(const uint8_t* p, bool be) {
    uint32_t u = GdU32(p, be);
    float f = 0.0f;
    std::memcpy(&f, &u, 4);
    return f;
}

inline double GdF64(const uint8_t* p, bool be) {
    uint64_t u = GdU64(p, be);
    double d = 0.0;
    std::memcpy(&d, &u, 8);
    return d;
}

inline bool DecodeBase64Bytes(std::string_view b64, std::vector<uint8_t>& out) {
    out.clear();
    out.reserve((b64.size() * 3) / 4);
    uint32_t val = 0;
    int valb = -8;
    for (unsigned char c : b64) {
        if (c <= 0x20) continue;
        if (c == '=') break;
        int d = -1;
        if (c >= 'A' && c <= 'Z') d = c - 'A';
        else if (c >= 'a' && c <= 'z') d = c - 'a' + 26;
        else if (c >= '0' && c <= '9') d = c - '0' + 52;
        else if (c == '+') d = 62;
        else if (c == '/') d = 63;
        else return false;
        val = (val << 6) | static_cast<uint32_t>(d);
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<uint8_t>((val >> valb) & 0xFFu));
            valb -= 8;
        }
    }
    return true;
}

struct GdXform3D {
    // Godot Transform3D: 3x3 basis columns/rows (xx, xy, xz, yx, yy, yz, zx, zy, zz) + origin (ox, oy, oz)
    // In Godot's text/binary serialization: Transform3D(basis.rows[0].x, rows[0].y, rows[0].z, rows[1]..., rows[2]..., origin.x, origin.y, origin.z)
    float m[3][4] = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f }
    };

    static GdXform3D From12Floats(const float f[12]) {
        GdXform3D xf{};
        // Godot Transform3D constructor in .tscn/.tres takes (x_axis.x, x_axis.y, x_axis.z, y_axis.x, y_axis.y, y_axis.z, z_axis.x, z_axis.y, z_axis.z, origin.x, origin.y, origin.z)
        xf.m[0][0] = f[0]; xf.m[1][0] = f[1]; xf.m[2][0] = f[2];
        xf.m[0][1] = f[3]; xf.m[1][1] = f[4]; xf.m[2][1] = f[5];
        xf.m[0][2] = f[6]; xf.m[1][2] = f[7]; xf.m[2][2] = f[8];
        xf.m[0][3] = f[9]; xf.m[1][3] = f[10]; xf.m[2][3] = f[11];
        return xf;
    }

    static GdXform3D Mul(const GdXform3D& a, const GdXform3D& b) {
        GdXform3D c{};
        for (int r = 0; r < 3; ++r) {
            for (int col = 0; col < 3; ++col) {
                c.m[r][col] = a.m[r][0] * b.m[0][col] + a.m[r][1] * b.m[1][col] + a.m[r][2] * b.m[2][col];
            }
            c.m[r][3] = a.m[r][0] * b.m[0][3] + a.m[r][1] * b.m[1][3] + a.m[r][2] * b.m[2][3] + a.m[r][3];
        }
        return c;
    }

    Vec3f Apply(const Vec3f& v) const {
        return {
            m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z + m[0][3],
            m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z + m[1][3],
            m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z + m[2][3]
        };
    }
};

struct GdLocalMesh {
    std::vector<Vec3f> verts;
    std::vector<std::array<uint32_t, 3>> tris;
    bool empty() const { return verts.empty() || tris.empty(); }
};

inline void BuildBoxPrimitive(float sx, float sy, float sz, GdLocalMesh& out) {
    float hx = std::max(0.001f, sx * 0.5f);
    float hy = std::max(0.001f, sy * 0.5f);
    float hz = std::max(0.001f, sz * 0.5f);
    uint32_t b = static_cast<uint32_t>(out.verts.size());
    out.verts.push_back({ -hx, -hy, -hz });
    out.verts.push_back({  hx, -hy, -hz });
    out.verts.push_back({  hx,  hy, -hz });
    out.verts.push_back({ -hx,  hy, -hz });
    out.verts.push_back({ -hx, -hy,  hz });
    out.verts.push_back({  hx, -hy,  hz });
    out.verts.push_back({  hx,  hy,  hz });
    out.verts.push_back({ -hx,  hy,  hz });
    static const uint32_t kBoxTris[12][3] = {
        {0,2,1}, {0,3,2}, {4,5,6}, {4,6,7},
        {0,1,5}, {0,5,4}, {2,3,7}, {2,7,6},
        {1,2,6}, {1,6,5}, {3,0,4}, {3,4,7}
    };
    for (const auto& t : kBoxTris) out.tris.push_back({ b + t[0], b + t[1], b + t[2] });
}

inline void BuildCylinderOrConePrimitive(float topR, float botR, float height, int segs, GdLocalMesh& out) {
    segs = std::clamp(segs, 6, 48);
    float hh = std::max(0.001f, height * 0.5f);
    topR = std::max(0.0f, topR);
    botR = std::max(0.0f, botR);
    uint32_t b = static_cast<uint32_t>(out.verts.size());
    for (int i = 0; i < segs; ++i) {
        float a = (6.2831853f * static_cast<float>(i)) / static_cast<float>(segs);
        float ca = std::cos(a), sa = std::sin(a);
        out.verts.push_back({ ca * botR, -hh, sa * botR });
        out.verts.push_back({ ca * topR,  hh, sa * topR });
    }
    uint32_t botC = static_cast<uint32_t>(out.verts.size());
    out.verts.push_back({ 0.0f, -hh, 0.0f });
    uint32_t topC = static_cast<uint32_t>(out.verts.size());
    out.verts.push_back({ 0.0f,  hh, 0.0f });
    for (int i = 0; i < segs; ++i) {
        int ni = (i + 1) % segs;
        uint32_t b0 = b + static_cast<uint32_t>(i * 2);
        uint32_t t0 = b0 + 1;
        uint32_t b1 = b + static_cast<uint32_t>(ni * 2);
        uint32_t t1 = b1 + 1;
        if (botR > 1e-4f || topR > 1e-4f) {
            if (botR > 1e-4f) out.tris.push_back({ b0, b1, t1 });
            if (topR > 1e-4f) out.tris.push_back({ b0, t1, t0 });
        }
        if (botR > 1e-4f) out.tris.push_back({ botC, b1, b0 });
        if (topR > 1e-4f) out.tris.push_back({ topC, t0, t1 });
    }
}

inline void BuildSpherePrimitive(float radius, float height, GdLocalMesh& out) {
    float rx = std::max(0.001f, radius);
    float ry = std::max(0.001f, height * 0.5f);
    const int rings = 10, segs = 16;
    uint32_t b = static_cast<uint32_t>(out.verts.size());
    for (int r = 0; r <= rings; ++r) {
        float phi = 3.14159265f * static_cast<float>(r) / static_cast<float>(rings);
        float y = std::cos(phi) * ry;
        float sr = std::sin(phi) * rx;
        for (int s = 0; s <= segs; ++s) {
            float th = 6.2831853f * static_cast<float>(s) / static_cast<float>(segs);
            out.verts.push_back({ std::cos(th) * sr, y, std::sin(th) * sr });
        }
    }
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < segs; ++s) {
            uint32_t i0 = b + static_cast<uint32_t>(r * (segs + 1) + s);
            uint32_t i1 = i0 + 1;
            uint32_t i2 = i0 + static_cast<uint32_t>(segs + 1);
            uint32_t i3 = i2 + 1;
            if (r > 0) out.tris.push_back({ i0, i2, i1 });
            if (r + 1 < rings) out.tris.push_back({ i1, i2, i3 });
        }
    }
}

inline bool DecodeGodot4SurfaceRawBytes(
    const std::vector<uint8_t>& vData,
    uint32_t vertexCount,
    const std::vector<uint8_t>& iData,
    uint32_t indexCount,
    const float aabb[6],
    bool hasAabb,
    GdLocalMesh& out)
{
    if (vertexCount < 3 || vertexCount > 2000000 || vData.empty()) return false;
    const uint32_t baseV = static_cast<uint32_t>(out.verts.size());

    // Godot 4 uncompressed position stream: 12B float3 (or interleaved >= 12B where vertex_data == vertexCount * 12 or 16)
    if (vData.size() >= static_cast<size_t>(vertexCount) * 12 &&
        (vData.size() % vertexCount == 0) && (vData.size() / vertexCount == 12 || vData.size() / vertexCount >= 16))
    {
        size_t stride = vData.size() / vertexCount;
        // Note: in Godot 4 ArrayMesh non-interleaved attribute streams, positions come FIRST as vertexCount * 12 bytes
        // followed by normals/tangents! Check if float3 at 12B tight packing matches AABB or finite bounds:
        bool useTight12 = (stride != 12);
        if (useTight12 && hasAabb) {
            float f0x = GdF32(vData.data(), false), f0y = GdF32(vData.data() + 4, false), f0z = GdF32(vData.data() + 8, false);
            float f1x = GdF32(vData.data() + 12, false), f1y = GdF32(vData.data() + 16, false), f1z = GdF32(vData.data() + 20, false);
            if (!std::isfinite(f0x) || !std::isfinite(f1x)) useTight12 = false;
        }
        size_t posStride = useTight12 ? 12u : stride;
        for (uint32_t v = 0; v < vertexCount; ++v) {
            const uint8_t* vp = vData.data() + static_cast<size_t>(v) * posStride;
            float vx = GdF32(vp + 0, false), vy = GdF32(vp + 4, false), vz = GdF32(vp + 8, false);
            if (!std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(vz)) return false;
            out.verts.push_back({ vx, vy, vz });
        }
    }
    // Godot 4.2+ compressed uint16x4 AABB-normalized position stream (8B per vertex + optional 8B normal/tangent stream)
    else if (vData.size() >= static_cast<size_t>(vertexCount) * 8 && (vData.size() % vertexCount == 0)) {
        float ox = hasAabb ? aabb[0] : -0.5f;
        float oy = hasAabb ? aabb[1] : -0.5f;
        float oz = hasAabb ? aabb[2] : -0.5f;
        float sx = hasAabb ? aabb[3] : 1.0f;
        float sy = hasAabb ? aabb[4] : 1.0f;
        float sz = hasAabb ? aabb[5] : 1.0f;
        for (uint32_t v = 0; v < vertexCount; ++v) {
            const uint8_t* vp = vData.data() + static_cast<size_t>(v) * 8;
            float ux = static_cast<float>(GdU16(vp + 0, false)) / 65535.0f;
            float uy = static_cast<float>(GdU16(vp + 2, false)) / 65535.0f;
            float uz = static_cast<float>(GdU16(vp + 4, false)) / 65535.0f;
            out.verts.push_back({ ox + ux * sx, oy + uy * sy, oz + uz * sz });
        }
    } else {
        return false;
    }

    if (indexCount >= 3 && !iData.empty()) {
        if (vertexCount <= 65535 && iData.size() >= static_cast<size_t>(indexCount) * 2 && iData.size() < static_cast<size_t>(indexCount) * 4) {
            for (uint32_t t = 0; t + 2 < indexCount; t += 3) {
                uint32_t i0 = GdU16(iData.data() + static_cast<size_t>(t + 0) * 2, false);
                uint32_t i1 = GdU16(iData.data() + static_cast<size_t>(t + 1) * 2, false);
                uint32_t i2 = GdU16(iData.data() + static_cast<size_t>(t + 2) * 2, false);
                if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount) return false;
                if (i0 != i1 && i1 != i2 && i2 != i0) out.tris.push_back({ baseV + i0, baseV + i1, baseV + i2 });
            }
        } else if (iData.size() >= static_cast<size_t>(indexCount) * 4) {
            for (uint32_t t = 0; t + 2 < indexCount; t += 3) {
                uint32_t i0 = GdU32(iData.data() + static_cast<size_t>(t + 0) * 4, false);
                uint32_t i1 = GdU32(iData.data() + static_cast<size_t>(t + 1) * 4, false);
                uint32_t i2 = GdU32(iData.data() + static_cast<size_t>(t + 2) * 4, false);
                if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount) return false;
                if (i0 != i1 && i1 != i2 && i2 != i0) out.tris.push_back({ baseV + i0, baseV + i1, baseV + i2 });
            }
        } else {
            return false;
        }
    } else {
        for (uint32_t t = 0; t + 2 < vertexCount; t += 3) {
            out.tris.push_back({ baseV + t, baseV + t + 1, baseV + t + 2 });
        }
    }

    return !out.tris.empty();
}

inline void ParseFloatListInParens(std::string_view sv, size_t openParen, std::vector<float>& outFloats) {
    outFloats.clear();
    if (openParen >= sv.size() || sv[openParen] != '(') return;
    size_t closeParen = sv.find(')', openParen + 1);
    if (closeParen == std::string_view::npos) return;
    std::string_view body = sv.substr(openParen + 1, closeParen - openParen - 1);
    size_t i = 0;
    while (i < body.size()) {
        while (i < body.size() && (body[i] == ' ' || body[i] == '\t' || body[i] == '\r' || body[i] == '\n' || body[i] == ',')) ++i;
        if (i >= body.size()) break;
        size_t st = i;
        while (i < body.size() && body[i] != ' ' && body[i] != '\t' && body[i] != '\r' && body[i] != '\n' && body[i] != ',') ++i;
        float v = 0.0f;
        auto res = std::from_chars(body.data() + st, body.data() + i, v);
        if (res.ec == std::errc{} && std::isfinite(v)) outFloats.push_back(v);
    }
}

inline void ParseIntListInParens(std::string_view sv, size_t openParen, std::vector<int32_t>& outInts) {
    outInts.clear();
    if (openParen >= sv.size() || sv[openParen] != '(') return;
    size_t closeParen = sv.find(')', openParen + 1);
    if (closeParen == std::string_view::npos) return;
    std::string_view body = sv.substr(openParen + 1, closeParen - openParen - 1);
    size_t i = 0;
    while (i < body.size()) {
        while (i < body.size() && (body[i] == ' ' || body[i] == '\t' || body[i] == '\r' || body[i] == '\n' || body[i] == ',')) ++i;
        if (i >= body.size()) break;
        size_t st = i;
        while (i < body.size() && body[i] != ' ' && body[i] != '\t' && body[i] != '\r' && body[i] != '\n' && body[i] != ',') ++i;
        int32_t v = 0;
        auto res = std::from_chars(body.data() + st, body.data() + i, v);
        if (res.ec == std::errc{}) outInts.push_back(v);
    }
}

inline bool ExtractPackedByteArray(std::string_view sec, std::string_view key, std::vector<uint8_t>& outBytes) {
    outBytes.clear();
    size_t kp = sec.find(key);
    if (kp == std::string_view::npos) return false;
    size_t pba = sec.find("ByteArray(", kp);
    if (pba == std::string_view::npos) return false;
    size_t openP = pba + 9; // points at '('
    size_t closeP = sec.find(')', openP + 1);
    if (closeP == std::string_view::npos) return false;
    std::string_view inner = sec.substr(openP + 1, closeP - openP - 1);
    size_t q1 = inner.find('"');
    if (q1 != std::string_view::npos) {
        size_t q2 = inner.find('"', q1 + 1);
        if (q2 == std::string_view::npos) return false;
        return DecodeBase64Bytes(inner.substr(q1 + 1, q2 - q1 - 1), outBytes);
    }
    std::vector<int32_t> ints;
    ParseIntListInParens(sec, openP, ints);
    for (int32_t b : ints) {
        if (b < 0 || b > 255) return false;
        outBytes.push_back(static_cast<uint8_t>(b));
    }
    return !outBytes.empty();
}

inline bool ExtractKeyUint32(std::string_view sec, std::string_view key, uint32_t& outVal) {
    size_t kp = sec.find(key);
    if (kp == std::string_view::npos) return false;
    size_t sep = sec.find_first_of(":=", kp + key.size());
    if (sep == std::string_view::npos) return false;
    size_t i = sep + 1;
    while (i < sec.size() && (sec[i] == ' ' || sec[i] == '\t')) ++i;
    size_t st = i;
    while (i < sec.size() && sec[i] >= '0' && sec[i] <= '9') ++i;
    if (st == i) return false;
    auto res = std::from_chars(sec.data() + st, sec.data() + i, outVal);
    return res.ec == std::errc{};
}

inline float ExtractKeyFloatDef(std::string_view sec, std::string_view key, float defVal) {
    size_t kp = sec.find(key);
    if (kp == std::string_view::npos) return defVal;
    size_t eq = sec.find('=', kp + key.size());
    if (eq == std::string_view::npos || eq > sec.find('\n', kp)) return defVal;
    size_t i = eq + 1;
    while (i < sec.size() && (sec[i] == ' ' || sec[i] == '\t')) ++i;
    size_t st = i;
    while (i < sec.size() && ((sec[i] >= '0' && sec[i] <= '9') || sec[i] == '-' || sec[i] == '+' || sec[i] == '.' || sec[i] == 'e' || sec[i] == 'E')) ++i;
    float v = defVal;
    if (st < i && std::from_chars(sec.data() + st, sec.data() + i, v).ec == std::errc{} && std::isfinite(v)) return v;
    return defVal;
}

inline bool ParseMeshFromTextSection(std::string_view rType, std::string_view body, GdLocalMesh& outMesh) {
    // 1. Godot 4 ArrayMesh / ImporterMesh with PackedByteArray("base64") vertex_data + index_data
    if (body.find("vertex_data") != std::string_view::npos) {
        size_t cur = 0;
        bool anySurf = false;
        while (cur < body.size()) {
            size_t vdPos = body.find("\"vertex_data\"", cur);
            if (vdPos == std::string_view::npos) vdPos = body.find("vertex_data", cur);
            if (vdPos == std::string_view::npos) break;
            // Find enclosing '{' ... '}' dictionary block for this surface
            size_t openBrace = body.rfind('{', vdPos);
            size_t closeBrace = body.find('}', vdPos);
            if (openBrace == std::string_view::npos || closeBrace == std::string_view::npos || closeBrace <= openBrace) {
                break;
            }
            std::string_view surfDict = body.substr(openBrace, closeBrace - openBrace + 1);
            uint32_t vCnt = 0, iCnt = 0;
            std::vector<uint8_t> vBytes, iBytes;
            bool hasVCnt = ExtractKeyUint32(surfDict, "\"vertex_count\"", vCnt) || ExtractKeyUint32(surfDict, "vertex_count", vCnt);
            ExtractKeyUint32(surfDict, "\"index_count\"", iCnt) || ExtractKeyUint32(surfDict, "index_count", iCnt);
            bool hasVData = ExtractPackedByteArray(surfDict, "vertex_data", vBytes);
            ExtractPackedByteArray(surfDict, "index_data", iBytes);

            float aabb[6] = { -0.5f, -0.5f, -0.5f, 1.0f, 1.0f, 1.0f };
            bool hasAabb = false;
            size_t aabbPos = surfDict.find("AABB(");
            if (aabbPos != std::string_view::npos) {
                std::vector<float> af;
                ParseFloatListInParens(surfDict, aabbPos + 4, af);
                if (af.size() == 6) {
                    for (int k = 0; k < 6; ++k) aabb[k] = af[static_cast<size_t>(k)];
                    hasAabb = true;
                }
            }

            if (!hasVCnt || !hasVData || !DecodeGodot4SurfaceRawBytes(vBytes, vCnt, iBytes, iCnt, aabb, hasAabb, outMesh)) {
                return false;
            }
            anySurf = true;
            cur = closeBrace + 1;
        }
        if (anySurf) return !outMesh.empty();
    }

    // 2. Godot 3 / Godot 4 PoolVector3Array / PackedVector3Array + PoolIntArray / PackedInt32Array
    size_t v3Pos = body.find("Vector3Array(");
    if (v3Pos != std::string_view::npos) {
        size_t openV = v3Pos + 12;
        std::vector<float> vf;
        ParseFloatListInParens(body, openV, vf);
        if (vf.size() < 9 || (vf.size() % 3) != 0) return false;
        uint32_t baseV = static_cast<uint32_t>(outMesh.verts.size());
        uint32_t nVerts = static_cast<uint32_t>(vf.size() / 3);
        for (uint32_t i = 0; i < nVerts; ++i) {
            outMesh.verts.push_back({ vf[i * 3 + 0], vf[i * 3 + 1], vf[i * 3 + 2] });
        }

        size_t iPos = body.find("Int32Array(", openV);
        if (iPos != std::string_view::npos) {
            iPos += 10;
        } else {
            iPos = body.find("IntArray(", openV);
            if (iPos != std::string_view::npos) iPos += 8;
        }

        if (iPos != std::string_view::npos) {
            std::vector<int32_t> idx;
            ParseIntListInParens(body, iPos, idx);
            if (idx.size() < 3) return false;
            for (size_t t = 0; t + 2 < idx.size(); t += 3) {
                if (idx[t] < 0 || idx[t + 1] < 0 || idx[t + 2] < 0) return false;
                uint32_t i0 = static_cast<uint32_t>(idx[t]);
                uint32_t i1 = static_cast<uint32_t>(idx[t + 1]);
                uint32_t i2 = static_cast<uint32_t>(idx[t + 2]);
                if (i0 >= nVerts || i1 >= nVerts || i2 >= nVerts) return false;
                if (i0 != i1 && i1 != i2 && i2 != i0) outMesh.tris.push_back({ baseV + i0, baseV + i1, baseV + i2 });
            }
        } else {
            for (uint32_t t = 0; t + 2 < nVerts; t += 3) {
                outMesh.tris.push_back({ baseV + t, baseV + t + 1, baseV + t + 2 });
            }
        }
        return !outMesh.empty();
    }

    // 3. Parameterized Godot Primitive Meshes (BoxMesh/CubeMesh, SphereMesh, CylinderMesh, PrismMesh, PlaneMesh/QuadMesh)
    if (rType == "BoxMesh" || rType == "CubeMesh") {
        float sx = 1.0f, sy = 1.0f, sz = 1.0f;
        size_t sp = body.find("size");
        if (sp != std::string_view::npos) {
            size_t vp = body.find("Vector3(", sp);
            if (vp != std::string_view::npos) {
                std::vector<float> sf;
                ParseFloatListInParens(body, vp + 7, sf);
                if (sf.size() == 3) { sx = sf[0]; sy = sf[1]; sz = sf[2]; }
            }
        }
        BuildBoxPrimitive(sx, sy, sz, outMesh);
        return !outMesh.empty();
    }
    if (rType == "SphereMesh" || rType == "CapsuleMesh") {
        float rad = ExtractKeyFloatDef(body, "radius", 0.5f);
        float h   = ExtractKeyFloatDef(body, "height", rad * 2.0f);
        BuildSpherePrimitive(rad, h, outMesh);
        return !outMesh.empty();
    }
    if (rType == "CylinderMesh") {
        float tr = ExtractKeyFloatDef(body, "top_radius", 0.5f);
        float br = ExtractKeyFloatDef(body, "bottom_radius", 0.5f);
        float h  = ExtractKeyFloatDef(body, "height", 2.0f);
        BuildCylinderOrConePrimitive(tr, br, h, 16, outMesh);
        return !outMesh.empty();
    }
    if (rType == "PlaneMesh" || rType == "QuadMesh") {
        BuildBoxPrimitive(2.0f, 0.02f, 2.0f, outMesh);
        return !outMesh.empty();
    }

    return false;
}

inline std::string ExtractAttrQuoted(std::string_view tag, std::string_view key) {
    std::string pat(key);
    pat += "=\"";
    size_t p = tag.find(pat);
    if (p == std::string_view::npos) return {};
    p += pat.size();
    size_t q = tag.find('"', p);
    if (q == std::string_view::npos) return {};
    return std::string(tag.substr(p, q - p));
}

// Parse Godot Text Scene (.tscn) or Text Resource (.tres / text .mesh)
inline bool ParseGodotTextMeshOrScene(std::string_view sv, const std::wstring& fullPath, LoadedMesh3D& mesh) {
    size_t firstBracket = sv.find('[');
    if (firstBracket == std::string_view::npos) return false;
    size_t firstClose = sv.find(']', firstBracket + 1);
    if (firstClose == std::string_view::npos) return false;
    std::string_view rootTag = sv.substr(firstBracket + 1, firstClose - firstBracket - 1);
    const bool isScene = (rootTag.rfind("gd_scene", 0) == 0);
    const bool isRes   = (rootTag.rfind("gd_resource", 0) == 0);
    if (!isScene && !isRes) return false;

    struct Section {
        std::string_view header;
        std::string_view body;
    };
    std::vector<Section> sections;
    size_t pos = firstBracket;
    while (pos < sv.size()) {
        size_t closeB = sv.find(']', pos + 1);
        if (closeB == std::string_view::npos) break;
        std::string_view hdr = sv.substr(pos + 1, closeB - pos - 1);
        // Find next "\n[" section boundary at start of line
        size_t nextSec = std::string_view::npos;
        size_t scan = closeB + 1;
        while (scan < sv.size()) {
            size_t nl = sv.find('\n', scan);
            if (nl == std::string_view::npos) break;
            size_t p = nl + 1;
            while (p < sv.size() && (sv[p] == ' ' || sv[p] == '\t')) ++p;
            if (p < sv.size() && sv[p] == '[') {
                nextSec = p;
                break;
            }
            scan = nl + 1;
        }
        std::string_view body = (nextSec == std::string_view::npos)
            ? sv.substr(closeB + 1)
            : sv.substr(closeB + 1, nextSec - (closeB + 1));
        sections.push_back({ hdr, body });
        if (nextSec == std::string_view::npos) break;
        pos = nextSec;
    }

    if (isRes) {
        std::string rootType = ExtractAttrQuoted(rootTag, "type");
        GdLocalMesh lm;
        for (const auto& sec : sections) {
            if (sec.header.rfind("resource", 0) == 0 || sec.header.rfind("gd_resource", 0) == 0) {
                ParseMeshFromTextSection(rootType, sec.body, lm);
            } else if (sec.header.rfind("sub_resource", 0) == 0) {
                std::string st = ExtractAttrQuoted(sec.header, "type");
                ParseMeshFromTextSection(st, sec.body, lm);
            }
        }
        if (lm.empty()) return false;
        mesh.vertices = std::move(lm.verts);
        for (const auto& t : lm.tris) mesh.tris.push_back({ t[0], t[1], t[2], {} });
        mesh.formatName = L"Godot Engine Text Mesh Resource (.tres)";
        return true;
    }

    // Scene (.tscn): collect sub_resources, optional ext_resources, and instance [node] hierarchy with Transform3D!
    std::unordered_map<std::string, GdLocalMesh> subMeshes;
    std::unordered_map<std::string, GdLocalMesh> extMeshes;

    struct SceneNode {
        std::string path;
        std::string parentPath;
        GdXform3D localXf{};
        std::string subResId;
        std::string extResId;
    };
    std::vector<SceneNode> nodes;

    for (const auto& sec : sections) {
        if (sec.header.rfind("sub_resource", 0) == 0) {
            std::string sType = ExtractAttrQuoted(sec.header, "type");
            std::string sId   = ExtractAttrQuoted(sec.header, "id");
            GdLocalMesh lm;
            if (!sId.empty() && ParseMeshFromTextSection(sType, sec.body, lm)) {
                subMeshes[sId] = std::move(lm);
            }
        } else if (sec.header.rfind("ext_resource", 0) == 0 && !fullPath.empty()) {
            std::string eId   = ExtractAttrQuoted(sec.header, "id");
            std::string ePath = ExtractAttrQuoted(sec.header, "path");
            if (!eId.empty() && !ePath.empty()) {
                if (ePath.rfind("res://", 0) == 0) ePath.erase(0, 6);
                std::wstring relW = PathUtils::Utf8ToWide(ePath);
                for (wchar_t& wc : relW) if (wc == L'/') wc = L'\\';
                std::wstring dir = PathUtils::GetParentPath(fullPath);
                std::wstring cand = dir + L"\\" + relW;
                if (GetFileAttributesW(cand.c_str()) == INVALID_FILE_ATTRIBUTES) {
                    cand = dir + L"\\" + PathUtils::GetFileName(relW);
                }
                if (_wcsicmp(cand.c_str(), fullPath.c_str()) != 0 &&
                    GetFileAttributesW(cand.c_str()) != INVALID_FILE_ATTRIBUTES)
                {
                    LoadedMesh3D extLoaded;
                    if (Model3DViewer::InspectMeshFile(cand, extLoaded) && extLoaded.loadedOk) {
                        GdLocalMesh lm;
                        lm.verts = std::move(extLoaded.vertices);
                        for (const auto& t : extLoaded.tris) lm.tris.push_back({ t.i0, t.i1, t.i2 });
                        extMeshes[eId] = std::move(lm);
                    }
                }
            }
        } else if (sec.header.rfind("node", 0) == 0) {
            SceneNode sn;
            std::string nName   = ExtractAttrQuoted(sec.header, "name");
            std::string nParent = ExtractAttrQuoted(sec.header, "parent");
            if (nParent.empty()) {
                sn.path = ".";
            } else if (nParent == ".") {
                sn.path = nName;
                sn.parentPath = ".";
            } else {
                sn.path = nParent + "/" + nName;
                sn.parentPath = nParent;
            }

            size_t tfPos = sec.body.find("Transform3D(");
            size_t tfLen = 11;
            if (tfPos == std::string_view::npos) {
                tfPos = sec.body.find("Transform(");
                tfLen = 9;
            }
            if (tfPos != std::string_view::npos) {
                std::vector<float> tf;
                ParseFloatListInParens(sec.body, tfPos + tfLen, tf);
                if (tf.size() == 12) {
                    sn.localXf = GdXform3D::From12Floats(tf.data());
                }
            }

            // mesh = SubResource("...") or SubResource( 1 ) or ExtResource("...")
            size_t mp = sec.body.find("mesh =");
            if (mp == std::string_view::npos) mp = sec.body.find("mesh=");
            if (mp != std::string_view::npos) {
                size_t lineEnd = sec.body.find('\n', mp);
                std::string_view mLine = sec.body.substr(mp, (lineEnd == std::string_view::npos) ? std::string_view::npos : (lineEnd - mp));
                auto extractRefId = [](std::string_view ml, std::string_view fn) -> std::string {
                    size_t p = ml.find(fn);
                    if (p == std::string_view::npos) return {};
                    size_t op = ml.find('(', p);
                    size_t cp = ml.find(')', op == std::string_view::npos ? p : op);
                    if (op == std::string_view::npos || cp == std::string_view::npos || cp <= op + 1) return {};
                    std::string_view inside = ml.substr(op + 1, cp - op - 1);
                    while (!inside.empty() && (inside.front() == ' ' || inside.front() == '"')) inside.remove_prefix(1);
                    while (!inside.empty() && (inside.back() == ' ' || inside.back() == '"')) inside.remove_suffix(1);
                    return std::string(inside);
                };
                sn.subResId = extractRefId(mLine, "SubResource");
                sn.extResId = extractRefId(mLine, "ExtResource");
            }
            nodes.push_back(std::move(sn));
        }
    }

    std::unordered_map<std::string, GdXform3D> worldByPath;
    for (const auto& sn : nodes) {
        GdXform3D wxf = sn.localXf;
        if (!sn.parentPath.empty()) {
            auto pit = worldByPath.find(sn.parentPath);
            if (pit != worldByPath.end()) wxf = GdXform3D::Mul(pit->second, sn.localXf);
        }
        worldByPath[sn.path] = wxf;

        const GdLocalMesh* srcMesh = nullptr;
        if (!sn.subResId.empty()) {
            auto it = subMeshes.find(sn.subResId);
            if (it != subMeshes.end()) srcMesh = &it->second;
        } else if (!sn.extResId.empty()) {
            auto it = extMeshes.find(sn.extResId);
            if (it != extMeshes.end()) srcMesh = &it->second;
        }
        if (srcMesh && !srcMesh->empty()) {
            uint32_t baseV = static_cast<uint32_t>(mesh.vertices.size());
            for (const auto& v : srcMesh->verts) mesh.vertices.push_back(wxf.Apply(v));
            for (const auto& t : srcMesh->tris) mesh.tris.push_back({ baseV + t[0], baseV + t[1], baseV + t[2], {} });
        }
    }

    // If scene had sub_resource meshes not explicitly bound (e.g. CSG/MultiMesh), emit unreferenced sub_resources once
    if (mesh.vertices.empty() && !subMeshes.empty()) {
        for (const auto& kv : subMeshes) {
            if (kv.second.empty()) continue;
            uint32_t baseV = static_cast<uint32_t>(mesh.vertices.size());
            for (const auto& v : kv.second.verts) mesh.vertices.push_back(v);
            for (const auto& t : kv.second.tris) mesh.tris.push_back({ baseV + t[0], baseV + t[1], baseV + t[2], {} });
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"Godot Engine 3D Scene (.tscn)";
    return true;
}

// ============================================================================
// Godot Binary Resource / Scene ("RSRC" & "RSCC" — .res, .scn, binary .mesh)
// ============================================================================
struct GdBinReader {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t pos = 0;
    bool be = false;
    bool real64 = false;
    uint32_t verMajor = 4;

    bool CanRead(size_t n) const { return pos <= size && n <= size - pos; }
    bool ReadU32(uint32_t& v) { if (!CanRead(4)) return false; v = GdU32(data + pos, be); pos += 4; return true; }
    bool ReadU64(uint64_t& v) { if (!CanRead(8)) return false; v = GdU64(data + pos, be); pos += 8; return true; }
    bool ReadF32(float& v)    { if (!CanRead(4)) return false; v = GdF32(data + pos, be); pos += 4; return true; }
    bool ReadReal(float& v) {
        if (real64) {
            if (!CanRead(8)) return false;
            v = static_cast<float>(GdF64(data + pos, be));
            pos += 8;
            return std::isfinite(v);
        }
        if (!ReadF32(v)) return false;
        return std::isfinite(v);
    }
    bool ReadUnicodeString(std::string& out) {
        uint32_t len = 0;
        if (!ReadU32(len) || len > 1048576 || !CanRead(len)) return false;
        out.assign(reinterpret_cast<const char*>(data + pos), len);
        pos += len;
        while (!out.empty() && out.back() == '\0') out.pop_back();
        return true;
    }
};

struct GdBinVal {
    enum Kind { kOther, kInt, kFloat, kStr, kVec3, kAabb, kXform, kRawBytes, kIntArr, kVec3Arr, kSubResIdx, kDict, kArr } kind = kOther;
    int64_t iVal = 0;
    float fVal = 0.0f;
    std::string sVal;
    float f12[12] = {};
    std::vector<uint8_t> rawBytes;
    std::vector<int32_t> intArr;
    std::vector<Vec3f> vec3Arr;
    std::vector<std::pair<std::string, GdBinVal>> dict;
    std::vector<GdBinVal> arr;
};

inline bool ParseGodotBinaryVariant(
    GdBinReader& r,
    const std::vector<std::string>& strTable,
    GdBinVal& out,
    int depth = 0)
{
    if (depth > 24) return false;
    uint32_t typeTag = 0;
    if (!r.ReadU32(typeTag)) return false;
    const bool v4 = (r.verMajor >= 4);

    auto resolveStrOrName = [&](std::string& sOut) -> bool {
        uint32_t v = 0;
        if (!r.ReadU32(v)) return false;
        if (v & 0x80000000u) {
            uint32_t idx = v & 0x7FFFFFFFu;
            if (idx < strTable.size()) sOut = strTable[idx];
            return true;
        }
        r.pos -= 4;
        return r.ReadUnicodeString(sOut);
    };

    if (typeTag == 1) { // NIL
        out.kind = GdBinVal::kOther;
        return true;
    }
    if (typeTag == 2 || typeTag == 3) { // BOOL / INT32
        uint32_t iv = 0;
        if (!r.ReadU32(iv)) return false;
        out.kind = GdBinVal::kInt;
        out.iVal = static_cast<int32_t>(iv);
        return true;
    }
    if (typeTag == 40) { // INT64
        uint64_t iv = 0;
        if (!r.ReadU64(iv)) return false;
        out.kind = GdBinVal::kInt;
        out.iVal = static_cast<int64_t>(iv);
        return true;
    }
    if (typeTag == 4) { // FLOAT (real)
        float fv = 0.0f;
        if (!r.ReadReal(fv)) return false;
        out.kind = GdBinVal::kFloat;
        out.fVal = fv;
        return true;
    }
    if (typeTag == 41) { // DOUBLE
        if (!r.CanRead(8)) return false;
        out.kind = GdBinVal::kFloat;
        out.fVal = static_cast<float>(GdF64(r.data + r.pos, r.be));
        r.pos += 8;
        return std::isfinite(out.fVal);
    }
    if (typeTag == 5 || (v4 && typeTag == 44)) { // STRING / STRING_NAME
        out.kind = GdBinVal::kStr;
        if (v4 && typeTag == 44) return resolveStrOrName(out.sVal);
        return r.ReadUnicodeString(out.sVal);
    }
    if (typeTag == 6) { float a, b; return r.ReadReal(a) && r.ReadReal(b); } // VECTOR2
    if (typeTag == 7 || typeTag == 10 || typeTag == 11 || (v4 && typeTag == 50)) { // RECT2 / PLANE / QUAT / VECTOR4
        float a, b, c, d;
        return r.ReadReal(a) && r.ReadReal(b) && r.ReadReal(c) && r.ReadReal(d);
    }
    if (typeTag == 8) { // VECTOR3
        out.kind = GdBinVal::kVec3;
        return r.ReadReal(out.f12[0]) && r.ReadReal(out.f12[1]) && r.ReadReal(out.f12[2]);
    }
    if (typeTag == 9) { // TRANSFORM2D (6 reals)
        float t;
        for (int i = 0; i < 6; ++i) if (!r.ReadReal(t)) return false;
        return true;
    }
    if (typeTag == 12) { // AABB (6 reals: pos + size)
        out.kind = GdBinVal::kAabb;
        for (int i = 0; i < 6; ++i) if (!r.ReadReal(out.f12[i])) return false;
        return true;
    }
    if (typeTag == 13) { // BASIS (9 reals)
        float t;
        for (int i = 0; i < 9; ++i) if (!r.ReadReal(t)) return false;
        return true;
    }
    if (typeTag == 14) { // TRANSFORM3D (12 reals: 9 basis + 3 origin)
        out.kind = GdBinVal::kXform;
        // Binary RSRC stores Basis rows (row0[3], row1[3], row2[3]) + origin[3]:
        float rf[12] = {};
        for (int i = 0; i < 12; ++i) if (!r.ReadReal(rf[i])) return false;
        // Convert Basis rows + origin into our column-compatible f12 layout used by From12Floats:
        // Basis row0=(m00,m01,m02), row1=(m10,m11,m12), row2=(m20,m21,m22); From12Floats expects cols (m00,m10,m20, m01,m11,m21, m02,m12,m22, ox,oy,oz)
        out.f12[0] = rf[0]; out.f12[1] = rf[3]; out.f12[2] = rf[6];
        out.f12[3] = rf[1]; out.f12[4] = rf[4]; out.f12[5] = rf[7];
        out.f12[6] = rf[2]; out.f12[7] = rf[5]; out.f12[8] = rf[8];
        out.f12[9] = rf[9]; out.f12[10] = rf[10]; out.f12[11] = rf[11];
        return true;
    }
    if (typeTag == 15) { // COLOR (Always 4 float32s in Godot RSRC!)
        float c;
        for (int i = 0; i < 4; ++i) if (!r.ReadF32(c)) return false;
        return true;
    }
    // NODE_PATH (17 in G3, 16 in G4)
    if ((!v4 && typeTag == 17) || (v4 && typeTag == 16)) {
        uint32_t npHead = 0;
        if (!r.ReadU32(npHead)) return false;
        if (npHead & 0x80000000u) {
            uint32_t nNames = npHead & 0x7FFFFFFFu;
            uint32_t nSub = 0, npFlags = 0;
            if (!r.ReadU32(nSub) || !r.ReadU32(npFlags)) return false;
            if ((nNames + nSub) > 4096) return false;
            for (uint32_t i = 0; i < nNames + nSub; ++i) {
                std::string dummy;
                if (!resolveStrOrName(dummy)) return false;
            }
            return true;
        }
        r.pos -= 4;
        std::string s;
        return r.ReadUnicodeString(s);
    }
    // RID (18 in G3, 17 in G4)
    if ((!v4 && typeTag == 18) || (v4 && typeTag == 17)) {
        uint32_t id = 0;
        return r.ReadU32(id);
    }
    // OBJECT (19 in G3, 18 in G4)
    if ((!v4 && typeTag == 19) || (v4 && typeTag == 18)) {
        uint32_t objKind = 0;
        if (!r.ReadU32(objKind)) return false;
        if (objKind == 0) return true; // OBJECT_EMPTY
        if (objKind == 1) { // OBJECT_EXTERNAL_RESOURCE
            std::string t, p;
            return r.ReadUnicodeString(t) && r.ReadUnicodeString(p);
        }
        if (objKind == 2) { // OBJECT_INTERNAL_RESOURCE
            uint32_t idx = 0;
            if (!r.ReadU32(idx)) return false;
            out.kind = GdBinVal::kSubResIdx;
            out.iVal = idx;
            return true;
        }
        if (objKind == 3) { // OBJECT_EXTERNAL_RESOURCE_INDEX
            uint32_t idx = 0;
            return r.ReadU32(idx);
        }
        return false;
    }
    // DICTIONARY (21 in G3, 19 in G4)
    if ((!v4 && typeTag == 21) || (v4 && typeTag == 19)) {
        uint32_t cnt = 0;
        if (!r.ReadU32(cnt)) return false;
        cnt &= 0x7FFFFFFFu;
        if (cnt > 65536) return false;
        out.kind = GdBinVal::kDict;
        out.dict.reserve(cnt);
        for (uint32_t i = 0; i < cnt; ++i) {
            GdBinVal k, v;
            if (!ParseGodotBinaryVariant(r, strTable, k, depth + 1)) return false;
            if (!ParseGodotBinaryVariant(r, strTable, v, depth + 1)) return false;
            out.dict.push_back({ k.sVal, std::move(v) });
        }
        return true;
    }
    // ARRAY (30 in G3, 20 in G4)
    if ((!v4 && typeTag == 30) || (v4 && typeTag == 20)) {
        uint32_t cnt = 0;
        if (!r.ReadU32(cnt)) return false;
        cnt &= 0x7FFFFFFFu;
        if (cnt > 200000) return false;
        out.kind = GdBinVal::kArr;
        out.arr.resize(cnt);
        for (uint32_t i = 0; i < cnt; ++i) {
            if (!ParseGodotBinaryVariant(r, strTable, out.arr[i], depth + 1)) return false;
        }
        return true;
    }
    // RAW_ARRAY / PackedByteArray (31 in G3, 21 in G4)
    if ((!v4 && typeTag == 31) || (v4 && typeTag == 21)) {
        uint32_t len = 0;
        if (!r.ReadU32(len) || len > 128 * 1024 * 1024 || !r.CanRead(len)) return false;
        out.kind = GdBinVal::kRawBytes;
        out.rawBytes.assign(r.data + r.pos, r.data + r.pos + len);
        r.pos += len;
        size_t pad = (4 - (len & 3u)) & 3u;
        if (r.CanRead(pad)) r.pos += pad;
        return true;
    }
    // INT32_ARRAY (32 in G3, 22 in G4)
    if ((!v4 && typeTag == 32) || (v4 && typeTag == 22)) {
        uint32_t len = 0;
        if (!r.ReadU32(len) || len > 16 * 1024 * 1024 || !r.CanRead(static_cast<size_t>(len) * 4)) return false;
        out.kind = GdBinVal::kIntArr;
        out.intArr.resize(len);
        for (uint32_t i = 0; i < len; ++i) out.intArr[i] = static_cast<int32_t>(GdU32(r.data + r.pos + i * 4, r.be));
        r.pos += static_cast<size_t>(len) * 4;
        return true;
    }
    // FLOAT32_ARRAY (33 in G3, 23 in G4)
    if ((!v4 && typeTag == 33) || (v4 && typeTag == 23)) {
        uint32_t len = 0;
        if (!r.ReadU32(len) || len > 16 * 1024 * 1024 || !r.CanRead(static_cast<size_t>(len) * 4)) return false;
        r.pos += static_cast<size_t>(len) * 4;
        return true;
    }
    // STRING_ARRAY (34 in G3, 24 in G4)
    if ((!v4 && typeTag == 34) || (v4 && typeTag == 24)) {
        uint32_t len = 0;
        if (!r.ReadU32(len) || len > 1000000) return false;
        std::string tmp;
        for (uint32_t i = 0; i < len; ++i) if (!r.ReadUnicodeString(tmp)) return false;
        return true;
    }
    // VECTOR3_ARRAY (35 in G3, 25 in G4)
    if ((!v4 && typeTag == 35) || (v4 && typeTag == 25)) {
        uint32_t len = 0;
        size_t elemSz = r.real64 ? 24u : 12u;
        if (!r.ReadU32(len) || len > 4000000 || !r.CanRead(static_cast<size_t>(len) * elemSz)) return false;
        out.kind = GdBinVal::kVec3Arr;
        out.vec3Arr.resize(len);
        for (uint32_t i = 0; i < len; ++i) {
            float x, y, z;
            if (!r.ReadReal(x) || !r.ReadReal(y) || !r.ReadReal(z)) return false;
            out.vec3Arr[i] = { x, y, z };
        }
        return true;
    }
    // COLOR_ARRAY (36 in G3, 26 in G4)
    if ((!v4 && typeTag == 36) || (v4 && typeTag == 26)) {
        uint32_t len = 0;
        if (!r.ReadU32(len) || len > 4000000 || !r.CanRead(static_cast<size_t>(len) * 16)) return false;
        r.pos += static_cast<size_t>(len) * 16;
        return true;
    }
    // VECTOR2_ARRAY (37 in G3, 27 in G4)
    if ((!v4 && typeTag == 37) || (v4 && typeTag == 27)) {
        uint32_t len = 0;
        size_t elemSz = r.real64 ? 16u : 8u;
        if (!r.ReadU32(len) || len > 4000000 || !r.CanRead(static_cast<size_t>(len) * elemSz)) return false;
        r.pos += static_cast<size_t>(len) * elemSz;
        return true;
    }
    if (v4 && (typeTag == 45 || typeTag == 46 || typeTag == 47 || typeTag == 51)) {
        size_t nInts = (typeTag == 45) ? 2u : (typeTag == 47 ? 3u : 4u);
        if (!r.CanRead(nInts * 4)) return false;
        r.pos += nInts * 4;
        return true;
    }
    if (v4 && (typeTag == 48 || typeTag == 49)) { // INT64_ARRAY / FLOAT64_ARRAY
        uint32_t len = 0;
        if (!r.ReadU32(len) || len > 8000000 || !r.CanRead(static_cast<size_t>(len) * 8)) return false;
        r.pos += static_cast<size_t>(len) * 8;
        return true;
    }
    return false;
}

inline bool ExtractGodotBinarySurfaceDict(const GdBinVal& d, GdLocalMesh& outMesh) {
    if (d.kind != GdBinVal::kDict) return false;
    // Check Godot 4 surface dictionary ("vertex_data", "vertex_count", "index_data", "index_count", "aabb")
    const std::vector<uint8_t>* vData = nullptr;
    const std::vector<uint8_t>* iData = nullptr;
    uint32_t vCnt = 0, iCnt = 0;
    float aabb[6] = { -0.5f, -0.5f, -0.5f, 1.0f, 1.0f, 1.0f };
    bool hasAabb = false;

    // Also check Godot 3 surface dictionary ("arrays": Array where [0] = VECTOR3_ARRAY and [8 or 12] = INT_ARRAY)
    const GdBinVal* g3Arrays = nullptr;

    for (const auto& kv : d.dict) {
        if (kv.first == "vertex_data" && kv.second.kind == GdBinVal::kRawBytes) vData = &kv.second.rawBytes;
        else if (kv.first == "index_data" && kv.second.kind == GdBinVal::kRawBytes) iData = &kv.second.rawBytes;
        else if (kv.first == "vertex_count" && kv.second.kind == GdBinVal::kInt && kv.second.iVal > 0) vCnt = static_cast<uint32_t>(kv.second.iVal);
        else if (kv.first == "index_count" && kv.second.kind == GdBinVal::kInt && kv.second.iVal > 0) iCnt = static_cast<uint32_t>(kv.second.iVal);
        else if (kv.first == "aabb" && kv.second.kind == GdBinVal::kAabb) {
            for (int k = 0; k < 6; ++k) aabb[k] = kv.second.f12[k];
            hasAabb = true;
        }
        else if (kv.first == "arrays" && kv.second.kind == GdBinVal::kArr) g3Arrays = &kv.second;
    }

    if (vData && vCnt >= 3) {
        static const std::vector<uint8_t> kEmpty;
        return DecodeGodot4SurfaceRawBytes(*vData, vCnt, iData ? *iData : kEmpty, iCnt, aabb, hasAabb, outMesh);
    }

    if (g3Arrays && !g3Arrays->arr.empty() && g3Arrays->arr[0].kind == GdBinVal::kVec3Arr) {
        const auto& va = g3Arrays->arr[0].vec3Arr;
        if (va.size() < 3) return false;
        uint32_t baseV = static_cast<uint32_t>(outMesh.verts.size());
        outMesh.verts.insert(outMesh.verts.end(), va.begin(), va.end());
        const std::vector<int32_t>* ia = nullptr;
        // ARRAY_INDEX = 8 in Godot 3, 12 in Godot 4
        if (g3Arrays->arr.size() > 12 && g3Arrays->arr[12].kind == GdBinVal::kIntArr) ia = &g3Arrays->arr[12].intArr;
        else if (g3Arrays->arr.size() > 8 && g3Arrays->arr[8].kind == GdBinVal::kIntArr) ia = &g3Arrays->arr[8].intArr;
        if (ia && ia->size() >= 3) {
            for (size_t t = 0; t + 2 < ia->size(); t += 3) {
                int32_t i0 = (*ia)[t], i1 = (*ia)[t + 1], i2 = (*ia)[t + 2];
                if (i0 < 0 || i1 < 0 || i2 < 0 ||
                    static_cast<size_t>(i0) >= va.size() ||
                    static_cast<size_t>(i1) >= va.size() ||
                    static_cast<size_t>(i2) >= va.size()) return false;
                if (i0 != i1 && i1 != i2 && i2 != i0) {
                    outMesh.tris.push_back({ baseV + static_cast<uint32_t>(i0), baseV + static_cast<uint32_t>(i1), baseV + static_cast<uint32_t>(i2) });
                }
            }
        } else {
            for (uint32_t t = 0; t + 2 < va.size(); t += 3) outMesh.tris.push_back({ baseV + t, baseV + t + 1, baseV + t + 2 });
        }
        return !outMesh.empty();
    }

    return false;
}

inline bool DecompressGodotRscc(const uint8_t* data, size_t size, std::vector<uint8_t>& outRsrc) {
    outRsrc.clear();
    if (!data || size < 24 || std::memcmp(data, "RSCC", 4) != 0) return false;
    uint32_t compMode  = GdU32(data + 4, false);
    uint32_t blockSize = GdU32(data + 8, false);
    uint32_t readTotal = GdU32(data + 12, false);
    if (blockSize == 0 || blockSize > 16 * 1024 * 1024 || readTotal < 24 || readTotal > 256 * 1024 * 1024) {
        return false;
    }
    uint32_t blockCount = (readTotal + blockSize - 1u) / blockSize;
    if (blockCount == 0 || blockCount > 262144 || 16ULL + static_cast<uint64_t>(blockCount) * 4ULL > size) {
        return false;
    }

    // Godot FileAccessCompressed stores "RSCC" header in place of "RSRC", compressing everything after "RSRC" (or full "RSRC")
    std::vector<uint8_t> rawOut;
    rawOut.reserve(readTotal);
    size_t cPos = 16 + static_cast<size_t>(blockCount) * 4;
    for (uint32_t b = 0; b < blockCount; ++b) {
        uint32_t cSz = GdU32(data + 16 + static_cast<size_t>(b) * 4, false);
        if (cPos + cSz > size) return false;
        uint32_t expectUnp = (b + 1 == blockCount) ? (readTotal - b * blockSize) : blockSize;
        std::vector<uint8_t> chunk;
        if (compMode == 1) { // COMPRESSION_DEFLATE (zlib stream in Godot!)
            if (!ZipDeflate::InflateZlib(data + cPos, cSz, chunk) &&
                !ZipDeflate::InflateRaw(data + cPos, cSz, chunk)) {
                return false;
            }
        } else if (compMode == 2) { // COMPRESSION_ZSTD
            if (!StreamCodecs::DecompressZstdFrame(data + cPos, cSz, chunk, expectUnp + 64)) return false;
        } else if (compMode == 3) { // COMPRESSION_GZIP
            if (!ZipDeflate::InflateGzip(data + cPos, cSz, chunk)) return false;
        } else {
            return false;
        }
        if (chunk.size() != expectUnp) return false;
        rawOut.insert(rawOut.end(), chunk.begin(), chunk.end());
        cPos += cSz;
    }

    if (rawOut.size() >= 4 && std::memcmp(rawOut.data(), "RSRC", 4) == 0) {
        outRsrc = std::move(rawOut);
    } else {
        outRsrc.reserve(4 + rawOut.size());
        outRsrc.push_back('R'); outRsrc.push_back('S'); outRsrc.push_back('R'); outRsrc.push_back('C');
        outRsrc.insert(outRsrc.end(), rawOut.begin(), rawOut.end());
    }
    return true;
}

inline bool ParseGodotBinaryRsrc(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    std::vector<uint8_t> decomp;
    if (size >= 24 && std::memcmp(data, "RSCC", 4) == 0) {
        if (!DecompressGodotRscc(data, size, decomp)) return false;
        data = decomp.data();
        size = decomp.size();
    }
    if (!data || size < 64 || std::memcmp(data, "RSRC", 4) != 0) return false;

    GdBinReader r;
    r.data = data;
    r.size = size;
    r.pos  = 4;
    uint32_t bigEnd = 0;
    if (GdU32(data + 4, false) > 1 && GdU32(data + 4, true) == 1) bigEnd = 1;
    else bigEnd = GdU32(data + 4, false);
    if (bigEnd > 1) return false;
    r.be = (bigEnd != 0);
    r.pos = 8;

    uint32_t real64Flag = 0, verMaj = 0, verMin = 0, verFmt = 0;
    if (!r.ReadU32(real64Flag) || !r.ReadU32(verMaj) || !r.ReadU32(verMin) || !r.ReadU32(verFmt)) return false;
    if (real64Flag > 1 || verMaj < 2 || verMaj > 5 || verFmt < 1 || verFmt > 12) return false;
    r.real64 = (real64Flag != 0);
    r.verMajor = verMaj;

    std::string mainResType;
    uint64_t importMdOfs = 0;
    uint32_t flags = 0;
    if (!r.ReadUnicodeString(mainResType) || !r.ReadU64(importMdOfs) || !r.ReadU32(flags)) return false;

    // Skip UID(8B) + optional script_class string + 9 reserved uint32s (= 44B without script_class, matching Godot 3's 11 reserved uint32s)
    if (!r.CanRead(8)) return false;
    r.pos += 8;
    if (verMaj >= 4 && (flags & 8u) != 0) {
        std::string scriptCls;
        if (!r.ReadUnicodeString(scriptCls)) return false;
    }
    if (!r.CanRead(36)) return false;
    r.pos += 36;

    uint32_t strCount = 0;
    if (!r.ReadU32(strCount) || strCount > 500000) return false;
    std::vector<std::string> strTable(strCount);
    for (uint32_t i = 0; i < strCount; ++i) {
        if (!r.ReadUnicodeString(strTable[i])) return false;
    }

    uint32_t extCount = 0;
    if (!r.ReadU32(extCount) || extCount > 100000) return false;
    for (uint32_t i = 0; i < extCount; ++i) {
        std::string et, ep;
        if (!r.ReadUnicodeString(et) || !r.ReadUnicodeString(ep)) return false;
        if (verMaj >= 4 && (flags & 2u) != 0) {
            uint64_t uid = 0;
            if (!r.ReadU64(uid)) return false;
        }
    }

    uint32_t intCount = 0;
    if (!r.ReadU32(intCount) || intCount == 0 || intCount > 100000) return false;
    struct IntResEntry {
        std::string path;
        uint64_t offset = 0;
        uint32_t localIndex = 0;
    };
    std::vector<IntResEntry> intEntries(intCount);
    for (uint32_t i = 0; i < intCount; ++i) {
        if (!r.ReadUnicodeString(intEntries[i].path) || !r.ReadU64(intEntries[i].offset)) return false;
        if (intEntries[i].offset < r.pos || intEntries[i].offset >= size) return false;
        intEntries[i].localIndex = i + 1;
    }

    std::unordered_map<uint32_t, GdLocalMesh> parsedIntMeshes;
    bool hasBundledScene = false;

    for (uint32_t i = 0; i < intCount; ++i) {
        r.pos = static_cast<size_t>(intEntries[i].offset);
        std::string rType;
        uint32_t propCount = 0;
        if (!r.ReadUnicodeString(rType) || !r.ReadU32(propCount) || propCount > 65536) return false;

        std::vector<std::pair<std::string, GdBinVal>> props;
        props.reserve(propCount);
        for (uint32_t p = 0; p < propCount; ++p) {
            uint32_t nameIdx = 0;
            if (!r.ReadU32(nameIdx) || nameIdx >= strTable.size()) return false;
            GdBinVal val;
            if (!ParseGodotBinaryVariant(r, strTable, val, 0)) return false;
            props.push_back({ strTable[nameIdx], std::move(val) });
        }

        GdLocalMesh lm;
        if (rType == "ArrayMesh" || rType == "ImporterMesh" || rType == "Mesh") {
            for (const auto& pr : props) {
                if ((pr.first == "_surfaces" || pr.first == "surfaces") && pr.second.kind == GdBinVal::kArr) {
                    for (const auto& sEl : pr.second.arr) {
                        if (!ExtractGodotBinarySurfaceDict(sEl, lm)) return false;
                    }
                } else if (pr.first.rfind("surfaces/", 0) == 0 && pr.second.kind == GdBinVal::kDict) {
                    if (!ExtractGodotBinarySurfaceDict(pr.second, lm)) return false;
                }
            }
        } else if (rType == "BoxMesh" || rType == "CubeMesh") {
            float sx = 1.0f, sy = 1.0f, sz = 1.0f;
            for (const auto& pr : props) {
                if (pr.first == "size" && pr.second.kind == GdBinVal::kVec3) {
                    sx = pr.second.f12[0]; sy = pr.second.f12[1]; sz = pr.second.f12[2];
                }
            }
            BuildBoxPrimitive(sx, sy, sz, lm);
        } else if (rType == "SphereMesh" || rType == "CapsuleMesh") {
            float rad = 0.5f, h = 1.0f;
            for (const auto& pr : props) {
                if (pr.first == "radius" && pr.second.kind == GdBinVal::kFloat) rad = pr.second.fVal;
                else if (pr.first == "height" && pr.second.kind == GdBinVal::kFloat) h = pr.second.fVal;
            }
            BuildSpherePrimitive(rad, h, lm);
        } else if (rType == "CylinderMesh") {
            float tr = 0.5f, br = 0.5f, h = 2.0f;
            for (const auto& pr : props) {
                if (pr.first == "top_radius" && pr.second.kind == GdBinVal::kFloat) tr = pr.second.fVal;
                else if (pr.first == "bottom_radius" && pr.second.kind == GdBinVal::kFloat) br = pr.second.fVal;
                else if (pr.first == "height" && pr.second.kind == GdBinVal::kFloat) h = pr.second.fVal;
            }
            BuildCylinderOrConePrimitive(tr, br, h, 16, lm);
        } else if (rType == "PackedScene") {
            // PackedScene "_bundled" Dictionary: "names" (StringArray or Array), "variants" (Array), "nodes" (Int32Array)
            for (const auto& pr : props) {
                if (pr.first == "_bundled" && pr.second.kind == GdBinVal::kDict) {
                    const GdBinVal* variantsVal = nullptr;
                    const std::vector<int32_t>* nodesArr = nullptr;
                    for (const auto& bk : pr.second.dict) {
                        if (bk.first == "variants" && bk.second.kind == GdBinVal::kArr) variantsVal = &bk.second;
                        else if (bk.first == "nodes" && bk.second.kind == GdBinVal::kIntArr) nodesArr = &bk.second.intArr;
                    }
                    if (variantsVal && nodesArr && nodesArr->size() >= 5) {
                        hasBundledScene = true;
                        // Walk PackedScene packed int stream to pair Transform3D + SubResource mesh per node!
                        size_t ni = 0;
                        std::vector<GdXform3D> nodeWorld;
                        while (ni + 5 <= nodesArr->size()) {
                            int32_t parentIdx = (*nodesArr)[ni + 0];
                            int32_t ownerIdx  = (*nodesArr)[ni + 1];
                            (void)ownerIdx;
                            int32_t nPropCnt  = (*nodesArr)[ni + 4];
                            ni += 5;
                            if (nPropCnt < 0 || ni + static_cast<size_t>(nPropCnt) * 2 + 1 > nodesArr->size()) break;

                            GdXform3D localXf{};
                            uint32_t meshSubIdx = 0;
                            for (int32_t pi = 0; pi < nPropCnt; ++pi) {
                                int32_t vIdx = (*nodesArr)[ni + static_cast<size_t>(pi) * 2 + 1];
                                if (vIdx >= 0 && static_cast<size_t>(vIdx) < variantsVal->arr.size()) {
                                    const GdBinVal& vv = variantsVal->arr[static_cast<size_t>(vIdx)];
                                    if (vv.kind == GdBinVal::kXform) localXf = GdXform3D::From12Floats(vv.f12);
                                    else if (vv.kind == GdBinVal::kSubResIdx) meshSubIdx = static_cast<uint32_t>(vv.iVal);
                                }
                            }
                            ni += static_cast<size_t>(nPropCnt) * 2;
                            int32_t nGroups = (*nodesArr)[ni++];
                            if (nGroups < 0 || ni + static_cast<size_t>(nGroups) > nodesArr->size()) break;
                            ni += static_cast<size_t>(nGroups);

                            int32_t realParent = (parentIdx >= 0) ? (parentIdx & 0x7FFFFFFF) : -1;
                            GdXform3D wxf = (realParent >= 0 && static_cast<size_t>(realParent) < nodeWorld.size())
                                ? GdXform3D::Mul(nodeWorld[static_cast<size_t>(realParent)], localXf)
                                : localXf;
                            nodeWorld.push_back(wxf);

                            auto mit = parsedIntMeshes.find(meshSubIdx);
                            if (mit != parsedIntMeshes.end() && !mit->second.empty()) {
                                uint32_t baseV = static_cast<uint32_t>(mesh.vertices.size());
                                for (const auto& v : mit->second.verts) mesh.vertices.push_back(wxf.Apply(v));
                                for (const auto& t : mit->second.tris) mesh.tris.push_back({ baseV + t[0], baseV + t[1], baseV + t[2], {} });
                            }
                        }
                    }
                }
            }
        }

        if (!lm.empty()) {
            parsedIntMeshes[intEntries[i].localIndex] = std::move(lm);
        }
    }

    if (mesh.vertices.empty()) {
        for (uint32_t i = 0; i < intCount; ++i) {
            auto it = parsedIntMeshes.find(intEntries[i].localIndex);
            if (it == parsedIntMeshes.end() || it->second.empty()) continue;
            uint32_t baseV = static_cast<uint32_t>(mesh.vertices.size());
            for (const auto& v : it->second.verts) mesh.vertices.push_back(v);
            for (const auto& t : it->second.tris) mesh.tris.push_back({ baseV + t[0], baseV + t[1], baseV + t[2], {} });
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = hasBundledScene
        ? L"Godot Engine Binary Scene (.scn)"
        : L"Godot Engine Binary Mesh Resource (.res / .mesh)";
    return true;
}

} // namespace GodotDetail

inline bool IsLikelyGodotResourceHeader(const uint8_t* data, size_t size) {
    if (!data || size < 12) return false;
    if (std::memcmp(data, "RSRC", 4) == 0 || std::memcmp(data, "RSCC", 4) == 0) return true;
    size_t scan = std::min<size_t>(size, 256);
    std::string_view sv(reinterpret_cast<const char*>(data), scan);
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r' || sv.front() == '\n' ||
                           static_cast<uint8_t>(sv.front()) == 0xEF || static_cast<uint8_t>(sv.front()) == 0xBB || static_cast<uint8_t>(sv.front()) == 0xBF)) {
        sv.remove_prefix(1);
    }
    return sv.rfind("[gd_scene", 0) == 0 || sv.rfind("[gd_resource", 0) == 0;
}

inline bool ParseGodotMeshOrScene(const uint8_t* data, size_t size, const std::wstring& fullPath, LoadedMesh3D& mesh) {
    using namespace GodotDetail;
    if (!IsLikelyGodotResourceHeader(data, size)) return false;
    if (std::memcmp(data, "RSRC", 4) == 0 || std::memcmp(data, "RSCC", 4) == 0) {
        return ParseGodotBinaryRsrc(data, size, mesh);
    }
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    return ParseGodotTextMeshOrScene(sv, fullPath, mesh);
}

} // namespace Formats
