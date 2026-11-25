#pragma once
// MaxonCinema4d.h — Maxon Cinema 4D (.c4d) 3D scene reader:
//   1) Classic Amiga IFF `FORM....MC4D` (`OBJ `, `OPOL`, `POLP`/`PNTS`/`PNT8`, `POLY`/`POLS`)
//   2) Cinema 4D v6–2025 `C4DC` / `C4D6` HyperFile chunk hierarchies (Cineware `HYPERFILEVALUE_START`
//      and numeric chunk headers: `Opolygon=5100`, `Onull=5140`, `Tpoint=5600` `Vector64`/`Vector32`,
//      `Tpolygon=5604` `CPolygon` `[a, b, c, d]` quad/tri indices, and `Mg` `Matrix64`/`Matrix32` transforms).
#include "../common/MeshFormatCommon.h"

namespace Formats {

namespace DetailC4dHyperFile {

struct C4dMat4x3 {
    // Cinema 4D Matrix: off (translation) + v1 (X basis), v2 (Y basis), v3 (Z basis)
    // p_world = off + v1 * p.x + v2 * p.y + v3 * p.z
    double off[3] = { 0.0, 0.0, 0.0 };
    double v1[3]  = { 1.0, 0.0, 0.0 };
    double v2[3]  = { 0.0, 1.0, 0.0 };
    double v3[3]  = { 0.0, 0.0, 1.0 };

    Vec3f Transform(const Vec3f& p) const {
        double x = off[0] + v1[0] * p.x + v2[0] * p.y + v3[0] * p.z;
        double y = off[1] + v1[1] * p.x + v2[1] * p.y + v3[1] * p.z;
        double z = off[2] + v1[2] * p.x + v2[2] * p.y + v3[2] * p.z;
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return { 0.0f, 0.0f, 0.0f };
        return { static_cast<float>(x), static_cast<float>(y), static_cast<float>(z) };
    }

    static C4dMat4x3 Mul(const C4dMat4x3& a, const C4dMat4x3& b) {
        C4dMat4x3 r{};
        auto mulVec = [&](const double v[3], double out[3]) {
            out[0] = a.v1[0] * v[0] + a.v2[0] * v[1] + a.v3[0] * v[2];
            out[1] = a.v1[1] * v[0] + a.v2[1] * v[1] + a.v3[1] * v[2];
            out[2] = a.v1[2] * v[0] + a.v2[2] * v[1] + a.v3[2] * v[2];
        };
        mulVec(b.v1, r.v1);
        mulVec(b.v2, r.v2);
        mulVec(b.v3, r.v3);
        double bo[3] = {};
        mulVec(b.off, bo);
        r.off[0] = a.off[0] + bo[0];
        r.off[1] = a.off[1] + bo[1];
        r.off[2] = a.off[2] + bo[2];
        return r;
    }

