#pragma once
// SoftimageXsi.h — Softimage dotXSI 3.0 / 3.5 / 3.6 / 5.0 Modding Scene & Mesh (.xsi) reader
// Parses "xsi 03xxtxt" / "xsi 05xxtxt" scenes with hierarchical SI_Model + SI_Transform transforms,
// SI_Mesh / XSI_Mesh, SI_Shape / XSI_SubComponentAttributeList POSITION arrays, and
// SI_TriangleList / SI_PolygonList / XSI_TriangleList / XSI_PolygonList geometry.

namespace Formats {

namespace DetailSoftimageXsi {

struct XsiTok {
    std::string_view text;
    bool isQuoted = false;
    bool isBraceOpen = false;
    bool isBraceClose = false;
};

inline bool TokenizeXsi(std::string_view sv, std::vector<XsiTok>& out) {
    out.clear();
    out.reserve(1024);
    const size_t n = sv.size();
    size_t i = 0;
    while (i < n) {
        const char c = sv[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',' || c == ';') {
            ++i;
            continue;
        }
        if (c == '/' && i + 1u < n && sv[i + 1u] == '/') {
            while (i < n && sv[i] != '\n') ++i;
            continue;
        }
        if (c == '#') {
            while (i < n && sv[i] != '\n') ++i;
            continue;
        }
        if (c == '{') {
            out.push_back({ sv.substr(i, 1u), false, true, false });
            ++i;
            continue;
        }
        if (c == '}') {
            out.push_back({ sv.substr(i, 1u), false, false, true });
            ++i;
            continue;
        }
        if (c == '"') {
            const size_t start = ++i;
            while (i < n && sv[i] != '"') {
                if (sv[i] == '\\' && i + 1u < n) ++i;
                ++i;
            }
            if (i >= n) return false;
            out.push_back({ sv.substr(start, i - start), true, false, false });
            ++i;
            continue;
        }
        const size_t start = i;
        while (i < n) {
            const char ch = sv[i];
            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' ||
                ch == ',' || ch == ';' || ch == '{' || ch == '}' || ch == '"') {
                break;
            }
            ++i;
        }
        out.push_back({ sv.substr(start, i - start), false, false, false });
        if (out.size() > 8000000u) return false;
    }
    return true;
}

inline bool SkipBalancedBlock(const std::vector<XsiTok>& toks, size_t& ti) {
    while (ti < toks.size() && !toks[ti].isBraceOpen) ++ti;
    if (ti >= toks.size()) return false;
    int depth = 1;
    ++ti;
    while (ti < toks.size() && depth > 0) {
        if (toks[ti].isBraceOpen) ++depth;
        else if (toks[ti].isBraceClose) --depth;
        ++ti;
    }
    return depth == 0;
}

inline void MakeTrsMatrix4(
    float sx, float sy, float sz,
    float rxDeg, float ryDeg, float rzDeg,
    float tx, float ty, float tz,
    float outM[16])
{
    constexpr float kDeg2Rad = 3.14159265358979323846f / 180.0f;
    const float cx = std::cos(rxDeg * kDeg2Rad), sxr = std::sin(rxDeg * kDeg2Rad);
    const float cy = std::cos(ryDeg * kDeg2Rad), syr = std::sin(ryDeg * kDeg2Rad);
    const float cz = std::cos(rzDeg * kDeg2Rad), szr = std::sin(rzDeg * kDeg2Rad);

    // R = Rz * Ry * Rx, scaled by diag(sx, sy, sz), translated by (tx, ty, tz)
    outM[0]  = (cz * cy) * sx;
    outM[1]  = (cz * syr * sxr - szr * cx) * sy;
    outM[2]  = (cz * syr * cx + szr * sxr) * sz;
    outM[3]  = tx;

    outM[4]  = (szr * cy) * sx;
    outM[5]  = (szr * syr * sxr + cz * cx) * sy;
    outM[6]  = (szr * syr * cx - cz * sxr) * sz;
    outM[7]  = ty;

    outM[8]  = (-syr) * sx;
    outM[9]  = (cy * sxr) * sy;
    outM[10] = (cy * cx) * sz;
    outM[11] = tz;

    outM[12] = 0.0f; outM[13] = 0.0f; outM[14] = 0.0f; outM[15] = 1.0f;
}

inline Vec3f ApplyMat4(const float m[16], const Vec3f& p) noexcept {
    return {
        m[0] * p.x + m[1] * p.y + m[2]  * p.z + m[3],
        m[4] * p.x + m[5] * p.y + m[6]  * p.z + m[7],
        m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11]
    };
}

