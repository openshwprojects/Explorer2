#pragma once
// XPlaneObj8Mesh.h — Laminar Research X-Plane 8+ 3D Aircraft / Scenery Object (.obj8, .obj)
//
// Spec-accurate parser for X-Plane OBJ8 (`I`/`A` + `800` + `OBJ`):
//   - Parses `POINT_COUNTS`, `VT` (x y z nx ny nz s t), `IDX10` & `IDX` index tables.
//   - Evaluates nested `ANIM_begin` / `ANIM_end` rest-pose transform stacks:
//     `ANIM_trans`, `ANIM_rotate`, `ANIM_trans_begin`/`ANIM_trans_key`, `ANIM_rotate_begin`/`ANIM_rotate_key`.
//   - Filters secondary `ATTR_lod` (`near > 0`) levels when primary LOD0 geometry is present.
//   - Instantiates `TRIS <offset> <count>` batches in right-handed Y-up space.
#include "../common/MeshBuild3D.h"

namespace Formats {

namespace DetailXPlaneObj8 {

struct XpMat4 {
    float m[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };

    static XpMat4 Identity() { return XpMat4{}; }

    static XpMat4 Translate(float tx, float ty, float tz) {
        XpMat4 r{};
        r.m[3] = tx; r.m[7] = ty; r.m[11] = tz;
        return r;
    }

    static XpMat4 RotateAxisDeg(float vx, float vy, float vz, float deg) {
        float len = std::sqrt(vx * vx + vy * vy + vz * vz);
        if (!std::isfinite(len) || len < 1e-6f || !std::isfinite(deg) || std::fabs(deg) < 1e-6f) {
            return Identity();
        }
        vx /= len; vy /= len; vz /= len;
        // X-Plane OBJ8 uses clockwise-positive or OpenGL right-hand rule around axis:
        // Laminar OBJ8 spec: positive rotation is clockwise when looking down the axis vector (+axis away from viewer),
        // which is -deg in standard right-hand rule (or +deg around -axis).
        float rad = -deg * (3.14159265358979323846f / 180.0f);
        float c = std::cos(rad), s = std::sin(rad), t = 1.0f - c;
        XpMat4 r{};
        r.m[0]  = t * vx * vx + c;      r.m[1]  = t * vx * vy - s * vz; r.m[2]  = t * vx * vz + s * vy;
        r.m[4]  = t * vx * vy + s * vz; r.m[5]  = t * vy * vy + c;      r.m[6]  = t * vy * vz - s * vx;
        r.m[8]  = t * vx * vz - s * vy; r.m[9]  = t * vy * vz + s * vx; r.m[10] = t * vz * vz + c;
        return r;
    }

    static XpMat4 Mul(const XpMat4& a, const XpMat4& b) {
        XpMat4 c{};
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                c.m[row * 4 + col] =
                    a.m[row * 4 + 0] * b.m[0 * 4 + col] +
                    a.m[row * 4 + 1] * b.m[1 * 4 + col] +
                    a.m[row * 4 + 2] * b.m[2 * 4 + col] +
                    a.m[row * 4 + 3] * b.m[3 * 4 + col];
            }
        }
        return c;
    }

    Vec3f TransformPoint(const Vec3f& p) const {
        return {
            m[0] * p.x + m[1] * p.y + m[2]  * p.z + m[3],
            m[4] * p.x + m[5] * p.y + m[6]  * p.z + m[7],
            m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11]
        };
    }

    bool IsIdentity() const {
        static constexpr float kId[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
        for (int i = 0; i < 16; ++i) {
            if (std::fabs(m[i] - kId[i]) > 1e-6f) return false;
        }
        return true;
    }
};

inline bool IsXPlaneObj8Header(const uint8_t* data, size_t size) {
    if (!data || size < 9) return false;
    size_t pos = 0;
    if (size >= 3 && data[0] == 0xEFu && data[1] == 0xBBu && data[2] == 0xBFu) pos = 3;
    while (pos < size && (data[pos] == ' ' || data[pos] == '\t' || data[pos] == '\r' || data[pos] == '\n')) ++pos;
    if (pos >= size || (data[pos] != 'I' && data[pos] != 'A')) return false;
    ++pos;
    while (pos < size && (data[pos] == ' ' || data[pos] == '\t')) ++pos;
    if (pos >= size || (data[pos] != '\r' && data[pos] != '\n')) return false;
    while (pos < size && (data[pos] == '\r' || data[pos] == '\n' || data[pos] == ' ' || data[pos] == '\t')) ++pos;
    if (pos + 3 > size || std::memcmp(data + pos, "800", 3) != 0) return false;
    pos += 3;
    while (pos < size && data[pos] != '\n') ++pos;
    if (pos >= size) return false;
    ++pos;
    while (pos < size && (data[pos] == '\r' || data[pos] == ' ' || data[pos] == '\t')) ++pos;
    return (pos + 3 <= size && std::memcmp(data + pos, "OBJ", 3) == 0 &&
            (pos + 3 == size || data[pos + 3] == '\r' || data[pos + 3] == '\n' || data[pos + 3] == ' ' || data[pos + 3] == '\t'));
}