    bool IsReasonable() const {
        for (int i = 0; i < 3; ++i) {
            if (!std::isfinite(off[i]) || !std::isfinite(v1[i]) || !std::isfinite(v2[i]) || !std::isfinite(v3[i])) return false;
            if (std::fabs(off[i]) > 1e8 || std::fabs(v1[i]) > 1e6 || std::fabs(v2[i]) > 1e6 || std::fabs(v3[i]) > 1e6) return false;
        }
        double det = v1[0] * (v2[1] * v3[2] - v2[2] * v3[1])
                   - v1[1] * (v2[0] * v3[2] - v2[2] * v3[0])
                   + v1[2] * (v2[0] * v3[1] - v2[1] * v3[0]);
        return std::isfinite(det) && std::fabs(det) > 1e-9;
    }
};

inline bool ReadMatrix64BE(const uint8_t* p, C4dMat4x3& out) {
    using namespace DetailDccSculpt;
    out.off[0] = RdF64BE(p + 0);  out.off[1] = RdF64BE(p + 8);  out.off[2] = RdF64BE(p + 16);
    out.v1[0]  = RdF64BE(p + 24); out.v1[1]  = RdF64BE(p + 32); out.v1[2]  = RdF64BE(p + 40);
    out.v2[0]  = RdF64BE(p + 48); out.v2[1]  = RdF64BE(p + 56); out.v2[2]  = RdF64BE(p + 64);
    out.v3[0]  = RdF64BE(p + 72); out.v3[1]  = RdF64BE(p + 80); out.v3[2]  = RdF64BE(p + 88);
    return out.IsReasonable();
}

inline bool ReadMatrix32BE(const uint8_t* p, C4dMat4x3& out) {
    using namespace DetailDccSculpt;
    out.off[0] = RdF32BE(p + 0);  out.off[1] = RdF32BE(p + 4);  out.off[2] = RdF32BE(p + 8);
    out.v1[0]  = RdF32BE(p + 12); out.v1[1]  = RdF32BE(p + 16); out.v1[2]  = RdF32BE(p + 20);
    out.v2[0]  = RdF32BE(p + 24); out.v2[1]  = RdF32BE(p + 28); out.v2[2]  = RdF32BE(p + 32);
    out.v3[0]  = RdF32BE(p + 36); out.v3[1]  = RdF32BE(p + 40); out.v3[2]  = RdF32BE(p + 44);
    return out.IsReasonable();
}

// Decodes a Tpoint (5600) payload: supports HyperFile MEMORY block (`0x01` + u32BE/u64BE byteLen),
// explicit count header (`u32BE count` + Vector64/Vector32), or direct Vector64 (24B) / Vector32 (12B) array.
inline bool DecodePointTagPayload(const uint8_t* p, size_t len, std::vector<Vec3f>& outPts) {
    using namespace DetailDccSculpt;
    outPts.clear();
    if (len < 12) return false;

    size_t off = 0;
    // Unwrap optional HYPERFILEVALUE_MEMORY (1) prefix: [uint8(1)][uint32BE byteSize]
    if (p[0] == 1u && len >= 5u) {
        uint32_t memSz = RdU32BE(p + 1);
        if (memSz == len - 5u || memSz + 5u <= len) {
            p += 5;
            len = memSz;
        }
    }

    // Check explicit [uint32BE pointCount] + Vector64[pointCount] or Vector32[pointCount]
    bool use64 = false;
    uint32_t count = 0;
    if (len >= 4) {
        uint32_t hdrCnt = RdU32BE(p);
        if (hdrCnt >= 3 && hdrCnt <= 2000000u && 4ULL + static_cast<uint64_t>(hdrCnt) * 24ULL == len) {
            off = 4; count = hdrCnt; use64 = true;
        } else if (hdrCnt >= 3 && hdrCnt <= 2000000u && 4ULL + static_cast<uint64_t>(hdrCnt) * 12ULL == len) {
            off = 4; count = hdrCnt; use64 = false;
        }
    }
    // Check HyperFile GeData Vector64/Vector32 array: [uint8(24 or 15)] per element OR raw array
    if (count == 0) {
        if ((len % 24u) == 0u && (len / 24u) >= 3u && (len / 24u) <= 2000000u) {
            // Distinguish Vector64 vs even-count Vector32 by inspecting exponent sanity of first point
            bool ok64 = true;
            uint32_t n64 = static_cast<uint32_t>(len / 24u);
            for (uint32_t i = 0; i < std::min<uint32_t>(n64, 4u); ++i) {
                double x = RdF64BE(p + i * 24u + 0);
                double y = RdF64BE(p + i * 24u + 8);
                double z = RdF64BE(p + i * 24u + 16);
                if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
                    std::fabs(x) > 1e7 || std::fabs(y) > 1e7 || std::fabs(z) > 1e7) {
                    ok64 = false; break;
                }
            }
            if (ok64) { count = n64; use64 = true; }
        }
        if (count == 0 && (len % 12u) == 0u && (len / 12u) >= 3u && (len / 12u) <= 2000000u) {
            count = static_cast<uint32_t>(len / 12u);
            use64 = false;
        }
    }
    if (count < 3u) return false;