inline bool ParseMeshBlock(
    const std::vector<XsiTok>& toks,
    size_t& ti,
    const float worldM[16],
    LoadedMesh3D& mesh)
{
    while (ti < toks.size() && !toks[ti].isBraceOpen) ++ti;
    if (ti >= toks.size()) return false;
    ++ti; // '{'

    std::vector<Vec3f> localPos;
    uint32_t baseV = static_cast<uint32_t>(mesh.vertices.size());
    bool emittedVerts = false;

    auto ensureVertsEmitted = [&]() -> bool {
        if (localPos.size() < 3u) return false;
        if (!emittedVerts) {
            baseV = static_cast<uint32_t>(mesh.vertices.size());
            for (const auto& lp : localPos) {
                mesh.vertices.push_back(ApplyMat4(worldM, lp));
            }
            emittedVerts = true;
        }
        return true;
    };

    while (ti < toks.size() && !toks[ti].isBraceClose) {
        const std::string_view kw = toks[ti].text;
        if (!toks[ti].isQuoted && (kw == "SI_Shape" || kw == "XSI_Shape")) {
            ++ti;
            while (ti < toks.size() && !toks[ti].isBraceOpen) ++ti;
            if (ti >= toks.size()) return false;
            ++ti; // '{' inside Shape
            int depth = 1;
            while (ti < toks.size() && depth > 0) {
                if (toks[ti].isBraceOpen) { ++depth; ++ti; continue; }
                if (toks[ti].isBraceClose) { --depth; ++ti; continue; }

                // dotXSI 5.0: XSI_SubComponentAttributeList "POSITION" "FLOAT" 3 { count, x, y, z ... }
                if (!toks[ti].isQuoted && toks[ti].text == "XSI_SubComponentAttributeList") {
                    ++ti;
                    if (ti + 4u >= toks.size()) return false;
                    const bool isPos = (toks[ti + 1u].text == "POSITION" || toks[ti].text == "POSITION");
                    while (ti < toks.size() && !toks[ti].isBraceOpen) ++ti;
                    if (ti >= toks.size()) return false;
                    ++ti; // '{'
                    if (isPos && localPos.empty()) {
                        int cnt = 0;
                        if (ti >= toks.size() || !ReaderText::ParseIntSv(toks[ti++].text, cnt) || cnt < 3 || cnt > 1000000) {
                            return false;
                        }
                        if (ti + static_cast<size_t>(cnt) * 3u > toks.size()) return false;
                        localPos.resize(static_cast<size_t>(cnt));
                        for (int v = 0; v < cnt; ++v) {
                            float x = 0, y = 0, z = 0;
                            if (!ReaderText::ParseFloatSv(toks[ti++].text, x) ||
                                !ReaderText::ParseFloatSv(toks[ti++].text, y) ||
                                !ReaderText::ParseFloatSv(toks[ti++].text, z)) {
                                return false;
                            }
                            localPos[static_cast<size_t>(v)] = { x, y, z };
                        }
                    }
                    int subD = 1;
                    while (ti < toks.size() && subD > 0) {
                        if (toks[ti].isBraceOpen) ++subD;
                        else if (toks[ti].isBraceClose) --subD;
                        ++ti;
                    }
                    continue;
                }

                // dotXSI 3.0/3.6: <count>, "POSITION", x0, y0, z0, ...
                if (toks[ti].isQuoted && toks[ti].text == "POSITION" && ti >= 1u && localPos.empty()) {
                    int cnt = 0;
                    if (!ReaderText::ParseIntSv(toks[ti - 1u].text, cnt) || cnt < 3 || cnt > 1000000) return false;
                    ++ti; // past "POSITION"
                    if (ti + static_cast<size_t>(cnt) * 3u > toks.size()) return false;
                    localPos.resize(static_cast<size_t>(cnt));
                    for (int v = 0; v < cnt; ++v) {
                        float x = 0, y = 0, z = 0;
                        if (!ReaderText::ParseFloatSv(toks[ti++].text, x) ||
                            !ReaderText::ParseFloatSv(toks[ti++].text, y) ||
                            !ReaderText::ParseFloatSv(toks[ti++].text, z)) {
                            return false;
                        }
                        localPos[static_cast<size_t>(v)] = { x, y, z };
                    }
                    continue;
                }
                ++ti;
            }
        } else if (!toks[ti].isQuoted && (kw == "SI_TriangleList" || kw == "XSI_TriangleList")) {
            ++ti;
            while (ti < toks.size() && !toks[ti].isBraceOpen) ++ti;
            if (ti >= toks.size()) return false;
            ++ti;
            if (!ensureVertsEmitted()) return false;
            if (ti + 3u > toks.size()) return false;
            int nTris = 0;
            if (!ReaderText::ParseIntSv(toks[ti++].text, nTris) || nTris < 1 || nTris > 1000000) return false;
            ++ti; // attribute descriptor string ("NORMAL|TEX_COORD_UV" etc.)
            if (ti < toks.size() && toks[ti].isQuoted) ++ti; // optional material name string
            if (ti + static_cast<size_t>(nTris) * 3u > toks.size()) return false;
            for (int t = 0; t < nTris; ++t) {
                int i0 = -1, i1 = -1, i2 = -1;
                if (!ReaderText::ParseIntSv(toks[ti++].text, i0) ||
                    !ReaderText::ParseIntSv(toks[ti++].text, i1) ||
                    !ReaderText::ParseIntSv(toks[ti++].text, i2)) {
                    return false;
                }
                if (i0 < 0 || static_cast<size_t>(i0) >= localPos.size() ||
                    i1 < 0 || static_cast<size_t>(i1) >= localPos.size() ||
                    i2 < 0 || static_cast<size_t>(i2) >= localPos.size()) {
                    return false;
                }
                if (i0 != i1 && i1 != i2 && i0 != i2) {
                    mesh.tris.push_back({ baseV + uint32_t(i0), baseV + uint32_t(i1), baseV + uint32_t(i2), {} });
                }
            }
            int subD = 1;
            while (ti < toks.size() && subD > 0) {
                if (toks[ti].isBraceOpen) ++subD;
                else if (toks[ti].isBraceClose) --subD;
                ++ti;
            }
        } else if (!toks[ti].isQuoted && (kw == "SI_PolygonList" || kw == "XSI_PolygonList")) {
            ++ti;
            while (ti < toks.size() && !toks[ti].isBraceOpen) ++ti;
            if (ti >= toks.size()) return false;
            ++ti;
            if (!ensureVertsEmitted()) return false;
            if (ti + 4u > toks.size()) return false;
            int nPolys = 0;
            if (!ReaderText::ParseIntSv(toks[ti++].text, nPolys) || nPolys < 1 || nPolys > 1000000) return false;
            ++ti; // attributes string
            if (ti < toks.size() && toks[ti].isQuoted) ++ti; // material string
            int nCornersTotal = 0;
            if (ti >= toks.size() || !ReaderText::ParseIntSv(toks[ti++].text, nCornersTotal) ||
                nCornersTotal < nPolys * 3 || nCornersTotal > 4000000) {
                return false;
            }
            if (ti + static_cast<size_t>(nPolys) + static_cast<size_t>(nCornersTotal) > toks.size()) return false;
            std::vector<int> vCounts(static_cast<size_t>(nPolys));
            int sumCorners = 0;
            for (int p = 0; p < nPolys; ++p) {
                if (!ReaderText::ParseIntSv(toks[ti++].text, vCounts[static_cast<size_t>(p)]) ||
                    vCounts[static_cast<size_t>(p)] < 3 || vCounts[static_cast<size_t>(p)] > 256) {
                    return false;
                }
                sumCorners += vCounts[static_cast<size_t>(p)];
            }
            if (sumCorners != nCornersTotal) return false;

            for (int p = 0; p < nPolys; ++p) {
                const int vc = vCounts[static_cast<size_t>(p)];
                std::vector<uint32_t> pIdx(static_cast<size_t>(vc));
                for (int k = 0; k < vc; ++k) {
                    int iv = -1;
                    if (!ReaderText::ParseIntSv(toks[ti++].text, iv) || iv < 0 || static_cast<size_t>(iv) >= localPos.size()) {
                        return false;
                    }
                    pIdx[static_cast<size_t>(k)] = static_cast<uint32_t>(iv);
                }
                for (int k = 1; k + 1 < vc; ++k) {
                    if (pIdx[0] != pIdx[static_cast<size_t>(k)] &&
                        pIdx[static_cast<size_t>(k)] != pIdx[static_cast<size_t>(k + 1)] &&
                        pIdx[0] != pIdx[static_cast<size_t>(k + 1)]) {
                        mesh.tris.push_back({ baseV + pIdx[0], baseV + pIdx[static_cast<size_t>(k)], baseV + pIdx[static_cast<size_t>(k + 1)], {} });
                    }
                }
            }
            int subD = 1;
            while (ti < toks.size() && subD > 0) {
                if (toks[ti].isBraceOpen) ++subD;
                else if (toks[ti].isBraceClose) --subD;
                ++ti;
            }
        } else if (toks[ti].isBraceOpen) {
            if (!SkipBalancedBlock(toks, ti)) return false;
        } else {
            ++ti;
        }
    }
    if (ti >= toks.size() || !toks[ti].isBraceClose) return false;
    ++ti; // '}'
    return true;
}

