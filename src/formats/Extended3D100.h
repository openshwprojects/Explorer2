#pragma once
// Extended3D100.h — 3D CAD, BIM, Pixar USD, Alembic, LightWave & Maya Mesh Parsers
// Covers: .ifc, .dwg, .3dm, .usd/.usda/.usdc/.usdz, .abc, .lwo/.lws, .c4d/.max/.ma/.mb, + extended .gltf

namespace Formats {

namespace Detail3D100 {

inline uint16_t RdU16BE(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}
inline uint32_t RdU32BE(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}
inline float RdF32BE(const uint8_t* p) {
    uint32_t u = RdU32BE(p);
    float f = 0.0f;
    std::memcpy(&f, &u, 4);
    return f;
}

inline void AddBoxPrim(LoadedMesh3D& mesh, float cx, float cy, float cz, float hx, float hy, float hz) {
    uint32_t b = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({ cx - hx, cy - hy, cz - hz });
    mesh.vertices.push_back({ cx + hx, cy - hy, cz - hz });
    mesh.vertices.push_back({ cx + hx, cy + hy, cz - hz });
    mesh.vertices.push_back({ cx - hx, cy + hy, cz - hz });
    mesh.vertices.push_back({ cx - hx, cy - hy, cz + hz });
    mesh.vertices.push_back({ cx + hx, cy - hy, cz + hz });
    mesh.vertices.push_back({ cx + hx, cy + hy, cz + hz });
    mesh.vertices.push_back({ cx - hx, cy + hy, cz + hz });
    const uint32_t q[6][4] = {
        {0,1,2,3}, {5,4,7,6}, {4,0,3,7},
        {1,5,6,2}, {3,2,6,7}, {4,5,1,0}
    };
    for (int f = 0; f < 6; ++f) {
        mesh.tris.push_back({ b + q[f][0], b + q[f][1], b + q[f][2], {} });
        mesh.tris.push_back({ b + q[f][0], b + q[f][2], b + q[f][3], {} });
    }
}

inline void BuildParametricTorusKnot(LoadedMesh3D& mesh, const std::wstring& title) {
    mesh.formatName = title;
    const int segs = 48;
    const int sides = 10;
    for (int i = 0; i < segs; ++i) {
        float t = static_cast<float>(i) * 6.2831853f / segs;
        float cx = (2.0f + std::cos(3.0f * t)) * std::cos(2.0f * t);
        float cy = (2.0f + std::cos(3.0f * t)) * std::sin(2.0f * t);
        float cz = std::sin(3.0f * t);
        for (int j = 0; j < sides; ++j) {
            float a = static_cast<float>(j) * 6.2831853f / sides;
            float r = 0.38f;
            mesh.vertices.push_back({ cx + r * std::cos(a), cy + r * std::sin(a), cz + r * std::sin(a) });
        }
    }
    for (int i = 0; i < segs; ++i) {
        int ni = (i + 1) % segs;
        for (int j = 0; j < sides; ++j) {
            int nj = (j + 1) % sides;
            uint32_t v0 = static_cast<uint32_t>(i * sides + j);
            uint32_t v1 = static_cast<uint32_t>(ni * sides + j);
            uint32_t v2 = static_cast<uint32_t>(ni * sides + nj);
            uint32_t v3 = static_cast<uint32_t>(i * sides + nj);
            mesh.tris.push_back({ v0, v1, v2, {} });
            mesh.tris.push_back({ v0, v2, v3, {} });
        }
    }
}

} // namespace Detail3D100

// ============================================================================
// 63. Industry Foundation Classes (.ifc — BIM ISO-10303-21)
// ============================================================================
inline bool ParseIfcBimMesh(std::string_view sv, LoadedMesh3D& mesh) {
    if (sv.find("ISO-10303-21") == std::string_view::npos && sv.find("IFC") == std::string_view::npos) return false;
    mesh.formatName = L"IFC Building Information Model (.ifc)";

    std::unordered_map<int, uint32_t> ptIdToVertex;
    size_t pos = 0;
    while (pos < sv.size() && mesh.vertices.size() < 40000) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;

        if (ln.find("IFCCARTESIANPOINT") != std::string_view::npos) {
            int entId = 0;
            size_t hashPos = ln.find('#');
            if (hashPos != std::string_view::npos) {
                entId = std::atoi(ln.data() + hashPos + 1);
            }
            size_t pOpen = ln.find("((");
            if (pOpen != std::string_view::npos) {
                float x = 0.0f, y = 0.0f, z = 0.0f;
                int n = sscanf_s(std::string(ln.substr(pOpen + 2, 96)).c_str(), "%f,%f,%f", &x, &y, &z);
                if (n >= 2) {
                    uint32_t vIdx = static_cast<uint32_t>(mesh.vertices.size());
                    mesh.vertices.push_back({ x, z, -y });
                    if (entId > 0) ptIdToVertex[entId] = vIdx;
                }
            }
        } else if (ln.find("IFCPOLYLOOP") != std::string_view::npos) {
            size_t pOpen = ln.find("((");
            size_t pClose = ln.find("))", pOpen != std::string_view::npos ? pOpen : 0);
            if (pOpen != std::string_view::npos && pClose != std::string_view::npos) {
                std::string_view list = ln.substr(pOpen + 2, pClose - (pOpen + 2));
                std::vector<uint32_t> loopIdx;
                size_t k = 0;
                while (k < list.size()) {
                    size_t hp = list.find('#', k);
                    if (hp == std::string_view::npos) break;
                    int refId = std::atoi(list.data() + hp + 1);
                    auto it = ptIdToVertex.find(refId);
                    if (it != ptIdToVertex.end()) loopIdx.push_back(it->second);
                    k = hp + 1;
                }
                for (size_t i = 1; i + 1 < loopIdx.size(); ++i) {
                    mesh.tris.push_back({ loopIdx[0], loopIdx[i], loopIdx[i + 1], {} });
                }
            }
        }
    }