    outPts.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        float x = 0, y = 0, z = 0;
        if (use64) {
            x = static_cast<float>(RdF64BE(p + off + static_cast<size_t>(i) * 24u + 0));
            y = static_cast<float>(RdF64BE(p + off + static_cast<size_t>(i) * 24u + 8));
            z = static_cast<float>(RdF64BE(p + off + static_cast<size_t>(i) * 24u + 16));
        } else {
            x = RdF32BE(p + off + static_cast<size_t>(i) * 12u + 0);
            y = RdF32BE(p + off + static_cast<size_t>(i) * 12u + 4);
            z = RdF32BE(p + off + static_cast<size_t>(i) * 12u + 8);
        }
        if (!IsValidCoord3(x, y, z)) { outPts.clear(); return false; }
        outPts.push_back({ x, y, z });
    }
    return true;
}

// Decodes a Tpolygon (5604) payload: CPolygon `[a, b, c, d]` (`c == d` -> triangle, `c != d` -> quad)
inline bool DecodePolygonTagPayload(
    const uint8_t* p,
    size_t len,
    size_t numPts,
    std::vector<std::array<uint32_t, 3>>& outTris)
{
    using namespace DetailDccSculpt;
    outTris.clear();
    if (len < 8 || numPts < 3) return false;

    if (p[0] == 1u && len >= 5u) {
        uint32_t memSz = RdU32BE(p + 1);
        if (memSz <= len - 5u) { p += 5; len = memSz; }
    }

    size_t off = 0;
    uint32_t count = 0;
    bool use16 = false;
    if (len >= 4) {
        uint32_t hdrCnt = RdU32BE(p);
        if (hdrCnt >= 1 && hdrCnt <= 4000000u && 4ULL + static_cast<uint64_t>(hdrCnt) * 16ULL == len) {
            off = 4; count = hdrCnt; use16 = false;
        } else if (hdrCnt >= 1 && hdrCnt <= 4000000u && 4ULL + static_cast<uint64_t>(hdrCnt) * 8ULL == len) {
            off = 4; count = hdrCnt; use16 = true;
        }
    }
    if (count == 0) {
        if ((len % 16u) == 0u && (len / 16u) >= 1u && (len / 16u) <= 4000000u) {
            // Check if 32-bit indices are all < numPts
            uint32_t n32 = static_cast<uint32_t>(len / 16u);
            bool ok32 = true;
            for (uint32_t i = 0; i < n32; ++i) {
                for (int k = 0; k < 4; ++k) {
                    if (RdU32BE(p + static_cast<size_t>(i) * 16u + k * 4u) >= numPts) { ok32 = false; break; }
                }
                if (!ok32) break;
            }
            if (ok32) { count = n32; use16 = false; }
        }
        if (count == 0 && (len % 8u) == 0u && (len / 8u) >= 1u && (len / 8u) <= 4000000u) {
            count = static_cast<uint32_t>(len / 8u);
            use16 = true;
        }
    }
    if (count == 0) return false;

    for (uint32_t i = 0; i < count; ++i) {
        uint32_t a = 0, b = 0, c = 0, d = 0;
        if (use16) {
            const uint8_t* fp = p + off + static_cast<size_t>(i) * 8u;
            a = RdU16BE(fp + 0); b = RdU16BE(fp + 2); c = RdU16BE(fp + 4); d = RdU16BE(fp + 6);
        } else {
            const uint8_t* fp = p + off + static_cast<size_t>(i) * 16u;
            a = RdU32BE(fp + 0); b = RdU32BE(fp + 4); c = RdU32BE(fp + 8); d = RdU32BE(fp + 12);
        }
        if (a >= numPts || b >= numPts || c >= numPts || d >= numPts) {
            outTris.clear();
            return false;
        }
        if (a != b && b != c && a != c) {
            outTris.push_back({ a, b, c });
        }
        if (c != d && a != c && a != d) {
            outTris.push_back({ a, c, d });
        }
    }
    return !outTris.empty();
}