inline bool ParseSceneScope(
    const std::vector<XsiTok>& toks,
    size_t& ti,
    const float parentM[16],
    LoadedMesh3D& mesh,
    int depth,
    bool stopAtCloseBrace)
{
    if (depth > 32) return false;
    float curWorldM[16] = {};
    std::memcpy(curWorldM, parentM, sizeof(curWorldM));

    while (ti < toks.size()) {
        if (toks[ti].isBraceClose) {
            if (stopAtCloseBrace) {
                ++ti;
                return true;
            }
            return false;
        }
        const std::string_view kw = toks[ti].text;
        if (!toks[ti].isQuoted && kw == "SI_Transform") {
            ++ti;
            while (ti < toks.size() && !toks[ti].isBraceOpen) ++ti;
            if (ti >= toks.size()) return false;
            ++ti;
            if (ti + 9u <= toks.size()) {
                float v[9] = {};
                bool ok9 = true;
                for (int k = 0; k < 9; ++k) {
                    if (!ReaderText::ParseFloatSv(toks[ti + static_cast<size_t>(k)].text, v[k])) {
                        ok9 = false;
                        break;
                    }
                }
                if (ok9) {
                    float localM[16] = {};
                    MakeTrsMatrix4(v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], localM);
                    MeshBuild3D::MatMul4RowMajor(parentM, localM, curWorldM);
                }
            }
            int subD = 1;
            while (ti < toks.size() && subD > 0) {
                if (toks[ti].isBraceOpen) ++subD;
                else if (toks[ti].isBraceClose) --subD;
                ++ti;
            }
        } else if (!toks[ti].isQuoted && (kw == "SI_Model" || kw == "XSI_Model")) {
            ++ti;
            while (ti < toks.size() && !toks[ti].isBraceOpen) ++ti;
            if (ti >= toks.size()) return false;
            ++ti;
            if (!ParseSceneScope(toks, ti, curWorldM, mesh, depth + 1, true)) return false;
        } else if (!toks[ti].isQuoted && (kw == "SI_Mesh" || kw == "XSI_Mesh")) {
            ++ti;
            if (!ParseMeshBlock(toks, ti, curWorldM, mesh)) return false;
        } else if (toks[ti].isBraceOpen) {
            if (!SkipBalancedBlock(toks, ti)) return false;
        } else {
            ++ti;
        }
    }
    return !stopAtCloseBrace;
}

} // namespace DetailSoftimageXsi

inline bool ParseSoftimageXsi(
    const uint8_t* data,
    size_t size,
    LoadedMesh3D& mesh)
{
    if (!data || size < 24 || size > 64u * 1024u * 1024u) return false;
    std::string_view sv = ReaderText::StripUtf8Bom(std::string_view(reinterpret_cast<const char*>(data), size));
    if (sv.size() < 16u || sv.substr(0, 4) != "xsi ") return false;
    if (sv.substr(8, 4) != "txt ") return false;
    for (size_t k = 4; k < 8; ++k) {
        if (sv[k] < '0' || sv[k] > '9') return false;
    }

    size_t firstEol = sv.find('\n');
    if (firstEol == std::string_view::npos) return false;
    std::string_view body = sv.substr(firstEol + 1u);
    if (body.find('\0') != std::string_view::npos) return false;

    std::vector<DetailSoftimageXsi::XsiTok> toks;
    if (!DetailSoftimageXsi::TokenizeXsi(body, toks)) return false;

    float ident[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };
    size_t ti = 0;
    if (!DetailSoftimageXsi::ParseSceneScope(toks, ti, ident, mesh, 0, false)) return false;
    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"Softimage dotXSI Scene (.xsi)";
    return true;
}

} // namespace Formats
