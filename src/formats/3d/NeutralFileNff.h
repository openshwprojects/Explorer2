#pragma once
// NeutralFileNff.h — Neutral File Format (.nff) 3D reader
// Supports BOTH industry .nff dialects:
//   1. Eric Haines Standard Procedural Database (SPD) NFF (`p`, `pp`, `s`, `c`, `v`/`f`/`b`/`l`)
//   2. Sense8 WorldToolKit (WTK) NFF (`nff` + `version 2.x` + indexed vertex/polygon blocks)

namespace Formats {

namespace DetailNeutralNff {

inline void EmitNffSphere(LoadedMesh3D& mesh, Vec3f ctr, float rad) {
    rad = std::fabs(rad);
    if (rad <= 1e-6f) return;
    constexpr int kRings = 10, kSegs = 16;
    const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({ ctr.x, ctr.y + rad, ctr.z });
    for (int r = 1; r < kRings; ++r) {
        const float phi = 3.1415926535f * static_cast<float>(r) / kRings;
        for (int s = 0; s < kSegs; ++s) {
            const float th = 6.283185307f * static_cast<float>(s) / kSegs;
            mesh.vertices.push_back({
                ctr.x + rad * std::sin(phi) * std::cos(th),
                ctr.y + rad * std::cos(phi),
                ctr.z + rad * std::sin(phi) * std::sin(th)
            });
        }
    }
    const uint32_t south = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({ ctr.x, ctr.y - rad, ctr.z });
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

inline void EmitNffCone(LoadedMesh3D& mesh, Vec3f p0, float r0, Vec3f p1, float r1) {
    r0 = std::fabs(r0); r1 = std::fabs(r1);
    Vec3f axis = { p1.x - p0.x, p1.y - p0.y, p1.z - p0.z };
    const float len = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    if (len <= 1e-6f || (r0 <= 1e-6f && r1 <= 1e-6f)) return;
    axis = { axis.x / len, axis.y / len, axis.z / len };
    Vec3f ref = (std::fabs(axis.y) < 0.9f) ? Vec3f{ 0, 1, 0 } : Vec3f{ 1, 0, 0 };
    Vec3f u = { axis.y * ref.z - axis.z * ref.y, axis.z * ref.x - axis.x * ref.z, axis.x * ref.y - axis.y * ref.x };
    const float ul = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z);
    u = { u.x / ul, u.y / ul, u.z / ul };
    Vec3f v = { axis.y * u.z - axis.z * u.y, axis.z * u.x - axis.x * u.z, axis.x * u.y - axis.y * u.x };

    constexpr int kSegs = 16;
    const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
    for (int k = 0; k < 2; ++k) {
        const Vec3f& pc = (k == 0) ? p0 : p1;
        const float rk = (k == 0) ? r0 : r1;
        for (int s = 0; s < kSegs; ++s) {
            const float th = 6.283185307f * static_cast<float>(s) / kSegs;
            const float cs = std::cos(th), sn = std::sin(th);
            mesh.vertices.push_back({
                pc.x + rk * (cs * u.x + sn * v.x),
                pc.y + rk * (cs * u.y + sn * v.y),
                pc.z + rk * (cs * u.z + sn * v.z)
            });
        }
    }
    for (int s = 0; s < kSegs; ++s) {
        const uint32_t sn = static_cast<uint32_t>((s + 1) % kSegs);
        mesh.tris.push_back({ base + static_cast<uint32_t>(s), base + static_cast<uint32_t>(kSegs + s), base + static_cast<uint32_t>(kSegs) + sn, {} });
        mesh.tris.push_back({ base + static_cast<uint32_t>(s), base + static_cast<uint32_t>(kSegs) + sn, base + sn, {} });
    }
}

inline bool NextWtkLine(std::string_view sv, size_t& pos, std::string_view& outLine) {
    std::string_view raw;
    while (ReaderText::NextLineSv(sv, pos, raw)) {
        const size_t sl = raw.find("//");
        if (sl != std::string_view::npos) raw = raw.substr(0, sl);
        const size_t hp = raw.find('#');
        if (hp != std::string_view::npos) raw = raw.substr(0, hp);
        raw = ReaderText::TrimSv(raw);
        if (!raw.empty()) { outLine = raw; return true; }
    }
    return false;
}

inline bool ParseWorldToolKitNff(std::string_view sv, LoadedMesh3D& mesh) {
    size_t pos = 0;
    std::string_view line;
    if (!NextWtkLine(sv, pos, line) || !ReaderText::AsciiEqCI(line, "nff")) return false;

    while (NextWtkLine(sv, pos, line)) {
        if (ReaderText::StartsWithCI(line, "version") || ReaderText::StartsWithCI(line, "viewpos") ||
            ReaderText::StartsWithCI(line, "viewdir")) {
            continue;
        }
        // Object name line -> followed by vertex count line -> vertices -> polygon count line -> polygons
        std::string_view vcLine;
        if (!NextWtkLine(sv, pos, vcLine)) break;
        std::string_view tVc;
        int nv = 0;
        if (!ReaderText::NextTokenSv(vcLine, tVc) || !ReaderText::ParseIntSv(tVc, nv) || nv < 3 || nv > 2000000) {
            return false;
        }

        std::vector<Vec3f> objVerts;
        objVerts.reserve(static_cast<size_t>(nv));
        for (int i = 0; i < nv; ++i) {
            std::string_view vl;
            if (!NextWtkLine(sv, pos, vl)) return false;
            std::string_view tx, ty, tz;
            float x = 0, y = 0, z = 0;
            if (!ReaderText::NextTokenSv(vl, tx) || !ReaderText::ParseFloatSv(tx, x) ||
                !ReaderText::NextTokenSv(vl, ty) || !ReaderText::ParseFloatSv(ty, y) ||
                !ReaderText::NextTokenSv(vl, tz) || !ReaderText::ParseFloatSv(tz, z)) {
                return false;
            }
            objVerts.push_back({ x, y, z });
        }

        std::string_view pcLine;
        if (!NextWtkLine(sv, pos, pcLine)) return false;
        std::string_view tPc;
        int np = 0;
        if (!ReaderText::NextTokenSv(pcLine, tPc) || !ReaderText::ParseIntSv(tPc, np) || np < 1 || np > 4000000) {
            return false;
        }

        const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        for (const auto& v : objVerts) mesh.vertices.push_back(v);

        for (int p = 0; p < np; ++p) {
            std::string_view pl;
            if (!NextWtkLine(sv, pos, pl)) return false;
            std::string_view tCnt;
            int corners = 0;
            if (!ReaderText::NextTokenSv(pl, tCnt) || !ReaderText::ParseIntSv(tCnt, corners) || corners < 3 || corners > 4096) {
                return false;
            }
            std::vector<uint32_t> idx(static_cast<size_t>(corners));
            for (int c = 0; c < corners; ++c) {
                std::string_view tv;
                int vi = -1;
                if (!ReaderText::NextTokenSv(pl, tv) || !ReaderText::ParseIntSv(tv, vi) || vi < 0 || vi >= nv) {
                    return false;
                }
                idx[static_cast<size_t>(c)] = static_cast<uint32_t>(vi);
            }
            if (corners == 3) {
                mesh.tris.push_back({ base + idx[0], base + idx[1], base + idx[2], {} });
            } else {
                std::vector<Vec3f> ring;
                ring.reserve(idx.size());
                for (uint32_t vi : idx) ring.push_back(objVerts[vi]);
                MeshBuild3D::EmitPlanarPolygon(mesh, ring, {});
            }
        }
    }

    return !mesh.vertices.empty() && !mesh.tris.empty();
}

inline bool ParseHainesSpdNff(std::string_view sv, LoadedMesh3D& mesh) {
    size_t pos = 0;
    std::string_view line;
    bool sawNffHeaderOrPrim = false;

    auto nextCleanLine = [&](std::string_view& out) -> bool {
        std::string_view raw;
        while (ReaderText::NextLineSv(sv, pos, raw)) {
            const size_t hp = raw.find('#');
            if (hp != std::string_view::npos) raw = raw.substr(0, hp);
            raw = ReaderText::TrimSv(raw);
            if (!raw.empty()) { out = raw; return true; }
        }
        return false;
    };

    while (nextCleanLine(line)) {
        std::string_view rem = line, cmd;
        if (!ReaderText::NextTokenSv(rem, cmd)) continue;

        if (cmd == "v") {
            sawNffHeaderOrPrim = true;
            // Consume 6 camera lines (from, at, up, angle, hither, resolution) if present
            for (int i = 0; i < 6; ++i) {
                size_t save = pos;
                std::string_view cl;
                if (!nextCleanLine(cl)) break;
                std::string_view cRem = cl, cKw;
                ReaderText::NextTokenSv(cRem, cKw);
                if (cKw != "from" && cKw != "at" && cKw != "up" &&
                    cKw != "angle" && cKw != "hither" && cKw != "resolution") {
                    pos = save;
                    break;
                }
            }
        } else if (cmd == "b" || cmd == "l" || cmd == "f") {
            sawNffHeaderOrPrim = true;
        } else if (cmd == "p" || cmd == "pp") {
            std::string_view tCnt;
            int nv = 0;
            if (!ReaderText::NextTokenSv(rem, tCnt) || !ReaderText::ParseIntSv(tCnt, nv) || nv < 3 || nv > 65536) {
                return false;
            }
            std::vector<Vec3f> ring;
            ring.reserve(static_cast<size_t>(nv));
            for (int i = 0; i < nv; ++i) {
                std::string_view vl;
                if (!nextCleanLine(vl)) return false;
                std::string_view tx, ty, tz;
                float x = 0, y = 0, z = 0;
                if (!ReaderText::NextTokenSv(vl, tx) || !ReaderText::ParseFloatSv(tx, x) ||
                    !ReaderText::NextTokenSv(vl, ty) || !ReaderText::ParseFloatSv(ty, y) ||
                    !ReaderText::NextTokenSv(vl, tz) || !ReaderText::ParseFloatSv(tz, z)) {
                    return false;
                }
                ring.push_back({ x, y, z });
            }
            if (ring.size() == 3) {
                const uint32_t b = static_cast<uint32_t>(mesh.vertices.size());
                mesh.vertices.push_back(ring[0]);
                mesh.vertices.push_back(ring[1]);
                mesh.vertices.push_back(ring[2]);
                mesh.tris.push_back({ b, b + 1, b + 2, {} });
            } else {
                MeshBuild3D::EmitPlanarPolygon(mesh, ring, {});
            }
        } else if (cmd == "s") {
            std::string_view tx, ty, tz, tr;
            float cx = 0, cy = 0, cz = 0, rad = 0;
            if (!ReaderText::NextTokenSv(rem, tx) || !ReaderText::ParseFloatSv(tx, cx) ||
                !ReaderText::NextTokenSv(rem, ty) || !ReaderText::ParseFloatSv(ty, cy) ||
                !ReaderText::NextTokenSv(rem, tz) || !ReaderText::ParseFloatSv(tz, cz) ||
                !ReaderText::NextTokenSv(rem, tr) || !ReaderText::ParseFloatSv(tr, rad) || rad <= 0.0f) {
                return false;
            }
            EmitNffSphere(mesh, { cx, cy, cz }, rad);
        } else if (cmd == "c") {
            float vals[8] = {};
            int got = 0;
            auto pullFloats = [&](std::string_view& r) -> bool {
                std::string_view tv;
                while (got < 8 && ReaderText::NextTokenSv(r, tv)) {
                    if (!ReaderText::ParseFloatSv(tv, vals[got++])) return false;
                }
                return true;
            };
            if (!pullFloats(rem)) return false;
            while (got < 8) {
                std::string_view cl;
                if (!nextCleanLine(cl) || !pullFloats(cl)) return false;
            }
            EmitNffCone(mesh, { vals[0], vals[1], vals[2] }, vals[3], { vals[4], vals[5], vals[6] }, vals[7]);
        } else {
            // Unknown command token in SPD NFF -> reject random text files cleanly!
            return false;
        }
    }

    (void)sawNffHeaderOrPrim;
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace DetailNeutralNff

inline bool ParseNeutralFileNff(
    const uint8_t* data,
    size_t size,
    LoadedMesh3D& mesh)
{
    if (!data || size < 12 || size > 64u * 1024u * 1024u) return false;
    std::string_view sv = ReaderText::StripUtf8Bom(std::string_view(reinterpret_cast<const char*>(data), size));
    if (sv.find('\0') != std::string_view::npos) return false;

    size_t probe = 0;
    std::string_view firstLine;
    if (!DetailNeutralNff::NextWtkLine(sv, probe, firstLine)) return false;

    if (ReaderText::AsciiEqCI(firstLine, "nff")) {
        if (!DetailNeutralNff::ParseWorldToolKitNff(sv, mesh)) return false;
        mesh.formatName = L"WorldToolKit Neutral File (NFF)";
        return true;
    }
    if (!DetailNeutralNff::ParseHainesSpdNff(sv, mesh)) return false;
    mesh.formatName = L"Eric Haines SPD Neutral File (NFF)";
    return true;
}

} // namespace Formats
