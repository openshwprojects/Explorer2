#pragma once
// RadianceSceneRad.h — LBNL Radiance Lighting & Daylighting Scene (.rad) 3D reader
// Parses exact Radiance primitive records (<mod> <type> <id> / n_str / n_int / n_real, plus
// special 4-token `alias` records and `!genbox` + `| xform` inline box generators) for
// polygon, sphere/bubble, cone/cup, cylinder/tube, and ring geometry.

namespace Formats {

namespace DetailRadianceRad {

inline Vec3f RadToViewer(Vec3f p) {
    // Radiance uses right-handed Z-up (+X East, +Y North, +Z Up) -> viewer Y-up
    return { p.x, p.z, -p.y };
}

inline void EmitRadTubeOrCone(
    LoadedMesh3D& mesh,
    Vec3f p0, Vec3f p1,
    float r0, float r1,
    bool capEnds)
{
    Vec3f axis = { p1.x - p0.x, p1.y - p0.y, p1.z - p0.z };
    const float len = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    if (len <= 1e-6f || (r0 <= 1e-6f && r1 <= 1e-6f)) return;
    axis = { axis.x / len, axis.y / len, axis.z / len };
    Vec3f ref = (std::fabs(axis.z) < 0.9f) ? Vec3f{ 0, 0, 1 } : Vec3f{ 1, 0, 0 };
    Vec3f u = { axis.y * ref.z - axis.z * ref.y, axis.z * ref.x - axis.x * ref.z, axis.x * ref.y - axis.y * ref.x };
    const float ul = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z);
    if (ul <= 1e-6f) return;
    u = { u.x / ul, u.y / ul, u.z / ul };
    Vec3f v = { axis.y * u.z - axis.z * u.y, axis.z * u.x - axis.x * u.z, axis.x * u.y - axis.y * u.x };