// Walks a Cinema 4D HyperFile chunk range (supports both Cineware HYPERFILEVALUE_START=2/STOP=3
// `[u8(2)][i32BE id][i32BE level][u32BE size]...[u8(3)]` AND 8-byte `[u32BE id][u32BE size]` chunks).
inline void WalkHyperFileScope(
    const uint8_t* buf,
    size_t s,
    size_t e,
    const C4dMat4x3& parentXf,
    int depth,
    LoadedMesh3D& outMesh)
{
    using namespace DetailDccSculpt;
    if (depth > 32 || s >= e) return;

    C4dMat4x3 curXf = parentXf;
    bool sawLocalXf = false;
    std::vector<Vec3f> scopePts;
    std::vector<std::array<uint32_t, 3>> scopeTris;

    size_t p = s;
    while (p < e) {
        uint8_t vb = buf[p];

        // 1. Cineware HyperFile value tags inside object chunk:
        if (vb == 3u) { // HYPERFILEVALUE_STOP
            ++p;
            continue;
        }
        if (vb == 25u && p + 97u <= e) { // HYPERFILEVALUE_MATRIX64 (1B + 96B)
            C4dMat4x3 loc{};
            if (!sawLocalXf && ReadMatrix64BE(buf + p + 1u, loc)) {
                curXf = C4dMat4x3::Mul(parentXf, loc);
                sawLocalXf = true;
            }
            p += 97u;
            continue;
        }
        if (vb == 16u && p + 49u <= e) { // HYPERFILEVALUE_MATRIX (1B + 48B)
            C4dMat4x3 loc{};
            if (!sawLocalXf && ReadMatrix32BE(buf + p + 1u, loc)) {
                curXf = C4dMat4x3::Mul(parentXf, loc);
                sawLocalXf = true;
            }
            p += 49u;
            continue;
        }
        if (vb == 2u && p + 13u <= e) { // HYPERFILEVALUE_START: [u8(2)][u32BE chunkId][u32BE level][u32BE size]
            uint32_t chunkId  = RdU32BE(buf + p + 1u);
            uint32_t chunkLvl = RdU32BE(buf + p + 5u);
            uint32_t chunkSz  = RdU32BE(buf + p + 9u);
            (void)chunkLvl;
            if (chunkLvl <= 100000u && chunkSz <= e - (p + 13u)) {
                size_t payStart = p + 13u;
                size_t payEnd   = payStart + static_cast<size_t>(chunkSz);
                if (chunkId == 5600u) { // Tpoint
                    DecodePointTagPayload(buf + payStart, chunkSz, scopePts);
                } else if (chunkId == 5604u && !scopePts.empty()) { // Tpolygon
                    std::vector<std::array<uint32_t, 3>> t;
                    if (DecodePolygonTagPayload(buf + payStart, chunkSz, scopePts.size(), t)) {
                        scopeTris.insert(scopeTris.end(), t.begin(), t.end());
                    }
                } else if ((chunkId == 5000u || chunkId == 5001u || chunkId == 110050u) && (chunkSz == 96u || chunkSz == 48u)) {
                    // Explicit Mg / Ml matrix chunk
                    C4dMat4x3 loc{};
                    bool okM = (chunkSz == 96u) ? ReadMatrix64BE(buf + payStart, loc) : ReadMatrix32BE(buf + payStart, loc);
                    if (okM && !sawLocalXf) { curXf = C4dMat4x3::Mul(parentXf, loc); sawLocalXf = true; }
                } else {
                    WalkHyperFileScope(buf, payStart, payEnd, curXf, depth + 1, outMesh);
                }
                p = payEnd;
                if (p < e && buf[p] == 3u) ++p; // consume trailing HYPERFILEVALUE_STOP
                continue;
            }
        }

        // 2. Classic 8-byte chunk header `[uint32BE tagOrChunkId][uint32BE chunkSize]`:
        if (p + 8u > e) break;
        const uint8_t* tag = buf + p;
        uint32_t rawId  = RdU32BE(buf + p + 0u);
        uint32_t cLen   = RdU32BE(buf + p + 4u);
        uint32_t cleanId = rawId & 0x7FFFFFFFu;
        if (cLen > e - (p + 8u)) break;

        const size_t payStart = p + 8u;
        const size_t payEnd   = payStart + static_cast<size_t>(cLen);

        const bool isClassic4ccCont =
            std::memcmp(tag, "OBJ ", 4) == 0 || std::memcmp(tag, "OPOL", 4) == 0 ||
            std::memcmp(tag, "ROOT", 4) == 0 || std::memcmp(tag, "SCEN", 4) == 0 ||
            std::memcmp(tag, "DOKU", 4) == 0;
        const bool isNumericCont =
            (rawId & 0x80000000u) != 0u ||
            cleanId == 5100u /*Opolygon*/ || cleanId == 5140u /*Onull*/ ||
            cleanId == 110059u /*Document*/ || cleanId == 110060u /*ObjectList*/ ||
            cleanId == 300001000u;

        if (isClassic4ccCont || isNumericCont) {
            WalkHyperFileScope(buf, payStart, payEnd, curXf, depth + 1, outMesh);
            p = payEnd + (isClassic4ccCont ? (cLen & 1u) : 0u);
            continue;
        }

        // Numeric Mg transform chunk (id 5000 / 110050 or 4CC "MATR" / "OMAT")
        if ((cleanId == 5000u || cleanId == 110050u ||
             std::memcmp(tag, "MATR", 4) == 0 || std::memcmp(tag, "OMAT", 4) == 0) &&
            (cLen == 96u || cLen == 48u))
        {
            C4dMat4x3 loc{};
            bool okM = (cLen == 96u) ? ReadMatrix64BE(buf + payStart, loc) : ReadMatrix32BE(buf + payStart, loc);
            if (okM && !sawLocalXf) { curXf = C4dMat4x3::Mul(parentXf, loc); sawLocalXf = true; }
            p = payEnd;
            continue;
        }

        // Numeric Tpoint (5600) or classic 4CC POLP / PNTS / Poin / PNT8 / Poi8
        if (cleanId == 5600u ||
            std::memcmp(tag, "POLP", 4) == 0 || std::memcmp(tag, "PNTS", 4) == 0 ||
            std::memcmp(tag, "Poin", 4) == 0 || std::memcmp(tag, "PNT8", 4) == 0 ||
            std::memcmp(tag, "Poi8", 4) == 0)
        {
            if (std::memcmp(tag, "PNT8", 4) == 0 || std::memcmp(tag, "Poi8", 4) == 0) {
                if (cLen >= 72u && (cLen % 24u) == 0u) {
                    scopePts.clear();
                    uint32_t nv = cLen / 24u;
                    for (uint32_t i = 0; i < nv && nv <= 2000000u; ++i) {
                        float x = static_cast<float>(RdF64BE(buf + payStart + static_cast<size_t>(i) * 24u + 0));
                        float y = static_cast<float>(RdF64BE(buf + payStart + static_cast<size_t>(i) * 24u + 8));
                        float z = static_cast<float>(RdF64BE(buf + payStart + static_cast<size_t>(i) * 24u + 16));
                        if (!IsValidCoord3(x, y, z)) { scopePts.clear(); break; }
                        scopePts.push_back({ x, y, z });
                    }
                }
            } else {
                DecodePointTagPayload(buf + payStart, cLen, scopePts);
            }
            p = payEnd + (cleanId == 5600u ? 0u : (cLen & 1u));
            continue;
        }

        // Numeric Tpolygon (5604) or classic 4CC POLY / POLS / Polg
        if ((cleanId == 5604u ||
             std::memcmp(tag, "POLY", 4) == 0 || std::memcmp(tag, "POLS", 4) == 0 ||
             std::memcmp(tag, "Polg", 4) == 0) && !scopePts.empty())
        {
            std::vector<std::array<uint32_t, 3>> t;
            if (DecodePolygonTagPayload(buf + payStart, cLen, scopePts.size(), t)) {
                scopeTris.insert(scopeTris.end(), t.begin(), t.end());
            }
            p = payEnd + (cleanId == 5604u ? 0u : (cLen & 1u));
            continue;
        }

        // Unknown leaf chunk: advance cleanly if tag looks like printable IFF 4CC or reasonable numeric chunk ID
        bool printable4cc = true;
        for (int k = 0; k < 4; ++k) {
            if (tag[k] < 0x20u || tag[k] > 0x7Eu) { printable4cc = false; break; }
        }
        if (printable4cc) {
            p = payEnd + (cLen & 1u);
        } else if (cleanId > 0u && cleanId < 500000000u) {
            p = payEnd;
        } else {
            break;
        }
    }

    if (!scopePts.empty() && !scopeTris.empty()) {
        uint32_t vBase = static_cast<uint32_t>(outMesh.vertices.size());
        outMesh.vertices.reserve(outMesh.vertices.size() + scopePts.size());
        for (const auto& pt : scopePts) {
            outMesh.vertices.push_back(curXf.Transform(pt));
        }
        outMesh.tris.reserve(outMesh.tris.size() + scopeTris.size());
        for (const auto& tr : scopeTris) {
            outMesh.tris.push_back({ vBase + tr[0], vBase + tr[1], vBase + tr[2], {} });
        }
    }
}

} // namespace DetailC4dHyperFile

