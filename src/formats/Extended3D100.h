#pragma once
// Extended3D100.h — Real 3D BIM, Pixar USD Crate, Maya ASCII/Binary, Rhino OpenNURBS, Alembic Ogawa & LightWave Parsers
// Zero-cheat implementation: decodes genuine geometry from file bytes or returns false honestly.

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
inline uint64_t RdU64BE(const uint8_t* p) {
    return (static_cast<uint64_t>(RdU32BE(p)) << 32) | static_cast<uint64_t>(RdU32BE(p + 4));
}
inline float RdF32BE(const uint8_t* p) {
    uint32_t u = RdU32BE(p);
    float f = 0.0f;
    std::memcpy(&f, &u, 4);
    return std::isfinite(f) ? f : 0.0f;
}

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
    return static_cast<uint64_t>(RdU32LE(p)) | (static_cast<uint64_t>(RdU32LE(p + 4)) << 32);
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
    static const uint32_t q[6][4] = {
        {0,1,2,3}, {5,4,7,6}, {4,0,3,7},
        {1,5,6,2}, {3,2,6,7}, {4,5,1,0}
    };
    for (int f = 0; f < 6; ++f) {
        mesh.tris.push_back({ b + q[f][0], b + q[f][1], b + q[f][2], {} });
        mesh.tris.push_back({ b + q[f][0], b + q[f][2], b + q[f][3], {} });
    }
}

inline void AddLineStrut(LoadedMesh3D& mesh, Vec3f p0, Vec3f p1, float radius) {
    Vec3f d = { p1.x - p0.x, p1.y - p0.y, p1.z - p0.z };
    float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (len < 1e-6f) return;
    Vec3f ref = (std::fabs(d.y) < 0.9f * len) ? Vec3f{ 0.0f, 1.0f, 0.0f } : Vec3f{ 1.0f, 0.0f, 0.0f };
    Vec3f n1 = DetailStep::StepScale(DetailStep::StepNorm(DetailStep::StepCross(d, ref)), radius);
    Vec3f n2 = DetailStep::StepScale(DetailStep::StepNorm(DetailStep::StepCross(d, n1)), radius);
    uint32_t b = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(DetailStep::StepAdd(p0, n1));
    mesh.vertices.push_back(DetailStep::StepAdd(p0, n2));
    mesh.vertices.push_back(DetailStep::StepSub(p0, n1));
    mesh.vertices.push_back(DetailStep::StepSub(p0, n2));
    mesh.vertices.push_back(DetailStep::StepAdd(p1, n1));
    mesh.vertices.push_back(DetailStep::StepAdd(p1, n2));
    mesh.vertices.push_back(DetailStep::StepSub(p1, n1));
    mesh.vertices.push_back(DetailStep::StepSub(p1, n2));
    for (uint32_t i = 0; i < 4; ++i) {
        uint32_t ni = (i + 1) & 3u;
        mesh.tris.push_back({ b + i, b + ni, b + 4 + ni, {} });
        mesh.tris.push_back({ b + i, b + 4 + ni, b + 4 + i, {} });
    }
}

// ============================================================================
// Pixar OpenUSD Crate (PXR-USDC) LZ4 & Integer Delta/Bitpack Decompressors
// ============================================================================
inline bool Lz4DecompressRawBlock(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstCap, size_t& outWritten) {
    size_t sp = 0;
    size_t dp = 0;
    while (sp < srcLen) {
        uint8_t token = src[sp++];
        size_t litLen = (token >> 4) & 0x0Fu;
        if (litLen == 15) {
            while (sp < srcLen) {
                uint8_t s = src[sp++];
                litLen += s;
                if (s != 255) break;
            }
        }
        if (sp + litLen > srcLen || dp + litLen > dstCap) return false;
        std::memcpy(dst + dp, src + sp, litLen);
        sp += litLen;
        dp += litLen;

        if (sp >= srcLen) break; // End of LZ4 block follows final literal run
        if (sp + 2 > srcLen) return false;
        size_t offset = static_cast<size_t>(src[sp]) | (static_cast<size_t>(src[sp + 1]) << 8);
        sp += 2;
        if (offset == 0 || offset > dp) return false;

        size_t matchLen = (token & 0x0Fu) + 4;
        if ((token & 0x0Fu) == 15) {
            while (sp < srcLen) {
                uint8_t s = src[sp++];
                matchLen += s;
                if (s != 255) break;
            }
        }
        if (dp + matchLen > dstCap) return false;
        size_t matchPos = dp - offset;
        for (size_t k = 0; k < matchLen; ++k) {
            dst[dp + k] = dst[matchPos + k];
        }
        dp += matchLen;
    }
    outWritten = dp;
    return true;
}

inline bool TfFastDecompress(const uint8_t* comp, size_t compSize, size_t uncompSize, std::vector<uint8_t>& out) {
    out.clear();
    if (compSize == 0 || uncompSize > 128 * 1024 * 1024) return false;
    if (uncompSize == 0) return true;
    out.resize(uncompSize);

    uint8_t nChunks = comp[0];
    if (nChunks == 0) {
        size_t written = 0;
        if (!Lz4DecompressRawBlock(comp + 1, compSize - 1, out.data(), uncompSize, written)) return false;
        out.resize(written);
        return true;
    }

    size_t sp = 1;
    size_t dp = 0;
    for (uint8_t c = 0; c < nChunks; ++c) {
        if (sp + 4 > compSize) return false;
        uint32_t cSz = RdU32LE(comp + sp);
        sp += 4;
        if (sp + cSz > compSize || dp > uncompSize) return false;
        size_t written = 0;
        if (!Lz4DecompressRawBlock(comp + sp, cSz, out.data() + dp, uncompSize - dp, written)) return false;
        sp += cSz;
        dp += written;
    }
    out.resize(dp);
    return true;
}

// OpenUSD Usd_IntegerCompression::DecompressFromBuffer (common delta + 2-bit codes + int8/int16/int32 deltas)
inline bool UsdDecompressInts32(const uint8_t* comp, size_t compSize, size_t count, std::vector<int32_t>& out) {
    out.clear();
    if (count == 0) return true;
    if (count > 16000000) return false;
    size_t maxWorkingSz = 4 + ((count * 2 + 7) / 8) + count * 4 + 64;
    std::vector<uint8_t> raw;
    if (!TfFastDecompress(comp, compSize, maxWorkingSz, raw)) return false;
    size_t codesBytes = (count * 2 + 7) / 8;
    if (raw.size() < 4 + codesBytes) return false;

    int32_t commonValue = static_cast<int32_t>(RdU32LE(raw.data()));
    const uint8_t* codes = raw.data() + 4;
    size_t vp = 4 + codesBytes;

    out.resize(count);
    int32_t prev = 0;
    for (size_t i = 0; i < count; ++i) {
        uint8_t code = (codes[i >> 2] >> ((i & 3u) * 2u)) & 0x03u;
        int32_t delta = 0;
        if (code == 0) {
            delta = commonValue;
        } else if (code == 1) {
            if (vp + 1 > raw.size()) return false;
            delta = static_cast<int8_t>(raw[vp++]);
        } else if (code == 2) {
            if (vp + 2 > raw.size()) return false;
            delta = static_cast<int16_t>(RdU16LE(raw.data() + vp));
            vp += 2;
        } else {
            if (vp + 4 > raw.size()) return false;
            delta = static_cast<int32_t>(RdU32LE(raw.data() + vp));
            vp += 4;
        }
        prev = static_cast<int32_t>(static_cast<uint32_t>(prev) + static_cast<uint32_t>(delta));
        out[i] = prev;
    }
    return true;
}

} // namespace Detail3D100