    constexpr int kSegs = 16;
    const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
    for (int k = 0; k < 2; ++k) {
        const Vec3f& pc = (k == 0) ? p0 : p1;
        const float rk = std::max(0.0f, (k == 0) ? r0 : r1);
        for (int s = 0; s < kSegs; ++s) {
            const float th = 6.283185307f * static_cast<float>(s) / kSegs;
            const float cs = std::cos(th), sn = std::sin(th);
            mesh.vertices.push_back(RadToViewer({
                pc.x + rk * (cs * u.x + sn * v.x),
                pc.y + rk * (cs * u.y + sn * v.y),
                pc.z + rk * (cs * u.z + sn * v.z)
            }));
        }
    }
    for (int s = 0; s < kSegs; ++s) {
        const uint32_t sn = static_cast<uint32_t>((s + 1) % kSegs);
        mesh.tris.push_back({ base + static_cast<uint32_t>(s), base + sn, base + static_cast<uint32_t>(kSegs) + sn, {} });
        mesh.tris.push_back({ base + static_cast<uint32_t>(s), base + static_cast<uint32_t>(kSegs) + sn, base + static_cast<uint32_t>(kSegs + s), {} });
    }
    if (capEnds) {
        const uint32_t c0 = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back(RadToViewer(p0));
        const uint32_t c1 = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back(RadToViewer(p1));
        for (int s = 0; s < kSegs; ++s) {
            const uint32_t sn = static_cast<uint32_t>((s + 1) % kSegs);
            if (r0 > 1e-6f) mesh.tris.push_back({ c0, base + sn, base + static_cast<uint32_t>(s), {} });
            if (r1 > 1e-6f) mesh.tris.push_back({ c1, base + static_cast<uint32_t>(kSegs + s), base + static_cast<uint32_t>(kSegs) + sn, {} });
        }
    }
}

inline void EmitRadSphere(LoadedMesh3D& mesh, Vec3f ctr, float rad) {
    rad = std::fabs(rad);
    if (rad <= 1e-6f) return;
    constexpr int kRings = 10, kSegs = 16;
    const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(RadToViewer({ ctr.x, ctr.y, ctr.z + rad }));
    for (int r = 1; r < kRings; ++r) {
        const float phi = 3.1415926535f * static_cast<float>(r) / kRings;
        for (int s = 0; s < kSegs; ++s) {
            const float th = 6.283185307f * static_cast<float>(s) / kSegs;
            mesh.vertices.push_back(RadToViewer({
                ctr.x + rad * std::sin(phi) * std::cos(th),
                ctr.y + rad * std::sin(phi) * std::sin(th),
                ctr.z + rad * std::cos(phi)
            }));
        }
    }
    const uint32_t south = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(RadToViewer({ ctr.x, ctr.y, ctr.z - rad }));
    auto rIdx = [&](int r, int s) -> uint32_t { return base + static_cast<uint32_t>(1 + (r - 1) * kSegs + (s % kSegs)); };
    for (int s = 0; s < kSegs; ++s) mesh.tris.push_back({ base, rIdx(1, s), rIdx(1, s + 1), {} });
    for (int r = 1; r < kRings - 1; ++r) {
        for (int s = 0; s < kSegs; ++s) {
            mesh.tris.push_back({ rIdx(r, s), rIdx(r + 1, s), rIdx(r + 1, s + 1), {} });
            mesh.tris.push_back({ rIdx(r, s), rIdx(r + 1, s + 1), rIdx(r, s + 1), {} });
        }
    }
    for (int s = 0; s < kSegs; ++s) mesh.tris.push_back({ rIdx(kRings - 1, s), south, rIdx(kRings - 1, s + 1), {} });
}

inline void EmitRadRing(LoadedMesh3D& mesh, Vec3f ctr, Vec3f nrm, float rIn, float rOut) {
    const float nl = std::sqrt(nrm.x * nrm.x + nrm.y * nrm.y + nrm.z * nrm.z);
    if (nl <= 1e-6f || rOut <= 1e-6f || rOut <= rIn) return;
    nrm = { nrm.x / nl, nrm.y / nl, nrm.z / nl };
    Vec3f ref = (std::fabs(nrm.z) < 0.9f) ? Vec3f{ 0, 0, 1 } : Vec3f{ 1, 0, 0 };
    Vec3f u = { nrm.y * ref.z - nrm.z * ref.y, nrm.z * ref.x - nrm.x * ref.z, nrm.x * ref.y - nrm.y * ref.x };
    const float ul = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z);
    u = { u.x / ul, u.y / ul, u.z / ul };
    Vec3f v = { nrm.y * u.z - nrm.z * u.y, nrm.z * u.x - nrm.x * u.z, nrm.x * u.y - nrm.y * u.x };

    constexpr int kSegs = 20;
    rIn = std::max(0.0f, rIn);
    if (rIn <= 1e-6f) {
        const uint32_t cIdx = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back(RadToViewer(ctr));
        for (int s = 0; s < kSegs; ++s) {
            const float th = 6.283185307f * static_cast<float>(s) / kSegs;
            const float cs = std::cos(th), sn = std::sin(th);
            mesh.vertices.push_back(RadToViewer({
                ctr.x + rOut * (cs * u.x + sn * v.x),
                ctr.y + rOut * (cs * u.y + sn * v.y),
                ctr.z + rOut * (cs * u.z + sn * v.z)
            }));
        }
        for (int s = 0; s < kSegs; ++s) {
            mesh.tris.push_back({ cIdx, cIdx + 1u + static_cast<uint32_t>(s), cIdx + 1u + static_cast<uint32_t>((s + 1) % kSegs), {} });
        }
    } else {
        const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        for (float rk : { rIn, rOut }) {
            for (int s = 0; s < kSegs; ++s) {
                const float th = 6.283185307f * static_cast<float>(s) / kSegs;
                const float cs = std::cos(th), sn = std::sin(th);
                mesh.vertices.push_back(RadToViewer({
                    ctr.x + rk * (cs * u.x + sn * v.x),
                    ctr.y + rk * (cs * u.y + sn * v.y),
                    ctr.z + rk * (cs * u.z + sn * v.z)
                }));
            }
        }
        for (int s = 0; s < kSegs; ++s) {
            const uint32_t sn = static_cast<uint32_t>((s + 1) % kSegs);
            mesh.tris.push_back({ base + static_cast<uint32_t>(s), base + static_cast<uint32_t>(kSegs + s), base + static_cast<uint32_t>(kSegs) + sn, {} });
            mesh.tris.push_back({ base + static_cast<uint32_t>(s), base + static_cast<uint32_t>(kSegs) + sn, base + sn, {} });
        }
    }
}