inline float EvalRestLerp(float out1, float out2, float v1, float v2, bool hasVRange) {
    if (!hasVRange || std::fabs(v2 - v1) < 1e-6f) return out1;
    float vRest = std::clamp(0.0f, std::min(v1, v2), std::max(v1, v2));
    float t = (vRest - v1) / (v2 - v1);
    return out1 + t * (out2 - out1);
}

} // namespace DetailXPlaneObj8

inline bool ParseXPlaneObj8Mesh(const uint8_t* data, size_t size, LoadedMesh3D& outMesh) {
    using namespace DetailXPlaneObj8;
    if (!IsXPlaneObj8Header(data, size)) return false;

    const size_t nulCheck = std::min<size_t>(size, 8192u);
    for (size_t i = 0; i < nulCheck; ++i) {
        if (data[i] == 0) return false;
    }

    std::string_view sv(reinterpret_cast<const char*>(data), size);
    std::vector<Vec3f> vtPool;
    std::vector<uint32_t> idxPool;

    struct DrawBatch {
        uint32_t offset = 0;
        uint32_t count = 0;
        XpMat4 xf;
        bool isPrimaryLod = true;
    };
    std::vector<DrawBatch> batches;

    std::vector<XpMat4> animStack;
    animStack.push_back(XpMat4::Identity());

    bool inTransKeyBlock = false;
    bool haveBestTransKey = false;
    float bestTransAbsVal = 1e30f;
    Vec3f bestTransVec{ 0, 0, 0 };

    bool inRotKeyBlock = false;
    bool haveBestRotKey = false;
    float bestRotAbsVal = 1e30f;
    float bestRotDeg = 0.0f;
    Vec3f curRotAxis{ 0, 1, 0 };

    bool currentLodPrimary = true;
    int64_t declVt = -1, declIdx = -1;

    size_t linePos = 0;
    std::string_view rawLine;
    while (ReaderText::NextLineSv(sv, linePos, rawLine)) {
        size_t hash = rawLine.find('#');
        if (hash != std::string_view::npos) rawLine = rawLine.substr(0, hash);
        std::string_view line = ReaderText::TrimSv(rawLine);
        if (line.empty()) continue;

        std::string_view kw;
        if (!ReaderText::NextTokenSv(line, kw)) continue;

        if (kw == "VT") {
            float x = 0, y = 0, z = 0;
            std::string_view tx, ty, tz;
            if (!ReaderText::NextTokenSv(line, tx) || !ReaderText::NextTokenSv(line, ty) || !ReaderText::NextTokenSv(line, tz) ||
                !ReaderText::ParseFloatSv(tx, x) || !ReaderText::ParseFloatSv(ty, y) || !ReaderText::ParseFloatSv(tz, z) ||
                !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
                return false;
            }
            if (vtPool.size() < 4000000u) vtPool.push_back({ x, y, z });
        } else if (kw == "IDX10" || kw == "IDX") {
            std::string_view itok;
            int parsedOnLine = 0;
            while (ReaderText::NextTokenSv(line, itok)) {
                int64_t iv = -1;
                if (!ReaderText::ParseInt64Sv(itok, iv) || iv < 0 || iv > 0x7FFFFFFFLL) return false;
                if (idxPool.size() < 12000000u) idxPool.push_back(static_cast<uint32_t>(iv));
                ++parsedOnLine;
            }
            if (parsedOnLine == 0 || (kw == "IDX10" && parsedOnLine != 10)) return false;
        } else if (kw == "POINT_COUNTS") {
            std::string_view t0, t1, t2, t3;
            int64_t nTrisVt = 0, nLinesVt = 0, nLightsVt = 0, nIdx = 0;
            if (!ReaderText::NextTokenSv(line, t0) || !ReaderText::NextTokenSv(line, t1) ||
                !ReaderText::NextTokenSv(line, t2) || !ReaderText::NextTokenSv(line, t3) ||
                !ReaderText::ParseInt64Sv(t0, nTrisVt) || !ReaderText::ParseInt64Sv(t1, nLinesVt) ||
                !ReaderText::ParseInt64Sv(t2, nLightsVt) || !ReaderText::ParseInt64Sv(t3, nIdx) ||
                nTrisVt < 0 || nIdx < 0) {
                return false;
            }
            (void)nLinesVt; (void)nLightsVt;
            declVt = nTrisVt;
            declIdx = nIdx;
            if (nTrisVt > 0 && nTrisVt <= 2000000) vtPool.reserve(static_cast<size_t>(nTrisVt));
            if (nIdx > 0 && nIdx <= 6000000) idxPool.reserve(static_cast<size_t>(nIdx));
        } else if (kw == "ATTR_lod") {
            std::string_view nearTok;
            float nearDist = 0.0f;
            if (ReaderText::NextTokenSv(line, nearTok) && ReaderText::ParseFloatSv(nearTok, nearDist)) {
                currentLodPrimary = (nearDist <= 0.5f);
            }
        } else if (kw == "ANIM_begin") {
            if (animStack.size() < 64u) animStack.push_back(animStack.back());
        } else if (kw == "ANIM_end") {
            if (animStack.size() > 1u) animStack.pop_back();
        } else if (kw == "ANIM_trans") {
            float f[8] = {};
            int nf = 0;
            std::string_view ftok;
            while (nf < 8 && ReaderText::NextTokenSv(line, ftok)) {
                if (!ReaderText::ParseFloatSv(ftok, f[nf])) break;
                ++nf;
            }
            if (nf >= 6) {
                bool hasRange = (nf >= 8);
                float tx = EvalRestLerp(f[0], f[3], f[6], f[7], hasRange);
                float ty = EvalRestLerp(f[1], f[4], f[6], f[7], hasRange);
                float tz = EvalRestLerp(f[2], f[5], f[6], f[7], hasRange);
                animStack.back() = XpMat4::Mul(animStack.back(), XpMat4::Translate(tx, ty, tz));
            }
        } else if (kw == "ANIM_trans_begin") {
            inTransKeyBlock = true;
            haveBestTransKey = false;
            bestTransAbsVal = 1e30f;
        } else if (kw == "ANIM_trans_key" && inTransKeyBlock) {
            float kv = 0, kx = 0, ky = 0, kz = 0;
            std::string_view t0, t1, t2, t3;
            if (ReaderText::NextTokenSv(line, t0) && ReaderText::NextTokenSv(line, t1) &&
                ReaderText::NextTokenSv(line, t2) && ReaderText::NextTokenSv(line, t3) &&
                ReaderText::ParseFloatSv(t0, kv) && ReaderText::ParseFloatSv(t1, kx) &&
                ReaderText::ParseFloatSv(t2, ky) && ReaderText::ParseFloatSv(t3, kz))
            {
                if (!haveBestTransKey || std::fabs(kv) < bestTransAbsVal) {
                    haveBestTransKey = true;
                    bestTransAbsVal = std::fabs(kv);
                    bestTransVec = { kx, ky, kz };
                }
            }
        } else if (kw == "ANIM_trans_end") {
            if (inTransKeyBlock && haveBestTransKey) {
                animStack.back() = XpMat4::Mul(animStack.back(), XpMat4::Translate(bestTransVec.x, bestTransVec.y, bestTransVec.z));
            }
            inTransKeyBlock = false;
        } else if (kw == "ANIM_rotate") {
            float f[7] = {};
            int nf = 0;
            std::string_view ftok;
            while (nf < 7 && ReaderText::NextTokenSv(line, ftok)) {
                if (!ReaderText::ParseFloatSv(ftok, f[nf])) break;
                ++nf;
            }
            if (nf >= 5) {
                bool hasRange = (nf >= 7);
                float deg = EvalRestLerp(f[3], f[4], f[5], f[6], hasRange);
                animStack.back() = XpMat4::Mul(animStack.back(), XpMat4::RotateAxisDeg(f[0], f[1], f[2], deg));
            }
        } else if (kw == "ANIM_rotate_begin") {
            float vx = 0, vy = 1, vz = 0;
            std::string_view t0, t1, t2;
            if (ReaderText::NextTokenSv(line, t0) && ReaderText::NextTokenSv(line, t1) && ReaderText::NextTokenSv(line, t2) &&
                ReaderText::ParseFloatSv(t0, vx) && ReaderText::ParseFloatSv(t1, vy) && ReaderText::ParseFloatSv(t2, vz)) {
                curRotAxis = { vx, vy, vz };
                inRotKeyBlock = true;
                haveBestRotKey = false;
                bestRotAbsVal = 1e30f;
            }
        } else if (kw == "ANIM_rotate_key" && inRotKeyBlock) {
            float kv = 0, kd = 0;
            std::string_view t0, t1;
            if (ReaderText::NextTokenSv(line, t0) && ReaderText::NextTokenSv(line, t1) &&
                ReaderText::ParseFloatSv(t0, kv) && ReaderText::ParseFloatSv(t1, kd))
            {
                if (!haveBestRotKey || std::fabs(kv) < bestRotAbsVal) {
                    haveBestRotKey = true;
                    bestRotAbsVal = std::fabs(kv);
                    bestRotDeg = kd;
                }
            }
        } else if (kw == "ANIM_rotate_end") {
            if (inRotKeyBlock && haveBestRotKey) {
                animStack.back() = XpMat4::Mul(animStack.back(), XpMat4::RotateAxisDeg(curRotAxis.x, curRotAxis.y, curRotAxis.z, bestRotDeg));
            }
            inRotKeyBlock = false;
        } else if (kw == "TRIS") {
            std::string_view offTok, cntTok;
            int64_t off = -1, cnt = -1;
            if (!ReaderText::NextTokenSv(line, offTok) || !ReaderText::NextTokenSv(line, cntTok) ||
                !ReaderText::ParseInt64Sv(offTok, off) || !ReaderText::ParseInt64Sv(cntTok, cnt) ||
                off < 0 || cnt < 3 || (cnt % 3) != 0) {
                return false;
            }
            batches.push_back({ static_cast<uint32_t>(off), static_cast<uint32_t>(cnt), animStack.back(), currentLodPrimary });
        }
    }

    if (vtPool.size() < 3u || idxPool.size() < 3u || batches.empty()) return false;
    if (declVt >= 0 && vtPool.size() != static_cast<size_t>(declVt)) return false;
    if (declIdx >= 0 && idxPool.size() != static_cast<size_t>(declIdx)) return false;

    bool hasPrimaryBatch = false;
    for (const auto& b : batches) {
        if (b.isPrimaryLod) { hasPrimaryBatch = true; break; }
    }

    outMesh = LoadedMesh3D{};
    // Fast path: if all batches use identity transform, share vtPool directly!
    bool allIdentity = true;
    for (const auto& b : batches) {
        if (hasPrimaryBatch && !b.isPrimaryLod) continue;
        if (!b.xf.IsIdentity()) { allIdentity = false; break; }
    }

    if (allIdentity) {
        outMesh.vertices = vtPool;
        for (const auto& b : batches) {
            if (hasPrimaryBatch && !b.isPrimaryLod) continue;
            if (static_cast<uint64_t>(b.offset) + b.count > idxPool.size()) { outMesh = {}; return false; }
            for (uint32_t k = 0; k + 2 < b.count; k += 3) {
                uint32_t i0 = idxPool[b.offset + k + 0];
                uint32_t i1 = idxPool[b.offset + k + 1];
                uint32_t i2 = idxPool[b.offset + k + 2];
                if (i0 >= vtPool.size() || i1 >= vtPool.size() || i2 >= vtPool.size()) { outMesh = {}; return false; }
                if (i0 != i1 && i1 != i2 && i0 != i2) {
                    outMesh.tris.push_back({ i0, i1, i2, {} });
                }
            }
        }
    } else {
        for (const auto& b : batches) {
            if (hasPrimaryBatch && !b.isPrimaryLod) continue;
            if (static_cast<uint64_t>(b.offset) + b.count > idxPool.size()) { outMesh = {}; return false; }
            std::unordered_map<uint32_t, uint32_t> remap;
            auto mapVert = [&](uint32_t srcIdx, uint32_t& dstIdx) -> bool {
                if (srcIdx >= vtPool.size()) return false;
                auto it = remap.find(srcIdx);
                if (it != remap.end()) { dstIdx = it->second; return true; }
                dstIdx = static_cast<uint32_t>(outMesh.vertices.size());
                outMesh.vertices.push_back(b.xf.TransformPoint(vtPool[srcIdx]));
                remap[srcIdx] = dstIdx;
                return true;
            };
            for (uint32_t k = 0; k + 2 < b.count; k += 3) {
                uint32_t s0 = idxPool[b.offset + k + 0];
                uint32_t s1 = idxPool[b.offset + k + 1];
                uint32_t s2 = idxPool[b.offset + k + 2];
                uint32_t d0 = 0, d1 = 0, d2 = 0;
                if (!mapVert(s0, d0) || !mapVert(s1, d1) || !mapVert(s2, d2)) { outMesh = {}; return false; }
                if (d0 != d1 && d1 != d2 && d0 != d2) {
                    outMesh.tris.push_back({ d0, d1, d2, {} });
                }
            }
        }
    }

    if (outMesh.vertices.empty() || outMesh.tris.empty()) {
        outMesh = {};
        return false;
    }
    outMesh.formatName = L"X-Plane 8+ 3D Object (.obj8)";
    return true;
}

} // namespace Formats