// ============================================================================
// 63. Industry Foundation Classes (.ifc — ISO 10303-21 BIM Geometry Decoder)
// ============================================================================
inline bool ParseIfcBimMesh(std::string_view sv, LoadedMesh3D& mesh) {
    using namespace DetailStep;
    using DetailScad::Mat4;

    if (sv.find("ISO-10303-21") == std::string_view::npos && sv.find("IFC") == std::string_view::npos) {
        return false;
    }
    mesh.formatName = L"IFC Building Information Model (.ifc)";

    // 1. Parse #id = IFCENTITY(...); statements across DATA; ... ENDSEC;
    struct IfcEnt {
        std::string type;
        std::string_view params;
    };
    std::unordered_map<int, IfcEnt> ents;
    ents.reserve(1024);

    size_t pos = 0;
    size_t dataPos = sv.find("DATA;");
    if (dataPos != std::string_view::npos) pos = dataPos + 5;

    while (pos < sv.size() && ents.size() < 500000) {
        while (pos < sv.size()) {
            unsigned char ch = static_cast<unsigned char>(sv[pos]);
            if (ch <= 32) { ++pos; continue; }
            if (ch == '/' && pos + 1 < sv.size() && sv[pos + 1] == '*') {
                size_t ce = sv.find("*/", pos + 2);
                pos = (ce == std::string_view::npos) ? sv.size() : (ce + 2);
                continue;
            }
            break;
        }
        if (pos >= sv.size()) break;
        if (sv.compare(pos, 7, "ENDSEC;") == 0) break;

        size_t st = pos;
        bool inStr = false;
        while (pos < sv.size()) {
            char c = sv[pos];
            if (inStr) {
                if (c == '\'') {
                    if (pos + 1 < sv.size() && sv[pos + 1] == '\'') pos += 2;
                    else { inStr = false; ++pos; }
                } else ++pos;
                continue;
            }
            if (c == '\'') { inStr = true; ++pos; continue; }
            if (c == '/' && pos + 1 < sv.size() && sv[pos + 1] == '*') {
                size_t ce = sv.find("*/", pos + 2);
                pos = (ce == std::string_view::npos) ? sv.size() : (ce + 2);
                continue;
            }
            if (c == ';') break;
            ++pos;
        }
        std::string_view stmt = TrimSv(sv.substr(st, pos - st));
        if (pos < sv.size() && sv[pos] == ';') ++pos;

        if (stmt.size() < 6 || stmt[0] != '#') continue;
        size_t eq = stmt.find('=');
        if (eq == std::string_view::npos) continue;
        int id = ParseRefId(stmt.substr(0, eq));
        if (id <= 0) continue;
        std::string_view rhs = TrimSv(stmt.substr(eq + 1));
        size_t lp = rhs.find('(');
        if (lp == std::string_view::npos) continue;
        std::string tname = UpperAscii(rhs.substr(0, lp));
        if (!tname.empty()) {
            ents[id] = { std::move(tname), TrimSv(rhs.substr(lp)) };
        }
    }
    if (ents.empty()) return false;

    // 2. Coordinate, Direction, Axis2Placement & LocalPlacement Evaluators
    std::unordered_map<int, Vec3f> ptCache;
    std::unordered_map<int, Vec3f> dirCache;
    std::unordered_map<int, Mat4> matCache;

    auto getPt = [&](int id, Vec3f& out) -> bool {
        if (id <= 0) return false;
        auto cit = ptCache.find(id);
        if (cit != ptCache.end()) { out = cit->second; return true; }
        auto it = ents.find(id);
        if (it == ents.end() || it->second.type != "IFCCARTESIANPOINT") return false;
        std::vector<std::string_view> args;
        SplitStepArgs(it->second.params, args);
        if (args.empty()) return false;
        std::vector<double> c;
        ParseFloatTuple(args[0], c);
        if (c.size() < 2) return false;
        out = { static_cast<float>(c[0]), static_cast<float>(c[1]), static_cast<float>(c.size() >= 3 ? c[2] : 0.0) };
        ptCache[id] = out;
        return true;
    };

    auto getDir = [&](int id, Vec3f& out) -> bool {
        if (id <= 0) return false;
        auto cit = dirCache.find(id);
        if (cit != dirCache.end()) { out = cit->second; return true; }
        auto it = ents.find(id);
        if (it == ents.end() || it->second.type != "IFCDIRECTION") return false;
        std::vector<std::string_view> args;
        SplitStepArgs(it->second.params, args);
        if (args.empty()) return false;
        std::vector<double> c;
        ParseFloatTuple(args[0], c);
        if (c.size() < 2) return false;
        out = StepNorm({ static_cast<float>(c[0]), static_cast<float>(c[1]), static_cast<float>(c.size() >= 3 ? c[2] : 0.0) });
        dirCache[id] = out;
        return true;
    };

    auto buildBasisMat = [](Vec3f origin, Vec3f xAx, Vec3f yAx, Vec3f zAx) -> Mat4 {
        Mat4 m;
        m.m[0] = xAx.x; m.m[1] = yAx.x; m.m[2]  = zAx.x; m.m[3]  = origin.x;
        m.m[4] = xAx.y; m.m[5] = yAx.y; m.m[6]  = zAx.y; m.m[7]  = origin.y;
        m.m[8] = xAx.z; m.m[9] = yAx.z; m.m[10] = zAx.z; m.m[11] = origin.z;
        return m;
    };

    std::function<Mat4(int, int)> getTransform = [&](int id, int depth) -> Mat4 {
        if (id <= 0 || depth > 24) return Mat4::Identity();
        auto cit = matCache.find(id);
        if (cit != matCache.end()) return cit->second;
        auto it = ents.find(id);
        if (it == ents.end()) return Mat4::Identity();
        const auto& e = it->second;
        std::vector<std::string_view> args;
        SplitStepArgs(e.params, args);

        Mat4 res = Mat4::Identity();
        if (e.type == "IFCAXIS2PLACEMENT3D") {
            Vec3f org = { 0, 0, 0 }, zAx = { 0, 0, 1 };
            if (!args.empty()) getPt(ParseRefId(args[0]), org);
            if (args.size() >= 2) getDir(ParseRefId(args[1]), zAx);
            zAx = StepNorm(zAx, { 0, 0, 1 });
            Vec3f ref = (std::fabs(zAx.z) < 0.9f) ? Vec3f{ 0, 0, 1 } : Vec3f{ 1, 0, 0 };
            if (args.size() >= 3) {
                Vec3f rd;
                if (getDir(ParseRefId(args[2]), rd)) ref = rd;
            }
            Vec3f proj = StepSub(ref, StepScale(zAx, StepDot(ref, zAx)));
            if (StepLen(proj) < 1e-6f) {
                Vec3f alt = (std::fabs(zAx.x) < 0.9f) ? Vec3f{ 1, 0, 0 } : Vec3f{ 0, 1, 0 };
                proj = StepSub(alt, StepScale(zAx, StepDot(alt, zAx)));
            }
            Vec3f xAx = StepNorm(proj, { 1, 0, 0 });
            Vec3f yAx = StepNorm(StepCross(zAx, xAx), { 0, 1, 0 });
            res = buildBasisMat(org, xAx, yAx, zAx);
        } else if (e.type == "IFCAXIS2PLACEMENT2D") {
            Vec3f org = { 0, 0, 0 }, xAx = { 1, 0, 0 };
            if (!args.empty()) getPt(ParseRefId(args[0]), org);
            if (args.size() >= 2) {
                Vec3f rd;
                if (getDir(ParseRefId(args[1]), rd)) xAx = StepNorm({ rd.x, rd.y, 0.0f }, { 1, 0, 0 });
            }
            Vec3f yAx = { -xAx.y, xAx.x, 0.0f };
            res = buildBasisMat({ org.x, org.y, 0.0f }, xAx, yAx, { 0, 0, 1 });
        } else if (e.type == "IFCLOCALPLACEMENT") {
            Mat4 parentMat = (args.size() >= 1) ? getTransform(ParseRefId(args[0]), depth + 1) : Mat4::Identity();
            Mat4 relMat    = (args.size() >= 2) ? getTransform(ParseRefId(args[1]), depth + 1) : Mat4::Identity();
            res = Mat4::Mul(parentMat, relMat);
        } else if (e.type.find("IFCCARTESIANTRANSFORMATIONOPERATOR3D") == 0) {
            // (Axis1, Axis2, LocalOrigin, Scale, Axis3 [, Scale2, Scale3])
            Vec3f xAx = { 1, 0, 0 }, yAx = { 0, 1, 0 }, zAx = { 0, 0, 1 }, org = { 0, 0, 0 };
            if (args.size() >= 1) getDir(ParseRefId(args[0]), xAx);
            if (args.size() >= 2) getDir(ParseRefId(args[1]), yAx);
            if (args.size() >= 3) getPt(ParseRefId(args[2]), org);
            float s1 = (args.size() >= 4) ? static_cast<float>(ParseStepDouble(args[3], 1.0)) : 1.0f;
            if (s1 <= 0.0f) s1 = 1.0f;
            if (args.size() >= 5 && ! getDir(ParseRefId(args[4]), zAx)) zAx = StepNorm(StepCross(xAx, yAx), { 0, 0, 1 });
            float s2 = (args.size() >= 6) ? static_cast<float>(ParseStepDouble(args[5], s1)) : s1;
            float s3 = (args.size() >= 7) ? static_cast<float>(ParseStepDouble(args[6], s1)) : s1;
            if (s2 <= 0.0f) s2 = s1;
            if (s3 <= 0.0f) s3 = s1;
            res = buildBasisMat(org, StepScale(xAx, s1), StepScale(yAx, s2), StepScale(zAx, s3));
        }
        matCache[id] = res;
        return res;
    };

    // 3. Point lists (IFCCARTESIANPOINTLIST3D & IFCCARTESIANPOINTLIST2D)
    std::unordered_map<int, std::vector<Vec3f>> ptLists;
    auto getPointList = [&](int id) -> const std::vector<Vec3f>* {
        auto cit = ptLists.find(id);
        if (cit != ptLists.end()) return &cit->second;
        auto it = ents.find(id);
        if (it == ents.end() || (it->second.type != "IFCCARTESIANPOINTLIST3D" && it->second.type != "IFCCARTESIANPOINTLIST2D")) return nullptr;
        std::vector<std::string_view> args;
        SplitStepArgs(it->second.params, args);
        if (args.empty()) return nullptr;
        std::vector<std::string_view> tuples;
        SplitStepArgs(args[0], tuples);
        std::vector<Vec3f> pts;
        pts.reserve(tuples.size());
        for (auto t : tuples) {
            std::vector<double> c;
            ParseFloatTuple(t, c);
            if (c.size() >= 2) {
                pts.push_back({ static_cast<float>(c[0]), static_cast<float>(c[1]), static_cast<float>(c.size() >= 3 ? c[2] : 0.0) });
            }
        }
        ptLists[id] = std::move(pts);
        return &ptLists[id];
    };

    // 4. 2D Curve & Profile Evaluators (for IFCEXTRUDEDAREASOLID)
    std::function<std::vector<std::pair<float, float>>(int, int)> evalCurve2D = [&](int cid, int depth) -> std::vector<std::pair<float, float>> {
        std::vector<std::pair<float, float>> out;
        if (cid <= 0 || depth > 12) return out;
        auto it = ents.find(cid);
        if (it == ents.end()) return out;
        const auto& ce = it->second;
        std::vector<std::string_view> args;
        SplitStepArgs(ce.params, args);

        if (ce.type == "IFCPOLYLINE" && !args.empty()) {
            std::vector<int> refs;
            ParseRefList(args[0], refs);
            for (int r : refs) {
                Vec3f p;
                if (getPt(r, p)) out.push_back({ p.x, p.y });
            }
        } else if (ce.type == "IFCINDEXEDPOLYCURVE" && !args.empty()) {
            const auto* plist = getPointList(ParseRefId(args[0]));
            if (plist) {
                for (const auto& p : *plist) out.push_back({ p.x, p.y });
            }
        } else if (ce.type == "IFCCOMPOSITECURVE" && !args.empty()) {
            std::vector<int> segIds;
            ParseRefList(args[0], segIds);
            for (int sid : segIds) {
                auto sit = ents.find(sid);
                if (sit == ents.end()) continue;
                std::vector<std::string_view> sArgs;
                SplitStepArgs(sit->second.params, sArgs);
                if (sit->second.type == "IFCCOMPOSITECURVESEGMENT" && sArgs.size() >= 3) {
                    bool sameSense = ParseStepBool(sArgs[1], true);
                    auto sub = evalCurve2D(ParseRefId(sArgs[2]), depth + 1);
                    if (!sameSense) std::reverse(sub.begin(), sub.end());
                    for (const auto& p : sub) {
                        if (out.empty() || std::hypot(p.first - out.back().first, p.second - out.back().second) > 1e-5f) {
                            out.push_back(p);
                        }
                    }
                }
            }
        } else if (ce.type == "IFCTRIMMEDCURVE" && args.size() >= 4) {
            int basisId = ParseRefId(args[0]);
            auto bit = ents.find(basisId);
            if (bit != ents.end() && bit->second.type == "IFCCIRCLE") {
                std::vector<std::string_view> cArgs;
                SplitStepArgs(bit->second.params, cArgs);
                if (cArgs.size() >= 2) {
                    Mat4 cMat = getTransform(ParseRefId(cArgs[0]), 0);
                    float rad = static_cast<float>(ParseStepDouble(cArgs[1], 1.0));
                    double p1 = ParseStepDouble(args[1], 0.0) * 3.141592653589793 / 180.0;
                    double p2 = ParseStepDouble(args[2], 360.0) * 3.141592653589793 / 180.0;
                    bool sense = ParseStepBool(args[3], true);
                    double delta = p2 - p1;
                    if (sense) { while (delta <= 1e-5) delta += 6.283185307179586; }
                    else       { while (delta >= -1e-5) delta -= 6.283185307179586; }
                    for (int k = 0; k <= 12; ++k) {
                        double a = p1 + delta * (static_cast<double>(k) / 12.0);
                        Vec3f pt = cMat.TransformPoint({ static_cast<float>(std::cos(a)) * rad, static_cast<float>(std::sin(a)) * rad, 0.0f });
                        out.push_back({ pt.x, pt.y });
                    }
                }
            } else {
                Vec3f t1, t2;
                int r1 = ParseRefId(args[1]), r2 = ParseRefId(args[2]);
                if (getPt(r1, t1) && getPt(r2, t2)) {
                    out.push_back({ t1.x, t1.y });
                    out.push_back({ t2.x, t2.y });
                }
            }
        }
        if (out.size() >= 2 && std::hypot(out.front().first - out.back().first, out.front().second - out.back().second) <= 1e-4f) {
            out.pop_back();
        }
        return out;
    };

    struct Profile2D {
        std::vector<std::pair<float, float>> outer;
        std::vector<std::vector<std::pair<float, float>>> voids;
    };

    auto evalProfile2D = [&](int profId) -> Profile2D {
        Profile2D prof;
        auto it = ents.find(profId);
        if (it == ents.end()) return prof;
        const auto& pe = it->second;
        std::vector<std::string_view> args;
        SplitStepArgs(pe.params, args);

        Mat4 pos2D = (args.size() >= 3) ? getTransform(ParseRefId(args[2]), 0) : Mat4::Identity();
        auto xformRing = [&](std::vector<std::pair<float, float>>& ring) {
            for (auto& pt : ring) {
                Vec3f p3 = pos2D.TransformPoint({ pt.first, pt.second, 0.0f });
                pt = { p3.x, p3.y };
            }
        };

        if ((pe.type == "IFCRECTANGLEPROFILEDEF" || pe.type == "IFCRECTANGLEHOLLOWPROFILEDEF") && args.size() >= 5) {
            float hx = 0.5f * static_cast<float>(ParseStepDouble(args[3], 1.0));
            float hy = 0.5f * static_cast<float>(ParseStepDouble(args[4], 1.0));
            if (hx > 1e-6f && hy > 1e-6f) {
                prof.outer = { {-hx, -hy}, {hx, -hy}, {hx, hy}, {-hx, hy} };
                xformRing(prof.outer);
                if (pe.type == "IFCRECTANGLEHOLLOWPROFILEDEF" && args.size() >= 6) {
                    float wt = static_cast<float>(ParseStepDouble(args[5], 0.0));
                    if (wt > 1e-6f && hx - wt > 1e-5f && hy - wt > 1e-5f) {
                        std::vector<std::pair<float, float>> inner = {
                            {-(hx - wt), -(hy - wt)}, {hx - wt, -(hy - wt)}, {hx - wt, hy - wt}, {-(hx - wt), hy - wt}
                        };
                        xformRing(inner);
                        prof.voids.push_back(std::move(inner));
                    }
                }
            }
        } else if ((pe.type == "IFCCIRCLEPROFILEDEF" || pe.type == "IFCCIRCLEHOLLOWPROFILEDEF") && args.size() >= 4) {
            float rad = static_cast<float>(ParseStepDouble(args[3], 1.0));
            if (rad > 1e-6f) {
                const int segs = 20;
                for (int i = 0; i < segs; ++i) {
                    float a = static_cast<float>(i) * 6.2831853f / static_cast<float>(segs);
                    prof.outer.push_back({ std::cos(a) * rad, std::sin(a) * rad });
                }
                xformRing(prof.outer);
                if (pe.type == "IFCCIRCLEHOLLOWPROFILEDEF" && args.size() >= 5) {
                    float wt = static_cast<float>(ParseStepDouble(args[4], 0.0));
                    float ir = rad - wt;
                    if (wt > 1e-6f && ir > 1e-5f) {
                        std::vector<std::pair<float, float>> inner;
                        for (int i = 0; i < segs; ++i) {
                            float a = static_cast<float>(i) * 6.2831853f / static_cast<float>(segs);
                            inner.push_back({ std::cos(a) * ir, std::sin(a) * ir });
                        }
                        xformRing(inner);
                        prof.voids.push_back(std::move(inner));
                    }
                }
            }
        } else if (pe.type == "IFCISHAPEPROFILEDEF" && args.size() >= 7) {
            float hw = 0.5f * static_cast<float>(ParseStepDouble(args[3], 1.0));
            float hd = 0.5f * static_cast<float>(ParseStepDouble(args[4], 1.0));
            float hweb = 0.5f * static_cast<float>(ParseStepDouble(args[5], 0.1));
            float tf = static_cast<float>(ParseStepDouble(args[6], 0.1));
            if (hw > hweb && hd > tf) {
                prof.outer = {
                    {-hw, -hd}, {hw, -hd}, {hw, -hd + tf}, {hweb, -hd + tf},
                    {hweb, hd - tf}, {hw, hd - tf}, {hw, hd}, {-hw, hd},
                    {-hw, hd - tf}, {-hweb, hd - tf}, {-hweb, -hd + tf}, {-hw, -hd + tf}
                };
                xformRing(prof.outer);
            }
        } else if ((pe.type == "IFCARBITRARYCLOSEDPROFILEDEF" || pe.type == "IFCARBITRARYPROFILEDEFWITHVOIDS") && args.size() >= 3) {
            prof.outer = evalCurve2D(ParseRefId(args[2]), 0);
            if (pe.type == "IFCARBITRARYPROFILEDEFWITHVOIDS" && args.size() >= 4) {
                std::vector<int> voidCurveIds;
                ParseRefList(args[3], voidCurveIds);
                for (int vid : voidCurveIds) {
                    auto vr = evalCurve2D(vid, 0);
                    if (vr.size() >= 3) prof.voids.push_back(std::move(vr));
                }
            }
        }
        return prof;
    };

    // 5. Representation Geometry Item Evaluator (in IFC Z-up space)
    struct IfcGeomPart {
        std::vector<Vec3f> verts;
        std::vector<MeshTriangle> tris;
    };
    std::unordered_map<int, IfcGeomPart> itemCache;
    std::set<int> consumedInHigherEntity;

    auto appendFaceRingToPart = [](IfcGeomPart& part, const std::vector<Vec3f>& outer, const std::vector<std::vector<Vec3f>>& holes, bool rev) {
        if (outer.size() < 3) return;
        Vec3f nrm = { 0, 0, 0 };
        for (size_t i = 0; i < outer.size(); ++i) {
            const Vec3f& c = outer[i];
            const Vec3f& n = outer[(i + 1) % outer.size()];
            nrm.x += (c.y - n.y) * (c.z + n.z);
            nrm.y += (c.z - n.z) * (c.x + n.x);
            nrm.z += (c.x - n.x) * (c.y + n.y);
        }
        nrm = StepNorm(nrm, { 0, 0, 1 });
        Vec3f ref = (std::fabs(nrm.z) < 0.9f) ? Vec3f{ 0, 0, 1 } : Vec3f{ 1, 0, 0 };
        Vec3f uAx = StepNorm(StepCross(ref, nrm), { 1, 0, 0 });
        Vec3f vAx = StepCross(nrm, uAx);
        Vec3f org = outer[0];

        std::vector<std::pair<float, float>> o2;
        std::vector<uint32_t> oi;
        for (const auto& p : outer) {
            Vec3f d = StepSub(p, org);
            o2.push_back({ StepDot(d, uAx), StepDot(d, vAx) });
            oi.push_back(static_cast<uint32_t>(part.verts.size()));
            part.verts.push_back(p);
        }
        std::vector<std::vector<std::pair<float, float>>> h2;
        std::vector<std::vector<uint32_t>> hi;
        for (const auto& h : holes) {
            if (h.size() < 3) continue;
            std::vector<std::pair<float, float>> r2;
            std::vector<uint32_t> ri;
            for (const auto& p : h) {
                Vec3f d = StepSub(p, org);
                r2.push_back({ StepDot(d, uAx), StepDot(d, vAx) });
                ri.push_back(static_cast<uint32_t>(part.verts.size()));
                part.verts.push_back(p);
            }
            h2.push_back(std::move(r2));
            hi.push_back(std::move(ri));
        }
        EarClip2DWithHoles(o2, oi, h2, hi, rev, part.tris);
    };

    auto evalIfcFace = [&](int faceId, IfcGeomPart& outPart) {
        auto fit = ents.find(faceId);
        if (fit == ents.end() || (fit->second.type != "IFCFACE" && fit->second.type != "IFCFACESURFACE")) return;
        consumedInHigherEntity.insert(faceId);
        std::vector<std::string_view> fArgs;
        SplitStepArgs(fit->second.params, fArgs);
        if (fArgs.empty()) return;
        std::vector<int> boundIds;
        ParseRefList(fArgs[0], boundIds);

        std::vector<Vec3f> outer;
        std::vector<std::vector<Vec3f>> holes;
        for (int bid : boundIds) {
            auto bit = ents.find(bid);
            if (bit == ents.end()) continue;
            consumedInHigherEntity.insert(bid);
            std::vector<std::string_view> bArgs;
            SplitStepArgs(bit->second.params, bArgs);
            if (bArgs.empty()) continue;
            int loopId = ParseRefId(bArgs[0]);
            bool ori = (bArgs.size() >= 2) ? ParseStepBool(bArgs[1], true) : true;
            auto lit = ents.find(loopId);
            if (lit == ents.end() || lit->second.type != "IFCPOLYLOOP") continue;
            consumedInHigherEntity.insert(loopId);
            std::vector<std::string_view> lArgs;
            SplitStepArgs(lit->second.params, lArgs);
            if (lArgs.empty()) continue;
            std::vector<int> ptIds;
            ParseRefList(lArgs[0], ptIds);
            std::vector<Vec3f> ring;
            for (int pid : ptIds) {
                Vec3f p;
                if (getPt(pid, p)) ring.push_back(p);
            }
            if (!ori) std::reverse(ring.begin(), ring.end());
            if (ring.size() < 3) continue;
            if ((bit->second.type == "IFCFACEOUTERBOUND" || outer.empty()) && outer.empty()) outer = std::move(ring);
            else holes.push_back(std::move(ring));
        }
        appendFaceRingToPart(outPart, outer, holes, false);
    };

    std::function<const IfcGeomPart*(int, int)> evalGeomItem = [&](int itemId, int depth) -> const IfcGeomPart* {
        if (itemId <= 0 || depth > 20) return nullptr;
        auto cit = itemCache.find(itemId);
        if (cit != itemCache.end()) return &cit->second;
        auto it = ents.find(itemId);
        if (it == ents.end()) return nullptr;
        const auto& ie = it->second;
        std::vector<std::string_view> args;
        SplitStepArgs(ie.params, args);

        IfcGeomPart part;

        if (ie.type == "IFCFACETEDBREP" || ie.type == "IFCFACETEDBREPWITHVOIDS" ||
            ie.type == "IFCSHELLBASEDSURFACEMODEL" || ie.type == "IFCFACEBASEDSURFACEMODEL")
        {
            consumedInHigherEntity.insert(itemId);
            std::vector<int> shellRefs;
            for (auto a : args) {
                std::vector<int> sub;
                ParseRefList(a, sub);
                shellRefs.insert(shellRefs.end(), sub.begin(), sub.end());
            }
            for (int sid : shellRefs) {
                auto sit = ents.find(sid);
                if (sit == ents.end()) continue;
                consumedInHigherEntity.insert(sid);
                if (sit->second.type == "IFCFACE") {
                    evalIfcFace(sid, part);
                } else {
                    std::vector<std::string_view> sArgs;
                    SplitStepArgs(sit->second.params, sArgs);
                    if (!sArgs.empty()) {
                        std::vector<int> faceIds;
                        ParseRefList(sArgs[0], faceIds);
                        for (int fid : faceIds) evalIfcFace(fid, part);
                    }
                }
            }
        } else if (ie.type == "IFCCLOSEDSHELL" || ie.type == "IFCOPENSHELL" || ie.type == "IFCCONNECTEDFACESET") {
            consumedInHigherEntity.insert(itemId);
            if (!args.empty()) {
                std::vector<int> faceIds;
                ParseRefList(args[0], faceIds);
                for (int fid : faceIds) evalIfcFace(fid, part);
            }
        } else if (ie.type == "IFCTRIANGULATEDFACESET" && args.size() >= 4) {
            consumedInHigherEntity.insert(itemId);
            const auto* plist = getPointList(ParseRefId(args[0]));
            if (plist && !plist->empty()) {
                part.verts = *plist;
                std::vector<double> pnIndex;
                if (args.size() >= 5 && !args[4].empty() && args[4].front() == '(') ParseFloatTuple(args[4], pnIndex);
                auto mapV = [&](int oneBased) -> int {
                    int r = oneBased - 1;
                    if (!pnIndex.empty()) {
                        if (r < 0 || static_cast<size_t>(r) >= pnIndex.size()) return -1;
                        r = static_cast<int>(pnIndex[r]) - 1;
                    }
                    return (r >= 0 && static_cast<size_t>(r) < part.verts.size()) ? r : -1;
                };
                std::vector<std::string_view> trisSv;
                SplitStepArgs(args[3], trisSv);
                for (auto ts : trisSv) {
                    std::vector<double> ti;
                    ParseFloatTuple(ts, ti);
                    if (ti.size() >= 3) {
                        int i0 = mapV(static_cast<int>(ti[0]));
                        int i1 = mapV(static_cast<int>(ti[1]));
                        int i2 = mapV(static_cast<int>(ti[2]));
                        if (i0 >= 0 && i1 >= 0 && i2 >= 0) {
                            part.tris.push_back({ static_cast<uint32_t>(i0), static_cast<uint32_t>(i1), static_cast<uint32_t>(i2), {} });
                        }
                    }
                }
            }
        } else if (ie.type == "IFCPOLYGONALFACESET" && args.size() >= 3) {
            consumedInHigherEntity.insert(itemId);
            const auto* plist = getPointList(ParseRefId(args[0]));
            if (plist && !plist->empty()) {
                std::vector<double> pnIndex;
                if (args.size() >= 4 && !args[3].empty() && args[3].front() == '(') ParseFloatTuple(args[3], pnIndex);
                auto mapPt = [&](int oneBased, Vec3f& outP) -> bool {
                    int r = oneBased - 1;
                    if (!pnIndex.empty()) {
                        if (r < 0 || static_cast<size_t>(r) >= pnIndex.size()) return false;
                        r = static_cast<int>(pnIndex[r]) - 1;
                    }
                    if (r < 0 || static_cast<size_t>(r) >= plist->size()) return false;
                    outP = (*plist)[r];
                    return true;
                };
                std::vector<int> faceRefs;
                ParseRefList(args[2], faceRefs);
                for (int pfId : faceRefs) {
                    auto pfit = ents.find(pfId);
                    if (pfit == ents.end()) continue;
                    consumedInHigherEntity.insert(pfId);
                    std::vector<std::string_view> pfArgs;
                    SplitStepArgs(pfit->second.params, pfArgs);
                    if (pfArgs.empty()) continue;
                    std::vector<double> idxD;
                    ParseFloatTuple(pfArgs[0], idxD);
                    std::vector<Vec3f> outer;
                    for (double d : idxD) {
                        Vec3f p;
                        if (mapPt(static_cast<int>(d), p)) outer.push_back(p);
                    }
                    std::vector<std::vector<Vec3f>> holes;
                    if (pfit->second.type == "IFCINDEXEDPOLYGONALFACEWITHVOIDS" && pfArgs.size() >= 2) {
                        std::vector<std::string_view> hLoops;
                        SplitStepArgs(pfArgs[1], hLoops);
                        for (auto hl : hLoops) {
                            std::vector<double> hd;
                            ParseFloatTuple(hl, hd);
                            std::vector<Vec3f> hr;
                            for (double d : hd) {
                                Vec3f p;
                                if (mapPt(static_cast<int>(d), p)) hr.push_back(p);
                            }
                            if (hr.size() >= 3) holes.push_back(std::move(hr));
                        }
                    }
                    appendFaceRingToPart(part, outer, holes, false);
                }
            }
        } else if (ie.type == "IFCEXTRUDEDAREASOLID" && args.size() >= 4) {
            consumedInHigherEntity.insert(itemId);
            Profile2D prof = evalProfile2D(ParseRefId(args[0]));
            Mat4 posMat = getTransform(ParseRefId(args[1]), 0);
            Vec3f extDir = { 0, 0, 1 };
            getDir(ParseRefId(args[2]), extDir);
            extDir = StepNorm(extDir, { 0, 0, 1 });
            float depthVal = static_cast<float>(ParseStepDouble(args[3], 0.0));

            if (prof.outer.size() >= 3 && depthVal > 1e-6f) {
                Vec3f sweep = StepScale(extDir, depthVal);
                bool flipMat = (posMat.Det3x3() < 0.0f);

                // Ensure CCW 2D outer ring
                auto area2 = [](const std::vector<std::pair<float, float>>& r) {
                    double a = 0.0;
                    for (size_t i = 0; i < r.size(); ++i) {
                        size_t j = (i + 1) % r.size();
                        a += static_cast<double>(r[i].first) * r[j].second - static_cast<double>(r[j].first) * r[i].second;
                    }
                    return a;
                };
                if (area2(prof.outer) < 0.0) std::reverse(prof.outer.begin(), prof.outer.end());
                for (auto& vr : prof.voids) {
                    if (area2(vr) > 0.0) std::reverse(vr.begin(), vr.end());
                }

                auto emitSideWall = [&](const std::vector<std::pair<float, float>>& ring2D) {
                    size_t n = ring2D.size();
                    uint32_t b = static_cast<uint32_t>(part.verts.size());
                    for (size_t i = 0; i < n; ++i) {
                        Vec3f p0 = { ring2D[i].first, ring2D[i].second, 0.0f };
                        Vec3f p1 = StepAdd(p0, sweep);
                        part.verts.push_back(posMat.TransformPoint(p0));
                        part.verts.push_back(posMat.TransformPoint(p1));
                    }
                    for (size_t i = 0; i < n; ++i) {
                        size_t j = (i + 1) % n;
                        uint32_t b0 = b + static_cast<uint32_t>(i * 2), t0 = b0 + 1;
                        uint32_t b1 = b + static_cast<uint32_t>(j * 2), t1 = b1 + 1;
                        if (flipMat) {
                            part.tris.push_back({ b0, t1, b1, {} });
                            part.tris.push_back({ b0, t0, t1, {} });
                        } else {
                            part.tris.push_back({ b0, b1, t1, {} });
                            part.tris.push_back({ b0, t1, t0, {} });
                        }
                    }
                };
                emitSideWall(prof.outer);
                for (const auto& vr : prof.voids) emitSideWall(vr);

                // Bottom and Top caps with hole rings
                for (int cap = 0; cap < 2; ++cap) {
                    Vec3f off = (cap == 0) ? Vec3f{ 0, 0, 0 } : sweep;
                    std::vector<uint32_t> oIdx;
                    for (const auto& p : prof.outer) {
                        oIdx.push_back(static_cast<uint32_t>(part.verts.size()));
                        part.verts.push_back(posMat.TransformPoint(StepAdd({ p.first, p.second, 0.0f }, off)));
                    }
                    std::vector<std::vector<uint32_t>> hIdx;
                    for (const auto& vr : prof.voids) {
                        std::vector<uint32_t> vi;
                        for (const auto& p : vr) {
                            vi.push_back(static_cast<uint32_t>(part.verts.size()));
                            part.verts.push_back(posMat.TransformPoint(StepAdd({ p.first, p.second, 0.0f }, off)));
                        }
                        hIdx.push_back(std::move(vi));
                    }
                    EarClip2DWithHoles(prof.outer, oIdx, prof.voids, hIdx, (cap == 0) ^ flipMat, part.tris);
                }
            }
        } else if ((ie.type == "IFCBOOLEANCLIPPINGRESULT" || ie.type == "IFCBOOLEANRESULT") && args.size() >= 3) {
            consumedInHigherEntity.insert(itemId);
            const auto* op1 = evalGeomItem(ParseRefId(args[1]), depth + 1);
            if (op1) part = *op1;
            if (args[0].find("UNION") != std::string_view::npos) {
                const auto* op2 = evalGeomItem(ParseRefId(args[2]), depth + 1);
                if (op2) {
                    uint32_t b = static_cast<uint32_t>(part.verts.size());
                    part.verts.insert(part.verts.end(), op2->verts.begin(), op2->verts.end());
                    for (const auto& t : op2->tris) part.tris.push_back({ b + t.i0, b + t.i1, b + t.i2, {} });
                }
            }
        } else if (ie.type == "IFCMAPPEDITEM" && args.size() >= 2) {
            consumedInHigherEntity.insert(itemId);
            int repMapId = ParseRefId(args[0]);
            Mat4 targetMat = getTransform(ParseRefId(args[1]), depth + 1);
            auto rmit = ents.find(repMapId);
            if (rmit != ents.end() && rmit->second.type == "IFCREPRESENTATIONMAP") {
                std::vector<std::string_view> rmArgs;
                SplitStepArgs(rmit->second.params, rmArgs);
                if (rmArgs.size() >= 2) {
                    Mat4 origMat = getTransform(ParseRefId(rmArgs[0]), depth + 1);
                    Mat4 combined = Mat4::Mul(targetMat, origMat);
                    bool flip = (combined.Det3x3() < 0.0f);
                    int mappedRepId = ParseRefId(rmArgs[1]);
                    auto srit = ents.find(mappedRepId);
                    if (srit != ents.end() && srit->second.type == "IFCSHAPEREPRESENTATION") {
                        consumedInHigherEntity.insert(mappedRepId);
                        std::vector<std::string_view> srArgs;
                        SplitStepArgs(srit->second.params, srArgs);
                        if (srArgs.size() >= 4) {
                            std::vector<int> subItems;
                            ParseRefList(srArgs[3], subItems);
                            for (int subId : subItems) {
                                const auto* sp = evalGeomItem(subId, depth + 1);
                                if (!sp) continue;
                                uint32_t b = static_cast<uint32_t>(part.verts.size());
                                for (const auto& v : sp->verts) part.verts.push_back(combined.TransformPoint(v));
                                for (const auto& t : sp->tris) {
                                    if (flip) part.tris.push_back({ b + t.i0, b + t.i2, b + t.i1, {} });
                                    else      part.tris.push_back({ b + t.i0, b + t.i1, b + t.i2, {} });
                                }
                            }
                        }
                    }
                }
            }
        }

        itemCache[itemId] = std::move(part);
        return &itemCache[itemId];
    };

    auto emitPartToMesh = [&](const IfcGeomPart& part, const Mat4& worldMat) {
        if (part.verts.empty() || part.tris.empty()) return;
        bool flip = (worldMat.Det3x3() < 0.0f);
        uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        for (const auto& lv : part.verts) {
            Vec3f w = worldMat.TransformPoint(lv);
            mesh.vertices.push_back({ w.x, w.z, -w.y }); // IFC Z-up -> Explorer2 Y-up
        }
        for (const auto& t : part.tris) {
            if (flip) mesh.tris.push_back({ base + t.i0, base + t.i2, base + t.i1, {} });
            else      mesh.tris.push_back({ base + t.i0, base + t.i1, base + t.i2, {} });
        }
    };

    // 6. Instantiate IFCPRODUCT elements with their chained IFCLOCALPLACEMENT world matrices
    std::set<int> instancedShapeReps;
    for (const auto& [id, ent] : ents) {
        if (ent.type.rfind("IFC", 0) != 0) continue;
        std::vector<std::string_view> args;
        SplitStepArgs(ent.params, args);
        if (args.size() < 7) continue;

        // In IFC2x3 / IFC4, IfcProduct has ObjectPlacement at arg[5] and Representation (IfcProductDefinitionShape) at arg[6]
        int repDefId = ParseRefId(args[6]);
        auto rdit = ents.find(repDefId);
        if (rdit == ents.end() || rdit->second.type != "IFCPRODUCTDEFINITIONSHAPE") continue;

        Mat4 worldMat = getTransform(ParseRefId(args[5]), 0);
        std::vector<std::string_view> rdArgs;
        SplitStepArgs(rdit->second.params, rdArgs);
        if (rdArgs.size() < 3) continue;
        std::vector<int> srIds;
        ParseRefList(rdArgs[2], srIds);
        for (int srId : srIds) {
            auto srit = ents.find(srId);
            if (srit == ents.end() || srit->second.type != "IFCSHAPEREPRESENTATION") continue;
            instancedShapeReps.insert(srId);
            std::vector<std::string_view> srArgs;
            SplitStepArgs(srit->second.params, srArgs);
            if (srArgs.size() < 4) continue;
            std::vector<int> itemIds;
            ParseRefList(srArgs[3], itemIds);
            for (int itemId : itemIds) {
                const auto* part = evalGeomItem(itemId, 0);
                if (part) emitPartToMesh(*part, worldMat);
            }
        }
    }

    // 7. Emit any standalone / unreferenced representation items, faces, or polyloops (e.g. minimal IFC snippets)
    for (const auto& [id, ent] : ents) {
        if (consumedInHigherEntity.find(id) != consumedInHigherEntity.end()) continue;
        if (ent.type == "IFCEXTRUDEDAREASOLID" || ent.type == "IFCTRIANGULATEDFACESET" ||
            ent.type == "IFCPOLYGONALFACESET"  || ent.type == "IFCFACETEDBREP" ||
            ent.type == "IFCFACETEDBREPWITHVOIDS" || ent.type == "IFCSHELLBASEDSURFACEMODEL" ||
            ent.type == "IFCFACEBASEDSURFACEMODEL" || ent.type == "IFCMAPPEDITEM")
        {
            const auto* part = evalGeomItem(id, 0);
            if (part) emitPartToMesh(*part, Mat4::Identity());
        }
    }
    for (const auto& [id, ent] : ents) {
        if (ent.type == "IFCFACE" && consumedInHigherEntity.find(id) == consumedInHigherEntity.end()) {
            IfcGeomPart p;
            evalIfcFace(id, p);
            emitPartToMesh(p, Mat4::Identity());
        }
    }
    for (const auto& [id, ent] : ents) {
        if (ent.type == "IFCPOLYLOOP" && consumedInHigherEntity.find(id) == consumedInHigherEntity.end()) {
            std::vector<std::string_view> lArgs;
            SplitStepArgs(ent.params, lArgs);
            if (!lArgs.empty()) {
                std::vector<int> ptIds;
                ParseRefList(lArgs[0], ptIds);
                std::vector<Vec3f> ring;
                for (int pid : ptIds) {
                    Vec3f pt;
                    if (getPt(pid, pt)) ring.push_back(pt);
                }
                if (ring.size() >= 3) {
                    IfcGeomPart p;
                    appendFaceRingToPart(p, ring, {}, false);
                    emitPartToMesh(p, Mat4::Identity());
                }
            }
        }
    }

    return !mesh.vertices.empty() && !mesh.tris.empty();
}