// Handles `!genbox mat name xs ys zs [| xform -t tx ty tz -s sc -rz deg]`
inline void HandleGenBoxCommand(std::string_view cmdLine, LoadedMesh3D& mesh) {
    std::vector<std::string_view> toks;
    std::string_view rem = cmdLine;
    std::string_view t;
    while (ReaderText::NextTokenSv(rem, t)) toks.push_back(t);
    if (toks.size() < 6 || !ReaderText::AsciiEqCI(toks[0], "!genbox")) return;

    float xs = 0, ys = 0, zs = 0;
    if (!ReaderText::ParseFloatSv(toks[3], xs) ||
        !ReaderText::ParseFloatSv(toks[4], ys) ||
        !ReaderText::ParseFloatSv(toks[5], zs)) return;
    if (xs <= 0 || ys <= 0 || zs <= 0) return;

    Vec3f pts[8] = {
        {0,0,0}, {xs,0,0}, {xs,ys,0}, {0,ys,0},
        {0,0,zs}, {xs,0,zs}, {xs,ys,zs}, {0,ys,zs}
    };

    for (size_t i = 6; i < toks.size(); ++i) {
        if (toks[i] == "-t" && i + 3 < toks.size()) {
            float tx = 0, ty = 0, tz = 0;
            if (ReaderText::ParseFloatSv(toks[i + 1], tx) &&
                ReaderText::ParseFloatSv(toks[i + 2], ty) &&
                ReaderText::ParseFloatSv(toks[i + 3], tz)) {
                for (auto& p : pts) { p.x += tx; p.y += ty; p.z += tz; }
            }
            i += 3;
        } else if (toks[i] == "-s" && i + 1 < toks.size()) {
            float sc = 1.0f;
            if (ReaderText::ParseFloatSv(toks[i + 1], sc)) {
                for (auto& p : pts) { p.x *= sc; p.y *= sc; p.z *= sc; }
            }
            ++i;
        } else if (toks[i] == "-rz" && i + 1 < toks.size()) {
            float deg = 0.0f;
            if (ReaderText::ParseFloatSv(toks[i + 1], deg)) {
                const float r = deg * (3.1415926535f / 180.0f);
                const float cs = std::cos(r), sn = std::sin(r);
                for (auto& p : pts) {
                    const float nx = p.x * cs - p.y * sn;
                    const float ny = p.x * sn + p.y * cs;
                    p.x = nx; p.y = ny;
                }
            }
            ++i;
        }
    }

    const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
    for (const auto& p : pts) mesh.vertices.push_back(RadToViewer(p));
    const uint32_t q[6][4] = {
        {0,3,2,1}, {4,5,6,7}, {0,1,5,4}, {2,3,7,6}, {0,4,7,3}, {1,2,6,5}
    };
    for (const auto& f : q) {
        mesh.tris.push_back({ base + f[0], base + f[1], base + f[2], {} });
        mesh.tris.push_back({ base + f[0], base + f[2], base + f[3], {} });
    }
}

} // namespace DetailRadianceRad