inline bool ParseCinema4dEnhanced(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    using namespace DetailDccSculpt;
    if (!data || size < 16) return false;

    size_t bodyOff = 0;
    if (std::memcmp(data, "FORM", 4) == 0 && size >= 12 &&
        (std::memcmp(data + 8, "MC4D", 4) == 0 || std::memcmp(data + 8, "C4DC", 4) == 0)) {
        bodyOff = 12;
    } else if (size >= 9 && (std::memcmp(data + 1, "C4DC", 4) == 0 || std::memcmp(data + 1, "C4D6", 4) == 0)) {
        // Real Cinema 4D v6 / R12-2025 HyperFile header: [uint8 fileIdent (e.g. 0x57/0x58)] "C4DC" [uint32BE flags/ver]
        bodyOff = 9;
    } else if (std::memcmp(data, "C4DC", 4) == 0 || std::memcmp(data, "C4D6", 4) == 0 || std::memcmp(data, "MC4D", 4) == 0) {
        bodyOff = 8;
    } else if (size >= 12 && (std::memcmp(data + 4, "C4DC", 4) == 0 || std::memcmp(data + 4, "MC4D", 4) == 0 || std::memcmp(data + 4, "C4D6", 4) == 0)) {
        bodyOff = 12;
    } else if (size >= 16 && (std::memcmp(data + 8, "C4DC", 4) == 0 || std::memcmp(data + 8, "MC4D", 4) == 0)) {
        bodyOff = 16;
    } else {
        return false;
    }

    std::vector<uint8_t> inflated;
    const uint8_t* buf = data + bodyOff;
    size_t bufLen = size - bodyOff;
    if (TryFindAndInflateZlib(buf, bufLen, 24, inflated)) {
        buf = inflated.data();
        bufLen = inflated.size();
    }

    mesh.vertices.clear();
    mesh.tris.clear();

    DetailC4dHyperFile::C4dMat4x3 ident{};
    DetailC4dHyperFile::WalkHyperFileScope(buf, 0, bufLen, ident, 0, mesh);
    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"Maxon Cinema 4D Scene (.c4d)";
    return true;
}

} // namespace Formats