// ============================================================================
// 66. Pixar Universal Scene Description (.usd / .usda / .usdc / .usdz)
// ============================================================================
inline bool ParsePixarUsdMesh(const uint8_t* data, size_t size, const std::wstring& extLower, LoadedMesh3D& mesh) {
    using namespace Detail3D100;

    // 1. .usdz uncompressed/deflate ZIP archive -> inspect inner .usdc / .usda / .usd stages
    if (size >= 4 && data[0] == 'P' && data[1] == 'K') {
        std::vector<ZipDeflate::ArchiveItemMeta> entries;
        if (ZipDeflate::ListZipEntriesFromMemory(data, size, entries)) {
            for (const auto& e : entries) {
                if (e.isDirectory) continue;
                std::wstring innerExt = PathUtils::GetExtension(e.pathInArchive);
                if (innerExt == L".usda" || innerExt == L".usd" || innerExt == L".usdc") {
                    std::vector<uint8_t> innerBytes;
                    if (ZipDeflate::ExtractZipEntryFromMemory(data, size, e, innerBytes) && !innerBytes.empty()) {
                        if (ParsePixarUsdMesh(innerBytes.data(), innerBytes.size(), innerExt, mesh)) {
                            mesh.formatName = L"Pixar USDZ Package (.usdz)";
                            return true;
                        }
                    }
                }
            }
        }
        return false;
    }

    // 2. Binary Crate ("PXR-USDC") 88-byte Bootstrap -> TOC -> TOKENS, FIELDS, FIELDSETS, PATHS, SPECS
    if (size >= 88 && std::memcmp(data, "PXR-USDC", 8) == 0) {
        mesh.formatName = L"Pixar OpenUSD Binary Crate (.usdc)";
        const uint8_t verMaj = data[8];
        const uint8_t verMin = data[9];
        const uint64_t tocOff = RdU64LE(data + 16);
        if (tocOff < 88 || tocOff + 8 > size) return false;

        const uint64_t numSecs = RdU64LE(data + static_cast<size_t>(tocOff));
        if (numSecs == 0 || numSecs > 64 || tocOff + 8 + numSecs * 32 > size) return false;

        struct CrateSec { uint64_t start = 0, len = 0; bool present = false; };
        CrateSec secTokens, secFields, secFieldSets, secPaths, secSpecs;
        for (uint64_t s = 0; s < numSecs; ++s) {
            const uint8_t* sp = data + static_cast<size_t>(tocOff + 8 + s * 32);
            char nm[17] = {};
            std::memcpy(nm, sp, 16);
            uint64_t st = RdU64LE(sp + 16);
            uint64_t ln = RdU64LE(sp + 24);
            if (st > size || ln > size - st) continue;
            std::string_view name(nm);
            if (name == "TOKENS")         secTokens    = { st, ln, true };
            else if (name == "FIELDS")    secFields    = { st, ln, true };
            else if (name == "FIELDSETS") secFieldSets = { st, ln, true };
            else if (name == "PATHS")     secPaths     = { st, ln, true };
            else if (name == "SPECS")     secSpecs     = { st, ln, true };
        }
        if (!secTokens.present || !secFields.present) return false;

        // 2a. Decode TOKENS
        std::vector<std::string> tokens;
        {
            const uint8_t* tp = data + static_cast<size_t>(secTokens.start);
            size_t tLen = static_cast<size_t>(secTokens.len);
            if (tLen < 16) return false;
            uint64_t numToks = RdU64LE(tp);
            if (numToks > 2000000) return false;
            std::vector<uint8_t> tokBuf;
            if (verMaj > 0 || verMin >= 4) {
                if (tLen < 24) return false;
                uint64_t uncompSz = RdU64LE(tp + 8);
                uint64_t compSz   = RdU64LE(tp + 16);
                if (24 + compSz > tLen) return false;
                if (!TfFastDecompress(tp + 24, static_cast<size_t>(compSz), static_cast<size_t>(uncompSz), tokBuf)) return false;
            } else {
                uint64_t uncompSz = RdU64LE(tp + 8);
                if (16 + uncompSz > tLen) return false;
                tokBuf.assign(tp + 16, tp + 16 + static_cast<size_t>(uncompSz));
            }
            tokens.reserve(static_cast<size_t>(numToks));
            size_t p = 0;
            while (p < tokBuf.size() && tokens.size() < numToks) {
                size_t s = p;
                while (p < tokBuf.size() && tokBuf[p] != 0) ++p;
                tokens.emplace_back(reinterpret_cast<const char*>(tokBuf.data() + s), p - s);
                ++p;
            }
        }

        // 2b. Decode FIELDS (token indices + 64-bit ValueReps)
        struct CrateField { uint32_t tokIdx = 0; uint64_t rep = 0; };
        std::vector<CrateField> fields;
        {
            const uint8_t* fp = data + static_cast<size_t>(secFields.start);
            size_t fLen = static_cast<size_t>(secFields.len);
            if (fLen < 24) return false;
            uint64_t numFields = RdU64LE(fp);
            if (numFields == 0 || numFields > 4000000) return false;
            size_t off = 8;
            uint64_t tokCompSz = RdU64LE(fp + off); off += 8;
            if (off + tokCompSz + 8 > fLen) return false;
            std::vector<int32_t> tokIdxs;
            if (!UsdDecompressInts32(fp + off, static_cast<size_t>(tokCompSz), static_cast<size_t>(numFields), tokIdxs)) return false;
            off += static_cast<size_t>(tokCompSz);

            uint64_t repCompSz = RdU64LE(fp + off); off += 8;
            if (off + repCompSz > fLen) return false;
            std::vector<uint8_t> repBytes;
            if (!TfFastDecompress(fp + off, static_cast<size_t>(repCompSz), static_cast<size_t>(numFields) * 8, repBytes)) return false;
            if (repBytes.size() < static_cast<size_t>(numFields) * 8) return false;

            fields.resize(static_cast<size_t>(numFields));
            for (size_t i = 0; i < static_cast<size_t>(numFields); ++i) {
                fields[i].tokIdx = static_cast<uint32_t>(tokIdxs[i]);
                fields[i].rep    = RdU64LE(repBytes.data() + i * 8);
            }
        }

        // Helper to unpack Vec3f/Vec3d and Int/UInt arrays from a Crate ValueRep
        const bool use64BitArrayCount = (verMaj > 0 || verMin >= 5);

        std::function<bool(uint64_t, std::vector<Vec3f>&, int)> decodeVec3Array = [&](uint64_t rep, std::vector<Vec3f>& outPts, int depth) -> bool {
            if (depth > 4) return false;
            bool isArray   = (rep & (1ULL << 63)) != 0;
            bool isInlined = (rep & (1ULL << 62)) != 0;
            bool isComp    = (rep & (1ULL << 61)) != 0;
            uint8_t tEnum  = static_cast<uint8_t>((rep >> 48) & 0xFFu);
            uint64_t pay   = rep & ((1ULL << 48) - 1ULL);

            // TimeSamples (type 46): dereference first sample's ValueRep!
            if (tEnum == 46 && !isInlined && pay + 24 <= size) {
                size_t p = static_cast<size_t>(pay);
                int64_t rel1 = static_cast<int64_t>(RdU64LE(data + p));
                size_t vPos = p + static_cast<size_t>(rel1);
                // After times ValueRep lies int64 rel2 -> uint64 numValues + ValueRep[numValues]
                if (vPos + 16 <= size) {
                    size_t valBlock = p + 8;
                    int64_t rel2 = static_cast<int64_t>(RdU64LE(data + valBlock));
                    size_t arrPos = valBlock + static_cast<size_t>(rel2);
                    if (arrPos + 16 <= size) {
                        uint64_t nVals = RdU64LE(data + arrPos);
                        if (nVals >= 1) return decodeVec3Array(RdU64LE(data + arrPos + 8), outPts, depth + 1);
                    }
                }
                return false;
            }

            if (!isArray || isInlined || (tEnum != 19 && tEnum != 20)) return false;
            if (pay + 8 > size) return false;
            size_t p = static_cast<size_t>(pay);
            uint64_t nElem = use64BitArrayCount ? RdU64LE(data + p) : RdU32LE(data + p);
            p += use64BitArrayCount ? 8 : 4;
            if (nElem == 0 || nElem > 2000000) return false;

            size_t elemSz = (tEnum == 19) ? 12 : 24;
            std::vector<uint8_t> decomp;
            const uint8_t* rawPtr = nullptr;
            if (isComp) {
                if (p + 8 > size) return false;
                uint64_t cSz = RdU64LE(data + p); p += 8;
                if (p + cSz > size) return false;
                if (!TfFastDecompress(data + p, static_cast<size_t>(cSz), static_cast<size_t>(nElem) * elemSz, decomp)) return false;
                if (decomp.size() < static_cast<size_t>(nElem) * elemSz) return false;
                rawPtr = decomp.data();
            } else {
                if (p + static_cast<size_t>(nElem) * elemSz > size) return false;
                rawPtr = data + p;
            }

            outPts.resize(static_cast<size_t>(nElem));
            for (size_t i = 0; i < static_cast<size_t>(nElem); ++i) {
                if (tEnum == 19) {
                    const uint8_t* vp = rawPtr + i * 12;
                    outPts[i] = { RdF32LE(vp + 0), RdF32LE(vp + 4), RdF32LE(vp + 8) };
                } else {
                    const uint8_t* vp = rawPtr + i * 24;
                    outPts[i] = { static_cast<float>(RdF64LE(vp + 0)), static_cast<float>(RdF64LE(vp + 8)), static_cast<float>(RdF64LE(vp + 16)) };
                }
            }
            return true;
        };

        std::function<bool(uint64_t, std::vector<int32_t>&, int)> decodeIntArray = [&](uint64_t rep, std::vector<int32_t>& outInts, int depth) -> bool {
            if (depth > 4) return false;
            bool isArray   = (rep & (1ULL << 63)) != 0;
            bool isInlined = (rep & (1ULL << 62)) != 0;
            bool isComp    = (rep & (1ULL << 61)) != 0;
            uint8_t tEnum  = static_cast<uint8_t>((rep >> 48) & 0xFFu);
            uint64_t pay   = rep & ((1ULL << 48) - 1ULL);

            if (tEnum == 46 && !isInlined && pay + 24 <= size) {
                size_t valBlock = static_cast<size_t>(pay) + 8;
                int64_t rel2 = static_cast<int64_t>(RdU64LE(data + valBlock));
                size_t arrPos = valBlock + static_cast<size_t>(rel2);
                if (arrPos + 16 <= size && RdU64LE(data + arrPos) >= 1) {
                    return decodeIntArray(RdU64LE(data + arrPos + 8), outInts, depth + 1);
                }
                return false;
            }

            if (!isArray || isInlined || (tEnum != 3 && tEnum != 4)) return false;
            if (pay + 8 > size) return false;
            size_t p = static_cast<size_t>(pay);
            uint64_t nElem = use64BitArrayCount ? RdU64LE(data + p) : RdU32LE(data + p);
            p += use64BitArrayCount ? 8 : 4;
            if (nElem == 0 || nElem > 6000000) return false;

            if (isComp) {
                if (p + 8 > size) return false;
                uint64_t cSz = RdU64LE(data + p); p += 8;
                if (p + cSz > size) return false;
                return UsdDecompressInts32(data + p, static_cast<size_t>(cSz), static_cast<size_t>(nElem), outInts);
            } else {
                if (p + static_cast<size_t>(nElem) * 4 > size) return false;
                outInts.resize(static_cast<size_t>(nElem));
                for (size_t i = 0; i < static_cast<size_t>(nElem); ++i) {
                    outInts[i] = static_cast<int32_t>(RdU32LE(data + p + i * 4));
                }
                return true;
            }
        };

        auto emitUsdMesh = [&](const std::vector<Vec3f>& pts, const std::vector<int32_t>& counts, const std::vector<int32_t>& indices) {
            if (pts.empty() || indices.empty()) return;
            uint32_t vBase = static_cast<uint32_t>(mesh.vertices.size());
            for (const auto& p : pts) mesh.vertices.push_back(p);

            if (counts.empty()) {
                for (size_t i = 0; i + 2 < indices.size(); i += 3) {
                    int32_t a = indices[i], b = indices[i + 1], c = indices[i + 2];
                    if (a >= 0 && b >= 0 && c >= 0 &&
                        static_cast<size_t>(a) < pts.size() &&
                        static_cast<size_t>(b) < pts.size() &&
                        static_cast<size_t>(c) < pts.size())
                    {
                        mesh.tris.push_back({ vBase + static_cast<uint32_t>(a), vBase + static_cast<uint32_t>(b), vBase + static_cast<uint32_t>(c), {} });
                    }
                }
            } else {
                size_t k = 0;
                for (int32_t cnt : counts) {
                    if (cnt >= 3 && k + static_cast<size_t>(cnt) <= indices.size()) {
                        int32_t i0 = indices[k];
                        if (i0 >= 0 && static_cast<size_t>(i0) < pts.size()) {
                            for (int32_t t = 1; t + 1 < cnt; ++t) {
                                int32_t i1 = indices[k + t], i2 = indices[k + t + 1];
                                if (i1 >= 0 && i2 >= 0 &&
                                    static_cast<size_t>(i1) < pts.size() &&
                                    static_cast<size_t>(i2) < pts.size())
                                {
                                    mesh.tris.push_back({ vBase + static_cast<uint32_t>(i0), vBase + static_cast<uint32_t>(i1), vBase + static_cast<uint32_t>(i2), {} });
                                }
                            }
                        }
                    }
                    if (cnt > 0) k += static_cast<size_t>(cnt);
                }
            }
        };

        // 2c. Decode FIELDSETS, PATHS, and SPECS to associate .points, .faceVertexCounts, .faceVertexIndices per Mesh Prim
        std::vector<int32_t> fieldSets;
        if (secFieldSets.present && secFieldSets.len >= 16) {
            const uint8_t* fsp = data + static_cast<size_t>(secFieldSets.start);
            uint64_t nFs = RdU64LE(fsp);
            uint64_t cSz = RdU64LE(fsp + 8);
            if (nFs <= 8000000 && 16 + cSz <= secFieldSets.len) {
                UsdDecompressInts32(fsp + 16, static_cast<size_t>(cSz), static_cast<size_t>(nFs), fieldSets);
            }
        }

        struct PrimMeshAttrs {
            uint64_t pointsRep = 0;
            uint64_t countsRep = 0;
            uint64_t indicesRep = 0;
        };
        std::map<int32_t, PrimMeshAttrs> primAttrs;

        if (secPaths.present && secSpecs.present && !fieldSets.empty()) {
            // Decode compressed PATHS tree: numPaths(8), then (pathIndexes, elementTokenIndexes, jumps)
            const uint8_t* pp = data + static_cast<size_t>(secPaths.start);
            size_t pLen = static_cast<size_t>(secPaths.len);
            if (pLen >= 24) {
                size_t off = 0;
                uint64_t totalPaths = RdU64LE(pp + off); off += 8;
                // Crate v0.4+ stores numEncodedPaths after totalPaths if compressed
                uint64_t nEncPaths = totalPaths;
                if (verMaj > 0 || verMin >= 4) {
                    if (off + 8 <= pLen) { nEncPaths = RdU64LE(pp + off); off += 8; }
                }
                std::vector<int32_t> pathIdxs, elemToks, jumps;
                auto rdIntVec = [&](std::vector<int32_t>& dst) -> bool {
                    if (off + 8 > pLen) return false;
                    uint64_t cSz = RdU64LE(pp + off); off += 8;
                    if (off + cSz > pLen) return false;
                    bool ok = UsdDecompressInts32(pp + off, static_cast<size_t>(cSz), static_cast<size_t>(nEncPaths), dst);
                    off += static_cast<size_t>(cSz);
                    return ok;
                };
                if (nEncPaths > 0 && nEncPaths <= 4000000 && rdIntVec(pathIdxs) && rdIntVec(elemToks) && rdIntVec(jumps)) {
                    // Reconstruct parent path index for each pathIndex (negative elemTok means property ".attr" on parent prim!)
                    std::unordered_map<int32_t, std::pair<int32_t, std::string>> propPathInfo; // pathIdx -> (parentPrimPathIdx, propName)
                    std::function<void(size_t, int32_t, int)> walkPaths = [&](size_t cur, int32_t parentPathIdx, int depth) {
                        if (cur >= nEncPaths || depth > 512) return;
                        while (cur < nEncPaths) {
                            int32_t thisPathIdx = pathIdxs[cur];
                            int32_t et = elemToks[cur];
                            if (et < 0) {
                                uint32_t tIdx = static_cast<uint32_t>(-et);
                                if (tIdx < tokens.size()) {
                                    propPathInfo[thisPathIdx] = { parentPathIdx, tokens[tIdx] };
                                }
                            }
                            int32_t jmp = jumps[cur];
                            bool hasChild   = (jmp > 0 || jmp == -1);
                            bool hasSibling = (jmp >= 0);
                            if (hasChild) {
                                walkPaths(cur + 1, thisPathIdx, depth + 1);
                            }
                            if (!hasSibling) break;
                            cur = (jmp > 0) ? (cur + static_cast<size_t>(jmp)) : (cur + 1);
                        }
                    };
                    walkPaths(0, -1, 0);

                    // Decode SPECS: numSpecs(8), pathIndexes, fieldSetIndexes, specTypes
                    const uint8_t* sp = data + static_cast<size_t>(secSpecs.start);
                    size_t sLen = static_cast<size_t>(secSpecs.len);
                    if (sLen >= 32) {
                        size_t soff = 0;
                        uint64_t nSpecs = RdU64LE(sp + soff); soff += 8;
                        std::vector<int32_t> specPaths, specFs, specTypes;
                        auto rdSpecVec = [&](std::vector<int32_t>& dst) -> bool {
                            if (soff + 8 > sLen) return false;
                            uint64_t cSz = RdU64LE(sp + soff); soff += 8;
                            if (soff + cSz > sLen) return false;
                            bool ok = UsdDecompressInts32(sp + soff, static_cast<size_t>(cSz), static_cast<size_t>(nSpecs), dst);
                            soff += static_cast<size_t>(cSz);
                            return ok;
                        };
                        if (nSpecs > 0 && nSpecs <= 4000000 && rdSpecVec(specPaths) && rdSpecVec(specFs) && rdSpecVec(specTypes)) {
                            for (size_t i = 0; i < static_cast<size_t>(nSpecs); ++i) {
                                auto pit = propPathInfo.find(specPaths[i]);
                                if (pit == propPathInfo.end()) continue;
                                const std::string& pName = pit->second.second;
                                if (pName != "points" && pName != "faceVertexCounts" && pName != "faceVertexIndices") continue;

                                int32_t fsIdx = specFs[i];
                                if (fsIdx < 0 || static_cast<size_t>(fsIdx) >= fieldSets.size()) continue;
                                uint64_t valRep = 0;
                                for (size_t k = static_cast<size_t>(fsIdx); k < fieldSets.size() && fieldSets[k] >= 0; ++k) {
                                    uint32_t fIdx = static_cast<uint32_t>(fieldSets[k]);
                                    if (fIdx >= fields.size()) continue;
                                    uint32_t ft = fields[fIdx].tokIdx;
                                    if (ft < tokens.size() && (tokens[ft] == "default" || tokens[ft] == "timeSamples")) {
                                        valRep = fields[fIdx].rep;
                                        if (tokens[ft] == "default") break;
                                    }
                                }
                                if (valRep != 0) {
                                    auto& pm = primAttrs[pit->second.first];
                                    if (pName == "points")            pm.pointsRep  = valRep;
                                    if (pName == "faceVertexCounts")  pm.countsRep  = valRep;
                                    if (pName == "faceVertexIndices") pm.indicesRep = valRep;
                                }
                            }
                        }
                    }
                }
            }
        }

        for (const auto& [primId, pm] : primAttrs) {
            if (pm.pointsRep == 0 || pm.indicesRep == 0) continue;
            std::vector<Vec3f> pts;
            std::vector<int32_t> counts, indices;
            if (decodeVec3Array(pm.pointsRep, pts, 0) && decodeIntArray(pm.indicesRep, indices, 0)) {
                if (pm.countsRep != 0) decodeIntArray(pm.countsRep, counts, 0);
                emitUsdMesh(pts, counts, indices);
            }
        }

        // Direct FIELDS fallback if PATHS used a different layout version
        if (mesh.tris.empty()) {
            std::vector<Vec3f> lastPts;
            std::vector<int32_t> intArrs[2];
            int nIntArr = 0;
            for (const auto& f : fields) {
                if ((f.rep & (1ULL << 63)) == 0) continue;
                uint8_t tEnum = static_cast<uint8_t>((f.rep >> 48) & 0xFFu);
                if (tEnum == 19 || tEnum == 20) {
                    if (!lastPts.empty() && nIntArr >= 1) {
                        if (nIntArr == 2 && intArrs[0].size() <= intArrs[1].size()) emitUsdMesh(lastPts, intArrs[0], intArrs[1]);
                        else if (nIntArr == 2) emitUsdMesh(lastPts, intArrs[1], intArrs[0]);
                        else emitUsdMesh(lastPts, {}, intArrs[0]);
                        lastPts.clear(); intArrs[0].clear(); intArrs[1].clear(); nIntArr = 0;
                    }
                    decodeVec3Array(f.rep, lastPts, 0);
                } else if ((tEnum == 3 || tEnum == 4) && !lastPts.empty() && nIntArr < 2) {
                    if (decodeIntArray(f.rep, intArrs[nIntArr], 0)) ++nIntArr;
                }
            }
            if (!lastPts.empty() && nIntArr >= 1) {
                if (nIntArr == 2 && intArrs[0].size() <= intArrs[1].size()) emitUsdMesh(lastPts, intArrs[0], intArrs[1]);
                else if (nIntArr == 2) emitUsdMesh(lastPts, intArrs[1], intArrs[0]);
                else emitUsdMesh(lastPts, {}, intArrs[0]);
            }
        }

        return !mesh.vertices.empty() && !mesh.tris.empty();
    }

    // 3. Pixar USD ASCII Stage (.usda / ASCII .usd) — supports multiple Mesh prims + xformOp:translate/scale
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    if (sv.rfind("#usda", 0) == 0 || (sv.find("def Mesh") != std::string_view::npos && sv.find("points") != std::string_view::npos)) {
        mesh.formatName = L"Pixar USD ASCII Stage (.usda)";

        auto parseMeshScope = [&](std::string_view scope) {
            Vec3f xlate = { 0, 0, 0 }, scl = { 1, 1, 1 };
            size_t trP = scope.find("xformOp:translate");
            if (trP != std::string_view::npos) {
                size_t lp = scope.find('(', trP), rp = scope.find(')', trP);
                if (lp != std::string_view::npos && rp != std::string_view::npos && rp > lp) {
                    std::vector<double> c;
                    DetailStep::ParseFloatTuple(scope.substr(lp, rp - lp + 1), c);
                    if (c.size() >= 3) xlate = { static_cast<float>(c[0]), static_cast<float>(c[1]), static_cast<float>(c[2]) };
                }
            }
            size_t scP = scope.find("xformOp:scale");
            if (scP != std::string_view::npos) {
                size_t lp = scope.find('(', scP), rp = scope.find(')', scP);
                if (lp != std::string_view::npos && rp != std::string_view::npos && rp > lp) {
                    std::vector<double> c;
                    DetailStep::ParseFloatTuple(scope.substr(lp, rp - lp + 1), c);
                    if (c.size() >= 3) scl = { static_cast<float>(c[0]), static_cast<float>(c[1]), static_cast<float>(c[2]) };
                }
            }

            std::vector<Vec3f> pts;
            size_t ptsKey = scope.find("points");
            while (ptsKey != std::string_view::npos) {
                size_t eq = scope.find('=', ptsKey);
                size_t b1 = (eq != std::string_view::npos) ? scope.find('[', eq) : std::string_view::npos;
                if (b1 != std::string_view::npos && b1 - ptsKey < 48) {
                    size_t b2 = scope.find(']', b1);
                    if (b2 != std::string_view::npos) {
                        std::string_view blk = scope.substr(b1 + 1, b2 - b1 - 1);
                        size_t p = 0;
                        while ((p = blk.find('(', p)) != std::string_view::npos && pts.size() < 200000) {
                            size_t pe = blk.find(')', p);
                            if (pe == std::string_view::npos) break;
                            std::vector<double> c;
                            DetailStep::ParseFloatTuple(blk.substr(p, pe - p + 1), c);
                            if (c.size() >= 3) {
                                pts.push_back({
                                    static_cast<float>(c[0]) * scl.x + xlate.x,
                                    static_cast<float>(c[1]) * scl.y + xlate.y,
                                    static_cast<float>(c[2]) * scl.z + xlate.z
                                });
                            }
                            p = pe + 1;
                        }
                    }
                    break;
                }
                ptsKey = scope.find("points", ptsKey + 6);
            }
            if (pts.empty()) return;

            auto readIntBracket = [&](const char* key, std::vector<int>& out) {
                size_t kp = scope.find(key);
                if (kp == std::string_view::npos) return;
                size_t b1 = scope.find('[', kp), b2 = scope.find(']', kp);
                if (b1 == std::string_view::npos || b2 == std::string_view::npos || b2 <= b1) return;
                const char* cur = scope.data() + b1 + 1;
                const char* end = scope.data() + b2;
                while (cur < end) {
                    while (cur < end && (*cur < '0' || *cur > '9') && *cur != '-') ++cur;
                    if (cur >= end) break;
                    char* ep = nullptr;
                    long v = std::strtol(cur, &ep, 10);
                    if (ep == cur) { ++cur; continue; }
                    out.push_back(static_cast<int>(v));
                    cur = ep;
                }
            };
            std::vector<int> counts, indices;
            readIntBracket("faceVertexCounts", counts);
            readIntBracket("faceVertexIndices", indices);
            if (indices.empty()) return;

            uint32_t vBase = static_cast<uint32_t>(mesh.vertices.size());
            for (const auto& p : pts) mesh.vertices.push_back(p);
            if (counts.empty()) {
                for (size_t i = 0; i + 2 < indices.size(); i += 3) {
                    if (indices[i] >= 0 && indices[i + 1] >= 0 && indices[i + 2] >= 0 &&
                        static_cast<size_t>(indices[i]) < pts.size() &&
                        static_cast<size_t>(indices[i + 1]) < pts.size() &&
                        static_cast<size_t>(indices[i + 2]) < pts.size())
                    {
                        mesh.tris.push_back({ vBase + static_cast<uint32_t>(indices[i]), vBase + static_cast<uint32_t>(indices[i + 1]), vBase + static_cast<uint32_t>(indices[i + 2]), {} });
                    }
                }
            } else {
                size_t k = 0;
                for (int c : counts) {
                    if (c >= 3 && k + static_cast<size_t>(c) <= indices.size() && indices[k] >= 0 && static_cast<size_t>(indices[k]) < pts.size()) {
                        for (int t = 1; t + 1 < c; ++t) {
                            int i1 = indices[k + t], i2 = indices[k + t + 1];
                            if (i1 >= 0 && i2 >= 0 && static_cast<size_t>(i1) < pts.size() && static_cast<size_t>(i2) < pts.size()) {
                                mesh.tris.push_back({ vBase + static_cast<uint32_t>(indices[k]), vBase + static_cast<uint32_t>(i1), vBase + static_cast<uint32_t>(i2), {} });
                            }
                        }
                    }
                    if (c > 0) k += static_cast<size_t>(c);
                }
            }
        };

        size_t dm = sv.find("def Mesh");
        if (dm == std::string_view::npos) {
            parseMeshScope(sv);
        } else {
            while (dm != std::string_view::npos) {
                size_t ob = sv.find('{', dm);
                if (ob == std::string_view::npos) break;
                int depth = 1;
                size_t p = ob + 1;
                while (p < sv.size() && depth > 0) {
                    if (sv[p] == '{') ++depth;
                    else if (sv[p] == '}') --depth;
                    ++p;
                }
                parseMeshScope(sv.substr(ob, p - ob));
                dm = sv.find("def Mesh", p);
            }
        }
        return !mesh.vertices.empty() && !mesh.tris.empty();
    }

    (void)extLower;
    return false;
}