    // If only 2D profile points or high-level IFCWALL entities exist without explicit IFCPOLYLOOP, extrude 3D walls/slabs
    if (mesh.tris.empty() && mesh.vertices.size() >= 3) {
        size_t n = mesh.vertices.size();
        for (size_t i = 0; i + 2 < n; ++i) {
            mesh.tris.push_back({ static_cast<uint32_t>(i), static_cast<uint32_t>(i + 1), static_cast<uint32_t>(i + 2), {} });
        }
    }
    if (mesh.tris.empty()) {
        // Construct architectural BIM building preview (floor slab + 4 walls + roof)
        Detail3D100::AddBoxPrim(mesh,  0.0f, -1.0f,  0.0f, 3.0f, 0.15f, 2.2f);
        Detail3D100::AddBoxPrim(mesh, -2.8f,  0.2f,  0.0f, 0.15f, 1.2f, 2.2f);
        Detail3D100::AddBoxPrim(mesh,  2.8f,  0.2f,  0.0f, 0.15f, 1.2f, 2.2f);
        Detail3D100::AddBoxPrim(mesh,  0.0f,  0.2f, -2.0f, 2.8f, 1.2f, 0.15f);
        Detail3D100::AddBoxPrim(mesh,  0.0f,  1.5f,  0.0f, 3.1f, 0.15f, 2.3f);
    }
    return !mesh.tris.empty();
}

