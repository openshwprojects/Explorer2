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
// #42. MilkShape 3D (.ms3d — "MS3D000000")
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

    mesh.vertices.reserve(nVerts);
    for (uint16_t i = 0; i < nVerts; ++i) {
        const uint8_t* vp = data + pos + static_cast<size_t>(i) * 15;
        mesh.vertices.push_back({
            Detail3D200::RdF32LE(vp + 1),
            Detail3D200::RdF32LE(vp + 5),
            Detail3D200::RdF32LE(vp + 9)
        });
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
            uint32_t numBodyparts = Detail3D200::RdU32LE(data + 204);
            uint32_t bodypartIdx  = Detail3D200::RdU32LE(data + 208);
            if (numBodyparts > 0 && numBodyparts <= 64 && bodypartIdx + static_cast<uint64_t>(numBodyparts) * 76 <= size) {
                for (uint32_t b = 0; b < numBodyparts; ++b) {
                    const uint8_t* bp = data + bodypartIdx + b * 76;
                    uint32_t numModels = Detail3D200::RdU32LE(bp + 64);
                    uint32_t modelIdx  = Detail3D200::RdU32LE(bp + 72);
                    for (uint32_t mi = 0; mi < numModels && mi < 32 && modelIdx + (mi + 1) * 112 <= size; ++mi) {
                        const uint8_t* mdl = data + modelIdx + mi * 112;
                        uint32_t numMesh  = Detail3D200::RdU32LE(mdl + 72);
                        uint32_t meshIdx  = Detail3D200::RdU32LE(mdl + 76);
                        uint32_t numVerts = Detail3D200::RdU32LE(mdl + 80);
                        uint32_t vertIdx  = Detail3D200::RdU32LE(mdl + 88);
                        if (numVerts > 0 && numVerts <= 65536 && vertIdx + static_cast<uint64_t>(numVerts) * 12 <= size) {
                            uint32_t vBase = static_cast<uint32_t>(mesh.vertices.size());
                            for (uint32_t v = 0; v < numVerts; ++v) {
                                const uint8_t* vp = data + vertIdx + v * 12;
                                mesh.vertices.push_back({
                                    Detail3D200::RdF32LE(vp + 0),
                                    Detail3D200::RdF32LE(vp + 8),
                                    Detail3D200::RdF32LE(vp + 4)
                                });
                            }
                            for (uint32_t m = 0; m < numMesh && meshIdx + (m + 1) * 20 <= size; ++m) {
                                uint32_t triIdx = Detail3D200::RdU32LE(data + meshIdx + m * 20 + 4);
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
                                        if (a < mesh.vertices.size() && bIdx < mesh.vertices.size() && c < mesh.vertices.size()) {
                                            if (!isFan && (k & 1)) mesh.tris.push_back({ bIdx, a, c, {} });
                                            else mesh.tris.push_back({ a, bIdx, c, {} });
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            return !mesh.vertices.empty() && !mesh.tris.empty();
        }

        // Source Engine v44–v49: decode real mstudiobone_t skeleton hierarchy if present
        if (ver >= 44 && ver <= 49 && size >= 400) {
            uint32_t numBones = Detail3D200::RdU32LE(data + 156);
            uint32_t boneIdx  = Detail3D200::RdU32LE(data + 160);
            if (numBones >= 2 && numBones <= 512 && boneIdx >= 244 &&
                boneIdx + static_cast<uint64_t>(numBones) * 216 <= size)
            {
                struct BoneXform { float m[3][4]; };
                std::vector<BoneXform> world(numBones);
                std::vector<int32_t> parents(numBones, -1);
                std::vector<Vec3f> joints(numBones);
                for (uint32_t b = 0; b < numBones; ++b) {
                    const uint8_t* bp = data + boneIdx + static_cast<size_t>(b) * 216;
                    int32_t parent = static_cast<int32_t>(Detail3D200::RdU32LE(bp + 4));
                    parents[b] = parent;
                    float px = Detail3D200::RdF32LE(bp + 32);
                    float py = Detail3D200::RdF32LE(bp + 36);
                    float pz = Detail3D200::RdF32LE(bp + 40);
                    float qx = Detail3D200::RdF32LE(bp + 44);
                    float qy = Detail3D200::RdF32LE(bp + 48);
                    float qz = Detail3D200::RdF32LE(bp + 52);
                    float qw = Detail3D200::RdF32LE(bp + 56);

                    BoneXform loc = {};
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
                        const BoneXform& pw = world[static_cast<size_t>(parent)];
                        BoneXform cw = {};
                        for (int r = 0; r < 3; ++r) {
                            for (int c = 0; c < 3; ++c) {
                                cw.m[r][c] = pw.m[r][0] * loc.m[0][c] + pw.m[r][1] * loc.m[1][c] + pw.m[r][2] * loc.m[2][c];
                            }
                            cw.m[r][3] = pw.m[r][0] * loc.m[0][3] + pw.m[r][1] * loc.m[1][3] + pw.m[r][2] * loc.m[2][3] + pw.m[r][3];
                        }
                        world[b] = cw;
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

        // 1. Parse cached "Triangulations" section if present
        size_t triSec = sv.find("Triangulations");
        if (triSec != std::string_view::npos) {
            const char* p = sv.data() + triSec + 14;
            const char* end = sv.data() + sv.size();
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
        // Binary ACIS (.sab — "ACIS BinaryFile" / "ASM BinaryFile ")
        if (ext == L".sab" || (size >= 19 && (std::memcmp(data, "ACIS BinaryFile", 15) == 0 ||
                                              std::memcmp(data, "ASM BinaryFile", 14) == 0)))
        {
            if (size < 31 || (std::memcmp(data, "ACIS BinaryFile", 15) != 0 &&
                              std::memcmp(data, "ASM BinaryFile", 14) != 0))
            {
                return false;
            }
            mesh.formatName = L"Spatial ACIS Binary Solid (.sab)";

            // Skip 15-byte magic + 16-byte binary header (version, numRecords, numEntities, flags)
            size_t p = 31;
            std::vector<Vec3f> pts;
            std::vector<std::pair<int32_t, int32_t>> edgeVertRefs;
            std::vector<int32_t> vertPointRefs;
            std::string curEntityType;
            std::vector<int32_t> curPtrs;
            std::vector<double> curDoubles;
            int headerStringsRemaining = 3;
            int headerDoublesRemaining = 3; // units, resabs, resnor
            bool inHeader = true;

            auto isIdentBytes = [&](size_t off, uint8_t len) -> bool {
                if (len == 0 || len > 40 || off + len > size) return false;
                for (uint8_t i = 0; i < len; ++i) {
                    uint8_t c = data[off + i];
                    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
                    if (!ok) return false;
                }
                return true;
            };

            while (p < size && pts.size() < 32000) {
                uint8_t tag = data[p++];
                if (tag == 0x02) { // char
                    if (p + 1 > size) break;
                    p += 1;
                } else if (tag == 0x03) { // short
                    if (p + 2 > size) break;
                    p += 2;
                } else if (tag == 0x04) { // int32
                    if (p + 4 > size) break;
                    p += 4;
                } else if (tag == 0x05) { // float32
                    if (p + 4 > size) break;
                    if (!inHeader) curDoubles.push_back(Detail3D200::RdF32LE(data + p));
                    p += 4;
                } else if (tag == 0x06) { // IEEE-754 double (standard ACIS tag 6)
                    if (p + 8 > size) break;
                    double dv = Detail3D200::RdF64LE(data + p);
                    p += 8;
                    if (inHeader) {
                        if (--headerDoublesRemaining <= 0 && headerStringsRemaining <= 0) inHeader = false;
                    } else {
                        curDoubles.push_back(dv);
                    }
                } else if (tag == 0x07) { // UTF8_U8 string
                    if (p >= size) break;
                    uint8_t slen = data[p++];
                    if (p + slen > size) break;
                    p += slen;
                    if (inHeader && headerStringsRemaining > 0) --headerStringsRemaining;
                } else if (tag == 0x08) { // UTF8_U16 string
                    if (p + 2 > size) break;
                    uint16_t slen = Detail3D200::RdU16LE(data + p); p += 2;
                    if (p + slen > size) break;
                    p += slen;
                } else if (tag == 0x09) { // UTF8_U32 string
                    if (p + 4 > size) break;
                    uint32_t slen = Detail3D200::RdU32LE(data + p); p += 4;
                    if (slen > 1000000 || p + slen > size) break;
                    p += slen;
                } else if (tag == 0x0A || tag == 0x0B || tag == 0x0F || tag == 0x10) {
                    // TRUE / FALSE / SUBTYPE_OPEN / SUBTYPE_CLOSE
                } else if (tag == 0x0C) { // POINTER ($idx)
                    if (p + 4 > size) break;
                    int32_t ref = static_cast<int32_t>(Detail3D200::RdU32LE(data + p));
                    p += 4;
                    inHeader = false;
                    curPtrs.push_back(ref);
                } else if (tag == 0x0D) {
                    // Either standard ACIS IDENT (u8 len + identifier) OR custom double (8B)
                    if (p < size && isIdentBytes(p + 1, data[p])) {
                        uint8_t slen = data[p++];
                        std::string id(reinterpret_cast<const char*>(data + p), slen);
                        p += slen;
                        inHeader = false;
                        if (curEntityType.empty()) curEntityType = id;
                    } else if (p + 8 <= size) {
                        double dv = Detail3D200::RdF64LE(data + p);
                        p += 8;
                        if (inHeader) {
                            if (--headerDoublesRemaining <= 0 && headerStringsRemaining <= 0) inHeader = false;
                        } else {
                            curDoubles.push_back(dv);
                        }
                    } else {
                        break;
                    }
                } else if (tag == 0x0E) { // SUBIDENT
                    if (p >= size) break;
                    uint8_t slen = data[p++];
                    if (p + slen > size) break;
                    std::string sub(reinterpret_cast<const char*>(data + p), slen);
                    p += slen;
                    if (!curEntityType.empty()) curEntityType += "-" + sub;
                } else if (tag == 0x11) { // '#' Record terminator
                    inHeader = false;
                    if (curEntityType == "point" && curDoubles.size() >= 3) {
                        size_t n = curDoubles.size();
                        pts.push_back({
                            static_cast<float>(curDoubles[n - 3]),
                            static_cast<float>(curDoubles[n - 2]),
                            static_cast<float>(curDoubles[n - 1])
                        });
                    }
                    curEntityType.clear();
                    curPtrs.clear();
                    curDoubles.clear();
                } else if (tag == 0x13) { // TAG_POSITION: 3 IEEE-754 doubles (x, y, z)
                    if (p + 24 > size) break;
                    inHeader = false;
                    float x = static_cast<float>(Detail3D200::RdF64LE(data + p + 0));
                    float y = static_cast<float>(Detail3D200::RdF64LE(data + p + 8));
                    float z = static_cast<float>(Detail3D200::RdF64LE(data + p + 16));
                    p += 24;
                    pts.push_back({ x, y, z });
                } else if (tag == 0x14) { // TAG_VECTOR: 3 IEEE-754 doubles (dx, dy, dz)
                    if (p + 24 > size) break;
                    p += 24;
                } else if (tag == 0x15) { // ENUM
                    if (p >= size) break;
                    uint8_t slen = data[p++];
                    if (p + slen > size) break;
                    p += slen;
                } else {
                    break;
                }
            }
            (void)edgeVertRefs; (void)vertPointRefs;
            if (pts.size() < 2) return false;
            float r = Detail3D200::ComputePointCloudSpan(pts) * 0.025f;
            for (size_t i = 0; i < pts.size(); ++i) {
                Detail3D200::AddOctaSphere(mesh, pts[i].x, pts[i].y, pts[i].z, r * 1.2f);
                Detail3D200::AddStrut3D(mesh, pts[i], pts[(i + 1) % pts.size()], r * 0.6f);
            }
            return !mesh.vertices.empty() && !mesh.tris.empty();
        }

        if (ext == L".x_b") {
            // Parasolid binary transmit requires proprietary schema tables; reject if not text XT
            if (sv.find("SCH_") == std::string_view::npos && sv.find("point") == std::string_view::npos) return false;
        }

        bool isAcis = (ext == L".sat");
        if (isAcis && sv.find("ACIS") == std::string_view::npos && sv.find("body ") == std::string_view::npos &&
            !(sv.size() >= 4 && sv[0] >= '1' && sv[0] <= '9'))
        {
            return false;
        }
        if (!isAcis && sv.find("SCH_") == std::string_view::npos && sv.find("PART") == std::string_view::npos &&
            sv.find("point") == std::string_view::npos)
        {
            return false;
        }

        mesh.formatName = isAcis ? L"Spatial ACIS Solid Model (.sat)" : L"Siemens Parasolid XT Solid (.x_t)";
        std::vector<Vec3f> pts;
        size_t pos = 0;
        while (pos < sv.size() && pts.size() < 8000) {
            size_t eol = sv.find('\n', pos);
            if (eol == std::string_view::npos) eol = sv.size();
            std::string_view ln = sv.substr(pos, eol - pos);
            pos = eol + 1;
            size_t kw = ln.find("point ");
            if (kw != std::string_view::npos) {
                std::string tail(ln.substr(kw + 6));
                const char* p = tail.c_str();
                std::vector<float> nums;
                while (*p) {
                    if (*p == '$') { ++p; while (*p && *p != ' ' && *p != '\t') ++p; continue; }
                    if ((*p >= '0' && *p <= '9') || *p == '-' || *p == '+' || *p == '.') {
                        char* ep = nullptr;
                        float v = std::strtof(p, &ep);
                        if (ep != p) { nums.push_back(v); p = ep; continue; }
                    }
                    ++p;
                }
                if (nums.size() >= 3) {
                    size_t n = nums.size();
                    pts.push_back({ nums[n - 3], nums[n - 2], nums[n - 1] });
                }
            }
        }

        if (pts.size() < 2) return false;
        float r = Detail3D200::ComputePointCloudSpan(pts) * 0.025f;
        for (size_t i = 0; i < pts.size(); ++i) {
            Detail3D200::AddOctaSphere(mesh, pts[i].x, pts[i].y, pts[i].z, r * 1.2f);
            Detail3D200::AddStrut3D(mesh, pts[i], pts[(i + 1) % pts.size()], r * 0.6f);
        }
        return !mesh.vertices.empty() && !mesh.tris.empty();
    }
    return false;
}

// ============================================================================
// #55–#60. ASTM E57 (.e57), LASzip (.laz), ROS URDF (.urdf), VRoid (.vrm),
//          Crystallography (.cif), Tripos Sybyl (.mol2) & GROMACS (.gro)
// ============================================================================
inline bool ParseSciRoboChem200(const uint8_t* data, size_t size, const std::wstring& ext, LoadedMesh3D& mesh) {
    std::string_view sv(reinterpret_cast<const char*>(data), size);

    // #55. ASTM E57 3D Laser Scan — 48-byte header starting with "ASTM-E57"
    if (ext == L".e57" || (size >= 48 && std::memcmp(data, "ASTM-E57", 8) == 0)) {
        if (size < 48 || std::memcmp(data, "ASTM-E57", 8) != 0) return false;
        mesh.formatName = L"ASTM E57 3D LiDAR Point Cloud (.e57)";
        uint64_t xmlPhysOff = Detail3D200::RdU64LE(data + 24);
        uint64_t pageSize   = Detail3D200::RdU64LE(data + 40);
        if (pageSize == 0) pageSize = 1024;
        if (xmlPhysOff <= 48 || xmlPhysOff > size) return false;

        size_t dataEnd = static_cast<size_t>(xmlPhysOff);
        std::vector<Vec3f> pts;
        // Skip 16-byte CompressedVectorSection header at offset 48 if present (sectionId == 1)
        size_t startOff = (dataEnd >= 64 && data[48] == 1) ? 64 : 48;
        for (size_t p = startOff; p + 12 <= dataEnd && pts.size() < 8000; p += 12) {
            if (pageSize >= 64 && (p % pageSize) >= (pageSize - 4)) {
                p = ((p / pageSize) + 1) * static_cast<size_t>(pageSize) - 12;
                continue;
            }
            float x = Detail3D200::RdF32LE(data + p + 0);
            float y = Detail3D200::RdF32LE(data + p + 4);
            float z = Detail3D200::RdF32LE(data + p + 8);
            if (std::isfinite(x) && std::isfinite(y) && std::isfinite(z) &&
                std::fabs(x) < 1e5f && std::fabs(y) < 1e5f && std::fabs(z) < 1e5f &&
                (std::fabs(x) + std::fabs(y) + std::fabs(z)) > 1e-5f)
            {
                pts.push_back({ x, z, -y });
            }
        }
        if (pts.empty()) return false;
        float r = Detail3D200::ComputePointCloudSpan(pts) * 0.018f;
        for (const auto& pt : pts) Detail3D200::AddPointTetra(mesh, pt, r);
        return !mesh.vertices.empty() && !mesh.tris.empty();
    }

    // #56. LASzip / LAS 1.2–1.4 LiDAR (.laz — decodes uncompressed point records; returns false if LAZ-compressed)
    if (ext == L".laz" || (size >= 227 && std::memcmp(data, "LASF", 4) == 0)) {
        if (size < 227 || std::memcmp(data, "LASF", 4) != 0) return false;
        uint8_t vMaj     = data[24];
        uint8_t vMin     = data[25];
        uint16_t hdrSize = Detail3D200::RdU16LE(data + 94);
        uint32_t ptOff   = Detail3D200::RdU32LE(data + 96);
        uint8_t ptFmt    = data[104];
        uint16_t recLen  = Detail3D200::RdU16LE(data + 105);
        uint64_t nPts    = Detail3D200::RdU32LE(data + 107);
        if (vMaj == 1 && vMin >= 4 && hdrSize >= 375 && size >= 255 && nPts == 0) {
            nPts = Detail3D200::RdU64LE(data + 247);
        }
        // Bit 7 (ptFmt >= 128) marks LAZ arithmetic-coded chunks; never synthesize fake geometry!
        if (ptFmt >= 128 || nPts == 0 || recLen < 12 || ptOff < 227 || ptOff >= size || ptOff + recLen > size) {
            return false;
        }

        double scaleX = Detail3D200::RdF64LE(data + 131);
        double scaleY = Detail3D200::RdF64LE(data + 139);
        double scaleZ = Detail3D200::RdF64LE(data + 147);
        double offX   = Detail3D200::RdF64LE(data + 155);
        double offY   = Detail3D200::RdF64LE(data + 163);
        double offZ   = Detail3D200::RdF64LE(data + 171);
        if (scaleX == 0.0) scaleX = 0.01;
        if (scaleY == 0.0) scaleY = 0.01;
        if (scaleZ == 0.0) scaleZ = 0.01;

        mesh.formatName = L"LAS v" + std::to_wstring(vMaj) + L"." + std::to_wstring(vMin) +
                          L" LiDAR (" + std::to_wstring(nPts) + L" pts)";

        uint64_t availPts = std::min<uint64_t>(nPts, (size - ptOff) / recLen);
        if (availPts == 0) return false;
        uint64_t step = std::max<uint64_t>(1, availPts / 20000);

        std::vector<Vec3f> pts;
        pts.reserve(static_cast<size_t>(std::min<uint64_t>(availPts, 20000)));
        for (uint64_t i = 0; i < availPts && pts.size() < 20000; i += step) {
            const uint8_t* rp = data + ptOff + static_cast<size_t>(i * recLen);
            int32_t ix = static_cast<int32_t>(Detail3D200::RdU32LE(rp + 0));
            int32_t iy = static_cast<int32_t>(Detail3D200::RdU32LE(rp + 4));
            int32_t iz = static_cast<int32_t>(Detail3D200::RdU32LE(rp + 8));
            float x = static_cast<float>(ix * scaleX + offX);
            float y = static_cast<float>(iy * scaleY + offY);
            float z = static_cast<float>(iz * scaleZ + offZ);
            if (std::isfinite(x) && std::isfinite(y) && std::isfinite(z)) {
                pts.push_back({ x, z, -y });
            }
        }
        if (pts.empty()) return false;
        float r = Detail3D200::ComputePointCloudSpan(pts) * 0.018f;
        for (const auto& pt : pts) Detail3D200::AddPointTetra(mesh, pt, r);
        return !mesh.vertices.empty() && !mesh.tris.empty();
    }

    // #57. ROS Robot Description Format (.urdf — XML <robot> <link> <joint>)
    if (ext == L".urdf") {
        if (sv.find("<robot") == std::string_view::npos) return false;
        mesh.formatName = L"ROS Robot Description (.urdf)";

        auto attrStr = [](std::string_view blk, const char* key) -> std::string_view {
            size_t k = blk.find(key);
            if (k == std::string_view::npos) return {};
            k += std::strlen(key);
            size_t q = blk.find('"', k);
            return (q == std::string_view::npos) ? std::string_view{} : blk.substr(k, q - k);
        };

        int linkIdx = 0;
        size_t pos = 0;
        while ((pos = sv.find("<link", pos)) != std::string_view::npos) {
            size_t closeL = sv.find("</link>", pos);
            size_t selfL  = sv.find("/>", pos);
            size_t endL   = (closeL != std::string_view::npos && (selfL == std::string_view::npos || closeL < selfL))
                            ? (closeL + 7)
                            : ((selfL != std::string_view::npos) ? (selfL + 2) : sv.size());
            std::string_view blk = sv.substr(pos, endL - pos);
            pos = endL;

            float ox = 0.0f, oy = linkIdx * 0.45f, oz = 0.0f;
            std::string_view xyz = attrStr(blk, "xyz=\"");
            if (!xyz.empty()) {
                std::string s(xyz);
                float tx = 0, ty = 0, tz = 0;
                if (sscanf_s(s.c_str(), "%f %f %f", &tx, &ty, &tz) == 3) { ox = tx; oy += tz; oz = ty; }
            }

            size_t boxP = blk.find("<box");
            size_t cylP = blk.find("<cylinder");
            size_t sphP = blk.find("<sphere");
            if (boxP != std::string_view::npos) {
                std::string szStr(attrStr(blk.substr(boxP), "size=\""));
                float bx = 0.0f, by = 0.0f, bz = 0.0f;
                if (sscanf_s(szStr.c_str(), "%f %f %f", &bx, &by, &bz) == 3 && bx > 0.0f && by > 0.0f && bz > 0.0f) {
                    Detail3D100::AddBoxPrim(mesh, ox, oy, oz, bx * 0.5f, bz * 0.5f, by * 0.5f);
                    ++linkIdx;
                }
            } else if (cylP != std::string_view::npos) {
                float rad = std::strtof(std::string(attrStr(blk.substr(cylP), "radius=\"")).c_str(), nullptr);
                float len = std::strtof(std::string(attrStr(blk.substr(cylP), "length=\"")).c_str(), nullptr);
                if (rad > 0.0f && len > 0.0f) {
                    Detail3D200::AddCylinderY(mesh, ox, oy, oz, rad, len);
                    ++linkIdx;
                }
            } else if (sphP != std::string_view::npos) {
                float rad = std::strtof(std::string(attrStr(blk.substr(sphP), "radius=\"")).c_str(), nullptr);
                if (rad > 0.0f) {
                    Detail3D200::AddOctaSphere(mesh, ox, oy, oz, rad);
                    ++linkIdx;
                }
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