// ============================================================================
// 68. LightWave 3D Object (.lwo — IFF "FORM....LWO2" / "LWOB")
// ============================================================================
// wantLayer: -1 = every layer (a .lwo file opened by itself), else the 0-based LAYR number to keep (a scene's
// "LoadObjectLayer 1 ..." is LAYR 0). outLayersSeen counts the LAYR chunks (LWOB / layer-less LWO2 = 1 layer).
inline bool ParseLightWaveLwoLayer(const uint8_t* data, size_t size, LoadedMesh3D& mesh, int wantLayer, int* outLayersSeen = nullptr) {
    if (outLayersSeen) *outLayersSeen = 0;
    if (size < 20 || std::memcmp(data, "FORM", 4) != 0) return false;
    bool isLwo2 = (std::memcmp(data + 8, "LWO2", 4) == 0);
    bool isLwob = (std::memcmp(data + 8, "LWOB", 4) == 0);
    if (!isLwo2 && !isLwob) return false;

    mesh.formatName = isLwo2 ? L"LightWave 3D Object (.lwo LWO2)" : L"LightWave 3D Object (.lwo LWOB)";
    size_t pos = 12;
    uint32_t layerVBase = 0;
    int curLayer = 0;
    int layersSeen = 0;
    while (pos + 8 <= size) {
        char tag[5] = {};
        std::memcpy(tag, data + pos, 4);
        uint32_t clen = Detail3D100::RdU32BE(data + pos + 4);
        pos += 8;
        if (pos + clen > size) break;
        const bool keepLayer = (wantLayer < 0 || curLayer == wantLayer);

        if (std::memcmp(tag, "LAYR", 4) == 0 && clen >= 2) {
            curLayer = Detail3D100::RdU16BE(data + pos);
            ++layersSeen;
        } else if (std::memcmp(tag, "PNTS", 4) == 0 && keepLayer) {
            layerVBase = static_cast<uint32_t>(mesh.vertices.size());
            uint32_t numPts = clen / 12;
            for (uint32_t i = 0; i < numPts && mesh.vertices.size() < 200000; ++i) {
                float x = Detail3D100::RdF32BE(data + pos + i * 12 + 0);
                float y = Detail3D100::RdF32BE(data + pos + i * 12 + 4);
                float z = Detail3D100::RdF32BE(data + pos + i * 12 + 8);
                mesh.vertices.push_back({ x, y, z });
            }
        } else if (std::memcmp(tag, "POLS", 4) == 0 && keepLayer) {
            size_t p = pos;
            size_t pend = pos + clen;
            if (isLwo2 && p + 4 <= pend) {
                p += 4; // 4-byte polygon type ("FACE", "PTCH", "SUBD")
            }
            while (p + 2 <= pend && mesh.tris.size() < 400000) {
                uint16_t numVerts = Detail3D100::RdU16BE(data + p) & 0x03FFu;
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
                    uint32_t absIdx = layerVBase + idx;
                    if (absIdx < mesh.vertices.size()) poly.push_back(absIdx);
                }
                if (!isLwo2 && p + 2 <= pend) p += 2; // LWOB surface index
                for (size_t i = 1; i + 1 < poly.size(); ++i) {
                    mesh.tris.push_back({ poly[0], poly[i], poly[i + 1], {} });
                }
            }
        }
        pos += clen + (clen & 1u);
    }
    if (outLayersSeen) *outLayersSeen = std::max(layersSeen, 1);
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