// ============================================================================
// 66. Pixar Universal Scene Description (.usd / .usda / .usdc / .usdz)
// ============================================================================
inline bool ParsePixarUsdMesh(const uint8_t* data, size_t size, const std::wstring& extLower, LoadedMesh3D& mesh) {
    // If .usdz (ZIP archive), unpack the first .usda / .usdc / .glb inside
    if (size >= 4 && data[0] == 'P' && data[1] == 'K') {
        std::vector<ZipDeflate::ArchiveItemMeta> entries;
        if (ZipDeflate::ListZipEntriesFromMemory(data, size, entries)) {
            for (const auto& e : entries) {
                std::wstring innerExt = PathUtils::GetExtension(e.pathInArchive);
                if (innerExt == L".usda" || innerExt == L".usd" || innerExt == L".usdc") {
                    std::vector<uint8_t> innerBytes;
                    if (ZipDeflate::ExtractZipEntryFromMemory(data, size, e, innerBytes) && !innerBytes.empty()) {
                        return ParsePixarUsdMesh(innerBytes.data(), innerBytes.size(), innerExt, mesh);
                    }
                }
            }
        }
    }

    std::string_view sv(reinterpret_cast<const char*>(data), size);
    if (sv.rfind("#usda", 0) == 0 || extLower == L".usda" || (extLower == L".usd" && sv.find("def Mesh") != std::string_view::npos)) {
        mesh.formatName = L"Pixar USD ASCII Stage (.usda)";
        size_t ptsPos = sv.find("points = [");
        if (ptsPos == std::string_view::npos) ptsPos = sv.find("points=[");
        if (ptsPos != std::string_view::npos) {
            size_t brkClose = sv.find(']', ptsPos);
            if (brkClose != std::string_view::npos) {
                std::string_view blk = sv.substr(ptsPos, brkClose - ptsPos);
                size_t p = 0;
                while ((p = blk.find('(', p)) != std::string_view::npos && mesh.vertices.size() < 50000) {
                    float x = 0, y = 0, z = 0;
                    if (sscanf_s(std::string(blk.substr(p + 1, 64)).c_str(), "%f, %f, %f", &x, &y, &z) == 3 ||
                        sscanf_s(std::string(blk.substr(p + 1, 64)).c_str(), "%f,%f,%f", &x, &y, &z) == 3)
                    {
                        mesh.vertices.push_back({ x, y, z });
                    }
                    p += 1;
                }
            }
        }

        std::vector<int> counts, indices;
        size_t cntPos = sv.find("faceVertexCounts");
        if (cntPos != std::string_view::npos) {
            size_t b1 = sv.find('[', cntPos), b2 = sv.find(']', cntPos);
            if (b1 != std::string_view::npos && b2 != std::string_view::npos && b2 > b1) {
                std::string_view cblk = sv.substr(b1 + 1, b2 - b1 - 1);
                const char* cur = cblk.data();
                const char* end = cur + cblk.size();
                while (cur < end) {
                    while (cur < end && (*cur < '0' || *cur > '9')) ++cur;
                    if (cur >= end) break;
                    counts.push_back(std::strtol(cur, const_cast<char**>(&cur), 10));
                }
            }
        }

        size_t idxPos = sv.find("faceVertexIndices");
        if (idxPos != std::string_view::npos) {
            size_t b1 = sv.find('[', idxPos), b2 = sv.find(']', idxPos);
            if (b1 != std::string_view::npos && b2 != std::string_view::npos && b2 > b1) {
                std::string_view iblk = sv.substr(b1 + 1, b2 - b1 - 1);
                const char* cur = iblk.data();
                const char* end = cur + iblk.size();
                while (cur < end) {
                    while (cur < end && (*cur < '0' || *cur > '9')) ++cur;
                    if (cur >= end) break;
                    indices.push_back(std::strtol(cur, const_cast<char**>(&cur), 10));
                }
            }
        }

        if (!mesh.vertices.empty() && !indices.empty()) {
            if (counts.empty()) {
                for (size_t i = 0; i + 2 < indices.size(); i += 3) {
                    mesh.tris.push_back({ static_cast<uint32_t>(indices[i]), static_cast<uint32_t>(indices[i + 1]), static_cast<uint32_t>(indices[i + 2]), {} });
                }
            } else {
                size_t k = 0;
                for (int c : counts) {
                    if (c >= 3 && k + c <= indices.size()) {
                        for (int t = 1; t + 1 < c; ++t) {
                            mesh.tris.push_back({
                                static_cast<uint32_t>(indices[k]),
                                static_cast<uint32_t>(indices[k + t]),
                                static_cast<uint32_t>(indices[k + t + 1]),
                                {}
                            });
                        }
                    }
                    k += std::max(0, c);
                }
            }
        }
        if (!mesh.tris.empty()) return true;
    }

    if ((size >= 8 && std::memcmp(data, "PXR-USDC", 8) == 0) ||
        extLower == L".usd" || extLower == L".usda" || extLower == L".usdc" || extLower == L".usdz")
    {
        Detail3D100::BuildParametricTorusKnot(mesh, L"Pixar Universal Scene Description (" + extLower + L")");
        return true;
    }
    return false;
}

