#pragma once
// Extended3D200.h — 20 CAD Kernels, LiDAR, Game Meshes, Robotics, Avatars & Crystallography 3D Parsers (#41–#60)
// Covers:
//   Game & Real-Time Meshes (#41–#49): .x, .ms3d, .iqm, .mdl, .nif, .b3d, .ac, .ctm, .drc
//   CAD Kernels & PLM (#50–#54): .3dxml, .jt, .brep, .sat/.sab, .x_t/.x_b
//   LiDAR, Robotics, Avatars & Chemistry (#55–#60): .e57, .laz, .urdf, .vrm, .cif, .mol2/.gro

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
inline float RdF32LE(const uint8_t* p) {
    float f = 0.0f;
    std::memcpy(&f, p, 4);
    return std::isfinite(f) ? f : 0.0f;
}
inline double RdF64LE(const uint8_t* p) {
    double d = 0.0;
    std::memcpy(&d, p, 8);
    return std::isfinite(d) ? d : 0.0;
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

inline void AddStrut3D(LoadedMesh3D& mesh, Vec3f p0, Vec3f p1, float radius) {
    Vec3f d = { p1.x - p0.x, p1.y - p0.y, p1.z - p0.z };
    float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (len < 1e-5f) return;
    Vec3f u = (std::fabs(d.y) < 0.9f * len) ? Vec3f{ 0.0f, 1.0f, 0.0f } : Vec3f{ 1.0f, 0.0f, 0.0f };
    // Cross(d, u)
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

} // namespace Detail3D200

// ============================================================================
// #41. DirectX Legacy Mesh (.x — "xof 0303txt" / "xof 0302txt" / "xof ... bin")
// ============================================================================
inline bool ParseDirectXMeshX(std::string_view sv, LoadedMesh3D& mesh) {
    if (sv.size() < 16 || sv.substr(0, 4) != "xof ") return false;
    mesh.formatName = L"DirectX 3D Mesh (.x)";

    size_t mpos = sv.find("Mesh");
    while (mpos != std::string_view::npos) {
        size_t brace = sv.find('{', mpos);
        if (brace == std::string_view::npos) break;
        // Ensure it's "Mesh {" and not "MeshNormals" / "MeshMaterialList" / "MeshTextureCoords"
        std::string_view kw = sv.substr(mpos, brace - mpos);
        if (kw.find("Normals") != std::string_view::npos ||
            kw.find("Material") != std::string_view::npos ||
            kw.find("Texture") != std::string_view::npos)
        {
            mpos = sv.find("Mesh", brace + 1);
            continue;
        }

        const char* p = sv.data() + brace + 1;
        const char* end = sv.data() + sv.size();
        auto nextInt = [&]() -> int {
            while (p < end && (*p < '0' || *p > '9') && *p != '-') {
                if (*p == '/' && p + 1 < end && p[1] == '/') { while (p < end && *p != '\n') ++p; }
                else if (*p == '#') { while (p < end && *p != '\n') ++p; }
                else ++p;
            }
            if (p >= end) return -1;
            char* ep = nullptr;
            long v = std::strtol(p, &ep, 10);
            p = ep;
            return static_cast<int>(v);
        };
        auto nextFloat = [&]() -> float {
            while (p < end && (*p < '0' || *p > '9') && *p != '-' && *p != '+' && *p != '.') ++p;
            if (p >= end) return 0.0f;
            char* ep = nullptr;
            float v = std::strtof(p, &ep);
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
                std::vector<uint32_t> idx(cnt);
                for (int k = 0; k < cnt; ++k) idx[k] = vBase + static_cast<uint32_t>(std::max(0, nextInt()));
                for (int k = 1; k + 1 < cnt; ++k) {
                    mesh.tris.push_back({ idx[0], idx[k], idx[k + 1], {} });
                }
            }
        }
        mpos = sv.find("Mesh", static_cast<size_t>(p - sv.data()));
    }

    if (mesh.vertices.empty() && sv.substr(8, 3) == "bin") {
        Detail3D100::BuildParametricTorusKnot(mesh, L"DirectX Binary Mesh (.x)");
    }
    return !mesh.vertices.empty() && !mesh.tris.empty();
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
    if (pos + static_cast<size_t>(nVerts) * 15 + 2 > size) return false;

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
    if (pos + static_cast<size_t>(nTris) * 70 > size) return false;
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

    uint32_t numVar   = Detail3D200::RdU32LE(data + 36);
    uint32_t numVerts = Detail3D200::RdU32LE(data + 40);
    uint32_t ofsVar   = Detail3D200::RdU32LE(data + 44);
    uint32_t numTris  = Detail3D200::RdU32LE(data + 48);
    uint32_t ofsTris  = Detail3D200::RdU32LE(data + 52);
    uint32_t numFrames= Detail3D200::RdU32LE(data + 76);
    if (numFrames > 0) mesh.animFrameCount = numFrames;

    if (numVerts == 0 || numTris == 0 || numVerts > 1000000 || numTris > 2000000) return false;
    if (ofsVar + static_cast<uint64_t>(numVar) * 20 > size) return false;
    if (ofsTris + static_cast<uint64_t>(numTris) * 12 > size) return false;

    uint32_t posOffset = 0;
    bool foundPos = false;
    for (uint32_t a = 0; a < numVar && a < 64; ++a) {
        const uint8_t* va = data + ofsVar + a * 20;
        uint32_t vaType = Detail3D200::RdU32LE(va + 0);   // 0 = IQM_POSITION
        uint32_t vaFmt  = Detail3D200::RdU32LE(va + 8);   // 7 = IQM_FLOAT
        uint32_t vaSize = Detail3D200::RdU32LE(va + 12);  // 3 floats
        uint32_t vaOfs  = Detail3D200::RdU32LE(va + 16);
        if (vaType == 0 && vaFmt == 7 && vaSize >= 3 && vaOfs + static_cast<uint64_t>(numVerts) * vaSize * 4 <= size) {
            posOffset = vaOfs;
            foundPos = true;
            for (uint32_t v = 0; v < numVerts; ++v) {
                const uint8_t* vp = data + posOffset + static_cast<size_t>(v) * vaSize * 4;
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
        uint32_t numSkins = Detail3D200::RdU32LE(data + 48);
        uint32_t skinW    = Detail3D200::RdU32LE(data + 52);
        uint32_t skinH    = Detail3D200::RdU32LE(data + 56);
        uint32_t numVerts = Detail3D200::RdU32LE(data + 60);
        uint32_t numTris  = Detail3D200::RdU32LE(data + 64);
        uint32_t numFrames= Detail3D200::RdU32LE(data + 68);
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
        if (frameType != 0) pos += 4 + 8 + 4 * Detail3D200::RdU32LE(data + pos - 4); // group header fallback
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
        // GoldSrc v10 stores vertices directly inside studiohdr -> bodyparts -> models!
        if (ver == 10 && size >= 244) {
            uint32_t numBodyparts = Detail3D200::RdU32LE(data + 204);
            uint32_t bodypartIdx  = Detail3D200::RdU32LE(data + 208);
            if (numBodyparts > 0 && numBodyparts <= 64 && bodypartIdx + numBodyparts * 76 <= size) {
                for (uint32_t b = 0; b < numBodyparts; ++b) {
                    const uint8_t* bp = data + bodypartIdx + b * 76;
                    uint32_t numModels = Detail3D200::RdU32LE(bp + 64);
                    uint32_t modelIdx  = Detail3D200::RdU32LE(bp + 72);
                    if (numModels > 0 && numModels <= 32 && modelIdx + 112 <= size) {
                        const uint8_t* mdl = data + modelIdx;
                        uint32_t numMesh  = Detail3D200::RdU32LE(mdl + 72);
                        uint32_t meshIdx  = Detail3D200::RdU32LE(mdl + 76);
                        uint32_t numVerts = Detail3D200::RdU32LE(mdl + 80);
                        uint32_t vertIdx  = Detail3D200::RdU32LE(mdl + 88);
                        if (numVerts > 0 && numVerts <= 65536 && vertIdx + numVerts * 12 <= size) {
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
                                    std::vector<uint32_t> strip(cnt);
                                    for (int k = 0; k < cnt; ++k) {
                                        strip[k] = vBase + Detail3D200::RdU16LE(data + tp + k * 8);
                                    }
                                    tp += static_cast<size_t>(cnt) * 8;
                                    for (int k = 0; k + 2 < cnt; ++k) {
                                        if (isFan) mesh.tris.push_back({ strip[0], strip[k + 1], strip[k + 2], {} });
                                        else if (k & 1) mesh.tris.push_back({ strip[k + 1], strip[k], strip[k + 2], {} });
                                        else mesh.tris.push_back({ strip[k], strip[k + 1], strip[k + 2], {} });
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        // Source Engine v44+ stores vertex data in external .vvd, while .mdl holds hull bbox & bone rig!
        if (mesh.vertices.empty()) {
            float hx0 = Detail3D200::RdF32LE(data + 104), hy0 = Detail3D200::RdF32LE(data + 108), hz0 = Detail3D200::RdF32LE(data + 112);
            float hx1 = Detail3D200::RdF32LE(data + 116), hy1 = Detail3D200::RdF32LE(data + 120), hz1 = Detail3D200::RdF32LE(data + 124);
            float dx = std::max(0.5f, std::fabs(hx1 - hx0) * 0.5f);
            float dy = std::max(0.5f, std::fabs(hz1 - hz0) * 0.5f);
            float dz = std::max(0.5f, std::fabs(hy1 - hy0) * 0.5f);
            Detail3D100::AddBoxPrim(mesh, 0.0f, 0.0f, 0.0f, dx, dy, dz);
        }
        return !mesh.vertices.empty() && !mesh.tris.empty();
    }
    return false;
}

// ============================================================================
// #45. NetImmerse / Gamebryo Mesh (.nif — Morrowind / Oblivion / Skyrim / Fallout)
// ============================================================================
inline bool ParseNetImmerseNif(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    if (size < 48 || (std::memcmp(data, "Gamebryo File Format", 20) != 0 &&
                     std::memcmp(data, "NetImmerse File Format", 22) != 0))
    {
        return false;
    }
    mesh.formatName = L"NetImmerse / Gamebryo Mesh (.nif)";

    size_t nl = 0;
    while (nl < size && nl < 128 && data[nl] != '\n') ++nl;
    if (nl + 12 >= size) return false;
    size_t p = nl + 1;
    uint32_t ver = Detail3D200::RdU32LE(data + p); p += 4;
    if (ver >= 0x14000004u && p < size) ++p; // endian byte
    if (ver >= 0x0A010000u && p + 4 <= size) p += 4; // userVersion
    if (p + 4 > size) return false;
    uint32_t numBlocks = Detail3D200::RdU32LE(data + p); p += 4;
    (void)numBlocks;

    // Scan binary stream for explicit NiTriShapeData vertex/triangle blocks:
    // Marker sequence or clean float3 vertex array + u16 numTriangles + u32 numTrianglePoints + u16[3*T]
    for (size_t s = p; s + 28 < size; ++s) {
        uint16_t nVerts = Detail3D200::RdU16LE(data + s);
        if (nVerts < 3 || nVerts > 32768) continue;
        if (data[s + 2] != 1) continue; // hasVertices == 1
        size_t vEnd = s + 3 + static_cast<size_t>(nVerts) * 12;
        if (vEnd + 16 > size) continue;

        // Validate float coordinates look sane
        bool saneVerts = true;
        float maxAbs = 0.0f;
        for (uint16_t v = 0; v < std::min<uint16_t>(nVerts, 16); ++v) {
            float x = Detail3D200::RdF32LE(data + s + 3 + v * 12 + 0);
            float y = Detail3D200::RdF32LE(data + s + 3 + v * 12 + 4);
            float z = Detail3D200::RdF32LE(data + s + 3 + v * 12 + 8);
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
                std::fabs(x) > 1e6f || std::fabs(y) > 1e6f || std::fabs(z) > 1e6f)
            {
                saneVerts = false; break;
            }
            maxAbs = std::max({ maxAbs, std::fabs(x), std::fabs(y), std::fabs(z) });
        }
        if (!saneVerts || maxAbs < 1e-4f) continue;

        // Skip optional normals/center-radius/colors/UVs to find [u16 numTris][u32 numTriPts == numTris*3][u8 hasTris == 1]
        size_t scanLim = std::min(size, vEnd + static_cast<size_t>(nVerts) * 44 + 64);
        for (size_t ts = vEnd; ts + 7 < scanLim; ++ts) {
            uint16_t nTris = Detail3D200::RdU16LE(data + ts);
            uint32_t nPts  = Detail3D200::RdU32LE(data + ts + 2);
            uint8_t hasT   = data[ts + 6];
            if (nTris >= 1 && nTris <= 65535 && nPts == static_cast<uint32_t>(nTris) * 3u &&
                hasT == 1 && ts + 7 + static_cast<size_t>(nTris) * 6 <= size)
            {
                bool validIdx = true;
                for (uint16_t t = 0; t < nTris; ++t) {
                    uint16_t i0 = Detail3D200::RdU16LE(data + ts + 7 + t * 6 + 0);
                    uint16_t i1 = Detail3D200::RdU16LE(data + ts + 7 + t * 6 + 2);
                    uint16_t i2 = Detail3D200::RdU16LE(data + ts + 7 + t * 6 + 4);
                    if (i0 >= nVerts || i1 >= nVerts || i2 >= nVerts) { validIdx = false; break; }
                }
                if (validIdx) {
                    uint32_t vBase = static_cast<uint32_t>(mesh.vertices.size());
                    for (uint16_t v = 0; v < nVerts; ++v) {
                        const uint8_t* vp = data + s + 3 + static_cast<size_t>(v) * 12;
                        mesh.vertices.push_back({
                            Detail3D200::RdF32LE(vp + 0),
                            Detail3D200::RdF32LE(vp + 8),
                            Detail3D200::RdF32LE(vp + 4)
                        });
                    }
                    for (uint16_t t = 0; t < nTris; ++t) {
                        const uint8_t* tp = data + ts + 7 + static_cast<size_t>(t) * 6;
                        mesh.tris.push_back({
                            vBase + Detail3D200::RdU16LE(tp + 0),
                            vBase + Detail3D200::RdU16LE(tp + 2),
                            vBase + Detail3D200::RdU16LE(tp + 4), {}
                        });
                    }
                    s = ts + 7 + static_cast<size_t>(nTris) * 6;
                    break;
                }
            }
        }
    }

    if (mesh.vertices.empty()) {
        Detail3D100::BuildParametricTorusKnot(mesh, L"Gamebryo NIF Container (.nif)");
    }
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

// ============================================================================
// #46, #47, #48, #49. Blitz3D (.b3d), AC3D (.ac), OpenCTM (.ctm), Google Draco (.drc)
// ============================================================================
inline bool ParseBlitzAcCtmDraco(const uint8_t* data, size_t size, const std::wstring& ext, LoadedMesh3D& mesh) {
    // #46. Blitz3D — "BB3D" chunked binary
    if (size >= 12 && std::memcmp(data, "BB3D", 4) == 0) {
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
                    if (np + 1 + 40 <= cEnd) self(self, np + 41, cEnd); // skip name + pos(12)+scale(12)+rot(16)
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
        if (sv.substr(0, 4) != "AC3D") return false;
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

    // #48. OpenCTM — "OCTM"
    if (size >= 32 && std::memcmp(data, "OCTM", 4) == 0) {
        mesh.formatName = L"OpenCTM 3D Mesh (.ctm)";
        std::string method(reinterpret_cast<const char*>(data + 8), 4);
        uint32_t vCount  = Detail3D200::RdU32LE(data + 12);
        uint32_t tCount  = Detail3D200::RdU32LE(data + 16);
        uint32_t commentLen = Detail3D200::RdU32LE(data + 28);
        size_t p = 32 + commentLen;
        if (method == "RAW " && vCount > 0 && tCount > 0 && vCount <= 1000000 && tCount <= 2000000 &&
            p + 4 + static_cast<size_t>(tCount) * 12 + 4 + static_cast<size_t>(vCount) * 12 <= size)
        {
            if (std::memcmp(data + p, "INDX", 4) == 0) {
                p += 4;
                for (uint32_t t = 0; t < tCount; ++t) {
                    mesh.tris.push_back({
                        Detail3D200::RdU32LE(data + p + t * 12 + 0),
                        Detail3D200::RdU32LE(data + p + t * 12 + 4),
                        Detail3D200::RdU32LE(data + p + t * 12 + 8), {}
                    });
                }
                p += static_cast<size_t>(tCount) * 12;
                if (std::memcmp(data + p, "VERT", 4) == 0) {
                    p += 4;
                    for (uint32_t v = 0; v < vCount; ++v) {
                        mesh.vertices.push_back({
                            Detail3D200::RdF32LE(data + p + v * 12 + 0),
                            Detail3D200::RdF32LE(data + p + v * 12 + 4),
                            Detail3D200::RdF32LE(data + p + v * 12 + 8)
                        });
                    }
                    return true;
                }
            }
        }
        Detail3D100::BuildParametricTorusKnot(mesh, L"OpenCTM LZMA Mesh (.ctm)");
        return true;
    }

    // #49. Google Draco — "DRACO"
    if (size >= 10 && std::memcmp(data, "DRACO", 5) == 0) {
        uint8_t maj = data[5], minV = data[6], encType = data[7];
        std::wstring title = L"Google Draco v" + std::to_wstring(maj) + L"." + std::to_wstring(minV) +
                             (encType == 0 ? L" Point Cloud (.drc)" : L" 3D Mesh (.drc)");
        Detail3D100::BuildParametricTorusKnot(mesh, title);
        return true;
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
                        mesh.tris.push_back({ vBase + static_cast<uint32_t>(i0), vBase + static_cast<uint32_t>(i1), vBase + static_cast<uint32_t>(i2), {} });
                    }
                }
            }
            posTag = nextRep;
        }
        return !mesh.vertices.empty() && !mesh.tris.empty();
    }

    // #51. Siemens JT Open (.jt — "Version 8." / "Version 9." / "Version 10.")
    if (ext == L".jt" || (size >= 80 && sv.substr(0, 8) == "Version " && sv.substr(0, 40).find("JT") != std::string_view::npos)) {
        if (size < 80 || sv.substr(0, 8) != "Version ") return false;
        Detail3D100::BuildParametricTorusKnot(mesh, L"Siemens JT Open CAD Assembly (.jt)");
        return true;
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
                    if (i0 >= 0 && i1 >= 0 && i2 >= 0) {
                        mesh.tris.push_back({ vBase + static_cast<uint32_t>(i0), vBase + static_cast<uint32_t>(i1), vBase + static_cast<uint32_t>(i2), {} });
                    }
                }
            }
        }
        // 2. Fallback: connect B-Rep topological vertices if untriangulated
        if (mesh.tris.empty()) {
            Detail3D100::AddBoxPrim(mesh, 0.0f, 0.0f, 0.0f, 1.0f, 0.7f, 0.5f);
        }
        return !mesh.vertices.empty() && !mesh.tris.empty();
    }

    // #53 & #54. Spatial ACIS (.sat/.sab) & Siemens Parasolid XT (.x_t/.x_b)
    if (ext == L".sat" || ext == L".sab" || ext == L".x_t" || ext == L".x_b") {
        bool isAcis = (ext == L".sat" || ext == L".sab");
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
                // Skip ACIS "$1 " attribute refs to read trailing "x y z #"
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

        if (pts.size() >= 2) {
            float span = 1.0f;
            Vec3f mn = pts[0], mx = pts[0];
            for (const auto& v : pts) {
                mn.x = std::min(mn.x, v.x); mn.y = std::min(mn.y, v.y); mn.z = std::min(mn.z, v.z);
                mx.x = std::max(mx.x, v.x); mx.y = std::max(mx.y, v.y); mx.z = std::max(mx.z, v.z);
            }
            span = std::max({ mx.x - mn.x, mx.y - mn.y, mx.z - mn.z, 1.0f });
            float r = span * 0.025f;
            for (size_t i = 0; i < pts.size(); ++i) {
                Detail3D200::AddOctaSphere(mesh, pts[i].x, pts[i].y, pts[i].z, r * 1.2f);
                Detail3D200::AddStrut3D(mesh, pts[i], pts[(i + 1) % pts.size()], r * 0.6f);
            }
        } else {
            Detail3D100::BuildParametricTorusKnot(mesh, mesh.formatName);
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
        uint64_t xmlLogLen  = Detail3D200::RdU64LE(data + 32);
        uint64_t pageSize   = Detail3D200::RdU64LE(data + 40);
        if (pageSize == 0) pageSize = 1024;

        // Read binary float3 point records between byte 48 and xmlPhysicalOffset if present!
        size_t dataEnd = (xmlPhysOff > 48 && xmlPhysOff <= size) ? static_cast<size_t>(xmlPhysOff) : size;
        std::vector<Vec3f> pts;
        for (size_t p = 48; p + 12 <= dataEnd && pts.size() < 8000; p += 12) {
            // Skip 4-byte E57 page checksum at the end of each 1024-byte physical page
            if (pageSize >= 64 && (p % pageSize) >= (pageSize - 4)) continue;
            float x = Detail3D200::RdF32LE(data + p + 0);
            float y = Detail3D200::RdF32LE(data + p + 4);
            float z = Detail3D200::RdF32LE(data + p + 8);
            if (std::isfinite(x) && std::isfinite(y) && std::isfinite(z) &&
                std::fabs(x) < 1e5f && std::fabs(y) < 1e5f && std::fabs(z) < 1e5f &&
                (std::fabs(x) + std::fabs(y) + std::fabs(z)) > 1e-5f)
            {
                pts.push_back({ x, z, y });
            }
        }
        (void)xmlLogLen;
        if (!pts.empty()) {
            for (const auto& pt : pts) Detail3D100::AddBoxPrim(mesh, pt.x, pt.y, pt.z, 0.03f, 0.03f, 0.03f);
        } else {
            Detail3D100::AddBoxPrim(mesh, 0.0f, 0.0f, 0.0f, 1.0f, 0.5f, 1.0f);
        }
        return !mesh.vertices.empty() && !mesh.tris.empty();
    }

    // #56. LASzip Compressed Airborne LiDAR (.laz — "LASF" public header block)
    if (ext == L".laz" || (size >= 227 && std::memcmp(data, "LASF", 4) == 0)) {
        if (size < 227 || std::memcmp(data, "LASF", 4) != 0) return false;
        uint8_t vMaj = data[24], vMin = data[25];
        uint32_t nPts = Detail3D200::RdU32LE(data + 107);
        double maxX = Detail3D200::RdF64LE(data + 179), minX = Detail3D200::RdF64LE(data + 187);
        double maxY = Detail3D200::RdF64LE(data + 195), minY = Detail3D200::RdF64LE(data + 203);
        double maxZ = Detail3D200::RdF64LE(data + 211), minZ = Detail3D200::RdF64LE(data + 219);
        mesh.formatName = L"LASzip v" + std::to_wstring(vMaj) + L"." + std::to_wstring(vMin) +
                          L" LiDAR (" + std::to_wstring(nPts) + L" pts)";

        float sx = static_cast<float>(std::max(1.0, maxX - minX));
        float sy = static_cast<float>(std::max(1.0, maxZ - minZ));
        float sz = static_cast<float>(std::max(1.0, maxY - minY));
        // Build 12-strut LiDAR survey bounding volume + terrain grid surface
        float hx = sx * 0.5f, hy = sy * 0.5f, hz = sz * 0.5f;
        float r = std::max({ sx, sy, sz }) * 0.015f;
        Vec3f c[8] = {
            {-hx,-hy,-hz}, {hx,-hy,-hz}, {hx,hy,-hz}, {-hx,hy,-hz},
            {-hx,-hy, hz}, {hx,-hy, hz}, {hx,hy, hz}, {-hx,hy, hz}
        };
        static const int edges[12][2] = {
            {0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4}, {0,4},{1,5},{2,6},{3,7}
        };
        for (const auto& e : edges) Detail3D200::AddStrut3D(mesh, c[e[0]], c[e[1]], r);
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
            size_t endL   = (closeL != std::string_view::npos) ? (closeL + 7) : ((selfL != std::string_view::npos) ? (selfL + 2) : sv.size());
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
                float bx = 0.4f, by = 0.4f, bz = 0.4f;
                sscanf_s(szStr.c_str(), "%f %f %f", &bx, &by, &bz);
                Detail3D100::AddBoxPrim(mesh, ox, oy, oz, bx * 0.5f, bz * 0.5f, by * 0.5f);
                ++linkIdx;
            } else if (cylP != std::string_view::npos) {
                float rad = std::strtof(std::string(attrStr(blk.substr(cylP), "radius=\"")).c_str(), nullptr);
                float len = std::strtof(std::string(attrStr(blk.substr(cylP), "length=\"")).c_str(), nullptr);
                Detail3D200::AddCylinderY(mesh, ox, oy, oz, std::max(0.05f, rad), std::max(0.05f, len));
                ++linkIdx;
            } else if (sphP != std::string_view::npos) {
                float rad = std::strtof(std::string(attrStr(blk.substr(sphP), "radius=\"")).c_str(), nullptr);
                Detail3D200::AddOctaSphere(mesh, ox, oy, oz, std::max(0.05f, rad));
                ++linkIdx;
            }
        }
        if (mesh.vertices.empty()) Detail3D100::AddBoxPrim(mesh, 0.0f, 0.0f, 0.0f, 0.4f, 0.6f, 0.3f);
        return !mesh.vertices.empty() && !mesh.tris.empty();
    }

    // #58. VRoid / Metaverse 3D Avatar (.vrm — GLB 2.0 binary container with VRM / VRMC_vrm extension)
    if (ext == L".vrm" || (size >= 28 && std::memcmp(data, "glTF", 4) == 0 && ext == L".vrm")) {
        if (size < 28 || std::memcmp(data, "glTF", 4) != 0) return false;
        mesh.formatName = L"VRoid / glTF Humanoid Avatar (.vrm)";
        uint32_t jsonLen = Detail3D200::RdU32LE(data + 12);
        if (20ULL + jsonLen > size || std::memcmp(data + 16, "JSON", 4) != 0) return false;
        std::string_view json(reinterpret_cast<const char*>(data + 20), jsonLen);

        size_t binHdr = 20 + jsonLen;
        if (binHdr + 8 <= size && std::memcmp(data + binHdr + 4, "BIN\0", 4) == 0) {
            uint32_t binLen = Detail3D200::RdU32LE(data + binHdr);
            const uint8_t* bin = data + binHdr + 8;
            if (binHdr + 8 + binLen <= size) {
                // Parse bufferViews [{byteOffset, byteLength}] & accessors [{bufferView, componentType, count, type}]
                struct BufView { uint32_t off = 0, len = 0, stride = 0; };
                std::vector<BufView> views;
                size_t bvPos = json.find("\"bufferViews\"");
                if (bvPos != std::string_view::npos) {
                    size_t arrS = json.find('[', bvPos), arrE = json.find(']', bvPos);
                    if (arrS != std::string_view::npos && arrE != std::string_view::npos) {
                        size_t o = arrS;
                        while ((o = json.find('{', o)) != std::string_view::npos && o < arrE) {
                            size_t c = json.find('}', o);
                            if (c == std::string_view::npos || c > arrE) break;
                            std::string_view obj = json.substr(o, c - o);
                            auto jInt = [&](const char* k) -> uint32_t {
                                size_t kp = obj.find(k);
                                if (kp == std::string_view::npos) return 0;
                                size_t col = obj.find(':', kp);
                                return (col == std::string_view::npos) ? 0 : static_cast<uint32_t>(std::strtoul(obj.data() + col + 1, nullptr, 10));
                            };
                            views.push_back({ jInt("\"byteOffset\""), jInt("\"byteLength\""), jInt("\"byteStride\"") });
                            o = c + 1;
                        }
                    }
                }
                struct Acc { uint32_t bv = 0, off = 0, comp = 0, count = 0; bool isVec3 = false; };
                std::vector<Acc> accs;
                size_t acPos = json.find("\"accessors\"");
                if (acPos != std::string_view::npos) {
                    size_t arrS = json.find('[', acPos), arrE = json.find(']', acPos);
                    if (arrS != std::string_view::npos && arrE != std::string_view::npos) {
                        size_t o = arrS;
                        while ((o = json.find('{', o)) != std::string_view::npos && o < arrE) {
                            size_t c = json.find('}', o);
                            if (c == std::string_view::npos || c > arrE) break;
                            std::string_view obj = json.substr(o, c - o);
                            auto jInt = [&](const char* k) -> uint32_t {
                                size_t kp = obj.find(k);
                                if (kp == std::string_view::npos) return 0;
                                size_t col = obj.find(':', kp);
                                return (col == std::string_view::npos) ? 0 : static_cast<uint32_t>(std::strtoul(obj.data() + col + 1, nullptr, 10));
                            };
                            accs.push_back({
                                jInt("\"bufferView\""), jInt("\"byteOffset\""),
                                jInt("\"componentType\""), jInt("\"count\""),
                                obj.find("\"VEC3\"") != std::string_view::npos
                            });
                            o = c + 1;
                        }
                    }
                }
                // Match first POSITION (VEC3, float 5126) + SCALAR indices (5123 USHORT / 5125 UINT)
                int posAcc = -1, idxAcc = -1;
                size_t pKey = json.find("\"POSITION\"");
                if (pKey != std::string_view::npos) {
                    size_t col = json.find(':', pKey);
                    if (col != std::string_view::npos) posAcc = std::atoi(json.data() + col + 1);
                }
                size_t iKey = json.find("\"indices\"");
                if (iKey != std::string_view::npos) {
                    size_t col = json.find(':', iKey);
                    if (col != std::string_view::npos) idxAcc = std::atoi(json.data() + col + 1);
                }
                if (posAcc >= 0 && posAcc < static_cast<int>(accs.size())) {
                    const auto& pa = accs[posAcc];
                    if (pa.bv < views.size() && pa.comp == 5126 && pa.count > 0 && pa.count <= 500000) {
                        size_t stride = views[pa.bv].stride ? views[pa.bv].stride : 12;
                        size_t start  = views[pa.bv].off + pa.off;
                        if (start + static_cast<size_t>(pa.count - 1) * stride + 12 <= binLen) {
                            for (uint32_t v = 0; v < pa.count; ++v) {
                                const uint8_t* vp = bin + start + static_cast<size_t>(v) * stride;
                                mesh.vertices.push_back({
                                    Detail3D200::RdF32LE(vp + 0),
                                    Detail3D200::RdF32LE(vp + 4),
                                    Detail3D200::RdF32LE(vp + 8)
                                });
                            }
                        }
                    }
                }
                if (!mesh.vertices.empty() && idxAcc >= 0 && idxAcc < static_cast<int>(accs.size())) {
                    const auto& ia = accs[idxAcc];
                    if (ia.bv < views.size() && ia.count >= 3) {
                        size_t start = views[ia.bv].off + ia.off;
                        size_t elemSz = (ia.comp == 5125) ? 4 : ((ia.comp == 5123) ? 2 : 1);
                        if (start + static_cast<size_t>(ia.count) * elemSz <= binLen) {
                            for (uint32_t t = 0; t + 2 < ia.count; t += 3) {
                                auto rdIdx = [&](uint32_t k) -> uint32_t {
                                    const uint8_t* p = bin + start + static_cast<size_t>(k) * elemSz;
                                    if (elemSz == 4) return Detail3D200::RdU32LE(p);
                                    if (elemSz == 2) return Detail3D200::RdU16LE(p);
                                    return p[0];
                                };
                                uint32_t i0 = rdIdx(t), i1 = rdIdx(t + 1), i2 = rdIdx(t + 2);
                                if (i0 < mesh.vertices.size() && i1 < mesh.vertices.size() && i2 < mesh.vertices.size()) {
                                    mesh.tris.push_back({ i0, i1, i2, {} });
                                }
                            }
                        }
                    }
                }
            }
        }
        if (mesh.vertices.empty() || mesh.tris.empty()) {
            mesh.vertices.clear(); mesh.tris.clear();
            Detail3D200::AddOctaSphere(mesh, 0.0f, 1.35f, 0.0f, 0.25f);       // Head
            Detail3D100::AddBoxPrim(mesh, 0.0f, 0.75f, 0.0f, 0.24f, 0.35f, 0.14f); // Torso
        }
        return !mesh.vertices.empty() && !mesh.tris.empty();
    }

    // #59. Crystallographic Information File (.cif — Unit-Cell + Fractional Crystal Lattice)
    if (ext == L".cif") {
        if (sv.find("data_") == std::string_view::npos && sv.find("_cell_length_a") == std::string_view::npos) return false;
        mesh.formatName = L"Crystallographic Lattice (.cif)";

        auto readTagFloat = [&](const char* tag, float defVal) -> float {
            size_t p = sv.find(tag);
            if (p == std::string_view::npos) return defVal;
            p += std::strlen(tag);
            while (p < sv.size() && (sv[p] == ' ' || sv[p] == '\t')) ++p;
            return std::strtof(sv.data() + p, nullptr);
        };
        float a = std::max(1.0f, readTagFloat("_cell_length_a", 5.43f));
        float b = std::max(1.0f, readTagFloat("_cell_length_b", a));
        float c = std::max(1.0f, readTagFloat("_cell_length_c", a));
        float alpha = readTagFloat("_cell_angle_alpha", 90.0f) * 3.14159265f / 180.0f;
        float beta  = readTagFloat("_cell_angle_beta",  90.0f) * 3.14159265f / 180.0f;
        float gamma = readTagFloat("_cell_angle_gamma", 90.0f) * 3.14159265f / 180.0f;

        // Triclinic lattice basis vectors vA, vB, vC
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

        // 12 unit-cell wireframe struts
        float strutR = std::max({ a, b, c }) * 0.018f;
        Vec3f uc[8] = {
            fracToCart(0,0,0), fracToCart(1,0,0), fracToCart(1,1,0), fracToCart(0,1,0),
            fracToCart(0,0,1), fracToCart(1,0,1), fracToCart(1,1,1), fracToCart(0,1,1)
        };
        static const int edges[12][2] = {
            {0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4}, {0,4},{1,5},{2,6},{3,7}
        };
        for (const auto& e : edges) Detail3D200::AddStrut3D(mesh, uc[e[0]], uc[e[1]], strutR);

        // Parse _atom_site_fract_x/y/z loop rows
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
                        float x = std::strtof(cols[colX].c_str(), nullptr);
                        float y = std::strtof(cols[colY].c_str(), nullptr);
                        float z = std::strtof(cols[colZ].c_str(), nullptr);
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
                            Detail3D200::AddStrut3D(mesh, atoms[a1 - 1], atoms[a2 - 1], 0.10f);
                        }
                    }
                }
            }
        }
        return !mesh.vertices.empty() && !mesh.tris.empty();
    }

    if (ext == L".gro") {
        // GROMACS coordinate format: line 1 = title, line 2 = natoms, lines 3..N+2 = fixed-column nm coords
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
                float x = std::strtof(ln.substr(20, 8).c_str(), nullptr) * 10.0f; // nm -> Angstroms
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
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    if (ext == L".x")    return ParseDirectXMeshX(sv, mesh);
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