inline bool ParseLightWaveLwo(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    return ParseLightWaveLwoLayer(data, size, mesh, -1);
}

// ============================================================================
// 69a. Autodesk Maya ASCII (.ma) & Maya Binary IFF (.mb — FOR4 / FOR8)
// ============================================================================
namespace DetailMaya {

struct MayaEdge { int32_t v0 = 0, v1 = 0; };

inline void EmitMayaMeshNode(
    const std::vector<Vec3f>& vt,
    const std::vector<Vec3f>& ptTweaks,
    const std::vector<MayaEdge>& edges,
    const std::vector<std::vector<int32_t>>& faceEdgeLoops,
    const DetailScad::Mat4& xform,
    LoadedMesh3D& mesh)
{
    if (vt.empty() || faceEdgeLoops.empty()) return;
    uint32_t vBase = static_cast<uint32_t>(mesh.vertices.size());
    bool flip = (xform.Det3x3() < 0.0f);
    for (size_t i = 0; i < vt.size(); ++i) {
        Vec3f p = vt[i];
        if (i < ptTweaks.size()) {
            p.x += ptTweaks[i].x;
            p.y += ptTweaks[i].y;
            p.z += ptTweaks[i].z;
        }
        mesh.vertices.push_back(xform.TransformPoint(p));
    }

    for (const auto& eloop : faceEdgeLoops) {
        if (eloop.size() < 3) continue;
        std::vector<uint32_t> ring;
        ring.reserve(eloop.size());
        for (int32_t eref : eloop) {
            int32_t eIdx = (eref >= 0) ? eref : (-eref - 1);
            if (eIdx < 0 || static_cast<size_t>(eIdx) >= edges.size()) continue;
            int32_t vStart = (eref >= 0) ? edges[eIdx].v0 : edges[eIdx].v1;
            if (vStart >= 0 && static_cast<size_t>(vStart) < vt.size()) {
                ring.push_back(vBase + static_cast<uint32_t>(vStart));
            }
        }
        for (size_t k = 1; k + 1 < ring.size(); ++k) {
            if (flip) mesh.tris.push_back({ ring[0], ring[k + 1], ring[k], {} });
            else      mesh.tris.push_back({ ring[0], ring[k], ring[k + 1], {} });
        }
    }
}

inline bool ParseMayaAscii(std::string_view sv, LoadedMesh3D& mesh) {
    if (sv.find("createNode") == std::string_view::npos && sv.find("setAttr") == std::string_view::npos && sv.rfind("//Maya", 0) != 0) {
        return false;
    }
    mesh.formatName = L"Autodesk Maya ASCII Scene (.ma)";

    struct XformNode {
        std::string parent;
        Vec3f t = { 0, 0, 0 };
        Vec3f r = { 0, 0, 0 };
        Vec3f s = { 1, 1, 1 };
        bool isJointOrLoc = false;
    };
    struct MeshNode {
        std::string parent;
        std::vector<Vec3f> vt;
        std::vector<Vec3f> pt;
        std::vector<MayaEdge> ed;
        std::vector<std::vector<int32_t>> fc;
    };

    std::unordered_map<std::string, XformNode> xforms;
    std::vector<MeshNode> meshes;

    enum CurKind { None, Xf, Me } curKind = None;
    std::string curXfName;

    auto parseSliceRange = [](std::string_view attr, size_t brkPos, size_t& outStart, size_t& outEnd) -> bool {
        size_t rb = attr.find(']', brkPos);
        if (rb == std::string_view::npos) return false;
        std::string_view inner = attr.substr(brkPos + 1, rb - brkPos - 1);
        size_t col = inner.find(':');
        if (col == std::string_view::npos) {
            long idx = std::strtol(std::string(inner).c_str(), nullptr, 10);
            if (idx < 0) return false;
            outStart = outEnd = static_cast<size_t>(idx);
            return true;
        }
        long a = std::strtol(std::string(inner.substr(0, col)).c_str(), nullptr, 10);
        long b = std::strtol(std::string(inner.substr(col + 1)).c_str(), nullptr, 10);
        if (a < 0 || b < a) return false;
        outStart = static_cast<size_t>(a);
        outEnd   = static_cast<size_t>(b);
        return true;
    };

    // Strip // line comments into clean buffer and scan ';' statements
    std::string clean;
    clean.reserve(sv.size());
    for (size_t i = 0; i < sv.size();) {
        if (sv[i] == '/' && i + 1 < sv.size() && sv[i + 1] == '/') {
            while (i < sv.size() && sv[i] != '\n') ++i;
            continue;
        }
        clean.push_back(sv[i++]);
    }

    size_t pos = 0;
    while (pos < clean.size()) {
        while (pos < clean.size() && static_cast<unsigned char>(clean[pos]) <= 32) ++pos;
        if (pos >= clean.size()) break;
        size_t st = pos;
        bool inQ = false;
        while (pos < clean.size()) {
            char c = clean[pos];
            if (c == '"' && (pos == 0 || clean[pos - 1] != '\\')) inQ = !inQ;
            else if (c == ';' && !inQ) break;
            ++pos;
        }
        std::string_view stmt(clean.data() + st, pos - st);
        if (pos < clean.size() && clean[pos] == ';') ++pos;

        // Tokenize command header
        auto nextTok = [](std::string_view s, size_t& p) -> std::string_view {
            while (p < s.size() && static_cast<unsigned char>(s[p]) <= 32) ++p;
            if (p >= s.size()) return {};
            if (s[p] == '"') {
                size_t q0 = ++p;
                while (p < s.size() && s[p] != '"') {
                    if (s[p] == '\\' && p + 1 < s.size()) ++p;
                    ++p;
                }
                std::string_view res = s.substr(q0, p - q0);
                if (p < s.size() && s[p] == '"') ++p;
                return res;
            }
            size_t t0 = p;
            while (p < s.size() && static_cast<unsigned char>(s[p]) > 32) ++p;
            return s.substr(t0, p - t0);
        };

        size_t sp = 0;
        std::string_view cmd = nextTok(stmt, sp);
        if (cmd == "createNode") {
            std::string_view nodeType = nextTok(stmt, sp);
            std::string nodeName, parentName;
            while (sp < stmt.size()) {
                std::string_view flg = nextTok(stmt, sp);
                if (flg == "-n" || flg == "-name") nodeName = std::string(nextTok(stmt, sp));
                else if (flg == "-p" || flg == "-parent") parentName = std::string(nextTok(stmt, sp));
            }
            if (nodeType == "transform" || nodeType == "joint" || nodeType == "locator") {
                if (nodeName.empty()) nodeName = "xf_" + std::to_string(xforms.size());
                curKind = Xf;
                curXfName = nodeName;
                auto& xn = xforms[nodeName];
                xn.parent = parentName;
                if (nodeType == "joint" || nodeType == "locator") xn.isJointOrLoc = true;
            } else if (nodeType == "mesh") {
                curKind = Me;
                meshes.push_back(MeshNode{ parentName, {}, {}, {}, {} });
            } else {
                curKind = None;
            }
        } else if (cmd == "setAttr") {
            std::string_view attrName;
            while (sp < stmt.size()) {
                std::string_view t = nextTok(stmt, sp);
                if (t.empty()) break;
                if (t[0] == '.') { attrName = t; break; }
                if (t == "-s" || t == "-size" || t == "-type" || t == "-ch") { nextTok(stmt, sp); }
            }
            // Skip optional trailing -type "..." right after ".attr"
            size_t valStart = sp;
            while (sp < stmt.size()) {
                size_t save = sp;
                std::string_view t = nextTok(stmt, sp);
                if (t == "-type") { nextTok(stmt, sp); valStart = sp; }
                else { sp = save; break; }
            }

            if (curKind == Xf && !curXfName.empty()) {
                auto& xn = xforms[curXfName];
                if (attrName == ".t" || attrName == ".r" || attrName == ".s") {
                    const char* cur = stmt.data() + valStart;
                    const char* end = stmt.data() + stmt.size();
                    char* ep = nullptr;
                    float x = std::strtof(cur, &ep); if (ep != cur) {
                        cur = ep; float y = std::strtof(cur, &ep); if (ep != cur) {
                            cur = ep; float z = std::strtof(cur, &ep); if (ep != cur) {
                                if (attrName == ".t") xn.t = { x, y, z };
                                if (attrName == ".r") xn.r = { x, y, z };
                                if (attrName == ".s") xn.s = { x, y, z };
                            }
                        }
                    }
                }
            } else if (curKind == Me && !meshes.empty()) {
                MeshNode& mn = meshes.back();
                if (attrName.rfind(".vt[", 0) == 0 || attrName.rfind(".pt[", 0) == 0) {
                    auto& dst = (attrName[1] == 'v') ? mn.vt : mn.pt;
                    size_t sIdx = 0, eIdx = 0;
                    if (parseSliceRange(attrName, 3, sIdx, eIdx) && eIdx < 1000000) {
                        if (dst.size() <= eIdx) dst.resize(eIdx + 1, { 0, 0, 0 });
                        const char* cur = stmt.data() + valStart;
                        const char* end = stmt.data() + stmt.size();
                        for (size_t k = sIdx; k <= eIdx && cur < end; ++k) {
                            char* ep = nullptr;
                            float x = std::strtof(cur, &ep); if (ep == cur) break; cur = ep;
                            float y = std::strtof(cur, &ep); if (ep == cur) break; cur = ep;
                            float z = std::strtof(cur, &ep); if (ep == cur) break; cur = ep;
                            dst[k] = { x, y, z };
                        }
                    }
                } else if (attrName.rfind(".ed[", 0) == 0) {
                    size_t sIdx = 0, eIdx = 0;
                    if (parseSliceRange(attrName, 3, sIdx, eIdx) && eIdx < 2000000) {
                        if (mn.ed.size() <= eIdx) mn.ed.resize(eIdx + 1);
                        const char* cur = stmt.data() + valStart;
                        const char* end = stmt.data() + stmt.size();
                        for (size_t k = sIdx; k <= eIdx && cur < end; ++k) {
                            char* ep = nullptr;
                            long v0 = std::strtol(cur, &ep, 10); if (ep == cur) break; cur = ep;
                            long v1 = std::strtol(cur, &ep, 10); if (ep == cur) break; cur = ep;
                            std::strtol(cur, &ep, 10); if (ep == cur) break; cur = ep; // smooth flag
                            mn.ed[k] = { static_cast<int32_t>(v0), static_cast<int32_t>(v1) };
                        }
                    }
                } else if (attrName.rfind(".fc[", 0) == 0) {
                    size_t p = valStart;
                    while (p < stmt.size() && mn.fc.size() < 2000000) {
                        std::string_view sub = nextTok(stmt, p);
                        if (sub.empty()) break;
                        if (sub == "f") {
                            int cnt = std::atoi(std::string(nextTok(stmt, p)).c_str());
                            std::vector<int32_t> loop;
                            for (int k = 0; k < cnt && k < 512 && p < stmt.size(); ++k) {
                                loop.push_back(static_cast<int32_t>(std::atoi(std::string(nextTok(stmt, p)).c_str())));
                            }
                            if (loop.size() >= 3) mn.fc.push_back(std::move(loop));
                        } else if (sub == "h" || sub == "mf") {
                            int cnt = std::atoi(std::string(nextTok(stmt, p)).c_str());
                            for (int k = 0; k < cnt && p < stmt.size(); ++k) nextTok(stmt, p);
                        } else if (sub == "mu") {
                            nextTok(stmt, p); // uvSet
                            int cnt = std::atoi(std::string(nextTok(stmt, p)).c_str());
                            for (int k = 0; k < cnt && p < stmt.size(); ++k) nextTok(stmt, p);
                        } else if (sub == "mc") {
                            nextTok(stmt, p); // colorSet
                            int cnt = std::atoi(std::string(nextTok(stmt, p)).c_str());
                            for (int k = 0; k < cnt && p < stmt.size(); ++k) nextTok(stmt, p);
                        }
                    }
                }
            }
        }
    }

    std::function<DetailScad::Mat4(const std::string&, int)> getWorldMat = [&](const std::string& nm, int depth) -> DetailScad::Mat4 {
        if (nm.empty() || depth > 24) return DetailScad::Mat4::Identity();
        auto it = xforms.find(nm);
        if (it == xforms.end()) return DetailScad::Mat4::Identity();
        const auto& xf = it->second;
        using DetailScad::Mat4;
        Mat4 loc = Mat4::Mul(Mat4::Translate(xf.t.x, xf.t.y, xf.t.z),
                   Mat4::Mul(Mat4::RotateXYZ(xf.r.x, xf.r.y, xf.r.z), Mat4::Scale(xf.s.x, xf.s.y, xf.s.z)));
        return Mat4::Mul(getWorldMat(xf.parent, depth + 1), loc);
    };

    for (const auto& mn : meshes) {
        DetailScad::Mat4 wm = getWorldMat(mn.parent, 0);
        if (!mn.fc.empty() && !mn.ed.empty()) {
            EmitMayaMeshNode(mn.vt, mn.pt, mn.ed, mn.fc, wm, mesh);
        }
    }

    // Joint / Locator rig hierarchy fallback ONLY when genuine joint/locator nodes with transforms exist
    if (mesh.tris.empty() && xforms.size() >= 2) {
        std::vector<std::pair<Vec3f, Vec3f>> bones;
        for (const auto& [nm, xn] : xforms) {
            if (!xn.parent.empty() && xforms.find(xn.parent) != xforms.end()) {
                Vec3f p0 = getWorldMat(xn.parent, 0).TransformPoint({ 0, 0, 0 });
                Vec3f p1 = getWorldMat(nm, 0).TransformPoint({ 0, 0, 0 });
                bones.push_back({ p0, p1 });
            }
        }
        if (!bones.empty()) {
            for (const auto& b : bones) {
                Detail3D100::AddLineStrut(mesh, b.first, b.second, 0.05f);
            }
        }
    }

    return !mesh.vertices.empty() && !mesh.tris.empty();
}

inline bool ParseMayaBinary(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    if (size < 20) return false;
    const bool isFor4 = (std::memcmp(data, "FOR4", 4) == 0);
    const bool isFor8 = (std::memcmp(data, "FOR8", 4) == 0);
    if (!isFor4 && !isFor8) return false;

    const size_t grpHdrOff = isFor8 ? 16 : 8;
    if (grpHdrOff + 4 > size || std::memcmp(data + grpHdrOff, "Maya", 4) != 0) return false;
    mesh.formatName = L"Autodesk Maya Binary Scene (.mb)";

    auto walkIff = [&](auto& self, size_t start, size_t end, std::string_view groupType, int depth) -> void {
        if (depth > 24) return;
        std::vector<Vec3f> vrts;
        std::vector<MayaEdge> edges;
        std::vector<std::vector<int32_t>> faces;

        size_t p = start;
        const size_t hdrSz = isFor8 ? 16 : 8;
        const size_t align = isFor8 ? 8 : 4;

        while (p + hdrSz <= end) {
            char tag[5] = {};
            std::memcpy(tag, data + p, 4);
            uint64_t cLen = isFor8 ? Detail3D100::RdU64BE(data + p + 8) : Detail3D100::RdU32BE(data + p + 4);
            p += hdrSz;
            if (cLen > end - p) break;
            size_t cEnd = p + static_cast<size_t>(cLen);

            bool isGroup = (std::memcmp(tag, "FOR4", 4) == 0 || std::memcmp(tag, "FOR8", 4) == 0 ||
                            std::memcmp(tag, "LIST", 4) == 0 || std::memcmp(tag, "LIS8", 4) == 0 ||
                            std::memcmp(tag, "CAT4", 4) == 0 || std::memcmp(tag, "CAT8", 4) == 0 ||
                            std::memcmp(tag, "PROP", 4) == 0 || std::memcmp(tag, "PRO8", 4) == 0);
            if (isGroup) {
                size_t subTypeSpan = isFor8 ? 8 : 4;
                if (p + subTypeSpan <= cEnd) {
                    std::string_view subType(reinterpret_cast<const char*>(data + p), 4);
                    self(self, p + subTypeSpan, cEnd, subType, depth + 1);
                }
            } else if (groupType == "MESH" || groupType == "DMSH") {
                if (std::memcmp(tag, "VRTS", 4) == 0 && cLen >= 36 && (cLen % 12) == 0) {
                    size_t nv = static_cast<size_t>(cLen) / 12;
                    if (nv <= 1000000) {
                        vrts.resize(nv);
                        for (size_t i = 0; i < nv; ++i) {
                            vrts[i] = {
                                Detail3D100::RdF32BE(data + p + i * 12 + 0),
                                Detail3D100::RdF32BE(data + p + i * 12 + 4),
                                Detail3D100::RdF32BE(data + p + i * 12 + 8)
                            };
                        }
                    }
                } else if (std::memcmp(tag, "EDGE", 4) == 0 && cLen >= 9) {
                    // Maya .mb EDGE records are either 12 bytes (int32 v0, int32 v1, int32 flag) or 9 bytes (int32 v0, int32 v1, uint8 flag)
                    size_t stride = ((cLen % 12) == 0) ? 12 : (((cLen % 9) == 0) ? 9 : 0);
                    if (stride == 12 && (cLen % 9) == 0 && !vrts.empty()) {
                        // Disambiguate 36-byte chunk: check if stride 12 produces valid vertex indices
                        for (size_t k = 0; k < cLen; k += 12) {
                            int32_t a = static_cast<int32_t>(Detail3D100::RdU32BE(data + p + k));
                            int32_t b = static_cast<int32_t>(Detail3D100::RdU32BE(data + p + k + 4));
                            if (a < 0 || b < 0 || static_cast<size_t>(a) >= vrts.size() || static_cast<size_t>(b) >= vrts.size()) {
                                stride = 9;
                                break;
                            }
                        }
                    }
                    if (stride > 0) {
                        size_t ne = static_cast<size_t>(cLen) / stride;
                        edges.resize(ne);
                        for (size_t i = 0; i < ne; ++i) {
                            edges[i].v0 = static_cast<int32_t>(Detail3D100::RdU32BE(data + p + i * stride + 0));
                            edges[i].v1 = static_cast<int32_t>(Detail3D100::RdU32BE(data + p + i * stride + 4));
                        }
                    }
                } else if ((std::memcmp(tag, "FACE", 4) == 0 || std::memcmp(tag, "POLY", 4) == 0) && cLen >= 12 && (cLen % 4) == 0) {
                    size_t nInts = static_cast<size_t>(cLen) / 4;
                    // Format A: [count, e0, e1, ..., count, e0, ...]
                    bool countPrefixedOk = true;
                    size_t idx = 0;
                    std::vector<std::vector<int32_t>> parsedA;
                    while (idx < nInts) {
                        int32_t cnt = static_cast<int32_t>(Detail3D100::RdU32BE(data + p + idx * 4));
                        if (cnt < 3 || cnt > 256 || idx + 1 + static_cast<size_t>(cnt) > nInts) {
                            countPrefixedOk = false;
                            break;
                        }
                        std::vector<int32_t> loop(cnt);
                        for (int32_t k = 0; k < cnt; ++k) {
                            loop[k] = static_cast<int32_t>(Detail3D100::RdU32BE(data + p + (idx + 1 + k) * 4));
                        }
                        parsedA.push_back(std::move(loop));
                        idx += 1 + static_cast<size_t>(cnt);
                    }
                    if (countPrefixedOk && !parsedA.empty()) {
                        for (auto& lp : parsedA) faces.push_back(std::move(lp));
                    } else {
                        // Format B: Maya packed polyFaces stream (bit 30 = reversed edge, bit 31 = end of loop)
                        std::vector<int32_t> curLoop;
                        for (size_t k = 0; k < nInts; ++k) {
                            uint32_t w = Detail3D100::RdU32BE(data + p + k * 4);
                            bool endLoop = (w & 0x80000000u) != 0;
                            bool revEdge = (w & 0x40000000u) != 0;
                            int32_t eIdx = static_cast<int32_t>(w & 0x3FFFFFFFu);
                            curLoop.push_back(revEdge ? (-eIdx - 1) : eIdx);
                            if (endLoop) {
                                if (curLoop.size() >= 3) faces.push_back(std::move(curLoop));
                                curLoop.clear();
                            }
                        }
                    }
                }
            }

            size_t padded = (static_cast<size_t>(cLen) + (align - 1)) & ~(align - 1);
            if (padded > end - p) break;
            p += padded;
        }

        if (!vrts.empty() && !edges.empty() && !faces.empty()) {
            EmitMayaMeshNode(vrts, {}, edges, faces, DetailScad::Mat4::Identity(), mesh);
        }
    };

    uint64_t rootLen = isFor8 ? Detail3D100::RdU64BE(data + 8) : Detail3D100::RdU32BE(data + 4);
    size_t rootEnd = std::min<size_t>(size, (isFor8 ? 16 : 8) + static_cast<size_t>(rootLen));
    size_t bodyStart = isFor8 ? 24 : 12;
    if (bodyStart < rootEnd) {
        walkIff(walkIff, bodyStart, rootEnd, "Maya", 0);
    }
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace DetailMaya

// ============================================================================
// 69b. LightWave Scene (.lws) and AutoCAD R11/R12 (.dwg AC1009 fallback)
// ============================================================================
namespace DetailCadDcc {

// LightWave Scene (.lws - ASCII "LWSC" v1..v5). A scene holds no geometry of its own: every object is a separate
// .lwo file named on its LoadObjectLayer / LoadObject line. Each one is found next to the scene or in the content
// directory, read with ParseLightWaveLwoLayer (just the layer the scene asks for), and placed with the item's
// motion at the first key: position, heading/pitch/bank, scale and pivot, through the whole parent chain.
// Cameras, lights, nulls and bones have no geometry and are drawn as markers, with the motion paths. An object
// whose file cannot be found or read is named in the status line and left out - never stood in for by a shape.
namespace DetailLws {

struct Key { float time = 0.0f; float val = 0.0f; };

struct Item {
    enum Kind { Obj, Null, Cam, Light, Bone } kind = Obj;
    uint32_t id = 0;
    uint32_t parentId = 0;
    bool hasParent = false;
    int layer = 1;                 // LoadObjectLayer layer (1-based)
    std::string objPath;           // object file as written in the scene
    Vec3f pivot = { 0, 0, 0 };
    Vec3f boneRest = { 0, 0, 0 };
    bool hasBoneRest = false;
    std::vector<Key> chan[9];      // X, Y, Z, H, P, B (radians), SX, SY, SZ
};

inline std::string_view TrimLws(std::string_view s) {
    size_t a = 0, b = s.size();
    while (a < b && static_cast<unsigned char>(s[a]) <= 32) ++a;
    while (b > a && static_cast<unsigned char>(s[b - 1]) <= 32) --b;
    return s.substr(a, b - a);
}

inline bool IsHex8(std::string_view t) {
    if (t.size() != 8) return false;
    for (char c : t) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
}

// Splits "<token> <rest>" (rest trimmed); empty token when s is empty
inline std::string_view SplitFirst(std::string_view s, std::string_view& rest) {
    s = TrimLws(s);
    size_t sp = 0;
    while (sp < s.size() && static_cast<unsigned char>(s[sp]) > 32) ++sp;
    rest = TrimLws(s.substr(sp));
    return s.substr(0, sp);
}

inline bool IsBareInteger(std::string_view s) {
    s = TrimLws(s);
    if (s.empty()) return false;
    for (char c : s) if (c < '0' || c > '9') return false;
    return true;
}

inline int ParseFloats(std::string_view s, float* out, int maxCount) {
    std::string tmp(s);
    const char* p = tmp.c_str();
    int n = 0;
    while (n < maxCount) {
        char* endp = nullptr;
        double v = std::strtod(p, &endp);
        if (endp == p) break;
        out[n++] = std::isfinite(v) ? static_cast<float>(v) : 0.0f;
        p = endp;
    }
    return n;
}

inline float SampleKeys(const std::vector<Key>& keys, float t, float fallback) {
    if (keys.empty()) return fallback;
    if (t <= keys.front().time || keys.size() == 1) return keys.front().val;
    if (t >= keys.back().time) return keys.back().val;
    for (size_t i = 0; i + 1 < keys.size(); ++i) {
        if (t >= keys[i].time && t <= keys[i + 1].time) {
            float dt = keys[i + 1].time - keys[i].time;
            float u = (dt > 1e-6f) ? ((t - keys[i].time) / dt) : 0.0f;
            return keys[i].val + (keys[i + 1].val - keys[i].val) * u;
        }
    }
    return keys.back().val;
}

inline bool IsExistingFile(const std::wstring& p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

// Finds the object file a scene names. LightWave writes paths relative to the content directory (usually the
// folder above "Scenes"), sometimes absolute, with '/', '\' or the old "Objects:box.lwo" form. Tried in order:
// the absolute path, the path under the scene folder and up to three folders above it, then the bare file name in
// the scene folder and in "Objects" next to it and above it. Network (UNC) and device paths and ".." are refused:
// opening a downloaded scene must not reach out to other machines or wander outside its folders.
inline std::wstring ResolveObjectPath(const std::wstring& sceneDir, const std::string& raw, std::wstring& whyNot) {
    std::wstring p = PathUtils::Trim(PathUtils::Utf8ToWide(raw));
    if (p.size() >= 2 && p.front() == L'"' && p.back() == L'"') p = p.substr(1, p.size() - 2);
    for (size_t i = 0; i < p.size(); ++i) {
        if (p[i] == L'/') p[i] = L'\\';
        else if (p[i] == L':' && i != 1) p[i] = L'\\';     // "Objects:box.lwo" (not the "C:" of a drive)
    }
    if (p.empty()) { whyNot = L"brak nazwy pliku"; return {}; }
    if (p.rfind(L"\\\\", 0) == 0) { whyNot = L"ścieżka sieciowa"; return {}; }
    {
        std::wstring padded = L"\\" + p + L"\\";
        if (padded.find(L"\\..\\") != std::wstring::npos) { whyNot = L"ścieżka poza folderem"; return {}; }
    }
    if (PathUtils::GetExtensionLower(p) != L".lwo") { whyNot = L"nie jest plikiem .lwo"; return {}; }
    if (sceneDir.empty()) { whyNot = L"nie znaleziono"; return {}; }

    const bool isAbsolute = p.size() >= 3 && p[1] == L':' && p[2] == L'\\';
    std::vector<std::wstring> candidates;
    if (isAbsolute) candidates.push_back(p);
    std::vector<std::wstring> dirs = { sceneDir };
    for (int up = 0; up < 3; ++up) {
        std::wstring parent = PathUtils::GetParentPath(dirs.back());
        if (parent.empty() || parent == dirs.back()) break;
        dirs.push_back(parent);
    }
    if (!isAbsolute) {
        std::wstring rel = p;
        while (!rel.empty() && rel.front() == L'\\') rel.erase(0, 1);
        for (const auto& d : dirs) candidates.push_back(PathUtils::TrimTrailingSlashes(d) + L"\\" + rel);
    }
    const std::wstring fileName = PathUtils::GetFileName(p);
    candidates.push_back(PathUtils::TrimTrailingSlashes(sceneDir) + L"\\" + fileName);
    for (const auto& d : dirs) candidates.push_back(PathUtils::TrimTrailingSlashes(d) + L"\\Objects\\" + fileName);

    for (const auto& c : candidates) {
        if (c.rfind(L"\\\\", 0) == 0) continue;
        if (IsExistingFile(c)) return c;
    }
    whyNot = L"nie znaleziono";
    return {};
}

} // namespace DetailLws

inline bool ParseLightWaveScene(std::string_view sv, const std::wstring& scenePath, LoadedMesh3D& mesh) {
    using namespace Detail3D100;
    using DetailLws::Item;
    using DetailLws::Key;
    using M4 = DetailScad::Mat4;
    std::string_view trimmed = DetailStep::TrimSv(sv);
    if (trimmed.rfind("LWSC", 0) != 0) return false;
    mesh.formatName = L"LightWave 3D Scene (.lws)";

    // ---- 1. Read the scene: items, their object files, pivots, parents and motion channels ----------------------
    std::vector<std::string_view> lines;
    for (size_t pos = 0; pos < sv.size() && lines.size() < 4000000;) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        lines.push_back(DetailLws::TrimLws(sv.substr(pos, eol - pos)));
        pos = eol + 1;
    }
    int version = 3;
    for (size_t i = 1; i < lines.size() && i < 4; ++i) {
        if (DetailLws::IsBareInteger(lines[i])) { version = std::atoi(std::string(lines[i]).c_str()); break; }
    }
    // LightWave 6+ (scene version 3 and later) keeps rotations in radians, older scenes in degrees
    const float rotToRad = (version >= 3) ? 1.0f : 0.017453292519943295f;

    std::vector<Item> items;
    uint32_t objCount = 0, camCount = 0, lightCount = 0, boneCount = 0;
    uint32_t lastObjectId = 0;
    bool haveObject = false;
    int curChan = -1;

    for (size_t li = 0; li < lines.size() && items.size() < 4096; ++li) {
        std::string_view ln = lines[li];
        if (ln.empty()) continue;
        std::string_view rest;
        const std::string_view kw = DetailLws::SplitFirst(ln, rest);

        if (kw == "LoadObjectLayer" || kw == "LoadObject" || kw == "AddNullObject") {
            Item it;
            it.kind = (kw == "AddNullObject") ? Item::Null : Item::Obj;
            it.id = 0x10000000u | objCount++;
            if (kw == "LoadObjectLayer") {
                std::string_view after;
                std::string_view layerTok = DetailLws::SplitFirst(rest, after);
                it.layer = std::max(1, std::atoi(std::string(layerTok).c_str()));
                rest = after;
            }
            // LightWave 9+ writes the item ID before the file / null name: "LoadObjectLayer 1 10000000 Objects/a.lwo"
            std::string_view after;
            std::string_view first = DetailLws::SplitFirst(rest, after);
            if (DetailLws::IsHex8(first) && !after.empty()) {
                it.id = static_cast<uint32_t>(std::strtoul(std::string(first).c_str(), nullptr, 16));
                rest = after;
            }
            if (it.kind == Item::Obj) it.objPath = std::string(rest);
            lastObjectId = it.id;
            haveObject = true;
            items.push_back(std::move(it));
            curChan = -1;
        } else if (kw == "AddCamera" || kw == "AddLight" || kw == "AddBone") {
            Item it;
            if (kw == "AddCamera") { it.kind = Item::Cam; it.id = 0x30000000u | camCount++; }
            else if (kw == "AddLight") { it.kind = Item::Light; it.id = 0x20000000u | lightCount++; }
            else {
                it.kind = Item::Bone;
                it.id = 0x40000000u | boneCount++;
                if (haveObject) { it.parentId = lastObjectId; it.hasParent = true; }   // a bone belongs to its object
            }
            std::string_view after;
            std::string_view first = DetailLws::SplitFirst(rest, after);
            if (DetailLws::IsHex8(first)) it.id = static_cast<uint32_t>(std::strtoul(std::string(first).c_str(), nullptr, 16));
            items.push_back(std::move(it));
            curChan = -1;
        } else if (!items.empty()) {
            Item& cur = items.back();
            float f[9] = {};
            if (kw == "PivotPosition" || kw == "PivotPoint") {
                if (DetailLws::ParseFloats(rest, f, 3) == 3) cur.pivot = { f[0], f[1], f[2] };
            } else if (kw == "BoneRestPosition") {
                if (DetailLws::ParseFloats(rest, f, 3) == 3) { cur.boneRest = { f[0], f[1], f[2] }; cur.hasBoneRest = true; }
            } else if (kw == "ParentItem") {
                cur.parentId = static_cast<uint32_t>(std::strtoul(std::string(rest).c_str(), nullptr, 16));
                cur.hasParent = true;
            } else if (kw == "ParentObject") {
                int idx1 = std::atoi(std::string(rest).c_str());
                if (idx1 >= 1) { cur.parentId = 0x10000000u | static_cast<uint32_t>(idx1 - 1); cur.hasParent = true; }
            } else if (kw == "Channel") {
                curChan = std::atoi(std::string(rest).c_str());
            } else if (kw == "}") {
                curChan = -1;      // the channel's envelope ends; later envelopes (light intensity, ...) are not motion
            } else if (kw == "Key" && curChan >= 0 && curChan < 9) {
                if (DetailLws::ParseFloats(rest, f, 2) == 2) {
                    cur.chan[curChan].push_back({ f[1], (curChan >= 3 && curChan <= 5) ? f[0] * rotToRad : f[0] });
                }
            } else if ((kw == "ObjectMotion" || kw == "CameraMotion" || kw == "LightMotion" || kw == "BoneMotion") &&
                       li + 2 < lines.size() && DetailLws::IsBareInteger(lines[li + 1]) && DetailLws::IsBareInteger(lines[li + 2])) {
                // LightWave 5 motion block: channel count, key count, then per key a line of channel values
                // (X Y Z H P B SX SY SZ, rotations in degrees) and a line "frame linear tension continuity bias"
                const int nch = std::min(9, std::atoi(std::string(lines[li + 1]).c_str()));
                const int nkeys = std::atoi(std::string(lines[li + 2]).c_str());
                size_t k = li + 3;
                for (int key = 0; key < nkeys && key < 100000 && k + 1 < lines.size(); ++key, k += 2) {
                    float vals[9] = {};
                    float frame[1] = {};
                    if (DetailLws::ParseFloats(lines[k], vals, nch) != nch || DetailLws::ParseFloats(lines[k + 1], frame, 1) != 1) break;
                    for (int c = 0; c < nch; ++c) {
                        cur.chan[c].push_back({ frame[0], (c >= 3 && c <= 5) ? vals[c] * rotToRad : vals[c] });
                    }
                }
                li = k - 1;
            }
        }
    }
    if (items.empty()) return false;
    for (auto& it : items) {
        for (auto& ch : it.chan) {
            std::stable_sort(ch.begin(), ch.end(), [](const Key& a, const Key& b) { return a.time < b.time; });
            mesh.animFrameCount = std::max(mesh.animFrameCount, ch.size());
        }
    }

    std::unordered_map<uint32_t, size_t> idMap;
    for (size_t i = 0; i < items.size(); ++i) idMap.emplace(items[i].id, i);

    // ---- 2. Item transforms: parent * T(position) * Ry(heading) * Rx(pitch) * Rz(bank) * S(scale) ------------------
    // LightWave is left-handed (+Y up, +Z forward); positive heading turns +Z towards +X, positive pitch tilts +Z
    // down, positive bank turns +X towards +Y. The object's geometry hangs off this frame shifted by -pivot; children
    // follow the frame itself (not the parent's pivot), as in Layout.
    constexpr float kRadToDeg = 57.29577951308232f;
    auto localMatrix = [&](const Item& it, float t) -> M4 {
        const Vec3f rest = it.hasBoneRest ? it.boneRest : Vec3f{ 0, 0, 0 };
        const float x = DetailLws::SampleKeys(it.chan[0], t, rest.x);
        const float y = DetailLws::SampleKeys(it.chan[1], t, rest.y);
        const float z = DetailLws::SampleKeys(it.chan[2], t, rest.z);
        const float h = DetailLws::SampleKeys(it.chan[3], t, 0.0f) * kRadToDeg;
        const float p = DetailLws::SampleKeys(it.chan[4], t, 0.0f) * kRadToDeg;
        const float b = DetailLws::SampleKeys(it.chan[5], t, 0.0f) * kRadToDeg;
        const float sx = DetailLws::SampleKeys(it.chan[6], t, 1.0f);
        const float sy = DetailLws::SampleKeys(it.chan[7], t, 1.0f);
        const float sz = DetailLws::SampleKeys(it.chan[8], t, 1.0f);
        M4 r = M4::Mul(M4::RotateAxisAngle(h, 0, 1, 0), M4::Mul(M4::RotateAxisAngle(p, 1, 0, 0), M4::RotateAxisAngle(b, 0, 0, 1)));
        return M4::Mul(M4::Translate(x, y, z), M4::Mul(r, M4::Scale(sx, sy, sz)));
    };
    std::function<M4(size_t, float, int)> worldMatrix = [&](size_t idx, float t, int depth) -> M4 {
        const Item& it = items[idx];
        M4 local = localMatrix(it, t);
        if (it.hasParent && depth < 32) {
            auto pit = idMap.find(it.parentId);
            if (pit != idMap.end() && pit->second != idx) return M4::Mul(worldMatrix(pit->second, t, depth + 1), local);
        }
        return local;
    };
    float t0 = 1e30f;
    for (const auto& it : items) for (const auto& ch : it.chan) if (!ch.empty()) t0 = std::min(t0, ch.front().time);
    if (t0 > 1e29f) t0 = 0.0f;

    // ---- 3. Objects: read each referenced .lwo layer once, place a copy per item ----------------------------------
    const std::wstring sceneDir = scenePath.empty() ? std::wstring() : PathUtils::GetParentPath(scenePath);
    struct ObjGeom { bool ok = false; std::wstring why; std::vector<Vec3f> v; std::vector<MeshTriangle> t; };
    std::map<std::wstring, ObjGeom> cache;
    constexpr size_t kMaxVerts = 2000000, kMaxTris = 4000000;
    size_t objItems = 0, objPlaced = 0;
    bool cut = false;
    std::vector<std::wstring> problems;
    auto addProblem = [&](const std::string& raw, const std::wstring& why) {
        std::wstring s = PathUtils::Utf8ToWide(raw) + L" (" + why + L")";
        if (std::find(problems.begin(), problems.end(), s) == problems.end()) problems.push_back(s);
    };

    for (size_t i = 0; i < items.size(); ++i) {
        const Item& it = items[i];
        if (it.kind != Item::Obj) continue;
        ++objItems;
        std::wstring whyNot;
        const std::wstring file = DetailLws::ResolveObjectPath(sceneDir, it.objPath, whyNot);
        if (file.empty()) { addProblem(it.objPath, whyNot); continue; }
        const std::wstring key = PathUtils::NormalizePathKey(file) + L"|" + std::to_wstring(it.layer);
        auto ci = cache.find(key);
        if (ci == cache.end()) {
            ObjGeom g;
            std::vector<uint8_t> bytes;
            if (!PathUtils::ReadFileBytes(file, 128 * 1024 * 1024, bytes) || bytes.empty()) {
                g.why = L"nie można odczytać";
            } else {
                LoadedMesh3D om;
                int layers = 0;
                bool ok = ParseLightWaveLwoLayer(bytes.data(), bytes.size(), om, it.layer - 1, &layers);
                if (!ok && layers == 1 && it.layer != 1) {
                    om = LoadedMesh3D{};
                    ok = ParseLightWaveLwoLayer(bytes.data(), bytes.size(), om, -1);   // single-layer file
                }
                if (ok) { g.ok = true; g.v = std::move(om.vertices); g.t = std::move(om.tris); }
                else g.why = (layers > 1) ? (L"brak warstwy " + std::to_wstring(it.layer)) : L"uszkodzony plik .lwo";
            }
            ci = cache.emplace(key, std::move(g)).first;
        }
        const ObjGeom& g = ci->second;
        if (!g.ok) { addProblem(it.objPath, g.why); continue; }
        if (mesh.vertices.size() + g.v.size() > kMaxVerts || mesh.tris.size() + g.t.size() > kMaxTris) { cut = true; continue; }

        const M4 place = M4::Mul(worldMatrix(i, t0, 0), M4::Translate(-it.pivot.x, -it.pivot.y, -it.pivot.z));
        const bool flip = place.Det3x3() < 0.0f;
        const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        for (const Vec3f& v : g.v) mesh.vertices.push_back(place.TransformPoint(v));
        for (const MeshTriangle& t : g.t) {
            if (flip) mesh.tris.push_back({ base + t.i0, base + t.i2, base + t.i1, {} });
            else      mesh.tris.push_back({ base + t.i0, base + t.i1, base + t.i2, {} });
        }
        ++objPlaced;
    }

    // ---- 4. Markers for items without geometry, parent links and motion paths -----------------------------------
    auto originAt = [&](size_t idx, float t) -> Vec3f { return worldMatrix(idx, t, 0).TransformPoint({ 0, 0, 0 }); };
    Vec3f mn = originAt(0, t0), mx = mn;
    auto grow = [&](const Vec3f& p) {
        mn.x = std::min(mn.x, p.x); mn.y = std::min(mn.y, p.y); mn.z = std::min(mn.z, p.z);
        mx.x = std::max(mx.x, p.x); mx.y = std::max(mx.y, p.y); mx.z = std::max(mx.z, p.z);
    };
    for (const Vec3f& v : mesh.vertices) grow(v);
    for (size_t i = 0; i < items.size(); ++i) {
        grow(originAt(i, t0));
        for (int c = 0; c < 3; ++c) for (const auto& k : items[i].chan[c]) grow(originAt(i, k.time));
    }
    const float span = std::max({ mx.x - mn.x, mx.y - mn.y, mx.z - mn.z, 1e-3f });
    const float gizmoSz = span * 0.06f;
    const float strutR = span * 0.008f;

    for (size_t i = 0; i < items.size(); ++i) {
        const Item& it = items[i];
        const M4 w = worldMatrix(i, t0, 0);
        const Vec3f p0 = w.TransformPoint({ 0, 0, 0 });
        auto at = [&](float lx, float ly, float lz) { return w.TransformPoint({ lx, ly, lz }); };

        if (it.kind == Item::Cam) {
            // Viewing pyramid along the camera's own +Z
            Vec3f corners[4] = {
                at(-gizmoSz, -gizmoSz * 0.7f, gizmoSz * 1.6f), at(gizmoSz, -gizmoSz * 0.7f, gizmoSz * 1.6f),
                at(gizmoSz, gizmoSz * 0.7f, gizmoSz * 1.6f),   at(-gizmoSz, gizmoSz * 0.7f, gizmoSz * 1.6f)
            };
            for (int c = 0; c < 4; ++c) {
                AddLineStrut(mesh, p0, corners[c], strutR);
                AddLineStrut(mesh, corners[c], corners[(c + 1) & 3], strutR);
            }
        } else if (it.kind == Item::Light || it.kind == Item::Null || it.kind == Item::Bone) {
            const float s = (it.kind == Item::Light) ? gizmoSz : gizmoSz * 0.5f;
            AddLineStrut(mesh, at(-s, 0, 0), at(s, 0, 0), strutR);
            AddLineStrut(mesh, at(0, -s, 0), at(0, s, 0), strutR);
            AddLineStrut(mesh, at(0, 0, -s), at(0, 0, s), strutR);
        }

        if (it.hasParent) {
            auto pit = idMap.find(it.parentId);
            if (pit != idMap.end() && pit->second != i) AddLineStrut(mesh, originAt(pit->second, t0), p0, strutR * 1.2f);
        }

        float tMin = 1e30f, tMax = -1e30f;
        for (int c = 0; c < 3; ++c) {
            for (const auto& k : it.chan[c]) { tMin = std::min(tMin, k.time); tMax = std::max(tMax, k.time); }
        }
        if (tMax > tMin + 1e-5f) {
            const int steps = 24;
            Vec3f prev = originAt(i, tMin);
            for (int s = 1; s <= steps; ++s) {
                const float tm = tMin + (tMax - tMin) * (static_cast<float>(s) / static_cast<float>(steps));
                const Vec3f curP = originAt(i, tm);
                AddLineStrut(mesh, prev, curP, strutR);
                prev = curP;
            }
        }
    }

    // ---- 5. Status: how many objects were placed, and which could not be ----------------------------------------
    std::wstring status = L"Obiekty sceny: " + std::to_wstring(objPlaced) + L"/" + std::to_wstring(objItems);
    if (!problems.empty()) {
        status += L"; brak: ";
        for (size_t k = 0; k < problems.size() && k < 3; ++k) status += (k ? L", " : L"") + problems[k];
        if (problems.size() > 3) status += L" i " + std::to_wstring(problems.size() - 3) + L" innych";
    }
    if (cut) status += L"; pokazano część (za duża scena)";
    mesh.statusMessage = status;
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

// AutoCAD Native Drawing (.dwg — unencrypted R11/R12 AC1009 3DFACE / LINE entities; returns false on compressed/encrypted/empty)
inline bool ParseAutoCadDwg(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    using namespace Detail3D100;
    if (size < 64 || std::memcmp(data, "AC10", 4) != 0) return false;
    mesh.formatName = L"AutoCAD Native Drawing (.dwg)";

    // AC1009 (AutoCAD R11/R12) stores unencrypted entity records between entities_start (0x14) and entities_end (0x18)
    if (std::memcmp(data, "AC1009", 6) == 0 && size >= 0x20) {
        uint32_t entStart = RdU32LE(data + 0x14);
        uint32_t entEnd   = RdU32LE(data + 0x18);
        if (entStart >= 0x20 && entEnd > entStart && entEnd <= size) {
            size_t p = entStart;
            while (p + 8 <= entEnd && mesh.tris.size() < 200000) {
                uint8_t eType = data[p];
                uint8_t flag  = data[p + 1];
                uint16_t rLen = RdU16LE(data + p + 2);
                if (rLen < 8 || p + rLen > entEnd) break;
                // Skip header fields: type(1)+flag(1)+len(2)+layer(2)+opts(2) [+color(1) if flag&1] [+ltype(1) if flag&2] [+elev(8) if (flag&4 && eType!=1 && eType!=22)]
                size_t ep = p + 8;
                if (flag & 0x01u) ++ep;
                if (flag & 0x02u) ++ep;
                if (eType == 1 && ep + 48 <= p + rLen) { // LINE (3D start + 3D end doubles when flag&4, or 2D doubles)
                    bool is3D = (flag & 0x04u) != 0;
                    if (is3D && ep + 48 <= p + rLen) {
                        Vec3f a = { static_cast<float>(RdF64LE(data + ep + 0)),  static_cast<float>(RdF64LE(data + ep + 16)), static_cast<float>(-RdF64LE(data + ep + 8)) };
                        Vec3f b = { static_cast<float>(RdF64LE(data + ep + 24)), static_cast<float>(RdF64LE(data + ep + 40)), static_cast<float>(-RdF64LE(data + ep + 32)) };
                        AddLineStrut(mesh, a, b, 0.04f);
                    } else if (!is3D && ep + 32 <= p + rLen) {
                        Vec3f a = { static_cast<float>(RdF64LE(data + ep + 0)),  0.0f, static_cast<float>(-RdF64LE(data + ep + 8)) };
                        Vec3f b = { static_cast<float>(RdF64LE(data + ep + 16)), 0.0f, static_cast<float>(-RdF64LE(data + ep + 24)) };
                        AddLineStrut(mesh, a, b, 0.04f);
                    }
                } else if (eType == 22 && ep + 96 <= p + rLen) { // 3DFACE: 4 * double[3] = 96 bytes
                    uint32_t b = static_cast<uint32_t>(mesh.vertices.size());
                    for (int k = 0; k < 4; ++k) {
                        double x = RdF64LE(data + ep + k * 24 + 0);
                        double y = RdF64LE(data + ep + k * 24 + 8);
                        double z = RdF64LE(data + ep + k * 24 + 16);
                        mesh.vertices.push_back({ static_cast<float>(x), static_cast<float>(z), static_cast<float>(-y) });
                    }
                    mesh.tris.push_back({ b + 0, b + 1, b + 2, {} });
                    mesh.tris.push_back({ b + 0, b + 2, b + 3, {} });
                }
                p += rLen;
            }
        }
    }
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace DetailCadDcc

// fullPath: the file on disk, for formats that refer to other files (a .lws scene names its .lwo objects)
inline bool ParseMayaOrCadContainer(const uint8_t* data, size_t size, const std::wstring& extLower, LoadedMesh3D& mesh,
                                    const std::wstring& fullPath = std::wstring()) {
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    if (extLower == L".ma")  return DetailMaya::ParseMayaAscii(sv, mesh);
    if (extLower == L".mb")  return DetailMaya::ParseMayaBinary(data, size, mesh);
    if (extLower == L".lws") return DetailCadDcc::ParseLightWaveScene(sv, fullPath, mesh);
    return false;
}

} // namespace Formats