inline bool ParseRadianceSceneRad(
    const uint8_t* data,
    size_t size,
    LoadedMesh3D& mesh)
{
    if (!data || size < 12 || size > 64u * 1024u * 1024u) return false;
    std::string_view sv = ReaderText::StripUtf8Bom(std::string_view(reinterpret_cast<const char*>(data), size));
    if (sv.find('\0') != std::string_view::npos) return false;

    // Pre-split lines: handle '#' comments and '!' generator commands, collect remaining body tokens
    std::vector<std::string_view> toks;
    size_t pos = 0;
    std::string_view line;
    std::string joinedCmd;
    while (ReaderText::NextLineSv(sv, pos, line)) {
        const size_t hp = line.find('#');
        if (hp != std::string_view::npos) line = line.substr(0, hp);
        std::string_view tr = ReaderText::TrimSv(line);
        if (tr.empty()) continue;

        if (tr.front() == '!' || !joinedCmd.empty()) {
            if (!tr.empty() && tr.back() == '\\') {
                joinedCmd.append(tr.substr(0, tr.size() - 1));
                joinedCmd.push_back(' ');
                continue;
            }
            joinedCmd.append(tr);
            DetailRadianceRad::HandleGenBoxCommand(joinedCmd, mesh);
            joinedCmd.clear();
            continue;
        }

        std::string_view rem = tr;
        std::string_view t;
        while (ReaderText::NextTokenSv(rem, t)) {
            toks.push_back(t);
            if (toks.size() > 8000000) break;
        }
    }

    // Parse Radiance primitive records sequentially
    size_t idx = 0;
    while (idx < toks.size()) {
        if (idx + 3 > toks.size()) return false;
        const std::string_view mod = toks[idx++];
        const std::string_view typ = toks[idx++];
        const std::string_view ident = toks[idx++];
        (void)mod; (void)ident;

        if (typ == "alias") {
            if (idx >= toks.size()) return false;
            ++idx; // target identifier
            continue;
        }

        int nStr = -1;
        if (idx >= toks.size() || !ReaderText::ParseIntSv(toks[idx++], nStr) || nStr < 0 || nStr > 65536) return false;
        if (idx + static_cast<size_t>(nStr) > toks.size()) return false;
        idx += static_cast<size_t>(nStr);

        int nInt = -1;
        if (idx >= toks.size() || !ReaderText::ParseIntSv(toks[idx++], nInt) || nInt < 0 || nInt > 65536) return false;
        if (idx + static_cast<size_t>(nInt) > toks.size()) return false;
        for (int k = 0; k < nInt; ++k) {
            int dummy = 0;
            if (!ReaderText::ParseIntSv(toks[idx++], dummy)) return false;
        }

        int nReal = -1;
        if (idx >= toks.size() || !ReaderText::ParseIntSv(toks[idx++], nReal) || nReal < 0 || nReal > 3000000) return false;
        if (idx + static_cast<size_t>(nReal) > toks.size()) return false;

        std::vector<float> reals(static_cast<size_t>(nReal));
        for (int k = 0; k < nReal; ++k) {
            if (!ReaderText::ParseFloatSv(toks[idx++], reals[static_cast<size_t>(k)])) return false;
        }

        if (typ == "polygon") {
            if (nReal < 9 || (nReal % 3) != 0) return false;
            const size_t nv = static_cast<size_t>(nReal) / 3u;
            std::vector<Vec3f> ring;
            ring.reserve(nv);
            for (size_t v = 0; v < nv; ++v) {
                ring.push_back(DetailRadianceRad::RadToViewer({
                    reals[v * 3u + 0], reals[v * 3u + 1], reals[v * 3u + 2]
                }));
            }
            MeshBuild3D::EmitPlanarPolygon(mesh, ring, {});
        } else if (typ == "sphere" || typ == "bubble") {
            if (nReal != 4) return false;
            DetailRadianceRad::EmitRadSphere(mesh, { reals[0], reals[1], reals[2] }, reals[3]);
        } else if (typ == "cylinder" || typ == "tube") {
            if (nReal != 7) return false;
            DetailRadianceRad::EmitRadTubeOrCone(
                mesh, { reals[0], reals[1], reals[2] }, { reals[3], reals[4], reals[5] }, reals[6], reals[6], false);
        } else if (typ == "cone" || typ == "cup") {
            if (nReal != 8) return false;
            DetailRadianceRad::EmitRadTubeOrCone(
                mesh, { reals[0], reals[1], reals[2] }, { reals[3], reals[4], reals[5] }, reals[6], reals[7], false);
        } else if (typ == "ring") {
            if (nReal != 8) return false;
            DetailRadianceRad::EmitRadRing(
                mesh, { reals[0], reals[1], reals[2] }, { reals[3], reals[4], reals[5] }, reals[6], reals[7]);
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"Radiance Lighting Scene (.rad)";
    return true;
}

} // namespace Formats
