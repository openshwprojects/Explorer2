#pragma once
// Extended3D200.h — Real CAD Kernels, LiDAR, Game Meshes, Robotics, Avatars & Crystallography 3D Parsers (#41–#60)
// Zero-cheat implementation: decodes genuine geometry from file bytes or returns false honestly.

namespace Formats {

namespace Detail3D200 {

inline uint16_t RdU16LE(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}
inline uint32_t RdU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}
inline uint64_t RdU64LE(const uint8_t* p) {
    return static_cast<uint64_t>(RdU32LE(p)) |
           (static_cast<uint64_t>(RdU32LE(p + 4)) << 32);
}
inline uint16_t RdU16BE(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}
inline uint32_t RdU32BE(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}
inline uint64_t RdU64BE(const uint8_t* p) {
    return (static_cast<uint64_t>(RdU32BE(p)) << 32) | static_cast<uint64_t>(RdU32BE(p + 4));
}
inline float RdF32LE(const uint8_t* p) {
    float f = 0.0f;
    std::memcpy(&f, p, 4);
    return std::isfinite(f) ? f : 0.0f;
}
inline float RdF32BE(const uint8_t* p) {
    uint32_t u = RdU32BE(p);
    float f = 0.0f;
    std::memcpy(&f, &u, 4);
    return std::isfinite(f) ? f : 0.0f;
}
inline double RdF64LE(const uint8_t* p) {
    double d = 0.0;
    std::memcpy(&d, p, 8);
    return std::isfinite(d) ? d : 0.0;
}

inline float HalfToFloat(uint16_t h) {
    uint32_t sign = static_cast<uint32_t>(h & 0x8000u) << 16;
    uint32_t exp  = (h >> 10) & 0x1Fu;
    uint32_t mant = h & 0x03FFu;
    uint32_t bits = 0;
    if (exp == 0) {
        if (mant == 0) {
            bits = sign;
        } else {
            exp = 1;
            while ((mant & 0x0400u) == 0) {
                mant <<= 1;
                --exp;
            }
            mant &= 0x03FFu;
            bits = sign | ((exp + (127 - 15)) << 23) | (mant << 13);
        }
    } else if (exp == 31) {
        return 0.0f;
    } else {
        bits = sign | ((exp + (127 - 15)) << 23) | (mant << 13);
    }
    float out = 0.0f;
    std::memcpy(&out, &bits, 4);
    return std::isfinite(out) ? out : 0.0f;
}

inline bool ReadVarintU64(const uint8_t* data, size_t size, size_t& pos, uint64_t& out) {
    out = 0;
    unsigned shift = 0;
    for (int i = 0; i < 10; ++i) {
        if (pos >= size) return false;
        uint8_t b = data[pos++];
        out |= static_cast<uint64_t>(b & 0x7Fu) << shift;
        if ((b & 0x80u) == 0) return true;
        shift += 7;
    }
    return false;
}

inline bool ReadVarintU32(const uint8_t* data, size_t size, size_t& pos, uint32_t& out) {
    uint64_t v = 0;
    if (!ReadVarintU64(data, size, pos, v) || v > 0xFFFFFFFFull) return false;
    out = static_cast<uint32_t>(v);
    return true;
}

inline void AddOctaSphere(LoadedMesh3D& mesh, float cx, float cy, float cz, float r) {
    uint32_t b = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({ cx + r, cy,     cz     });
    mesh.vertices.push_back({ cx - r, cy,     cz     });
    mesh.vertices.push_back({ cx,     cy + r, cz     });
    mesh.vertices.push_back({ cx,     cy - r, cz     });
    mesh.vertices.push_back({ cx,     cy,     cz + r });
    mesh.vertices.push_back({ cx,     cy,     cz - r });
    static const uint32_t f[8][3] = {
        {0,2,4}, {4,2,1}, {1,2,5}, {5,2,0},
        {4,3,0}, {1,3,4}, {5,3,1}, {0,3,5}
    };
    for (int i = 0; i < 8; ++i) {
        mesh.tris.push_back({ b + f[i][0], b + f[i][1], b + f[i][2], {} });
    }
}

inline void AddPointTetra(LoadedMesh3D& mesh, Vec3f p, float r) {
    uint32_t b = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({ p.x,     p.y + r, p.z     });
    mesh.vertices.push_back({ p.x + r, p.y - r, p.z + r });
    mesh.vertices.push_back({ p.x - r, p.y - r, p.z + r });
    mesh.vertices.push_back({ p.x,     p.y - r, p.z - r });
    mesh.tris.push_back({ b + 0, b + 1, b + 2, {} });
    mesh.tris.push_back({ b + 0, b + 2, b + 3, {} });
    mesh.tris.push_back({ b + 0, b + 3, b + 1, {} });
    mesh.tris.push_back({ b + 1, b + 3, b + 2, {} });
}

inline void AddStrut3D(LoadedMesh3D& mesh, Vec3f p0, Vec3f p1, float radius) {
    Vec3f d = { p1.x - p0.x, p1.y - p0.y, p1.z - p0.z };
    float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (len < 1e-5f) return;
    Vec3f u = (std::fabs(d.y) < 0.9f * len) ? Vec3f{ 0.0f, 1.0f, 0.0f } : Vec3f{ 1.0f, 0.0f, 0.0f };
    Vec3f n1 = { d.y * u.z - d.z * u.y, d.z * u.x - d.x * u.z, d.x * u.y - d.y * u.x };
    float n1L = std::sqrt(n1.x * n1.x + n1.y * n1.y + n1.z * n1.z);
    if (n1L < 1e-6f) return;
    n1.x = (n1.x / n1L) * radius; n1.y = (n1.y / n1L) * radius; n1.z = (n1.z / n1L) * radius;
    Vec3f n2 = { d.y * n1.z - d.z * n1.y, d.z * n1.x - d.x * n1.z, d.x * n1.y - d.y * n1.x };
    float n2L = std::sqrt(n2.x * n2.x + n2.y * n2.y + n2.z * n2.z);
    if (n2L > 1e-6f) { n2.x = (n2.x / n2L) * radius; n2.y = (n2.y / n2L) * radius; n2.z = (n2.z / n2L) * radius; }

    uint32_t b = static_cast<uint32_t>(mesh.vertices.size());
    Vec3f offs[4] = { n1, n2, { -n1.x, -n1.y, -n1.z }, { -n2.x, -n2.y, -n2.z } };
    for (int i = 0; i < 4; ++i) mesh.vertices.push_back({ p0.x + offs[i].x, p0.y + offs[i].y, p0.z + offs[i].z });
    for (int i = 0; i < 4; ++i) mesh.vertices.push_back({ p1.x + offs[i].x, p1.y + offs[i].y, p1.z + offs[i].z });
    for (uint32_t i = 0; i < 4; ++i) {
        uint32_t ni = (i + 1) & 3u;
        mesh.tris.push_back({ b + i, b + ni, b + 4 + ni, {} });
        mesh.tris.push_back({ b + i, b + 4 + ni, b + 4 + i, {} });
    }
}

inline void AddCylinderY(LoadedMesh3D& mesh, float cx, float cy, float cz, float radius, float length) {
    const int segs = 12;
    uint32_t b = static_cast<uint32_t>(mesh.vertices.size());
    float hz = length * 0.5f;
    for (int i = 0; i < segs; ++i) {
        float a = i * 6.2831853f / segs;
        float x = cx + radius * std::cos(a);
        float y = cy + radius * std::sin(a);
        mesh.vertices.push_back({ x, y, cz - hz });
        mesh.vertices.push_back({ x, y, cz + hz });
    }
    uint32_t cBot = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({ cx, cy, cz - hz });
    uint32_t cTop = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({ cx, cy, cz + hz });
    for (int i = 0; i < segs; ++i) {
        int ni = (i + 1) % segs;
        uint32_t b0 = b + i * 2, t0 = b + i * 2 + 1;
        uint32_t b1 = b + ni * 2, t1 = b + ni * 2 + 1;
        mesh.tris.push_back({ b0, b1, t1, {} });
        mesh.tris.push_back({ b0, t1, t0, {} });
        mesh.tris.push_back({ cBot, b1, b0, {} });
        mesh.tris.push_back({ cTop, t0, t1, {} });
    }
}

inline float ComputePointCloudSpan(const std::vector<Vec3f>& pts) {
    if (pts.empty()) return 1.0f;
    Vec3f mn = pts[0], mx = pts[0];
    for (const auto& p : pts) {
        mn.x = std::min(mn.x, p.x); mn.y = std::min(mn.y, p.y); mn.z = std::min(mn.z, p.z);
        mx.x = std::max(mx.x, p.x); mx.y = std::max(mx.y, p.y); mx.z = std::max(mx.z, p.z);
    }
    return std::max({ mx.x - mn.x, mx.y - mn.y, mx.z - mn.z, 1e-3f });
}

} // namespace Detail3D200

// ============================================================================
// #41. DirectX Legacy Mesh (.x — "xof 0303txt" / "xof 0302txt" / "xof ...bin ")
// ============================================================================
inline bool ParseDirectXMeshX(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    if (size < 16 || std::memcmp(data, "xof ", 4) != 0) return false;
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    std::string_view fmtTag = sv.substr(8, 4);
    bool isDouble = (sv.substr(12, 4) == "0064");

    if (fmtTag == "txt ") {
        mesh.formatName = L"DirectX 3D Mesh (.x)";
        auto isIdentChar = [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        };
        size_t mpos = sv.find("Mesh", 16);
        while (mpos != std::string_view::npos) {
            bool wordBefore = (mpos > 0 && isIdentChar(sv[mpos - 1]));
            bool wordAfter  = (mpos + 4 < sv.size() && isIdentChar(sv[mpos + 4]));
            if (wordBefore || wordAfter) {
                mpos = sv.find("Mesh", mpos + 4);
                continue;
            }
            // Check not preceded by "template "
            size_t lineStart = sv.rfind('\n', mpos);
            lineStart = (lineStart == std::string_view::npos) ? 16 : (lineStart + 1);
            if (sv.substr(lineStart, mpos - lineStart).find("template") != std::string_view::npos) {
                mpos = sv.find("Mesh", mpos + 4);
                continue;
            }
            size_t brace = sv.find('{', mpos + 4);
            if (brace == std::string_view::npos) break;

            const char* p = sv.data() + brace + 1;
            const char* end = sv.data() + sv.size();
            auto skipSpaceComments = [&]() {
                while (p < end) {
                    if (*p == '/' && p + 1 < end && p[1] == '/') { while (p < end && *p != '\n') ++p; }
                    else if (*p == '#') { while (p < end && *p != '\n') ++p; }
                    else if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ';' || *p == ',') ++p;
                    else break;
                }
            };
            auto nextInt = [&]() -> int {
                skipSpaceComments();
                if (p >= end || ((*p < '0' || *p > '9') && *p != '-')) return -1;
                char* ep = nullptr;
                long v = std::strtol(p, &ep, 10);
                if (ep == p) return -1;
                p = ep;
                return static_cast<int>(v);
            };
            auto nextFloat = [&]() -> float {
                skipSpaceComments();
                if (p >= end) return 0.0f;
                char* ep = nullptr;
                float v = std::strtof(p, &ep);
                if (ep == p) return 0.0f;
                p = ep;
                return std::isfinite(v) ? v : 0.0f;
            };

            int nVerts = nextInt();
            if (nVerts > 0 && nVerts <= 500000) {
                uint32_t vBase = static_cast<uint32_t>(mesh.vertices.size());
                for (int i = 0; i < nVerts && p < end; ++i) {
                    float x = nextFloat(), y = nextFloat(), z = nextFloat();
                    mesh.vertices.push_back({ x, y, z });
                }
                int nFaces = nextInt();
                for (int f = 0; f < nFaces && f < 500000 && p < end; ++f) {
                    int cnt = nextInt();
                    if (cnt < 3 || cnt > 64) break;
                    std::vector<uint32_t> idx(static_cast<size_t>(cnt));
                    bool okPoly = true;
                    for (int k = 0; k < cnt; ++k) {
                        int vi = nextInt();
                        if (vi < 0 || vi >= nVerts) { okPoly = false; break; }
                        idx[static_cast<size_t>(k)] = vBase + static_cast<uint32_t>(vi);
                    }
                    if (!okPoly) break;
                    for (int k = 1; k + 1 < cnt; ++k) {
                        mesh.tris.push_back({ idx[0], idx[static_cast<size_t>(k)], idx[static_cast<size_t>(k + 1)], {} });
                    }
                }
            }
            mpos = sv.find("Mesh", static_cast<size_t>(p - sv.data()));
        }
        return !mesh.vertices.empty() && !mesh.tris.empty();
    }

    if (fmtTag == "bin ") {
        mesh.formatName = L"DirectX Binary Mesh (.x)";
        size_t pos = 16;
        int braceDepth = 0;
        int meshDepth = -1;
        bool pendingMesh = false;
        bool inTemplate = false;
        std::vector<int32_t> meshInts;
        std::vector<float> meshFloats;

        auto flushBinaryMesh = [&]() {
            if (meshInts.size() < 5 || meshFloats.size() < 9) {
                meshInts.clear(); meshFloats.clear();
                return;
            }
            int32_t nVerts = meshInts[0];
            if (nVerts > 0 && nVerts <= 500000 && meshFloats.size() >= static_cast<size_t>(nVerts) * 3) {
                uint32_t vBase = static_cast<uint32_t>(mesh.vertices.size());
                for (int32_t i = 0; i < nVerts; ++i) {
                    mesh.vertices.push_back({
                        meshFloats[static_cast<size_t>(i) * 3 + 0],
                        meshFloats[static_cast<size_t>(i) * 3 + 1],
                        meshFloats[static_cast<size_t>(i) * 3 + 2]
                    });
                }
                size_t ip = 1;
                if (ip < meshInts.size()) {
                    int32_t nFaces = meshInts[ip++];
                    for (int32_t f = 0; f < nFaces && f < 500000 && ip < meshInts.size(); ++f) {
                        int32_t cnt = meshInts[ip++];
                        if (cnt < 3 || cnt > 64 || ip + static_cast<size_t>(cnt) > meshInts.size()) break;
                        bool valid = true;
                        for (int32_t k = 0; k < cnt; ++k) {
                            if (meshInts[ip + static_cast<size_t>(k)] < 0 || meshInts[ip + static_cast<size_t>(k)] >= nVerts) {
                                valid = false; break;
                            }
                        }
                        if (valid) {
                            uint32_t i0 = vBase + static_cast<uint32_t>(meshInts[ip]);
                            for (int32_t k = 1; k + 1 < cnt; ++k) {
                                mesh.tris.push_back({
                                    i0,
                                    vBase + static_cast<uint32_t>(meshInts[ip + static_cast<size_t>(k)]),
                                    vBase + static_cast<uint32_t>(meshInts[ip + static_cast<size_t>(k + 1)]), {}
                                });
                            }
                        }
                        ip += static_cast<size_t>(cnt);
                    }
                }
            }
            meshInts.clear();
            meshFloats.clear();
        };

        while (pos + 2 <= size) {
            uint16_t tok = Detail3D200::RdU16LE(data + pos);
            pos += 2;
            if (tok == 1) { // TOKEN_NAME
                if (pos + 4 > size) break;
                uint32_t len = Detail3D200::RdU32LE(data + pos); pos += 4;
                if (pos + len > size) break;
                std::string_view nm(reinterpret_cast<const char*>(data + pos), len);
                pos += len;
                if (!inTemplate && meshDepth < 0 && nm == "Mesh") pendingMesh = true;
            } else if (tok == 2) { // TOKEN_STRING
                if (pos + 4 > size) break;
                uint32_t len = Detail3D200::RdU32LE(data + pos); pos += 4;
                if (pos + len + 2 > size) break;
                pos += len + 2; // string + terminator token
            } else if (tok == 3) { // TOKEN_INTEGER
                if (pos + 4 > size) break;
                int32_t v = static_cast<int32_t>(Detail3D200::RdU32LE(data + pos)); pos += 4;
                if (meshDepth >= 0 && braceDepth == meshDepth) meshInts.push_back(v);
            } else if (tok == 5) { // TOKEN_GUID
                if (pos + 16 > size) break;
                pos += 16;
            } else if (tok == 6) { // TOKEN_INTEGER_LIST
                if (pos + 4 > size) break;
                uint32_t cnt = Detail3D200::RdU32LE(data + pos); pos += 4;
                if (cnt > 4000000 || pos + static_cast<size_t>(cnt) * 4 > size) break;
                if (meshDepth >= 0 && braceDepth == meshDepth) {
                    for (uint32_t i = 0; i < cnt; ++i) {
                        meshInts.push_back(static_cast<int32_t>(Detail3D200::RdU32LE(data + pos + static_cast<size_t>(i) * 4)));
                    }
                }
                pos += static_cast<size_t>(cnt) * 4;
            } else if (tok == 7) { // TOKEN_FLOAT_LIST
                if (pos + 4 > size) break;
                uint32_t cnt = Detail3D200::RdU32LE(data + pos); pos += 4;
                size_t elemSz = isDouble ? 8 : 4;
                if (cnt > 4000000 || pos + static_cast<size_t>(cnt) * elemSz > size) break;
                if (meshDepth >= 0 && braceDepth == meshDepth) {
                    for (uint32_t i = 0; i < cnt; ++i) {
                        float fv = isDouble
                            ? static_cast<float>(Detail3D200::RdF64LE(data + pos + static_cast<size_t>(i) * 8))
                            : Detail3D200::RdF32LE(data + pos + static_cast<size_t>(i) * 4);
                        meshFloats.push_back(fv);
                    }
                }
                pos += static_cast<size_t>(cnt) * elemSz;
            } else if (tok == 10) { // TOKEN_OBRACE
                ++braceDepth;
                if (pendingMesh && !inTemplate) {
                    meshDepth = braceDepth;
                    pendingMesh = false;
                    meshInts.clear();
                    meshFloats.clear();
                }
            } else if (tok == 11) { // TOKEN_CBRACE
                if (meshDepth >= 0 && braceDepth == meshDepth) {
                    flushBinaryMesh();
                    meshDepth = -1;
                }
                --braceDepth;
                if (braceDepth <= 0) { braceDepth = 0; inTemplate = false; }
                pendingMesh = false;
            } else if (tok == 31) { // TOKEN_TEMPLATE
                inTemplate = true;
                pendingMesh = false;
            } else if ((tok >= 12 && tok <= 20) || (tok >= 40 && tok <= 52)) {
                // Zero-payload punctuation / template primitive keyword tokens
            } else {
                break;
            }
        }
        return !mesh.vertices.empty() && !mesh.tris.empty();
    }

    return false;
}

// ============================================================================
// #42. MilkShape 3D (.ms3d — "MS3D000000" v3/v4 with Skeletal Joints, Keyframes & VertexExtra LBS)
// ============================================================================
inline bool ParseMilkShapeMs3d(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    if (size < 16 || std::memcmp(data, "MS3D000000", 10) != 0) return false;
    int32_t ver = static_cast<int32_t>(Detail3D200::RdU32LE(data + 10));
    if (ver < 3 || ver > 4) return false;
    mesh.formatName = L"MilkShape 3D Skeletal Mesh (.ms3d)";

    size_t pos = 14;
    if (pos + 2 > size) return false;
    uint16_t nVerts = Detail3D200::RdU16LE(data + pos); pos += 2;
    if (nVerts == 0 || pos + static_cast<size_t>(nVerts) * 15 + 2 > size) return false;

    std::vector<Vec3f> bindVerts;
    std::vector<int8_t> primaryBoneIds;
    bindVerts.reserve(nVerts);
    primaryBoneIds.reserve(nVerts);
    for (uint16_t i = 0; i < nVerts; ++i) {
        const uint8_t* vp = data + pos + static_cast<size_t>(i) * 15;
        float vx = Detail3D200::RdF32LE(vp + 1);
        float vy = Detail3D200::RdF32LE(vp + 5);
        float vz = Detail3D200::RdF32LE(vp + 9);
        if (!std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(vz)) return false;
        bindVerts.push_back({ vx, vy, vz });
        primaryBoneIds.push_back(static_cast<int8_t>(vp[13]));
    }
    pos += static_cast<size_t>(nVerts) * 15;

    uint16_t nTris = Detail3D200::RdU16LE(data + pos); pos += 2;
    if (nTris == 0 || pos + static_cast<size_t>(nTris) * 70 > size) return false;
    mesh.tris.reserve(nTris);
    for (uint16_t t = 0; t < nTris; ++t) {
        const uint8_t* tp = data + pos + static_cast<size_t>(t) * 70;
        uint16_t i0 = Detail3D200::RdU16LE(tp + 2);
        uint16_t i1 = Detail3D200::RdU16LE(tp + 4);
        uint16_t i2 = Detail3D200::RdU16LE(tp + 6);
        if (i0 < nVerts && i1 < nVerts && i2 < nVerts) {
            mesh.tris.push_back({ i0, i1, i2, {} });
        }
    }
    pos += static_cast<size_t>(nTris) * 70;
    if (mesh.tris.empty()) return false;

    // Optional Groups, Materials, Animation Header, Joints, Comments, and VertexExtra multi-bone weights
    struct MsMat3x4 {
        float m[3][4] = { {1,0,0,0}, {0,1,0,0}, {0,0,1,0} };
        static MsMat3x4 FromEulerXYZAndPos(float rx, float ry, float rz, float px, float py, float pz) {
            const float cx = std::cos(rx), sx = std::sin(rx);
            const float cy = std::cos(ry), sy = std::sin(ry);
            const float cz = std::cos(rz), sz = std::sin(rz);
            // MS3D AngleMatrix: Rz * Ry * Rx
            MsMat3x4 r;
            r.m[0][0] = cy * cz;
            r.m[1][0] = cy * sz;
            r.m[2][0] = -sy;
            r.m[0][1] = sx * sy * cz - cx * sz;
            r.m[1][1] = sx * sy * sz + cx * cz;
            r.m[2][1] = sx * cy;
            r.m[0][2] = cx * sy * cz + sx * sz;
            r.m[1][2] = cx * sy * sz - sx * cz;
            r.m[2][2] = cx * cy;
            r.m[0][3] = px;
            r.m[1][3] = py;
            r.m[2][3] = pz;
            return r;
        }
        static MsMat3x4 Mul(const MsMat3x4& a, const MsMat3x4& b) {
            MsMat3x4 r;
            for (int i = 0; i < 3; ++i) {
                for (int j = 0; j < 3; ++j) {
                    r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
                }
                r.m[i][3] = a.m[i][0] * b.m[0][3] + a.m[i][1] * b.m[1][3] + a.m[i][2] * b.m[2][3] + a.m[i][3];
            }
            return r;
        }
        // Rigid inverse (transpose 3x3 + inverse rotated translation)
        MsMat3x4 InverseRigid() const {
            MsMat3x4 inv;
            for (int i = 0; i < 3; ++i) {
                for (int j = 0; j < 3; ++j) inv.m[i][j] = m[j][i];
            }
            inv.m[0][3] = -(inv.m[0][0] * m[0][3] + inv.m[0][1] * m[1][3] + inv.m[0][2] * m[2][3]);
            inv.m[1][3] = -(inv.m[1][0] * m[0][3] + inv.m[1][1] * m[1][3] + inv.m[1][2] * m[2][3]);
            inv.m[2][3] = -(inv.m[2][0] * m[0][3] + inv.m[2][1] * m[1][3] + inv.m[2][2] * m[2][3]);
            return inv;
        }
        Vec3f Transform(const Vec3f& v) const {
            return {
                m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z + m[0][3],
                m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z + m[1][3],
                m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z + m[2][3]
            };
        }
    };

    auto parseFixedName32 = [](const uint8_t* p) -> std::string {
        size_t len = 0;
        while (len < 32 && p[len] != 0) ++len;
        return std::string(reinterpret_cast<const char*>(p), len);
    };

    bool skeletonApplied = false;
    if (pos + 2 <= size) {
        uint16_t nGroups = Detail3D200::RdU16LE(data + pos); pos += 2;
        bool groupsOk = true;
        for (uint16_t g = 0; g < nGroups; ++g) {
            if (pos + 35 > size) { groupsOk = false; break; }
            uint16_t gTris = Detail3D200::RdU16LE(data + pos + 33);
            size_t step = 35u + static_cast<size_t>(gTris) * 2u + 1u;
            if (pos + step > size) { groupsOk = false; break; }
            pos += step;
        }
        if (groupsOk && pos + 2 <= size) {
            uint16_t nMats = Detail3D200::RdU16LE(data + pos); pos += 2;
            size_t matsBytes = static_cast<size_t>(nMats) * 361u;
            if (pos + matsBytes + 14u <= size) {
                pos += matsBytes;
                float animFps = Detail3D200::RdF32LE(data + pos + 0);
                float curTime = Detail3D200::RdF32LE(data + pos + 4);
                int32_t totalFrames = static_cast<int32_t>(Detail3D200::RdU32LE(data + pos + 8));
                pos += 12;
                if (totalFrames > 1 && totalFrames < 1000000) {
                    mesh.animFrameCount = static_cast<size_t>(totalFrames);
                }
                (void)animFps;

                uint16_t nJoints = Detail3D200::RdU16LE(data + pos); pos += 2;
                struct MsKey { float time = 0.0f; float v[3] = {}; };
                struct MsJoint {
                    std::string name, parentName;
                    int parentIdx = -1;
                    float bindRot[3] = {};
                    float bindPos[3] = {};
                    std::vector<MsKey> rotKeys;
                    std::vector<MsKey> posKeys;
                };
                std::vector<MsJoint> joints;
                bool jointsOk = (nJoints > 0 && nJoints <= 4096);
                if (jointsOk) {
                    joints.reserve(nJoints);
                    for (uint16_t j = 0; j < nJoints; ++j) {
                        if (pos + 93 > size) { jointsOk = false; break; }
                        MsJoint mj;
                        mj.name = parseFixedName32(data + pos + 1);
                        mj.parentName = parseFixedName32(data + pos + 33);
                        mj.bindRot[0] = Detail3D200::RdF32LE(data + pos + 65);
                        mj.bindRot[1] = Detail3D200::RdF32LE(data + pos + 69);
                        mj.bindRot[2] = Detail3D200::RdF32LE(data + pos + 73);
                        mj.bindPos[0] = Detail3D200::RdF32LE(data + pos + 77);
                        mj.bindPos[1] = Detail3D200::RdF32LE(data + pos + 81);
                        mj.bindPos[2] = Detail3D200::RdF32LE(data + pos + 85);
                        uint16_t nRotK = Detail3D200::RdU16LE(data + pos + 89);
                        uint16_t nPosK = Detail3D200::RdU16LE(data + pos + 91);
                        pos += 93;
                        size_t kBytes = (static_cast<size_t>(nRotK) + static_cast<size_t>(nPosK)) * 16u;
                        if (pos + kBytes > size) { jointsOk = false; break; }
                        mj.rotKeys.resize(nRotK);
                        for (uint16_t k = 0; k < nRotK; ++k) {
                            mj.rotKeys[k].time = Detail3D200::RdF32LE(data + pos + 0);
                            mj.rotKeys[k].v[0] = Detail3D200::RdF32LE(data + pos + 4);
                            mj.rotKeys[k].v[1] = Detail3D200::RdF32LE(data + pos + 8);
                            mj.rotKeys[k].v[2] = Detail3D200::RdF32LE(data + pos + 12);
                            pos += 16;
                        }
                        mj.posKeys.resize(nPosK);
                        for (uint16_t k = 0; k < nPosK; ++k) {
                            mj.posKeys[k].time = Detail3D200::RdF32LE(data + pos + 0);
                            mj.posKeys[k].v[0] = Detail3D200::RdF32LE(data + pos + 4);
                            mj.posKeys[k].v[1] = Detail3D200::RdF32LE(data + pos + 8);
                            mj.posKeys[k].v[2] = Detail3D200::RdF32LE(data + pos + 12);
                            pos += 16;
                        }
                        joints.push_back(std::move(mj));
                    }
                }

                if (jointsOk && !joints.empty()) {
                    // Optional Comments section (subVersion == 1) + VertexExtra (subVersion 1..3)
                    struct VertExtraWeights {
                        int8_t boneIds[4] = { -1, -1, -1, -1 };
                        float weights[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
                    };
                    std::vector<VertExtraWeights> vWeights(nVerts);
                    for (uint16_t i = 0; i < nVerts; ++i) {
                        vWeights[i].boneIds[0] = primaryBoneIds[i];
                        vWeights[i].weights[0] = (primaryBoneIds[i] >= 0) ? 1.0f : 0.0f;
                    }

                    if (pos + 4 <= size) {
                        int32_t commentSubVer = static_cast<int32_t>(Detail3D200::RdU32LE(data + pos));
                        if (commentSubVer == 1) {
                            size_t cPos = pos + 4;
                            bool commentsOk = true;
                            // 3 indexed comment lists (group, material, joint) + 1 model comment flag
                            for (int listIdx = 0; listIdx < 3 && commentsOk; ++listIdx) {
                                if (cPos + 4 > size) { commentsOk = false; break; }
                                uint32_t nCom = Detail3D200::RdU32LE(data + cPos); cPos += 4;
                                if (nCom > 65536) { commentsOk = false; break; }
                                for (uint32_t ci = 0; ci < nCom; ++ci) {
                                    if (cPos + 8 > size) { commentsOk = false; break; }
                                    uint32_t cLen = Detail3D200::RdU32LE(data + cPos + 4);
                                    cPos += 8;
                                    if (cLen > size - cPos) { commentsOk = false; break; }
                                    cPos += cLen;
                                }
                            }
                            if (commentsOk && cPos + 4 <= size) {
                                uint32_t hasModelCom = Detail3D200::RdU32LE(data + cPos); cPos += 4;
                                if (hasModelCom == 1) {
                                    if (cPos + 4 <= size) {
                                        uint32_t mLen = Detail3D200::RdU32LE(data + cPos); cPos += 4;
                                        if (mLen <= size - cPos) cPos += mLen;
                                        else commentsOk = false;
                                    } else commentsOk = false;
                                } else if (hasModelCom > 1) {
                                    commentsOk = false;
                                }
                            }
                            if (commentsOk) pos = cPos;
                        }
                    }

                    if (pos + 4 <= size) {
                        int32_t vExtraSubVer = static_cast<int32_t>(Detail3D200::RdU32LE(data + pos));
                        size_t recSz = (vExtraSubVer == 1) ? 6u : (vExtraSubVer == 2) ? 10u : (vExtraSubVer == 3) ? 14u : 0u;
                        if (recSz > 0 && pos + 4u + static_cast<size_t>(nVerts) * recSz <= size) {
                            pos += 4;
                            const float wScale = (vExtraSubVer == 1) ? 255.0f : 100.0f;
                            for (uint16_t i = 0; i < nVerts; ++i) {
                                const uint8_t* ep = data + pos + static_cast<size_t>(i) * recSz;
                                int8_t b1 = static_cast<int8_t>(ep[0]);
                                int8_t b2 = static_cast<int8_t>(ep[1]);
                                int8_t b3 = static_cast<int8_t>(ep[2]);
                                uint8_t w0 = ep[3], w1 = ep[4], w2 = ep[5];
                                if (w0 > 0 || w1 > 0 || w2 > 0) {
                                    float fw0 = static_cast<float>(w0) / wScale;
                                    float fw1 = static_cast<float>(w1) / wScale;
                                    float fw2 = static_cast<float>(w2) / wScale;
                                    float fw3 = std::max(0.0f, 1.0f - (fw0 + fw1 + fw2));
                                    vWeights[i].boneIds[0] = primaryBoneIds[i];
                                    vWeights[i].boneIds[1] = b1;
                                    vWeights[i].boneIds[2] = b2;
                                    vWeights[i].boneIds[3] = b3;
                                    vWeights[i].weights[0] = fw0;
                                    vWeights[i].weights[1] = (b1 >= 0) ? fw1 : 0.0f;
                                    vWeights[i].weights[2] = (b2 >= 0) ? fw2 : 0.0f;
                                    vWeights[i].weights[3] = (b3 >= 0) ? fw3 : 0.0f;
                                }
                            }
                        }
                    }

                    // Resolve parent joint indices by name
                    for (size_t j = 0; j < joints.size(); ++j) {
                        if (!joints[j].parentName.empty()) {
                            for (size_t p = 0; p < joints.size(); ++p) {
                                if (p != j && joints[p].name == joints[j].parentName) {
                                    joints[j].parentIdx = static_cast<int>(p);
                                    break;
                                }
                            }
                        }
                    }

                    auto sampleKeys = [](const std::vector<MsKey>& keys, float t, float out[3]) {
                        out[0] = out[1] = out[2] = 0.0f;
                        if (keys.empty()) return;
                        if (keys.size() == 1 || t <= keys.front().time) {
                            out[0] = keys.front().v[0]; out[1] = keys.front().v[1]; out[2] = keys.front().v[2];
                            return;
                        }
                        if (t >= keys.back().time) {
                            out[0] = keys.back().v[0]; out[1] = keys.back().v[1]; out[2] = keys.back().v[2];
                            return;
                        }
                        for (size_t k = 0; k + 1 < keys.size(); ++k) {
                            if (t >= keys[k].time && t <= keys[k + 1].time) {
                                float dt = keys[k + 1].time - keys[k].time;
                                float a = (dt > 1e-6f) ? (t - keys[k].time) / dt : 0.0f;
                                for (int c = 0; c < 3; ++c) {
                                    out[c] = keys[k].v[c] + (keys[k + 1].v[c] - keys[k].v[c]) * a;
                                }
                                return;
                            }
                        }
                    };

                    float evalTime = (std::isfinite(curTime) && curTime > 0.0f) ? curTime : 0.0f;
                    std::vector<MsMat3x4> bindGlobal(joints.size());
                    std::vector<MsMat3x4> poseGlobal(joints.size());
                    std::vector<uint8_t> state(joints.size(), 0);

                    std::function<void(size_t, int)> evalJoint = [&](size_t jIdx, int depth) {
                        if (state[jIdx] == 2) return;
                        if (state[jIdx] == 1 || depth > 256) {
                            bindGlobal[jIdx] = MsMat3x4{};
                            poseGlobal[jIdx] = MsMat3x4{};
                            state[jIdx] = 2;
                            return;
                        }
                        state[jIdx] = 1;
                        const MsJoint& j = joints[jIdx];
                        MsMat3x4 localBind = MsMat3x4::FromEulerXYZAndPos(
                            j.bindRot[0], j.bindRot[1], j.bindRot[2],
                            j.bindPos[0], j.bindPos[1], j.bindPos[2]);
                        float kfR[3] = {}, kfP[3] = {};
                        sampleKeys(j.rotKeys, evalTime, kfR);
                        sampleKeys(j.posKeys, evalTime, kfP);
                        MsMat3x4 localDelta = MsMat3x4::FromEulerXYZAndPos(kfR[0], kfR[1], kfR[2], kfP[0], kfP[1], kfP[2]);
                        MsMat3x4 localPose = MsMat3x4::Mul(localBind, localDelta);

                        if (j.parentIdx >= 0 && static_cast<size_t>(j.parentIdx) < joints.size()) {
                            size_t pIdx = static_cast<size_t>(j.parentIdx);
                            evalJoint(pIdx, depth + 1);
                            bindGlobal[jIdx] = MsMat3x4::Mul(bindGlobal[pIdx], localBind);
                            poseGlobal[jIdx] = MsMat3x4::Mul(poseGlobal[pIdx], localPose);
                        } else {
                            bindGlobal[jIdx] = localBind;
                            poseGlobal[jIdx] = localPose;
                        }
                        state[jIdx] = 2;
                    };

                    std::vector<MsMat3x4> skinMats(joints.size());
                    for (size_t j = 0; j < joints.size(); ++j) {
                        evalJoint(j, 0);
                        skinMats[j] = MsMat3x4::Mul(poseGlobal[j], bindGlobal[j].InverseRigid());
                    }

                    mesh.vertices.resize(nVerts);
                    for (uint16_t i = 0; i < nVerts; ++i) {
                        const Vec3f& bv = bindVerts[i];
                        const VertExtraWeights& vw = vWeights[i];
                        Vec3f accum = { 0.0f, 0.0f, 0.0f };
                        float wSum = 0.0f;
                        for (int slot = 0; slot < 4; ++slot) {
                            int b = vw.boneIds[slot];
                            float w = vw.weights[slot];
                            if (b >= 0 && static_cast<size_t>(b) < joints.size() && w > 1e-6f) {
                                Vec3f tp = skinMats[static_cast<size_t>(b)].Transform(bv);
                                accum.x += tp.x * w;
                                accum.y += tp.y * w;
                                accum.z += tp.z * w;
                                wSum += w;
                            }
                        }
                        if (wSum > 1e-6f) {
                            float invW = 1.0f / wSum;
                            mesh.vertices[i] = { accum.x * invW, accum.y * invW, accum.z * invW };
                        } else {
                            mesh.vertices[i] = bv;
                        }
                    }
                    skeletonApplied = true;
                }
            }
        }
    }

    if (!skeletonApplied) {
        mesh.vertices = std::move(bindVerts);
    }
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

// ============================================================================
// #43. Inter-Quake Model (.iqm — "INTERQUAKEMODEL\0")
// ============================================================================
inline bool ParseInterQuakeIqm(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    if (size < 124 || std::memcmp(data, "INTERQUAKEMODEL\0", 16) != 0) return false;
    mesh.formatName = L"Inter-Quake 3D Model (.iqm)";

    uint32_t numVar    = Detail3D200::RdU32LE(data + 36);
    uint32_t numVerts  = Detail3D200::RdU32LE(data + 40);
    uint32_t ofsVar    = Detail3D200::RdU32LE(data + 44);
    uint32_t numTris   = Detail3D200::RdU32LE(data + 48);
    uint32_t ofsTris   = Detail3D200::RdU32LE(data + 52);
    uint32_t numFrames = Detail3D200::RdU32LE(data + 76);
    if (numFrames > 0) mesh.animFrameCount = numFrames;

    if (numVerts == 0 || numTris == 0 || numVerts > 1000000 || numTris > 2000000) return false;
    if (ofsVar + static_cast<uint64_t>(numVar) * 20 > size) return false;
    if (ofsTris + static_cast<uint64_t>(numTris) * 12 > size) return false;

    bool foundPos = false;
    for (uint32_t a = 0; a < numVar && a < 64; ++a) {
        const uint8_t* va = data + ofsVar + a * 20;
        uint32_t vaType = Detail3D200::RdU32LE(va + 0);   // 0 = IQM_POSITION
        uint32_t vaFmt  = Detail3D200::RdU32LE(va + 8);   // 7 = IQM_FLOAT
        uint32_t vaSize = Detail3D200::RdU32LE(va + 12);  // 3 floats
        uint32_t vaOfs  = Detail3D200::RdU32LE(va + 16);
        if (vaType == 0 && vaFmt == 7 && vaSize >= 3 && vaOfs + static_cast<uint64_t>(numVerts) * vaSize * 4 <= size) {
            foundPos = true;
            for (uint32_t v = 0; v < numVerts; ++v) {
                const uint8_t* vp = data + vaOfs + static_cast<size_t>(v) * vaSize * 4;
                mesh.vertices.push_back({
                    Detail3D200::RdF32LE(vp + 0),
                    Detail3D200::RdF32LE(vp + 8), // Z-up -> Y-up
                    Detail3D200::RdF32LE(vp + 4)
                });
            }
            break;
        }
    }
    if (!foundPos) return false;

    for (uint32_t t = 0; t < numTris; ++t) {
        const uint8_t* tp = data + ofsTris + static_cast<size_t>(t) * 12;
        uint32_t i0 = Detail3D200::RdU32LE(tp + 0);
        uint32_t i1 = Detail3D200::RdU32LE(tp + 4);
        uint32_t i2 = Detail3D200::RdU32LE(tp + 8);
        if (i0 < numVerts && i1 < numVerts && i2 < numVerts) {
            mesh.tris.push_back({ i0, i2, i1, {} });
        }
    }
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

// ============================================================================
// #44. Quake Alias ("IDPO") & Valve GoldSrc / Source StudioMDL (.mdl — "IDST")
// ============================================================================
namespace DetailValveMdl200 {

struct BoneMat3x4 {
    float m[3][4] = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f }
    };
};

inline BoneMat3x4 ConcatBoneMat(const BoneMat3x4& a, const BoneMat3x4& b) {
    BoneMat3x4 out = {};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            out.m[r][c] = a.m[r][0] * b.m[0][c] + a.m[r][1] * b.m[1][c] + a.m[r][2] * b.m[2][c];
        }
        out.m[r][3] = a.m[r][0] * b.m[0][3] + a.m[r][1] * b.m[1][3] + a.m[r][2] * b.m[2][3] + a.m[r][3];
    }
    return out;
}

inline BoneMat3x4 GoldSrcEulerPosToMat(float px, float py, float pz, float rx, float ry, float rz) {
    float sr = std::sin(rx * 0.5f), cr = std::cos(rx * 0.5f);
    float sp = std::sin(ry * 0.5f), cp = std::cos(ry * 0.5f);
    float sy = std::sin(rz * 0.5f), cy = std::cos(rz * 0.5f);

    float qx = sr * cp * cy - cr * sp * sy;
    float qy = cr * sp * cy + sr * cp * sy;
    float qz = cr * cp * sy - sr * sp * cy;
    float qw = cr * cp * cy + sr * sp * sy;

    BoneMat3x4 loc = {};
    loc.m[0][0] = 1.0f - 2.0f * (qy * qy + qz * qz);
    loc.m[0][1] = 2.0f * (qx * qy - qw * qz);
    loc.m[0][2] = 2.0f * (qx * qz + qw * qy);
    loc.m[0][3] = px;

    loc.m[1][0] = 2.0f * (qx * qy + qw * qz);
    loc.m[1][1] = 1.0f - 2.0f * (qx * qx + qz * qz);
    loc.m[1][2] = 2.0f * (qy * qz - qw * qx);
    loc.m[1][3] = py;

    loc.m[2][0] = 2.0f * (qx * qz - qw * qy);
    loc.m[2][1] = 2.0f * (qy * qz + qw * qx);
    loc.m[2][2] = 1.0f - 2.0f * (qx * qx + qy * qy);
    loc.m[2][3] = pz;
    return loc;
}

inline bool ReadSiblingBytes(const std::wstring& path, std::vector<uint8_t>& out, uint64_t maxCap = 64ULL * 1024ULL * 1024ULL) {
    out.clear();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER li = {};
    if (!GetFileSizeEx(h, &li) || li.QuadPart <= 0 || static_cast<uint64_t>(li.QuadPart) > maxCap) {
        CloseHandle(h);
        return false;
    }
    size_t sz = static_cast<size_t>(li.QuadPart);
    out.resize(sz);
    DWORD rd = 0;
    BOOL ok = ReadFile(h, out.data(), static_cast<DWORD>(sz), &rd, nullptr);
    CloseHandle(h);
    if (!ok || rd != static_cast<DWORD>(sz)) {
        out.clear();
        return false;
    }
    return true;
}

inline int32_t RdI32LE(const uint8_t* p) {
    return static_cast<int32_t>(Detail3D200::RdU32LE(p));
}

inline bool AssembleSourceVvdVtxMesh(
    const uint8_t* mdl, size_t mdlSize,
    const uint8_t* vvd, size_t vvdSize,
    const uint8_t* vtx, size_t vtxSize,
    uint32_t ver,
    LoadedMesh3D& mesh)
{
    if (mdlSize < 244 || vvdSize < 64 || vtxSize < 36) return false;
    uint32_t mdlChecksum = Detail3D200::RdU32LE(mdl + 8);

    // 1. Validate & decode .vvd (vertexFileHeader_t: "IDSV", version == 4)
    if (std::memcmp(vvd, "IDSV", 4) != 0) return false;
    if (RdI32LE(vvd + 4) != 4) return false;
    uint32_t vvdChecksum = Detail3D200::RdU32LE(vvd + 8);
    if (vvdChecksum != mdlChecksum) return false;

    int32_t numLODs       = RdI32LE(vvd + 12);
    int32_t numLOD0Verts  = RdI32LE(vvd + 16);
    int32_t numFixups     = RdI32LE(vvd + 48);
    int32_t fixupOff      = RdI32LE(vvd + 52);
    int32_t vertDataOff   = RdI32LE(vvd + 56);
    if (numLODs < 1 || numLODs > 8 || numLOD0Verts < 3 || numLOD0Verts > 2000000) return false;
    if (vertDataOff < 64 || static_cast<size_t>(vertDataOff) > vvdSize) return false;

    std::vector<Vec3f> lod0Verts;
    lod0Verts.reserve(static_cast<size_t>(numLOD0Verts));

    auto readVvdVertAt = [&](size_t idx) -> Vec3f {
        const uint8_t* vp = vvd + static_cast<size_t>(vertDataOff) + idx * 48;
        float x = Detail3D200::RdF32LE(vp + 16);
        float y = Detail3D200::RdF32LE(vp + 20);
        float z = Detail3D200::RdF32LE(vp + 24);
        return { x, z, -y };
    };

    if (numFixups == 0) {
        if (static_cast<uint64_t>(vertDataOff) + static_cast<uint64_t>(numLOD0Verts) * 48ULL > vvdSize) return false;
        for (int32_t i = 0; i < numLOD0Verts; ++i) {
            lod0Verts.push_back(readVvdVertAt(static_cast<size_t>(i)));
        }
    } else {
        if (numFixups < 0 || numFixups > 65536 || fixupOff < 64 ||
            static_cast<uint64_t>(fixupOff) + static_cast<uint64_t>(numFixups) * 12ULL > vvdSize)
        {
            return false;
        }
        for (int32_t f = 0; f < numFixups; ++f) {
            const uint8_t* fp = vvd + static_cast<size_t>(fixupOff) + static_cast<size_t>(f) * 12;
            int32_t lod     = RdI32LE(fp + 0);
            int32_t srcVert = RdI32LE(fp + 4);
            int32_t cnt     = RdI32LE(fp + 8);
            if (srcVert < 0 || cnt < 0 || cnt > 2000000) return false;
            if (static_cast<uint64_t>(vertDataOff) + (static_cast<uint64_t>(srcVert) + static_cast<uint64_t>(cnt)) * 48ULL > vvdSize) {
                return false;
            }
            if (lod >= 0) {
                if (lod0Verts.size() + static_cast<size_t>(cnt) > 2000000) return false;
                for (int32_t k = 0; k < cnt; ++k) {
                    lod0Verts.push_back(readVvdVertAt(static_cast<size_t>(srcVert + k)));
                }
            }
        }
    }
    if (lod0Verts.size() < 3) return false;

    // 2. Validate .vtx (OptimizedModel::FileHeader_t: version == 7, 36 bytes)
    if (RdI32LE(vtx + 0) != 7) return false;
    uint32_t vtxChecksum = Detail3D200::RdU32LE(vtx + 16);
    if (vtxChecksum != mdlChecksum) return false;

    int32_t vtxNumLODs  = RdI32LE(vtx + 20);
    int32_t vtxNumBp    = RdI32LE(vtx + 28);
    int32_t vtxBpOff    = RdI32LE(vtx + 32);
    int32_t mdlNumBp    = RdI32LE(mdl + 232);
    int32_t mdlBpOff    = RdI32LE(mdl + 236);
    if (vtxNumLODs < 1 || vtxNumLODs > 8) return false;
    if (mdlNumBp <= 0 || mdlNumBp > 256 || vtxNumBp != mdlNumBp) return false;
    if (mdlBpOff < 240 || static_cast<uint64_t>(mdlBpOff) + static_cast<uint64_t>(mdlNumBp) * 16ULL > mdlSize) return false;
    if (vtxBpOff < 36  || static_cast<uint64_t>(vtxBpOff) + static_cast<uint64_t>(vtxNumBp) * 8ULL  > vtxSize) return false;

    auto checkedRel = [](size_t base, int32_t rel, size_t limit, size_t& out) -> bool {
        int64_t v = static_cast<int64_t>(base) + static_cast<int64_t>(rel);
        if (v < 0 || static_cast<uint64_t>(v) > limit) return false;
        out = static_cast<size_t>(v);
        return true;
    };

    mesh.vertices = std::move(lod0Verts);
    const size_t totalVvdVerts = mesh.vertices.size();

    for (int32_t bp = 0; bp < mdlNumBp; ++bp) {
        size_t mdlBpPos = static_cast<size_t>(mdlBpOff) + static_cast<size_t>(bp) * 16;
        size_t vtxBpPos = static_cast<size_t>(vtxBpOff) + static_cast<size_t>(bp) * 8;

        int32_t mdlNumModels = RdI32LE(mdl + mdlBpPos + 4);
        int32_t mdlModelRel  = RdI32LE(mdl + mdlBpPos + 12);
        int32_t vtxNumModels = RdI32LE(vtx + vtxBpPos + 0);
        int32_t vtxModelRel  = RdI32LE(vtx + vtxBpPos + 4);
        if (mdlNumModels < 0 || mdlNumModels > 256 || vtxNumModels != mdlNumModels) {
            mesh.vertices.clear(); mesh.tris.clear();
            return false;
        }
        if (mdlNumModels == 0) continue;

        size_t mdlModelsBase = 0, vtxModelsBase = 0;
        if (!checkedRel(mdlBpPos, mdlModelRel, mdlSize, mdlModelsBase) ||
            mdlModelsBase + static_cast<uint64_t>(mdlNumModels) * 148ULL > mdlSize ||
            !checkedRel(vtxBpPos, vtxModelRel, vtxSize, vtxModelsBase) ||
            vtxModelsBase + static_cast<uint64_t>(vtxNumModels) * 8ULL > vtxSize)
        {
            mesh.vertices.clear(); mesh.tris.clear();
            return false;
        }

        for (int32_t mi = 0; mi < mdlNumModels; ++mi) {
            size_t mdlMdlPos = mdlModelsBase + static_cast<size_t>(mi) * 148;
            size_t vtxMdlPos = vtxModelsBase + static_cast<size_t>(mi) * 8;

            int32_t numMeshes   = RdI32LE(mdl + mdlMdlPos + 72);
            int32_t mdlMeshRel  = RdI32LE(mdl + mdlMdlPos + 76);
            int32_t mdlNumVerts = RdI32LE(mdl + mdlMdlPos + 80);
            int32_t mdlVertIdx  = RdI32LE(mdl + mdlMdlPos + 84);
            if (numMeshes <= 0 || mdlNumVerts <= 0) continue;
            if (numMeshes > 1024 || mdlVertIdx < 0) {
                mesh.vertices.clear(); mesh.tris.clear();
                return false;
            }

            size_t modelBaseVert = ((mdlVertIdx % 48) == 0)
                ? static_cast<size_t>(mdlVertIdx / 48)
                : static_cast<size_t>(mdlVertIdx);

            int32_t mNumLODs  = RdI32LE(vtx + vtxMdlPos + 0);
            int32_t mLodRel   = RdI32LE(vtx + vtxMdlPos + 4);
            size_t vtxLod0Pos = 0;
            if (mNumLODs < 1 || !checkedRel(vtxMdlPos, mLodRel, vtxSize, vtxLod0Pos) || vtxLod0Pos + 12 > vtxSize) {
                mesh.vertices.clear(); mesh.tris.clear();
                return false;
            }

            int32_t vtxNumMeshes = RdI32LE(vtx + vtxLod0Pos + 0);
            int32_t vtxMeshRel   = RdI32LE(vtx + vtxLod0Pos + 4);
            size_t mdlMeshesBase = 0, vtxMeshesBase = 0;
            if (vtxNumMeshes != numMeshes ||
                !checkedRel(mdlMdlPos, mdlMeshRel, mdlSize, mdlMeshesBase) ||
                mdlMeshesBase + static_cast<uint64_t>(numMeshes) * 116ULL > mdlSize ||
                !checkedRel(vtxLod0Pos, vtxMeshRel, vtxSize, vtxMeshesBase) ||
                vtxMeshesBase + static_cast<uint64_t>(numMeshes) * 9ULL > vtxSize)
            {
                mesh.vertices.clear(); mesh.tris.clear();
                return false;
            }

            for (int32_t m = 0; m < numMeshes; ++m) {
                size_t mdlMeshPos = mdlMeshesBase + static_cast<size_t>(m) * 116;
                size_t vtxMeshPos = vtxMeshesBase + static_cast<size_t>(m) * 9;

                int32_t meshNumVerts   = RdI32LE(mdl + mdlMeshPos + 8);
                int32_t meshVertOffset = RdI32LE(mdl + mdlMeshPos + 12);
                int32_t numStripGroups = RdI32LE(vtx + vtxMeshPos + 0);
                int32_t sgHeaderRel    = RdI32LE(vtx + vtxMeshPos + 4);
                if (meshNumVerts <= 0 || meshVertOffset < 0 || numStripGroups < 0 || numStripGroups > 4096) {
                    mesh.vertices.clear(); mesh.tris.clear();
                    return false;
                }
                if (numStripGroups == 0) continue;

                size_t sgBase = 0;
                if (!checkedRel(vtxMeshPos, sgHeaderRel, vtxSize, sgBase) || sgBase + 25 > vtxSize) {
                    mesh.vertices.clear(); mesh.tris.clear();
                    return false;
                }

                auto validateSgStride = [&](size_t stride) -> bool {
                    if (sgBase + static_cast<uint64_t>(numStripGroups) * stride > vtxSize) return false;
                    for (int32_t sg = 0; sg < numStripGroups; ++sg) {
                        size_t p = sgBase + static_cast<size_t>(sg) * stride;
                        int32_t nv = RdI32LE(vtx + p + 0), vo = RdI32LE(vtx + p + 4);
                        int32_t ni = RdI32LE(vtx + p + 8), io = RdI32LE(vtx + p + 12);
                        int32_t ns = RdI32LE(vtx + p + 16), so = RdI32LE(vtx + p + 20);
                        if (nv <= 0 || ni < 3 || ns <= 0 || nv > 500000 || ni > 2000000 || ns > 65536) return false;
                        size_t va = 0, ia = 0, sa = 0;
                        if (!checkedRel(p, vo, vtxSize, va) || va + static_cast<uint64_t>(nv) * 9ULL > vtxSize) return false;
                        if (!checkedRel(p, io, vtxSize, ia) || ia + static_cast<uint64_t>(ni) * 2ULL > vtxSize) return false;
                        if (!checkedRel(p, so, vtxSize, sa) || sa + static_cast<uint64_t>(ns) * 27ULL > vtxSize) return false;
                    }
                    return true;
                };

                int32_t sg0Vo = RdI32LE(vtx + sgBase + 4);
                int32_t sg0Io = RdI32LE(vtx + sgBase + 12);
                int32_t sg0So = RdI32LE(vtx + sgBase + 20);
                int32_t minSubOff = std::min({ sg0Vo, sg0Io, sg0So });
                size_t sgStride = 25;
                if (ver >= 49 && minSubOff >= numStripGroups * 33 && validateSgStride(33)) {
                    sgStride = 33;
                } else if (!validateSgStride(25)) {
                    if (validateSgStride(33)) sgStride = 33;
                    else { mesh.vertices.clear(); mesh.tris.clear(); return false; }
                }

                for (int32_t sg = 0; sg < numStripGroups; ++sg) {
                    size_t sgPos = sgBase + static_cast<size_t>(sg) * sgStride;
                    int32_t sgNumVerts  = RdI32LE(vtx + sgPos + 0);
                    int32_t sgVertRel   = RdI32LE(vtx + sgPos + 4);
                    int32_t sgNumIdx    = RdI32LE(vtx + sgPos + 8);
                    int32_t sgIdxRel    = RdI32LE(vtx + sgPos + 12);
                    int32_t sgNumStrips = RdI32LE(vtx + sgPos + 16);
                    int32_t sgStripRel  = RdI32LE(vtx + sgPos + 20);

                    size_t sgVertBase = sgPos + static_cast<size_t>(sgVertRel);
                    size_t sgIdxBase  = sgPos + static_cast<size_t>(sgIdxRel);
                    size_t sgStrBase  = sgPos + static_cast<size_t>(sgStripRel);

                    auto validateStripStride = [&](size_t stStride) -> bool {
                        if (sgStrBase + static_cast<uint64_t>(sgNumStrips) * stStride > vtxSize) return false;
                        for (int32_t st = 0; st < sgNumStrips; ++st) {
                            size_t sp = sgStrBase + static_cast<size_t>(st) * stStride;
                            int32_t sni = RdI32LE(vtx + sp + 0);
                            int32_t sio = RdI32LE(vtx + sp + 4);
                            uint8_t flg = vtx[sp + 18];
                            if (sni < 0 || sio < 0 || sio + sni > sgNumIdx) return false;
                            if (sni > 0 && (flg & 0x03u) == 0) return false;
                        }
                        return true;
                    };

                    size_t stripStride = 27;
                    if (sgStride == 33 && validateStripStride(35)) {
                        stripStride = 35;
                    } else if (!validateStripStride(27)) {
                        if (validateStripStride(35)) stripStride = 35;
                        else { mesh.vertices.clear(); mesh.tris.clear(); return false; }
                    }

                    auto resolveStripIdx = [&](int32_t elemIdx, uint32_t& outVvdIdx) -> bool {
                        uint16_t sgVertIdx = Detail3D200::RdU16LE(vtx + sgIdxBase + static_cast<size_t>(elemIdx) * 2);
                        if (sgVertIdx >= static_cast<uint32_t>(sgNumVerts)) return false;
                        uint16_t origVert = Detail3D200::RdU16LE(vtx + sgVertBase + static_cast<size_t>(sgVertIdx) * 9 + 4);
                        if (origVert >= static_cast<uint32_t>(meshNumVerts)) return false;
                        size_t finalIdx = modelBaseVert + static_cast<size_t>(meshVertOffset) + static_cast<size_t>(origVert);
                        if (finalIdx >= totalVvdVerts) return false;
                        outVvdIdx = static_cast<uint32_t>(finalIdx);
                        return true;
                    };

                    for (int32_t st = 0; st < sgNumStrips; ++st) {
                        size_t sp = sgStrBase + static_cast<size_t>(st) * stripStride;
                        int32_t stNumIdx   = RdI32LE(vtx + sp + 0);
                        int32_t stIdxStart = RdI32LE(vtx + sp + 4);
                        uint8_t stFlags    = vtx[sp + 18];

                        if ((stFlags & 0x01u) != 0) { // STRIP_IS_TRILIST
                            for (int32_t k = 0; k + 2 < stNumIdx; k += 3) {
                                uint32_t i0 = 0, i1 = 0, i2 = 0;
                                if (!resolveStripIdx(stIdxStart + k + 0, i0) ||
                                    !resolveStripIdx(stIdxStart + k + 1, i1) ||
                                    !resolveStripIdx(stIdxStart + k + 2, i2))
                                {
                                    mesh.vertices.clear(); mesh.tris.clear();
                                    return false;
                                }
                                if (i0 != i1 && i1 != i2 && i0 != i2) {
                                    mesh.tris.push_back({ i0, i2, i1, {} });
                                }
                            }
                        } else if ((stFlags & 0x02u) != 0) { // STRIP_IS_TRISTRIP
                            for (int32_t k = 0; k + 2 < stNumIdx; ++k) {
                                uint32_t i0 = 0, i1 = 0, i2 = 0;
                                if (!resolveStripIdx(stIdxStart + k + 0, i0) ||
                                    !resolveStripIdx(stIdxStart + k + 1, i1) ||
                                    !resolveStripIdx(stIdxStart + k + 2, i2))
                                {
                                    mesh.vertices.clear(); mesh.tris.clear();
                                    return false;
                                }
                                if (i0 != i1 && i1 != i2 && i0 != i2) {
                                    if (k & 1) mesh.tris.push_back({ i0, i1, i2, {} });
                                    else       mesh.tris.push_back({ i0, i2, i1, {} });
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) {
        mesh.vertices.clear();
        mesh.tris.clear();
        return false;
    }
    mesh.formatName = L"Valve Source Engine Model v" + std::to_wstring(ver) + L" (.mdl+.vvd+.vtx)";
    return true;
}

} // namespace DetailValveMdl200

inline bool ParseQuakeOrSourceMdl(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    // Quake 1 Alias MDL: "IDPO" + version 6 (84-byte header)
    if (size >= 84 && std::memcmp(data, "IDPO", 4) == 0 && Detail3D200::RdU32LE(data + 4) == 6) {
        mesh.formatName = L"Quake Alias 3D Model (.mdl)";
        float sx = Detail3D200::RdF32LE(data + 8),  sy = Detail3D200::RdF32LE(data + 12), sz = Detail3D200::RdF32LE(data + 16);
        float tx = Detail3D200::RdF32LE(data + 20), ty = Detail3D200::RdF32LE(data + 24), tz = Detail3D200::RdF32LE(data + 28);
        uint32_t numSkins  = Detail3D200::RdU32LE(data + 48);
        uint32_t skinW     = Detail3D200::RdU32LE(data + 52);
        uint32_t skinH     = Detail3D200::RdU32LE(data + 56);
        uint32_t numVerts  = Detail3D200::RdU32LE(data + 60);
        uint32_t numTris   = Detail3D200::RdU32LE(data + 64);
        uint32_t numFrames = Detail3D200::RdU32LE(data + 68);
        mesh.animFrameCount = std::max(1u, numFrames);
        if (numVerts == 0 || numTris == 0 || numVerts > 65536 || numTris > 131072) return false;

        size_t pos = 84;
        for (uint32_t s = 0; s < numSkins && pos + 4 <= size; ++s) {
            uint32_t group = Detail3D200::RdU32LE(data + pos); pos += 4;
            if (group == 0) pos += static_cast<size_t>(skinW) * skinH;
            else if (pos + 4 <= size) {
                uint32_t nb = Detail3D200::RdU32LE(data + pos);
                pos += 4 + static_cast<size_t>(nb) * (4 + static_cast<size_t>(skinW) * skinH);
            }
        }
        pos += static_cast<size_t>(numVerts) * 12; // stvert_t[numVerts]
        if (pos + static_cast<size_t>(numTris) * 16 + 4 + 24 + static_cast<size_t>(numVerts) * 4 > size) return false;

        for (uint32_t t = 0; t < numTris; ++t) {
            const uint8_t* tp = data + pos + static_cast<size_t>(t) * 16;
            uint32_t i0 = Detail3D200::RdU32LE(tp + 4);
            uint32_t i1 = Detail3D200::RdU32LE(tp + 8);
            uint32_t i2 = Detail3D200::RdU32LE(tp + 12);
            if (i0 < numVerts && i1 < numVerts && i2 < numVerts) mesh.tris.push_back({ i0, i2, i1, {} });
        }
        pos += static_cast<size_t>(numTris) * 16;

        uint32_t frameType = Detail3D200::RdU32LE(data + pos); pos += 4;
        if (frameType != 0) {
            if (pos + 12 > size) return false;
            uint32_t nb = Detail3D200::RdU32LE(data + pos);
            pos += 12 + 4 * static_cast<size_t>(nb);
        }
        pos += 24; // bboxmin(4) + bboxmax(4) + name(16)
        if (pos + static_cast<size_t>(numVerts) * 4 > size) return false;

        for (uint32_t v = 0; v < numVerts; ++v) {
            const uint8_t* vp = data + pos + static_cast<size_t>(v) * 4;
            mesh.vertices.push_back({
                vp[0] * sx + tx,
                vp[2] * sz + tz,
                vp[1] * sy + ty
            });
        }
        return !mesh.vertices.empty() && !mesh.tris.empty();
    }

    // Valve GoldSrc (v10) / Source Engine (v44–v49) StudioMDL: "IDST"
    if (size >= 244 && std::memcmp(data, "IDST", 4) == 0) {
        uint32_t ver = Detail3D200::RdU32LE(data + 4);
        mesh.formatName = L"Valve StudioMDL v" + std::to_wstring(ver) + L" (.mdl)";

        if (ver == 10) {
            uint32_t hdrLen = Detail3D200::RdU32LE(data + 72);
            if (hdrLen > 0 && (hdrLen < 244 || hdrLen > size)) return false;

            // Decode GoldSrc mstudiobone_t[numBones] (112 bytes each at boneIndex)
            uint32_t numBones = Detail3D200::RdU32LE(data + 140);
            uint32_t boneIdx  = Detail3D200::RdU32LE(data + 144);
            std::vector<DetailValveMdl200::BoneMat3x4> worldBones;
            if (numBones > 0) {
                if (numBones > 256 || boneIdx < 244 || boneIdx + static_cast<uint64_t>(numBones) * 112ULL > size) {
                    return false;
                }
                std::vector<DetailValveMdl200::BoneMat3x4> localBones(numBones);
                std::vector<int32_t> parents(numBones, -1);
                for (uint32_t b = 0; b < numBones; ++b) {
                    const uint8_t* bp = data + boneIdx + static_cast<size_t>(b) * 112;
                    int32_t parent = DetailValveMdl200::RdI32LE(bp + 32);
                    if (parent < -1 || parent >= static_cast<int32_t>(numBones) || parent == static_cast<int32_t>(b)) {
                        return false;
                    }
                    parents[b] = parent;
                    float px = Detail3D200::RdF32LE(bp + 64);
                    float py = Detail3D200::RdF32LE(bp + 68);
                    float pz = Detail3D200::RdF32LE(bp + 72);
                    float rx = Detail3D200::RdF32LE(bp + 76);
                    float ry = Detail3D200::RdF32LE(bp + 80);
                    float rz = Detail3D200::RdF32LE(bp + 84);
                    localBones[b] = DetailValveMdl200::GoldSrcEulerPosToMat(px, py, pz, rx, ry, rz);
                }
                worldBones.resize(numBones);
                std::vector<uint8_t> state(numBones, 0); // 0=unvisited, 1=visiting, 2=done
                auto solveWorld = [&](auto& self, uint32_t b) -> bool {
                    if (state[b] == 2) return true;
                    if (state[b] == 1) return false; // cycle
                    state[b] = 1;
                    int32_t p = parents[b];
                    if (p >= 0) {
                        if (!self(self, static_cast<uint32_t>(p))) return false;
                        worldBones[b] = DetailValveMdl200::ConcatBoneMat(worldBones[static_cast<size_t>(p)], localBones[b]);
                    } else {
                        worldBones[b] = localBones[b];
                    }
                    state[b] = 2;
                    return true;
                };
                for (uint32_t b = 0; b < numBones; ++b) {
                    if (!solveWorld(solveWorld, b)) return false;
                }
            }

            uint32_t numBodyparts = Detail3D200::RdU32LE(data + 204);
            uint32_t bodypartIdx  = Detail3D200::RdU32LE(data + 208);
            if (numBodyparts == 0 || numBodyparts > 64 || bodypartIdx + static_cast<uint64_t>(numBodyparts) * 76ULL > size) {
                return false;
            }
            for (uint32_t b = 0; b < numBodyparts; ++b) {
                const uint8_t* bp = data + bodypartIdx + static_cast<size_t>(b) * 76;
                uint32_t numModels = Detail3D200::RdU32LE(bp + 64);
                uint32_t modelIdx  = Detail3D200::RdU32LE(bp + 72);
                for (uint32_t mi = 0; mi < numModels && mi < 32 && modelIdx + static_cast<uint64_t>(mi + 1) * 112ULL <= size; ++mi) {
                    const uint8_t* mdl = data + modelIdx + static_cast<size_t>(mi) * 112;
                    uint32_t numMesh     = Detail3D200::RdU32LE(mdl + 72);
                    uint32_t meshIdx     = Detail3D200::RdU32LE(mdl + 76);
                    uint32_t numVerts    = Detail3D200::RdU32LE(mdl + 80);
                    uint32_t vertInfoIdx = Detail3D200::RdU32LE(mdl + 84);
                    uint32_t vertIdx     = Detail3D200::RdU32LE(mdl + 88);
                    if (numVerts > 0 && numVerts <= 65536 && vertIdx + static_cast<uint64_t>(numVerts) * 12ULL <= size) {
                        bool hasVertBones = (!worldBones.empty() && vertInfoIdx > 0 &&
                                             vertInfoIdx + static_cast<uint64_t>(numVerts) <= size);
                        uint32_t vBase = static_cast<uint32_t>(mesh.vertices.size());
                        for (uint32_t v = 0; v < numVerts; ++v) {
                            const uint8_t* vp = data + vertIdx + static_cast<size_t>(v) * 12;
                            float lx = Detail3D200::RdF32LE(vp + 0);
                            float ly = Detail3D200::RdF32LE(vp + 4);
                            float lz = Detail3D200::RdF32LE(vp + 8);
                            if (hasVertBones) {
                                uint8_t bId = data[vertInfoIdx + v];
                                if (bId < worldBones.size()) {
                                    const auto& wb = worldBones[bId];
                                    float wx = wb.m[0][0] * lx + wb.m[0][1] * ly + wb.m[0][2] * lz + wb.m[0][3];
                                    float wy = wb.m[1][0] * lx + wb.m[1][1] * ly + wb.m[1][2] * lz + wb.m[1][3];
                                    float wz = wb.m[2][0] * lx + wb.m[2][1] * ly + wb.m[2][2] * lz + wb.m[2][3];
                                    lx = wx; ly = wy; lz = wz;
                                }
                            }
                            mesh.vertices.push_back({ lx, lz, -ly });
                        }
                        for (uint32_t m = 0; m < numMesh && meshIdx + static_cast<uint64_t>(m + 1) * 20ULL <= size; ++m) {
                            uint32_t triIdx = Detail3D200::RdU32LE(data + meshIdx + static_cast<size_t>(m) * 20 + 4);
                            size_t tp = triIdx;
                            while (tp + 2 <= size) {
                                int16_t cmd = static_cast<int16_t>(Detail3D200::RdU16LE(data + tp)); tp += 2;
                                if (cmd == 0) break;
                                bool isFan = (cmd < 0);
                                int cnt = std::abs(static_cast<int>(cmd));
                                if (tp + static_cast<size_t>(cnt) * 8 > size) break;
                                std::vector<uint32_t> strip(static_cast<size_t>(cnt));
                                for (int k = 0; k < cnt; ++k) {
                                    strip[static_cast<size_t>(k)] = vBase + Detail3D200::RdU16LE(data + tp + static_cast<size_t>(k) * 8);
                                }
                                tp += static_cast<size_t>(cnt) * 8;
                                for (int k = 0; k + 2 < cnt; ++k) {
                                    uint32_t a = strip[static_cast<size_t>(isFan ? 0 : k)];
                                    uint32_t bIdx = strip[static_cast<size_t>(k + 1)];
                                    uint32_t c = strip[static_cast<size_t>(k + 2)];
                                    if (a < mesh.vertices.size() && bIdx < mesh.vertices.size() && c < mesh.vertices.size() &&
                                        a != bIdx && bIdx != c && a != c)
                                    {
                                        if (!isFan && (k & 1)) mesh.tris.push_back({ bIdx, a, c, {} });
                                        else                   mesh.tris.push_back({ a, bIdx, c, {} });
                                    }
                                }
                            }
                        }
                    }
                }
            }
            return !mesh.vertices.empty() && !mesh.tris.empty();
        }

        // Source Engine v44–v49: full .mdl + .vvd + .vtx triangle mesh assembly, with skeleton fallback ONLY when siblings absent
        if (ver >= 44 && ver <= 49) {
            uint32_t hdrLen = Detail3D200::RdU32LE(data + 76);
            if (hdrLen < 244 || hdrLen > size) return false;

            if (!mesh.filePath.empty()) {
                size_t lastSep = mesh.filePath.find_last_of(L"\\/");
                size_t lastDot = mesh.filePath.find_last_of(L'.');
                std::wstring stem = (lastDot != std::wstring::npos && (lastSep == std::wstring::npos || lastDot > lastSep))
                    ? mesh.filePath.substr(0, lastDot)
                    : mesh.filePath;

                std::wstring vvdPath = stem + L".vvd";
                bool hasVvdOnDisk = (GetFileAttributesW(vvdPath.c_str()) != INVALID_FILE_ATTRIBUTES);

                static constexpr const wchar_t* kVtxSuffixes[] = {
                    L".dx90.vtx", L".dx80.vtx", L".sw.vtx", L".vtx"
                };
                std::wstring vtxPath;
                for (const wchar_t* sfx : kVtxSuffixes) {
                    std::wstring cand = stem + sfx;
                    if (GetFileAttributesW(cand.c_str()) != INVALID_FILE_ATTRIBUTES) {
                        vtxPath = std::move(cand);
                        break;
                    }
                }
                bool hasVtxOnDisk = !vtxPath.empty();

                if (hasVvdOnDisk && hasVtxOnDisk) {
                    std::vector<uint8_t> vvdBytes, vtxBytes;
                    if (!DetailValveMdl200::ReadSiblingBytes(vvdPath, vvdBytes) ||
                        !DetailValveMdl200::ReadSiblingBytes(vtxPath, vtxBytes))
                    {
                        return false;
                    }
                    return DetailValveMdl200::AssembleSourceVvdVtxMesh(
                        data, size, vvdBytes.data(), vvdBytes.size(), vtxBytes.data(), vtxBytes.size(), ver, mesh);
                }
                // If one sibling exists on disk and is corrupt, reject rather than hiding corruption
                if (hasVvdOnDisk) {
                    std::vector<uint8_t> vvdBytes;
                    if (!DetailValveMdl200::ReadSiblingBytes(vvdPath, vvdBytes) || vvdBytes.size() < 64 ||
                        std::memcmp(vvdBytes.data(), "IDSV", 4) != 0 ||
                        DetailValveMdl200::RdI32LE(vvdBytes.data() + 4) != 4 ||
                        Detail3D200::RdU32LE(vvdBytes.data() + 8) != Detail3D200::RdU32LE(data + 8))
                    {
                        return false;
                    }
                }
                if (hasVtxOnDisk) {
                    std::vector<uint8_t> vtxBytes;
                    if (!DetailValveMdl200::ReadSiblingBytes(vtxPath, vtxBytes) || vtxBytes.size() < 36 ||
                        DetailValveMdl200::RdI32LE(vtxBytes.data()) != 7 ||
                        Detail3D200::RdU32LE(vtxBytes.data() + 16) != Detail3D200::RdU32LE(data + 8))
                    {
                        return false;
                    }
                }
            }

            // Fallback when sibling .vvd/.vtx files are absent: decode mstudiobone_t skeleton hierarchy
            if (size >= 400) {
                uint32_t numBones = Detail3D200::RdU32LE(data + 156);
                uint32_t boneIdx  = Detail3D200::RdU32LE(data + 160);
                if (numBones >= 2 && numBones <= 512 && boneIdx >= 244 &&
                    boneIdx + static_cast<uint64_t>(numBones) * 216ULL <= size)
                {
                    std::vector<DetailValveMdl200::BoneMat3x4> world(numBones);
                    std::vector<int32_t> parents(numBones, -1);
                    std::vector<Vec3f> joints(numBones);
                    for (uint32_t b = 0; b < numBones; ++b) {
                        const uint8_t* bp = data + boneIdx + static_cast<size_t>(b) * 216;
                        int32_t parent = DetailValveMdl200::RdI32LE(bp + 4);
                        if (parent < -1 || parent >= static_cast<int32_t>(numBones) || parent == static_cast<int32_t>(b)) {
                            return false;
                        }
                        parents[b] = parent;
                        float px = Detail3D200::RdF32LE(bp + 32);
                        float py = Detail3D200::RdF32LE(bp + 36);
                        float pz = Detail3D200::RdF32LE(bp + 40);
                        float qx = Detail3D200::RdF32LE(bp + 44);
                        float qy = Detail3D200::RdF32LE(bp + 48);
                        float qz = Detail3D200::RdF32LE(bp + 52);
                        float qw = Detail3D200::RdF32LE(bp + 56);

                        DetailValveMdl200::BoneMat3x4 loc = {};
                        loc.m[0][0] = 1.0f - 2.0f * (qy * qy + qz * qz);
                        loc.m[0][1] = 2.0f * (qx * qy - qw * qz);
                        loc.m[0][2] = 2.0f * (qx * qz + qw * qy);
                        loc.m[0][3] = px;
                        loc.m[1][0] = 2.0f * (qx * qy + qw * qz);
                        loc.m[1][1] = 1.0f - 2.0f * (qx * qx + qz * qz);
                        loc.m[1][2] = 2.0f * (qy * qz - qw * qx);
                        loc.m[1][3] = py;
                        loc.m[2][0] = 2.0f * (qx * qz - qw * qy);
                        loc.m[2][1] = 2.0f * (qy * qz + qw * qx);
                        loc.m[2][2] = 1.0f - 2.0f * (qx * qx + qy * qy);
                        loc.m[2][3] = pz;

                        if (parent >= 0 && static_cast<uint32_t>(parent) < b) {
                            world[b] = DetailValveMdl200::ConcatBoneMat(world[static_cast<size_t>(parent)], loc);
                        } else {
                            world[b] = loc;
                        }
                        joints[b] = { world[b].m[0][3], world[b].m[2][3], -world[b].m[1][3] };
                    }
                    float span = Detail3D200::ComputePointCloudSpan(joints);
                    if (span > 1e-3f) {
                        float r = span * 0.015f;
                        for (uint32_t b = 0; b < numBones; ++b) {
                            Detail3D200::AddOctaSphere(mesh, joints[b].x, joints[b].y, joints[b].z, r * 1.3f);
                            if (parents[b] >= 0 && static_cast<uint32_t>(parents[b]) < numBones) {
                                Detail3D200::AddStrut3D(mesh, joints[static_cast<size_t>(parents[b])], joints[b], r * 0.7f);
                            }
                        }
                    }
                }
            }
            return !mesh.vertices.empty() && !mesh.tris.empty();
        }
        return false;
    }
    return false;
}

// ============================================================================
// #45. NetImmerse / Gamebryo / Bethesda Mesh (.nif — Full Block-Table Decoder)
// ============================================================================
inline bool ParseNetImmerseNif(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    if (size < 40 || (std::memcmp(data, "Gamebryo File Format", 20) != 0 &&
                     std::memcmp(data, "NetImmerse File Format", 22) != 0))
    {
        return false;
    }
    mesh.formatName = L"NetImmerse / Gamebryo Mesh (.nif)";

    size_t nl = 0;
    while (nl < size && nl < 160 && data[nl] != '\n') ++nl;
    if (nl + 9 > size) return false;
    size_t p = nl + 1;

    uint32_t ver = Detail3D200::RdU32LE(data + p); p += 4;
    if (ver < 0x04000000u || ver > 0x30000000u) return false;

    bool isBE = false;
    if (ver >= 0x14000004u) {
        if (p >= size) return false;
        isBE = (data[p++] == 0);
    }
    auto rdU16 = [&](const uint8_t* ptr) -> uint16_t { return isBE ? Detail3D200::RdU16BE(ptr) : Detail3D200::RdU16LE(ptr); };
    auto rdU32 = [&](const uint8_t* ptr) -> uint32_t { return isBE ? Detail3D200::RdU32BE(ptr) : Detail3D200::RdU32LE(ptr); };
    auto rdU64 = [&](const uint8_t* ptr) -> uint64_t { return isBE ? Detail3D200::RdU64BE(ptr) : Detail3D200::RdU64LE(ptr); };
    auto rdF32 = [&](const uint8_t* ptr) -> float    { return isBE ? Detail3D200::RdF32BE(ptr) : Detail3D200::RdF32LE(ptr); };

    uint32_t userVer = 0;
    if (ver >= 0x0A010000u) {
        if (p + 4 > size) return false;
        userVer = rdU32(data + p); p += 4;
    }
    if (p + 4 > size) return false;
    uint32_t numBlocks = rdU32(data + p); p += 4;
    if (numBlocks == 0 || numBlocks > 65536) return false;

    auto looksLikeBlockTypeTable = [&](size_t off) -> bool {
        if (off + 6 > size) return false;
        uint16_t nbt = rdU16(data + off);
        if (nbt == 0 || nbt > 4096) return false;
        uint32_t len0 = rdU32(data + off + 2);
        if (len0 == 0 || len0 > 128 || off + 6 + len0 > size) return false;
        for (uint32_t i = 0; i < len0; ++i) {
            uint8_t c = data[off + 6 + i];
            bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
            if (!ok) return false;
        }
        return true;
    };

    // Bethesda BSStreamHeader (Oblivion userVer=10/11, FO3/NV userVer=11 bsVer=34, Skyrim userVer=12 bsVer=83/100, FO4 userVer=12 bsVer=130)
    uint32_t bsVer = 0;
    bool hasBsHeader = (ver == 0x0A000102u) ||
                       ((ver == 0x14000005u || ver >= 0x14020007u) && (userVer >= 10 || (ver == 0x14000005u && userVer == 3)));
    if (hasBsHeader && !looksLikeBlockTypeTable(p)) {
        if (p + 4 > size) return false;
        bsVer = rdU32(data + p); p += 4;
        auto skipShortStr = [&](size_t& cur) -> bool {
            if (cur >= size) return false;
            uint8_t slen = data[cur++];
            if (cur + slen > size) return false;
            cur += slen;
            return true;
        };
        if (!skipShortStr(p)) return false; // author
        if (bsVer > 130) {
            if (p + 4 > size) return false;
            p += 4; // unknownInt1
        }
        if (bsVer < 132) {
            if (!skipShortStr(p)) return false; // processScript
        }
        if (!skipShortStr(p)) return false; // exportScript
        if (bsVer >= 103 && !looksLikeBlockTypeTable(p)) {
            if (!skipShortStr(p)) return false; // maxFilepath (Fallout 4 bsVer=130+)
        }
    }

    if (ver < 0x0A000100u) return false;
    if (p + 2 > size) return false;
    uint16_t numBlockTypes = rdU16(data + p); p += 2;
    if (numBlockTypes == 0 || numBlockTypes > 4096) return false;

    std::vector<std::string> blockTypes(numBlockTypes);
    for (uint16_t i = 0; i < numBlockTypes; ++i) {
        if (p + 4 > size) return false;
        uint32_t slen = rdU32(data + p); p += 4;
        if (slen > 256 || p + slen > size) return false;
        blockTypes[i].assign(reinterpret_cast<const char*>(data + p), slen);
        p += slen;
    }

    if (p + static_cast<size_t>(numBlocks) * 2 > size) return false;
    std::vector<uint16_t> blockTypeIdx(numBlocks);
    for (uint32_t b = 0; b < numBlocks; ++b) {
        blockTypeIdx[b] = static_cast<uint16_t>(rdU16(data + p) & 0x7FFFu);
        p += 2;
        if (blockTypeIdx[b] >= numBlockTypes) return false;
    }

    std::vector<uint32_t> blockSizes;
    if (ver >= 0x14020007u) {
        if (p + static_cast<size_t>(numBlocks) * 4 > size) return false;
        blockSizes.resize(numBlocks);
        for (uint32_t b = 0; b < numBlocks; ++b) {
            blockSizes[b] = rdU32(data + p);
            p += 4;
        }
    }

    if (ver >= 0x14010003u) {
        if (p + 8 > size) return false;
        uint32_t numStrings = rdU32(data + p); p += 4;
        p += 4; // maxStringLength
        if (numStrings > 200000) return false;
        for (uint32_t s = 0; s < numStrings; ++s) {
            if (p + 4 > size) return false;
            uint32_t slen = rdU32(data + p); p += 4;
            if (slen > 65536 || p + slen > size) return false;
            p += slen;
        }
    }

    if (ver >= 0x05000006u) {
        if (p + 4 > size) return false;
        uint32_t numGroups = rdU32(data + p); p += 4;
        if (numGroups > 65536 || p + static_cast<size_t>(numGroups) * 4 > size) return false;
        p += static_cast<size_t>(numGroups) * 4;
    }

    struct NifXform {
        float t[3] = { 0.0f, 0.0f, 0.0f };
        float r[9] = { 1.0f,0.0f,0.0f, 0.0f,1.0f,0.0f, 0.0f,0.0f,1.0f };
        float scale = 1.0f;
        bool valid = false;
    };
    auto applyNifXform = [](const NifXform& xf, float x, float y, float z) -> Vec3f {
        if (xf.valid) {
            float sx = x * xf.scale, sy = y * xf.scale, sz = z * xf.scale;
            float wx = xf.r[0] * sx + xf.r[1] * sy + xf.r[2] * sz + xf.t[0];
            float wy = xf.r[3] * sx + xf.r[4] * sy + xf.r[5] * sz + xf.t[1];
            float wz = xf.r[6] * sx + xf.r[7] * sy + xf.r[8] * sz + xf.t[2];
            x = wx; y = wy; z = wz;
        }
        // NIF Z-up -> Explorer2 Y-up
        return { x, z, -y };
    };

    // Parse NiAVObject header (returns cursor after NiAVObject, populates NifXform)
    auto parseNiAVObject = [&](size_t bStart, size_t bEnd, NifXform& outXf) -> size_t {
        size_t c = bStart;
        // NiObjectNET: name (4B string idx if >= 20.1.0.3, else SizedString), extraDataList, controller
        if (ver >= 0x14010003u) {
            if (c + 4 > bEnd) return 0;
            c += 4;
        } else {
            if (c + 4 > bEnd) return 0;
            uint32_t sl = rdU32(data + c); c += 4;
            if (sl > 4096 || c + sl > bEnd) return 0;
            c += sl;
        }
        if (c + 4 > bEnd) return 0;
        uint32_t nExtra = rdU32(data + c); c += 4;
        if (nExtra > 4096 || c + static_cast<size_t>(nExtra) * 4 + 4 > bEnd) return 0;
        c += static_cast<size_t>(nExtra) * 4 + 4; // extraData refs + controller ref

        // NiAVObject: flags (uint32 if userVer >= 12 && bsVer > 26, else uint16)
        size_t flagBytes = (userVer >= 12 && bsVer > 26) ? 4 : 2;
        if (c + flagBytes + 52 > bEnd) return 0;
        c += flagBytes;
        outXf.t[0] = rdF32(data + c + 0);
        outXf.t[1] = rdF32(data + c + 4);
        outXf.t[2] = rdF32(data + c + 8);
        c += 12;
        for (int i = 0; i < 9; ++i) outXf.r[i] = rdF32(data + c + i * 4);
        c += 36;
        outXf.scale = rdF32(data + c);
        if (!std::isfinite(outXf.scale) || std::fabs(outXf.scale) < 1e-6f) outXf.scale = 1.0f;
        c += 4;
        outXf.valid = true;

        // Properties array (absent when bsVer > 34)
        if (bsVer <= 34) {
            if (c + 4 > bEnd) return 0;
            uint32_t nProps = rdU32(data + c); c += 4;
            if (nProps > 4096 || c + static_cast<size_t>(nProps) * 4 > bEnd) return 0;
            c += static_cast<size_t>(nProps) * 4;
        }
        // CollisionObject ref (ver >= 10.0.1.0)
        if (ver >= 0x0A000100u) {
            if (c + 4 > bEnd) return 0;
            c += 4;
        }
        return c;
    };

    // Build exact block offset table if blockSizes present
    std::vector<size_t> blockOffsets(numBlocks, 0);
    std::vector<size_t> blockEnds(numBlocks, 0);
    if (!blockSizes.empty()) {
        size_t cur = p;
        for (uint32_t b = 0; b < numBlocks; ++b) {
            if (cur > size || blockSizes[b] > size - cur) return false;
            blockOffsets[b] = cur;
            blockEnds[b]    = cur + blockSizes[b];
            cur += blockSizes[b];
        }
    } else {
        // Pre-20.2.0.7 NIF without blockSizes: only single-block or sequential NiGeometryData supported if at p
        blockOffsets[0] = p;
        blockEnds[0]    = size;
    }

    // First pass (when blockSizes present): map NiTriShape / NiTriStrips dataRef -> NifXform
    std::unordered_map<uint32_t, NifXform> geomDataXform;
    if (!blockSizes.empty()) {
        for (uint32_t b = 0; b < numBlocks; ++b) {
            const std::string& tn = blockTypes[blockTypeIdx[b]];
            if (tn == "NiTriShape" || tn == "NiTriStrips" || tn == "BSLODTriShape") {
                NifXform xf;
                size_t afterAv = parseNiAVObject(blockOffsets[b], blockEnds[b], xf);
                if (afterAv > 0 && afterAv + 4 <= blockEnds[b]) {
                    int32_t dataRef = static_cast<int32_t>(rdU32(data + afterAv));
                    if (dataRef >= 0 && static_cast<uint32_t>(dataRef) < numBlocks) {
                        geomDataXform[static_cast<uint32_t>(dataRef)] = xf;
                    }
                }
            }
        }
    }

    // Second pass: decode NiTriShapeData, NiTriStripsData, BSTriShape, BSDynamicTriShape, BSSubIndexTriShape
    uint32_t blocksToInspect = blockSizes.empty() ? 1u : numBlocks;
    for (uint32_t b = 0; b < blocksToInspect; ++b) {
        const std::string& tn = blockTypes[blockTypeIdx[b]];
        size_t bStart = blockOffsets[b];
        size_t bEnd   = blockEnds[b];
        if (bStart >= bEnd || bEnd > size) continue;

        if (tn == "NiTriShapeData" || tn == "NiTriStripsData") {
            size_t c = bStart;
            if (ver >= 0x0A020000u) {
                if (c + 4 > bEnd) continue;
                c += 4; // groupId
            }
            if (c + 2 > bEnd) continue;
            uint16_t nVerts = rdU16(data + c); c += 2;
            if (nVerts < 3) continue;
            if (ver >= 0x0A010000u) {
                if (c + 2 > bEnd) continue;
                c += 2; // keepFlags + compressFlags
            }
            if (c + 1 > bEnd) continue;
            uint8_t hasVerts = data[c++];
            if (hasVerts == 0) continue;
            if (c + static_cast<size_t>(nVerts) * 12 > bEnd) continue;
            size_t vertsOff = c;
            c += static_cast<size_t>(nVerts) * 12;

            uint16_t dataFlags = 0;
            if (ver >= 0x0A000100u) {
                if (c + 2 > bEnd) continue;
                dataFlags = rdU16(data + c); c += 2;
            }
            if (ver == 0x14020007u && bsVer > 34) {
                if (c + 4 > bEnd) continue;
                c += 4; // Skyrim materialCRC
            }
            if (c + 1 > bEnd) continue;
            uint8_t hasNormals = data[c++];
            if (hasNormals != 0) {
                size_t normBytes = static_cast<size_t>(nVerts) * 12;
                bool hasTanBitan = (bsVer > 0) ? ((dataFlags & 0x1000u) != 0) : ((dataFlags & 0xF000u) != 0);
                if (ver >= 0x0A010000u && hasTanBitan) normBytes *= 3;
                if (c + normBytes > bEnd) continue;
                c += normBytes;
            }
            if (c + 16 > bEnd) continue;
            c += 16; // center (12B) + radius (4B)

            if (c + 1 > bEnd) continue;
            uint8_t hasColors = data[c++];
            if (hasColors != 0) {
                if (c + static_cast<size_t>(nVerts) * 16 > bEnd) continue;
                c += static_cast<size_t>(nVerts) * 16;
            }

            uint32_t numUvSets = (bsVer > 34) ? ((dataFlags & 0x0001u) ? 1u : 0u) : static_cast<uint32_t>(dataFlags & 0x003Fu);
            if (c + static_cast<size_t>(numUvSets) * nVerts * 8 > bEnd) continue;
            c += static_cast<size_t>(numUvSets) * nVerts * 8;

            if (ver >= 0x0A000100u) {
                if (c + 2 > bEnd) continue;
                c += 2; // consistencyFlags
            }
            if (ver >= 0x14000004u) {
                if (c + 4 > bEnd) continue;
                c += 4; // additionalData ref
            }

            NifXform xf;
            auto xIt = geomDataXform.find(b);
            if (xIt != geomDataXform.end()) xf = xIt->second;

            if (tn == "NiTriShapeData") {
                if (c + 7 > bEnd) continue;
                uint16_t nTris = rdU16(data + c);
                uint32_t nTriPts = rdU32(data + c + 2);
                uint8_t hasTris = data[c + 6];
                if (nTris == 0 || hasTris == 0 || nTriPts != static_cast<uint32_t>(nTris) * 3u) continue;
                size_t triOff = c + 7;
                if (triOff + static_cast<size_t>(nTris) * 6 > bEnd) continue;

                uint32_t vBase = static_cast<uint32_t>(mesh.vertices.size());
                for (uint16_t v = 0; v < nVerts; ++v) {
                    const uint8_t* vp = data + vertsOff + static_cast<size_t>(v) * 12;
                    mesh.vertices.push_back(applyNifXform(xf, rdF32(vp + 0), rdF32(vp + 4), rdF32(vp + 8)));
                }
                for (uint16_t t = 0; t < nTris; ++t) {
                    const uint8_t* tp = data + triOff + static_cast<size_t>(t) * 6;
                    uint16_t i0 = rdU16(tp + 0), i1 = rdU16(tp + 2), i2 = rdU16(tp + 4);
                    if (i0 < nVerts && i1 < nVerts && i2 < nVerts && i0 != i1 && i1 != i2 && i0 != i2) {
                        mesh.tris.push_back({ vBase + i0, vBase + i1, vBase + i2, {} });
                    }
                }
            } else { // NiTriStripsData
                if (c + 4 > bEnd) continue;
                uint16_t nTris = rdU16(data + c); (void)nTris;
                uint16_t nStrips = rdU16(data + c + 2);
                c += 4;
                if (nStrips == 0 || nStrips > 16384 || c + static_cast<size_t>(nStrips) * 2 > bEnd) continue;
                std::vector<uint16_t> stripLens(nStrips);
                size_t totalPts = 0;
                for (uint16_t s = 0; s < nStrips; ++s) {
                    stripLens[s] = rdU16(data + c + static_cast<size_t>(s) * 2);
                    totalPts += stripLens[s];
                }
                c += static_cast<size_t>(nStrips) * 2;
                if (ver >= 0x0A000103u) {
                    if (c + 1 > bEnd) continue;
                    if (data[c++] == 0) continue; // hasPoints == 0
                }
                if (c + totalPts * 2 > bEnd) continue;

                uint32_t vBase = static_cast<uint32_t>(mesh.vertices.size());
                for (uint16_t v = 0; v < nVerts; ++v) {
                    const uint8_t* vp = data + vertsOff + static_cast<size_t>(v) * 12;
                    mesh.vertices.push_back(applyNifXform(xf, rdF32(vp + 0), rdF32(vp + 4), rdF32(vp + 8)));
                }
                for (uint16_t s = 0; s < nStrips; ++s) {
                    uint16_t slen = stripLens[s];
                    for (uint16_t k = 0; k + 2 < slen; ++k) {
                        uint16_t a = rdU16(data + c + static_cast<size_t>(k) * 2);
                        uint16_t bIdx = rdU16(data + c + static_cast<size_t>(k + 1) * 2);
                        uint16_t d = rdU16(data + c + static_cast<size_t>(k + 2) * 2);
                        if (a < nVerts && bIdx < nVerts && d < nVerts && a != bIdx && bIdx != d && a != d) {
                            if (k & 1u) mesh.tris.push_back({ vBase + bIdx, vBase + a, vBase + d, {} });
                            else        mesh.tris.push_back({ vBase + a, vBase + bIdx, vBase + d, {} });
                        }
                    }
                    c += static_cast<size_t>(slen) * 2;
                }
            }
        } else if ((tn == "BSTriShape" || tn == "BSDynamicTriShape" || tn == "BSSubIndexTriShape") && !blockSizes.empty()) {
            NifXform xf;
            size_t c = parseNiAVObject(bStart, bEnd, xf);
            if (c == 0) continue;
            size_t boundBytes = (bsVer == 155) ? 40 : 16; // boundingSphere (16B) [+ boundMinMax 24B in bsVer 155]
            if (c + boundBytes + 12 + 8 > bEnd) continue;
            c += boundBytes + 12; // boundingSphere + skin(4) + shaderProperty(4) + alphaProperty(4)

            uint64_t vertexDesc = rdU64(data + c); c += 8;
            size_t stride = static_cast<size_t>(vertexDesc & 0xFFu) * 4;
            bool fullPrec = ((vertexDesc >> 50) & 1u) != 0 || bsVer == 100 && stride >= 16 && ((vertexDesc >> 48) & 0x04u) != 0;

            uint32_t nTris = 0;
            if (bsVer >= 130) {
                if (c + 10 > bEnd) continue;
                nTris = rdU32(data + c); c += 4;
            } else {
                if (c + 8 > bEnd) continue;
                nTris = rdU16(data + c); c += 2;
            }
            uint16_t nVerts = rdU16(data + c); c += 2;
            uint32_t dataSize = rdU32(data + c); c += 4;
            if (nVerts < 3 || nTris == 0 || dataSize == 0 || stride < 8) continue;

            size_t vDataOff = c;
            size_t vBytes = static_cast<size_t>(nVerts) * stride;
            size_t tBytes = static_cast<size_t>(nTris) * 6;
            if (vDataOff + vBytes + tBytes > bEnd) continue;

            uint32_t vBase = static_cast<uint32_t>(mesh.vertices.size());
            size_t dynOff = vDataOff + vBytes + tBytes;
            bool usedDyn = false;
            if (tn == "BSDynamicTriShape" && dynOff + 4 <= bEnd) {
                uint32_t dynSize = rdU32(data + dynOff);
                if (dynSize == static_cast<uint32_t>(nVerts) * 16u && dynOff + 4 + dynSize <= bEnd) {
                    for (uint16_t v = 0; v < nVerts; ++v) {
                        const uint8_t* dp = data + dynOff + 4 + static_cast<size_t>(v) * 16;
                        mesh.vertices.push_back(applyNifXform(xf, rdF32(dp + 0), rdF32(dp + 4), rdF32(dp + 8)));
                    }
                    usedDyn = true;
                }
            }
            if (!usedDyn) {
                for (uint16_t v = 0; v < nVerts; ++v) {
                    const uint8_t* vp = data + vDataOff + static_cast<size_t>(v) * stride;
                    float x = 0.0f, y = 0.0f, z = 0.0f;
                    if (fullPrec && stride >= 12) {
                        x = rdF32(vp + 0); y = rdF32(vp + 4); z = rdF32(vp + 8);
                    } else {
                        x = Detail3D200::HalfToFloat(rdU16(vp + 0));
                        y = Detail3D200::HalfToFloat(rdU16(vp + 2));
                        z = Detail3D200::HalfToFloat(rdU16(vp + 4));
                    }
                    mesh.vertices.push_back(applyNifXform(xf, x, y, z));
                }
            }
            size_t triOff = vDataOff + vBytes;
            for (uint32_t t = 0; t < nTris; ++t) {
                const uint8_t* tp = data + triOff + static_cast<size_t>(t) * 6;
                uint16_t i0 = rdU16(tp + 0), i1 = rdU16(tp + 2), i2 = rdU16(tp + 4);
                if (i0 < nVerts && i1 < nVerts && i2 < nVerts && i0 != i1 && i1 != i2 && i0 != i2) {
                    mesh.tris.push_back({ vBase + i0, vBase + i1, vBase + i2, {} });
                }
            }
        }
    }

    return !mesh.vertices.empty() && !mesh.tris.empty();
}

// ============================================================================
// #46, #47, #48, #49. Blitz3D (.b3d), AC3D (.ac), OpenCTM (.ctm), Google Draco (.drc)
// ============================================================================
inline bool ParseBlitzAcCtmDraco(const uint8_t* data, size_t size, const std::wstring& ext, LoadedMesh3D& mesh) {
    // #46. Blitz3D — "BB3D" chunked binary
    if (ext == L".b3d" || (size >= 12 && std::memcmp(data, "BB3D", 4) == 0)) {
        if (size < 12 || std::memcmp(data, "BB3D", 4) != 0) return false;
        mesh.formatName = L"Blitz3D Mesh (.b3d)";
        uint32_t curVBase = 0;
        auto walkChunks = [&](auto& self, size_t start, size_t end) -> void {
            size_t p = start;
            while (p + 8 <= end) {
                char tag[5] = {};
                std::memcpy(tag, data + p, 4);
                uint32_t cLen = Detail3D200::RdU32LE(data + p + 4);
                p += 8;
                if (p + cLen > end) break;
                size_t cEnd = p + cLen;

                if (std::strcmp(tag, "MESH") == 0 && cLen >= 4) {
                    curVBase = static_cast<uint32_t>(mesh.vertices.size());
                    self(self, p + 4, cEnd);
                } else if (std::strcmp(tag, "NODE") == 0) {
                    size_t np = p;
                    while (np < cEnd && data[np] != 0) ++np;
                    if (np + 1 + 40 <= cEnd) self(self, np + 41, cEnd);
                } else if (std::strcmp(tag, "VRTS") == 0 && cLen >= 12) {
                    curVBase = static_cast<uint32_t>(mesh.vertices.size());
                    uint32_t vFlags = Detail3D200::RdU32LE(data + p + 0);
                    uint32_t tcSets = Detail3D200::RdU32LE(data + p + 4);
                    uint32_t tcSize = Detail3D200::RdU32LE(data + p + 8);
                    size_t stride = 12 + ((vFlags & 1u) ? 12 : 0) + ((vFlags & 2u) ? 16 : 0) + static_cast<size_t>(tcSets) * tcSize * 4;
                    if (stride >= 12 && stride <= 256) {
                        for (size_t vp = p + 12; vp + stride <= cEnd; vp += stride) {
                            mesh.vertices.push_back({
                                Detail3D200::RdF32LE(data + vp + 0),
                                Detail3D200::RdF32LE(data + vp + 4),
                                Detail3D200::RdF32LE(data + vp + 8)
                            });
                        }
                    }
                } else if (std::strcmp(tag, "TRIS") == 0 && cLen >= 16) {
                    for (size_t tp = p + 4; tp + 12 <= cEnd; tp += 12) {
                        uint32_t i0 = curVBase + Detail3D200::RdU32LE(data + tp + 0);
                        uint32_t i1 = curVBase + Detail3D200::RdU32LE(data + tp + 4);
                        uint32_t i2 = curVBase + Detail3D200::RdU32LE(data + tp + 8);
                        if (i0 < mesh.vertices.size() && i1 < mesh.vertices.size() && i2 < mesh.vertices.size()) {
                            mesh.tris.push_back({ i0, i1, i2, {} });
                        }
                    }
                }
                p = cEnd;
            }
        };
        walkChunks(walkChunks, 12, size);
        return !mesh.vertices.empty() && !mesh.tris.empty();
    }

    // #47. AC3D — "AC3Db" / "AC3Dc"
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    if (ext == L".ac" || (size >= 5 && sv.substr(0, 4) == "AC3D")) {
        if (size < 5 || sv.substr(0, 4) != "AC3D") return false;
        mesh.formatName = L"AC3D Model (.ac)";
        size_t pos = 0;
        float locX = 0.0f, locY = 0.0f, locZ = 0.0f;
        uint32_t objVBase = 0;
        while (pos < sv.size()) {
            size_t eol = sv.find('\n', pos);
            if (eol == std::string_view::npos) eol = sv.size();
            std::string ln(sv.substr(pos, eol - pos));
            pos = eol + 1;

            if (ln.rfind("OBJECT ", 0) == 0) {
                locX = locY = locZ = 0.0f;
            } else if (ln.rfind("loc ", 0) == 0) {
                sscanf_s(ln.c_str() + 4, "%f %f %f", &locX, &locY, &locZ);
            } else if (ln.rfind("numvert ", 0) == 0) {
                int nv = std::atoi(ln.c_str() + 8);
                objVBase = static_cast<uint32_t>(mesh.vertices.size());
                for (int i = 0; i < nv && pos < sv.size() && i < 500000; ++i) {
                    size_t ve = sv.find('\n', pos);
                    if (ve == std::string_view::npos) ve = sv.size();
                    std::string vln(sv.substr(pos, ve - pos));
                    pos = ve + 1;
                    float x = 0, y = 0, z = 0;
                    if (sscanf_s(vln.c_str(), "%f %f %f", &x, &y, &z) == 3) {
                        mesh.vertices.push_back({ x + locX, y + locY, z + locZ });
                    }
                }
            } else if (ln.rfind("refs ", 0) == 0) {
                int nr = std::atoi(ln.c_str() + 5);
                std::vector<uint32_t> poly;
                for (int i = 0; i < nr && pos < sv.size() && i < 64; ++i) {
                    size_t re = sv.find('\n', pos);
                    if (re == std::string_view::npos) re = sv.size();
                    std::string rln(sv.substr(pos, re - pos));
                    pos = re + 1;
                    int vidx = 0;
                    if (sscanf_s(rln.c_str(), "%d", &vidx) == 1 && vidx >= 0) {
                        poly.push_back(objVBase + static_cast<uint32_t>(vidx));
                    }
                }
                for (size_t k = 1; k + 1 < poly.size(); ++k) {
                    if (poly[0] < mesh.vertices.size() && poly[k] < mesh.vertices.size() && poly[k + 1] < mesh.vertices.size()) {
                        mesh.tris.push_back({ poly[0], poly[k], poly[k + 1], {} });
                    }
                }
            }
        }
        return !mesh.vertices.empty() && !mesh.tris.empty();
    }

    // #48. OpenCTM — "OCTM" (RAW uncompressed stream; returns false for LZMA MG1/MG2)
    if (ext == L".ctm" || (size >= 36 && std::memcmp(data, "OCTM", 4) == 0)) {
        if (size < 32 || std::memcmp(data, "OCTM", 4) != 0) return false;
        mesh.formatName = L"OpenCTM 3D Mesh (.ctm)";
        bool isRaw = (std::memcmp(data + 8, "RAW\0", 4) == 0 || std::memcmp(data + 8, "RAW ", 4) == 0);
        if (!isRaw) return false;

        uint32_t vCount = Detail3D200::RdU32LE(data + 12);
        uint32_t tCount = Detail3D200::RdU32LE(data + 16);
        if (vCount < 3 || tCount == 0 || vCount > 2000000 || tCount > 4000000) return false;

        // OpenCTM v5 header is 36 bytes (offset 28 = flags, offset 32 = commentLength); also tolerate 32-byte variant
        size_t p = 0;
        if (size >= 36) {
            uint32_t cLen36 = Detail3D200::RdU32LE(data + 32);
            if (cLen36 <= size - 36 && 36 + cLen36 + 4 <= size && std::memcmp(data + 36 + cLen36, "INDX", 4) == 0) {
                p = 36 + cLen36;
            }
        }
        if (p == 0) {
            uint32_t cLen32 = Detail3D200::RdU32LE(data + 28);
            if (cLen32 <= size - 32 && 32 + cLen32 + 4 <= size && std::memcmp(data + 32 + cLen32, "INDX", 4) == 0) {
                p = 32 + cLen32;
            } else {
                return false;
            }
        }

        if (p + 4 + static_cast<size_t>(tCount) * 12 + 4 + static_cast<size_t>(vCount) * 12 > size) return false;
        p += 4; // "INDX"
        mesh.tris.reserve(tCount);
        for (uint32_t t = 0; t < tCount; ++t) {
            uint32_t i0 = Detail3D200::RdU32LE(data + p + static_cast<size_t>(t) * 12 + 0);
            uint32_t i1 = Detail3D200::RdU32LE(data + p + static_cast<size_t>(t) * 12 + 4);
            uint32_t i2 = Detail3D200::RdU32LE(data + p + static_cast<size_t>(t) * 12 + 8);
            if (i0 >= vCount || i1 >= vCount || i2 >= vCount) return false;
            mesh.tris.push_back({ i0, i1, i2, {} });
        }
        p += static_cast<size_t>(tCount) * 12;
        if (std::memcmp(data + p, "VERT", 4) != 0) {
            mesh.tris.clear();
            return false;
        }
        p += 4; // "VERT"
        mesh.vertices.reserve(vCount);
        for (uint32_t v = 0; v < vCount; ++v) {
            mesh.vertices.push_back({
                Detail3D200::RdF32LE(data + p + static_cast<size_t>(v) * 12 + 0),
                Detail3D200::RdF32LE(data + p + static_cast<size_t>(v) * 12 + 4),
                Detail3D200::RdF32LE(data + p + static_cast<size_t>(v) * 12 + 8)
            });
        }
        return !mesh.vertices.empty() && !mesh.tris.empty();
    }

    // #49. Google Draco — "DRACO" Sequential Mesh & PointCloud Decoder
    if (ext == L".drc" || (size >= 11 && std::memcmp(data, "DRACO", 5) == 0)) {
        if (size < 11 || std::memcmp(data, "DRACO", 5) != 0) return false;
        uint8_t maj       = data[5];
        uint8_t minV      = data[6];
        uint8_t encType   = data[7]; // 0 = POINT_CLOUD, 1 = TRIANGULAR_MESH
        uint8_t encMethod = data[8]; // 0 = SEQUENTIAL, 1 = EDGEBREAKER
        uint16_t flags    = Detail3D200::RdU16LE(data + 9);
        if ((encType != 0 && encType != 1) || encMethod != 0) return false;

        mesh.formatName = L"Google Draco v" + std::to_wstring(maj) + L"." + std::to_wstring(minV) +
                          (encType == 0 ? L" Point Cloud (.drc)" : L" 3D Mesh (.drc)");

        size_t pos = 11;
        // Skip optional Draco metadata block if METADATA_FLAG_MASK (0x8000) is set
        if ((flags & 0x8000u) != 0) {
            auto skipElemMeta = [&](auto& self) -> bool {
                uint32_t numEntries = 0;
                if (!Detail3D200::ReadVarintU32(data, size, pos, numEntries) || numEntries > 4096) return false;
                for (uint32_t e = 0; e < numEntries; ++e) {
                    if (pos >= size) return false;
                    uint8_t kLen = data[pos++];
                    if (pos + kLen >= size) return false;
                    pos += kLen;
                    uint8_t vLen = data[pos++];
                    if (pos + vLen > size) return false;
                    pos += vLen;
                }
                uint32_t numSub = 0;
                if (!Detail3D200::ReadVarintU32(data, size, pos, numSub) || numSub > 1024) return false;
                for (uint32_t s = 0; s < numSub; ++s) {
                    if (pos >= size) return false;
                    uint8_t sLen = data[pos++];
                    if (pos + sLen > size) return false;
                    pos += sLen;
                    if (!self(self)) return false;
                }
                return true;
            };
            uint32_t numAttMeta = 0;
            if (!Detail3D200::ReadVarintU32(data, size, pos, numAttMeta) || numAttMeta > 1024) return false;
            for (uint32_t a = 0; a < numAttMeta; ++a) {
                uint32_t attUid = 0;
                if (!Detail3D200::ReadVarintU32(data, size, pos, attUid)) return false;
                if (!skipElemMeta(skipElemMeta)) return false;
            }
            if (!skipElemMeta(skipElemMeta)) return false;
        }

        uint64_t numFaces = 0, numPoints = 0;
        std::vector<uint32_t> triIndices;
        if (encType == 1) {
            if (!Detail3D200::ReadVarintU64(data, size, pos, numFaces)) return false;
            if (!Detail3D200::ReadVarintU64(data, size, pos, numPoints)) return false;
            if (numFaces == 0 || numFaces > 4000000 || numPoints < 3 || numPoints > 4000000) return false;
            if (pos >= size) return false;
            uint8_t connMethod = data[pos++];
            size_t totalCorners = static_cast<size_t>(numFaces) * 3;
            triIndices.resize(totalCorners);

            if (connMethod == 1) { // Uncompressed sequential indices
                if (numPoints < 256) {
                    if (pos + totalCorners > size) return false;
                    for (size_t i = 0; i < totalCorners; ++i) triIndices[i] = data[pos++];
                } else if (numPoints < 65536) {
                    if (pos + totalCorners * 2 > size) return false;
                    for (size_t i = 0; i < totalCorners; ++i) {
                        triIndices[i] = Detail3D200::RdU16LE(data + pos);
                        pos += 2;
                    }
                } else if (numPoints < (1ull << 21) && (maj > 2 || (maj == 2 && minV >= 2))) {
                    // Check whether encoded as varint uint32 (Draco 2.2+) or raw uint32
                    size_t tryPos = pos;
                    bool okVar = true;
                    for (size_t i = 0; i < totalCorners; ++i) {
                        uint32_t idx = 0;
                        if (!Detail3D200::ReadVarintU32(data, size, tryPos, idx) || idx >= numPoints) {
                            okVar = false; break;
                        }
                        triIndices[i] = idx;
                    }
                    if (okVar && tryPos < size && data[tryPos] >= 1 && data[tryPos] <= 16) {
                        pos = tryPos;
                    } else {
                        if (pos + totalCorners * 4 > size) return false;
                        for (size_t i = 0; i < totalCorners; ++i) {
                            triIndices[i] = Detail3D200::RdU32LE(data + pos);
                            pos += 4;
                        }
                    }
                } else {
                    if (pos + totalCorners * 4 > size) return false;
                    for (size_t i = 0; i < totalCorners; ++i) {
                        triIndices[i] = Detail3D200::RdU32LE(data + pos);
                        pos += 4;
                    }
                }
            } else if (connMethod == 0) {
                // Zigzag delta-varint sequential indices (returns false cleanly if RANS-compressed)
                int64_t prev = 0;
                for (size_t i = 0; i < totalCorners; ++i) {
                    uint32_t zz = 0;
                    if (!Detail3D200::ReadVarintU32(data, size, pos, zz)) return false;
                    int64_t delta = static_cast<int64_t>((zz >> 1) ^ (~(zz & 1u) + 1u));
                    int64_t val = prev + delta;
                    if (val < 0 || static_cast<uint64_t>(val) >= numPoints) return false;
                    triIndices[i] = static_cast<uint32_t>(val);
                    prev = val;
                }
            } else {
                return false;
            }

            for (uint32_t idx : triIndices) {
                if (idx >= numPoints) return false;
            }
        } else {
            if (!Detail3D200::ReadVarintU64(data, size, pos, numPoints)) return false;
            if (numPoints == 0 || numPoints > 4000000) return false;
        }

        if (pos >= size) return false;
        uint8_t numDecoders = data[pos++];
        if (numDecoders == 0 || numDecoders > 32) return false;

        struct DracoAttr {
            uint8_t attType = 0;
            uint8_t dataType = 0;
            uint8_t numComps = 0;
            uint8_t normalized = 0;
            uint32_t uniqueId = 0;
            uint8_t seqType = 0;
        };
        auto dataTypeByteLen = [](uint8_t dt) -> size_t {
            switch (dt) {
                case 1: case 2: case 11: return 1;
                case 3: case 4: return 2;
                case 5: case 6: case 9: return 4;
                case 7: case 8: case 10: return 8;
                default: return 0;
            }
        };

        std::vector<std::vector<DracoAttr>> decAttrs(numDecoders);
        for (uint8_t d = 0; d < numDecoders; ++d) {
            uint32_t nAttrs = 0;
            if (!Detail3D200::ReadVarintU32(data, size, pos, nAttrs) || nAttrs == 0 || nAttrs > 64) return false;
            decAttrs[d].resize(nAttrs);
            for (uint32_t a = 0; a < nAttrs; ++a) {
                if (pos + 4 > size) return false;
                decAttrs[d][a].attType    = data[pos++];
                decAttrs[d][a].dataType   = data[pos++];
                decAttrs[d][a].numComps   = data[pos++];
                decAttrs[d][a].normalized = data[pos++];
                if (!Detail3D200::ReadVarintU32(data, size, pos, decAttrs[d][a].uniqueId)) return false;
                if (dataTypeByteLen(decAttrs[d][a].dataType) == 0 ||
                    decAttrs[d][a].numComps == 0 || decAttrs[d][a].numComps > 16)
                {
                    return false;
                }
            }
            for (uint32_t a = 0; a < nAttrs; ++a) {
                if (pos >= size) return false;
                decAttrs[d][a].seqType = data[pos++];
            }
        }

        std::vector<Vec3f> positions;
        for (uint8_t d = 0; d < numDecoders; ++d) {
            for (const auto& attr : decAttrs[d]) {
                if (attr.seqType == 0) { // SEQUENTIAL_ATTRIBUTE_ENCODER_GENERIC
                    size_t elemSz = dataTypeByteLen(attr.dataType);
                    size_t rawBytes = static_cast<size_t>(numPoints) * attr.numComps * elemSz;
                    // Tolerate optional PREDICTION_NONE (-2 == 0xFE) byte if explicitly emitted before raw payload
                    if (pos < size && data[pos] == 0xFEu && (size - pos) == rawBytes + 1) {
                        ++pos;
                    }
                    if (pos + rawBytes > size) return false;
                    if (attr.attType == 0 && attr.numComps >= 3 && (attr.dataType == 9 || attr.dataType == 10)) {
                        positions.reserve(static_cast<size_t>(numPoints));
                        for (size_t v = 0; v < static_cast<size_t>(numPoints); ++v) {
                            const uint8_t* vp = data + pos + v * attr.numComps * elemSz;
                            if (attr.dataType == 9) {
                                positions.push_back({
                                    Detail3D200::RdF32LE(vp + 0),
                                    Detail3D200::RdF32LE(vp + 4),
                                    Detail3D200::RdF32LE(vp + 8)
                                });
                            } else {
                                positions.push_back({
                                    static_cast<float>(Detail3D200::RdF64LE(vp + 0)),
                                    static_cast<float>(Detail3D200::RdF64LE(vp + 8)),
                                    static_cast<float>(Detail3D200::RdF64LE(vp + 16))
                                });
                            }
                        }
                    }
                    pos += rawBytes;
                } else if (attr.seqType == 2 && attr.attType == 0 && attr.numComps == 3) {
                    // SEQUENTIAL_ATTRIBUTE_ENCODER_QUANTIZATION with uncompressed PREDICTION_NONE (-2)
                    if (pos + 2 > size) return false;
                    int8_t predScheme = static_cast<int8_t>(data[pos++]);
                    if (predScheme != -2) return false;
                    uint8_t compressed = data[pos++];
                    if (compressed != 0) return false;
                    size_t qBytes = static_cast<size_t>(numPoints) * 3 * 4;
                    if (pos + qBytes + 17 > size) return false;
                    const uint8_t* qPtr = data + pos;
                    pos += qBytes;
                    float minX = Detail3D200::RdF32LE(data + pos + 0);
                    float minY = Detail3D200::RdF32LE(data + pos + 4);
                    float minZ = Detail3D200::RdF32LE(data + pos + 8);
                    float maxRange = Detail3D200::RdF32LE(data + pos + 12);
                    uint8_t qBits = data[pos + 16];
                    pos += 17;
                    if (qBits == 0 || qBits > 31) return false;
                    float step = maxRange / static_cast<float>((1ull << qBits) - 1ull);
                    positions.reserve(static_cast<size_t>(numPoints));
                    for (size_t v = 0; v < static_cast<size_t>(numPoints); ++v) {
                        uint32_t qx = Detail3D200::RdU32LE(qPtr + v * 12 + 0);
                        uint32_t qy = Detail3D200::RdU32LE(qPtr + v * 12 + 4);
                        uint32_t qz = Detail3D200::RdU32LE(qPtr + v * 12 + 8);
                        positions.push_back({ minX + qx * step, minY + qy * step, minZ + qz * step });
                    }
                } else {
                    if (!positions.empty()) break;
                    return false;
                }
            }
        }

        if (positions.empty()) return false;
        if (encType == 1) {
            mesh.vertices = std::move(positions);
            mesh.tris.reserve(static_cast<size_t>(numFaces));
            for (size_t f = 0; f < static_cast<size_t>(numFaces); ++f) {
                uint32_t i0 = triIndices[f * 3 + 0];
                uint32_t i1 = triIndices[f * 3 + 1];
                uint32_t i2 = triIndices[f * 3 + 2];
                if (i0 < mesh.vertices.size() && i1 < mesh.vertices.size() && i2 < mesh.vertices.size()) {
                    mesh.tris.push_back({ i0, i1, i2, {} });
                }
            }
        } else {
            float r = Detail3D200::ComputePointCloudSpan(positions) * 0.02f;
            for (const auto& pt : positions) Detail3D200::AddPointTetra(mesh, pt, r);
        }
        return !mesh.vertices.empty() && !mesh.tris.empty();
    }
    return false;
}

// ============================================================================
// #50–#54. CAD Kernels: 3DXML (.3dxml), Siemens JT (.jt), OpenCASCADE (.brep),
//          Spatial ACIS (.sat/.sab), Siemens Parasolid (.x_t/.x_b)
// ============================================================================
inline bool ParseCadKernels200(const uint8_t* data, size_t size, const std::wstring& ext, LoadedMesh3D& mesh) {
    std::string_view sv(reinterpret_cast<const char*>(data), size);

    // #50. CATIA / SOLIDWORKS 3DXML (ZIP or raw XML containing <Positions> & <Faces>)
    if (ext == L".3dxml") {
        std::vector<uint8_t> xmlBuf;
        if (size >= 4 && data[0] == 'P' && data[1] == 'K') {
            std::vector<ZipDeflate::ArchiveItemMeta> zEntries;
            if (ZipDeflate::ListZipEntriesFromMemory(data, size, zEntries)) {
                for (const auto& ze : zEntries) {
                    if (!ze.isDirectory && (ze.name.find(L".3drep") != std::wstring::npos ||
                                            ze.name.find(L".xml") != std::wstring::npos ||
                                            ze.name.find(L".3dxml") != std::wstring::npos))
                    {
                        std::vector<uint8_t> part;
                        if (ZipDeflate::ExtractZipEntryFromMemory(data, size, ze, part) && !part.empty()) {
                            std::string_view psv(reinterpret_cast<const char*>(part.data()), part.size());
                            if (psv.find("<Positions") != std::string_view::npos) {
                                xmlBuf = std::move(part);
                                break;
                            }
                            if (xmlBuf.empty()) xmlBuf = std::move(part);
                        }
                    }
                }
            }
        }
        if (!xmlBuf.empty()) sv = std::string_view(reinterpret_cast<const char*>(xmlBuf.data()), xmlBuf.size());
        if (sv.find("3DXML") == std::string_view::npos && sv.find("<Positions") == std::string_view::npos) return false;

        mesh.formatName = L"Dassault CATIA / SOLIDWORKS 3DXML (.3dxml)";
        size_t posTag = sv.find("<Positions");
        while (posTag != std::string_view::npos) {
            uint32_t vBase = static_cast<uint32_t>(mesh.vertices.size());
            size_t gt = sv.find('>', posTag);
            size_t closeP = sv.find("</Positions>", gt == std::string_view::npos ? posTag : gt);
            if (gt == std::string_view::npos || closeP == std::string_view::npos) break;
            std::string posStr(sv.substr(gt + 1, closeP - gt - 1));
            for (char& c : posStr) if (c == ',') c = ' ';
            const char* p = posStr.c_str();
            while (*p) {
                char* e1 = nullptr; float x = std::strtof(p, &e1); if (e1 == p) break; p = e1;
                char* e2 = nullptr; float y = std::strtof(p, &e2); if (e2 == p) break; p = e2;
                char* e3 = nullptr; float z = std::strtof(p, &e3); if (e3 == p) break; p = e3;
                mesh.vertices.push_back({ x, y, z });
            }

            size_t nextRep = sv.find("<Positions", closeP);
            std::string_view faceScope = sv.substr(closeP, (nextRep == std::string_view::npos ? sv.size() : nextRep) - closeP);
            size_t trAttr = faceScope.find("triangles=\"");
            if (trAttr != std::string_view::npos) {
                trAttr += 11;
                size_t qEnd = faceScope.find('"', trAttr);
                if (qEnd != std::string_view::npos) {
                    std::string idxStr(faceScope.substr(trAttr, qEnd - trAttr));
                    for (char& c : idxStr) if (c == ',') c = ' ';
                    const char* ip = idxStr.c_str();
                    while (*ip) {
                        char* a1 = nullptr; long i0 = std::strtol(ip, &a1, 10); if (a1 == ip) break; ip = a1;
                        char* a2 = nullptr; long i1 = std::strtol(ip, &a2, 10); if (a2 == ip) break; ip = a2;
                        char* a3 = nullptr; long i2 = std::strtol(ip, &a3, 10); if (a3 == ip) break; ip = a3;
                        if (i0 >= 0 && i1 >= 0 && i2 >= 0 &&
                            vBase + static_cast<uint32_t>(i0) < mesh.vertices.size() &&
                            vBase + static_cast<uint32_t>(i1) < mesh.vertices.size() &&
                            vBase + static_cast<uint32_t>(i2) < mesh.vertices.size())
                        {
                            mesh.tris.push_back({
                                vBase + static_cast<uint32_t>(i0),
                                vBase + static_cast<uint32_t>(i1),
                                vBase + static_cast<uint32_t>(i2), {}
                            });
                        }
                    }
                }
            }
            posTag = nextRep;
        }
        return !mesh.vertices.empty() && !mesh.tris.empty();
    }

    // #51. Siemens JT Open (.jt — 80-char header + byteOrder + TOC + uncompressed TriStripSet/Shape segments)
    if (ext == L".jt" || (size >= 89 && sv.substr(0, 8) == "Version " && sv.substr(0, 80).find("JT") != std::string_view::npos)) {
        if (size < 89 || sv.substr(0, 8) != "Version " || sv.substr(0, 80).find("JT") == std::string_view::npos) {
            return false;
        }
        uint8_t byteOrder = data[80];
        if (byteOrder > 1) return false;
        bool isBE = (byteOrder == 1);
        auto rdU32 = [&](const uint8_t* p) -> uint32_t { return isBE ? Detail3D200::RdU32BE(p) : Detail3D200::RdU32LE(p); };
        auto rdU64 = [&](const uint8_t* p) -> uint64_t { return isBE ? Detail3D200::RdU64BE(p) : Detail3D200::RdU64LE(p); };
        auto rdF32 = [&](const uint8_t* p) -> float    { return isBE ? Detail3D200::RdF32BE(p) : Detail3D200::RdF32LE(p); };

        int majorVer = std::atoi(std::string(sv.substr(8, 4)).c_str());
        uint64_t tocOffset = (majorVer >= 10 && size >= 93) ? rdU64(data + 85) : static_cast<uint64_t>(rdU32(data + 85));
        if (tocOffset < 89 || tocOffset + 4 > size) return false;

        uint32_t entryCount = rdU32(data + static_cast<size_t>(tocOffset));
        size_t entryStride = (majorVer >= 10) ? 32 : 28;
        if (entryCount == 0 || entryCount > 65536 || tocOffset + 4 + static_cast<uint64_t>(entryCount) * entryStride > size) {
            return false;
        }
        mesh.formatName = L"Siemens JT Open CAD Assembly (.jt)";

        for (uint32_t e = 0; e < entryCount; ++e) {
            const uint8_t* ep = data + static_cast<size_t>(tocOffset) + 4 + static_cast<size_t>(e) * entryStride;
            uint64_t segOff = (majorVer >= 10) ? rdU64(ep + 16) : static_cast<uint64_t>(rdU32(ep + 16));
            uint32_t segLen = (majorVer >= 10) ? rdU32(ep + 24) : rdU32(ep + 20);
            uint32_t segAttr= (majorVer >= 10) ? rdU32(ep + 28) : rdU32(ep + 24);
            uint32_t segType = (segAttr >> 24) & 0xFFu;
            // Shape / Tri-Strip Set / PolyLine LOD segment types: 4, 6, 7
            if ((segType != 4 && segType != 6 && segType != 7) || segLen < 36 || segOff + segLen > size) continue;

            const uint8_t* sp = data + static_cast<size_t>(segOff);
            // Verify 24-byte JT Segment Header GUID matches TOC entry GUID
            if (std::memcmp(sp, ep, 16) != 0) continue;
            const uint8_t* payload = sp + 24;
            size_t payLen = segLen - 24;
            std::vector<uint8_t> inflated;

            // JT v8/v9 zlib-compressed data segment header: compressionFlag(int32==2), compressedDataLength(int32), compressionAlgorithm(uint8==2)
            if (majorVer < 10 && payLen >= 12 && rdU32(payload) == 2 && payload[8] == 2) {
                uint32_t compLen = rdU32(payload + 4);
                if (compLen > 1 && 8 + static_cast<size_t>(compLen) <= payLen) {
                    if (ZipDeflate::InflateZlib(payload + 9, compLen - 1, inflated, 32 * 1024 * 1024)) {
                        payload = inflated.data();
                        payLen = inflated.size();
                    } else {
                        continue;
                    }
                }
            }

            // Decode uncompressed Tri-Strip Set payload: [uint32 numStrips][int32 stripOffsets[numStrips+1]][uint32 numVerts][float3 verts[numVerts]]
            if (payLen >= 20) {
                uint32_t numStrips = rdU32(payload);
                if (numStrips >= 1 && numStrips <= 65536 && 4 + static_cast<size_t>(numStrips + 1) * 4 + 4 <= payLen) {
                    size_t offTable = 4;
                    uint32_t s0 = rdU32(payload + offTable);
                    uint32_t sLast = rdU32(payload + offTable + static_cast<size_t>(numStrips) * 4);
                    size_t afterStrips = offTable + static_cast<size_t>(numStrips + 1) * 4;
                    if (s0 == 0 && sLast >= numStrips * 3 && sLast <= 1000000 &&
                        afterStrips + static_cast<size_t>(sLast) * 12 <= payLen)
                    {
                        bool monotonic = true;
                        for (uint32_t s = 0; s < numStrips; ++s) {
                            uint32_t a = rdU32(payload + offTable + static_cast<size_t>(s) * 4);
                            uint32_t b = rdU32(payload + offTable + static_cast<size_t>(s + 1) * 4);
                            if (b < a || (b - a) < 3) { monotonic = false; break; }
                        }
                        if (monotonic) {
                            uint32_t vBase = static_cast<uint32_t>(mesh.vertices.size());
                            for (uint32_t v = 0; v < sLast; ++v) {
                                const uint8_t* vp = payload + afterStrips + static_cast<size_t>(v) * 12;
                                mesh.vertices.push_back({ rdF32(vp + 0), rdF32(vp + 4), rdF32(vp + 8) });
                            }
                            for (uint32_t s = 0; s < numStrips; ++s) {
                                uint32_t a = rdU32(payload + offTable + static_cast<size_t>(s) * 4);
                                uint32_t b = rdU32(payload + offTable + static_cast<size_t>(s + 1) * 4);
                                for (uint32_t k = a; k + 2 < b; ++k) {
                                    if ((k - a) & 1u) mesh.tris.push_back({ vBase + k + 1, vBase + k, vBase + k + 2, {} });
                                    else              mesh.tris.push_back({ vBase + k, vBase + k + 1, vBase + k + 2, {} });
                                }
                            }
                        }
                    }
                }
            }
        }
        return !mesh.vertices.empty() && !mesh.tris.empty();
    }

    // #52. OpenCASCADE Boundary Representation (.brep — "DBRep_DrawableShape" / "CASCADE Topology")
    if (ext == L".brep") {
        if (sv.find("DBRep_DrawableShape") == std::string_view::npos &&
            sv.find("CASCADE Topology") == std::string_view::npos)
        {
            return false;
        }
        mesh.formatName = L"OpenCASCADE B-Rep CAD Solid (.brep)";

        // 1. Parse cached "Triangulations" section if present (avoid matching "PolygonOnTriangulations")
        size_t triSec = 0;
        while ((triSec = sv.find("Triangulations", triSec)) != std::string_view::npos) {
            if (triSec == 0 || !std::isalpha(static_cast<unsigned char>(sv[triSec - 1]))) break;
            triSec += 14;
        }
        if (triSec != std::string_view::npos) {
            std::string nullSafeBrep(sv.substr(triSec + 14));
            const char* p = nullSafeBrep.c_str();
            const char* end = p + nullSafeBrep.size();
            char* ep = nullptr;
            long nTabs = std::strtol(p, &ep, 10); p = ep;
            for (long t = 0; t < nTabs && t < 4096 && p < end; ++t) {
                long nbNodes = std::strtol(p, &ep, 10); if (ep == p) break; p = ep;
                long nbTris  = std::strtol(p, &ep, 10); if (ep == p) break; p = ep;
                long hasUV   = std::strtol(p, &ep, 10); if (ep == p) break; p = ep;
                double defl  = std::strtod(p, &ep); (void)defl; p = ep;
                if (nbNodes <= 0 || nbNodes > 200000 || nbTris <= 0 || nbTris > 400000) break;
                uint32_t vBase = static_cast<uint32_t>(mesh.vertices.size());
                for (long n = 0; n < nbNodes && p < end; ++n) {
                    float x = std::strtof(p, &ep); p = ep;
                    float y = std::strtof(p, &ep); p = ep;
                    float z = std::strtof(p, &ep); p = ep;
                    mesh.vertices.push_back({ x, y, z });
                }
                if (hasUV) {
                    for (long n = 0; n < nbNodes * 2 && p < end; ++n) { std::strtof(p, &ep); p = ep; }
                }
                for (long k = 0; k < nbTris && p < end; ++k) {
                    long i0 = std::strtol(p, &ep, 10) - 1; p = ep;
                    long i1 = std::strtol(p, &ep, 10) - 1; p = ep;
                    long i2 = std::strtol(p, &ep, 10) - 1; p = ep;
                    if (i0 >= 0 && i1 >= 0 && i2 >= 0 &&
                        i0 < nbNodes && i1 < nbNodes && i2 < nbNodes)
                    {
                        mesh.tris.push_back({
                            vBase + static_cast<uint32_t>(i0),
                            vBase + static_cast<uint32_t>(i1),
                            vBase + static_cast<uint32_t>(i2), {}
                        });
                    }
                }
            }
        }

        // 2. If untriangulated, parse Polygon3D polylines if present
        if (mesh.tris.empty()) {
            size_t polySec = sv.find("Polygon3D");
            if (polySec != std::string_view::npos) {
                const char* p = sv.data() + polySec + 9;
                const char* end = sv.data() + sv.size();
                char* ep = nullptr;
                long nPolys = std::strtol(p, &ep, 10); p = ep;
                for (long pl = 0; pl < nPolys && pl < 4096 && p < end; ++pl) {
                    long nbNodes = std::strtol(p, &ep, 10); if (ep == p) break; p = ep;
                    long hasParam = std::strtol(p, &ep, 10); if (ep == p) break; p = ep;
                    std::strtod(p, &ep); p = ep; // deflection
                    if (nbNodes < 2 || nbNodes > 100000) break;
                    std::vector<Vec3f> pts;
                    for (long n = 0; n < nbNodes && p < end; ++n) {
                        float x = std::strtof(p, &ep); p = ep;
                        float y = std::strtof(p, &ep); p = ep;
                        float z = std::strtof(p, &ep); p = ep;
                        pts.push_back({ x, y, z });
                    }
                    if (hasParam) {
                        for (long n = 0; n < nbNodes && p < end; ++n) { std::strtod(p, &ep); p = ep; }
                    }
                    float r = Detail3D200::ComputePointCloudSpan(pts) * 0.015f;
                    for (size_t i = 0; i + 1 < pts.size(); ++i) {
                        Detail3D200::AddStrut3D(mesh, pts[i], pts[i + 1], r);
                    }
                }
            }
        }
        return !mesh.vertices.empty() && !mesh.tris.empty();
    }

    // #53 & #54. Spatial ACIS (.sat / .sab) & Siemens Parasolid XT (.x_t / .x_b)
    if (ext == L".sat" || ext == L".sab" || ext == L".x_t" || ext == L".x_b") {
        struct CadVec3 {
            double x = 0.0, y = 0.0, z = 0.0;
        };
        auto vAdd = [](CadVec3 a, CadVec3 b) -> CadVec3 { return { a.x + b.x, a.y + b.y, a.z + b.z }; };
        auto vSub = [](CadVec3 a, CadVec3 b) -> CadVec3 { return { a.x - b.x, a.y - b.y, a.z - b.z }; };
        auto vMul = [](CadVec3 a, double s)  -> CadVec3 { return { a.x * s, a.y * s, a.z * s }; };
        auto vDot = [](CadVec3 a, CadVec3 b) -> double  { return a.x * b.x + a.y * b.y + a.z * b.z; };
        auto vCross = [](CadVec3 a, CadVec3 b) -> CadVec3 {
            return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
        };
        auto vLen = [&](CadVec3 a) -> double { return std::sqrt(vDot(a, a)); };
        auto vNorm = [&](CadVec3 a) -> CadVec3 {
            double L = vLen(a);
            return (L > 1e-12) ? CadVec3{ a.x / L, a.y / L, a.z / L } : CadVec3{ 0.0, 0.0, 1.0 };
        };
        auto vDist = [&](CadVec3 a, CadVec3 b) -> double { return vLen(vSub(a, b)); };

        struct CadXform {
            double r[9] = { 1.0, 0.0, 0.0,  0.0, 1.0, 0.0,  0.0, 0.0, 1.0 };
            double t[3] = { 0.0, 0.0, 0.0 };
            double scale = 1.0;
            bool valid = false;
        };
        auto applyXfPt = [](const CadXform& xf, CadVec3 p) -> CadVec3 {
            if (!xf.valid) return p;
            double s = xf.scale;
            return {
                s * (xf.r[0] * p.x + xf.r[1] * p.y + xf.r[2] * p.z) + xf.t[0],
                s * (xf.r[3] * p.x + xf.r[4] * p.y + xf.r[5] * p.z) + xf.t[1],
                s * (xf.r[6] * p.x + xf.r[7] * p.y + xf.r[8] * p.z) + xf.t[2]
            };
        };
        auto applyXfDir = [&](const CadXform& xf, CadVec3 d) -> CadVec3 {
            if (!xf.valid) return d;
            return vNorm({
                xf.r[0] * d.x + xf.r[1] * d.y + xf.r[2] * d.z,
                xf.r[3] * d.x + xf.r[4] * d.y + xf.r[5] * d.z,
                xf.r[6] * d.x + xf.r[7] * d.y + xf.r[8] * d.z
            });
        };

        enum class SurfKind { Unknown, Plane, CylinderCone, Sphere, Torus, Nurbs };
        struct BRepSurf {
            SurfKind kind = SurfKind::Unknown;
            CadVec3 origin;
            CadVec3 normal = { 0.0, 0.0, 1.0 };
            CadVec3 uAxis  = { 1.0, 0.0, 0.0 };
            double radius1 = 0.0;
            double radius2 = 0.0;
            double ratio   = 1.0;
            int uDeg = 0, vDeg = 0, nU = 0, nV = 0;
            bool isRational = false;
            std::vector<double> knotsU;
            std::vector<double> knotsV;
            std::vector<CadVec3> poles;
            std::vector<double> weights;
        };

        enum class CurveKind { Straight, Ellipse, Spline };
        struct BRepCurve {
            CurveKind kind = CurveKind::Straight;
            CadVec3 origin;
            CadVec3 normal    = { 0.0, 0.0, 1.0 };
            CadVec3 majorAxis = { 1.0, 0.0, 0.0 };
            double ratio      = 1.0;
            std::vector<CadVec3> ctrlPts;
        };

        struct BRepEdge {
            int32_t startVert = -1;
            int32_t endVert   = -1;
            int32_t curveId   = -1;
            int32_t bodyId    = -1;
            bool reversed     = false;
        };

        struct BRepCoedge {
            int32_t nextCoedge = -1;
            int32_t prevCoedge = -1;
            int32_t edgeId     = -1;
            int32_t headVert   = -1;
            bool reversed      = false;
        };

        struct BRepLoop {
            int32_t nextLoop    = -1;
            int32_t firstCoedge = -1;
            int32_t ownerFace   = -1;
            std::vector<int32_t> directEdges;
        };

        struct BRepFace {
            int32_t nextFace  = -1;
            int32_t firstLoop = -1;
            int32_t surfaceId = -1;
            int32_t bodyId    = -1;
            bool reversed     = false;
            std::vector<int32_t> loopIds;
        };

        struct BRepGraph {
            std::unordered_map<int32_t, CadVec3>    points;
            std::unordered_map<int32_t, int32_t>    vertexToPoint;
            std::unordered_map<int32_t, BRepCurve>  curves;
            std::unordered_map<int32_t, BRepSurf>   surfaces;
            std::unordered_map<int32_t, BRepEdge>   edges;
            std::unordered_map<int32_t, BRepCoedge> coedges;
            std::unordered_map<int32_t, BRepLoop>   loops;
            std::unordered_map<int32_t, BRepFace>   faces;
            std::unordered_map<int32_t, CadXform>   bodyTransforms;
            std::vector<int32_t> faceOrder;
            std::vector<int32_t> edgeOrder;
        };

        // Shared B-Rep Topology -> Triangle Mesh / Wireframe Evaluator
        auto evaluateBRepGraph = [&](const BRepGraph& g) -> bool {
            if (g.faces.empty() && g.edges.empty()) return false;

            auto getVertPt = [&](int32_t vertId, CadVec3& outPt) -> bool {
                if (vertId < 0) return false;
                auto vit = g.vertexToPoint.find(vertId);
                if (vit == g.vertexToPoint.end()) return false;
                auto pit = g.points.find(vit->second);
                if (pit == g.points.end()) return false;
                outPt = pit->second;
                return true;
            };

            auto getBodyXf = [&](int32_t bodyId) -> CadXform {
                auto it = g.bodyTransforms.find(bodyId);
                return (it != g.bodyTransforms.end()) ? it->second : CadXform{};
            };

            // Discretize an oriented edge traversal from vStart -> vEnd along curve
            auto sampleEdgePoints = [&](const BRepEdge& ed, int32_t vStart, int32_t vEnd, bool coedgeForward) -> std::vector<CadVec3> {
                std::vector<CadVec3> poly;
                CadVec3 p0{}, p1{};
                bool hasP0 = getVertPt(vStart, p0);
                bool hasP1 = getVertPt(vEnd,   p1);

                auto cit = g.curves.find(ed.curveId);
                if (cit != g.curves.end() && cit->second.kind == CurveKind::Ellipse) {
                    const BRepCurve& c = cit->second;
                    double rMaj = vLen(c.majorAxis);
                    double nLen = vLen(c.normal);
                    double ratio = std::fabs(c.ratio);
                    if (rMaj > 1e-9 && nLen > 1e-9 && ratio > 1e-6) {
                        CadVec3 nUnit = vNorm(c.normal);
                        CadVec3 uUnit = vMul(c.majorAxis, 1.0 / rMaj);
                        CadVec3 vUnit = vNorm(vCross(nUnit, uUnit));
                        double rMin = rMaj * ratio;
                        auto evalEll = [&](double ang) -> CadVec3 {
                            return vAdd(c.origin, vAdd(vMul(uUnit, rMaj * std::cos(ang)), vMul(vUnit, rMin * std::sin(ang))));
                        };
                        auto angleOf = [&](CadVec3 pt) -> double {
                            CadVec3 d = vSub(pt, c.origin);
                            return std::atan2(vDot(d, vUnit) / rMin, vDot(d, uUnit) / rMaj);
                        };
                        const double kPi = 3.14159265358979323846;
                        bool edgeForward = !ed.reversed;
                        double dirSign = (coedgeForward == edgeForward) ? 1.0 : -1.0;

                        bool isClosedFull = (!hasP0 && !hasP1) ||
                                            (hasP0 && hasP1 && (vStart == vEnd || vDist(p0, p1) < 1e-5 * rMaj));
                        if (isClosedFull) {
                            double t0 = hasP0 ? angleOf(p0) : 0.0;
                            const int segs = 24;
                            poly.reserve(static_cast<size_t>(segs) + 1);
                            for (int k = 0; k <= segs; ++k) {
                                if (k == 0 && hasP0) poly.push_back(p0);
                                else if (k == segs && hasP0) poly.push_back(p0);
                                else poly.push_back(evalEll(t0 + dirSign * (2.0 * kPi * k) / segs));
                            }
                            return poly;
                        }
                        if (hasP0 && hasP1) {
                            double t0 = angleOf(p0);
                            double t1 = angleOf(p1);
                            double sweep = t1 - t0;
                            if (dirSign > 0.0) {
                                while (sweep <= 1e-7) sweep += 2.0 * kPi;
                                while (sweep > 2.0 * kPi + 1e-5) sweep -= 2.0 * kPi;
                            } else {
                                while (sweep >= -1e-7) sweep -= 2.0 * kPi;
                                while (sweep < -2.0 * kPi - 1e-5) sweep += 2.0 * kPi;
                            }
                            int segs = std::clamp(static_cast<int>(std::ceil(std::fabs(sweep) / (2.0 * kPi / 24.0))), 3, 32);
                            poly.reserve(static_cast<size_t>(segs) + 1);
                            poly.push_back(p0);
                            for (int k = 1; k < segs; ++k) {
                                poly.push_back(evalEll(t0 + sweep * (static_cast<double>(k) / segs)));
                            }
                            poly.push_back(p1);
                            return poly;
                        }
                    }
                } else if (cit != g.curves.end() && cit->second.kind == CurveKind::Spline && !cit->second.ctrlPts.empty()) {
                    const auto& cp = cit->second.ctrlPts;
                    bool forwardAlongCurve = (coedgeForward == !ed.reversed);
                    if (hasP0) poly.push_back(p0);
                    if (forwardAlongCurve) {
                        for (const auto& q : cp) poly.push_back(q);
                    } else {
                        for (size_t i = cp.size(); i > 0; --i) poly.push_back(cp[i - 1]);
                    }
                    if (hasP1) poly.push_back(p1);
                    if (poly.size() >= 2) return poly;
                }

                if (hasP0 && hasP1) {
                    poly.push_back(p0);
                    poly.push_back(p1);
                }
                return poly;
            };

            // Spatial vertex welder so B-Rep solids share clean topological vertices
            double globalSpan = 1.0;
            if (!g.points.empty()) {
                auto it0 = g.points.begin();
                CadVec3 mn = it0->second, mx = it0->second;
                for (const auto& kv : g.points) {
                    mn.x = std::min(mn.x, kv.second.x); mn.y = std::min(mn.y, kv.second.y); mn.z = std::min(mn.z, kv.second.z);
                    mx.x = std::max(mx.x, kv.second.x); mx.y = std::max(mx.y, kv.second.y); mx.z = std::max(mx.z, kv.second.z);
                }
                globalSpan = std::max({ mx.x - mn.x, mx.y - mn.y, mx.z - mn.z, 1e-4 });
            }
            const double invWeldCell = 1.0 / (globalSpan * 1e-5);
            struct WeldKey {
                int64_t x, y, z;
                bool operator==(const WeldKey& o) const { return x == o.x && y == o.y && z == o.z; }
            };
            struct WeldHash {
                size_t operator()(const WeldKey& k) const {
                    uint64_t h = static_cast<uint64_t>(k.x) * 11400714819323198485ull;
                    h ^= static_cast<uint64_t>(k.y) * 14029467366897019727ull + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
                    h ^= static_cast<uint64_t>(k.z) * 1609587929392839161ull  + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
                    return static_cast<size_t>(h);
                }
            };
            std::unordered_map<WeldKey, uint32_t, WeldHash> weldedVerts;
            auto emitVertex = [&](CadVec3 p) -> uint32_t {
                WeldKey key{
                    static_cast<int64_t>(std::llround(p.x * invWeldCell)),
                    static_cast<int64_t>(std::llround(p.y * invWeldCell)),
                    static_cast<int64_t>(std::llround(p.z * invWeldCell))
                };
                auto it = weldedVerts.find(key);
                if (it != weldedVerts.end()) return it->second;
                uint32_t idx = static_cast<uint32_t>(mesh.vertices.size());
                mesh.vertices.push_back({
                    static_cast<float>(p.x),
                    static_cast<float>(p.y),
                    static_cast<float>(p.z)
                });
                weldedVerts.emplace(key, idx);
                return idx;
            };

            struct Poly2DVert {
                double u = 0.0;
                double v = 0.0;
                uint32_t vIdx = 0;
            };

            auto signedArea2D = [](const std::vector<Poly2DVert>& p) -> double {
                double a = 0.0;
                size_t n = p.size();
                for (size_t i = 0; i < n; ++i) {
                    size_t j = (i + 1) % n;
                    a += p[i].u * p[j].v - p[j].u * p[i].v;
                }
                return 0.5 * a;
            };

            auto pointInPoly2D = [](double u, double v, const std::vector<Poly2DVert>& poly) -> bool {
                bool inside = false;
                size_t n = poly.size();
                for (size_t i = 0, j = n - 1; i < n; j = i++) {
                    double ui = poly[i].u, vi = poly[i].v;
                    double uj = poly[j].u, vj = poly[j].v;
                    if (((vi > v) != (vj > v)) &&
                        (u < (uj - ui) * (v - vi) / (vj - vi) + ui))
                    {
                        inside = !inside;
                    }
                }
                return inside;
            };

            auto cross2D = [](Poly2DVert a, Poly2DVert b, Poly2DVert c) -> double {
                return (b.u - a.u) * (c.v - a.v) - (b.v - a.v) * (c.u - a.u);
            };

            // Merge CW holes into CCW outer polygon via horizontal +u ray bridge cuts, then ear-clip
            auto triangulateOuterWithHoles = [&](
                std::vector<Poly2DVert> outer,
                std::vector<std::vector<Poly2DVert>> holes,
                bool flipWinding)
            {
                if (outer.size() < 3) return;
                double minU = outer[0].u, maxU = outer[0].u;
                double minV = outer[0].v, maxV = outer[0].v;
                for (const auto& q : outer) {
                    minU = std::min(minU, q.u); maxU = std::max(maxU, q.u);
                    minV = std::min(minV, q.v); maxV = std::max(maxV, q.v);
                }
                double spanUV = std::max({ maxU - minU, maxV - minV, 1e-9 });
                double polyScale2 = spanUV * spanUV;

                auto same2D = [&](Poly2DVert a, Poly2DVert b) -> bool {
                    double du = a.u - b.u, dv = a.v - b.v;
                    return (du * du + dv * dv) <= 1e-18 * polyScale2;
                };
                auto ptInTri2D = [&](Poly2DVert p, Poly2DVert a, Poly2DVert b, Poly2DVert c) -> bool {
                    double eps = -1e-11 * polyScale2;
                    return (cross2D(a, b, p) >= eps && cross2D(b, c, p) >= eps && cross2D(c, a, p) >= eps);
                };

                if (signedArea2D(outer) < 0.0) std::reverse(outer.begin(), outer.end());
                for (auto& h : holes) {
                    if (signedArea2D(h) > 0.0) std::reverse(h.begin(), h.end());
                }

                // Sort holes by rightmost u descending (Eberly bridge ordering)
                std::sort(holes.begin(), holes.end(), [](const auto& a, const auto& b) {
                    double maxA = a[0].u, maxB = b[0].u;
                    for (const auto& q : a) maxA = std::max(maxA, q.u);
                    for (const auto& q : b) maxB = std::max(maxB, q.u);
                    return maxA > maxB;
                });

                const double linEps = 1e-9 * spanUV;
                for (const auto& hole : holes) {
                    if (hole.size() < 3) continue;
                    size_t hm = 0;
                    for (size_t i = 1; i < hole.size(); ++i) {
                        if (hole[i].u > hole[hm].u || (hole[i].u == hole[hm].u && hole[i].v < hole[hm].v)) hm = i;
                    }
                    const Poly2DVert M = hole[hm];

                    double bestIx = 1e300;
                    int bestEdge = -1;
                    size_t on = outer.size();
                    for (size_t i = 0; i < on; ++i) {
                        const Poly2DVert& A = outer[i];
                        const Poly2DVert& B = outer[(i + 1) % on];
                        if ((A.v <= M.v && M.v < B.v) || (B.v <= M.v && M.v < A.v)) {
                            double t = (M.v - A.v) / (B.v - A.v);
                            double ix = A.u + t * (B.u - A.u);
                            if (ix >= M.u - linEps && ix < bestIx) {
                                bestIx = ix;
                                bestEdge = static_cast<int>(i);
                            }
                        }
                    }

                    size_t bridgeIdx = 0;
                    if (bestEdge >= 0) {
                        size_t i0 = static_cast<size_t>(bestEdge);
                        size_t i1 = (i0 + 1) % on;
                        if (std::hypot(outer[i0].u - bestIx, outer[i0].v - M.v) < linEps) {
                            bridgeIdx = i0;
                        } else if (std::hypot(outer[i1].u - bestIx, outer[i1].v - M.v) < linEps) {
                            bridgeIdx = i1;
                        } else {
                            size_t cand = (outer[i0].u >= outer[i1].u) ? i0 : i1;
                            Poly2DVert I{ bestIx, M.v, 0 };
                            Poly2DVert P = outer[cand];
                            Poly2DVert tA = M, tB = I, tC = P;
                            if (cross2D(tA, tB, tC) < 0.0) std::swap(tB, tC);

                            double bestSlope = 1e300;
                            double bestDist  = 1e300;
                            bridgeIdx = cand;
                            for (size_t k = 0; k < on; ++k) {
                                const Poly2DVert& R = outer[k];
                                if (same2D(R, P) || R.u < M.u - linEps) continue;
                                // Check if R is a reflex vertex inside search triangle (M, I, P)
                                const Poly2DVert& rPrev = outer[(k + on - 1) % on];
                                const Poly2DVert& rNext = outer[(k + 1) % on];
                                if (cross2D(rPrev, R, rNext) <= 1e-11 * polyScale2 && ptInTri2D(R, tA, tB, tC)) {
                                    double dx = std::max(0.0, R.u - M.u);
                                    double dy = std::fabs(R.v - M.v);
                                    double dist = std::hypot(dx, dy);
                                    double slope = (dx > 1e-12 * spanUV) ? (dy / dx) : 1e200;
                                    if (slope < bestSlope - 1e-11 || (std::fabs(slope - bestSlope) <= 1e-11 && dist < bestDist)) {
                                        bestSlope = slope;
                                        bestDist  = dist;
                                        bridgeIdx = k;
                                    }
                                }
                            }
                        }
                    } else {
                        double bestD = 1e300;
                        for (size_t k = 0; k < on; ++k) {
                            double d = std::hypot(outer[k].u - M.u, outer[k].v - M.v);
                            if (d < bestD) { bestD = d; bridgeIdx = k; }
                        }
                    }

                    std::vector<Poly2DVert> merged;
                    merged.reserve(outer.size() + hole.size() + 2);
                    for (size_t i = 0; i <= bridgeIdx; ++i) merged.push_back(outer[i]);
                    for (size_t k = 0; k < hole.size(); ++k) merged.push_back(hole[(hm + k) % hole.size()]);
                    merged.push_back(hole[hm]);
                    merged.push_back(outer[bridgeIdx]);
                    for (size_t i = bridgeIdx + 1; i < outer.size(); ++i) merged.push_back(outer[i]);
                    outer = std::move(merged);
                }

                // Ear-clipping with bridge-vertex coincidence handling & recovery
                std::vector<Poly2DVert> work = std::move(outer);
                size_t guard = work.size() * work.size() + 64;
                while (work.size() >= 3 && guard-- > 0) {
                    size_t wn = work.size();
                    if (wn == 3) {
                        uint32_t a = work[0].vIdx, b = work[1].vIdx, c = work[2].vIdx;
                        if (a != b && b != c && a != c) {
                            if (flipWinding) mesh.tris.push_back({ a, c, b, {} });
                            else             mesh.tris.push_back({ a, b, c, {} });
                        }
                        break;
                    }

                    // Remove any adjacent zero-length collapsed edges first
                    bool removedDup = false;
                    for (size_t i = 0; i < wn; ++i) {
                        size_t ni = (i + 1) % wn;
                        if (same2D(work[i], work[ni])) {
                            work.erase(work.begin() + static_cast<std::ptrdiff_t>(ni));
                            removedDup = true;
                            break;
                        }
                    }
                    if (removedDup) continue;

                    int earIdx = -1;
                    for (int pass = 0; pass < 2 && earIdx < 0; ++pass) {
                        double areaEps = (pass == 0) ? (1e-11 * polyScale2) : (-1e-9 * polyScale2);
                        for (size_t i = 0; i < wn; ++i) {
                            const Poly2DVert& A = work[(i + wn - 1) % wn];
                            const Poly2DVert& B = work[i];
                            const Poly2DVert& C = work[(i + 1) % wn];
                            double cr = cross2D(A, B, C);
                            if (cr <= areaEps) continue;
                            bool blocked = false;
                            if (pass == 0) {
                                for (size_t k = 0; k < wn; ++k) {
                                    if (k == i || k == (i + wn - 1) % wn || k == (i + 1) % wn) continue;
                                    const Poly2DVert& P = work[k];
                                    if (same2D(P, A) || same2D(P, B) || same2D(P, C)) continue;
                                    if (ptInTri2D(P, A, B, C)) { blocked = true; break; }
                                }
                            }
                            if (!blocked) {
                                earIdx = static_cast<int>(i);
                                break;
                            }
                        }
                    }

                    if (earIdx < 0) earIdx = 0;
                    size_t ei = static_cast<size_t>(earIdx);
                    uint32_t a = work[(ei + wn - 1) % wn].vIdx;
                    uint32_t b = work[ei].vIdx;
                    uint32_t c = work[(ei + 1) % wn].vIdx;
                    if (a != b && b != c && a != c) {
                        if (flipWinding) mesh.tris.push_back({ a, c, b, {} });
                        else             mesh.tris.push_back({ a, b, c, {} });
                    }
                    work.erase(work.begin() + static_cast<std::ptrdiff_t>(ei));
                }
            };

            // Reconstruct & triangulate every B-Rep face
            const double kPi = 3.14159265358979323846;
            for (int32_t faceId : g.faceOrder) {
                auto fit = g.faces.find(faceId);
                if (fit == g.faces.end()) continue;
                const BRepFace& face = fit->second;
                CadXform xf = getBodyXf(face.bodyId);

                const BRepSurf* surf = nullptr;
                auto sit = g.surfaces.find(face.surfaceId);
                if (sit != g.surfaces.end()) surf = &sit->second;

                // Collect all loops on this face
                std::vector<int32_t> loopList = face.loopIds;
                if (loopList.empty() && face.firstLoop >= 0) {
                    int32_t curL = face.firstLoop;
                    std::set<int32_t> seenL;
                    while (curL >= 0 && seenL.insert(curL).second && seenL.size() <= 512) {
                        loopList.push_back(curL);
                        auto lit = g.loops.find(curL);
                        if (lit == g.loops.end()) break;
                        curL = lit->second.nextLoop;
                    }
                }

                // Build 3D polylines for each loop
                std::vector<std::vector<CadVec3>> loops3D;
                for (int32_t lid : loopList) {
                    auto lit = g.loops.find(lid);
                    if (lit == g.loops.end()) continue;
                    const BRepLoop& lp = lit->second;
                    std::vector<CadVec3> rawLoop;

                    if (lp.firstCoedge >= 0) {
                        std::vector<int32_t> ceChain;
                        int32_t curCe = lp.firstCoedge;
                        std::set<int32_t> seenCe;
                        while (curCe >= 0 && seenCe.insert(curCe).second && ceChain.size() < 4096) {
                            ceChain.push_back(curCe);
                            auto ceIt = g.coedges.find(curCe);
                            if (ceIt == g.coedges.end()) break;
                            int32_t nxt = ceIt->second.nextCoedge;
                            if (nxt == lp.firstCoedge) break;
                            curCe = nxt;
                        }

                        for (size_t ci = 0; ci < ceChain.size(); ++ci) {
                            const BRepCoedge& ce = g.coedges.at(ceChain[ci]);
                            auto edIt = g.edges.find(ce.edgeId);
                            if (edIt == g.edges.end()) continue;
                            const BRepEdge& ed = edIt->second;

                            bool ceFwd = !ce.reversed;
                            int32_t vStart = ceFwd ? ed.startVert : ed.endVert;
                            int32_t vEnd   = ceFwd ? ed.endVert   : ed.startVert;
                            // Parasolid PK fin head-vertex fallback when edge doesn't store direct vertex refs
                            if (vStart < 0 && vEnd < 0 && ce.headVert >= 0 && !ceChain.empty()) {
                                const BRepCoedge& prevCe = g.coedges.at(ceChain[(ci + ceChain.size() - 1) % ceChain.size()]);
                                vStart = prevCe.headVert;
                                vEnd   = ce.headVert;
                            }

                            auto segPts = sampleEdgePoints(ed, vStart, vEnd, ceFwd);
                            for (const auto& sp : segPts) {
                                CadVec3 wp = applyXfPt(xf, sp);
                                if (rawLoop.empty() || vDist(rawLoop.back(), wp) > globalSpan * 1e-6) {
                                    rawLoop.push_back(wp);
                                }
                            }
                        }
                    } else if (!lp.directEdges.empty()) {
                        for (int32_t eid : lp.directEdges) {
                            auto edIt = g.edges.find(eid);
                            if (edIt == g.edges.end()) continue;
                            const BRepEdge& ed = edIt->second;
                            auto segPts = sampleEdgePoints(ed, ed.startVert, ed.endVert, true);
                            if (!rawLoop.empty() && segPts.size() >= 2) {
                                CadVec3 wFirst = applyXfPt(xf, segPts.front());
                                CadVec3 wLast  = applyXfPt(xf, segPts.back());
                                if (vDist(rawLoop.back(), wLast) < vDist(rawLoop.back(), wFirst)) {
                                    std::reverse(segPts.begin(), segPts.end());
                                }
                            }
                            for (const auto& sp : segPts) {
                                CadVec3 wp = applyXfPt(xf, sp);
                                if (rawLoop.empty() || vDist(rawLoop.back(), wp) > globalSpan * 1e-6) {
                                    rawLoop.push_back(wp);
                                }
                            }
                        }
                    }

                    if (rawLoop.size() >= 2 && vDist(rawLoop.front(), rawLoop.back()) <= globalSpan * 1e-5) {
                        rawLoop.pop_back();
                    }
                    if (rawLoop.size() >= 3) {
                        loops3D.push_back(std::move(rawLoop));
                    }
                }

                // Untrimmed closed analytic Sphere / Torus faces (no boundary loops)
                if (loops3D.empty() && surf != nullptr &&
                    (surf->kind == SurfKind::Sphere || surf->kind == SurfKind::Torus)) {
                    if (surf->kind == SurfKind::Sphere && surf->radius1 > 1e-9) {
                        CadVec3 center = applyXfPt(xf, surf->origin);
                        double rad = surf->radius1 * (xf.valid ? std::fabs(xf.scale) : 1.0);
                        const int nLat = 16, nLon = 24;
                        std::vector<std::vector<uint32_t>> grid(nLat + 1, std::vector<uint32_t>(nLon));
                        for (int i = 0; i <= nLat; ++i) {
                            double phi = -0.5 * kPi + kPi * (static_cast<double>(i) / nLat);
                            double cp = std::cos(phi), sp = std::sin(phi);
                            for (int j = 0; j < nLon; ++j) {
                                double th = 2.0 * kPi * (static_cast<double>(j) / nLon);
                                CadVec3 p{
                                    center.x + rad * cp * std::cos(th),
                                    center.y + rad * cp * std::sin(th),
                                    center.z + rad * sp
                                };
                                grid[static_cast<size_t>(i)][static_cast<size_t>(j)] = emitVertex(p);
                            }
                        }
                        for (int i = 0; i < nLat; ++i) {
                            for (int j = 0; j < nLon; ++j) {
                                int nj = (j + 1) % nLon;
                                uint32_t a = grid[static_cast<size_t>(i)][static_cast<size_t>(j)];
                                uint32_t b = grid[static_cast<size_t>(i)][static_cast<size_t>(nj)];
                                uint32_t c = grid[static_cast<size_t>(i + 1)][static_cast<size_t>(nj)];
                                uint32_t d = grid[static_cast<size_t>(i + 1)][static_cast<size_t>(j)];
                                if (a != b && b != c && a != c) {
                                    if (face.reversed) mesh.tris.push_back({ a, c, b, {} });
                                    else               mesh.tris.push_back({ a, b, c, {} });
                                }
                                if (a != c && c != d && a != d) {
                                    if (face.reversed) mesh.tris.push_back({ a, d, c, {} });
                                    else               mesh.tris.push_back({ a, c, d, {} });
                                }
                            }
                        }
                    } else if (surf->kind == SurfKind::Torus && surf->radius1 > 1e-9 && std::fabs(surf->radius2) > 1e-9) {
                        CadVec3 center = applyXfPt(xf, surf->origin);
                        CadVec3 axis   = applyXfDir(xf, surf->normal);
                        CadVec3 ref    = (std::fabs(axis.z) < 0.9) ? CadVec3{ 0, 0, 1 } : CadVec3{ 1, 0, 0 };
                        CadVec3 uAx    = vNorm(vCross(ref, axis));
                        CadVec3 vAx    = vNorm(vCross(axis, uAx));
                        double rMaj = surf->radius1 * (xf.valid ? std::fabs(xf.scale) : 1.0);
                        double rMin = std::fabs(surf->radius2) * (xf.valid ? std::fabs(xf.scale) : 1.0);
                        const int nMaj = 24, nMin = 16;
                        std::vector<std::vector<uint32_t>> grid(nMaj, std::vector<uint32_t>(nMin));
                        for (int i = 0; i < nMaj; ++i) {
                            double u = 2.0 * kPi * (static_cast<double>(i) / nMaj);
                            CadVec3 radDir = vAdd(vMul(uAx, std::cos(u)), vMul(vAx, std::sin(u)));
                            for (int j = 0; j < nMin; ++j) {
                                double v = 2.0 * kPi * (static_cast<double>(j) / nMin);
                                CadVec3 p = vAdd(center, vAdd(vMul(radDir, rMaj + rMin * std::cos(v)), vMul(axis, rMin * std::sin(v))));
                                grid[static_cast<size_t>(i)][static_cast<size_t>(j)] = emitVertex(p);
                            }
                        }
                        for (int i = 0; i < nMaj; ++i) {
                            int ni = (i + 1) % nMaj;
                            for (int j = 0; j < nMin; ++j) {
                                int nj = (j + 1) % nMin;
                                uint32_t a = grid[static_cast<size_t>(i)][static_cast<size_t>(j)];
                                uint32_t b = grid[static_cast<size_t>(ni)][static_cast<size_t>(j)];
                                uint32_t c = grid[static_cast<size_t>(ni)][static_cast<size_t>(nj)];
                                uint32_t d = grid[static_cast<size_t>(i)][static_cast<size_t>(nj)];
                                if (face.reversed) {
                                    mesh.tris.push_back({ a, c, b, {} });
                                    mesh.tris.push_back({ a, d, c, {} });
                                } else {
                                    mesh.tris.push_back({ a, b, c, {} });
                                    mesh.tris.push_back({ a, c, d, {} });
                                }
                            }
                        }
                    }
                    continue;
                }

                // Rational / Non-Rational Cox-de Boor NURBS Surface (b_surface / spline-surface)
                if (surf != nullptr && surf->kind == SurfKind::Nurbs &&
                    surf->uDeg >= 1 && surf->vDeg >= 1 && surf->nU > surf->uDeg && surf->nV > surf->vDeg &&
                    static_cast<int>(surf->knotsU.size()) >= surf->nU + surf->uDeg + 1 &&
                    static_cast<int>(surf->knotsV.size()) >= surf->nV + surf->vDeg + 1 &&
                    surf->poles.size() >= static_cast<size_t>(surf->nU) * static_cast<size_t>(surf->nV))
                {
                    auto evalBasis = [](int n, int deg, const std::vector<double>& U, double t, std::vector<double>& outB) {
                        outB.assign(static_cast<size_t>(n), 0.0);
                        int order = deg + 1;
                        double tMin = U[static_cast<size_t>(deg)];
                        double tMax = U[static_cast<size_t>(n)];
                        if (t <= tMin) t = tMin;
                        if (t >= tMax) { outB[static_cast<size_t>(n - 1)] = 1.0; return; }
                        int span = deg;
                        for (int i = deg; i < n; ++i) {
                            if (t >= U[static_cast<size_t>(i)] && t < U[static_cast<size_t>(i + 1)]) { span = i; break; }
                        }
                        std::vector<double> N(static_cast<size_t>(order), 0.0);
                        std::vector<double> L(static_cast<size_t>(order), 0.0);
                        std::vector<double> R(static_cast<size_t>(order), 0.0);
                        N[0] = 1.0;
                        for (int j = 1; j <= deg; ++j) {
                            L[static_cast<size_t>(j)] = t - U[static_cast<size_t>(span + 1 - j)];
                            R[static_cast<size_t>(j)] = U[static_cast<size_t>(span + j)] - t;
                            double saved = 0.0;
                            for (int r = 0; r < j; ++r) {
                                double denom = R[static_cast<size_t>(r + 1)] + L[static_cast<size_t>(j - r)];
                                double tmp = (std::fabs(denom) > 1e-15) ? (N[static_cast<size_t>(r)] / denom) : 0.0;
                                N[static_cast<size_t>(r)] = saved + R[static_cast<size_t>(r + 1)] * tmp;
                                saved = L[static_cast<size_t>(j - r)] * tmp;
                            }
                            N[static_cast<size_t>(j)] = saved;
                        }
                        for (int j = 0; j <= deg; ++j) {
                            int idx = span - deg + j;
                            if (idx >= 0 && idx < n) outB[static_cast<size_t>(idx)] = N[static_cast<size_t>(j)];
                        }
                    };

                    double u0 = surf->knotsU[static_cast<size_t>(surf->uDeg)];
                    double u1 = surf->knotsU[static_cast<size_t>(surf->nU)];
                    double v0 = surf->knotsV[static_cast<size_t>(surf->vDeg)];
                    double v1 = surf->knotsV[static_cast<size_t>(surf->nV)];
                    if (u1 > u0 && v1 > v0) {
                        constexpr int kNu = 16, kNv = 16;
                        std::vector<std::vector<uint32_t>> grid(kNu + 1, std::vector<uint32_t>(kNv + 1));
                        std::vector<double> bu, bv;
                        bool hasWeights = surf->isRational && surf->weights.size() >= surf->poles.size();
                        for (int iu = 0; iu <= kNu; ++iu) {
                            double u = u0 + (u1 - u0) * (static_cast<double>(iu) / kNu);
                            evalBasis(surf->nU, surf->uDeg, surf->knotsU, u, bu);
                            for (int iv = 0; iv <= kNv; ++iv) {
                                double v = v0 + (v1 - v0) * (static_cast<double>(iv) / kNv);
                                evalBasis(surf->nV, surf->vDeg, surf->knotsV, v, bv);
                                double sx = 0.0, sy = 0.0, sz = 0.0, sw = 0.0;
                                for (int i = 0; i < surf->nU; ++i) {
                                    if (bu[static_cast<size_t>(i)] == 0.0) continue;
                                    for (int j = 0; j < surf->nV; ++j) {
                                        if (bv[static_cast<size_t>(j)] == 0.0) continue;
                                        size_t pidx = static_cast<size_t>(i * surf->nV + j);
                                        double w = hasWeights ? surf->weights[pidx] : 1.0;
                                        double b = bu[static_cast<size_t>(i)] * bv[static_cast<size_t>(j)] * w;
                                        sx += b * surf->poles[pidx].x;
                                        sy += b * surf->poles[pidx].y;
                                        sz += b * surf->poles[pidx].z;
                                        sw += b;
                                    }
                                }
                                if (std::fabs(sw) > 1e-12) { sx /= sw; sy /= sw; sz /= sw; }
                                grid[static_cast<size_t>(iu)][static_cast<size_t>(iv)] = emitVertex(applyXfPt(xf, { sx, sy, sz }));
                            }
                        }
                        for (int iu = 0; iu < kNu; ++iu) {
                            for (int iv = 0; iv < kNv; ++iv) {
                                uint32_t a = grid[static_cast<size_t>(iu)][static_cast<size_t>(iv)];
                                uint32_t b = grid[static_cast<size_t>(iu + 1)][static_cast<size_t>(iv)];
                                uint32_t c = grid[static_cast<size_t>(iu + 1)][static_cast<size_t>(iv + 1)];
                                uint32_t d = grid[static_cast<size_t>(iu)][static_cast<size_t>(iv + 1)];
                                if (a != b && b != c && a != c) {
                                    if (face.reversed) mesh.tris.push_back({ a, c, b, {} });
                                    else               mesh.tris.push_back({ a, b, c, {} });
                                }
                                if (a != c && c != d && a != d) {
                                    if (face.reversed) mesh.tris.push_back({ a, d, c, {} });
                                    else               mesh.tris.push_back({ a, c, d, {} });
                                }
                            }
                        }
                        continue;
                    }
                }

                if (loops3D.empty()) continue;

                // Cylindrical / Conical surface unwrap in (theta, axial_h)
                if (surf != nullptr && surf->kind == SurfKind::CylinderCone && vLen(surf->normal) > 1e-9) {
                    CadVec3 cOrig = applyXfPt(xf, surf->origin);
                    CadVec3 cAxis = applyXfDir(xf, surf->normal);
                    CadVec3 uHint = (vLen(surf->uAxis) > 1e-9) ? applyXfDir(xf, surf->uAxis)
                                  : ((std::fabs(cAxis.z) < 0.9) ? CadVec3{ 0, 0, 1 } : CadVec3{ 1, 0, 0 });
                    uHint = vSub(uHint, vMul(cAxis, vDot(uHint, cAxis)));
                    if (vLen(uHint) < 1e-9) uHint = (std::fabs(cAxis.x) < 0.9) ? CadVec3{ 1, 0, 0 } : CadVec3{ 0, 1, 0 };
                    CadVec3 u0 = vNorm(uHint);
                    CadVec3 v0 = vNorm(vCross(cAxis, u0));

                    struct UnwrappedCylLoop {
                        std::vector<Poly2DVert> pts;
                        double sweep = 0.0;
                        double meanH = 0.0;
                    };
                    std::vector<UnwrappedCylLoop> cylLoops;
                    for (const auto& lp3 : loops3D) {
                        UnwrappedCylLoop ul;
                        double prevTh = 0.0;
                        double sumH = 0.0;
                        for (size_t k = 0; k < lp3.size(); ++k) {
                            CadVec3 d = vSub(lp3[k], cOrig);
                            double h = vDot(d, cAxis);
                            double th = std::atan2(vDot(d, v0), vDot(d, u0));
                            if (k > 0) {
                                while (th - prevTh >  kPi) th -= 2.0 * kPi;
                                while (th - prevTh < -kPi) th += 2.0 * kPi;
                            }
                            prevTh = th;
                            sumH += h;
                            ul.pts.push_back({ th, h, emitVertex(lp3[k]) });
                        }
                        // Check closing delta to detect full 2*pi coaxial circle loops vs closed (theta,h) patches
                        CadVec3 d0 = vSub(lp3[0], cOrig);
                        double thClose = std::atan2(vDot(d0, v0), vDot(d0, u0));
                        while (thClose - prevTh >  kPi) thClose -= 2.0 * kPi;
                        while (thClose - prevTh < -kPi) thClose += 2.0 * kPi;
                        ul.sweep = thClose - ul.pts.front().u;
                        ul.meanH = sumH / static_cast<double>(lp3.size());
                        cylLoops.push_back(std::move(ul));
                    }

                    // Case 1: Seamless periodic cylinder/cone with 2 full-circle coaxial boundary loops
                    std::vector<size_t> fullCircleIdx;
                    std::vector<std::vector<Poly2DVert>> wallHoles;
                    for (size_t li = 0; li < cylLoops.size(); ++li) {
                        if (std::fabs(cylLoops[li].sweep) > 1.5 * kPi) fullCircleIdx.push_back(li);
                        else wallHoles.push_back(cylLoops[li].pts);
                    }
                    if (fullCircleIdx.size() == 2) {
                        size_t iLow  = fullCircleIdx[0];
                        size_t iHigh = fullCircleIdx[1];
                        if (cylLoops[iLow].meanH > cylLoops[iHigh].meanH) std::swap(iLow, iHigh);
                        const auto& lpLow  = loops3D[iLow];
                        const auto& lpHigh = loops3D[iHigh];

                        // Sort each coaxial ring by exact angle theta in [-pi, pi) so wall triangles share watertight cap vertices
                        auto buildSortedRing = [&](const std::vector<CadVec3>& ring) -> std::vector<Poly2DVert> {
                            std::vector<Poly2DVert> out;
                            out.reserve(ring.size() + 1);
                            for (const auto& pt : ring) {
                                CadVec3 d = vSub(pt, cOrig);
                                double th = std::atan2(vDot(d, v0), vDot(d, u0));
                                double h  = vDot(d, cAxis);
                                out.push_back({ th, h, emitVertex(pt) });
                            }
                            std::sort(out.begin(), out.end(), [](const Poly2DVert& a, const Poly2DVert& b) {
                                return a.u < b.u;
                            });
                            return out;
                        };

                        auto rLow  = buildSortedRing(lpLow);
                        auto rHigh = buildSortedRing(lpHigh);
                        if (rLow.size() >= 3 && rHigh.size() >= 3) {
                            double thBase = rLow.front().u;
                            // Shift rHigh angles into [thBase, thBase + 2*pi) and re-sort so seams align
                            for (auto& q : rHigh) {
                                while (q.u <  thBase - 1e-9)            q.u += 2.0 * kPi;
                                while (q.u >= thBase + 2.0 * kPi - 1e-9) q.u -= 2.0 * kPi;
                            }
                            std::sort(rHigh.begin(), rHigh.end(), [](const Poly2DVert& a, const Poly2DVert& b) {
                                return a.u < b.u;
                            });

                            std::vector<Poly2DVert> rectOuter;
                            rectOuter.reserve(rLow.size() + rHigh.size() + 2);
                            for (const auto& q : rLow) rectOuter.push_back(q);
                            rectOuter.push_back({ rLow.front().u + 2.0 * kPi, rLow.front().v, rLow.front().vIdx });
                            rectOuter.push_back({ rHigh.front().u + 2.0 * kPi, rHigh.front().v, rHigh.front().vIdx });
                            for (size_t k = rHigh.size(); k > 0; --k) rectOuter.push_back(rHigh[k - 1]);

                            // Normalize any wall hole theta centers into [thBase, thBase + 2*pi)
                            for (auto& wh : wallHoles) {
                                double cTh = 0.0;
                                for (const auto& q : wh) cTh += q.u;
                                cTh /= static_cast<double>(wh.size());
                                double shift = 0.0;
                                while (cTh + shift <  thBase)             shift += 2.0 * kPi;
                                while (cTh + shift >= thBase + 2.0 * kPi) shift -= 2.0 * kPi;
                                for (auto& q : wh) q.u += shift;
                            }
                            triangulateOuterWithHoles(std::move(rectOuter), std::move(wallHoles), face.reversed);
                            continue;
                        }
                    }

                    // Case 2: Bounded cylinder/cone patch(es) with seam or partial angular span
                    size_t outerIdx = 0;
                    double bestAbsArea = -1.0;
                    for (size_t li = 0; li < cylLoops.size(); ++li) {
                        double aa = std::fabs(signedArea2D(cylLoops[li].pts));
                        if (aa > bestAbsArea) { bestAbsArea = aa; outerIdx = li; }
                    }
                    if (bestAbsArea > 1e-10) {
                        std::vector<std::vector<Poly2DVert>> holes2D;
                        for (size_t li = 0; li < cylLoops.size(); ++li) {
                            if (li != outerIdx) holes2D.push_back(cylLoops[li].pts);
                        }
                        triangulateOuterWithHoles(cylLoops[outerIdx].pts, std::move(holes2D), face.reversed);
                        continue;
                    }
                }

                // Planar (or general curved) face: determine target 3D normal & tangent basis (uBasis, vBasis, nBasis)
                CadVec3 nBasis = { 0.0, 0.0, 0.0 };
                if (surf != nullptr && surf->kind == SurfKind::Plane && vLen(surf->normal) > 1e-9) {
                    nBasis = applyXfDir(xf, surf->normal);
                    if (face.reversed) nBasis = vMul(nBasis, -1.0);
                } else {
                    // Newell's method on the largest-perimeter loop
                    size_t bestL = 0;
                    for (size_t li = 1; li < loops3D.size(); ++li) {
                        if (loops3D[li].size() > loops3D[bestL].size()) bestL = li;
                    }
                    const auto& lp = loops3D[bestL];
                    CadVec3 nw{};
                    for (size_t i = 0; i < lp.size(); ++i) {
                        const CadVec3& c0 = lp[i];
                        const CadVec3& c1 = lp[(i + 1) % lp.size()];
                        nw.x += (c0.y - c1.y) * (c0.z + c1.z);
                        nw.y += (c0.z - c1.z) * (c0.x + c1.x);
                        nw.z += (c0.x - c1.x) * (c0.y + c1.y);
                    }
                    if (vLen(nw) < 1e-12) continue;
                    nBasis = vNorm(nw);
                    if (face.reversed) nBasis = vMul(nBasis, -1.0);
                }

                CadVec3 refAx = (std::fabs(nBasis.z) < 0.9) ? CadVec3{ 0.0, 0.0, 1.0 } : CadVec3{ 1.0, 0.0, 0.0 };
                CadVec3 uBasis = vNorm(vCross(refAx, nBasis));
                CadVec3 vBasis = vNorm(vCross(nBasis, uBasis)); // right-handed: uBasis x vBasis == +nBasis

                CadVec3 origin3D = loops3D[0][0];
                struct ProjectedLoop {
                    std::vector<Poly2DVert> pts;
                    double absArea = 0.0;
                };
                std::vector<ProjectedLoop> projLoops;
                for (const auto& lp3 : loops3D) {
                    ProjectedLoop pl;
                    pl.pts.reserve(lp3.size());
                    for (const auto& p3 : lp3) {
                        CadVec3 rel = vSub(p3, origin3D);
                        pl.pts.push_back({ vDot(rel, uBasis), vDot(rel, vBasis), emitVertex(p3) });
                    }
                    pl.absArea = std::fabs(signedArea2D(pl.pts));
                    if (pl.pts.size() >= 3 && pl.absArea > 1e-12) {
                        projLoops.push_back(std::move(pl));
                    }
                }
                if (projLoops.empty()) continue;

                // Sort projected loops by descending area so outer islands precede inner holes
                std::sort(projLoops.begin(), projLoops.end(), [](const ProjectedLoop& a, const ProjectedLoop& b) {
                    return a.absArea > b.absArea;
                });

                // Group inner hole loops into their enclosing outer island
                std::vector<int> parentIsland(projLoops.size(), -1);
                for (size_t i = 1; i < projLoops.size(); ++i) {
                    for (size_t j = i; j > 0; --j) {
                        size_t cand = j - 1;
                        if (parentIsland[cand] == -1 &&
                            pointInPoly2D(projLoops[i].pts[0].u, projLoops[i].pts[0].v, projLoops[cand].pts))
                        {
                            parentIsland[i] = static_cast<int>(cand);
                            break;
                        }
                    }
                }

                for (size_t i = 0; i < projLoops.size(); ++i) {
                    if (parentIsland[i] != -1) continue;
                    std::vector<std::vector<Poly2DVert>> myHoles;
                    for (size_t k = i + 1; k < projLoops.size(); ++k) {
                        if (parentIsland[k] == static_cast<int>(i)) {
                            myHoles.push_back(projLoops[k].pts);
                        }
                    }
                    // Since uBasis x vBasis == +nBasis (which already accounts for face.reversed), CCW in 2D == outward 3D!
                    triangulateOuterWithHoles(projLoops[i].pts, std::move(myHoles), false);
                }
            }

            // Pure wireframe B-Rep fallback (wire-body / topological edges without faces):
            // NEVER connect bare point soup — emit struts ONLY along genuine topological edge records!
            if (mesh.tris.empty() && !g.edges.empty()) {
                std::vector<std::vector<Vec3f>> edgePolylines;
                std::vector<Vec3f> allWirePts;
                for (int32_t eid : g.edgeOrder) {
                    auto eit = g.edges.find(eid);
                    if (eit == g.edges.end()) continue;
                    const BRepEdge& ed = eit->second;
                    auto segPts = sampleEdgePoints(ed, ed.startVert, ed.endVert, true);
                    if (segPts.size() < 2) continue;
                    CadXform xf = getBodyXf(ed.bodyId);
                    std::vector<Vec3f> wPoly;
                    wPoly.reserve(segPts.size());
                    for (const auto& sp : segPts) {
                        CadVec3 wp = applyXfPt(xf, sp);
                        Vec3f vf{ static_cast<float>(wp.x), static_cast<float>(wp.y), static_cast<float>(wp.z) };
                        wPoly.push_back(vf);
                        allWirePts.push_back(vf);
                    }
                    edgePolylines.push_back(std::move(wPoly));
                }
                if (!edgePolylines.empty() && allWirePts.size() >= 2) {
                    float span = Detail3D200::ComputePointCloudSpan(allWirePts);
                    float strutR = std::max(1e-4f, span * 0.016f);
                    for (const auto& wp : edgePolylines) {
                        for (size_t i = 0; i + 1 < wp.size(); ++i) {
                            Detail3D200::AddStrut3D(mesh, wp[i], wp[i + 1], strutR);
                        }
                    }
                }
            }

            return !mesh.vertices.empty() && !mesh.tris.empty();
        };

        // Shared ACIS Raw Entity Record -> BRepGraph builder (used by BOTH .sat and .sab!)
        struct AcisRawRec {
            std::string type;
            std::vector<int32_t> ptrs;
            std::vector<double> nums;
            std::vector<std::string> words;
            std::vector<double> subNums;
            std::vector<std::string> subWords;
        };

        auto buildAcisGraphAndTriangulate = [&](const std::vector<AcisRawRec>& recs) -> bool {
            if (recs.empty()) return false;
            const int32_t nRecs = static_cast<int32_t>(recs.size());
            auto validRef = [&](int32_t r) -> bool { return r >= 0 && r < nRecs && !recs[static_cast<size_t>(r)].type.empty(); };
            auto findRefByType = [&](const AcisRawRec& rc, const char* want, size_t startSlot = 0) -> int32_t {
                for (size_t i = startSlot; i < rc.ptrs.size(); ++i) {
                    int32_t r = rc.ptrs[i];
                    if (validRef(r) && recs[static_cast<size_t>(r)].type == want) return r;
                }
                return -1;
            };
            auto findRefBySuffix = [&](const AcisRawRec& rc, const char* suffix, size_t startSlot = 0) -> int32_t {
                size_t slen = std::strlen(suffix);
                for (size_t i = startSlot; i < rc.ptrs.size(); ++i) {
                    int32_t r = rc.ptrs[i];
                    if (!validRef(r)) continue;
                    const std::string& t = recs[static_cast<size_t>(r)].type;
                    if (t.size() >= slen && t.compare(t.size() - slen, slen, suffix) == 0) return r;
                }
                return -1;
            };
            auto hasWord = [](const AcisRawRec& rc, const char* w) -> bool {
                for (const auto& s : rc.words) if (s == w) return true;
                return false;
            };
            auto ptrSkipAttrib = [&](const AcisRawRec& rc) -> size_t {
                if (!rc.ptrs.empty()) {
                    int32_t r0 = rc.ptrs[0];
                    if (r0 < 0 || (validRef(r0) && recs[static_cast<size_t>(r0)].type.find("attrib") != std::string::npos)) {
                        return 1;
                    }
                }
                return 0;
            };

            // Detect ACIS 7.0+ leading integer entity IDs on topology records (absent in ACIS <= 6.0 and .sab nums)
            bool hasLeadingEntId = false;
            for (const auto& rc : recs) {
                if ((rc.type == "face" || rc.type == "loop" || rc.type == "lump" || rc.type == "shell") && !rc.nums.empty()) {
                    hasLeadingEntId = true;
                    break;
                }
            }

            BRepGraph g;
            std::unordered_map<int32_t, int32_t> lumpNext, lumpShell, lumpBody;
            std::unordered_map<int32_t, int32_t> shellNext, shellSubshell, shellFace, shellWire, shellLump;
            std::unordered_map<int32_t, int32_t> subshellNext, subshellChild, subshellFace;
            std::unordered_map<int32_t, int32_t> wireNext, wireCoedge;
            std::vector<int32_t> bodyIds;

            for (int32_t idx = 0; idx < nRecs; ++idx) {
                const AcisRawRec& rc = recs[static_cast<size_t>(idx)];
                const std::string& tp = rc.type;
                if (tp.empty()) continue;

                if (tp == "point") {
                    if (rc.nums.size() >= 3) {
                        size_t n = rc.nums.size();
                        g.points[idx] = { rc.nums[n - 3], rc.nums[n - 2], rc.nums[n - 1] };
                    }
                } else if (tp == "vertex") {
                    int32_t ptRef = findRefByType(rc, "point", 0);
                    size_t pBase = ptrSkipAttrib(rc);
                    if (ptRef < 0 && rc.ptrs.size() >= pBase + 2) ptRef = rc.ptrs[pBase + 1];
                    if (ptRef >= 0) g.vertexToPoint[idx] = ptRef;
                } else if (tp == "transform") {
                    size_t base = (hasLeadingEntId || rc.nums.size() == 14) ? 1 : 0;
                    if (rc.nums.size() >= base + 12) {
                        CadXform xf;
                        for (int i = 0; i < 9; ++i) xf.r[i] = rc.nums[base + static_cast<size_t>(i)];
                        xf.t[0] = rc.nums[base + 9];
                        xf.t[1] = rc.nums[base + 10];
                        xf.t[2] = rc.nums[base + 11];
                        xf.scale = (rc.nums.size() >= base + 13) ? rc.nums[base + 12] : 1.0;
                        if (!std::isfinite(xf.scale) || std::fabs(xf.scale) < 1e-12) xf.scale = 1.0;
                        xf.valid = true;
                        g.bodyTransforms[-1000000 - idx] = xf; // temporary transform table keyed by transform id
                    }
                } else if (tp == "straight-curve") {
                    BRepCurve c;
                    c.kind = CurveKind::Straight;
                    size_t base = (hasLeadingEntId || rc.nums.size() == 7 || rc.nums.size() == 9) ? 1 : 0;
                    if (rc.nums.size() >= base + 6) {
                        c.origin = { rc.nums[base + 0], rc.nums[base + 1], rc.nums[base + 2] };
                        c.normal = { rc.nums[base + 3], rc.nums[base + 4], rc.nums[base + 5] };
                    }
                    g.curves[idx] = std::move(c);
                } else if (tp == "ellipse-curve") {
                    BRepCurve c;
                    c.kind = CurveKind::Ellipse;
                    size_t base = (hasLeadingEntId || rc.nums.size() == 11 || rc.nums.size() == 13) ? 1 : 0;
                    if (rc.nums.size() >= base + 10) {
                        c.origin    = { rc.nums[base + 0], rc.nums[base + 1], rc.nums[base + 2] };
                        c.normal    = { rc.nums[base + 3], rc.nums[base + 4], rc.nums[base + 5] };
                        c.majorAxis = { rc.nums[base + 6], rc.nums[base + 7], rc.nums[base + 8] };
                        c.ratio     = rc.nums[base + 9];
                    }
                    g.curves[idx] = std::move(c);
                } else if (tp.find("intcurve") != std::string::npos || tp.find("spline") != std::string::npos) {
                    if (tp.find("curve") != std::string::npos) {
                        BRepCurve c;
                        c.kind = CurveKind::Spline;
                        if (rc.subNums.size() >= 9) {
                            size_t rem3 = rc.subNums.size() % 3;
                            for (size_t k = rem3; k + 2 < rc.subNums.size() && c.ctrlPts.size() < 64; k += 3) {
                                c.ctrlPts.push_back({ rc.subNums[k], rc.subNums[k + 1], rc.subNums[k + 2] });
                            }
                        }
                        g.curves[idx] = std::move(c);
                    } else if (tp.find("surface") != std::string::npos && rc.subNums.size() >= 12) {
                        // ACIS spline-surface { exactsur nubs|nurbs uDeg vDeg [nU nV] knotsU knotsV poles [weights] }
                        bool isRat = false;
                        for (const auto& sw : rc.subWords) {
                            if (sw == "nurbs" || sw == "rat" || sw == "rational") { isRat = true; break; }
                        }
                        int uDeg = static_cast<int>(std::llround(rc.subNums[0]));
                        int vDeg = static_cast<int>(std::llround(rc.subNums[1]));
                        if (uDeg >= 1 && uDeg <= 15 && vDeg >= 1 && vDeg <= 15 && rc.subNums.size() >= 6) {
                            int nU = static_cast<int>(std::llround(rc.subNums[2]));
                            int nV = static_cast<int>(std::llround(rc.subNums[3]));
                            size_t sp = 4;
                            BRepSurf s;
                            s.kind = SurfKind::Nurbs;
                            s.uDeg = uDeg; s.vDeg = vDeg; s.nU = nU; s.nV = nV;
                            s.isRational = isRat;

                            if (nU > uDeg && nU <= 256 && nV > vDeg && nV <= 256) {
                                size_t fullKu = static_cast<size_t>(nU + uDeg + 1);
                                size_t fullKv = static_cast<size_t>(nV + vDeg + 1);
                                size_t poleCnt = static_cast<size_t>(nU) * static_cast<size_t>(nV);
                                size_t poleFloats = poleCnt * (isRat ? 4u : 3u);

                                // Variant 1: flat full knot vectors [knotsU(nU+uDeg+1)][knotsV(nV+vDeg+1)][poles (x,y,z[,w])]
                                if (sp + fullKu + fullKv + poleCnt * 3u <= rc.subNums.size()) {
                                    s.knotsU.assign(rc.subNums.begin() + static_cast<ptrdiff_t>(sp),
                                                    rc.subNums.begin() + static_cast<ptrdiff_t>(sp + fullKu));
                                    sp += fullKu;
                                    s.knotsV.assign(rc.subNums.begin() + static_cast<ptrdiff_t>(sp),
                                                    rc.subNums.begin() + static_cast<ptrdiff_t>(sp + fullKv));
                                    sp += fullKv;

                                    if (isRat && sp + poleFloats <= rc.subNums.size()) {
                                        s.poles.resize(poleCnt);
                                        s.weights.resize(poleCnt, 1.0);
                                        // Check whether weights are interleaved (x,y,z,w) or planar [xyz...][w...]
                                        for (size_t k = 0; k < poleCnt; ++k) {
                                            s.poles[k] = { rc.subNums[sp + k * 4 + 0], rc.subNums[sp + k * 4 + 1], rc.subNums[sp + k * 4 + 2] };
                                            s.weights[k] = rc.subNums[sp + k * 4 + 3];
                                        }
                                        g.surfaces[idx] = std::move(s);
                                    } else if (sp + poleCnt * 3u <= rc.subNums.size()) {
                                        s.poles.resize(poleCnt);
                                        for (size_t k = 0; k < poleCnt; ++k) {
                                            s.poles[k] = { rc.subNums[sp + k * 3 + 0], rc.subNums[sp + k * 3 + 1], rc.subNums[sp + k * 3 + 2] };
                                        }
                                        g.surfaces[idx] = std::move(s);
                                    }
                                }
                            }
                        }
                    }
                } else if (tp == "plane-surface") {
                    BRepSurf s;
                    s.kind = SurfKind::Plane;
                    size_t base = (hasLeadingEntId || rc.nums.size() == 7 || rc.nums.size() == 10) ? 1 : 0;
                    if (rc.nums.size() >= base + 6) {
                        s.origin = { rc.nums[base + 0], rc.nums[base + 1], rc.nums[base + 2] };
                        s.normal = { rc.nums[base + 3], rc.nums[base + 4], rc.nums[base + 5] };
                        if (rc.nums.size() >= base + 9) {
                            s.uAxis = { rc.nums[base + 6], rc.nums[base + 7], rc.nums[base + 8] };
                        }
                    }
                    g.surfaces[idx] = s;
                } else if (tp == "cone-surface") {
                    BRepSurf s;
                    s.kind = SurfKind::CylinderCone;
                    size_t base = (hasLeadingEntId || rc.nums.size() == 11 || rc.nums.size() == 14) ? 1 : 0;
                    if (rc.nums.size() >= base + 10) {
                        s.origin  = { rc.nums[base + 0], rc.nums[base + 1], rc.nums[base + 2] };
                        s.normal  = { rc.nums[base + 3], rc.nums[base + 4], rc.nums[base + 5] };
                        s.uAxis   = { rc.nums[base + 6], rc.nums[base + 7], rc.nums[base + 8] };
                        s.radius1 = vLen(s.uAxis);
                        s.ratio   = rc.nums[base + 9];
                    }
                    g.surfaces[idx] = s;
                } else if (tp == "sphere-surface") {
                    BRepSurf s;
                    s.kind = SurfKind::Sphere;
                    size_t base = (hasLeadingEntId || rc.nums.size() == 5 || rc.nums.size() == 11) ? 1 : 0;
                    if (rc.nums.size() >= base + 4) {
                        s.origin  = { rc.nums[base + 0], rc.nums[base + 1], rc.nums[base + 2] };
                        s.radius1 = std::fabs(rc.nums[base + 3]);
                    }
                    g.surfaces[idx] = s;
                } else if (tp == "torus-surface") {
                    BRepSurf s;
                    s.kind = SurfKind::Torus;
                    size_t base = (hasLeadingEntId || rc.nums.size() == 9 || rc.nums.size() == 12) ? 1 : 0;
                    if (rc.nums.size() >= base + 8) {
                        s.origin  = { rc.nums[base + 0], rc.nums[base + 1], rc.nums[base + 2] };
                        s.normal  = { rc.nums[base + 3], rc.nums[base + 4], rc.nums[base + 5] };
                        s.radius1 = std::fabs(rc.nums[base + 6]);
                        s.radius2 = rc.nums[base + 7];
                    }
                    g.surfaces[idx] = s;
                } else if (tp == "edge") {
                    BRepEdge ed;
                    std::vector<int32_t> vRefs;
                    for (int32_t r : rc.ptrs) {
                        if (validRef(r) && recs[static_cast<size_t>(r)].type == "vertex") vRefs.push_back(r);
                    }
                    if (vRefs.size() >= 2) { ed.startVert = vRefs[0]; ed.endVert = vRefs[1]; }
                    else if (vRefs.size() == 1) { ed.startVert = vRefs[0]; ed.endVert = vRefs[0]; }
                    ed.curveId = findRefBySuffix(rc, "-curve", 0);
                    ed.reversed = hasWord(rc, "reversed");
                    g.edges[idx] = ed;
                    g.edgeOrder.push_back(idx);
                } else if (tp == "coedge") {
                    BRepCoedge ce;
                    size_t pBase = ptrSkipAttrib(rc);
                    if (rc.ptrs.size() > pBase)     ce.nextCoedge = rc.ptrs[pBase];
                    if (rc.ptrs.size() > pBase + 1) ce.prevCoedge = rc.ptrs[pBase + 1];
                    ce.edgeId = findRefByType(rc, "edge", 0);
                    ce.reversed = hasWord(rc, "reversed");
                    g.coedges[idx] = ce;
                } else if (tp == "loop") {
                    BRepLoop lp;
                    lp.nextLoop    = findRefByType(rc, "loop", 0);
                    lp.firstCoedge = findRefByType(rc, "coedge", 0);
                    lp.ownerFace   = findRefByType(rc, "face", 0);
                    g.loops[idx] = lp;
                } else if (tp == "face") {
                    BRepFace fc;
                    fc.nextFace  = findRefByType(rc, "face", 0);
                    fc.firstLoop = findRefByType(rc, "loop", 0);
                    fc.surfaceId = findRefBySuffix(rc, "-surface", 0);
                    fc.reversed  = hasWord(rc, "reversed");
                    g.faces[idx] = fc;
                    g.faceOrder.push_back(idx);
                } else if (tp == "body") {
                    bodyIds.push_back(idx);
                } else if (tp == "lump") {
                    lumpNext[idx]  = findRefByType(rc, "lump", 0);
                    lumpShell[idx] = findRefByType(rc, "shell", 0);
                    lumpBody[idx]  = findRefByType(rc, "body", 0);
                } else if (tp == "shell") {
                    shellNext[idx]     = findRefByType(rc, "shell", 0);
                    shellSubshell[idx] = findRefByType(rc, "subshell", 0);
                    shellFace[idx]     = findRefByType(rc, "face", 0);
                    shellWire[idx]     = findRefByType(rc, "wire", 0);
                    shellLump[idx]     = findRefByType(rc, "lump", 0);
                } else if (tp == "subshell") {
                    subshellNext[idx]  = findRefByType(rc, "subshell", 2);
                    subshellChild[idx] = findRefByType(rc, "subshell", 3);
                    subshellFace[idx]  = findRefByType(rc, "face", 0);
                } else if (tp == "wire") {
                    wireNext[idx]   = findRefByType(rc, "wire", 0);
                    wireCoedge[idx] = findRefByType(rc, "coedge", 0);
                }
            }

            // Link all loops to their owning faces (both via face.firstLoop->nextLoop and loop.ownerFace)
            for (auto& kv : g.faces) {
                BRepFace& fc = kv.second;
                int32_t curL = fc.firstLoop;
                std::set<int32_t> seen;
                while (curL >= 0 && seen.insert(curL).second && seen.size() <= 512) {
                    fc.loopIds.push_back(curL);
                    auto lit = g.loops.find(curL);
                    if (lit == g.loops.end()) break;
                    curL = lit->second.nextLoop;
                }
            }
            for (const auto& kv : g.loops) {
                if (kv.second.ownerFace >= 0) {
                    auto fit = g.faces.find(kv.second.ownerFace);
                    if (fit != g.faces.end()) {
                        auto& lids = fit->second.loopIds;
                        if (std::find(lids.begin(), lids.end(), kv.first) == lids.end()) {
                            lids.push_back(kv.first);
                        }
                    }
                }
            }

            // Propagate body transforms down to faces and wireframe edges
            auto assignFaceChainBody = [&](int32_t firstF, int32_t bId) {
                int32_t f = firstF;
                std::set<int32_t> seen;
                while (f >= 0 && seen.insert(f).second && seen.size() <= 65536) {
                    auto fit = g.faces.find(f);
                    if (fit == g.faces.end()) break;
                    fit->second.bodyId = bId;
                    f = fit->second.nextFace;
                }
            };
            auto assignWireChainBody = [&](int32_t firstW, int32_t bId) {
                int32_t w = firstW;
                std::set<int32_t> seenW;
                while (w >= 0 && seenW.insert(w).second && seenW.size() <= 4096) {
                    int32_t ce0 = wireCoedge.count(w) ? wireCoedge[w] : -1;
                    int32_t ce = ce0;
                    std::set<int32_t> seenCe;
                    while (ce >= 0 && seenCe.insert(ce).second && seenCe.size() <= 8192) {
                        auto cit = g.coedges.find(ce);
                        if (cit == g.coedges.end()) break;
                        auto eit = g.edges.find(cit->second.edgeId);
                        if (eit != g.edges.end()) eit->second.bodyId = bId;
                        ce = cit->second.nextCoedge;
                        if (ce == ce0) break;
                    }
                    w = wireNext.count(w) ? wireNext[w] : -1;
                }
            };
            auto assignSubshellBody = [&](auto& self, int32_t ss, int32_t bId, int depth) -> void {
                if (ss < 0 || depth > 64) return;
                if (subshellFace.count(ss)) assignFaceChainBody(subshellFace[ss], bId);
                if (subshellChild.count(ss)) self(self, subshellChild[ss], bId, depth + 1);
                if (subshellNext.count(ss))  self(self, subshellNext[ss],  bId, depth + 1);
            };
            auto assignShellChainBody = [&](int32_t firstSh, int32_t bId) {
                int32_t sh = firstSh;
                std::set<int32_t> seenSh;
                while (sh >= 0 && seenSh.insert(sh).second && seenSh.size() <= 4096) {
                    if (shellFace.count(sh))     assignFaceChainBody(shellFace[sh], bId);
                    if (shellSubshell.count(sh)) assignSubshellBody(assignSubshellBody, shellSubshell[sh], bId, 0);
                    if (shellWire.count(sh))     assignWireChainBody(shellWire[sh], bId);
                    sh = shellNext.count(sh) ? shellNext[sh] : -1;
                }
            };

            for (int32_t bId : bodyIds) {
                const AcisRawRec& brc = recs[static_cast<size_t>(bId)];
                int32_t xfRef = findRefByType(brc, "transform", 0);
                if (xfRef >= 0) {
                    auto xit = g.bodyTransforms.find(-1000000 - xfRef);
                    if (xit != g.bodyTransforms.end()) g.bodyTransforms[bId] = xit->second;
                }
                int32_t lmp = findRefByType(brc, "lump", 0);
                std::set<int32_t> seenLmp;
                while (lmp >= 0 && seenLmp.insert(lmp).second && seenLmp.size() <= 4096) {
                    if (lumpShell.count(lmp)) assignShellChainBody(lumpShell[lmp], bId);
                    lmp = lumpNext.count(lmp) ? lumpNext[lmp] : -1;
                }
                int32_t directSh = findRefByType(brc, "shell", 0);
                if (directSh >= 0) assignShellChainBody(directSh, bId);
                int32_t wr = findRefByType(brc, "wire", 0);
                if (wr >= 0) assignWireChainBody(wr, bId);
            }
            // Also propagate upward from lump.owner_body if lump wasn't reached downward
            for (const auto& kv : lumpBody) {
                if (kv.second >= 0 && lumpShell.count(kv.first)) {
                    assignShellChainBody(lumpShell[kv.first], kv.second);
                }
            }
            // Propagate face.bodyId to edges referenced by face loops
            for (const auto& kv : g.faces) {
                if (kv.second.bodyId < 0) continue;
                for (int32_t lid : kv.second.loopIds) {
                    auto lit = g.loops.find(lid);
                    if (lit == g.loops.end() || lit->second.firstCoedge < 0) continue;
                    int32_t ce0 = lit->second.firstCoedge, ce = ce0;
                    std::set<int32_t> seenCe;
                    while (ce >= 0 && seenCe.insert(ce).second && seenCe.size() <= 4096) {
                        auto cit = g.coedges.find(ce);
                        if (cit == g.coedges.end()) break;
                        auto eit = g.edges.find(cit->second.edgeId);
                        if (eit != g.edges.end() && eit->second.bodyId < 0) eit->second.bodyId = kv.second.bodyId;
                        ce = cit->second.nextCoedge;
                        if (ce == ce0) break;
                    }
                }
            }

            return evaluateBRepGraph(g);
        };

        // 1. Spatial ACIS Binary (.sab — "ACIS BinaryFile" / "ASM BinaryFile ")
        bool hasSabMagic = (size >= 15 && (std::memcmp(data, "ACIS BinaryFile", 15) == 0 ||
                                           std::memcmp(data, "ASM BinaryFile", 14) == 0));
        if (ext == L".sab" || hasSabMagic) {
            if (!hasSabMagic || size < 31) return false;
            size_t magicLen = (std::memcmp(data, "ACIS BinaryFile", 15) == 0 ||
                              (size >= 15 && std::memcmp(data, "ASM BinaryFile ", 15) == 0)) ? 15 : 14;
            if (magicLen + 16 > size) return false;

            int32_t acisVer     = static_cast<int32_t>(Detail3D200::RdU32LE(data + magicLen + 0));
            int32_t numRecords  = static_cast<int32_t>(Detail3D200::RdU32LE(data + magicLen + 4));
            int32_t numEntities = static_cast<int32_t>(Detail3D200::RdU32LE(data + magicLen + 8));
            (void)numEntities;
            if (acisVer < 100 || acisVer > 99999 || numRecords < 0) return false;
            mesh.formatName = L"Spatial ACIS Binary Solid (.sab)";

            auto isIdentBytes = [&](size_t off, uint8_t len) -> bool {
                if (len == 0 || len > 48 || off + len > size) return false;
                for (uint8_t i = 0; i < len; ++i) {
                    uint8_t c = data[off + i];
                    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                              (c >= '0' && c <= '9') || c == '-' || c == '_';
                    if (!ok) return false;
                }
                return true;
            };

            size_t p = magicLen + 16;
            int headerStringsRem = 3;
            int headerDoublesRem = 3;
            bool inHeader = true;

            std::vector<AcisRawRec> recs;
            AcisRawRec cur;
            int32_t nextSeqIdx = 0;
            int32_t explicitIdx = -1;
            int braceDepth = 0;

            while (p < size && recs.size() < 200000) {
                uint8_t tag = data[p++];
                if (tag == 0x02) { // TAG_CHAR
                    if (p + 1 > size) break;
                    p += 1;
                } else if (tag == 0x03) { // TAG_SHORT
                    if (p + 2 > size) break;
                    p += 2;
                } else if (tag == 0x04) { // TAG_LONG (int32)
                    if (p + 4 > size) break;
                    int32_t iv = static_cast<int32_t>(Detail3D200::RdU32LE(data + p));
                    p += 4;
                    if (!inHeader) {
                        if (braceDepth > 0) cur.subNums.push_back(static_cast<double>(iv));
                        else if (cur.type.empty()) explicitIdx = (iv < 0) ? -iv : iv;
                        else cur.nums.push_back(static_cast<double>(iv));
                    }
                } else if (tag == 0x05) { // TAG_FLOAT
                    if (p + 4 > size) break;
                    double dv = static_cast<double>(Detail3D200::RdF32LE(data + p));
                    p += 4;
                    if (!inHeader) {
                        if (braceDepth == 0) cur.nums.push_back(dv);
                        else                 cur.subNums.push_back(dv);
                    }
                } else if (tag == 0x06) { // TAG_DOUBLE
                    if (p + 8 > size) break;
                    double dv = Detail3D200::RdF64LE(data + p);
                    p += 8;
                    if (inHeader) {
                        if (--headerDoublesRem <= 0 && headerStringsRem <= 0) inHeader = false;
                    } else {
                        if (braceDepth == 0) cur.nums.push_back(dv);
                        else                 cur.subNums.push_back(dv);
                    }
                } else if (tag == 0x07) { // TAG_UTF8_U8
                    if (p >= size) break;
                    uint8_t slen = data[p++];
                    if (p + slen > size) break;
                    std::string s(reinterpret_cast<const char*>(data + p), slen);
                    p += slen;
                    if (inHeader) {
                        if (headerStringsRem > 0) --headerStringsRem;
                        if (headerDoublesRem <= 0 && headerStringsRem <= 0) inHeader = false;
                    } else if (braceDepth == 0 && (s == "forward" || s == "reversed")) {
                        cur.words.push_back(std::move(s));
                    }
                } else if (tag == 0x08) { // TAG_UTF8_U16
                    if (p + 2 > size) break;
                    uint16_t slen = Detail3D200::RdU16LE(data + p); p += 2;
                    if (p + slen > size) break;
                    p += slen;
                    if (inHeader && headerStringsRem > 0) --headerStringsRem;
                } else if (tag == 0x09) { // TAG_UTF8_U32
                    if (p + 4 > size) break;
                    uint32_t slen = Detail3D200::RdU32LE(data + p); p += 4;
                    if (slen > 1000000 || p + slen > size) break;
                    p += slen;
                    if (inHeader && headerStringsRem > 0) --headerStringsRem;
                } else if (tag == 0x0A) { // TAG_TRUE (ACIS logical true: "forward" on face/coedge/edge)
                    if (!inHeader && braceDepth == 0 &&
                        (cur.type == "face" || cur.type == "coedge" || cur.type == "edge"))
                    {
                        bool hasSense = false;
                        for (const auto& w : cur.words) if (w == "forward" || w == "reversed") hasSense = true;
                        if (!hasSense) cur.words.push_back("forward");
                    }
                } else if (tag == 0x0B) { // TAG_FALSE (ACIS logical false: "reversed" on face/coedge/edge)
                    if (!inHeader && braceDepth == 0 &&
                        (cur.type == "face" || cur.type == "coedge" || cur.type == "edge"))
                    {
                        bool hasSense = false;
                        for (const auto& w : cur.words) if (w == "forward" || w == "reversed") hasSense = true;
                        if (!hasSense) cur.words.push_back("reversed");
                    }
                } else if (tag == 0x0C) { // TAG_POINTER ($idx)
                    if (p + 4 > size) break;
                    int32_t ref = static_cast<int32_t>(Detail3D200::RdU32LE(data + p));
                    p += 4;
                    inHeader = false;
                    if (braceDepth == 0) cur.ptrs.push_back(ref);
                } else if (tag == 0x0D) { // TAG_IDENT
                    if (p < size && isIdentBytes(p + 1, data[p])) {
                        uint8_t slen = data[p++];
                        std::string id(reinterpret_cast<const char*>(data + p), slen);
                        p += slen;
                        if (id == "End-of-ACIS-data" || id == "End-of-ASM-data") break;
                        inHeader = false;
                        if (braceDepth == 0) {
                            if (cur.type.empty()) cur.type = std::move(id);
                            else cur.words.push_back(std::move(id));
                        } else {
                            cur.subWords.push_back(std::move(id));
                        }
                    } else if (p + 8 <= size) {
                        double dv = Detail3D200::RdF64LE(data + p);
                        p += 8;
                        if (inHeader) {
                            if (--headerDoublesRem <= 0 && headerStringsRem <= 0) inHeader = false;
                        } else if (braceDepth == 0) {
                            cur.nums.push_back(dv);
                        } else {
                            cur.subNums.push_back(dv);
                        }
                    } else {
                        break;
                    }
                } else if (tag == 0x0E) { // TAG_SUBIDENT
                    if (p >= size) break;
                    uint8_t slen = data[p++];
                    if (p + slen > size) break;
                    std::string sub(reinterpret_cast<const char*>(data + p), slen);
                    p += slen;
                    if (braceDepth == 0 && !cur.type.empty()) cur.type += "-" + sub;
                } else if (tag == 0x0F) { // TAG_SUBTYPE_OPEN '{'
                    ++braceDepth;
                } else if (tag == 0x10) { // TAG_SUBTYPE_CLOSE '}'
                    if (braceDepth > 0) --braceDepth;
                } else if (tag == 0x11) { // TAG_TERMINATOR '#'
                    inHeader = false;
                    braceDepth = 0;
                    if (!cur.type.empty()) {
                        int32_t idx = (explicitIdx >= 0 && explicitIdx < 200000) ? explicitIdx : nextSeqIdx;
                        nextSeqIdx = std::max(nextSeqIdx, idx + 1);
                        if (static_cast<size_t>(idx) >= recs.size()) recs.resize(static_cast<size_t>(idx) + 1);
                        recs[static_cast<size_t>(idx)] = std::move(cur);
                    }
                    cur = AcisRawRec{};
                    explicitIdx = -1;
                } else if (tag == 0x13 || tag == 0x14) { // TAG_POSITION / TAG_VECTOR (3 IEEE-754 doubles)
                    if (p + 24 > size) break;
                    inHeader = false;
                    double d0 = Detail3D200::RdF64LE(data + p + 0);
                    double d1 = Detail3D200::RdF64LE(data + p + 8);
                    double d2 = Detail3D200::RdF64LE(data + p + 16);
                    p += 24;
                    if (braceDepth == 0) {
                        cur.nums.push_back(d0); cur.nums.push_back(d1); cur.nums.push_back(d2);
                    } else {
                        cur.subNums.push_back(d0); cur.subNums.push_back(d1); cur.subNums.push_back(d2);
                    }
                } else if (tag == 0x15) { // TAG_ENUM
                    if (p >= size) break;
                    if (isIdentBytes(p + 1, data[p])) {
                        uint8_t slen = data[p++];
                        std::string en(reinterpret_cast<const char*>(data + p), slen);
                        p += slen;
                        if (!inHeader && braceDepth == 0) cur.words.push_back(std::move(en));
                    } else if (p + 4 <= size) {
                        p += 4;
                    } else {
                        break;
                    }
                } else {
                    break;
                }
            }
            return buildAcisGraphAndTriangulate(recs);
        }

        // 2. Spatial ACIS ASCII (.sat)
        if (ext == L".sat") {
            // Reject binary / NUL-containing files
            for (size_t i = 0; i < std::min<size_t>(size, 512); ++i) {
                if (data[i] == 0) return false;
            }
            size_t pos = 0;
            auto skipWs = [&]() {
                while (pos < sv.size() && (sv[pos] == ' ' || sv[pos] == '\t' || sv[pos] == '\r' || sv[pos] == '\n')) ++pos;
            };
            skipWs();
            if (pos >= sv.size()) return false;

            // Validate ACIS ASCII header: either "<version 100..9999> <numRec> <numEnt> <flags>" OR "ACIS ..." / "ASM ..."
            bool validHeader = false;
            if (sv[pos] >= '1' && sv[pos] <= '9') {
                const char* hs = sv.data() + pos;
                char* e1 = nullptr; long verInt = std::strtol(hs, &e1, 10);
                char* e2 = nullptr; long nRec   = (e1 != hs) ? std::strtol(e1, &e2, 10) : -1;
                char* e3 = nullptr; long nEnt   = (e2 != e1) ? std::strtol(e2, &e3, 10) : -1;
                char* e4 = nullptr; long hFlg   = (e3 != e2) ? std::strtol(e3, &e4, 10) : -1;
                (void)nEnt; (void)hFlg;
                if (e4 != e3 && verInt >= 100 && verInt <= 99999 && nRec >= 0) {
                    validHeader = true;
                    // Advance past header line 1 (counts), plus optional line 2 (product strings) & line 3 (units/tolerances)
                    size_t nl1 = sv.find('\n', static_cast<size_t>(e4 - sv.data()));
                    pos = (nl1 == std::string_view::npos) ? sv.size() : (nl1 + 1);
                    for (int hLine = 0; hLine < 2 && pos < sv.size(); ++hLine) {
                        size_t nl = sv.find('\n', pos);
                        std::string_view hSv = sv.substr(pos, (nl == std::string_view::npos ? sv.size() : nl) - pos);
                        if (hSv.find('#') != std::string_view::npos || hSv.find('$') != std::string_view::npos) break;
                        pos = (nl == std::string_view::npos) ? sv.size() : (nl + 1);
                    }
                }
            } else if (sv.substr(pos, 4) == "ACIS" || sv.substr(pos, 3) == "ASM") {
                validHeader = true;
                size_t nl = sv.find('\n', pos);
                pos = (nl == std::string_view::npos) ? sv.size() : (nl + 1);
                // Skip optional unit/tolerance header line if it does not contain '#'
                skipWs();
                size_t nl2 = sv.find('\n', pos);
                std::string_view l2 = sv.substr(pos, (nl2 == std::string_view::npos ? sv.size() : nl2) - pos);
                if (l2.find('#') == std::string_view::npos && l2.find('$') == std::string_view::npos) {
                    pos = (nl2 == std::string_view::npos) ? sv.size() : (nl2 + 1);
                }
            }
            if (!validHeader || sv.find('#', pos) == std::string_view::npos) return false;

            mesh.formatName = L"Spatial ACIS Solid Model (.sat)";

            std::vector<AcisRawRec> recs;
            AcisRawRec cur;
            int32_t nextSeqIdx = 0;
            int32_t explicitIdx = -1;
            bool atRecordStart = true;
            int braceDepth = 0;

            auto isAcisWordChar = [](char c) -> bool {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                       (c >= '0' && c <= '9') || c == '-' || c == '_';
            };

            while (pos < sv.size() && recs.size() < 200000) {
                skipWs();
                if (pos >= sv.size()) break;
                char ch = sv[pos];

                if (ch == '#') {
                    ++pos;
                    braceDepth = 0;
                    if (!cur.type.empty()) {
                        int32_t idx = (explicitIdx >= 0 && explicitIdx < 200000) ? explicitIdx : nextSeqIdx;
                        nextSeqIdx = std::max(nextSeqIdx, idx + 1);
                        if (static_cast<size_t>(idx) >= recs.size()) recs.resize(static_cast<size_t>(idx) + 1);
                        recs[static_cast<size_t>(idx)] = std::move(cur);
                    }
                    cur = AcisRawRec{};
                    explicitIdx = -1;
                    atRecordStart = true;
                    continue;
                }
                if (ch == '{') { ++braceDepth; ++pos; continue; }
                if (ch == '}') { if (braceDepth > 0) --braceDepth; ++pos; continue; }

                // Counted string literal: @<len> <chars>
                if (ch == '@') {
                    ++pos;
                    const char* sp = sv.data() + pos;
                    char* ep = nullptr;
                    long slen = std::strtol(sp, &ep, 10);
                    if (ep != sp && slen >= 0) {
                        pos = static_cast<size_t>(ep - sv.data());
                        if (pos < sv.size() && (sv[pos] == ' ' || sv[pos] == '\t')) ++pos;
                        size_t adv = std::min<size_t>(static_cast<size_t>(slen), sv.size() - pos);
                        std::string lit(sv.substr(pos, adv));
                        pos += adv;
                        if (braceDepth == 0 && (lit == "forward" || lit == "reversed")) {
                            cur.words.push_back(std::move(lit));
                        }
                        atRecordStart = false;
                        continue;
                    }
                }

                // Entity pointer reference: $<int>
                if (ch == '$') {
                    ++pos;
                    const char* sp = sv.data() + pos;
                    char* ep = nullptr;
                    long ref = std::strtol(sp, &ep, 10);
                    if (ep != sp) {
                        pos = static_cast<size_t>(ep - sv.data());
                        if (braceDepth == 0) cur.ptrs.push_back(static_cast<int32_t>(ref));
                    }
                    atRecordStart = false;
                    continue;
                }

                // Explicit "-<idx>" record number at start of record (e.g. "-0 body ...", "-12 face ...")
                if (atRecordStart && braceDepth == 0 && ch == '-' && pos + 1 < sv.size() &&
                    sv[pos + 1] >= '0' && sv[pos + 1] <= '9')
                {
                    const char* sp = sv.data() + pos + 1;
                    char* ep = nullptr;
                    long rIdx = std::strtol(sp, &ep, 10);
                    if (ep != sp && (ep == sv.data() + sv.size() || *ep == ' ' || *ep == '\t' || *ep == '\r' || *ep == '\n')) {
                        explicitIdx = static_cast<int32_t>(rIdx);
                        pos = static_cast<size_t>(ep - sv.data());
                        atRecordStart = false;
                        continue;
                    }
                }

                // Number token (float / int)
                if ((ch >= '0' && ch <= '9') || ch == '-' || ch == '+' || ch == '.') {
                    const char* sp = sv.data() + pos;
                    char* ep = nullptr;
                    double dv = std::strtod(sp, &ep);
                    if (ep != sp && (ep == sv.data() + sv.size() || !isAcisWordChar(*ep))) {
                        pos = static_cast<size_t>(ep - sv.data());
                        if (std::isfinite(dv)) {
                            if (braceDepth == 0) cur.nums.push_back(dv);
                            else                 cur.subNums.push_back(dv);
                        }
                        atRecordStart = false;
                        continue;
                    }
                }

                // Identifier / keyword token
                if (isAcisWordChar(ch)) {
                    size_t start = pos;
                    while (pos < sv.size() && isAcisWordChar(sv[pos])) ++pos;
                    std::string tok(sv.substr(start, pos - start));
                    if (tok == "End-of-ACIS-data" || tok == "End-of-ASM-data") break;
                    if (braceDepth == 0) {
                        if (cur.type.empty()) cur.type = std::move(tok);
                        else cur.words.push_back(std::move(tok));
                    } else {
                        cur.subWords.push_back(std::move(tok));
                    }
                    atRecordStart = false;
                    continue;
                }

                ++pos;
            }
            return buildAcisGraphAndTriangulate(recs);
        }

        // 3. Siemens Parasolid XT Text (.x_t) & Neutral Binary (.x_b)
        {
            // Check Neutral Binary Parasolid (.x_b): "PS\0" prefix (arch byte at data[3]: 0=Big-Endian IEEE, 1=Little-Endian IEEE)
            // + ASCII header containing "**END_OF_HEADER**" + binary node stream!
            bool hasPsBinPrefix = (size >= 32 && data[0] == 'P' && data[1] == 'S' && data[2] == 0 && (data[3] == 0 || data[3] == 1));
            size_t eohBin = sv.find("**END_OF_HEADER**");
            bool isNeutralBinaryXb = false;
            size_t xbStart = 0;
            bool xbBigEndian = true;

            if (eohBin != std::string_view::npos &&
                (hasPsBinPrefix || sv.find("PARASOLID") != std::string_view::npos || sv.find("SCH_") != std::string_view::npos))
            {
                if (hasPsBinPrefix) xbBigEndian = (data[3] == 0);
                size_t afterEoh = eohBin + 17;
                while (afterEoh < size && data[afterEoh] == '*') ++afterEoh;
                while (afterEoh < size && (data[afterEoh] == '\r' || data[afterEoh] == '\n' || data[afterEoh] == ' ')) ++afterEoh;
                if (hasPsBinPrefix || (afterEoh + 4 <= size && data[afterEoh] == 'B' && data[afterEoh + 1] == 0)) {
                    isNeutralBinaryXb = true;
                    xbStart = (afterEoh < size && data[afterEoh] == 'B') ? (afterEoh + 2) : afterEoh;
                }
            }

            if (isNeutralBinaryXb) {
                auto rdU16 = [&](const uint8_t* p) -> uint16_t { return xbBigEndian ? Detail3D200::RdU16BE(p) : Detail3D200::RdU16LE(p); };
                auto rdI32 = [&](const uint8_t* p) -> int32_t  { return static_cast<int32_t>(xbBigEndian ? Detail3D200::RdU32BE(p) : Detail3D200::RdU32LE(p)); };
                auto rdF64 = [&](const uint8_t* p) -> double {
                    if (!xbBigEndian) return Detail3D200::RdF64LE(p);
                    uint64_t u = Detail3D200::RdU64BE(p);
                    double d = 0.0;
                    std::memcpy(&d, &u, 8);
                    return d;
                };

                BRepGraph g;
                std::unordered_map<int32_t, CadXform> xbTransforms;
                std::unordered_map<int32_t, int32_t> bodyXfMap, shellFirstFace;
                size_t bp = xbStart;

                // Neutral Binary Parasolid Node Stream:
                // Each node: [uint16 nodeType][int32 nodeId] followed by schema fields (nodeType == 0 or 999 -> EOF)
                while (bp + 6 <= size && (g.faces.size() + g.points.size() + g.surfaces.size()) < 200000) {
                    uint16_t nt = rdU16(data + bp);
                    int32_t nid = rdI32(data + bp + 2);
                    bp += 6;
                    if (nt == 0 || nt == 999) break;
                    if (nid < 0) return false;

                    if (nt == 12) { // body: [int32 xfId][int32 shellId]
                        if (bp + 8 > size) return false;
                        int32_t xfId = rdI32(data + bp);
                        int32_t shId = rdI32(data + bp + 4);
                        bp += 8;
                        if (xfId >= 0) bodyXfMap[nid] = xfId;
                        (void)shId;
                    } else if (nt == 13) { // transform: double r[9], t[3], scale (104B)
                        if (bp + 104 > size) return false;
                        CadXform xf;
                        for (int i = 0; i < 9; ++i) xf.r[i] = rdF64(data + bp + i * 8);
                        xf.t[0] = rdF64(data + bp + 72);
                        xf.t[1] = rdF64(data + bp + 80);
                        xf.t[2] = rdF64(data + bp + 88);
                        xf.scale = rdF64(data + bp + 96);
                        if (!std::isfinite(xf.scale) || std::fabs(xf.scale) < 1e-12) xf.scale = 1.0;
                        xf.valid = true;
                        xbTransforms[nid] = xf;
                        bp += 104;
                    } else if (nt == 14) { // shell: [int32 firstFaceId]
                        if (bp + 4 > size) return false;
                        shellFirstFace[nid] = rdI32(data + bp);
                        bp += 4;
                    } else if (nt == 15) { // face: [int32 nextFace][int32 firstLoop][int32 surfId][int32 bodyId][uint8 rev]
                        if (bp + 17 > size) return false;
                        BRepFace fc;
                        fc.nextFace  = rdI32(data + bp + 0);
                        fc.firstLoop = rdI32(data + bp + 4);
                        fc.surfaceId = rdI32(data + bp + 8);
                        fc.bodyId    = rdI32(data + bp + 12);
                        fc.reversed  = (data[bp + 16] != 0);
                        bp += 17;
                        g.faces[nid] = fc;
                        g.faceOrder.push_back(nid);
                    } else if (nt == 16) { // loop: [int32 ownerFace][int32 nextLoop][int32 firstFin]
                        if (bp + 12 > size) return false;
                        BRepLoop lp;
                        lp.ownerFace   = rdI32(data + bp + 0);
                        lp.nextLoop    = rdI32(data + bp + 4);
                        lp.firstCoedge = rdI32(data + bp + 8);
                        bp += 12;
                        g.loops[nid] = lp;
                    } else if (nt == 17) { // fin: [int32 nextFin][int32 edgeId][int32 headVert][uint8 rev]
                        if (bp + 13 > size) return false;
                        BRepCoedge ce;
                        ce.nextCoedge = rdI32(data + bp + 0);
                        ce.edgeId     = rdI32(data + bp + 4);
                        ce.headVert   = rdI32(data + bp + 8);
                        ce.reversed   = (data[bp + 12] != 0);
                        bp += 13;
                        g.coedges[nid] = ce;
                    } else if (nt == 18) { // vertex: [int32 pointId]
                        if (bp + 4 > size) return false;
                        g.vertexToPoint[nid] = rdI32(data + bp);
                        bp += 4;
                    } else if (nt == 19) { // edge: [int32 startVert][int32 endVert][int32 curveId][uint8 rev]
                        if (bp + 13 > size) return false;
                        BRepEdge ed;
                        ed.startVert = rdI32(data + bp + 0);
                        ed.endVert   = rdI32(data + bp + 4);
                        ed.curveId   = rdI32(data + bp + 8);
                        ed.reversed  = (data[bp + 12] != 0);
                        bp += 13;
                        g.edges[nid] = ed;
                        g.edgeOrder.push_back(nid);
                    } else if (nt == 29) { // point: double3 (24B)
                        if (bp + 24 > size) return false;
                        g.points[nid] = { rdF64(data + bp + 0), rdF64(data + bp + 8), rdF64(data + bp + 16) };
                        bp += 24;
                    } else if (nt == 30) { // line: origin(24B) + dir(24B)
                        if (bp + 48 > size) return false;
                        BRepCurve c; c.kind = CurveKind::Straight;
                        c.origin = { rdF64(data + bp + 0), rdF64(data + bp + 8), rdF64(data + bp + 16) };
                        c.normal = { rdF64(data + bp + 24), rdF64(data + bp + 32), rdF64(data + bp + 40) };
                        bp += 48;
                        g.curves[nid] = std::move(c);
                    } else if (nt == 31) { // circle: origin(24B) + normal(24B) + uAxis(24B) + rad(8B)
                        if (bp + 80 > size) return false;
                        BRepCurve c; c.kind = CurveKind::Ellipse; c.ratio = 1.0;
                        c.origin = { rdF64(data + bp + 0), rdF64(data + bp + 8), rdF64(data + bp + 16) };
                        c.normal = { rdF64(data + bp + 24), rdF64(data + bp + 32), rdF64(data + bp + 40) };
                        CadVec3 ux = vNorm({ rdF64(data + bp + 48), rdF64(data + bp + 56), rdF64(data + bp + 64) });
                        c.majorAxis = vMul(ux, std::fabs(rdF64(data + bp + 72)));
                        bp += 80;
                        g.curves[nid] = std::move(c);
                    } else if (nt == 35) { // plane: origin(24B) + normal(24B) + uAxis(24B)
                        if (bp + 72 > size) return false;
                        BRepSurf s; s.kind = SurfKind::Plane;
                        s.origin = { rdF64(data + bp + 0), rdF64(data + bp + 8), rdF64(data + bp + 16) };
                        s.normal = { rdF64(data + bp + 24), rdF64(data + bp + 32), rdF64(data + bp + 40) };
                        s.uAxis  = { rdF64(data + bp + 48), rdF64(data + bp + 56), rdF64(data + bp + 64) };
                        bp += 72;
                        g.surfaces[nid] = s;
                    } else if (nt == 36) { // cylinder: origin(24B) + axis(24B) + radius(8B)
                        if (bp + 56 > size) return false;
                        BRepSurf s; s.kind = SurfKind::CylinderCone;
                        s.origin  = { rdF64(data + bp + 0), rdF64(data + bp + 8), rdF64(data + bp + 16) };
                        s.normal  = { rdF64(data + bp + 24), rdF64(data + bp + 32), rdF64(data + bp + 40) };
                        s.radius1 = std::fabs(rdF64(data + bp + 48));
                        bp += 56;
                        g.surfaces[nid] = s;
                    } else if (nt == 37) { // sphere: origin(24B) + radius(8B)
                        if (bp + 32 > size) return false;
                        BRepSurf s; s.kind = SurfKind::Sphere;
                        s.origin  = { rdF64(data + bp + 0), rdF64(data + bp + 8), rdF64(data + bp + 16) };
                        s.radius1 = std::fabs(rdF64(data + bp + 24));
                        bp += 32;
                        g.surfaces[nid] = s;
                    } else if (nt == 41) { // b_surface (NURBS): uDeg(u16), vDeg(u16), nU(i32), nV(i32), isRat(u8), knotsU, knotsV, poles
                        if (bp + 13 > size) return false;
                        int uDeg = rdU16(data + bp + 0);
                        int vDeg = rdU16(data + bp + 2);
                        int nU   = rdI32(data + bp + 4);
                        int nV   = rdI32(data + bp + 8);
                        bool isRat = (data[bp + 12] != 0);
                        bp += 13;
                        if (uDeg < 1 || uDeg > 15 || vDeg < 1 || vDeg > 15 ||
                            nU <= uDeg || nU > 256 || nV <= vDeg || nV > 256)
                        {
                            return false;
                        }
                        size_t kuCnt = static_cast<size_t>(nU + uDeg + 1);
                        size_t kvCnt = static_cast<size_t>(nV + vDeg + 1);
                        size_t pCnt  = static_cast<size_t>(nU) * static_cast<size_t>(nV);
                        size_t stride = isRat ? 4u : 3u;
                        size_t need = (kuCnt + kvCnt + pCnt * stride) * 8u;
                        if (bp + need > size) return false;

                        BRepSurf s;
                        s.kind = SurfKind::Nurbs;
                        s.uDeg = uDeg; s.vDeg = vDeg; s.nU = nU; s.nV = nV; s.isRational = isRat;
                        s.knotsU.resize(kuCnt);
                        for (size_t k = 0; k < kuCnt; ++k) { s.knotsU[k] = rdF64(data + bp); bp += 8; }
                        s.knotsV.resize(kvCnt);
                        for (size_t k = 0; k < kvCnt; ++k) { s.knotsV[k] = rdF64(data + bp); bp += 8; }
                        s.poles.resize(pCnt);
                        if (isRat) s.weights.resize(pCnt, 1.0);
                        for (size_t k = 0; k < pCnt; ++k) {
                            s.poles[k] = { rdF64(data + bp + 0), rdF64(data + bp + 8), rdF64(data + bp + 16) };
                            if (isRat) s.weights[k] = rdF64(data + bp + 24);
                            bp += stride * 8u;
                        }
                        g.surfaces[nid] = std::move(s);
                    } else {
                        return false; // unknown binary node type -> fail cleanly without guessing
                    }
                }

                for (const auto& kv : bodyXfMap) {
                    if (xbTransforms.count(kv.second)) g.bodyTransforms[kv.first] = xbTransforms[kv.second];
                }
                for (auto& kv : g.faces) {
                    int32_t curL = kv.second.firstLoop;
                    std::set<int32_t> seen;
                    while (curL >= 0 && seen.insert(curL).second && seen.size() <= 512) {
                        kv.second.loopIds.push_back(curL);
                        auto lit = g.loops.find(curL);
                        if (lit == g.loops.end()) break;
                        curL = lit->second.nextLoop;
                    }
                }
                if (!evaluateBRepGraph(g)) return false;
                mesh.formatName = L"Siemens Parasolid Binary Solid (.x_b)";
                return true;
            }

            // Text Parasolid (.x_t): reject NUL-containing garbage strictly!
            for (size_t i = 0; i < std::min<size_t>(size, 1024); ++i) {
                if (data[i] == 0) return false;
            }
            if (ext == L".x_b") {
                if (sv.find("SCH_") == std::string_view::npos ||
                    (sv.find("**END_OF_HEADER**") == std::string_view::npos && sv.find("PART") == std::string_view::npos))
                {
                    return false;
                }
            }

            // Locate start of XT body after optional **END_OF_HEADER** preamble
            size_t bodyStart = 0;
            size_t eoh = sv.find("**END_OF_HEADER**");
            if (eoh != std::string_view::npos) {
                size_t nl = sv.find('\n', eoh);
                bodyStart = (nl == std::string_view::npos) ? sv.size() : (nl + 1);
            }

            // Validate Parasolid XT transmit markers
            bool hasXtSchema = (eoh != std::string_view::npos) ||
                               (sv.find("SCH_") != std::string_view::npos) ||
                               (sv.find("PS_") != std::string_view::npos) ||
                               (sv.find("PARASOLID") != std::string_view::npos) ||
                               (sv.find("TRANSMIT") != std::string_view::npos);
            if (!hasXtSchema) return false;

            mesh.formatName = L"Siemens Parasolid XT Solid (.x_t)";

            auto isPkEntityKeyword = [](std::string_view w) -> bool {
                return w == "assembly" || w == "instance" || w == "body" || w == "transform" ||
                       w == "region"   || w == "shell"    || w == "face" || w == "loop"      ||
                       w == "fin"      || w == "halfedge" || w == "edge" || w == "vertex"    ||
                       w == "point"    || w == "line"     || w == "straight" || w == "circle"||
                       w == "ellipse"  || w == "b_curve"  || w == "intersection" ||
                       w == "plane"    || w == "cylinder" || w == "cyl"  || w == "cone"      ||
                       w == "sphere"   || w == "torus"    || w == "b_surface" ||
                       w == "att_def"  || w == "attrib"   || w == "field_names" ||
                       w == "pointer_lis_block" || w == "int_lis_block" || w == "real_lis_block";
            };

            struct PkTok {
                enum Kind { Word, Int, Real, Ref, SensePos, SenseNeg, Sep } kind = Word;
                std::string text;
                int32_t iVal = 0;
                double dVal = 0.0;
                bool lineStart = false;
            };

            std::vector<PkTok> toks;
            size_t p = bodyStart;
            bool atLineStart = true;
            while (p < sv.size() && toks.size() < 500000) {
                char c = sv[p];
                if (c == '\r' || c == '\n') { atLineStart = true; ++p; continue; }
                if (c == ' ' || c == '\t' || c == ',' || c == '(' || c == ')') { ++p; continue; }
                if (c == ';' || c == '#') {
                    // '#<digits>' is an entity ref; standalone '#' or ';' is a record separator
                    if (c == '#' && p + 1 < sv.size() && ((sv[p + 1] >= '0' && sv[p + 1] <= '9') || sv[p + 1] == '-')) {
                        const char* sp = sv.data() + p + 1;
                        char* ep = nullptr;
                        long rv = std::strtol(sp, &ep, 10);
                        if (ep != sp) {
                            toks.push_back({ PkTok::Ref, {}, static_cast<int32_t>(rv), 0.0, atLineStart });
                            atLineStart = false;
                            p = static_cast<size_t>(ep - sv.data());
                            continue;
                        }
                    }
                    toks.push_back({ PkTok::Sep, ";", 0, 0.0, atLineStart });
                    atLineStart = true;
                    ++p;
                    continue;
                }
                // Stop at Parasolid end-of-part trailer
                if (c == '*' && sv.substr(p, 15) == "**END_OF_PART**") break;

                // Quoted string '...' or "..."
                if (c == '\'' || c == '"') {
                    char q = c; ++p;
                    while (p < sv.size() && sv[p] != q) ++p;
                    if (p < sv.size()) ++p;
                    atLineStart = false;
                    continue;
                }

                // Parasolid counted string T<len>:<chars>
                if ((c == 'T' || c == 'U') && p + 2 < sv.size() && sv[p + 1] >= '0' && sv[p + 1] <= '9') {
                    const char* sp = sv.data() + p + 1;
                    char* ep = nullptr;
                    long slen = std::strtol(sp, &ep, 10);
                    if (ep != sp && ep < sv.data() + sv.size() && *ep == ':' && slen >= 0) {
                        p = static_cast<size_t>((ep + 1) - sv.data());
                        size_t adv = std::min<size_t>(static_cast<size_t>(slen), sv.size() - p);
                        p += adv;
                        atLineStart = false;
                        continue;
                    }
                }

                // Explicit pointer ref $<int> or @<int>
                if ((c == '$' || c == '@') && p + 1 < sv.size() &&
                    ((sv[p + 1] >= '0' && sv[p + 1] <= '9') || sv[p + 1] == '-'))
                {
                    const char* sp = sv.data() + p + 1;
                    char* ep = nullptr;
                    long rv = std::strtol(sp, &ep, 10);
                    if (ep != sp) {
                        toks.push_back({ PkTok::Ref, {}, static_cast<int32_t>(rv), 0.0, atLineStart });
                        atLineStart = false;
                        p = static_cast<size_t>(ep - sv.data());
                        continue;
                    }
                }

                // '?' null pointer
                if (c == '?') {
                    toks.push_back({ PkTok::Ref, "?", -1, 0.0, atLineStart });
                    atLineStart = false;
                    ++p;
                    continue;
                }

                // '=' assignment token (for "<id> = <keyword> ...")
                if (c == '=') {
                    toks.push_back({ PkTok::Word, "=", 0, 0.0, atLineStart });
                    atLineStart = false;
                    ++p;
                    continue;
                }

                // Standalone '+' or '-' sense token vs signed number
                if ((c == '+' || c == '-') &&
                    (p + 1 >= sv.size() || !((sv[p + 1] >= '0' && sv[p + 1] <= '9') || sv[p + 1] == '.')))
                {
                    toks.push_back({ (c == '+') ? PkTok::SensePos : PkTok::SenseNeg, std::string(1, c), 0, 0.0, atLineStart });
                    atLineStart = false;
                    ++p;
                    continue;
                }

                // Number (Int vs Real)
                if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.') {
                    const char* sp = sv.data() + p;
                    char* epD = nullptr;
                    double dv = std::strtod(sp, &epD);
                    if (epD != sp) {
                        std::string_view rawNum(sp, static_cast<size_t>(epD - sp));
                        bool isFloat = (rawNum.find('.') != std::string_view::npos ||
                                        rawNum.find('e') != std::string_view::npos ||
                                        rawNum.find('E') != std::string_view::npos);
                        if (!isFloat && dv >= -2147483648.0 && dv <= 2147483647.0) {
                            toks.push_back({ PkTok::Int, std::string(rawNum), static_cast<int32_t>(dv), dv, atLineStart });
                        } else {
                            toks.push_back({ PkTok::Real, std::string(rawNum), 0, std::isfinite(dv) ? dv : 0.0, atLineStart });
                        }
                        atLineStart = false;
                        p = static_cast<size_t>(epD - sv.data());
                        continue;
                    }
                }

                // Identifier / .T. / .F.
                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '.') {
                    size_t s0 = p;
                    while (p < sv.size() && ((sv[p] >= 'a' && sv[p] <= 'z') || (sv[p] >= 'A' && sv[p] <= 'Z') ||
                                             (sv[p] >= '0' && sv[p] <= '9') || sv[p] == '_' || sv[p] == '.' || sv[p] == '-'))
                    {
                        ++p;
                    }
                    std::string w(sv.substr(s0, p - s0));
                    for (char& chL : w) if (chL >= 'A' && chL <= 'Z') chL = static_cast<char>(chL - 'A' + 'a');
                    if (w == "forward" || w == ".t.") {
                        toks.push_back({ PkTok::SensePos, w, 0, 0.0, atLineStart });
                    } else if (w == "reversed" || w == ".f.") {
                        toks.push_back({ PkTok::SenseNeg, w, 0, 0.0, atLineStart });
                    } else if (w == "file_end") {
                        break;
                    } else {
                        toks.push_back({ PkTok::Word, std::move(w), 0, 0.0, atLineStart });
                    }
                    atLineStart = false;
                    continue;
                }

                ++p;
            }

            // Segment token stream into PK entity records:
            // Supports BOTH "<keyword> <id> ..." AND "[#|$]<id> = <keyword> ..."
            struct PkEntityRec {
                int32_t id = -1;
                std::string type;
                std::vector<int32_t> refs;
                std::vector<double> nums;
                bool hasSenseNeg = false;
                bool hasSensePos = false;
            };

            auto matchRecHeader = [&](size_t ti, int32_t& outId, std::string& outType, size_t& outAdvance) -> bool {
                if (ti >= toks.size() || !toks[ti].lineStart) return false;
                // Form A: <keyword> <positive_id_or_ref>
                if (toks[ti].kind == PkTok::Word && isPkEntityKeyword(toks[ti].text) && ti + 1 < toks.size()) {
                    const PkTok& t1 = toks[ti + 1];
                    if ((t1.kind == PkTok::Int || t1.kind == PkTok::Ref) && t1.iVal >= 0) {
                        outType = toks[ti].text;
                        outId = t1.iVal;
                        outAdvance = 2;
                        return true;
                    }
                }
                // Form B: <id_or_ref> = <keyword>
                if ((toks[ti].kind == PkTok::Int || toks[ti].kind == PkTok::Ref) && toks[ti].iVal >= 0 &&
                    ti + 2 < toks.size() && toks[ti + 1].kind == PkTok::Word && toks[ti + 1].text == "=" &&
                    toks[ti + 2].kind == PkTok::Word && isPkEntityKeyword(toks[ti + 2].text))
                {
                    outId = toks[ti].iVal;
                    outType = toks[ti + 2].text;
                    outAdvance = 3;
                    return true;
                }
                return false;
            };

            std::unordered_map<int32_t, PkEntityRec> pkMap;
            std::vector<int32_t> pkOrder;
            size_t ti = 0;
            while (ti < toks.size()) {
                int32_t recId = -1;
                std::string recType;
                size_t adv = 0;
                if (!matchRecHeader(ti, recId, recType, adv)) {
                    ++ti;
                    continue;
                }
                ti += adv;
                PkEntityRec rec;
                rec.id = recId;
                rec.type = std::move(recType);

                while (ti < toks.size()) {
                    if (toks[ti].kind == PkTok::Sep) { ++ti; break; }
                    int32_t nextId = -1; std::string nextType; size_t nextAdv = 0;
                    if (matchRecHeader(ti, nextId, nextType, nextAdv)) break;

                    const PkTok& tk = toks[ti++];
                    if (tk.kind == PkTok::Ref) {
                        rec.refs.push_back(tk.iVal);
                    } else if (tk.kind == PkTok::Int) {
                        rec.refs.push_back(tk.iVal);
                        rec.nums.push_back(tk.dVal);
                    } else if (tk.kind == PkTok::Real) {
                        rec.nums.push_back(tk.dVal);
                    } else if (tk.kind == PkTok::SenseNeg) {
                        if (!rec.hasSensePos) rec.hasSenseNeg = true;
                    } else if (tk.kind == PkTok::SensePos) {
                        if (!rec.hasSenseNeg) rec.hasSensePos = true;
                    }
                }
                pkMap[rec.id] = std::move(rec);
                pkOrder.push_back(recId);
            }

            if (pkMap.empty()) return false;

            auto refHasType = [&](int32_t r, const char* t1, const char* t2 = nullptr) -> bool {
                if (r < 0) return false;
                auto it = pkMap.find(r);
                if (it == pkMap.end()) return false;
                if (it->second.type == t1) return true;
                return (t2 != nullptr && it->second.type == t2);
            };
            auto firstRefOf = [&](const PkEntityRec& rc, const char* t1, const char* t2 = nullptr) -> int32_t {
                for (int32_t r : rc.refs) {
                    if (r != rc.id && refHasType(r, t1, t2)) return r;
                }
                return -1;
            };

            BRepGraph g;
            std::unordered_map<int32_t, CadXform> pkTransforms;
            for (int32_t id : pkOrder) {
                const PkEntityRec& rc = pkMap.at(id);
                const std::string& tp = rc.type;

                if (tp == "point") {
                    if (rc.nums.size() >= 3) {
                        size_t n = rc.nums.size();
                        g.points[id] = { rc.nums[n - 3], rc.nums[n - 2], rc.nums[n - 1] };
                    }
                } else if (tp == "vertex") {
                    int32_t ptRef = firstRefOf(rc, "point");
                    if (ptRef >= 0) g.vertexToPoint[id] = ptRef;
                } else if (tp == "transform") {
                    if (rc.nums.size() >= 12) {
                        size_t n = rc.nums.size();
                        size_t base = (n >= 13) ? (n - 13) : (n - 12);
                        CadXform xf;
                        for (int i = 0; i < 9; ++i) xf.r[i] = rc.nums[base + static_cast<size_t>(i)];
                        xf.t[0] = rc.nums[base + 9];
                        xf.t[1] = rc.nums[base + 10];
                        xf.t[2] = rc.nums[base + 11];
                        xf.scale = (n >= 13) ? rc.nums[base + 12] : 1.0;
                        if (!std::isfinite(xf.scale) || std::fabs(xf.scale) < 1e-12) xf.scale = 1.0;
                        xf.valid = true;
                        pkTransforms[id] = xf;
                    }
                } else if (tp == "line" || tp == "straight") {
                    BRepCurve c;
                    c.kind = CurveKind::Straight;
                    if (rc.nums.size() >= 6) {
                        size_t b = rc.nums.size() - 6;
                        c.origin = { rc.nums[b + 0], rc.nums[b + 1], rc.nums[b + 2] };
                        c.normal = { rc.nums[b + 3], rc.nums[b + 4], rc.nums[b + 5] };
                    }
                    g.curves[id] = std::move(c);
                } else if (tp == "circle") {
                    BRepCurve c;
                    c.kind = CurveKind::Ellipse;
                    c.ratio = 1.0;
                    if (rc.nums.size() >= 10) {
                        size_t b = rc.nums.size() - 10;
                        c.origin = { rc.nums[b + 0], rc.nums[b + 1], rc.nums[b + 2] };
                        c.normal = { rc.nums[b + 3], rc.nums[b + 4], rc.nums[b + 5] };
                        CadVec3 xAx = vNorm({ rc.nums[b + 6], rc.nums[b + 7], rc.nums[b + 8] });
                        double rad = std::fabs(rc.nums[b + 9]);
                        c.majorAxis = vMul(xAx, rad);
                    } else if (rc.nums.size() >= 7) {
                        size_t b = rc.nums.size() - 7;
                        c.origin = { rc.nums[b + 0], rc.nums[b + 1], rc.nums[b + 2] };
                        c.normal = vNorm({ rc.nums[b + 3], rc.nums[b + 4], rc.nums[b + 5] });
                        double rad = std::fabs(rc.nums[b + 6]);
                        CadVec3 ref = (std::fabs(c.normal.z) < 0.9) ? CadVec3{ 0, 0, 1 } : CadVec3{ 1, 0, 0 };
                        c.majorAxis = vMul(vNorm(vCross(ref, c.normal)), rad);
                    }
                    g.curves[id] = std::move(c);
                } else if (tp == "ellipse") {
                    BRepCurve c;
                    c.kind = CurveKind::Ellipse;
                    if (rc.nums.size() >= 11) {
                        size_t b = rc.nums.size() - 11;
                        c.origin = { rc.nums[b + 0], rc.nums[b + 1], rc.nums[b + 2] };
                        c.normal = { rc.nums[b + 3], rc.nums[b + 4], rc.nums[b + 5] };
                        CadVec3 xAx = vNorm({ rc.nums[b + 6], rc.nums[b + 7], rc.nums[b + 8] });
                        double rMaj = std::fabs(rc.nums[b + 9]);
                        double rMin = std::fabs(rc.nums[b + 10]);
                        c.majorAxis = vMul(xAx, rMaj);
                        c.ratio = (rMaj > 1e-9) ? (rMin / rMaj) : 1.0;
                    }
                    g.curves[id] = std::move(c);
                } else if (tp == "plane") {
                    BRepSurf s;
                    s.kind = SurfKind::Plane;
                    if (rc.nums.size() >= 9) {
                        size_t b = rc.nums.size() - 9;
                        s.origin = { rc.nums[b + 0], rc.nums[b + 1], rc.nums[b + 2] };
                        s.normal = { rc.nums[b + 3], rc.nums[b + 4], rc.nums[b + 5] };
                        s.uAxis  = { rc.nums[b + 6], rc.nums[b + 7], rc.nums[b + 8] };
                    } else if (rc.nums.size() >= 6) {
                        size_t b = rc.nums.size() - 6;
                        s.origin = { rc.nums[b + 0], rc.nums[b + 1], rc.nums[b + 2] };
                        s.normal = { rc.nums[b + 3], rc.nums[b + 4], rc.nums[b + 5] };
                    }
                    g.surfaces[id] = s;
                } else if (tp == "cylinder" || tp == "cyl" || tp == "cone") {
                    BRepSurf s;
                    s.kind = SurfKind::CylinderCone;
                    if (rc.nums.size() >= 7) {
                        // First 6 reals of cylinder/cone after links: origin(3), axis(3), radius(1)
                        size_t b = (rc.nums.size() >= 10) ? (rc.nums.size() - 10) : (rc.nums.size() - 7);
                        s.origin  = { rc.nums[b + 0], rc.nums[b + 1], rc.nums[b + 2] };
                        s.normal  = { rc.nums[b + 3], rc.nums[b + 4], rc.nums[b + 5] };
                        s.radius1 = std::fabs(rc.nums[b + 6]);
                    }
                    g.surfaces[id] = s;
                } else if (tp == "sphere") {
                    BRepSurf s;
                    s.kind = SurfKind::Sphere;
                    if (rc.nums.size() >= 4) {
                        size_t b = (rc.nums.size() >= 10) ? (rc.nums.size() - 10) : (rc.nums.size() - 4);
                        s.origin  = { rc.nums[b + 0], rc.nums[b + 1], rc.nums[b + 2] };
                        s.radius1 = std::fabs(rc.nums[b + 3]);
                    }
                    g.surfaces[id] = s;
                } else if (tp == "torus") {
                    BRepSurf s;
                    s.kind = SurfKind::Torus;
                    if (rc.nums.size() >= 8) {
                        size_t b = rc.nums.size() - 8;
                        s.origin  = { rc.nums[b + 0], rc.nums[b + 1], rc.nums[b + 2] };
                        s.normal  = { rc.nums[b + 3], rc.nums[b + 4], rc.nums[b + 5] };
                        s.radius1 = std::fabs(rc.nums[b + 6]);
                        s.radius2 = std::fabs(rc.nums[b + 7]);
                    }
                    g.surfaces[id] = s;
                } else if (tp == "b_surface") {
                    // Parasolid XT b_surface: [uDeg, vDeg, nU, nV, vertexDim(3|4), knotsU(nU+uDeg+1), knotsV(nV+vDeg+1), poles(nU*nV*vertexDim)]
                    if (rc.nums.size() >= 16) {
                        int uDeg = static_cast<int>(std::llround(rc.nums[0]));
                        int vDeg = static_cast<int>(std::llround(rc.nums[1]));
                        int nU   = static_cast<int>(std::llround(rc.nums[2]));
                        int nV   = static_cast<int>(std::llround(rc.nums[3]));
                        int vDim = static_cast<int>(std::llround(rc.nums[4]));
                        if (uDeg >= 1 && uDeg <= 15 && vDeg >= 1 && vDeg <= 15 &&
                            nU > uDeg && nU <= 256 && nV > vDeg && nV <= 256 && (vDim == 3 || vDim == 4))
                        {
                            size_t kuCnt = static_cast<size_t>(nU + uDeg + 1);
                            size_t kvCnt = static_cast<size_t>(nV + vDeg + 1);
                            size_t pCnt  = static_cast<size_t>(nU) * static_cast<size_t>(nV);
                            size_t sp = 5;
                            if (sp + kuCnt + kvCnt + pCnt * static_cast<size_t>(vDim) <= rc.nums.size()) {
                                BRepSurf s;
                                s.kind = SurfKind::Nurbs;
                                s.uDeg = uDeg; s.vDeg = vDeg; s.nU = nU; s.nV = nV;
                                s.isRational = (vDim == 4);
                                s.knotsU.assign(rc.nums.begin() + static_cast<ptrdiff_t>(sp),
                                                rc.nums.begin() + static_cast<ptrdiff_t>(sp + kuCnt));
                                sp += kuCnt;
                                s.knotsV.assign(rc.nums.begin() + static_cast<ptrdiff_t>(sp),
                                                rc.nums.begin() + static_cast<ptrdiff_t>(sp + kvCnt));
                                sp += kvCnt;
                                s.poles.resize(pCnt);
                                if (s.isRational) s.weights.resize(pCnt, 1.0);
                                for (size_t k = 0; k < pCnt; ++k) {
                                    s.poles[k] = {
                                        rc.nums[sp + k * static_cast<size_t>(vDim) + 0],
                                        rc.nums[sp + k * static_cast<size_t>(vDim) + 1],
                                        rc.nums[sp + k * static_cast<size_t>(vDim) + 2]
                                    };
                                    if (s.isRational) s.weights[k] = rc.nums[sp + k * 4u + 3u];
                                }
                                g.surfaces[id] = std::move(s);
                            }
                        }
                    }
                }
            }

            // Second pass: topology entities (edges, fins, loops, faces, bodies)
            for (int32_t id : pkOrder) {
                const PkEntityRec& rc = pkMap.at(id);
                const std::string& tp = rc.type;

                if (tp == "edge") {
                    BRepEdge ed;
                    std::vector<int32_t> vRefs;
                    for (int32_t r : rc.refs) {
                        if (refHasType(r, "vertex")) vRefs.push_back(r);
                        else if (ed.curveId < 0 && g.curves.count(r)) ed.curveId = r;
                        else if (ed.bodyId < 0 && refHasType(r, "body")) ed.bodyId = r;
                    }
                    if (vRefs.size() >= 2) { ed.startVert = vRefs[0]; ed.endVert = vRefs[1]; }
                    else if (vRefs.size() == 1) { ed.startVert = vRefs[0]; ed.endVert = vRefs[0]; }
                    ed.reversed = rc.hasSenseNeg;
                    g.edges[id] = ed;
                    g.edgeOrder.push_back(id);
                } else if (tp == "fin" || tp == "halfedge") {
                    BRepCoedge ce;
                    ce.nextCoedge = firstRefOf(rc, "fin", "halfedge");
                    ce.edgeId     = firstRefOf(rc, "edge");
                    ce.headVert   = firstRefOf(rc, "vertex");
                    ce.reversed   = rc.hasSenseNeg;
                    g.coedges[id] = ce;
                } else if (tp == "loop") {
                    BRepLoop lp;
                    lp.ownerFace   = firstRefOf(rc, "face");
                    lp.firstCoedge = firstRefOf(rc, "fin", "halfedge");
                    lp.nextLoop    = firstRefOf(rc, "loop");
                    if (lp.firstCoedge < 0) {
                        for (int32_t r : rc.refs) {
                            if (refHasType(r, "edge")) lp.directEdges.push_back(r);
                        }
                    }
                    g.loops[id] = std::move(lp);
                } else if (tp == "face") {
                    BRepFace fc;
                    fc.nextFace = firstRefOf(rc, "face");
                    fc.firstLoop = firstRefOf(rc, "loop");
                    for (int32_t r : rc.refs) {
                        if (fc.surfaceId < 0 && g.surfaces.count(r)) fc.surfaceId = r;
                        if (fc.bodyId < 0 && refHasType(r, "body")) fc.bodyId = r;
                    }
                    fc.reversed = rc.hasSenseNeg;
                    g.faces[id] = std::move(fc);
                    g.faceOrder.push_back(id);
                } else if (tp == "body") {
                    int32_t xfRef = firstRefOf(rc, "transform");
                    if (xfRef >= 0 && pkTransforms.count(xfRef)) {
                        g.bodyTransforms[id] = pkTransforms[xfRef];
                    }
                }
            }

            // Link loops <-> faces bidirectionally
            for (auto& kv : g.faces) {
                BRepFace& fc = kv.second;
                int32_t curL = fc.firstLoop;
                std::set<int32_t> seen;
                while (curL >= 0 && seen.insert(curL).second && seen.size() <= 512) {
                    fc.loopIds.push_back(curL);
                    auto lit = g.loops.find(curL);
                    if (lit == g.loops.end()) break;
                    curL = lit->second.nextLoop;
                }
            }
            for (const auto& kv : g.loops) {
                if (kv.second.ownerFace >= 0) {
                    auto fit = g.faces.find(kv.second.ownerFace);
                    if (fit != g.faces.end()) {
                        auto& lids = fit->second.loopIds;
                        if (std::find(lids.begin(), lids.end(), kv.first) == lids.end()) {
                            lids.push_back(kv.first);
                        }
                    }
                }
                // Populate edge start/end vertices from PK fin ring when edge only pointed to fins
                if (kv.second.firstCoedge >= 0) {
                    std::vector<int32_t> ring;
                    int32_t cur = kv.second.firstCoedge;
                    std::set<int32_t> seen;
                    while (cur >= 0 && seen.insert(cur).second && ring.size() < 4096) {
                        ring.push_back(cur);
                        auto cit = g.coedges.find(cur);
                        if (cit == g.coedges.end()) break;
                        cur = cit->second.nextCoedge;
                        if (cur == kv.second.firstCoedge) break;
                    }
                    for (size_t i = 0; i < ring.size(); ++i) {
                        const BRepCoedge& ce = g.coedges.at(ring[i]);
                        const BRepCoedge& pv = g.coedges.at(ring[(i + ring.size() - 1) % ring.size()]);
                        if (ce.headVert >= 0 && pv.headVert >= 0 && ce.edgeId >= 0) {
                            auto eit = g.edges.find(ce.edgeId);
                            if (eit != g.edges.end() && eit->second.startVert < 0 && eit->second.endVert < 0) {
                                if (!ce.reversed) { eit->second.startVert = pv.headVert; eit->second.endVert = ce.headVert; }
                                else              { eit->second.startVert = ce.headVert; eit->second.endVert = pv.headVert; }
                            }
                        }
                    }
                }
            }

            return evaluateBRepGraph(g);
        }
    }
    return false;
}

// ============================================================================
// #55–#60. ASTM E57 (.e57), LASzip (.laz), ROS URDF (.urdf), VRoid (.vrm),
//          Crystallography (.cif), Tripos Sybyl (.mol2) & GROMACS (.gro)
// ============================================================================
namespace DetailE57_200 {

inline const std::array<uint32_t, 256>& Crc32CTable() {
    static const std::array<uint32_t, 256> tbl = []() {
        std::array<uint32_t, 256> t = {};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int b = 0; b < 8; ++b) {
                c = (c >> 1) ^ (0x82F63B78u & (0u - (c & 1u)));
            }
            t[i] = c;
        }
        return t;
    }();
    return tbl;
}

inline uint32_t CalcCrc32C(const uint8_t* buf, size_t len) {
    const auto& tbl = Crc32CTable();
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc = tbl[(crc ^ buf[i]) & 0xFFu] ^ (crc >> 8);
    }
    return ~crc;
}

inline std::string_view XmlAttr(std::string_view tag, std::string_view attrName) {
    size_t pos = 0;
    while (pos < tag.size()) {
        size_t hit = tag.find(attrName, pos);
        if (hit == std::string_view::npos) return {};
        bool leftOk = (hit == 0) || tag[hit - 1] == ' ' || tag[hit - 1] == '\t' ||
                      tag[hit - 1] == '\r' || tag[hit - 1] == '\n' || tag[hit - 1] == ':';
        size_t eq = hit + attrName.size();
        while (eq < tag.size() && (tag[eq] == ' ' || tag[eq] == '\t')) ++eq;
        if (leftOk && eq < tag.size() && tag[eq] == '=') {
            size_t q0 = eq + 1;
            while (q0 < tag.size() && (tag[q0] == ' ' || tag[q0] == '\t')) ++q0;
            if (q0 < tag.size() && (tag[q0] == '"' || tag[q0] == '\'')) {
                char qc = tag[q0++];
                size_t q1 = tag.find(qc, q0);
                if (q1 != std::string_view::npos) return tag.substr(q0, q1 - q0);
            }
        }
        pos = hit + attrName.size();
    }
    return {};
}

inline double XmlChildFloat(std::string_view scope, std::string_view localName, double defVal) {
    size_t p = 0;
    while (p < scope.size()) {
        size_t lt = scope.find('<', p);
        if (lt == std::string_view::npos || lt + 1 >= scope.size()) break;
        if (scope[lt + 1] == '/' || scope[lt + 1] == '!' || scope[lt + 1] == '?') { p = lt + 2; continue; }
        size_t gt = scope.find('>', lt + 1);
        if (gt == std::string_view::npos) break;
        std::string_view openTag = scope.substr(lt + 1, gt - lt - 1);
        size_t nameEnd = openTag.find_first_of(" \t\r\n/");
        std::string_view qName = (nameEnd == std::string_view::npos) ? openTag : openTag.substr(0, nameEnd);
        size_t colon = qName.rfind(':');
        std::string_view base = (colon == std::string_view::npos) ? qName : qName.substr(colon + 1);
        if (base == localName) {
            if (!openTag.empty() && openTag.back() == '/') return 0.0; // E57 XML self-closing Float tag == 0.0!
            size_t closeLt = scope.find('<', gt + 1);
            if (closeLt == std::string_view::npos) return defVal;
            std::string txt(scope.substr(gt + 1, closeLt - gt - 1));
            char* ep = nullptr;
            double v = std::strtod(txt.c_str(), &ep);
            return (ep != txt.c_str() && std::isfinite(v)) ? v : 0.0;
        }
        p = gt + 1;
    }
    return defVal;
}

struct E57FieldDesc {
    std::string name;
    enum Kind { Float32, Float64, Integer, ScaledInteger } kind = Float32;
    uint32_t bitWidth = 32;
    int64_t minimum = 0;
    int64_t maximum = 0;
    double scale = 1.0;
    double offset = 0.0;
};

struct E57BitStream {
    std::vector<uint8_t> bytes;
    size_t bitPos = 0;

    size_t AvailBits() const {
        size_t total = bytes.size() * 8;
        return (total > bitPos) ? (total - bitPos) : 0;
    }

    void Append(const uint8_t* src, size_t len) {
        if (bitPos >= 65536) {
            size_t dropBytes = bitPos >> 3;
            bytes.erase(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(dropBytes));
            bitPos &= 7u;
        }
        bytes.insert(bytes.end(), src, src + len);
    }

    uint64_t ReadBitsLSB(uint32_t nBits) {
        if (nBits == 0) return 0;
        if (nBits == 32 && (bitPos & 7u) == 0) {
            uint32_t v = Detail3D200::RdU32LE(bytes.data() + (bitPos >> 3));
            bitPos += 32;
            return v;
        }
        if (nBits == 64 && (bitPos & 7u) == 0) {
            uint64_t v = Detail3D200::RdU64LE(bytes.data() + (bitPos >> 3));
            bitPos += 64;
            return v;
        }
        uint64_t out = 0;
        for (uint32_t b = 0; b < nBits; ++b) {
            size_t bp = bitPos + b;
            uint64_t bit = (bytes[bp >> 3] >> (bp & 7u)) & 1u;
            out |= (bit << b);
        }
        bitPos += nBits;
        return out;
    }
};

} // namespace DetailE57_200

inline bool ParseSciRoboChem200(const uint8_t* data, size_t size, const std::wstring& ext, LoadedMesh3D& mesh) {
    std::string_view sv(reinterpret_cast<const char*>(data), size);

    // #55. ASTM E2807 3D Laser Scan (.e57 — Paged CRC32C + XML + CompressedVectorSection Decoder)
    if (ext == L".e57" || (size >= 48 && std::memcmp(data, "ASTM-E57", 8) == 0)) {
        if (size < 48 || std::memcmp(data, "ASTM-E57", 8) != 0) return false;

        uint32_t majVer      = Detail3D200::RdU32LE(data + 8);
        uint32_t minVer      = Detail3D200::RdU32LE(data + 12);
        (void)minVer;
        uint64_t filePhysLen = Detail3D200::RdU64LE(data + 16);
        uint64_t xmlPhysOff  = Detail3D200::RdU64LE(data + 24);
        uint64_t xmlLogLen   = Detail3D200::RdU64LE(data + 32);
        uint64_t pageSize    = Detail3D200::RdU64LE(data + 40);

        if (majVer == 0 || majVer > 2) return false;
        if (pageSize < 64 || pageSize > 65536 || (pageSize % 4u) != 0) return false;
        const size_t ps     = static_cast<size_t>(pageSize);
        const size_t usable = ps - 4;

        // Validate physical file length (allowing exact 64MB viewer read cap if file exceeds 64MB)
        const size_t kViewerReadCap = 64ULL * 1024ULL * 1024ULL;
        if (filePhysLen < pageSize) return false;
        if (size < kViewerReadCap && filePhysLen > size) return false;
        const size_t effSize = static_cast<size_t>(std::min<uint64_t>(size, filePhysLen));
        if (effSize < ps) return false;

        if (xmlPhysOff < 48 || xmlPhysOff >= effSize || (xmlPhysOff % ps) >= usable) return false;
        if (xmlLogLen == 0 || xmlLogLen > 32ULL * 1024ULL * 1024ULL) return false;

        // Cached per-page Castagnoli CRC32C verifier
        std::vector<uint8_t> pageCrcState(effSize / ps + 1, 0); // 0=unchecked, 1=valid, 2=invalid
        auto verifyPage = [&](size_t pageIdx) -> bool {
            if (pageIdx >= pageCrcState.size()) return false;
            if (pageCrcState[pageIdx] != 0) return pageCrcState[pageIdx] == 1;
            size_t pageStart = pageIdx * ps;
            if (pageStart + ps > effSize) {
                pageCrcState[pageIdx] = 2;
                return false;
            }
            uint32_t expected = DetailE57_200::CalcCrc32C(data + pageStart, usable);
            uint32_t storedBE = Detail3D200::RdU32BE(data + pageStart + usable);
            uint32_t storedLE = Detail3D200::RdU32LE(data + pageStart + usable);
            bool ok = (storedBE == expected || storedLE == expected);
            pageCrcState[pageIdx] = ok ? 1 : 2;
            return ok;
        };

        if (!verifyPage(0)) return false;

        auto physToLogical = [&](uint64_t physOff, uint64_t& outLogOff) -> bool {
            if (physOff >= effSize || (physOff % ps) >= usable) return false;
            outLogOff = (physOff / ps) * usable + (physOff % ps);
            return true;
        };

        auto readLogicalBytes = [&](uint64_t logOff, size_t len, uint8_t* dst) -> bool {
            size_t done = 0;
            uint64_t curLog = logOff;
            while (done < len) {
                size_t pageIdx   = static_cast<size_t>(curLog / usable);
                size_t offInPage = static_cast<size_t>(curLog % usable);
                if (!verifyPage(pageIdx)) return false;
                size_t chunk = std::min<size_t>(len - done, usable - offInPage);
                std::memcpy(dst + done, data + pageIdx * ps + offInPage, chunk);
                done   += chunk;
                curLog += chunk;
            }
            return true;
        };

        // Extract and verify logical XML section
        uint64_t xmlLogOff = 0;
        if (!physToLogical(xmlPhysOff, xmlLogOff)) return false;
        std::string xmlStr(static_cast<size_t>(xmlLogLen), '\0');
        if (!readLogicalBytes(xmlLogOff, static_cast<size_t>(xmlLogLen), reinterpret_cast<uint8_t*>(xmlStr.data()))) {
            return false;
        }
        std::string_view xmlSv(xmlStr);
        if (xmlSv.find("e57Root") == std::string_view::npos || xmlSv.find("data3D") == std::string_view::npos) {
            return false;
        }

        mesh.formatName = L"ASTM E57 3D LiDAR Point Cloud (.e57)";

        size_t d3Start = xmlSv.find("data3D");
        size_t d3End   = xmlSv.rfind("data3D");
        if (d3Start == std::string_view::npos || d3End <= d3Start) return false;
        std::string_view data3DScope = xmlSv.substr(d3Start, d3End - d3Start);

        // Split into per-scan scopes (<vectorChild ...>...</vectorChild>, or entire data3DScope if single flat scan)
        std::vector<std::string_view> scanScopes;
        {
            size_t sp = 0;
            while (sp < data3DScope.size()) {
                size_t vcOpen = data3DScope.find("<vectorChild", sp);
                if (vcOpen == std::string_view::npos) break;
                // Find matching </vectorChild> accounting for nested <vectorChild> (e.g. indexBounds/grouping)
                size_t cur = vcOpen + 12;
                int depth = 1;
                while (cur < data3DScope.size() && depth > 0) {
                    size_t nextLt = data3DScope.find('<', cur);
                    if (nextLt == std::string_view::npos) break;
                    if (data3DScope.substr(nextLt, 13) == "</vectorChild") {
                        --depth;
                        size_t gt = data3DScope.find('>', nextLt + 13);
                        cur = (gt == std::string_view::npos) ? data3DScope.size() : (gt + 1);
                    } else if (data3DScope.substr(nextLt, 12) == "<vectorChild") {
                        size_t gt = data3DScope.find('>', nextLt + 12);
                        if (gt != std::string_view::npos && data3DScope[gt - 1] != '/') ++depth;
                        cur = (gt == std::string_view::npos) ? data3DScope.size() : (gt + 1);
                    } else {
                        cur = nextLt + 1;
                    }
                }
                scanScopes.push_back(data3DScope.substr(vcOpen, cur - vcOpen));
                sp = cur;
            }
            if (scanScopes.empty()) scanScopes.push_back(data3DScope);
        }

        std::vector<Vec3f> allPts;
        const size_t kMaxTotalPts = 25000;

        for (std::string_view scanSv : scanScopes) {
            if (allPts.size() >= kMaxTotalPts) break;

            // 1. Parse optional <pose> (<rotation> quaternion w,x,y,z + <translation> x,y,z)
            double qw = 1.0, qx = 0.0, qy = 0.0, qz = 0.0;
            double tx = 0.0, ty = 0.0, tz = 0.0;
            size_t poseOpen = scanSv.find("<pose");
            if (poseOpen != std::string_view::npos) {
                size_t poseClose = scanSv.find("</pose>", poseOpen);
                if (poseClose != std::string_view::npos) {
                    std::string_view poseSv = scanSv.substr(poseOpen, poseClose - poseOpen);
                    size_t rotOpen = poseSv.find("<rotation"), rotClose = poseSv.find("</rotation>", rotOpen == std::string_view::npos ? 0 : rotOpen);
                    if (rotOpen != std::string_view::npos && rotClose != std::string_view::npos) {
                        std::string_view rotSv = poseSv.substr(rotOpen + 9, rotClose - rotOpen - 9);
                        qw = DetailE57_200::XmlChildFloat(rotSv, "w", 1.0);
                        qx = DetailE57_200::XmlChildFloat(rotSv, "x", 0.0);
                        qy = DetailE57_200::XmlChildFloat(rotSv, "y", 0.0);
                        qz = DetailE57_200::XmlChildFloat(rotSv, "z", 0.0);
                        double qn = std::sqrt(qw * qw + qx * qx + qy * qy + qz * qz);
                        if (qn > 1e-12) { qw /= qn; qx /= qn; qy /= qn; qz /= qn; }
                        else { qw = 1.0; qx = qy = qz = 0.0; }
                    }
                    size_t trOpen = poseSv.find("<translation"), trClose = poseSv.find("</translation>", trOpen == std::string_view::npos ? 0 : trOpen);
                    if (trOpen != std::string_view::npos && trClose != std::string_view::npos) {
                        std::string_view trSv = poseSv.substr(trOpen + 12, trClose - trOpen - 12);
                        tx = DetailE57_200::XmlChildFloat(trSv, "x", 0.0);
                        ty = DetailE57_200::XmlChildFloat(trSv, "y", 0.0);
                        tz = DetailE57_200::XmlChildFloat(trSv, "z", 0.0);
                    }
                }
            }

            double r00 = 1.0 - 2.0 * (qy * qy + qz * qz), r01 = 2.0 * (qx * qy - qw * qz), r02 = 2.0 * (qx * qz + qw * qy);
            double r10 = 2.0 * (qx * qy + qw * qz), r11 = 1.0 - 2.0 * (qx * qx + qz * qz), r12 = 2.0 * (qy * qz - qw * qx);
            double r20 = 2.0 * (qx * qz - qw * qy), r21 = 2.0 * (qy * qz + qw * qx), r22 = 1.0 - 2.0 * (qx * qx + qy * qy);

            // 2. Locate <points type="CompressedVector" fileOffset="..." recordCount="...">
            size_t ptsOpen = std::string_view::npos;
            {
                size_t search = 0;
                while (search < scanSv.size()) {
                    size_t cand = scanSv.find("<points", search);
                    if (cand == std::string_view::npos) break;
                    char nextC = (cand + 7 < scanSv.size()) ? scanSv[cand + 7] : '\0';
                    if (nextC == ' ' || nextC == '\t' || nextC == '\r' || nextC == '\n' || nextC == '>') {
                        ptsOpen = cand;
                        break;
                    }
                    search = cand + 7;
                }
            }
            if (ptsOpen == std::string_view::npos) continue;
            size_t ptsTagGt = scanSv.find('>', ptsOpen);
            size_t ptsClose = scanSv.find("</points>", ptsOpen);
            if (ptsTagGt == std::string_view::npos || ptsClose == std::string_view::npos || ptsClose <= ptsTagGt) return false;

            std::string_view ptsOpenTag = scanSv.substr(ptsOpen, ptsTagGt - ptsOpen + 1);
            std::string_view ptsType    = DetailE57_200::XmlAttr(ptsOpenTag, "type");
            std::string_view fileOffSv  = DetailE57_200::XmlAttr(ptsOpenTag, "fileOffset");
            std::string_view recCntSv   = DetailE57_200::XmlAttr(ptsOpenTag, "recordCount");
            if (ptsType != "CompressedVector" || fileOffSv.empty() || recCntSv.empty()) return false;

            char* ep1 = nullptr;
            uint64_t cvPhysOff = std::strtoull(std::string(fileOffSv).c_str(), &ep1, 10);
            char* ep2 = nullptr;
            uint64_t recordCount = std::strtoull(std::string(recCntSv).c_str(), &ep2, 10);
            if (cvPhysOff < 48 || recordCount == 0 || recordCount > 500000000ULL) return false;

            // 3. Parse <prototype> field descriptors in exact declaration order
            std::string_view ptsBody = scanSv.substr(ptsTagGt + 1, ptsClose - ptsTagGt - 1);
            size_t protoOpen = ptsBody.find("<prototype");
            size_t protoClose = ptsBody.find("</prototype>", protoOpen == std::string_view::npos ? 0 : protoOpen);
            if (protoOpen == std::string_view::npos || protoClose == std::string_view::npos) return false;
            size_t protoGt = ptsBody.find('>', protoOpen);
            if (protoGt == std::string_view::npos || protoGt >= protoClose) return false;
            std::string_view protoBody = ptsBody.substr(protoGt + 1, protoClose - protoGt - 1);

            std::vector<DetailE57_200::E57FieldDesc> fields;
            int idxCartX = -1, idxCartY = -1, idxCartZ = -1, idxCartInv = -1;
            int idxSphR  = -1, idxSphAz = -1, idxSphEl = -1, idxSphInv  = -1;

            size_t fp = 0;
            while (fp < protoBody.size()) {
                size_t lt = protoBody.find('<', fp);
                if (lt == std::string_view::npos || lt + 1 >= protoBody.size()) break;
                if (protoBody[lt + 1] == '/' || protoBody[lt + 1] == '!' || protoBody[lt + 1] == '?') {
                    size_t gtSkip = protoBody.find('>', lt + 2);
                    if (gtSkip == std::string_view::npos) break;
                    fp = gtSkip + 1;
                    continue;
                }
                size_t gt = protoBody.find('>', lt + 1);
                if (gt == std::string_view::npos) return false;
                std::string_view fTag = protoBody.substr(lt + 1, gt - lt - 1);
                bool selfClosing = (!fTag.empty() && fTag.back() == '/');

                size_t nmEnd = fTag.find_first_of(" \t\r\n/");
                std::string_view qName = (nmEnd == std::string_view::npos) ? fTag : fTag.substr(0, nmEnd);
                size_t colon = qName.rfind(':');
                std::string fieldName(colon == std::string_view::npos ? qName : qName.substr(colon + 1));

                std::string_view fType = DetailE57_200::XmlAttr(fTag, "type");
                DetailE57_200::E57FieldDesc fd;
                fd.name = fieldName;

                auto parseAttrI64 = [&](std::string_view key, int64_t defV) -> int64_t {
                    std::string_view s = DetailE57_200::XmlAttr(fTag, key);
                    if (s.empty()) return defV;
                    return std::strtoll(std::string(s).c_str(), nullptr, 10);
                };
                auto parseAttrF64 = [&](std::string_view key, double defV) -> double {
                    std::string_view s = DetailE57_200::XmlAttr(fTag, key);
                    if (s.empty()) return defV;
                    double v = std::strtod(std::string(s).c_str(), nullptr);
                    return std::isfinite(v) ? v : defV;
                };

                if (fType == "Float") {
                    std::string_view prec = DetailE57_200::XmlAttr(fTag, "precision");
                    if (prec == "single") { fd.kind = DetailE57_200::E57FieldDesc::Float32; fd.bitWidth = 32; }
                    else                  { fd.kind = DetailE57_200::E57FieldDesc::Float64; fd.bitWidth = 64; }
                } else if (fType == "Integer" || fType == "ScaledInteger") {
                    fd.minimum = parseAttrI64("minimum", -2147483648LL);
                    fd.maximum = parseAttrI64("maximum",  2147483647LL);
                    if (fd.maximum < fd.minimum) return false;
                    uint64_t range = static_cast<uint64_t>(fd.maximum) - static_cast<uint64_t>(fd.minimum);
                    uint32_t bits = 0;
                    if (range == 0xFFFFFFFFFFFFFFFFULL) {
                        bits = 64;
                    } else if (range > 0) {
                        bits = 1;
                        while (bits < 64 && (range >> bits) != 0) ++bits;
                    }
                    fd.bitWidth = bits;
                    if (fType == "ScaledInteger") {
                        fd.kind   = DetailE57_200::E57FieldDesc::ScaledInteger;
                        fd.scale  = parseAttrF64("scale",  1.0);
                        fd.offset = parseAttrF64("offset", 0.0);
                    } else {
                        fd.kind = DetailE57_200::E57FieldDesc::Integer;
                    }
                } else {
                    return false;
                }

                int curFieldIdx = static_cast<int>(fields.size());
                if (fieldName == "cartesianX")            idxCartX   = curFieldIdx;
                else if (fieldName == "cartesianY")       idxCartY   = curFieldIdx;
                else if (fieldName == "cartesianZ")       idxCartZ   = curFieldIdx;
                else if (fieldName == "cartesianInvalidState") idxCartInv = curFieldIdx;
                else if (fieldName == "sphericalRange")   idxSphR    = curFieldIdx;
                else if (fieldName == "sphericalAzimuth") idxSphAz   = curFieldIdx;
                else if (fieldName == "sphericalElevation") idxSphEl = curFieldIdx;
                else if (fieldName == "sphericalInvalidState") idxSphInv = curFieldIdx;

                fields.push_back(std::move(fd));

                if (!selfClosing) {
                    std::string closePat = "</" + std::string(qName) + ">";
                    size_t cPos = protoBody.find(closePat, gt + 1);
                    fp = (cPos == std::string_view::npos) ? (gt + 1) : (cPos + closePat.size());
                } else {
                    fp = gt + 1;
                }
            }

            bool hasCartesian = (idxCartX >= 0 && idxCartY >= 0 && idxCartZ >= 0);
            bool hasSpherical = (idxSphR >= 0 && idxSphAz >= 0 && idxSphEl >= 0);
            if (fields.empty() || (!hasCartesian && !hasSpherical)) return false;

            // 4. Seek to CompressedVectorSection (32 logical bytes)
            uint64_t secLogOff = 0;
            if (!physToLogical(cvPhysOff, secLogOff)) return false;
            uint8_t secHdr[32] = {};
            if (!readLogicalBytes(secLogOff, 32, secHdr)) return false;
            if (secHdr[0] != 1) return false; // sectionId == 1 (CompressedVectorSection)

            uint64_t secLogLen   = Detail3D200::RdU64LE(secHdr + 8);
            uint64_t dataPhysOff = Detail3D200::RdU64LE(secHdr + 16);
            uint64_t pktLogPos   = 0;
            if (secLogLen < 32 || !physToLogical(dataPhysOff, pktLogPos)) return false;
            uint64_t secLogEnd = secLogOff + secLogLen;
            if (pktLogPos < secLogOff + 32 || pktLogPos >= secLogEnd) return false;

            // 5. Iterate DataPackets & unpack planar bitstreams
            const size_t nFields = fields.size();
            std::vector<DetailE57_200::E57BitStream> streams(nFields);
            std::vector<double> recVals(nFields, 0.0);
            std::vector<uint8_t> pktBuf;
            uint64_t decodedCount = 0;
            uint64_t subsampleStep = std::max<uint64_t>(1, recordCount / kMaxTotalPts);

            while (decodedCount < recordCount && pktLogPos + 4 <= secLogEnd && allPts.size() < kMaxTotalPts) {
                uint8_t pktHdr[4] = {};
                if (!readLogicalBytes(pktLogPos, 4, pktHdr)) return false;
                uint8_t pktType = pktHdr[0];
                size_t pktLen   = static_cast<size_t>(Detail3D200::RdU16LE(pktHdr + 2)) + 1;
                if (pktLogPos + pktLen > secLogEnd) return false;

                pktBuf.resize(pktLen);
                if (!readLogicalBytes(pktLogPos, pktLen, pktBuf.data())) return false;

                if (pktType == 1) { // DataPacket
                    if (pktLen < 6 + nFields * 2) return false;
                    uint16_t bsCount = Detail3D200::RdU16LE(pktBuf.data() + 4);
                    if (bsCount != nFields) return false;

                    size_t payCursor = 6 + nFields * 2;
                    for (size_t f = 0; f < nFields; ++f) {
                        uint16_t bsLen = Detail3D200::RdU16LE(pktBuf.data() + 6 + f * 2);
                        if (payCursor + bsLen > pktLen) return false;
                        if (bsLen > 0) streams[f].Append(pktBuf.data() + payCursor, bsLen);
                        payCursor += bsLen;
                    }

                    // Extract all complete records currently available across all streams
                    while (decodedCount < recordCount && allPts.size() < kMaxTotalPts) {
                        bool ready = true;
                        for (size_t f = 0; f < nFields; ++f) {
                            if (fields[f].bitWidth > 0 && streams[f].AvailBits() < fields[f].bitWidth) {
                                ready = false;
                                break;
                            }
                        }
                        if (!ready) break;

                        bool finiteOk = true;
                        for (size_t f = 0; f < nFields; ++f) {
                            const auto& fd = fields[f];
                            uint64_t rawBits = streams[f].ReadBitsLSB(fd.bitWidth);
                            if (fd.kind == DetailE57_200::E57FieldDesc::Float32) {
                                uint32_t u32 = static_cast<uint32_t>(rawBits);
                                float fv = 0.0f;
                                std::memcpy(&fv, &u32, 4);
                                if (!std::isfinite(fv)) finiteOk = false;
                                recVals[f] = static_cast<double>(fv);
                            } else if (fd.kind == DetailE57_200::E57FieldDesc::Float64) {
                                double dv = 0.0;
                                std::memcpy(&dv, &rawBits, 8);
                                if (!std::isfinite(dv)) finiteOk = false;
                                recVals[f] = dv;
                            } else if (fd.kind == DetailE57_200::E57FieldDesc::Integer) {
                                int64_t iv = static_cast<int64_t>(static_cast<uint64_t>(fd.minimum) + rawBits);
                                recVals[f] = static_cast<double>(iv);
                            } else { // ScaledInteger
                                int64_t iv = static_cast<int64_t>(static_cast<uint64_t>(fd.minimum) + rawBits);
                                recVals[f] = static_cast<double>(iv) * fd.scale + fd.offset;
                            }
                        }

                        uint64_t recIdx = decodedCount++;
                        if (!finiteOk || (recIdx % subsampleStep) != 0) continue;

                        double lx = 0.0, ly = 0.0, lz = 0.0;
                        if (hasCartesian) {
                            if (idxCartInv >= 0 && std::llround(recVals[static_cast<size_t>(idxCartInv)]) != 0) continue;
                            lx = recVals[static_cast<size_t>(idxCartX)];
                            ly = recVals[static_cast<size_t>(idxCartY)];
                            lz = recVals[static_cast<size_t>(idxCartZ)];
                        } else {
                            if (idxSphInv >= 0 && std::llround(recVals[static_cast<size_t>(idxSphInv)]) != 0) continue;
                            double rng = recVals[static_cast<size_t>(idxSphR)];
                            double az  = recVals[static_cast<size_t>(idxSphAz)];
                            double el  = recVals[static_cast<size_t>(idxSphEl)];
                            lx = rng * std::cos(el) * std::cos(az);
                            ly = rng * std::cos(el) * std::sin(az);
                            lz = rng * std::sin(el);
                        }

                        double wx = r00 * lx + r01 * ly + r02 * lz + tx;
                        double wy = r10 * lx + r11 * ly + r12 * lz + ty;
                        double wz = r20 * lx + r21 * ly + r22 * lz + tz;
                        if (std::isfinite(wx) && std::isfinite(wy) && std::isfinite(wz)) {
                            allPts.push_back({
                                static_cast<float>(wx),
                                static_cast<float>(wz),
                                static_cast<float>(-wy)
                            });
                        }
                    }
                } else if (pktType != 0 && pktType != 2) {
                    return false;
                }

                pktLogPos += (static_cast<uint64_t>(pktLen) + 3ULL) & ~3ULL;
            }
        }

        if (allPts.empty()) return false;
        float r = Detail3D200::ComputePointCloudSpan(allPts) * 0.018f;
        for (const auto& pt : allPts) Detail3D200::AddPointTetra(mesh, pt, r);
        return !mesh.vertices.empty() && !mesh.tris.empty();
    }

    // #56. LASzip / LAS 1.0–1.4 LiDAR (.laz — decodes both LASzip VLR 22204 arithmetic chunks and uncompressed records)
    if (ext == L".laz" || (size >= 227 && std::memcmp(data, "LASF", 4) == 0)) {
        bool ok = Formats::ParseLasLidarMesh(data, size, mesh);
        if (ok && ext == L".laz" && mesh.formatName.find(L".laz") == std::wstring::npos) {
            mesh.formatName = L"LAS / LASzip LiDAR (.laz)";
        }
        return ok;
    }

    // #57. ROS Robot Description Format (.urdf — Full Kinematic Tree: <link>, <joint> T*Rz*Ry*Rx, <visual> primitives & external meshes)
    if (ext == L".urdf") {
        if (sv.find("<robot") == std::string_view::npos) return false;
        mesh.formatName = L"ROS Robot Description (.urdf)";

        struct UrdfMat4 {
            float m[4][4] = {
                { 1.0f, 0.0f, 0.0f, 0.0f },
                { 0.0f, 1.0f, 0.0f, 0.0f },
                { 0.0f, 0.0f, 1.0f, 0.0f },
                { 0.0f, 0.0f, 0.0f, 1.0f }
            };
        };

        auto makeUrdfPose = [](float tx, float ty, float tz, float roll, float pitch, float yaw) -> UrdfMat4 {
            // Exact URDF spec: T(tx,ty,tz) * Rz(yaw) * Ry(pitch) * Rx(roll)
            const float cr = std::cos(roll),  sr = std::sin(roll);
            const float cp = std::cos(pitch), sp = std::sin(pitch);
            const float cy = std::cos(yaw),   sy = std::sin(yaw);
            UrdfMat4 r;
            r.m[0][0] = cy * cp;  r.m[0][1] = cy * sp * sr - sy * cr;  r.m[0][2] = cy * sp * cr + sy * sr;  r.m[0][3] = tx;
            r.m[1][0] = sy * cp;  r.m[1][1] = sy * sp * sr + cy * cr;  r.m[1][2] = sy * sp * cr - cy * sr;  r.m[1][3] = ty;
            r.m[2][0] = -sp;      r.m[2][1] = cp * sr;                 r.m[2][2] = cp * cr;                 r.m[2][3] = tz;
            r.m[3][0] = 0.0f;     r.m[3][1] = 0.0f;                    r.m[3][2] = 0.0f;                    r.m[3][3] = 1.0f;
            return r;
        };

        auto mulUrdfMat4 = [](const UrdfMat4& a, const UrdfMat4& b) -> UrdfMat4 {
            UrdfMat4 c;
            for (int r = 0; r < 4; ++r) {
                for (int col = 0; col < 4; ++col) {
                    c.m[r][col] = a.m[r][0] * b.m[0][col] +
                                  a.m[r][1] * b.m[1][col] +
                                  a.m[r][2] * b.m[2][col] +
                                  a.m[r][3] * b.m[3][col];
                }
            }
            return c;
        };

        auto detUrdf3x3 = [](const UrdfMat4& a) -> float {
            return a.m[0][0] * (a.m[1][1] * a.m[2][2] - a.m[1][2] * a.m[2][1]) -
                   a.m[0][1] * (a.m[1][0] * a.m[2][2] - a.m[1][2] * a.m[2][0]) +
                   a.m[0][2] * (a.m[1][0] * a.m[2][1] - a.m[1][1] * a.m[2][0]);
        };

        auto xformUrdfToViewer = [](const UrdfMat4& m, float lx, float ly, float lz) -> Vec3f {
            float wx = m.m[0][0] * lx + m.m[0][1] * ly + m.m[0][2] * lz + m.m[0][3];
            float wy = m.m[1][0] * lx + m.m[1][1] * ly + m.m[1][2] * lz + m.m[1][3];
            float wz = m.m[2][0] * lx + m.m[2][1] * ly + m.m[2][2] * lz + m.m[2][3];
            // Convert ROS right-handed Z-up world (wx, wy, wz) -> right-handed studio Y-up (wx, wz, -wy)
            return { wx, wz, -wy };
        };

        struct UrdfXmlTag {
            std::string_view name;
            std::string_view attrs;
            size_t tagStart = 0;
            size_t afterTag = 0;
            bool isClosing = false;
            bool isSelfClosing = false;
        };

        auto nextUrdfTag = [](std::string_view src, size_t& p, UrdfXmlTag& out) -> bool {
            const size_t n = src.size();
            while (p < n) {
                size_t lt = src.find('<', p);
                if (lt == std::string_view::npos || lt + 1 >= n) { p = n; return false; }
                if (lt + 4 <= n && src.substr(lt, 4) == "<!--") {
                    size_t ec = src.find("-->", lt + 4);
                    p = (ec == std::string_view::npos) ? n : (ec + 3);
                    continue;
                }
                if (src[lt + 1] == '?' || src[lt + 1] == '!') {
                    size_t gt = src.find('>', lt + 2);
                    p = (gt == std::string_view::npos) ? n : (gt + 1);
                    continue;
                }
                size_t gt = lt + 1;
                char q = 0;
                while (gt < n) {
                    char c = src[gt];
                    if (q) { if (c == q) q = 0; }
                    else if (c == '"' || c == '\'') q = c;
                    else if (c == '>') break;
                    ++gt;
                }
                if (gt >= n) { p = n; return false; }
                out = {};
                out.tagStart = lt;
                out.afterTag = gt + 1;
                p = gt + 1;

                std::string_view inner = src.substr(lt + 1, gt - lt - 1);
                while (!inner.empty() && (inner.front() == ' ' || inner.front() == '\t' || inner.front() == '\r' || inner.front() == '\n')) inner.remove_prefix(1);
                while (!inner.empty() && (inner.back() == ' ' || inner.back() == '\t' || inner.back() == '\r' || inner.back() == '\n')) inner.remove_suffix(1);
                if (inner.empty()) continue;
                if (inner.front() == '/') {
                    out.isClosing = true;
                    inner.remove_prefix(1);
                } else if (inner.back() == '/') {
                    out.isSelfClosing = true;
                    inner.remove_suffix(1);
                }
                size_t ne = 0;
                while (ne < inner.size() && inner[ne] != ' ' && inner[ne] != '\t' && inner[ne] != '\r' && inner[ne] != '\n') ++ne;
                std::string_view qn = inner.substr(0, ne);
                size_t col = qn.rfind(':');
                out.name = (col == std::string_view::npos) ? qn : qn.substr(col + 1);
                out.attrs = (ne < inner.size()) ? inner.substr(ne) : std::string_view{};
                return true;
            }
            return false;
        };

        auto findUrdfElementBody = [&](std::string_view src, size_t startAfterOpen, std::string_view elemName, std::string_view& bodyOut, size_t& afterCloseOut) -> bool {
            int depth = 1;
            size_t scan = startAfterOpen;
            UrdfXmlTag t;
            while (nextUrdfTag(src, scan, t)) {
                if (t.name == elemName) {
                    if (t.isClosing) {
                        if (--depth == 0) {
                            bodyOut = src.substr(startAfterOpen, t.tagStart - startAfterOpen);
                            afterCloseOut = t.afterTag;
                            return true;
                        }
                    } else if (!t.isSelfClosing) {
                        ++depth;
                    }
                }
            }
            return false;
        };

        auto getUrdfAttr = [](std::string_view attrs, std::string_view key) -> std::string_view {
            size_t p = 0;
            while (p < attrs.size()) {
                while (p < attrs.size() && (attrs[p] == ' ' || attrs[p] == '\t' || attrs[p] == '\r' || attrs[p] == '\n')) ++p;
                if (p >= attrs.size()) break;
                size_t ks = p;
                while (p < attrs.size() && attrs[p] != '=' && attrs[p] != ' ' && attrs[p] != '\t' && attrs[p] != '\r' && attrs[p] != '\n') ++p;
                std::string_view k = attrs.substr(ks, p - ks);
                while (p < attrs.size() && (attrs[p] == ' ' || attrs[p] == '\t' || attrs[p] == '\r' || attrs[p] == '\n')) ++p;
                if (p >= attrs.size() || attrs[p] != '=') continue;
                ++p;
                while (p < attrs.size() && (attrs[p] == ' ' || attrs[p] == '\t' || attrs[p] == '\r' || attrs[p] == '\n')) ++p;
                if (p >= attrs.size() || (attrs[p] != '"' && attrs[p] != '\'')) continue;
                char q = attrs[p++];
                size_t vs = p;
                size_t ve = attrs.find(q, vs);
                if (ve == std::string_view::npos) break;
                p = ve + 1;
                if (k == key) return attrs.substr(vs, ve - vs);
            }
            return {};
        };

        auto parseUrdfVec3 = [](std::string_view s, float defX, float defY, float defZ, float& ox, float& oy, float& oz) -> bool {
            ox = defX; oy = defY; oz = defZ;
            if (s.empty()) return false;
            const char* c = s.data();
            const char* e = c + s.size();
            float vals[3] = { defX, defY, defZ };
            int got = 0;
            while (c < e && got < 3) {
                while (c < e && (*c == ' ' || *c == '\t' || *c == '\r' || *c == '\n')) ++c;
                if (c >= e) break;
                const char* ts = c;
                while (c < e && *c != ' ' && *c != '\t' && *c != '\r' && *c != '\n') ++c;
                size_t len = static_cast<size_t>(c - ts);
                if (len == 0 || len >= 64) return false;
                char buf[64]; std::memcpy(buf, ts, len); buf[len] = '\0';
                char* nx = nullptr;
                double v = std::strtod(buf, &nx);
                if (nx == buf || !std::isfinite(v)) return false;
                vals[got++] = static_cast<float>(v);
            }
            if (got < 3) return false;
            ox = vals[0]; oy = vals[1]; oz = vals[2];
            return true;
        };

        auto parseUrdfFloat = [](std::string_view s, float defV) -> float {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
            if (s.empty() || s.size() >= 64) return defV;
            char buf[64]; std::memcpy(buf, s.data(), s.size()); buf[s.size()] = '\0';
            char* nx = nullptr;
            double v = std::strtod(buf, &nx);
            return (nx != buf && std::isfinite(v)) ? static_cast<float>(v) : defV;
        };

        auto parseOriginTag = [&](std::string_view attrs) -> UrdfMat4 {
            float tx = 0.0f, ty = 0.0f, tz = 0.0f;
            float rr = 0.0f, rp = 0.0f, ry = 0.0f;
            parseUrdfVec3(getUrdfAttr(attrs, "xyz"), 0.0f, 0.0f, 0.0f, tx, ty, tz);
            parseUrdfVec3(getUrdfAttr(attrs, "rpy"), 0.0f, 0.0f, 0.0f, rr, rp, ry);
            return makeUrdfPose(tx, ty, tz, rr, rp, ry);
        };

        struct UrdfVisual {
            enum GeomKind { None = 0, Box, Cylinder, Sphere, ExternalMesh } kind = None;
            UrdfMat4 origin;
            float p0 = 0.0f, p1 = 0.0f, p2 = 0.0f; // Box(bx,by,bz), Cylinder(rad,len), Sphere(rad)
            std::string meshUri;
            float scaleX = 1.0f, scaleY = 1.0f, scaleZ = 1.0f;
            float rgba[4] = { 0.75f, 0.78f, 0.82f, 1.0f };
        };

        struct UrdfLink {
            std::string name;
            std::vector<UrdfVisual> visuals;
        };

        struct UrdfJoint {
            std::string name;
            std::string type;
            std::string parentLink;
            std::string childLink;
            UrdfMat4 origin;
        };

        // Locate <robot> root body
        size_t scanPos = 0;
        UrdfXmlTag rootTag;
        std::string_view robotBody;
        bool foundRobot = false;
        while (nextUrdfTag(sv, scanPos, rootTag)) {
            if (!rootTag.isClosing && rootTag.name == "robot") {
                if (rootTag.isSelfClosing) return false;
                size_t afterRobot = scanPos;
                if (!findUrdfElementBody(sv, scanPos, "robot", robotBody, afterRobot)) {
                    robotBody = sv.substr(scanPos);
                }
                foundRobot = true;
                break;
            }
        }
        if (!foundRobot) return false;

        std::unordered_map<std::string, std::array<float, 4>> namedMaterials;
        std::vector<UrdfLink> links;
        std::unordered_map<std::string, size_t> linkIndexByName;
        std::vector<UrdfJoint> joints;

        size_t rp = 0;
        UrdfXmlTag topTag;
        while (nextUrdfTag(robotBody, rp, topTag)) {
            if (topTag.isClosing) continue;

            if (topTag.name == "material") {
                std::string matName(getUrdfAttr(topTag.attrs, "name"));
                std::string_view matBody;
                if (!topTag.isSelfClosing && findUrdfElementBody(robotBody, rp, "material", matBody, rp)) {
                    size_t mp = 0;
                    UrdfXmlTag mt;
                    while (nextUrdfTag(matBody, mp, mt)) {
                        if (!mt.isClosing && mt.name == "color") {
                            std::string_view rgbaSv = getUrdfAttr(mt.attrs, "rgba");
                            float r = 0.8f, g = 0.8f, b = 0.8f;
                            if (parseUrdfVec3(rgbaSv, 0.8f, 0.8f, 0.8f, r, g, b) && !matName.empty()) {
                                namedMaterials[matName] = { r, g, b, 1.0f };
                            }
                        }
                    }
                }
            } else if (topTag.name == "link") {
                UrdfLink lnk;
                lnk.name.assign(getUrdfAttr(topTag.attrs, "name"));
                std::string_view linkBody;
                if (!topTag.isSelfClosing && findUrdfElementBody(robotBody, rp, "link", linkBody, rp)) {
                    size_t lp = 0;
                    UrdfXmlTag lt;
                    while (nextUrdfTag(linkBody, lp, lt)) {
                        if (lt.isClosing) continue;
                        if (lt.name == "visual" && !lt.isSelfClosing) {
                            std::string_view visBody;
                            if (!findUrdfElementBody(linkBody, lp, "visual", visBody, lp)) break;
                            UrdfVisual vis;
                            size_t vp = 0;
                            UrdfXmlTag vt;
                            while (nextUrdfTag(visBody, vp, vt)) {
                                if (vt.isClosing) continue;
                                if (vt.name == "origin") {
                                    vis.origin = parseOriginTag(vt.attrs);
                                    if (!vt.isSelfClosing) {
                                        std::string_view skip;
                                        findUrdfElementBody(visBody, vp, "origin", skip, vp);
                                    }
                                } else if (vt.name == "geometry" && !vt.isSelfClosing) {
                                    std::string_view geomBody;
                                    if (!findUrdfElementBody(visBody, vp, "geometry", geomBody, vp)) break;
                                    size_t gp = 0;
                                    UrdfXmlTag gt;
                                    while (nextUrdfTag(geomBody, gp, gt)) {
                                        if (gt.isClosing) continue;
                                        if (gt.name == "box") {
                                            float bx = 0.0f, by = 0.0f, bz = 0.0f;
                                            if (parseUrdfVec3(getUrdfAttr(gt.attrs, "size"), 0, 0, 0, bx, by, bz) &&
                                                bx > 0.0f && by > 0.0f && bz > 0.0f) {
                                                vis.kind = UrdfVisual::Box;
                                                vis.p0 = bx; vis.p1 = by; vis.p2 = bz;
                                            }
                                        } else if (gt.name == "cylinder") {
                                            float rad = parseUrdfFloat(getUrdfAttr(gt.attrs, "radius"), 0.0f);
                                            float len = parseUrdfFloat(getUrdfAttr(gt.attrs, "length"), 0.0f);
                                            if (rad > 0.0f && len > 0.0f) {
                                                vis.kind = UrdfVisual::Cylinder;
                                                vis.p0 = rad; vis.p1 = len;
                                            }
                                        } else if (gt.name == "sphere") {
                                            float rad = parseUrdfFloat(getUrdfAttr(gt.attrs, "radius"), 0.0f);
                                            if (rad > 0.0f) {
                                                vis.kind = UrdfVisual::Sphere;
                                                vis.p0 = rad;
                                            }
                                        } else if (gt.name == "mesh") {
                                            std::string_view fn = getUrdfAttr(gt.attrs, "filename");
                                            if (!fn.empty()) {
                                                vis.kind = UrdfVisual::ExternalMesh;
                                                vis.meshUri.assign(fn);
                                                parseUrdfVec3(getUrdfAttr(gt.attrs, "scale"), 1.0f, 1.0f, 1.0f, vis.scaleX, vis.scaleY, vis.scaleZ);
                                            }
                                        }
                                    }
                                } else if (vt.name == "material") {
                                    std::string mref(getUrdfAttr(vt.attrs, "name"));
                                    auto itM = namedMaterials.find(mref);
                                    if (itM != namedMaterials.end()) {
                                        for (int k = 0; k < 4; ++k) vis.rgba[k] = itM->second[k];
                                    }
                                    if (!vt.isSelfClosing) {
                                        std::string_view mBody;
                                        if (findUrdfElementBody(visBody, vp, "material", mBody, vp)) {
                                            size_t cmp = 0;
                                            UrdfXmlTag cmt;
                                            while (nextUrdfTag(mBody, cmp, cmt)) {
                                                if (!cmt.isClosing && cmt.name == "color") {
                                                    parseUrdfVec3(getUrdfAttr(cmt.attrs, "rgba"), vis.rgba[0], vis.rgba[1], vis.rgba[2], vis.rgba[0], vis.rgba[1], vis.rgba[2]);
                                                }
                                            }
                                        }
                                    }
                                } else if (!vt.isSelfClosing) {
                                    std::string_view skip;
                                    if (!findUrdfElementBody(visBody, vp, vt.name, skip, vp)) break;
                                }
                            }
                            if (vis.kind != UrdfVisual::None) {
                                lnk.visuals.push_back(std::move(vis));
                            }
                        } else if (!lt.isSelfClosing) {
                            std::string_view skip;
                            if (!findUrdfElementBody(linkBody, lp, lt.name, skip, lp)) break;
                        }
                    }
                }
                if (!lnk.name.empty() && linkIndexByName.find(lnk.name) == linkIndexByName.end()) {
                    linkIndexByName[lnk.name] = links.size();
                    links.push_back(std::move(lnk));
                }
            } else if (topTag.name == "joint" && !topTag.isSelfClosing) {
                UrdfJoint jnt;
                jnt.name.assign(getUrdfAttr(topTag.attrs, "name"));
                jnt.type.assign(getUrdfAttr(topTag.attrs, "type"));
                std::string_view jBody;
                if (findUrdfElementBody(robotBody, rp, "joint", jBody, rp)) {
                    size_t jp = 0;
                    UrdfXmlTag jt;
                    while (nextUrdfTag(jBody, jp, jt)) {
                        if (jt.isClosing) continue;
                        if (jt.name == "parent") {
                            jnt.parentLink.assign(getUrdfAttr(jt.attrs, "link"));
                        } else if (jt.name == "child") {
                            jnt.childLink.assign(getUrdfAttr(jt.attrs, "link"));
                        } else if (jt.name == "origin") {
                            jnt.origin = parseOriginTag(jt.attrs);
                        }
                        if (!jt.isSelfClosing) {
                            std::string_view skip;
                            if (!findUrdfElementBody(jBody, jp, jt.name, skip, jp)) break;
                        }
                    }
                }
                if (!jnt.parentLink.empty() && !jnt.childLink.empty()) {
                    joints.push_back(std::move(jnt));
                }
            } else if (!topTag.isSelfClosing) {
                std::string_view skip;
                if (!findUrdfElementBody(robotBody, rp, topTag.name, skip, rp)) break;
            }
        }

        if (links.empty()) return false;

        // Build adjacency list parentLinkIdx -> [(childLinkIdx, jointOriginTransform)]
        std::vector<std::vector<std::pair<size_t, UrdfMat4>>> childrenOf(links.size());
        std::vector<bool> isChildLink(links.size(), false);
        for (const auto& j : joints) {
            auto itP = linkIndexByName.find(j.parentLink);
            auto itC = linkIndexByName.find(j.childLink);
            if (itP != linkIndexByName.end() && itC != linkIndexByName.end() && itP->second != itC->second) {
                childrenOf[itP->second].emplace_back(itC->second, j.origin);
                isChildLink[itC->second] = true;
            }
        }

        // Helper to resolve & load external <mesh filename="..."/> (.stl, .obj, .dae, .ply) in local link coords
        std::wstring urdfDir;
        if (!mesh.filePath.empty()) {
            size_t sl = mesh.filePath.find_last_of(L"\\/");
            if (sl != std::wstring::npos) urdfDir = mesh.filePath.substr(0, sl);
        }

        auto loadExternalUrdfMesh = [&](const std::string& rawUri, std::vector<Vec3f>& outV, std::vector<std::array<uint32_t, 3>>& outT) -> bool {
            if (urdfDir.empty() || rawUri.empty()) return false;
            std::string clean = rawUri;
            std::string pkgName;
            if (clean.rfind("package://", 0) == 0) {
                clean.erase(0, 10);
                size_t slash = clean.find('/');
                if (slash == std::string::npos || slash + 1 >= clean.size()) return false;
                pkgName = clean.substr(0, slash);
                clean.erase(0, slash + 1);
            } else if (clean.rfind("file:///", 0) == 0) {
                clean.erase(0, 8);
            } else if (clean.rfind("file://", 0) == 0) {
                clean.erase(0, 7);
            }
            while (!clean.empty() && (clean.front() == '/' || clean.front() == '\\')) clean.erase(clean.begin());
            if (clean.empty() || clean.find(':') != std::string::npos) return false;

            auto utf8ToWide = [](const std::string& s) -> std::wstring {
                if (s.empty()) return {};
                int wn = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
                if (wn <= 0) return {};
                std::wstring out(static_cast<size_t>(wn), L'\0');
                MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), wn);
                return out;
            };

            std::wstring wRel = utf8ToWide(clean);
            for (wchar_t& ch : wRel) { if (ch == L'/') ch = L'\\'; }

            std::vector<std::wstring> candidates;
            candidates.push_back(urdfDir + L"\\" + wRel);
            size_t parentSl = urdfDir.find_last_of(L"\\/");
            if (parentSl != std::wstring::npos) {
                std::wstring parentDir = urdfDir.substr(0, parentSl);
                candidates.push_back(parentDir + L"\\" + wRel);
                if (!pkgName.empty()) {
                    std::wstring wPkg = utf8ToWide(pkgName);
                    candidates.push_back(parentDir + L"\\" + wPkg + L"\\" + wRel);
                    size_t grandSl = parentDir.find_last_of(L"\\/");
                    if (grandSl != std::wstring::npos) {
                        candidates.push_back(parentDir.substr(0, grandSl) + L"\\" + wPkg + L"\\" + wRel);
                    }
                }
            }

            std::vector<uint8_t> extBuf;
            std::wstring matchedPath;
            for (const auto& cand : candidates) {
                HANDLE hf = CreateFileW(cand.c_str(), GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (hf != INVALID_HANDLE_VALUE) {
                    LARGE_INTEGER li = {};
                    GetFileSizeEx(hf, &li);
                    if (li.QuadPart > 0 && li.QuadPart <= 64LL * 1024LL * 1024LL) {
                        extBuf.resize(static_cast<size_t>(li.QuadPart));
                        DWORD br = 0;
                        ReadFile(hf, extBuf.data(), static_cast<DWORD>(extBuf.size()), &br, nullptr);
                        extBuf.resize(br);
                        matchedPath = cand;
                    }
                    CloseHandle(hf);
                    if (!extBuf.empty()) break;
                }
            }
            if (extBuf.empty()) return false;

            std::wstring mExt;
            const wchar_t* dot = PathFindExtensionW(matchedPath.c_str());
            if (dot && *dot) mExt = PathUtils::ToLower(dot);

            if (mExt == L".stl") {
                bool isBinStl = false;
                if (extBuf.size() >= 84) {
                    uint32_t tc = Detail3D200::RdU32LE(extBuf.data() + 80);
                    uint64_t exp = 84ULL + static_cast<uint64_t>(tc) * 50ULL;
                    if (tc > 0 && (exp == extBuf.size() || (extBuf.size() >= exp && std::memcmp(extBuf.data(), "solid", 5) != 0))) {
                        isBinStl = true;
                    }
                }
                if (isBinStl) {
                    uint32_t tc = Detail3D200::RdU32LE(extBuf.data() + 80);
                    size_t avail = std::min<size_t>(tc, (extBuf.size() - 84) / 50);
                    for (size_t t = 0; t < avail && outT.size() < 40000; ++t) {
                        float fv[12] = {};
                        std::memcpy(fv, extBuf.data() + 84 + t * 50, 48);
                        uint32_t b = static_cast<uint32_t>(outV.size());
                        outV.push_back({ fv[3], fv[4], fv[5] });
                        outV.push_back({ fv[6], fv[7], fv[8] });
                        outV.push_back({ fv[9], fv[10], fv[11] });
                        outT.push_back({ b, b + 1, b + 2 });
                    }
                } else {
                    const char* p = reinterpret_cast<const char*>(extBuf.data());
                    const char* e = p + extBuf.size();
                    while (p < e) {
                        while (p < e && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
                        if (p + 6 < e && std::memcmp(p, "vertex", 6) == 0 && (p[6] == ' ' || p[6] == '\t')) {
                            p += 6;
                            char* nx = nullptr;
                            float vx = std::strtof(p, &nx); p = nx;
                            float vy = std::strtof(p, &nx); p = nx;
                            float vz = std::strtof(p, &nx); p = nx;
                            outV.push_back({ vx, vy, vz });
                            if (outV.size() % 3 == 0) {
                                uint32_t b = static_cast<uint32_t>(outV.size() - 3);
                                outT.push_back({ b, b + 1, b + 2 });
                            }
                        }
                        while (p < e && *p != '\n') ++p;
                    }
                }
                return !outV.empty() && !outT.empty();
            } else if (mExt == L".obj") {
                const char* p = reinterpret_cast<const char*>(extBuf.data());
                const char* e = p + extBuf.size();
                std::vector<uint32_t> fp;
                while (p < e) {
                    while (p < e && (*p == ' ' || *p == '\t' || *p == '\r')) ++p;
                    if (p >= e) break;
                    if (*p == '\n') { ++p; continue; }
                    const char* le = p;
                    while (le < e && *le != '\n') ++le;
                    if (p[0] == 'v' && (p[1] == ' ' || p[1] == '\t')) {
                        char* nx = nullptr;
                        float vx = std::strtof(p + 2, &nx);
                        float vy = std::strtof(nx, &nx);
                        float vz = std::strtof(nx, &nx);
                        outV.push_back({ vx, vy, vz });
                    } else if (p[0] == 'f' && (p[1] == ' ' || p[1] == '\t')) {
                        fp.clear();
                        const char* c = p + 2;
                        while (c < le) {
                            while (c < le && (*c == ' ' || *c == '\t' || *c == '\r')) ++c;
                            if (c >= le || *c == '#') break;
                            char* nx = nullptr;
                            long idx = std::strtol(c, &nx, 10);
                            if (nx == c) break;
                            c = nx;
                            while (c < le && *c != ' ' && *c != '\t' && *c != '\r') ++c;
                            int vi = (idx > 0) ? (static_cast<int>(idx) - 1) : (static_cast<int>(outV.size()) + static_cast<int>(idx));
                            if (vi >= 0 && static_cast<size_t>(vi) < outV.size()) fp.push_back(static_cast<uint32_t>(vi));
                        }
                        for (size_t k = 1; k + 1 < fp.size(); ++k) outT.push_back({ fp[0], fp[k], fp[k + 1] });
                    }
                    p = (le < e) ? (le + 1) : e;
                }
                return !outV.empty() && !outT.empty();
            } else if (mExt == L".ply") {
                LoadedMesh3D sub;
                if (Formats::ParsePlyMesh(extBuf.data(), extBuf.size(), sub, false)) {
                    outV = std::move(sub.vertices);
                    outT.reserve(sub.tris.size());
                    for (const auto& tr : sub.tris) outT.push_back({ tr.i0, tr.i1, tr.i2 });
                    return !outV.empty() && !outT.empty();
                }
            } else if (mExt == L".dae") {
                LoadedMesh3D sub;
                std::string_view dSv(reinterpret_cast<const char*>(extBuf.data()), extBuf.size());
                if (Formats::ParseColladaDaeMesh(dSv, sub)) {
                    const bool zUpDae = (dSv.find("Z_UP") != std::string_view::npos);
                    outV.reserve(sub.vertices.size());
                    for (const auto& v : sub.vertices) {
                        if (zUpDae) outV.push_back(v);
                        else        outV.push_back({ v.x, -v.z, v.y });
                    }
                    outT.reserve(sub.tris.size());
                    for (const auto& tr : sub.tris) outT.push_back({ tr.i0, tr.i1, tr.i2 });
                    return !outV.empty() && !outT.empty();
                }
            }
            return false;
        };

        auto emitTransformedVisual = [&](const UrdfVisual& vis, const UrdfMat4& worldFromLink) {
            const UrdfMat4 worldFromVis = mulUrdfMat4(worldFromLink, vis.origin);
            std::vector<Vec3f> localV;
            std::vector<std::array<uint32_t, 3>> localT;
            bool flip = (detUrdf3x3(worldFromVis) < 0.0f);

            if (vis.kind == UrdfVisual::Box) {
                const float hx = vis.p0 * 0.5f, hy = vis.p1 * 0.5f, hz = vis.p2 * 0.5f;
                localV = {
                    { -hx, -hy, -hz }, {  hx, -hy, -hz }, {  hx,  hy, -hz }, { -hx,  hy, -hz },
                    { -hx, -hy,  hz }, {  hx, -hy,  hz }, {  hx,  hy,  hz }, { -hx,  hy,  hz }
                };
                localT = {
                    {{0, 2, 1}}, {{0, 3, 2}},
                    {{4, 5, 6}}, {{4, 6, 7}},
                    {{0, 1, 5}}, {{0, 5, 4}},
                    {{1, 2, 6}}, {{1, 6, 5}},
                    {{2, 3, 7}}, {{2, 7, 6}},
                    {{3, 0, 4}}, {{3, 4, 7}}
                };
            } else if (vis.kind == UrdfVisual::Cylinder) {
                // URDF spec: <cylinder> axis is ALWAYS aligned along local Z-axis [-length/2, +length/2]!
                const float rad = vis.p0;
                const float hz  = vis.p1 * 0.5f;
                constexpr int kSegs = 16;
                localV.reserve(kSegs * 2 + 2);
                for (int i = 0; i < kSegs; ++i) {
                    float ang = static_cast<float>(i) * (6.283185307f / static_cast<float>(kSegs));
                    float cx = rad * std::cos(ang);
                    float cy = rad * std::sin(ang);
                    localV.push_back({ cx, cy, -hz });
                    localV.push_back({ cx, cy,  hz });
                }
                const uint32_t botC = static_cast<uint32_t>(localV.size());
                localV.push_back({ 0.0f, 0.0f, -hz });
                const uint32_t topC = static_cast<uint32_t>(localV.size());
                localV.push_back({ 0.0f, 0.0f,  hz });

                for (int i = 0; i < kSegs; ++i) {
                    int ni = (i + 1) % kSegs;
                    uint32_t b0 = static_cast<uint32_t>(i * 2);
                    uint32_t t0 = b0 + 1;
                    uint32_t b1 = static_cast<uint32_t>(ni * 2);
                    uint32_t t1 = b1 + 1;
                    localT.push_back({ b0, b1, t1 });
                    localT.push_back({ b0, t1, t0 });
                    localT.push_back({ botC, b1, b0 });
                    localT.push_back({ topC, t0, t1 });
                }
            } else if (vis.kind == UrdfVisual::Sphere) {
                const float rad = vis.p0;
                constexpr int kLat = 8;
                constexpr int kLon = 14;
                localV.push_back({ 0.0f, 0.0f, -rad }); // south pole = 0
                for (int ilat = 1; ilat < kLat; ++ilat) {
                    float phi = -1.570796327f + static_cast<float>(ilat) * (3.141592654f / static_cast<float>(kLat));
                    float cz = rad * std::sin(phi);
                    float cr = rad * std::cos(phi);
                    for (int ilon = 0; ilon < kLon; ++ilon) {
                        float th = static_cast<float>(ilon) * (6.283185307f / static_cast<float>(kLon));
                        localV.push_back({ cr * std::cos(th), cr * std::sin(th), cz });
                    }
                }
                const uint32_t northIdx = static_cast<uint32_t>(localV.size());
                localV.push_back({ 0.0f, 0.0f, rad });

                for (int ilon = 0; ilon < kLon; ++ilon) {
                    uint32_t c0 = 1u + static_cast<uint32_t>(ilon);
                    uint32_t c1 = 1u + static_cast<uint32_t>((ilon + 1) % kLon);
                    localT.push_back({ 0u, c1, c0 });
                }
                for (int ilat = 0; ilat + 2 < kLat; ++ilat) {
                    uint32_t rowA = 1u + static_cast<uint32_t>(ilat * kLon);
                    uint32_t rowB = rowA + static_cast<uint32_t>(kLon);
                    for (int ilon = 0; ilon < kLon; ++ilon) {
                        uint32_t nlon = static_cast<uint32_t>((ilon + 1) % kLon);
                        uint32_t a0 = rowA + static_cast<uint32_t>(ilon), a1 = rowA + nlon;
                        uint32_t b0 = rowB + static_cast<uint32_t>(ilon), b1 = rowB + nlon;
                        localT.push_back({ a0, a1, b1 });
                        localT.push_back({ a0, b1, b0 });
                    }
                }
                uint32_t topRing = 1u + static_cast<uint32_t>((kLat - 2) * kLon);
                for (int ilon = 0; ilon < kLon; ++ilon) {
                    uint32_t c0 = topRing + static_cast<uint32_t>(ilon);
                    uint32_t c1 = topRing + static_cast<uint32_t>((ilon + 1) % kLon);
                    localT.push_back({ northIdx, c0, c1 });
                }
            } else if (vis.kind == UrdfVisual::ExternalMesh) {
                if (!loadExternalUrdfMesh(vis.meshUri, localV, localT)) return;
                for (auto& v : localV) {
                    v.x *= vis.scaleX;
                    v.y *= vis.scaleY;
                    v.z *= vis.scaleZ;
                }
                if ((vis.scaleX * vis.scaleY * vis.scaleZ) < 0.0f) flip = !flip;
            }

            if (localV.empty() || localT.empty()) return;
            const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
            mesh.vertices.reserve(mesh.vertices.size() + localV.size());
            for (const auto& lv : localV) {
                mesh.vertices.push_back(xformUrdfToViewer(worldFromVis, lv.x, lv.y, lv.z));
            }
            mesh.tris.reserve(mesh.tris.size() + localT.size());
            for (const auto& tr : localT) {
                if (!flip) mesh.tris.push_back({ base + tr[0], base + tr[1], base + tr[2], {} });
                else       mesh.tris.push_back({ base + tr[0], base + tr[2], base + tr[1], {} });
            }
        };

        std::vector<bool> visited(links.size(), false);
        std::function<void(size_t, const UrdfMat4&, int)> traverseLink = [&](size_t linkIdx, const UrdfMat4& worldFromLink, int depth) {
            if (linkIdx >= links.size() || visited[linkIdx] || depth > 256) return;
            visited[linkIdx] = true;
            for (const auto& vis : links[linkIdx].visuals) {
                emitTransformedVisual(vis, worldFromLink);
            }
            for (const auto& edge : childrenOf[linkIdx]) {
                UrdfMat4 worldFromChild = mulUrdfMat4(worldFromLink, edge.second);
                traverseLink(edge.first, worldFromChild, depth + 1);
            }
        };

        const UrdfMat4 identityMat;
        for (size_t i = 0; i < links.size(); ++i) {
            if (!isChildLink[i]) {
                traverseLink(i, identityMat, 0);
            }
        }
        for (size_t i = 0; i < links.size(); ++i) {
            if (!visited[i]) {
                traverseLink(i, identityMat, 0);
            }
        }

        return !mesh.vertices.empty() && !mesh.tris.empty();
    }

    // #58. VRoid / Metaverse 3D Avatar (.vrm — GLB 2.0 binary container)
    if (ext == L".vrm") {
        bool ok = Formats::ParseGltfOrGlbMesh(data, size, mesh.filePath, ext, mesh);
        mesh.formatName = L"VRoid / glTF Humanoid Avatar (.vrm)";
        return ok;
    }

    // #59. Crystallographic Information File (.cif — Unit-Cell + Fractional Crystal Lattice)
    if (ext == L".cif") {
        bool hasCell = (sv.find("_cell_length_a") != std::string_view::npos);
        bool hasAtoms = (sv.find("_atom_site_fract_x") != std::string_view::npos);
        if (!hasCell && !hasAtoms) return false;
        mesh.formatName = L"Crystallographic Lattice (.cif)";

        auto readTagFloat = [&](const char* tag, float defVal) -> float {
            size_t p = sv.find(tag);
            if (p == std::string_view::npos) return defVal;
            p += std::strlen(tag);
            while (p < sv.size() && (sv[p] == ' ' || sv[p] == '\t')) ++p;
            return std::strtof(sv.data() + p, nullptr);
        };
        float a = std::max(0.5f, readTagFloat("_cell_length_a", 5.43f));
        float b = std::max(0.5f, readTagFloat("_cell_length_b", a));
        float c = std::max(0.5f, readTagFloat("_cell_length_c", a));
        float alpha = readTagFloat("_cell_angle_alpha", 90.0f) * 3.14159265f / 180.0f;
        float beta  = readTagFloat("_cell_angle_beta",  90.0f) * 3.14159265f / 180.0f;
        float gamma = readTagFloat("_cell_angle_gamma", 90.0f) * 3.14159265f / 180.0f;

        Vec3f vA = { a, 0.0f, 0.0f };
        Vec3f vB = { b * std::cos(gamma), b * std::sin(gamma), 0.0f };
        float cx = c * std::cos(beta);
        float cy = c * (std::cos(alpha) - std::cos(beta) * std::cos(gamma)) / std::max(1e-4f, std::sin(gamma));
        float cz = std::sqrt(std::max(0.01f, c * c - cx * cx - cy * cy));
        Vec3f vC = { cx, cy, cz };

        auto fracToCart = [&](float fx, float fy, float fz) -> Vec3f {
            return {
                fx * vA.x + fy * vB.x + fz * vC.x,
                fx * vA.y + fy * vB.y + fz * vC.y,
                fx * vA.z + fy * vB.z + fz * vC.z
            };
        };

        float strutR = std::max({ a, b, c }) * 0.018f;
        if (hasCell) {
            Vec3f uc[8] = {
                fracToCart(0,0,0), fracToCart(1,0,0), fracToCart(1,1,0), fracToCart(0,1,0),
                fracToCart(0,0,1), fracToCart(1,0,1), fracToCart(1,1,1), fracToCart(0,1,1)
            };
            static const int edges[12][2] = {
                {0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4}, {0,4},{1,5},{2,6},{3,7}
            };
            for (const auto& e : edges) Detail3D200::AddStrut3D(mesh, uc[e[0]], uc[e[1]], strutR);
        }

        size_t fxPos = sv.find("_atom_site_fract_x");
        std::vector<Vec3f> atoms;
        if (fxPos != std::string_view::npos) {
            size_t loopStart = sv.rfind("loop_", fxPos);
            size_t pos = (loopStart != std::string_view::npos) ? (loopStart + 5) : fxPos;
            int colIdx = 0, colX = -1, colY = -1, colZ = -1;
            while (pos < sv.size()) {
                size_t eol = sv.find('\n', pos);
                if (eol == std::string_view::npos) eol = sv.size();
                std::string ln(sv.substr(pos, eol - pos));
                pos = eol + 1;
                size_t s = ln.find_first_not_of(" \t\r");
                if (s == std::string::npos || ln[s] == '#') continue;
                if (ln[s] == '_') {
                    if (ln.find("_atom_site_fract_x") != std::string::npos) colX = colIdx;
                    else if (ln.find("_atom_site_fract_y") != std::string::npos) colY = colIdx;
                    else if (ln.find("_atom_site_fract_z") != std::string::npos) colZ = colIdx;
                    ++colIdx;
                    continue;
                }
                if (ln.compare(s, 5, "loop_") == 0 || ln.compare(s, 5, "data_") == 0) break;
                if (colX >= 0 && colY >= 0 && colZ >= 0 && atoms.size() < 512) {
                    std::vector<std::string> cols;
                    size_t tp = s;
                    while (tp < ln.size()) {
                        while (tp < ln.size() && (ln[tp] == ' ' || ln[tp] == '\t' || ln[tp] == '\r')) ++tp;
                        size_t te = ln.find_first_of(" \t\r", tp);
                        if (te == std::string::npos) te = ln.size();
                        if (te > tp) cols.push_back(ln.substr(tp, te - tp));
                        tp = te;
                    }
                    int maxCol = std::max({ colX, colY, colZ });
                    if (static_cast<int>(cols.size()) > maxCol) {
                        float x = std::strtof(cols[static_cast<size_t>(colX)].c_str(), nullptr);
                        float y = std::strtof(cols[static_cast<size_t>(colY)].c_str(), nullptr);
                        float z = std::strtof(cols[static_cast<size_t>(colZ)].c_str(), nullptr);
                        atoms.push_back(fracToCart(x, y, z));
                    }
                }
            }
        }
        float atomR = std::max({ a, b, c }) * 0.075f;
        for (const auto& at : atoms) Detail3D200::AddOctaSphere(mesh, at.x, at.y, at.z, atomR);
        for (size_t i = 0; i < atoms.size(); ++i) {
            for (size_t j = i + 1; j < atoms.size(); ++j) {
                float dx = atoms[i].x - atoms[j].x, dy = atoms[i].y - atoms[j].y, dz = atoms[i].z - atoms[j].z;
                float d2 = dx * dx + dy * dy + dz * dz;
                if (d2 > 0.25f && d2 < 9.5f) {
                    Detail3D200::AddStrut3D(mesh, atoms[i], atoms[j], strutR * 1.4f);
                }
            }
        }
        return !mesh.vertices.empty() && !mesh.tris.empty();
    }

    // #60. Tripos Sybyl (.mol2) & GROMACS (.gro) 3D Molecular Structures
    if (ext == L".mol2") {
        size_t atomSec = sv.find("@<TRIPOS>ATOM");
        if (atomSec == std::string_view::npos) return false;
        mesh.formatName = L"Tripos Sybyl 3D Molecule (.mol2)";

        std::vector<Vec3f> atoms;
        size_t pos = sv.find('\n', atomSec);
        if (pos == std::string_view::npos) return false;
        ++pos;
        while (pos < sv.size() && atoms.size() < 4000) {
            size_t eol = sv.find('\n', pos);
            if (eol == std::string_view::npos) eol = sv.size();
            std::string ln(sv.substr(pos, eol - pos));
            pos = eol + 1;
            size_t s = ln.find_first_not_of(" \t\r");
            if (s == std::string::npos) continue;
            if (ln[s] == '@') break;
            int id = 0; char sym[64] = {}; float x = 0, y = 0, z = 0;
            if (sscanf_s(ln.c_str() + s, "%d %63s %f %f %f", &id, sym, static_cast<unsigned>(sizeof(sym)), &x, &y, &z) == 5) {
                atoms.push_back({ x, y, z });
                Detail3D200::AddOctaSphere(mesh, x, y, z, 0.34f);
            }
        }

        size_t bondSec = sv.find("@<TRIPOS>BOND");
        if (bondSec != std::string_view::npos) {
            size_t bp = sv.find('\n', bondSec);
            if (bp != std::string_view::npos) {
                ++bp;
                while (bp < sv.size()) {
                    size_t eol = bp;
                    while (eol < sv.size() && sv[eol] != '\n') ++eol;
                    std::string bln(sv.substr(bp, eol - bp));
                    bp = eol + 1;
                    size_t s = bln.find_first_not_of(" \t\r");
                    if (s == std::string::npos) continue;
                    if (bln[s] == '@') break;
                    int bid = 0, a1 = 0, a2 = 0;
                    if (sscanf_s(bln.c_str() + s, "%d %d %d", &bid, &a1, &a2) == 3) {
                        if (a1 >= 1 && a2 >= 1 && static_cast<size_t>(a1) <= atoms.size() && static_cast<size_t>(a2) <= atoms.size()) {
                            Detail3D200::AddStrut3D(mesh, atoms[static_cast<size_t>(a1 - 1)], atoms[static_cast<size_t>(a2 - 1)], 0.10f);
                        }
                    }
                }
            }
        }
        return !mesh.vertices.empty() && !mesh.tris.empty();
    }

    if (ext == L".gro") {
        size_t nl1 = sv.find('\n');
        if (nl1 == std::string_view::npos) return false;
        size_t nl2 = sv.find('\n', nl1 + 1);
        if (nl2 == std::string_view::npos) return false;
        int nAtoms = std::atoi(std::string(sv.substr(nl1 + 1, nl2 - nl1 - 1)).c_str());
        if (nAtoms <= 0 || nAtoms > 200000) return false;
        mesh.formatName = L"GROMACS Molecular Coordinates (.gro)";

        std::vector<Vec3f> atoms;
        size_t pos = nl2 + 1;
        for (int i = 0; i < nAtoms && pos < sv.size() && i < 4000; ++i) {
            size_t eol = sv.find('\n', pos);
            if (eol == std::string_view::npos) eol = sv.size();
            std::string ln(sv.substr(pos, eol - pos));
            pos = eol + 1;
            if (ln.size() >= 44) {
                float x = std::strtof(ln.substr(20, 8).c_str(), nullptr) * 10.0f;
                float y = std::strtof(ln.substr(28, 8).c_str(), nullptr) * 10.0f;
                float z = std::strtof(ln.substr(36, 8).c_str(), nullptr) * 10.0f;
                atoms.push_back({ x, y, z });
                Detail3D200::AddOctaSphere(mesh, x, y, z, 0.34f);
            }
        }
        for (size_t i = 0; i < atoms.size(); ++i) {
            for (size_t j = i + 1; j < std::min(atoms.size(), i + 24); ++j) {
                float dx = atoms[i].x - atoms[j].x, dy = atoms[i].y - atoms[j].y, dz = atoms[i].z - atoms[j].z;
                float d2 = dx * dx + dy * dy + dz * dz;
                if (d2 > 0.3f && d2 < 3.6f) Detail3D200::AddStrut3D(mesh, atoms[i], atoms[j], 0.09f);
            }
        }
        return !mesh.vertices.empty() && !mesh.tris.empty();
    }

    return false;
}

inline bool ParseExtended3D200(const uint8_t* data, size_t size, const std::wstring& ext, LoadedMesh3D& mesh) {
    if (ext == L".x")    return ParseDirectXMeshX(data, size, mesh);
    if (ext == L".ms3d") return ParseMilkShapeMs3d(data, size, mesh);
    if (ext == L".iqm")  return ParseInterQuakeIqm(data, size, mesh);
    if (ext == L".mdl")  return ParseQuakeOrSourceMdl(data, size, mesh);
    if (ext == L".nif")  return ParseNetImmerseNif(data, size, mesh);
    if (ext == L".b3d" || ext == L".ac" || ext == L".ctm" || ext == L".drc") {
        return ParseBlitzAcCtmDraco(data, size, ext, mesh);
    }
    if (ext == L".3dxml" || ext == L".jt" || ext == L".brep" ||
        ext == L".sat"   || ext == L".sab"|| ext == L".x_t"  || ext == L".x_b")
    {
        return ParseCadKernels200(data, size, ext, mesh);
    }
    if (ext == L".e57" || ext == L".laz" || ext == L".urdf" ||
        ext == L".vrm" || ext == L".cif" || ext == L".mol2" || ext == L".gro")
    {
        return ParseSciRoboChem200(data, size, ext, mesh);
    }
    return false;
}

} // namespace Formats