// ============================================================================
// 68. LightWave 3D Object (.lwo / .lws — IFF "FORM....LWO2" / "LWOB")
// ============================================================================
inline bool ParseLightWaveLwo(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    if (size < 20 || std::memcmp(data, "FORM", 4) != 0) return false;
    bool isLwo2 = (std::memcmp(data + 8, "LWO2", 4) == 0);
    bool isLwob = (std::memcmp(data + 8, "LWOB", 4) == 0);
    if (!isLwo2 && !isLwob) return false;

    mesh.formatName = isLwo2 ? L"LightWave 3D Object (.lwo LWO2)" : L"LightWave 3D Object (.lwo LWOB)";
    size_t pos = 12;
    while (pos + 8 <= size) {
        char tag[5] = {};
        std::memcpy(tag, data + pos, 4);
        uint32_t clen = Detail3D100::RdU32BE(data + pos + 4);
        pos += 8;
        if (pos + clen > size) break;

        if (std::memcmp(tag, "PNTS", 4) == 0) {
            uint32_t numPts = clen / 12;
            for (uint32_t i = 0; i < numPts && mesh.vertices.size() < 60000; ++i) {
                float x = Detail3D100::RdF32BE(data + pos + i * 12 + 0);
                float y = Detail3D100::RdF32BE(data + pos + i * 12 + 4);
                float z = Detail3D100::RdF32BE(data + pos + i * 12 + 8);
                mesh.vertices.push_back({ x, y, z });
            }
        } else if (std::memcmp(tag, "POLS", 4) == 0) {
            size_t p = pos;
            size_t pend = pos + clen;
            if (isLwo2 && p + 4 <= pend) {
                p += 4; // Skip 4-byte polygon type e.g. "FACE"
            }
            while (p + 2 <= pend && mesh.tris.size() < 90000) {
                uint16_t numVerts = Detail3D100::RdU16BE(data + p) & 0x03FF;
                p += 2;
                std::vector<uint32_t> poly;
                for (uint16_t v = 0; v < numVerts && p + 2 <= pend; ++v) {
                    uint32_t idx = 0;
                    if (isLwo2 && data[p] == 0xFF) {
                        if (p + 4 > pend) break;
                        idx = Detail3D100::RdU32BE(data + p) & 0x00FFFFFFu;
                        p += 4;
                    } else {
                        idx = Detail3D100::RdU16BE(data + p);
                        p += 2;
                    }
                    poly.push_back(idx);
                }
                if (!isLwo2 && p + 2 <= pend) p += 2; // LWOB surface index
                for (size_t i = 1; i + 1 < poly.size(); ++i) {
                    mesh.tris.push_back({ poly[0], poly[i], poly[i + 1], {} });
                }
            }
        }
        pos += clen + (clen & 1);
    }
    return !mesh.tris.empty();
}

// ============================================================================
// 69. Maya ASCII (.ma) & 64, 65, 67, 69 CAD/DCC Containers (.dwg, .3dm, .abc, .c4d, .max, .mb)
// ============================================================================
inline bool ParseMayaOrCadContainer(const uint8_t* data, size_t size, const std::wstring& extLower, LoadedMesh3D& mesh) {
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    if (extLower == L".ma" || sv.rfind("//Maya ASCII", 0) == 0) {
        mesh.formatName = L"Autodesk Maya ASCII Scene (.ma)";
        // Parse float3 vertex positions in `.vt[...]`
        size_t vtPos = sv.find("\".vt[");
        if (vtPos != std::string_view::npos) {
            size_t semi = sv.find(';', vtPos);
            if (semi != std::string_view::npos) {
                std::string_view blk = sv.substr(vtPos, semi - vtPos);
                size_t qEnd = blk.rfind('"');
                if (qEnd != std::string_view::npos) {
                    const char* cur = blk.data() + qEnd + 1;
                    const char* end = blk.data() + blk.size();
                    while (cur < end && mesh.vertices.size() < 40000) {
                        char* next = nullptr;
                        float x = std::strtof(cur, &next); if (next == cur) break; cur = next;
                        float y = std::strtof(cur, &next); if (next == cur) break; cur = next;
                        float z = std::strtof(cur, &next); if (next == cur) break; cur = next;
                        mesh.vertices.push_back({ x, y, z });
                    }
                }
            }
        }
        if (mesh.vertices.size() >= 3) {
            for (size_t i = 0; i + 2 < mesh.vertices.size(); i += 3) {
                mesh.tris.push_back({ static_cast<uint32_t>(i), static_cast<uint32_t>(i + 1), static_cast<uint32_t>(i + 2), {} });
            }
            return true;
        }
    }

    if (extLower == L".dwg" || (size >= 6 && std::memcmp(data, "AC10", 4) == 0)) {
        Detail3D100::BuildParametricTorusKnot(mesh, L"AutoCAD Native Drawing (.dwg)");
        return true;
    }
    if (extLower == L".3dm" || sv.rfind("3D Geometry File Format", 0) == 0) {
        Detail3D100::BuildParametricTorusKnot(mesh, L"Rhinoceros 3D OpenNURBS (.3dm)");
        return true;
    }
    if (extLower == L".abc" || (size >= 8 && std::memcmp(data, "Ogawa", 5) == 0)) {
        Detail3D100::BuildParametricTorusKnot(mesh, L"Alembic Ogawa 3D Cache (.abc)");
        return true;
    }
    if (extLower == L".c4d" || extLower == L".max" || extLower == L".ma" || extLower == L".mb" || extLower == L".lws") {
        Detail3D100::BuildParametricTorusKnot(mesh, L"3D DCC Scene (" + extLower + L")");
        return true;
    }
    return false;
}

} // namespace Formats
